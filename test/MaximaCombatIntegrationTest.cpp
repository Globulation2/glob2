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

// Combat policy must survive the handoff to real flag and engine behavior.
namespace combat_regressions
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

static bool assigned(Context& context, int id, int workers)
{
    for(auto order:context.managementOrders) {
        auto assignment=dynamic_cast<Management::AssignWorkers*>(order.get());
        if(assignment && assignment->id==id && assignment->workers==workers) return true;
    }
    return false;
}

static void defenseWrapsBuildingOrigins()
{
    // A building whose origin has crossed the toroidal seam used to index the
    // threat arrays out of bounds before any wrapping was applied.
    for(auto origin:{position(-1,7),position(7,-1),position(64,7),position(7,64)}) {
        Fixture f;
        auto* building=f.building(origin.x,origin.y,0);
        auto& a=*f.ai;
        a.context.initialize();
        const int x=building->posX,y=building->posY;
        building->posX=origin.x;
        building->posY=origin.y;
        building->underAttackTimer=100;
        a.budget.reactive_defense_enabled=true;
        a.budget.reactive_defense_flag_radius=5;
        a.budget.reactive_defense_unit_cap=10;
        a.budget.reactive_defense_advantage_min=3;
        a.budget.defense_reserve=10;
        a.compute_defense_flag_positioning(a.context);
        assert(!a.context.buildingOrders.empty());
        building->posX=x;
        building->posY=y;
    }
}

static void defenseCoverage()
{
    // Exercise both threat inputs and wrapping at the map edge. The fourth
    // target used to disappear from the potential field outside flag coverage.
    for(bool invaders:{false,true}) for(int shift:{0,40}) {
        Fixture f;
        f.building(20+shift,24+shift,0);
        auto& a=*f.ai;
        auto& c=a.context;
        c.initialize();
        const int points[][2]={{28,29},{28,30},{20,20},{28,21}};
        for(auto& point:points) {
            Unit* unit=f.game.addUnit((point[0]+shift)%64,(point[1]+shift)%64,
                invaders ? 1 : 0,invaders ? WARRIOR : WORKER,1,0,0,0);
            assert(unit);
            if(!invaders) unit->underAttackTimer=100;
        }
        a.budget.reactive_defense_enabled=true;
        a.budget.reactive_defense_flag_radius=5;
        a.budget.reactive_defense_unit_cap=10;
        a.budget.reactive_defense_advantage_min=3;
        a.budget.defense_reserve=30;
        a.compute_defense_flag_positioning(c);
        std::vector<position> flags;
        int allocation=0;
        for(auto order:c.buildingOrders) {
            bool complete=false;
            auto location=order->find_location(c,1,complete);
            assert(complete && location.found);
            flags.push_back(location.value);
            allocation+=order->workers;
        }
        assert(allocation<=a.budget.defense_reserve);
        for(auto& point:points) {
            bool covered=false;
            for(auto flag:flags)
                covered|=f.game.map.warpDistSquare((point[0]+shift)%64,
                    (point[1]+shift)%64,flag.x,flag.y)<=25;
            assert(covered);
        }
    }
}

static void economicRecoveryPreservesMilitaryBudget()
{
    Fixture f; auto& a=*f.ai;
    a.snapshot.population=100;
    a.snapshot.workers=1;
    a.snapshot.warriors=a.snapshot.trained_warriors=60;
    a.snapshot.alive_enemies=1;
    a.opponents[1].alive=true;
    a.opponents[1].score=10;
    a.opponents[1].estimated_warriors=10;
    a.strategy.emergencies.colony_enabled=false;
    a.allocate_resources();
    const int reserve=a.budget.defense_reserve;
    const int attack=a.budget.attack_units;
    assert(a.budget.attack_flags==1 && attack>0);
    a.snapshot.unserved_food=50;
    a.snapshot.critical_food=50;
    a.posture=Maxima::PostureRecover;
    a.allocate_resources();
    assert(a.severe_food_emergency());
    assert(a.budget.attack_flags==1 && a.budget.attack_units==attack);
    assert(a.budget.defense_reserve==reserve);
    a.posture=Maxima::PostureCampaign;
    a.posture_since=0;
    a.campaign.cooldown_until=0;
    a.select_posture();
    assert(a.posture==Maxima::PostureRecover);
    assert(a.campaign.cooldown_until==0); // Recovery cannot inject a military cooldown.
}

