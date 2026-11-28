# The dwarfs archive reader and creator. Included from src/CMakeLists.txt once the rwzfs target exists; DWARFS_PREFIX
# comes from RwzfsDeps.cmake. Finds the static dwarfs libraries and what they need, and links them into rwzfs.

# Static archives: libdwarfs_writer.a (Unix) and dwarfs_writer.lib (MSVC). Link order is dependency order.
set(DWARFS_LIBS "")
foreach(base dwarfs_writer dwarfs_compressor dwarfs_reader dwarfs_decompressor dwarfs_common)
  find_library(RWZ_LIB_${base}
    NAMES lib${base}.a ${base}.lib lib${base}.lib
    PATHS "${DWARFS_PREFIX}/lib"
    NO_DEFAULT_PATH)
  if(NOT RWZ_LIB_${base})
    message(FATAL_ERROR "dwarfs library not found: ${base} under ${DWARFS_PREFIX}/lib (looked for lib${base}.a, ${base}.lib). Set DWARFS_PREFIX, or configure with -DRWZFS_WITH_DWARFS=OFF.")
  endif()
  list(APPEND DWARFS_LIBS "${RWZ_LIB_${base}}")
endforeach()

# The creator needs the writer headers: an install without them is incomplete.
foreach(hdr dwarfs/writer/filesystem_writer.h dwarfs/writer/scanner.h dwarfs/open_file_options.h)
  if(NOT EXISTS "${DWARFS_PREFIX}/include/${hdr}")
    message(FATAL_ERROR "dwarfs headers incomplete: ${DWARFS_PREFIX}/include/${hdr} is missing. Reinstall dwarfs (cmake --install) into DWARFS_PREFIX.")
  endif()
endforeach()

# The default archive profile compresses PCM audio with FLAC, so dwarfs must have been built with FLAC.
set(_rwz_dwarfs_config "${DWARFS_PREFIX}/include/dwarfs/config.h")
if(NOT EXISTS "${_rwz_dwarfs_config}")
  message(FATAL_ERROR "rwzfs: dwarfs's config.h is not under ${DWARFS_PREFIX}/include/dwarfs (is dwarfs installed there?)")
endif()
file(STRINGS "${_rwz_dwarfs_config}" _rwz_flac_line REGEX "DWARFS_HAVE_FLAC")
if(NOT _rwz_flac_line MATCHES "DWARFS_HAVE_FLAC 1")
  message(FATAL_ERROR
    "rwzfs: the dwarfs build has no FLAC (DWARFS_HAVE_FLAC is off). The archive profile compresses PCM audio with FLAC.\n"
    "  Build dwarfs with FLAC: TRY_ENABLE_FLAC=ON and libflac available (the vcpkg port and scripts/deps both do this).")
endif()

# What dwarfs needs. Optional libraries are used when present.
find_package(PkgConfig REQUIRED)
find_package(fmt 9.1 REQUIRED CONFIG)
find_package(Boost REQUIRED CONFIG COMPONENTS chrono filesystem program_options OPTIONAL_COMPONENTS process)
find_package(Threads REQUIRED)
pkg_check_modules(ZSTD REQUIRED IMPORTED_TARGET libzstd)
pkg_check_modules(LIBCRYPTO REQUIRED IMPORTED_TARGET libcrypto)
pkg_check_modules(XXHASH REQUIRED IMPORTED_TARGET libxxhash)
pkg_check_modules(FLAC REQUIRED IMPORTED_TARGET flac++)
pkg_check_modules(LIBLZ4 IMPORTED_TARGET liblz4)
pkg_check_modules(LIBLZMA IMPORTED_TARGET liblzma)
pkg_check_modules(LIBBROTLIDEC IMPORTED_TARGET libbrotlidec)
pkg_check_modules(LIBBROTLIENC IMPORTED_TARGET libbrotlienc)
find_package(cpptrace CONFIG QUIET)

target_include_directories(rwzfs PRIVATE "${DWARFS_PREFIX}/include")
target_link_libraries(rwzfs PUBLIC
  ${DWARFS_LIBS}
  fmt::fmt
  Boost::chrono Boost::filesystem Boost::program_options
  PkgConfig::FLAC
  Threads::Threads
  PkgConfig::ZSTD PkgConfig::LIBCRYPTO PkgConfig::XXHASH)
foreach(optional IN ITEMS PkgConfig::LIBLZ4 PkgConfig::LIBLZMA PkgConfig::LIBBROTLIDEC PkgConfig::LIBBROTLIENC
                          Boost::process cpptrace::cpptrace)
  if(TARGET ${optional})
    target_link_libraries(rwzfs PUBLIC ${optional})
  endif()
endforeach()
if(WIN32)
  target_link_libraries(rwzfs PUBLIC psapi)  # dwarfs: GetProcessMemoryInfo
endif()
target_compile_definitions(rwzfs PRIVATE RWZFS_HAVE_DWARFS=1)
target_compile_definitions(rwzfs PUBLIC RWZFS_HAS_DWARFS=1)
