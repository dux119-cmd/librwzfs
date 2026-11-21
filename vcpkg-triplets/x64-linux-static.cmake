# Static libraries, for linking dwarfs statically into rwzfs (vcpkg's Linux default is shared).
set(VCPKG_TARGET_ARCHITECTURE x64)
set(VCPKG_CRT_LINKAGE dynamic)
set(VCPKG_LIBRARY_LINKAGE static)
# Release only: rwzfs and dwarfs ship release builds, and a debug build of every package doubles the time.
set(VCPKG_BUILD_TYPE release)
set(VCPKG_CMAKE_SYSTEM_NAME Linux)
