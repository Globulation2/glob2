# The browser build, included by
# CMakeLists.txt for GLOB2_BUILD_ARGS target=web after cmake/web-toolchain.cmake
# selected the pinned Emscripten SDK.
#
# Both runtimes by default (web_variant=serial|threaded picks one in a
# development build): index.js/.wasm/.data here and threaded/index.*, built from
# their own SDL3 and Opus builds. Beside them: the asset packages and
# asset-manifest.js, the music, recording, asset-video and Hive worker modules,
# the page (index.html and its studio siblings), the loader and the launcher
# fonts. Targets: web-package (everything), web-serial,
# web-threaded and web-tests (script-tests.js for each runtime).
glob2_identity(GLOB2_WEB_RELEASE mode debug)
glob2_bridge(unused web-configure --builddir "${GLOB2_OUT}" ${build_arg_options})
include("${GLOB2_OUT}/web-configuration.cmake")
glob2_configure_depends(${GLOB2_WEB_RECORDING_LIBRARIES} "${GLOB2_WEB_MUSIC_OPUS_PREFIX}/lib")
foreach(variant IN LISTS GLOB2_WEB_VARIANTS)
  string(TOUPPER "${variant}" upper)
  glob2_configure_depends("${GLOB2_WEB_${upper}_SDL_PREFIX}/lib" "${GLOB2_WEB_${upper}_OPUS_PREFIX}/lib")
endforeach()
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
  "${GLOB2_ROOT}/platforms/web/toolchain.json" "${GLOB2_ROOT}/buildsys/sdl3_dependencies.py"
  "${GLOB2_ROOT}/buildsys/opus_dependencies.py" "${GLOB2_ROOT}/buildsys/renderer_shaders.py")
set(web "${GLOB2_ROOT}/platforms/web")
set(bridge "${Python3_EXECUTABLE}" "${GLOB2_BRIDGE}")
set(ports --use-port=zlib)
if(GLOB2_WEB_RELEASE STREQUAL "release")
  set(optimization -O2)
else()
  set(optimization -O0)
endif()
find_program(GLOB2_WEB_CCACHE ccache)
# ccache around em++ (unless Emscripten's EM_COMPILER_WRAPPER caches inside it),
# with relocatable debug paths.
set(web_ccache OFF)
if(GLOB2_WEB_CCACHE AND GLOB2_CCACHE AND NOT DEFINED ENV{EM_COMPILER_WRAPPER})
  set(web_ccache ON)
endif()
option(GLOB2_DEBUG_PREFIX_MAP "Map the checkout to . in debug information so checkouts share cache entries" ON)
set(web_prefix_map)
if(web_ccache AND GLOB2_DEBUG_PREFIX_MAP)
  set(web_prefix_map "-fdebug-prefix-map=${GLOB2_ROOT}=.")
endif()

# The CXXFLAGS/CCFLAGS/LINKFLAGS of a variant's environment, with
# dev_fast applied.
function(glob2_web_flags cxx_out cc_out link_out cxxflags ccflags linkflags)
  if(GLOB2_DEV_FAST)
    list(FILTER cxxflags EXCLUDE REGEX "^-[gO]")
    list(FILTER ccflags EXCLUDE REGEX "^-[gO]")
    list(FILTER linkflags EXCLUDE REGEX "^-[gO]")
    list(APPEND ccflags -O0 -g1)
  endif()
  if(web_prefix_map)
    list(APPEND ccflags "${web_prefix_map}")
  endif()
  set(${cxx_out} "${cxxflags}" PARENT_SCOPE)
  set(${cc_out} "${ccflags}" PARENT_SCOPE)
  set(${link_out} "${linkflags}" PARENT_SCOPE)
endfunction()

# CMake drops repeated options, so each --js-library/--pre-js/--post-js/--preload-file
# travels with its file as one SHELL: option.
function(glob2_web_link_options target)
  set(options)
  set(pending)
  foreach(word IN LISTS ARGN)
    if(pending)
      list(APPEND options "SHELL:${pending} ${word}")
      set(pending)
    elseif(word MATCHES "^--(js-library|pre-js|post-js|preload-file)$")
      set(pending "${word}")
    else()
      list(APPEND options "${word}")
    endif()
  endforeach()
  target_link_options(${target} PRIVATE ${options})
endfunction()

