cmake_minimum_required(VERSION 3.21)

# ---- Compiler cache -------------------------------------------------------
option(RWZFS_USE_CCACHE "Use ccache when it is installed" ON)
find_program(CCACHE_PROGRAM ccache)
if(RWZFS_USE_CCACHE AND CCACHE_PROGRAM)
  set(CMAKE_C_COMPILER_LAUNCHER ${CMAKE_COMMAND} -E env CCACHE_COMPRESSLEVEL=6 ${CCACHE_PROGRAM})
  set(CMAKE_CXX_COMPILER_LAUNCHER ${CMAKE_COMMAND} -E env CCACHE_COMPRESSLEVEL=6 ${CCACHE_PROGRAM})
  message(STATUS "ccache: ${CCACHE_PROGRAM} (compression level 6)")
endif()

project(rwzfs LANGUAGES CXX)

# Defaults for a top-level build. A parent project that adds rwzfs as a subdirectory keeps its own.
if(PROJECT_IS_TOP_LEVEL)
  if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    if(RWZFS_SANITIZE)
      set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type (default: Debug with RWZFS_SANITIZE)" FORCE)
    else()
      set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type (default: Release)" FORCE)
    endif()
  endif()
  if(MSVC AND NOT CMAKE_MSVC_RUNTIME_LIBRARY)
    # Static runtime, matching the static dependencies (scripts/deps).
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
  endif()
endif()

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(RWZFS_BUILD_TESTS "Build rwzfs tests" ON)
# One flag for sanitizer testing: debug info, AddressSanitizer and UndefinedBehaviorSanitizer, tests on.
# Use it with the sanitize preset, or: cmake -DRWZFS_SANITIZE=ON ...
option(RWZFS_SANITIZE "Debug build with AddressSanitizer and UndefinedBehaviorSanitizer; enables the tests" OFF)
option(RWZFS_PCH "Use precompiled headers for common standard headers" ON)
option(RWZFS_WITH_DWARFS "Build the dwarfs-backed archive reader and its integration tests" ON)

# ---- Sanitizer build (RWZFS_SANITIZE) ---------------------------------------
# Turns on the tests and compiles everything with -g, AddressSanitizer and UndefinedBehaviorSanitizer.
# -fno-sanitize-recover makes the first undefined behaviour abort the test that hit it.
if(RWZFS_SANITIZE)
  if(MSVC)
    message(FATAL_ERROR "RWZFS_SANITIZE needs GCC or Clang: AddressSanitizer and UBSan are not wired for MSVC.")
  endif()
  set(RWZFS_BUILD_TESTS ON CACHE BOOL "Build rwzfs tests" FORCE)
  add_compile_options(-g -O1 -fno-omit-frame-pointer -fsanitize=address,undefined -fno-sanitize-recover=all)
  add_link_options(-fsanitize=address,undefined)
  message(STATUS "rwzfs: sanitizer build (AddressSanitizer + UndefinedBehaviorSanitizer), tests on")
endif()

# ---- Managed dependencies ---------------------------------------------------
# RWZFS_USE_MANAGED_DEPS (default ON) builds dwarfs and its dependencies from pinned sources, as static,
# size-optimized libraries, once, into RWZFS_DEPS_DIR (default: build-deps/ beside build/). Every build
# directory (Release, Debug, sanitizer, tests) finds and uses the same libraries. Cleaning build/ keeps
# them. OFF uses system-provided libraries, with DWARFS_PREFIX (default /usr/local).
# Needs bash (Git's bash on Windows), git, curl, a C++23 compiler, and a few minutes (hours on one CPU).
option(RWZFS_USE_MANAGED_DEPS
  "Use rwzfs's own static dependencies (dwarfs and friends), built once into RWZFS_DEPS_DIR. OFF uses system libraries." ON)
set(RWZFS_DEPS_DIR "${PROJECT_SOURCE_DIR}/build-deps" CACHE PATH
  "Where the managed dependencies are installed. Outside build/, so cleaning build/ keeps them.")
