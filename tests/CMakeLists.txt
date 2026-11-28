# The test suite (GoogleTest). Built by default in a standalone build; off when rwzfs is a subdirectory.

# GoogleTest: a system package when present, otherwise fetched (works on Windows runners).
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
  fs/path_test.cpp
  fs/archive_path_test.cpp
  fs/filesystem_test.cpp
  fs/mount_table_test.cpp
  fs/cwd_test.cpp
  fs/resolve_test.cpp
  fs/archive_fs_test.cpp
  fs/archive_fs_queries_test.cpp
  fs/directory_test.cpp
  fs/overlay_test.cpp
  fs/walk_test.cpp
  io/posix_test.cpp
  io/stdio_test.cpp
  io/stdio_streams_test.cpp
  io/rwz_namespace_test.cpp
  fs/throwing_test.cpp
  fs/proximate_test.cpp
  io/stdio_open_modes_test.cpp
  io/stdio_api_test.cpp
  io/stdio_large_file_test.cpp
  archive/session_api_test.cpp
  archive/clear_changes_test.cpp
  fs/mount_at_path_test.cpp
  archive/extract_test.cpp
  io/stdio_archive_test.cpp
  io/stdio_lazy_test.cpp
  io/stdio_callbacks_test.cpp
  io/win32_archive_test.cpp
  io/stdio_windows_test.cpp
  io/archive_io_test.cpp
  overlay/cow_test.cpp
  overlay/overlay_integration_test.cpp
  overlay/overlay_metadata_test.cpp
  overlay/lock_test.cpp
  overlay/flatten_test.cpp
  archive/revision_name_test.cpp
  archive/create_source_test.cpp)
if(RWZFS_WITH_DWARFS AND WIN32)
  # The dwarfs POSIX tests do not build on Windows; the creator round trip does.
  list(APPEND RWZFS_TEST_SOURCES archive/create_view_windows_test.cpp)
elseif(RWZFS_WITH_DWARFS)
  list(APPEND RWZFS_TEST_SOURCES
  fs/mount_archive_test.cpp
  archive/dwarfs_archive_test.cpp
  archive/create_revise_test.cpp
  archive/create_view_test.cpp
  archive/session_round_trip_test.cpp
  archive/large_file_round_trip_test.cpp
  archive/extract_round_trip_test.cpp
  archive/overlay_cleanup_test.cpp
  archive/progress_test.cpp
)
endif()

add_executable(rwzfs_tests ${RWZFS_TEST_SOURCES})
target_link_libraries(rwzfs_tests PRIVATE rwzfs GTest::gtest_main)
if(RWZFS_PCH)
  target_precompile_headers(rwzfs_tests PRIVATE ${RWZFS_PCH_HEADERS} <gtest/gtest.h>)
endif()
include(GoogleTest)

if(RWZFS_WITH_DWARFS AND NOT WIN32)
  # The dwarfs tests share one fixture archive, made once by a setup test.
  gtest_discover_tests(rwzfs_tests PROPERTIES FIXTURES_REQUIRED rwzfs_archive)
  add_executable(rwzfs_make_fixture archive/make_fixture.cpp)
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