# Compiles and links a target with one variant's Emscripten environment.
function(glob2_web_launcher target environment)
  set(launcher "${CMAKE_COMMAND}" -E env ${environment})
  if(web_ccache)
    glob2_bridge(cache_directory ccache-directory)
    if(GLOB2_CCACHE_BASEDIR)
      list(APPEND launcher "CCACHE_BASEDIR=${GLOB2_CCACHE_BASEDIR}")
    endif()
    list(APPEND launcher "CCACHE_DIR=${cache_directory}" CCACHE_COMPILERCHECK=content)
    set(compile ${launcher} "${GLOB2_WEB_CCACHE}")
  else()
    set(compile ${launcher})
  endif()
  set_target_properties(${target} PROPERTIES C_COMPILER_LAUNCHER "${compile}" CXX_COMPILER_LAUNCHER "${compile}"
                        C_LINKER_LAUNCHER "${launcher}" CXX_LINKER_LAUNCHER "${launcher}")
endfunction()

# ---- Game data: the shared runtime-assets export, packaged beside the page ----
set(GLOB2_ASSET_JOBS 8 CACHE STRING "Parallel jobs for an asset export that misses the shared store")
add_custom_command(OUTPUT "${GLOB2_OUT}/asset-manifest.js"
  COMMAND ${bridge} web-assets --builddir "${GLOB2_OUT}" --depfile "${GLOB2_OUT}/asset-manifest.d"
          --jobs "${GLOB2_ASSET_JOBS}" ${build_arg_options}
  DEPFILE "${GLOB2_OUT}/asset-manifest.d"
  DEPENDS "${GLOB2_BRIDGE}" "${GLOB2_ROOT}/buildsys/web_assets.py" "${GLOB2_ROOT}/buildsys/shared_assets.py"
  WORKING_DIRECTORY "${GLOB2_ROOT}" COMMENT "Exporting and packaging browser game data" VERBATIM)
add_custom_target(glob2_web_assets DEPENDS "${GLOB2_OUT}/asset-manifest.js")

set(web_common_includes)
foreach(directory IN LISTS GLOB2_INCLUDE_DIRECTORIES)
  if(directory STREQUAL ".")
    list(APPEND web_common_includes "${GLOB2_ROOT}")
  else()
    list(APPEND web_common_includes "${GLOB2_ROOT}/${directory}")
  endif()
endforeach()
list(TRANSFORM GLOB2_WEB_SOURCES PREPEND "${GLOB2_ROOT}/" OUTPUT_VARIABLE web_sources)
list(TRANSFORM GLOB2_WEB_STRICT_NUMERIC_SOURCES PREPEND "${GLOB2_ROOT}/" OUTPUT_VARIABLE web_strict_sources)
list(TRANSFORM GLOB2_JAVASCRIPT_SOURCES PREPEND "${GLOB2_ROOT}/" OUTPUT_VARIABLE javascript_sources)
list(TRANSFORM GLOB2_JAVASCRIPT_INCLUDE_DIRECTORIES PREPEND "${GLOB2_ROOT}/" OUTPUT_VARIABLE javascript_includes)
set(official_definitions HAVE_CONFIG_H
  "GLOB2_OFFICIAL_INSTANCE_ORIGIN=\"${GLOB2_WEB_OFFICIAL_ORIGIN}\""
  "GLOB2_OFFICIAL_INSTANCE_FORMER_ORIGINS=\"${GLOB2_WEB_OFFICIAL_FORMER_ORIGINS}\"")
set_source_files_properties(${web_strict_sources} PROPERTIES COMPILE_OPTIONS "${GLOB2_STRICT_CXXFLAGS}")
set_source_files_properties("${GLOB2_ROOT}/src/app/Glob2.cpp" PROPERTIES
  COMPILE_DEFINITIONS "SDL_MAIN_HANDLED;main=glob2ApplicationMain")
# Shipped modules name sources relative to the checkout in __FILE__ (assertions),
# rather than the build host's path.
add_compile_options("-fmacro-prefix-map=${GLOB2_ROOT}/=")
# QuickJS and openlibm (buildsys/javascript.py javascript_objects): CFLAGS first,
# the interpreter's include directories after the shared ones.
function(glob2_web_javascript target ccflags optimization includes)
  add_library(${target} OBJECT EXCLUDE_FROM_ALL ${javascript_sources})
  target_compile_options(${target} PRIVATE ${optimization} ${GLOB2_JAVASCRIPT_CFLAGS} ${ccflags})
  target_compile_definitions(${target} PRIVATE ${GLOB2_JAVASCRIPT_DEFINES})
  target_include_directories(${target} PRIVATE ${includes} ${javascript_includes})
endfunction()

# The numeric guard over script-reachable objects, with LLVM's nm (emnm is a
# shell wrapper that breaks under paths with spaces).
set(web_nm "${GLOB2_EMSCRIPTEN}/../bin/llvm-nm")
if(NOT EXISTS "${web_nm}")
  set(web_nm "${CMAKE_NM}")
