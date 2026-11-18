#!/usr/bin/env bash
# Builds rwzfs's third-party dependencies as static, size-optimized libraries under $DEPS_PREFIX.
#
#   DEPS_PREFIX   install prefix (default: ./deps-install)
#   DEPS_WORK     scratch area for sources and build trees (default: ./deps-work)
#   JOBS          parallel jobs (default: number of CPUs)
#   WITH_TESTS    1 to also build googletest (for rwzfs's own tests)
#   ONLY          comma list of dependencies to build (default: all), e.g. ONLY=zstd,fmt
#   KEEP_WORK     1 to keep DEPS_WORK after a successful run (default: 0)
#   JEMALLOC      0 to skip jemalloc (default: 1). rwzfs links it for allocation speed and memory
#
# Each dependency leaves a stamp in $DEPS_PREFIX/.stamps and is skipped on a rerun.
# Versions are pinned by git tag; git verifies the tag's objects on clone.
set -euo pipefail

# Windows (Git Bash or MSYS) runs this script with MSVC: prefixes must be mixed Windows paths for
# CMake, and the Unix-only optimization flags are not passed to cl.exe.
case "$(uname -s)" in
  MINGW*|MSYS*|CYGWIN*) IS_WINDOWS=1 ;;
  *) IS_WINDOWS=0 ;;
esac
to_native() { if [[ $IS_WINDOWS == 1 ]] && command -v cygpath >/dev/null 2>&1; then cygpath -m "$1"; else printf '%s' "$1"; fi; }

if [[ $IS_WINDOWS == 1 ]]; then
  # Git Bash's login profile puts /usr/bin (Git's perl) first, and OpenSSL's Configure needs a
  # complete perl. Put Strawberry Perl first, then check it can load the modules Configure uses.
  # Only perl goes first on PATH. Strawberry's own gcc (c\bin) must not be used: MinGW lacks
  # headers Boost.Process needs (STARTUPINFOEXW), and the dependencies are built with MSVC.
  if [[ -x /c/Strawberry/perl/bin/perl.exe ]]; then
    export PATH="/c/Strawberry/perl/bin:$PATH"
  fi
  # Pin the compiler to MSVC so CMake never selects a MinGW compiler found elsewhere on PATH.
  export CC="${CC:-cl}" CXX="${CXX:-cl}"
  perl -MLocale::Maketext::Simple -e 1 2>/dev/null || {
    echo "error: perl lacks Locale::Maketext::Simple (install Strawberry Perl, or put a full perl first on PATH)" >&2
    exit 1
  }
fi

DEPS_PREFIX="$(to_native "${DEPS_PREFIX:-$PWD/deps-install}")"
DEPS_WORK="$(to_native "${DEPS_WORK:-$PWD/deps-work}")"
JOBS="${JOBS:-$( (nproc 2>/dev/null) || (sysctl -n hw.ncpu 2>/dev/null) || echo 2)}"
WITH_TESTS="${WITH_TESTS:-0}"
ONLY="${ONLY:-}"
KEEP_WORK="${KEEP_WORK:-0}"

# Size-optimized code, one section per function and object so the linker can drop what is unused.
# Two flag sets. Size (default): -Os, one section per function, hidden symbols. Speed (for the
# performance-critical libraries: zstd, xxHash, jemalloc): -Ofast. Visibility and sections do not
# affect speed, so both sets keep them, except jemalloc, which must export malloc and friends.
if [[ $IS_WINDOWS == 1 ]]; then
  STATIC_SUFFIX=".lib"
  # One C runtime for every library and the consumers: static (/MT). dwarfs already uses it, and a
  # mix with the DLL runtime fails to link. CMP0091=NEW makes the variable apply to dependencies
  # that declare old CMake minimums.
  MSVC_RUNTIME=(-DCMAKE_POLICY_DEFAULT_CMP0091=NEW -DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded)
else
  STATIC_SUFFIX=".a"
  MSVC_RUNTIME=()
