#!/usr/bin/env bash
# phmap: hash maps (header-only use). Optimization: size. Versions: versions.sh.
# Run by build-all.sh; can also be run alone with DEPS_PREFIX set.
DEP_OPTIMIZE=size
source "$(dirname "${BASH_SOURCE[0]}")/common.sh"

build_phmap() {
  local src
  src=$(fetch_tag phmap "$PHMAP_REPO" "$PHMAP_TAG")
  cmake_dep phmap "$src" -DPHMAP_BUILD_TESTS=OFF -DPHMAP_BUILD_EXAMPLES=OFF
}

dep_main phmap build_phmap
