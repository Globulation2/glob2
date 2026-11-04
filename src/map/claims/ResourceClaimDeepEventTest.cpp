// SPDX-License-Identifier: GPL-3.0-or-later
#include "ResourceClaimDeepEvent.h"
#include "ResourceClaimStratifiedDeep.h"
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

TEST_CASE("Stratified claim capture has disjoint stable phase and material bins" * doctest::test_suite("ResourceClaimDeepEvent"))
{
    using namespace ResourceClaims::Experiment;
    CHECK(deepStratum(59999,MaterialId::Food,false,true,true)==0);
    CHECK(deepStratum(59999,MaterialId::Prunes,true,false,false)==3);
    CHECK(deepStratum(59999,MaterialId::Stone,true,false,false)==5);
    CHECK(deepStratum(60000,MaterialId::Cherries,false,true,true)==6);
    CHECK(deepStratum(60000,MaterialId::Wood,true,false,false)==9);
    CHECK(deepStratum(60000,MaterialId::Food,false,true,true)==10);
    CHECK(deepStratum(60000,MaterialId::Food,true,false,true)==13);
    CHECK(deepStratum(60000,MaterialId::Food,true,false,false)==15);
    CHECK_THROWS_AS(deepStratum(0,MaterialId(255),false,false,false),std::invalid_argument);
}
TEST_CASE("Stratified claim minima count rejected population independently" * doctest::test_suite("ResourceClaimDeepEvent"))
{
    using namespace ResourceClaims::Experiment;
    std::array<StratifiedDeepSlot,DeepStratumCount> slots;
    CHECK(slots[10].consider(100,1,0,true));slots[10].reservoir.written(1);
    CHECK(slots[12].consider(200,2,1,false));slots[12].excludedInputBytes++;
    CHECK_FALSE(slots[10].censored());CHECK(slots[12].censored());
    CHECK_FALSE(slots[10].consider(100,3,2,true));
    CHECK(slots[10].minimumPriorityTies==1);
    CHECK(slots[10].reservoir.eligible==2);
    CHECK(slots[10].reasons[0]+slots[10].reasons[1]+slots[10].reasons[2]==2);
    CHECK(slots[12].unknownPool==1);
    CHECK(slots[12].consider(1,4,0,true));slots[12].reservoir.written(4);
    CHECK(slots[12].censored()); // Later replacement cannot erase missing coverage.
}
