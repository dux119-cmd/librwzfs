#!/usr/bin/env bash
# utfcpp: UTF-8 helpers. Optimization: size. Versions: versions.sh.
# Run by build-all.sh; can also be run alone with DEPS_PREFIX set.
DEP_OPTIMIZE=size
source "$(dirname "${BASH_SOURCE[0]}")/common.sh"

build_utfcpp() {
  local src
  src=$(fetch_tag utfcpp "$UTFCPP_REPO" "$UTFCPP_TAG")
  cmake_dep utfcpp "$src" -DUTF8_TESTS=OFF -DUTF8_SAMPLES=OFF
}

dep_main utfcpp build_utfcpp