static void forbiddenDefenseZones()
{
    for(bool amphibious:{false,true}) {
        Fixture f;
        for(int y=0;y<64;++y) for(int x=0;x<64;++x)
            f.game.map.setTerrain(x,y,(x>=5&&x<=50&&y>=20&&y<=27)?0:256);
        f.building(10,22,0);
        f.building(42,22,1)->seenByMask|=f.player.team->me;
        auto& a=*f.ai; auto& c=a.context; c.initialize();
        a.budget.preemptive_defense_active=true;
        a.budget.preemptive_amphibious_active=amphibious;
        a.budget.preemptive_effective_zone_max=1;
        a.budget.preemptive_inner_distance=4;
        a.budget.preemptive_band_width=8;
        a.budget.preemptive_path_slack=2;
        a.budget.preemptive_probe_radius=8;
        a.budget.preemptive_cross_section_max=10;
        a.budget.preemptive_zone_radius=2;
        // Include the surrounding sea so swimming cannot bypass the barrier.
        for(int y=0;y<64;++y) for(int x=0;x<64;++x)
            if(x<5||x>50||y<20||y>27||(x>=16&&x<=38))
                f.game.map.addForbidden(x,y,f.player.team->teamNumber);
        a.update_preemptive_defense(c);
        assert(a.preemptive_guard_tiles.empty());
        for(int y=20;y<=27;++y) for(int x=16;x<=38;++x)
            f.game.map.removeForbidden(x,y,f.player.team->teamNumber);
        a.timer+=a.budget.preemptive_recompute_ticks;
        a.update_preemptive_defense(c);
        assert(!a.preemptive_guard_tiles.empty());
        for(int i:a.preemptive_guard_tiles) {
            assert(!f.game.map.isForbidden(i%64,i/64,f.player.team->me));
            f.game.map.addGuardArea(i%64,i/64,0);
        }
        f.game.map.getGuardAreasGradient(0,amphibious);
        f.game.map.updateGuardAreasGradient(0,amphibious);
        assert(f.game.map.getGuardAreasGradient(0,amphibious)[22*64+8]>1);
    }
}

static void defenseDeadbandPreservesCoverage()
{
    // Check all three coverage inputs, including wrapping and a harmless
    // shift that should still be suppressed by the existing deadband.
    for(int threatKind:{0,1,2}) for(int shift:{0,44}) for(bool covered:{false,true}) {
        Fixture f;
        const auto wrap=[&](int value) { return (value+shift)%64; };
        f.building(wrap(20),wrap(24),0);
        int x=wrap(22),y=wrap(20);
        if(threatKind==2) {
            auto building=f.building(wrap(24),wrap(20),0);
            building->underAttackTimer=100;
            x=f.game.map.normalizeX(building->posX-building->type->decLeft);
            y=f.game.map.normalizeY(building->posY-building->type->decTop);
        } else {
            auto unit=f.game.addUnit(x,y,threatKind==0?1:0,
                threatKind==0?WARRIOR:WORKER,1,0,0,0);
            assert(unit);
            if(threatKind==1) unit->underAttackTimer=100;
        }
        auto flag=f.building(x-(covered?4:6),y,0,"warflag");
        flag->unitStayRange=5;
        auto& a=*f.ai; auto& c=a.context; c.initialize();
        a.defense_flags.push_back(f.id(flag));
        a.budget.reactive_defense_flag_radius=5;
        a.budget.reactive_defense_move_deadband=2;
        a.budget.reactive_defense_move_radius=10;
        a.budget.reactive_defense_unit_cap=10;
        a.budget.reactive_defense_advantage_min=3;
        a.budget.defense_reserve=10;
        a.compute_defense_flag_positioning(c);
        bool moved=false;
        int finalX=flag->posX,finalY=flag->posY;
        for(auto order:c.managementOrders) {
            auto move=dynamic_cast<Management::ChangeFlagPosition*>(order.get());
            if(move) { moved=true; finalX=move->x; finalY=move->y; }
        }
        assert(moved==!covered);
        assert(c.buildingOrders.empty());
        assert(f.game.map.warpDistSquare(finalX,finalY,x,y)<=25);
    }
}

