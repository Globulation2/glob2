# CMake + Ninja build of the native Linux development client.
#
# SCons (SConstruct, tools/dev_build.py) remains the default build system for
# every target and the only one that ships anything. This file reproduces the
# SCons dev tier object for object, for the everyday edit/build/test loop, and
# the two are compared by tools/compare_build_systems.py; see "CMake + Ninja
# build for daily work" in docs/development/build-options.md.
#
#   cmake --preset dev && cmake --build --preset dev
#   python3 tools/dev_build.py --generator=cmake
#
# Source lists come from cmake/sources.cmake and cmake/tests.cmake, generated
# from scons/sources.py, test/tests.py and scons/harness_programs.py by
# scons/export_cmake_sources.py. Generated headers, runtime assets and test
# provenance come from the same Python code SCons uses, through
# scons/cmake_generate.py.
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
include("${GLOB2_ROOT}/cmake/tests.cmake")
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
    # CCACHE_BASEDIR shares hits between checkouts but rewrites CMake's absolute
    # source paths relative to the build directory, which changes __FILE__;
    # parity builds (tools/compare_build_systems.py) set it empty to keep them.
    set(GLOB2_CCACHE_BASEDIR "${GLOB2_ROOT}" CACHE STRING "CCACHE_BASEDIR for compiles (empty: none)")
    if(GLOB2_CCACHE_BASEDIR)
      set(basedir "CCACHE_BASEDIR=${GLOB2_CCACHE_BASEDIR}")
    else()
      set(basedir --unset=CCACHE_BASEDIR)
    endif()
    set(launcher "${CMAKE_COMMAND}" -E env ${basedir} "CCACHE_DIR=${cache_directory}"
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
option(GLOB2_EXPORT_RUNTIME_ASSETS "Export the runtime assets into the build directory, as the SCons dev tier does" ON)
if(GLOB2_EXPORT_RUNTIME_ASSETS)
  set(GLOB2_RUNTIME_ASSET_DIR "${GLOB2_OUT}/runtime-assets")
else()
  set(GLOB2_RUNTIME_ASSET_DIR "${scons_directory}/runtime-assets" CACHE PATH
      "Runtime assets the CMake glob2 loads when it does not export its own (default: the SCons dev build's)")
endif()
# The SCons dev-tier identity, marked as a CMake build; test/run_tests.py reads its mode.
glob2_bridge(unused identity --builddir "${GLOB2_OUT}")
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
# ---- Runtime assets: scons/runtime_assets.py's export, shared through the store ----
# The bridge writes a depfile naming every asset input and the directories that
# hold them, so the export runs again only when one of them changes.
set(GLOB2_ASSET_JOBS 8 CACHE STRING "Parallel jobs for a runtime-asset export that misses the shared store")
add_custom_target(glob2_runtime_assets)
if(GLOB2_EXPORT_RUNTIME_ASSETS)
  add_custom_command(OUTPUT "${GLOB2_OUT}/runtime-assets.json"
    COMMAND ${bridge} runtime-assets --builddir "${GLOB2_OUT}" --depfile "${GLOB2_OUT}/runtime-assets.d"
            --jobs "${GLOB2_ASSET_JOBS}"
    DEPFILE "${GLOB2_OUT}/runtime-assets.d"
    DEPENDS "${GLOB2_BRIDGE}" "${GLOB2_ROOT}/scons/runtime_assets.py" "${GLOB2_ROOT}/scons/shared_assets.py"
    WORKING_DIRECTORY "${GLOB2_ROOT}" COMMENT "Exporting verified runtime assets" VERBATIM)
  add_custom_target(glob2_runtime_asset_export DEPENDS "${GLOB2_OUT}/runtime-assets.json")
  add_dependencies(glob2_runtime_assets glob2_runtime_asset_export)
endif()

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


# A generator expression selecting the objects of `target` compiled from
# `paths` (repository-relative), as SCons reuses client objects by source.
function(glob2_objects_of output target)
  set(pattern)
  if(NOT ARGN)
    set(${output} "" PARENT_SCOPE)
    return()
  endif()
  foreach(path IN LISTS ARGN)
    string(REGEX REPLACE "([.+])" "\\\\\\1" path "${path}")
    list(APPEND pattern "/${path}\\.o$")
  endforeach()
  list(JOIN pattern "|" pattern)
  set(${output} "$<FILTER:$<TARGET_OBJECTS:${target}>,INCLUDE,${pattern}>" PARENT_SCOPE)
endfunction()

# Link inputs in SConstruct's LIBS order; like SCons' unique merge, a library
# named again later moves to its last position.
set(libraries z pthread ssl crypto ${opengl_libraries} ${fribidi_libraries} ${recording_libraries}
    SDL3_net opusfile opus ogg SDL3_ttf SDL3_image speex ${sdl3_libraries} ${webp_libraries})
list(REVERSE libraries)
list(REMOVE_DUPLICATES libraries)
list(REVERSE libraries)
list(REMOVE_DUPLICATES sdl_directories)
set(link_options ${sdl_link_options} -Wall ${sdl3_link_options} ${webp_link_options})
if(GLOB2_SDL3_PREFIX)
  list(APPEND link_options "-Wl,-rpath=${sdl_prefix}/lib")
endif()
# The SCons dev tier's linker=auto (scons/dev_build.py configure): lld when installed.
set(GLOB2_LINKER auto CACHE STRING "Development linker: default, auto (lld when installed) or lld")
set_property(CACHE GLOB2_LINKER PROPERTY STRINGS default auto lld)
if(NOT GLOB2_LINKER STREQUAL "default")
  find_program(GLOB2_LLD_PROGRAM ld.lld)
  if(GLOB2_LLD_PROGRAM)
    list(APPEND link_options -fuse-ld=lld)
  elseif(GLOB2_LINKER STREQUAL "lld")
    message(FATAL_ERROR "GLOB2_LINKER=lld requires an installed ld.lld")
  endif()
endif()

# Every program links `ARGN` (object libraries and archives) before SConstruct's
# LIBS, with its library directories and LINKFLAGS.
function(glob2_link target)
  target_link_libraries(${target} PRIVATE ${ARGN} ${libraries})
  target_link_directories(${target} PRIVATE ${sdl_directories} ${sdl3_directories} ${webp_directories})
  target_link_options(${target} PRIVATE ${link_options})
  add_dependencies(${target} glob2_generated glob2_build_stamp)
endfunction()

# The numeric guard (tools/javascript/check-math-symbols.py) SCons runs after
# each script-reachable client object; here once they are compiled, before any
# program links them.
glob2_objects_of(guarded_objects glob2_client ${GLOB2_GUARDED_NUMERIC_SOURCES})
add_custom_command(OUTPUT "${stamps}/numeric-guard"
  COMMAND ${numeric_guard} "${guarded_objects}"
  COMMAND "${CMAKE_COMMAND}" -E touch "${stamps}/numeric-guard"
  DEPENDS "${guarded_objects}" "${GLOB2_ROOT}/tools/javascript/check-math-symbols.py"
  COMMENT "Checking script-reachable client objects for host math symbols" COMMAND_EXPAND_LISTS VERBATIM)
add_custom_target(glob2_numeric_guard DEPENDS "${stamps}/numeric-guard")
add_dependencies(glob2_numeric_guard glob2_client)

add_executable(glob2 "${GLOB2_ROOT}/${GLOB2_CLIENT_ENTRY_POINT}")
set_target_properties(glob2 PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${GLOB2_OUT}/src")
target_link_libraries(glob2 PRIVATE glob2_client_settings)
glob2_link(glob2 glob2_client Glob2JavaScript gag usl)
# As SCons' RUNTIME_ASSET_STAMP dependency: glob2, the test binaries and test programs.
add_dependencies(glob2 glob2_runtime_assets glob2_numeric_guard)

# ---- Stage 3: test binaries, test programs and harnesses (test/SConscript, src/SConscript) ----
# None is part of the default build, as in SCons; aliases match the SCons ones
# (engine-tests, unit-tests, tests, transport-test, map-generator-golden-test, ...).
set(alias_names)
function(glob2_alias alias)
  if(NOT TARGET ${alias} OR alias IN_LIST alias_names)
    set_property(GLOBAL APPEND PROPERTY "GLOB2_ALIAS_${alias}" ${ARGN})
    set(names ${alias_names} ${alias})
    list(REMOVE_DUPLICATES names)
    set(alias_names ${names} PARENT_SCOPE)
  endif()
endfunction()

# test/SConscript's test environment: the shared flags without the client's
# SkinRenderRecipe.h directory, plus these include directories and SDL_MAIN_HANDLED.
add_library(glob2_test_settings INTERFACE)
target_include_directories(glob2_test_settings INTERFACE "${GLOB2_ROOT}/test" "${GLOB2_ROOT}/test/support"
                           "${GLOB2_ROOT}/libs/gag/src" "${GLOB2_ROOT}/third_party/quickjs-ng")
target_compile_definitions(glob2_test_settings INTERFACE SDL_MAIN_HANDLED)

# test/SConscript available(): native Linux builds have the WSS transport, are never MinGW
# and build the full compile profile, so every scons/sources.py optional group is present.
function(glob2_available output)
  set(ok TRUE)
  foreach(feature IN LISTS ARGN)
    if(feature STREQUAL "mac" OR (feature STREQUAL "opengl" AND NOT opengl_libraries))
      set(ok FALSE)
    endif()
  endforeach()
  set(${output} ${ok} PARENT_SCOPE)
endfunction()

# One object library per group of registry entries compiled alike; returns their objects.
function(glob2_test_objects output registry prefix)
  set(objects)
  foreach(group IN LISTS GLOB2_${registry}_GROUPS)
    glob2_available(ok ${GLOB2_${registry}_${group}_REQUIRE})
    if(NOT ok)
      continue()
    endif()
    set(sources ${GLOB2_${registry}_${group}_SOURCES})
    list(TRANSFORM sources PREPEND "${GLOB2_ROOT}/")
    add_library(${prefix}_${group} OBJECT EXCLUDE_FROM_ALL ${sources})
    target_link_libraries(${prefix}_${group} PRIVATE glob2_test_settings)
    target_compile_options(${prefix}_${group} PRIVATE ${GLOB2_${registry}_${group}_CXXFLAGS})
    target_compile_definitions(${prefix}_${group} PRIVATE ${GLOB2_${registry}_${group}_DEFINES})
    add_dependencies(${prefix}_${group} glob2_generated glob2_build_stamp)
    list(APPEND objects "$<TARGET_OBJECTS:${prefix}_${group}>")
  endforeach()
  set(${output} ${objects} PARENT_SCOPE)
endfunction()

# The configuration a provenance sidecar records, as test_provenance._configuration does for SCons.
get_directory_property(provenance_defines COMPILE_DEFINITIONS)
set(provenance_arguments "--compiler=${CMAKE_CXX_COMPILER}" "--ccflags=${CMAKE_CXX_FLAGS}"
    "--cxxflags=-std=gnu++20 -Wall -fPIC ${GLOB2_EXTRA_COMPILE_FLAGS}")
foreach(define IN LISTS provenance_defines ITEMS SDL_MAIN_HANDLED)
  list(APPEND provenance_arguments "--define=${define}")
endforeach()
foreach(option IN LISTS link_options)
  list(APPEND provenance_arguments "--linkflag=${option}")
endforeach()
set(provenance_configuration "${GLOB2_OUT}/test-provenance-configuration.json")
glob2_bridge(unused provenance-configuration --output "${provenance_configuration}" ${provenance_arguments})

# <binary>.provenance.json after every build of the alias, like SCons' AlwaysBuild sidecar.
function(glob2_provenance target)
  add_custom_target(${target}-provenance
    COMMAND ${bridge} test-provenance --binary "$<TARGET_FILE:${target}>" --configuration "${provenance_configuration}"
    WORKING_DIRECTORY "${GLOB2_ROOT}" COMMENT "Recording $<TARGET_FILE:${target}>.provenance.json" VERBATIM)
  add_dependencies(${target}-provenance ${target})
endfunction()

function(glob2_test_binary target)
  set_target_properties(${target} PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${GLOB2_OUT}/test")
endfunction()

# Production sources compiled again with the test flags, shared by the binaries
# that link them, as test/SConscript shares its 'unit-' objects.
glob2_test_objects(unused TEST_PRODUCTION glob2_test_production)
set(production_libraries)
foreach(group IN LISTS GLOB2_TEST_PRODUCTION_GROUPS)
  if(TARGET glob2_test_production_${group})
    list(APPEND production_libraries glob2_test_production_${group})
  endif()
endforeach()

# The module libraries (scons/sources.py MODULE_SOURCES): archives of client objects
# that only the module test binaries link.
foreach(module map sim)
  string(TOUPPER "${module}" upper)
  glob2_objects_of(module_objects glob2_client ${GLOB2_${upper}_SOURCES})
  add_library(glob2_${module} STATIC EXCLUDE_FROM_ALL "${module_objects}")
  set_target_properties(glob2_${module} PROPERTIES LINKER_LANGUAGE CXX)
  add_dependencies(glob2_${module} glob2_client)
endforeach()

foreach(binary IN LISTS GLOB2_TEST_BINARIES)
  set(target ${GLOB2_${binary}_TARGET})
  string(TOLOWER "glob2_${binary}" prefix)
  glob2_test_objects(objects ${binary} ${prefix})
  glob2_objects_of(reused glob2_client ${GLOB2_${binary}_REUSED_CLIENT_SOURCES})
  set(production)
  foreach(library IN LISTS production_libraries)
    glob2_objects_of(selected ${library} ${GLOB2_${binary}_PRODUCTION_SOURCES})
    list(APPEND production ${selected})
  endforeach()
  add_executable(${target} EXCLUDE_FROM_ALL ${objects} ${reused} ${production})
  add_dependencies(${target} glob2_client ${production_libraries})
  if(GLOB2_${binary}_ALL_CLIENT_OBJECTS)
    glob2_link(${target} glob2_client Glob2JavaScript gag usl)
  elseif(GLOB2_${binary}_MODULES)
    # Map and sim still reference each other: GNU ld resolves the archives as one
    # group, as test/SConscript's --start-group does.
    set(group ${GLOB2_${binary}_MODULES})
    list(TRANSFORM group PREPEND glob2_)
    list(APPEND group Glob2JavaScript gag usl)
    list(JOIN group "," group)
    glob2_link(${target} "$<LINK_GROUP:RESCAN,${group}>")
  else()
    glob2_link(${target} Glob2JavaScript gag usl)
  endif()
  glob2_test_binary(${target})
  add_dependencies(${target} glob2_runtime_assets glob2_numeric_guard)
  glob2_provenance(${target})
  foreach(alias IN LISTS GLOB2_${binary}_ALIASES)
    glob2_alias(${alias} ${target} ${target}-provenance)
  endforeach()
endforeach()
foreach(entry IN LISTS GLOB2_TEST_LEGACY_ALIASES)
  string(REPLACE "=" ";" entry "${entry}")
  list(GET entry 0 alias)
  list(GET entry 1 binary)
  glob2_alias(${alias} ${binary})
endforeach()

# test/tests.py PROGRAMS: the test flags, every client object.
foreach(program IN LISTS GLOB2_TEST_PROGRAMS)
  glob2_available(ok ${GLOB2_TEST_PROGRAM_${program}_REQUIRE})
  if(NOT ok)
    continue()
  endif()
  add_executable(${program} EXCLUDE_FROM_ALL "${GLOB2_ROOT}/${GLOB2_TEST_PROGRAM_${program}_SOURCE}")
  target_link_libraries(${program} PRIVATE glob2_test_settings)
  glob2_link(${program} glob2_client Glob2JavaScript gag usl)
  add_dependencies(${program} glob2_runtime_assets glob2_numeric_guard)
  glob2_test_binary(${program})
  set(outputs ${program})
  if(program IN_LIST GLOB2_TEST_PROGRAM_SIDECARS)
    glob2_provenance(${program})
    list(APPEND outputs ${program}-provenance)
  endif()
  glob2_alias(${GLOB2_TEST_PROGRAM_${program}_ALIAS} ${outputs})
  if(GLOB2_TEST_PROGRAM_${program}_GROUP STREQUAL "tools")
    glob2_alias(glob2-tools ${outputs})
  endif()
endforeach()

# scons/harness_programs.py: the client flags; objects as each program's link kind says.
foreach(name IN LISTS GLOB2_HARNESS_SHARED_OBJECTS)
  add_library(glob2_harness_${name} OBJECT EXCLUDE_FROM_ALL "${GLOB2_ROOT}/${GLOB2_HARNESS_SHARED_${name}_SOURCE}")
  target_link_libraries(glob2_harness_${name} PRIVATE glob2_client_settings)
  add_dependencies(glob2_harness_${name} glob2_generated glob2_build_stamp)
endforeach()
foreach(program IN LISTS GLOB2_HARNESS_PROGRAMS)
  set(prefix GLOB2_HARNESS_${program})
  # Native Linux builds always have the WSS transport, the only requirement used.
  set(sources "${GLOB2_ROOT}/${${prefix}_SOURCE}")
  set(inputs)
  foreach(name IN LISTS ${prefix}_SHARED)
    list(APPEND inputs glob2_harness_${name})
  endforeach()
  if(${prefix}_LINK STREQUAL "client" OR ${prefix}_LINK STREQUAL "client-objects")
    list(APPEND inputs glob2_client Glob2JavaScript gag usl)
  elseif(${prefix}_LINK STREQUAL "libraries")
    list(APPEND inputs gag usl)
  else()
    glob2_objects_of(linked glob2_client ${${prefix}_LINK_SOURCES})
    list(APPEND sources "${linked}")
  endif()
  add_executable(${program} EXCLUDE_FROM_ALL ${sources})
  set_target_properties(${program} PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${GLOB2_OUT}/src")
  target_link_libraries(${program} PRIVATE glob2_client_settings)
  target_compile_options(${program} PRIVATE ${${prefix}_CXXFLAGS})
  if(NOT ${prefix}_LINK STREQUAL "libraries")
    add_dependencies(${program} glob2_client glob2_numeric_guard)
  endif()
  glob2_link(${program} ${inputs})
  glob2_alias(${${prefix}_ALIAS} ${program})
endforeach()

foreach(alias IN LISTS alias_names)
  get_property(members GLOBAL PROPERTY "GLOB2_ALIAS_${alias}")
  list(REMOVE_DUPLICATES members)
  add_custom_target(${alias})
  add_dependencies(${alias} ${members})
endforeach()
