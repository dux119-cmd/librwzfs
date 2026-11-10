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
| Path walk and resolution | `walk_lexically`, `resolve`, `resolve_no_follow` | Implemented, tested. Archive symlinks followed, 5-link limit (`ELOOP`). |
| Working directory | CWD-01..05 | Implemented, tested. `chdir` verifies archive directories. |
| Mount table | `mounts::add/find/clear` | Implemented, tested. Mounting sets the archive working directory. Each mount records its overlay directory. |
| Overlay storage path | `mount_options`, `overlay::overlay_directory` | Implemented, tested. Creates `<write root>/<stem>-writes` at mount. |
| Copy-on-write engine | `overlay::cow`, overlay POSIX and stdio | Implemented, tested. Copy-up, create, unlink, mkdir, rmdir, truncate, file rename, merged listings, whiteouts and opaque markers. Deferred copy-up for POSIX write-opens. |
| Overlay lock | `overlay::lock`, `unmount_archive` | Implemented, tested (POSIX). `.rwz.lock` breadcrumb: refuses while present, with a message to delete it. Released on unmount, clear, and exit. Windows not yet implemented. |
| Archive creation | `create_archive` | Implemented, tested (dwarfs CTest). Runs mkdwarfs with the CLI flags. POSIX only. |
| Archive revision | `flatten`, `revise_archive`, `next_revision_name` | Implemented, tested (dwarfs CTest). Flatten, create `-revN`, unmount, clear overlay. |
| CI | `.github/workflows/ci.yml` | Written, not yet run. `unit` on Linux, macOS, Windows (no dwarfs). `full` on Linux and macOS (dwarfs v0.15.5 from source). |
| Metadata COW | `chmod`, `utimensat`, `fs::permissions`, `fs::last_write_time` | Implemented, tested. Files only; directories `EPERM`. |
| Archive open | `mount_archive` | Opens the dwarfs image, registers it with its mountpoint. |
| Archive read backend | `archive::backend`, dwarfs backend | Implemented, tested against the dwarfs fixture (including symlinks). |
| Archive descriptors, DIR handles | `archive_io` | Implemented, tested |
| POSIX wrappers | POSIX-01..20 | Implemented, tested. Archive: overlay-backed when mounted with an overlay. `glob` over the merged view (no GLOB_APPEND/BRACE/TILDE). |
| FS queries | FS-01..24 | Implemented, tested. `exists`, `status`, `is_*`, `canonical`, `equivalent`, `is_empty`, `read_symlink` work on archives. Mutations `EROFS`. `last_write_time`, `space`, `proximate` `not_supported`. |
| Directory iterators | FS-09, FS-18 | Implemented, tested. Classes with the standard interface. |
| Stdio wrappers | STDIO-01..13 (`fwrite`, `fseek`, `fseeko`, `ftello` added for vendoring) | Implemented, tested. Archive files: read-only in-memory copy. |
| Win32 wrappers | WIN-01..05 (+ `close_handle`) | Implemented (Windows only, compiled by the Windows CI job; not yet run). Host paths call Win32; archive paths fail with `ERROR_NOT_SUPPORTED`. |

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
4. The Windows root is fixed to drive `C:`.
5. `close`, `seek`, `write`, `truncate` map to POSIX. `remove`, `rename` map to C stdio.
6. Directory iterators are wrapper classes with the standard interface.
7. Archive stdio streams are read-only in-memory copies. The buffer is freed by `fclose`.
9. Overlay lock: `.rwz.lock` in the overlay directory while the archive is open. If it is present, the archive is refused; the user deletes it after a crash. Released on `unmount_archive`, on clear, and at exit. Opt-in signal cleanup.
11. Archive creation uses the mkdwarfs CLI, not the writer library. Default compression is `zstd:level=12:wlog=27:strat=3`, which needs a zstd that exposes wlog/strat.
12. Revision unmounts the old archive and clears its overlay after the new archive exists. See docs/archive_creation.md.
10. Copy-up is file-level, in one step, and happens on the first write for POSIX descriptors. Stdio write opens copy up at open (a platform limitation). Metadata changes copy up files only; only modification times are stored.
8. Overlay storage: `mount_options::write_root` is the top-level writable directory. The library creates `<stem>-writes` inside it. Default: `$XDG_STATE_HOME/rwz` when `XDG_STATE_HOME` is an absolute path, else `$HOME/.local/state/rwz` on Linux and BSD; `~/Library/Application Support/rwz` on macOS; `%LOCALAPPDATA%\rwz` on Windows. The archive stem is the file name up to its first dot. See docs/overlay_design.md.

