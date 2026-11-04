# Overlay (copy-on-write) storage

## Public API (`include/rwzfs/mount.hpp`, `include/rwzfs/overlay.hpp`)
```cpp
rwzfs::mount_options opts;
opts.write_root = "/path/to/writable/top";   // optional, must be absolute
rwzfs::mount_archive("/data/archive.zip", "/rwz", opts);
```
- `mount_archive(archive)` and `mount_archive(archive, mountpoint)` use default options.
- `write_root` is the top-level writable directory. The library creates `<write_root>/<stem>-writes` at mount time.
- Without `write_root`, the default user state area is used:

| Platform | Top-level directory |
|---|---|
| Linux, BSD | `$XDG_STATE_HOME/rwz` if `XDG_STATE_HOME` is an absolute path, otherwise `$HOME/.local/state/rwz` |
| macOS | `$HOME/Library/Application Support/rwz` |
| Windows | `%LOCALAPPDATA%\rwz` |

## Naming
- `<stem>` is the archive file name with everything from its first dot onward removed. A leading dot is kept.
  - `archive.zip` -> `archive`, `fixture.dwarfs` -> `fixture`, `my.data.dwarfs` -> `my`, `.hidden.dwarfs` -> `.hidden`.
- The overlay directory is `<top-level>/<stem>-writes`.
- Two archives with the same stem in the same top-level directory share an overlay. This is the user's choice, but it is a collision risk.

## Errors
| Condition | `MountError` |
|---|---|
| `write_root` is relative | `WriteRootInvalid` |
| Overlay directory cannot be created (for example, a parent is a file) | `WriteRootUnavailable` |
| No `HOME` (or `LOCALAPPDATA` on Windows) and no `write_root` | `WriteRootUnavailable` |

## Mount table
Each mount records its overlay directory. `route::overlay` exposes it to the wrappers.

## Copy-on-write engine (implemented)
Code: `include/rwzfs/overlay/cow.hpp`, `src/overlay/cow.cpp`. POSIX and stdio dispatch: `src/io/overlay_posix.cpp`, `src/io/stdio.cpp`.

Overlay layout:
- `data/<rel>`: files and directories created or copied up from the archive.
- `whiteouts/<rel>`: marker file. The archive entry `<rel>` is deleted.
- `opaque/<rel>`: marker file. Directory `<rel>` was recreated over a deleted archive directory, so its archive children are hidden.

Behavior:
- Reads check `data/` first, then the archive. Whiteouts and opaque markers hide archive entries below them.
- The first write to an archive file copies its bytes into `data/` (copy-up). Truncating writes start from an empty file.
- Writes return host file descriptors and streams on `data/`, so host `read`, `write`, `lseek`, and `fsync` work unchanged.
- Create, unlink, rmdir, mkdir, truncate, rename (files), and recursive remove and create work on the merged view.
- Directory listings merge archive and overlay children, minus whiteouts. Overlay entries override archive entries with the same name.
- Routes without an overlay (for example, mounts made without a write root) stay read-only and return `EROFS`.
- The archive is never modified.

Limits:
- Renaming a directory moves the whole tree: each file is copied up and renamed, then the emptied source is removed. Large trees copy in full. Moving a directory into its own subtree fails with `EINVAL`.
- Symlinks cannot be renamed (`ENOTSUP`).
- Directory permissions are not stored: `mkdir` ignores the mode, and metadata changes on directories return `EPERM`.
- Access times are not stored; only modification times are.
- Symlinks cannot be created in the overlay.
- The layer trusts its inputs. Paths and the overlay location are not validated here, as required for the strict IO layer.

## Locking
- Mounting creates `.rwz.lock` in the overlay directory, containing the owner's process id. Creation uses `O_EXCL`.
- If the lock file already exists, the archive is refused with `MountError::ArchiveInUse`. Only one instance is allowed. The error message tells the user to delete the lock file if no other instance is running. There is no automatic takeover: a crashed run leaves the file for the user to remove.
- `unmount_archive(mountpoint)` and `mounts::clear()` remove the lock. An `atexit` hook removes held locks at normal exit.
- `rwzfs::overlay::lock::enable_signal_cleanup()` (opt-in) removes held locks on SIGINT, SIGTERM and SIGHUP, then re-raises. SIGKILL cannot be handled; the next mount takes over the stale lock.
- Windows locking is not implemented yet.

## Metadata (permissions and timestamps)
- `chmod`, `utimensat` (modification time; access time is not stored), `fs::permissions` and `fs::last_write_time` (setter) copy an archive file up first, then apply the change to the overlay copy. Later stat calls read the copy.
- Directories refuse metadata changes with `EPERM`. Only files are copied.
- Archive mode and mtime are visible through stat once they are in the overlay.

## Deferred copy-up
- Opening an archive-only file for writing, without `O_TRUNC` and without `O_EXCL`, does not copy it up. The descriptor reads from the archive until its first write. Then the whole file is copied up in one step.
- Truncating opens and creation copy up immediately, since they change content.
- Reads through a deferred descriptor switch to the overlay copy if another descriptor copied the file up.
- stdio write modes (`fopen` with `w`, `a`, `+`) copy up at open. Deferring it would need a hook on every `FILE*` write, which means `fopencookie`/`funopen`. Those differ across glibc, BSD/macOS, and Windows has neither, so this stays a documented limitation.

## Decisions to confirm
- macOS and Windows defaults follow platform convention, not the Linux path. Confirm.
- Stem collisions (two archives with the same stem) share an overlay directory. Decide whether to add a hash suffix.
- Overlay directory rename support and permission storage are not implemented.
