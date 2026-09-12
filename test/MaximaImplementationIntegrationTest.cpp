// Link with the game objects (excluding Glob2.cpp) to exercise the real runtime.
#include "../src/GlobalContainer.h"
#include "../src/Game.h"
#include "../src/team/Team.h"
#include "../src/ai/AIImplementation.h"
#include "../src/ai/AICastor.h"
#include "../src/ai/echo/Echo.h"
#include "../src/ai/AINicowar.h"
#include "../src/map/Map.h"
#include "../src/Order.h"
#include "../src/Player.h"
#include "../src/Version.h"
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
#include "../src/AIMaximaSwarmController.h"
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

static std::string saved(const Gradients::Entities::Entity& value)
{
    GAGCore::MemoryStreamBackend* backend=new GAGCore::MemoryStreamBackend;
    GAGCore::BinaryOutputStream stream(backend);
    value.save(&stream);
    return std::string(backend->getBuffer(), backend->getPosition());
}

static void entityRoundTrip(const Gradients::Entities::Entity& original)
{
    const std::string bytes=saved(original);
    GAGCore::BinaryInputStream stream(new GAGCore::MemoryStreamBackend(bytes.data(),bytes.size()));
    stream.seekFromStart(0);
    std::unique_ptr<Gradients::Entities::Entity> loaded(Gradients::Entities::Entity::load(&stream));
    assert(loaded.get() && original.equals(*loaded));
    assert(saved(*loaded)==bytes);
}

template<class T> static void payloadRoundTrip(const T& original)
{
    GAGCore::MemoryStreamBackend* backend=new GAGCore::MemoryStreamBackend;
    GAGCore::BinaryOutputStream output(backend);
    original.save(&output);
    const std::string bytes(backend->getBuffer(),backend->getPosition());
    GAGCore::BinaryInputStream input(new GAGCore::MemoryStreamBackend(bytes.data(),bytes.size()));
    input.seekFromStart(0);
    std::unique_ptr<T> loaded(T::load(&input));
    GAGCore::MemoryStreamBackend* second=new GAGCore::MemoryStreamBackend;
    GAGCore::BinaryOutputStream roundtrip(second);
    loaded->save(&roundtrip);
    assert(std::string(second->getBuffer(),second->getPosition())==bytes);
}










static void maximaBootstrapRegression()
{
    Game game(NULL); game.map.setSize(6,6,GRASS); game.map.setGame(&game);
    game.addTeam(); game.teams[0]->race.loadDefault();
    Player player; player.setTeam(game.teams[0]);
    std::string reference;
    for(unsigned char pattern:{0x00,0x5a,0xa5})
    {
        alignas(AIMaxima::Maxima) unsigned char storage[sizeof(AIMaxima::Maxima)];
        std::fill(std::begin(storage),std::end(storage),pattern);
        auto* ai=new(storage) AIMaxima::Maxima(&player);
        auto* backend=new GAGCore::MemoryStreamBackend;
        GAGCore::BinaryOutputStream output(backend); ai->save(&output);
        const std::string bytes(backend->getBuffer(),backend->getPosition());
        if(reference.empty()) reference=bytes;
        else assert(bytes==reference);
        std::destroy_at(ai);
    }
}







static void preemptiveSchedulingRegression()
{
    Game game(NULL); game.map.setSize(6,6,GRASS); game.map.setGame(&game);
    game.addTeam(); game.teams[0]->race.loadDefault();
    Player player; player.setTeam(game.teams[0]);
    AIMaxima::Maxima ai(&player); auto& context=ai.context; context.initialize();
    ai.timer=1000; ai.last_preemptive_defense_tick=900;
    ai.budget.preemptive_defense_active=true;
    ai.budget.preemptive_effective_zone_max=3;
    ai.last_preemptive_effective_zone_max=3;
    ai.budget.preemptive_amphibious_active=false;
    ai.last_preemptive_amphibious_active=false;
    ai.budget.preemptive_recompute_ticks=500;
    ai.preemptive_building_signature=ai.compute_preemptive_building_signature(context);
    ai.update_preemptive_defense(context);
    assert(ai.last_preemptive_defense_tick==900);
    ai.timer=1500;
    ai.update_preemptive_defense(context);
    assert(ai.last_preemptive_defense_tick==1500);
}

static void integrationRegressions()
{
    Game game(NULL);
    game.map.setSize(6,6,GRASS);
    game.map.setGame(&game);
    game.addTeam();
    game.addTeam();
    game.teams[0]->race.loadDefault();
    game.teams[1]->race.loadDefault();
    Player player;
    player.setTeam(game.teams[0]);
    AIMaxima::Maxima ai(&player);
    Context& context=ai.context;
    context.allies=player.team->allies;
    context.enemies=player.team->enemies;

    // A saved empty pre-growth tile must still belong to the applied mask.
    game.map.setMapDiscovered(10,10,player.team->me);
    game.map.addForbidden(10,10,player.team->teamNumber);
    assert(!game.map.isResource(10,10));
    ai.initialize_farming_cache(context);
    assert(ai.applied_farm_protection_mask[10*64+10]);
    ai.budget.farming_enabled=false;
    ai.update_farming(context);
    assert(!ai.applied_farm_protection_mask[10*64+10]);
    bool removed=false;
    for(size_t i=0;i<context.managementOrders.size();++i)
    {
        Management::RemoveArea* area=dynamic_cast<Management::RemoveArea*>(context.managementOrders[i].get());
        if(!area || area->areaType!=ForbiddenArea) continue;
        for(size_t j=0;j<area->locations.size();++j)
            if(area->locations[j].x==10 && area->locations[j].y==10) removed=true;
    }
    assert(removed);
    context.managementOrders.clear();

    // A hidden replacement has reused a remembered building's slot/GID.
    const int innType=globalContainer->buildingsTypes.getTypeNum("inn",0,false);
    ::Building* replacement=game.addBuilding(45,45,innType,1);
    assert(replacement);
    const int gid=replacement->gid;
    std::vector<int> living(1,1);
    ai.reconnaissance.beginObservation(0,living);
    ai.reconnaissance.observeBuilding(AIMaxima::Recon::BuildingSighting(
        gid,1,replacement->type->shortTypeNum,5,5,
        replacement->type->width,replacement->type->height,false,0));
    ai.reconnaissance.finishObservation();
    ai.timer=1;
    ai.update_reconnaissance(context);
    assert(ai.reconnaissance.opponent(1)->buildings.count(gid)==1);
    game.map.setMapDiscovered(5,5,replacement->type->width,
        replacement->type->height,player.team->me);
    assert(!game.map.isFOWDiscovered(45,45,player.team->me));
    ai.timer=2;
    ai.update_reconnaissance(context);
    assert(ai.reconnaissance.opponent(1)->buildings.count(gid)==0);

    // Two unmatched flags both cover the same local warrior. They must share
    // the remaining reserve instead of each independently claiming it.
    const int flagType=globalContainer->buildingsTypes.getTypeNum("warflag",0,false);
    assert(flagType>=0);
    assert(game.addBuilding(20,20,flagType,0,8,8));
    assert(game.addBuilding(23,20,flagType,0,8,8));
    assert(game.addUnit(21,20,1,WARRIOR,0,0,0,0));
    context.get_building_register().initiate();
    ai.defense_flags.push_back(0);
    ai.defense_flags.push_back(1);
    ai.budget.reactive_defense_enabled=true;
    ai.budget.reactive_defense_flag_radius=5;
    ai.budget.reactive_defense_unit_cap=10;
    ai.budget.reactive_defense_advantage_min=3;
    ai.budget.reactive_defense_advantage_percent=0;
    ai.budget.defense_reserve=3;
    ai.compute_defense_flag_positioning(context);
    int assigned=0,destroyed=0;
    for(size_t i=0;i<context.managementOrders.size();++i)
    {
        Management::AssignWorkers* assignment=dynamic_cast<Management::AssignWorkers*>(context.managementOrders[i].get());
        if(assignment) assigned+=assignment->workers;
        if(dynamic_cast<Management::DestroyBuilding*>(context.managementOrders[i].get())) ++destroyed;
    }
    assert(assigned==3 && destroyed==1);
    context.managementOrders.clear();
    ai.budget.defense_reserve=0;
    ai.compute_defense_flag_positioning(context);
    destroyed=0;
    for(size_t i=0;i<context.managementOrders.size();++i)
    {
        assert(!dynamic_cast<Management::AssignWorkers*>(context.managementOrders[i].get()));
        if(dynamic_cast<Management::DestroyBuilding*>(context.managementOrders[i].get())) ++destroyed;
    }
    assert(destroyed==2);
}

