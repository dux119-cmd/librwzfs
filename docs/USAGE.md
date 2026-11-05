# Using rwzfs

rwzfs gives an application the normal file API for a read-only dwarfs archive, with writes
kept in a separate overlay. The overlay can later be flattened into a new archive revision.

This guide covers the standard flow: create an archive, mount it with an overlay, use the
wrappers, and create a revision with the overlay's changes. The complete program is
`examples/usage.cpp`; its output is shown below.

## Build

    cmake -S . -B build -DDWARFS_PREFIX=/usr/local
    cmake --build build
    ctest --test-dir build --output-on-failure

`mkdwarfs` must be in `DWARFS_PREFIX/bin` or on `PATH`. Without dwarfs, configure with
`-DRWZFS_WITH_DWARFS=OFF`: paths, host IO, overlay logic, creation and revision still build;
mounting returns `MountError::OpenFailed`.

## 1. Create an archive

```cpp
#include "rwzfs/archive/create.hpp"

rwzfs::create_options create;
create.compression = "zstd:level=12";   // default: "zstd:level=12:wlog=27:strat=3"
auto ok = rwzfs::create_archive("source/", "project.dwarfs", create);
if (!ok) { /* create_error: source_missing, tool_not_found, tool_failed, io, ... */ }
```

- The archive is written to `project.dwarfs.tmp` and renamed on success. A failure leaves no
  partial archive.
- The default compression needs a zstd that exposes `wlog` and `strat`. If the build rejects it,
  `tool_failed` is returned and mkdwarfs's message is on stderr.

## 2. Mount with an overlay

```cpp
#include "rwzfs/fs/archive_path.hpp"
#include "rwzfs/mount.hpp"

rwzfs::mount_options options;
options.write_root = "/var/tmp/myapp";   // optional; must be absolute
const auto root = rwzfs::fs::archive_root().string();   // "/rwz" (POSIX) or "C:/RWZ" (Windows)
auto result = rwzfs::mount_archive("project.dwarfs", root, options);
```

- Mounting creates `<write_root>/project-writes/` (the stem is the archive name up to its first
  dot). This is the overlay. Without `write_root`, the default user state directory is used:
  `$XDG_STATE_HOME/rwz` (if absolute), else `~/.local/state/rwz` on Linux and BSD;
  `~/Library/Application Support/rwz` on macOS; `%LOCALAPPDATA%\rwz` on Windows.
- A lock file, `.rwz.lock`, is created in the overlay directory. While it exists the archive is
  refused with `MountError::ArchiveInUse`: only one instance may use an archive. If a previous run
  crashed, delete `.rwz.lock` after confirming no other instance is running.
- Relative paths resolve into the archive once it is mounted. `DIR/hello.txt` is the same as
  `/rwz/DIR/hello.txt`.

`MountError`: `ArchiveNotFound`, `MountPointInvalid`, `OpenFailed`, `WriteRootInvalid`,
`WriteRootUnavailable`, `ArchiveInUse`.

## 3. Use the wrappers

Call the rwzfs wrappers in place of the standard calls. With no archive mounted they behave like
the standard calls. Archive paths are answered from the merged view (archive plus overlay).

```cpp
#include "rwzfs/io/posix.hpp"
#include "rwzfs/io/stdio.hpp"
#include "rwzfs/fs/filesystem.hpp"

int fd = rwzfs::io::posix::open("/rwz/DIR/new.txt", O_WRONLY | O_CREAT, 0644);
rwzfs::io::posix::write(fd, "data", 4);
rwzfs::io::posix::close(fd);

rwzfs::io::posix::unlink("/rwz/readme.txt");   // hidden by a whiteout; the archive is untouched

std::error_code ec;
bool exists = rwzfs::fs::exists("/rwz/DIR/hello.txt", ec);
```

Behaviour on archive paths:

- **Reads** see the merged view. Overlay files override archive files, and whiteouts hide deleted
  entries.
- **Writes** go to the overlay. An archive file is copied up on its first write (not on open). An
  open for writing with `O_TRUNC`, or a file that is created, is handled immediately.
- **Renames** of files and directories, **unlink**, **mkdir**, **rmdir**, **truncate**,
  **chmod**, **utimensat** (modification time only) and `fs::permissions` /
  `fs::last_write_time` all work. Metadata changes apply to files; directories refuse them with
  `EPERM`.
- **stdio**: `fopen` with `w`, `a` or `+` copies the file up at open, not on the first write. Read
  streams of archive files are in-memory copies, so they must be closed with
  `rwzfs::io::stdio::fclose`.
- **Not supported on archive paths:** creating symlinks, `ioctl`, `mmap` of archive descriptors,
  `glob` with `GLOB_APPEND`/`GLOB_BRACE`/`GLOB_TILDE`, and Win32 handles to archive paths
  (`ERROR_NOT_SUPPORTED`).

Host paths pass through to the operating system unchanged, and host symlinks are followed.

## 4. Create a revision

A revision flattens the merged view (overlay wins, whiteouts and renames applied) into a new
archive, then clears the overlay.

```cpp
#include "rwzfs/revise.hpp"

auto revised = rwzfs::revise_archive(root, create);
if (revised) {
  // *revised is "project-rev1.dwarfs", next to the archive.
}
```

What happens:

1. The flattened tree is written to a scratch directory next to the archive.
2. `project-rev1.dwarfs` is created from it with the same compression options.
3. The old mount is unmounted (its lock is released) and its overlay's `data/`, `whiteouts/`
   and `opaque/` are removed.

Revising a revision bumps the number: `project-rev1.dwarfs` becomes `project-rev2.dwarfs`.

`revise_error`: `mount_not_found` (nothing mounted at `root`), `no_changes` (empty overlay),
`flatten_failed`, `create_failed`, `io`. Nothing is cleared unless the new archive was created.

Mount the revision to continue:

```cpp
rwzfs::mount_options fresh;
fresh.write_root = "/var/tmp/myapp-rev1";
rwzfs::mount_archive(*revised, root, fresh);
```

## Example output

The output of `examples/usage.cpp` on Linux (mkdwarfs's progress lines are omitted):

```
created "/tmp/rwex/project.dwarfs"
mounted at /rwz
read  DIR/hello.txt: hello from the archive
read  DIR/new.txt:   written through the overlay
revision "/tmp/rwex/project-rev1.dwarfs"
rev1 DIR/new.txt:    written through the overlay
rev1 readme.txt:     <missing>
ok
```

## Limits to know about

- Only one process may use an archive at a time (the lock). Delete a stale `.rwz.lock` by hand.
- Overlay directories with the same archive stem in the same write root share one overlay.
- Directory permissions are not stored; metadata changes on directories return `EPERM`.
- Access times are not stored; modification times are.
- Symlinks cannot be created or renamed in the overlay.
- Creation and revision run the mkdwarfs command-line tool. They are POSIX only for now.
- Revision copies every file of the merged view, so its disk use is at least the merged size.
- Windows: archive access through Win32 handles is not implemented; the overlay and mounting work
  through the POSIX-style wrappers and `std::filesystem`.
