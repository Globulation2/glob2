#include "Version.h"
// Link with the game objects (excluding Glob2.cpp) to exercise the real runtime.
#include "../../src/GlobalContainer.h"
#include "../../src/Game.h"
#include "../../src/team/Team.h"
#include "../../src/ai/AIImplementation.h"
#include "../../src/map/Map.h"
#include "../../src/Order.h"
#include "../../src/Player.h"
#include "../../src/TeamStat.h"
#include "../../src/Utilities.h"
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
#include "../../src/ai/maxima/AIMaximaSwarmController.h"
#undef private
#include "../../src/building/Building.h"
#include "../../src/game/entities/BuildingType.h"
#include "../../src/building/IntBuildingType.h"
#include "../../src/unit/Unit.h"
#include "../../src/ai/maxima/AIMaximaContinuation.h"
#include <BinaryStream.h>
#include <StreamBackend.h>
#include <cassert>
#include <bit>
#include <cstdlib>
#include <fstream>
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
        game.setWaitingOnMask(0);
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
        game.map.setResource(x+6,y+1,WHEAT,1);
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
    // City States seed 7: positive starter farms used to be truncated to zero,
    // disabling every birth despite having enough supply to fund one worker.
    for(int supply:{2483,6554,6901,15984,17699,23193,23594})
    {
        assert(plan(4,4,0,0,supply,275,25,6,6,65536).workers==1);
        assert(plan(4,4,0,0,supply/65536,275,25,6,6).workers==0);
    }
    {
        Fixture opening;
        Building* swarm=opening.swarm(32,32);
        auto& ai=*opening.ai;
        ai.context.initialize();
        ai.snapshot.population=ai.snapshot.workers=4;
        opening.player.team->stats.getLatestStat()->totalUnit=4;
        ai.environment.accessible_corn=0;
        ai.environment.accessible_corn_fraction=17699;
        ai.build_policy_bids(); ai.arbitrate_policy_bids();
        assert(ai.budget.swarm_workers==1);
        ai.manage_swarm(ai.context,0); opening.applyStaffing();
        assert(swarm->ratio[WORKER]>0);
        ai.environment.accessible_corn_fraction=0;
        ai.build_policy_bids(); ai.arbitrate_policy_bids();
        ai.manage_swarm(ai.context,0); opening.applyStaffing();
        assert(swarm->ratio[WORKER]+swarm->ratio[EXPLORER]+swarm->ratio[WARRIOR]==0);
    }
    // Physical boundaries and monotonic responses, across realistic domains.
    for(int w=0;w<=1000;w+=5) for(int food:{0,5,50,500,5000}) {
        auto p=plan(w,1000,0,0,food,250,100,3,6);
        assert(plan(w,1000,0,0,food*65536LL,250,100,3,6,65536).workers==p.workers);
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
    swarm->resources[WHEAT]=swarm->type->resourceForOneUnit;
    ai.finalize_director_plan(ai.context);
    assert(ai.budget.colony_swarm_requested);
    swarm->unitsWorking.clear(); swarm->resources[WHEAT]=0;
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
        if(f.game.map.isGrass(x,y)) f.game.map.setResource(x,y,WHEAT,1);
    ai.update_environment_model(c); ai.build_policy_bids(); ai.arbitrate_policy_bids();
    assert(ai.environment.accessible_corn>food);
    assert(ai.budget.swarm_workers>workers);
    assert(ai.budget.desired_swarms>1);
}

void distantWheatFundsRecovery()
{
    Fixture f;auto swarm=f.swarm(10,10);f.supply(30,10);
    auto& ai=*f.ai;auto& c=ai.context;c.initialize();
    ai.snapshot.population=4;ai.snapshot.workers=4;ai.budget.swarm_supply_radius=12;
    assert(ai.nearby_farm_capacity(c,0)==0);
    ai.update_environment_model(c);ai.build_policy_bids();ai.arbitrate_policy_bids();
    assert(ai.environment.accessible_corn*65536LL+ai.environment.accessible_corn_fraction>0);
    assert(ai.budget.swarm_workers>0);
    ai.manage_swarm(c,0);f.applyStaffing();assert(swarm->ratio[WORKER]>0);
    // Full stores cannot manufacture recurring capacity when no wheat remains.
    f.game.map.setNoResource(36,11,1);
    swarm->resources[WHEAT]=swarm->type->maxResource[WHEAT];
    ai.update_environment_model(c);ai.build_policy_bids();ai.arbitrate_policy_bids();
    assert(ai.budget.swarm_workers==0);
    f.game.map.setResource(36,11,WHEAT,1);
    // A full water barrier (including the wrap edge) cuts off nonswimmers.
    for(int y=0;y<64;++y)for(int x:{0,25})f.game.map.getTile(x,y).terrain=256;
    ai.fertility_cache=AIMaxima::Farming::ExactFertilityCache();
    ai.budget.can_swim=false;
    ai.update_environment_model(c);ai.build_policy_bids();ai.arbitrate_policy_bids();
    assert(ai.budget.swarm_workers==0);
}

