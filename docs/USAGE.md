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

Archives are written with the dwarfs writer library, so no `mkdwarfs` binary is needed. Without dwarfs, configure with
`-DRWZFS_WITH_DWARFS=OFF`: paths, host IO, overlay logic, creation and revision still build;
mounting returns `MountError::OpenFailed`.

## Namespaces

`#include "rwzfs/rwz.h"` brings in the library. The I/O wrappers and filesystem functions live in
flat namespaces: `rwz::posix::open(...)` (POSIX), `rwz::stdio::fopen(...)`, `rwz::fs::exists(...)`, and
on Windows `rwz::win32::create_file(...)`. The archive, overlay, mount and creator types stay in
`rwz::` (`rwz::create_archive`, `rwz::mount_archive`, `rwz::revise_archive`). The internal
namespaces (`rwz::detail::archive_io`, and so on) are not public API.

## 1. Create an archive

```cpp
#include "rwzfs/archive/create.h"

rwz::create_options create;
create.compression = "zstd:level=22";   // default: the dwarfs level-7 profile, "zstd:level=22"
auto ok = rwz::create_archive("source/", "project.dwarfs", create);
if (!ok) { /* create_error: source_missing, tool_not_found, tool_failed, io, ... */ }
```

- The archive is written to `project.dwarfs.tmp` and renamed on success. A failure leaves no
  partial archive.
- The default is the dwarfs level-7 profile with a 64 KiB block size. Other compression settings are unverified with the writer. If a build rejects a setting,
  `tool_failed` is returned and the reason is on the diagnostic stream.

## 2. Mount with an overlay

```cpp
#include "rwzfs/fs/archive_path.h"
#include "rwzfs/mount.h"

rwz::mount_options options;
options.write_root = "/var/tmp/myapp";   // optional; a relative path is resolved against the current directory
const auto root = rwz::fs::archive_root().string();   // "/rwz" (POSIX) or "C:/RWZ" (Windows)
auto result = rwz::mount_archive("project.dwarfs", root, options);
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
#include "rwzfs/io/posix.h"
#include "rwzfs/io/stdio.h"
#include "rwzfs/fs/filesystem.h"

int fd = rwz::posix::open("/rwz/DIR/new.txt", O_WRONLY | O_CREAT, 0644);
rwz::posix::write(fd, "data", 4);
rwz::posix::close(fd);

rwz::posix::unlink("/rwz/readme.txt");   // hidden by a whiteout; the archive is untouched

std::error_code ec;
bool exists = rwz::fs::exists("/rwz/DIR/hello.txt", ec);
```

Behaviour on archive paths:

- **Reads** see the merged view. Overlay files override archive files, and whiteouts hide deleted
  entries.
- **Writes** go to the overlay. An archive file is copied up on its first write (not on open). An
  open for writing with `O_TRUNC`, or a file that is created, is handled immediately.
- **Renames** of files and directories, **unlink**, **mkdir**, **rmdir**, **truncate**,
  **chmod**, **utimensat** (modification time only) and `fs::permissions` /
  `fs::last_write_time` all work. Permission changes on a directory copy its whole tree into the overlay; modification times apply to files only.
- **stdio**: `fopen` with `r+`, `a` or `+` copies the file up on the first write (at open on Windows). Read-only archive files read chunks on demand on glibc, macOS and FreeBSD; on Windows they are copied to a temporary file that `fclose` removes. Close them with `rwz::stdio::fclose`.
- **C++ streams**: `rwz::stdio::file_istream` and `file_ostream` (also returned by `input_file_stream` and `output_file_stream`) are `std::istream` and `std::ostream` subclasses over the same wrappers, so archive and overlay paths work with them.
- **Not supported on archive paths:** creating symlinks, `ioctl` beyond the forms listed in the
  header, `mmap` of archive descriptors, and `glob` with `GLOB_APPEND`/`GLOB_BRACE`/`GLOB_TILDE`.
- **Windows:** `CreateFileW` opens archive files read-only (a table handle; use `win32::read_file`,
  `win32::set_file_pointer` and `win32::close_handle` on it), archive directories with
  `FILE_FLAG_BACKUP_SEMANTICS` (reads fail, as on Windows), and overlay paths with real handles. Writes
  copy up into the overlay. `FindFirstFileW`, `FindNextFileW` and `FindClose` list the merged view.
  `DeleteFileW` and `RemoveDirectoryW` write whiteouts in the overlay. Windows `stdio` cannot read
  archive files (`ENOTSUP`).

Host paths pass through to the operating system unchanged, and host symlinks are followed.

## 4. Create a revision

A revision flattens the merged view (overlay wins, whiteouts and renames applied) into a new
archive, then clears the overlay.

```cpp
#include "rwzfs/revise.h"

auto revised = rwz::revise_archive(root, create);
if (revised) {
  // *revised is "project-rev1.dwarfs", next to the archive.
}
```

What happens:

1. The new archive is written straight from the mounted merged view (see section 5). No scratch
   directory is made.
2. The old mount is unmounted (its lock is released), and its overlay's `data/`, `whiteouts/`
   and `opaque/` are removed.

`revise_error`: `mount_not_found` (nothing mounted at `root`), `no_changes` (empty overlay),
`create_failed` (the view could not be written; `flatten_failed` is no longer produced). Nothing is
cleared unless the new archive was created.