fi
if [[ $IS_WINDOWS == 1 ]]; then
  # MSVC: MinSizeRel selects size optimization, Release selects speed. No GCC/Clang flags.
  SIZE_CFLAGS=""
  SIZE_CXXFLAGS=""
  SPEED_CFLAGS=""
  SPEED_CXXFLAGS=""
  SIZE_BUILD_TYPE=MinSizeRel
  SPEED_BUILD_TYPE=Release
  SPEED_OVERRIDES=()
else
  SIZE_CFLAGS="-Os -DNDEBUG -ffunction-sections -fdata-sections -fvisibility=hidden"
  SIZE_CXXFLAGS="$SIZE_CFLAGS -fvisibility-inlines-hidden"
  SPEED_CFLAGS="-Ofast -DNDEBUG -ffunction-sections -fdata-sections -fvisibility=hidden"
  SPEED_CXXFLAGS="$SPEED_CFLAGS -fvisibility-inlines-hidden"
  SIZE_BUILD_TYPE=MinSizeRel
  SPEED_BUILD_TYPE=Release
  # CMake appends its own -O3 after CFLAGS, which would win over -Ofast. Clear the Release flags so
  # the speed flags are the only optimization settings. (Windows keeps /O2 from Release.)
  SPEED_OVERRIDES=(-DCMAKE_C_FLAGS_RELEASE=-DNDEBUG -DCMAKE_CXX_FLAGS_RELEASE=-DNDEBUG)
fi
export CFLAGS="$SIZE_CFLAGS"
export CXXFLAGS="$SIZE_CXXFLAGS"
# Only pkg-config files from this prefix are seen. Otherwise a .pc file that says -lcrypto or -lzstd
# lets the linker pick the system shared library over the static one built here.
export PKG_CONFIG_LIBDIR="$DEPS_PREFIX/lib/pkgconfig:$DEPS_PREFIX/lib64/pkgconfig"

# GNU tar on Git Bash reads "D:/..." as host:path and tries to connect to a remote host. Windows
# needs --force-local; bsdtar (macOS) does not know that option, so it is passed only on Windows.
TAR_LOCAL=()
if [[ $IS_WINDOWS == 1 ]]; then TAR_LOCAL=(--force-local); fi

mkdir -p "$DEPS_PREFIX/.stamps" "$DEPS_WORK"
GEN=()
if command -v ninja >/dev/null 2>&1; then GEN=(-G Ninja); fi

# Shared cmake flags: static libraries only, minimum-size build type, position-independent code so
# the libraries can be linked into any executable or shared object.
CMAKE_STATIC=(
  # CMake 4 removed compatibility with cmake_minimum_required < 3.5. Several pinned dependencies
  # still declare older minimums, so accept them.
  -DCMAKE_POLICY_VERSION_MINIMUM=3.5
  -DCMAKE_BUILD_TYPE=${BUILD_TYPE-$SIZE_BUILD_TYPE}
  -DBUILD_SHARED_LIBS=OFF
  -DCMAKE_POSITION_INDEPENDENT_CODE=ON
  -DCMAKE_FIND_LIBRARY_SUFFIXES="$STATIC_SUFFIX"
  ${MSVC_RUNTIME[@]+"${MSVC_RUNTIME[@]}"}
  -DCMAKE_INSTALL_PREFIX="$DEPS_PREFIX"
  -DCMAKE_PREFIX_PATH="$DEPS_PREFIX"
)

log() { printf '\n==== %s ====\n' "$*"; }

wanted() {
  [[ -z "$ONLY" ]] || [[ ",$ONLY," == *",$1,"* ]]
}

done_already() { [[ -f "$DEPS_PREFIX/.stamps/$1" ]]; }
mark_done() { touch "$DEPS_PREFIX/.stamps/$1"; }

# git clone of a pinned tag into $DEPS_WORK/<name>.
fetch_tag() { # name repo tag
  local dir="$DEPS_WORK/$1"
  rm -rf "$dir"
  git clone --depth 1 --branch "$3" "https://github.com/$2.git" "$dir" >/dev/null 2>&1 || {
    echo "error: cannot clone $2 at $3" >&2
    exit 1
  }
  echo "$dir"
}

