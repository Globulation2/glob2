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

static bool assigned(Context& context, int id, int workers)
{
    for(auto order:context.managementOrders) {
        auto assignment=dynamic_cast<Management::AssignWorkers*>(order.get());
        if(assignment && assignment->id==id && assignment->workers==workers) return true;
    }
    return false;
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

static void reliefStaffing()
{
    for(bool muster:{false,true}) for(bool moving:{false,true}) {
        Fixture f;
        f.building(10,10,0);
        f.building(30,30,1);
        auto flag=f.building(20,20,0,"warflag");
        auto& a=*f.ai;
        auto& c=a.context;
        c.initialize();
        std::vector<Unit*> reserve;
        for(int i=0; i<4; ++i) f.attach(f.warrior(20+i,19),flag);
        for(int i=0; i<8; ++i) reserve.push_back(f.warrior(6+i,6));
        auto& mission=a.tactical_mission;
        mission.kind=Tactics::MissionRelief;
        mission.phase=muster ? Tactics::PhaseMuster : Tactics::PhaseTransit;
        mission.flagId=f.id(flag);
        mission.targetTeam=1;
        mission.targetX=30;
        mission.targetY=30;
        mission.requestedForce=4;
        mission.minimumForce=4;
        mission.launchedForce=muster ? 0 : 4;
        mission.startedTick=100;
        mission.lastContactTick=100;
        f.player.team->allies=f.player.team->me|f.game.teams[1]->me;
        f.player.team->enemies=f.game.teams[2]->me;
        a.strategy.teamplay.enabled=true;
        a.strategy.teamplay.defense_enabled=true;
        a.strategy.teamplay.defense_min_force=4;
        a.strategy.teamplay.defense_strength_percent=100;
        // This fixture calls authorization without the normal budget arbitration.
        a.budget.relief_follow_radius=a.strategy.teamplay.defense_follow_radius;
        a.budget.relief_retarget_margin=a.strategy.teamplay.defense_retarget_margin;
        auto unit=flag->unitsWorking.front();
        const int power=unit->getRealAttackStrength()*unit->performance[ATTACK_SPEED];
        Tactics::RaidRules rules;
        rules.width=64;
        rules.height=64;
        const auto plan=[&](int force) {
            a.tactics.beginObservation(a.timer);
            a.tactics.observeThreat(Tactics::ThreatSighting(123,2,31,31,power*force));
            a.tactics.finishObservation(rules);
            a.plan_tactical_authorization(c);
            assert(a.budget.tactical_contact_visible);
            assert(a.budget.tactical_requested_force==force);
        };
        plan(12);
        if(!moving) {
            mission.targetX=a.budget.tactical_target_x;
            mission.targetY=a.budget.tactical_target_y;
        }
        a.control_attacks(c);
        assert(mission.requestedForce==12 && assigned(c,mission.flagId,12));
        assert(mission.phase==Tactics::PhaseTransit || mission.phase==Tactics::PhaseEngage);
        if(muster) assert(mission.phase==Tactics::PhaseTransit); // Urgent relief still launches at its minimum.
        assert(mission.launchedForce==4); // Pending reinforcements are not casualties.
        c.managementOrders.clear();
        for(auto reinforcement:reserve) f.attach(reinforcement,flag);
        a.control_attacks(c);
        assert(mission.launchedForce==12);
        plan(4);
        a.control_attacks(c);
        assert(mission.requestedForce==4 && assigned(c,mission.flagId,4));
        assert(mission.launchedForce==4);
        for(auto reinforcement:reserve) {
            flag->unitsWorking.remove(reinforcement);
            reinforcement->attachedBuilding=NULL;
            reinforcement->activity=Unit::ACT_RANDOM;
        }
        c.managementOrders.clear();
        a.control_attacks(c);
        assert(mission.phase!=Tactics::PhaseWithdraw && mission.phase!=Tactics::PhaseCooldown);
    }
}

static void siegeProgress()
{
    Fixture f;
    f.building(10,10,0);
    for(int i=0;i<4;++i) f.warrior(6+i,6);
    auto first=f.building(30,30,1);
    auto next=f.building(40,40,1);
    auto flag=f.building(32,30,0,"warflag");
    assert(f.game.addUnit(50,50,1,WARRIOR,1,0,0,0));
    auto& a=*f.ai;
    auto& c=a.context;
    c.initialize();
    f.remember(first);
    auto& mission=a.tactical_mission;
    mission.kind=Tactics::MissionSiege;
    mission.phase=Tactics::PhaseMuster;
    mission.flagId=f.id(flag);
    mission.targetTeam=1;
    mission.targetGid=first->gid;
    mission.requestedForce=4;
    mission.minimumForce=4;
    const int flagId=mission.flagId;
    a.attack_flag_targets[flagId]=first->gid;
    a.campaign.target_team=1;
    a.campaign.buildings_destroyed=0;
    a.budget.tactical_target_gid=first->gid;
    a.budget.tactical_target_team=1;
    a.budget.tactical_requested_force=4;
    a.budget.tactical_minimum_force=4;
    a.retarget_tactical_siege(c);
    assert(a.campaign.buildings_destroyed==0); // A remembered/live target earns no credit.
    first->kill();
    f.game.teams[1]->syncStep();
    a.update_reconnaissance(c);
    assert(!a.reconnaissance.opponent(1)->buildings.count(mission.targetGid));
    a.budget.tactical_target_gid=next->gid;
    a.budget.tactical_target_x=42;
    a.budget.tactical_target_y=42;
    a.retarget_tactical_siege(c);
    assert(a.campaign.buildings_destroyed==1);
    assert(a.campaign.last_progress_tick==a.timer);
    assert(a.attack_flag_targets[flagId]==next->gid);
    a.retarget_tactical_siege(c);
    assert(a.campaign.buildings_destroyed==1);
    next->kill();
    f.game.teams[1]->syncStep();
    a.finish_tactical_mission(c,"target_lost_from_recon");
    a.handle_event(c,RuntimeEvent(RuntimeEvent::AttackFinished,flagId));
    assert(a.campaign.buildings_destroyed==2); // Final target still credited by completion.
    a.handle_event(c,RuntimeEvent(RuntimeEvent::AttackFinished,flagId));
    assert(a.campaign.buildings_destroyed==2);
    a.budget.tactical_kind=Tactics::MissionSiege;
    a.budget.tactical_target_team=2;
    a.begin_tactical_mission(c);
    assert(a.campaign.target_team==2 && a.campaign.buildings_destroyed==0);
}

static void rallyConnectivity()
{
    Fixture f;
    auto home=f.building(10,10,0);
    f.building(27,30,0);
    f.islands();
    for(int i=0;i<4;++i) f.warrior(6+i,6)->performance[SWIM]=0;
    auto& c=f.ai->context;
    c.initialize();
    int x=-1,y=-1;
    // The nearer colony is across water; the home building has a land route.
    assert(f.ai->choose_tactical_rally(c,18,30,x,y));
    assert(f.game.map.warpDistSquare(x,y,home->posX,home->posY)<=144);
    assert(f.game.map.getBuilding(x,y)==NOGBID);
    Gradients::GradientInfo route;
    route.add_source(new Gradients::Entities::Position(x,y));
    route.add_obstacle(new Gradients::Entities::Water);
    route.add_obstacle(new Gradients::Entities::AnyResource);
    assert(c.gradients.get_gradient(route).get_height(18,30)>=0);
    home->underAttackTimer=100;
    assert(!f.ai->choose_tactical_rally(c,18,30,x,y));
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
    a.strategy.teamplay.defense_enabled=false;
    Tactics::RaidCandidate raid;
    raid.team=1;
    raid.x=44;
    raid.y=45;
    int route=-1,nearest=-1;
    for(int ability:{ATTACK_SPEED,ATTACK_STRENGTH}) {
        // Neither single upgrade is sufficient for a level-2 tactical flag.
        for(auto unit:swimmers) {
            unit->level[ATTACK_SPEED]=0;
            unit->level[ATTACK_STRENGTH]=0;
            unit->level[ability]=1;
        }
        a.plan_tactical_authorization(c);
        assert(a.budget.tactical_kind==Tactics::MissionNone);
        assert(!a.raid_candidate_safe(c,raid,4,route,nearest));
    }
    for(auto unit:swimmers) {
        unit->level[ATTACK_SPEED]=unit->level[ATTACK_STRENGTH]=1;
        for(int ability:{ATTACK_SPEED,ATTACK_STRENGTH})
            unit->performance[ability]=unit->race->getUnitType(WARRIOR,1)->performance[ability];
    }
    a.plan_tactical_authorization(c);
    assert(a.budget.tactical_kind==Tactics::MissionSiege);
    assert(a.budget.tactical_requested_force==4);
    assert(a.raid_candidate_safe(c,raid,4,route,nearest));
    a.begin_tactical_mission(c);
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
    assert(flag->maxUnitWorking==4 && flag->minLevelToFlag==1);
    flag->updateCallLists();
    for(int step=0; step<33; ++step) flag->subscribeForFlagingStep();
    assert(flag->unitsWorking.size()==4);
    for(auto unit:flag->unitsWorking) assert(unit->performance[SWIM]>0);
    a.tactical_mission.kind=Tactics::MissionRaid;
    assert(a.raid_candidate_safe(c,raid,4,route,nearest)); // Existing raid troops remain available.
}

static void foodAndEconomicPostureDoNotVetoCombat()
{
    Fixture f;
    f.building(10,10,0);
    auto target=f.building(35,30,1);
    auto flag=f.building(20,20,0,"warflag");
    for(int i=0;i<4;++i) f.warrior(15+i,15);
    auto& a=*f.ai; auto& c=a.context; c.initialize(); f.remember(target);
    a.snapshot.population=100;
    a.snapshot.workers=1;
    a.snapshot.critical_food=10;
    a.snapshot.unserved_food=0;
    a.trends.population=-100;
    a.trends.food_pressure=100;
    a.strategy.emergencies.food_enabled=true;
    a.strategy.emergencies.food_critical_percent=20;
    assert(!a.severe_food_emergency()); // Trends alone are never a food emergency.
    a.snapshot.critical_food=20;
    assert(a.severe_food_emergency());
    a.budget.food_emergency=true;
    for(auto posture:{Maxima::PostureExpand,Maxima::PostureDevelop,
            Maxima::PostureRecover,Maxima::PostureDefend,Maxima::PostureCampaign}) {
        a.posture=posture;
        a.plan_tactical_authorization(c);
        assert(a.budget.tactical_kind==Tactics::MissionSiege);
        assert(a.budget.tactical_requested_force==4);
    }
    for(int i=0;i<4;++i) f.attach(f.warrior(19+i%2,19+i/2),flag);
    auto& mission=a.tactical_mission;
    mission.kind=Tactics::MissionSiege; mission.phase=Tactics::PhaseMuster;
    mission.flagId=f.id(flag); mission.targetTeam=1; mission.targetGid=target->gid;
    mission.requestedForce=mission.minimumForce=4;
    mission.targetX=35; mission.targetY=30;
    mission.phaseSinceTick=a.timer;
    a.budget.tactics_enabled=a.budget.siege_enabled=true;
    a.budget.tactical_siege_muster_percent=80;
    a.control_attacks(c);
    assert(mission.phase==Tactics::PhaseTransit); // Even severe hunger permits launch.
    a.budget.colony_emergency=true;
    a.control_attacks(c);
    assert(mission.phase==Tactics::PhaseCooldown); // Actual defense emergencies still act.
}

static void eliminatedCampaignTargetDoesNotBlockSurvivors()
{
    for(bool sealed:{false,true}) for(bool oldTargetAlive:{false,true}) {
        Fixture f; f.building(10,10,0);
        auto oldTarget=f.building(12,40,1);
        auto survivor=f.building(35,30,2);
        for(int i=0;i<12;++i) f.warrior(6+i,6)->performance[SWIM]=0;
        if(sealed) f.islands();
        auto& a=*f.ai; auto& c=a.context;
        c.initialize(); f.remember(oldTarget); f.remember(survivor);
        if(oldTargetAlive) {
            a.reconnaissance.beginObservation(a.timer,{1,2});
            a.reconnaissance.finishObservation();
        }
        assert(a.reconnaissance.opponent(1)->alive==oldTargetAlive);
        a.campaign.target_team=1;
        a.campaign.buildings_destroyed=1;
        a.campaign.last_progress_tick=a.timer;
        a.strategy.tactics.siege_target_lock_enabled=true;
        a.strategy.tactics.dig_out_enabled=true;
        a.budget.attack_flags=1; a.budget.attack_clearing_workers=10;
        a.opponents[2].alive=true; a.opponents[2].known_buildings=1;
        a.opponents[2].reachable_buildings=sealed ? 0 : 1;
        a.opponents[2].score=100;
        a.plan_tactical_authorization(c);
        if(oldTargetAlive) {
            assert(a.budget.tactical_kind==Tactics::MissionSiege);
            assert(a.budget.tactical_target_team==1); // A live progress lock still works.
        } else if(sealed) {
            assert(a.budget.tactical_kind==Tactics::MissionNone);
            assert(a.budget.tactical_dig_out_team==2); // No 15,000-tick wait to clear a route.
        } else {
            assert(a.budget.tactical_kind==Tactics::MissionSiege);
            assert(a.budget.tactical_target_team==2); // Immediately consider the survivor.
        }
    }
}

static void siegeUsesConfiguredQuorum()
{
    for(int percent:{50,75}) for(bool deadline:{false,true}) {
        Fixture f; f.building(10,10,0);
        auto target=f.building(35,30,1);
        auto flag=f.building(20,20,0,"warflag");
        auto& a=*f.ai; auto& c=a.context;
        c.initialize(); f.remember(target);
        a.strategy.tactics.siege_muster_percent=percent;
        a.allocate_resources();
        a.finalize_director_plan(c);
        assert(a.budget.tactical_siege_muster_percent==percent);
        a.budget.tactics_enabled=a.budget.siege_enabled=true;
        a.budget.colony_emergency=false;
        a.budget.campaign_stall_ticks=10000;
        auto& mission=a.tactical_mission;
        mission.kind=Tactics::MissionSiege; mission.phase=Tactics::PhaseMuster;
        mission.flagId=f.id(flag); mission.targetTeam=1; mission.targetGid=target->gid;
        mission.requestedForce=mission.minimumForce=11;
        mission.targetX=35; mission.targetY=30; mission.phaseSinceTick=a.timer;
        mission.lastProgressTick=a.timer;
        const int quorum=(11*percent+99)/100;
        for(int i=0;i<quorum-1;++i) f.attach(f.warrior(18+i%3,18+i/3),flag);
        a.control_attacks(c);
        assert(mission.phase==Tactics::PhaseMuster);
        if(deadline) {
            a.timer+=a.budget.raid_muster_timeout*2;
            a.control_attacks(c);
            assert(mission.phase==Tactics::PhaseCooldown); // No deadline bypass.
        } else {
            f.attach(f.warrior(21,21),flag);
            a.control_attacks(c);
            assert(mission.phase==Tactics::PhaseTransit); // Below the old minimum of 11.
            assert(mission.launchedForce==quorum);
            a.control_attacks(c);
            assert(mission.phase==Tactics::PhaseEngage); // No later minimum override.
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

static void hostileTowers()
{
    Fixture f;
    f.building(10,10,0);
    auto target=f.building(45,45,1);
    auto tower=f.building(25,25,2,"defencetower",2);
    auto& a=*f.ai;
    auto& c=a.context;
    c.initialize();
    a.reconnaissance.beginObservation(a.timer,{1,2});
    for(auto building:{target,tower})
        a.reconnaissance.observeBuilding(Recon::BuildingSighting(building->gid,
            building->owner->teamNumber,building->type->shortTypeNum,
            building->posX,building->posY,building->type->width,
            building->type->height,false,a.timer));
    a.reconnaissance.finishObservation();
    Tactics::RaidCandidate raid;
    raid.team=1;
    raid.x=24;
    raid.y=25;
    int route=-1,nearest=-1;
    assert(!a.raid_candidate_safe(c,raid,6,route,nearest));
    // Current diplomacy, not the fact that intel was recorded as an enemy,
    // controls whether this tower is dangerous.
    f.player.team->enemies&=~f.game.teams[2]->me;
    f.player.team->allies|=f.game.teams[2]->me;
    assert(a.raid_candidate_safe(c,raid,6,route,nearest));
    f.player.team->enemies|=f.game.teams[2]->me;
    f.player.team->allies&=~f.game.teams[2]->me;
    raid.x=15;
    raid.y=15;
    assert(a.raid_candidate_safe(c,raid,6,route,nearest));
}

static void raidLaunchRequiresSafeContact()
{
    for(bool visible:{false,true}) for(bool restored:{false,true}) {
        Fixture f;
        f.building(10,10,0);
        auto enemy=f.building(45,45,1);
        auto flag=f.building(10,10,0,"warflag");
        flag->unitStayRange=2;
        auto& a=*f.ai; auto& c=a.context;
        c.initialize(); f.remember(enemy);
        for(int i=0;i<4;++i) f.attach(f.warrior(9+i,9),flag);
        auto& mission=a.tactical_mission;
        mission.kind=Tactics::MissionRaid;
        mission.phase=Tactics::PhaseMuster;
        mission.flagId=f.id(flag); mission.targetTeam=1;
        mission.targetX=30; mission.targetY=30;
        mission.requestedForce=mission.minimumForce=4;
        mission.startedTick=mission.phaseSinceTick=mission.lastContactTick=100;
        a.budget.raid_muster_timeout=50;
        a.budget.raid_contact_ttl=300;
        const auto observe=[&](bool workers, bool defenders) {
            a.tactics.beginObservation(a.timer);
            if(workers) for(int i=0;i<3;++i)
                a.tactics.observeWorker(Tactics::WorkerSighting(
                    100+i,1,30+i,30,a.timer,true,true,1));
            if(defenders) for(int i=0;i<2;++i)
                a.tactics.observeThreat(Tactics::ThreatSighting(200+i,1,30+i,31,100));
            Tactics::RaidRules rules;
            rules.width=rules.height=64; rules.tick=a.timer;
            a.tactics.finishObservation(rules);
        };
        a.timer=200; observe(visible,true);
        a.control_attacks(c);
        // Even a full, timed-out muster waits while contact is lost or unsafe.
        assert(mission.phase==Tactics::PhaseMuster);
        for(auto order:c.managementOrders)
            assert(!dynamic_cast<Management::ChangeFlagPosition*>(order.get()));
        if(restored) {
            a.timer=250; observe(true,false);
            a.control_attacks(c);
            assert(mission.phase==Tactics::PhaseTransit);
        } else {
            a.timer=400; observe(visible,true);
            a.control_attacks(c);
            assert(mission.phase==Tactics::PhaseCooldown);
        }
    }
}

static void amphibiousRallyEnrollment()
{
    for(int radius:{2,4}) for(int shift:{0,40}) {
        Fixture f;
        const auto wrap=[&](int value) { return (value+shift)%64; };
        f.building(wrap(10),wrap(10),0);
        for(int y=0;y<64;++y) for(int x=0;x<64;++x) {
            const bool land=y>=5&&y<=55&&((x>=5&&x<=19)||(x>=21&&x<=45));
            f.game.map.setTerrain(wrap(x),wrap(y),land?0:256);
        }
        auto& a=*f.ai; auto& c=a.context;
        c.initialize(); a.budget.tactical_rally_radius=radius;
        std::vector<Unit*> walkers,swimmers;
        for(int i=0;i<4;++i) {
            auto walker=f.warrior(wrap(6+i),wrap(30),3);
            walker->performance[SWIM]=0; walkers.push_back(walker);
            auto swimmer=f.warrior(wrap(6+i),wrap(31),1);
            swimmer->performance[SWIM]=1; swimmers.push_back(swimmer);
        }
        a.budget.tactical_requested_force=4;
        int x=-1,y=-1;
        assert(a.choose_tactical_rally(c,wrap(29),wrap(30),x,y));
        assert(f.game.map.isWater(x,y)); // Assembly happens offshore by our base.
        assert(f.game.map.warpDistSquare(x,y,wrap(10),wrap(10))<=144);
        auto flag=f.building(x,y,0,"warflag");
        flag->unitStayRange=radius; flag->minLevelToFlag=1;
        flag->maxUnitWorking=4;
        for(int i=0;i<4;++i) {
            // Stronger non-swimmers would be recruited first if any part of
            // the rally radius reached their shore.
            auto walker=walkers[i];
            auto swimmer=swimmers[i];
            int distance=-1;
            assert(!f.game.map.buildingAvailable(flag,false,
                walker->posX,walker->posY,&distance));
            assert(f.game.map.buildingAvailable(flag,true,
                swimmer->posX,swimmer->posY,&distance));
        }
        flag->updateCallLists();
        for(int step=0;step<33;++step) flag->subscribeForFlagingStep();
        assert(flag->unitsWorking.size()==4);
        for(auto unit:flag->unitsWorking) assert(unit->performance[SWIM]>0);
    }
}

static void reachableMissionPower()
{
    for(bool relief:{false,true}) for(int reachableCount:{7,8}) {
        Fixture f;
        f.building(10,10,0);
        auto target=f.building(10,40,1);
        f.islands();
        for(int i=0;i<reachableCount;++i) f.warrior(6+i,6,1)->performance[SWIM]=0;
        for(int i=0;i<4;++i) f.warrior(30+i,6,3)->performance[SWIM]=0;
        auto& a=*f.ai; auto& c=a.context;
        c.initialize(); f.remember(target);
        a.snapshot.trained_warriors=reachableCount+4;
        a.opponents[1].score=a.opponents[2].score=0;
        a.strategy.tactics.siege_enabled=!relief;
        a.strategy.teamplay.enabled=true;
        a.strategy.teamplay.defense_enabled=relief;
        a.strategy.teamplay.defense_min_force=4;
        a.strategy.teamplay.defense_strength_percent=125;
        a.strategy.tactics.siege_strength_percent=125;
        if(relief) {
            f.player.team->allies=f.player.team->me|f.game.teams[1]->me;
            f.player.team->enemies=f.game.teams[2]->me;
        }
        auto unit=f.player.team->myUnits[0];
        const int power=unit->getRealAttackStrength()*unit->performance[ATTACK_SPEED];
        a.tactics.beginObservation(a.timer);
        a.tactics.observeThreat(Tactics::ThreatSighting(500,relief?2:1,12,40,power*6));
        Tactics::RaidRules rules; rules.width=rules.height=64;
        a.tactics.finishObservation(rules);
        a.plan_tactical_authorization(c);
        if(reachableCount==7) {
            assert(a.budget.tactical_kind==Tactics::MissionNone);
            if(!relief) {
            }
            continue;
        }
        assert(a.budget.tactical_kind==(relief?Tactics::MissionRelief:Tactics::MissionSiege));
        assert(a.budget.tactical_requested_force==8);
        int x,y;
        assert(a.choose_tactical_rally(c,a.budget.tactical_target_x,
            a.budget.tactical_target_y,x,y));
        auto flag=f.building(x,y,0,"warflag");
        flag->unitStayRange=a.budget.tactical_rally_radius; flag->minLevelToFlag=1;
        flag->maxUnitWorking=a.budget.tactical_requested_force;
        flag->updateCallLists();
        for(int step=0;step<33;++step) flag->subscribeForFlagingStep();
        int enrolledPower=0;
        for(auto member:flag->unitsWorking)
            enrolledPower+=member->getRealAttackStrength()*member->performance[ATTACK_SPEED];
        assert(flag->unitsWorking.size()==8);
        assert(enrolledPower>0);
    }
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
                f.game.map.setForbidden(x,y,f.player.team->me);
        a.update_preemptive_defense(c);
        assert(a.preemptive_guard_tiles.empty());
        for(int y=20;y<=27;++y) for(int x=16;x<=38;++x)
            f.game.map.setForbidden(x,y,0);
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
        assert(a.budget.tactics_enabled==parent && a.budget.siege_enabled==siege);
        a.plan_tactical_authorization(c);
        assert((a.budget.tactical_kind==Tactics::MissionSiege)==(parent && siege && eligible));
        a.control_attacks(c);
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
        assert(!a.budget.tactics_enabled);
        a.control_attacks(c);
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
    defenseCoverage();
    reliefStaffing();
    siegeProgress();
    rallyConnectivity();
    warriorEligibility();
    foodAndEconomicPostureDoNotVetoCombat();
    eliminatedCampaignTargetDoesNotBlockSurvivors();
    siegeUsesConfiguredQuorum();
    economicRecoveryPreservesMilitaryBudget();
    hostileTowers();
    raidLaunchRequiresSafeContact();
    amphibiousRallyEnrollment();
    reachableMissionPower();
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
