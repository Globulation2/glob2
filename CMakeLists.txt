# CMake + Ninja build of the native Linux, macOS and Windows (MSYS2 MinGW) client.
#
# GLOB2_BUILD_ARGS takes the same key=value build arguments SCons does
# (dev_fast=1, perf=1 opt=3, release=1, ...); buildsys/build_layout.py resolves
# them into the build identity, so both build systems agree on the tier, its
# flags and its default directory. Each tier reproduces the SCons build object
# for object, compared by tools/compare_build_systems.py; see "CMake + Ninja
# build" in docs/development/build-options.md.
#
#   cmake --preset dev && cmake --build --preset dev
#   cmake --preset perf && cmake --build --preset perf
#   python3 tools/dev_build.py [perf=1 [opt=3] | release=1]
#
# Source lists come from cmake/sources.cmake and cmake/tests.cmake, generated
# from buildsys/sources.py, test/tests.py and buildsys/harness_programs.py by
# buildsys/export_cmake_sources.py. Generated headers, runtime assets and test
# provenance come from the same Python code SCons uses, through
# buildsys/cmake_generate.py.
cmake_minimum_required(VERSION 3.25)
project(glob2 LANGUAGES C CXX)

# GLOB2_PLATFORM: linux, or mingw for Windows builds with MSYS2's MinGW-w64 GCC
# (SConstruct's mingw=1, which the build arguments gain automatically).
if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND NOT CMAKE_CROSSCOMPILING)
  set(GLOB2_PLATFORM linux)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Darwin" AND NOT CMAKE_CROSSCOMPILING)
  set(GLOB2_PLATFORM darwin)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Windows" AND MINGW)
  set(GLOB2_PLATFORM mingw)
else()
  message(FATAL_ERROR "The CMake build covers native Linux, macOS and MSYS2 MinGW builds; "
                      "use SCons (tools/dev_build.py) for every other target.")
endif()
set(windows OFF)
set(darwin OFF)
if(GLOB2_PLATFORM STREQUAL "mingw")
  set(windows ON)
elseif(GLOB2_PLATFORM STREQUAL "darwin")
  set(darwin ON)
  # libgag's scroll monitor and a test support file are Objective-C++.
  enable_language(OBJCXX)
endif()
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_BINARY_DIR)
  message(FATAL_ERROR "Configure into a separate build directory, for example: cmake --preset dev")
endif()
if(CMAKE_BUILD_TYPE)
  message(FATAL_ERROR "Choose the tier with GLOB2_BUILD_ARGS (for example perf=1), not CMAKE_BUILD_TYPE, "
                      "whose flags would differ from the SCons tiers")
endif()

find_package(Python3 3.8 REQUIRED COMPONENTS Interpreter)
find_program(GLOB2_PKG_CONFIG pkg-config REQUIRED)
set(GLOB2_ROOT "${CMAKE_SOURCE_DIR}")
set(GLOB2_OUT "${CMAKE_BINARY_DIR}")
set(GLOB2_BRIDGE "${GLOB2_ROOT}/buildsys/cmake_generate.py")
include("${GLOB2_ROOT}/cmake/sources.cmake")
include("${GLOB2_ROOT}/cmake/tests.cmake")
# The bridge and the generators it imports decide configure-time results.
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
  "${GLOB2_BRIDGE}" "${GLOB2_ROOT}/buildsys/build_config.py" "${GLOB2_ROOT}/buildsys/checkout_paths.py"
  "${GLOB2_ROOT}/buildsys/build_layout.py" "${GLOB2_ROOT}/buildsys/client_features.py"
  "${GLOB2_ROOT}/buildsys/official_instance.py" "${GLOB2_ROOT}/buildsys/recording_dependencies.py")

# Runs a buildsys/cmake_generate.py subcommand at configure time.
function(glob2_bridge output)
  execute_process(COMMAND "${Python3_EXECUTABLE}" "${GLOB2_BRIDGE}" ${ARGN}
                  WORKING_DIRECTORY "${GLOB2_ROOT}"
                  OUTPUT_VARIABLE result RESULT_VARIABLE status OUTPUT_STRIP_TRAILING_WHITESPACE)
  if(NOT status EQUAL 0)
    message(FATAL_ERROR "buildsys/cmake_generate.py ${ARGN} failed")
  endif()
  set(${output} "${result}" PARENT_SCOPE)
endfunction()

# ---- Build identity: SCons' key=value arguments, resolved by buildsys/build_layout.py ----
set(GLOB2_BUILD_ARGS "dev_fast=1 linker=auto" CACHE STRING
    "SCons-style build arguments, for example 'dev_fast=1 linker=auto', 'perf=1 opt=3' or 'release=1'")
separate_arguments(build_args UNIX_COMMAND "${GLOB2_BUILD_ARGS}")
if(windows AND NOT build_args MATCHES "(^|;)mingw=")
  list(APPEND build_args mingw=1)
endif()
set(build_arg_options)
foreach(word IN LISTS build_args)
  list(APPEND build_arg_options "--arg=${word}")
endforeach()
glob2_bridge(configuration configure --builddir "${GLOB2_OUT}" ${build_arg_options})
string(JSON identity GET "${configuration}" identity)
string(JSON GLOB2_DEFAULT_DIRECTORY GET "${configuration}" directory)
# opengl=0 (SConstruct's option): software rendering only, without probing for OpenGL.
string(JSON opengl_argument ERROR_VARIABLE unset GET "${configuration}" arguments opengl)
set(GLOB2_OPENGL ON)
if(NOT unset AND opengl_argument MATCHES "^(0|false|no|off)$")
  set(GLOB2_OPENGL OFF)
