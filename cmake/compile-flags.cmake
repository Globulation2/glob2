# How every object is compiled: the tier's flags (optimization, debug, link-time
# optimization, size experiments) and the definitions and options every object
# shares. Included from CMakeLists.txt where these sections used to sit.
#
# These settings reach the simulation, so this file is on the simulation path:
# the sim-diff check runs for any change to it (.github/scripts/ci_policy.py).
# Keep compile flags and global definitions here, not in CMakeLists.txt;
# test/build_system/test_ci_policy.py fails when CMakeLists.txt gains new ones.

# ---- Tier flags: CXXFLAGS/CFLAGS/CCFLAGS/LINKFLAGS, step by step ----
# CCFLAGS reach C and C++ objects, CXXFLAGS only C++ and CFLAGS only C.
set(GLOB2_CXXFLAGS "-g" CACHE STRING "CXXFLAGS before the tier's own (distributions pass their optflags)")
set(GLOB2_LINKFLAGS "-g" CACHE STRING "LINKFLAGS before the tier's own")
option(GLOB2_DEBUG_PREFIX_MAP "Map the checkout to . in debug information so checkouts share cache entries (off for shipping builds)" ON)
separate_arguments(cxxflags UNIX_COMMAND "${GLOB2_CXXFLAGS}")
separate_arguments(linkflags UNIX_COMMAND "${GLOB2_LINKFLAGS}")
set(cflags)
set(ccflags)
# Removes every -g* and -O* word from each named list.
macro(glob2_drop_debug_and_optimization)
  foreach(list_name ${ARGN})
    list(FILTER ${list_name} EXCLUDE REGEX "^-[gO]")
  endforeach()
endmacro()
# Release builds outside macOS compile with line tables only (-g1) unless a
# distribution keeps full symbols for its debug packages; macOS bundles keep a
# dSYM. The client keeps its symbols beside it as glob2.debug and is then
# stripped (GLOB2_SPLIT_SYMBOLS below), and every other program links with -s, so
# nothing shipped carries symbols. Release workflows upload glob2.debug's symbols
# for crash reports (docs/multiplayer/crash-reports.md#symbols). Debug
# information never changes the generated code (GCC and Clang both guarantee
# it), so the stripped client is the same program either way.
set(GLOB2_SPLIT_SYMBOLS OFF)
if(GLOB2_MODE STREQUAL "release" AND NOT GLOB2_RETAIN_SYMBOLS AND NOT darwin)
  list(TRANSFORM cxxflags REPLACE "^-g$" "-g1")
  list(TRANSFORM linkflags REPLACE "^-g$" "-g1")
  set(GLOB2_SPLIT_SYMBOLS ON)
