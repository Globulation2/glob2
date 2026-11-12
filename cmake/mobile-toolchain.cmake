# The cross compilers of an Android or iOS build, included before project().
#
# buildsys/mobile_toolchain.py discovers the pinned NDK or Xcode compilers for
# GLOB2_BUILD_ARGS (target=android|ios, arch=, android_sdk=, developer_dir=, ...);
# their target and sysroot flags become the languages' initial flags, so CMake's own compiler checks already
# build for the device. cmake/mobile.cmake builds the libraries and tests.
find_program(GLOB2_TOOLCHAIN_PYTHON NAMES python3 python REQUIRED)
if(Python3_EXECUTABLE)
  set(GLOB2_TOOLCHAIN_PYTHON "${Python3_EXECUTABLE}")
endif()
separate_arguments(mobile_words UNIX_COMMAND "${GLOB2_BUILD_ARGS}")
set(mobile_arguments)
foreach(word IN LISTS mobile_words)
  list(APPEND mobile_arguments "--arg=${word}")
endforeach()
execute_process(COMMAND "${GLOB2_TOOLCHAIN_PYTHON}" "${CMAKE_CURRENT_LIST_DIR}/../buildsys/cmake_generate.py"
                        mobile-toolchain ${mobile_arguments}
                WORKING_DIRECTORY "${CMAKE_CURRENT_LIST_DIR}/.."
                OUTPUT_VARIABLE mobile_toolchain RESULT_VARIABLE status OUTPUT_STRIP_TRAILING_WHITESPACE)
if(NOT status EQUAL 0)
  message(FATAL_ERROR "buildsys/cmake_generate.py mobile-toolchain failed for '${GLOB2_BUILD_ARGS}'")
endif()

# Joins a JSON array of words into one command-line string.
function(glob2_json_words output json key)
  string(JSON count LENGTH "${json}" ${key})
  set(words)
  if(count GREATER 0)
    math(EXPR last "${count} - 1")
    foreach(index RANGE ${last})
      string(JSON word GET "${json}" ${key} ${index})
      list(APPEND words "${word}")
    endforeach()
  endif()
  list(JOIN words " " words)
  set(${output} "${words}" PARENT_SCOPE)
endfunction()

string(JSON CMAKE_SYSTEM_NAME GET "${mobile_toolchain}" system)
string(JSON CMAKE_C_COMPILER GET "${mobile_toolchain}" cc)
string(JSON CMAKE_CXX_COMPILER GET "${mobile_toolchain}" cxx)
string(JSON mobile_ar GET "${mobile_toolchain}" ar)
string(JSON mobile_ranlib GET "${mobile_toolchain}" ranlib)
set(CMAKE_AR "${mobile_ar}" CACHE FILEPATH "The mobile toolchain's archiver")
set(CMAKE_RANLIB "${mobile_ranlib}" CACHE FILEPATH "The mobile toolchain's ranlib")
glob2_json_words(mobile_cflags "${mobile_toolchain}" cflags)
glob2_json_words(mobile_ldflags "${mobile_toolchain}" ldflags)
foreach(language C CXX OBJC OBJCXX)
  set(CMAKE_${language}_FLAGS_INIT "${mobile_cflags}")
endforeach()
set(CMAKE_EXE_LINKER_FLAGS_INIT "${mobile_ldflags}")
set(CMAKE_SHARED_LINKER_FLAGS_INIT "${mobile_ldflags}")
string(JSON mobile_sysroot GET "${mobile_toolchain}" sysroot)
if(mobile_sysroot)
  # iOS: the SDK is given; CMake must not add the macOS one. Its compiler checks
  # build archives, as nothing links for the device outside Xcode.
  set(CMAKE_OSX_SYSROOT "${mobile_sysroot}" CACHE PATH "The iOS SDK")
  set(CMAKE_OBJC_COMPILER "${CMAKE_C_COMPILER}")
  set(CMAKE_OBJCXX_COMPILER "${CMAKE_CXX_COMPILER}")
  set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)
endif()
set(GLOB2_MOBILE ON)