void soleSwarmStaffsFromItsOwnStock()
{
    Fixture f;Building* swarm=f.swarm(10,10,0);
    auto& ai=*f.ai;auto& c=ai.context;c.initialize();
    assert(ai.nearby_farm_capacity(c,0)==0);
    // Seed neutralised: these cases exercise the control loop, not the
    // starting staffing a newly built building is given.
    ai.budget.staffing_new_swarm_workers=1;
    ai.budget.staffing_window_samples=2;
    ai.budget.staffing_cooldown_passes=0;
    ai.budget.staffing_minimum_workers=1;
    ai.budget.staffing_maximum_workers=20;
    // Carriers no longer come from the colony birth budget: the swarm reads
    // its own wheat stock. A full swarm holds the minimum whatever the budget
    // says, and an empty one asks for more.
    swarm->resources[WHEAT]=swarm->type->maxResource[WHEAT];
    for(int budget:{10,0,2}) {
        ai.budget.swarm_workers=budget;
        for(int pass=0;pass<6;++pass){ai.manage_swarm(c,0);f.applyStaffing();}
        assert(swarm->maxUnitWorking==1);
    }
    swarm->resources[WHEAT]=0;
    for(int pass=0;pass<6;++pass){ai.manage_swarm(c,0);f.applyStaffing();}
    assert(swarm->maxUnitWorking>1);
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
    const int originalAmount=f.game.map.getTile(16,11).resource.amount;
    // A cell with no wheat is no supply. A cell with wheat supplies its
    // recurring growth plus the stock standing on it, amortised over the
    // planning horizon, so a deeper stack is more supply and not the same
    // supply: on ground where nothing regrows the stock is the only food there
    // is, and treating it as nothing left Maxima unable to play such a map.
    long long previous=0;
    for(int amount=0;amount<=8;++amount)
    {
        f.game.map.getTile(16,11).resource.amount=amount;
        const long long capacity=ai.nearby_farm_capacity(c,0);
        if(amount==0) assert(capacity==0);
        else assert(capacity>0 && capacity>=previous);
        previous=capacity;
    }
    f.game.map.getTile(16,11).resource.amount=originalAmount;
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
    f.game.map.setResource(16,11,WHEAT,1);
    ai.budget.staffing_new_swarm_workers=1;
    ai.budget.staffing_window_samples=2;
    ai.budget.staffing_cooldown_passes=0;
    ai.budget.staffing_minimum_workers=1;
    ai.budget.staffing_maximum_workers=20;
    // Staffing no longer divides a colony total between swarms by nearby corn.
    // Each reads its own stock, so the empty one outgrows the full one and
    // neither drops below the minimum.
    first->resources[WHEAT]=0;
    second->resources[WHEAT]=second->type->maxResource[WHEAT];
    for(int pass=0;pass<6;++pass)
    {
        for(int id=0;id<2;++id) ai.manage_swarm(c,id);
        f.applyStaffing();
    }
    assert(first->maxUnitWorking>second->maxUnitWorking);
    assert(first->maxUnitWorking>0 && second->maxUnitWorking>0);
    std::set<int> shared;
    // Two readings of the same patch at the same moment must agree, and the
    // second must take nothing, because the first claimed every tile. The
    // comparison is against a reading taken here rather than against the one
    // at the top of the test: farming has since published its protection mask,
    // which admits tiles that were forbidden before, so the colony genuinely
    // reaches more wheat than it did then.
    const long long steady=ai.nearby_farm_capacity(c,0);
    assert(steady>0);
    assert(ai.nearby_farm_capacity(c,0,&shared)==steady);
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
        f.game.map.setResource(x,y,WHEAT,1);
    const long long planted=ai.nearby_farm_capacity(c,0);
    assert(planted>before);
    f.game.map.setResource(38,10,STONE,1);
    const long long withoutCenter=ai.nearby_farm_capacity(c,0);
    assert(withoutCenter<planted);
    f.game.map.setNoResource(38,10,1);
    assert(ai.nearby_farm_capacity(c,0)==withoutCenter);
    // Staffing is a closed loop on the inn's own stock: an empty inn asks for
    // another carrier, a full one hands them back, and it never falls below the
    // minimum. Wheat growing nearby does not enter into the decision.
    ai.budget.staffing_new_inn_workers=1;
    ai.budget.staffing_window_samples=2;
    ai.budget.staffing_cooldown_passes=0;
    ai.budget.staffing_low_permille=333;
    ai.budget.staffing_high_permille=667;
    ai.budget.staffing_slack=1;
    ai.budget.staffing_minimum_workers=1;
    ai.budget.staffing_maximum_workers=20;
    inn->resources[WHEAT]=0;
    for(int pass=0;pass<6;++pass){ai.manage_inn(c,1); f.applyStaffing();}
    assert(inn->maxUnitWorking>=2);
    inn->resources[WHEAT]=inn->type->maxResource[WHEAT];
    for(int pass=0;pass<12;++pass){ai.manage_inn(c,1); f.applyStaffing();}
    assert(inn->maxUnitWorking==1);
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

void armyDemandFundsTrainingAndBirthMix()
{
    for(bool foodEmergency:{false,true}) {
        Fixture f;
        auto* swarm=f.swarm(40,40,6);f.supply(40,40);
        // The saved colony had one working barracks and four upgrade sites.
        for(int i=0;i<5;++i)
            assert(f.game.addBuilding(4+6*i,4,globalContainer->buildingsTypes
                .getTypeNum("barracks",i<3?2:1,i!=0),0));
        auto& a=*f.ai;auto& c=a.context;c.initialize();
        a.snapshot.population=533;a.snapshot.workers=491;
        a.snapshot.warriors=30;a.snapshot.trained_warriors=2;
        a.snapshot.barracks=5;a.snapshot.worker_jobs_open=43;
        a.snapshot.unserved_food=foodEmergency?300:0;
        a.labour_observation=a.observe_labour(c);
        a.labour_observation.workers=491;
        a.labour_observation.innCarriers=141;
        a.labour_observation.swarmCarriers=82;
        a.labour_observation.builders=53;
        auto& growth=a.policy_bids[AIMaxima::Maxima::PolicyGrowth];
        auto& defense=a.policy_bids[AIMaxima::Maxima::PolicyDefense];
        growth.worker_ratio=5;growth.swarm_workers=30;
        defense.desired_warriors=120;defense.desired_barracks=3;
        defense.utility=100;defense.warrior_ratio=3;
        a.arbitrate_policy_bids();
        assert(a.budget.desired_barracks>5);
        assert(a.budget.worker_ratio==0 && a.budget.warrior_ratio==3);
        f.player.team->stats.getLatestStat()->numberUnitPerType[WARRIOR]=30;
        a.manage_swarm(c,0);f.applyStaffing();
        assert(swarm->ratio[WORKER]==0 && swarm->ratio[WARRIOR]==3);
        // Backpressure must pause surplus-worker births too, not redirect
        // all funded food into workers when the training queue fills.
        a.snapshot.warriors=60;
        a.arbitrate_policy_bids();
        assert(a.budget.worker_ratio==0 && a.budget.warrior_ratio==0);
        // Genuine job growth still funds replacements and expansion.
        a.snapshot.worker_jobs_open=400;
        a.arbitrate_policy_bids();
        assert(a.budget.worker_ratio==5);
        // The military preference ends when its target is met.
        a.snapshot.worker_jobs_open=43;a.snapshot.warriors=120;
        a.arbitrate_policy_bids();
        assert(a.budget.worker_ratio==5);
    }
}

void barracksUpgradesKeepTrainingOpen()
{
    using namespace AIMaximaPlacement;
    Fixture f;
    for(int i=0;i<2;++i)
        assert(f.game.addBuilding(4+6*i,4,globalContainer->buildingsTypes
            .getTypeNum("barracks",0,false),0));
    auto& a=*f.ai;auto& c=a.context;c.initialize();
    a.snapshot.warriors=20;a.snapshot.trained_warriors=0;
    a.budget.upgrade_level1_barracks_weight=50;
    auto limits=a.collect_development_limits(c);
    assert(limits.upgradePriority(IntBuildingType::ATTACK_BUILDING,1)>0);
    DevelopmentAction action;
    action.id=7;action.type=UpgradeBuilding;action.state=CreateIssued;
    action.buildingType=IntBuildingType::ATTACK_BUILDING;
    action.buildingId=0;action.fromLevel=1;action.targetLevel=2;
    auto& actions=const_cast<std::map<int,DevelopmentAction>&>(a.development_planner.actions());
    actions[action.id]=action;
    const auto seats=a.barracks_capacity(c);
    assert(seats.first==2 && seats.second==6);
    limits=a.collect_development_limits(c);
    assert(limits.upgradePriority(IntBuildingType::ATTACK_BUILDING,1)==0);
    // An unissued reservation must not prevent its own first upgrade.
    actions[action.id].state=ParcelReserved;
    limits=a.collect_development_limits(c,action.id);
    assert(limits.upgradePriority(IntBuildingType::ATTACK_BUILDING,1)>0);
    // Once the army is trained, this queue-protection gate no longer applies.
    actions[action.id].state=CreateIssued;
    a.snapshot.trained_warriors=20;
    limits=a.collect_development_limits(c);
    assert(limits.upgradePriority(IntBuildingType::ATTACK_BUILDING,1)>0);
}

void armyBirthsMatchPlatformChecksums()
{
    setSyncRandSeed(5489);
    Fixture f;
    f.game.gameHeader.setHungerDisabled(true);
    auto* swarm=f.swarm(10,10,6);f.supply(10,10);
    assert(f.game.addBuilding(18,10,globalContainer->buildingsTypes
        .getTypeNum("barracks",0,false),0));
    for(int i=0;i<50;++i)assert(f.game.addUnit(24+i%10,24+i/10,0,WORKER,0,0,0,0));
    // addBuilding is the editor path; a playable map also builds team lists.
    f.player.team->playersMask=1;
    f.player.team->createLists();
    auto& a=*f.ai;auto& c=a.context;c.initialize();
    a.snapshot.population=50;a.snapshot.workers=50;a.snapshot.barracks=1;
    a.labour_observation=a.observe_labour(c);
    auto& growth=a.policy_bids[AIMaxima::Maxima::PolicyGrowth];
    auto& defense=a.policy_bids[AIMaxima::Maxima::PolicyDefense];
    growth.worker_ratio=5;growth.swarm_workers=6;
    defense.desired_warriors=12;defense.warrior_ratio=3;defense.utility=100;
    a.arbitrate_policy_bids();a.manage_swarm(c,0);f.applyStaffing();
    assert(swarm->ratio[WORKER]==0 && swarm->ratio[WARRIOR]>0);
    swarm->resources[WHEAT]=swarm->type->maxResource[WHEAT];
    swarm->productionTimeout=-1;
    const char* path="test/maxima/fixtures/army-birth-checksums.txt";
    const bool record=std::getenv("GLOB2_RECORD_ARMY_CHECKSUMS")!=nullptr;
    std::ifstream expected;
    std::ofstream output;
    if(record)output.open(path);else expected.open(path);
    assert(record?output.good():expected.good());
    for(int tick=1;tick<=512;++tick) {
        f.game.syncStep(0);assert(f.game.stepCounter==unsigned(tick));
        // Preserve the format-115 baseline across save-format bumps. With one
        // team and no players, the header version is rotated six times.
        const Uint32 checksum=f.game.checkSum(nullptr,nullptr,nullptr,true)
            ^ std::rotr(Uint32(f.game.mapHeader.getVersionMinor()^115),6);
        if(record)output<<tick<<' '<<checksum<<'\n';
        else {
            int expectedTick;Uint32 expectedChecksum;
            assert(expected>>expectedTick>>expectedChecksum);
            if(expectedTick!=tick || expectedChecksum!=checksum)
                std::cerr<<"Army birth checksum mismatch: tick="<<tick
                    <<" actual="<<checksum<<" expected="<<expectedChecksum<<'\n';
            assert(expectedTick==tick && expectedChecksum==checksum);
        }
    }
    int workers=0,warriors=0;
    for(int i=0;i<Unit::MAX_COUNT;++i)if(auto* u=f.player.team->myUnits[i]) {
        workers+=u->typeNum==WORKER;warriors+=u->typeNum==WARRIOR;
    }
    assert(workers==50 && warriors>0);
    if(!record){expected>>std::ws;assert(expected.eof());}
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
        // A zero birth budget pauses production through the ratios. Carriers
        // are the building's own business now and keep their minimum.
        assert(swarm->maxUnitWorking>=1);
        for(int type=0;type<NB_UNIT_TYPE;++type) assert(swarm->ratio[type]==0);
        swarm->resources[WHEAT]=20;
        swarm->productionTimeout=expiredTimer;
        const int before=f.population();
        for(int tick=0;tick<1000;++tick) swarm->swarmStep();
        // The existing engine cannot cancel an already-expired timer through
        // AI orders. That one pending birth may finish; further births stop.
        const int pending=expiredTimer<0 ? 1 : 0;
        assert(f.population()==before+pending && swarm->resources[WHEAT]==20-5*pending);
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
        assert(f.population()==before+pending+1 && swarm->resources[WHEAT]==15-5*pending);
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
    // The construction site carries the workers the placement action asked for.
    assert(fresh->maxUnitWorking==2);
    for(int resource=0;resource<MAX_RESOURCES;++resource)
        fresh->resources[resource]=fresh->type->maxResource[resource];
    fresh->updateBuildingSite();
    assert(fresh->maxUnitWorking==1);
    // Completion no longer redistributes a colony total. Each swarm runs its
    // own loop, so both stay staffed and both share the explorer stream.
    ai.budget.staffing_window_samples=2;
    ai.budget.staffing_cooldown_passes=0;
    ai.budget.staffing_minimum_workers=1;
    old->resources[WHEAT]=0; fresh->resources[WHEAT]=0;
    for(int pass=0;pass<6;++pass)
    {
        // The pre-existing swarm is the first building the fixture created.
        ai.manage_swarm(c,0);
        ai.manage_swarm(c,id);
        f.applyStaffing();
    }
    assert(old->maxUnitWorking>=1 && fresh->maxUnitWorking>=1);
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
        swarm->resources[WHEAT]=20;
        c.add_resource_tracker(new Management::ResourceTracker(c,0,25,WHEAT),0);
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
        swarm->resources[WHEAT]=0;
        for(int i=0;i<250;++i) tick();
        assert(tracker->get_age()==500 && tracker->get_total_level()==0);
    }
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
        if(map.getResource(x,y).type==WHEAT) map.setNoResource(x,y,1);
    for(auto entry:before) assert(ai.nearby_farm_capacity(c,entry.first)==0);
}

}

