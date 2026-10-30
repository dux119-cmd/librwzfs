cmake_minimum_required(VERSION 3.20)
project(rwzfs LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(RWZFS_BUILD_TESTS "Build rwzfs tests" ON)

set(DWARFS_PREFIX "/usr/local" CACHE PATH "Prefix where dwarfs is installed")

# Static dwarfs libraries needed by the reader. Link order is dependency order.
set(DWARFS_LIB_DIR "${DWARFS_PREFIX}/lib")
set(DWARFS_LIBS
  "${DWARFS_LIB_DIR}/libdwarfs_reader.a"
  "${DWARFS_LIB_DIR}/libdwarfs_decompressor.a"
  "${DWARFS_LIB_DIR}/libdwarfs_common.a")

foreach(lib IN LISTS DWARFS_LIBS)
  if(NOT EXISTS "${lib}")
    message(FATAL_ERROR "dwarfs library not found: ${lib}")
  endif()
endforeach()

# Third-party dependencies of the dwarfs static libraries.
find_package(PkgConfig REQUIRED)
find_package(fmt 9.1 REQUIRED CONFIG)
find_package(Boost REQUIRED CONFIG COMPONENTS chrono filesystem process program_options)
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

add_library(rwzfs
  src/mount.cpp
  src/fs/path.cpp
  src/fs/archive_path.cpp
  src/fs/filesystem.cpp
  src/fs/mount_table.cpp
  src/fs/cwd.cpp
  src/io/posix.cpp)
target_include_directories(rwzfs
  PUBLIC  ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${DWARFS_PREFIX}/include)
target_link_libraries(rwzfs PUBLIC
  ${DWARFS_LIBS}
  fmt::fmt
  Boost::chrono Boost::filesystem Boost::process Boost::program_options
  PkgConfig::FLAC
  Threads::Threads
  PkgConfig::ZSTD PkgConfig::LIBCRYPTO PkgConfig::XXHASH)
# Optional codecs: linked only if dwarfs was built with them.
foreach(target IN ITEMS PkgConfig::LIBLZ4 PkgConfig::LIBLZMA
                        PkgConfig::LIBBROTLIDEC PkgConfig::LIBBROTLIENC)
  if(TARGET ${target})
    target_link_libraries(rwzfs PUBLIC ${target})
  endif()
endforeach()
if(TARGET cpptrace::cpptrace)
  target_link_libraries(rwzfs PUBLIC cpptrace::cpptrace)
endif()

if(RWZFS_BUILD_TESTS)
  enable_testing()
  find_package(GTest REQUIRED)
  add_executable(rwzfs_tests
    tests/mount_test.cpp
    tests/fs/path_test.cpp
    tests/fs/archive_path_test.cpp
    tests/fs/filesystem_test.cpp
    tests/fs/mount_table_test.cpp
    tests/fs/cwd_test.cpp
    tests/io/posix_test.cpp)
  target_link_libraries(rwzfs_tests PRIVATE rwzfs GTest::gtest_main)
  include(GoogleTest)
  gtest_discover_tests(rwzfs_tests)
endif()
