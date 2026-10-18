#!/bin/sh
set -eu
# Run from the repository root at the tested commit.
mkdir -p artifacts/compute-auto
cat > artifacts/compute-auto/test-main.cpp <<'CPP'
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "Glob2Test.h"
CPP
c++ -std=c++20 -O0 -g1 -pthread -Itest/support -Isrc -Isrc/app -Isrc/common -Ilibgag/include artifacts/compute-auto/test-main.cpp src/common/ComputeExecutorHarness.cpp src/app/ApplicationResourcePlanTest.cpp src/app/ApplicationResourcePlan.cpp src/common/ComputeThreads.cpp -lSDL3 -o artifacts/compute-auto/rebased-compute-tests
artifacts/compute-auto/rebased-compute-tests
python3 - <<'PYTHON'
from pathlib import Path
s = Path('src/common/ComputeExecutorHarness.cpp').read_text()
cases = s[s.index('TEST_SUITE("ComputeExecutor")'):s.index('TEST_CASE("supported platforms')]
Path('artifacts/compute-auto/browser-sizing-cases.cpp').write_text('#include "Glob2Test.h"\n#include "ComputeThreads.h"\n#include <limits>\n' + cases + '}\n')
PYTHON
c++ -std=c++20 -O0 -g1 -pthread -D__EMSCRIPTEN__ -Itest/support -Isrc -Isrc/app -Isrc/common -Ilibgag/include artifacts/compute-auto/test-main.cpp artifacts/compute-auto/browser-sizing-cases.cpp src/app/ApplicationResourcePlanTest.cpp src/app/ApplicationResourcePlan.cpp src/common/ComputeThreads.cpp -o artifacts/compute-auto/browser-policy-tests
artifacts/compute-auto/browser-policy-tests
git diff origin/master --check
