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
| Linux, BSD | `$HOME/.local/state/rwz` |
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

## Not yet implemented: the copy-on-write engine
Mounting creates the overlay directory, but no write is redirected yet. Writes on archive paths still return `EROFS`. The engine needs:
- Copy-up: on the first write to an archive file, copy it into the overlay at the same relative path.
- Read-through: reads check the overlay first, then the archive.
- Create, rename, delete within the archive: stored in the overlay, with whiteout markers for deletes of archive entries. Whiteout format to be decided.
- Directory listings merge the archive and the overlay, minus whiteouts.
- Sizes and modes: the overlay copy is authoritative once it exists.

## Decisions to confirm
- Extension stripping uses the first dot (`\..*`). The alternative is stripping only the last extension.
- macOS and Windows defaults follow platform convention, not the Linux path.
- No `XDG_STATE_HOME` override. Add it if wanted.
