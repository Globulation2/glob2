// SPDX-License-Identifier: GPL-3.0-or-later
#include "EngineFixtures.h"
#include "ai/cortex/AICortex.h"
#include "ai/cortex/CortexObservation.h"
#include "Order.h"
#include "Player.h"

namespace
{
void drain(AICortex& ai,Game& game)
{
    while (!ai.orderQueue.empty())
    {
        auto order=ai.orderQueue.front(); ai.orderQueue.pop();
        order->sender=0; game.executeOrder(order,0);
    }
}
}
TEST_SUITE("CortexActionCoverage")
{
    TEST_CASE("live upgrade eligibility and worker columns drive the actual construction order")
    {
        glob2test::HeadlessGlobals globals;
        glob2test::HeadlessGame world(glob2test::GameOptions{.clearImmobile=true,.loadDefaultRace=true,.header=true});
        auto* worker=world.addUnit(WORKER,20,20,0,2);
        REQUIRE(worker->workerLevel()==2);
        auto* first=world.addBuilding("barracks",4,4);
        auto* second=world.addBuilding("barracks",12,4);
        AICortex ai(world.game.players[0]);
        REQUIRE(ai.findUpgradeTarget(IntBuildingType::ATTACK_BUILDING)==first);
        first->hp-=1;
        CHECK(ai.findUpgradeTarget(IntBuildingType::ATTACK_BUILDING)==second);
        second->hp-=1;
        CHECK(ai.findUpgradeTarget(IntBuildingType::ATTACK_BUILDING)==nullptr);
        first->hp=first->getEffectiveMaxHp(); second->hp=second->getEffectiveMaxHp();
        auto obs=Cortex::makeEmptyObservation(); obs.valid=1;
        bool found=false; Sint32 x=0,y=0,r=0;
        Cortex::observeBuildings(obs,world.team,&world.game,2,NOGBID,found,x,y,r);
        CHECK(obs.upgradableCount[Cortex::CORTEX_BUILD_ATTACK]==2);
        globals->settings.defaultUnitsAssigned[IntBuildingType::ATTACK_BUILDING][2]=7;
        globals->settings.defaultUnitsAssigned[IntBuildingType::ATTACK_BUILDING][3]=9;
        ai.translateAction(Cortex::makeUpgradeAction(Cortex::CORTEX_BUILD_ATTACK),obs);
        REQUIRE(ai.orderQueue.size()==1);
        auto order=std::dynamic_pointer_cast<OrderConstruction>(ai.orderQueue.front());
        REQUIRE(order!=nullptr);
        CHECK(order->gid==first->gid); CHECK(order->unitWorking==7); CHECK(order->unitWorkingFuture==9);
        ai.translateAction(Cortex::makeUpgradeAction(Cortex::CORTEX_BUILD_ATTACK),obs);
        CHECK(ai.orderQueue.size()==1);
        drain(ai,world.game);
        CHECK(first->constructionResultState==Building::UPGRADE);
        CHECK(first->maxUnitWorking==7);
    }

    TEST_CASE("market levels do not introduce an AI upgrade strategy")
    {
        glob2test::HeadlessGlobals globals;
        for (bool enabled : {false,true})
        {
            glob2test::GameOptions options{.clearImmobile=true,.loadDefaultRace=true,.header=true};
            options.experiments.set(ExperimentId::MarketsV2,enabled);
            glob2test::HeadlessGame world(options);
            world.addUnit(WORKER,20,20,0,2);
            auto* market=world.addBuilding("market",4,4);
            REQUIRE(market);
            AICortex ai(world.game.players[0]);
            CHECK(ai.findUpgradeTarget(IntBuildingType::MARKET_BUILDING)==nullptr);
            auto obs=Cortex::makeEmptyObservation(); obs.valid=1;
            bool found=false; Sint32 x=0,y=0,r=0;
            Cortex::observeBuildings(obs,world.team,&world.game,2,NOGBID,found,x,y,r);
            CHECK(obs.upgradableCount[IntBuildingType::MARKET_BUILDING]==0);
            ai.translateAction(Cortex::makeUpgradeAction(IntBuildingType::MARKET_BUILDING),obs);
            CHECK(ai.orderQueue.empty());
        }
    }

    TEST_CASE("build actions reject invalid slots and cooldown suppresses duplicate orders")
    {
        glob2test::HeadlessGlobals globals;
        glob2test::HeadlessGame world(glob2test::GameOptions{.discovered=true,.clearImmobile=true,.loadDefaultRace=true,.header=true});
        AICortex ai(world.game.players[0]);
        auto obs=Cortex::makeEmptyObservation(); obs.valid=1;
        for (int type : {-1,Cortex::CORTEX_BUILDING_TYPES})
            ai.translateAction(Cortex::makeBuildAction(type,0),obs);
        for (int slot : {-1,Cortex::CORTEX_BUILD_CANDIDATES})
            ai.translateAction(Cortex::makeBuildAction(Cortex::CORTEX_BUILD_FOOD,slot),obs);
        ai.translateAction(Cortex::makeBuildAction(Cortex::CORTEX_BUILD_FOOD,0),obs);
        CHECK(ai.orderQueue.empty());
        auto& candidate=obs.buildCandidates[Cortex::CORTEX_BUILD_FOOD][0];
        candidate.valid=1; candidate.x=4; candidate.y=4;
        const auto action=Cortex::makeBuildAction(Cortex::CORTEX_BUILD_FOOD,0);
        ai.translateAction(action,obs); ai.translateAction(action,obs);
        REQUIRE(ai.orderQueue.size()==1);
        auto order=std::dynamic_pointer_cast<OrderCreate>(ai.orderQueue.front());
        REQUIRE(order!=nullptr); CHECK(order->posX==4); CHECK(order->posY==4);
        drain(ai,world.game);
        const auto gid=world.game.map.getBuilding(4,4);
        REQUIRE(gid!=NOGBID);
        auto* inn=world.team->myBuildings[Building::GIDtoID(gid)];
        CHECK(inn->type->isBuildingSite);
        CHECK(inn->type->shortTypeNum==IntBuildingType::FOOD_BUILDING);
    }

    TEST_CASE("war flags create once reconcile force limits and retarget through real orders")
    {
        glob2test::HeadlessGlobals globals;
        glob2test::HeadlessGame world(glob2test::GameOptions{.discovered=true,.clearImmobile=true,.loadDefaultRace=true,.header=true});
        AICortex ai(world.game.players[0]);
        auto obs=Cortex::makeEmptyObservation(); obs.valid=1; obs.tick=100;
        auto& wave=ai.offenseWaves[0];
        ai.ensureFlagAt(wave.gid,wave.createCooldown,4,4,-2,-3,-4,1,obs);
        REQUIRE(ai.orderQueue.size()==1);
        auto create=std::dynamic_pointer_cast<OrderCreate>(ai.orderQueue.front());
        REQUIRE(create); CHECK(create->unitWorking==0); CHECK(create->flagRadius==1);
        ai.ensureFlagAt(wave.gid,wave.createCooldown,4,4,1,0,0,1,obs);
        CHECK(ai.orderQueue.size()==1);
        drain(ai,world.game);
        ai.ensureFlagAt(wave.gid,wave.createCooldown,4,4,1,0,0,1,obs);
        REQUIRE(wave.gid!=NOGBID);
        auto* flag=ai.findFlagByGid(wave.gid); REQUIRE(flag);
        CHECK(flag->priority==1); CHECK(flag->unitStayRange==1);
        drain(ai,world.game);
        ai.ensureFlagAt(wave.gid,wave.createCooldown,4,4,1,0,0,1,obs);
        CHECK(ai.orderQueue.empty());
        ai.ensureFlagAt(wave.gid,wave.createCooldown,12,12,999,999,999,0,obs);
        CHECK(ai.orderQueue.size()==4);
        drain(ai,world.game);
        CHECK(flag->posX==12); CHECK(flag->posY==12);
        CHECK(flag->maxUnitWorking==Cortex::CORTEX_MAX_FLAG_UNITS);
        CHECK(flag->minLevelToFlag==NB_UNIT_LEVELS-1); CHECK(flag->priority==0);
        ai.ensureFlagAt(wave.gid,wave.createCooldown,12,12,999,999,999,0,obs);
        CHECK(ai.orderQueue.empty());
    }

    TEST_CASE("flag rediscovery excludes claimed flags and stale defense teardown is idempotent")
    {
        glob2test::HeadlessGlobals globals;
        glob2test::HeadlessGame world(glob2test::GameOptions{.clearImmobile=true,.loadDefaultRace=true,.header=true});
        AICortex ai(world.game.players[0]);
        auto* first=world.addBuilding("warflag",4,4);
        auto* second=world.addBuilding("warflag",12,12);
        world.addBuilding("explorationflag",3,3);
        ai.offenseWaves[0].gid=first->gid;
        Uint16 unknown=NOGBID;
        REQUIRE(ai.rediscoverFlag(unknown,4,4)==second);
        CHECK(unknown==second->gid);
        ai.defenseFlags[0].gid=second->gid;
        CHECK(ai.rediscoverFlag(unknown,4,4)==nullptr);
        CHECK(ai.isOwnedGid(first->gid)); CHECK_FALSE(ai.isOwnedGid(NOGBID));
        auto obs=Cortex::makeEmptyObservation(); obs.buildingsUnderAttack=1;
        ai.reconcileStaleDefenseFlag(obs); CHECK(ai.orderQueue.empty());
        obs.buildingsUnderAttack=0;
        ai.reconcileStaleDefenseFlag(obs); CHECK(ai.orderQueue.size()==1);
        CHECK(ai.defenseFlags[0].gid==NOGBID);
        ai.reconcileStaleDefenseFlag(obs); CHECK(ai.orderQueue.size()==1);
        drain(ai,world.game);
        ai.clearAllOffenseFlags(); CHECK(ai.orderQueue.size()==1);
        CHECK(ai.offenseWaves[0].gid==NOGBID);
        CHECK(ai.offenseWaves[0].landingX==-1); CHECK(ai.offenseWaves[0].phase==AICortex::WAVE_NONE);
        drain(ai,world.game);
        ai.clearAllOffenseFlags(); CHECK(ai.orderQueue.empty());
        CHECK(ai.findFlagByGid(second->gid)==nullptr);
    }

    TEST_CASE("rally selection and bound warrior arrivals respect colony preference and toroidal range")
    {
        glob2test::HeadlessGlobals globals;
        glob2test::HeadlessGame world(glob2test::GameOptions{.clearImmobile=true,.loadDefaultRace=true,.header=true});
        AICortex ai(world.game.players[0]);
        int x=-1,y=-1;
        CHECK_FALSE(ai.computeRallyPoint(x,y));
        world.addBuilding("inn",4,4);
        REQUIRE(ai.computeRallyPoint(x,y)); CHECK(x==4); CHECK(y==4);
        auto* swarm=world.addBuilding("swarm",12,4);
        REQUIRE(ai.computeRallyPoint(x,y)); CHECK(x==12); CHECK(y==4);
        swarm->buildingState=Building::DEAD;
        REQUIRE(ai.computeRallyPoint(x,y)); CHECK(x==4); CHECK(y==4);
        swarm->buildingState=Building::ALIVE;
        auto* flag=world.addBuilding("warflag",0,0); flag->unitStayRange=1;
        auto* inside=world.addUnit(WARRIOR,31,31);
        auto* outside=world.addUnit(WARRIOR,10,10);
        flag->unitsWorking.push_back(inside); flag->unitsWorking.push_back(outside);
        flag->unitsWorking.push_back(nullptr);
        CHECK(ai.countArrivedAtFlag(flag)==1);
        CHECK(ai.countArrivedAtFlag(nullptr)==0);
        flag->unitsWorking.clear();
    }

    TEST_CASE("production clamps ratios applies orders and deduplicates finished swarms")
    {
        glob2test::HeadlessGlobals globals;
        glob2test::HeadlessGame world(glob2test::GameOptions{.discovered=true,.clearImmobile=true,.loadDefaultRace=true,.header=true});
        auto* swarm=world.addBuilding("swarm",4,4);
        world.addBuilding("inn",12,4);
        AICortex ai(world.game.players[0]);
        auto obs=Cortex::makeEmptyObservation(); obs.valid=1;
        const auto action=Cortex::makeSetProductionAction(-2,3,99);
        ai.translateAction(action,obs);
        REQUIRE(ai.orderQueue.size()==1);
        drain(ai,world.game);
        CHECK(swarm->ratio[WORKER]==0); CHECK(swarm->ratio[EXPLORER]==3);
        CHECK(swarm->ratio[WARRIOR]==Cortex::CORTEX_MAX_RATIO);
        ai.translateAction(action,obs); CHECK(ai.orderQueue.empty());
        swarm->buildingState=Building::WAITING_FOR_DESTRUCTION;
        ai.translateAction(Cortex::makeSetProductionAction(1,0,0),obs);
        CHECK(ai.orderQueue.empty());
        swarm->buildingState=Building::ALIVE;
    }
}
