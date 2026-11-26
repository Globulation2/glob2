# Mac App Store builds ask Apple's Declared Age Range for the age band of social
# features (src/app/AgeGate.h). The Swift shim, shared with the iOS Xcode project,
# defines glob2_request_age_range over src/app/AgeGate's weak "unavailable" default;
# every other build, the tests included, keeps that default and asks the game's own
# question. Compiled by swiftc into one object so the client still links with
# clang++; the store build is arm64 for macOS 26 (platforms/macos/package_app_store.py),
# so it needs no Swift back-deployment libraries.
#
# Included by CMakeLists.txt for darwin clients with distribution=app_store, after
# the glob2 target exists.
find_program(GLOB2_XCRUN xcrun REQUIRED)
execute_process(COMMAND "${GLOB2_XCRUN}" --sdk macosx --show-sdk-path
                OUTPUT_VARIABLE age_signal_sdk OUTPUT_STRIP_TRAILING_WHITESPACE COMMAND_ERROR_IS_FATAL ANY)
execute_process(COMMAND "${GLOB2_XCRUN}" --sdk macosx --find swiftc
                OUTPUT_VARIABLE age_signal_swiftc OUTPUT_STRIP_TRAILING_WHITESPACE COMMAND_ERROR_IS_FATAL ANY)
set(age_signal_deployment "${CMAKE_OSX_DEPLOYMENT_TARGET}")
if(NOT age_signal_deployment)
  set(age_signal_deployment "$ENV{MACOSX_DEPLOYMENT_TARGET}")
endif()
if(NOT age_signal_deployment)
  set(age_signal_deployment 26.0)
endif()
set(age_signal_arch "${CMAKE_SYSTEM_PROCESSOR}")
if(age_signal_arch STREQUAL "aarch64")
  set(age_signal_arch arm64)
endif()
set(age_signal_source "${GLOB2_ROOT}/platforms/apple/AgeSignal.swift")
set(age_signal_object "${GLOB2_OUT}/age-signal/AgeSignal.o")
add_custom_command(OUTPUT "${age_signal_object}"
  COMMAND "${CMAKE_COMMAND}" -E make_directory "${GLOB2_OUT}/age-signal"
  COMMAND "${age_signal_swiftc}" -parse-as-library -c -O -swift-version 5 -module-name Glob2AgeSignal
          -sdk "${age_signal_sdk}" -target "${age_signal_arch}-apple-macos${age_signal_deployment}"
          -o "${age_signal_object}" "${age_signal_source}"
  DEPENDS "${age_signal_source}"
  COMMENT "Compiling the Declared Age Range shim" VERBATIM)
target_sources(glob2 PRIVATE "${age_signal_object}")
set_source_files_properties("${age_signal_object}" PROPERTIES EXTERNAL_OBJECT TRUE GENERATED TRUE)
# The shim's autolinked Swift runtime lives in the SDK (and the OS); its toolchain
# directory holds the compatibility libraries. The framework is weak, so a
# system without it still launches and reports "unavailable".
get_filename_component(age_signal_toolchain "${age_signal_swiftc}" DIRECTORY)
target_link_options(glob2 PRIVATE "-L${age_signal_sdk}/usr/lib/swift"
                    "-L${age_signal_toolchain}/../lib/swift/macosx"
                    -Wl,-rpath,/usr/lib/swift -Wl,-weak_framework,DeclaredAgeRange)
