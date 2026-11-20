#!/usr/bin/env bash
# zstd: compression (speed-optimized: it is on the hot path). Optimization: speed. Versions: versions.sh.
# Run by build-all.sh; can also be run alone with DEPS_PREFIX set.
DEP_OPTIMIZE=speed
source "$(dirname "${BASH_SOURCE[0]}")/common.sh"

build_zstd() {
  local src
  src=$(fetch_tag zstd "$ZSTD_REPO" "$ZSTD_TAG")
  cmake_dep zstd "$src/build/cmake" \
    -DZSTD_BUILD_STATIC=ON -DZSTD_BUILD_SHARED=OFF -DZSTD_BUILD_PROGRAMS=OFF \
    -DZSTD_BUILD_TESTS=OFF -DZSTD_LEGACY_SUPPORT=OFF -DZSTD_MULTITHREAD_SUPPORT=ON
}

dep_main zstd build_zstd
