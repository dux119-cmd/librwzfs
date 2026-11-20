# rwzfs handover

## Goal
A cross-platform (Linux, macOS, BSD, Windows) C++23 library. Applications call its IO wrappers in place of the standard calls and get:
1. Passthrough to the host, always. With no archive mounted, every wrapper behaves like the standard call it replaces.
2. Read access to a mounted dwarfs archive through the same wrappers. Overlay writes are planned.

## Key design choices
- **Magic archive root.** Archive paths are absolute and start with `/rwz` (POSIX) or `C:/RWZ` (Windows). Relative paths under an archive working directory expand to this form. See `docs/archive_paths.md`.
- **Routing is lexical, then resolved.** `resolve()` makes a path absolute (archive working directory included) and applies `..` lexically inside the archive. A `..` that leaves the archive, or any `..` on a host path, is passed to the OS raw, so host symlinks are followed. `mounts::find` routes the result to the longest matching mountpoint. See `docs/cwd_design.md`.
- **Archive backend interface.** `rwz::archive::backend` is read-only and dwarfs-free: `lookup`, `read`, `list`, `link_target`. The dwarfs implementation is the only file that includes dwarfs headers. The fake backend in `tests/support` lets archive behavior be tested without dwarfs. See `docs/archive_access.md`.
- **Error reporting.** `std::error_code&` in the FS wrappers, `errno` and `-1` in the POSIX and stdio wrappers. Mutations on a mounted archive return `EROFS`. Queries the archive cannot answer return `not_supported`. Nothing throws across the wrapper boundary, except that iterator increments throw as the standard ones do.
- **Iterators are wrappers, not std types.** `directory_iterator` and `recursive_directory_iterator` are classes in `rwz::fs`. Host listings forward to the standard iterators. Archive listings walk the backend. Both support `auto`, range-for, `depth()`, and `disable_recursion_pending()`.
- **Archive descriptors** are numbered from `0x40000000`, so they cannot collide with host descriptors.
- **C++ streams**: `file_istream` / `file_ostream` (over the stdio wrappers) work on archive and overlay paths on every platform.
- **Archive stdio streams** are read-only. On glibc, macOS and FreeBSD they are callback streams: `fread` and `fseek` read chunks from the archive through dwarfs's block cache, with nothing copied. On Windows the CRT has no callback stream, so the file is copied, in chunks, to a temporary file that `fclose` deletes. Close them with `rwz::stdio::fclose`.
- **Wrapper names mirror the replaced call.** Chunk IDs (PATH-, FS-, CWD-, STDIO-, POSIX-, WIN-) are listed in `docs/wrapper_chunks.md`.
- **Build.** CMake, C++23. Links the static dwarfs libraries `reader`, `decompressor`, `common`, plus third-party dependencies. Boost.Process is optional.