endif()
set(numeric_guard "${Python3_EXECUTABLE}" "${GLOB2_ROOT}/tools/javascript/check-math-symbols.py" --nm "${web_nm}")
set(stamps "${GLOB2_OUT}/generated-stamps")
set(web_runtimes)
set(web_aliases)

foreach(variant IN LISTS GLOB2_WEB_VARIANTS)
  string(TOUPPER "${variant}" upper)
  set(out "${GLOB2_WEB_${upper}_OUT}")
  set(sdl "${GLOB2_WEB_${upper}_SDL_PREFIX}")
  set(opus "${GLOB2_WEB_${upper}_OPUS_PREFIX}")
  set(environment ${GLOB2_WEB_${upper}_ENVIRONMENT} "TMPDIR=${out}/tmp" "TMP=${out}/tmp" "TEMP=${out}/tmp")
  if(variant STREQUAL "threaded")
    set(threads -pthread)
  else()
    set(threads)
  endif()

  # Generated headers the game includes, in this variant's include directory.
  add_custom_command(OUTPUT "${out}/include/hive/PlayerAPIContract.h"
    COMMAND ${bridge} hive-api --builddir "${out}"
    DEPENDS "${GLOB2_BRIDGE}" "${GLOB2_ROOT}/buildsys/hive_api.py"
            "${GLOB2_ROOT}/services/online/packages/hive/src/commander-api.txt"
    WORKING_DIRECTORY "${GLOB2_ROOT}" COMMENT "Generating Hive player API contract (${variant})" VERBATIM)
  set(locales "${GLOB2_ROOT}/services/online/packages/i18n/locales")
  add_custom_command(OUTPUT "${out}/include/generated/OnlineErrorCatalog.h"
    COMMAND "${Python3_EXECUTABLE}" "${GLOB2_ROOT}/tools/localization/build_online_error_catalog.py"
            --english "${locales}/en.json" --bulgarian "${locales}/bg.json" --thai "${locales}/th.json"
            --output "${out}/include/generated/OnlineErrorCatalog.h"
    DEPENDS "${GLOB2_ROOT}/tools/localization/build_online_error_catalog.py"
            "${locales}/en.json" "${locales}/bg.json" "${locales}/th.json"
    WORKING_DIRECTORY "${GLOB2_ROOT}" COMMENT "Embedding reviewed API error text (${variant})" VERBATIM)
  # Warm the pinned Emscripten ports before anything compiles against them
  # (serial ports first: the threaded ones build from them).
  set(port_commands)
  if(threads)
    set(port_commands COMMAND "${CMAKE_COMMAND}" -E env ${environment} "${CMAKE_CXX_COMPILER}" ${ports}
                      -x c++ -c -o "${out}/ports-bootstrap.o" /dev/null)
  endif()
  add_custom_command(OUTPUT "${out}/ports-ready.o" ${port_commands}
    COMMAND "${CMAKE_COMMAND}" -E env ${environment} "${CMAKE_CXX_COMPILER}" ${threads} ${ports}
            -x c++ -c -o "${out}/ports-ready.o" /dev/null
    DEPENDS "${GLOB2_ROOT}/platforms/web/toolchain.json"
    COMMENT "Preparing pinned Emscripten ports (${variant})" VERBATIM)
  add_custom_target(glob2_web_${variant}_generated DEPENDS "${out}/ports-ready.o"
    "${out}/include/hive/PlayerAPIContract.h" "${out}/include/generated/OnlineErrorCatalog.h")

  glob2_web_flags(cxxflags ccflags linkflags
    "-std=gnu++20;-fwasm-exceptions;-g2;${optimization};${ports}" "${threads}" "")
  set(includes "${GLOB2_WEB_${upper}_SHADERS}" "${out}/include" "${sdl}/include" ${web_common_includes}
      "${opus}/include" "${opus}/include/opus" "${GLOB2_ROOT}/third_party/quickjs-ng")

  # The game's objects and the interpreter's.
  add_library(glob2_web_${variant} OBJECT EXCLUDE_FROM_ALL ${web_sources})
  target_compile_options(glob2_web_${variant} PRIVATE "$<$<COMPILE_LANGUAGE:CXX>:${cxxflags}>" ${ccflags})
  target_include_directories(glob2_web_${variant} PRIVATE ${includes})
  glob2_web_javascript(glob2_web_${variant}_javascript "${ccflags}" ${optimization} "${includes}")
  foreach(target glob2_web_${variant} glob2_web_${variant}_javascript)
    target_compile_definitions(${target} PRIVATE ${official_definitions})
    add_dependencies(${target} glob2_web_${variant}_generated)
    glob2_web_launcher(${target} "${environment}")
  endforeach()
  glob2_objects_of(guarded glob2_web_${variant} ${GLOB2_WEB_GUARDED_NUMERIC_SOURCES})
  add_custom_command(OUTPUT "${stamps}/numeric-guard-${variant}"
    COMMAND ${numeric_guard} "${guarded}" "$<TARGET_OBJECTS:glob2_web_${variant}_javascript>"
    COMMAND "${CMAKE_COMMAND}" -E touch "${stamps}/numeric-guard-${variant}"
    DEPENDS "${guarded}" "$<TARGET_OBJECTS:glob2_web_${variant}_javascript>"
            "${GLOB2_ROOT}/tools/javascript/check-math-symbols.py"
    COMMENT "Checking script-reachable objects for host math symbols (${variant})" COMMAND_EXPAND_LISTS VERBATIM)
  add_custom_target(glob2_web_${variant}_guard DEPENDS "${stamps}/numeric-guard-${variant}")
  add_dependencies(glob2_web_${variant}_guard glob2_web_${variant} glob2_web_${variant}_javascript)

  # ---- index.js: the runtime ----
  set(link --js-library "${web}/canvas-size.js" --js-library "${web}/webgpu-device.js"
      --js-library "${web}/gradient-compute.js")
  if(threads)
    list(APPEND link -pthread -sOFFSCREENCANVAS_SUPPORT=1 -sDEFAULT_PTHREAD_STACK_SIZE=8388608
         -sPTHREAD_POOL_SIZE_STRICT=0 "-sPTHREAD_POOL_SIZE=Math.min(2*Math.max(navigator.hardwareConcurrency||1,1),16)"
         -sALLOW_BLOCKING_ON_MAIN_THREAD=0 --js-library "${web}/threaded-egl.js")
  endif()
  set(shaders "${GLOB2_WEB_${upper}_SHADERS}")
  list(APPEND link --preload-file "${shaders}/webgl2@/shaders/webgl2")
  if(GLOB2_WEB_${upper}_WEBGPU)
    list(APPEND link --preload-file "${shaders}/webgpu@/shaders/webgpu"
         --preload-file "${shaders}/metadata.json@/shaders/metadata.json")
  endif()
  if(GLOB2_WEB_LEGACY_GL)
    set(legacy 1)
  else()
    set(legacy 0)
  endif()
  list(APPEND link -fwasm-exceptions ${optimization} -sLEGACY_GL_EMULATION=${legacy} -sFETCH=1
       -sMIN_WEBGL_VERSION=2 -sMAX_WEBGL_VERSION=2 -sALLOW_MEMORY_GROWTH -sINITIAL_MEMORY=134217728
       -sSTACK_SIZE=8388608 -sASSERTIONS=1 -sFORCE_FILESYSTEM -lidbfs.js -lwebsocket.js
       "-sEXPORTED_FUNCTIONS=[\"_main\",\"_malloc\",\"_free\"]"
       "-sEXPORTED_RUNTIME_METHODS=[\"callMain\",\"FS\"]"
       --pre-js "${web}/storage.js" --pre-js "${web}/file-selection.js" --pre-js "${web}/audio.js"
       --pre-js "${web}/recording.js" --pre-js "${web}/runtime.js" ${ports})
  if(GLOB2_WEB_LEGACY_GL)
    list(APPEND link --pre-js "${web}/legacy-gl-runtime.js" --post-js "${web}/webgl-shaders.js")
  endif()
  string(REGEX MATCH "(^| )web_profile=1( |$)" profile " ${GLOB2_BUILD_ARGS} ")
  if(profile)
    list(APPEND link --profiling-funcs)  # function names for browser CPU profiles
  endif()
  glob2_web_flags(unused_cxx unused_cc test_link "" "" "${link}")
  set(game_link ${test_link} --pre-js "${GLOB2_OUT}/asset-manifest.js" --pre-js "${web}/asset-loader.js")
  # SDL_ttf's static archive requires HarfBuzz; HarfBuzz in turn uses FreeType.
  set(libraries -L${sdl}/lib -lSDL3_ttf -lSDL3_image -lSDL3_net -lSDL3 -lharfbuzz -lfreetype -lwebpdemux
      -lwebpmux -lwebp -lsharpyuv)
  foreach(name IN LISTS GLOB2_WEB_OPUS_LIBRARIES)
    list(APPEND libraries "${opus}/lib/lib${name}.a")
  endforeach()
  set(link_inputs "${web}/gradient-compute.js" "${web}/storage.js" "${web}/file-selection.js" "${web}/audio.js"
      "${web}/recording.js" "${web}/runtime.js" "${web}/webgl-shaders.js" "${web}/asset-loader.js"
      "${web}/canvas-size.js" "${web}/webgpu-device.js" "${GLOB2_ROOT}/platforms/web/toolchain.json")
  if(GLOB2_WEB_LEGACY_GL)
    list(APPEND link_inputs "${web}/legacy-gl-runtime.js")
  endif()
  if(threads)
    list(APPEND link_inputs "${web}/threaded-egl.js")
  endif()
  set(runtime glob2_web_${variant}_runtime)
  add_executable(${runtime} EXCLUDE_FROM_ALL $<TARGET_OBJECTS:glob2_web_${variant}>
                 $<TARGET_OBJECTS:glob2_web_${variant}_javascript>)
  glob2_web_link_options(${runtime} ${game_link})
  target_link_libraries(${runtime} PRIVATE ${libraries})
  set_target_properties(${runtime} PROPERTIES OUTPUT_NAME index RUNTIME_OUTPUT_DIRECTORY "${out}"
                        LINK_DEPENDS "${link_inputs};${GLOB2_OUT}/asset-manifest.js" LINKER_LANGUAGE CXX
                        ADDITIONAL_CLEAN_FILES "${out}/index.wasm;${out}/index.data")
  add_dependencies(${runtime} glob2_web_assets glob2_web_${variant}_guard)
  glob2_web_launcher(${runtime} "${environment}")
  add_custom_target(web-${variant} DEPENDS ${runtime})
  list(APPEND web_runtimes ${runtime})

  # ---- script-tests.js: the in-process corpus (test/tests.py web_test_entries) ----
  # The harness page has no asset packages beside it: it preloads the exported data.
  set(harness_link ${test_link})
  foreach(directory data maps campaigns scripts)
    list(APPEND harness_link --preload-file "${GLOB2_OUT}/runtime-assets/${directory}@/${directory}")
  endforeach()
  list(APPEND harness_link --preload-file "${GLOB2_ROOT}/test/fixtures@/test/fixtures"
       --preload-file "${GLOB2_ROOT}/examples/javascript@/examples/javascript"
       --preload-file "${GLOB2_ROOT}/content/games@/games" -sEXIT_RUNTIME=0)
  set(provenance "${out}/test-provenance-configuration.json")
  list(JOIN cxxflags " " provenance_cxxflags)
  list(JOIN ccflags " " provenance_ccflags)
  glob2_bridge(unused provenance-configuration --output "${provenance}" "--compiler=${CMAKE_CXX_COMPILER}"
               "--ccflags=${provenance_ccflags}" "--cxxflags=${provenance_cxxflags}" --define=HAVE_CONFIG_H
               --define=GLOB2_TEST_PROVENANCE_EMBEDDED)
  add_custom_target(glob2_web_${variant}_provenance
    COMMAND ${bridge} embedded-provenance --builddir "${out}" --configuration "${provenance}"
    BYPRODUCTS "${out}/include/glob2/TestBuildProvenance.h" WORKING_DIRECTORY "${GLOB2_ROOT}" VERBATIM)
  set(test_objects)
  foreach(group IN LISTS GLOB2_WEB_TESTS_GROUPS)
    set(sources ${GLOB2_WEB_TESTS_${group}_SOURCES})
    list(TRANSFORM sources PREPEND "${GLOB2_ROOT}/")
    set(name glob2_web_${variant}_tests_${group})
    add_library(${name} OBJECT EXCLUDE_FROM_ALL ${sources})
    target_compile_options(${name} PRIVATE "$<$<COMPILE_LANGUAGE:CXX>:${cxxflags}>" ${ccflags}
                           ${GLOB2_WEB_TESTS_${group}_CXXFLAGS})
    target_compile_definitions(${name} PRIVATE ${official_definitions} GLOB2_TEST_PROVENANCE_EMBEDDED
                               ${GLOB2_WEB_TESTS_${group}_DEFINES})
    target_include_directories(${name} PRIVATE ${includes} "${GLOB2_ROOT}/test" "${GLOB2_ROOT}/test/support"
                               "${GLOB2_ROOT}/libs/gag/src")
    add_dependencies(${name} glob2_web_${variant}_generated glob2_web_${variant}_provenance)
    glob2_web_launcher(${name} "${environment}")
    list(APPEND test_objects "$<TARGET_OBJECTS:${name}>")
  endforeach()
  set(production ${GLOB2_WEB_SOURCES})
  list(REMOVE_ITEM production src/app/Glob2.cpp)
  glob2_objects_of(production_objects glob2_web_${variant} ${production})
  set(harness glob2_web_${variant}_script_tests)
  add_executable(${harness} EXCLUDE_FROM_ALL "${production_objects}"
                 $<TARGET_OBJECTS:glob2_web_${variant}_javascript> ${test_objects})
  glob2_web_link_options(${harness} ${harness_link})
  target_link_libraries(${harness} PRIVATE ${libraries})
  set_target_properties(${harness} PROPERTIES OUTPUT_NAME script-tests RUNTIME_OUTPUT_DIRECTORY "${out}"
                        LINK_DEPENDS "${link_inputs}" LINKER_LANGUAGE CXX)
  add_dependencies(${harness} glob2_web_assets glob2_web_${variant} glob2_web_${variant}_guard)
  glob2_web_launcher(${harness} "${environment}")
  list(APPEND web_aliases ${harness})