// Issue the pending flag order and materialize the engine building so later
// reviews see a found flag with real recruitment behavior.
static ::Building* materializeFlag(Fixture& f)
{
    auto& c=f.ai->context;
    c.update_building_orders();
    std::shared_ptr<OrderCreate> create;
    for(auto order:c.orders)
        if(auto candidate=std::dynamic_pointer_cast<OrderCreate>(order)) create=candidate;
    assert(create);
    auto flag=f.game.addBuilding(create->posX,create->posY,create->typeNum,0);
    assert(flag);
    c.buildings.tick();
    c.orders.clear();
    c.update_management_orders();
    for(auto order:c.orders) {
        if(auto minimum=std::dynamic_pointer_cast<OrderModifyMinLevelToFlag>(order))
            flag->minLevelToFlag=minimum->minLevelToFlag;
        if(auto size=std::dynamic_pointer_cast<OrderModifyFlag>(order))
            flag->unitStayRange=size->range;
        if(auto staffing=std::dynamic_pointer_cast<OrderModifyBuilding>(order))
            flag->maxUnitWorking=staffing->numberRequested;
    }
    c.orders.clear();
    return flag;
}

static void warriorEligibility()
{
    Fixture f;
    f.building(10,10,0);
    auto target=f.building(35,30,1);
    f.islands();
    for(int i=0; i<12; ++i) {
        auto unit=f.warrior(6+i,6);
        unit->performance[SWIM]=0;
        unit->level[SWIM]=0;
    }
    std::vector<Unit*> swimmers;
    for(int i=0; i<4; ++i) {
        auto unit=f.warrior(6+i,7,0);
        unit->performance[SWIM]=1;
        unit->level[SWIM]=1;
        swimmers.push_back(unit);
    }
    auto& a=*f.ai;
    auto& c=a.context;
    c.initialize();
    f.remember(target);
    a.snapshot.swimming_warriors=4;
    for(int ability:{ATTACK_SPEED,ATTACK_STRENGTH}) {
        // Neither single upgrade is sufficient for a level-2 tactical flag.
        for(auto unit:swimmers) {
            unit->level[ATTACK_SPEED]=0;
            unit->level[ATTACK_STRENGTH]=0;
            unit->level[ability]=1;
        }
        a.plan_offense(c);
        assert(a.budget.tactical_kind==Tactics::MissionNone);
    }
    for(auto unit:swimmers) {
        unit->level[ATTACK_SPEED]=unit->level[ATTACK_STRENGTH]=1;
        for(int ability:{ATTACK_SPEED,ATTACK_STRENGTH})
            unit->performance[ability]=unit->race->getUnitType(WARRIOR,1)->performance[ability];
    }
    a.plan_offense(c);
    assert(a.budget.tactical_kind==Tactics::MissionSiege);
    assert(a.budget.tactical_requested_force==a.offense_diagnostics.eligibleWarriors);
    a.control_offense(c);
    assert(a.tactical_mission.flagId>=0 && a.tactical_mission.kind==Tactics::MissionSiege);
    auto flag=materializeFlag(f);
    assert(flag->maxUnitWorking==a.offense_diagnostics.eligibleWarriors && flag->minLevelToFlag==1);
    flag->updateCallLists();
    for(int step=0; step<33; ++step) flag->subscribeForFlagingStep();
    assert(flag->unitsWorking.size()==4);
    for(auto unit:flag->unitsWorking) assert(unit->performance[SWIM]>0);
}

