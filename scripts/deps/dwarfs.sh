#!/usr/bin/env bash
# dwarfs (libdwarfs: reader and writer), built against the dependencies in DEPS_PREFIX. Installs into
# DWARFS_PREFIX, which defaults to DEPS_PREFIX: one prefix for everything, so CMake finds it all.
# Versions: versions.sh (DWARFS_TAG). Run last by build-all.sh.
#
# Feature set: zstd and FLAC compression only. LZ4, LZMA, Brotli, RICEPP, FUSE, man pages and desktop
# integration are off. The command-line tools are built and installed to $DWARFS_PREFIX/bin: mkdwarfs
# (create archives by hand), dwarfsck (check and inspect them), dwarfsextract (unpack them). rwzfs itself
# does not use the tools; it writes archives through the library.
DEP_OPTIMIZE=size
source "$(dirname "${BASH_SOURCE[0]}")/common.sh"

DWARFS_PREFIX="$(to_native "${DWARFS_PREFIX:-$DEPS_PREFIX}")"

build_dwarfs() {
  local src="$DEPS_WORK/dwarfs-src" build="$DEPS_WORK/dwarfs-build"
  local opt_flags="-Os -DNDEBUG -ffunction-sections -fdata-sections"
  local msvc_args=()
  if [[ $IS_WINDOWS == 1 ]]; then
    # FLAC__NO_DLL: FLAC is linked statically. Passing CMAKE_CXX_FLAGS replaces CMake's MSVC defaults
    # (which include /EHsc), so -EHsc is set here explicitly.
    msvc_args=(${MSVC_RUNTIME[@]+"${MSVC_RUNTIME[@]}"} -DCMAKE_C_FLAGS=-DFLAC__NO_DLL
               -DCMAKE_CXX_FLAGS="-EHsc -DFLAC__NO_DLL")
    opt_flags=""  # MSVC: MinSizeRel selects size optimization; no GCC flags
  fi
  # dwarfs keeps its own flags (no hidden visibility), as it always has.
  export CFLAGS="$opt_flags" CXXFLAGS="$opt_flags"

  src=$(fetch_tag dwarfs-src "$DWARFS_REPO" "$DWARFS_TAG")
  rm -rf "$build"

  # Apple's libc++ lacks std::chrono::from_stream, which dwarfs's timestamp parser uses.
  if [[ $IS_DARWIN == 1 ]]; then
    python3 "$DEPS_SCRIPTS_DIR/patch-dwarfs-apple.py" "$src"
  fi

  cmake -S "$src" -B "$build" ${GEN[@]+"${GEN[@]}"} \
    -DCMAKE_POLICY_VERSION_MINIMUM=3.5 \
    ${msvc_args[@]+"${msvc_args[@]}"} \
    -DCMAKE_BUILD_TYPE=MinSizeRel \
    -DCMAKE_PREFIX_PATH="$DEPS_PREFIX" \
    -DCMAKE_INSTALL_PREFIX="$DWARFS_PREFIX" \
    -DCMAKE_FIND_LIBRARY_SUFFIXES="$LIB_SUFFIX" \
    -DCMAKE_POSITION_INDEPENDENT_CODE=ON \
    -DWITH_LIBDWARFS=ON -DWITH_TOOLS=ON \
    -DWITH_TESTS=OFF -DWITH_BENCHMARKS=OFF -DWITH_FUZZ=OFF -DWITH_EXAMPLE=OFF -DWITH_DEV_TOOLS=OFF \
    -DWITH_FUSE_DRIVER=OFF -DWITH_FUSE_EXTRACT_BINARY=OFF -DWITH_LEGACY_FUSE=OFF \
    -DWITH_DESKTOP_INTEGRATION=OFF -DWITH_MAN_PAGES=OFF -DWITH_MAN_OPTION=OFF -DWITH_PXATTR=OFF \
    -DWITH_UNIVERSAL_BINARY=OFF \
    -DTRY_ENABLE_FLAC=ON \
    -DTRY_ENABLE_LZ4=OFF -DTRY_ENABLE_LZMA=OFF -DTRY_ENABLE_BROTLI=OFF -DENABLE_RICEPP=OFF \
    -DUSE_JEMALLOC=OFF -DUSE_MIMALLOC=OFF -DENABLE_PERFMON=OFF \
    -DDISABLE_FILESYSTEM_EXTRACTOR_FORMAT=ON \
    -DENABLE_STACKTRACE=OFF -DDISABLE_CCACHE=ON
  cmake --build "$build" --parallel "$JOBS"
  cmake --install "$build"

  # The tools are part of this build: a missing one means the install is incomplete.
  local tool
  for tool in mkdwarfs dwarfsck dwarfsextract; do
    local exe="$DWARFS_PREFIX/bin/$tool"
    [[ $IS_WINDOWS == 1 ]] && exe="$exe.exe"
    [[ -f "$exe" ]] || { echo "error: $tool was not installed to $DWARFS_PREFIX/bin" >&2; exit 1; }
  done

  local lib cand found
  for lib in libdwarfs_reader libdwarfs_writer libdwarfs_common libdwarfs_decompressor libdwarfs_compressor; do
    # MSVC names static libraries without the lib prefix only in some configurations; accept both.
    found=0
    for cand in "$DWARFS_PREFIX/lib/$lib$LIB_SUFFIX" "$DWARFS_PREFIX/lib/$lib.lib" "$DWARFS_PREFIX/lib/${lib#lib}$LIB_SUFFIX"; do
      [[ -f "$cand" ]] && found=1 && break
    done
    [[ $found == 1 ]] || { echo "error: missing $DWARFS_PREFIX/lib/$lib$LIB_SUFFIX" >&2; exit 1; }
  done
  test -f "$DWARFS_PREFIX/include/dwarfs/writer/filesystem_writer.h" || {
    echo "error: dwarfs headers are incomplete under $DWARFS_PREFIX/include" >&2
    exit 1
  }
  echo "dwarfs installed under $DWARFS_PREFIX (tools in $DWARFS_PREFIX/bin)"
}

dep_main dwarfs build_dwarfs
