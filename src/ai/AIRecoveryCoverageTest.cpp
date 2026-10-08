#include "CabinoObservationFixture.h"
#include "ai/engine/AIDecision.h"
// SPDX-License-Identifier: GPL-3.0-or-later
#include "EngineFixtures.h"
#include "AICastor.h"
#include "ai/observation/WorldQueries.h"
#include "AINicowar.h"
#include "AICabino.h"
#include "ai/cortex/CortexWater.h"
#include "ai/cortex/CortexFoodSources.h"
#include "ai/cortex/CortexQueryScratch.h"
#include "ai/cortex/CortexSnapshotQueries.h"
#include "Order.h"
#include "Brush.h"
#include "Player.h"
#include "ExperimentalFeatures.h"
#include <set>
#include <BinaryStream.h>
#include <StreamBackend.h>
#include "Version.h"

namespace
{
// Direct Castor helpers borrow the same immutable inputs as a decision, without
// advancing controller cadence or retaining a view after this fixture operation.
template<class Operation> decltype(auto) withCastorObservation(AICastor& ai, Game& game, Operation&& operation)
{
    const auto world = AIEngine::AIWorldView::capture(game, AIEngine::AIWorldView::captureCatalog(game));
    AIEngine::WorldQueries queries(*world, ai.teamNumber, ai.resourceInitializations);
    ai.observation = world.get(); ai.queries = &queries;
    ai.observedTeam = ai.teamAt(ai.teamNumber);
    struct Release {
        AICastor& ai;
        ~Release() { ai.observation = nullptr; ai.queries = nullptr; ai.observedTeam = nullptr; }
    } release{ai};
    return operation();
}
}