static void untrainedWarriorsFightAtLevelOne()
{
    for(int level:{1,2}) {
        Fixture f;
        f.building(10,10,0);
        auto target=f.building(35,30,1);
        for(int i=0;i<4;++i) f.warrior(15+i,15,0);
        auto& a=*f.ai; auto& c=a.context; c.initialize(); f.remember(target);
        a.strategy.tactics.flag_minimum_level=level;
        a.plan_offense(c);
        assert((a.budget.tactical_kind==Tactics::MissionSiege)==(level==1));
        assert(level==1 || a.offense_diagnostics.eligibleWarriors==0);
    }
}

static void minimumForceGate()
{
    for(int warriors:{3,4}) {
        Fixture f;
        f.building(10,10,0);
        auto target=f.building(35,30,1);
        for(int i=0;i<warriors;++i) f.warrior(15+i,15);
        auto& a=*f.ai; auto& c=a.context; c.initialize(); f.remember(target);
        a.plan_offense(c);
        assert(a.offense_diagnostics.eligibleWarriors==warriors);
        assert((a.budget.tactical_kind==Tactics::MissionSiege)==(warriors>=4));
        a.control_offense(c);
        assert((a.tactical_mission.flagId>=0)==(warriors>=4));
        assert(c.buildingOrders.empty()==(warriors<4));
    }
}

static void barracksAreFilledBeforeAttacking()
{
    for(int warriors:{6,8}) {
        Fixture f;
        f.building(10,10,0);
        auto barracks=f.building(14,10,0,"barracks");
        barracks->maxUnitInside=4;
        auto target=f.building(35,30,1);
        std::vector<Unit*> army;
        for(int i=0;i<warriors;++i) army.push_back(f.warrior(15+i,15));
        auto& a=*f.ai; auto& c=a.context; c.initialize(); f.remember(target);
        a.plan_offense(c);
        assert(a.offense_diagnostics.openTrainingSlots==4);
        // Six warriors leave only two beyond the barracks; eight leave four.
        assert((a.budget.tactical_kind==Tactics::MissionSiege)==(warriors==8));
        if(warriors==8) {
            assert(a.budget.tactical_requested_force==4);
            a.control_offense(c);
            assert(a.tactical_mission.requestedForce==4);
            // Two warriors enter training: the flag gives up two seats to them.
            barracks->unitsInside.push_back(army[0]); barracks->unitsInside.push_back(army[1]);
            a.plan_offense(c);
            assert(a.budget.tactical_requested_force==6); // 8 eligible - 2 open slots
            c.managementOrders.clear();
            a.control_offense(c);
            assert(a.tactical_mission.requestedForce==6 && assigned(c,a.tactical_mission.flagId,6));
        }
    }
}

static void foodPostureDoesNotVetoCombat()
{
    Fixture f;
    f.building(10,10,0);
    auto target=f.building(35,30,1);
    for(int i=0;i<4;++i) f.warrior(15+i,15);
    auto& a=*f.ai; auto& c=a.context; c.initialize(); f.remember(target);
    a.snapshot.population=100;
    a.snapshot.workers=1;
    a.snapshot.critical_food=20;
    a.trends.population=-100;
    a.trends.food_pressure=100;
    a.strategy.emergencies.food_enabled=true;
    a.strategy.emergencies.food_critical_percent=20;
    assert(a.severe_food_emergency());
    a.budget.food_emergency=true;
    for(auto posture:{Maxima::PostureExpand,Maxima::PostureDevelop,
            Maxima::PostureRecover,Maxima::PostureDefend,Maxima::PostureCampaign}) {
        a.posture=posture;
        a.plan_offense(c);
        assert(a.budget.tactical_kind==Tactics::MissionSiege);
    }
    a.control_offense(c);
    assert(a.tactical_mission.flagId>=0);
    // A colony emergency is the one thing that recalls the army.
    a.strategy.emergencies.colony_enabled=true;
    a.snapshot.visible_colony_threat=a.strategy.emergencies.colony_threat_threshold;
    assert(a.severe_colony_emergency());
    a.plan_offense(c);
    assert(a.budget.tactical_kind==Tactics::MissionNone);
    a.control_offense(c);
    assert(a.tactical_mission.flagId<0 && a.attack_flags.empty());
}

