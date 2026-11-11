# Android and iOS: what buildsys/mobile_build.py builds, included by CMakeLists.txt
# for GLOB2_BUILD_ARGS target=android|ios after cmake/mobile-toolchain.cmake chose
# the pinned cross compilers.
#
#   Android  lib/libmain.so (the JNI library Gradle packages) and, for
#            android-tests, tests/glob2-engine-tests and tests/glob2-unit-tests
#            (PIE executables run over adb).
#   iOS      lib/libglob2.a (Xcode links it with SDL's startup) and, for
#            ios-tests, lib/libglob2-script-tests.a.
#
# The same sources (buildsys/sources.py mobile_sources), flags and pinned
# dependency bundle as the SCons build; compared by tools/compare_build_systems.py.
glob2_identity(GLOB2_MOBILE_TARGET target "")
set(android OFF)
if(GLOB2_MOBILE_TARGET STREQUAL "android")
  set(android ON)
else()
  enable_language(OBJCXX)
endif()

# Verifies the dependency bundle, writes BuildConfig.h, the skin materials, the
# shaders and BuildPaths.h, provisions the recording libraries and stages their
# notices; the results come back in mobile-configuration.cmake.
glob2_bridge(unused mobile-configure --builddir "${GLOB2_OUT}" ${build_arg_options})
include("${GLOB2_OUT}/mobile-configuration.cmake")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
  "${GLOB2_MOBILE_MANIFEST}" "${GLOB2_ROOT}/platforms/mobile/toolchain.json"
  "${GLOB2_ROOT}/buildsys/mobile_toolchain.py" "${GLOB2_ROOT}/buildsys/mobile_artifacts.py")