option(RWZFS_SIZE_OPTIMIZED "Compile rwzfs for size in Release (-Os, per-function sections)" ON)

if(RWZFS_USE_MANAGED_DEPS AND RWZFS_WITH_DWARFS)
  set(RWZFS_DEPS_PREFIX "${RWZFS_DEPS_DIR}")
  set(RWZFS_DEPS_WORK "${RWZFS_DEPS_DIR}/work")
  set(RWZFS_DEPS_STAMP "${RWZFS_DEPS_DIR}/.rwz-recipe")

  # The recipe: every file in scripts/deps (versions.sh pins every version; the rest is the actions that
  # build them). A change to any of them rebuilds everything, so an installed library can never be older
  # than the recipe that names it.
  file(GLOB _rwz_recipe_files CONFIGURE_DEPENDS "${PROJECT_SOURCE_DIR}/scripts/deps/*")
  list(SORT _rwz_recipe_files)
  set(_rwz_recipe "")
  foreach(_rwz_f IN LISTS _rwz_recipe_files)
    file(SHA256 "${_rwz_f}" _rwz_h)
    string(APPEND _rwz_recipe "${_rwz_h} ")
  endforeach()
  set(_rwz_installed "")
  if(EXISTS "${RWZFS_DEPS_STAMP}")
    file(READ "${RWZFS_DEPS_STAMP}" _rwz_installed)
  endif()

  if(NOT _rwz_installed STREQUAL _rwz_recipe)
    if(EXISTS "${RWZFS_DEPS_STAMP}")
      message(STATUS "rwzfs: the dependency recipe changed; rebuilding the managed dependencies")
      file(REMOVE "${RWZFS_DEPS_STAMP}" "${RWZFS_DEPS_DIR}/.stamps")
    endif()
    if(WIN32)
      # Git's bash: the bash on PATH may be WSL's, which cannot run these scripts.
      find_program(RWZ_BASH NAMES bash
        PATHS "$ENV{ProgramFiles}/Git/bin" "$ENV{ProgramW6432}/Git/bin" NO_DEFAULT_PATH)
    endif()
    find_program(RWZ_BASH NAMES bash REQUIRED)
    file(MAKE_DIRECTORY "${RWZFS_DEPS_DIR}")
    message(STATUS "rwzfs: building the managed dependencies into ${RWZFS_DEPS_DIR} (once; shared by all builds)")
    # build-all.sh builds every dependency in order (dwarfs last), into one prefix.
    execute_process(
      COMMAND ${CMAKE_COMMAND} -E env
              "DEPS_PREFIX=${RWZFS_DEPS_PREFIX}"
              "DEPS_WORK=${RWZFS_DEPS_WORK}"
              "WITH_TESTS=1"
              "${RWZ_BASH}" "${PROJECT_SOURCE_DIR}/scripts/deps/build-all.sh"
      COMMAND_ERROR_IS_FATAL ANY)
    file(WRITE "${RWZFS_DEPS_STAMP}" "${_rwz_recipe}")
  endif()

  set(DWARFS_PREFIX "${RWZFS_DEPS_PREFIX}" CACHE PATH "Prefix where dwarfs is installed" FORCE)
  list(PREPEND CMAKE_PREFIX_PATH "${RWZFS_DEPS_PREFIX}")
  # Only the managed prefix is visible to pkg-config, and find_library/find_package prefer static
  # archives. Otherwise system .so files (libcrypto, libzstd, libgtest, ...) get linked.
  set(ENV{PKG_CONFIG_LIBDIR} "${RWZFS_DEPS_PREFIX}/lib/pkgconfig:${RWZFS_DEPS_PREFIX}/lib64/pkgconfig")
  if(WIN32)
    set(CMAKE_FIND_LIBRARY_SUFFIXES ".lib")
  else()
    set(CMAKE_FIND_LIBRARY_SUFFIXES ".a")
  endif()
  message(STATUS "rwzfs: using the managed dependencies in ${RWZFS_DEPS_PREFIX}")