static void sealedTargetOpensRoute()
{
    for(bool sealed:{false,true}) for(bool labor:{false,true}) {
        Fixture f; f.building(10,10,0);
        auto survivor=f.building(35,30,2);
        for(int i=0;i<12;++i) f.warrior(6+i,6)->performance[SWIM]=0;
        // A ring of wood seals the target: neither walkers nor swimmers can pass.
        if(sealed) for(int y=27;y<=37;++y) for(int x=32;x<=42;++x)
            if(x==32||x==42||y==27||y==37) f.game.map.setResource(x,y,WOOD,1);
        auto& a=*f.ai; auto& c=a.context;
        c.initialize(); f.remember(survivor);
        a.strategy.tactics.dig_out_enabled=true;
        a.budget.attack_clearing_workers=labor ? 10 : 0;
        a.opponents[2].alive=true; a.opponents[2].known_buildings=1;
        a.opponents[2].score=100;
        a.plan_offense(c);
        if(sealed) {
            assert(a.budget.tactical_kind==Tactics::MissionNone);
            assert(a.budget.tactical_dig_out_team==(labor ? 2 : -1));
            assert(a.offense_diagnostics.rejections["no_route"]==1);
        } else {
            assert(a.budget.tactical_kind==Tactics::MissionSiege);
            assert(a.budget.tactical_target_team==2);
        }
    }
}

static void offenseFollowsDestroyedTargets()
{
    Fixture f;
    f.building(10,10,0);
    for(int i=0;i<4;++i) f.warrior(6+i,6);
    auto first=f.building(30,30,1);
    auto next=f.building(40,40,1);
    auto& a=*f.ai; auto& c=a.context; c.initialize();
    f.remember(first); f.remember(next);
    a.plan_offense(c);
    assert(a.budget.tactical_kind==Tactics::MissionSiege);
    assert(a.budget.tactical_target_gid==first->gid); // Nearer, same value.
    a.control_offense(c);
    const int flagId=a.tactical_mission.flagId;
    assert(flagId>=0 && a.campaign.state==Maxima::CampaignActive);
    assert(a.attack_flag_targets[flagId]==first->gid);
    // A better target within the margin does not move the flag.
    a.plan_offense(c);
    assert(a.budget.tactical_target_gid==first->gid);
    c.managementOrders.clear();
    a.control_offense(c);
    for(auto order:c.managementOrders)
        assert(!dynamic_cast<Management::ChangeFlagPosition*>(order.get()));
    // Once the dwell has passed, a quarantined target hands the flag to the
    // next building without tearing the flag down.
    a.attack_target_quarantine_until[first->gid]=a.timer+1000;
    a.timer+=a.strategy.tactics.dwell_ticks;
    a.plan_offense(c);
    assert(a.budget.tactical_target_gid==next->gid);
    c.managementOrders.clear();
    a.control_offense(c);
    assert(a.tactical_mission.flagId==flagId && a.tactical_mission.targetGid==next->gid);
    assert(a.campaign.buildings_destroyed==0); // The first building still stands.
    bool moved=false;
    for(auto order:c.managementOrders)
        if(auto move=dynamic_cast<Management::ChangeFlagPosition*>(order.get()))
            moved|=move->x==a.budget.tactical_target_x && move->y==a.budget.tactical_target_y;
    assert(moved);
    // Destruction is credited when recon no longer remembers the flag's target.
    const int nextGid=next->gid;
    next->kill();
    f.game.teams[1]->syncStep();
    a.reconnaissance.beginObservation(a.timer,{1});
    a.reconnaissance.confirmBuildingAbsent(1,nextGid);
    a.reconnaissance.finishObservation();
    assert(!a.reconnaissance.opponent(1)->buildings.count(nextGid));
    a.budget.tactical_kind=Tactics::MissionSiege;
    a.budget.tactical_target_team=1;
    a.budget.tactical_target_gid=first->gid;
    a.budget.tactical_target_x=31; a.budget.tactical_target_y=31;
    a.budget.tactical_requested_force=a.strategy.military.attack_unit_cap;
    c.managementOrders.clear();
    a.control_offense(c);
    assert(a.campaign.buildings_destroyed==1 && a.tactical_mission.targetGid==first->gid);
    // Without any remembered target the flag comes down.
    a.plan_offense(c);
    assert(a.budget.tactical_kind==Tactics::MissionNone);
    a.control_offense(c);
    assert(a.tactical_mission.flagId<0 && a.campaign.state==Maxima::CampaignIdle);
}

