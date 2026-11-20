#!/usr/bin/env bash
# googletest: test framework (only with WITH_TESTS=1). Optimization: size. Versions: versions.sh.
# Run by build-all.sh; can also be run alone with DEPS_PREFIX set.
DEP_OPTIMIZE=size
source "$(dirname "${BASH_SOURCE[0]}")/common.sh"

build_googletest() {
  local src
  src=$(fetch_tag googletest "$GTEST_REPO" "$GTEST_TAG")
  cmake_dep googletest "$src" -DBUILD_GMOCK=OFF -DINSTALL_GTEST=ON
}

dep_main googletest build_googletest