// Exercise policy-to-runtime handoffs against the engine's actual objects.
static void executionRegressions()
{
    IntBuildingType::init();
    Game game(NULL);
    game.map.setSize(6,6,GRASS);
    game.map.setGame(&game);
    for(int team=0; team<3; ++team) {
        game.addTeam();
        game.teams[team]->race.loadDefault();
    }
    Player player; player.setTeam(game.teams[0]);
    AIMaxima::Maxima ai(&player);
    Context& c=ai.context;
    const int innType=globalContainer->buildingsTypes.getTypeNum("inn",0,false);
    const int scoutType=globalContainer->buildingsTypes.getTypeNum("explorationflag",0,false);
    assert(game.addBuilding(10,10,innType,0));
    c.initialize(); c.activeAI=&ai;

    // Both axes, negative coordinates, and multiple laps must wrap before lookup.
    Gradients::GradientInfo source;
    source.add_source(new Gradients::Entities::Position(0,0));
    Gradients::Gradient& gradient=c.gradients.get_gradient(source);
    assert(gradient.get_height(64,0)==0);
    assert(gradient.get_height(0,64)==0);
    assert(gradient.get_height(-64,-128)==0);
    assert(gradient.get_height(-1,64)==gradient.get_height(63,0));
    Construction::BuildingOrder wrapped(IntBuildingType::WAR_FLAG,4);
    wrapped.add_constraint(new Construction::SinglePosition(64,-64));
    bool complete=false;
    PlacementResult location=wrapped.find_location(c,1,complete);
    assert(complete && location.found && location.value.x==0 && location.value.y==0);
    // Explorer strikes need visible space and a warrior force to follow.
    for(int y=0;y<64;++y) for(int x=0;x<64;++x)
        game.map.setMapDiscovered(x,y,player.team->me);
    for(int i=0;i<8;++i) {
        Unit* warrior=game.addUnit(6+i,7,0,WARRIOR,1,1,1,1); assert(warrior);
        warrior->medical=Unit::MED_FREE;
        warrior->activity=Unit::ACT_RANDOM;
        warrior->level[ATTACK_SPEED]=warrior->level[ATTACK_STRENGTH]=1;
    }

    // A raid retarget can happen between director ticks. Explorers follow that
    // live mission, not the unset or obsolete legacy target.
    assert(game.addUnit(30,30,1,WARRIOR,0,0,0,0));
    assert(game.addUnit(40,40,2,WARRIOR,0,0,0,0));
    game.map.setMapDiscovered(30,30,player.team->me);
    game.map.setMapDiscovered(40,40,player.team->me);
    ai.target=-1;
    ai.budget.explorer_campaign_active=true;
    ai.budget.explorer_campaign_flags=1;
    ai.budget.explorer_campaign_units_per_flag=2;
    ai.tactical_mission.kind=AIMaxima::Tactics::MissionRaid;
    ai.tactical_mission.phase=AIMaxima::Tactics::PhaseEngage;
    ai.tactical_mission.flagId=1;
    for(int team=1; team<=2; ++team) {
        ai.tactical_mission.targetTeam=team;
        ai.compute_explorer_flag_attack_positioning(c);
        assert(ai.explorer_attack_flags.size()==1);
        location=c.buildingOrders.back()->find_location(c,1,complete);
        assert(location.found && location.value.x==(team==1 ? 30 : 40));
        c.cancel_or_destroy_building(ai.explorer_attack_flags.front());
        c.update_management_orders();
        assert(ai.explorer_attack_flags.empty());
    }
    c.orders.clear();

    // Ending the offense before the flag order is issued removes it entirely.
    ai.budget.tactical_kind=AIMaxima::Tactics::MissionSiege;
    ai.budget.tactical_target_team=1;
    ai.budget.tactical_target_x=30; ai.budget.tactical_target_y=30;
    ai.budget.tactical_requested_force=6;
    ai.control_offense(c);
    int pending=ai.tactical_mission.flagId;
    assert(pending>=0);
    ai.end_offense(c,"economic_emergency");
    c.update_management_orders(); c.update_building_orders();
    assert(!c.buildings.is_building_pending(pending));
    assert(c.buildingOrders.empty() && c.orders.empty() && ai.attack_flags.empty());

    // Ending it after OrderCreate was issued still deletes the eventual flag.
    ai.control_offense(c);
    pending=ai.tactical_mission.flagId;
    c.update_building_orders();
    assert(c.orders.size()==1);
    auto create=std::dynamic_pointer_cast<OrderCreate>(c.orders.front());
    assert(create);
    c.orders.clear();
    ai.end_offense(c,"economic_emergency");
    ::Building* flag=game.addBuilding(create->posX,create->posY,create->typeNum,0);
    assert(flag);
    c.buildings.tick(); c.update_management_orders();
    bool deleted=false;
    for(auto order:c.orders) {
        auto remove=std::dynamic_pointer_cast<OrderDelete>(order);
        if(remove && remove->gid==flag->gid) deleted=true;
    }
    assert(deleted);
    c.orders.clear(); c.managementOrders.clear(); ai.attack_flags.clear();
    c.managementOrders.clear();

    // Failed searches retry independently, while pending/successful fruit flags
    // survive save/load without duplicate creation. No save format change needed.
    ai.budget.fruit_active=true; ai.budget.fruit_units_per_flag=1;
    ai.budget.fruit_flag_radius=3;
    ai.update_fruit_flags(c);
    assert(c.buildingOrders.empty()); // Absent species do not queue futile searches.
    // Legacy saves can still contain these impossible pending searches.
    for(int fruit:{CHERRY,ORANGE,PRUNE}) {
        Gradients::GradientInfo absent;
        absent.add_source(new Gradients::Entities::Resource(fruit));
        auto order=new Construction::BuildingOrder(IntBuildingType::EXPLORATION_FLAG,1);
        order->add_constraint(new Construction::MaximumDistance(absent,0));
        c.add_building_order(order);
    }
    ai.update_fruit_flags(c);
    assert(c.buildingOrders.size()==3);
    for(int step=0; step<40; ++step) {
        c.gradients.update(step); c.update_building_orders();
    }
    assert(c.buildingOrders.empty()); // No fruit exists yet: all searches failed.
    c.update_management_orders(); c.orders.clear();
    game.map.setResource(25,25,CHERRY,1);
    game.map.setResource(35,35,ORANGE,1);
    game.map.setResource(45,45,PRUNE,1);
    c.gradients.invalidate(); // The fixture has replaced the static fruit layout.
    ai.update_fruit_flags(c);
    assert(c.buildingOrders.size()==3);
    const int cherryId=c.resource_flags(CHERRY).front();
    // Resolve just the cherry creation; the other searches remain pending.
    c.buildings.issue_order(cherryId,25,25,IntBuildingType::EXPLORATION_FLAG);
    c.buildingOrders.erase(std::remove_if(c.buildingOrders.begin(),c.buildingOrders.end(),
        [&](const std::shared_ptr<Construction::BuildingOrder>& order){return order->id==cherryId;}),
        c.buildingOrders.end());
    ::Building* cherry=game.addBuilding(25,25,scoutType,0);
    assert(cherry); c.buildings.tick();
    GAGCore::MemoryStreamBackend* backend=new GAGCore::MemoryStreamBackend;
    GAGCore::BinaryOutputStream output(backend);
    c.save(&output);
    const std::string bytes(backend->getBuffer(),backend->getPosition());
    AIMaxima::Maxima loaded(&player);
    GAGCore::BinaryInputStream input(new GAGCore::MemoryStreamBackend(bytes.data(),bytes.size()));
    input.seekFromStart(0);
    assert(loaded.context.load(&input,92));
    loaded.budget.fruit_active=true;
    loaded.update_fruit_flags(loaded.context);
    assert(loaded.context.buildingOrders.size()==2);
    assert(loaded.context.resource_flags(CHERRY).size()==1);
    // The engine destroys the observed cherry flag; only cherry is replaced.
    cherry->kill(); player.team->syncStep(); c.buildings.tick();
    ai.update_fruit_flags(c);
    assert(c.buildingOrders.size()==3);
    assert(c.resource_flags(CHERRY).size()==1 && c.resource_flags(CHERRY).front()!=cherryId);
    ai.budget.fruit_active=false; ai.update_fruit_flags(c);
    assert(c.buildingOrders.empty());
    std::cout << "execution handoff regressions passed\n";
}


