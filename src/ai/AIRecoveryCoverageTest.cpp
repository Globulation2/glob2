// SPDX-License-Identifier: GPL-3.0-or-later
#include "EngineFixtures.h"
#include "AICastor.h"
#include "AINicowar.h"
#include "AICabino.h"
#include "ai/cortex/CortexWater.h"
#include "ai/cortex/CortexWheat.h"
#include "Order.h"
#include "Brush.h"
#include "Player.h"
#include "ExperimentalFeatures.h"
#include <set>

TEST_SUITE("AIRecoveryCoverage")
{
    TEST_CASE("Cortex shore harvest differs from inaccessible algae and swimming opens islands")
    {
        glob2test::HeadlessGlobals globals;
        glob2test::HeadlessGame w(glob2test::GameOptions{
            .terrain=WATER,.discovered=true,.clearImmobile=true,.loadDefaultRace=true,.header=true});
        for (int y=2;y<12;++y) for (int x=2;x<12;++x) w.game.map.setTerrain(x,y,GRASS);
        for (int y=18;y<28;++y) for (int x=18;x<28;++x) w.game.map.setTerrain(x,y,GRASS);
        w.addBuilding("swarm",4,4);
        auto* player=w.game.players[0];
        auto empty=Cortex::assessSwim(player,true);
        CHECK(empty.landReach>0); CHECK(empty.waterReach>empty.landReach);
        CHECK(empty.algaeDiscovered==0); CHECK(empty.algaeReachable==0);
        w.game.map.setResource(20,16,ALGA,1);
        w.game.map.setMapDiscovered();
        auto distant=Cortex::assessSwim(player,true);
        CHECK(distant.algaeDiscovered==1); CHECK(distant.algaeReachable==0);
        w.game.map.setResource(12,6,ALGA,1);
        auto shore=Cortex::assessSwim(player,false);
        CHECK(shore.algaeDiscovered==1); CHECK(shore.algaeReachable==1);
        CHECK(shore.landReach==empty.landReach); CHECK(shore.waterReach==0);
        CHECK(Cortex::assessSwim(nullptr,true).waterReach==0);
    }

    TEST_CASE("Cortex amphibious assessment distinguishes land water and blocked targets")
    {
        glob2test::HeadlessGlobals globals;
        glob2test::HeadlessGame w(glob2test::GameOptions{
            .terrain=WATER,.teams=2,.discovered=true,.clearImmobile=true,.loadDefaultRace=true,.header=true});
        for (int y=2;y<12;++y) for (int x=2;x<12;++x) w.game.map.setTerrain(x,y,GRASS);
        for (int y=18;y<28;++y) for (int x=18;x<28;++x) w.game.map.setTerrain(x,y,GRASS);
        w.addBuilding("swarm",4,4); w.addBuilding("swarm",20,20,0,1);
        auto water=Cortex::assessAmphibious(w.game.players[0],20,20,nullptr,nullptr,0,2,8);
        CHECK(water.amphibious==1); CHECK(water.landDist==-1); CHECK(water.swimDist>=0);
        CHECK(water.landingValid==1);
        CHECK(!w.game.map.isWater(water.landingX,water.landingY));
        for (int y=0;y<32;++y) for (int x=0;x<32;++x) w.game.map.setTerrain(x,y,GRASS);
        auto land=Cortex::assessAmphibious(w.game.players[0],20,20,nullptr,nullptr,0,2,8);
        CHECK(land.amphibious==0); CHECK(land.landDist>=0);
    }

    TEST_CASE("Cortex wheat protection emits real area orders and food burst releases only our paint")
    {
        glob2test::HeadlessGlobals globals;
        glob2test::HeadlessGame w(glob2test::GameOptions{
            .teams=2,.discovered=true,.clearImmobile=true,.loadDefaultRace=true,.header=true});
        w.addBuilding("inn",2,2);
        for (int y=7;y<13;++y) for (int x=7;x<13;++x) w.game.map.setResource(x,y,WHEAT,1);
        w.game.map.setMapDiscovered();
        std::fill(w.game.map.fogOfWar,w.game.map.fogOfWar+32*32,~Uint32(0));
        w.game.map.addForbidden(8,8,1);
        auto protection=Cortex::reconcileWheatForbidden(w.game.players[0],0,true);
        REQUIRE(protection.addCount>0);
        auto apply=[&](Uint8 mode,BrushAccumulator& brush) {
            auto order=std::make_shared<OrderAlterForbidden>(0,mode,&brush,&w.game.map);
            order->sender=0; w.game.executeOrder(order,0);
        };
        apply(BrushTool::MODE_ADD,protection.add);
        auto settled=Cortex::reconcileWheatForbidden(w.game.players[0],0,true);
        CHECK(settled.addCount==0); CHECK(settled.delCount==0);
        auto burst=Cortex::reconcileWheatForbidden(w.game.players[0],0,true,true);
        CHECK(burst.addCount==0); REQUIRE(burst.delCount>0);
        apply(BrushTool::MODE_DEL,burst.del);
        for (int y=7;y<13;++y) for (int x=7;x<13;++x) CHECK(!w.game.map.isForbidden(x,y,w.team->me));
        CHECK(w.game.map.isForbidden(8,8,w.game.teams[1]->me));
    }

    TEST_CASE("Cortex farm checkerboard retires prototype paint and wheat blitz releases it")
    {
        glob2test::HeadlessGlobals globals;
        glob2test::HeadlessGame w(glob2test::GameOptions{
            .teams=2,.discovered=true,.clearImmobile=true,.loadDefaultRace=true,.header=true});
        w.game.gameHeader.getExperiments().set(ExperimentId::FarmAreas);
        w.addBuilding("inn",4,4);
        for(int y=14; y<18; ++y) for(int x=7; x<13; ++x) w.game.map.setTerrain(x,y,256);
        for(int y=7; y<13; ++y) for(int x=7; x<13; ++x) w.game.map.setResource(x,y,WHEAT,1);
        w.game.map.setMapDiscovered();
        std::fill(w.game.map.fogOfWar,w.game.map.fogOfWar+32*32,~Uint32(0));
        // A save from the prototype contains both wheat parities and its ring.
        for(int y=6; y<14; ++y) for(int x=6; x<14; ++x) w.game.map.addFarmArea(x,y,0);
        w.game.map.addFarmArea(8,8,1);
        auto apply=[&](Uint8 mode,BrushAccumulator& brush) {
            auto order=std::make_shared<OrderAlterFarmArea>(0,mode,&brush,&w.game.map);
            order->sender=0; w.game.executeOrder(order,0);
        };
        auto farm=Cortex::reconcileWheatForbidden(w.game.players[0],0,true,false,true);
        REQUIRE(farm.delCount>0);
        apply(BrushTool::MODE_DEL,farm.del);
        auto expected=Cortex::scanWheatForbidden(w.game.map,w.team->me,0,{4*32+4},0,0,31,31,0,true,false);
        std::set<int> pattern(expected.desired.begin(),expected.desired.end());
        for(int y=6; y<14; ++y) for(int x=6; x<14; ++x)
            CHECK(w.game.map.isFarmArea(x,y,w.team->me)==pattern.contains(y*32+x));
        auto settled=Cortex::reconcileWheatForbidden(w.game.players[0],0,true,false,true);
        CHECK(settled.addCount==0); CHECK(settled.delCount==0);
        auto burst=Cortex::reconcileWheatForbidden(w.game.players[0],0,true,true,true);
        REQUIRE(burst.delCount>0); CHECK(burst.addCount==0);
        apply(BrushTool::MODE_DEL,burst.del);
        CHECK(w.game.map.isFarmArea(8,8,w.game.teams[1]->me));
        auto restored=Cortex::reconcileWheatForbidden(w.game.players[0],0,true,false,true);
        apply(BrushTool::MODE_ADD,restored.add);
        // Depletion must still retire farm paint when the entire field is gone.
        for(int y=7; y<13; ++y) for(int x=7; x<13; ++x) w.game.map.getResource(x,y).type=NO_RES_TYPE;
        auto depleted=Cortex::reconcileWheatForbidden(w.game.players[0],0,true,false,true);
        CHECK(depleted.addCount==0); CHECK(depleted.delCount==restored.addCount);
    }

    TEST_CASE("Castor projects deduplicate replace and decline already satisfied requests")
    {
        glob2test::HeadlessGlobals globals;
        glob2test::HeadlessGame w(glob2test::GameOptions{.clearImmobile=true,.loadDefaultRace=true,.header=true});
        AICastor ai(w.game.players[0]);
        ai.computeBuildingSum(); ai.timer=100;
        using Project=AICastor::Project;
        REQUIRE(ai.addProject(new Project(IntBuildingType::FOOD_BUILDING,1,2,"test")));
        auto* original=ai.projects.front();
        ai.timer=150;
        CHECK(!ai.addProject(new Project(IntBuildingType::FOOD_BUILDING,1,2,"repeat")));
        REQUIRE(ai.projects.size()==1); CHECK(ai.projects.front()==original); CHECK(original->timer==150);
        REQUIRE(ai.addProject(new Project(IntBuildingType::FOOD_BUILDING,2,3,"larger")));
        REQUIRE(ai.projects.size()==1); CHECK(ai.projects.front()->amount==2);
        w.addBuilding("inn",4,4); ai.computeBuildingSum();
        CHECK(!ai.addProject(new Project(IntBuildingType::FOOD_BUILDING,1,2,"satisfied")));
        CHECK(ai.projects.size()==1);
    }

    TEST_CASE("Castor food lock backs off growth and boot projects wait for their cadence")
    {
        glob2test::HeadlessGlobals globals;
        glob2test::HeadlessGame w(glob2test::GameOptions{.clearImmobile=true,.loadDefaultRace=true,.header=true});
        AICastor ai(w.game.players[0]);
        AICastor::Project p(IntBuildingType::SWARM_BUILDING,"test");
        ai.timer=100; p.timer=100;
        CHECK(!ai.continueProject(&p)); CHECK(p.subPhase==AICastor::AI_CASTOR_SUBPHASE_BOOT);
        ai.timer=1000; ai.foodLock=true; ai.starvingWarning=true;
        p.critical=false; p.blocking=true;
        CHECK(!ai.continueProject(&p)); CHECK(p.timer>ai.timer); CHECK(!p.blocking);
        CHECK(p.subPhase==AICastor::AI_CASTOR_SUBPHASE_CHECK_SITES);
    }
    TEST_CASE("Nicowar defense flags materialize once and withdraw when the threat disappears")
    {
        glob2test::HeadlessGlobals globals;
        glob2test::HeadlessGame w(glob2test::GameOptions{
            .teams=2,.clearImmobile=true,.loadDefaultRace=true,.header=true});
        auto* home=w.addBuilding("swarm",4,4);
        auto* threatened=w.addUnit(WORKER,9,9);
        auto* enemy=w.addUnit(WARRIOR,8,8,1);
        w.team->enemies=w.game.teams[1]->me;
        threatened->underAttackTimer=100;
        AISharedRuntime::Runtime runtime(new NewNicowar,w.game.players[0]);
        runtime.gm.reset(new AISharedRuntime::Gradients::GradientManager(&w.game.map));
        runtime.br.initiate();
        auto& ai=*static_cast<NewNicowar*>(runtime.runtimeai.get());
        auto flush=[&] {
            for(int pass=0;pass<4;++pass) {
                runtime.br.tick(); runtime.update_building_orders(); runtime.update_management_orders();
                for(auto order:runtime.orders) { order->sender=0; w.game.executeOrder(order,0); }
                runtime.orders.clear();
            }
        };
        ai.compute_defense_flag_positioning(runtime); flush();
        REQUIRE(ai.defense_flags.size()==1);
        const auto id=ai.defense_flags.front();
        REQUIRE(runtime.br.is_building_found(id));
        auto* flag=runtime.br.get_building(id);
        CHECK(flag->type->shortTypeNum==IntBuildingType::WAR_FLAG);
        CHECK(flag->maxUnitWorking==1);
        ai.compute_defense_flag_positioning(runtime); flush();
        REQUIRE(ai.defense_flags.size()==1); CHECK(ai.defense_flags.front()==id);
        w.game.map.setGroundUnit(enemy->posX,enemy->posY,NOGUID);
        threatened->underAttackTimer=0;
        ai.compute_defense_flag_positioning(runtime); flush();
        CHECK(flag->buildingState!=Building::ALIVE);
    }

    TEST_CASE("Cabino defense records create assign deduplicate and retire actual flags")
    {
        glob2test::HeadlessGlobals globals;
        glob2test::HeadlessGame w(glob2test::GameOptions{
            .teams=2,.clearImmobile=true,.loadDefaultRace=true,.header=true});
        auto* home=w.addBuilding("swarm",4,4);
        auto* enemy=w.addUnit(WARRIOR,8,8,1);
        w.team->enemies=w.game.teams[1]->me;
        Cabino::AICabino ai(w.game.players[0]);
        auto& defense=*static_cast<Cabino::SimpleBuildingDefense*>(ai.getDefenseModule());
        auto flush=[&] {
            while(!ai.orders.empty()) { auto order=ai.orders.front(); ai.orders.pop();
                order->sender=0; w.game.executeOrder(order,0); }
        };
        defense.perform(0); CHECK(defense.defending_zones.empty());
        home->hp-=10; defense.perform(0); flush();
        REQUIRE(defense.defending_zones.size()==1);
        defense.perform(2); flush();
        const auto gid=defense.defending_zones.front().flag;
        REQUIRE(gid!=NOGBID);
        auto* flag=w.team->myBuildings[Building::GIDtoID(gid)];
        REQUIRE(flag); CHECK(flag->type->shortTypeNum==IntBuildingType::WAR_FLAG);
        CHECK(flag->maxUnitWorking==2);
        home->hp-=10; defense.perform(0); flush(); CHECK(defense.defending_zones.size()==1);
        defense.perform(1); flush(); CHECK(defense.defending_zones.size()==1);
        w.game.map.setGroundUnit(enemy->posX,enemy->posY,NOGUID);
        defense.perform(1); flush();
        CHECK(defense.defending_zones.empty()); CHECK(flag->buildingState!=Building::ALIVE);
    }

}
