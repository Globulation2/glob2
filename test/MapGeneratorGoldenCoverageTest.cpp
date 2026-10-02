// SPDX-License-Identifier: GPL-3.0-or-later
#include "Glob2Test.h"
#include "MapGeneratorGoldenCoverage.h"
#include <vector>

TEST_CASE("generator golden coverage cannot borrow a foreign revision")
{
    struct Row { std::string platform; int id; unsigned revision; };
    std::vector<Row> rows = {{"macos-arm64", 59, 2}, {"linux-x86_64", 59, 1}};
    CHECK(mapGoldenCoverage(rows, "linux-x86_64", 59, 2) == MapGoldenCoverage::Stale);
    CHECK(mapGoldenCoverage(rows, "windows-x86_64", 59, 2) == MapGoldenCoverage::Missing);
    CHECK(mapGoldenCoverage(rows, "macos-arm64", 59, 2) == MapGoldenCoverage::Current);
    rows.push_back({"linux-x86_64", 59, 2});
    CHECK(mapGoldenCoverage(rows, "linux-x86_64", 59, 2) == MapGoldenCoverage::Stale);
    rows.erase(rows.begin() + 1);
    CHECK(mapGoldenCoverage(rows, "linux-x86_64", 59, 2) == MapGoldenCoverage::Current);
}