static void reviewBugRegressions()
{
    Game game(NULL); game.map.setSize(6,6,GRASS); game.map.setGame(&game);
    for(int team=0;team<3;++team) {
        game.addTeam(); game.teams[team]->race.loadDefault();
    }
    Player player; player.setTeam(game.teams[0]);
    AIMaxima::Maxima ai(&player); Context& c=ai.context;
    const int innType=globalContainer->buildingsTypes.getTypeNum("inn",0,false);
    assert(game.addBuilding(10,10,innType,0));
    ::Building* ally=game.addBuilding(35,35,innType,1); assert(ally);
    ::Building* enemy=game.addBuilding(50,50,innType,2); assert(enemy);
    player.team->allies|=game.teams[1]->me;
    player.team->enemies&=~game.teams[1]->me;
    player.team->enemies|=game.teams[2]->me;
    ally->seenByMask|=player.team->me;
    enemy->seenByMask|=player.team->me;
    c.initialize();
    auto world=ai.collect_development_world(c);
    assert(world.tile(35,35).threat==0);
    assert(world.tile(50,50).threat==ai.strategy.placement.enemy_threat_base);
    // Diplomacy changes must affect the next placement world as well.
    player.team->enemies|=game.teams[1]->me;
    player.team->allies&=~game.teams[1]->me;
    world=ai.collect_development_world(c);
    assert(world.tile(35,35).threat==ai.strategy.placement.enemy_threat_base);

    // Two islands separated by a narrow channel. The old selector chose
    // (19,30) on the home island for a target on the opposite shore.
    for(int y=0;y<64;++y) for(int x=0;x<64;++x) {
        const bool land=y>=5&&y<=55&&((x>=5&&x<=19)||(x>=26&&x<=45));
        game.map.setTerrain(x,y,land?0:256);
        game.map.setMapDiscovered(x,y,player.team->me);
    }
    c.gradients.invalidate();
    ::Building* across=game.addBuilding(27,30,innType,2); assert(across);
    across->seenByMask|=player.team->me;
    ai.reconnaissance.beginObservation(ai.timer,std::vector<int>(1,2));
    ai.reconnaissance.observeBuilding(AIMaxima::Recon::BuildingSighting(
        across->gid,2,across->type->shortTypeNum,across->posX,across->posY,
        across->type->width,across->type->height,false,ai.timer));
    ai.reconnaissance.finishObservation();
    ai.strategy.tactics.min_force=4;
    for(int i=0;i<8;++i) {
        Unit* walker=game.addUnit(6+i,7,0,WARRIOR,1,1,1,1); assert(walker);
        walker->medical=Unit::MED_FREE; walker->activity=Unit::ACT_RANDOM;
        walker->level[ATTACK_SPEED]=walker->level[ATTACK_STRENGTH]=1;
        walker->performance[SWIM]=0;
    }
    // Walkers cannot reach the far island, so no offense is planned.
    ai.plan_offense(c);
    assert(ai.budget.tactical_kind==AIMaxima::Tactics::MissionNone);
    assert(ai.offense_diagnostics.rejections["too_few_reachable"]==1);
    for(int i=0;i<4;++i) {
        Unit* swimmer=game.addUnit(6+i,8,0,WARRIOR,1,1,1,1); assert(swimmer);
        swimmer->medical=Unit::MED_FREE; swimmer->activity=Unit::ACT_RANDOM;
        swimmer->level[ATTACK_SPEED]=swimmer->level[ATTACK_STRENGTH]=1;
        swimmer->performance[SWIM]=1;
    }
    ai.plan_offense(c);
    assert(ai.budget.tactical_kind==AIMaxima::Tactics::MissionSiege);
    assert(ai.budget.tactical_target_gid==across->gid);

    // Shoreline contracts are covered through actual farming output in
    // MaximaFarmingIntegrationTest, including every direction and map seams.
    std::cout << "allied placement and amphibious offense regressions passed\n";
}

// The director must count troops committed to the mission it is continuing.
static void missionForceRegressions()
{
    IntBuildingType::init();
    Game game(NULL);
    game.map.setSize(6,6,GRASS);
    game.map.setGame(&game);
    for(int team=0; team<3; ++team) {
        game.addTeam();
        game.teams[team]->race.loadDefault();
    }
    Player player; player.setTeam(game.teams[1]);
    player.team->allies=game.teams[0]->me|player.team->me;
    player.team->enemies=game.teams[2]->me;
    AIMaxima::Maxima ai(&player);
    Context& c=ai.context;
    const int innType=globalContainer->buildingsTypes.getTypeNum("inn",0,false);
    ::Building* home=game.addBuilding(10,10,innType,1);
    assert(home && game.addBuilding(25,25,innType,0));
    ::Building* enemy=game.addBuilding(40,40,innType,2);
    ::Building* flag=game.addBuilding(15,15,
        globalContainer->buildingsTypes.getTypeNum("warflag",0,false),1);
    assert(enemy && flag);
    c.initialize();
    int flagId=-1;
    for(auto record:c.buildings.found())
        if(record.second.gid==flag->gid) flagId=record.first;
    assert(flagId>=0 && flagId!=flag->gid);
    for(int y=0; y<64; ++y) for(int x=0; x<64; ++x)
        game.map.setMapDiscovered(x,y,player.team->me);
    std::vector<Unit*> army;
    for(int n=0; n<12; ++n) {
        Unit* unit=game.addUnit(5+n,5,1,WARRIOR,1,0,0,0);
        assert(unit);
        unit->medical=Unit::MED_FREE;
        unit->activity=Unit::ACT_RANDOM;
        army.push_back(unit);
    }
    ai.timer=100;
    ai.posture=AIMaxima::Maxima::PostureCampaign;
    ai.snapshot.population=150; ai.snapshot.workers=100;
    ai.snapshot.trained_warriors=12;
    ai.budget.defense_reserve=0;
    ai.budget.food_emergency=false; ai.budget.colony_emergency=false;
    ai.strategy.tactics.enabled=true; ai.strategy.tactics.siege_enabled=true;
    ai.strategy.tactics.min_force=4; ai.strategy.raiding.enabled=false;
    ai.reconnaissance.beginObservation(100,std::vector<int>(1,2));
    ai.reconnaissance.observeBuilding(AIMaxima::Recon::BuildingSighting(
        enemy->gid,2,enemy->type->shortTypeNum,40,40,
        enemy->type->width,enemy->type->height,false,100));
    ai.reconnaissance.finishObservation();
    // Warriors already on the offensive flag count toward continuing it.
    ai.tactical_mission.kind=AIMaxima::Tactics::MissionSiege;
    ai.tactical_mission.phase=AIMaxima::Tactics::PhaseEngage;
    ai.tactical_mission.flagId=flagId;
    ai.tactical_mission.targetTeam=2; ai.tactical_mission.targetGid=enemy->gid;
    for(Unit* unit:army) {
        unit->attachedBuilding=flag;
        unit->activity=Unit::ACT_FLAG;
        flag->unitsWorking.push_back(unit);
    }
    ai.plan_offense(c);
    assert(ai.budget.tactical_target_gid==enemy->gid);
    // Troops committed elsewhere must not be borrowed by this exception.
    for(Unit* unit:army) unit->attachedBuilding=home;
    flag->unitsWorking.clear();
    ai.plan_offense(c);
    assert(ai.budget.tactical_kind==AIMaxima::Tactics::MissionNone);
    for(Unit* unit:army) {
        unit->attachedBuilding=NULL;
        unit->activity=Unit::ACT_RANDOM;
    }
    ai.plan_offense(c);
    assert(ai.budget.tactical_target_gid==enemy->gid);
}

// Apply emitted geometry/selector orders to the real engine flag, then ask
// the engine's resource gradient whether its assigned work is reachable.
static void applyClearingGeometry(Context& c, ::Building* flag)
{
    c.update_management_orders();
    for(auto order:c.orders) {
        auto size=std::dynamic_pointer_cast<OrderModifyFlag>(order);
        if(size && size->gid==flag->gid) flag->unitStayRange=size->range;
        auto move=std::dynamic_pointer_cast<OrderMoveFlag>(order);
        if(move && move->gid==flag->gid) {
            flag->posX=move->x; flag->posY=move->y;
        }
        auto selector=std::dynamic_pointer_cast<OrderModifyClearingFlag>(order);
        if(selector && selector->gid==flag->gid)
            for(int r=0; r<BASIC_COUNT; ++r)
                flag->clearingResources[r]=selector->clearingResources[r];
    }
    c.orders.clear();
}

