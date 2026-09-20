// Link with the game objects (excluding Glob2.cpp) to exercise the real runtime.
#include "../../src/GlobalContainer.h"
#include "../../src/Version.h"
#include "../../src/Game.h"
#include "../../src/team/Team.h"
#include "../../src/ai/AIImplementation.h"
#include "../../src/map/Map.h"
#include "../../src/Order.h"
#include "../../src/ai/maxima/AIMaximaContinuation.h"
#include "../../src/Player.h"
#include "../../src/Utilities.h"
#include "../../src/TeamStat.h"
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
#include "../../src/ai/maxima/AIMaximaRuntime.h"
#include "../../src/ai/maxima/AIMaxima.h"
#undef private
#include "../../src/building/Building.h"
#include "../../src/game/entities/BuildingType.h"
#include "../../src/building/IntBuildingType.h"
#include "../../src/unit/Unit.h"
#include <BinaryStream.h>
#include <StreamBackend.h>
#include <cassert>
#include <cstdlib>
#include <fstream>
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
        // This fixture skips setGameHeader, which normally initializes the
        // player-wait state. syncStep must actually advance the simulation.
        game.setWaitingOnMask(0);
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

// Place synthetic policy-test arrivals on distinct legal cells and keep map
// occupancy consistent. Real movement to the rally is tested separately below.
static void placeAtRally(Fixture& f, ::Building* flag, const std::vector<Unit*>& units, int count)
{
    for(int i=0;i<count;++i) {
        auto* unit=units[i];
        f.game.map.setGroundUnit(unit->posX,unit->posY,NOGUID);
        bool placed=false;
        for(int dy=-flag->unitStayRange;dy<=flag->unitStayRange && !placed;++dy)
            for(int dx=-flag->unitStayRange;dx<=flag->unitStayRange && !placed;++dx) {
                if(dx*dx+dy*dy>flag->unitStayRange*flag->unitStayRange)continue;
                const int x=f.game.map.normalizeX(flag->posX+dx),y=f.game.map.normalizeY(flag->posY+dy);
                if(!f.game.map.isFreeForGroundUnit(x,y,false,f.player.team->me))continue;
                unit->posX=x;unit->posY=y;
                f.game.map.setGroundUnit(x,y,unit->gid);placed=true;
            }
        assert(placed);
    }
}

static void nearbyWarriorsCountAsRallied()
{
    for(bool withinTolerance:{false,true}) {
        Fixture f;
        f.building(10,10,0);
        auto target=f.building(35,30,1);
        std::vector<Unit*> warriors;
        for(int i=0;i<20;++i)warriors.push_back(f.warrior(i%5,i/5,3));
        auto& a=*f.ai;auto& c=a.context;c.initialize();f.remember(target);
        a.plan_offense(c);a.control_offense(c);
        auto* flag=materializeFlag(f);
        for(auto* warrior:warriors)f.attach(warrior,flag);
        const int inner=flag->unitStayRange+(withinTolerance?0:2),outer=inner+2;
        int placed=0;
        for(int dy=-outer;dy<=outer && placed<15;++dy)
            for(int dx=-outer;dx<=outer && placed<15;++dx) {
                if(dx*dx+dy*dy<=inner*inner || dx*dx+dy*dy>outer*outer)continue;
                const int x=f.game.map.normalizeX(flag->posX+dx),y=f.game.map.normalizeY(flag->posY+dy);
                if(!f.game.map.isFreeForGroundUnit(x,y,false,f.player.team->me))continue;
                auto* unit=warriors[placed++];
                f.game.map.setGroundUnit(unit->posX,unit->posY,NOGUID);
                unit->posX=x;unit->posY=y;f.game.map.setGroundUnit(x,y,unit->gid);
            }
        assert(placed==15);
        a.timer+=100;
        a.plan_offense(c);a.control_offense(c);
        assert((a.offense_waves[0].phase==Tactics::WaveAdvance)==withinTolerance);
    }
}

