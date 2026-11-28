cmake_minimum_required(VERSION 3.21)

# rwzfs: a cross-platform archive file system (see README.md and docs/).
#
# Options
#   RWZFS_DEPS            vcpkg (default), scripts, or system: where dwarfs's dependencies come from
#   RWZFS_WITH_DWARFS     ON: the dwarfs archive reader and creator
#   RWZFS_BUILD_TESTS     the test suite   (on in a standalone build; off when vendored)
#   RWZFS_BUILD_EXAMPLES  the examples and tools (on in a standalone build; off when vendored)
#   RWZFS_SANITIZE        AddressSanitizer and UndefinedBehaviorSanitizer build, with tests
#   RWZFS_USE_JEMALLOC, RWZFS_PCH, RWZFS_SIZE_OPTIMIZED, RWZFS_USE_CCACHE, RWZFS_DEPS_DIR
# A standalone build is Debug (single-config generators) with tests and examples. A vendored copy (add_subdirectory)
# uses the parent's build type, and Release if the parent has none, with neither tests nor examples.

# ---- Options that choose the toolchain, declared before project() -----------
option(RWZFS_WITH_DWARFS "Build the dwarfs-backed archive reader and creator" ON)
set(RWZFS_DEPS "vcpkg" CACHE STRING "Where dwarfs's dependencies come from: vcpkg, scripts or system")
set_property(CACHE RWZFS_DEPS PROPERTY STRINGS vcpkg scripts system)
if(NOT RWZFS_DEPS MATCHES "^(vcpkg|scripts|system)$")
  message(FATAL_ERROR "rwzfs: RWZFS_DEPS must be vcpkg, scripts or system (got '${RWZFS_DEPS}').")
endif()
if(RWZFS_WITH_DWARFS AND RWZFS_DEPS STREQUAL "vcpkg")
  include(cmake/RwzfsVcpkg.cmake)
endif()

project(rwzfs LANGUAGES CXX)

# ---- Build type -----------------------------------------------------------------
# project() leaves CMAKE_BUILD_TYPE as an empty cache entry, which a plain CACHE set would not replace: FORCE is needed,
# and is safe because it applies only when no type was given.
if(PROJECT_IS_TOP_LEVEL)
  if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type (Debug for a standalone build)" FORCE)
  endif()
elseif(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release)
endif()

if(PROJECT_IS_TOP_LEVEL AND MSVC AND NOT CMAKE_MSVC_RUNTIME_LIBRARY)
  # Static runtime, matching the static dependencies (scripts/deps).
  set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ---- What to build ----------------------------------------------------------------
option(RWZFS_BUILD_TESTS "Build the test suite" ${PROJECT_IS_TOP_LEVEL})
option(RWZFS_BUILD_EXAMPLES "Build the examples and tools" ${PROJECT_IS_TOP_LEVEL})
option(RWZFS_SANITIZE "AddressSanitizer and UndefinedBehaviorSanitizer build, with tests" OFF)
option(RWZFS_USE_CCACHE "Use ccache when it is installed" ON)
option(RWZFS_PCH "Precompiled headers for the common standard headers" ON)
option(RWZFS_SIZE_OPTIMIZED "Release builds for size (-Os, per-function sections)" ON)
if(WIN32)
  set(_rwz_jemalloc_default OFF)  # not built for MSVC (scripts/deps)
else()
  set(_rwz_jemalloc_default ON)
endif()
option(RWZFS_USE_JEMALLOC "Link the static jemalloc from scripts/deps (replaces malloc in the final executable)"
       ${_rwz_jemalloc_default})
# The managed dependencies (RWZFS_DEPS=scripts) live beside the top-level build/, so every build shares them.
set(RWZFS_DEPS_DIR "${CMAKE_SOURCE_DIR}/build-deps" CACHE PATH
    "Where the managed dependencies are installed. Outside build/, so cleaning build/ keeps them.")
set(RWZFS_SCRIPTS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/scripts/deps")

if(RWZFS_SANITIZE)
  if(MSVC)
    message(FATAL_ERROR "RWZFS_SANITIZE needs GCC or Clang: AddressSanitizer and UBSan are not wired for MSVC.")
  endif()
  # -fno-sanitize-recover: the first undefined behaviour aborts the test that hit it.
  set(RWZFS_BUILD_TESTS ON CACHE BOOL "Build the test suite" FORCE)
  set(RWZFS_BUILD_EXAMPLES ON CACHE BOOL "Build the examples and tools" FORCE)
  add_compile_options(-g -O1 -fno-omit-frame-pointer -fsanitize=address,undefined -fno-sanitize-recover=all)
  add_link_options(-fsanitize=address,undefined)
endif()

if(RWZFS_USE_CCACHE)
  find_program(CCACHE_PROGRAM ccache)
  if(CCACHE_PROGRAM)
    set(CMAKE_C_COMPILER_LAUNCHER ${CMAKE_COMMAND} -E env CCACHE_COMPRESSLEVEL=6 ${CCACHE_PROGRAM})
    set(CMAKE_CXX_COMPILER_LAUNCHER ${CMAKE_COMMAND} -E env CCACHE_COMPRESSLEVEL=6 ${CCACHE_PROGRAM})
  endif()
endif()

# The headers precompiled for the library and the tests.
set(RWZFS_PCH_HEADERS
  <algorithm> <cerrno> <cstddef> <cstdint> <cstring> <deque> <expected> <filesystem>
  <iterator> <map> <memory> <mutex> <optional> <string> <string_view> <system_error>
  <utility> <vector>)

# ---- Build -----------------------------------------------------------------------
include(cmake/RwzfsDeps.cmake)
include(cmake/RwzfsWarnings.cmake)

add_subdirectory(src)

if(RWZFS_BUILD_TESTS OR RWZFS_BUILD_EXAMPLES)
  enable_testing()
endif()
if(RWZFS_BUILD_TESTS)
  add_subdirectory(tests)
endif()
if(RWZFS_BUILD_EXAMPLES)
  add_subdirectory(examples)
endif()