// Director decisions must survive arbitration and the final engine handoff.
static void directorExecutionRegressions()
{
    Game game(NULL);
    game.map.setSize(6,6,GRASS); game.map.setGame(&game);
    game.addTeam(); game.addTeam();
    game.teams[0]->race.loadDefault(); game.teams[1]->race.loadDefault();
    Player player; player.setTeam(game.teams[0]);
    AIMaxima::Maxima ai(&player); Context& c=ai.context;
    const int swarmType=globalContainer->buildingsTypes.getTypeNum("swarm",0,false);
    assert(game.addBuilding(10,10,swarmType,0));
    c.initialize(); c.activeAI=&ai;
    TeamStat* stat=player.team->stats.getLatestStat();
    stat->totalUnit=400;
    stat->numberUnitPerType[WORKER]=300;
    ai.snapshot.population=400; ai.snapshot.workers=300;
    ai.snapshot.warriors=100; ai.snapshot.trained_warriors=100;
    ai.snapshot.swarms=1; ai.snapshot.completed_swarms=1; ai.snapshot.barracks=1;
    ai.environment.food_security=100; ai.environment.food_headroom=100;
    ai.environment.resource_capacity=100;
    ai.environment.accessible_corn=1000; // Fund the current colony birth controller.
    ai.demands.aggression=100; ai.demands.military=100;
    ai.build_policy_bids(); ai.arbitrate_policy_bids(); ai.finalize_director_plan(c);
    assert(ai.budget.desired_warriors>0 && ai.budget.warrior_ratio>0);
    const auto birthRatio=[&](int warriors) {
        stat->numberUnitPerType[WARRIOR]=warriors;
        c.managementOrders.clear(); ai.manage_swarm(c,0);
        int ratio=-1;
        for(auto order:c.managementOrders)
            if(auto* change=dynamic_cast<Management::ChangeSwarm*>(order.get()))
                ratio=change->warrior;
        assert(ratio>=0); return ratio;
    };
    // Use current stats even when the director snapshot still says 100 warriors.
    assert(birthRatio(ai.budget.desired_warriors-1)>0);
    assert(birthRatio(ai.budget.desired_warriors)==0);
    assert(birthRatio(ai.budget.desired_warriors+1)==0);
    ai.budget.desired_warriors=0;
    assert(birthRatio(0)==0);

    const int backlog=std::max(ai.strategy.military.training_backlog_floor,
        ai.strategy.military.training_backlog_per_barracks);
    ai.snapshot.warriors=backlog+20;
    ai.snapshot.trained_warriors=20;
    ai.build_policy_bids(); ai.arbitrate_policy_bids();
    assert(ai.policy_bids[AIMaxima::Maxima::PolicyOffense].warrior_ratio>0);
    assert(ai.budget.warrior_ratio==0);
    ++ai.snapshot.trained_warriors;
    ai.build_policy_bids(); ai.arbitrate_policy_bids();
    assert(ai.budget.warrior_ratio>0);
    --ai.snapshot.trained_warriors;
    ai.strategy.military.warrior_training_backlog_throttle_enabled=false;
    ai.build_policy_bids(); ai.arbitrate_policy_bids();
    assert(ai.budget.warrior_ratio>0);

    // The only food lies ten tiles away across water, on a toroidal map.
    for(int y=0;y<64;++y) for(int x=0;x<64;++x)
        game.map.setMapDiscovered(x,y,player.team->me);
    for(int y=0;y<64;++y) {
        game.map.setTerrain(17,y,256); game.map.setTerrain(18,y,256);
        game.map.setTerrain(40,y,256); game.map.setTerrain(41,y,256);
    }
    game.map.setResource(20,10,CORN,1);
    ai.fertility_cache=AIMaxima::Farming::ExactFertilityCache();
    ai.configure_development_planner();
    ai.snapshot.swimming_workers=0; ai.finalize_director_plan(c);
    ai.timer=100; ai.update_swarm_retirement(c);
    ai.timer=3100; ai.update_swarm_retirement(c);
    assert(ai.remote_swarms_ready.count(0));
    ai.snapshot.swimming_workers=20; ai.finalize_director_plan(c);
    ai.update_swarm_retirement(c);
    assert(ai.remote_swarms_ready.empty() && ai.remote_swarm_since.empty());
    ai.snapshot.swimming_workers=0; ai.finalize_director_plan(c);
    ai.timer=3200; ai.update_swarm_retirement(c);
    ai.timer=6200; ai.update_swarm_retirement(c);
    assert(ai.remote_swarms_ready.count(0));
}

static void directorUpgradeRegressions()
{
    using namespace AIMaximaPlacement;
    Game game(NULL);
    game.map.setSize(6,6,GRASS); game.map.setGame(&game);
    game.addTeam(); game.teams[0]->race.loadDefault();
    Player player; player.setTeam(game.teams[0]);
    AIMaxima::Maxima ai(&player); Context& c=ai.context;
    const int raceType=globalContainer->buildingsTypes.getTypeNum("racetrack",0,false);
    assert(game.addBuilding(25,25,raceType,0));
    c.initialize(); c.activeAI=&ai;
    for(int y=0;y<64;++y)for(int x=0;x<64;++x)
        game.map.setMapDiscovered(x,y,player.team->me);
    TeamStat* stat=player.team->stats.getLatestStat();
    stat->totalUnit=100; stat->upgradeState[BUILD][1]=20;
    ai.snapshot.population=100; ai.snapshot.schools=1;
    ai.budget.allow_upgrades=true;
    ai.configure_development_planner();
    ai.finalize_director_plan(c);
    WorldState world=ai.collect_development_world(c);
    ai.development_planner.adoptStartingBuildings(world);
    DevelopmentAction action;
    DevelopmentLimits limits=ai.collect_development_limits(c);
    assert(ai.development_planner.selectAction(world,{},limits,action));
    assert(action.type==UpgradeBuilding);
    assert(action.utility.unmetDemand==ai.budget.upgrade_level1_racetrack_weight);
    // A pending result loses authorization when Recover zeros its priority.
    ai.posture=AIMaxima::Maxima::PostureRecover;
    ai.finalize_director_plan(c);
    limits=ai.collect_development_limits(c);
    RejectionReason reason=RejectedTerrain;
    assert(!ai.development_planner.revalidateSelection(world,{},limits,action,&reason));
    assert(reason==RejectedAuthorization);
    assert(!ai.issue_development_action(c,action));
    assert(c.orders.empty());
    assert(!ai.development_planner.selectAction(world,{},limits,action));
    ai.posture=AIMaxima::Maxima::PostureDevelop;
    ai.finalize_director_plan(c);
    limits=ai.collect_development_limits(c);
    assert(ai.development_planner.selectAction(world,{},limits,action));

    // Prestige is a school level-two to level-three upgrade. Its thresholds
    // must use the current workforce and completed schools, not stale snapshots.
    const int schoolType=IntBuildingType::SCIENCE_BUILDING;
    const int trainingSchool=globalContainer->buildingsTypes.getTypeNum("school",1,false);
    assert(game.addBuilding(10,10,trainingSchool,0)); c.buildings.initiate();
    ai.budget.allow_level2_upgrades=true; ai.finalize_director_plan(c);
    world=ai.collect_development_world(c);
    world.buildings.erase(std::remove_if(world.buildings.begin(),world.buildings.end(),
        [schoolType](const WorldBuilding& b){return b.buildingType!=schoolType;}),world.buildings.end());
    ai.development_planner.adoptStartingBuildings(world);
    const auto selectPrestige=[&]() {
        return ai.development_planner.selectAction(world,{},ai.collect_development_limits(c),action);
    };
    stat->upgradeState[BUILD][2]=ai.budget.first_prestige_trained_workers-1;
    assert(ai.collect_development_limits(c).upgradePriority(schoolType,2)==0);
    assert(!selectPrestige());
    ++stat->upgradeState[BUILD][2];
    assert(ai.collect_development_limits(c).upgradePriority(schoolType,2)!=0);
    assert(selectPrestige() && action.fromLevel==2 && action.targetLevel==3);
    const int prestigeType=globalContainer->buildingsTypes.getTypeNum("school",2,false);
    assert(game.addBuilding(45,45,prestigeType,0)); c.buildings.initiate();
    stat->upgradeState[BUILD][2]=ai.budget.second_prestige_trained_workers-1;
    assert(ai.collect_development_limits(c).upgradePriority(schoolType,2)==0);
    ++stat->upgradeState[BUILD][2];
    stat->totalUnit=ai.budget.second_prestige_population_min-1;
    assert(ai.collect_development_limits(c).upgradePriority(schoolType,2)==0);
    ++stat->totalUnit;
    assert(ai.collect_development_limits(c).upgradePriority(schoolType,2)!=0);
    assert(selectPrestige());
}