static void stalledTargetIsQuarantined()
{
    Fixture f;
    f.building(10,10,0);
    auto target=f.building(35,30,1);
    auto flag=f.building(36,31,0,"warflag");
    std::vector<Unit*> army;
    for(int i=0;i<4;++i) { army.push_back(f.warrior(35+i%2,32+i/2)); f.attach(army.back(),flag); }
    auto& a=*f.ai; auto& c=a.context; c.initialize(); f.remember(target);
    auto& mission=a.tactical_mission;
    mission.kind=Tactics::MissionSiege; mission.phase=Tactics::PhaseEngage;
    mission.flagId=f.id(flag); mission.targetTeam=1; mission.targetGid=target->gid;
    mission.targetX=c.player->map->normalizeX(target->posX+target->type->width/2);
    mission.targetY=c.player->map->normalizeY(target->posY+target->type->height/2);
    mission.lastTargetHp=target->hp;
    mission.lastProgressTick=a.timer-a.strategy.tactics.stall_ticks;
    target->seenByMask|=f.player.team->me;
    a.plan_offense(c);
    assert(a.budget.tactical_target_gid==target->gid); // Same target keeps the flag.
    a.control_offense(c);
    assert(a.attack_target_quarantine_until.count(target->gid));
    a.plan_offense(c);
    assert(a.budget.tactical_kind==Tactics::MissionNone);
    assert(a.offense_diagnostics.rejections["quarantined"]==1);
    a.control_offense(c);
    assert(a.tactical_mission.flagId<0);
    // The fixture flag never actually disappears; release its warriors by hand.
    for(auto unit:army) { unit->attachedBuilding=NULL; unit->activity=Unit::ACT_RANDOM; }
    flag->unitsWorking.clear();
    a.timer=a.attack_target_quarantine_until[target->gid];
    a.plan_offense(c);
    assert(a.budget.tactical_target_gid==target->gid);
}

static void raidsOutscoreDistantBuildings()
{
    Fixture f;
    f.building(10,10,0);
    auto target=f.building(50,50,1);
    for(int i=0;i<4;++i) f.warrior(15+i,15);
    auto& a=*f.ai; auto& c=a.context; c.initialize(); f.remember(target);
    a.strategy.raiding.enabled=true;
    a.tactics.beginObservation(a.timer);
    for(int i=0;i<4;++i)
        a.tactics.observeWorker(Tactics::WorkerSighting(100+i,1,20+i,20,a.timer,true,true,4));
    Tactics::RaidRules rules; rules.width=rules.height=64; rules.tick=a.timer;
    rules.workerWeight=a.strategy.raiding.worker_weight;
    a.tactics.finishObservation(rules);
    a.plan_offense(c);
    assert(a.budget.tactical_kind==Tactics::MissionRaid);
    assert(a.budget.tactical_target_gid==-1 && a.budget.tactical_target_team==1);
    a.control_offense(c);
    assert(a.tactical_mission.kind==Tactics::MissionRaid && a.campaign.state==Maxima::CampaignIdle);
    // Once the workers vanish the flag moves on to the building.
    a.tactics.beginObservation(a.timer);
    a.tactics.finishObservation(rules);
    a.plan_offense(c);
    assert(a.budget.tactical_kind==Tactics::MissionSiege);
}