endforeach()
set_source_files_properties("${GLOB2_ROOT}/test/support/TestMain.cpp" PROPERTIES
  COMPILE_DEFINITIONS "SDL_MAIN_HANDLED;main=glob2ApplicationMain")
add_custom_target(web-tests DEPENDS ${web_aliases})

# ---- Worker modules: own WebAssembly memories, built once from the serial environment ----
list(GET GLOB2_WEB_VARIANTS 0 first)
string(TOUPPER "${first}" upper)
set(worker_environment ${GLOB2_WEB_${upper}_ENVIRONMENT} "TMPDIR=${GLOB2_OUT}/tmp")
set(worker_includes "${GLOB2_WEB_${upper}_SHADERS}" "${GLOB2_WEB_${upper}_OUT}/include"
    "${GLOB2_WEB_${upper}_SDL_PREFIX}/include" ${web_common_includes} "${GLOB2_WEB_${upper}_OPUS_PREFIX}/include"
    "${GLOB2_WEB_${upper}_OPUS_PREFIX}/include/opus" "${GLOB2_ROOT}/third_party/quickjs-ng")
glob2_web_flags(game_cxxflags unused_cc unused_link "-std=gnu++20;-fwasm-exceptions;-g2;${optimization};${ports}" "" "")
glob2_web_flags(recording_cxxflags recording_ccflags unused_link "-std=gnu++20;-fwasm-exceptions" "-O3;-msimd128" "")
# The recording modules have their own CCFLAGS, without the debug-path map.
if(web_prefix_map)
  list(REMOVE_ITEM recording_ccflags "${web_prefix_map}")
