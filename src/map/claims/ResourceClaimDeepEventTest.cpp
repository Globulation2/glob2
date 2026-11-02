// SPDX-License-Identifier: GPL-3.0-or-later
#include "ResourceClaimDeepEvent.h"
#include "Glob2Test.h"
using ResourceClaims::Experiment::DeepReservoir;
TEST_SUITE("ResourceClaimDeepEvent")
{
TEST_CASE("Claim deep reservoir replaces three eligible minima deterministically")
{
    DeepReservoir state;
    CHECK_FALSE(state.censored());
    for(Uint64 i=1;i<=3;++i) {
        CHECK(state.consider(100-i,i));
        CHECK(state.censored());
        state.written(i);
        CHECK_FALSE(state.censored());
    }
    CHECK(state.eligible==3);CHECK(state.replacements==3);
    CHECK(state.ordinal==3);CHECK(state.priority==97);
    CHECK_FALSE(state.consider(97,4)); // Equal rank keeps lower ordinal.
    CHECK_FALSE(state.consider(999,5));
    CHECK(state.eligible==5);CHECK_FALSE(state.censored());
}
TEST_CASE("Claim deep reservoir distinguishes missing writes and admission censoring")
{
    for(const bool queue:{false,true}) {
        DeepReservoir state;
        REQUIRE(state.consider(100,1));state.written(1);
        REQUIRE(state.consider(20,2));state.excluded(queue);
        CHECK(state.censored());CHECK(state.ordinal==2);CHECK(state.writtenOrdinal==1);
        CHECK(state.excludedQueueCap==Uint64(queue));CHECK(state.excludedSampleCap==Uint64(!queue));
        REQUIRE(state.consider(10,3));state.written(3);
        CHECK(state.censored()); // A later successful minimum does not hide truncation.
    }
    DeepReservoir missing;
    REQUIRE(missing.consider(0,1));
    CHECK(missing.censored()); // Failed/missing write cannot be called complete.
    CHECK_THROWS_AS(missing.consider(10,0),std::invalid_argument);
}
TEST_CASE("Claim deep reservoir rank is independent of pending publication")
{
    DeepReservoir state;
    REQUIRE(state.consider(100,1));
    REQUIRE(state.consider(10,2));
    state.written(1);CHECK(state.censored());
    state.written(2);CHECK_FALSE(state.censored());
    CHECK(state.replacements==2);
}
}
