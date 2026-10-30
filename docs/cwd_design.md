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
5. `change_directory` to an archive path returns `not_supported` until archive lookups can verify a directory.
6. `set_archive_cwd` only accepts absolute paths inside a mount. It is the seam used by tests and by the later verified enter path.

## Limitation
With an archive cwd set, `..` is resolved lexically. If a host symlink sits on the path, the result can differ from the OS. This affects only paths that resolve through a host symlink while an archive cwd is active.

## Identifiers
| ID | Wrapper | Notes |
|---|---|---|
| CWD-01 | `fs::current_path()` | Getter (was FS-14). Returns the archive cwd when set. |
| CWD-02 | `fs::current_path(p)` | Setter. New overload. |
| CWD-03 | `io::posix::chdir` | New. Not in the call list. |
| CWD-04 | `io::posix::getcwd` | Was POSIX-19. Returns the archive cwd when set. |
| CWD-05 | `fs::resolve` | Internal. Used by every wrapper. |

## Errors
- Archive target not yet supported: `std::errc::not_supported` (`ENOTSUP` at the POSIX layer).
- `getcwd` with an archive cwd and a too-small buffer: `ERANGE`.
- `getcwd` with an archive cwd and a null buffer: `EINVAL`. Allocation is not implemented.