## Implementation status
| Area | IDs | Status |
|---|---|---|
| Path wrappers | PATH-01..14 | Implemented, tested |
| Archive path normalization, archive root | PATH-15, `archive_root` | Implemented, tested |
| Path walk and resolution | `walk_lexically`, `resolve`, `resolve_no_follow` | Implemented, tested. Archive symlinks followed, 5-link limit (`ELOOP`). |
| Working directory | CWD-01..05 | Implemented, tested. `chdir` verifies archive directories. |
| Mount table | `mounts::add/find/clear` | Implemented, tested. Mounting sets the archive working directory. Each mount records its overlay directory. |
| Overlay storage path | `mount_options`, `overlay::overlay_directory` | Implemented, tested. Creates `<write root>/<stem>-writes` at mount. |
| Copy-on-write engine | `overlay::cow`, overlay POSIX and stdio | Implemented, tested. Copy-up, create, unlink, mkdir, rmdir, truncate, file rename, merged listings, whiteouts and opaque markers. Deferred copy-up for POSIX write-opens. |
| Overlay lock | `overlay::lock`, `unmount_archive` | Implemented, tested (POSIX). Windows uses an exclusive create of the same file. `.rwz.lock` breadcrumb: refuses while present, with a message to delete it. Released on unmount, clear, and exit. Windows not yet implemented. |
| Archive creation | `create_archive`, `create_archive_from_view` | Implemented and tested on Linux, macOS and Windows (dwarfs builds). In-process dwarfs writer (no CLI), with the CLI's categorize, no-history and compression settings. POSIX only for now. |
| Archive revision | `revise_archive`, `next_revision_name` | Implemented, tested (dwarfs CTest). Writes `-revN` from the merged view (no scratch tree, no CLI), unmounts, clears overlay. |
| CI | `.github/workflows/ci.yml` | Running. `unit` on Linux, macOS, Windows (no dwarfs). `full` on Linux and macOS (dwarfs v0.15.5 from source, cached by script hash). `deps-windows` builds the static dependencies and dwarfs with MSVC, builds rwzfs with dwarfs, and runs the Windows tests. |
| Metadata COW | `chmod`, `utimensat`, `fs::permissions`, `fs::last_write_time` | Implemented, tested. Directory permission changes copy the whole tree; modification times apply to files only. |
| Archive open | `mount_archive` | Opens the dwarfs image, registers it with its mountpoint. |
| Archive read backend | `archive::backend`, dwarfs backend | Implemented, tested against the dwarfs fixture (including symlinks). |
| Archive descriptors, DIR handles | `archive_io` | Implemented, tested |
| POSIX wrappers | POSIX-01..20 | Implemented, tested. Archive: overlay-backed when mounted with an overlay. `glob` over the merged view (no GLOB_APPEND/BRACE/TILDE). |
| FS queries | FS-01..24 | Implemented, tested. `exists`, `status`, `is_*`, `canonical`, `equivalent`, `is_empty`, `read_symlink` work on archives. Mutations `EROFS`. `last_write_time`, `space`, `proximate` `not_supported`. |
| Directory iterators | FS-09, FS-18 | Implemented, tested. Classes with the standard interface. |
| Stdio wrappers | STDIO-01..13 (`fwrite`, `fseek`, `fseeko`, `ftello` added for vendoring) | Implemented, tested. Archive files (glibc, macOS, FreeBSD): read-only callback stream, chunk reads, no copy. Windows: read-only temporary copy, removed on `fclose`. |
| Win32 wrappers | WIN-01..05 (`CreateFileW`, `ReadFile`, `SetFilePointer`, `FindFirstFileW`, `CreateDirectoryW`) plus `FindNextFileW`, `FindClose`, `DeleteFileW`, `RemoveDirectoryW`, `CloseHandle` | Implemented, tested on Windows CI. `CreateFileW` opens overlay and host paths with real handles; read-only archive files and directories get table handles; writes copy up into the overlay. Host paths call Win32. Archive paths: read-only file opens use an archive handle table; writes copy up; directory handles and `FindFirstFileW` return `ERROR_NOT_SUPPORTED`. |

## Build and test settings
- ccache is used when installed, with `CCACHE_COMPRESSLEVEL=6` applied through the compiler launcher. Disable with `-DRWZFS_USE_CCACHE=OFF`.
- Precompiled headers cover the common standard headers, for the library and the tests. Disable with `-DRWZFS_PCH=OFF`.
- `CMakePresets.json` sets build and test `jobs` to 0, which uses all cores with the native build tool. Plain `cmake --build build` does not: pass `--parallel` or set `CMAKE_BUILD_PARALLEL_LEVEL`. CMake cannot set this from `CMakeLists.txt`.

## Tests
- Standalone (no dwarfs): 316 tests pass with `-Wall -Wextra -Werror`.
- CMake with dwarfs: 349 tests pass under CTest. Mount tests use a temporary write root, so they never write to the real user state directory. CTest rebuilds the test archive (`build/fixtures/fixture.dwarfs`, from `tests/archive/make_fixture.cmake`) before tests that need it. The archive content is fixed and listed in that file. Tests check the exact content.
- The dwarfs libraries used for testing came from upstream 0.15.5. The headers were the uploaded set (0.15.3). Key headers are identical.