endif()
# Reads an identity key, or `default` when the identity leaves it out.
function(glob2_identity output key default)
  string(JSON value ERROR_VARIABLE missing GET "${identity}" ${key})
  if(missing)
    set(value "${default}")
  endif()
  set(${output} "${value}" PARENT_SCOPE)
endfunction()
glob2_identity(GLOB2_MODE mode debug)
glob2_identity(GLOB2_OPT opt 2)
glob2_identity(GLOB2_ROLE role client)
glob2_identity(GLOB2_DEV_FAST dev_fast OFF)
glob2_identity(GLOB2_RETAIN_SYMBOLS retain_symbols OFF)
glob2_identity(GLOB2_COMPILE_PROFILE compile_profile full)
glob2_identity(GLOB2_IDENTITY_LINKER linker default)
glob2_identity(GLOB2_LEAN_IMAGES lean_images OFF)
glob2_identity(GLOB2_SIZE_OPTIMIZATION size_optimization "")
foreach(key recording_codec)
  glob2_identity(value ${key} "")
  if(value)
    message(FATAL_ERROR "The CMake build does not support ${key} yet (GLOB2_BUILD_ARGS='${GLOB2_BUILD_ARGS}')")
  endif()
endforeach()
if(GLOB2_MODE STREQUAL "profile")
  message(FATAL_ERROR "The CMake build does not support profile=1")
endif()
# role=relay builds glob2-relay and its tests only (src/SConscript's relay branch).
set(relay OFF)
if(GLOB2_ROLE STREQUAL "relay")
  set(relay ON)
elseif(NOT GLOB2_ROLE STREQUAL "client")
  message(FATAL_ERROR "The CMake build does not support role=${GLOB2_ROLE}")
endif()
file(REAL_PATH "${GLOB2_OUT}" binary_directory)
if(NOT binary_directory STREQUAL GLOB2_DEFAULT_DIRECTORY)
  message(STATUS "Building '${GLOB2_BUILD_ARGS}' in ${GLOB2_OUT}; its default directory is ${GLOB2_DEFAULT_DIRECTORY}")
endif()

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
    elseif(word MATCHES "^-Wl," OR word STREQUAL "-mwindows")
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
  # MinGW's pkg-config separates its search path with ';', as Windows paths hold ':'.
  if(windows)
    set(ENV{PKG_CONFIG_PATH} "${sdl_prefix}/lib/pkgconfig;$ENV{PKG_CONFIG_PATH}")
  else()
    set(ENV{PKG_CONFIG_PATH} "${sdl_prefix}/lib/pkgconfig:$ENV{PKG_CONFIG_PATH}")
  endif()
  set(sdl_includes "${sdl_prefix}/include")
  set(sdl_directories "${sdl_prefix}/lib")
  # Development runs load the prefix in place; installed copies use only their
  # bundled $ORIGIN/../lib/glob2 (distributions reject build paths). Windows
  # loads DLLs from the executable's directory instead.
  if(GLOB2_PLATFORM STREQUAL "linux")
    set(sdl_link_options "-Wl,-rpath,$ORIGIN/../lib/glob2")
  endif()
endif()
# The linker spelling of SConstruct's RPATH entries: ld64 takes no -rpath=.
if(darwin)
  set(rpath_flag "-Wl,-rpath,")
else()
  set(rpath_flag "-Wl,-rpath=")
endif()
# macOS's other libraries come from Homebrew, else MacPorts, as in SConstruct.
set(host_includes)
set(host_directories)
if(darwin)
  find_program(GLOB2_BREW brew)
  set(brew_prefix)
  if(GLOB2_BREW)
    execute_process(COMMAND "${GLOB2_BREW}" --prefix OUTPUT_VARIABLE brew_prefix
                    RESULT_VARIABLE status OUTPUT_STRIP_TRAILING_WHITESPACE)
    if(NOT status EQUAL 0)
      set(brew_prefix)
    endif()
  endif()
  if(brew_prefix)
    set(host_includes "${brew_prefix}/include")
    set(host_directories "${brew_prefix}/lib")
    set(ENV{PATH} "${brew_prefix}/bin:$ENV{PATH}")
  else()
    set(host_includes /opt/local/include)
    set(host_directories /opt/local/lib)
  endif()
endif()
# What `scons install` changes for the binaries it installs: no build-prefix rpath
# and no debug-prefix map. Packaging configures with it; `cmake --install` needs it
# whenever GLOB2_SDL3_PREFIX is set.
option(GLOB2_SHIPPING "Build binaries for installation: no build-prefix rpath or debug-path mapping" OFF)
if(GLOB2_SHIPPING)
  set(GLOB2_DEBUG_PREFIX_MAP OFF CACHE BOOL "" FORCE)
endif()
# Removes the build prefix's rpath from a link option list in shipping builds.
macro(glob2_drop_build_rpath list_name)
  if(GLOB2_SHIPPING AND GLOB2_SDL3_PREFIX)
    list(REMOVE_ITEM ${list_name} "-Wl,-rpath,${sdl_prefix}/lib" "-Wl,-rpath=${sdl_prefix}/lib")
  endif()
endmacro()

# The pinned h264 recording libraries: GLOB2_RECORDING_PREFIX, the prefix beside
# GLOB2_SDL3_PREFIX, or a shared-store build (built on first use, as SCons does).
# The relay links no media library and only needs SDL's headers for libgag's types.
set(recording_includes)
set(recording_libraries)
set(build_config_options)
set(opengl_libraries)
set(fribidi_libraries)
if(relay)
  glob2_pkg_config(sdl3 sdl3 --cflags)
