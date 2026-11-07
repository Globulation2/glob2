# Experimental CMake + Ninja build of the native Linux development client.
#
# SCons (SConstruct, tools/dev_build.py) remains the supported build system for
# every target and the only one that ships anything. This file reproduces the
# SCons fast-development tier object for object so the two can be compared
# (tools/compare_build_systems.py); see "Experimental CMake build" in
# docs/development/build-options.md for what it covers and what it does not.
#
#   cmake --preset dev && cmake --build --preset dev
#   python3 tools/dev_build.py --generator=cmake
#
# Source lists come from cmake/sources.cmake, generated from scons/sources.py by
# scons/export_cmake_sources.py. Generated headers come from the same Python
# generators SCons uses, through scons/cmake_generate.py.
cmake_minimum_required(VERSION 3.25)
project(glob2 LANGUAGES C CXX)

if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux" OR CMAKE_CROSSCOMPILING)
  message(FATAL_ERROR "The experimental CMake build covers native Linux development builds only; "
                      "use SCons (tools/dev_build.py) for every other target.")
endif()
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_BINARY_DIR)
  message(FATAL_ERROR "Configure into a separate build directory, for example: cmake --preset dev")
endif()

find_package(Python3 3.8 REQUIRED COMPONENTS Interpreter)
find_program(GLOB2_PKG_CONFIG pkg-config REQUIRED)
set(GLOB2_ROOT "${CMAKE_SOURCE_DIR}")
set(GLOB2_OUT "${CMAKE_BINARY_DIR}")
set(GLOB2_BRIDGE "${GLOB2_ROOT}/scons/cmake_generate.py")
include("${GLOB2_ROOT}/cmake/sources.cmake")
# The bridge and the generators it imports decide configure-time results.
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
  "${GLOB2_BRIDGE}" "${GLOB2_ROOT}/scons/build_config.py" "${GLOB2_ROOT}/scons/checkout_paths.py"
  "${GLOB2_ROOT}/scons/build_layout.py" "${GLOB2_ROOT}/scons/client_features.py"
  "${GLOB2_ROOT}/scons/official_instance.py" "${GLOB2_ROOT}/scons/recording_dependencies.py")

# Runs a scons/cmake_generate.py subcommand at configure time.
function(glob2_bridge output)
  execute_process(COMMAND "${Python3_EXECUTABLE}" "${GLOB2_BRIDGE}" ${ARGN}
                  WORKING_DIRECTORY "${GLOB2_ROOT}"
                  OUTPUT_VARIABLE result RESULT_VARIABLE status OUTPUT_STRIP_TRAILING_WHITESPACE)
  if(NOT status EQUAL 0)
    message(FATAL_ERROR "scons/cmake_generate.py ${ARGN} failed")
  endif()
  set(${output} "${result}" PARENT_SCOPE)
endfunction()

# Splits pkg-config output the way SCons ParseConfig does: -I to the include
# path, -D to the definitions, linker words to the link line, the rest to the
# compile options.
function(glob2_pkg_config prefix)
  execute_process(COMMAND "${GLOB2_PKG_CONFIG}" ${ARGN} OUTPUT_VARIABLE words
                  RESULT_VARIABLE status OUTPUT_STRIP_TRAILING_WHITESPACE)
  if(NOT status EQUAL 0)
    message(FATAL_ERROR "pkg-config ${ARGN} failed")
  endif()
  separate_arguments(words UNIX_COMMAND "${words}")
  foreach(kind includes defines options libraries directories link_options)
    set(${kind})
  endforeach()
  foreach(word IN LISTS words)
    if(word MATCHES "^-I(.+)")
      list(APPEND includes "${CMAKE_MATCH_1}")
    elseif(word MATCHES "^-D(.+)")
      list(APPEND defines "${CMAKE_MATCH_1}")
    elseif(word MATCHES "^-l(.+)")
      list(APPEND libraries "${CMAKE_MATCH_1}")
    elseif(word MATCHES "^-L(.+)")
      list(APPEND directories "${CMAKE_MATCH_1}")
    elseif(word MATCHES "^-Wl,")
      list(APPEND link_options "${word}")
    else()
      list(APPEND options "${word}")
    endif()
  endforeach()
  foreach(kind includes defines options libraries directories link_options)
    set(${prefix}_${kind} "${${kind}}" PARENT_SCOPE)
  endforeach()
endfunction()

