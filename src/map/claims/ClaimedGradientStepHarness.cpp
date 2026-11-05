// SPDX-License-Identifier: GPL-3.0-or-later
#include "EngineFixtures.h"
#include "GradientFixture.h"
#include "field/GradientDestinationPlanes.h"

namespace {
struct Pursuit {
    glob2test::HeadlessGame world{ {.discovered=true,.clearImmobile=true,.loadDefaultRace=true,.header=true,.seed=7183} };
    Map& map=world.game.map;
    Unit* unit=nullptr;
    explicit Pursuit(bool air=false) {
        world.game.gameHeader.setResourceGrowthDisabled(true);
        world.game.gameHeader.getWinningConditions().clear();
        map.setMaterialDestinationsEnabled(true);
        map.setActiveClaimPolicy(ResourceClaims::ActiveClaimPolicy::PrototypeStationaryV1);
        auto* inn=world.addBuilding("inn",2,2);
        unit=world.addUnit(WORKER,8,8);
        if(air) {
            map.setGroundUnit(8,8,NOGUID);unit->setPerformance(FLY,32);map.setAirUnit(8,8,unit->gid);
        }
        map.setResourceByIndex(12,8,WHEAT,1);map.setMaterialAmountSlot(map.coordToIndex(12,8),WHEAT,3);
        unit->attachedBuilding=inn;unit->activity=Unit::ACT_FILLING;unit->medical=Unit::MED_FREE;
        unit->displacement=Unit::DIS_GOING_TO_RESOURCE;unit->destinationPurpose=WHEAT;
        REQUIRE(unit->acquireMaterialClaimAt(Uint32(map.coordToIndex(12,8))));
        REQUIRE(glob2test::awaitGradient(world.game,[&]{return map.getMaterialGradientSlot(0,WHEAT,unit->movementAccess());}));
    }
    ~Pursuit(){unit->releaseActiveMaterialClaim();unit->attachedBuilding=nullptr;}
    template<class F> void neighbors(F apply) {
        for(int dy=-1;dy<=1;++dy)for(int dx=-1;dx<=1;++dx)if(dx||dy)apply(8+dx,8+dy);
    }
};
}

TEST_SUITE("ClaimedGradientStep") {
TEST_CASE("live claimed descent preserves RNG and waits for immobile traffic") {
    glob2test::HeadlessGlobals globals;Pursuit f;
    const auto random=f.unit->entityRandom;const auto claim=f.unit->activeMaterialClaim;
    int dx=99,dy=99;
    CHECK(f.map.nextClaimGradientStep(*f.unit,&dx,&dy)==ResourceClaims::StepResult::Ready);
    CHECK(dx==1);CHECK(dy==0);CHECK(f.unit->entityRandom==random);
    f.neighbors([&](int x,int y){f.map.markImmobileUnit(x,y,0);});
    dx=99;dy=99;
    CHECK(f.map.nextClaimGradientStep(*f.unit,&dx,&dy)==ResourceClaims::StepResult::Pending);
    CHECK(dx==99);CHECK(dy==99);CHECK(f.unit->activeMaterialClaim==claim);
    f.neighbors([&](int x,int y){f.map.addForbidden(x,y,0);});
    CHECK(f.map.nextClaimGradientStep(*f.unit,&dx,&dy)==ResourceClaims::StepResult::Failed);
    CHECK(f.unit->activeMaterialClaim==claim);CHECK(f.unit->entityRandom==random);
    CHECK_THROWS_AS(f.map.setActiveClaimPolicy(ResourceClaims::ActiveClaimPolicy::Disabled),std::logic_error);
    CHECK(f.unit->activeMaterialClaim==claim);
    f.unit->releaseActiveMaterialClaim();
    CHECK(f.map.nextClaimGradientStep(*f.unit,&dx,&dy)==ResourceClaims::StepResult::Failed);
    CHECK(f.unit->entityRandom==random);
}
TEST_CASE("flying claimed descent crosses paint but waits for air occupancy") {
    glob2test::HeadlessGlobals globals;Pursuit f(true);
    f.neighbors([&](int x,int y){f.map.addForbidden(x,y,0);f.map.markImmobileUnit(x,y,0);});
    int dx=0,dy=0;
    CHECK(f.map.nextClaimGradientStep(*f.unit,&dx,&dy)==ResourceClaims::StepResult::Ready);
    CHECK(dx==1);CHECK(dy==0);
    f.neighbors([&](int x,int y){f.map.setAirUnit(x,y,Uint16(f.unit->gid+1));});
    CHECK(f.map.nextClaimGradientStep(*f.unit,&dx,&dy)==ResourceClaims::StepResult::Pending);
    CHECK(f.unit->activeMaterialClaim.has_value());
}
TEST_CASE("new published sources cannot redirect a held access through the fast path") {
    glob2test::HeadlessGlobals globals;Pursuit f;
    const auto original=f.unit->activeMaterialClaim;
    const auto old=f.map.materialGradientDestinations(f.map.getMaterialGradientSlot(0,WHEAT,MovementAccess::Land));
    REQUIRE(old);CHECK(old->x[f.map.coordToIndex(8,8)]==12);
    f.map.setResourceByIndex(10,8,WHEAT,1);
    f.map.updateMaterialGradient(0,WHEAT,MovementAccess::Land,false);
    REQUIRE(glob2test::awaitGradient(f.world.game,[&]()->const Uint16*{
        const auto* field=f.map.getMaterialGradientSlot(0,WHEAT,MovementAccess::Land);
        const auto labels=f.map.materialGradientDestinations(field);
        return labels && labels->x[f.map.coordToIndex(8,8)]==10 ? field : nullptr;
    }));
    int dx=0,dy=0;
    CHECK(f.map.nextClaimGradientStep(*f.unit,&dx,&dy)==ResourceClaims::StepResult::Failed);
    CHECK(f.unit->activeMaterialClaim==original);
    CHECK(old->x[f.map.coordToIndex(8,8)]==12);
}
}
