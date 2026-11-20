#!/usr/bin/env bash
# json: JSON (header-only use). Optimization: size. Versions: versions.sh.
# Run by build-all.sh; can also be run alone with DEPS_PREFIX set.
DEP_OPTIMIZE=size
source "$(dirname "${BASH_SOURCE[0]}")/common.sh"

build_json() {
  local src
  src=$(fetch_tag json "$JSON_REPO" "$JSON_TAG")
  cmake_dep json "$src" -DJSON_BuildTests=OFF -DJSON_Install=ON
}

dep_main json build_json
