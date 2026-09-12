// Link with the game objects (excluding Glob2.cpp) to exercise the real runtime.
#include "../src/GlobalContainer.h"
#include "../src/Game.h"
#include "../src/team/Team.h"
#include "../src/ai/AIImplementation.h"
#include "../src/map/Map.h"
#include "../src/Order.h"
#include "../src/Player.h"
#include "../src/TeamStat.h"
#include <memory>
#include <boost/tuple/tuple.hpp>
#include <boost/tuple/tuple_comparison.hpp>
#include <list>
#include <map>
#include <queue>
#include <set>
#include <sstream>
#include <string>
#include <vector>
// Access the scheduler boundary without adding a production testing API.
#define private public
#include "../src/AIMaximaRuntime.h"
#include "../src/AIMaxima.h"
#undef private
#include "../src/building/Building.h"
#include "../src/game/entities/BuildingType.h"
#include "../src/building/IntBuildingType.h"
#include "../src/unit/Unit.h"
#include <BinaryStream.h>
#include <StreamBackend.h>
#include <cassert>
#include <iostream>
#include <memory>

GlobalContainer* globalContainer = NULL;
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
        ai->strategy.tactics.siege_min_force=4;
        ai->strategy.raiding.enabled=false;
    }

    ::Building* building(int x, int y, int team, const char* type="inn", int level=0)
    {
        ::Building* result=game.addBuilding(game.map.normalizeX(x),game.map.normalizeY(y),
            globalContainer->buildingsTypes.getTypeNum(type,level,false),team);
        assert(result);
        return result;
    }

    int id(::Building* building)
    {
        for(auto record:ai->context.buildings.found())
            if(record.second.gid==building->gid) return record.first;
        assert(false);
        return -1;
    }

    Unit* warrior(int x, int y, int level=1)
    {
        Unit* unit=game.addUnit(x,y,0,WARRIOR,level,0,0,0);
        assert(unit);
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
            building->owner->teamNumber,building->type->shortTypeNum,
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
    assert(a.reconnaissance.report().exploredPercent==79);
    a.plan_reconnaissance_objectives(c);
    assert(a.budget.reconnaissance_suspended);
    assert(a.budget.reconnaissance_objectives.empty());
    assert(a.reconnaissance.report().desiredMissions==0);
    // A stale loaded budget cannot start missions before the next director pass.
    a.budget.reconnaissance_suspended=false;
    a.update_reconnaissance_missions(c);
    assert(a.reconnaissance_suspended);
    // 3277 / 4096 tiles is the first count that reaches 80 percent.
    f.game.map.setMapDiscovered(12,51,f.player.team->me);
    a.update_reconnaissance(c);
    assert(a.reconnaissance.report().exploredPercent==80);
    a.plan_reconnaissance_objectives(c);
    assert(!a.budget.reconnaissance_suspended);
    a.update_reconnaissance_missions(c);
    assert(!a.reconnaissance_suspended);
    a.budget.food_emergency=true;
    a.plan_reconnaissance_objectives(c);
    assert(!a.budget.reconnaissance_suspended);
    a.budget.colony_emergency=true;
    a.plan_reconnaissance_objectives(c);
    assert(a.budget.reconnaissance_suspended);
}

void cadence() {
    Fixture f; auto& a=*f.ai;
    a.strategy.scheduling.strategy_interval_ticks=10;
    a.snapshot.tick=100; a.timer=110; a.director.committed();
    a.director.evaluate(a,a.context);
    assert(a.snapshot.tick==110);
    // Duplicate calls within one tick remain suppressed; invalidation overrides it.
    a.snapshot.population=123;
    a.director.evaluate(a,a.context);
    assert(a.snapshot.population==123);
    a.director.invalidate(); a.director.evaluate(a,a.context);
    assert(a.snapshot.population!=123);
}