void newBuildingsStartStaffed()
{
    Fixture f;
    Building* swarm=f.swarm(10,10,0);
    Building* inn=f.game.addBuilding(30,10,
        globalContainer->buildingsTypes.getTypeNum("inn",0,false),0,0,0);
    assert(inn);
    auto& ai=*f.ai; auto& c=ai.context; c.initialize();
    ai.budget.staffing_new_swarm_workers=8;
    ai.budget.staffing_new_inn_workers=4;
    ai.budget.staffing_window_samples=8;
    ai.budget.staffing_cooldown_passes=3;
    ai.budget.staffing_minimum_workers=1;
    ai.budget.staffing_maximum_workers=20;
    // A newly built building starts where it is useful instead of climbing
    // from one carrier at a cooldown apiece.
    ai.manage_swarm(c,0); ai.manage_inn(c,1); f.applyStaffing();
    assert(swarm->maxUnitWorking==8);
    assert(inn->maxUnitWorking==4);
    // The seed applies once. From here the loop owns the number, so a building
    // that stays full hands carriers back below its starting count.
    swarm->resources[WHEAT]=swarm->type->maxResource[WHEAT];
    inn->resources[WHEAT]=inn->type->maxResource[WHEAT];
    for(int pass=0;pass<40;++pass)
    {
        ai.manage_swarm(c,0); ai.manage_inn(c,1); f.applyStaffing();
    }
    assert(swarm->maxUnitWorking<8);
    assert(inn->maxUnitWorking<4);
}

