set(VCPKG_TARGET_ARCHITECTURE arm)
set(VCPKG_MAKE_BUILD_TRIPLET "--host=arm-linux-androideabi")
set(VCPKG_CMAKE_CONFIGURE_OPTIONS -DANDROID_ABI=armeabi-v7a)
include("${CMAKE_CURRENT_LIST_DIR}/mobile-common.cmake")