# cmake configure, build and install for one dependency.
cmake_dep() { # name source-dir [extra cmake args...]
  local name=$1 src=$2
  shift 2
  cmake -S "$src" -B "$DEPS_WORK/$name-build" "${GEN[@]}" "${CMAKE_STATIC[@]}" \
    ${CONFIG_OVERRIDES[@]+"${CONFIG_OVERRIDES[@]}"} "$@"
  cmake --build "$DEPS_WORK/$name-build" --parallel "$JOBS"
  cmake --install "$DEPS_WORK/$name-build"
}

build_zlib() {
  local src
  src=$(fetch_tag zlib madler/zlib v1.3.1)
  if [[ $IS_WINDOWS == 1 ]]; then
    # zlib's configure script is for Unix. Its CMake build installs static libraries and config files.
    cmake_dep zlib "$src" -DZLIB_BUILD_TESTING=OFF
  else
    (cd "$src" && ./configure --static --prefix="$DEPS_PREFIX" && make -j"$JOBS" && make install)
  fi
}

build_zstd() {
  local src
  src=$(fetch_tag zstd facebook/zstd v1.5.7)
  cmake_dep zstd "$src/build/cmake" \
    -DZSTD_BUILD_STATIC=ON -DZSTD_BUILD_SHARED=OFF -DZSTD_BUILD_PROGRAMS=OFF \
    -DZSTD_BUILD_TESTS=OFF -DZSTD_LEGACY_SUPPORT=OFF -DZSTD_MULTITHREAD_SUPPORT=ON
}

build_xxhash() {
  local src
  src=$(fetch_tag xxhash Cyan4973/xxHash v0.8.3)
  cmake_dep xxhash "$src/cmake_unofficial" -DXXH_BUILD_XXHSUM=OFF
}

build_fmt() {
  local src
  src=$(fetch_tag fmt fmtlib/fmt 11.0.2)
  cmake_dep fmt "$src" -DFMT_TEST=OFF -DFMT_DOC=OFF -DFMT_INSTALL=ON
}

build_flac() {
  local src
  src=$(fetch_tag flac xiph/flac 1.4.3)
  cmake_dep flac "$src" -DBUILD_CXXLIBS=ON -DBUILD_PROGRAMS=OFF -DBUILD_EXAMPLES=OFF \
    -DBUILD_TESTING=OFF -DBUILD_DOCS=OFF -DINSTALL_MANPAGES=OFF -DWITH_OGG=OFF
}

build_openssl() {
  # Only libcrypto is needed (dwarfs hashes with it). no-asm keeps the build independent of nasm.
  local src
  src=$(fetch_tag openssl openssl/openssl openssl-3.4.1)
  if [[ $IS_WINDOWS == 1 ]]; then
    # MSVC: run from a developer shell (nmake on PATH) with a full perl (Strawberry Perl, not the
    # Git for Windows perl, which lacks modules OpenSSL's Configure needs). Static libcrypto only.
    (cd "$src" && perl Configure VC-WIN64A --prefix="$DEPS_PREFIX" --openssldir="$DEPS_PREFIX/ssl" \
        no-shared no-asm no-tests no-docs no-apps no-ssl no-dso no-engine no-legacy \
      && nmake build_libs && nmake install_dev)
  else
    (cd "$src" && ./Configure --prefix="$DEPS_PREFIX" --openssldir="$DEPS_PREFIX/ssl" \
        -Os no-shared no-asm no-tests no-docs no-apps no-ssl no-dso no-engine no-legacy \
      && make -j"$JOBS" build_libs && make install_dev)
  fi
}