static void schoolsDoNotRequireKnownAlgae()
{
    Fixture f; f.swarm(10,10);
    auto& ai=*f.ai; auto& c=ai.context; c.initialize();
    ai.snapshot.population=184; ai.snapshot.workers=92;
    ai.snapshot.swarms=1; ai.snapshot.completed_swarms=1;
    ai.environment.food_security=100; ai.environment.food_headroom=100;
    ai.environment.resource_capacity=100;
    ai.demands.technology=100;
    ai.known_algae_units=0; ai.accessible_algae_units=0;
    ai.build_policy_bids(); ai.arbitrate_policy_bids(); ai.finalize_director_plan(c);
    assert(ai.budget.desired_schools>0);
    AIMaximaPlacement::WorldState world;
    const auto intents=ai.collect_development_intents(world);
    bool schoolRequested=false;
    for(const auto& intent:intents)
        if(intent.buildingType==IntBuildingType::SCIENCE_BUILDING)
        {
            schoolRequested=true;
            assert(intent.requiredResourceType==-1);
        }
    assert(schoolRequested);
}

static void strandedAlgaeKeepsPoolDemand()
{
    Fixture f;
    auto& ai=*f.ai;
    ai.context.initialize();
    ai.ensure_strategy();
    ai.environment.mobility_opportunity=5;
    ai.known_algae_units=100;
    ai.walk_accessible_algae_units=20;
    auto& access=ai.policy_bids[AIMaxima::Maxima::PolicyAccess];
    access.utility=100;
    access.desired_pools=1;

    assert(ai.labour_swimming_matters());
    ai.arbitrate_policy_bids();
    assert(ai.budget.desired_pools==1);

    // Connected maps without stranded water resources retain the cheap
    // suppression that avoids spending labour on useless swimming lessons.
    ai.walk_accessible_algae_units=ai.known_algae_units;
    assert(!ai.labour_swimming_matters());
    ai.arbitrate_policy_bids();
    assert(ai.budget.desired_pools==0);

    // Fragmented terrain remains sufficient even before algae is discovered.
    ai.environment.mobility_opportunity=15;
    ai.known_algae_units=ai.walk_accessible_algae_units=0;
    assert(ai.labour_swimming_matters());
}

