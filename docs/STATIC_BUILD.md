# Static, size-optimized dwarfs and dependencies

rwzfs can use dwarfs and its dependencies as static, size-optimized libraries built from pinned
source. Two scripts do the work, and CI and users run the same ones.

| script | builds |
|---|---|
| `scripts/deps/build-deps.sh` | zlib, zstd, xxHash, fmt, FLAC, OpenSSL (libcrypto), libarchive, Boost, jemalloc, utfcpp, nlohmann/json, parallel-hashmap; googletest with `WITH_TESTS=1` |
| `scripts/deps/build-dwarfs.sh` | libdwarfs (reader and writer) at `DWARFS_REF`, against the dependencies above |

## Use them directly

    DEPS_PREFIX=$PWD/deps-install bash scripts/deps/build-deps.sh
    DEPS_PREFIX=$PWD/deps-install DWARFS_PREFIX=$PWD/dwarfs-install bash scripts/deps/build-dwarfs.sh
    cmake -S . -B build -DDWARFS_PREFIX=$PWD/dwarfs-install -DCMAKE_PREFIX_PATH=$PWD/deps-install

Environment variables (see the header of each script): `DEPS_PREFIX`, `DEPS_WORK`, `DWARFS_PREFIX`,
`DWARFS_WORK`, `DWARFS_REF`, `JOBS`, `WITH_TESTS`, `JEMALLOC` (default 1), `ONLY` (a comma list to
build a subset), `KEEP_WORK`, `CC`, `CXX`. A dependency that is already built is skipped on a rerun; delete
`$DEPS_PREFIX/.stamps/<name>` to rebuild it.

## Or let CMake do it

    cmake -S . -B build -DRWZFS_BUILD_DEPS_STATIC=ON

This runs both scripts at configure time, into `build/deps-install` and `build/dwarfs-install`, and
points rwzfs at them. It needs bash, git, curl, a C++23 compiler, cmake, and (on Apple) Xcode 16.x or
later. Expect hours on one CPU; a cache makes later configures instant.

## What is kept and what is dropped

dwarfs is configured for zstd and FLAC compression only:

- Off: LZ4, LZMA, Brotli, RICEPP compressors; dwarfs's own jemalloc and mimalloc hooks; the command-line
  tools (rwzfs writes archives through the library); FUSE driver and extract binary; man pages; desktop
  integration; perf monitor; stack traces; ccache.
- On: zstd (always), FLAC.
- jemalloc is built by `build-deps.sh` (static, `JEMALLOC=1` by default). rwzfs links it by default
  (`RWZFS_USE_JEMALLOC`). An unprefixed static jemalloc replaces malloc in the executable that links
  rwzfs. Set `-DRWZFS_USE_JEMALLOC=OFF` to keep the platform allocator.

Dependencies that dwarfs requires unconditionally stay (libarchive, libcrypto, xxHash, Boost chrono,
filesystem, program_options, process, thread).

Every library is built with `-Os -ffunction-sections -fdata-sections`, `MinSizeRel`, static, and
position-independent code, so a consumer's linker can drop unused functions (`-Wl,--gc-sections`).
rwzfs itself is compiled with the same flags in Release (`RWZFS_SIZE_OPTIMIZED`, on by default).

## Notes

- Versions are pinned by git tag (and the Boost CMake release tarball). git verifies tag objects on
  clone. The tarball has no checksum yet; add one to `build-deps.sh` before relying on it for
  release builds.
- macOS: `patch-dwarfs-apple.py` replaces dwarfs's one use of `std::chrono::from_stream`, which Apple's
  libc++ lacks. It is applied only on Darwin.
- Static consumers: pkg-config files list `Libs` without `Libs.private`, so a consumer that links the
  static libraries directly may need the transitive list (`pkg-config --static`). The build scripts do
  not change that.
