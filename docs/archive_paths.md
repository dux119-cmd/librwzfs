# Archive path convention

Applies to every path string that names an entry inside a mounted archive.

## Form
1. Separator is `/` on every platform. Never `\`.
2. Paths are relative to the archive root. A leading `/` is stripped, so `/a/b` and `a/b` name the same entry.
3. No `.` components, no empty components, no trailing `/`. The root itself is the empty string `""`.
4. `..` components are resolved lexically before lookup. Resolution never consults the host filesystem and never follows symlinks in the host.
5. Absolute paths clamp at the root, matching the OS and C++23: `/../x` and `/a/../../x` resolve to `x` (the root, with the leading `/` stripped). This is the behavior of `realpath`, `open`, and `std::filesystem::path::lexically_normal()`.
6. Relative paths that climb above the root are an error. Both the OS and `lexically_normal()` resolve them against a directory outside the archive, which has no archive meaning. `a/../../x` is rejected.

## Normalization function
`normalize_archive_path(text) -> result<std::string, error>`

- Input is a string, not a `std::filesystem::path`, so the host separator rules never apply.
- Output is always the generic form above.
- Lexical only: no `canonical`, no `realpath`, no `exists`.
- Clamping of absolute paths is done the same way `lexically_normal()` does it. Only relative escapes are rejected, since `lexically_normal()` leaves them as a leading `..`.

Why not `std::filesystem`: `lexically_normal()` resolves `..` without storage access. It matches the OS for absolute paths, but keeps a leading `..` in relative paths (`a/../../x` becomes `../x`), leaves trailing separators (`a/b/..` becomes `a/`), and on Windows emits `\`. `canonical()` needs the path to exist and follows host symlinks, so it cannot be used for archive entries.

## Examples
| Input | Result |
|---|---|
| `a/b/../c` | `a/c` |
| `/a/./b//c/` | `a/b/c` |
| `a/b/..` | `a` |
| `.` | `""` |
| `/../x` | `x` (clamped at root, as the OS does) |
| `/a/../../../x` | `x` (clamped at root) |
| `a/../../x` | error: relative path escapes archive root |

## Interaction with the path wrappers
PATH-01 to PATH-14 are pure and unaffected by the archive. Any caller that needs an archive-relative path must go through `normalize_archive_path`. Passing a raw `fs::path` into an archive lookup is not allowed.

## Test requirements
- Each example row above.
- Backslash input on Windows is treated as a separator and normalized.
- Names with spaces, Unicode, and leading dots (`.hidden`) are preserved.
- A path built with the path wrappers and then normalized resolves to the same entry as the hand-written form.

## Magic archive root
Archive paths use one fixed absolute prefix: `/rwz` on POSIX, `C:/RWZ` on Windows.
- `archive_root()` returns the prefix.
- `to_archive_absolute("DIR/FILE")` gives `/rwz/DIR/FILE`. `""` gives `/rwz`.
- `to_archive_relative("/rwz/DIR/FILE")` gives `DIR/FILE`. Anything outside the prefix, such as `/rwzx/y`, gives nothing.
- Mountpoints must be at or under the prefix. A mountpoint is virtual, so the host filesystem is not checked for it.
- While an archive is mounted, host paths under the prefix route to the archive.
- Relative paths under an archive working directory expand to `/rwz/...` before any wrapper sees them, so an absolute archive path can always be told apart from a host path.

## Path resolution vs. normalization
- `normalize_archive_path` (PATH-15) normalizes archive-relative text and rejects relative escapes. Use it for archive-relative text only.
- `walk_lexically` resolves paths for the wrappers. Inside the archive, `..` is lexical. Once a path leaves the archive, the rest is host-owned and host symlinks are followed.

## Mount points: the archive's own path

A session mounts an archive at its own path, without the extension: `/a/b/c.dwarfs` mounts at `/a/b/c`. The archive's
contents are then the directory `c`, and they shadow any host directory of that name. Everything outside `c` is the
host, unchanged.

- Relative paths resolve from the working directory, which sits at the mount root once the archive is mounted.
  `../log.txt` from `/a/b/c` is the host file `/a/b/log.txt`.
- A `..` that climbs out of the mount continues from the real parent, which may be the host or another mount. A `..`
  from the host that re-enters a mount (`/a/b/x/../c/file`) resolves into the archive.
- Symbolic links inside the archive are followed within the archive, as before.
- An archive without an extension is refused (it would mount over itself), and a mount at the filesystem root is
  refused.

The archive root `/rwz` (`C:/RWZ` on Windows) still works as a mount point for code that uses it directly.
