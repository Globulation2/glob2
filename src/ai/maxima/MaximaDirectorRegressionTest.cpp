#include "MaximaObservationFixture.h"
// Link with the game objects (excluding Glob2.cpp) to exercise the real runtime.
#include "EngineFixtures.h"
#include "Version.h"
#include "GlobalContainer.h"
#include "Game.h"
#include "../../src/team/Team.h"
#include "../../src/ai/AIImplementation.h"
#include "../../src/map/Map.h"
#include "Order.h"
#include "Player.h"
#include "TeamStat.h"
#include <memory>
#include <limits>
#include <locale>
#include <tuple>
#include <type_traits>
#include <list>
#include <map>
#include <queue>
#include <set>
#include <sstream>
#include <string>
#include <vector>
// Access the scheduler boundary without adding a production testing API.
#include "../../src/ai/maxima/AIMaximaRuntime.h"
#include "../../src/ai/maxima/AIMaxima.h"
#include "../../src/building/Building.h"
#include "BuildingType.h"
#include "../../src/building/IntBuildingType.h"
#include "../../src/unit/Unit.h"
#include <BinaryStream.h>
#include <StreamBackend.h>
#include <iostream>
#include <memory>

using namespace AIMaximaRuntime;

// Director decisions must survive scheduling, diplomacy and runtime lifecycles.
namespace director_regressions
{
using namespace AIMaxima;
struct Fixture
{
    Game game;
    Player player;
    std::unique_ptr<Maxima> ai;

    Fixture() : game(NULL)
    {
        game.map.setSize(6,6,GRASS);
        game.map.setGame(&game);
        for(int team=0; team<3; ++team) {
            game.addTeam();
            game.teams[team]->race.loadDefault();
            game.teams[team]->playersMask=1u<<team;
        }
        player.setTeam(game.teams[0]);
        ai.reset(new Maxima(&player));
        for(int y=0; y<64; ++y) for(int x=0; x<64; ++x) {
            game.map.setMapDiscovered(x,y,player.team->me);
            // Loading a playable map clears these; setSize alone does not.
            game.map.clearImmobileUnit(x,y);
        }
        ai->timer=100;
        ai->posture=Maxima::PostureCampaign;
        ai->snapshot.population=150;
        ai->snapshot.workers=100;
        ai->snapshot.trained_warriors=12;
        ai->budget.defense_reserve=0;
        ai->budget.food_emergency=false;
        ai->budget.colony_emergency=false;
        ai->strategy.tactics.enabled=true;
        ai->strategy.tactics.siege_enabled=true;
        ai->strategy.tactics.min_force=4;
        ai->strategy.raiding.enabled=false;
    }

    ::Building* building(int x, int y, int team, const char* type="inn", int level=0)
    {
        ::Building* result=game.addBuilding(game.map.normalizeX(x),game.map.normalizeY(y),
            game.buildingsTypes.getTypeNum(type,level,false),team);
        REQUIRE(result);
        return result;
    }

    int id(::Building* building)
    {
        for(auto record:ai->context.buildings.found())
            if(record.second.gid==building->gid) return record.first;
        REQUIRE(false);
        return -1;
    }

    Unit* warrior(int x, int y, int level=1)
    {
        Unit* unit=game.addUnit(x,y,0,WARRIOR,level,0,0,0);
        REQUIRE(unit);
        unit->medical=Unit::MED_FREE;
        unit->activity=Unit::ACT_RANDOM;
        return unit;
    }

    void attach(Unit* unit, ::Building* flag)
    {
        unit->attachedBuilding=flag;
        unit->activity=Unit::ACT_FLAG;
        flag->unitsWorking.push_back(unit);
    }

    void remember(::Building* building)
    {
        ai->reconnaissance.beginObservation(ai->timer,{building->owner->teamNumber});
        ai->reconnaissance.observeBuilding(Recon::BuildingSighting(building->gid,
            building->owner->teamNumber,building->typeNum,
            building->posX,building->posY,building->type->width,
            building->type->height,false,ai->timer));
        ai->reconnaissance.finishObservation();
    }

