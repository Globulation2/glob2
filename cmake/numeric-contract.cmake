# The strict numeric contract: which sources compile with the strict numeric flags
# (GLOB2_STRICT_CXXFLAGS), and QuickJS and openlibm's own flags, exactly as
# buildsys/javascript.py compiles them. Included from CMakeLists.txt once the
# source lists hold absolute paths; source properties are per directory, so they
# apply to the targets CMakeLists.txt creates afterwards.
#
# On the simulation path: the sim-diff check runs for any change to this file
# (.github/scripts/ci_policy.py).

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
set_source_files_properties(${GLOB2_STRICT_NUMERIC_SOURCES} PROPERTIES COMPILE_OPTIONS "${GLOB2_STRICT_CXXFLAGS}")
