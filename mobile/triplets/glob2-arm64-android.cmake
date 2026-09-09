set(VCPKG_TARGET_ARCHITECTURE arm64)
set(VCPKG_MAKE_BUILD_TRIPLET "--host=aarch64-linux-android")
set(VCPKG_CMAKE_CONFIGURE_OPTIONS -DANDROID_ABI=arm64-v8a)
include("${CMAKE_CURRENT_LIST_DIR}/mobile-common.cmake")