// Economy authority must survive arbitration and the runtime order handoff.
static void economyStaffingRegressions()
{
    IntBuildingType::init();
    Game game(NULL); game.map.setSize(6,6,GRASS); game.map.setGame(&game);
    game.addTeam(); game.teams[0]->race.loadDefault();
    Player player; player.setTeam(game.teams[0]);
    AIMaxima::Maxima ai(&player); Context& c=ai.context;
    ai.snapshot.population=100; ai.snapshot.workers=60;
    ai.snapshot.warriors=40; ai.snapshot.trained_warriors=0;
    ai.snapshot.barracks=1; ai.demands.aggression=100;
    ai.demands.military=100; ai.demands.growth=60;
    ai.environment.food_headroom=80;
    ai.strategy.military.training_backlog_floor=10;
    ai.strategy.military.training_backlog_per_barracks=4;
    ai.strategy.military.warrior_training_backlog_throttle_enabled=true;
    ai.build_policy_bids(); ai.arbitrate_policy_bids();
    assert(ai.policy_bids[AIMaxima::Maxima::PolicyOffense].warrior_ratio>0);
    assert(ai.budget.warrior_ratio==0);
    // Resume below the threshold, and preserve the switch's opt-out behavior.
    ai.snapshot.trained_warriors=31;
    ai.build_policy_bids(); ai.arbitrate_policy_bids();
    assert(ai.budget.warrior_ratio>0);
    ai.snapshot.trained_warriors=30;
    ai.build_policy_bids(); ai.arbitrate_policy_bids();
    assert(ai.budget.warrior_ratio==0);
    ai.strategy.military.warrior_training_backlog_throttle_enabled=false;
    ai.build_policy_bids(); ai.arbitrate_policy_bids();
    assert(ai.budget.warrior_ratio>0);

    // Survival utility cannot overwrite the coherent birth controller.
    ai.demands.food=100; ai.demands.survival=100;
    ai.environment.accessible_corn=1000;
    ai.build_policy_bids(); ai.arbitrate_policy_bids();
    assert(ai.budget.swarm_workers>0);
    ai.policy_bids[AIMaxima::Maxima::PolicyGrowth].swarm_workers=8;
    ai.policy_bids[AIMaxima::Maxima::PolicySurvival].swarm_workers=2;
    ai.arbitrate_policy_bids(); assert(ai.budget.swarm_workers==8);

    const int swarmType=globalContainer->buildingsTypes.getTypeNum("swarm",0,false);
    assert(game.addBuilding(10,10,swarmType,0));
    assert(game.addBuilding(30,30,swarmType,0));
    c.initialize();
    TeamStat* stat=player.team->stats.getLatestStat(); stat->totalUnit=100;
    ai.budget.desired_warriors=5; ai.budget.warrior_ratio=3;
    for(int count:{6,5,4})
    {
        stat->numberUnitPerType[WARRIOR]=count;
        // The runtime must use the current stats, not the older director sample.
        ai.snapshot.warriors=0;
        ai.manage_swarm(c,0);
        int ratio=-1;
        for(auto order:c.managementOrders)
            if(auto r=dynamic_cast<Management::ChangeSwarm*>(order.get())) ratio=r->warrior;
        assert(ratio==(count<5 ? 3 : 0));
        c.managementOrders.clear();
    }

    // Two islands: the first swarm's corn requires crossing a water strip.
    // Retirement evaluates discovered harvesting routes, not hidden terrain.
    for(int y=0;y<64;++y) for(int x=0;x<64;++x)
        game.map.setMapDiscovered(x,y,player.team->me);
    for(int y=0;y<64;++y)
    { game.map.getTile(0,y).terrain=256; game.map.getTile(16,y).terrain=256; }
    // The retained swarm needs renewable fertility as well as existing corn.
    game.map.getTile(36,31).terrain=256;
    game.map.setResource(19,11,CORN,5); game.map.setResource(34,31,CORN,5);
    // This fixture changes terrain after manage_swarm populated the cache.
    ai.fertility_cache=AIMaxima::Farming::ExactFertilityCache();
    ai.budget.swarm_retirement_enabled=true; ai.budget.desired_swarms=1;
    ai.budget.recovery_active=false; ai.budget.can_swim=true; ai.can_swim=false;
    ai.timer=1; ai.update_swarm_retirement(c);
    ai.timer=3001; ai.update_swarm_retirement(c);
    assert(ai.remote_swarms_ready.empty());
    for(auto order:c.managementOrders)
        assert(!dynamic_cast<Management::DestroyBuilding*>(order.get()));
    // Losing swimming restores the usual probation and retirement behavior.
    ai.budget.can_swim=false; ai.can_swim=true;
    ai.timer=3002; ai.update_swarm_retirement(c);
    ai.timer=6002; ai.update_swarm_retirement(c);
    bool deleted=false;
    for(auto order:c.managementOrders)
        if(auto d=dynamic_cast<Management::DestroyBuilding*>(order.get())) deleted|=d->id==0;
    assert(deleted);
}

static void innCompletionStaffingRegressions()
{
    Game game(NULL); game.map.setSize(6,6,GRASS); game.map.setGame(&game);
    game.addTeam(); game.teams[0]->race.loadDefault();
    Player player; player.setTeam(game.teams[0]);
    AIMaxima::Maxima ai(&player); Context& c=ai.context;
    c.initialize(); c.activeAI=&ai;
    game.map.setMapDiscovered(19,19,2,2,player.team->me);
    AIMaximaPlacement::DevelopmentAction action;
    action.type=AIMaximaPlacement::BuildStandalone;
    action.buildingType=IntBuildingType::FOOD_BUILDING;
    action.centerX=20; action.centerY=20; action.workers=2;
    action.initialFootprint=AIMaximaPlacement::Footprint(-1,-1,2,2);
    assert(ai.issue_development_action(c,action));
    const int id=c.previousBuildingId;
    const int siteType=globalContainer->buildingsTypes.getTypeNum("inn",0,true);
    Building* inn=game.addBuilding(19,19,siteType,0,1,1); assert(inn);
    c.orders.clear(); c.buildings.tick(); c.update_management_orders();
    c.orders.clear();
    // Finish through the engine so its one-worker post-construction default
    // is in place before the completion callback runs.
    for(int resource=0;resource<MAX_RESOURCES;++resource)
        inn->resources[resource]=inn->type->maxResource[resource];
    inn->updateBuildingSite();
    assert(inn->constructionResultState==Building::NO_CONSTRUCTION);
    assert(inn->maxUnitWorking==1);
    c.buildings.tick(); c.update_management_orders();
    for(auto order:c.orders)
        if(auto a=std::dynamic_pointer_cast<OrderModifyBuilding>(order))
            if(a->gid==inn->gid)
                // Staffing is a closed loop on the inn's own stock and every
                // building keeps at least one carrier. The old policy released
                // them outright when no wheat grew nearby.
                assert(a->numberRequested>=1);
    (void)id;
}

static void directorUpgradePriorityRegressions()
{
    Game game(NULL); game.map.setSize(6,6,GRASS); game.map.setGame(&game);
    game.addTeam(); game.teams[0]->race.loadDefault();
    Player player; player.setTeam(game.teams[0]);
    AIMaxima::Maxima ai(&player); Context& c=ai.context; c.initialize();
    ai.snapshot.population=100; ai.snapshot.schools=1;
    ai.budget.allow_upgrades=true; ai.budget.allow_level2_upgrades=true;
    ai.posture=AIMaxima::Maxima::PostureRecover;
    ai.finalize_director_plan(c);
    const auto limits=ai.collect_development_limits(c);
    for(int level:{1,2})
    {
        assert(limits.upgradePriority(IntBuildingType::FOOD_BUILDING,level)
            ==ai.strategy.upgrades.recovery_inn_weight);
        for(int type:{IntBuildingType::HEAL_BUILDING,IntBuildingType::WALKSPEED_BUILDING,
                      IntBuildingType::SWIMSPEED_BUILDING,IntBuildingType::ATTACK_BUILDING})
            assert(limits.upgradePriority(type,level)==0);
    }
    ai.posture=AIMaxima::Maxima::PostureExpand;
    ai.finalize_director_plan(c);
    const auto healthy=ai.collect_development_limits(c);
    assert(healthy.upgradePriority(IntBuildingType::ATTACK_BUILDING,1)
        ==ai.budget.upgrade_level1_barracks_weight);
    assert(healthy.upgradePriority(IntBuildingType::ATTACK_BUILDING,1)>0);
}