endif()
# name, compile options, extra include, extra definitions, libraries, link options
function(glob2_web_worker name cxxflags ccflags include definitions libraries link)
  list(TRANSFORM GLOB2_WEB_WORKER_${name}_SOURCES PREPEND "${GLOB2_ROOT}/" OUTPUT_VARIABLE sources)
  set(target glob2_web_${name}_worker)
  add_executable(${target} EXCLUDE_FROM_ALL ${sources} ${ARGN})
  target_compile_options(${target} PRIVATE "$<$<COMPILE_LANGUAGE:CXX>:${cxxflags}>" ${ccflags})
  target_include_directories(${target} PRIVATE ${include} ${worker_includes})
  target_compile_definitions(${target} PRIVATE ${official_definitions} ${definitions})
  glob2_web_link_options(${target} ${link})
  target_link_libraries(${target} PRIVATE ${libraries})
  set_target_properties(${target} PROPERTIES OUTPUT_NAME ${name}-runtime RUNTIME_OUTPUT_DIRECTORY "${GLOB2_OUT}"
                        LINKER_LANGUAGE CXX)
  add_dependencies(${target} glob2_web_${first}_generated)
  glob2_web_launcher(${target} "${worker_environment}")
endfunction()
set(music_libraries)
foreach(name opusfile opus ogg)
  list(APPEND music_libraries "${GLOB2_WEB_MUSIC_OPUS_PREFIX}/lib/lib${name}.a")