static void offensiveControlSwitches()
{
    // Identical combat-ready worlds: only the requested switch changes.
    for(bool parent:{false,true}) for(bool siege:{false,true}) for(bool eligible:{false,true})
    {
        Fixture f;
        f.building(10,10,0);
        auto target=f.building(35,30,1);
        if(eligible) for(int i=0;i<4;++i) f.warrior(15+i,15);
        auto& a=*f.ai; auto& c=a.context;
        c.initialize(); f.remember(target);
        a.strategy.tactics.enabled=parent;
        a.strategy.tactics.siege_enabled=siege;
        a.finalize_director_plan(c);
        a.plan_offense(c);
        assert((a.budget.tactical_kind==Tactics::MissionSiege)==(parent && siege && eligible));
        a.control_offense(c);
        assert((a.tactical_mission.kind==Tactics::MissionSiege)==(parent && siege && eligible));
        if(parent && siege && eligible) assert(!c.buildingOrders.empty());
        else assert(c.buildingOrders.empty());
    }
    for(bool enabled:{false,true}) for(bool eligible:{false,true})
    {
        Fixture f;
        f.building(10,10,0); f.building(35,30,1);
        auto& a=*f.ai; auto& c=a.context;
        c.initialize();
        // Explorer campaigns require prestige, trained explorers and a hostile target.
        a.snapshot.prestige=eligible ? 1 : 0;
        a.snapshot.trained_explorers=30;
        a.snapshot.alive_enemies=1;
        a.strategy.explorer_campaign.enabled=enabled;
        a.strategy.emergencies.colony_enabled=false;
        a.target=1;
        auto enemy=f.game.addUnit(35,35,1,WARRIOR,0,0,0,0);
        assert(enemy);
        a.allocate_resources();
        a.finalize_director_plan(c);
        assert(a.budget.explorer_campaign_active==(enabled && eligible));
        c.buildingOrders.clear(); c.managementOrders.clear();
        a.compute_explorer_flag_attack_positioning(c);
        assert(c.buildingOrders.empty()!= (enabled && eligible));
    }
    {
        Fixture f;
        auto flag=f.building(20,20,0,"warflag");
        auto& a=*f.ai; auto& c=a.context; c.initialize();
        a.attack_flags.push_back(f.id(flag));
        a.strategy.tactics.enabled=false;
        a.strategy.explorer_campaign.enabled=false;
        a.allocate_resources();
        a.finalize_director_plan(c);
        a.control_offense(c);
        assert(a.attack_flags.empty());
        bool removed=false;
        for(auto order:c.managementOrders)
            removed|=dynamic_cast<Management::DestroyBuilding*>(order.get())!=nullptr;
        assert(removed);
        assert(!a.budget.explorer_campaign_active);
    }
    std::cout << "offensive controls: tactics/siege eligible, ineligible, parent-disabled; explorer eligible/ineligible; inherited flag removal PASS\n";
}

static void run()
{
    defenseWrapsBuildingOrigins();
    defenseCoverage();
    warriorEligibility();
    untrainedWarriorsFightAtLevelOne();
    minimumForceGate();
    barracksAreFilledBeforeAttacking();
    foodPostureDoesNotVetoCombat();
    sealedTargetOpensRoute();
    offenseFollowsDestroyedTargets();
    stalledTargetIsQuarantined();
    raidsOutscoreDistantBuildings();
    economicRecoveryPreservesMilitaryBudget();
    forbiddenDefenseZones();
    defenseDeadbandPreservesCoverage();
    offensiveControlSwitches();
    std::cout << "combat execution regressions passed\n";
}
}

int main()
{
    GlobalContainer container;
    globalContainer=&container;
    container.runNoX=true;
    container.buildingsTypes.init();

    IntBuildingType::init();

    combat_regressions::run();
}