build_libarchive() {
  # dwarfs requires libarchive unconditionally. Keep only the reader core; no extra formats or
  # compressors. zlib is the one codec it needs, built above.
  local src
  src=$(fetch_tag libarchive libarchive/libarchive v3.7.7)
  cmake_dep libarchive "$src" \
    -DENABLE_TAR=OFF -DENABLE_CPIO=OFF -DENABLE_CAT=OFF -DENABLE_TEST=OFF -DENABLE_INSTALL=ON \
    -DENABLE_ICONV=OFF -DENABLE_LIBXML2=OFF -DENABLE_EXPAT=OFF -DENABLE_PCREPOSIX=OFF \
    -DENABLE_PCRE2POSIX=OFF -DENABLE_LZMA=OFF -DENABLE_BZip2=OFF -DENABLE_LZ4=OFF \
    -DENABLE_ZSTD=OFF -DENABLE_NETTLE=OFF -DENABLE_OPENSSL=OFF -DENABLE_MBEDTLS=OFF \
    -DENABLE_CNG=OFF -DENABLE_ACL=OFF -DENABLE_XATTR=OFF -DENABLE_ZLIB=ON
}

build_boost() {
  # The CMake release of Boost ships the package config files dwarfs needs (find_package CONFIG).
  # The list is what dwarfs includes: compiled libraries (chrono, filesystem, program_options,
  # process) and header-only ones. Boost builds only what is listed.
  local url="https://github.com/boostorg/boost/releases/download/boost-1.90.0/boost-1.90.0-cmake.tar.gz"
  local tarball="$DEPS_WORK/boost-1.90.0-cmake.tar.gz"
  curl -fsSL "$url" -o "$tarball"
  tar ${TAR_LOCAL[@]+"${TAR_LOCAL[@]}"} -xzf "$tarball" -C "$DEPS_WORK"
  cmake_dep boost "$DEPS_WORK/boost-1.90.0" \
    -DBOOST_INCLUDE_LIBRARIES="chrono;filesystem;program_options;process;convert;range;iterator;container_hash;algorithm;uuid;core;container;config;compat;align;accumulators;asio;assert" \
    -DBOOST_ENABLE_PYTHON=OFF -DBUILD_TESTING=OFF
}

build_utfcpp() {
  local src
  src=$(fetch_tag utfcpp nemtrif/utfcpp v4.0.6)
  cmake_dep utfcpp "$src" -DUTF8_TESTS=OFF -DUTF8_SAMPLES=OFF
}

build_json() {
  local src
  src=$(fetch_tag json nlohmann/json v3.11.3)
  cmake_dep json "$src" -DJSON_BuildTests=OFF -DJSON_Install=ON
}

build_phmap() {
  local src
  src=$(fetch_tag phmap greg7mdp/parallel-hashmap v1.3.12)
  cmake_dep phmap "$src" -DPHMAP_BUILD_TESTS=OFF -DPHMAP_BUILD_EXAMPLES=OFF
}

build_jemalloc() {
  if [[ $IS_WINDOWS == 1 ]]; then
    # jemalloc is not built for MSVC here. rwzfs runs with the platform allocator on Windows.
    echo "jemalloc skipped on Windows"
    return 0
  fi
  # Release tarball: it ships the generated configure script, so no autotools are needed. Only the
  # static library is built; the C++ wrappers and the shared library are not.
  local tarball="$DEPS_WORK/jemalloc-5.3.0.tar.bz2"
  curl -fsSL "https://github.com/jemalloc/jemalloc/releases/download/5.3.0/jemalloc-5.3.0.tar.bz2" -o "$tarball"
  tar ${TAR_LOCAL[@]+"${TAR_LOCAL[@]}"} -xjf "$tarball" -C "$DEPS_WORK"
  # jemalloc keeps default symbol visibility: it must export malloc, free and friends.
  (cd "$DEPS_WORK/jemalloc-5.3.0" && ./configure --prefix="$DEPS_PREFIX" \
      --disable-shared --enable-static --disable-cxx --disable-doc \
    && make -j"$JOBS" build_lib_static \
    && make install_lib_static install_include install_lib_pc)
}

build_gtest() {
  local src
  src=$(fetch_tag googletest google/googletest v1.17.0)
  cmake_dep googletest "$src" -DBUILD_GMOCK=OFF -DINSTALL_GTEST=ON
}