endforeach()
glob2_web_flags(unused_cxx unused_cc music_link "" "" "-O2;-fwasm-exceptions")
glob2_web_worker(music "${game_cxxflags}" "${web_prefix_map}" "" "" "${music_libraries}"
  "${music_link};--no-entry;-sMODULARIZE=1;-sEXPORT_NAME=createMusicRuntime;-sENVIRONMENT=worker;-sALLOW_MEMORY_GROWTH=1;-sMAXIMUM_MEMORY=268435456;-sEXPORTED_FUNCTIONS=[\"_malloc\",\"_free\"];-sEXPORTED_RUNTIME_METHODS=[\"UTF8ToString\",\"HEAPU8\",\"HEAP16\"]")
glob2_web_flags(unused_cxx unused_cc recording_link "" "" "-O3;-msimd128;-fwasm-exceptions")
glob2_web_worker(recording "${recording_cxxflags}" "${recording_ccflags}" "${GLOB2_WEB_RECORDING_PREFIX}/include" ""
  "${GLOB2_WEB_RECORDING_LIBRARIES}"
  "${recording_link};--no-entry;-sMODULARIZE=1;-sEXPORT_NAME=createRecordingRuntime;-sENVIRONMENT=worker;-sFILESYSTEM=0;-sALLOW_MEMORY_GROWTH=1;-sINITIAL_MEMORY=33554432;-sMAXIMUM_MEMORY=1073741824;-sSTACK_SIZE=8388608;-sEXPORTED_FUNCTIONS=[\"_malloc\",\"_free\"];-sEXPORTED_RUNTIME_METHODS=[\"ccall\",\"HEAPU8\"]")
