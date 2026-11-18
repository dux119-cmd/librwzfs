#!/usr/bin/env bash
# Builds dwarfs (libdwarfs: reader and writer) as static, size-optimized libraries under $DWARFS_PREFIX,
# against the static dependencies that build-deps.sh installed in $DEPS_PREFIX.
#
#   DEPS_PREFIX    dependency prefix from build-deps.sh (required)
#   DWARFS_PREFIX  install prefix (default: ./dwarfs-install)
#   DWARFS_REF     dwarfs git tag (default: v0.15.5)
#   DWARFS_WORK    scratch area for the source and build tree (default: ./dwarfs-work)
#   JOBS           parallel jobs (default: number of CPUs)
#   CC, CXX        compilers (default: the platform default)
#
# Feature set: zstd and FLAC compression only. LZ4, LZMA, Brotli, RICEPP, FUSE, man pages and
# desktop integration are off. The command-line tools are off too: rwzfs writes archives through
# the library, so no mkdwarfs binary is needed. (jemalloc is built by build-deps.sh and linked by rwzfs.)
set -euo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
case "$(uname -s)" in
  MINGW*|MSYS*|CYGWIN*) IS_WINDOWS=1 ;;
  *) IS_WINDOWS=0 ;;
esac
# Windows (Git Bash or MSYS, MSVC toolchain): CMake needs mixed Windows paths.
to_native() { if [[ $IS_WINDOWS == 1 ]] && command -v cygpath >/dev/null 2>&1; then cygpath -m "$1"; else printf '%s' "$1"; fi; }

DEPS_PREFIX="$(to_native "${DEPS_PREFIX:?set DEPS_PREFIX to the dependency prefix from build-deps.sh}")"
DWARFS_PREFIX="$(to_native "${DWARFS_PREFIX:-$PWD/dwarfs-install}")"
DWARFS_REF="${DWARFS_REF:-v0.15.5}"
DWARFS_WORK="$(to_native "${DWARFS_WORK:-$PWD/dwarfs-work}")"
JOBS="${JOBS:-$( (nproc 2>/dev/null) || (sysctl -n hw.ncpu 2>/dev/null) || echo 2)}"

if [[ $IS_WINDOWS == 1 ]]; then
  # MSVC, never MinGW: CMake must not pick up a GCC from PATH.
  export CC="${CC:-cl}" CXX="${CXX:-cl}"
  OPT=""                       # MSVC: MinSizeRel selects size optimization; no GCC flags
  # The same static C runtime as the dependencies. FLAC__NO_DLL: FLAC is linked statically, so its
  # symbols must not be imported as DLL exports. Passing CMAKE_CXX_FLAGS replaces CMake's MSVC
  # defaults (which include /EHsc), so -EHsc is set here explicitly.
  MSVC_ARGS=(-DCMAKE_POLICY_DEFAULT_CMP0091=NEW -DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded
             -DCMAKE_C_FLAGS=-DFLAC__NO_DLL -DCMAKE_CXX_FLAGS="-EHsc -DFLAC__NO_DLL")
  LIB_SUFFIX=".lib"            # static libraries on Windows
else
  OPT="-Os -DNDEBUG -ffunction-sections -fdata-sections"
  LIB_SUFFIX=".a"
fi
export CFLAGS="$OPT"
export CXXFLAGS="$OPT"
export PKG_CONFIG_LIBDIR="$DEPS_PREFIX/lib/pkgconfig:$DEPS_PREFIX/lib64/pkgconfig"

GEN=()
if command -v ninja >/dev/null 2>&1; then GEN=(-G Ninja); fi

src="$DWARFS_WORK/src"
build="$DWARFS_WORK/build"
rm -rf "$src" "$build"
mkdir -p "$DWARFS_WORK"
git clone --depth 1 --branch "$DWARFS_REF" https://github.com/mhx/dwarfs.git "$src" >/dev/null 2>&1 || {
  echo "error: cannot clone dwarfs at $DWARFS_REF" >&2
  exit 1
}

# Apple's libc++ lacks std::chrono::from_stream, which dwarfs's timestamp parser uses.
if [[ "$(uname -s)" == Darwin ]]; then
  python3 "$HERE/patch-dwarfs-apple.py" "$src"
fi

cmake -S "$src" -B "$build" "${GEN[@]}" \
  -DCMAKE_POLICY_VERSION_MINIMUM=3.5 \
  ${MSVC_ARGS[@]+"${MSVC_ARGS[@]}"} \
  -DCMAKE_BUILD_TYPE=MinSizeRel \
  -DCMAKE_PREFIX_PATH="$DEPS_PREFIX" \
  -DCMAKE_INSTALL_PREFIX="$DWARFS_PREFIX" \
  -DCMAKE_FIND_LIBRARY_SUFFIXES="$LIB_SUFFIX" \
  -DCMAKE_POSITION_INDEPENDENT_CODE=ON \
  -DWITH_LIBDWARFS=ON -DWITH_TOOLS=OFF \
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

rm -rf "$DWARFS_WORK"
echo "dwarfs installed under $DWARFS_PREFIX"
