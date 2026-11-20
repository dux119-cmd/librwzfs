#!/usr/bin/env bash
# flac: FLAC decoder (dwarfs compression), with the C++ wrappers. Optimization: size. Versions: versions.sh.
# Run by build-all.sh; can also be run alone with DEPS_PREFIX set.
DEP_OPTIMIZE=size
source "$(dirname "${BASH_SOURCE[0]}")/common.sh"

build_flac() {
  local src
  src=$(fetch_tag flac "$FLAC_REPO" "$FLAC_TAG")
  cmake_dep flac "$src" -DBUILD_CXXLIBS=ON -DBUILD_PROGRAMS=OFF -DBUILD_EXAMPLES=OFF \
    -DBUILD_TESTING=OFF -DBUILD_DOCS=OFF -DINSTALL_MANPAGES=OFF -DWITH_OGG=OFF
}

dep_main flac build_flac