# ---- Compiler cache: the same managed ccache store and settings as SCons ----
option(GLOB2_CCACHE "Compile through ccache (the managed SCons cache) when it is installed" ON)
if(GLOB2_CCACHE AND NOT CMAKE_CXX_COMPILER_LAUNCHER)
  find_program(GLOB2_CCACHE_PROGRAM ccache)
  if(GLOB2_CCACHE_PROGRAM)
    glob2_bridge(cache_directory ccache-directory)
    if(DEFINED ENV{CCACHE_COMPILERCHECK})
      set(compiler_check "$ENV{CCACHE_COMPILERCHECK}")
    else()
      set(compiler_check content)
    endif()
    set(launcher "${CMAKE_COMMAND}" -E env "CCACHE_DIR=${cache_directory}" "CCACHE_BASEDIR=${GLOB2_ROOT}"
                 "CCACHE_COMPILERCHECK=${compiler_check}" "${GLOB2_CCACHE_PROGRAM}")
    set(CMAKE_C_COMPILER_LAUNCHER "${launcher}")
    set(CMAKE_CXX_COMPILER_LAUNCHER "${launcher}")
  endif()
endif()

# ---- Dependencies, located as SConstruct does ----
set(GLOB2_SDL3_PREFIX "$ENV{GLOB2_SDL3_PREFIX}" CACHE PATH "Isolated SDL3 dependency prefix (GLOB2_SDL3_PREFIX)")
set(sdl_includes)
set(sdl_link_options)
set(sdl_directories)
if(GLOB2_SDL3_PREFIX)
  file(REAL_PATH "${GLOB2_SDL3_PREFIX}" sdl_prefix)
  set(ENV{PKG_CONFIG_PATH} "${sdl_prefix}/lib/pkgconfig:$ENV{PKG_CONFIG_PATH}")
  set(sdl_includes "${sdl_prefix}/include")
  set(sdl_directories "${sdl_prefix}/lib")
  # Development runs load the prefix in place; installed copies (a later stage)
  # use only $ORIGIN/../lib/glob2.
  set(sdl_link_options "-Wl,-rpath,$ORIGIN/../lib/glob2")
endif()

glob2_bridge(scons_directory scons-dev-directory)
if(DEFINED ENV{GLOB2_RECORDING_PREFIX})
  set(recording_default "$ENV{GLOB2_RECORDING_PREFIX}")
elseif(GLOB2_SDL3_PREFIX AND EXISTS "${sdl_prefix}/recording/recording-manifest.json")
  set(recording_default "${sdl_prefix}/recording")
else()
  # SCons builds the pinned recording libraries into its own build directory.
  set(recording_default "${scons_directory}/recording/prefix")
endif()
set(GLOB2_RECORDING_PREFIX "${recording_default}" CACHE PATH "Pinned h264 recording library prefix")
if(NOT EXISTS "${GLOB2_RECORDING_PREFIX}/include")
  message(FATAL_ERROR "No recording prefix at ${GLOB2_RECORDING_PREFIX}. Set GLOB2_SDL3_PREFIX to a "
                      "shared prefix with recording/, or run tools/dev_build.py once so SCons builds one.")
endif()
file(REAL_PATH "${GLOB2_RECORDING_PREFIX}" recording_prefix)
glob2_bridge(recording_libraries recording --prefix "${recording_prefix}")

glob2_pkg_config(opusfile opusfile --cflags)
glob2_pkg_config(sdl3 sdl3 --cflags --libs)
glob2_pkg_config(webp libwebp --cflags --libs --static)

include(CheckIncludeFileCXX)
find_library(GLOB2_GL_LIBRARY GL)
find_library(GLOB2_GLU_LIBRARY GLU)
find_library(GLOB2_EPOXY_LIBRARY epoxy)
find_library(GLOB2_FRIBIDI_LIBRARY fribidi)
check_include_file_cxx(GL/gl.h GLOB2_HAVE_GL_H)
check_include_file_cxx(GL/glu.h GLOB2_HAVE_GLU_H)
check_include_file_cxx(epoxy/gl.h GLOB2_HAVE_EPOXY_H)
check_include_file_cxx(fribidi/fribidi.h GLOB2_HAVE_FRIBIDI_H)
set(opengl_libraries)
set(build_config_options)
if(GLOB2_GL_LIBRARY AND GLOB2_HAVE_GL_H AND GLOB2_GLU_LIBRARY AND GLOB2_HAVE_GLU_H
   AND GLOB2_EPOXY_LIBRARY AND GLOB2_HAVE_EPOXY_H)
  set(opengl_libraries GL GLU epoxy)
  list(APPEND build_config_options --opengl)