else()
  if(windows)
    set(recording_target mingw32)
  elseif(darwin)
    set(recording_target darwin)
  else()
    set(recording_target linux)
  endif()
  glob2_bridge(recording_prefix recording-prefix --builddir "${GLOB2_OUT}" --target ${recording_target})
  glob2_bridge(recording_libraries recording --prefix "${recording_prefix}" --target ${recording_target})
  set(recording_includes "${recording_prefix}/include")
  glob2_pkg_config(opusfile opusfile --cflags)
  glob2_pkg_config(sdl3 sdl3 --cflags --libs)
  glob2_pkg_config(webp libwebp --cflags --libs --static)
endif()

include(CheckIncludeFileCXX)
# SConstruct's Windows libraries: zlib, a POSIX regex, Winsock and the Windows OpenGL names.
set(zlib_libraries z)
set(regex_libraries)
set(platform_libraries)
set(windows_libraries)
# macOS frameworks, linked after every library as SCons' FRAMEWORKS are.
set(frameworks)
if(darwin)
  set(frameworks Security CoreFoundation)
  if(NOT relay)
    list(APPEND frameworks AppKit)
  endif()
endif()
if(windows)
  find_library(GLOB2_Z_LIBRARY z)
  if(NOT GLOB2_Z_LIBRARY)
    set(zlib_libraries zlib1)
  endif()
  find_library(GLOB2_REGEX_LIBRARY regex)
  if(GLOB2_REGEX_LIBRARY)
    set(regex_libraries regex)
  else()
    set(regex_libraries systre tre intl iconv)
  endif()
  set(platform_libraries ws2_32 mswsock crypt32)
  set(windows_libraries wsock32 winmm)
  set(gl_name opengl32)
  set(glu_name glu32)
else()
  set(gl_name GL)
  set(glu_name GLU)
endif()
if(NOT relay)
  # On macOS the probes search the Homebrew or MacPorts prefix too, as SConstruct's do.
  if(darwin)
    set(CMAKE_REQUIRED_INCLUDES ${sdl_includes} ${host_includes})
  endif()
  find_library(GLOB2_EPOXY_LIBRARY epoxy HINTS ${host_directories})
  find_library(GLOB2_FRIBIDI_LIBRARY fribidi HINTS ${host_directories})
  check_include_file_cxx(epoxy/gl.h GLOB2_HAVE_EPOXY_H)
  check_include_file_cxx(fribidi/fribidi.h GLOB2_HAVE_FRIBIDI_H)
  if(darwin)
    # Apple's OpenGL and GLUT frameworks.
    set(have_gl ON)
    set(gl_libraries)
    if(GLOB2_OPENGL)
      list(APPEND frameworks OpenGL CoreFoundation GLUT)
      add_compile_definitions(GL_SILENCE_DEPRECATION)  # Apple's OpenGL deprecation warnings
    endif()
  else()
    find_library(GLOB2_GL_LIBRARY ${gl_name})
    find_library(GLOB2_GLU_LIBRARY ${glu_name})
    check_include_file_cxx(GL/gl.h GLOB2_HAVE_GL_H)
    check_include_file_cxx(GL/glu.h GLOB2_HAVE_GLU_H)
    set(have_gl OFF)
    if(GLOB2_GL_LIBRARY AND GLOB2_HAVE_GL_H AND GLOB2_GLU_LIBRARY AND GLOB2_HAVE_GLU_H)
      set(have_gl ON)
    endif()
    set(gl_libraries ${gl_name} ${glu_name})
  endif()
  if(NOT GLOB2_OPENGL)
    message(STATUS "opengl=0: building without OpenGL support")
  elseif(have_gl AND GLOB2_EPOXY_LIBRARY AND GLOB2_HAVE_EPOXY_H)
    set(opengl_libraries ${gl_libraries} epoxy)
    list(APPEND build_config_options --opengl)
  else()
    message(STATUS "One or more library missing for OpenGL support, disabling OpenGL support.")
  endif()
  if(GLOB2_FRIBIDI_LIBRARY AND GLOB2_HAVE_FRIBIDI_H)
    set(fribidi_libraries fribidi)
    list(APPEND build_config_options --fribidi)
  endif()
endif()

# ---- Generated headers: the SCons generators, called through the bridge ----
option(GLOB2_EXPORT_RUNTIME_ASSETS "Export the runtime assets into the build directory, as SCons does" ON)
if(relay)
  set(GLOB2_EXPORT_RUNTIME_ASSETS OFF)
  set(GLOB2_RUNTIME_ASSET_DIR "${GLOB2_OUT}/runtime-assets")
elseif(GLOB2_EXPORT_RUNTIME_ASSETS)
  set(GLOB2_RUNTIME_ASSET_DIR "${GLOB2_OUT}/runtime-assets")
else()
  set(GLOB2_RUNTIME_ASSET_DIR "" CACHE PATH "Runtime assets glob2 loads when it does not export its own")
  if(NOT GLOB2_RUNTIME_ASSET_DIR)
    message(FATAL_ERROR "GLOB2_EXPORT_RUNTIME_ASSETS=OFF needs GLOB2_RUNTIME_ASSET_DIR, another build's runtime-assets")
  endif()
endif()
# SConstruct's DATADIR: the installed data directory compiled into glob2/BuildConfig.h.
if(windows OR darwin)
  set(default_datadir /usr/local/share)
else()
  set(default_datadir "${CMAKE_INSTALL_PREFIX}/share")
endif()
set(GLOB2_DATADIR "${default_datadir}" CACHE PATH "Installed data directory compiled into the game (SCons DATADIR)")
glob2_bridge(unused build-config --builddir "${GLOB2_OUT}" "--datadir=${GLOB2_DATADIR}"
             ${build_config_options} ${build_arg_options})
