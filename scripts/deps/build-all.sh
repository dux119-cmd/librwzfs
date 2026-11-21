#!/usr/bin/env bash
# Builds every managed dependency, in dependency order, into one prefix (DEPS_PREFIX, default
# build-deps/ beside build/). Each step is its own script in this directory; versions.sh pins the
# versions; common.sh holds the shared actions. CMake runs this script when RWZFS_DEPS=scripts.
#
# Environment:
#   DEPS_PREFIX  install prefix (default: <repo>/build-deps)
#   DEPS_WORK    scratch area (default: $DEPS_PREFIX/work), removed after success unless KEEP_WORK=1
#   JOBS         parallel jobs
#   WITH_TESTS   1 to build googletest (for rwzfs's tests)
#   JEMALLOC     0 to skip jemalloc (default 1)
#   ONLY         comma list of steps to build, e.g. ONLY=zstd,fmt (default: all)
#   DRY_RUN      1 to print the plan and build nothing
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
export DEPS_PREFIX="${DEPS_PREFIX:-$HERE/../../build-deps}"

# Build order matters: libarchive needs zlib, dwarfs needs everything else.
STEPS=(zlib zstd xxhash fmt flac openssl libarchive boost utfcpp json phmap)
if [[ "${JEMALLOC:-1}" == 1 ]]; then STEPS+=(jemalloc); fi
if [[ "${WITH_TESTS:-0}" == 1 ]]; then STEPS+=(googletest); fi
STEPS+=(windows-shims dwarfs)

for step in "${STEPS[@]}"; do
  bash "$HERE/$step.sh"
done

if [[ "${KEEP_WORK:-0}" != 1 && "${DRY_RUN:-0}" != 1 ]]; then
  rm -rf "${DEPS_WORK:-$DEPS_PREFIX/work}"
fi
echo "managed dependencies: ${DEPS_PREFIX}"