else()
  message(STATUS "One or more library missing for OpenGL support, disabling OpenGL support.")
endif()
set(fribidi_libraries)
if(GLOB2_FRIBIDI_LIBRARY AND GLOB2_HAVE_FRIBIDI_H)
  set(fribidi_libraries fribidi)
  list(APPEND build_config_options --fribidi)
endif()

# ---- Generated headers: the SCons generators, called through the bridge ----
set(GLOB2_RUNTIME_ASSET_DIR "${scons_directory}/runtime-assets" CACHE PATH
    "Runtime assets the CMake glob2 loads; CMake does not export them yet, so this defaults to the SCons dev build's")
glob2_bridge(unused build-config --builddir "${GLOB2_OUT}" ${build_config_options})
glob2_bridge(unused build-paths --builddir "${GLOB2_OUT}" --runtime-assets "${GLOB2_RUNTIME_ASSET_DIR}")
glob2_bridge(unused build-stamp --builddir "${GLOB2_OUT}")

set(bridge "${Python3_EXECUTABLE}" "${GLOB2_BRIDGE}")
set(stamps "${GLOB2_OUT}/generated-stamps")
add_custom_command(OUTPUT "${stamps}/skin-materials"
  BYPRODUCTS "${GLOB2_OUT}/include/glob2/SkinMaterials.h"
  COMMAND ${bridge} skin-materials --builddir "${GLOB2_OUT}" --stamp "${stamps}/skin-materials"
  DEPENDS "${GLOB2_BRIDGE}" "${GLOB2_ROOT}/scons/skin_materials.py"
          "${GLOB2_ROOT}/libs/gag/shaders/skin-materials.json" "${GLOB2_ROOT}/libs/gag/shaders/skin-material.glsl"
  WORKING_DIRECTORY "${GLOB2_ROOT}" COMMENT "Generating glob2/SkinMaterials.h" VERBATIM)

file(GLOB shader_globs CONFIGURE_DEPENDS "${GLOB2_ROOT}/libs/gag/shaders/*.glsl")
glob2_bridge(shader_inputs shader-inputs)
add_custom_command(OUTPUT "${stamps}/shaders"
  BYPRODUCTS "${GLOB2_OUT}/shaders/RendererShaderLayouts.h"
  COMMAND ${bridge} shaders --builddir "${GLOB2_OUT}" --stamp "${stamps}/shaders"
  DEPENDS "${GLOB2_BRIDGE}" "${GLOB2_ROOT}/scons/renderer_shaders.py" ${shader_inputs}
          "${GLOB2_ROOT}/tools/shaders/gradient_compute.py" "${GLOB2_ROOT}/tools/shaders/gradient_preparation_rules.py"
  WORKING_DIRECTORY "${GLOB2_ROOT}" COMMENT "Generating renderer shader assets" VERBATIM)

add_custom_command(OUTPUT "${stamps}/hive-api"
  BYPRODUCTS "${GLOB2_OUT}/include/hive/PlayerAPIContract.h"
  COMMAND ${bridge} hive-api --builddir "${GLOB2_OUT}" --stamp "${stamps}/hive-api"
  DEPENDS "${GLOB2_BRIDGE}" "${GLOB2_ROOT}/scons/hive_api.py"
          "${GLOB2_ROOT}/services/online/packages/hive/src/commander-api.txt"
  WORKING_DIRECTORY "${GLOB2_ROOT}" COMMENT "Generating Hive player API contract" VERBATIM)

set(locales "${GLOB2_ROOT}/services/online/packages/i18n/locales")
add_custom_command(OUTPUT "${GLOB2_OUT}/include/generated/OnlineErrorCatalog.h"
  COMMAND "${Python3_EXECUTABLE}" "${GLOB2_ROOT}/tools/localization/build_online_error_catalog.py"
          --english "${locales}/en.json" --bulgarian "${locales}/bg.json" --thai "${locales}/th.json"
          --output "${GLOB2_OUT}/include/generated/OnlineErrorCatalog.h"
  DEPENDS "${GLOB2_ROOT}/tools/localization/build_online_error_catalog.py"
          "${locales}/en.json" "${locales}/bg.json" "${locales}/th.json"
  WORKING_DIRECTORY "${GLOB2_ROOT}" COMMENT "Embedding reviewed Bulgarian and Thai API error text" VERBATIM)