elseif(RWZFS_WITH_DWARFS)
  set(DWARFS_PREFIX "/usr/local" CACHE PATH "Prefix where dwarfs is installed")
  message(STATUS "rwzfs: using system-provided dependencies (RWZFS_USE_MANAGED_DEPS=OFF)")
endif()

set(RWZFS_PCH_HEADERS
  <algorithm> <cerrno> <cstddef> <cstdint> <cstring> <deque> <expected> <filesystem>
  <iterator> <map> <memory> <mutex> <optional> <string> <string_view> <system_error>
  <utility> <vector>)

# Sources that do not depend on dwarfs.
set(RWZFS_CORE_SOURCES
  src/fs/path.cpp
  src/fs/archive_path.cpp
  src/fs/filesystem.cpp
  src/fs/directory.cpp
  src/fs/mount_table.cpp
  src/fs/cwd.cpp
  src/fs/walk.cpp
  src/fs/resolve.cpp
  src/io/posix.cpp
  src/io/stdio.cpp
  src/io/archive_io.cpp
  src/io/archive_glob.cpp
  src/io/win32.cpp
  src/io/overlay_posix.cpp
  src/overlay.cpp
  src/overlay/cow.cpp
  src/overlay/host_meta.cpp
  src/overlay/lock.cpp
  src/overlay/flatten.cpp
  src/archive/create_view.cpp
  src/revise.cpp
  src/session.cpp)

if(RWZFS_WITH_DWARFS)
  # Static dwarfs libraries needed by the reader. Link order is dependency order.
  set(DWARFS_LIB_DIR "${DWARFS_PREFIX}/lib")
  # Static library names differ by platform: libdwarfs_writer.a (Unix) and dwarfs_writer.lib (MSVC).
  set(DWARFS_LIBS "")
  foreach(base dwarfs_writer dwarfs_compressor dwarfs_reader dwarfs_decompressor dwarfs_common)
    find_library(RWZ_LIB_${base}
      NAMES lib${base}.a ${base}.lib lib${base}.lib
      PATHS "${DWARFS_LIB_DIR}"
      NO_DEFAULT_PATH)
    if(NOT RWZ_LIB_${base})
      message(FATAL_ERROR "dwarfs library not found: ${base} under ${DWARFS_LIB_DIR} (looked for lib${base}.a, ${base}.lib). Set DWARFS_PREFIX, or configure with -DRWZFS_WITH_DWARFS=OFF.")
    endif()
    # Link order is dependency order: writer, compressor, reader, decompressor, common.
    list(APPEND DWARFS_LIBS "${RWZ_LIB_${base}}")
  endforeach()

  # The archive creator needs the writer headers. An install without them is incomplete.
  foreach(hdr dwarfs/writer/filesystem_writer.h dwarfs/writer/scanner.h dwarfs/open_file_options.h)
    if(NOT EXISTS "${DWARFS_PREFIX}/include/${hdr}")
      message(FATAL_ERROR "dwarfs headers incomplete: ${DWARFS_PREFIX}/include/${hdr} is missing. Reinstall dwarfs (cmake --install) into DWARFS_PREFIX.")
    endif()
  endforeach()

  find_package(PkgConfig REQUIRED)
  find_package(fmt 9.1 REQUIRED CONFIG)
  find_package(Boost REQUIRED CONFIG COMPONENTS chrono filesystem program_options OPTIONAL_COMPONENTS process)
  find_package(Threads REQUIRED)
  pkg_check_modules(ZSTD REQUIRED IMPORTED_TARGET libzstd)
  pkg_check_modules(LIBCRYPTO REQUIRED IMPORTED_TARGET libcrypto)
  pkg_check_modules(XXHASH REQUIRED IMPORTED_TARGET libxxhash)
  pkg_check_modules(LIBLZ4 IMPORTED_TARGET liblz4)
  pkg_check_modules(LIBLZMA IMPORTED_TARGET liblzma)
  pkg_check_modules(LIBBROTLIDEC IMPORTED_TARGET libbrotlidec)
  pkg_check_modules(LIBBROTLIENC IMPORTED_TARGET libbrotlienc)
  pkg_check_modules(FLAC REQUIRED IMPORTED_TARGET flac++)
  find_package(cpptrace CONFIG QUIET)
  set(RWZFS_DWARFS_SOURCES src/archive/dwarfs_backend.cpp src/mount.cpp)
  set(RWZFS_DWARFS_DEFINE RWZFS_HAVE_DWARFS=1)
