#!/usr/bin/env bash
# libarchive: archive reader core (dwarfs requires it); only the zlib codec. Optimization: size. Versions: versions.sh.
# Run by build-all.sh; can also be run alone with DEPS_PREFIX set.
DEP_OPTIMIZE=size
source "$(dirname "${BASH_SOURCE[0]}")/common.sh"

build_libarchive() {
  # Keep only the reader core: no extra formats or compressors. zlib is the one codec it needs.
  local src
  src=$(fetch_tag libarchive "$LIBARCHIVE_REPO" "$LIBARCHIVE_TAG")
  cmake_dep libarchive "$src" \
    -DENABLE_TAR=OFF -DENABLE_CPIO=OFF -DENABLE_CAT=OFF -DENABLE_TEST=OFF -DENABLE_INSTALL=ON \
    -DENABLE_ICONV=OFF -DENABLE_LIBXML2=OFF -DENABLE_EXPAT=OFF -DENABLE_PCREPOSIX=OFF \
    -DENABLE_PCRE2POSIX=OFF -DENABLE_LZMA=OFF -DENABLE_BZip2=OFF -DENABLE_LZ4=OFF \
    -DENABLE_ZSTD=OFF -DENABLE_NETTLE=OFF -DENABLE_OPENSSL=OFF -DENABLE_MBEDTLS=OFF \
    -DENABLE_CNG=OFF -DENABLE_ACL=OFF -DENABLE_XATTR=OFF -DENABLE_ZLIB=ON
}

dep_main libarchive build_libarchive
