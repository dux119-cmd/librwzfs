# The rwzfs library. Layers: fs (paths, the mount table, the working directory), io (the wrappers), overlay (saved
# changes), archive (creating archives), and session (mounting and revising, tying them together).

set(RWZFS_SOURCES
  fs/path.cpp
  fs/archive_path.cpp
  fs/filesystem.cpp
  fs/directory.cpp
  fs/mount_table.cpp
  fs/cwd.cpp
  fs/walk.cpp
  fs/resolve.cpp
  io/posix.cpp
  io/stdio.cpp
  io/archive_io.cpp
  io/archive_glob.cpp
  io/win32.cpp
  io/overlay_posix.cpp
  overlay.cpp
  overlay/cow.cpp
  overlay/host_meta.cpp
  overlay/lock.cpp
  overlay/flatten.cpp
  archive/create_view.cpp
  revise.cpp
  session.cpp
)

if(RWZFS_WITH_DWARFS)
  list(APPEND RWZFS_SOURCES archive/dwarfs_backend.cpp mount.cpp)
else()
  list(APPEND RWZFS_SOURCES mount_nodwarfs.cpp)
endif()

add_library(rwzfs ${RWZFS_SOURCES})
target_include_directories(rwzfs PUBLIC "${PROJECT_SOURCE_DIR}/include")
rwzfs_warnings(rwzfs)

if(RWZFS_WITH_DWARFS)
  include("${PROJECT_SOURCE_DIR}/cmake/RwzfsDwarfs.cmake")
endif()

# Size-optimized Release (GCC and Clang).
if(RWZFS_SIZE_OPTIMIZED AND NOT MSVC)
  target_compile_options(rwzfs PRIVATE $<$<CONFIG:Release,MinSizeRel>:-Os -ffunction-sections -fdata-sections>)
endif()

# jemalloc (static, built by scripts/deps/jemalloc.sh) replaces malloc in the final executable, for faster, leaner
# allocation. Set RWZFS_USE_JEMALLOC=OFF to keep the platform allocator.
if(RWZFS_USE_JEMALLOC)
  find_library(RWZFS_JEMALLOC_LIB NAMES libjemalloc.a HINTS "${RWZFS_DEPS_DIR}/lib" NO_DEFAULT_PATH)
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

if(RWZFS_PCH)
  target_precompile_headers(rwzfs PRIVATE ${RWZFS_PCH_HEADERS})
endif()
