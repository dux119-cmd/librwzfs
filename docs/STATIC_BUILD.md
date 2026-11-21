# Managed dependencies

rwzfs builds dwarfs and its dependencies from pinned sources, as static, size-optimized libraries, into
one prefix: `build-deps/`, beside `build/`. This is the default (`RWZFS_USE_MANAGED_DEPS=ON`).

Because the prefix sits outside `build/`, every build directory shares it (Release, Debug, sanitizer,
tests), and cleaning `build/` keeps it. The libraries are built once.

## Layout

    scripts/deps/
      versions.sh        every pinned version (the only place a version changes)
      common.sh          shared actions: flags, fetch, cmake install, stamps, dry run, .pc shims
      zlib.sh zstd.sh xxhash.sh fmt.sh flac.sh openssl.sh libarchive.sh boost.sh
      utfcpp.sh json.sh phmap.sh jemalloc.sh googletest.sh dwarfs.sh
                         one builder per dependency: sets its optimization level, sources common.sh,
                         and registers its build with dep_main
      windows-shims.sh   Windows only: .pc files for libraries whose MSVC installs lack one
      build-all.sh       runs each builder in dependency order (dwarfs last)
      patch-dwarfs-apple.py   the one source patch dwarfs needs on Apple's libc++

CMake runs `build-all.sh` when a managed build is configured and the recipe has changed. The recipe is
a hash of every file in `scripts/deps`, so any change to a version or an action rebuilds everything.

## The dwarfs tools

The dwarfs build also installs its command-line tools to `build-deps/bin`: `mkdwarfs` (build an archive
from a directory by hand), `dwarfsck` (check and inspect an archive) and `dwarfsextract` (unpack one). Use
them to make or verify archives outside rwzfs. rwzfs does not need them: it writes archives in-process.
The FUSE mount binary is not built (it needs FUSE).

## Use

    cmake -S . -B build                          # managed dependencies, built on first use
    cmake -S . -B build -DRWZFS_USE_MANAGED_DEPS=OFF -DDWARFS_PREFIX=/usr/local   # system libraries

To build the dependencies by hand, without CMake:

    DEPS_PREFIX=$PWD/build-deps bash scripts/deps/build-all.sh

Options (environment, for `build-all.sh` or any single step):

| variable | meaning |
|---|---|
| `DEPS_PREFIX` | install prefix (default `build-deps/`) |
| `DEPS_WORK` | scratch area (default `$DEPS_PREFIX/work`), removed on success unless `KEEP_WORK=1` |
| `JOBS` | parallel jobs |
| `WITH_TESTS=1` | also build googletest |
| `JEMALLOC=0` | skip jemalloc (rwzfs then uses the platform allocator) |
| `ONLY=zstd,fmt` | build only the listed steps |
| `DRY_RUN=1` | print the plan and build nothing |

A single step can be run alone, for example `DEPS_PREFIX=... bash scripts/deps/zstd.sh`. It finds its
versions and the shared actions, and skips itself if its stamp exists in `$DEPS_PREFIX/.stamps`.

## Changing a version

Edit `versions.sh`. The next configure sees a different recipe and rebuilds the managed dependencies
from scratch. To force a rebuild without a version change, delete `build-deps/`.

## Notes

- Speed-optimized libraries (zstd, xxHash, jemalloc) use `-Ofast`. The rest use `-Os`. dwarfs keeps its
  own flags, as it always has.
- Windows uses MSVC with the static C runtime. The scripts run under Git for Windows' bash; CMake
  looks for that bash first, because a `bash` on PATH may be WSL's.
- jemalloc is linked by rwzfs by default (`RWZFS_USE_JEMALLOC`). It replaces malloc in the final
  executable, so it is process-wide.
- Building takes a long time on one CPU. Keep `build-deps/` between builds.
- Release archives (Boost, jemalloc) are pinned by SHA-256 in `versions.sh`. `fetch_archive` checks the
  hash before extracting, and deletes a mismatched download. Git-cloned sources are pinned by tag.