    void islands()
    {
        for(int y=0; y<64; ++y) for(int x=0; x<64; ++x) {
            const bool land=y>=5 && y<=55 && ((x>=5 && x<=19) || (x>=26 && x<=45));
            game.map.setTerrain(x,y,land ? 0 : 256);
        }
        ai->context.gradients.invalidate();
    }
};



static void reconnaissanceWaitsForEightyPercent()
{
    Fixture f;auto& a=*f.ai;auto& c=a.context;c.initialize();
    a.strategy.reconnaissance.enabled=true;
    a.strategy.reconnaissance.scouting_missions_enabled=true;
    f.game.map.unsetMapDiscovered();
    for(int y=0;y<64;++y)for(int x=0;x<64;++x)
        if(y*64+x<3276)f.game.map.setMapDiscovered(x,y,f.player.team->me);
    a.update_reconnaissance(c);
    REQUIRE(a.reconnaissance.report().exploredPercent==79);
    a.plan_reconnaissance_objectives(c);
    REQUIRE(a.budget.reconnaissance_suspended);
    REQUIRE(a.budget.reconnaissance_objectives.empty());
    REQUIRE(a.reconnaissance.report().desiredMissions==0);
    // A stale loaded budget cannot start missions before the next director pass.
    a.budget.reconnaissance_suspended=false;
    a.update_reconnaissance_missions(c);
    REQUIRE(a.reconnaissance_suspended);
    // 3277 / 4096 tiles is the first count that reaches 80 percent.
    f.game.map.setMapDiscovered(12,51,f.player.team->me);
    a.update_reconnaissance(c);
    REQUIRE(a.reconnaissance.report().exploredPercent==80);
    a.plan_reconnaissance_objectives(c);
    REQUIRE(!a.budget.reconnaissance_suspended);
    a.update_reconnaissance_missions(c);
    REQUIRE(!a.reconnaissance_suspended);
    a.budget.food_emergency=true;
    a.plan_reconnaissance_objectives(c);
    REQUIRE(!a.budget.reconnaissance_suspended);
    a.budget.colony_emergency=true;
    a.plan_reconnaissance_objectives(c);
    REQUIRE(a.budget.reconnaissance_suspended);
}

void cadence() {
    Fixture f; auto& a=*f.ai;
    a.strategy.scheduling.strategy_interval_ticks=10;
    a.snapshot.tick=100; a.timer=110; a.director.committed();
    a.director.evaluate(a,a.context);
    REQUIRE(a.snapshot.tick==110);
    // Duplicate calls within one tick remain suppressed; invalidation overrides it.
    a.snapshot.population=123;
    a.director.evaluate(a,a.context);
    REQUIRE(a.snapshot.population==123);
    a.director.invalidate(); a.director.evaluate(a,a.context);
    REQUIRE(a.snapshot.population!=123);
}

static void allyPrestige() {
    Fixture f; auto& a=*f.ai;
    f.player.team->enemies &= ~f.game.teams[2]->me;
    f.player.team->allies |= f.game.teams[2]->me;
    f.game.teams[2]->prestige=100; f.game.totalPrestige=100;
    a.snapshot=a.collect_snapshot(a.context);
    a.large_economy_committed=true; glob2test::withMaximaObservation(a.context,[&]() -> decltype(auto) {return a.build_policy_bids();});
    REQUIRE(a.snapshot.enemy_prestige==0);
    REQUIRE(a.policy_bids[Maxima::PolicyDefense].desired_towers==0);
    f.game.teams[1]->prestige=50; f.game.totalPrestige=150;
    a.snapshot=a.collect_snapshot(a.context); glob2test::withMaximaObservation(a.context,[&]() -> decltype(auto) {return a.build_policy_bids();});
    REQUIRE(a.snapshot.enemy_prestige==50);
    REQUIRE(a.policy_bids[Maxima::PolicyDefense].desired_towers>0);
}

static void thirdPartyTower() {
    Fixture f; f.building(10,10,0);
    auto target=f.building(40,40,1);
    auto tower=f.building(43,40,2,"defencetower",2);
    for(int i=0;i<12;++i) f.warrior(8+i,8);
    auto& a=*f.ai; auto& c=a.context; c.initialize();
    a.reconnaissance.beginObservation(a.timer,{1,2});
    for(auto b:{target,tower}) a.reconnaissance.observeBuilding(Recon::BuildingSighting(
        b->gid,b->owner->teamNumber,b->type->shortTypeNum,b->posX,b->posY,
        b->type->width,b->type->height,false,a.timer));
    a.reconnaissance.finishObservation();
    a.opponents[1].score=10000; a.opponents[2].score=0;
    a.plan_offense(c);
    REQUIRE(a.budget.tactical_target_gid==target->gid);
    // Remembered towers belonging to a current ally must not be counted.
    f.player.team->enemies &= ~f.game.teams[2]->me;
    f.player.team->allies |= f.game.teams[2]->me;
    a.plan_offense(c);
    REQUIRE(a.budget.tactical_target_gid==target->gid);
}

static void disconnectedArmy() {
    Fixture f; f.building(10,10,0); f.building(27,30,0);
    auto target=f.building(38,30,1); f.islands();
    std::vector<Unit*> warriors;
    for(int i=0;i<12;++i) {
        auto u=f.warrior(6+i,6);u->performance[SWIM]=0;warriors.push_back(u);
    }
    auto& a=*f.ai; auto& c=a.context; c.initialize(); f.remember(target);
    a.plan_offense(c);
    REQUIRE(a.budget.tactical_kind==Tactics::MissionNone);
    for(int i=0;i<4;++i) warriors[i]->performance[SWIM]=1;
    a.plan_offense(c);
    REQUIRE(a.budget.tactical_kind==Tactics::MissionSiege);
    REQUIRE(a.budget.tactical_requested_force==a.offense_diagnostics.eligibleWarriors);
    // Warriors already in the destination component need no swimming ability.
    for(int i=0;i<4;++i) {
        auto u=warriors[i];u->performance[SWIM]=0;
        f.game.map.setGroundUnit(u->posX,u->posY,NOGUID);
        u->posX=30+i;u->posY=20;f.game.map.setGroundUnit(u->posX,u->posY,u->gid);
    }
    a.plan_offense(c);
    REQUIRE(a.budget.tactical_kind==Tactics::MissionSiege);
    REQUIRE(a.budget.tactical_requested_force==a.offense_diagnostics.eligibleWarriors);
}

static void reusedOwnId() {
    for(bool sameLocation:{false,true}) {
        Fixture f; auto old=f.building(10,10,0); auto& c=f.ai->context;c.initialize();
        int oldId=f.id(old),oldGid=old->gid;
        old->kill();f.player.team->removeFromAbilitiesLists(old);
        f.player.team->myBuildings[::Building::GIDtoID(oldGid)]=NULL;delete old;
        // Identity remains safe even when a worker never observes the empty
        // slot between removal and replacement.
        auto replacement=f.building(sameLocation?10:40,sameLocation?10:40,0);
        REQUIRE(replacement->gid==oldGid);
        // Check before housekeeping, when deferred management work can execute.
        auto observation=c.scopeOwnerObservation();
        REQUIRE(!c.buildings.get_building(oldId));
        REQUIRE(Conditions::BuildingDestroyed(oldId).passes(c)==Conditions::Ready);
        Management::AssignWorkers(7,oldId).modify(c);
        REQUIRE(c.orders.empty());
        GAGCore::MemoryStreamBackend* backend=new GAGCore::MemoryStreamBackend;
        GAGCore::BinaryOutputStream output(backend);([&]{auto observation=c.scopeOwnerObservation();return c.buildings.save(&output);}());
        std::string bytes(backend->getBuffer(),backend->getPosition());
        GAGCore::BinaryInputStream input(new GAGCore::MemoryStreamBackend(bytes.data(),bytes.size()));
        input.seekFromStart(0);
        Construction::BuildingRegister loaded(&f.player);loaded.bind(c.observation(),c.playerNumber(),c.teamNumber());loaded.load(&input,VERSION_MINOR);
        REQUIRE(!loaded.is_building_found(oldId));
        {auto observation=c.scopeOwnerObservation();c.buildings.tick();} REQUIRE(c.buildings.found().empty());
    }
    // Moving flags and loading live registrations preserve valid identities.
    Fixture f; auto flag=f.building(10,10,0,"warflag");auto& c=f.ai->context;c.initialize();
    int id=f.id(flag);flag->posX=20;flag->posY=20;{auto observation=c.scopeOwnerObservation();c.buildings.tick();}
    auto observation=c.scopeOwnerObservation();
    REQUIRE((c.buildings.get_building(id) && c.buildings.get_building(id)->gid==flag->gid));
    GAGCore::MemoryStreamBackend* backend=new GAGCore::MemoryStreamBackend;
    GAGCore::BinaryOutputStream output(backend);([&]{auto observation=c.scopeOwnerObservation();return c.buildings.save(&output);}());
    std::string bytes(backend->getBuffer(),backend->getPosition());
    GAGCore::BinaryInputStream input(new GAGCore::MemoryStreamBackend(bytes.data(),bytes.size()));
    input.seekFromStart(0);
    Construction::BuildingRegister loaded(&f.player);loaded.bind(c.observation(),c.playerNumber(),c.teamNumber());loaded.load(&input,VERSION_MINOR);
    REQUIRE((loaded.get_building(id) && loaded.get_building(id)->gid==flag->gid));
}

static void fortificationUsesOnlySpareLabour()
{
    Fixture f;auto& a=*f.ai;
    AIMaximaPlacement::WorldState world;world.reset(16,16);world.profiles=glob2test::withMaximaObservation(a.context,[&]() -> decltype(auto) {return a.collect_building_profiles();});
    const int towerType=f.game.buildingsTypes.getPlaceableTypeNum("defencetower");
    a.strategy.staffing.construction_large_workers=6;
    a.labour_plan.trainingReserve=2;
    auto count=[&](AIMaximaPlacement::DevelopmentPurpose purpose) {
        int result=0;
        for(const auto& intent:glob2test::withMaximaObservation(a.context,[&]() -> decltype(auto) {return a.collect_development_intents(world);}))
            if(intent.buildingType==towerType && intent.purpose==purpose)
                ++result;
        return result;
    };
    a.labour_observation.idle=7;
    REQUIRE(count(AIMaximaPlacement::Fortification)==0);
    a.labour_observation.idle=8;
    REQUIRE(count(AIMaximaPlacement::Fortification)==1);
    a.budget.desired_towers=1;
    REQUIRE(count(AIMaximaPlacement::Fortification)==0);
    REQUIRE(count(AIMaximaPlacement::CoreCapacity)==1);
    AIMaximaPlacement::WorldBuilding tower;tower.buildingType=towerType;tower.level=1;
    world.buildings.push_back(tower);
    REQUIRE(count(AIMaximaPlacement::Fortification)==1);
}

static void unifiedHospitalCapacity() {
    using namespace AIMaximaPlacement;
    Fixture f; auto& a=*f.ai;
    a.snapshot.warriors=20;
    a.strategy.military.hospital_beds_per_warrior_percent=50;
    WorldState world;world.profiles=glob2test::withMaximaObservation(a.context,[&]() -> decltype(auto) {return a.collect_building_profiles();});
    const int hospitalType=f.game.buildingsTypes.getPlaceableTypeNum("hospital");
    WorldBuilding hospital; hospital.id=10;
    hospital.buildingType=hospitalType; hospital.level=1;
    world.buildings.push_back(hospital);
    const auto unmet=[&]() {
        for(const auto& intent:glob2test::withMaximaObservation(a.context,[&]() -> decltype(auto) {return a.collect_development_intents(world);}))
            if(intent.buildingType==hospitalType) return intent.unmetCount;
        return 0;
    };
    REQUIRE(a.committed_hospital_beds(world.buildings)==2);
    REQUIRE(unmet()==4);
    DevelopmentAction upgrade; upgrade.id=1; upgrade.type=UpgradeBuilding;
    upgrade.buildingType=hospitalType;
    upgrade.buildingId=10; upgrade.targetLevel=2; upgrade.state=ParcelReserved;
    a.development_planner.actionMap[1]=upgrade;
    REQUIRE(a.committed_hospital_beds(world.buildings)==5);
    REQUIRE(a.committed_hospital_beds(world.buildings,1)==2);
    REQUIRE(unmet()==3);
    world.buildings[0].level=2; world.buildings[0].site=true;
    REQUIRE(a.committed_hospital_beds(world.buildings)==5); // No double credit.
    DevelopmentAction build; build.id=2; build.type=BuildStandalone;
    build.buildingType=hospitalType;
    build.buildingId=11; build.state=CreateIssued;
    a.development_planner.actionMap[2]=build;
    REQUIRE(a.committed_hospital_beds(world.buildings)==7);
    hospital.id=11; hospital.site=true; world.buildings.push_back(hospital);
    REQUIRE(a.committed_hospital_beds(world.buildings)==7);
    a.development_planner.actionMap[2].state=Completed;
    REQUIRE(a.committed_hospital_beds(world.buildings)==7);
    a.snapshot.warriors=0;
    REQUIRE(unmet()==0); // No demolition when the army shrinks.
    // Existing hospitals upgrade only while the same capacity target is unmet.
    a.development_planner.actionMap.clear();
    f.building(20,20,0,"hospital"); a.context.initialize();
    REQUIRE(f.game.addUnit(4,4,0,WORKER,1,0,0,0));
    a.budget.upgrade_level1_hospital_weight=20;
    a.snapshot.warriors=4;
    REQUIRE(a.collect_development_limits(a.context).upgradePriority(hospitalType,1)==0);
    a.snapshot.warriors=10;
    REQUIRE(a.collect_development_limits(a.context).upgradePriority(hospitalType,1)==20);
    int id=-1;
    for(const auto& entry:a.context.get_building_register().found())
        if(entry.second.type==f.game.buildingsTypes.getTypeNum("hospital",0,false)) id=entry.first;
    REQUIRE(id>=0); upgrade.buildingId=id;
    a.development_planner.actionMap[1]=upgrade;
    REQUIRE(a.collect_development_limits(a.context).upgradePriority(hospitalType,1)==0);
    REQUIRE(a.collect_development_limits(a.context,1).upgradePriority(hospitalType,1)==20);
}

static void proactiveProtection() {
    Fixture f; f.building(10,10,0);auto& a=*f.ai;auto& c=a.context;c.initialize();
    for(int y=0;y<64;++y)f.game.map.setTerrain(0,y,256);
    f.game.map.setResource(3,11,WOOD,5);f.game.map.setResource(4,11,WOOD,5);f.game.map.setResource(5,11,WOOD,5);
    a.timer=5000;a.budget.farming_enabled=true;a.budget.farming_protection_enabled=true;
    a.budget.farming_minimum_wood_fertility=0;
    a.budget.farming_wood_firebreak_enabled=false;
    a.budget.farming_proactive_clearing_enabled=true;a.budget.farming_allow_proactive_clearing=true;
    a.budget.farming_min_workers_for_clearing=0;a.budget.farming_clearing_cooldown=0;
    a.budget.farming_clearing_duration=1500;a.budget.farming_clearing_quota=8;
    a.update_farming(c);
    for(auto o:c.managementOrders) if(auto p=dynamic_cast<Management::AddArea*>(o.get()))
        if(p->areaType==ForbiddenArea) for(auto xy:p->locations) f.game.map.addForbidden(xy.x,xy.y,f.player.team->teamNumber);
    c.managementOrders.clear();a.manage_land_clearing(c);
    int x=0,y=0;REQUIRE(([&]{auto observation=c.scopeOwnerObservation();return c.get_building_position(a.proactive_clearing_flag,x,y);}()));
    int released=0;
    for(auto o:c.managementOrders) if(auto p=dynamic_cast<Management::RemoveArea*>(o.get()))
        if(p->areaType==ForbiddenArea) for(auto xy:p->locations) {
            if(f.game.map.isForbidden(xy.x,xy.y,f.player.team->me))++released;
            f.game.map.removeForbidden(xy.x,xy.y,f.player.team->teamNumber);
        }
    REQUIRE(released>0);c.managementOrders.clear();a.timer+=64;a.update_farming(c);
    int openWood=0;
    for(int i=0;i<4096;++i) if(f.game.map.getTile(i%64,i/64).resource.type==WOOD
        &&f.game.map.warpDistSquare(x,y,i%64,i/64)<=16) {
        REQUIRE(!a.farm_protection_mask[i]);++openWood;
    }
    REQUIRE(openWood>0);
    // Releasing the flag must allow the ordinary farming policy to resume.
    ([&]{auto observation=c.scopeOwnerObservation();return c.cancel_or_destroy_building(a.proactive_clearing_flag,1u<<WORKER);}());
    c.managementOrders.clear();a.timer+=64;a.update_farming(c);
    int protectedAgain=0;
    for(int i=0;i<4096;++i) if(f.game.map.getTile(i%64,i/64).resource.type==WOOD
        &&f.game.map.warpDistSquare(x,y,i%64,i/64)<=16 && a.farm_protection_mask[i])++protectedAgain;
    REQUIRE(protectedAgain>0);
}
}

TEST_SUITE("Maxima.Director")
{
	TEST_CASE("reconnaissance waits for eighty percent") { glob2test::HeadlessGlobals globals; director_regressions::reconnaissanceWaitsForEightyPercent(); }
	TEST_CASE("fortification uses only spare labour") { glob2test::HeadlessGlobals globals; director_regressions::fortificationUsesOnlySpareLabour(); }
	TEST_CASE("unified hospital capacity") { glob2test::HeadlessGlobals globals; director_regressions::unifiedHospitalCapacity(); }
	TEST_CASE("cadence") { glob2test::HeadlessGlobals globals; director_regressions::cadence(); }
	TEST_CASE("ally prestige") { glob2test::HeadlessGlobals globals; director_regressions::allyPrestige(); }
	TEST_CASE("third party tower") { glob2test::HeadlessGlobals globals; director_regressions::thirdPartyTower(); }
	TEST_CASE("disconnected army") { glob2test::HeadlessGlobals globals; director_regressions::disconnectedArmy(); }
	TEST_CASE("proactive protection") { glob2test::HeadlessGlobals globals; director_regressions::proactiveProtection(); }
	TEST_CASE("reused own id") { glob2test::HeadlessGlobals globals; director_regressions::reusedOwnId(); }
}
