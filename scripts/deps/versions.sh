#!/usr/bin/env bash
# Pinned versions of every managed dependency, dwarfs included. This is the only place a version
# changes. Sourced by common.sh; not run directly.
#
# Git-pinned libraries: <NAME>_REPO is a GitHub owner/name, <NAME>_TAG a tag. git verifies the objects
# on clone. <NAME>_VERSION is the version number written into generated .pc files (Windows).
# Changing any value changes the recipe: CMake rebuilds the managed dependencies.

ZLIB_REPO=madler/zlib;                    ZLIB_TAG=v1.3.1;          ZLIB_VERSION=1.3.1
ZSTD_REPO=facebook/zstd;                  ZSTD_TAG=v1.5.7;          ZSTD_VERSION=1.5.7
XXHASH_REPO=Cyan4973/xxHash;              XXHASH_TAG=v0.8.3;        XXHASH_VERSION=0.8.3
FMT_REPO=fmtlib/fmt;                      FMT_TAG=11.0.2
FLAC_REPO=xiph/flac;                      FLAC_TAG=1.4.3;           FLAC_VERSION=1.4.3
OPENSSL_REPO=openssl/openssl;             OPENSSL_TAG=openssl-3.4.1; OPENSSL_VERSION=3.4.1
LIBARCHIVE_REPO=libarchive/libarchive;    LIBARCHIVE_TAG=v3.7.7;    LIBARCHIVE_VERSION=3.7.7
UTFCPP_REPO=nemtrif/utfcpp;               UTFCPP_TAG=v4.0.6
JSON_REPO=nlohmann/json;                  JSON_TAG=v3.11.3
PHMAP_REPO=greg7mdp/parallel-hashmap;     PHMAP_TAG=v1.3.12
GTEST_REPO=google/googletest;             GTEST_TAG=v1.17.0
DWARFS_REPO=mhx/dwarfs;                   DWARFS_TAG=v0.15.5

# Release archives (not git): the URL, the file name and the SHA-256 are pinned here. A download whose
# hash does not match stops the build before anything is extracted.
BOOST_VERSION=1.92.0
BOOST_URL="https://github.com/boostorg/boost/releases/download/boost-${BOOST_VERSION}/boost-${BOOST_VERSION}-cmake.tar.xz"
BOOST_SHA256=9bed76128d4e46755dbe818487788c6fceb6f72b378f4daa49b7e1e600d9088d
JEMALLOC_VERSION=5.4.0
JEMALLOC_URL="https://github.com/jemalloc/jemalloc/releases/download/${JEMALLOC_VERSION}/jemalloc-${JEMALLOC_VERSION}.tar.bz2"
JEMALLOC_SHA256=200776fac271093e7c2f21edd6d62657ecd2be578d9328633f2a86bfa6ef4f1d