# ---- Generated headers the mobile sources include ----
set(bridge "${Python3_EXECUTABLE}" "${GLOB2_BRIDGE}")
add_custom_command(OUTPUT "${GLOB2_OUT}/include/hive/PlayerAPIContract.h"
  COMMAND ${bridge} hive-api --builddir "${GLOB2_OUT}"
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
add_custom_target(glob2_generated DEPENDS
  "${GLOB2_OUT}/include/hive/PlayerAPIContract.h" "${GLOB2_OUT}/include/generated/OnlineErrorCatalog.h")

# ---- Flags: mobile_build.py's CCFLAGS (after the toolchain's) and CXXFLAGS ----
if(GLOB2_MODE STREQUAL "release")
  set(ccflags -g -O2)
  set(javascript_optimization -O2)
else()
  set(ccflags -g -O0)
  set(javascript_optimization -O0)
endif()
if(GLOB2_DEV_FAST)
  list(FILTER ccflags EXCLUDE REGEX "^-[gO]")
  list(APPEND ccflags -O0 -g1)
endif()
option(GLOB2_DEBUG_PREFIX_MAP "Map the checkout to . in debug information so checkouts share cache entries" ON)
if(GLOB2_DEBUG_PREFIX_MAP AND CMAKE_CXX_COMPILER_LAUNCHER)
  list(APPEND ccflags "-fdebug-prefix-map=${GLOB2_ROOT}=.")
endif()
set(cxxflags -std=gnu++20 -fexceptions)
add_compile_options("$<$<COMPILE_LANGUAGE:CXX,OBJCXX>:${cxxflags}>" ${ccflags})
add_compile_definitions(HAVE_CONFIG_H
  "GLOB2_OFFICIAL_INSTANCE_ORIGIN=\"${GLOB2_MOBILE_OFFICIAL_ORIGIN}\""
  "GLOB2_OFFICIAL_INSTANCE_FORMER_ORIGINS=\"${GLOB2_MOBILE_OFFICIAL_FORMER_ORIGINS}\""
  GLOB2_H264_ASSETS)
set(include_directories "${GLOB2_MOBILE_RECORDING_PREFIX}/include" "${GLOB2_OUT}/shaders" "${GLOB2_ROOT}/third_party/quickjs-ng"
    "${GLOB2_OUT}/include" "${GLOB2_MOBILE_PREFIX}/include" "${GLOB2_MOBILE_PREFIX}/include/opus"
    "${GLOB2_MOBILE_PREFIX}/include/SDL3")
foreach(directory IN LISTS GLOB2_INCLUDE_DIRECTORIES)
  if(directory STREQUAL ".")
    list(APPEND include_directories "${GLOB2_ROOT}")
  else()
    list(APPEND include_directories "${GLOB2_ROOT}/${directory}")
  endif()
endforeach()
include_directories(${include_directories})
# Android links every object into a shared library. Nothing carries a host
# runtime search path: the APK supplies libSDL3.so beside libmain.so.
set(CMAKE_POSITION_INDEPENDENT_CODE ${android})
set(CMAKE_SKIP_RPATH ON)

# ---- The game's objects ----
if(android)
  set(mobile_sources ${GLOB2_ANDROID_SOURCES})
else()
  set(mobile_sources ${GLOB2_IOS_SOURCES})
endif()
list(TRANSFORM mobile_sources PREPEND "${GLOB2_ROOT}/")
add_library(glob2_mobile OBJECT ${mobile_sources})
add_dependencies(glob2_mobile glob2_generated)
list(TRANSFORM GLOB2_MOBILE_STRICT_NUMERIC_SOURCES PREPEND "${GLOB2_ROOT}/")
set_source_files_properties(${GLOB2_MOBILE_STRICT_NUMERIC_SOURCES} PROPERTIES COMPILE_OPTIONS "${GLOB2_STRICT_CXXFLAGS}")
if(NOT android)
  list(TRANSFORM GLOB2_IOS_ARC_SOURCES PREPEND "${GLOB2_ROOT}/")
  set_source_files_properties(${GLOB2_IOS_ARC_SOURCES} PROPERTIES COMPILE_OPTIONS -fobjc-arc)
endif()

# QuickJS and openlibm with the interpreter's strict numeric contract (buildsys/javascript.py).
list(TRANSFORM GLOB2_JAVASCRIPT_SOURCES PREPEND "${GLOB2_ROOT}/")
list(TRANSFORM GLOB2_JAVASCRIPT_INCLUDE_DIRECTORIES PREPEND "${GLOB2_ROOT}/")
add_library(glob2_javascript OBJECT ${GLOB2_JAVASCRIPT_SOURCES})
target_compile_options(glob2_javascript PRIVATE ${javascript_optimization} ${GLOB2_JAVASCRIPT_CFLAGS})
target_compile_definitions(glob2_javascript PRIVATE ${GLOB2_JAVASCRIPT_DEFINES})
target_include_directories(glob2_javascript PRIVATE ${GLOB2_JAVASCRIPT_INCLUDE_DIRECTORIES})

# The numeric guard over the script-reachable objects (javascript.numeric_guard).
set(stamps "${GLOB2_OUT}/generated-stamps")
glob2_objects_of(guarded_objects glob2_mobile ${GLOB2_MOBILE_GUARDED_NUMERIC_SOURCES})
set(numeric_guard "${Python3_EXECUTABLE}" "${GLOB2_ROOT}/tools/javascript/check-math-symbols.py" --nm "${CMAKE_NM}")
add_custom_command(OUTPUT "${stamps}/numeric-guard"
  COMMAND ${numeric_guard} "${guarded_objects}" "$<TARGET_OBJECTS:glob2_javascript>"
  COMMAND "${CMAKE_COMMAND}" -E touch "${stamps}/numeric-guard"
  DEPENDS "${guarded_objects}" "$<TARGET_OBJECTS:glob2_javascript>" "${GLOB2_ROOT}/tools/javascript/check-math-symbols.py"
  COMMENT "Checking script-reachable objects for host math symbols" COMMAND_EXPAND_LISTS VERBATIM)
add_custom_target(glob2_numeric_guard ALL DEPENDS "${stamps}/numeric-guard")
add_dependencies(glob2_numeric_guard glob2_mobile glob2_javascript)

# Test objects carry their build identity (test/support/BuildProvenance.h);
# rewritten on every build, as the revision may have moved.
list(JOIN ccflags " " provenance_ccflags)
list(JOIN cxxflags " " provenance_cxxflags)
set(provenance_configuration "${GLOB2_OUT}/test-provenance-configuration.json")
glob2_bridge(unused provenance-configuration --output "${provenance_configuration}"
             "--compiler=${CMAKE_CXX_COMPILER}" "--ccflags=${CMAKE_CXX_FLAGS} ${provenance_ccflags}"
             "--cxxflags=${provenance_cxxflags}" --define=HAVE_CONFIG_H --define=GLOB2_H264_ASSETS
             --define=GLOB2_TEST_PROVENANCE_EMBEDDED)
add_custom_target(glob2_test_provenance
  COMMAND ${bridge} embedded-provenance --builddir "${GLOB2_OUT}" --configuration "${provenance_configuration}"
  BYPRODUCTS "${GLOB2_OUT}/include/glob2/TestBuildProvenance.h"
  WORKING_DIRECTORY "${GLOB2_ROOT}" VERBATIM)

# Test sources compiled with the test flags: one object library per registry group.
function(glob2_mobile_test_objects output registry prefix)
  set(objects)
  foreach(group IN LISTS GLOB2_${registry}_GROUPS)
    # Mobile devices have no desktop OpenGL and are not macOS.
    set(requirements ${GLOB2_${registry}_${group}_REQUIRE})
    if("opengl" IN_LIST requirements OR "mac" IN_LIST requirements)
      continue()
    endif()
    set(sources ${GLOB2_${registry}_${group}_SOURCES})
    list(TRANSFORM sources PREPEND "${GLOB2_ROOT}/")
    add_library(${prefix}_${group} OBJECT EXCLUDE_FROM_ALL ${sources})
    # Executables' own objects: the toolchain's -fPIC only, as SCons' Object.
    set_target_properties(${prefix}_${group} PROPERTIES POSITION_INDEPENDENT_CODE OFF)
    target_include_directories(${prefix}_${group} PRIVATE "${GLOB2_ROOT}/test" "${GLOB2_ROOT}/test/support"
                               "${GLOB2_ROOT}/libs/gag/src")
    target_compile_definitions(${prefix}_${group} PRIVATE GLOB2_TEST_PROVENANCE_EMBEDDED
                               ${GLOB2_${registry}_${group}_DEFINES} ${ARGN})
    target_compile_options(${prefix}_${group} PRIVATE ${GLOB2_${registry}_${group}_CXXFLAGS})
    add_dependencies(${prefix}_${group} glob2_generated glob2_test_provenance)
    list(APPEND objects "$<TARGET_OBJECTS:${prefix}_${group}>")
  endforeach()
  set(${output} ${objects} PARENT_SCOPE)
endfunction()

if(android)
  # ---- libmain.so: the JNI library, with SDL's and Android's entry points exported ----
  set(libraries -Wl,--start-group ${GLOB2_MOBILE_LIBRARIES} android log dl m ${GLOB2_MOBILE_RECORDING_LIBRARIES}
      -Wl,--end-group)
  set(exports "${GLOB2_ROOT}/platforms/mobile/android/libmain.exports")
  add_library(main SHARED $<TARGET_OBJECTS:glob2_mobile> $<TARGET_OBJECTS:glob2_javascript>)
  target_link_libraries(main PRIVATE ${libraries})
  target_link_options(main PRIVATE ${GLOB2_MOBILE_RECORDING_LINK_OPTIONS}
                      "-Wl,--version-script=${exports}" -Wl,--no-undefined-version)
  set_target_properties(main PROPERTIES LIBRARY_OUTPUT_DIRECTORY "${GLOB2_OUT}/lib" LINK_DEPENDS "${exports}"
                        NO_SONAME ON)
  add_dependencies(main glob2_numeric_guard)

  # ---- android-tests: doctest executables for adb (platforms/mobile/android_device_tests.py) ----
  # They reuse the game's objects; test/support/TestMain.cpp is the entry point and
  # platforms/mobile/NativeTestMain.cpp interposes the platform bridges.
  add_library(glob2_android_test_bridge OBJECT EXCLUDE_FROM_ALL "${GLOB2_ROOT}/platforms/mobile/NativeTestMain.cpp")
  set_target_properties(glob2_android_test_bridge PROPERTIES POSITION_INDEPENDENT_CODE OFF)
  target_include_directories(glob2_android_test_bridge PRIVATE "${GLOB2_ROOT}/test" "${GLOB2_ROOT}/test/support"
                             "${GLOB2_ROOT}/libs/gag/src")
  target_compile_definitions(glob2_android_test_bridge PRIVATE GLOB2_TEST_PROVENANCE_EMBEDDED)
  add_dependencies(glob2_android_test_bridge glob2_generated)
  glob2_mobile_test_objects(engine_objects ANDROID_ENGINE_TESTS glob2_android_engine)
  glob2_mobile_test_objects(unit_objects ANDROID_UNIT_TESTS glob2_android_unit)
  glob2_mobile_test_objects(production_objects ANDROID_UNIT_PRODUCTION glob2_android_production)
  set(client_sources ${GLOB2_ANDROID_SOURCES})
  list(REMOVE_ITEM client_sources "src/app/Glob2.cpp")
  glob2_objects_of(client_objects glob2_mobile ${client_sources})
  set(library_sources ${GLOB2_ANDROID_SOURCES})
  list(FILTER library_sources INCLUDE REGEX "^libs/(gag|usl)/")
  glob2_objects_of(library_objects glob2_mobile ${library_sources})
  glob2_objects_of(reused_objects glob2_mobile ${GLOB2_ANDROID_UNIT_REUSED_SOURCES})
  add_executable(glob2-android-engine-tests EXCLUDE_FROM_ALL ${engine_objects}
                 $<TARGET_OBJECTS:glob2_android_test_bridge> "${client_objects}" $<TARGET_OBJECTS:glob2_javascript>)
  add_executable(glob2-android-unit-tests EXCLUDE_FROM_ALL ${unit_objects} ${production_objects} "${reused_objects}"
                 $<TARGET_OBJECTS:glob2_android_test_bridge> "${library_objects}" $<TARGET_OBJECTS:glob2_javascript>)
  foreach(target glob2-android-engine-tests glob2-android-unit-tests)
    string(REPLACE "-android" "" name "${target}")
    set_target_properties(${target} PROPERTIES OUTPUT_NAME "${name}" RUNTIME_OUTPUT_DIRECTORY "${GLOB2_OUT}/tests"
                          LINKER_LANGUAGE CXX)
    target_link_libraries(${target} PRIVATE ${libraries})
    target_link_options(${target} PRIVATE ${GLOB2_MOBILE_RECORDING_LINK_OPTIONS})
    add_dependencies(${target} glob2_mobile glob2_numeric_guard)
  endforeach()
  add_custom_target(android-tests DEPENDS glob2-android-engine-tests glob2-android-unit-tests)
else()
  # ---- libglob2.a: Xcode links it with SDL's startup and the system frameworks ----
  add_library(glob2 STATIC $<TARGET_OBJECTS:glob2_mobile> $<TARGET_OBJECTS:glob2_javascript>)
  set_target_properties(glob2 PROPERTIES ARCHIVE_OUTPUT_DIRECTORY "${GLOB2_OUT}/lib" LINKER_LANGUAGE CXX)
  add_dependencies(glob2 glob2_numeric_guard)
  # ---- ios-tests: the in-process script corpus as an archive for the test app ----
  # Its TestMain.cpp entry point is renamed; the app calls glob2ScriptTestMain.
  glob2_mobile_test_objects(script_objects IOS_SCRIPT_TESTS glob2_ios_script)
  set_source_files_properties("${GLOB2_ROOT}/test/support/TestMain.cpp" TARGET_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
                              PROPERTIES COMPILE_DEFINITIONS main=glob2ScriptTestMain)
  add_library(glob2-script-tests STATIC EXCLUDE_FROM_ALL $<TARGET_OBJECTS:glob2_mobile>
              $<TARGET_OBJECTS:glob2_javascript> ${script_objects})
  set_target_properties(glob2-script-tests PROPERTIES ARCHIVE_OUTPUT_DIRECTORY "${GLOB2_OUT}/lib" LINKER_LANGUAGE CXX)
  add_dependencies(glob2-script-tests glob2_numeric_guard)
  add_custom_target(ios-tests DEPENDS glob2-script-tests)
endif()
