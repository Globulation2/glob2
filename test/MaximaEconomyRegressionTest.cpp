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

namespace
{
struct Fixture
{
    Game game;
    Player player;
    std::unique_ptr<AIMaxima::Maxima> ai;
    Fixture() : game(NULL)
    {
        game.map.setSize(6,6,GRASS);
        game.map.setGame(&game);
        game.addTeam();
        game.teams[0]->race.loadDefault();
        player.setTeam(game.teams[0]);
        for(int y=0;y<64;++y) for(int x=0;x<64;++x)
        {
            game.map.setMapDiscovered(x,y,player.team->me);
            game.map.clearImmobileUnit(x,y);
        }
        ai.reset(new AIMaxima::Maxima(&player));
        player.team->stats.getLatestStat()->totalUnit=100;
    }
    Building* swarm(int x,int y,int workers=1)
    {
        Building* b=game.addBuilding(x,y,
            globalContainer->buildingsTypes.getTypeNum("swarm",0,false),0,workers,workers);
        assert(b); return b;
    }
    void supply(int x,int y)
    {
        game.map.setResource(x+6,y+1,CORN,1);
        game.map.getTile(x+6,y+3).terrain=256;
    }
    int population() const
    {
        int count=0;
        for(int id=0;id<Unit::MAX_COUNT;++id) count+=player.team->myUnits[id]!=NULL;
        return count;
    }
    // Apply the runtime's emitted staffing payloads to the actual buildings,
    // without a network player/session; production still uses Building::swarmStep.
    void applyStaffing()
    {
        Context& c=ai->context;
        c.update_management_orders();
        for(auto order:c.orders)
        {
            if(auto assignment=std::dynamic_pointer_cast<OrderModifyBuilding>(order))
            {
                Building* b=player.team->myBuildings[Building::GIDtoID(assignment->gid)];
                assert(b); b->maxUnitWorking=assignment->numberRequested;
            }
            else if(auto ratios=std::dynamic_pointer_cast<OrderModifySwarm>(order))
            {
                Building* b=player.team->myBuildings[Building::GIDtoID(ratios->gid)];
                assert(b);
                for(int type=0;type<NB_UNIT_TYPE;++type) b->ratio[type]=ratios->ratio[type];
            }
            else assert(false);
        }
        c.orders.clear();
    }
};

void birthBudgetScalesBeyondTwenty()
{
    using namespace AIMaxima::SwarmController;
    // Physical boundaries and monotonic responses, across realistic domains.
    for(int w=0;w<=1000;w+=5) for(int food:{0,5,50,500,5000}) {
        auto p=plan(w,1000,0,0,food,250,100,3,6);
        assert(p.workers>=0 && p.workers<=w && p.swarms>=1);
        assert(plan(w+1,1000,0,0,food,250,100,3,6).workers>=p.workers);
        assert(plan(w,1000,0,0,food+1,250,100,3,6).workers>=p.workers);
        assert(plan(w,1000,100,0,food,250,100,3,6).workers<=p.workers);
        if(food==0) assert(p.workers==0 && p.swarms==1);
    }
    assert(plan(1000,1000,0,0,5000,250,100,3,6).workers>20);
    auto a=plan(100,100,11,0,1000,250,100,3,6);
    auto b=plan(100,100,12,0,1000,250,100,3,6);
    auto c=plan(100,100,13,0,1000,250,100,3,6);
    assert(a.workers-b.workers<=1 && b.workers-c.workers<=1 && c.workers>0);
    assert(plan(100,100,12,12,1000,250,100,3,6).workers==b.workers);
    Fixture f; auto& ai=*f.ai;
    ai.snapshot.population=180; ai.snapshot.workers=120;
    ai.environment.accessible_corn=1000;
    ai.build_policy_bids(); ai.arbitrate_policy_bids();
    const int workers=ai.budget.swarm_workers,count=ai.budget.desired_swarms;
    for(int existing:{1,3,10}) for(int pressure:{0,50,100}) {
        ai.snapshot.completed_swarms=ai.snapshot.swarms=existing;
        ai.demands.food=ai.demands.survival=pressure;
        ai.environment.threat_pressure=pressure;
        ai.large_economy_committed=existing>1;
        ai.build_policy_bids(); ai.arbitrate_policy_bids();
        assert(ai.budget.swarm_workers==workers && ai.budget.desired_swarms==count);
    }
    // Expansion has its own construction permission even with no production
    // deficit. Core and colony intents remain distinct and share staffing.
    auto world=ai.collect_development_world(ai.context);
    ai.budget.colony_swarm_requested=true;
    ai.budget.desired_swarms=0;
    int colonies=0;
    for(const auto& intent:ai.collect_development_intents(world))
        if(intent.buildingType==IntBuildingType::SWARM_BUILDING) {
            assert(intent.purpose==AIMaximaPlacement::ColonySeed);
            assert(intent.unmetCount==1); ++colonies;
        }
    assert(colonies==1);
    ai.budget.colony_swarm_requested=false;
    for(const auto& intent:ai.collect_development_intents(world))
        assert(intent.buildingType!=IntBuildingType::SWARM_BUILDING);
    ai.snapshot.critical_food=30; ai.build_policy_bids(); ai.arbitrate_policy_bids();
    assert(ai.budget.swarm_workers>0 && ai.budget.swarm_workers<workers);
}

void colonyStartupAndAffordability()
{
    Fixture f; Building* swarm=f.swarm(32,32); auto& ai=*f.ai;
    ai.context.initialize();
    ai.snapshot.workers=30; ai.snapshot.population=45;
    ai.snapshot.free_workers=10; ai.snapshot.worker_jobs_open=0;
    ai.posture=AIMaxima::Maxima::PostureRecover;
    ai.finalize_director_plan(ai.context);
    assert(ai.budget.colony_swarm_requested); // Recovery is not an expansion veto.
    ai.snapshot.free_workers=0;
    ai.finalize_director_plan(ai.context);
    assert(!ai.budget.colony_swarm_requested);
    ai.snapshot.free_workers=10;
    int id=-1;
    for(const auto& entry:ai.context.get_building_register().found())
        if(ai.context.get_building_register().get_building(entry.first)==swarm)id=entry.first;
    assert(id>=0);
    AIMaximaPlacement::DevelopmentAction action;
    action.id=123; action.buildingId=id; action.purpose=AIMaximaPlacement::ColonySeed;
    action.state=AIMaximaPlacement::Completed;
    // This is a lifecycle fixture; normal production populates this action map.
    auto& actions=const_cast<std::map<int,AIMaximaPlacement::DevelopmentAction>&>(ai.development_planner.actions());
    actions[action.id]=action;
    ai.finalize_director_plan(ai.context);
    assert(!ai.budget.colony_swarm_requested);
    Unit* worker=f.game.addUnit(20,20,0,WORKER,0,0,0,0); assert(worker);
    swarm->unitsWorking.push_back(worker);
    swarm->resources[CORN]=swarm->type->resourceForOneUnit;
    ai.finalize_director_plan(ai.context);
    assert(ai.budget.colony_swarm_requested);
    swarm->unitsWorking.clear(); swarm->resources[CORN]=0;
    ai.finalize_director_plan(ai.context);
    assert(ai.budget.colony_swarm_requested); // Later idleness is not a new startup.
}

void growingFoodFundsCapacity()
{
    Fixture f; f.swarm(10,10,0); f.supply(10,10);
    for(int y=8;y<=24;++y) for(int x=21;x<=35;++x)
        f.game.map.getTile(x,y).terrain=256;
    auto& ai=*f.ai; auto& c=ai.context; c.initialize();
    ai.snapshot.population=120; ai.snapshot.workers=90;
    ai.update_environment_model(c); ai.build_policy_bids(); ai.arbitrate_policy_bids();
    const int food=ai.environment.accessible_corn;
    const int workers=ai.budget.swarm_workers;
    for(int y=12;y<=20;++y) for(int x=12;x<=20;++x)
        if(f.game.map.isGrass(x,y)) f.game.map.setResource(x,y,CORN,1);
    ai.update_environment_model(c); ai.build_policy_bids(); ai.arbitrate_policy_bids();
    assert(ai.environment.accessible_corn>food);
    assert(ai.budget.swarm_workers>workers);
    assert(ai.budget.desired_swarms>1);
}

void soleSwarmKeepsBirthBudget()
{
    Fixture f;Building* swarm=f.swarm(10,10,0);
    auto& ai=*f.ai;auto& c=ai.context;c.initialize();
    assert(ai.nearby_farm_capacity(c,0)==0);
    for(int budget:{10,0,2}) {
        ai.budget.swarm_workers=budget;
        ai.manage_swarm(c,0);f.applyStaffing();
        assert(swarm->maxUnitWorking==budget);
    }
}

void nearbyCornDeterminesStaffing()
{
    Fixture f;
    Building* first=f.swarm(10,10,0);
    Building* second=f.swarm(30,10,0);
    f.supply(10,10); f.supply(30,10);
    auto& ai=*f.ai; auto& c=ai.context; c.initialize();
    ai.budget.swarm_supply_radius=6; ai.budget.swarm_workers=10;

    const long long before=ai.nearby_farm_capacity(c,0);
    assert(before>0);
    for(int amount=0;amount<=8;++amount)
    {
        f.game.map.getTile(16,11).resource.amount=amount;
        assert(ai.nearby_farm_capacity(c,0)==(amount>0 ? before : 0));
    }
    auto world=ai.collect_development_world(c);
    assert(world.tile(16,11).foodOpportunity>0);
    assert(world.tile(15,11).fertility>0);
    assert(world.tile(15,11).foodOpportunity==0);
    assert(world.tile(15,11).farmCapacity==0);
    f.game.map.setNoResource(16,11,1);
    assert(ai.nearby_farm_capacity(c,0)==0);
    world=ai.collect_development_world(c);
    assert(world.tile(16,11).foodOpportunity==0);
    assert(world.tile(16,11).farmCapacity==0);
    f.game.map.setResource(16,11,CORN,1);
    for(int id=0;id<2;++id) ai.manage_swarm(c,id);
    f.applyStaffing();
    assert(first->maxUnitWorking+second->maxUnitWorking==10);
    assert(first->maxUnitWorking>0 && second->maxUnitWorking>0);
    std::set<int> shared;
    assert(ai.nearby_farm_capacity(c,0,&shared)==before);
    assert(ai.nearby_farm_capacity(c,0,&shared)==0);
    f.game.map.unsetMapDiscovered();
    assert(ai.nearby_farm_capacity(c,0)==0);
}

void cornPileInteriorIsSupply()
{
    Fixture f;
    f.swarm(30,10); f.supply(30,10); f.supply(10,10);
    Building* inn=f.game.addBuilding(10,10,
        globalContainer->buildingsTypes.getTypeNum("inn",0,false),0,0,0);
    assert(inn);
    auto& ai=*f.ai; auto& c=ai.context; c.initialize();
    ai.budget.swarm_supply_radius=10;
    const long long before=ai.nearby_farm_capacity(c,0);
    for(int y=9;y<=11;++y) for(int x=37;x<=39;++x)
        f.game.map.setResource(x,y,CORN,1);
    const long long planted=ai.nearby_farm_capacity(c,0);
    assert(planted>before);
    f.game.map.setResource(38,10,STONE,1);
    const long long withoutCenter=ai.nearby_farm_capacity(c,0);
    assert(withoutCenter<planted);
    f.game.map.setNoResource(38,10,1);
    assert(ai.nearby_farm_capacity(c,0)==withoutCenter);
    ai.budget.inn_adaptive_staffing_enabled=true;
    ai.budget.inn_normal_workers[0]=1; ai.budget.inn_low_corn_workers[0]=2;
    ai.manage_inn(c,1); f.applyStaffing();
    const int workers=inn->maxUnitWorking;
    assert(workers>0);
    for(int y=0;y<64;++y) for(int x=0;x<64;++x)
        if(f.game.map.getResource(x,y).type==CORN)
            f.game.map.setNoResource(x,y,1);
    ai.manage_inn(c,1); f.applyStaffing();
    assert(inn->maxUnitWorking==0);
}

void explorerTargetAlwaysGetsProduction()
{
    Fixture f;
    Building* first=f.swarm(10,10), *second=f.swarm(30,30);
    f.supply(10,10); f.supply(30,30);
    auto& ai=*f.ai; auto& c=ai.context; c.initialize();
    auto& growth=ai.policy_bids[AIMaxima::Maxima::PolicyGrowth];
    auto& access=ai.policy_bids[AIMaxima::Maxima::PolicyAccess];
    auto& offense=ai.policy_bids[AIMaxima::Maxima::PolicyOffense];
    auto* stat=f.player.team->stats.getLatestStat();
    growth.worker_ratio=4;
    access.desired_explorers=14;
    // Zero utility bids must still replenish the full target, even when the
    // existing count already exceeds the old three-explorer scouting floor.
    for(int prestige : {0,100})
    for(int requestedWeight : {0,2,3})
    for(int birthWorkers : {0,6})
    for(int explorers : {0,3,13,14,15})
    {
        ai.snapshot.prestige=prestige;
        ai.snapshot.explorers=explorers;
        stat->numberUnitPerType[EXPLORER]=explorers;
        offense.explorer_ratio=requestedWeight;
        growth.swarm_workers=birthWorkers;
        ai.arbitrate_policy_bids();
        assert(ai.budget.desired_explorers==14);
        assert(ai.budget.explorer_ratio==std::max(1,requestedWeight));
        ai.manage_swarm(c,0); ai.manage_swarm(c,1); f.applyStaffing();
        const int expected=birthWorkers>0 && explorers<14
            ? std::max(1,requestedWeight) : 0;
        assert(first->ratio[EXPLORER]==expected);
        assert(second->ratio[EXPLORER]==expected);
    }
}

void crisisProductionPause()
{
    for(int expiredTimer : {0,-1})
    {
        Fixture f;
        Building* swarm=f.swarm(10,10,6); f.supply(10,10);
        auto& ai=*f.ai; auto& c=ai.context; c.initialize();
        ai.snapshot.population=100;
        ai.snapshot.unserved_food=20;
        auto& growth=ai.policy_bids[AIMaxima::Maxima::PolicyGrowth];
        auto& survival=ai.policy_bids[AIMaxima::Maxima::PolicySurvival];
        auto& access=ai.policy_bids[AIMaxima::Maxima::PolicyAccess];
        auto& defense=ai.policy_bids[AIMaxima::Maxima::PolicyDefense];
        growth.swarm_workers=0; survival.swarm_workers=6; growth.worker_ratio=4;
        access.explorer_ratio=1; access.desired_explorers=5;
        defense.warrior_ratio=2; defense.desired_warriors=10;
        ai.arbitrate_policy_bids();
        assert(ai.budget.swarm_workers==0);
        ai.manage_swarm(c,0); f.applyStaffing();
        assert(swarm->maxUnitWorking==0);
        for(int type=0;type<NB_UNIT_TYPE;++type) assert(swarm->ratio[type]==0);
        swarm->resources[CORN]=20;
        swarm->productionTimeout=expiredTimer;
        const int before=f.population();
        for(int tick=0;tick<1000;++tick) swarm->swarmStep();
        // The existing engine cannot cancel an already-expired timer through
        // AI orders. That one pending birth may finish; further births stop.
        const int pending=expiredTimer<0 ? 1 : 0;
        assert(f.population()==before+pending && swarm->resources[CORN]==20-5*pending);
        assert(swarm->productionTimeout==(pending ? swarm->type->unitProductionTime : expiredTimer));

        // Funding returns through the same director and executor path.
        ai.snapshot.unserved_food=0;
        growth.swarm_workers=6;
        ai.arbitrate_policy_bids();
        assert(ai.budget.swarm_workers==6);
        ai.manage_swarm(c,0); f.applyStaffing();
        assert(swarm->ratio[WORKER]==4 && swarm->ratio[EXPLORER]==1
            && swarm->ratio[WARRIOR]==2);
        for(int tick=0;tick<=swarm->type->unitProductionTime;++tick) swarm->swarmStep();
        assert(f.population()==before+pending+1 && swarm->resources[CORN]==15-5*pending);
    }
}

void completionReallocatesColony()
{
    Fixture f;
    Building* old=f.swarm(10,10,6); f.supply(10,10); f.supply(28,28);
    auto& ai=*f.ai; auto& c=ai.context; c.initialize(); c.activeAI=&ai;
    ai.budget.swarm_workers=6;

    ai.budget.worker_ratio=4;
    ai.budget.explorer_ratio=1; ai.budget.desired_explorers=5;
    ai.budget.resource_tracker_samples=25;
    AIMaximaPlacement::DevelopmentAction action;
    action.type=AIMaximaPlacement::BuildStandalone;
    action.buildingType=IntBuildingType::SWARM_BUILDING;
    action.centerX=30; action.centerY=30; action.workers=2;
    action.initialFootprint=AIMaximaPlacement::Footprint(-2,-2,4,4);
    assert(ai.issue_development_action(c,action));
    const int id=c.previousBuildingId;
    Building* fresh=f.game.addBuilding(28,28,
        globalContainer->buildingsTypes.getTypeNum("swarm",0,true),0,1,1);
    assert(fresh);
    c.orders.clear(); c.buildings.tick(); f.applyStaffing();
    assert(old->maxUnitWorking==6 && fresh->maxUnitWorking==2);
    for(int resource=0;resource<MAX_RESOURCES;++resource)
        fresh->resources[resource]=fresh->type->maxResource[resource];
    fresh->updateBuildingSite();
    assert(fresh->maxUnitWorking==1);
    c.buildings.tick(); f.applyStaffing();
    assert(c.get_resource_tracker(id));
    assert(old->maxUnitWorking==3 && fresh->maxUnitWorking==3);
    assert(old->ratio[EXPLORER]==1 && fresh->ratio[EXPLORER]==1);
}

struct IdleAI : RuntimeAI
{
    void tick(Context&) override {}
    void handle_event(Context&,const RuntimeEvent&) override {}
};

void trackerLogicalCadence()
{
    // Cover each staggered housekeeping phase, and queued engine orders that
    // deliberately do not advance the runtime's logical clock.
    for(int phase=0;phase<4;++phase)
    {
        Fixture f;
        Building* swarm=f.swarm(10,10);
        auto& c=f.ai->context; c.initialize(); c.timer=phase;
        swarm->resources[CORN]=20;
        c.add_resource_tracker(new Management::ResourceTracker(c,0,25,CORN),0);
        IdleAI idle;
        auto tracker=c.get_resource_tracker(0);
        const auto tick=[&]() {++f.game.stepCounter; c.getOrder(idle);};
        for(int i=0;i<9;++i) tick();
        assert(tracker->get_age()==9 && tracker->get_total_level()==0);
        for(int i=0;i<3;++i) c.push_order(std::shared_ptr<Order>(new NullOrder));
        for(int i=0;i<3;++i) tick();
        assert(tracker->get_age()==9);
        tick();
        assert(tracker->get_age()==10 && tracker->get_total_level()==20);
        for(int i=10;i<250;++i) tick();
        assert(tracker->get_age()==250 && tracker->get_total_level()==500);
        swarm->resources[CORN]=0;
        for(int i=0;i<250;++i) tick();
        assert(tracker->get_age()==500 && tracker->get_total_level()==0);
    }
}

void retirementRequiresHarvestRoute()
{
    Fixture f;
    f.swarm(10,10); f.supply(10,10);
    auto& ai=*f.ai; auto& c=ai.context; c.initialize();
    ai.budget.swarm_retirement_enabled=true; ai.budget.desired_swarms=1;
    ai.timer=1; ai.update_swarm_retirement(c);
    assert(ai.remote_swarm_since.empty());
    f.game.map.setNoResource(16,11,1);
    ai.timer=4000; ai.update_swarm_retirement(c);
    assert(ai.remote_swarm_since.count(0));
    // Explicitly forbidden territory is not available productive capacity.
    for(int y=0;y<64;++y) for(int x=0;x<64;++x)
        f.game.map.setForbidden(x,y,f.player.team->me);
    ai.update_swarm_retirement(c);
    assert(ai.remote_swarm_since.count(0));
    ai.timer=7000; ai.update_swarm_retirement(c);
    assert(ai.remote_swarms_ready.count(0));
}

void holidayHarvestCapacity()
{
    Game game(NULL);
    FILE* file=fopen("maps/Holiday_Island_2.map","rb"); assert(file);
    BinaryInputStream input(new FileStreamBackend(file)); assert(game.load(&input));
    Player player; player.setTeam(game.teams[0]);
    AIMaxima::Maxima ai(&player); auto& c=ai.context; c.initialize();
    ai.budget.can_swim=true;
    Map& map=game.map;
    for(int y=0;y<map.getH();++y) for(int x=0;x<map.getW();++x)
        map.setMapDiscovered(x,y,player.team->me);
    std::map<int,long long> before;
    SearchTools::BuildingSearch buildings(c);
    buildings.add_condition(new Conditions::NotUnderConstruction);
    for(auto i=buildings.begin();i!=buildings.end();++i)
        before[*i]=ai.nearby_farm_capacity(c,*i);
    assert(!before.empty());
    int productive=0; for(auto entry:before) productive+=entry.second>0;
    assert(productive>0);
    for(int y=0;y<map.getH();++y) for(int x=0;x<map.getW();++x)
        if(map.getResource(x,y).type==CORN) map.setNoResource(x,y,1);
    for(auto entry:before) assert(ai.nearby_farm_capacity(c,entry.first)==0);
}

}

int main()
{
    GlobalContainer container; globalContainer=&container; container.runNoX=true;
    container.buildingsTypes.init();
    IntBuildingType::init();


    birthBudgetScalesBeyondTwenty();
    growingFoodFundsCapacity();
    colonyStartupAndAffordability();
    soleSwarmKeepsBirthBudget();
    nearbyCornDeterminesStaffing();
    cornPileInteriorIsSupply();
    explorerTargetAlwaysGetsProduction();
    crisisProductionPause();
    completionReallocatesColony();
    trackerLogicalCadence();
    retirementRequiresHarvestRoute();
    holidayHarvestCapacity();
    std::cout<<"Maxima economy regression tests passed\n";
}