glob2_web_worker(asset-video "${recording_cxxflags}" "${recording_ccflags}" "${GLOB2_WEB_RECORDING_PREFIX}/include"
  GLOB2_H264_ASSETS "${GLOB2_WEB_RECORDING_LIBRARIES}"
  "${recording_link};--no-entry;-sMODULARIZE=1;-sEXPORT_NAME=createAssetVideoRuntime;-sENVIRONMENT=worker;-sFILESYSTEM=0;-sALLOW_MEMORY_GROWTH=1;-sINITIAL_MEMORY=33554432;-sMAXIMUM_MEMORY=268435456;-sSTACK_SIZE=2097152;-sEXPORTED_FUNCTIONS=[\"_glob2_decode_asset_video\",\"_malloc\",\"_free\"];-sEXPORTED_RUNTIME_METHODS=[\"HEAPU8\"]")
# Assistant programs never execute in the live game's memory; their interpreter
# is always optimized (javascript_objects(..., release=True)).
glob2_web_javascript(glob2_web_hive_javascript "${web_prefix_map}" -O2 "${worker_includes}")
target_compile_definitions(glob2_web_hive_javascript PRIVATE ${official_definitions})
glob2_web_launcher(glob2_web_hive_javascript "${worker_environment}")
add_dependencies(glob2_web_hive_javascript glob2_web_${first}_generated)
glob2_web_flags(unused_cxx unused_cc hive_link "" "" "-fwasm-exceptions")
glob2_web_worker(hive "${game_cxxflags};${GLOB2_STRICT_CXXFLAGS}" "${web_prefix_map}" "" "" ""
  "${hive_link};--no-entry;-sMODULARIZE=1;-sEXPORT_NAME=createHiveRuntime;-sENVIRONMENT=worker;-sFILESYSTEM=0;-sALLOW_MEMORY_GROWTH=1;-sMAXIMUM_MEMORY=268435456;-sSTACK_SIZE=8388608;-sEXPORTED_FUNCTIONS=[\"_glob2_hive_invoke\",\"_malloc\",\"_free\"];-sEXPORTED_RUNTIME_METHODS=[\"ccall\",\"stringToUTF8\",\"lengthBytesUTF8\"]"
  $<TARGET_OBJECTS:glob2_web_hive_javascript>)
add_custom_command(OUTPUT "${stamps}/numeric-guard-hive"
  COMMAND ${numeric_guard} "$<TARGET_OBJECTS:glob2_web_hive_javascript>"
  COMMAND "${CMAKE_COMMAND}" -E touch "${stamps}/numeric-guard-hive"
  DEPENDS "$<TARGET_OBJECTS:glob2_web_hive_javascript>" COMMAND_EXPAND_LISTS VERBATIM)
add_custom_target(glob2_web_hive_guard DEPENDS "${stamps}/numeric-guard-hive")
add_dependencies(glob2_web_hive_worker glob2_web_hive_guard)

# ---- Files served beside the page ----
set(installed)
function(glob2_web_install destination)
  foreach(source IN LISTS ARGN)
    get_filename_component(name "${source}" NAME)
    add_custom_command(OUTPUT "${destination}/${name}"
      COMMAND "${CMAKE_COMMAND}" -E copy "${source}" "${destination}/${name}" DEPENDS "${source}" VERBATIM)
    set_property(GLOBAL APPEND PROPERTY GLOB2_WEB_INSTALLED "${destination}/${name}")
  endforeach()
