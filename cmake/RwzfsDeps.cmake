# Where dwarfs's dependencies come from (RWZFS_DEPS), after project(). Each mode sets DWARFS_PREFIX, puts that prefix
# on the search path, and limits pkg-config to it, so no host library is picked up by accident.
#   vcpkg    the dwarfs port (vcpkg-ports/dwarfs) installs dwarfs; without the port, dwarfs.sh builds it against vcpkg's
#            installed tree. The other dependencies come from vcpkg.json.
#   scripts  build-all.sh builds dwarfs and everything it needs, once, into RWZFS_DEPS_DIR (shared by all builds).
#   system   DWARFS_PREFIX (default /usr/local) is used as installed.
# Changing a recipe, or the installed vcpkg packages, rebuilds what depends on them.

# The Git bash on Windows: the bash on PATH may be WSL's, which cannot run these scripts.
function(rwzfs_run_bash script)
  if(WIN32)
    find_program(RWZFS_BASH NAMES bash
      PATHS "$ENV{ProgramFiles}/Git/bin" "$ENV{ProgramW6432}/Git/bin" NO_DEFAULT_PATH)
  endif()
  find_program(RWZFS_BASH NAMES bash REQUIRED)
  execute_process(COMMAND ${CMAKE_COMMAND} -E env ${ARGN} "${RWZFS_BASH}" "${script}"
                  COMMAND_ERROR_IS_FATAL ANY)
endfunction()

# The SHA-256 of each file, joined: a recipe is "changed" when this text differs from the one stored by the last build.
function(rwzfs_recipe_hash out)
  set(text "")
  foreach(file IN LISTS ARGN)
    file(SHA256 "${file}" digest)
    string(APPEND text "${digest} ")
  endforeach()
  set(${out} "${text}" PARENT_SCOPE)
endfunction()

