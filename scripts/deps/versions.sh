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

# Release archives (not git): the URL and the file name are built from the version.
BOOST_VERSION=1.90.0
BOOST_URL="https://github.com/boostorg/boost/releases/download/boost-${BOOST_VERSION}/boost-${BOOST_VERSION}-cmake.tar.gz"
JEMALLOC_VERSION=5.3.0
JEMALLOC_URL="https://github.com/jemalloc/jemalloc/releases/download/${JEMALLOC_VERSION}/jemalloc-${JEMALLOC_VERSION}.tar.bz2"