# Build order matters: libarchive needs zlib.
DEPS=(zlib zstd xxhash fmt flac openssl libarchive boost utfcpp json phmap)
if [[ "${JEMALLOC:-1}" == 1 ]]; then DEPS+=(jemalloc); fi  # skipped on Windows (see build_jemalloc)
if [[ "$WITH_TESTS" == 1 ]]; then DEPS+=(gtest); fi

# Performance-critical libraries are built for speed. Each build runs in a subshell so the
# settings do not leak into the next dependency.
PERF_DEPS=(zstd xxhash jemalloc)

for dep in "${DEPS[@]}"; do
  wanted "$dep" || continue
  if done_already "$dep"; then
    echo "skip $dep (already built)"
    continue
  fi
  log "building $dep"
  if [[ " ${PERF_DEPS[*]} " == *" $dep "* ]]; then
    (
      export CFLAGS="$SPEED_CFLAGS" CXXFLAGS="$SPEED_CXXFLAGS" BUILD_TYPE="$SPEED_BUILD_TYPE"
      CONFIG_OVERRIDES=(${SPEED_OVERRIDES[@]+"${SPEED_OVERRIDES[@]}"})
      "build_$dep"
    )
  else
    "build_$dep"
  fi
  mark_done "$dep"
done

# Windows: dwarfs locates its libraries through pkg-config, and the MSVC installs of OpenSSL,
# libarchive, xxHash, zstd and FLAC do not always write .pc files. Write any that are missing,
# with absolute paths to the static libraries (a -l flag does not map onto the MSVC linker).
write_pc() { # name version lib-glob extra-libs cflags-extra requires
  local name=$1 version=$2 glob=$3 extra=${4:-} cflags_extra=${5:-} requires=${6:-}
  local pc="$DEPS_PREFIX/lib/pkgconfig/$name.pc"
  [[ -f "$pc" ]] && return 0
  local lib
  lib=$(cd "$DEPS_PREFIX/lib" 2>/dev/null && ls $glob 2>/dev/null | head -n 1 || true)
  if [[ -z "$lib" ]]; then
    echo "warning: no library matching $glob under $DEPS_PREFIX/lib; $name.pc not written" >&2
    return 0
  fi
  mkdir -p "$DEPS_PREFIX/lib/pkgconfig"
  cat > "$pc" << PC
prefix=$DEPS_PREFIX
includedir=\${prefix}/include
libdir=\${prefix}/lib

Name: $name
Description: $name (static, built by rwzfs scripts/deps)
Version: $version
Cflags: -I\${includedir} $cflags_extra
Libs: \${libdir}/$lib $extra
${requires:+Requires: $requires}
PC
}

if [[ $IS_WINDOWS == 1 ]]; then
  write_pc libcrypto 3.4.1 "libcrypto*.lib" "ws2_32.lib crypt32.lib advapi32.lib user32.lib"
  # Some consumers (dwarfs's zstd::preferred on MSVC) link the plain name zstd.lib. Provide it as a
  # copy of the static library, if the zstd build named it differently.
  for base in zstd xxhash; do
    if [[ ! -f "$DEPS_PREFIX/lib/$base.lib" ]]; then
      cand=$(cd "$DEPS_PREFIX/lib" 2>/dev/null && ls *$base*.lib 2>/dev/null | head -n 1 || true)
      [[ -n "$cand" ]] && cp "$DEPS_PREFIX/lib/$cand" "$DEPS_PREFIX/lib/$base.lib"
    fi
  done
  write_pc zlib 1.3.1 "*zlib*.lib" ""
  write_pc libarchive 3.7.7 "archive*.lib" "" "" "zlib"
  write_pc libxxhash 0.8.3 "xxhash*.lib" ""
  write_pc libzstd 1.5.7 "*zstd*.lib" ""
  write_pc flac 1.4.3 "FLAC.lib" ""
  write_pc flac++ 1.4.3 "FLAC++*.lib" "" "" "flac"
fi

if [[ "$KEEP_WORK" != 1 ]]; then
  rm -rf "$DEPS_WORK"
fi
echo "dependencies installed under $DEPS_PREFIX"
