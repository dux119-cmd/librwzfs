#!/usr/bin/env bash
# openssl: libcrypto only (dwarfs hashes with it). Optimization: size. Versions: versions.sh.
# Run by build-all.sh; can also be run alone with DEPS_PREFIX set.
DEP_OPTIMIZE=size
source "$(dirname "${BASH_SOURCE[0]}")/common.sh"

build_openssl() {
  # no-asm keeps the build independent of nasm.
  local src
  src=$(fetch_tag openssl "$OPENSSL_REPO" "$OPENSSL_TAG")
  if [[ $IS_WINDOWS == 1 ]]; then
    # MSVC: run from a developer shell (nmake on PATH) with a full perl (Strawberry Perl). Static libcrypto only.
    (cd "$src" && perl Configure VC-WIN64A --prefix="$DEPS_PREFIX" --openssldir="$DEPS_PREFIX/ssl" \
        no-shared no-asm no-tests no-docs no-apps no-ssl no-dso no-engine no-legacy \
      && nmake build_libs && nmake install_dev)
  else
    (cd "$src" && ./Configure --prefix="$DEPS_PREFIX" --openssldir="$DEPS_PREFIX/ssl" \
        -Os no-shared no-asm no-tests no-docs no-apps no-ssl no-dso no-engine no-legacy \
      && make -j"$JOBS" build_libs && make install_dev)
  fi
}

dep_main openssl build_openssl
