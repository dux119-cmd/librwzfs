#!/usr/bin/env bash
# Windows only: .pc files for the libraries whose MSVC installs do not write one. dwarfs finds its
# dependencies through pkg-config, so the shims point at the static libraries by a glob. Also copies
# zstd.lib and xxhash.lib under the plain names dwarfs asks for. Does nothing elsewhere.
DEP_OPTIMIZE=size
source "$(dirname "${BASH_SOURCE[0]}")/common.sh"

shims() {
  # libcrypto needs the Windows system libraries its static build uses.
  write_pc libcrypto "$OPENSSL_VERSION" "libcrypto*.lib" "ws2_32.lib crypt32.lib advapi32.lib user32.lib"
  for base in zstd xxhash; do
    if [[ ! -f "$DEPS_PREFIX/lib/$base.lib" ]]; then
      local cand
      cand=$(cd "$DEPS_PREFIX/lib" 2>/dev/null && ls *$base*.lib 2>/dev/null | head -n 1 || true)
      [[ -n "$cand" ]] && cp "$DEPS_PREFIX/lib/$cand" "$DEPS_PREFIX/lib/$base.lib"
    fi
  done
  write_pc zlib "$ZLIB_VERSION" "*zlib*.lib" ""
  write_pc libarchive "$LIBARCHIVE_VERSION" "archive*.lib" "" "" "zlib"
  write_pc libxxhash "$XXHASH_VERSION" "xxhash*.lib" ""
  write_pc libzstd "$ZSTD_VERSION" "*zstd*.lib" ""
  write_pc flac "$FLAC_VERSION" "FLAC.lib" ""
  write_pc flac++ "$FLAC_VERSION" "FLAC++*.lib" "" "" "flac"
}

if [[ $IS_WINDOWS == 1 ]]; then
  dep_main windows-shims shims
else
  echo "windows-shims: not Windows, nothing to do"
fi
