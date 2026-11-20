#!/usr/bin/env bash
# zlib: compression library (libarchive needs it). Optimization: size. Versions: versions.sh.
# Run by build-all.sh; can also be run alone with DEPS_PREFIX set.
DEP_OPTIMIZE=size
source "$(dirname "${BASH_SOURCE[0]}")/common.sh"

build_zlib() {
  local src
  src=$(fetch_tag zlib "$ZLIB_REPO" "$ZLIB_TAG")
  if [[ $IS_WINDOWS == 1 ]]; then
    # zlib's configure script is for Unix. Its CMake build installs static libraries and config files.
    cmake_dep zlib "$src" -DZLIB_BUILD_TESTING=OFF
  else
    (cd "$src" && ./configure --static --prefix="$DEPS_PREFIX" && make -j"$JOBS" && make install)
  fi
}

dep_main zlib build_zlib