## Remaining work (in order)
1. **Verify the CI workflow** on GitHub Actions for all three platforms. Windows Win32 wrappers and the Windows lock and metadata are compiled and tested for the first time there.
2. **Win32 archive access.** Route Win32 calls on archive paths to the archive and overlay (currently `ERROR_NOT_SUPPORTED`).
3. **Creation without the mkdwarfs CLI** (library writer), if the process-spawn dependency becomes a problem.
4. **funopen path (macOS/FreeBSD)** is compiled but not yet run: check the macOS CI job. Windows still copies at open for stdio write-opens.
5. **Directory permission storage** in the overlay.

## Known limitations
- `input_file_stream` and `output_file_stream` return failed streams for archive paths. A C++ stream cannot wrap an in-memory copy.
- `open` and `ioctl` have the C call forms: `open(path, flags)`, and `ioctl` with no argument, an integer, or a pointer. Other variadic forms are not covered, and `fcntl` is not wrapped yet.
- `getcwd` with an archive working directory and a null buffer returns `EINVAL`.
- Archive stdio streams hold the whole file in memory.
- Overlay: directory permissions, access times, symlink creation and symlink rename are not implemented. Directory rename copies the whole tree.
- The stdio and POSIX wrappers are not `[[nodiscard]]` (they mirror C).
- Stdio write opens (`fopen` with `w`, `a`, `+`) copy the file up at open, not on the first write.
- Archive creation and revision run the mkdwarfs CLI. They are POSIX only; Windows returns `unsupported_platform`.
- Revision holds the whole flattened tree in a scratch directory, so its disk use is at least the size of the merged view. Deferring this needs `fopencookie`/`funopen`, which are not portable to all platforms (Windows has neither), so it is a permanent limitation of the stdio layer.
- Signal cleanup is best effort; SIGKILL leaves a stale lock, which the next mount takes over.
- Overlay stem collisions: archives with the same stem share one overlay directory.
- Archive descriptor numbers are never reused.
- Host symlinks that point into `/rwz` are not followed into the archive.

## Building and testing
```
cmake --preset default                               # or: cmake -S . -B build -DDWARFS_PREFIX=/usr/local
cmake --build --preset default                       # uses all cores
ctest --preset default                               # rebuilds the test archive first
```
Run tests through CTest, not the test binary directly, so the archive fixture is regenerated.
`mkdwarfs` must be in `DWARFS_PREFIX/bin` or on `PATH`.

## Documentation for users
- `docs/USAGE.md`: create, mount, use the wrappers, revise. Worked example in `examples/usage.cpp` (built by CMake with dwarfs; output in the guide).

## Benchmark
- `docs/BENCHMARK.md`: `rwzfs_bench compare` runs the read benchmark on the host tree and the mounted archive, with statistics and a comparison. The reduced run is verified here; the full 4.4 GB run needs a machine with ~10 GB of RAM.

## Using rwzfs in an existing project
- `docs/VENDORING.md` and `tools/rwzfs_vendor.py`: vendor the library into a CMake C++ project and switch its IO calls to the wrappers. The report lists what needs a person. Tested on a sample project.

## Layout
- `include/rwzfs/` public headers (`fs/`, `io/`, `archive/`, `mount.hpp`)
- `src/` implementations (`src/archive/dwarfs_backend.cpp` is the only dwarfs-dependent backend file)
- `tests/` GoogleTest suites (`tests/fs/`, `tests/io/`, `tests/archive/`, `tests/support/` fake backend)
- `docs/` specs: `archive_paths.md`, `cwd_design.md`, `archive_access.md`, `wrapper_chunks.md`, this file
- `CMakePresets.json` configure, build, and test presets