endif()
# Every native target is an executable over static archives, so position-independent
# executable code suffices; -fPIC would make GCC treat each global function as
# interposable and stop inlining it within its own file.
list(APPEND cxxflags -std=gnu++20 -Wall -fPIE)
list(APPEND cflags -fPIE)
# Uninitialized-read diagnostics (DET_INIT=zero|pattern), an environment variable.
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
  # Native release builds optimize the C++ across files at link time: GCC (Linux,
  # MinGW) with full LTO, clang with ThinLTO. QuickJS and openlibm stay out (their
  # objects take only CFLAGS). The archives hold LTO objects, so they are written with
  # the compiler's own archiver where CMake found one (macOS's ar reads bitcode itself).
  find_program(GLOB2_LLD_PROGRAM ld.lld)
  set(release_lto)
  if((GLOB2_PLATFORM STREQUAL "linux" OR windows) AND CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    set(release_lto -flto=auto)
  elseif(darwin AND CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    set(release_lto -flto=thin)
    # The dSYM reads its debug information from these objects. ld64 names them
    # <N>.arm64.thinlto.o inside the directory, so each executable needs its own:
    # links that run concurrently in one directory overwrite each other's objects
    # and fail with undefined ".llvm.<hash>" symbols (ld creates the directory).
    list(APPEND linkflags "-Wl,-object_path_lto,${CMAKE_BINARY_DIR}/lto-objects/$<TARGET_PROPERTY:NAME>")
  elseif(GLOB2_PLATFORM STREQUAL "linux" AND CMAKE_CXX_COMPILER_ID MATCHES "Clang" AND GLOB2_LLD_PROGRAM)
    # GNU ld cannot read LLVM bitcode without a plugin; lld always can.
    set(release_lto -flto=thin)
    list(APPEND linkflags -fuse-ld=lld)
  endif()
  if(release_lto)
    list(APPEND cxxflags ${release_lto})
    list(APPEND linkflags ${release_lto})
    if(CMAKE_CXX_COMPILER_AR AND CMAKE_CXX_COMPILER_RANLIB)
      set(CMAKE_AR "${CMAKE_CXX_COMPILER_AR}")
      set(CMAKE_RANLIB "${CMAKE_CXX_COMPILER_RANLIB}")
    endif()
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
    list(APPEND ccflags -flto)
    list(APPEND linkflags -flto)
  endif()
  if(GLOB2_SIZE_OPTIMIZATION STREQUAL "size")
    list(APPEND cxxflags -Os)
    list(APPEND cflags -Os)
    list(APPEND linkflags -Os)
  endif()
endif()
# profile=1: gprof instrumentation at -O3.
if(GLOB2_MODE STREQUAL "profile")
  list(APPEND cxxflags -pg -O3)
  list(APPEND linkflags -pg -O3)
endif()
# Windows programs start without a console; test binaries drop it again.
if(windows)
  list(APPEND linkflags -mwindows)
endif()
# dev_fast compiles everything at -O0 -g1.
if(GLOB2_DEV_FAST)
  glob2_drop_debug_and_optimization(cflags cxxflags ccflags linkflags)
  list(APPEND ccflags -O0 -g1)
endif()
# Relocatable debug paths whenever ccache is on, so checkouts share cache entries.
# Shipped binaries (installs, macOS bundles) keep absolute debug paths, so their
# symbols resolve without a source map (buildsys/dev_build.py SHIPPING_TARGETS).
if(GLOB2_DEBUG_PREFIX_MAP AND CMAKE_CXX_COMPILER_LAUNCHER AND NOT GLOB2_SHIPPING
   AND NOT (darwin AND GLOB2_MAC_BUNDLE))
  list(APPEND ccflags "-fdebug-prefix-map=${GLOB2_ROOT}=.")
endif()
# SCons put CXXFLAGS ahead of its include path, so a -I passed there wins over the
# Homebrew or MacPorts prefix (the macOS jobs pass keg-only openssl@3 this way while
# the prefix may hold another OpenSSL's headers). CMake's compile rule puts options
# after the include list, so those directories move to the front of it instead.
set(cxx_option_flags)
set(cxx_flag_includes)
set(include_follows OFF)
foreach(word IN LISTS cxxflags)
  if(include_follows)
    list(APPEND cxx_flag_includes "${word}")
    set(include_follows OFF)
  elseif(word STREQUAL "-I")
    set(include_follows ON)
  elseif(word MATCHES "^-I(.+)$")
    list(APPEND cxx_flag_includes "${CMAKE_MATCH_1}")
  else()
    list(APPEND cxx_option_flags "${word}")
  endif()
endforeach()
list(TRANSFORM cxx_flag_includes REPLACE "^(.+)$" "$<$<COMPILE_LANGUAGE:CXX,OBJCXX>:\\1>")
set(cxx_compile_flags ${cxx_option_flags} ${ccflags})
set(c_compile_flags ${cflags} ${ccflags})
# Objective-C++ (macOS) compiles with the C++ flags.
add_compile_options("$<$<COMPILE_LANGUAGE:CXX,OBJCXX>:${cxx_compile_flags}>" "$<$<COMPILE_LANGUAGE:C>:${c_compile_flags}>")
set(GLOB2_EXTRA_COMPILE_FLAGS "" CACHE STRING "Extra compile flags for every object")
if(GLOB2_EXTRA_COMPILE_FLAGS)
  separate_arguments(extra_flags UNIX_COMMAND "${GLOB2_EXTRA_COMPILE_FLAGS}")
  add_compile_options(${extra_flags})
endif()

glob2_bridge(instance official-instance ${build_arg_options})
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
