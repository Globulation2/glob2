// SPDX-License-Identifier: GPL-3.0-or-later
#include "EngineFixtures.h"
#include "MenuColony.h"

TEST_SUITE("MenuColony")
{
    TEST_CASE("shipped historical snapshot loads and advances without graphics [save-format]")
    {
        glob2test::HeadlessGlobals globals;
        MenuColony first, second;
        REQUIRE(first.load());
        REQUIRE(second.load());
        REQUIRE(first.ready());
        const auto initialTick = first.tick();
        const auto initialChecksum = first.checksum();
        // The checked-in colony is a warmed historical save, not a fixture
        // synthesized by the same serializer that is being tested.
        REQUIRE(initialTick > 0);
        for (Uint64 time : {1000, 1040, 1080, 1120})
        {
            first.update(time);
            second.update(time);
            CHECK(first.tick() == second.tick());
            CHECK(first.checksum() == second.checksum());
        }
        CHECK(first.tick() == initialTick + 3);
        CHECK(first.checksum() != initialChecksum);
    }
}