static void rallyRejectsBlockedGround()
{
    Fixture f;
    f.building(10,10,0);
    auto target=f.building(35,30,1);
    for(int i=0;i<20;++i)f.warrior(i%5,i/5,3);
    auto& a=*f.ai;auto& c=a.context;c.initialize();f.remember(target);
    for(int y=4;y<22;++y)for(int x=4;x<22;++x)f.game.map.addForbidden(x,y,0);
    // A reachable nine-tile pocket is still too small for the wave.
    for(int y=7;y<10;++y)for(int x=7;x<10;++x)f.game.map.removeForbidden(x,y,0);
    a.plan_offense(c);a.control_offense(c);
    assert(a.offense_waves.empty());
    // No room to muster must not fall through to an ungathered attack flag.
    assert(c.buildingOrders.empty());
    f.building(40,40,0);
    c.buildings.tick();
    a.plan_offense(c);a.control_offense(c);
    assert(a.offense_waves.size()==1);
    auto* flag=materializeFlag(f);
    assert(f.game.map.warpDistMax(flag->posX,flag->posY,40,40)<=10);
    assert(f.game.map.getBuilding(flag->posX,flag->posY)==NOGBID);
    const int oldX=flag->posX,oldY=flag->posY;
    f.building(oldX,oldY,0);
    a.timer+=100;
    a.plan_offense(c);a.control_offense(c);
    bool moved=false;
    for(auto order:c.managementOrders)
        if(auto move=dynamic_cast<Management::ChangeFlagPosition*>(order.get())) {
            assert(f.game.map.getBuilding(move->x,move->y)==NOGBID);
            assert(!f.game.map.isForbidden(move->x,move->y,f.player.team->me));
            moved=true;
        }
    assert(moved && a.offense_waves[0].phase==Tactics::WaveMuster);
}

static void rallyAssemblesByMovement()
{
    const char* tracePath="test/maxima/fixtures/rally-movement-checksums.txt";
    const bool record=std::getenv("GLOB2_RECORD_RALLY_CHECKSUMS")!=nullptr;
    std::ifstream expected;
    std::ofstream output;
    if(record)output.open(tracePath);
    else expected.open(tracePath);
    assert(record ? output.good() : expected.good());
    for(int shift:{0,52}) {
        setSyncRandSeed(5489);
        Fixture f;
        const auto wrap=[&](int v){return (v+shift)%64;};
        f.game.gameHeader.setHungerDisabled(true);
        f.building(wrap(10),wrap(10),0);
        auto target=f.building(wrap(35),wrap(30),1);
        std::vector<Unit*> warriors;
        for(int i=0;i<20;++i)warriors.push_back(f.warrior(wrap(2+i%5),wrap(2+i/5),3));
        auto& a=*f.ai;auto& c=a.context;c.initialize();f.remember(target);
        a.plan_offense(c);a.control_offense(c);
        assert(a.offense_waves.size()==1);
        auto* flag=materializeFlag(f);
        assert(f.game.map.getBuilding(flag->posX,flag->posY)==NOGBID);
        assert(flag->unitStayRange==4);
        for(auto* warrior:warriors) {
            warrior->subscriptionSuccess(flag,false);
            flag->unitsWorking.push_back(warrior);
        }
        // Readiness includes the two-tile margin: crowding at the flag edge
        // must not make this movement test demand more than the launch policy.
        const int arrivalRadius=flag->unitStayRange+2;
        int arrived=0;
        for(int tick=0;tick<2000 && arrived<15;++tick) {
            const auto previousStep=f.game.stepCounter;
            f.game.syncStep(0);
            assert(f.game.stepCounter==previousStep+1);
            const Uint32 checksum=f.game.checkSum(nullptr,nullptr,nullptr,true);
            if(record)output << shift << ' ' << f.game.stepCounter << ' ' << checksum << '\n';
            else {
                int expectedShift;
                Uint32 expectedStep,expectedChecksum;
                assert(expected >> expectedShift >> expectedStep >> expectedChecksum);
                if(expectedShift!=shift || expectedStep!=f.game.stepCounter || expectedChecksum!=checksum)
                    std::cerr << "Rally checksum mismatch: shift=" << shift
                        << " step=" << f.game.stepCounter << " actual=" << checksum
                        << " expected=" << expectedChecksum << '\n';
                assert(expectedShift==shift && expectedStep==f.game.stepCounter && expectedChecksum==checksum);
            }
            arrived=0;
            for(auto* warrior:warriors)
                arrived+=f.game.map.warpDistSquare(warrior->posX,warrior->posY,flag->posX,flag->posY)
                    <=arrivalRadius*arrivalRadius;
        }
        if(arrived<15)
            std::cerr << "Rally movement: shift=" << shift << " arrived=" << arrived
                << " radius=" << arrivalRadius << '\n';
        assert(arrived>=15);
        std::set<std::pair<int,int>> occupied;
        for(auto* warrior:warriors) {
            assert(f.game.map.getBuilding(warrior->posX,warrior->posY)==NOGBID);
            assert(occupied.insert({warrior->posX,warrior->posY}).second);
        }
        a.timer+=100;
        a.plan_offense(c);a.control_offense(c);
        assert(a.offense_waves[0].phase==Tactics::WaveAdvance);
    }
    if(!record) {
        expected >> std::ws;
        assert(expected.eof());
    }
}

