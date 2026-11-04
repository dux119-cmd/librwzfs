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

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(RWZFS_BUILD_TESTS "Build rwzfs tests" ON)
option(RWZFS_PCH "Use precompiled headers for common standard headers" ON)
option(RWZFS_WITH_DWARFS "Build the dwarfs-backed archive reader and its integration tests" ON)

set(DWARFS_PREFIX "/usr/local" CACHE PATH "Prefix where dwarfs is installed")

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
  src/io/overlay_posix.cpp
  src/overlay.cpp
  src/overlay/cow.cpp
  src/overlay/host_meta.cpp
  src/overlay/lock.cpp
  src/overlay/flatten.cpp
  src/archive/create.cpp
  src/revise.cpp)

if(RWZFS_WITH_DWARFS)
  # Static dwarfs libraries needed by the reader. Link order is dependency order.
  set(DWARFS_LIB_DIR "${DWARFS_PREFIX}/lib")
  set(DWARFS_LIBS
    "${DWARFS_LIB_DIR}/libdwarfs_reader.a"
    "${DWARFS_LIB_DIR}/libdwarfs_decompressor.a"
    "${DWARFS_LIB_DIR}/libdwarfs_common.a")
  foreach(lib IN LISTS DWARFS_LIBS)
    if(NOT EXISTS "${lib}")
      message(FATAL_ERROR "dwarfs library not found: ${lib}. Set DWARFS_PREFIX, or configure with -DRWZFS_WITH_DWARFS=OFF.")
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
else()
  set(RWZFS_DWARFS_SOURCES src/mount_nodwarfs.cpp)
endif()

add_library(rwzfs ${RWZFS_CORE_SOURCES} ${RWZFS_DWARFS_SOURCES})
target_include_directories(rwzfs PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)

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
endif()

# mkdwarfs is used by create_archive. Its path is compiled in when it can be found now.
if(RWZFS_WITH_DWARFS)
  find_program(MKDWARFS mkdwarfs HINTS "${DWARFS_PREFIX}/bin")
else()
  find_program(MKDWARFS mkdwarfs)
endif()
if(MKDWARFS)
  target_compile_definitions(rwzfs PRIVATE RWZFS_MKDWARFS="${MKDWARFS}")
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
      URL https://github.com/google/googletest/archive/refs/tags/v1.14.0.tar.gz
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
    tests/io/stdio_archive_test.cpp
    tests/io/archive_io_test.cpp
    tests/overlay/cow_test.cpp
    tests/overlay/overlay_integration_test.cpp
    tests/overlay/overlay_metadata_test.cpp
    tests/overlay/lock_test.cpp
    tests/overlay/flatten_test.cpp
    tests/archive/revision_name_test.cpp
    tests/archive/create_source_test.cpp)
  if(RWZFS_WITH_DWARFS)
    list(APPEND RWZFS_TEST_SOURCES
      tests/fs/mount_archive_test.cpp
      tests/archive/dwarfs_archive_test.cpp
      tests/archive/create_revise_test.cpp)
  endif()

  add_executable(rwzfs_tests ${RWZFS_TEST_SOURCES})
  target_link_libraries(rwzfs_tests PRIVATE rwzfs GTest::gtest_main)
  if(RWZFS_PCH)
    target_precompile_headers(rwzfs_tests PRIVATE ${RWZFS_PCH_HEADERS} <gtest/gtest.h>)
  endif()
  include(GoogleTest)

  if(RWZFS_WITH_DWARFS)
    gtest_discover_tests(rwzfs_tests PROPERTIES FIXTURES_REQUIRED rwzfs_archive)
    if(NOT MKDWARFS)
      message(FATAL_ERROR "mkdwarfs not found (looked under ${DWARFS_PREFIX}/bin and PATH). It is needed to build the test archive.")
    endif()
    set(RWZFS_FIXTURE_DIR "${CMAKE_CURRENT_BINARY_DIR}/fixtures")
    set(RWZFS_FIXTURE_ARCHIVE "${RWZFS_FIXTURE_DIR}/fixture.dwarfs")
    add_test(NAME rwzfs_fixture_setup
      COMMAND ${CMAKE_COMMAND}
              -DMKDWARFS=${MKDWARFS}
              -DOUT=${RWZFS_FIXTURE_ARCHIVE}
              -DSRC=${RWZFS_FIXTURE_DIR}/src
              -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/archive/make_fixture.cmake)
    set_tests_properties(rwzfs_fixture_setup PROPERTIES FIXTURES_SETUP rwzfs_archive)
    target_compile_definitions(rwzfs_tests PRIVATE RWZFS_FIXTURE_ARCHIVE="${RWZFS_FIXTURE_ARCHIVE}")
  else()
    gtest_discover_tests(rwzfs_tests)
  endif()
endif()