glob2_bridge(unused build-paths --builddir "${GLOB2_OUT}" --runtime-assets "${GLOB2_RUNTIME_ASSET_DIR}")
glob2_bridge(unused build-stamp --builddir "${GLOB2_OUT}")
glob2_bridge(unused compile-profile --builddir "${GLOB2_OUT}" --profile "${GLOB2_COMPILE_PROFILE}")

set(bridge "${Python3_EXECUTABLE}" "${GLOB2_BRIDGE}")
set(stamps "${GLOB2_OUT}/generated-stamps")
add_custom_command(OUTPUT "${stamps}/skin-materials"
  BYPRODUCTS "${GLOB2_OUT}/include/glob2/SkinMaterials.h"
  COMMAND ${bridge} skin-materials --builddir "${GLOB2_OUT}" --stamp "${stamps}/skin-materials"
  DEPENDS "${GLOB2_BRIDGE}" "${GLOB2_ROOT}/buildsys/skin_materials.py"
          "${GLOB2_ROOT}/libs/gag/shaders/skin-materials.json" "${GLOB2_ROOT}/libs/gag/shaders/skin-material.glsl"
  WORKING_DIRECTORY "${GLOB2_ROOT}" COMMENT "Generating glob2/SkinMaterials.h" VERBATIM)

file(GLOB shader_globs CONFIGURE_DEPENDS "${GLOB2_ROOT}/libs/gag/shaders/*.glsl")
glob2_bridge(shader_inputs shader-inputs)
add_custom_command(OUTPUT "${stamps}/shaders"
  BYPRODUCTS "${GLOB2_OUT}/shaders/RendererShaderLayouts.h"
  COMMAND ${bridge} shaders --builddir "${GLOB2_OUT}" --stamp "${stamps}/shaders"
  DEPENDS "${GLOB2_BRIDGE}" "${GLOB2_ROOT}/buildsys/renderer_shaders.py" ${shader_inputs}
          "${GLOB2_ROOT}/tools/shaders/gradient_compute.py" "${GLOB2_ROOT}/tools/shaders/gradient_preparation_rules.py"
  WORKING_DIRECTORY "${GLOB2_ROOT}" COMMENT "Generating renderer shader assets" VERBATIM)

add_custom_command(OUTPUT "${stamps}/hive-api"
  BYPRODUCTS "${GLOB2_OUT}/include/hive/PlayerAPIContract.h"
  COMMAND ${bridge} hive-api --builddir "${GLOB2_OUT}" --stamp "${stamps}/hive-api"
  DEPENDS "${GLOB2_BRIDGE}" "${GLOB2_ROOT}/buildsys/hive_api.py"
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
  DEPENDS "${GLOB2_BRIDGE}" "${GLOB2_ROOT}/buildsys/skin_render.py" ${skin_render_inputs}
  WORKING_DIRECTORY "${GLOB2_ROOT}" COMMENT "Generating SkinRenderRecipe.h" VERBATIM)

add_custom_target(glob2_generated DEPENDS
  "${stamps}/skin-materials" "${stamps}/shaders" "${stamps}/hive-api" "${stamps}/skin-render"
  "${GLOB2_OUT}/include/generated/OnlineErrorCatalog.h")