static void waveAssemblyAndPipeline()
{
    Fixture f;
    f.building(10,10,0);
    auto target=f.building(35,30,1);
    std::vector<Unit*> warriors;
    for(int i=0;i<40;++i)warriors.push_back(f.warrior(i%10,i/10,3));
    auto& a=*f.ai;auto& c=a.context;c.initialize();f.remember(target);
    a.director.dirty=false;
    a.budget.tactical_kind=Tactics::MissionNone;
    a.control_offense(c);
    assert(!a.director.dirty); // Idle wave reviews must not reschedule the economy.
    a.plan_offense(c);
    assert(a.budget.tactical_requested_force==40);
    a.control_offense(c);
    assert(a.offense_waves.size()==1);
    assert(a.offense_waves[0].requestedForce==20);
    const int first=a.offense_waves[0].flagId;
    auto flag=materializeFlag(f);
    assert(f.game.map.getBuilding(flag->posX,flag->posY)==NOGBID);
    assert(f.game.map.warpDistMax(flag->posX,flag->posY,10,10)<=10);
    for(int i=0;i<20;++i)f.attach(warriors[i],flag);
    a.timer+=100;
    a.plan_offense(c);a.control_offense(c);
    // Enrollment alone must not release the cohort; nobody has arrived.
    assert(a.offense_waves.size()==1);
    assert(a.offense_waves[0].phase==Tactics::WaveMuster);
    placeAtRally(f,flag,warriors,15);
    a.timer+=100;
    a.plan_offense(c);a.control_offense(c);
    assert(a.offense_waves.size()==2);
    assert(a.offense_waves[0].phase==Tactics::WaveAdvance);
    assert(a.offense_waves[1].phase==Tactics::WaveMuster);
    bool moved=false,lowPriority=false;
    for(auto order:c.managementOrders) {
        if(auto move=dynamic_cast<Management::ChangeFlagPosition*>(order.get()))
            moved=moved || (move->id==first && move->x==a.budget.tactical_target_x);
        if(auto priority=dynamic_cast<Management::ChangePriority*>(order.get()))
            lowPriority=lowPriority || (priority->id==first && priority->priority==-1);
    }
    assert(moved && lowPriority);
    // Save with one advancing and one pending muster flag. Restore the real
    // Maxima execution queue and verify all wave fields, including clocks.
    auto* saved=new GAGCore::MemoryStreamBackend;
    GAGCore::BinaryOutputStream output(saved);
    a.save(&output);
    const std::string bytes(saved->getBuffer(),saved->getPosition());
    GAGCore::BinaryInputStream input(new GAGCore::MemoryStreamBackend(bytes.data(),bytes.size()));
    input.seekFromStart(0);
    Maxima restored(&input,&f.player,VERSION_MINOR);
    const auto waveBytes=[](const std::vector<Tactics::Wave>& waves) {
        auto* storage=new GAGCore::MemoryStreamBackend;
        GAGCore::BinaryOutputStream stream(storage);
        AIMaximaContinuation::Writer archive(&stream);
        archive("waves",waves);
        return std::string(storage->getBuffer(),storage->getPosition());
    };
    assert(waveBytes(restored.offense_waves)==waveBytes(a.offense_waves));
    assert(restored.context.get_building_register().is_building_pending(a.offense_waves[1].flagId));
    a.timer+=100;restored.timer+=100;
    a.control_offense(c);restored.control_offense(restored.context);
    assert(waveBytes(restored.offense_waves)==waveBytes(a.offense_waves));
    assert(restored.context.managementOrders.size()==c.managementOrders.size());
    // A flag lifecycle notification cannot discard another live cohort.
    a.handle_event(c,RuntimeEvent(RuntimeEvent::AttackFinished,first));
    assert(a.offense_waves.size()==1);
    assert(a.tactical_mission.flagId==a.offense_waves[0].flagId);
    a.end_offense(c,"test");
    assert(a.offense_waves.empty() && a.attack_flags.empty());
}

