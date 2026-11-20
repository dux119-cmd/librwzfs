#!/usr/bin/env bash
# fmt: formatting. Optimization: size. Versions: versions.sh.
# Run by build-all.sh; can also be run alone with DEPS_PREFIX set.
DEP_OPTIMIZE=size
source "$(dirname "${BASH_SOURCE[0]}")/common.sh"

build_fmt() {
  local src
  src=$(fetch_tag fmt "$FMT_REPO" "$FMT_TAG")
  cmake_dep fmt "$src" -DFMT_TEST=OFF -DFMT_DOC=OFF -DFMT_INSTALL=ON
}

dep_main fmt build_fmt
