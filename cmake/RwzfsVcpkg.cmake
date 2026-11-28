# vcpkg, before project(). The vcpkg toolchain installs the dependencies in vcpkg.json during project(), so the overlay
# (the dwarfs port and the static triplets) and the triplet must be set here. Values given on the command line win.
# vcpkg is found through VCPKG_ROOT, unless the caller has set CMAKE_TOOLCHAIN_FILE.

if(NOT DEFINED VCPKG_OVERLAY_PORTS)
  set(VCPKG_OVERLAY_PORTS "${CMAKE_CURRENT_SOURCE_DIR}/vcpkg-ports")
endif()
if(NOT DEFINED VCPKG_OVERLAY_TRIPLETS)
  set(VCPKG_OVERLAY_TRIPLETS "${CMAKE_CURRENT_SOURCE_DIR}/vcpkg-triplets")
endif()

if(NOT DEFINED VCPKG_TARGET_TRIPLET)
  # The host is not known before project(), so ask CMake for it.
  cmake_host_system_information(RESULT _rwz_os QUERY OS_NAME)
  cmake_host_system_information(RESULT _rwz_arch QUERY OS_PLATFORM)
  if(_rwz_os STREQUAL "Darwin" AND _rwz_arch STREQUAL "arm64")
    set(VCPKG_TARGET_TRIPLET "arm64-osx-static")
  elseif(_rwz_os STREQUAL "Linux" AND _rwz_arch STREQUAL "x86_64")
    set(VCPKG_TARGET_TRIPLET "x64-linux-static")
  elseif(_rwz_os STREQUAL "Windows" AND _rwz_arch MATCHES "^(x86_64|AMD64)$")
    set(VCPKG_TARGET_TRIPLET "x64-windows-static-rel")
  endif()
endif()

if(NOT CMAKE_TOOLCHAIN_FILE AND DEFINED ENV{VCPKG_ROOT})
  # Windows environment paths use backslashes; CMake would read them as escapes when it embeds the path.
  file(TO_CMAKE_PATH "$ENV{VCPKG_ROOT}" _rwz_vcpkg_root)
  if(EXISTS "${_rwz_vcpkg_root}/scripts/buildsystems/vcpkg.cmake")
    set(CMAKE_TOOLCHAIN_FILE "${_rwz_vcpkg_root}/scripts/buildsystems/vcpkg.cmake")
  endif()
endif()