static void labourContinuation()
{
    Fixture f;
    f.swarm(10,10,1);
    auto& a=*f.ai;
    a.context.initialize();
    a.ensure_strategy();
    a.labour_observation.workers=24;
    a.labour_observation.idle=9;
    a.labour_plan.trainingReserve=5;
    a.labour_plan.swarmCap=2;
    a.swarm_allowance[0]=2;
    a.staffing_control[0].request=12;
    auto* storage=new GAGCore::MemoryStreamBackend;
    GAGCore::BinaryOutputStream output(storage);
    a.save(&output);
    const std::string bytes(storage->getBuffer(),storage->getPosition());
    GAGCore::BinaryInputStream input(new GAGCore::MemoryStreamBackend(bytes.data(),bytes.size()));
    input.seekFromStart(0);
    AIMaxima::Maxima restored(&input,&f.player,VERSION_MINOR);
    assert(restored.labour_observation.workers==24);
    assert(restored.labour_observation.idle==9);
    assert(restored.labour_plan.trainingReserve==5);
    assert(restored.labour_plan.swarmCap==2);
    assert(restored.swarm_allowance==a.swarm_allowance);
    // A completion event before the next building pass must retain the cap.
    for(auto* ai:{&a,&restored})
    {
        ai->context.managementOrders.clear();
        ai->handle_event(ai->context,RuntimeEvent(RuntimeEvent::UpdateSwarm,0));
        bool assigned=false;
        for(auto order:ai->context.managementOrders)
            if(auto request=dynamic_cast<Management::AssignWorkers*>(order.get()))
            {
                assert(request->workers==2);
                assigned=true;
            }
        assert(assigned);
    }

}

int main()
{
    GlobalContainer container; globalContainer=&container; container.runNoX=true;
    container.buildingsTypes.init();
    IntBuildingType::init();


    labourContinuation();
    strandedAlgaeKeepsPoolDemand();
    schoolsDoNotRequireKnownAlgae();
    birthBudgetScalesBeyondTwenty();
    growingFoodFundsCapacity();
    colonyStartupAndAffordability();
    distantWheatFundsRecovery();
    soleSwarmStaffsFromItsOwnStock();
    newBuildingsStartStaffed();
    nearbyCornDeterminesStaffing();
    cornPileInteriorIsSupply();
    explorerTargetAlwaysGetsProduction();
    armyDemandFundsTrainingAndBirthMix();
    barracksUpgradesKeepTrainingOpen();
    armyBirthsMatchPlatformChecksums();
    crisisProductionPause();
    completionReallocatesColony();
    trackerLogicalCadence();
    holidayHarvestCapacity();
    std::cout<<"Maxima economy regression tests passed\n";
}