TEST_SUITE("AIRecoveryCoverage")
{
    TEST_CASE("Cabino declared inputs omit published fields without changing decisions")
 {
  glob2test::HeadlessGlobals globals;
  glob2test::HeadlessGame fixture{glob2test::GameOptions{.wDec=5,.hDec=5,.teams=2,.discovered=true,.clearImmobile=true,.loadDefaultRace=true,.header=true}};
  auto& game=fixture.game;
  REQUIRE(fixture.addBuilding("swarm",4,4));
  game.map.setResourceByIndex(18,18,WHEAT,1);
  Sint32 x=0,y=0,distance=0;
  game.map.materialAvailableUpdateSlot(0,materialIndex(MaterialId::Food),0,4,4,&x,&y,&distance);
  Cabino::AICabino full(game.players[0]),projected(game.players[0]);
  MersenneTwister fullRandom(713),projectedRandom(713);
  full.setRandomEngine(fullRandom);projected.setRandomEngine(projectedRandom);
  const auto requirements=projected.observationRequirements();
  CHECK_FALSE(SimulationSnapshot::needs(requirements,SimulationSnapshot::Component::ResourceFields));
  CHECK(SimulationSnapshot::needs(requirements,SimulationSnapshot::Component::Growth));
  const auto world=AIEngine::AIWorldView::capture(game,AIEngine::AIWorldView::captureCatalog(game));
  REQUIRE_FALSE(world->resourceGradient(0,WHEAT,0).empty());
  const AIEngine::AIWorldView input(world->components().project(requirements));
  CHECK_FALSE(input.components().resourceFields);
  std::vector<AIEngine::ExecutionReceipt> receipts;
  std::vector<AIEngine::ResourceEnrollmentRequest> enrollments;
  for(Uint64 poll=0;poll<128;++poll) {
   AIEngine::DecisionContext before{*world,0,0,receipts},after{input,0,0,receipts};
   before.pollSequence=after.pollSequence=poll;after.resourceEnrollments=&enrollments;
   auto a=full.getOrder(before),b=projected.getOrder(after);
   REQUIRE(a);REQUIRE(b);REQUIRE(a->getOrderType()==b->getOrderType());
   REQUIRE(a->getDataLength()==b->getDataLength());
   if(a->getDataLength()) CHECK(std::equal(a->getData(),a->getData()+a->getDataLength(),b->getData()));
   CHECK(enrollments.empty());
  }
 }

    TEST_CASE("Cortex shore harvest differs from inaccessible algae and swimming opens islands")
    {
        glob2test::HeadlessGlobals globals;
        glob2test::HeadlessGame w(glob2test::GameOptions{
            .terrain=WATER,.discovered=true,.clearImmobile=true,.loadDefaultRace=true,.header=true});
        for (int y=2;y<12;++y) for (int x=2;x<12;++x) w.game.map.paintCell(x, y, GRASS);
        for (int y=18;y<28;++y) for (int x=18;x<28;++x) w.game.map.paintCell(x, y, GRASS);
        w.addBuilding("swarm",4,4);
        auto* player=w.game.players[0];
        auto empty=Cortex::assessSwim(player,true);
        CHECK(empty.landReach>0); CHECK(empty.waterReach>empty.landReach);
        CHECK(empty.algaeDiscovered==0); CHECK(empty.algaeReachable==0);
        w.game.map.setResourceByIndex(20,16,ALGA,1);
        w.game.map.setMapDiscovered();
        auto distant=Cortex::assessSwim(player,true);
        CHECK(distant.algaeDiscovered==1); CHECK(distant.algaeReachable==0);
        // The island's grass vertices end at x=12; cell (13,6) is the first all-water one.
        w.game.map.setResourceByIndex(13,6,ALGA,1);
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
        for (int y=2;y<12;++y) for (int x=2;x<12;++x) w.game.map.paintCell(x, y, GRASS);
        for (int y=18;y<28;++y) for (int x=18;x<28;++x) w.game.map.paintCell(x, y, GRASS);
        w.addBuilding("swarm",4,4); w.addBuilding("swarm",20,20,0,1);
        auto water=Cortex::assessAmphibious(w.game.players[0],20,20,nullptr,nullptr,0,2,8);
        CHECK(water.amphibious==1); CHECK(water.landDist==-1); CHECK(water.swimDist>=0);
        CHECK(water.landingValid==1);
        CHECK(!w.game.map.isWater(water.landingX,water.landingY));
        for (int y=0;y<32;++y) for (int x=0;x<32;++x) w.game.map.paintCell(x, y, GRASS);
        auto land=Cortex::assessAmphibious(w.game.players[0],20,20,nullptr,nullptr,0,2,8);
        CHECK(land.amphibious==0); CHECK(land.landDist>=0);
    }

    TEST_CASE("Cortex wheat protection emits real area orders and food burst releases only our paint")
    {
        glob2test::HeadlessGlobals globals;
        glob2test::HeadlessGame w(glob2test::GameOptions{
            .teams=2,.discovered=true,.clearImmobile=true,.loadDefaultRace=true,.header=true});
        w.addBuilding("inn",2,2);
        for (int y=7;y<13;++y) for (int x=7;x<13;++x) w.game.map.setResourceByIndex(x,y,WHEAT,1);
        w.game.map.setMapDiscovered();
        std::fill(w.game.map.fogOfWar,w.game.map.fogOfWar+32*32,~Uint32(0));
        w.game.map.addForbidden(8,8,1);
        Cortex::QueryScratch scratch; Cortex::PlanningIntent intents;
        auto reconcile=[&](int openMargin,bool buildMasks,bool liftAll=false,bool farms=false) {
            const auto observed=AIEngine::AIWorldView::capture(w.game,AIEngine::AIWorldView::captureCatalog(w.game));
            return Cortex::reconcileFoodSourcesForbiddenWorld(observed.get(),&observed->teams[0],scratch,intents,nullptr,openMargin,buildMasks,liftAll,farms);
        };
        auto protection=reconcile(0,true);
        REQUIRE(protection.addCount>0);
        auto apply=[&](Uint8 mode,BrushAccumulator& brush) {
            auto order=std::make_shared<OrderAlterForbidden>(0,mode,&brush,&w.game.map);
            order->sender=0; w.game.executeOrder(order,0);
        };
        apply(BrushTool::MODE_ADD,protection.add);
        auto settled=reconcile(0,true);
        CHECK(settled.addCount==0); CHECK(settled.delCount==0);
        auto burst=reconcile(0,true,true);
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
        for(int y=14; y<18; ++y) for(int x=7; x<13; ++x) w.game.map.paintCell(x, y, WATER);
        for(int y=7; y<13; ++y) for(int x=7; x<13; ++x) w.game.map.setResourceByIndex(x,y,WHEAT,1);
        w.game.map.setMapDiscovered();
        std::fill(w.game.map.fogOfWar,w.game.map.fogOfWar+32*32,~Uint32(0));
        // A save from the prototype contains both wheat parities and its ring.
        for(int y=6; y<14; ++y) for(int x=6; x<14; ++x) w.game.map.addFarmArea(x,y,0);
        w.game.map.addFarmArea(8,8,1);
        Cortex::QueryScratch scratch; Cortex::PlanningIntent intents;
        auto reconcile=[&](int openMargin,bool buildMasks,bool liftAll=false,bool farms=false) {
            const auto observed=AIEngine::AIWorldView::capture(w.game,AIEngine::AIWorldView::captureCatalog(w.game));
            return Cortex::reconcileFoodSourcesForbiddenWorld(observed.get(),&observed->teams[0],scratch,intents,nullptr,openMargin,buildMasks,liftAll,farms);
        };
        auto apply=[&](Uint8 mode,BrushAccumulator& brush) {
            auto order=std::make_shared<OrderAlterFarmArea>(0,mode,&brush,&w.game.map);
            order->sender=0; w.game.executeOrder(order,0);
        };
        auto farm=reconcile(0,true,false,true);
        REQUIRE(farm.delCount>0);
        apply(BrushTool::MODE_DEL,farm.del);
        auto expected=Cortex::scanFoodSourcesForbidden(w.game.map,w.team->me,0,{4*32+4},0,0,31,31,0,true,false);
        std::set<int> pattern(expected.desired.begin(),expected.desired.end());
        for(int y=6; y<14; ++y) for(int x=6; x<14; ++x)
            CHECK(w.game.map.isFarmArea(x,y,w.team->me)==pattern.contains(y*32+x));
        auto settled=reconcile(0,true,false,true);
        CHECK(settled.addCount==0); CHECK(settled.delCount==0);
        auto burst=reconcile(0,true,true,true);
        REQUIRE(burst.delCount>0); CHECK(burst.addCount==0);
        apply(BrushTool::MODE_DEL,burst.del);
        CHECK(w.game.map.isFarmArea(8,8,w.game.teams[1]->me));
        auto restored=reconcile(0,true,false,true);
        apply(BrushTool::MODE_ADD,restored.add);
        // Depletion must still retire farm paint when the entire field is gone.
        for(int y=7; y<13; ++y) for(int x=7; x<13; ++x) {auto resource=w.game.map.getResource(x,y);resource.type=NO_RES_TYPE;w.game.map.replaceResource(x,y,resource);}
        auto depleted=reconcile(0,true,false,true);
        CHECK(depleted.addCount==0); CHECK(depleted.delCount==restored.addCount);
    }

    TEST_CASE("Castor projects deduplicate replace and decline already satisfied requests")
    {
        glob2test::HeadlessGlobals globals;
        glob2test::HeadlessGame w(glob2test::GameOptions{.clearImmobile=true,.loadDefaultRace=true,.header=true});
        AICastor ai(w.game.players[0]);
        withCastorObservation(ai,w.game,[&]{ai.computeBuildingSum();}); ai.timer=100;
        using Project=AICastor::Project;
        REQUIRE(withCastorObservation(ai,w.game,[&]{return ai.addProject(new Project(IntBuildingType::FOOD_BUILDING,1,2,"test"));}));
        auto* original=ai.projects.front();
        ai.timer=150;
        CHECK(!withCastorObservation(ai,w.game,[&]{return ai.addProject(new Project(IntBuildingType::FOOD_BUILDING,1,2,"repeat"));}));
        REQUIRE(ai.projects.size()==1); CHECK(ai.projects.front()==original); CHECK(original->timer==150);
        REQUIRE(withCastorObservation(ai,w.game,[&]{return ai.addProject(new Project(IntBuildingType::FOOD_BUILDING,2,3,"larger"));}));
        REQUIRE(ai.projects.size()==1); CHECK(ai.projects.front()->amount==2);
        w.addBuilding("inn",4,4); withCastorObservation(ai,w.game,[&]{ai.computeBuildingSum();});
        CHECK(!withCastorObservation(ai,w.game,[&]{return ai.addProject(new Project(IntBuildingType::FOOD_BUILDING,1,2,"satisfied"));}));
        CHECK(ai.projects.size()==1);
    }

    TEST_CASE("Castor food lock backs off growth and boot projects wait for their cadence")
    {
        glob2test::HeadlessGlobals globals;
        glob2test::HeadlessGame w(glob2test::GameOptions{.clearImmobile=true,.loadDefaultRace=true,.header=true});
        AICastor ai(w.game.players[0]);
        AICastor::Project p(IntBuildingType::SWARM_BUILDING,"test");
        ai.timer=100; p.timer=100;
        CHECK(!withCastorObservation(ai,w.game,[&]{return ai.continueProject(&p);})); CHECK(p.subPhase==AICastor::AI_CASTOR_SUBPHASE_BOOT);
        ai.timer=1000; ai.foodLock=true; ai.starvingWarning=true;
        p.critical=false; p.blocking=true;
        CHECK(!withCastorObservation(ai,w.game,[&]{return ai.continueProject(&p);})); CHECK(p.timer>ai.timer); CHECK(!p.blocking);
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
        AISharedRuntime::Runtime::OwnerObservationScope observationScope(runtime);
        MersenneTwister controllerRandom(713);runtime.setRandomEngine(controllerRandom);
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
        Uint16 flagGid;
        {
            const auto* flag=runtime.br.get_building(id);
            flagGid=flag->gid;
            CHECK(runtime.br.get_building_type(id)->shortTypeNum==IntBuildingType::WAR_FLAG);
            CHECK(flag->maxUnitWorking==1);
        }
        ai.compute_defense_flag_positioning(runtime); flush();
        REQUIRE(ai.defense_flags.size()==1); CHECK(ai.defense_flags.front()==id);
        w.game.map.setGroundUnit(enemy->posX,enemy->posY,NOGUID);
        threatened->underAttackTimer=0;
        // Begin the next owner observation after removing the threat. Captured
        // records belong to one borrow; inspect the executed live result by GID.
        runtime.refreshOwnerObservation();
        ai.compute_defense_flag_positioning(runtime); flush();
        const auto* flag=w.team->myBuildings[Building::GIDtoID(flagGid)];
        REQUIRE(flag);
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
        MersenneTwister controllerRandom(713);ai.setRandomEngine(controllerRandom);
        auto& defense=*static_cast<Cabino::SimpleBuildingDefense*>(ai.getDefenseModule());
        auto flush=[&] {
            while(!ai.orders.empty()) { auto order=ai.orders.front(); ai.orders.pop();
                order->sender=0; w.game.executeOrder(order,0); }
        };
        glob2test::withCabinoObservation(ai,w.game,[&]{return defense.perform(0);}); CHECK(defense.defending_zones.empty());
        home->hp-=10; glob2test::withCabinoObservation(ai,w.game,[&]{return defense.perform(0);}); flush();
        REQUIRE(defense.defending_zones.size()==1);
        glob2test::withCabinoObservation(ai,w.game,[&]{return defense.perform(2);}); flush();
        const auto gid=defense.defending_zones.front().flag;
        REQUIRE(gid!=NOGBID);
        auto* flag=w.team->myBuildings[Building::GIDtoID(gid)];
        REQUIRE(flag); CHECK(flag->type->shortTypeNum==IntBuildingType::WAR_FLAG);
        CHECK(flag->maxUnitWorking==2);
        home->hp-=10; glob2test::withCabinoObservation(ai,w.game,[&]{return defense.perform(0);}); flush(); CHECK(defense.defending_zones.size()==1);
        glob2test::withCabinoObservation(ai,w.game,[&]{return defense.perform(1);}); flush(); CHECK(defense.defending_zones.size()==1);
        w.game.map.setGroundUnit(enemy->posX,enemy->posY,NOGUID);
        glob2test::withCabinoObservation(ai,w.game,[&]{return defense.perform(1);}); flush();
        CHECK(defense.defending_zones.empty()); CHECK(flag->buildingState!=Building::ALIVE);
    }

    TEST_CASE("Cabino pending upgrades survive delayed observation and release rejected reservations")
    {
        glob2test::HeadlessGlobals globals;
        glob2test::HeadlessGame w(glob2test::GameOptions{.clearImmobile=true,.loadDefaultRace=true,.header=true});
        auto* building=w.addBuilding("barracks",4,4);
        REQUIRE(building);
        Cabino::AICabino ai(w.game.players[0]);
        auto& upgrades=*static_cast<Cabino::RandomUpgradeRepairModule*>(ai.getUpgradeRepairModule());
        upgrades.pending_construction.push_back({building->gid,4,unsigned(building->maxUnitWorking),false,0});
        glob2test::withCabinoObservation(ai,w.game,[&]{ai.getUnitModule()->reserve("RandomUpgradeRepairModule",WORKER,BUILD,1,4);});
        for(int delay:{0,8}) {
            w.game.stepCounter+=delay;
            glob2test::withCabinoObservation(ai,w.game,[&]{upgrades.updatePendingConstruction();});
            CHECK(upgrades.pending_construction.size()==1);
            CHECK(upgrades.active_construction.empty());
            CHECK(ai.orders.empty());
            CHECK(building->constructionResultState==Building::NO_CONSTRUCTION);
        }
        OrderConstruction command(building->gid,1,1);
        AIEngine::ExecutionReceipt rejected;
        rejected.status=AIEngine::ExecutionStatus::Rejected;
        rejected.command.push_back(command.getOrderType());
        rejected.command.insert(rejected.command.end(),command.getData(),command.getData()+command.getDataLength());
        const auto view=AIEngine::AIWorldView::capture(w.game,AIEngine::AIWorldView::captureCatalog(w.game));
        const std::vector<AIEngine::ExecutionReceipt> receipts{rejected};
        ai.getOrder(AIEngine::DecisionContext{*view,0,0,receipts});
        CHECK(upgrades.pending_construction.empty());
        auto& units=*static_cast<Cabino::DistributedUnitManager*>(ai.getUnitModule());
        CHECK(units.module_records["RandomUpgradeRepairModule"].reservedUnits[WORKER][BUILD][0]==0);
        CHECK(ai.game==nullptr); CHECK(ai.team==nullptr); CHECK(ai.map==nullptr);
    }

    TEST_CASE("Cabino queued target identities survive saves and reject reused building slots")
    {
        glob2test::HeadlessGlobals globals;
        for(int delay:{0,8}) {
            glob2test::HeadlessGame fixture({.clearImmobile=true,.loadDefaultRace=true,.header=true});
            auto* building=fixture.addBuilding("barracks",4,4);REQUIRE(building);
            Cabino::AICabino ai(fixture.game.players[0]);
            auto& upgrades=*static_cast<Cabino::RandomUpgradeRepairModule*>(ai.getUpgradeRepairModule());
            upgrades.pending_construction.push_back({building->gid,4,unsigned(building->maxUnitWorking),false,0});
            glob2test::withCabinoObservation(ai,fixture.game,[&] {
                ai.getUnitModule()->reserve("RandomUpgradeRepairModule",WORKER,BUILD,1,4);
                ai.enqueueOrder(std::make_shared<OrderConstruction>(building->gid,1,1));
            });
            REQUIRE(ai.orders.size()==1);REQUIRE(ai.orders.front()->aiSelectedTarget.has_value());
            const BuildingRef selected{building->gid,building->scriptIdentity};
            CHECK(*ai.orders.front()->aiSelectedTarget==selected);
            auto* backend=new GAGCore::MemoryStreamBackend;
            GAGCore::BinaryOutputStream output(backend);ai.save(&output);output.flush();
            const auto bytes=backend->takeContents();
            GAGCore::BinaryInputStream input(new GAGCore::MemoryStreamBackend(bytes.data(),bytes.size()));input.seekFromStart(0);
            Cabino::AICabino resumed(fixture.game.players[0]);
            REQUIRE(resumed.load(&input,fixture.game.players[0],VERSION_MINOR));
            REQUIRE(resumed.orders.size()==1);REQUIRE(resumed.orders.front()->aiSelectedTarget.has_value());
            CHECK(*resumed.orders.front()->aiSelectedTarget==selected);
            building->scriptIdentity=fixture.game.allocateScriptIdentity(true,building->gid);
            fixture.game.stepCounter+=delay;
            const auto view=AIEngine::AIWorldView::capture(fixture.game,AIEngine::AIWorldView::captureCatalog(fixture.game));
            const std::vector<AIEngine::ExecutionReceipt> receipts;
            AIEngine::DecisionContext context{*view,0,0,receipts};context.scheduledTick=view->tick+delay;
            CHECK(ai.getOrder(context)->getOrderType()==ORDER_NULL);
            CHECK(resumed.getOrder(context)->getOrderType()==ORDER_NULL);
            CHECK(ai.orders.empty());CHECK(resumed.orders.empty());
            CHECK(upgrades.pending_construction.empty());
            CHECK(static_cast<Cabino::RandomUpgradeRepairModule*>(resumed.getUpgradeRepairModule())->pending_construction.empty());
            auto& units=*static_cast<Cabino::DistributedUnitManager*>(ai.getUnitModule());
            CHECK(units.module_records["RandomUpgradeRepairModule"].reservedUnits[WORKER][BUILD][0]==0);
            CHECK(building->constructionResultState==Building::NO_CONSTRUCTION);
            CHECK(ai.game==nullptr);CHECK(resumed.game==nullptr);
        }
    }

}