static void placementMaintenanceRegressions()
{
    using namespace AIMaximaPlacement;
    IntBuildingType::init();
    {
        Game game(NULL);game.map.setSize(6,6,GRASS);game.map.setGame(&game);
        game.addTeam();game.teams[0]->race.loadDefault();
        Player player;player.setTeam(game.teams[0]);
        AIMaxima::Maxima ai(&player);Context& c=ai.context;c.initialize();
        for(int y=0;y<64;++y)for(int x=0;x<64;++x)
        {
            game.map.setMapDiscovered(x,y,player.team->me);
            if(x<20||x>=24||y<20||y>=24)game.map.setResource(x,y,WOOD,1);
        }
        game.map.setNoResource(17,21,0);
        assert(game.addUnit(17,21,0,WORKER,0,0,0,0));
        ai.initialize_farming_cache(c);ai.configure_development_planner();
        WorldState world=ai.collect_development_world(c);
        DevelopmentIntent intent;intent.buildingType=IntBuildingType::WALKSPEED_BUILDING;
        intent.unmetCount=1;intent.priority=100;intent.workers=2;
        DevelopmentLimits limits;limits.newConstruction=1;DevelopmentAction action;
        assert(ai.development_planner.selectAction(world,{intent},limits,action));
        assert(ai.development_planner.reserve(world,action));
        ai.budget.farming_enabled=true;ai.budget.farming_maintenance_clearing_enabled=true;
        ai.budget.farming_resource_preserving_circulation_enabled=true;
        ai.budget.farming_wood_firebreak_enabled=false;
        ai.budget.farming_wheat_invasion_clearing_enabled=false;
        // Simulate an old save that still owns clearing across the future parcel.
        for(int index:action.parcelTiles)if(world.tiles[index].clearableResource)
        {
            game.map.addClearArea(index%64,index/64,player.team->teamNumber);
            ai.applied_maintenance_clearing_mask[index]=1;
        }
        c.managementOrders.clear();ai.update_maintenance_clearing_areas(c);
        int futureResources=0,clearedResources=0,removedResources=0;
        for(int index:action.parcelTiles)if(world.tiles[index].clearableResource)
        {
            ++futureResources;clearedResources+=ai.maintenance_circulation_mask[index]!=0;
            for(auto order:c.managementOrders)
                if(auto area=dynamic_cast<Management::RemoveArea*>(order.get()))
                    if(area->areaType==ClearingArea)
                        for(auto location:area->locations)
                            if(location.x==index%64&&location.y==index/64)++removedResources;
        }
        // Only one necessary entrance may consume a future resource; the other
        // expansion tiles must survive, including after loading old clearing masks.
        if(futureResources!=20 || clearedResources!=1 || removedResources!=19)
            std::cerr<<"parcel resources="<<futureResources<<" cleared="<<clearedResources<<" removed="<<removedResources<<" action="<<action.centerX<<","<<action.centerY<<"\n";
        assert(futureResources==20 && clearedResources==1 && removedResources==19);
        // Clearing the requested cells must connect the actual outside worker
        // to the site, not merely open an enclosed tile beside its footprint.
        std::vector<Uint8> reached(64*64,0);std::vector<int> queue={21*64+17};
        reached[queue[0]]=1;
        for(size_t head=0;head<queue.size();++head)
            for(int dy=-1;dy<=1;++dy)for(int dx=-1;dx<=1;++dx)
            {
                const int x=game.map.normalizeX(queue[head]%64+dx);
                const int y=game.map.normalizeY(queue[head]/64+dy),index=y*64+x;
                if(!reached[index]&&(game.map.getResource(x,y).type==NO_RES_TYPE
                   ||ai.maintenance_circulation_mask[index]))
                {reached[index]=1;queue.push_back(index);}
            }
        assert(reached[21*64+20]);
    }
    {
        Game game(NULL);game.map.setSize(6,6,GRASS);game.map.setGame(&game);
        game.addTeam();game.teams[0]->race.loadDefault();
        Player player;player.setTeam(game.teams[0]);
        AIMaxima::Maxima ai(&player);Context& c=ai.context;c.initialize();
        for(int y=0;y<64;++y)for(int x=0;x<64;++x)game.map.setMapDiscovered(x,y,player.team->me);
        ai.initialize_farming_cache(c);ai.configure_development_planner();
        assert(game.addUnit(10,10,0,WORKER,0,0,0,0));
        DevelopmentIntent intent;intent.buildingType=IntBuildingType::HEAL_BUILDING;
        intent.unmetCount=4;intent.priority=100;intent.workers=2;
        DevelopmentLimits limits;limits.newConstruction=4;
        std::vector<Building*> members;
        for(int member=0;member<4;++member)
        {
            WorldState world=ai.collect_development_world(c);DevelopmentAction action;
            assert(ai.development_planner.selectAction(world,{intent},limits,action));
            assert(action.templateId==HospitalCompact);
            assert(ai.development_planner.reserve(world,action));
            assert(ai.issue_development_action(c,action));
            Building* building=game.addBuilding(
                game.map.normalizeX(action.centerX+action.initialFootprint.left),
                game.map.normalizeY(action.centerY+action.initialFootprint.top),
                globalContainer->buildingsTypes.getTypeNum("hospital",0,false),0);
            assert(building);members.push_back(building);
            c.orders.clear();c.buildings.tick();
            ai.development_planner.observe(ai.collect_development_world(c));
        }
        assert(ai.development_planner.campuses().size()==1);
        const auto& reservation=ai.development_planner.reservations().begin()->second;
        for(int index:reservation.circulationTiles)game.map.setResource(index%64,index/64,WOOD,1);
        ai.budget.farming_enabled=true;ai.budget.farming_maintenance_clearing_enabled=true;
        ai.budget.farming_resource_preserving_circulation_enabled=true;
        ai.budget.farming_wood_firebreak_enabled=false;
        ai.budget.farming_wheat_invasion_clearing_enabled=false;
        ai.update_maintenance_clearing_areas(c);
        for(Building* member:members)
        {
            bool entrance=false;
            for(int index:reservation.circulationTiles)
                if(ai.maintenance_circulation_mask[index])
                    for(int dy=0;dy<member->type->height;++dy)
                        for(int dx=0;dx<member->type->width;++dx)
                            entrance|=game.map.warpDistMax(index%64,index/64,
                                member->posX+dx,member->posY+dy)<=1;
            assert(entrance);
        }
        int preserved=0;
        for(int index:reservation.circulationTiles)preserved+=!ai.maintenance_circulation_mask[index];
        assert(preserved>0);
    }
}

static void upgradeClearingRegressions()
{
    using namespace AIMaximaPlacement;
    for(bool cancel:{false,true})
    {
        Game game(NULL);game.map.setSize(6,6,GRASS);game.map.setGame(&game);
        game.addTeam();game.teams[0]->race.loadDefault();
        Player player;player.setTeam(game.teams[0]);
        Building* building=game.addBuilding(20,20,
            globalContainer->buildingsTypes.getTypeNum("racetrack",0,false),0);
        assert(building);assert(game.addUnit(10,10,0,WORKER,1,0,0,0));
        AIMaxima::Maxima ai(&player);Context& c=ai.context;c.initialize();
        for(int y=0;y<64;++y)for(int x=0;x<64;++x)game.map.setMapDiscovered(x,y,player.team->me);
        ai.initialize_farming_cache(c);ai.configure_development_planner();
        WorldState world=ai.collect_development_world(c);
        ai.development_planner.adoptStartingBuildings(world);ai.development_planner_initialized=true;
        const auto contract=ai.development_planner.reservations().begin()->second;
        std::vector<int> expansion;
        for(int index:contract.footprintTiles)if(game.map.getBuilding(index%64,index/64)==NOGBID)
        {
            game.map.setResource(index%64,index/64,WOOD,1);
            expansion.push_back(index);
        }
        assert(expansion.size()==20);
        ai.budget.construction_sites=0;ai.budget.allow_upgrades=true;ai.budget.allow_level2_upgrades=false;
        ai.budget.upgrade_level1_racetrack_weight=100;
        player.team->stats.getLatestStat()->upgradeState[BUILD][1]=ai.budget.upgrade_level1_trained_units_per_slot;
        ai.budget.farming_enabled=true;ai.budget.farming_maintenance_clearing_enabled=true;
        ai.budget.farming_resource_preserving_circulation_enabled=true;
        ai.budget.farming_wood_firebreak_enabled=false;ai.budget.farming_wheat_invasion_clearing_enabled=false;
        for(int step=0;step<8&&ai.development_planner.actions().empty();++step)ai.development_cycle(c);
        assert(ai.development_planner.actions().size()==1);
        const int actionId=ai.development_planner.actions().begin()->first;
        assert(ai.development_planner.actions().at(actionId).state==ParcelReserved);
        for(auto order:c.orders)assert(!std::dynamic_pointer_cast<OrderConstruction>(order));
        ai.update_maintenance_clearing_areas(c);
        for(int index:expansion)assert(ai.maintenance_circulation_mask[index]);
        if(cancel)
        {
            ai.budget.allow_upgrades=false;ai.development_cycle(c);
            assert(ai.development_planner.actions().at(actionId).state==UpgradeBlocked);
            assert(ai.development_planner.reservations().size()==1);
            ai.update_maintenance_clearing_areas(c);
            int cleared=0;for(int index:expansion)cleared+=ai.maintenance_circulation_mask[index]!=0;
            assert(cleared<20);
        }
        else
        {
            for(int index:expansion)game.map.setNoResource(index%64,index/64,0);
            ai.development_cycle(c);
            assert(ai.development_planner.actions().at(actionId).state==CreateIssued);
            bool issued=false;for(auto order:c.orders)issued|=bool(std::dynamic_pointer_cast<OrderConstruction>(order));
            assert(issued);
            assert(building->isHardSpaceForBuildingSite(Building::UPGRADE));
        }
    }
}

static void rejectedPlacementUpgradeRegressions()
{
    using namespace AIMaximaPlacement;
    Game game(NULL);game.map.setSize(6,6,GRASS);game.map.setGame(&game);
    game.addTeam();game.teams[0]->race.loadDefault();
    Player player;player.setTeam(game.teams[0]);
    Building* building=game.addBuilding(20,20,
        globalContainer->buildingsTypes.getTypeNum("racetrack",0,false),0);
    assert(building);
    AIMaxima::Maxima ai(&player);Context& c=ai.context;c.initialize();
    for(int y=0;y<64;++y)for(int x=0;x<64;++x)game.map.setMapDiscovered(x,y,player.team->me);
    ai.initialize_farming_cache(c);ai.configure_development_planner();
    WorldState world=ai.collect_development_world(c);ai.development_planner.adoptStartingBuildings(world);
    const int id=world.buildings[0].id;
    DevelopmentAction action;action.id=100;action.type=UpgradeBuilding;
    action.buildingType=IntBuildingType::WALKSPEED_BUILDING;
    action.buildingId=id;action.fromLevel=1;action.targetLevel=2;action.workers=4;
    action.centerX=22;action.centerY=22;
    assert(ai.development_planner.reserve(world,action));
    ai.budget.allow_upgrades=true;
    ai.budget.upgrade_level1_racetrack_weight=10;
    assert(ai.issue_development_action(c,action));
    // Merely draining a queued order must not drop the optimistic upgrade flag.
    auto dispatched=c.getOrder(ai);
    assert(std::dynamic_pointer_cast<OrderConstruction>(dispatched));
    assert(c.buildings.is_building_upgrading(id));
    game.map.setResource(19,19,WOOD,1);
    building->launchConstruction(1,1);
    assert(building->constructionResultState==Building::NO_CONSTRUCTION);
    c.buildings.tick();assert(!c.buildings.is_building_upgrading(id));
    ai.timer=1000;ai.development_planner.observe(ai.collect_development_world(c));
    assert(ai.development_planner.actions().at(100).state==UpgradeBlocked);
    assert(ai.collect_development_limits(c).activeLevel1Upgrades==0);
    // An accepted repair shares the same register state and remains tracked
    // throughout its actual construction lifetime.
    building->hp-=1;
    assert(c.issue_upgrade_repair(id,true));c.orders.clear();
    building->launchConstruction(1,1);c.buildings.tick();
    assert(building->constructionResultState!=Building::NO_CONSTRUCTION);
    assert(c.buildings.is_building_upgrading(id));
    building->cancelConstruction(1);c.buildings.tick();
    assert(!c.buildings.is_building_upgrading(id));
}