## Decisions (settled)
1. `..` from the archive root leaves the archive, resolving to the host path `/`.
2. Host symlinks are followed by the OS. Archive symlinks are followed by `resolve()`, at most 5 per path (`ELOOP` beyond that).
3. While an archive is mounted, host paths under `/rwz` route to the archive.
4. The Windows archive root is fixed at `C:/RWZ`.
5. `close`, `seek`, `write`, `truncate` map to POSIX. `remove`, `rename` map to C stdio.
6. Directory iterators are wrapper classes with the standard interface.
7. Archive stdio streams are read-only: callback streams over chunk reads on glibc, macOS and FreeBSD (no copy); a temporary copy on Windows, removed on `fclose`.
9. Overlay lock: `.rwz.lock` in the overlay directory while the archive is open. If it is present, the archive is refused; the user deletes it after a crash. Released on `unmount_archive`, on clear, and at exit. Opt-in signal cleanup.
11. Archive creation uses the dwarfs writer library. Default compression is `zstd:level=12:wlog=27:strat=3`, which needs a zstd that exposes wlog/strat (the static build's zstd does).
12. Revision unmounts the old archive and clears its overlay after the new archive exists. See docs/archive_creation.md.
10. Copy-up is file-level, in one step, and happens on the first write for POSIX descriptors. Stdio write opens of archive files copy up on the first write where the platform has a callback stream (glibc, macOS, FreeBSD), and at open on Windows. Metadata changes copy up files only; only modification times are stored.
8. Overlay storage: `mount_options::write_root` is the top-level writable directory. The library creates `<stem>-writes` inside it. Default: `$XDG_STATE_HOME/rwz` when `XDG_STATE_HOME` is an absolute path, else `$HOME/.local/state/rwz` on Linux and BSD; `~/Library/Application Support/rwz` on macOS; `%LOCALAPPDATA%\rwz` on Windows. The archive stem is the file name up to its first dot. See docs/overlay_design.md.

## Remaining work
Nothing is open that is in scope. Accepted behaviour, not work items:
- Host symlinks that point into `/rwz` are not followed into the archive. This is normal host behaviour.
- Ownership is not stored, by design (like a zip file: the mounting process owns the view).

## Known limitations
- `std::ifstream` / `std::ofstream` that the vendoring tool cannot rewrite (member `open()` / `close()`) still call the CRT directly and do not reach the wrappers.
- `open`, `ioctl` and `fcntl` have the C call forms (no argument, an integer, or a pointer). Other variadic uses are not covered. `fcntl` on archive descriptors supports the read-only queries and no-op flag changes; locks and duplication return `ENOTSUP`.
- Archive stdio on Windows copies the whole file to a temporary file. Elsewhere the callback streams read chunks, so memory use does not grow with file size.
- Overlay: access times are not stored; symlinks cannot be created or renamed in the overlay. A directory permission change copies its whole tree into the overlay; a directory rename moves the whole tree.
- The stdio and POSIX wrappers are not `[[nodiscard]]` (they mirror C).
- Stdio write opens of archive files copy up on the first write where the platform has a callback stream (glibc, macOS, FreeBSD). On Windows they copy up at open. A `w` open truncates, so it copies up at open everywhere.
- Archive creation and revision are in-process with the dwarfs writer (no `mkdwarfs` binary), on all three platforms.
- Signal cleanup is best effort; SIGKILL leaves a stale lock, which the next mount takes over.
- Overlay stem collisions: archives with the same stem share one overlay directory.
- Archive descriptor numbers are never reused.
- Windows archive stdio copies the file to a temporary file (see `docs/WINDOWS_STDIO_PLAN.md`). The copy is the archive file's size. `std::ifstream` calls the CRT directly; the vendoring tool rewrites declarations to `file_istream`.
- Archive directories: reads, seeks and writes through a `CreateFileW` handle fail, as on Windows. Overlay directories return real handles.
- Symlinks: the library follows archive symlinks; host symlinks that point into `/rwz` are not followed into the archive (accepted).
- Owners are not stored in archives (by design).

## Building and testing
```
cmake -S . -B build                                    # managed dependencies (default)
cmake -S . -B build -DRWZFS_USE_MANAGED_DEPS=OFF -DDWARFS_PREFIX=/usr/local   # system libraries
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```
Run tests through CTest, not the test binary directly, so the archive fixture is regenerated.
Archive creation needs no `mkdwarfs` binary; dwarfs is built as libraries (`scripts/deps/dwarfs.sh`).

## Documentation for users
- `docs/USAGE.md`: create, mount, use the wrappers, revise. The showcase `examples/usage.cpp` (`rwzfs_example`) runs the whole library end to end, comparing host, archive, changed and revised transcripts. It runs as a CTest test and as a CI step on Linux, macOS and Windows.

## Benchmark
- `docs/BENCHMARK.md`: `rwzfs_bench compare` runs the read benchmark on the host tree and the mounted archive, with statistics and a comparison. The reduced run is verified here; the full 4.4 GB run needs a machine with ~10 GB of RAM.

## Using rwzfs in an existing project
- `docs/VENDORING.md` and `tools/rwzfs_vendor.py`: vendor the library into a CMake C++ project and switch its IO calls to the wrappers. The report lists what needs a person. Tested on a sample project.

## Static builds
- `docs/STATIC_BUILD.md`: `scripts/deps/build-all.sh` (one builder per dependency, versions in `versions.sh`) builds dwarfs and its dependencies as static, size-optimized libraries from pinned tags. CMake runs it at configure time (`RWZFS_USE_MANAGED_DEPS`, on by default). CI builds them on Linux, macOS and Windows.

## Layout
- `include/rwzfs/` public headers (`fs/`, `io/`, `archive/`, `mount.h`)
- `src/` implementations (`src/archive/dwarfs_backend.cpp` is the only dwarfs-dependent backend file)
- `tests/` GoogleTest suites (`tests/fs/`, `tests/io/`, `tests/archive/`, `tests/support/` fake backend)
- `docs/` specs: `archive_paths.md`, `cwd_design.md`, `archive_access.md`, `wrapper_chunks.md`, this file
- `CMakePresets.json` configure, build, and test presets

## Namespace layout
- `rwz::posix`, `rwz::stdio`, `rwz::win32`, `rwz::fs`: the public wrappers and filesystem functions (POSIX-only `posix`, Windows-only `win32`).
- `rwz::detail`: internal (archive I/O, archive glob, overlay POSIX support). Not public API.
- `rwzfs`: archive, overlay, mount, revise, creator, and the `rwz::fs::cwd` / `rwz::fs::mounts` helpers.
- `rwzfs/rwz.h`: umbrella include for the library.

## Headers and errors
- Library headers are `.h` (for example `include/rwzfs/archive/create.h`). Test-support headers keep `.hpp`.
- The throwing forms of the nine `rwz::fs` functions (see USAGE) are covered by `tests/fs/throwing_test.cpp`.

## Review: allocations, constexpr, branches (last pass)
- Changed: C++ stream buffers hold their 64 KiB read chunk in a `std::array` inside the buffer object (one allocation per stream, not two); the chunk size is a named `constexpr`. Overlay path helpers and `has_meta` take `std::string_view`.
- Checked, no change: if/else pairs that assign one variable already use initialized `const` or ternaries (none found). The copy-on-write copy buffer and the flatten buffer are `std::vector` because their size is runtime data (up to 1 MiB) and must not sit on the stack. Directory listings and archive children are `std::vector`s: their length is only known after the walk.
- Left alone: `const std::string&` parameters that pass to `c_str()`, `open()` or `fopen()`, since `string_view` has no `c_str()` and the call would copy anyway.

## Sanitizer testing
One flag builds the tests with debug info, AddressSanitizer and UndefinedBehaviorSanitizer:

    cmake --preset sanitize && cmake --build --preset sanitize && ctest --preset sanitize

or, without presets: `cmake -S . -B build-sanitize -DRWZFS_SANITIZE=ON`. The flag turns on the tests and
sets the build type to Debug (unless one is given). `-fno-sanitize-recover` makes the first undefined
behaviour fail its test. The test preset sets `ASAN_OPTIONS` and `UBSAN_OPTIONS` (leak detection on).
GCC and Clang only. Verified here: all 348 tests pass with the sanitizers linked and no reports.
Not covered: the managed dependencies are built without the sanitizers (shared by all builds), and the dwarfs
tests need `-DRWZFS_WITH_DWARFS=ON` with a dwarfs install.

## Coding standard
- Every variable, struct, array and class member is initialized when declared: `T a = {};`, `int a = 0;`, `bool b{};`. No uninitialized declarations.
- Exception: dwarfs-typed members in `dwarfs_backend.h` are initialized in the constructor's initializer list (their types are not default-constructed here).

## Archive reader tuning
- dwarfs read-ahead: 64 KiB (`inode_reader.readahead`), set in `src/archive/dwarfs_backend.cpp`. The block cache keeps dwarfs's default (512 MiB). The read-ahead value is not yet measured; use `rwzfs_bench` to revisit it.
