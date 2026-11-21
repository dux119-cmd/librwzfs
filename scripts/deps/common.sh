#!/usr/bin/env bash
# Shared actions for the managed-dependency builders. Sourced by each <dep>.sh (and dwarfs.sh); never
# run directly. Versions come from versions.sh.
#
# A builder sets DEP_OPTIMIZE before sourcing this file:
#   size   (default): -Os, one section per function, hidden symbols, MinSizeRel
#   speed            : -Ofast (zstd, xxHash, jemalloc), Release
#
# Environment (build-all.sh sets these; a single step can be run with them set by hand):
#   DEPS_PREFIX  install prefix (default: <repo>/build-deps)
#   DEPS_WORK    scratch for sources and build trees (default: $DEPS_PREFIX/work)
#   JOBS         parallel jobs (default: CPU count)
#   WITH_TESTS   1 to build googletest as well
#   KEEP_WORK    1 to keep DEPS_WORK after a successful run
#   ONLY         comma list of steps to build, when run from build-all.sh
#   DEPS_EXTERNAL_PREFIX  a prefix that already holds dependencies (vcpkg's installed tree): searched by
#                  CMake and pkg-config, for dwarfs.sh. Empty: the managed prefix only.
#   DRY_RUN      1 to print what would be built, and build nothing

set -euo pipefail

DEPS_SCRIPTS_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$DEPS_SCRIPTS_DIR/versions.sh"

case "$(uname -s)" in
  MINGW*|MSYS*|CYGWIN*) IS_WINDOWS=1 ;;
  *) IS_WINDOWS=0 ;;
esac
IS_DARWIN=0
[[ "$(uname -s)" == Darwin ]] && IS_DARWIN=1

# Windows (Git Bash or MSYS): CMake needs mixed Windows paths.
to_native() {
  # An empty path stays empty (cygpath rejects it), e.g. for an optional prefix that is not set.
  [[ -z "$1" ]] && return 0
  if [[ $IS_WINDOWS == 1 ]] && command -v cygpath >/dev/null 2>&1; then cygpath -m "$1"; else printf '%s' "$1"; fi
}

DEPS_PREFIX="$(to_native "${DEPS_PREFIX:-$DEPS_SCRIPTS_DIR/../../build-deps}")"
DEPS_WORK="$(to_native "${DEPS_WORK:-$DEPS_PREFIX/work}")"
JOBS="${JOBS:-$( (nproc 2>/dev/null) || (sysctl -n hw.ncpu 2>/dev/null) || echo 2)}"
WITH_TESTS="${WITH_TESTS:-0}"
KEEP_WORK="${KEEP_WORK:-0}"
ONLY="${ONLY:-}"
DRY_RUN="${DRY_RUN:-0}"
DEP_OPTIMIZE="${DEP_OPTIMIZE:-size}"

if [[ $IS_WINDOWS == 1 ]]; then
  # Git Bash's profile puts /usr/bin (Git's perl) first, and OpenSSL's Configure needs a complete perl.
  # Only perl goes first on PATH: Strawberry's gcc must not be used (MinGW lacks STARTUPINFOEXW, which
  # Boost.Process needs). The dependencies are built with MSVC.
  if [[ -x /c/Strawberry/perl/bin/perl.exe ]]; then
    export PATH="/c/Strawberry/perl/bin:$PATH"
  fi
  export CC="${CC:-cl}" CXX="${CXX:-cl}"
  if [[ $DRY_RUN != 1 ]]; then
    perl -MLocale::Maketext::Simple -e 1 2>/dev/null || {
      echo "error: perl lacks Locale::Maketext::Simple (install Strawberry Perl, or put a full perl first on PATH)" >&2
      exit 1
    }
  fi
fi

# Flags. Windows: MSVC, the optimization comes from the build type. Elsewhere: GCC/Clang flags.
if [[ $IS_WINDOWS == 1 ]]; then
  LIB_SUFFIX=".lib"
  # One static C runtime for every library and its consumers: a mix with the DLL runtime does not link.
  MSVC_RUNTIME=(-DCMAKE_POLICY_DEFAULT_CMP0091=NEW -DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded)
  SIZE_CFLAGS=""
  SIZE_CXXFLAGS=""
  SPEED_CFLAGS=""
  SPEED_CXXFLAGS=""
else
  LIB_SUFFIX=".a"
  MSVC_RUNTIME=()
  SIZE_CFLAGS="-Os -DNDEBUG -ffunction-sections -fdata-sections -fvisibility=hidden"
  SIZE_CXXFLAGS="$SIZE_CFLAGS -fvisibility-inlines-hidden"
  SPEED_CFLAGS="-Ofast -DNDEBUG -ffunction-sections -fdata-sections -fvisibility=hidden"
  SPEED_CXXFLAGS="$SPEED_CFLAGS -fvisibility-inlines-hidden"
fi
SIZE_BUILD_TYPE=MinSizeRel
SPEED_BUILD_TYPE=Release

case "$DEP_OPTIMIZE" in
  size)  export CFLAGS="$SIZE_CFLAGS"  CXXFLAGS="$SIZE_CXXFLAGS" ;;
  speed) export CFLAGS="$SPEED_CFLAGS" CXXFLAGS="$SPEED_CXXFLAGS" ;;
  *) echo "error: DEP_OPTIMIZE must be size or speed, not '$DEP_OPTIMIZE'" >&2; exit 1 ;;
esac