else()
  set(RWZFS_DWARFS_SOURCES src/mount_nodwarfs.cpp)
  set(RWZFS_DWARFS_DEFINE)
endif()

add_library(rwzfs ${RWZFS_CORE_SOURCES} ${RWZFS_DWARFS_SOURCES})

# Warnings for the library: reported, not errors (so a new compiler cannot break the build). Each flag is
# checked first, so a compiler that lacks one (MSVC, Clang without a GCC-only warning) just skips it.
set(RWZFS_WARNING_FLAGS
  -Wall -Wextra -Wdeprecated -Wno-unknown-pragmas
  -Wno-conversion -Wno-narrowing
  -Wdisabled-optimization -Wduplicated-branches -Wtrampolines -Wvla
  -Wlogical-op -Wduplicated-cond -Wpointer-arith
  -Wredundant-decls -Wfloat-conversion -Weffc++
  -Wextra-semi
  -Wimplicit-fallthrough -Winvalid-utf8
  -Wtrivial-auto-var-init -Wconversion -Wsign-conversion -Wnarrowing)
include(CheckCXXCompilerFlag)
set(RWZFS_ACTIVE_WARNINGS "")
foreach(_rwz_flag IN LISTS RWZFS_WARNING_FLAGS)
  string(MAKE_C_IDENTIFIER "RWZ_HAS${_rwz_flag}" _rwz_var)
  check_cxx_compiler_flag("${_rwz_flag}" ${_rwz_var})
  if(${_rwz_var})
    list(APPEND RWZFS_ACTIVE_WARNINGS "${_rwz_flag}")
  endif()