if(RWZFS_WITH_DWARFS)
  if(RWZFS_DEPS STREQUAL "vcpkg")
    # The toolchain defines VCPKG_INSTALLED_DIR; the triplet is set by RwzfsVcpkg.cmake when the host has one.
    if(NOT DEFINED VCPKG_INSTALLED_DIR OR NOT DEFINED VCPKG_TARGET_TRIPLET)
      message(FATAL_ERROR
        "rwzfs: RWZFS_DEPS=vcpkg, but no vcpkg toolchain is active (or the host has no static triplet).\n"
        "  - Set VCPKG_ROOT to your vcpkg clone, or pass -DCMAKE_TOOLCHAIN_FILE=<vcpkg>/scripts/buildsystems/vcpkg.cmake\n"
        "  - To build dwarfs's dependencies from source instead: -DRWZFS_DEPS=scripts\n"
        "  - To use libraries already installed on this machine: -DRWZFS_DEPS=system")
    endif()
    set(_vcpkg_prefix "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}")
    if(EXISTS "${_vcpkg_prefix}/include/dwarfs/writer/filesystem_writer.h")
      set(_prefix "${_vcpkg_prefix}")
      message(STATUS "rwzfs: dwarfs comes from the vcpkg port (${_prefix})")
    else()
      set(_prefix "${RWZFS_DEPS_DIR}")
      set(_scripts "${RWZFS_SCRIPTS_DIR}")
      rwzfs_recipe_hash(_recipe "${_scripts}/dwarfs.sh" "${_scripts}/common.sh" "${_scripts}/versions.sh"
                        "${_scripts}/patch-dwarfs-apple.py")
      string(APPEND _recipe "${_vcpkg_prefix}")
      # vcpkg's status file lists every installed package and version: a change there rebuilds dwarfs.
      if(EXISTS "${VCPKG_INSTALLED_DIR}/vcpkg/status")
        rwzfs_recipe_hash(_status "${VCPKG_INSTALLED_DIR}/vcpkg/status")
        string(APPEND _recipe "${_status}")
      endif()
      set(_stamp "${_prefix}/.rwz-recipe-vcpkg")
      set(_installed "")
      if(EXISTS "${_stamp}")
        file(READ "${_stamp}" _installed)
      endif()
      if(NOT _installed STREQUAL _recipe)
        message(STATUS "rwzfs: building dwarfs from script against vcpkg (${_vcpkg_prefix}) into ${_prefix}")
        file(MAKE_DIRECTORY "${_prefix}")
        file(REMOVE "${_prefix}/.stamps/dwarfs")
        rwzfs_run_bash("${_scripts}/dwarfs.sh"
          "DEPS_PREFIX=${_prefix}" "DWARFS_PREFIX=${_prefix}" "DEPS_EXTERNAL_PREFIX=${_vcpkg_prefix}"
          "DEPS_WORK=${_prefix}/work")
        file(REMOVE_RECURSE "${_prefix}/work")
        file(WRITE "${_stamp}" "${_recipe}")
      endif()
      message(STATUS "rwzfs: dwarfs built from script; its dependencies come from vcpkg (${_vcpkg_prefix})")
    endif()
    set(DWARFS_PREFIX "${_prefix}" CACHE PATH "Prefix where dwarfs is installed" FORCE)
    list(PREPEND CMAKE_PREFIX_PATH "${_vcpkg_prefix}")
    set(ENV{PKG_CONFIG_LIBDIR} "${_prefix}/lib/pkgconfig:${_vcpkg_prefix}/lib/pkgconfig:${_vcpkg_prefix}/share/pkgconfig")

  elseif(RWZFS_DEPS STREQUAL "scripts")
    set(_prefix "${RWZFS_DEPS_DIR}")
    file(GLOB _recipe_files CONFIGURE_DEPENDS "${RWZFS_SCRIPTS_DIR}/*")
    list(SORT _recipe_files)
    rwzfs_recipe_hash(_recipe ${_recipe_files})
    set(_stamp "${_prefix}/.rwz-recipe")
    set(_installed "")
    if(EXISTS "${_stamp}")
      file(READ "${_stamp}" _installed)
    endif()
    if(NOT _installed STREQUAL _recipe)
      if(EXISTS "${_stamp}")
        message(STATUS "rwzfs: the dependency recipe changed; rebuilding the dependencies")
        file(REMOVE "${_stamp}" "${_prefix}/.stamps")
      endif()
      message(STATUS "rwzfs: building the dependencies from source into ${_prefix} (once; shared by all builds)")
      file(MAKE_DIRECTORY "${_prefix}")
      rwzfs_run_bash("${RWZFS_SCRIPTS_DIR}/build-all.sh"
        "DEPS_PREFIX=${_prefix}" "DEPS_WORK=${_prefix}/work" "WITH_TESTS=1")
      file(WRITE "${_stamp}" "${_recipe}")
    endif()
    set(DWARFS_PREFIX "${_prefix}" CACHE PATH "Prefix where dwarfs is installed" FORCE)
    list(PREPEND CMAKE_PREFIX_PATH "${_prefix}")
    # Only this prefix is visible to pkg-config, and static archives are preferred: otherwise system .so files
    # (libcrypto, libzstd, ...) get linked.
    set(ENV{PKG_CONFIG_LIBDIR} "${_prefix}/lib/pkgconfig:${_prefix}/lib64/pkgconfig")
    # Static archives only (libcrypto, libzstd, ... must not resolve to shared libraries from the host).
    if(WIN32)
      set(CMAKE_FIND_LIBRARY_SUFFIXES ".lib")
    else()
      set(CMAKE_FIND_LIBRARY_SUFFIXES ".a")
    endif()
    message(STATUS "rwzfs: using the dependencies in ${_prefix}")

  else()  # system
    set(DWARFS_PREFIX "/usr/local" CACHE PATH "Prefix where dwarfs is installed")
    message(STATUS "rwzfs: using the system dependencies (RWZFS_DEPS=system), dwarfs in ${DWARFS_PREFIX}")
  endif()
endif()