// All swarms share specialist production, independently of prestige and labor share.
static void explorerSwarmStaffingRegressions()
{
    Game game(NULL); game.map.setSize(6,6,GRASS); game.map.setGame(&game);
    game.addTeam(); game.teams[0]->race.loadDefault();
    Player player; player.setTeam(game.teams[0]);
    const int swarmType=globalContainer->buildingsTypes.getTypeNum("swarm",0,false);
    for(int position:{10,30,50}) assert(game.addBuilding(position,position,swarmType,0));
    AIMaxima::Maxima ai(&player); Context& c=ai.context; c.initialize();
    TeamStat* stat=player.team->stats.getLatestStat(); stat->totalUnit=100;
    ai.budget.desired_explorers=5; ai.budget.explorer_ratio=1;
    ai.budget.worker_ratio=1;
    ai.budget.swarm_supply_radius=6;
    for(int y=0;y<64;++y) for(int x=0;x<64;++x)
        game.map.setMapDiscovered(x,y,player.team->me);
    for(int position:{30,50})
    {
        game.map.setResource(position+6,position+1,CORN,1);
        game.map.getTile(position+6,position+3).terrain=256;
    }
    // Swarm 0 runs empty; swarms 1 and 2 stay full. Staffing is each swarm's
    // own closed loop now, so there is no colony total to divide and no
    // apportionment to check: what matters is that the empty one ends up with
    // more carriers than the full ones.
    for(int id=0;id<3;++id)
        c.buildings.get_building(id)->resources[CORN]=id ? 20 : 0;
    ai.budget.staffing_window_samples=2;
    ai.budget.staffing_cooldown_passes=0;
    ai.budget.staffing_minimum_workers=1;
    ai.budget.staffing_maximum_workers=20;
    for(int prestige:{0,1})
    for(int budget:{0,2,3})
    {
        ai.snapshot.prestige=prestige;
        ai.budget.swarm_workers=budget;
        int workers[3]={0,0,0}, producers=0;
        for(int id=0;id<3;++id)
        {
            int explorers=-1;
            for(int pass=0;pass<8;++pass)
            {
                c.managementOrders.clear(); ai.manage_swarm(c,id);
                workers[id]=c.buildings.get_assigned(id);
                for(auto order:c.managementOrders)
                {
                    if(auto a=dynamic_cast<Management::AssignWorkers*>(order.get()))
                    {
                        workers[id]=a->workers;
                        c.buildings.get_building(id)->maxUnitWorking=a->workers;
                    }
                    if(auto r=dynamic_cast<Management::ChangeSwarm*>(order.get()))
                        explorers=r->explorer;
                }
            }
            if(explorers>0)
            {
                ++producers;
                assert(explorers==ai.budget.explorer_ratio);
            }
            // Every building keeps at least one carrier, whatever its stock.
            assert(workers[id]>=1);
        }
        // The starving swarm asks for more than the ones that are already full.
        assert(workers[0]>workers[1] && workers[0]>workers[2]);
        // Birth funding stays a colony decision even though staffing is local.
        assert(producers==(budget>0 ? 3 : 0));
    }
    // Reaching demand still disables the stream at every swarm.
    stat->numberUnitPerType[EXPLORER]=ai.budget.desired_explorers;
    for(int id=0;id<3;++id)
    {
        c.managementOrders.clear(); ai.manage_swarm(c,id);
        for(auto order:c.managementOrders)
            if(auto r=dynamic_cast<Management::ChangeSwarm*>(order.get())) assert(r->explorer==0);
    }
}

static void completedSwarmBudgetRegressions()
{
    Game game(NULL); game.map.setSize(6,6,GRASS); game.map.setGame(&game);
    game.addTeam(); game.teams[0]->race.loadDefault();
    Player player; player.setTeam(game.teams[0]);
    const int completeType=globalContainer->buildingsTypes.getTypeNum("swarm",0,false);
    const int siteType=globalContainer->buildingsTypes.getTypeNum("swarm",0,true);
    Building* complete=game.addBuilding(10,10,completeType,0); assert(complete);
    Building* site=game.addBuilding(30,30,siteType,0); assert(site);
    AIMaxima::Maxima ai(&player); Context& c=ai.context; c.initialize();
    for(int tick=0;tick<32;++tick) player.team->stats.step(player.team,true);
    ai.large_economy_committed=true;
    ai.environment.terrain_abundance=100; ai.environment.connected_abundance=100;
    ai.environment.food_security=100; ai.environment.food_headroom=75;
    ai.demands.growth=60;
    const auto verify=[&](int completed) {
        ai.snapshot=ai.collect_snapshot(c);
        assert(ai.snapshot.swarms==2 && ai.snapshot.completed_swarms==completed);
        ai.snapshot.population=100; ai.snapshot.workers=60;
        ai.environment.accessible_corn=1000;
        ai.build_policy_bids(); ai.arbitrate_policy_bids();
        assert(ai.budget.swarm_workers==AIMaxima::SwarmController::plan(60,100,0,0,1000,
            ai.strategy.economy.swarm_labor_scale_percent,ai.strategy.economy.swarm_food_per_worker_percent,
            ai.strategy.economy.swarm_pressure_sensitivity,ai.strategy.economy.swarm_workers_per_building).workers);
    };
    verify(1);
    for(int resource=0;resource<MAX_RESOURCES;++resource) site->resources[resource]=site->type->maxResource[resource];
    site->updateBuildingSite(); verify(2);
    // Repair changes available producers, not the colony-wide labor budget.
    complete->hp/=2; complete->launchConstruction(1,1);
    assert(complete->constructionResultState==Building::REPAIR); verify(1);
}

static void economicResourceAccessRegressions()
{
    Game game(NULL); game.map.setSize(6,6,GRASS); game.map.setGame(&game);
    game.addTeam(); game.teams[0]->race.loadDefault();
    Player player; player.setTeam(game.teams[0]);
    assert(game.addBuilding(10,10,globalContainer->buildingsTypes.getTypeNum("swarm",0,false),0));
    Unit* worker=game.addUnit(12,16,0,WORKER,0,0,0,0); assert(worker);
    // Both vertical water strips close the route around the toroidal map.
    for(int y=0;y<64;++y)
    { game.map.getTile(0,y).terrain=256; game.map.getTile(16,y).terrain=256; }
    for(int y=0;y<64;++y) for(int x=0;x<64;++x) game.map.setMapDiscovered(x,y,player.team->me);
    game.map.setResource(13,20,CORN,1);
    game.map.setResource(19,11,CORN,1);
    game.map.setResource(19,12,WOOD,1);
    game.map.setResource(19,13,STONE,1);
    AIMaxima::Maxima ai(&player); Context& c=ai.context; c.initialize();
    ai.snapshot.population=1; ai.snapshot.workers=1;
    // Food now reports whole fertility-equivalent tiles, not deposit counts.
    // These two sparse deposits together are below one full productive tile.
    ai.initialize_farming_cache(c);
    const auto fertility=ai.fertility_cache.at(13,20)+ai.fertility_cache.at(19,11);
    assert(fertility>0 && fertility<65536);
    const auto verify=[&](int wood,int stone) {
        ai.update_environment_model(c);
        assert(ai.environment.accessible_corn==0);
        assert(ai.environment.accessible_wood==wood);
        assert(ai.environment.accessible_stone==stone);
    };
    verify(0,0);
    worker->performance[SWIM]=1; verify(1,1);
    // Deposits reserved against harvesting must not become available supply.
    game.map.getTile(19,12).forbidden|=player.team->me; verify(0,1);
    game.map.getTile(19,12).forbidden&=~player.team->me;
    worker->performance[SWIM]=0; verify(0,0);
    // A local worker on the second island can reach its resources without swimming.
    assert(game.addUnit(20,16,0,WORKER,0,0,0,0)); verify(1,1);

    // Sharing connectivity must preserve algae's unit counts and shore access.
    game.map.setResource(16,24,ALGA,1);
    game.map.getTile(16,24).resource.amount=4;
    for(int y=18;y<=21;++y) for(int x=20;x<=23;++x) game.map.getTile(x,y).terrain=256;
    game.map.setResource(21,19,ALGA,1);
    game.map.getTile(21,19).resource.amount=3;
    ai.update_environment_model(c);
    assert(ai.known_algae_units==7 && ai.walk_accessible_algae_units==4);
    assert(ai.accessible_algae_units==4 && ai.environment.accessible_algae==1);
    worker->performance[SWIM]=1; ai.update_environment_model(c);
    assert(ai.accessible_algae_units==7 && ai.environment.accessible_algae==2);
}