endforeach()
target_compile_options(rwzfs PRIVATE ${RWZFS_ACTIVE_WARNINGS})
target_include_directories(rwzfs PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
# jemalloc (static, built by scripts/deps/jemalloc.sh) gives faster, leaner allocation for the
# archive creator and the merged view. Linked publicly, so the final executable gets it. An
# unprefixed static jemalloc replaces malloc in that executable; set RWZFS_USE_JEMALLOC=OFF to keep
# the platform allocator.
if(WIN32)
  set(RWZFS_USE_JEMALLOC_DEFAULT OFF)  # not built for MSVC (scripts/deps)
else()
  set(RWZFS_USE_JEMALLOC_DEFAULT ON)
endif()
option(RWZFS_USE_JEMALLOC "Link the static jemalloc from scripts/deps (replaces malloc in the final executable)" ${RWZFS_USE_JEMALLOC_DEFAULT})
if(RWZFS_USE_JEMALLOC)
  find_library(RWZFS_JEMALLOC_LIB NAMES libjemalloc.a
               HINTS "${RWZFS_DEPS_PREFIX}/lib" "${RWZFS_DEPS_PREFIX}/lib64" NO_DEFAULT_PATH)
  if(NOT RWZFS_JEMALLOC_LIB)
    find_library(RWZFS_JEMALLOC_LIB NAMES libjemalloc.a)
  endif()
  if(RWZFS_JEMALLOC_LIB)
    find_package(Threads REQUIRED)
    target_link_libraries(rwzfs PUBLIC "${RWZFS_JEMALLOC_LIB}" Threads::Threads ${CMAKE_DL_LIBS})
    message(STATUS "rwzfs: linking jemalloc ${RWZFS_JEMALLOC_LIB}")
  else()
    message(STATUS "rwzfs: no static jemalloc found; using the platform allocator")
  endif()
endif()

if(RWZFS_SIZE_OPTIMIZED AND NOT MSVC)
  target_compile_options(rwzfs PRIVATE $<$<CONFIG:Release,MinSizeRel>:-Os -ffunction-sections -fdata-sections>)
endif()
if(RWZFS_DWARFS_DEFINE)
  target_compile_definitions(rwzfs PRIVATE ${RWZFS_DWARFS_DEFINE})
endif()

if(RWZFS_WITH_DWARFS)
  target_include_directories(rwzfs PRIVATE ${DWARFS_PREFIX}/include)
  target_link_libraries(rwzfs PUBLIC
    ${DWARFS_LIBS}
    fmt::fmt
    Boost::chrono Boost::filesystem Boost::program_options
    PkgConfig::FLAC
    Threads::Threads
    PkgConfig::ZSTD PkgConfig::LIBCRYPTO PkgConfig::XXHASH)
  foreach(target IN ITEMS PkgConfig::LIBLZ4 PkgConfig::LIBLZMA
                          PkgConfig::LIBBROTLIDEC PkgConfig::LIBBROTLIENC)
    if(TARGET ${target})
      target_link_libraries(rwzfs PUBLIC ${target})
    endif()
  endforeach()
  if(TARGET Boost::process)
    target_link_libraries(rwzfs PUBLIC Boost::process)
  endif()
  if(TARGET cpptrace::cpptrace)
    target_link_libraries(rwzfs PUBLIC cpptrace::cpptrace)
  endif()
  target_compile_definitions(rwzfs PUBLIC RWZFS_HAS_DWARFS=1)
  if(WIN32)
    target_link_libraries(rwzfs PUBLIC psapi)  # dwarfs: GetProcessMemoryInfo
  endif()
endif()

if(RWZFS_PCH)
  target_precompile_headers(rwzfs PRIVATE ${RWZFS_PCH_HEADERS})
endif()

if(RWZFS_BUILD_TESTS)
  enable_testing()

  # GoogleTest: system package when present, otherwise fetched (works on Windows runners).
  find_package(GTest CONFIG QUIET)
  if(NOT GTest_FOUND)
    include(FetchContent)
    FetchContent_Declare(googletest
      URL https://github.com/google/googletest/archive/refs/tags/v1.17.0.tar.gz
      DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
    set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(googletest)
    if(NOT TARGET GTest::gtest_main)
      add_library(GTest::gtest_main ALIAS gtest_main)
    endif()
  endif()

  set(RWZFS_TEST_SOURCES
    tests/fs/path_test.cpp
    tests/fs/archive_path_test.cpp
    tests/fs/filesystem_test.cpp
    tests/fs/mount_table_test.cpp
    tests/fs/cwd_test.cpp
    tests/fs/resolve_test.cpp
    tests/fs/archive_fs_test.cpp
    tests/fs/archive_fs_queries_test.cpp
    tests/fs/directory_test.cpp
    tests/fs/overlay_test.cpp
    tests/fs/walk_test.cpp
    tests/io/posix_test.cpp
    tests/io/stdio_test.cpp
    tests/io/stdio_streams_test.cpp
    tests/io/rwz_namespace_test.cpp
    tests/fs/throwing_test.cpp
    tests/fs/proximate_test.cpp
    tests/io/stdio_open_modes_test.cpp
    tests/io/stdio_api_test.cpp
    tests/archive/session_api_test.cpp
    tests/io/stdio_archive_test.cpp
    tests/io/stdio_lazy_test.cpp
    tests/io/stdio_callbacks_test.cpp
    tests/io/win32_archive_test.cpp
    tests/io/stdio_windows_test.cpp
    tests/io/archive_io_test.cpp
    tests/overlay/cow_test.cpp
    tests/overlay/overlay_integration_test.cpp
    tests/overlay/overlay_metadata_test.cpp
    tests/overlay/lock_test.cpp
    tests/overlay/flatten_test.cpp
    tests/archive/revision_name_test.cpp
    tests/archive/create_source_test.cpp)
  if(RWZFS_WITH_DWARFS AND WIN32)
    # The dwarfs POSIX tests do not build on Windows; the creator round trip does.
    list(APPEND RWZFS_TEST_SOURCES tests/archive/create_view_windows_test.cpp)
  elseif(RWZFS_WITH_DWARFS)
    list(APPEND RWZFS_TEST_SOURCES
      tests/fs/mount_archive_test.cpp
      tests/archive/dwarfs_archive_test.cpp
      tests/archive/create_revise_test.cpp
      tests/archive/create_view_test.cpp
      tests/archive/session_round_trip_test.cpp)
  endif()

  add_executable(rwzfs_tests ${RWZFS_TEST_SOURCES})
  target_link_libraries(rwzfs_tests PRIVATE rwzfs GTest::gtest_main)
  if(RWZFS_PCH)
    target_precompile_headers(rwzfs_tests PRIVATE ${RWZFS_PCH_HEADERS} <gtest/gtest.h>)
  endif()
  include(GoogleTest)

  if(RWZFS_WITH_DWARFS AND NOT WIN32)
    gtest_discover_tests(rwzfs_tests PROPERTIES FIXTURES_REQUIRED rwzfs_archive)
    add_executable(rwzfs_make_fixture tests/archive/make_fixture.cpp)
    target_link_libraries(rwzfs_make_fixture PRIVATE rwzfs)
    set(RWZFS_FIXTURE_DIR "${CMAKE_CURRENT_BINARY_DIR}/fixtures")
    set(RWZFS_FIXTURE_ARCHIVE "${RWZFS_FIXTURE_DIR}/fixture.dwarfs")
    add_test(NAME rwzfs_fixture_setup
      COMMAND rwzfs_make_fixture ${RWZFS_FIXTURE_ARCHIVE} ${RWZFS_FIXTURE_DIR}/src)
    set_tests_properties(rwzfs_fixture_setup PROPERTIES FIXTURES_SETUP rwzfs_archive)
    target_compile_definitions(rwzfs_tests PRIVATE RWZFS_FIXTURE_ARCHIVE="${RWZFS_FIXTURE_ARCHIVE}")
  else()
    gtest_discover_tests(rwzfs_tests)
  endif()
endif()

# Showcase program (docs/USAGE.md): host tree, mounted archive, writes, revision. Needs dwarfs.
if(RWZFS_WITH_DWARFS AND RWZFS_BUILD_TESTS)
  add_executable(rwzfs_example examples/usage.cpp)
  target_link_libraries(rwzfs_example PRIVATE rwzfs)
  add_test(NAME rwzfs_example COMMAND rwzfs_example ${CMAKE_BINARY_DIR}/example-work)

  # rwz-tool: open, revise and create archives from the command line (docs/USAGE.md).
  add_executable(rwz-tool examples/rwz_tool.cpp)
  target_link_libraries(rwz-tool PRIVATE rwzfs)
  add_test(NAME rwz_tool_usage COMMAND rwz-tool)
  set_tests_properties(rwz_tool_usage PROPERTIES WILL_FAIL TRUE)
endif()

# Read benchmark (docs/BENCHMARK.md): host tree vs mounted archive. POSIX only.
if(RWZFS_WITH_DWARFS AND NOT WIN32 AND RWZFS_BUILD_TESTS)
  add_executable(rwzfs_bench examples/bench_read.cpp)
  target_link_libraries(rwzfs_bench PRIVATE rwzfs)
endif()
