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
- Renaming a directory returns `ENOTSUP`.
- Permissions and timestamps are not stored. `chmod`-style calls return `EROFS`, and `mkdir` ignores the mode.
- Symlinks cannot be created in the overlay.
- The layer trusts its inputs. Paths and the overlay location are not validated here, as required for the strict IO layer.

## Decisions to confirm
- macOS and Windows defaults follow platform convention, not the Linux path. Confirm.
- Stem collisions (two archives with the same stem) share an overlay directory. Decide whether to add a hash suffix.
- Overlay directory rename support and permission storage are not implemented.