Mount the revision to continue:

```cpp
rwz::mount_options fresh;
fresh.write_root = "/var/tmp/myapp-rev1";
rwz::mount_archive(*revised, root, fresh);
```

## 5. Create an archive from the merged view

`rwz::create_archive_from_view(root, output, options)` writes an archive of the merged view
(archive plus overlay) mounted at `root`. It reads contents and directory structure through the
library's own calls, so no temporary tree is made and only the output is written.

```cpp
auto made = rwz::create_archive_from_view(rwz::fs::archive_root(), "view.dwarfs", create);
```

- The output is written to `view.dwarfs.tmp` and renamed on success.
- `create_error::source_missing` if nothing is mounted at `root`. `tool_failed` if the writer fails
  (the temporary output is removed). `unsupported_platform` only in a build without the dwarfs writer.
- Preserved: names, directory structure, file contents, symlinks (through the merged view).
- Preserved: permissions and modification times (whole seconds). Owners are not stored at all (see section 5).

## The showcase (examples/usage.cpp)

`rwzfs_example <work directory>` runs the whole library end to end and prints side-by-side transcripts:

1. The same read script runs on a host tree (pass-through), then on the mounted archive built from that tree.
2. A write script on the mounted archive: overlay writes, C++ streams, a delete, a permission change, and a new directory.
3. The read script again, showing the merged view with the changes.
4. The archive is revised, the revision is mounted, and the read script runs on it.

The read script calls the same functions for every root, so the transcripts can be compared line by line. Each row is marked `=` when identical and `!` when not. The program exits nonzero if a comparison differs.

It runs as a CTest test (`rwzfs_example`) and as its own CI step on Linux, macOS and Windows.

## Limits to know about

- Only one process may use an archive at a time (the lock). Delete a stale `.rwz.lock` by hand.
- Overlay directories with the same archive stem in the same write root share one overlay.
- A directory permission change copies its whole tree into the overlay.
- Access times are not stored; modification times are.
- Symlinks cannot be created or renamed in the overlay.
- Creation and revision are in-process on Linux, macOS and Windows. Windows creation and revision are tested by the Windows round-trip test (`tests/archive/create_view_windows_test.cpp`).
- Revision reads each file once from the merged view into the new archive.

### Throwing forms of rwz::fs

`rwz::fs::exists`, `create_directories`, `is_regular_file`, `is_symlink`, `equivalent`, `absolute`,
`current_path`, `resize_file` and the `directory_iterator` constructor have the same signatures as their
`std::filesystem` counterparts when called without an `std::error_code`. A path outside every mounted
archive goes straight to `std::filesystem`, so its exceptions come out unchanged. A path inside a mounted
archive view throws `std::filesystem::filesystem_error` with the same operation name and path, so code
that already handles `std::filesystem` needs no change. (The directory iterator's constructor always takes
this second route, since its storage is shared with the archive listing.)

## Command-line tool (rwz-tool)

`rwz-tool` (built with dwarfs, from `examples/rwz_tool.cpp`) runs the three common operations without writing code:

    rwz-tool open   <archive> [--overlay-root DIR]     # mount, then a shell on the merged view
    rwz-tool revise <archive> [--overlay-root DIR]     # report the overlay's changes, then write a revision
    rwz-tool create <name> <source-dir> [--out-dir DIR] [--compression SPEC]

- `open` mounts the archive (and its overlay) at the archive root and runs a shell with `ls`, `cat`, `stat`,
  `put`, `rm`, `mkdir`. Changes go to the overlay, as they do in a program.
- `revise` mounts the archive with its overlay, counts what the overlay changes (files written, deleted,
  directories recreated), and writes `<stem>-revN.dwarfs` beside the original from the merged view. Nothing is
  written if the overlay is empty. The original archive is not changed.
- `create` archives a directory as `<out-dir>/<name>.dwarfs` (`.dwarfs` is added if missing). It refuses to
  overwrite an existing file.

`--overlay-root` sets the write root; without it the library's default is used.

## The high-level interface (`rwzfs/session.h`)

For most programs, three calls cover the common work. They are in `rwz::`:

    // Mount an archive for a scope; the destructor unmounts it.
    auto game = rwz::session::open("Game (USA).dwarfs", {.overlay_root = save_dir});
    if (!game) { report(rwz::to_string(game.error())); return; }
    std::FILE* rom = rwz::stdio::fopen((game->root() / "Game.nes").string().c_str(), "rb");

    // What the overlay changed, and a revision that folds those changes into a new archive.
    rwz::overlay_changes c = game->changes();          // c.written, c.deleted, c.new_dirs, c.empty()
    auto revised = game->revise();                     // Game (USA)-rev1.dwarfs, original untouched

    // Create an archive from a game directory. A bare name goes to the current directory.
    auto made = rwz::archive_directory("Game (USA)", "/path/to/game/dir");

`session::open` returns `std::expected<session, MountError>`. `session` is movable, not copyable, and it
unmounts on destruction or on `close()`. `archive_directory` never overwrites an existing file
(`create_error::already_exists`). The lower-level calls in `mount.h`, `revise.h` and `archive/create.h`
remain available.

`rwz-tool` is a thin front end over this interface.