static void repairLaborContractRegressions()
{
    using namespace AIMaximaPlacement;
    for(int level=0;level<3;++level)
    {
        Game game(NULL); game.map.setSize(6,6,GRASS); game.map.setGame(&game);
        game.addTeam(); game.teams[0]->race.loadDefault();
        Player player; player.setTeam(game.teams[0]);
        Building* inn=game.addBuilding(20,20,globalContainer->buildingsTypes.getTypeNum("inn",level,false),0);
        assert(inn); inn->hp/=2;
        AIMaxima::Maxima ai(&player); Context& c=ai.context; c.initialize();
        ai.finalize_director_plan(c);
        ai.configure_development_planner();
        WorldState world=ai.collect_development_world(c);
        DevelopmentLimits limits; limits.allowRepairs=true; limits.allowUpgrades=false;
        limits.newConstruction=1;
        DevelopmentAction action;
        assert(ai.development_planner.selectAction(world,
            std::vector<DevelopmentIntent>(),limits,action));
        assert(action.type==RepairBuilding && action.workers==1);
        assert(ai.issue_development_action(c,action));
        inn->launchConstruction(1,1);
        // launchConstruction leaves the building evacuating rather than ALIVE.
        // The engine drops worker orders until its site is live, so stand the
        // site up before expecting the repair crew to be assigned.
        inn->buildingState=::Building::ALIVE;
        c.update_management_orders();
        bool assigned=false;
        for(auto order:c.orders)
            if(auto a=std::dynamic_pointer_cast<OrderModifyBuilding>(order))
            {
                assert(a->gid==inn->gid && a->numberRequested==action.workers);
                assigned=true;
            }
        assert(assigned);
    }
}

// An upgrade only pays off once it finishes, so the site outranks its equals
// for workers while it is live and hands that advantage back on completion.
void upgradeWorkerPriorityRegressions()
{
    using namespace AIMaximaPlacement;
    Game game(NULL);
    game.map.setSize(6,6,GRASS); game.map.setGame(&game);
    game.addTeam(); game.teams[0]->race.loadDefault();
    Player player; player.setTeam(game.teams[0]);
    AIMaxima::Maxima ai(&player); Context& c=ai.context;
    const int raceType=globalContainer->buildingsTypes.getTypeNum("racetrack",0,false);
    ::Building* track=game.addBuilding(25,25,raceType,0);
    assert(track);
    c.initialize(); c.activeAI=&ai;
    for(int y=0;y<64;++y)for(int x=0;x<64;++x)
        game.map.setMapDiscovered(x,y,player.team->me);
    TeamStat* stat=player.team->stats.getLatestStat();
    stat->totalUnit=100; stat->upgradeState[BUILD][1]=20;
    ai.snapshot.population=100; ai.snapshot.schools=1;
    ai.budget.allow_upgrades=true;
    ai.configure_development_planner();
    ai.finalize_director_plan(c);
    WorldState world=ai.collect_development_world(c);
    ai.development_planner.adoptStartingBuildings(world);
    DevelopmentAction action;
    DevelopmentLimits limits=ai.collect_development_limits(c);
    assert(ai.development_planner.selectAction(world,{},limits,action));
    assert(action.type==UpgradeBuilding);
    assert(ai.issue_development_action(c,action));

    auto priorities=[&]()
    {
        std::vector<int> found;
        for(auto order:c.orders)
            if(auto change=std::dynamic_pointer_cast<OrderChangePriority>(order))
                found.push_back(change->priority);
        return found;
    };
    auto workerRequests=[&]()
    {
        std::vector<int> found;
        for(auto order:c.orders)
            if(auto assign=std::dynamic_pointer_cast<OrderModifyBuilding>(order))
                found.push_back(assign->numberRequested);
        return found;
    };

    // The engine has not started the site yet; nothing is due.
    c.orders.clear();
    c.update_management_orders();
    assert(priorities().empty());
    assert(workerRequests().empty());

    // The upgrade has begun but the building is still evacuating into its
    // site, so it is not ALIVE. Priority applies regardless, but the engine
    // would silently drop a worker order here, so none is issued yet.
    track->constructionResultState=::Building::UPGRADE;
    track->buildingState=::Building::WAITING_FOR_CONSTRUCTION;
    c.orders.clear();
    c.update_management_orders();
    assert(priorities()==std::vector<int>({1}));
    assert(workerRequests().empty());

    // Once the site is live it receives the configured upgrade staffing.
    track->buildingState=::Building::ALIVE;
    c.orders.clear();
    c.update_management_orders();
    assert(workerRequests()==std::vector<int>({ai.budget.upgrade_level1_workers}));

    // While it is still building, nothing further is issued.
    c.orders.clear();
    c.update_management_orders();
    assert(priorities().empty());

    // Completion restores the normal priority exactly once.
    track->constructionResultState=::Building::NO_CONSTRUCTION;
    c.buildings.tick();
    c.orders.clear();
    c.update_management_orders();
    assert(priorities()==std::vector<int>({0}));
    c.orders.clear();
    c.update_management_orders();
    assert(priorities().empty());
}

int main(int argc,char** argv)
{
    entityRoundTrip(Gradients::Entities::Building(7,2,true));
    entityRoundTrip(Gradients::Entities::Building(3,5,false));
    entityRoundTrip(Gradients::Entities::AnyTeamBuilding(4,true));
    entityRoundTrip(Gradients::Entities::Position(17,93));
    Management::AssignWorkers workers(7,103);
    payloadRoundTrip<Management::ManagementOrder>(workers);
    Construction::SinglePosition anchor(29,117);
    payloadRoundTrip<Construction::Constraint>(anchor);

    GlobalContainer container;
    globalContainer=&container;
    container.runNoX=true;
    container.buildingsTypes.init();
    {
        Map map;
        map.setSize(9,9,GRASS);
        Player player;
        player.map=&map;
        Context context(&player);
        Gradients::GradientInfo first;
        first.add_source(new Gradients::Entities::Position(13,27));
        first.add_obstacle(new Gradients::Entities::AnyResource);
        Gradients::GradientInfo second;
        second.add_source(new Gradients::Entities::Position(71,19));
        second.add_obstacle(new Gradients::Entities::AnyResource);
        Construction::BuildingOrder order(0,1);
        order.add_constraint(new Construction::MinimizedDistance(first,1));
        order.add_constraint(new Construction::MinimizedDistance(second,1));
        Gradients::GradientManager& manager=context.get_gradient_manager();
        order.queue_gradients(manager);
        manager.update(0);
        manager.update(1);
        assert(order.conditions_pass(context)==Conditions::Ready);
        // A 512x512 search takes longer than the 150-tick cache lifetime.
        // Every expiry must queue all stale dependencies and eventually resume.
        for(int epoch=0;epoch<4;++epoch)
        {
            const int start=2+epoch*160;
            for(int step=start;step<start+155;++step) manager.update(step);
            assert(order.conditions_pass(context)==Conditions::Waiting);
            manager.update(start+155);
            manager.update(start+156);
            assert(order.conditions_pass(context)==Conditions::Ready);
        }
    }

    if(argc>1&&std::string(argv[1])=="--production-only")
    {
        IntBuildingType::init();

        explorerSwarmStaffingRegressions();
        completedSwarmBudgetRegressions();
        std::cout << "explorer production and swarm budget regressions passed\n";
        return 0;
    }
    placementMaintenanceRegressions();
    upgradeClearingRegressions();
    rejectedPlacementUpgradeRegressions();
    if(argc>1&&std::string(argv[1])=="--placement-only")
    {
        std::cout << "placement maintenance and upgrade lifecycle regressions passed\n";
        return 0;
    }
    preemptiveSchedulingRegression();
    maximaBootstrapRegression();
    integrationRegressions();
    executionRegressions();
    reviewBugRegressions();
    missionForceRegressions();
    economyStaffingRegressions();
    explorerSwarmStaffingRegressions();
    completedSwarmBudgetRegressions();
    economicResourceAccessRegressions();
    repairLaborContractRegressions();
    innCompletionStaffingRegressions();
    directorUpgradePriorityRegressions();
    directorExecutionRegressions();
    directorUpgradeRegressions();
    upgradeWorkerPriorityRegressions();
    std::cout << "save, gradient refresh, farming restoration, reused GID and defense reserve regressions passed\n";
}