# Only pkg-config files from this prefix are seen. Otherwise a .pc file that says -lcrypto lets the
# linker pick a system shared library over the static one built here.
PKG_CONFIG_DIRS="$DEPS_PREFIX/lib/pkgconfig:$DEPS_PREFIX/lib64/pkgconfig"
# An external prefix (vcpkg's installed tree, when the dependencies come from vcpkg) is searched too.
DEPS_EXTERNAL_PREFIX="$(to_native "${DEPS_EXTERNAL_PREFIX:-}")"
if [[ -n "$DEPS_EXTERNAL_PREFIX" ]]; then
  PKG_CONFIG_DIRS="$PKG_CONFIG_DIRS:$DEPS_EXTERNAL_PREFIX/lib/pkgconfig:$DEPS_EXTERNAL_PREFIX/share/pkgconfig"
fi
export PKG_CONFIG_LIBDIR="$PKG_CONFIG_DIRS"

# GNU tar on Git Bash reads "D:/..." as host:path. bsdtar (macOS) does not know --force-local.
TAR_LOCAL=()
if [[ $IS_WINDOWS == 1 ]]; then TAR_LOCAL=(--force-local); fi

GEN=()
if command -v ninja >/dev/null 2>&1; then GEN=(-G Ninja); fi

if [[ $DRY_RUN != 1 ]]; then
  mkdir -p "$DEPS_PREFIX/.stamps" "$DEPS_WORK"
fi

log() { printf '\n==== %s ====\n' "$*"; }

# Runs one step if it is selected and not built yet. The step's function does the work.
dep_main() { # name function
  local name=$1 fn=$2
  if [[ -n "$ONLY" && ",$ONLY," != *",$name,"* ]]; then
    echo "skip $name (not in ONLY)"
    return 0
  fi
  if [[ -f "$DEPS_PREFIX/.stamps/$name" ]]; then
    echo "skip $name (already built)"
    return 0
  fi
  if [[ $DRY_RUN == 1 ]]; then
    echo "would build $name ($fn)"
    return 0
  fi
  log "building $name"
  "$fn"
  touch "$DEPS_PREFIX/.stamps/$name"
}

# Shallow clone of a pinned tag into $DEPS_WORK/<name>. Prints the directory.
fetch_tag() { # name owner/repo tag
  local dir="$DEPS_WORK/$1"
  rm -rf "$dir"
  git clone --depth 1 --branch "$3" "https://github.com/$2.git" "$dir" >/dev/null 2>&1 || {
    echo "error: cannot clone $2 at $3" >&2
    exit 1
  }
  echo "$dir"
}

# SHA-256 of a file: sha256sum (Linux, Git Bash) or shasum (macOS).
sha256_of() {
  if command -v sha256sum >/dev/null 2>&1; then
    sha256sum "$1" | cut -d' ' -f1
  else
    shasum -a 256 "$1" | cut -d' ' -f1
  fi
}

# Downloads a release archive into $DEPS_WORK, checks its SHA-256, and extracts it there. The archive
# is extracted into a directory named by the archive. kind: gz, bz2 or xz.
fetch_archive() { # url file-name kind sha256
  local file="$DEPS_WORK/$2" got
  curl -fsSL "$1" -o "$file"
  got=$(sha256_of "$file")
  if [[ "$got" != "$4" ]]; then
    echo "error: $2 has sha256 $got, expected $4. Not extracted." >&2
    rm -f "$file"
    exit 1
  fi
  case "$3" in
    gz)  tar ${TAR_LOCAL[@]+"${TAR_LOCAL[@]}"} -xzf "$file" -C "$DEPS_WORK" ;;
    bz2) tar ${TAR_LOCAL[@]+"${TAR_LOCAL[@]}"} -xjf "$file" -C "$DEPS_WORK" ;;
    xz)  tar ${TAR_LOCAL[@]+"${TAR_LOCAL[@]}"} -xJf "$file" -C "$DEPS_WORK" ;;
    *) echo "error: unknown archive kind $3" >&2; exit 1 ;;
  esac
}

# cmake configure, build and install for one dependency. DEP_OPTIMIZE picks the build type and flags.
cmake_dep() { # name source-dir [extra cmake args...]
  local name=$1 src=$2
  shift 2
  local build_type="$SIZE_BUILD_TYPE"
  local overrides=()
  if [[ $DEP_OPTIMIZE == speed ]]; then
    build_type="$SPEED_BUILD_TYPE"
    # CMake appends its own -O3 after CFLAGS, which would win over -Ofast. Clear the Release flags so the
    # speed flags are the only optimization settings. (Windows keeps /O2 from Release.)
    if [[ $IS_WINDOWS != 1 ]]; then
      overrides=(-DCMAKE_C_FLAGS_RELEASE=-DNDEBUG -DCMAKE_CXX_FLAGS_RELEASE=-DNDEBUG)
    fi
  fi
  cmake -S "$src" -B "$DEPS_WORK/$name-build" ${GEN[@]+"${GEN[@]}"} \
    -DCMAKE_POLICY_VERSION_MINIMUM=3.5 \
    -DCMAKE_BUILD_TYPE="$build_type" \
    -DBUILD_SHARED_LIBS=OFF \
    -DCMAKE_POSITION_INDEPENDENT_CODE=ON \
    -DCMAKE_FIND_LIBRARY_SUFFIXES="$LIB_SUFFIX" \
    ${MSVC_RUNTIME[@]+"${MSVC_RUNTIME[@]}"} \
    -DCMAKE_INSTALL_PREFIX="$DEPS_PREFIX" \
    -DCMAKE_PREFIX_PATH="$DEPS_PREFIX" \
    ${overrides[@]+"${overrides[@]}"} \
    "$@"
  cmake --build "$DEPS_WORK/$name-build" --parallel "$JOBS"
  cmake --install "$DEPS_WORK/$name-build"
}

# Writes $DEPS_PREFIX/lib/pkgconfig/<name>.pc for a Windows library whose MSVC install lacks one, with
# absolute paths to the static library (a -l flag does not map onto the MSVC linker).
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