static void allyPrestige() {
    Fixture f; auto& a=*f.ai;
    f.player.team->enemies &= ~f.game.teams[2]->me;
    f.player.team->allies |= f.game.teams[2]->me;
    f.game.teams[2]->prestige=100; f.game.totalPrestige=100;
    a.snapshot=a.collect_snapshot(a.context);
    a.large_economy_committed=true; a.build_policy_bids();
    assert(a.snapshot.enemy_prestige==0);
    assert(a.policy_bids[Maxima::PolicyDefense].desired_towers==0);
    f.game.teams[1]->prestige=50; f.game.totalPrestige=150;
    a.snapshot=a.collect_snapshot(a.context); a.build_policy_bids();
    assert(a.snapshot.enemy_prestige==50);
    assert(a.policy_bids[Maxima::PolicyDefense].desired_towers>0);
}

static void thirdPartyTower() {
    Fixture f; f.building(10,10,0);
    auto target=f.building(40,40,1);
    auto tower=f.building(43,40,2,"defencetower",2);
    for(int i=0;i<12;++i) f.warrior(8+i,8);
    auto& a=*f.ai; auto& c=a.context; c.initialize();
    a.strategy.teamplay.defense_enabled=false;
    a.reconnaissance.beginObservation(a.timer,{1,2});
    for(auto b:{target,tower}) a.reconnaissance.observeBuilding(Recon::BuildingSighting(
        b->gid,b->owner->teamNumber,b->type->shortTypeNum,b->posX,b->posY,
        b->type->width,b->type->height,false,a.timer));
    a.reconnaissance.finishObservation();
    a.opponents[1].score=10000; a.opponents[2].score=0;
    a.plan_tactical_authorization(c);
    assert(a.budget.tactical_target_gid==target->gid);
    // Remembered towers belonging to a current ally must not be counted.
    f.player.team->enemies &= ~f.game.teams[2]->me;
    f.player.team->allies |= f.game.teams[2]->me;
    a.plan_tactical_authorization(c);
    assert(a.budget.tactical_target_gid==target->gid);
}

static void disconnectedArmy() {
    Fixture f; f.building(10,10,0); f.building(27,30,0);
    auto target=f.building(38,30,1); f.islands();
    std::vector<Unit*> warriors;
    for(int i=0;i<12;++i) {
        auto u=f.warrior(6+i,6);u->performance[SWIM]=0;warriors.push_back(u);
    }
    auto& a=*f.ai; auto& c=a.context; c.initialize(); f.remember(target);
    a.strategy.teamplay.defense_enabled=false;
    a.plan_tactical_authorization(c);
    assert(a.budget.tactical_kind==Tactics::MissionNone);
    for(int i=0;i<4;++i) warriors[i]->performance[SWIM]=1;
    a.plan_tactical_authorization(c);
    assert(a.budget.tactical_kind==Tactics::MissionSiege);
    assert(a.budget.tactical_requested_force==4);
    // Warriors already in the destination component need no swimming ability.
    for(int i=0;i<4;++i) {
        auto u=warriors[i];u->performance[SWIM]=0;
        f.game.map.setGroundUnit(u->posX,u->posY,NOGUID);
        u->posX=30+i;u->posY=20;f.game.map.setGroundUnit(u->posX,u->posY,u->gid);
    }
    a.plan_tactical_authorization(c);
    assert(a.budget.tactical_kind==Tactics::MissionSiege);
    assert(a.budget.tactical_requested_force==4);
}

