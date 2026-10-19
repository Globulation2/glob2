// SPDX-License-Identifier: GPL-3.0-or-later
#include "Glob2Test.h"
#include "ResourceGradientExploration.h"
#include <algorithm>
#include <array>
#include <vector>

TEST_SUITE("ResourceGradientExploration") {
TEST_CASE("interval strata are validated and assigned without consuming game randomness") {
    CHECK((ResourceGradientExploration::parseIntervals("0,32,128,512")==std::vector<unsigned>{0,32,128,512}));
    for (const auto* invalid:{"","8,","-1","1","7","8","16","31","65537","32,32","32, 64","32x"})
        CHECK_THROWS_AS(ResourceGradientExploration::parseIntervals(invalid),std::invalid_argument);
    ResourceGradientExploration first,second;
    first.intervals=second.intervals={0,32,128,512};first.seed=second.seed=123456;
    unsigned changed=0;std::array<unsigned,4> counts{};
    for (int key=0;key<1024;++key) {
        const auto interval=first.intervalFor(key);
        CHECK(interval==second.intervalFor(key));
        ++counts[std::find(first.intervals.begin(),first.intervals.end(),interval)-first.intervals.begin()];
        second.seed=654321;changed+=interval!=second.intervalFor(key);second.seed=first.seed;
    }
    for (auto count:counts) CHECK(count>150);
    CHECK(changed>500);
}
}
