# rwzfs handover

## Goal
A cross-platform (Linux, macOS, BSD, Windows) C++23 library. Applications call its IO wrappers in place of the standard calls and get:
1. Passthrough to the host, always. With no archive mounted, every wrapper behaves like the standard call it replaces.
2. Read access to a mounted dwarfs archive through the same wrappers. Overlay writes are planned.

## Key design choices
- **Magic archive root.** Archive paths are absolute and start with `/rwz` (POSIX) or `C:/RWZ` (Windows). Relative paths under an archive working directory expand to this form. See `docs/archive_paths.md`.
- **Routing is lexical, then resolved.** `resolve()` makes a path absolute (archive working directory included) and applies `..` lexically inside the archive. A `..` that leaves the archive, or any `..` on a host path, is passed to the OS raw, so host symlinks are followed. `mounts::find` routes the result to the longest matching mountpoint. See `docs/cwd_design.md`.
- **Archive backend interface.** `rwzfs::archive::backend` is read-only and dwarfs-free: `lookup`, `read`, `list`, `link_target`. The dwarfs implementation is the only file that includes dwarfs headers. The fake backend in `tests/support` lets archive behavior be tested without dwarfs. See `docs/archive_access.md`.
- **Error reporting.** `std::error_code&` in the FS wrappers, `errno` and `-1` in the POSIX and stdio wrappers. Mutations on a mounted archive return `EROFS`. Queries the archive cannot answer return `not_supported`. Nothing throws across the wrapper boundary, except that iterator increments throw as the standard ones do.
- **Iterators are wrappers, not std types.** `directory_iterator` and `recursive_directory_iterator` are classes in `rwzfs::fs`. Host listings forward to the standard iterators. Archive listings walk the backend. Both support `auto`, range-for, `depth()`, and `disable_recursion_pending()`.
- **Archive descriptors** are numbered from `0x40000000`, so they cannot collide with host descriptors.
- **Archive stdio streams** are read-only in-memory copies (`fmemopen`). They must be closed with `rwzfs::io::stdio::fclose`.
- **Wrapper names mirror the replaced call.** Chunk IDs (PATH-, FS-, CWD-, STDIO-, POSIX-, WIN-) are listed in `docs/wrapper_chunks.md`.
- **Build.** CMake, C++23. Links the static dwarfs libraries `reader`, `decompressor`, `common`, plus third-party dependencies. Boost.Process is optional.

## Implementation status
| Area | IDs | Status |
|---|---|---|
| Path wrappers | PATH-01..14 | Implemented, tested |
| Archive path normalization, archive root | PATH-15, `archive_root` | Implemented, tested |
| Path walk (`..`, symlink-aware for host) | `walk_lexically` | Implemented, tested |
| Working directory | CWD-01..05 | Implemented, tested. `chdir` verifies archive directories. |
| Mount table | `mounts::add/find/clear` | Implemented, tested. Mounting sets the archive working directory. |
| Archive open | `mount_archive` | Opens the dwarfs image, registers it with its mountpoint. |
| Archive read backend | `archive::backend`, dwarfs backend | Implemented, tested against the dwarfs fixture |
| Archive descriptors, DIR handles | `archive_io` | Implemented, tested |
| POSIX wrappers | POSIX-01..20 | Implemented, tested. Archive: read-only, mutations `EROFS`. |
| FS queries | FS-01..24 | Implemented, tested. `exists`, `status`, `is_*`, `canonical`, `equivalent`, `is_empty`, `read_symlink` work on archives. Mutations return `EROFS`. `last_write_time`, `space`, `proximate` return `not_supported`. |
| Directory iterators | FS-09, FS-18 | Implemented, tested. Classes, not std types. |
| Stdio wrappers | STDIO-01..09 | Implemented, tested. Archive files: read-only in-memory copy. |
| Win32 wrappers | WIN-01..05 | Declared only. Needs a Windows toolchain. |

## Tests
- Standalone (no dwarfs): 214 tests pass with `-Wall -Wextra -Werror`.
- CMake with dwarfs: 227 tests pass under CTest. CTest rebuilds the test archive (`build/fixtures/fixture.dwarfs`, from `tests/archive/make_fixture.cmake`) before tests that need it. The archive content is fixed: `DIR/FILE.DAT` containing `payload`, and nothing else. Tests check that exact content.
- The dwarfs libraries used for testing came from upstream 0.15.5. The headers were the uploaded set (0.15.3). Key headers are identical.

## Decisions (settled)
1. `..` from the archive root leaves the archive, resolving to the host path `/`.
2. Host symlinks are followed. `..` is lexical only inside the archive.
3. While an archive is mounted, host paths under `/rwz` route to the archive.
4. The Windows root is fixed to drive `C:`.
5. `close`, `seek`, `write`, `truncate` map to POSIX. `remove`, `rename` map to C stdio.
6. Directory iterators are wrapper classes with the standard interface. Callers using `auto` need no changes.
7. Archive stdio streams are read-only in-memory copies. The buffer is freed by `fclose`.

## Remaining work (in order)
1. **Archive symlink following.** Reads of link targets work (`read_symlink`). Following links during resolution does not. This needs a design first. Open questions:
   - A link target outside the archive (for example `/usr/lib/x`) becomes a host path. Its remaining components should go to the OS unchanged, as `..` already does.
   - Relative targets resolve against the link's directory, inside the archive.
   - `..` after a link must be resolved against the link's target, as the OS does, not lexically.
   - Loop limit: `ELOOP` after 40 links, matching POSIX.
   Plan: implement it inside `resolve()`, walking component by component, so `link/..` behaves like the OS.
2. **Overlay writes.** Create, write, rename, delete, stored outside the archive. Needs a storage location decision and a design doc.
3. **Streaming for large archive files.** The current stdio path copies whole files into memory. Check `fmemopen` availability on macOS and BSD before relying on it.
4. **Windows wrappers (WIN-01..05)** and helpers not in the call list (for example `CloseHandle`, `FindClose`). Needs a Windows toolchain.
5. **Cross-platform test matrix** (Linux, macOS, BSD, Windows).
6. **Fixture symlinks.** The fixture has no symlinks, so link following is untested against real dwarfs. Adding them needs a change to the fixture content and its exact-content tests.

## Known limitations
- Archive symlinks are reported as links and not followed. `open`, `stat`, and similar calls on a link do not follow it.
- `input_file_stream` and `output_file_stream` return failed streams for archive paths. A C++ stream cannot wrap an in-memory copy.
- `glob` does not list archive entries.
- `ioctl` is declared with one `void*` argument. Variadic callers must be checked.
- `getcwd` with an archive working directory and a null buffer returns `EINVAL`.
- Archive stdio streams hold the whole file in memory.
- Archive descriptor numbers are never reused.

## Building and testing
```
cmake -S . -B build -DDWARFS_PREFIX=/usr/local      # mkdwarfs must be in DWARFS_PREFIX/bin or on PATH
cmake --build build
ctest --test-dir build --output-on-failure           # CTest rebuilds the test archive first
```
Run tests through CTest, not the test binary directly, so the archive fixture is regenerated.

## Layout
- `include/rwzfs/` public headers (`fs/`, `io/`, `archive/`, `mount.hpp`)
- `src/` implementations (`src/archive/dwarfs_backend.cpp` is the only dwarfs-dependent backend file)
- `tests/` GoogleTest suites (`tests/fs/`, `tests/io/`, `tests/archive/`, `tests/support/` fake backend)
- `docs/` specs: `archive_paths.md`, `cwd_design.md`, `archive_access.md`, `wrapper_chunks.md`, this file