static void reusedOwnId() {
    for(bool sameLocation:{false,true}) {
        Fixture f; auto old=f.building(10,10,0); auto& c=f.ai->context;c.initialize();
        int oldId=f.id(old),oldGid=old->gid;
        old->kill();f.player.team->removeFromAbilitiesLists(old);
        f.player.team->myBuildings[::Building::GIDtoID(oldGid)]=NULL;delete old;
        auto replacement=f.building(sameLocation?10:40,sameLocation?10:40,0);
        assert(replacement->gid==oldGid);
        // Check before housekeeping, when deferred management work can execute.
        assert(!c.buildings.get_building(oldId));
        assert(Conditions::BuildingDestroyed(oldId).passes(c)==Conditions::Ready);
        Management::AssignWorkers(7,oldId).modify(c);
        assert(c.orders.empty());
        GAGCore::MemoryStreamBackend* backend=new GAGCore::MemoryStreamBackend;
        GAGCore::BinaryOutputStream output(backend);c.buildings.save(&output);
        std::string bytes(backend->getBuffer(),backend->getPosition());
        GAGCore::BinaryInputStream input(new GAGCore::MemoryStreamBackend(bytes.data(),bytes.size()));
        input.seekFromStart(0);
        Construction::BuildingRegister loaded(&f.player);loaded.load(&input);
        assert(!loaded.is_building_found(oldId));
        c.buildings.tick(); assert(c.buildings.found().empty());
    }
    // Moving flags and loading live registrations preserve valid identities.
    Fixture f; auto flag=f.building(10,10,0,"warflag");auto& c=f.ai->context;c.initialize();
    int id=f.id(flag);flag->posX=20;flag->posY=20;c.buildings.tick();
    assert(c.buildings.get_building(id)==flag);
    GAGCore::MemoryStreamBackend* backend=new GAGCore::MemoryStreamBackend;
    GAGCore::BinaryOutputStream output(backend);c.buildings.save(&output);
    std::string bytes(backend->getBuffer(),backend->getPosition());
    GAGCore::BinaryInputStream input(new GAGCore::MemoryStreamBackend(bytes.data(),bytes.size()));
    input.seekFromStart(0);
    Construction::BuildingRegister loaded(&f.player);loaded.load(&input);
    assert(loaded.get_building(id)==flag);
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
        if(p->areaType==ForbiddenArea) for(auto xy:p->locations) f.game.map.setForbidden(xy.x,xy.y,f.player.team->me);
    c.managementOrders.clear();a.manage_land_clearing(c);
    int x=0,y=0;assert(c.get_building_position(a.proactive_clearing_flag,x,y));
    int released=0;
    for(auto o:c.managementOrders) if(auto p=dynamic_cast<Management::RemoveArea*>(o.get()))
        if(p->areaType==ForbiddenArea) for(auto xy:p->locations) {
            if(f.game.map.isForbidden(xy.x,xy.y,f.player.team->me))++released;
            f.game.map.setForbidden(xy.x,xy.y,0);
        }
    assert(released>0);c.managementOrders.clear();a.timer+=64;a.update_farming(c);
    int openWood=0;
    for(int i=0;i<4096;++i) if(f.game.map.getTile(i%64,i/64).resource.type==WOOD
        &&f.game.map.warpDistSquare(x,y,i%64,i/64)<=16) {
        assert(!a.farm_protection_mask[i]);++openWood;
    }
    assert(openWood>0);
    // Releasing the flag must allow the ordinary farming policy to resume.
    c.cancel_or_destroy_building(a.proactive_clearing_flag);
    c.managementOrders.clear();a.timer+=64;a.update_farming(c);
    int protectedAgain=0;
    for(int i=0;i<4096;++i) if(f.game.map.getTile(i%64,i/64).resource.type==WOOD
        &&f.game.map.warpDistSquare(x,y,i%64,i/64)<=16 && a.farm_protection_mask[i])++protectedAgain;
    assert(protectedAgain>0);
}
}

int main() {
    GlobalContainer container;globalContainer=&container;container.runNoX=true;
    container.buildingsTypes.init();
    IntBuildingType::init();
    director_regressions::reconnaissanceWaitsForEightyPercent();
    director_regressions::cadence();director_regressions::allyPrestige();
    director_regressions::thirdPartyTower();director_regressions::disconnectedArmy();
    director_regressions::proactiveProtection();director_regressions::reusedOwnId();
    std::cout << "director review regressions passed\n";
}
