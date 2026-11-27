# Desktop Linux Crashpad (buildsys/crashpad_dependencies.py): static release
# archives linked into the game, and the handler executable shipped beside it.
set(VCPKG_TARGET_ARCHITECTURE x64)
set(VCPKG_CRT_LINKAGE dynamic)
set(VCPKG_LIBRARY_LINKAGE static)
set(VCPKG_CMAKE_SYSTEM_NAME Linux)
set(VCPKG_BUILD_TYPE release)
set(VCPKG_FIXUP_ELF_RPATH ON)
