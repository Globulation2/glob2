// SPDX-License-Identifier: GPL-3.0-or-later
#include "EngineFixtures.h"
#include "ai/cortex/AICortex.h"
#include "ai/cortex/CortexObservation.h"
#include "ai/cortex/CortexBuildings.h"
#include "ai/model/BuildingProjection.h"
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
    TEST_CASE("bounded model channels preserve stock identity and ignore custom family metadata")
    {
        glob2test::HeadlessGlobals globals;
        glob2test::HeadlessGame world;
        for(size_t i=0;i<world.game.buildingsTypes.size();++i) {
            const auto& type=*world.game.buildingsTypes.get(i);
            if(!type.runtimeAvailable)continue;
            CAPTURE(type.key);
            CHECK(ModelBuildingProjection::channel(world.game.buildingsTypes,type)==type.shortTypeNum);
        }
        auto* passive=world.game.buildingsTypes.get(world.game.buildingsTypes.getFinishedTypeNum("stonewall"));
        passive->shortTypeNum=0;
        CHECK(ModelBuildingProjection::channel(world.game.buildingsTypes,*passive)==ModelBuildingProjection::PassiveGround);
        passive->semantics.production.enabledUnitMask=1u<<WORKER;
        CHECK(ModelBuildingProjection::channel(world.game.buildingsTypes,*passive)==ModelBuildingProjection::Production);
    }

    TEST_CASE("stock consumers are not exchange providers and hybrid attractors survive retirement")
    {
        glob2test::HeadlessGlobals globals;
        glob2test::HeadlessGame world(glob2test::GameOptions{.discovered=true,.clearImmobile=true,.loadDefaultRace=true,.header=true});
        auto* hybrid=world.addBuilding("inn",4,4);
        hybrid->type->runtimeFetchesStock=true;
        hybrid->type->zonable[WARRIOR]=1;
        CHECK_FALSE(Cortex::servesRole(world.game,*hybrid->type,Cortex::CORTEX_BUILD_EXCHANGE));
        AICortex ai(world.game.players[0]);
        Uint16 gid=hybrid->gid;
        REQUIRE(ai.findFlagByGid(gid)==hybrid);
        ai.clearOneFlag(gid);
        CHECK(gid==NOGBID);
        CHECK(ai.orderQueue.empty());
        CHECK(hybrid->buildingState==Building::ALIVE);
        // A pure ground attractor releases its assignment without demolition.
        hybrid->type->semantics.feeding.enabled=false;
        hybrid->maxUnitWorking=3;
        gid=hybrid->gid;ai.clearOneFlag(gid);
        REQUIRE(ai.orderQueue.size()==1);
        CHECK(std::dynamic_pointer_cast<OrderModifyBuilding>(ai.orderQueue.front())!=nullptr);
        drain(ai,world.game);
        CHECK(hybrid->maxUnitWorking==0);
        CHECK(hybrid->buildingState==Building::ALIVE);
    }

    TEST_CASE("mixed renamed provider has multiple policy roles but one model and worker identity")
    {
        glob2test::HeadlessGlobals globals;
        glob2test::HeadlessGame world(glob2test::GameOptions{.discovered=true,.clearImmobile=true,.loadDefaultRace=true,.header=true});
        auto* building = world.addBuilding("inn", 4, 4);
        auto& type = *building->type;
        type.type = "unfamiliar-service";
        type.shortTypeNum = IntBuildingType::STONE_WALL;
        type.semantics.production.recipes[EXPLORER].enabled = true;
        type.semantics.production.recipes[EXPLORER].duration = 20;
        type.semantics.production.enabledUnitMask = 1u << EXPLORER;
        type.semantics.feeding.cost.fill(0);
        auto obs = Cortex::makeEmptyObservation(); obs.valid = 1;
        bool found = false; Sint32 x=0,y=0,r=0;
        Cortex::observeBuildings(obs, world.team, &world.game, 0, NOGBID, found, x,y,r);
        CHECK(Cortex::cortexFinishedBuildings(obs, Cortex::CORTEX_BUILD_SWARM) == 1);
        CHECK(Cortex::cortexFinishedBuildings(obs, Cortex::CORTEX_BUILD_FOOD) == 1);
        CHECK(obs.swarmCount == 1); CHECK(obs.innCount == 0);
        int modelCount=0;
        for (const auto& role : obs.modelBuildingCountPerLevel) for (int count : role) modelCount += count;
        CHECK(modelCount == 1);
        CHECK(obs.feedCapacity > 0);
        AICortex ai(world.game.players[0]);
        ai.translateAction(Cortex::makeSetProductionAction(2,3,4), obs);
        REQUIRE(ai.orderQueue.size() == 1);
        const auto order = std::dynamic_pointer_cast<OrderModifySwarm>(ai.orderQueue.front());
        REQUIRE(order);
        // Recipe masking survives the real executor; no unavailable unit is requested.
        drain(ai, world.game);
        CHECK(building->ratio[WORKER] == 0);
        CHECK(building->ratio[EXPLORER] == 3);
        CHECK(building->ratio[WARRIOR] == 0);
    }

    TEST_CASE("live capability upgrade eligibility and descriptor capacities drive construction")
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
        globals->settings.setBuildingAssignment(world.game.buildingsTypes.fingerprint(),
            *world.game.buildingsTypes.get(world.game.buildingsTypes.getTypeNum("barracks",1,true)),7);
        globals->settings.setBuildingAssignment(world.game.buildingsTypes.fingerprint(),
            *world.game.buildingsTypes.get(world.game.buildingsTypes.getTypeNum("barracks",1,false)),9);
        ai.translateAction(Cortex::makeUpgradeAction(Cortex::CORTEX_BUILD_ATTACK),obs);
        REQUIRE(ai.orderQueue.size()==1);
        auto order=std::dynamic_pointer_cast<OrderConstruction>(ai.orderQueue.front());
        REQUIRE(order!=nullptr);
        CHECK(order->gid==first->gid); CHECK(order->unitWorking==4); CHECK(order->unitWorkingFuture==0);
        ai.translateAction(Cortex::makeUpgradeAction(Cortex::CORTEX_BUILD_ATTACK),obs);
        CHECK(ai.orderQueue.size()==1);
        drain(ai,world.game);
        CHECK(first->constructionResultState==Building::UPGRADE);
        CHECK(first->maxUnitWorking==4);
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
