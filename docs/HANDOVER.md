# rwzfs handover

## Goal
A cross-platform (Linux, macOS, BSD, Windows) C++23 library. Applications call its IO wrappers in place of the standard calls and get:
1. Passthrough to the host, always. With no archive mounted, every wrapper behaves like the standard call it replaces.
2. Optional read access to a dwarfs archive, and later write overlays, through the same wrappers.

## Key design choices
- **Magic archive root.** Archive paths are absolute and start with `/rwz` (POSIX) or `C:/RWZ` (Windows). Relative paths under an archive working directory expand to this form, so an archive path can never be mistaken for a host path. See `docs/archive_paths.md`.
- **Routing is lexical.** A path is an archive path if, after `lexically_normal()`, it is at or under a registered mountpoint. The check never touches storage. Mountpoints must be under the root. See `include/rwzfs/fs/mount_table.hpp`.
- **Logical working directory.** One optional archive cwd, consulted by every wrapper through `resolve()`. `chdir` into the host clears it. See `docs/cwd_design.md`.
- **Path normalization.** Archive text is normalized by our own rules (forward slashes, `..` resolved lexically, absolute paths clamp at the root, relative escapes rejected). `std::filesystem` is not used for this, because `canonical()` needs the host and `lexically_normal()` keeps leading `..`. See `docs/archive_paths.md`.
- **Error reporting.** `std::error_code&` in the `fs` wrappers, `errno` and `-1` in the POSIX wrappers. Archive targets that are not yet implemented report `not_supported` / `ENOTSUP`. Nothing throws across the wrapper boundary.
- **Wrapper names mirror the replaced call.** Each wrapper has a chunk ID (PATH-, FS-, CWD-, STDIO-, POSIX-, WIN-) listed in `docs/wrapper_chunks.md`.
- **Build.** CMake, C++23, links the static dwarfs libraries `reader`, `decompressor`, `common`, and their third-party dependencies (fmt, Boost, zstd, libcrypto, xxhash, FLAC++, optional lz4/lzma/brotli/cpptrace). See `CMakeLists.txt`.

## Implementation status
| Area | IDs | Status |
|---|---|---|
| Path wrappers | PATH-01..14 | Implemented, tested |
| Archive path normalization and root | PATH-15, root helpers | Implemented, tested |
| Filesystem wrappers | FS-01..24 | Implemented, tested (passthrough and routing) |
| Working directory | CWD-01..05 | Implemented, tested |
| POSIX wrappers | POSIX-01..20 | Implemented, tested (host passthrough, archive paths fail `ENOTSUP`) |
| Mount / archive open | `mount_archive` | Opens the dwarfs image and registers it with the mountpoint. |
| Archive FS queries | `canonical`, `equivalent`, `is_symlink`, `is_empty`, `read_symlink`, mutations (`EROFS`) | Implemented, tested |
| Archive read access | `archive::backend`, `archive_io`, FS and POSIX branches | Implemented, tested against an in-memory backend. dwarfs path tested with a fixture image (see below). |
| stdio wrappers | STDIO-01..09 | Implemented, tested. Archive files read-only via in-memory copy (see archive_access.md). |
| Path walk | `walk_lexically` | Implemented, tested. Host symlinks followed. |
| Win32 wrappers | WIN-01..05 | Declared only |

Tests: 196 pass under CTest. CTest rebuilds the test archive (`build/fixtures/fixture.dwarfs`, from `tests/archive/make_fixture.cmake`) before any test that needs it. The tests mount only that archive and check its exact contents: `DIR/FILE.DAT` containing `payload`, nothing else. The dwarfs libraries used for this check came from the upstream source (0.15.5); the headers were the uploaded set (0.15.3), and the key headers are identical.

## Decisions (settled)
1. `..` from the archive root leaves the archive, resolving to the host path `/`.
2. Host symlinks are followed. `..` is lexical only inside the archive.
3. While an archive is mounted, host paths under `/rwz` are routed to the archive.
4. The Windows root is fixed to drive `C:`.
5. `close`, `seek`, `write`, `truncate` map to POSIX. `remove`, `rename` map to C stdio.

## Remaining work (in order)
1. Archive symlink following.
2. Archive directory iteration: a new iterator type for `directory_iterator` and `recursive_directory_iterator`. This needs a design decision, because the standard iterator types can't represent archive entries.
3. Overlay writes: create, write, rename, delete, stored outside the archive.
4. Streaming for large archive files (the current stdio path copies whole files into memory). Check `fmemopen` on macOS and BSD.
5. Win32 wrappers (WIN-01..05), plus helpers not in the call list (for example `CloseHandle`). Needs a Windows toolchain.
6. Cross-platform test matrix (Linux, macOS, BSD, Windows).

## Known limitations
- Overlay writes are not started. Archive writes return `EROFS`.
- Archive symlinks are not followed.
- `ioctl` is declared with one `void*` argument. Variadic callers must be checked.
- `getcwd` with an archive cwd and a null buffer returns `EINVAL`. Allocation is not implemented.
- Relative paths resolve against the host cwd unless an archive cwd is set.
- `mount_archive` does not keep the image, so mounted archives cannot be read yet.

## Building and testing
```
cmake -S . -B build -DDWARFS_PREFIX=/usr/local      # mkdwarfs must be in DWARFS_PREFIX/bin or on PATH
cmake --build build
ctest --test-dir build --output-on-failure           # CTest rebuilds the test archive first
```
Run tests through CTest, not the test binary directly, so the archive fixture is regenerated.
Boost.Process is optional in CMake. It is linked when found.

## Layout
- `include/rwzfs/` public headers (`fs/`, `io/`, `mount.hpp`)
- `src/` implementations
- `tests/` GoogleTest suites (`tests/fs/`, `tests/io/`)
- `docs/` specs: `archive_paths.md`, `cwd_design.md`, `wrapper_chunks.md`, this file