# ---- Runtime assets: buildsys/runtime_assets.py's export, shared through the store ----
# The bridge writes a depfile naming every asset input and the directories that
# hold them, so the export runs again only when one of them changes.
set(GLOB2_ASSET_JOBS 8 CACHE STRING "Parallel jobs for a runtime-asset export that misses the shared store")
add_custom_target(glob2_runtime_assets)
if(GLOB2_EXPORT_RUNTIME_ASSETS)
  add_custom_command(OUTPUT "${GLOB2_OUT}/runtime-assets.json"
    COMMAND ${bridge} runtime-assets --builddir "${GLOB2_OUT}" --depfile "${GLOB2_OUT}/runtime-assets.d"
            --jobs "${GLOB2_ASSET_JOBS}" ${build_arg_options}
    DEPFILE "${GLOB2_OUT}/runtime-assets.d"
    DEPENDS "${GLOB2_BRIDGE}" "${GLOB2_ROOT}/buildsys/runtime_assets.py" "${GLOB2_ROOT}/buildsys/shared_assets.py"
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

# ---- Tier flags: SConstruct's CXXFLAGS/CFLAGS/CCFLAGS/LINKFLAGS, step by step ----
# CCFLAGS reach C and C++ objects, CXXFLAGS only C++ and CFLAGS only C, as in SCons.
set(GLOB2_CXXFLAGS "-g" CACHE STRING "SCons CXXFLAGS before the tier's own (distributions pass their optflags)")
set(GLOB2_LINKFLAGS "-g" CACHE STRING "SCons LINKFLAGS before the tier's own")
option(GLOB2_DEBUG_PREFIX_MAP "Map the checkout to . in debug information so checkouts share cache entries (off for shipping builds)" ON)
separate_arguments(cxxflags UNIX_COMMAND "${GLOB2_CXXFLAGS}")
separate_arguments(linkflags UNIX_COMMAND "${GLOB2_LINKFLAGS}")
set(cflags)
set(ccflags)
# Removes every -g* and -O* word from each named list, as SCons' tier code does.
macro(glob2_drop_debug_and_optimization)
  foreach(list_name ${ARGN})
    list(FILTER ${list_name} EXCLUDE REGEX "^-[gO]")
  endforeach()
endmacro()
# Release builds outside macOS link with -s, so they compile without -g unless
# a distribution keeps symbols for its debug packages. macOS bundles keep a dSYM.
if(GLOB2_MODE STREQUAL "release" AND NOT GLOB2_RETAIN_SYMBOLS AND NOT darwin)
  list(REMOVE_ITEM cxxflags -g)
  list(REMOVE_ITEM linkflags -g)
endif()
list(APPEND cxxflags -std=gnu++20 -Wall -fPIC)
list(APPEND cflags -fPIC)
# Uninitialized-read diagnostics (DET_INIT=zero|pattern), an environment variable as in SConstruct.
if(DEFINED ENV{DET_INIT} AND NOT "$ENV{DET_INIT}" STREQUAL "")
  list(APPEND cxxflags "-ftrivial-auto-var-init=$ENV{DET_INIT}")
  list(APPEND ccflags "-ftrivial-auto-var-init=$ENV{DET_INIT}")
endif()
list(APPEND linkflags -Wall)
if(GLOB2_MODE STREQUAL "perf")
  # Measurement builds: shipping optimization with symbols for profilers.
  glob2_drop_debug_and_optimization(cxxflags linkflags)
  list(APPEND cxxflags -O${GLOB2_OPT} -g1)
  list(APPEND linkflags -O${GLOB2_OPT})
elseif(GLOB2_MODE STREQUAL "release")
  list(APPEND cxxflags -O3)
  list(APPEND linkflags -O3)
  if(NOT darwin)
    list(APPEND linkflags -fwhole-program)
  endif()
  if(NOT GLOB2_RETAIN_SYMBOLS AND NOT darwin)
    list(APPEND cxxflags -s)
    list(APPEND linkflags -s)
  endif()
endif()
# buildsys/size_optimization.py: the opt-in GCC size experiments of release builds.
if(GLOB2_SIZE_OPTIMIZATION)
  if(NOT CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    message(FATAL_ERROR "size_optimization experiments currently require GCC")
  endif()
  list(APPEND ccflags -ffunction-sections -fdata-sections)
  list(APPEND linkflags -Wl,--gc-sections)
  if(GLOB2_SIZE_OPTIMIZATION STREQUAL "lto" OR GLOB2_SIZE_OPTIMIZATION STREQUAL "size")
    # GCC's linker plugin decides visibility; -fwhole-program would change it under LTO.
    list(APPEND ccflags -flto)
    list(REMOVE_ITEM linkflags -fwhole-program)
    list(APPEND linkflags -flto)
  endif()
  if(GLOB2_SIZE_OPTIMIZATION STREQUAL "size")
    list(APPEND cxxflags -Os)
    list(APPEND cflags -Os)
    list(APPEND linkflags -Os)
  endif()
endif()
# Windows programs start without a console; test binaries drop it again.
if(windows)
  list(APPEND linkflags -mwindows)
endif()
# buildsys/dev_build.py configure(): dev_fast compiles everything at -O0 -g1.
if(GLOB2_DEV_FAST)
  glob2_drop_debug_and_optimization(cflags cxxflags ccflags linkflags)
  list(APPEND ccflags -O0 -g1)
endif()
# buildsys/dev_build.py cache_flags(): relocatable debug paths whenever ccache is on.
if(GLOB2_DEBUG_PREFIX_MAP AND CMAKE_CXX_COMPILER_LAUNCHER)
  list(APPEND ccflags "-fdebug-prefix-map=${GLOB2_ROOT}=.")
endif()
set(cxx_compile_flags ${cxxflags} ${ccflags})
set(c_compile_flags ${cflags} ${ccflags})
# Objective-C++ (macOS) compiles with the C++ flags, as SCons compiles .mm with CXXFLAGS.
add_compile_options("$<$<COMPILE_LANGUAGE:CXX,OBJCXX>:${cxx_compile_flags}>" "$<$<COMPILE_LANGUAGE:C>:${c_compile_flags}>")
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
  HAVE_CONFIG_H $<$<NOT:$<BOOL:${relay}>>:GLOB2_H264_ASSETS>
  # Strict C++ mode omits MinGW's WIN32 alias, which platform guards use; MinGW
  # headers expose _POSIX_THREADS at different include depths, so fix Asio's choice.
  $<$<BOOL:${windows}>:WIN32> $<$<BOOL:${windows}>:BOOST_ASIO_HAS_PTHREADS>
  ${opusfile_defines} ${sdl3_defines} ${webp_defines})
add_compile_options(${opusfile_options} ${sdl3_options} ${webp_options})

# lean_images=1: the private PNG/JPEG/WebP SDL_image comes before every other prefix.
set(lean_image_prefix)
set(lean_includes)
if(GLOB2_LEAN_IMAGES)
  glob2_bridge(lean_image_prefix lean-image-prefix --builddir "${GLOB2_OUT}")
  set(lean_includes "${lean_image_prefix}/include/SDL3")
  list(PREPEND sdl_directories "${lean_image_prefix}/lib")
  if(GLOB2_PLATFORM STREQUAL "linux")
    list(APPEND sdl_link_options "-Wl,-rpath,$ORIGIN/../lib/glob2")
  endif()
endif()
# macOS app bundles (the bundle and package targets) link the pinned PNG/JPEG
# SDL_image mac_image_dependency builds, as SConstruct does when asked for them;
# tools/dev_build.py turns this on for those targets.
option(GLOB2_MAC_BUNDLE "Build the macOS release for the bundle and package targets" OFF)
set(mac_image_prefix)
if(darwin AND GLOB2_MAC_BUNDLE AND NOT relay)
  if(NOT GLOB2_MODE STREQUAL "release")
    message(FATAL_ERROR "GLOB2_MAC_BUNDLE (the bundle and package targets) needs release=1")
  endif()
  set(image_options)
  if(GLOB2_SDL3_PREFIX)
    set(image_options "--sdl-prefix=${sdl_prefix}")
  endif()
  glob2_bridge(mac_image_prefix mac-image-prefix --builddir "${GLOB2_OUT}" ${image_options})
  list(PREPEND lean_includes "${mac_image_prefix}/include")
  list(PREPEND sdl_directories "${mac_image_prefix}/lib")
endif()
set(include_directories ${recording_includes} ${lean_includes} ${sdl_includes} ${host_includes} "${GLOB2_OUT}/include"
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

# compile_profile=core leaves the optional groups out (buildsys/sources.py profile_sources).
set(GLOB2_PROFILE_GROUPS ${GLOB2_OPTIONAL_GROUPS})
if(GLOB2_COMPILE_PROFILE STREQUAL "core")
  list(REMOVE_ITEM GLOB2_CLIENT_SOURCES ${GLOB2_CORE_EXCLUDED_SOURCES})
  list(REMOVE_ITEM GLOB2_STRICT_NUMERIC_SOURCES ${GLOB2_CORE_EXCLUDED_SOURCES})
  list(REMOVE_ITEM GLOB2_GUARDED_NUMERIC_SOURCES ${GLOB2_CORE_EXCLUDED_SOURCES})
  set(GLOB2_PROFILE_GROUPS)
elseif(NOT GLOB2_COMPILE_PROFILE STREQUAL "full")
  message(FATAL_ERROR "Unknown compile_profile ${GLOB2_COMPILE_PROFILE}")
endif()
list(TRANSFORM GLOB2_CLIENT_SOURCES PREPEND "${GLOB2_ROOT}/")
list(TRANSFORM GLOB2_STRICT_NUMERIC_SOURCES PREPEND "${GLOB2_ROOT}/")
list(TRANSFORM GLOB2_GAG_SOURCES PREPEND "${GLOB2_ROOT}/")
list(TRANSFORM GLOB2_USL_SOURCES PREPEND "${GLOB2_ROOT}/")
list(TRANSFORM GLOB2_JAVASCRIPT_SOURCES PREPEND "${GLOB2_ROOT}/")
list(TRANSFORM GLOB2_JAVASCRIPT_INCLUDE_DIRECTORIES PREPEND "${GLOB2_ROOT}/")

# ---- The relay: glob2-relay and glob2-relay-tests (src/SConscript's relay branch) ----
if(relay)
  # SConstruct's LINKFLAGS, and its LIBS filtered to these, in their order.
  set(relay_link_options ${sdl_link_options} ${linkflags})
  if(GLOB2_SDL3_PREFIX)
    list(APPEND relay_link_options "${rpath_flag}${sdl_prefix}/lib")
  endif()
  glob2_drop_build_rpath(relay_link_options)
  list(TRANSFORM GLOB2_RELAY_SOURCES PREPEND "${GLOB2_ROOT}/")
  list(TRANSFORM GLOB2_RELAY_GAG_SOURCES PREPEND "${GLOB2_ROOT}/")
  add_library(glob2_relay_objects OBJECT ${GLOB2_RELAY_SOURCES} ${GLOB2_RELAY_GAG_SOURCES})
  list(TRANSFORM GLOB2_RELAY_TEST_SOURCES PREPEND "${GLOB2_ROOT}/")
  add_executable(glob2-relay "${GLOB2_ROOT}/${GLOB2_RELAY_MAIN_SOURCE}")
  add_executable(glob2-relay-tests EXCLUDE_FROM_ALL ${GLOB2_RELAY_TEST_SOURCES})
  foreach(target glob2_relay_objects glob2-relay glob2-relay-tests)
    target_include_directories(${target} PRIVATE "${GLOB2_ROOT}/src/relay")
    add_dependencies(${target} glob2_generated glob2_build_stamp)
  endforeach()
  target_include_directories(glob2-relay-tests PRIVATE "${GLOB2_ROOT}/test/third_party")
  target_compile_definitions(glob2-relay-tests PRIVATE "RELAY_FIXTURE_DIR=\"${GLOB2_ROOT}/test/fixtures/relay-tickets\"")
  foreach(target glob2-relay glob2-relay-tests)
    target_link_libraries(${target} PRIVATE glob2_relay_objects pthread ssl crypto ${platform_libraries})
    target_link_options(${target} PRIVATE ${relay_link_options})
    set_target_properties(${target} PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${GLOB2_OUT}/src")
  endforeach()
  add_custom_target(relay DEPENDS glob2-relay glob2-relay-tests)
  add_custom_target(relay-tests DEPENDS glob2-relay-tests)
  return()
endif()

# ---- Stage 1: libraries ----
add_library(usl STATIC ${GLOB2_USL_SOURCES})
if(darwin)
  list(TRANSFORM GLOB2_GAG_DARWIN_SOURCES PREPEND "${GLOB2_ROOT}/")
  list(APPEND GLOB2_GAG_SOURCES ${GLOB2_GAG_DARWIN_SOURCES})
endif()
add_library(gag STATIC ${GLOB2_GAG_SOURCES})

# QuickJS and openlibm: the interpreter's strict numeric contract, per source
# exactly as buildsys/javascript.py compiles them.
add_library(Glob2JavaScript STATIC ${GLOB2_JAVASCRIPT_SOURCES})
# buildsys/javascript.py: -O2 for the optimized tiers, -O0 otherwise.
if(GLOB2_MODE STREQUAL "release" OR GLOB2_MODE STREQUAL "perf")
  set(javascript_optimization -O2)
else()
  set(javascript_optimization -O0)
endif()
if(GLOB2_SIZE_OPTIMIZATION STREQUAL "size")
  set(javascript_size -Os)
else()
  set(javascript_size)
endif()
set(javascript_options ${javascript_optimization} ${GLOB2_JAVASCRIPT_CFLAGS} ${javascript_size})
set_source_files_properties(${GLOB2_JAVASCRIPT_SOURCES} PROPERTIES
  COMPILE_OPTIONS "${javascript_options}"
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
# macOS and Windows verify TLS certificates through the platform store
# (buildsys/sources.py PLATFORM_TRUST_SOURCES); Linux uses OpenSSL's.
set(platform_trust_sources)
if(windows)
  set(platform_trust_sources ${GLOB2_MINGW_TRUST_SOURCES})
elseif(darwin)
  set(platform_trust_sources ${GLOB2_DARWIN_TRUST_SOURCES})
endif()
list(TRANSFORM platform_trust_sources PREPEND "${GLOB2_ROOT}/" OUTPUT_VARIABLE trust_sources)
add_library(glob2_client OBJECT ${client_sources} ${trust_sources})
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
  # MinGW names objects .obj, Linux .o.
  string(REPLACE "." "\\." extension "${CMAKE_CXX_OUTPUT_EXTENSION}")
  foreach(path IN LISTS ARGN)
    string(REGEX REPLACE "([.+])" "\\\\\\1" path "${path}")
    list(APPEND pattern "/${path}${extension}$")
  endforeach()
  list(JOIN pattern "|" pattern)
  set(${output} "$<FILTER:$<TARGET_OBJECTS:${target}>,INCLUDE,${pattern}>" PARENT_SCOPE)
endfunction()

# Link inputs in SConstruct's LIBS order; like SCons' unique merge, a library
# named again later moves to its last position.
set(libraries ${zlib_libraries} ${regex_libraries} pthread ssl crypto ${platform_libraries} ${opengl_libraries}
    ${fribidi_libraries} ${recording_libraries} SDL3_net opusfile opus ogg SDL3_ttf SDL3_image speex
    ${windows_libraries} ${sdl3_libraries} ${webp_libraries})
list(REVERSE libraries)
list(REMOVE_DUPLICATES libraries)
list(REVERSE libraries)
list(REMOVE_DUPLICATES frameworks)
foreach(framework IN LISTS frameworks)
  list(APPEND libraries "-framework ${framework}")
endforeach()
list(APPEND sdl_directories ${host_directories})
list(REMOVE_DUPLICATES sdl_directories)
set(link_options ${sdl_link_options} ${linkflags} ${sdl3_link_options} ${webp_link_options})
if(GLOB2_SDL3_PREFIX)
  list(APPEND link_options "${rpath_flag}${sdl_prefix}/lib")
endif()
glob2_drop_build_rpath(link_options)
# linker=auto|lld (buildsys/dev_build.py configure): lld when installed.
if(NOT GLOB2_IDENTITY_LINKER STREQUAL "default" AND GLOB2_PLATFORM STREQUAL "linux")
  find_program(GLOB2_LLD_PROGRAM ld.lld)
  if(GLOB2_LLD_PROGRAM)
    list(APPEND link_options -fuse-ld=lld)
  elseif(GLOB2_IDENTITY_LINKER STREQUAL "lld")
    message(FATAL_ERROR "linker=lld requires an installed ld.lld")
  endif()
endif()
# test/SConscript keeps function names in test binaries for crash backtraces.
# Test programs are console programs: no -mwindows either.
set(test_link_options ${link_options})
list(REMOVE_ITEM test_link_options -s -mwindows)

# Every program links `ARGN` (object libraries and archives) before SConstruct's
# LIBS, with its library directories and LINKFLAGS (the test flags for tests).
function(glob2_link target)
  target_link_libraries(${target} PRIVATE ${ARGN} ${libraries})
  target_link_directories(${target} PRIVATE ${sdl_directories} ${sdl3_directories} ${webp_directories})
  get_target_property(is_test ${target} GLOB2_TEST)
  if(is_test)
    target_link_options(${target} PRIVATE ${test_link_options})
    if(darwin)
      target_link_libraries(${target} PRIVATE "-framework Foundation")  # test/support/TestActivity_mac.mm
    endif()
  else()
    target_link_options(${target} PRIVATE ${link_options})
  endif()
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

# test/SConscript available(): 'mac' needs macOS; native builds always have the WSS transport; an
# optional buildsys/sources.py group is present only in the full compile profile.
function(glob2_available output)
  set(ok TRUE)
  foreach(feature IN LISTS ARGN)
    if((feature STREQUAL "mac" AND NOT darwin) OR (feature STREQUAL "not-mingw" AND windows)
       OR (feature STREQUAL "opengl" AND NOT opengl_libraries)
       OR (feature IN_LIST GLOB2_OPTIONAL_GROUPS AND NOT feature IN_LIST GLOB2_PROFILE_GROUPS))
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
list(JOIN ccflags " " provenance_ccflags)
list(JOIN cxxflags " " provenance_cxxflags)
set(provenance_arguments "--compiler=${CMAKE_CXX_COMPILER}" "--ccflags=${provenance_ccflags}"
    "--cxxflags=${provenance_cxxflags} ${GLOB2_EXTRA_COMPILE_FLAGS}")
foreach(define IN LISTS provenance_defines ITEMS SDL_MAIN_HANDLED)
  list(APPEND provenance_arguments "--define=${define}")
endforeach()
foreach(option IN LISTS test_link_options)
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

# The module libraries (buildsys/sources.py MODULE_SOURCES): archives of client objects
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
  set_target_properties(${target} PROPERTIES GLOB2_TEST ON)
  add_dependencies(${target} glob2_client ${production_libraries})
  if(GLOB2_${binary}_ALL_CLIENT_OBJECTS)
    glob2_link(${target} glob2_client Glob2JavaScript gag usl)
  elseif(GLOB2_${binary}_MODULES)
    # Map and sim still reference each other: GNU ld resolves the archives as one
    # group, as test/SConscript's --start-group does; ld64 rescans on its own.
    set(group ${GLOB2_${binary}_MODULES})
    list(TRANSFORM group PREPEND glob2_)
    list(APPEND group Glob2JavaScript gag usl)
    if(darwin)
      glob2_link(${target} ${group})
    else()
      list(JOIN group "," group)
      glob2_link(${target} "$<LINK_GROUP:RESCAN,${group}>")
    endif()
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
  set_target_properties(${program} PROPERTIES GLOB2_TEST ON)
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

# buildsys/harness_programs.py: the client flags; objects as each program's link kind says.
foreach(name IN LISTS GLOB2_HARNESS_SHARED_OBJECTS)
  add_library(glob2_harness_${name} OBJECT EXCLUDE_FROM_ALL "${GLOB2_ROOT}/${GLOB2_HARNESS_SHARED_${name}_SOURCE}")
  target_link_libraries(glob2_harness_${name} PRIVATE glob2_client_settings)
  add_dependencies(glob2_harness_${name} glob2_generated glob2_build_stamp)
endforeach()
foreach(program IN LISTS GLOB2_HARNESS_PROGRAMS)
  set(prefix GLOB2_HARNESS_${program})
  if(${prefix}_PROFILE AND NOT ${prefix}_PROFILE IN_LIST GLOB2_PROFILE_GROUPS)
    continue()  # compile_profile=core leaves out the code it drives
  endif()
  # Native builds always have the WSS transport, the only requirement used.
  set(sources "${GLOB2_ROOT}/${${prefix}_SOURCE}")
  # Programs that verify certificates link the platform trust objects, which
  # glob2_client already holds for the programs that link all of it.
  if(platform_trust_sources AND ${prefix}_TRUST AND NOT ${prefix}_LINK MATCHES "^client")
    glob2_objects_of(trust glob2_client ${platform_trust_sources})
    list(APPEND sources "${trust}")
  endif()
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
  if(NOT ${prefix}_LINK STREQUAL "libraries" OR (platform_trust_sources AND ${prefix}_TRUST))
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

# ---- macOS application bundle and disk image (SConstruct's bundle and package targets) ----
# bundle: build/darwin/.../Glob2.app with its assets, libraries, MoltenVK and dSYM
# (buildsys/bundle.py); package: also Glob2-<version>.dmg in the checkout.
if(darwin AND GLOB2_MAC_BUNDLE)
  add_custom_target(bundle
    COMMAND ${bridge} bundle --builddir "${GLOB2_OUT}" --binary "$<TARGET_FILE:glob2>"
            --lean-image-prefix "${mac_image_prefix}" --recording-prefix "${recording_prefix}"
            --jobs "${GLOB2_ASSET_JOBS}" ${build_arg_options}
    WORKING_DIRECTORY "${GLOB2_ROOT}" COMMENT "Creating ${GLOB2_OUT}/Glob2.app" VERBATIM)
  add_dependencies(bundle glob2)
  glob2_bridge(package_version package-version)
  add_custom_target(package
    COMMAND ${bridge} dmg --app "${GLOB2_OUT}/Glob2.app" --output "${GLOB2_ROOT}/Glob2-${package_version}.dmg"
    WORKING_DIRECTORY "${GLOB2_ROOT}" COMMENT "Creating Glob2-${package_version}.dmg" VERBATIM)
  add_dependencies(package bundle)
endif()

# ---- Installation: what `scons install` installs (cmake --install, honours DESTDIR) ----
# SConstruct's BINDIR and INSTALLDIR: the executable's directory and the share
# directory that holds glob2/, applications/, icons/, man/ and the completions.
set(GLOB2_BINDIR "${CMAKE_INSTALL_PREFIX}/bin" CACHE PATH "Installed executable directory (SCons BINDIR)")
set(GLOB2_INSTALLDIR "${CMAKE_INSTALL_PREFIX}/share" CACHE PATH "Installed share directory (SCons INSTALLDIR)")
if(GLOB2_SDL3_PREFIX AND NOT GLOB2_SHIPPING)
  install(CODE "message(FATAL_ERROR \"Installing a GLOB2_SDL3_PREFIX build needs -DGLOB2_SHIPPING=ON, \"
                \"so the installed binary carries no build-prefix rpath\")")
endif()
install(TARGETS glob2 RUNTIME DESTINATION "${GLOB2_BINDIR}")
set(install_arguments "--builddir=${GLOB2_OUT}" "--installdir=${GLOB2_INSTALLDIR}" "--bindir=${GLOB2_BINDIR}"
    "--recording-prefix=${recording_prefix}")
if(GLOB2_SDL3_PREFIX)
  list(APPEND install_arguments "--sdl-prefix=${sdl_prefix}")
endif()
if(GLOB2_LEAN_IMAGES)
  list(APPEND install_arguments "--lean-image-prefix=${lean_image_prefix}")
endif()
string(REPLACE ";" "\" \"" install_arguments "${install_arguments}")
install(CODE "
  message(STATUS \"Installing verified runtime assets and desktop integration\")
  execute_process(COMMAND \"${Python3_EXECUTABLE}\" \"${GLOB2_BRIDGE}\" install \"${install_arguments}\"
                  WORKING_DIRECTORY \"${GLOB2_ROOT}\" RESULT_VARIABLE status)
  if(NOT status EQUAL 0)
    message(FATAL_ERROR \"buildsys/cmake_generate.py install failed\")
  endif()")
