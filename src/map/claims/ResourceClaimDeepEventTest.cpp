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

#include "ResourceClaimTopKDeep.h"
TEST_SUITE("ResourceClaimTopKDeep")
{
TEST_CASE("Bottom four preserves exact cardinality and excludes evicted pending publications")
{
    ResourceClaims::Experiment::TopKDeepSlot state;
    for(Uint64 i=1;i<=4;++i) {const auto slot=state.consider(100+i,i,0,true);REQUIRE(slot>=0);CHECK(state.written(slot,i,"digest"));}
    CHECK_FALSE(state.censored());CHECK(state.retained()==4);
    const auto replacement=state.consider(1,5,1,false);REQUIRE(replacement>=0);
    CHECK(state.censored());CHECK_FALSE(state.written(replacement,4,"stale"));
    CHECK(state.written(replacement,5,"new"));CHECK_FALSE(state.censored());
    CHECK(state.consider(999,6,2,true)==-1);CHECK(state.eligible==6);
    CHECK((state.reasons==std::array<Uint64,3>{4,1,1}));CHECK(state.unknownPool==1);
    CHECK_THROWS_AS(state.consider(1,6,0,true),std::invalid_argument);
    ResourceClaims::Experiment::TopKDeepSlot ties;
    for(Uint64 i=1;i<=4;++i)CHECK(ties.consider(10,i,0,true)==int(i-1));
    CHECK(ties.consider(10,5,0,true)==-1);
    CHECK(ties.currentPriorityTies==10);
    for(unsigned i=0;i<4;++i)CHECK(ties.entries[i].ordinal==i+1);
}
TEST_CASE("All rank permutations give every event four fifths inclusion")
{
    std::array<unsigned,5> ranks={0,1,2,3,4};std::array<unsigned,5> included{};unsigned permutations=0;
    do {
        ResourceClaims::Experiment::TopKDeepSlot state;
        for(unsigned i=0;i<5;++i)state.consider(ranks[i],i+1,0,true);
        for(const auto& entry:state.entries) {REQUIRE(entry.ordinal>0);++included[entry.ordinal-1];}
        ++permutations;
    } while(std::next_permutation(ranks.begin(),ranks.end()));
    CHECK(permutations==120);for(const auto count:included)CHECK(count==96);
}
TEST_CASE("Admission loss is never salvaged by later replacement or fewer written entries")
{
    for(unsigned loss=0;loss<3;++loss) {
        ResourceClaims::Experiment::TopKDeepSlot state;
        const auto first=state.consider(10,1,0,true);REQUIRE(first==0);state.written(first,1,"digest");
        CHECK(state.retained()==1);CHECK_FALSE(state.censored());
        for(Uint64 i=2;i<=7;++i) {auto slot=state.consider(10-i,i,0,true);if(slot>=0)state.written(slot,i,"digest");}
        if(loss==2)++state.excludedInputBytes;else state.excluded(loss==1);
        CHECK(state.retained()==4);CHECK(state.censored());
    }
    ResourceClaims::Experiment::TopKDeepSlot empty;CHECK(empty.retained()==0);CHECK_FALSE(empty.censored());
}
}
