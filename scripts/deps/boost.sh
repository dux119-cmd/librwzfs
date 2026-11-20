#!/usr/bin/env bash
# boost: the libraries dwarfs includes (the CMake release ships package config files). Optimization: size. Versions: versions.sh.
# Run by build-all.sh; can also be run alone with DEPS_PREFIX set.
DEP_OPTIMIZE=size
source "$(dirname "${BASH_SOURCE[0]}")/common.sh"

build_boost() {
  # Boost builds only the listed libraries: compiled (chrono, filesystem, program_options, process) and
  # header-only (the rest). This is the list dwarfs includes.
  local src
  fetch_archive "$BOOST_URL" "boost-$BOOST_VERSION-cmake.tar.xz" xz "$BOOST_SHA256"
  src="$DEPS_WORK/boost-$BOOST_VERSION"
  cmake_dep boost "$src" \
    -DBOOST_INCLUDE_LIBRARIES="chrono;filesystem;program_options;process;convert;range;iterator;container_hash;algorithm;uuid;core;container;config;compat;align;accumulators;asio;assert" \
    -DBOOST_ENABLE_PYTHON=OFF -DBUILD_TESTING=OFF
}

dep_main boost build_boost
