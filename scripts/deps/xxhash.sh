#!/usr/bin/env bash
# xxhash: hashing (speed-optimized). Optimization: speed. Versions: versions.sh.
# Run by build-all.sh; can also be run alone with DEPS_PREFIX set.
DEP_OPTIMIZE=speed
source "$(dirname "${BASH_SOURCE[0]}")/common.sh"

build_xxhash() {
  local src
  src=$(fetch_tag xxhash "$XXHASH_REPO" "$XXHASH_TAG")
  cmake_dep xxhash "$src/cmake_unofficial" -DXXH_BUILD_XXHSUM=OFF
}

dep_main xxhash build_xxhash