static void smallWaveKeepsRecruiting()
{
    Fixture f;
    f.building(10,10,0);
    auto target=f.building(35,30,1);
    std::vector<Unit*> warriors;
    for(int i=0;i<4;++i)warriors.push_back(f.warrior(i,0,3));
    auto& a=*f.ai;auto& c=a.context;c.initialize();f.remember(target);
    a.plan_offense(c);a.control_offense(c);
    assert(a.offense_waves.size()==1 && a.offense_waves[0].requestedForce==4);
    auto flag=materializeFlag(f);
    for(auto warrior:warriors)f.attach(warrior,flag);
    placeAtRally(f,flag,warriors,4);
    a.timer+=100;
    a.plan_offense(c);a.control_offense(c);
    // Four of four present is not a full wave: allow time for the army to grow.
    assert(a.offense_waves[0].phase==Tactics::WaveMuster);
    for(int i=4;i<20;++i)warriors.push_back(f.warrior(20+i%10,20+i/10,3));
    a.timer+=100;
    a.plan_offense(c);a.control_offense(c);
    assert(a.offense_waves[0].requestedForce==20);
    assert(assigned(c,a.offense_waves[0].flagId,20));
    for(int i=4;i<20;++i)f.attach(warriors[i],flag);
    a.timer+=a.strategy.assault.muster_stall_ticks;
    a.plan_offense(c);a.control_offense(c);
    // A timeout cannot launch four arrived warriors with sixteen stragglers.
    assert(a.offense_waves[0].phase==Tactics::WaveMuster);
    placeAtRally(f,flag,warriors,15);
    a.timer+=100;
    a.plan_offense(c);a.control_offense(c);
    assert(a.offense_waves[0].phase==Tactics::WaveAdvance);
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

static void loweredFlagLevelSurvivesReview()
{
    Fixture f;
    f.building(10,10,0);
    auto target=f.building(35,30,1);
    for(int i=0;i<8;++i) f.warrior(15+i,15,0);
    auto& a=*f.ai; auto& c=a.context; c.initialize(); f.remember(target);
    a.strategy.tactics.flag_minimum_level=2;
    a.opponents[1].alive=true;
    a.opponents[1].estimated_warriors=1;
    a.plan_offense(c);
    assert(a.budget.tactical_kind==Tactics::MissionSiege);
    assert(a.budget.tactical_flag_level==1);
    a.control_offense(c);
    const int flagId=a.tactical_mission.flagId;
    assert(flagId>=0);
    // A director refresh must not reset the level while creation is pending.
    a.finalize_director_plan(c);
    a.plan_offense(c);
    assert(a.budget.tactical_flag_level==1);
    assert(a.budget.tactical_target_gid==target->gid);
    auto flag=materializeFlag(f);
    assert(flag->minLevelToFlag==0);
    flag->updateCallLists();
    for(int step=0;step<33;++step) flag->subscribeForFlagingStep();
    assert(!flag->unitsWorking.empty());
    // Even a stale budget must defer to the actual flag after creation.
    a.budget.tactical_flag_level=2;
    a.plan_offense(c);
    assert(a.budget.tactical_flag_level==1);
    assert(a.budget.tactical_target_gid==target->gid);
    a.control_offense(c);
    assert(a.tactical_mission.flagId==flagId);
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
        auto barracks=f.building(14,10,0,"barracks",1);
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

static void unusableTrainingDoesNotBlockAttacks()
{
    for(int untrained:{0,2,4}) {
        Fixture f;f.building(10,10,0);
        auto target=f.building(35,30,1);
        auto low=f.building(14,10,0,"barracks");low->maxUnitInside=4;
        auto high=f.building(14,30,0,"barracks",1);high->maxUnitInside=2;
        for(int i=0;i<4;++i)f.warrior(20+i,20,3);
        for(int i=0;i<untrained;++i)f.warrior(20+i,22,1);
        auto& a=*f.ai;auto& c=a.context;c.initialize();f.remember(target);
        a.plan_offense(c);
        assert(a.offense_diagnostics.openTrainingSlots==std::min(2,untrained));
        assert(a.budget.tactical_kind==Tactics::MissionSiege);
        assert(a.budget.tactical_requested_force==4+untrained-std::min(2,untrained));
    }
}

static void trainingReservationsMatchDifferentLevels()
{
    Fixture f;f.building(10,10,0);
    auto target=f.building(35,30,1);
    // The first trainee fits either school of combat; the second only fits
    // the higher level. Reassigning the first must preserve both reservations.
    auto high=f.building(14,10,0,"barracks",1);high->maxUnitInside=1;
    auto low=f.building(14,30,0,"barracks");low->maxUnitInside=1;
    f.warrior(20,20,0);f.warrior(21,20,1);
    for(int i=0;i<4;++i)f.warrior(22+i,20,3);
    auto& a=*f.ai;auto& c=a.context;c.initialize();f.remember(target);
    a.strategy.tactics.flag_minimum_level=1;
    a.plan_offense(c);
    assert(a.offense_diagnostics.openTrainingSlots==2);
    assert(a.budget.tactical_requested_force==4);
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
    f.islands();
    for(int i=0;i<4;++i) {
        auto unit=f.warrior(6+i,6);
        unit->level[SWIM]=1;
        unit->performance[SWIM]=1;
    }
    auto first=f.building(30,30,1);
    auto next=f.building(40,40,1);
    auto& a=*f.ai; auto& c=a.context; c.initialize();
    a.snapshot.swimming_warriors=4;
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
    Tactics::Wave wave;
    wave.flagId=mission.flagId; wave.phase=Tactics::WaveAdvance;
    wave.requestedForce=4; wave.targetX=mission.targetX; wave.targetY=mission.targetY;
    wave.progressTick=a.timer;
    a.offense_waves.push_back(wave);
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
    assert(a.tactical_mission.kind==Tactics::MissionRaid && a.campaign.state==Maxima::CampaignActive);
    assert(a.offense_waves.size()==1);
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
        a.tactical_mission.flagId=f.id(flag);
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

static void fittedForceUsesOnlyVisibleUnits()
{
    Fixture f;
    f.building(10,10,0);f.building(35,30,1);
    auto& a=*f.ai;auto& c=a.context;c.initialize();
    f.game.map.unsetMapDiscovered();
    std::fill(f.game.map.fogOfWarA.begin(),f.game.map.fogOfWarA.end(),0);
    std::fill(f.game.map.fogOfWarB.begin(),f.game.map.fogOfWarB.end(),0);
    Unit* seen=f.game.addUnit(30,30,1,WARRIOR,1,0,0,0);
    assert(seen);f.game.map.setMapDiscovered(30,30,f.player.team->me);
    a.update_reconnaissance(c);
    assert(a.force_beliefs.count(1));
    const auto baseline=a.force_beliefs.at(1);
    assert(baseline.features[ForceModel::VisibleWarriors]==1);
    // Adding hidden forces cannot affect any input or prediction.
    assert(f.game.addUnit(50,50,1,WARRIOR,3,0,0,0));
    assert(f.game.addUnit(51,50,1,WORKER,0,0,0,0));
    a.force_beliefs.clear();a.update_reconnaissance(c);
    const auto& hidden=a.force_beliefs.at(1);
    for(int i=0;i<ForceModel::FeatureCount;++i) assert(hidden.features[i]==baseline.features[i]);
    for(int t=0;t<ForceModel::TargetCount;++t)
        for(int q=0;q<ForceModel::QuantileCount;++q)
            assert(hidden.prediction.values[t][q]==baseline.prediction.values[t][q]);
    assert(a.reconnaissance.opponent(1)->estimatedWarriors==hidden.prediction.rounded(ForceModel::Warriors));
    a.sample_reconnaissance_forces(c);
    assert(a.reconnaissance.opponent(1)->estimatedWarriors==hidden.prediction.rounded(ForceModel::Warriors));
    a.update_opponent_models(c);
    assert(a.opponents[1].estimated_warriors==a.reconnaissance.opponent(1)->estimatedWarriors);
    a.strategy.reconnaissance.force_memory_enabled=false;
    a.reconnaissance.configure(10000,2500,2500,false);
    a.sample_reconnaissance_forces(c);
    assert(a.reconnaissance.opponent(1)->estimatedWarriors==1);
}

static void fittedHistorySurvivesSave()
{
    Fixture f;f.building(10,10,0);f.building(35,30,1);
    auto& a=*f.ai;a.context.initialize();
    int64_t observation[ForceModel::ObservationFeatures]={12,20,0,0,0,2,40,800,9,3,70,40,1000};
    auto& before=a.force_beliefs[1];before.observe(observation);
    observation[ForceModel::Tick]=2200;observation[ForceModel::VisibleWarriors]=9;
    before.observe(observation);before.forecast(2400);a.timer=2400;
    auto* storage=new GAGCore::MemoryStreamBackend;
    GAGCore::BinaryOutputStream output(storage);a.save(&output);
    const std::string bytes(storage->getBuffer(),storage->getPosition());
    GAGCore::BinaryInputStream input(new GAGCore::MemoryStreamBackend(bytes.data(),bytes.size()));
    input.seekFromStart(0);
    Maxima restored(&f.player);assert(restored.load(&input,&f.player,VERSION_MINOR));
    auto& after=restored.force_beliefs.at(1);
    assert(after.initialized);
    for(int i=0;i<ForceModel::FeatureCount;++i) assert(after.features[i]==before.features[i]);
    for(int t=0;t<ForceModel::TargetCount;++t)
        for(int q=0;q<ForceModel::QuantileCount;++q) assert(after.prediction.values[t][q]==before.prediction.values[t][q]);
    observation[ForceModel::Tick]=2600;after.observe(observation);before.observe(observation);
    assert(after.features[ForceModel::Tick]==2200); // Cadence also survives loading.
    observation[ForceModel::Tick]=3400;after.observe(observation);before.observe(observation);
    after.forecast(3600);before.forecast(3600);
    for(int i=0;i<ForceModel::FeatureCount;++i) assert(after.features[i]==before.features[i]);
    for(int t=0;t<ForceModel::TargetCount;++t)
        for(int q=0;q<ForceModel::QuantileCount;++q) assert(after.prediction.values[t][q]==before.prediction.values[t][q]);
}

static void fittedPowerControlsAttackGate()
{
    Fixture f;
    f.building(10,10,0);auto target=f.building(35,30,1);
    for(int i=0;i<8;++i) f.warrior(15+i,15);
    auto& a=*f.ai;auto& c=a.context;c.initialize();f.remember(target);
    a.opponents[1].alive=true;a.opponents[1].estimated_warriors=1;
    auto& belief=a.force_beliefs[1];belief.initialized=true;
    belief.prediction.values[ForceModel::Power][ForceModel::Median]=1000000*ForceModel::Scale;
    a.plan_offense(c);
    assert(a.offense_diagnostics.gate.find("not strong enough")!=std::string::npos);
    belief.prediction.values[ForceModel::Power][ForceModel::Median]=ForceModel::Scale;
    a.plan_offense(c);
    assert(a.budget.tactical_kind==Tactics::MissionSiege);
    // Without force memory, only observed defenders constrain the attack.
    a.strategy.reconnaissance.force_memory_enabled=false;
    belief.prediction.values[ForceModel::Power][ForceModel::Median]=1000000*ForceModel::Scale;
    a.plan_offense(c);
    assert(a.budget.tactical_kind==Tactics::MissionSiege);
}

static void run()
{
    fittedForceUsesOnlyVisibleUnits();
    fittedPowerControlsAttackGate();
    fittedHistorySurvivesSave();
    nearbyWarriorsCountAsRallied();
    rallyRejectsBlockedGround();
    rallyAssemblesByMovement();
    waveAssemblyAndPipeline();
    smallWaveKeepsRecruiting();
    defenseWrapsBuildingOrigins();
    defenseCoverage();
    warriorEligibility();
    untrainedWarriorsFightAtLevelOne();
    minimumForceGate();
    loweredFlagLevelSurvivesReview();
    barracksAreFilledBeforeAttacking();
    unusableTrainingDoesNotBlockAttacks();
    trainingReservationsMatchDifferentLevels();
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
