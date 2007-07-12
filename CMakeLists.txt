project(glob2)
set(PACKAGE_VERSION 0.8.23)

set(CMAKE_MODULE_PATH ${glob2_SOURCE_DIR}/CMake)
find_package(SDL 1.2 REQUIRED)
find_package(SDL_ttf REQUIRED)
find_package(SDL_image REQUIRED)
find_package(SDL_net REQUIRED)
find_package(Boost REQUIRED)
find_package(Vorbis REQUIRED)

add_subdirectory(libgag)
add_subdirectory(src)
