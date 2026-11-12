# The browser build's Emscripten toolchain, included before project() for a
# GLOB2_BUILD_ARGS with target=web.
#
# The pinned SDK (buildsys/dev_store.py browser_sdk, checked against
# platforms/web/toolchain.json) supplies its own CMake toolchain file. Its
# compilers need the variant's EM_CACHE, EM_PORTS and EM_CONFIG, which the
# configure-time checks get here and every compile and link gets through its
# launcher (cmake/web.cmake).
find_program(GLOB2_TOOLCHAIN_PYTHON NAMES python3 python REQUIRED)
if(Python3_EXECUTABLE)
  set(GLOB2_TOOLCHAIN_PYTHON "${Python3_EXECUTABLE}")
endif()
separate_arguments(web_words UNIX_COMMAND "${GLOB2_BUILD_ARGS}")
set(web_arguments)
foreach(word IN LISTS web_words)
  list(APPEND web_arguments "--arg=${word}")
endforeach()
execute_process(COMMAND "${GLOB2_TOOLCHAIN_PYTHON}" "${CMAKE_CURRENT_LIST_DIR}/../buildsys/cmake_generate.py"
                        web-toolchain --builddir "${CMAKE_BINARY_DIR}" ${web_arguments}
                WORKING_DIRECTORY "${CMAKE_CURRENT_LIST_DIR}/.."
                OUTPUT_VARIABLE web_toolchain RESULT_VARIABLE status OUTPUT_STRIP_TRAILING_WHITESPACE)
if(NOT status EQUAL 0)
  message(FATAL_ERROR "buildsys/cmake_generate.py web-toolchain failed for '${GLOB2_BUILD_ARGS}'")
endif()
string(JSON GLOB2_EMSCRIPTEN GET "${web_toolchain}" emscripten)
string(JSON count LENGTH "${web_toolchain}" environment)
math(EXPR last "${count} - 1")
foreach(index RANGE ${last})
  string(JSON name MEMBER "${web_toolchain}" environment ${index})
  string(JSON value GET "${web_toolchain}" environment ${name})
  set(ENV{${name}} "${value}")
endforeach()
set(CMAKE_TOOLCHAIN_FILE "${GLOB2_EMSCRIPTEN}/cmake/Modules/Platform/Emscripten.cmake")
set(GLOB2_WEB ON)