file(GLOB skin_globs CONFIGURE_DEPENDS "${GLOB2_ROOT}/content/data/skins/colony-v1/*")
glob2_bridge(skin_render_inputs skin-render-inputs)
add_custom_command(OUTPUT "${stamps}/skin-render"
  BYPRODUCTS "${GLOB2_OUT}/src/SkinRenderRecipe.h"
  COMMAND ${bridge} skin-render --builddir "${GLOB2_OUT}" --stamp "${stamps}/skin-render"
  DEPENDS "${GLOB2_BRIDGE}" "${GLOB2_ROOT}/scons/skin_render.py" ${skin_render_inputs}
  WORKING_DIRECTORY "${GLOB2_ROOT}" COMMENT "Generating SkinRenderRecipe.h" VERBATIM)

add_custom_target(glob2_generated DEPENDS
  "${stamps}/skin-materials" "${stamps}/shaders" "${stamps}/hive-api" "${stamps}/skin-render"
  "${GLOB2_OUT}/include/generated/OnlineErrorCatalog.h")
# SCons refreshes the revision on every invocation; so does this (it rewrites
# the header only when the revision changed).
add_custom_target(glob2_build_stamp
  COMMAND ${bridge} build-stamp --builddir "${GLOB2_OUT}"
  BYPRODUCTS "${GLOB2_OUT}/include/glob2/BuildStamp.h"
  WORKING_DIRECTORY "${GLOB2_ROOT}" VERBATIM)

# ---- Flags shared by every object, in SConstruct's order ----
# The preset supplies the tier flags (-O0 -g1 -fdebug-prefix-map) through
# CMAKE_<LANG>_FLAGS, as dev_build.configure and cache_flags do for SCons.
add_compile_options("$<$<COMPILE_LANGUAGE:CXX>:-std=gnu++20;-Wall;-fPIC>" "$<$<COMPILE_LANGUAGE:C>:-fPIC>")
set(GLOB2_EXTRA_COMPILE_FLAGS "" CACHE STRING "Extra compile flags (tools/compare_build_systems.py parity builds)")
if(GLOB2_EXTRA_COMPILE_FLAGS)
  separate_arguments(extra_flags UNIX_COMMAND "${GLOB2_EXTRA_COMPILE_FLAGS}")
  add_compile_options(${extra_flags})
endif()

glob2_bridge(instance official-instance)
string(JSON official_origin GET "${instance}" origin)
string(JSON former_origins GET "${instance}" former)
add_compile_definitions(
  "GLOB2_OFFICIAL_INSTANCE_ORIGIN=\"${official_origin}\""
  "GLOB2_OFFICIAL_INSTANCE_FORMER_ORIGINS=\"${former_origins}\""
  HAVE_CONFIG_H GLOB2_H264_ASSETS
  ${opusfile_defines} ${sdl3_defines} ${webp_defines})
add_compile_options(${opusfile_options} ${sdl3_options} ${webp_options})

set(include_directories "${recording_prefix}/include" ${sdl_includes} "${GLOB2_OUT}/include"
    ${opusfile_includes} "${GLOB2_OUT}/shaders")
foreach(directory IN LISTS GLOB2_INCLUDE_DIRECTORIES)
  if(directory STREQUAL ".")
    list(APPEND include_directories "${GLOB2_ROOT}")
  else()
    list(APPEND include_directories "${GLOB2_ROOT}/${directory}")
  endif()
endforeach()
list(APPEND include_directories ${sdl3_includes} ${webp_includes})
list(REMOVE_DUPLICATES include_directories)
include_directories(${include_directories})

list(TRANSFORM GLOB2_CLIENT_SOURCES PREPEND "${GLOB2_ROOT}/")
list(TRANSFORM GLOB2_STRICT_NUMERIC_SOURCES PREPEND "${GLOB2_ROOT}/")
list(TRANSFORM GLOB2_GAG_SOURCES PREPEND "${GLOB2_ROOT}/")
list(TRANSFORM GLOB2_USL_SOURCES PREPEND "${GLOB2_ROOT}/")
list(TRANSFORM GLOB2_JAVASCRIPT_SOURCES PREPEND "${GLOB2_ROOT}/")
list(TRANSFORM GLOB2_JAVASCRIPT_INCLUDE_DIRECTORIES PREPEND "${GLOB2_ROOT}/")

# ---- Stage 1: libraries ----
add_library(usl STATIC ${GLOB2_USL_SOURCES})
add_library(gag STATIC ${GLOB2_GAG_SOURCES})

