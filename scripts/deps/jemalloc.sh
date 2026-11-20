#!/usr/bin/env bash
# jemalloc: allocator (speed-optimized). Skipped on Windows: rwzfs uses the platform allocator there. Optimization: speed. Versions: versions.sh.
# Run by build-all.sh; can also be run alone with DEPS_PREFIX set.
DEP_OPTIMIZE=speed
source "$(dirname "${BASH_SOURCE[0]}")/common.sh"

build_jemalloc() {
  if [[ $IS_WINDOWS == 1 ]]; then
    echo "jemalloc skipped on Windows"
    return 0
  fi
  # Release tarball: it ships the generated configure script. Only the static library is built.
  # jemalloc keeps default symbol visibility: it must export malloc, free and friends.
  fetch_archive "$JEMALLOC_URL" "jemalloc-$JEMALLOC_VERSION.tar.bz2" bz2
  (cd "$DEPS_WORK/jemalloc-$JEMALLOC_VERSION" && ./configure --prefix="$DEPS_PREFIX" \
      --disable-shared --enable-static --disable-cxx --disable-doc \
    && make -j"$JOBS" build_lib_static \
    && make install_lib_static install_include install_lib_pc)
}

dep_main jemalloc build_jemalloc
