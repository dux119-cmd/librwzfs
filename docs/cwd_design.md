# Working directory design

The application may move its working directory into an archive. Relative paths and `./` listings must then resolve inside the archive.

## State
- One process-wide optional: the archive working directory. `nullopt` means the host working directory applies.
- Protected by a mutex, because applications call the wrappers directly.

## Rules
1. Absolute paths are never changed by the cwd state.
2. With no archive cwd, relative paths pass through unchanged and the host resolves them.
3. With an archive cwd, a relative path becomes `(archive_cwd / p).lexically_normal()`. `..` can climb out of the archive, and the result is then a host path.
4. `change_directory` to a host path calls the host `chdir` and clears the archive cwd.
5. `change_directory` to an archive path verifies that it is a directory in the archive, then enters it. A file or missing path fails with `ENOTDIR` or `ENOENT`.
6. `set_archive_cwd` only accepts absolute paths inside a mount. It is the seam used by tests and by the later verified enter path.

## Mounting sets the archive cwd
Registering a mountpoint sets the archive cwd to it, unless one is already set. Mounting at `/rwz` makes `DIR/FILE.DAT` resolve to `/rwz/DIR/FILE.DAT`. `mounts::clear()` also clears the archive cwd. Trade-off: after a mount, relative paths go to the archive until the application changes to a host directory.

## Archive root prefix
Archive paths are absolute and start with the magic root `/rwz` (`C:/RWZ` on Windows). An archive working directory is always stored in that form, so relative expansion produces `/rwz/...` and never a host-looking path. `set_archive_cwd("/rwz")` gets to the archive root.

## Decided: `..` from the archive root
`/rwz/..` leaves the archive and resolves to the host path `/`. The rest of the path is host-owned. `PATH-15` (`normalize_archive_path`) is a different function: it normalizes archive-relative text and rejects escapes, and it is not used for path resolution.

## Decided: host symlinks are followed
Inside the archive, `..` is applied lexically. Once a path leaves the archive, or is a host path, `..` is passed to the OS unchanged, so host symlinks are followed. See `walk_lexically` in `include/rwzfs/fs/walk.hpp`.


## Identifiers
| ID | Wrapper | Notes |
|---|---|---|
| CWD-01 | `fs::current_path()` | Getter (was FS-14). Returns the archive cwd when set. |
| CWD-02 | `fs::current_path(p)` | Setter. New overload. |
| CWD-03 | `io::posix::chdir` | New. Not in the call list. Verifies archive directories. |
| CWD-04 | `io::posix::getcwd` | Was POSIX-19. Returns the archive cwd when set. |
| CWD-05 | `fs::resolve` | Internal. Used by every wrapper. |

## Errors
- Archive target not yet supported: `std::errc::not_supported` (`ENOTSUP` at the POSIX layer).
- `getcwd` with an archive cwd and a too-small buffer: `ERANGE`.
- `getcwd` with an archive cwd and a null buffer: `EINVAL`. Allocation is not implemented.
