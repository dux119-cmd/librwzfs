# Static libraries and the static CRT (matching rwzfs's MultiThreaded runtime), release only. vcpkg's built-in
# x64-windows-static builds debug too, which doubles the time; this triplet is ours.
set(VCPKG_TARGET_ARCHITECTURE x64)
set(VCPKG_CRT_LINKAGE static)
set(VCPKG_LIBRARY_LINKAGE static)
set(VCPKG_BUILD_TYPE release)
