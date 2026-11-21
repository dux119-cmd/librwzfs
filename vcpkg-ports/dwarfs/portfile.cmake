# dwarfs is not in the vcpkg registry: this overlay port builds it from the upstream release. Its
# dependencies are the ones in vcpkg.json (the same list scripts/deps/versions.sh pins by hand).
#
# SHA512: vcpkg prints the correct value when it first downloads the archive with a mismatch. Replace the
# placeholder with that value before using the port (upstream acceptance needs the real hash).
vcpkg_from_github(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO mhx/dwarfs
    REF "v${VERSION}"
    SHA512 07d24dc441132c16cd3cf4519ff6db457d548b1fab93c0ecbb3c43dc93d2046aa382de7f946cca8e2c921e20547817ec039ce2000d47bd49b3931ddcefcc571f
    HEAD_REF master
)

vcpkg_check_features(OUT_FEATURE_OPTIONS FEATURE_OPTIONS
    FEATURES tools WITH_TOOLS
)

# The same feature set as scripts/deps/dwarfs.sh: zstd and FLAC compression, no FUSE, no optional codecs.
if(VCPKG_TARGET_IS_OSX)
    # Apple's libc++ lacks std::chrono::from_stream, which dwarfs's timestamp parser uses.
    find_program(RWZ_PYTHON3 NAMES python3 REQUIRED)
    vcpkg_execute_required_process(
        COMMAND "${RWZ_PYTHON3}" "${CMAKE_CURRENT_LIST_DIR}/patch-dwarfs-apple.py" "${SOURCE_PATH}"
        WORKING_DIRECTORY "${SOURCE_PATH}"
        LOGNAME patch-apple)
endif()

vcpkg_cmake_configure(
    SOURCE_PATH "${SOURCE_PATH}"
    OPTIONS
        ${FEATURE_OPTIONS}
        -DWITH_LIBDWARFS=ON
        -DWITH_TESTS=OFF
        -DWITH_BENCHMARKS=OFF
        -DWITH_FUZZ=OFF
        -DWITH_EXAMPLE=OFF
        -DWITH_DEV_TOOLS=OFF
        -DWITH_FUSE_DRIVER=OFF
        -DWITH_FUSE_EXTRACT_BINARY=OFF
        -DWITH_LEGACY_FUSE=OFF
        -DWITH_DESKTOP_INTEGRATION=OFF
        -DWITH_MAN_PAGES=OFF
        -DWITH_MAN_OPTION=OFF
        -DWITH_PXATTR=OFF
        -DWITH_UNIVERSAL_BINARY=OFF
        -DTRY_ENABLE_FLAC=ON
        -DTRY_ENABLE_LZ4=OFF
        -DTRY_ENABLE_LZMA=OFF
        -DTRY_ENABLE_BROTLI=OFF
        -DENABLE_RICEPP=OFF
        -DUSE_JEMALLOC=OFF
        -DUSE_MIMALLOC=OFF
        -DENABLE_PERFMON=OFF
        -DDISABLE_FILESYSTEM_EXTRACTOR_FORMAT=ON
        -DENABLE_STACKTRACE=OFF
        -DDISABLE_CCACHE=ON
)

vcpkg_cmake_install()

if(EXISTS "${CURRENT_PACKAGES_DIR}/lib/cmake/dwarfs")
    vcpkg_cmake_config_fixup(PACKAGE_NAME dwarfs CONFIG_PATH lib/cmake/dwarfs)
endif()

# Release only, as the rest of rwzfs's dependency build.
file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/debug")
file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/debug/include")

if(EXISTS "${SOURCE_PATH}/LICENSE")
    file(INSTALL "${SOURCE_PATH}/LICENSE" DESTINATION "${CURRENT_PACKAGES_DIR}/share/${PORT}" RENAME copyright)
endif()