# QuickJS and openlibm: the interpreter's strict numeric contract, per source
# exactly as scons/javascript.py compiles them.
add_library(Glob2JavaScript STATIC ${GLOB2_JAVASCRIPT_SOURCES})
set_source_files_properties(${GLOB2_JAVASCRIPT_SOURCES} PROPERTIES
  COMPILE_OPTIONS "-O0;${GLOB2_JAVASCRIPT_CFLAGS}"
  COMPILE_DEFINITIONS "${GLOB2_JAVASCRIPT_DEFINES}")
# A source's INCLUDE_DIRECTORIES would precede the shared path; SCons appends
# these, and the archive holds nothing else.
target_include_directories(Glob2JavaScript PRIVATE ${GLOB2_JAVASCRIPT_INCLUDE_DIRECTORIES})

# The numeric guard (tools/javascript/check-math-symbols.py) SCons runs after
# each script-reachable object. Here it runs once the archive or program links.
set(numeric_guard "${Python3_EXECUTABLE}" "${GLOB2_ROOT}/tools/javascript/check-math-symbols.py" --nm "${CMAKE_NM}")
add_custom_command(TARGET Glob2JavaScript POST_BUILD
  COMMAND ${numeric_guard} "$<TARGET_OBJECTS:Glob2JavaScript>"
  COMMAND_EXPAND_LISTS VERBATIM)

foreach(target usl gag Glob2JavaScript)
  add_dependencies(${target} glob2_generated glob2_build_stamp)
endforeach()

# ---- Stage 2: client objects and the glob2 executable ----
# Client objects also see SkinRenderRecipe.h's directory and the QuickJS headers.
add_library(glob2_client_settings INTERFACE)
target_include_directories(glob2_client_settings INTERFACE "${GLOB2_OUT}/src" "${GLOB2_ROOT}/third_party/quickjs-ng")

set(client_sources ${GLOB2_CLIENT_SOURCES})
list(REMOVE_ITEM client_sources "${GLOB2_ROOT}/${GLOB2_CLIENT_ENTRY_POINT}")
# Every client object except the entry point, as src/SConscript exports for tests.
add_library(glob2_client OBJECT ${client_sources})
target_link_libraries(glob2_client PRIVATE glob2_client_settings)
add_dependencies(glob2_client glob2_generated glob2_build_stamp)
set_source_files_properties(${GLOB2_STRICT_NUMERIC_SOURCES} PROPERTIES COMPILE_OPTIONS "${GLOB2_STRICT_CXXFLAGS}")

add_executable(glob2 "${GLOB2_ROOT}/${GLOB2_CLIENT_ENTRY_POINT}")
set_target_properties(glob2 PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${GLOB2_OUT}/src")
add_dependencies(glob2 glob2_generated glob2_build_stamp)

# Link inputs in SConstruct's LIBS order; like SCons' unique merge, a library
# named again later moves to its last position.
set(libraries z pthread ssl crypto ${opengl_libraries} ${fribidi_libraries} ${recording_libraries}
    SDL3_net opusfile opus ogg SDL3_ttf SDL3_image speex ${sdl3_libraries} ${webp_libraries})
list(REVERSE libraries)
list(REMOVE_DUPLICATES libraries)
list(REVERSE libraries)
target_link_libraries(glob2 PRIVATE glob2_client_settings glob2_client Glob2JavaScript gag usl ${libraries})
list(REMOVE_DUPLICATES sdl_directories)
target_link_directories(glob2 PRIVATE ${sdl_directories} ${sdl3_directories} ${webp_directories})
set(link_options ${sdl_link_options} -Wall ${sdl3_link_options} ${webp_link_options})
if(GLOB2_SDL3_PREFIX)
  list(APPEND link_options "-Wl,-rpath=${sdl_prefix}/lib")
endif()
target_link_options(glob2 PRIVATE ${link_options})

list(TRANSFORM GLOB2_GUARDED_NUMERIC_SOURCES PREPEND "${GLOB2_ROOT}/")
set(guarded_pattern)
foreach(source IN LISTS GLOB2_GUARDED_NUMERIC_SOURCES)
  file(RELATIVE_PATH relative "${GLOB2_ROOT}" "${source}")
  string(REGEX REPLACE "([.+])" "\\\\\\1" relative "${relative}")
  list(APPEND guarded_pattern "/${relative}\\.o$")
endforeach()
list(JOIN guarded_pattern "|" guarded_pattern)
add_custom_command(TARGET glob2 POST_BUILD
  COMMAND ${numeric_guard} "$<FILTER:$<TARGET_OBJECTS:glob2_client>,INCLUDE,${guarded_pattern}>"
  COMMAND_EXPAND_LISTS VERBATIM)