endfunction()
glob2_web_install("${GLOB2_OUT}" "${web}/music-worker.js" "${web}/music-output.js" "${web}/recording-worker.js"
  "${web}/recording-storage.js" "${web}/recording-video.js" "${web}/asset-video-worker.js" "${web}/hive-worker.js"
  "${web}/loader.js" "${web}/presentation-surface.js" "${web}/i18n.js")
file(GLOB locale_files CONFIGURE_DEPENDS "${GLOB2_ROOT}/services/online/packages/i18n/locales/*.json")
glob2_web_install("${GLOB2_OUT}/locales" ${locale_files})
file(GLOB recording_notices "${GLOB2_WEB_RECORDING_PREFIX}/share/licenses/recording/*")
glob2_web_install("${GLOB2_OUT}/licenses/recording" ${recording_notices})
list(GET GLOB2_WEB_VARIANTS 0 first)
string(TOUPPER "${first}" upper)
file(GLOB opus_notices "${GLOB2_WEB_${upper}_OPUS_PREFIX}/share/licenses/opus/*")
glob2_web_install("${GLOB2_OUT}/licenses/opus" ${opus_notices})
get_property(installed GLOBAL PROPERTY GLOB2_WEB_INSTALLED)
# The pre-Wasm launcher cannot use fonts inside Emscripten's file system.
file(READ "${GLOB2_ROOT}/content/data/fonts/scripts/manifest.json" font_manifest)
string(JSON font_count LENGTH "${font_manifest}" fonts)
set(font_outputs "${GLOB2_OUT}/script-fonts.css" "${GLOB2_OUT}/fonts/sans-core.ttf"
    "${GLOB2_OUT}/fonts/LICENSE-DejaVu.txt" "${GLOB2_OUT}/fonts/LICENSE-Droid.txt" "${GLOB2_OUT}/fonts/LICENSE-Noto.txt")
set(font_inputs "${GLOB2_ROOT}/services/online/packages/i18n/script-fonts.css"
    "${GLOB2_ROOT}/content/data/fonts/scripts/manifest.json" "${GLOB2_ROOT}/content/data/fonts/LICENSE-Noto.txt"
    "${GLOB2_ROOT}/content/data/fonts/LICENSE-DejaVu.txt" "${GLOB2_ROOT}/content/data/fonts/LICENSE-Droid.txt"
    "${web}/assets/sans-core.ttf" "${GLOB2_ROOT}/buildsys/web_assets.py")
math(EXPR last "${font_count} - 1")
foreach(index RANGE ${last})
  string(JSON font GET "${font_manifest}" fonts ${index} name)
  list(APPEND font_outputs "${GLOB2_OUT}/fonts/${font}-Regular.ttf")
  list(APPEND font_inputs "${GLOB2_ROOT}/content/data/fonts/scripts/${font}-Regular.ttf")
endforeach()
add_custom_command(OUTPUT ${font_outputs}
  COMMAND ${bridge} web-fonts --builddir "${GLOB2_OUT}" DEPENDS ${font_inputs}
  WORKING_DIRECTORY "${GLOB2_ROOT}" COMMENT "Packaging launcher script fonts" VERBATIM)

# ---- The page: index.html and its studio siblings, sized against each runtime's .wasm ----
set(page_outputs "${GLOB2_OUT}/index.html" "${GLOB2_OUT}/studio.html" "${GLOB2_OUT}/set-preview.html"
    "${GLOB2_OUT}/generator-studio.html")
set(variant_options)
set(runtime_files)
foreach(variant IN LISTS GLOB2_WEB_VARIANTS)
  list(APPEND variant_options --variant ${variant})
  list(APPEND runtime_files "$<TARGET_FILE:glob2_web_${variant}_runtime>")
endforeach()
add_custom_command(OUTPUT ${page_outputs}
  COMMAND ${bridge} web-page --builddir "${GLOB2_OUT}" ${variant_options}
  DEPENDS ${web_runtimes} ${runtime_files} "${web}/shell.html" "${web}/studio-envelope.js"
          "${web}/generator-studio.js" "${web}/studio.js" "${web}/set-preview.js" "${web}/loader.js"
  WORKING_DIRECTORY "${GLOB2_ROOT}" COMMENT "Packaging browser runtimes" VERBATIM)
add_custom_target(web-package ALL DEPENDS ${page_outputs} ${installed} ${font_outputs})
add_dependencies(web-package glob2_web_music_worker glob2_web_recording_worker glob2_web_asset-video_worker
                 glob2_web_hive_worker ${web_runtimes})
