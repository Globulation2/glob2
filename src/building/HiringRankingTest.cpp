// SPDX-License-Identifier: GPL-3.0-or-later
#include "Glob2Test.h"
#include "HiringRanking.h"
#include <climits>
TEST_SUITE("HiringRanking")
{
TEST_CASE("material ranking keeps skill preference and stable ties")
{
    CHECK(HiringRanking::materialLevel(2, 1)>HiringRanking::materialLevel(1, 9));
    CHECK(HiringRanking::materialBetter(21, 1000, 19, 0));
    CHECK(HiringRanking::materialBetter(21, 9, 21, 10));
    CHECK_FALSE(HiringRanking::materialBetter(21, 10, 21, 10));
    CHECK(HiringRanking::materialBetter(0, INT_MAX, -1, INT_MAX));
}
TEST_CASE("attraction roles preserve opposite skill preferences")
{
    CHECK(HiringRanking::roleBetter(0, 1, 1000, 2, 0));
    CHECK(HiringRanking::roleBetter(1, 1, 1000, 2, 0));
    CHECK(HiringRanking::roleBetter(2, 2, 1000, 1, 0));
    CHECK(HiringRanking::roleBetter(2, 2, -10, 2, 0));
    CHECK_FALSE(HiringRanking::roleBetter(2, 2, 0, 2, 0));
}
}
