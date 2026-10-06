#include "EngineFixtures.h"
#include <algorithm>
#include "Version.h"
#include "AIMaximaBuildings.h"
#include "AIMaximaFeedingEstimate.h"
#include "AIMaximaFeedingDemand.h"
#include "AIMaximaPlacementContinuation.h"
#include <nlohmann/json.hpp>
// Link with the game objects (excluding Glob2.cpp) to exercise the real runtime.
#include "GlobalContainer.h"
#include "Game.h"
#include "../../src/team/Team.h"
#include "../../src/ai/AIImplementation.h"
#include "../../src/map/Map.h"
#include "Order.h"
#include "Player.h"
#include "TeamStat.h"
#include "Utilities.h"
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
// Maxima inherits its runtime privately; -fno-access-control relaxes member access
// but GCC still rejects the base conversion, so the old wrapper stays for these headers.
#define private public
#include "../../src/ai/maxima/AIMaximaRuntime.h"
#include "../../src/ai/maxima/AIMaxima.h"
#include "../../src/ai/maxima/AIMaximaSwarmController.h"
#undef private
#include "../../src/building/Building.h"
#include "BuildingType.h"
#include "../../src/building/IntBuildingType.h"
#include "../../src/unit/Unit.h"
#include "../../src/ai/maxima/AIMaximaContinuation.h"
#include <BinaryStream.h>
#include <GzipUtil.h>
#include <StreamBackend.h>
#include <bit>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <memory>

using namespace AIMaximaRuntime;

namespace
{
struct Fixture
{
    Game game;
    glob2test::BoundGameRandom random{game};
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
        REQUIRE(b); return b;
    }
    void supply(int x,int y)
    {
        game.map.setResourceByIndex(x+6,y+1,WHEAT,1);
        game.map.setCellTerrain(x+6,y+3,WATER);
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
                REQUIRE(b); b->maxUnitWorking=assignment->numberRequested;
            }
            else if(auto ratios=std::dynamic_pointer_cast<OrderModifySwarm>(order))
            {
                Building* b=player.team->myBuildings[Building::GIDtoID(ratios->gid)];
                REQUIRE(b);
                for(int type=0;type<NB_UNIT_TYPE;++type) b->ratio[type]=ratios->ratio[type];
            }
            else REQUIRE(false);
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
        REQUIRE(plan(4,4,0,0,supply,275,25,6,6,65536).workers==1);
        REQUIRE(plan(4,4,0,0,supply/65536,275,25,6,6).workers==0);
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
        REQUIRE(ai.budget.swarm_workers==1);
        ai.manage_swarm(ai.context,0); opening.applyStaffing();
        REQUIRE(swarm->ratio[WORKER]>0);
        ai.environment.accessible_corn_fraction=0;
        ai.build_policy_bids(); ai.arbitrate_policy_bids();
        ai.manage_swarm(ai.context,0); opening.applyStaffing();
        REQUIRE(swarm->ratio[WORKER]+swarm->ratio[EXPLORER]+swarm->ratio[WARRIOR]==0);
    }
    // Physical boundaries and monotonic responses, across realistic domains.
    for(int w=0;w<=1000;w+=5) for(int food:{0,5,50,500,5000}) {
        auto p=plan(w,1000,0,0,food,250,100,3,6);
        REQUIRE(plan(w,1000,0,0,food*65536LL,250,100,3,6,65536).workers==p.workers);
        REQUIRE((p.workers>=0 && p.workers<=w && p.swarms>=1));
        REQUIRE(plan(w+1,1000,0,0,food,250,100,3,6).workers>=p.workers);
        REQUIRE(plan(w,1000,0,0,food+1,250,100,3,6).workers>=p.workers);
        REQUIRE(plan(w,1000,100,0,food,250,100,3,6).workers<=p.workers);
        if(food==0) REQUIRE((p.workers==0 && p.swarms==1));
    }
    REQUIRE(plan(1000,1000,0,0,5000,250,100,3,6).workers>20);
    auto a=plan(100,100,11,0,1000,250,100,3,6);
    auto b=plan(100,100,12,0,1000,250,100,3,6);
    auto c=plan(100,100,13,0,1000,250,100,3,6);
    REQUIRE((a.workers-b.workers<=1 && b.workers-c.workers<=1 && c.workers>0));
    REQUIRE(plan(100,100,12,12,1000,250,100,3,6).workers==b.workers);
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
        REQUIRE((ai.budget.swarm_workers==workers && ai.budget.desired_swarms==count));
    }
    // Expansion has its own construction permission even with no production
    // deficit. Core and colony intents remain distinct and share staffing.
    auto world=ai.collect_development_world(ai.context);
    ai.budget.colony_swarm_requested=true;
    ai.budget.desired_swarms=0;
    ai.budget.desired_explorers=0;ai.budget.desired_warriors=0;
    int colonies=0;
    for(const auto& intent:ai.collect_development_intents(world))
        if(intent.buildingType==f.game.buildingsTypes.getPlaceableTypeNum("swarm")) {
            REQUIRE(intent.purpose==AIMaximaPlacement::ColonySeed);
            REQUIRE(intent.unmetCount==1); ++colonies;
        }
    REQUIRE(colonies==1);
    ai.budget.colony_swarm_requested=false;
    for(const auto& intent:ai.collect_development_intents(world))
        REQUIRE(intent.buildingType!=f.game.buildingsTypes.getPlaceableTypeNum("swarm"));
    ai.snapshot.critical_food=30; ai.build_policy_bids(); ai.arbitrate_policy_bids();
    REQUIRE((ai.budget.swarm_workers>0 && ai.budget.swarm_workers<workers));
}

void colonyStartupAndAffordability()
{
    Fixture f; Building* swarm=f.swarm(32,32); auto& ai=*f.ai;
    ai.context.initialize();
    ai.snapshot.workers=30; ai.snapshot.population=45;
    ai.snapshot.free_workers=10; ai.snapshot.worker_jobs_open=0;
    ai.posture=AIMaxima::Maxima::PostureRecover;
    ai.finalize_director_plan(ai.context);
    REQUIRE(ai.budget.colony_swarm_requested); // Recovery is not an expansion veto.
    ai.snapshot.free_workers=0;
    ai.finalize_director_plan(ai.context);
    REQUIRE(!ai.budget.colony_swarm_requested);
    ai.snapshot.free_workers=10;
    int id=-1;
    for(const auto& entry:ai.context.get_building_register().found())
        if(ai.context.get_building_register().get_building(entry.first)==swarm)id=entry.first;
    REQUIRE(id>=0);
    AIMaximaPlacement::DevelopmentAction action;
    action.id=123; action.buildingId=id; action.purpose=AIMaximaPlacement::ColonySeed;
    action.state=AIMaximaPlacement::Completed;
    // This is a lifecycle fixture; normal production populates this action map.
    auto& actions=const_cast<std::map<int,AIMaximaPlacement::DevelopmentAction>&>(ai.development_planner.actions());
    actions[action.id]=action;
    ai.finalize_director_plan(ai.context);
    REQUIRE(!ai.budget.colony_swarm_requested);
    Unit* worker=f.game.addUnit(20,20,0,WORKER,0,0,0,0); REQUIRE(worker);
    swarm->unitsWorking.push_back(worker);
    swarm->materials[WHEAT]=swarm->type->foodPerUnit;
    ai.finalize_director_plan(ai.context);
    REQUIRE(ai.budget.colony_swarm_requested);
    swarm->unitsWorking.clear(); swarm->materials[WHEAT]=0;
    ai.finalize_director_plan(ai.context);
    REQUIRE(ai.budget.colony_swarm_requested); // Later idleness is not a new startup.
}

void growingFoodFundsCapacity()
{
    Fixture f; f.swarm(10,10,0); f.supply(10,10);
    for(int y=8;y<=24;++y) for(int x=21;x<=35;++x)
        f.game.map.setCellTerrain(x,y,WATER);
    auto& ai=*f.ai; auto& c=ai.context; c.initialize();
    ai.snapshot.population=120; ai.snapshot.workers=90;
    ai.update_environment_model(c); ai.build_policy_bids(); ai.arbitrate_policy_bids();
    const int food=ai.environment.accessible_corn;
    const int workers=ai.budget.swarm_workers;
    for(int y=12;y<=20;++y) for(int x=12;x<=20;++x)
        if(f.game.map.isGrass(x,y)) f.game.map.setResourceByIndex(x,y,WHEAT,1);
    ai.update_environment_model(c); ai.build_policy_bids(); ai.arbitrate_policy_bids();
    REQUIRE(ai.environment.accessible_corn>food);
    REQUIRE(ai.budget.swarm_workers>workers);
    REQUIRE(ai.budget.desired_swarms>1);
}

void distantWheatFundsRecovery()
{
    Fixture f;auto swarm=f.swarm(10,10);f.supply(30,10);
    auto& ai=*f.ai;auto& c=ai.context;c.initialize();
    ai.snapshot.population=4;ai.snapshot.workers=4;ai.budget.swarm_supply_radius=12;
    REQUIRE(ai.nearby_farm_capacity(c,0)==0);
    ai.update_environment_model(c);ai.build_policy_bids();ai.arbitrate_policy_bids();
    REQUIRE(ai.environment.accessible_corn*65536LL+ai.environment.accessible_corn_fraction>0);
    REQUIRE(ai.budget.swarm_workers>0);
    ai.manage_swarm(c,0);f.applyStaffing();REQUIRE(swarm->ratio[WORKER]>0);
    // Full stores cannot manufacture recurring capacity when no wheat remains.
    f.game.map.setNoResource(36,11,1);
    swarm->materials[WHEAT]=swarm->type->maxMaterial[WHEAT];
    ai.update_environment_model(c);ai.build_policy_bids();ai.arbitrate_policy_bids();
    REQUIRE(ai.budget.swarm_workers==0);
    f.game.map.setResourceByIndex(36,11,WHEAT,1);
    // A full water barrier (including the wrap edge) cuts off nonswimmers.
    for(int y=0;y<64;++y)for(int x:{0,25})f.game.map.setCellTerrain(x,y,WATER);
    ai.fertility_cache=AIMaxima::Farming::ExactFertilityCache();
    ai.budget.can_swim=false;
    ai.update_environment_model(c);ai.build_policy_bids();ai.arbitrate_policy_bids();
    REQUIRE(ai.budget.swarm_workers==0);
}

void soleSwarmStaffsFromItsOwnStock()
{
    Fixture f;Building* swarm=f.swarm(10,10,0);
    auto& ai=*f.ai;auto& c=ai.context;c.initialize();
    REQUIRE(ai.nearby_farm_capacity(c,0)==0);
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
    swarm->materials[WHEAT]=swarm->type->maxMaterial[WHEAT];
    for(int budget:{10,0,2}) {
        ai.budget.swarm_workers=budget;
        for(int pass=0;pass<6;++pass){ai.manage_swarm(c,0);f.applyStaffing();}
        REQUIRE(swarm->maxUnitWorking==1);
    }
    swarm->materials[WHEAT]=0;
    for(int pass=0;pass<6;++pass){ai.manage_swarm(c,0);f.applyStaffing();}
    REQUIRE(swarm->maxUnitWorking>1);
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
    REQUIRE(before>0);
    const auto originalResource=f.game.map.getTile(16,11).resource;
    const auto stockLimit=f.game.map.resourceRegistry().yields(static_cast<ResourceId>(originalResource.type))[materialIndex(MaterialId::Food)].capacity;
    // Empty nonpersistent deposits disappear. Recreate each authored stock
    // level independently; only valid levels up to its declared capacity apply.
    // Live replenishment varies with stock, while standing food always counts.
    for(unsigned amount=0;amount<=stockLimit;++amount)
    {
        f.game.map.replaceResource(16,11,originalResource);
        f.game.map.setResourceAmount(f.game.map.coordToIndex(16,11), amount);
        const long long capacity=ai.nearby_farm_capacity(c,0);
        if(amount==0) REQUIRE(capacity==0);
        else REQUIRE(capacity>0);
    }
    f.game.map.replaceResource(16,11,originalResource);
    auto world=ai.collect_development_world(c);
    REQUIRE(world.tile(16,11).foodOpportunity>0);
    REQUIRE(world.tile(15,11).fertility>0);
    REQUIRE(world.tile(15,11).foodOpportunity==0);
    REQUIRE(world.tile(15,11).farmCapacity==0);
    f.game.map.setNoResource(16,11,1);
    REQUIRE(ai.nearby_farm_capacity(c,0)==0);
    world=ai.collect_development_world(c);
    REQUIRE(world.tile(16,11).foodOpportunity==0);
    REQUIRE(world.tile(16,11).farmCapacity==0);
    f.game.map.setResourceByIndex(16,11,WHEAT,1);
    ai.budget.staffing_new_swarm_workers=1;
    ai.budget.staffing_window_samples=2;
    ai.budget.staffing_cooldown_passes=0;
    ai.budget.staffing_minimum_workers=1;
    ai.budget.staffing_maximum_workers=20;
    // Staffing no longer divides a colony total between swarms by nearby corn.
    // Each reads its own stock, so the empty one outgrows the full one and
    // neither drops below the minimum.
    first->materials[WHEAT]=0;
    second->materials[WHEAT]=second->type->maxMaterial[WHEAT];
    for(int pass=0;pass<6;++pass)
    {
        for(int id=0;id<2;++id) ai.manage_swarm(c,id);
        f.applyStaffing();
    }
    REQUIRE(first->maxUnitWorking>second->maxUnitWorking);
    REQUIRE((first->maxUnitWorking>0 && second->maxUnitWorking>0));
    std::set<int> shared;
    // Two readings of the same patch at the same moment must agree, and the
    // second must take nothing, because the first claimed every tile. The
    // comparison is against a reading taken here rather than against the one
    // at the top of the test: farming has since published its protection mask,
    // which admits tiles that were forbidden before, so the colony genuinely
    // reaches more wheat than it did then.
    const long long steady=ai.nearby_farm_capacity(c,0);
    REQUIRE(steady>0);
    REQUIRE(ai.nearby_farm_capacity(c,0,&shared)==steady);
    REQUIRE(ai.nearby_farm_capacity(c,0,&shared)==0);
    f.game.map.unsetMapDiscovered();
    REQUIRE(ai.nearby_farm_capacity(c,0)==0);
}

void cornPileInteriorIsSupply()
{
    Fixture f;
    f.swarm(30,10); f.supply(30,10); f.supply(10,10);
    Building* inn=f.game.addBuilding(10,10,
        globalContainer->buildingsTypes.getTypeNum("inn",0,false),0,0,0);
    REQUIRE(inn);
    auto& ai=*f.ai; auto& c=ai.context; c.initialize();
    ai.budget.swarm_supply_radius=10;
    const long long before=ai.nearby_farm_capacity(c,0);
    for(int y=9;y<=11;++y) for(int x=37;x<=39;++x)
        f.game.map.setResourceByIndex(x,y,WHEAT,1);
    const long long planted=ai.nearby_farm_capacity(c,0);
    REQUIRE(planted>before);
    f.game.map.setResourceByIndex(38,10,STONE,1);
    const long long withoutCenter=ai.nearby_farm_capacity(c,0);
    REQUIRE(withoutCenter<planted);
    f.game.map.setNoResource(38,10,1);
    REQUIRE(ai.nearby_farm_capacity(c,0)==withoutCenter);
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
    inn->materials[WHEAT]=0;
    for(int pass=0;pass<6;++pass){ai.manage_inn(c,1); f.applyStaffing();}
    REQUIRE(inn->maxUnitWorking>=2);
    inn->materials[WHEAT]=inn->type->maxMaterial[WHEAT];
    for(int pass=0;pass<12;++pass){ai.manage_inn(c,1); f.applyStaffing();}
    REQUIRE(inn->maxUnitWorking==1);
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
        REQUIRE(ai.budget.desired_explorers==14);
        REQUIRE(ai.budget.explorer_ratio==std::max(1,requestedWeight));
        ai.manage_swarm(c,0); ai.manage_swarm(c,1); f.applyStaffing();
        const int expected=birthWorkers>0 && explorers<14
            ? std::max(1,requestedWeight) : 0;
        REQUIRE(first->ratio[EXPLORER]==expected);
        REQUIRE(second->ratio[EXPLORER]==expected);
    }
}

void armyDemandFundsTrainingAndBirthMix()
{
    for(bool foodEmergency:{false,true}) {
        Fixture f;
        auto* swarm=f.swarm(40,40,6);f.supply(40,40);
        // The saved colony had one working barracks and four upgrade sites.
        for(int i=0;i<5;++i)
            REQUIRE(f.game.addBuilding(4+6*i,4,globalContainer->buildingsTypes
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
        REQUIRE(a.budget.desired_barracks>5);
        REQUIRE((a.budget.worker_ratio==0 && a.budget.warrior_ratio==3));
        f.player.team->stats.getLatestStat()->numberUnitPerType[WARRIOR]=30;
        a.manage_swarm(c,0);f.applyStaffing();
        REQUIRE((swarm->ratio[WORKER]==0 && swarm->ratio[WARRIOR]==3));
        // Backpressure must pause surplus-worker births too, not redirect
        // all funded food into workers when the training queue fills.
        a.snapshot.warriors=60;
        a.arbitrate_policy_bids();
        REQUIRE((a.budget.worker_ratio==0 && a.budget.warrior_ratio==0));
        // Genuine job growth still funds replacements and expansion.
        a.snapshot.worker_jobs_open=400;
        a.arbitrate_policy_bids();
        REQUIRE(a.budget.worker_ratio==5);
        // The military preference ends when its target is met.
        a.snapshot.worker_jobs_open=43;a.snapshot.warriors=120;
        a.arbitrate_policy_bids();
        REQUIRE(a.budget.worker_ratio==5);
    }
}

void barracksUpgradesKeepTrainingOpen()
{
    using namespace AIMaximaPlacement;
    Fixture f;
    REQUIRE(f.game.addUnit(20,20,0,WORKER,1,0,0,0));
    for(int i=0;i<2;++i)
        REQUIRE(f.game.addBuilding(4+6*i,4,globalContainer->buildingsTypes
            .getTypeNum("barracks",0,false),0));
    auto& a=*f.ai;auto& c=a.context;c.initialize();
    a.snapshot.warriors=20;a.snapshot.trained_warriors=0;
    a.budget.upgrade_level1_barracks_weight=50;
    auto limits=a.collect_development_limits(c);
    REQUIRE(limits.upgradePriority(f.game.buildingsTypes.getPlaceableTypeNum("barracks"),1)>0);
    DevelopmentAction action;
    action.id=7;action.type=UpgradeBuilding;action.state=CreateIssued;
    action.buildingType=f.game.buildingsTypes.getPlaceableTypeNum("barracks");
    action.buildingId=0;action.fromLevel=1;action.targetLevel=2;
    auto& actions=const_cast<std::map<int,DevelopmentAction>&>(a.development_planner.actions());
    actions[action.id]=action;
    const auto seats=a.barracks_capacity(c);
    REQUIRE((seats.first==2 && seats.second==6));
    limits=a.collect_development_limits(c);
    REQUIRE(limits.upgradePriority(f.game.buildingsTypes.getPlaceableTypeNum("barracks"),1)==0);
    // An unissued reservation must not prevent its own first upgrade.
    actions[action.id].state=ParcelReserved;
    limits=a.collect_development_limits(c,action.id);
    REQUIRE(limits.upgradePriority(f.game.buildingsTypes.getPlaceableTypeNum("barracks"),1)>0);
    // Once the army is trained, this queue-protection gate no longer applies.
    actions[action.id].state=CreateIssued;
    a.snapshot.trained_warriors=20;
    limits=a.collect_development_limits(c);
    REQUIRE(limits.upgradePriority(f.game.buildingsTypes.getPlaceableTypeNum("barracks"),1)>0);
}

void armyBirthsMatchPlatformChecksums()
{
    setSyncRandSeed(5489);
    Fixture f;
    f.game.gameHeader.setHungerDisabled(true);
    auto* swarm=f.swarm(10,10,6);f.supply(10,10);
    REQUIRE(f.game.addBuilding(18,10,globalContainer->buildingsTypes
        .getTypeNum("barracks",0,false),0));
    for(int i=0;i<50;++i)REQUIRE(f.game.addUnit(24+i%10,24+i/10,0,WORKER,0,0,0,0));
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
    REQUIRE((swarm->ratio[WORKER]==0 && swarm->ratio[WARRIOR]>0));
    swarm->materials[WHEAT]=swarm->type->maxMaterial[WHEAT];
    swarm->productionTimeout=-1;
    const std::string fixtureFile=(glob2test::sourceRoot() / "test/maxima/fixtures/army-birth-checksums.txt").string();
    const char* path=fixtureFile.c_str();
    const bool record=std::getenv("GLOB2_RECORD_ARMY_CHECKSUMS")!=nullptr;
    std::ifstream expected;
    std::ofstream output;
    // Record with serial execution of the default delayed schedule; normal
    // verification uses its background worker and must match every tick.
    if(record) { f.game.map.configureGradientPipeline(0,8); output.open(path); }
    else expected.open(path);
    REQUIRE((record?output.good():expected.good()));
    for(int tick=1;tick<=512;++tick) {
        f.game.syncStep(0);REQUIRE(f.game.stepCounter==unsigned(tick));
        // Normalize the header to 115; the fixture uses eight-tick publication. With one
        // team and no players, the header version is rotated six times.
        const Uint32 checksum=f.game.checkSum(nullptr,nullptr,nullptr,true)
            ^ std::rotr(Uint32(f.game.mapHeader.getVersionMinor()^115),6);
        if(record)output<<tick<<' '<<checksum<<'\n';
        else {
            int expectedTick;Uint32 expectedChecksum;
            REQUIRE((expected>>expectedTick>>expectedChecksum));
            if(expectedTick!=tick || expectedChecksum!=checksum)
                std::cerr<<"Army birth checksum mismatch: tick="<<tick
                    <<" actual="<<checksum<<" expected="<<expectedChecksum<<'\n';
            REQUIRE((expectedTick==tick && expectedChecksum==checksum));
        }
    }
    int workers=0,warriors=0;
    for(int i=0;i<Unit::MAX_COUNT;++i)if(auto* u=f.player.team->myUnits[i]) {
        workers+=u->typeNum==WORKER;warriors+=u->typeNum==WARRIOR;
    }
    REQUIRE((workers==50 && warriors>0));
    if(!record){expected>>std::ws;REQUIRE(expected.eof());}
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
        REQUIRE(ai.budget.swarm_workers==0);
        ai.manage_swarm(c,0); f.applyStaffing();
        // A zero birth budget pauses production through the ratios. Carriers
        // are the building's own business now and keep their minimum.
        REQUIRE(swarm->maxUnitWorking>=1);
        for(int type=0;type<NB_UNIT_TYPE;++type) REQUIRE(swarm->ratio[type]==0);
        swarm->materials[WHEAT]=20;
        swarm->productionTimeout=expiredTimer;
        const int before=f.population();
        for(int tick=0;tick<1000;++tick) swarm->swarmStep();
        // The existing engine cannot cancel an already-expired timer through
        // AI orders. That one pending birth may finish; further births stop.
        const int pending=expiredTimer<0 ? 1 : 0;
        REQUIRE((f.population()==before+pending && swarm->materials[WHEAT]==20-5*pending));
        REQUIRE(swarm->productionTimeout==(pending ? swarm->type->unitProductionTime : expiredTimer));

        // Funding returns through the same director and executor path.
        ai.snapshot.unserved_food=0;
        growth.swarm_workers=6;
        ai.arbitrate_policy_bids();
        REQUIRE(ai.budget.swarm_workers==6);
        ai.manage_swarm(c,0); f.applyStaffing();
        REQUIRE((swarm->ratio[WORKER]==4 && swarm->ratio[EXPLORER]==1
            && swarm->ratio[WARRIOR]==2));
        for(int tick=0;tick<=swarm->type->unitProductionTime;++tick) swarm->swarmStep();
        REQUIRE((f.population()==before+pending+1 && swarm->materials[WHEAT]==15-5*pending));
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
    action.buildingType=f.game.buildingsTypes.getPlaceableTypeNum("swarm");
    action.centerX=30; action.centerY=30; action.workers=2;
    action.initialFootprint=AIMaximaPlacement::Footprint(-2,-2,4,4);
    REQUIRE(ai.issue_development_action(c,action));
    const int id=c.previousBuildingId;
    Building* fresh=f.game.addBuilding(28,28,
        globalContainer->buildingsTypes.getTypeNum("swarm",0,true),0,1,1);
    REQUIRE(fresh);
    c.orders.clear(); c.buildings.tick(); f.applyStaffing();
    // The construction site carries the workers the placement action asked for.
    REQUIRE(fresh->maxUnitWorking==2);
    for(int resource=0;resource<MaterialCount;++resource)
        fresh->materials[resource]=fresh->type->maxMaterial[resource];
    fresh->updateBuildingSite();
    REQUIRE(fresh->maxUnitWorking==1);
    // Completion no longer redistributes a colony total. Each swarm runs its
    // own loop, so both stay staffed and both share the explorer stream.
    ai.budget.staffing_window_samples=2;
    ai.budget.staffing_cooldown_passes=0;
    ai.budget.staffing_minimum_workers=1;
    old->materials[WHEAT]=0; fresh->materials[WHEAT]=0;
    for(int pass=0;pass<6;++pass)
    {
        // The pre-existing swarm is the first building the fixture created.
        ai.manage_swarm(c,0);
        ai.manage_swarm(c,id);
        f.applyStaffing();
    }
    REQUIRE((old->maxUnitWorking>=1 && fresh->maxUnitWorking>=1));
    REQUIRE((old->ratio[EXPLORER]==1 && fresh->ratio[EXPLORER]==1));
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
        swarm->materials[WHEAT]=20;
        c.add_material_tracker(new Management::MaterialTracker(c,0,25,WHEAT),0);
        IdleAI idle;
        auto tracker=c.get_material_tracker(0);
        const auto tick=[&]() {++f.game.stepCounter; c.getOrder(idle);};
        for(int i=0;i<9;++i) tick();
        REQUIRE((tracker->get_age()==9 && tracker->get_total_level()==0));
        for(int i=0;i<3;++i) c.push_order(std::shared_ptr<Order>(new NullOrder));
        for(int i=0;i<3;++i) tick();
        REQUIRE(tracker->get_age()==9);
        tick();
        REQUIRE((tracker->get_age()==10 && tracker->get_total_level()==20));
        for(int i=10;i<250;++i) tick();
        REQUIRE((tracker->get_age()==250 && tracker->get_total_level()==500));
        swarm->materials[WHEAT]=0;
        for(int i=0;i<250;++i) tick();
        REQUIRE((tracker->get_age()==500 && tracker->get_total_level()==0));
    }
}

void holidayHarvestCapacity()
{
    Game game(NULL);
    BinaryInputStream input(GAGCore::openInflatingFileStreamBackend((glob2test::sourceRoot() / "maps/Holiday_Island_2.map.gz").string()));
    REQUIRE(input.isValid()); REQUIRE(game.load(&input));
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
    REQUIRE(!before.empty());
    int productive=0; for(auto entry:before) productive+=entry.second>0;
    REQUIRE(productive>0);
    for(int y=0;y<map.getH();++y) for(int x=0;x<map.getW();++x)
        if(map.getResource(x,y).type==WHEAT) map.setNoResource(x,y,1);
    for(auto entry:before) REQUIRE(ai.nearby_farm_capacity(c,entry.first)==0);
}

}

void newBuildingsStartStaffed()
{
    Fixture f;
    Building* swarm=f.swarm(10,10,0);
    Building* inn=f.game.addBuilding(30,10,
        globalContainer->buildingsTypes.getTypeNum("inn",0,false),0,0,0);
    REQUIRE(inn);
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
    REQUIRE(swarm->maxUnitWorking==8);
    REQUIRE(inn->maxUnitWorking==4);
    // The seed applies once. From here the loop owns the number, so a building
    // that stays full hands carriers back below its starting count.
    swarm->materials[WHEAT]=swarm->type->maxMaterial[WHEAT];
    inn->materials[WHEAT]=inn->type->maxMaterial[WHEAT];
    for(int pass=0;pass<40;++pass)
    {
        ai.manage_swarm(c,0); ai.manage_inn(c,1); f.applyStaffing();
    }
    REQUIRE(swarm->maxUnitWorking<8);
    REQUIRE(inn->maxUnitWorking<4);
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
    REQUIRE(ai.budget.desired_schools>0);
    AIMaximaPlacement::WorldState world;world.profiles=ai.collect_building_profiles();
    const auto intents=ai.collect_development_intents(world);
    bool schoolRequested=false;
    for(const auto& intent:intents)
        if(intent.buildingType==f.game.buildingsTypes.getPlaceableTypeNum("school"))
        {
            schoolRequested=true;
            REQUIRE(intent.requiredMaterialType==-1);
        }
    REQUIRE(schoolRequested);
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

    REQUIRE(ai.labour_swimming_matters());
    ai.arbitrate_policy_bids();
    REQUIRE(ai.budget.desired_pools==1);

    // Connected maps without stranded water resources retain the cheap
    // suppression that avoids spending labour on useless swimming lessons.
    ai.walk_accessible_algae_units=ai.known_algae_units;
    REQUIRE(!ai.labour_swimming_matters());
    ai.arbitrate_policy_bids();
    REQUIRE(ai.budget.desired_pools==0);

    // Fragmented terrain remains sufficient even before algae is discovered.
    ai.environment.mobility_opportunity=15;
    ai.known_algae_units=ai.walk_accessible_algae_units=0;
    REQUIRE(ai.labour_swimming_matters());
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
    REQUIRE(restored.labour_observation.workers==24);
    REQUIRE(restored.labour_observation.idle==9);
    REQUIRE(restored.labour_plan.trainingReserve==5);
    REQUIRE(restored.labour_plan.swarmCap==2);
    REQUIRE(restored.swarm_allowance==a.swarm_allowance);
    // A completion event before the next building pass must retain the cap.
    for(auto* ai:{&a,&restored})
    {
        ai->context.managementOrders.clear();
        ai->handle_event(ai->context,RuntimeEvent(RuntimeEvent::UpdateSwarm,0));
        bool assigned=false;
        for(auto order:ai->context.managementOrders)
            if(auto request=dynamic_cast<Management::AssignWorkers*>(order.get()))
            {
                REQUIRE(request->workers==2);
                assigned=true;
            }
        REQUIRE(assigned);
    }

}

TEST_CASE("counted opponent records preserve slot fifteen and load the legacy twelve-record layout [save-format]" * doctest::test_suite("Maxima.Economy"))
{
    glob2test::HeadlessGlobals globals;
    Fixture f;
    auto &ai = *f.ai;
    ai.context.initialize();
    ai.ensure_strategy();
    for (int i = 0; i < Team::MAX_COUNT; ++i)
    {
        ai.opponents[i].alive = true;
        ai.opponents[i].visible_warriors = 100 + i;
        ai.opponents[i].nearest_building = 200 + i;
    }
    // Capture the counted section boundary without searching for ambiguous byte patterns.
    struct OpponentWriter : GAGCore::BinaryOutputStream
    {
        using BinaryOutputStream::BinaryOutputStream;
        size_t countOffset = 0;
        std::vector<size_t> mealOffsets;
        void writeSint32(Sint32 value, const std::string name) override
        {
            if(name=="feeding_workers" || name=="feeding_explorers" || name=="feeding_warriors")
                mealOffsets.push_back(getPosition());
            BinaryOutputStream::writeSint32(value,name);
        }
        void writeUint32(Uint32 value, const std::string name) override
        {
            if (name == "count") countOffset = getPosition();
            BinaryOutputStream::writeUint32(value, name);
        }
    };
    auto *storage = new GAGCore::MemoryStreamBackend;
    OpponentWriter output(storage);
    ai.saveDirector(&output);
    const std::string bytes = storage->takeContents();
    REQUIRE(output.countOffset > 0);
    const auto load = [&](const std::string &saved, int version)
    {
        GAGCore::BinaryInputStream input(new GAGCore::MemoryStreamBackend(saved.data(), saved.size()));
        input.seekFromStart(0);
        REQUIRE(ai.loadDirector(&input, version));
        CHECK(input.getPosition() == saved.size());
    };
    ai.opponents[15] = AIMaxima::Maxima::OpponentAssessment{};
    load(bytes, VERSION_MINOR);
    CHECK(ai.opponents[15].visible_warriors == 115);
    CHECK(ai.opponents[15].nearest_building == 215);

    // Reconstruct the pre-127 binary layout: no count, one byte and fourteen
    // signed words per opponent, twelve records. The following director fields
    // must still be consumed at precisely the same boundary.
    constexpr size_t legacySlots = 12;
    constexpr size_t opponentBytes = 1 + 14 * sizeof(Sint32);
    auto legacy = bytes;
    // Version137 adds these three named fields in each of snapshot and
    // previous_snapshot. The version126 fixture must omit all six words.
    REQUIRE(output.mealOffsets.size()==6);
    std::vector<std::pair<size_t,size_t>> removals{
        {output.countOffset+sizeof(Uint32)+legacySlots*opponentBytes,
            (Team::MAX_COUNT-legacySlots)*opponentBytes},
        {output.countOffset,sizeof(Uint32)}};
    for(size_t offset:output.mealOffsets)removals.push_back({offset,sizeof(Sint32)});
    std::sort(removals.rbegin(),removals.rend());
    for(const auto& [offset,length]:removals)legacy.erase(offset,length);
    load(legacy, 126);
    CHECK(ai.opponents[11].visible_warriors == 111);
    for (int i = legacySlots; i < Team::MAX_COUNT; ++i)
    {
        CHECK_FALSE(ai.opponents[i].alive);
        CHECK(ai.opponents[i].visible_warriors == 0);
    }
    for (unsigned char count : {0, 17, 255})
    {
        auto invalid = bytes;
        // Uint32 uses network byte order; set all bytes to this value so every
        // nonzero probe exceeds the limit regardless of endian representation.
        invalid.replace(output.countOffset, sizeof(Uint32), sizeof(Uint32), char(count));
        GAGCore::BinaryInputStream input(new GAGCore::MemoryStreamBackend(invalid.data(), invalid.size()));
        input.seekFromStart(0);
        CHECK_THROWS_AS(ai.loadDirector(&input, VERSION_MINOR), std::runtime_error);
    }
}

TEST_SUITE("Maxima.Economy")
{
	TEST_CASE("labour continuation") { glob2test::HeadlessGlobals globals; labourContinuation(); }
	TEST_CASE("stranded algae keeps pool demand") { glob2test::HeadlessGlobals globals; strandedAlgaeKeepsPoolDemand(); }
	TEST_CASE("schools do not require known algae") { glob2test::HeadlessGlobals globals; schoolsDoNotRequireKnownAlgae(); }
	TEST_CASE("birth budget scales beyond twenty") { glob2test::HeadlessGlobals globals; birthBudgetScalesBeyondTwenty(); }
	TEST_CASE("growing food funds capacity") { glob2test::HeadlessGlobals globals; growingFoodFundsCapacity(); }
	TEST_CASE("colony startup and affordability") { glob2test::HeadlessGlobals globals; colonyStartupAndAffordability(); }
	TEST_CASE("distant wheat funds recovery") { glob2test::HeadlessGlobals globals; distantWheatFundsRecovery(); }
	TEST_CASE("sole swarm staffs from its own stock") { glob2test::HeadlessGlobals globals; soleSwarmStaffsFromItsOwnStock(); }
	TEST_CASE("new buildings start staffed") { glob2test::HeadlessGlobals globals; newBuildingsStartStaffed(); }
	TEST_CASE("nearby corn determines staffing") { glob2test::HeadlessGlobals globals; nearbyCornDeterminesStaffing(); }
	TEST_CASE("corn pile interior is supply") { glob2test::HeadlessGlobals globals; cornPileInteriorIsSupply(); }
	TEST_CASE("explorer target always gets production") { glob2test::HeadlessGlobals globals; explorerTargetAlwaysGetsProduction(); }
	TEST_CASE("army demand funds training and birth mix") { glob2test::HeadlessGlobals globals; armyDemandFundsTrainingAndBirthMix(); }
	TEST_CASE("barracks upgrades keep training open") { glob2test::HeadlessGlobals globals; barracksUpgradesKeepTrainingOpen(); }
	TEST_CASE("army births match platform checksums") { glob2test::HeadlessGlobals globals; armyBirthsMatchPlatformChecksums(); }
	TEST_CASE("crisis production pause") { glob2test::HeadlessGlobals globals; crisisProductionPause(); }
	TEST_CASE("completion reallocates colony") { glob2test::HeadlessGlobals globals; completionReallocatesColony(); }
	TEST_CASE("tracker logical cadence") { glob2test::HeadlessGlobals globals; trackerLogicalCadence(); }
	TEST_CASE("holiday harvest capacity") { glob2test::HeadlessGlobals globals; holidayHarvestCapacity(); }
}

TEST_CASE("service rate uses simulated visit ticks" * doctest::test_suite("Maxima.Economy"))
{
    glob2test::HeadlessGlobals globals;
    glob2test::HeadlessGame world({.loadDefaultRace=true});
    Building* inn=world.addBuilding("inn",8,8);
    inn->materials[WHEAT]=10;
    Unit* worker=world.addUnit(WORKER);
    worker->destinationPurpose=FEED;
    inn->subscribeUnitForInside(worker);
    REQUIRE(worker->serviceResourcesReserved);
    worker->displacement=Unit::DIS_ENTERING_BUILDING;
    worker->delta=255;worker->syncStep();
    REQUIRE(worker->displacement==Unit::DIS_INSIDE);
    worker->delta=0;
    int elapsed=0;
    while(worker->displacement==Unit::DIS_INSIDE && elapsed<2000){worker->syncStep();++elapsed;}
    CHECK(worker->displacement==Unit::DIS_EXITING_BUILDING);
    CHECK(elapsed==AIMaximaBuildings::serviceTicks(*inn->type,inn->type->semantics.feeding.duration));
    CHECK(inn->materials[WHEAT]==9);
}

TEST_CASE("feeding estimate shares resources and seats across capability combinations" * doctest::test_suite("Maxima.Economy"))
{
    glob2test::HeadlessGlobals globals;
    glob2test::HeadlessGame world({.loadDefaultRace=true});
    const int id=world.game.buildingsTypes.getTypeNum("inn",2,false);
    const auto baseline=nlohmann::json::parse(world.game.buildingsTypes.snapshotJson());
    const AIMaxima::FeedingPlan plan{4,16*23,105};
    auto estimate=[&](nlohmann::json snapshot,const AIMaxima::FeedingPlan& p) {
        world.game.buildingsTypes.loadSnapshotJson(snapshot.dump());world.game.configureBuildingCatalog();
        return AIMaxima::estimateFeeding(*world.game.buildingsTypes.get(id),p);
    };
    const auto normal=estimate(baseline,plan);
    auto costly=baseline;costly["variants"][id]["semantics"]["feeding"]["cost"]={{"food",2},{"wood",1}};
    costly["variants"][id]["properties"]["maxMaterial"][WOOD]=20;
    const auto mixedCost=estimate(costly,plan);
    CHECK(mixedCost.visitsPerTick<=normal.visitsPerTick);
    CHECK(mixedCost.materials[WHEAT]==2*mixedCost.materials[WOOD]);
    auto distant=plan;distant.oneWayTravelTicks*=2;
    CHECK(estimate(baseline,distant).visitsPerTick<=normal.visitsPerTick);
    auto free=baseline;free["variants"][id]["semantics"]["feeding"]["cost"]=nlohmann::json::object();
    auto nobody=plan;nobody.carriers=0;
    const auto freeEstimate=estimate(free,nobody);
    CHECK(freeEstimate.visitsPerTick>=normal.visitsPerTick);
    CHECK(freeEstimate.materials[WHEAT]==0);
    CHECK(freeEstimate.haulingWorkerTicks==0);
    auto hybrid=baseline;auto& spec=hybrid["variants"][id]["semantics"];
    spec["healing"]["enabled"]=true;spec["healing"]["duration"]=12;spec["healing"]["cost"]={{"food",1}};
    spec["production"]["recipes"]={{"worker",{{"enabled",true},{"duration",80},{"cost",{{"food",2}}}}}};
    const auto shared=estimate(hybrid,plan);
    CHECK(shared.visitsPerTick<=normal.visitsPerTick);
    CHECK(shared.haulingWorkerTicks<=plan.carriers*AIMaxima::FeedingEstimate::Scale);
    CHECK(shared.materials[WHEAT]>shared.visitsPerTick);
    CHECK(normal.materials[WHEAT]==normal.visitsPerTick);
}

namespace
{
// Historical overload diagnostic only: this old policy period is not a production capacity model.
int historicalFeedingForecast(const AIMaxima::FeedingEstimate& estimate)
{ return int(estimate.visitsPerTick*11759/AIMaxima::FeedingEstimate::Scale); }

struct FeedingSceneResult { int survivors; Uint64 meals; size_t distinctCarriers; int additionalFeeders; int settledSurvivors; };

FeedingSceneResult measureFeedingScene(const char* label,int stage,int distance,int population,
    int workers,int explorers,const AIMaxima::FeedingPlan& plan,bool adaptive=false)
{
        glob2test::HeadlessGame world({.wDec=6,.hDec=6,.discovered=true,.clearImmobile=true,.loadDefaultRace=true,.header=true,.seed=71});
        world.game.gameHeader.setResourceGrowthDisabled(true);
        // A fixed walkable catchment prevents random idling from taking the
        // population arbitrarily far away. Harvestable inputs are replenished
        // at source, never in the building: real workers must deliver them.
        for(int y=0;y<64;++y)for(int x=0;x<64;++x)
            if(x<8 || y<8 || x>48 || y>48)world.game.map.setCellTerrain(x,y,WATER);
        auto* inn=world.addBuilding("inn",16,16,stage);
        auto order=std::make_shared<OrderModifyBuilding>(inn->gid,plan.carriers);order->sender=0;world.game.executeOrder(order,0);
        for(int unit=0;unit<population;++unit) {
            auto* created=world.addUnit(unit<workers?WORKER:unit<workers+explorers?EXPLORER:WARRIOR);
            // Supply carriers meet this building's explicit qualification;
            // movement, work speed and hunger remain the same at every stage.
            if(unit<workers)created->constructionLevel=inn->type->semantics.requiredWorkerLevel;
        }
        Player player;player.setTeam(world.team);
        std::unique_ptr<AIMaxima::Maxima> controller;
        if(adaptive) {
            controller=std::make_unique<AIMaxima::Maxima>(&player);
            controller->context.initialize();controller->ensure_strategy();
        }
        int additionalFeeders=0,settledSurvivors=0;
        const int warmup=20000,window=40000;
        Uint64 previousMeals=0,previousDelivered=0,reservedSeatTicks=0,workingTicks=0,hungryTicks=0;
        std::set<Uint16> carriers;
        for(int tick=0;tick<warmup+window;++tick) {
            for(int cell=0;cell<8;++cell) {
                const int x=16+distance+cell%2,y=16+cell/2;
                if(!world.game.map.getTile(x,y).resource.amount)world.game.map.setResourceByIndex(x,y,WHEAT,1);
            }
            world.step();
            if(controller && tick%256==0) {
                auto& ai=*controller;
                ai.snapshot=ai.collect_snapshot(ai.context);
                ai.environment.accessible_corn=100;
                ai.environment.food_headroom=ai.environment.food_security=100;
                ai.food_ledger_valid=true;ai.food_supported_inns=1;
                ai.build_policy_bids();
                const bool hungry=ai.snapshot.unserved_food>0 || ai.snapshot.critical_food>0;
                const int requested=ai.policy_bids[AIMaxima::Maxima::PolicySurvival].desired_inns;
                if(hungry && requested>1+additionalFeeders) {
                    // Supply the service requested by the real policy. This
                    // isolates feeding recovery from construction latency;
                    // the separate farm-claim test exercises placement gating.
                    auto* extra=world.addBuilding("inn",22+additionalFeeders*6,16,stage);
                    auto staffing=std::make_shared<OrderModifyBuilding>(extra->gid,plan.carriers);
                    staffing->sender=0;world.game.executeOrder(staffing,0);
                    ++additionalFeeders;
                }
            }
            if(tick+1==warmup) {
                for(int unit=0;unit<Unit::MAX_COUNT;++unit)settledSurvivors+=world.team->myUnits[unit]!=nullptr;
                previousMeals=world.team->stats.measurements.meals;
                previousDelivered=world.team->stats.measurements.delivered[WHEAT];
            }
            if(tick>=warmup) {
                reservedSeatTicks+=inn->unitsInside.size();workingTicks+=inn->unitsWorking.size();
                for(const auto* carrier:inn->unitsWorking)carriers.insert(carrier->gid);
                for(int unit=0;unit<Unit::MAX_COUNT;++unit)if(const auto* u=world.team->myUnits[unit];u && u->hungry<=u->trigHungry)++hungryTicks;
            }
        }
        int survivors=0;for(int unit=0;unit<Unit::MAX_COUNT;++unit)survivors+=world.team->myUnits[unit]!=nullptr;
        const auto meals=world.team->stats.measurements.meals-previousMeals;
        std::cout<<"MAXIMA_FEEDING_MEASUREMENT case="<<label<<" stage="<<stage<<" route="<<distance<<" population="<<population
            <<" workers="<<workers<<" explorers="<<explorers<<" distinct_carriers="<<carriers.size()<<" additional_feeders="<<additionalFeeders<<" survivors="<<survivors
            <<" settled_survivors="<<settledSurvivors<<" meals="<<meals<<" delivered="<<world.team->stats.measurements.delivered[WHEAT]-previousDelivered
            <<" seat_ticks="<<reservedSeatTicks<<" carrier_ticks="<<workingTicks<<" hungry_ticks="<<hungryTicks
            <<" window="<<window<<'\n';
        CHECK(reservedSeatTicks<=Uint64(window)*inn->maxUnitInside);
        CHECK(workingTicks<=Uint64(window)*plan.carriers);
        return {survivors,meals,carriers.size(),additionalFeeders,settledSurvivors};
}
}

TEST_CASE("sustained stock feeding reports capability estimates and historical strategy targets" * doctest::test_suite("Maxima.Economy"))
{
    glob2test::HeadlessGlobals globals;
    const int historical[]={19,25,34};
    const AIMaxima::FeedingPlan plan{4,16*23,105};
    for(int stage=0;stage<3;++stage)for(int distance:{4,16}) {
        CAPTURE(stage);CAPTURE(distance);
        const auto baseline=measureFeedingScene("historical_army",stage,distance,historical[stage],plan.carriers,0,plan);
        CHECK(baseline.survivors==historical[stage]);
        CHECK(baseline.meals>0);
        const auto* inn=globalContainer->buildingsTypes.get(globalContainer->buildingsTypes.getTypeNum("inn",stage,false));
        const auto estimate=AIMaxima::estimateFeeding(*inn,plan);
        std::cout<<"MAXIMA_FEEDING_FORECAST stage="<<stage<<" historical="<<historical[stage]
            <<" historical_period_forecast="<<historicalFeedingForecast(estimate)<<" ticks_per_meal="<<11759<<'\n';
        // Deliberately hostile workload: only four total workers (no replacement
        // carriers), almost all recipients warriors, and raw nominal population
        // without the planner's reliability margin. Report losses, including a
        // possible complete collapse, without claiming this overload is safe.
        const auto overload=measureFeedingScene("nominal_army_overload",stage,distance,historicalFeedingForecast(estimate),plan.carriers,0,plan);
        CHECK(overload.survivors<=historicalFeedingForecast(estimate));
    }
}

TEST_CASE("observed hunger expands feeding for an opening colony mix beyond nominal forecasts" * doctest::test_suite("Maxima.Economy"))
{
    glob2test::HeadlessGlobals globals;
    const auto policy=[] {Fixture f;f.ai->ensure_strategy();return f.ai->strategy;}();
    const AIMaxima::FeedingPlan plan{policy.staffing.new_inn_workers,
        policy.farming.management_radius*policy.food.carrier_ticks_per_tile,
        policy.food.carrier_fixed_ticks_per_trip};
    // Shipped opening worker weight plus low-utility explorer/warrior weights.
    // This fixes a documented colony mix; it is not tuned to these outcomes.
    const int workerWeight=policy.economy.early_worker_ratio;
    const int explorerWeight=policy.economy.explorer_ratio_low;
    const int warriorWeight=policy.military.warrior_ratio_low;
    const int totalWeight=workerWeight+explorerWeight+warriorWeight;
    for(int stage=0;stage<3;++stage)for(int distance:{4,16}) {
        CAPTURE(stage);CAPTURE(distance);
        const auto* inn=globalContainer->buildingsTypes.get(globalContainer->buildingsTypes.getTypeNum("inn",stage,false));
        const int nominal=historicalFeedingForecast(AIMaxima::estimateFeeding(*inn,plan));
        const int population=nominal*policy.economy.reliable_inn_percent/100;
        const int workers=population*workerWeight/totalWeight;
        const int explorers=population*explorerWeight/totalWeight;
        REQUIRE(workers>plan.carriers);
        const auto unassisted=measureFeedingScene("nominal_static_colony",stage,distance,population,workers,explorers,plan);
        const auto observed=measureFeedingScene("adaptive_colony",stage,distance,population,workers,explorers,plan,true);
        // A reactive policy can lose units before recovery. Keep that loss in
        // the measurement; require recovery to stop further attrition and to
        // improve the overloaded base-stage scene over its static control.
        CHECK(observed.survivors==observed.settledSurvivors);
        CHECK(observed.survivors>=unassisted.survivors);
        if(stage==0)CHECK(observed.survivors>unassisted.survivors);
        CHECK(observed.meals>0);
        CHECK(observed.distinctCarriers>size_t(plan.carriers));
        if(stage==0)CHECK(observed.additionalFeeders>0);
    }
}

TEST_CASE("food pressure increases planned feeding capacity without a permanent queue ratchet" * doctest::test_suite("Maxima.Economy"))
{
    glob2test::HeadlessGlobals globals;
    Fixture f;auto& ai=*f.ai;ai.context.initialize();ai.ensure_strategy();ai.collect_building_profiles();
    const int root=f.game.buildingCapabilities().lineageRoot(f.game.buildingsTypes.getPlaceableTypeNum("inn"));
    const int reliable=ai.feeding_capacity(root,1)*ai.strategy.economy.reliable_inn_percent/100;
    // Two reliable providers clear the configured minimum without the unrelated
    // demographic floor masking the extra inn requested for unserved hunger.
    ai.snapshot.population=2*reliable;ai.snapshot.workers=ai.snapshot.population;
    ai.environment.accessible_corn=100;ai.environment.food_headroom=100;ai.environment.food_security=100;
    ai.food_ledger_valid=true;ai.food_supported_inns=1; // stale peak-capacity bound must not suppress recovery.
    ai.build_policy_bids();
    const int comfortable=ai.policy_bids[AIMaxima::Maxima::PolicySurvival].desired_inns;
    ai.snapshot.unserved_food=(ai.snapshot.population*ai.strategy.economy.service_unserved_percent+99)/100;
    ai.build_policy_bids();
    const int stressed=ai.policy_bids[AIMaxima::Maxima::PolicySurvival].desired_inns;
    CHECK(stressed>comfortable);
    for(int pass=0;pass<5;++pass) {
        ai.build_policy_bids();
        CHECK(ai.policy_bids[AIMaxima::Maxima::PolicySurvival].desired_inns==stressed);
    }
    ai.snapshot.unserved_food=0;ai.build_policy_bids();
    CHECK(ai.policy_bids[AIMaxima::Maxima::PolicySurvival].desired_inns==comfortable);
}

TEST_CASE("operating estimates use one production clock and packet denominators" * doctest::test_suite("Maxima.Economy"))
{
    glob2test::HeadlessGlobals globals;
    glob2test::HeadlessGame world({.loadDefaultRace=true});
    const int id=world.game.buildingsTypes.getFinishedTypeNum("inn");
    auto snapshot=nlohmann::json::parse(world.game.buildingsTypes.snapshotJson());
    auto& variant=snapshot["variants"][id];
    variant["properties"]["maxMaterial"][WOOD]=20;
    variant["semantics"]["feeding"]["enabled"]=false;
    variant["semantics"]["production"]["initialRatios"]={0,0,0};
    variant["semantics"]["production"]["scheduling"]="weighted_committed_job";
    variant["semantics"]["production"]["recipes"]={
        {"worker",{{"enabled",true},{"duration",10},{"cost",{{"food",1}}}}},
        {"explorer",{{"enabled",true},{"duration",100},{"cost",{{"wood",3}}}}}};
    world.game.buildingsTypes.loadSnapshotJson(snapshot.dump());world.game.configureBuildingCatalog();
    const AIMaxima::FeedingPlan plan{1000,0,1};
    const auto ordinary=AIMaxima::estimateFeeding(*world.game.buildingsTypes.get(id),plan);
    const auto rate=AIMaxima::FeedingEstimate::Scale/(11+101);
    CHECK(ordinary.materials[WHEAT]==rate);
    CHECK(ordinary.materials[WOOD]==3*rate);
    CHECK(ordinary.productionRates[WORKER]==rate*1000/AIMaxima::FeedingEstimate::Scale);
    CHECK(ordinary.productionRates[EXPLORER]==ordinary.productionRates[WORKER]);
    variant["properties"]["materialMultiplier"][WHEAT]=10;
    variant["properties"]["materialMultiplier"][WOOD]=10;
    world.game.buildingsTypes.loadSnapshotJson(snapshot.dump());world.game.configureBuildingCatalog();
    const auto packet=AIMaxima::estimateFeeding(*world.game.buildingsTypes.get(id),plan);
    CHECK(packet.materials==ordinary.materials);
    CHECK(packet.resourcePackets[WHEAT]==ordinary.resourcePackets[WHEAT]/10);
    CHECK(packet.resourcePackets[WOOD]==ordinary.resourcePackets[WOOD]/10);
    CHECK(packet.productionRates==ordinary.productionRates);
    CHECK(packet.haulingWorkerTicks==ordinary.materials[WHEAT]/10+ordinary.materials[WOOD]/10);
    CHECK(packet.haulingWorkerTicks<ordinary.haulingWorkerTicks);
}

TEST_CASE("parallel training budgets separate compatible recipients and disabled services" * doctest::test_suite("Maxima.Economy"))
{
    glob2test::HeadlessGlobals globals;
    glob2test::HeadlessGame world({.loadDefaultRace=true});
    const int id=world.game.buildingsTypes.getFinishedTypeNum("inn");
    auto snapshot=nlohmann::json::parse(world.game.buildingsTypes.snapshotJson());auto& variant=snapshot["variants"][id];
    variant["properties"]["maxUnitInside"]=2;
    variant["properties"]["maxMaterial"][WOOD]=20;variant["properties"]["maxMaterial"][STONE]=20;
    variant["semantics"]["feeding"]["enabled"]=false;
    variant["semantics"]["trainingInParallel"]=true;
    variant["semantics"]["training"]={
        {"walk",{{"enabled",true},{"unitMask",1},{"targetLevel",1},{"duration",10},{"cost",{{"wood",1}}}}},
        {"attackStrength",{{"enabled",true},{"unitMask",4},{"targetLevel",1},{"duration",100},{"cost",{{"stone",3}}}}},
        {"swim",{{"enabled",true},{"unitMask",1},{"targetLevel",0},{"constructionLevel",0},{"duration",500},{"cost",{{"food",5}}}}}};
    world.game.buildingsTypes.loadSnapshotJson(snapshot.dump());world.game.configureBuildingCatalog();
    AIMaxima::FeedingPlan plan{1000,0,1};
    const auto* type=world.game.buildingsTypes.get(id);
    const auto trained=AIMaxima::estimateFeeding(*type,plan);
    CHECK(trained.materials[WOOD]==AIMaxima::FeedingEstimate::Scale/AIMaximaBuildings::serviceTicks(*type,10));
    CHECK(trained.materials[STONE]==3*(AIMaxima::FeedingEstimate::Scale/AIMaximaBuildings::serviceTicks(*type,100)));
    CHECK(trained.materials[WHEAT]==0); // no level-zero improvement
    variant["semantics"]["feeding"]["enabled"]=true;
    world.game.buildingsTypes.loadSnapshotJson(snapshot.dump());world.game.configureBuildingCatalog();type=world.game.buildingsTypes.get(id);
    const auto hybrid=AIMaxima::estimateFeeding(*type,plan);
    plan.training=false;const auto disabled=AIMaxima::estimateFeeding(*type,plan);
    CHECK(disabled.visitsPerTick>hybrid.visitsPerTick);
    CHECK(disabled.materials[WOOD]==0);CHECK(disabled.materials[STONE]==0);
    plan.feeding=false;CHECK(AIMaxima::estimateFeeding(*type,plan).visitsPerTick==0);
}

TEST_CASE("projectile profiles reserve ammunition workers and cache nominal feeding" * doctest::test_suite("Maxima.Economy"))
{
    glob2test::HeadlessGlobals globals;
    Fixture world;auto& ai=*world.ai;ai.ensure_strategy();
    const int tower=world.game.buildingsTypes.getPlaceableTypeNum("defencetower");
    const int inn=world.game.buildingsTypes.getPlaceableTypeNum("inn");
    const auto* profile=ai.profile_variant(tower,1);REQUIRE(profile);
    CHECK(profile->operatingMaterials[STONE]>0);
    CHECK(profile->serviceRates[AIMaximaBuildings::ProjectileDefense]>0);
    const auto* cached=ai.development_feeding_visit_rate.data();
    const int capacity=ai.feeding_capacity(inn,1);
    REQUIRE(capacity>0);
    for(int read=0;read<100;++read)CHECK(ai.feeding_capacity(inn,1)==capacity);
    CHECK(ai.development_feeding_visit_rate.data()==cached);
}

TEST_CASE("feeding budget scaling handles maximum seats without overflowing intermediate products" * doctest::test_suite("Maxima.Economy"))
{
    glob2test::HeadlessGlobals globals;
    glob2test::HeadlessGame world({.loadDefaultRace=true});
    const int id=world.game.buildingsTypes.getFinishedTypeNum("inn");
    auto snapshot=nlohmann::json::parse(world.game.buildingsTypes.snapshotJson());
    auto& variant=snapshot["variants"][id];
    variant["properties"]["maxUnitInside"]=32767;
    variant["properties"]["insideSpeed"]=256;
    variant["semantics"]["feeding"]["duration"]=0;
    world.game.buildingsTypes.loadSnapshotJson(snapshot.dump());world.game.configureBuildingCatalog();
    const auto estimate=AIMaxima::estimateFeeding(*world.game.buildingsTypes.get(id),{1024,0,0});
    CHECK(estimate.visitsPerTick==1024*AIMaxima::FeedingEstimate::Scale);
    CHECK(estimate.haulingWorkerTicks==1024*AIMaxima::FeedingEstimate::Scale);
}

TEST_CASE("one training course credits independent movement and worker construction outcomes" * doctest::test_suite("Maxima.Economy"))
{
    glob2test::HeadlessGlobals globals;
    glob2test::HeadlessGame world({.loadDefaultRace=true});
    const int id=world.game.buildingsTypes.getFinishedTypeNum("inn");
    auto snapshot=nlohmann::json::parse(world.game.buildingsTypes.snapshotJson());auto& variant=snapshot["variants"][id];
    variant["properties"]["maxMaterial"][WOOD]=20;
    variant["semantics"]["feeding"]["enabled"]=false;
    variant["semantics"]["trainingInParallel"]=true;
    variant["semantics"]["training"]={{"walk",{{"enabled",true},{"unitMask",5},{"targetLevel",1},
        {"constructionLevel",1},{"duration",10},{"cost",{{"wood",1}}}}}};
    world.game.buildingsTypes.loadSnapshotJson(snapshot.dump());world.game.configureBuildingCatalog();
    const AIMaxima::FeedingPlan plan{1000,0,1};
    const auto both=AIMaxima::estimateFeeding(*world.game.buildingsTypes.get(id),plan);
    CHECK(both.services[AIMaximaBuildings::WalkTraining]>0);
    CHECK(both.services[AIMaximaBuildings::ConstructionTraining]>0);
    CHECK(both.services[AIMaximaBuildings::WalkTraining]==2*both.services[AIMaximaBuildings::ConstructionTraining]);
    variant["semantics"]["training"]["walk"]["unitMask"]=4;
    world.game.buildingsTypes.loadSnapshotJson(snapshot.dump());world.game.configureBuildingCatalog();
    const auto warrior=AIMaxima::estimateFeeding(*world.game.buildingsTypes.get(id),plan);
    CHECK(warrior.services[AIMaximaBuildings::WalkTraining]>0);
    CHECK(warrior.services[AIMaximaBuildings::ConstructionTraining]==0);
}

TEST_CASE("feeding demand allocation conserves recipient classes and shared provider capacity" * doctest::test_suite("Maxima.Economy"))
{
    using AIMaxima::allocateFeedingDemand;
    using AIMaxima::FeedingProvider;
    std::vector<std::array<int,3>> demand{{600,0,400},{200,0,0}};
    const auto split=allocateFeedingDemand(demand,{{0,1000,7},{0,1000,7},{1,1000,1}});
    CHECK(split[0]+split[1]==1000);CHECK(split[2]==200);
    const auto restricted=allocateFeedingDemand({{600,0,400}},{{0,600,1},{0,400,5}});
    CHECK(restricted[0]==600);CHECK(restricted[1]==400);
    const auto missing=allocateFeedingDemand({{600,0,400}},{{0,1000,1}});
    CHECK(missing[0]==600);
    const auto tight=allocateFeedingDemand({{1000,1000,1000}},{{0,700,7},{0,300,3}});
    CHECK(tight[0]+tight[1]==1000);
    CHECK(allocateFeedingDemand({{0,0,0}},{{0,1000,7}})[0]==0);
}

TEST_CASE("feeding farm claims follow one cohort while hybrid operating demand stays independent" * doctest::test_suite("Maxima.Economy"))
{
    using namespace AIMaximaPlacement;
    WorldState world;world.reset(32,32);
    for(auto& t:world.tiles){t.discovered=t.walkable=t.foodTraversable=t.buildable=true;}
    world.tile(9,10).protectedYield=1200;
    world.feedingColonies.push_back({8,8,{600,0,400}});
    BuildingProfile profile;profile.buildingType=0;
    BuildingLevelProfile shape;shape.level=1;shape.footprint=Footprint(0,0,1,1);
    shape.roles=AIMaximaBuildings::roleBit(AIMaximaBuildings::Feeding);
    shape.feedingRate=1000;shape.feedingMask=7;
    shape.operatingMaterials[WHEAT]=shape.feedingMaterials[WHEAT]=1000;
    profile.levels.push_back(shape);world.profiles.push_back(profile);
    WorldBuilding first;first.id=1;first.gid=1;first.buildingType=0;first.level=1;first.centerX=8;first.centerY=8;
    world.buildings.push_back(first);
    Planner planner;planner.mutablePolicy().foodLedgerEnabled=true;
    const auto configure=[&] {planner.configure(world.profiles,AIMaximaBuildings::Feeding,AIMaximaBuildings::Healing,
        AIMaximaBuildings::ConstructionTraining,AIMaximaBuildings::CombatTraining,AIMaximaBuildings::ProjectileDefense,AIMaximaBuildings::Production);};
    configure();
    CHECK(planner.evaluateFoodLedger(world).consumer(1)->demand==1000);
    DevelopmentAction candidate;candidate.type=BuildStandalone;candidate.buildingType=0;candidate.targetLevel=1;
    candidate.centerX=10;candidate.centerY=8;candidate.initialFootprint=shape.footprint;
    RejectionReason reason=RejectedFoodCapacity;
    CHECK(planner.foodCandidatePasses(world,nullptr,candidate,reason));
    CHECK(planner.foodCandidateDemand==500);
    CHECK(planner.selectedFoodResult().consumer(1)->demand==500);
    // Crossing colony boundaries must only select an immutable cached result.
    world.feedingColonies.push_back({24,24,{300,0,200}});
    planner.evaluateFoodLedger(world);
    planner.prepareFeedingCandidateSet(world,0,1,-1,-1);
    const auto* cachedLedgers=planner.candidateFoodLedgers.data();
    const auto cachedEpoch=planner.foodLedgerEpoch;
    for(int visit=0;visit<100;++visit) {
        candidate.centerX=candidate.centerY=(visit&1)?24:10;
        planner.prepareFeedingCandidate(world,candidate);
        CHECK(planner.candidateFoodLedgers.data()==cachedLedgers);
        CHECK(planner.foodLedgerEpoch==cachedEpoch);
        CHECK(planner.foodCandidateDemand==500);
    }
    world.feedingColonies.pop_back();candidate.centerX=10;candidate.centerY=8;
    auto second=first;second.id=second.gid=2;second.centerX=10;world.buildings.push_back(second);
    const auto& both=planner.evaluateFoodLedger(world);
    CHECK(both.consumer(1)->demand+both.consumer(2)->demand==1000);
    world.profiles[0].levels[0].operatingMaterials[WHEAT]+=250;
    configure();
    const auto& hybrid=planner.evaluateFoodLedger(world);
    CHECK(hybrid.consumer(1)->demand+hybrid.consumer(2)->demand==1500);
    world.buildings.pop_back();
    planner.evaluateFoodLedger(world);
    CHECK_FALSE(planner.foodCandidatePasses(world,nullptr,candidate,reason));
    CHECK(reason==RejectedFoodCapacity);
    // A free feeder still absorbs its share of meals; only its independent
    // production demand remains, and the other provider keeps its own budget.
    auto free=world.profiles[0];free.buildingType=1;
    free.levels[0].operatingMaterials[WHEAT]=250;free.levels[0].feedingMaterials[WHEAT]=0;
    world.profiles.push_back(free);second.buildingType=1;world.buildings.push_back(second);world.invalidateProfileIndex();configure();
    const auto& mixed=planner.evaluateFoodLedger(world);
    CHECK(mixed.consumer(1)->demand==750);CHECK(mixed.consumer(2)->demand==250);
    auto* bytes=new GAGCore::MemoryStreamBackend;
    GAGCore::BinaryOutputStream output(bytes);
    AIMaximaContinuation::Writer writer(&output,true);writer("world",world);
    const std::string saved(bytes->getBuffer(),bytes->getPosition());
    GAGCore::BinaryInputStream input(new GAGCore::MemoryStreamBackend(saved.data(),saved.size()));
    input.seekFromStart(0);
    WorldState restored;AIMaximaContinuation::Reader reader(&input,true);reader("world",restored);
    Planner resumed;resumed.mutablePolicy().foodLedgerEnabled=true;resumed.configure(restored.profiles,AIMaximaBuildings::Feeding,AIMaximaBuildings::Healing,
        AIMaximaBuildings::ConstructionTraining,AIMaximaBuildings::CombatTraining,AIMaximaBuildings::ProjectileDefense,AIMaximaBuildings::Production);
    const auto& continued=resumed.evaluateFoodLedger(restored);
    CHECK(continued.consumer(1)->demand==750);CHECK(continued.consumer(2)->demand==250);
    CHECK(restored.computeSignature()==world.computeSignature());
}

TEST_CASE("feeding packet denominations preserve large stock rates and profile continuation" * doctest::test_suite("Maxima.Economy"))
{
    glob2test::HeadlessGlobals globals;
    Fixture f;auto snapshot=nlohmann::json::parse(f.game.buildingsTypes.snapshotJson());
    const int id=f.game.buildingsTypes.getFinishedTypeNum("inn");
    snapshot["variants"][id]["properties"]["maxMaterial"][WHEAT]=1000000;
    snapshot["variants"][id]["properties"]["materialMultiplier"]=std::vector<int>(MaterialSlotCount,1);
    snapshot["variants"][id]["properties"]["materialMultiplier"][WHEAT]=1000000;
    snapshot["variants"][id]["semantics"]["feeding"]["cost"]={{"food",1000000}};
    f.game.buildingsTypes.loadSnapshotJson(snapshot.dump());f.game.configureBuildingCatalog();
    f.ai->ensure_strategy();
    const auto estimate=AIMaxima::estimateFeeding(*f.game.buildingsTypes.get(id),{4,16*23,105});
    CHECK(estimate.materials[WHEAT]==INT_MAX);
    CHECK(estimate.resourcePackets[WHEAT]>INT_MAX/1000000);
    CHECK(estimate.resourcePackets[WHEAT]==estimate.feedingResourcePackets[WHEAT]);
    const int root=f.game.buildingCapabilities().lineageRoot(id);
    const auto profiles=f.ai->collect_building_profiles();
    const auto found=std::find_if(profiles.begin(),profiles.end(),[&](const auto& p){return p.buildingType==root;});
    REQUIRE(found!=profiles.end());
    auto* bytes=new GAGCore::MemoryStreamBackend;GAGCore::BinaryOutputStream output(bytes);
    AIMaximaContinuation::Writer writer(&output,true);writer("profile",*found);
    const std::string saved(bytes->getBuffer(),bytes->getPosition());
    GAGCore::BinaryInputStream input(new GAGCore::MemoryStreamBackend(saved.data(),saved.size()));input.seekFromStart(0);
    AIMaximaPlacement::BuildingProfile restored;AIMaximaContinuation::Reader reader(&input,true);reader("profile",restored);
    CHECK(restored.levels.front().feedingMaterials[WHEAT]==restored.levels.front().operatingMaterials[WHEAT]);
}

TEST_CASE("feeding colonies join overlapping catchments transitively and preserve independent training" * doctest::test_suite("Maxima.Economy"))
{
    glob2test::HeadlessGlobals globals;
    Fixture f;
    auto snapshot=nlohmann::json::parse(f.game.buildingsTypes.snapshotJson());
    const int id=f.game.buildingsTypes.getFinishedTypeNum("inn");
    snapshot["variants"][id]["semantics"]["training"]={{"armor",{{"enabled",true},{"unitMask",4},{"targetLevel",1},{"duration",0},{"cost",nlohmann::json::object()}}}};
    f.game.buildingsTypes.loadSnapshotJson(snapshot.dump());f.game.configureBuildingCatalog();
    f.swarm(4,8);f.swarm(20,8);f.swarm(36,8);
    f.game.addUnit(40,14,0,WORKER,0,0,0,0);
    f.ai->context.initialize();f.ai->ensure_strategy();
    const auto world=f.ai->collect_development_world(f.ai->context);
    REQUIRE(world.feedingColonies.size()==1);
    CHECK(world.feedingColonies.front().demand[WORKER]>0);
    const int root=f.game.buildingCapabilities().lineageRoot(id);
    REQUIRE(world.profile(root));
    CHECK_FALSE(world.profile(root)->levels.front().foodRetirable);
}

TEST_CASE("recipient meal demand follows saved hunger and external action clocks" * doctest::test_suite("Maxima.Economy"))
{
    glob2test::HeadlessGlobals globals;
    Fixture f;auto& ai=*f.ai;ai.ensure_strategy();
    auto* worker=f.game.addUnit(10,10,0,WORKER,0,0,0,0);REQUIRE(worker);
    auto* stat=f.player.team->stats.getLatestStat();stat->totalUnit=stat->numberUnitPerType[WORKER]=1;
    ai.collect_building_profiles();
    worker->hungriness=425;
    ai.snapshot=ai.collect_snapshot(ai.context);
    const int initialDemand=ai.snapshot.feeding_demand[WORKER];
    const int inn=f.game.buildingsTypes.getPlaceableTypeNum("inn");
    const int capacity425=ai.feeding_capacity(inn,1);
    REQUIRE(initialDemand>0);REQUIRE(capacity425>0);
    worker->hungriness=700; // SmallForTwo's persisted race, not modern defaults.
    ai.snapshot=ai.collect_snapshot(ai.context);
    CHECK(ai.snapshot.feeding_demand[WORKER]>initialDemand);
    CHECK(ai.feeding_capacity(inn,1)<capacity425);
    const auto recurring=ai.recipient_meal_rate(*worker);
    worker->hungry=Unit::HUNGRY_MAX;worker->speed=256;worker->displacement=Unit::DIS_INSIDE;
    CHECK(ai.recipient_meal_rate(*worker)==recurring); // service state cannot erase recurring demand
    worker->displacement=Unit::DIS_RANDOM;worker->performance[WALK]=24;
    CHECK(ai.recipient_meal_rate(*worker)>recurring);
    worker->hungriness=0;CHECK(ai.recipient_meal_rate(*worker)==0);
    worker->hungriness=700;f.game.gameHeader.setHungerDisabled(true);
    CHECK(ai.recipient_meal_rate(*worker)==0);
    f.game.gameHeader.setHungerDisabled(false);
    const auto slow=AIMaxima::recipientMealRate(150000,37500,1,1,WALK,16,534);
    CHECK(slow>0); // fractional population demand survives until aggregation
    const auto shortVisit=AIMaxima::recipientMealRate(150000,37500,700,16,WALK,16,214);
    const auto longVisit=AIMaxima::recipientMealRate(150000,37500,700,16,WALK,16,534);
    CHECK(longVisit<shortVisit); // hunger pauses once during actual service
}

TEST_CASE("mechanical production ceilings are separate from planned carrier throughput" * doctest::test_suite("Maxima.Economy"))
{
    glob2test::HeadlessGlobals globals;
    Fixture f;const auto* producer=f.game.buildingsTypes.get(f.game.buildingsTypes.getFinishedTypeNum("swarm"));
    AIMaxima::FeedingPlan plan{8,16*23,105};plan.productionMask=1u<<WORKER;
    const auto planned=AIMaxima::estimateFeeding(*producer,plan);
    plan.constrainHauling=false;
    const auto ceiling=AIMaxima::estimateFeeding(*producer,plan);
    const auto& recipe=producer->semantics.production.recipes[WORKER];
    const auto raw=AIMaxima::FeedingEstimate::Scale/(recipe.duration+1);
    CHECK(ceiling.productionRates[WORKER]==raw*1000/AIMaxima::FeedingEstimate::Scale);
    CHECK(ceiling.productionResourcePackets[WHEAT]==raw*recipe.cost[WHEAT]/producer->materialMultiplier[WHEAT]);
    CHECK(ceiling.productionRates[WORKER]>planned.productionRates[WORKER]);
    plan.carriers=1;plan.oneWayTravelTicks=1000;
    const auto distant=AIMaxima::estimateFeeding(*producer,plan);
    CHECK(distant.productionRates==ceiling.productionRates);
    CHECK(distant.productionResourcePackets==ceiling.productionResourcePackets);
    plan.constrainHauling=true;
    CHECK(AIMaxima::estimateFeeding(*producer,plan).productionRates[WORKER]<planned.productionRates[WORKER]);
    f.ai->ensure_strategy();
    const auto* profile=f.ai->profile_variant(f.game.buildingsTypes.getPlaceableTypeNum("swarm"));
    REQUIRE(profile);
    CHECK(profile->productionMaterials[WHEAT]>planned.productionResourcePackets[WHEAT]);
}

TEST_CASE("birth funding counts production packets without independent hybrid service costs" * doctest::test_suite("Maxima.Economy"))
{
    glob2test::HeadlessGlobals globals;
    Fixture f;auto& ai=*f.ai;ai.ensure_strategy();
    using namespace AIMaximaPlacement;
    WorldState world;world.reset(16,16);
    for(auto& tile:world.tiles)tile.discovered=tile.walkable=tile.foodTraversable=tile.buildable=true;
    world.tile(8,8).protectedYield=3000;
    BuildingProfile profile;profile.buildingType=0;
    BuildingLevelProfile variant;variant.level=1;variant.engineType=0;variant.completedType=0;
    variant.footprint=Footprint(0,0,1,1);variant.roles=AIMaximaBuildings::roleBit(AIMaximaBuildings::Production);
    variant.productionUnitMask=1u<<WORKER;variant.productionMaterials[WHEAT]=1000;
    variant.operatingMaterials[WHEAT]=3000; // independent training consumes remaining2000
    profile.levels.push_back(variant);world.profiles.push_back(profile);
    WorldBuilding building;building.id=building.gid=1;building.buildingType=0;building.level=1;building.centerX=7;building.centerY=8;world.buildings.push_back(building);
    ai.development_building_profiles=world.profiles;ai.development_profiles_initialized=true;
    ai.development_profile_index.assign(f.game.buildingsTypes.size(),-1);ai.development_profile_index[0]=0;
    ai.development_planner.mutablePolicy().foodLedgerEnabled=true;
    ai.development_planner.mutablePolicy().foodSwarmDemand=1000;
    ai.development_planner.configure(world.profiles,AIMaximaBuildings::Feeding,AIMaximaBuildings::Healing,
        AIMaximaBuildings::ConstructionTraining,AIMaximaBuildings::CombatTraining,AIMaximaBuildings::ProjectileDefense,AIMaximaBuildings::Production);
    ai.update_food_retirement(ai.context,world);
    CHECK(ai.food_supported_swarms==1);
    CHECK(ai.food_birth_crop_rate==1000);
    CHECK(ai.birth_food_acreage()==1000LL*ai.strategy.food.growth_period_ticks*65536/AIMaximaFoodLedger::RateScale);
    world.profiles[0].levels[0].productionUnitMask=0;
    world.profiles[0].levels[0].productionMaterials[WHEAT]=0;
    ai.development_building_profiles=world.profiles;
    ai.development_planner.configure(world.profiles,AIMaximaBuildings::Feeding,AIMaximaBuildings::Healing,
        AIMaximaBuildings::ConstructionTraining,AIMaximaBuildings::CombatTraining,AIMaximaBuildings::ProjectileDefense,AIMaximaBuildings::Production);
    ai.update_food_retirement(ai.context,world);
    CHECK(ai.food_supported_swarms==0);
    CHECK(ai.food_birth_crop_rate==0);
}

TEST_CASE("feeding capacity preserves independently admitted recipient classes" * doctest::test_suite("Maxima.Economy"))
{
    const std::array<int,3> demand{1000,1000,0};
    std::array<long long,8> rates{};
    rates[1u<<WORKER]=1000000;
    rates[1u<<EXPLORER]=1000;
    CHECK(AIMaxima::feedingPopulationCapacity(demand,20,rates)==20);
    rates[1u<<EXPLORER]=0;
    CHECK(AIMaxima::feedingPopulationCapacity(demand,20,rates)==0);
    rates[3]=500;
    CHECK(AIMaxima::feedingPopulationCapacity(demand,20,rates)==10);
    rates={};rates[3]=1000;
    CHECK(AIMaxima::feedingPopulationCapacity(demand,20,rates)==10);
    rates[3]=2000;
    CHECK(AIMaxima::feedingPopulationCapacity(demand,20,rates)==20);
    CHECK(AIMaxima::feedingPopulationCapacity({INT_MAX,INT_MAX,INT_MAX},INT_MAX,rates)>=0);
}

TEST_CASE("service ceilings respect the engine action clock and crop funding avoids overflow" * doctest::test_suite("Maxima.Economy"))
{
    glob2test::HeadlessGlobals globals;
    Fixture f;auto& ai=*f.ai;ai.ensure_strategy();
    BuildingType type=*f.game.buildingsTypes.get(f.game.buildingsTypes.getFinishedTypeNum("inn"));
    type.insideSpeed=32000;
    CHECK(AIMaximaBuildings::serviceTicks(type,10)==11);
    type.insideSpeed=256;
    CHECK(AIMaximaBuildings::serviceTicks(type,10)==11);
    ai.food_birth_crop_rate=INT_MAX;
    ai.strategy.food.growth_period_ticks=100000;
    const long long crop=static_cast<long long>(INT_MAX)*100000;
    CHECK(ai.birth_food_acreage()==crop/AIMaximaFoodLedger::RateScale*65536
        +(crop%AIMaximaFoodLedger::RateScale)*65536/AIMaximaFoodLedger::RateScale);
}

TEST_CASE("food retirement preserves the only explorer feeder despite surplus worker seats" * doctest::test_suite("Maxima.Economy"))
{
    glob2test::HeadlessGlobals globals;
    Fixture f;
    const int workerType=f.game.buildingsTypes.getTypeNum("inn",0,false);
    const int explorerType=f.game.buildingsTypes.getTypeNum("inn",1,false);
    auto catalog=nlohmann::json::parse(f.game.buildingsTypes.snapshotJson());
    auto& workers=catalog["variants"][workerType];
    workers["semantics"]["feeding"]["unitMask"]=1u<<WORKER;
    workers["semantics"]["feeding"]["duration"]=0;
    workers["semantics"]["feeding"]["cost"]=nlohmann::json::object();
    workers["properties"]["maxUnitInside"]=100;
    catalog["variants"][explorerType]["semantics"]["feeding"]["unitMask"]=1u<<EXPLORER;
    f.game.buildingsTypes.loadSnapshotJson(catalog.dump());f.game.configureBuildingCatalog();
    REQUIRE(f.game.addBuilding(10,10,workerType,0));
    REQUIRE(f.game.addBuilding(26,26,explorerType,0));
    for(int i=0;i<10;++i) {
        REQUIRE(f.game.addUnit(5+i,5,0,WORKER,0,0,0,0));
        REQUIRE(f.game.addUnit(5+i,6,0,EXPLORER,0,0,0,0));
    }
    auto& ai=*f.ai;ai.ensure_strategy();ai.context.initialize();
    ai.initialize_farming_cache(ai.context);ai.configure_development_planner();
    ai.snapshot.population=20;ai.snapshot.workers=10;ai.snapshot.explorers=10;
    ai.snapshot.feeding_demand[WORKER]=1000;ai.snapshot.feeding_demand[EXPLORER]=1000;
    ai.budget.food_ledger_enabled=ai.budget.food_retirement_enabled=true;
    ai.budget.recovery_active=false;ai.timer=7000;ai.relocation_target_building=-1;
    ai.food_burden_since[0]=ai.food_burden_since[1]=1000;
    ai.update_food_retirement(ai.context,ai.collect_development_world(ai.context));
    CHECK(ai.food_burden_since.count(1)==1);
    for(const auto& order:ai.context.managementOrders)
        CHECK(dynamic_cast<Management::DestroyBuilding*>(order.get())==nullptr);
}

TEST_CASE("aggregate feeding demand escapes a crop count cap during service shortage" * doctest::test_suite("Maxima.Economy"))
{
    glob2test::HeadlessGlobals globals;
    for(bool ledger:{false,true})for(int recipe:{0,1,2}) {
        CAPTURE(ledger);CAPTURE(recipe);
        Fixture f;
        if(recipe) {
            auto catalog=nlohmann::json::parse(f.game.buildingsTypes.snapshotJson());
            for(auto& variant:catalog["variants"])if(variant["semantics"]["feeding"]["enabled"].get<bool>()) {
                variant["semantics"]["feeding"]["cost"]=recipe==1?nlohmann::json::object():nlohmann::json{{"wood",1}};
                if(recipe==2)variant["properties"]["maxMaterial"][WOOD]=20;
            }
            f.game.buildingsTypes.loadSnapshotJson(catalog.dump());f.game.configureBuildingCatalog();
        }
        auto& ai=*f.ai;ai.context.initialize();ai.ensure_strategy();ai.collect_building_profiles();
        ai.strategy.food.enabled=ledger;ai.food_ledger_valid=ledger;
        ai.snapshot.population=154;ai.snapshot.workers=149;ai.snapshot.explorers=5;
        ai.snapshot.inns=10;ai.snapshot.critical_food=24;ai.snapshot.unserved_food=22;
        ai.snapshot.feeding_demand[WORKER]=38000;ai.snapshot.feeding_demand[EXPLORER]=2000;
        ai.environment.accessible_corn=24;ai.environment.feeding_capacity=125;
        ai.environment.food_headroom=33;ai.environment.food_security=36;ai.demands.food=100;
        ai.build_policy_bids();
        const int requested=ai.policy_bids[AIMaxima::Maxima::PolicySurvival].desired_inns;
        if(recipe==0)CHECK(requested>10); // reproduce the observed paid-feeding shortage
        CHECK(requested>ai.strategy.economy.inn_target_floor);
        CHECK(requested<=ai.strategy.economy.inn_target_cap);
        // More wheat cannot change recipient demand or make a free/wood-based
        // feeding service require an arbitrary acreage allocation per building.
        ai.environment.accessible_corn=0;ai.build_policy_bids();
        CHECK(ai.policy_bids[AIMaxima::Maxima::PolicySurvival].desired_inns==requested);
    }
}

TEST_CASE("operating production claims preserve weighted recipe costs and shared carrier work" * doctest::test_suite("Maxima.Economy"))
{
    using namespace AIMaxima;
    ProductionRecipeModel recipes;
    recipes.ticks[WORKER]=1000001;recipes.costs[WORKER][WHEAT]=1000;
    int ratios[3]{1,0,0};
    CHECK(productionPacketCeiling(recipes,ratios)[WHEAT]==999);
    recipes.packetSize[WHEAT]=10;
    CHECK(productionPacketCeiling(recipes,ratios)[WHEAT]==99);
    recipes.packetSize[WHEAT]=1;recipes.ticks[WORKER]=151;recipes.costs[WORKER][WHEAT]=5;
    recipes.ticks[EXPLORER]=301;recipes.costs[EXPLORER][WHEAT]=7;
    ratios[WORKER]=3;ratios[EXPLORER]=1;
    CHECK(productionPacketCeiling(recipes,ratios)[WHEAT]==22000000/754);
    ratios[WORKER]=ratios[EXPLORER]=0;
    CHECK(productionPacketCeiling(recipes,ratios)[WHEAT]==0);
    recipes.ticks[WARRIOR]=1000001;recipes.costs[WARRIOR][WHEAT]=1000000;
    ratios[WARRIOR]=32767;
    CHECK(productionPacketCeiling(recipes,ratios)[WHEAT]==999999);

    std::array<int,MaterialCount> independent{},production{},trips{};
    independent[WHEAT]=3000;independent[WOOD]=4000;production[WHEAT]=10000;trips.fill(300);
    const auto shared=operatingClaim(independent,production,3,trips);
    CHECK(shared.production[WHEAT]==3000);CHECK(shared.total[WHEAT]==6000);CHECK(shared.total[WOOD]==4000);
    const auto shortStaffed=operatingClaim(independent,production,1,trips);
    CHECK(shortStaffed.production[WHEAT]==0);CHECK(shortStaffed.total[WHEAT]==3000);CHECK(shortStaffed.total[WOOD]==4000);
    trips.fill(100);CHECK(operatingClaim(independent,production,3,trips).production[WHEAT]==10000);
    production[WHEAT]=0;production[WOOD]=10000;
    CHECK(operatingClaim(independent,production,3,trips).production[WHEAT]==0);
    independent[WHEAT]=INT_MAX;trips.fill(INT_MAX);
    const auto saturated=operatingClaim(independent,production,1024,trips);
    CHECK(saturated.total[WHEAT]==INT_MAX);CHECK(saturated.total[WOOD]==4000);
    CHECK(saturated.production[WOOD]==0);
}

TEST_CASE("production ledger uses requested staffing ratios and target stage with continuation" * doctest::test_suite("Maxima.Economy"))
{
    using namespace AIMaximaPlacement;
    WorldState world;world.reset(32,32);
    for(auto& tile:world.tiles)tile.discovered=tile.walkable=tile.foodTraversable=tile.buildable=true;
    world.tile(9,8).foodOpportunity=1;world.tile(9,8).protectedYield=1000000;
    BuildingProfile profile;profile.buildingType=0;
    BuildingLevelProfile stage;stage.level=1;stage.footprint=Footprint(0,0,1,1);
    stage.roles=AIMaximaBuildings::roleBit(AIMaximaBuildings::Production);
    stage.initialCarriers=8;stage.operatingAssignmentLimit=20;
    stage.productionRecipes.ticks[WORKER]=151;stage.productionRecipes.costs[WORKER][WHEAT]=5;
    stage.productionMaterials[WHEAT]=5000000/151;
    stage.independentMaterials[WHEAT]=1000;
    stage.operatingMaterials[WHEAT]=stage.productionMaterials[WHEAT]+1000;
    profile.levels.push_back(stage);world.profiles.push_back(profile);
    WorldBuilding building;building.id=building.gid=1;building.buildingType=0;building.level=1;
    building.centerX=8;building.centerY=8;building.plannedCarriers=2;
    building.productionRatios[WORKER]=1;building.productionRatios[EXPLORER]=building.productionRatios[WARRIOR]=0;
    world.buildings.push_back(building);
    Planner planner;planner.mutablePolicy().foodLedgerEnabled=true;
    planner.mutablePolicy().carrierFixedTicksPerTrip=100;
    planner.mutablePolicy().carrierTicksPerTile=10;
    const auto configure=[&](Planner& target,const WorldState& state) {
        target.configure(state.profiles,AIMaximaBuildings::Feeding,AIMaximaBuildings::Healing,
            AIMaximaBuildings::ConstructionTraining,AIMaximaBuildings::CombatTraining,AIMaximaBuildings::ProjectileDefense,AIMaximaBuildings::Production);
    };
    configure(planner,world);
    const auto low=planner.evaluateFoodLedger(world).consumer(1)->demand;
    CHECK(low==20000);
    const auto oldSignature=world.computeSignature();
    world.buildings[0].plannedCarriers=8;
    CHECK(world.computeSignature()!=oldSignature);
    CHECK(planner.evaluateFoodLedger(world).consumer(1)->demand==stage.operatingMaterials[WHEAT]);
    world.buildings[0].centerX=24;
    CHECK(planner.evaluateFoodLedger(world).consumer(1)->demand<stage.operatingMaterials[WHEAT]);
    world.buildings[0].productionRatios[WORKER]=0;
    CHECK(planner.evaluateFoodLedger(world).consumer(1)->demand==1000);
    CHECK(planner.evaluateFoodLedger(world).consumer(1)->productionDemand==0);

    // A target newly enabling production and staffing cannot inherit the old
    // stage's zero request and disabled ratio. Its future plan reserves work.
    world.buildings[0].centerX=8;world.buildings[0].plannedCarriers=0;
    world.profiles[0].levels[0].operatingAssignmentLimit=0;
    stage.level=2;world.profiles[0].levels.push_back(stage);configure(planner,world);
    DevelopmentAction upgrade;upgrade.id=1;upgrade.buildingId=1;upgrade.buildingType=0;
    upgrade.type=UpgradeBuilding;upgrade.targetLevel=2;upgrade.state=ParcelReserved;
    planner.actionMap[1]=upgrade;
    CHECK(planner.evaluateFoodLedger(world).consumer(1)->demand==stage.operatingMaterials[WHEAT]);
    CHECK(planner.evaluateFoodLedger(world).consumer(1)->productionDemand==stage.productionMaterials[WHEAT]);

    auto* bytes=new GAGCore::MemoryStreamBackend;GAGCore::BinaryOutputStream output(bytes);
    AIMaximaContinuation::Writer writer(&output,true);writer("world",world);
    const std::string saved(bytes->getBuffer(),bytes->getPosition());
    GAGCore::BinaryInputStream input(new GAGCore::MemoryStreamBackend(saved.data(),saved.size()));input.seekFromStart(0);
    WorldState restored;AIMaximaContinuation::Reader reader(&input,true);reader("world",restored);
    CHECK(restored.computeSignature()==world.computeSignature());
    CHECK(restored.buildings[0].plannedCarriers==0);CHECK(restored.buildings[0].productionRatios[WORKER]==0);
    Planner resumed;resumed.mutablePolicy()=planner.policy();configure(resumed,restored);resumed.actionMap[1]=upgrade;
    CHECK(resumed.evaluateFoodLedger(restored).consumer(1)->demand==planner.evaluateFoodLedger(world).consumer(1)->demand);
}

TEST_CASE("production observation keeps planned work despite temporarily absent carriers" * doctest::test_suite("Maxima.Economy"))
{
    glob2test::HeadlessGlobals globals;Fixture f;auto* producer=f.swarm(8,8,0);auto& ai=*f.ai;
    GameHeader header;header.setNumberOfPlayers(1);
    header.getBasePlayer(0)=BasePlayer(0,"operating plan test",0,BasePlayer::P_LOCAL);
    f.game.setGameHeader(header,true);
    ai.context.initialize();ai.ensure_strategy();int id=-1;
    for(const auto& entry:ai.context.get_building_register().found())
        if(ai.context.get_building_register().get_building(entry.first)==producer)id=entry.first;
    REQUIRE(id>=0);
    ai.staffing_control[id].request=8;ai.swarm_allowance[id]=5;
    Sint32 ratios[3]{3,0,1};auto order=std::make_shared<OrderModifySwarm>(producer->gid,ratios);
    order->sender=0;f.game.executeOrder(order,0);
    const auto world=ai.collect_development_world(ai.context);
    REQUIRE(world.building(id));CHECK(world.building(id)->plannedCarriers==5);
    CHECK(world.building(id)->productionRatios[WORKER]==3);CHECK(world.building(id)->productionRatios[EXPLORER]==0);
    CHECK(world.building(id)->productionRatios[WARRIOR]==1);CHECK(producer->unitsWorking.empty());
}

TEST_CASE("producer candidates enforce local feasibility alongside strategic target caps" * doctest::test_suite("Maxima.Economy"))
{
    using namespace AIMaximaPlacement;
    WorldState world;world.reset(32,32);
    BuildingProfile profile;profile.buildingType=0;
    for(int stage=1;stage<=3;++stage) {
        BuildingLevelProfile level;level.level=stage;level.roles=AIMaximaBuildings::roleBit(AIMaximaBuildings::Production);
        profile.levels.push_back(level);
    }
    world.profiles.push_back(profile);
    for(auto& tile:world.tiles)tile.discovered=true;
    for(auto& tile:world.tiles){tile.walkable=tile.buildable=tile.foodTraversable=true;tile.swimmable=false;}
    auto& producer=world.profiles[0];
    for(auto& level:producer.levels) {
        level.footprint=Footprint(0,0,1,1);level.initialCarriers=2;level.operatingAssignmentLimit=20;
        level.productionMaterials[1]=10000;level.operatingMaterials[1]=10000;
        level.productionRecipes.ticks[0]=100;level.productionRecipes.costs[0][1]=1;
    }
    world.tile(9,8).foodOpportunity=1;world.tile(9,8).protectedYield=1000;
    world.tile(18,8).foodOpportunity=1;world.tile(18,8).protectedYield=4000;
    WorldBuilding existing;existing.id=10;existing.buildingType=0;existing.level=1;
    existing.centerX=8;existing.centerY=8;existing.hp=existing.hpMax=100;existing.plannedCarriers=0;
    world.buildings.push_back(existing);
    Planner planner;planner.configure(world.profiles,1,2,6,5,7);
    planner.mutablePolicy().foodLedgerEnabled=true;planner.mutablePolicy().foodSupplyRadius=12;
    planner.mutablePolicy().carrierFixedTicksPerTrip=100;planner.mutablePolicy().carrierTicksPerTile=50;
    planner.configure(world.profiles,1,2,6,5,7);planner.adoptStartingBuildings(world);
    DevelopmentAction candidate;candidate.type=BuildStandalone;candidate.buildingType=0;candidate.targetLevel=1;
    candidate.centerX=8;candidate.centerY=8;candidate.initialFootprint=Footprint(0,0,1,1);
    RejectionReason reason=RejectedFoodCapacity;
    // Five thousand supply cannot cover peak ten thousand, but can cover this
    // explicitly staffed partial producer (2900 packets plus placement margin).
    CHECK(planner.foodCandidatePasses(world,nullptr,candidate,reason));
    CHECK(planner.foodCandidateDemand==2900);
    CHECK(planner.foodLocationQuality(world,candidate)>0);
    auto* backend=new GAGCore::MemoryStreamBackend;
    auto* output=new GAGCore::BinaryOutputStream(backend);planner.save(output);backend->seekFromStart(0);
    auto* inputBackend=new GAGCore::MemoryStreamBackend(*backend);delete output;
    GAGCore::BinaryInputStream input(inputBackend);Planner restored;
    restored.mutablePolicy()=planner.policy();restored.configure(world.profiles,1,2,6,5,7);
    REQUIRE(restored.load(&input,VERSION_MINOR));
    CHECK_FALSE(restored.foodQueryValid);
    CHECK(restored.foodCandidatePasses(world,nullptr,candidate,reason));
    CHECK(restored.foodCandidateDemand==planner.foodCandidateDemand);
    world.tile(9,8).protectedYield=world.tile(18,8).protectedYield=0;
    planner.evaluateFoodLedger(world);
    CHECK_FALSE(planner.foodCandidatePasses(world,nullptr,candidate,reason));CHECK(reason==RejectedFoodCapacity);
    // A previously selected upgrade must recheck food at the issue boundary.
    DevelopmentAction upgrade=candidate;upgrade.type=UpgradeBuilding;upgrade.buildingId=10;
    upgrade.fromLevel=1;upgrade.targetLevel=2;
    CHECK_FALSE(planner.revalidate(world,upgrade,&reason,true));CHECK(reason==RejectedFoodCapacity);
}

TEST_CASE("feeding candidates transfer funded meal shares through cache reload and replacement" * doctest::test_suite("Maxima.Economy"))
{
    using namespace AIMaximaPlacement;
    WorldState world;world.reset(32,32);world.feedingColonies.push_back({8,8,{8000,0,0}});
    for(auto& tile:world.tiles)tile.discovered=tile.walkable=tile.buildable=tile.foodTraversable=true;
    world.tile(8,8).protectedYield=10000;world.tile(8,8).foodOpportunity=1;
    for(int type=0;type<2;++type) {
        BuildingProfile profile;profile.buildingType=type;
        for(int stage=1;stage<=2;++stage) {
            BuildingLevelProfile level;level.level=stage;level.footprint=Footprint(0,0,1,1);
            level.initialCarriers=20;level.operatingAssignmentLimit=20;level.operatingMaterials[1]=8000;
            if(type==1) {
                level.roles=AIMaximaBuildings::roleBit(AIMaximaBuildings::Feeding);
                level.feedingRate=8000;level.feedingMask=1;level.feedingMaterials[1]=8000;
            } else {
                level.roles=AIMaximaBuildings::roleBit(AIMaximaBuildings::Production);
                level.productionMaterials[1]=8000;level.productionUnitMask=1;
                level.productionRecipes.ticks[0]=125;level.productionRecipes.costs[0][1]=1;
            }
            profile.levels.push_back(level);
        }
        world.profiles.push_back(profile);
        WorldBuilding building;building.id=type+1;building.buildingType=type;building.level=1;
        building.centerX=type?7:9;building.centerY=8;building.hp=building.hpMax=100;
        building.plannedCarriers=20;building.productionRatios[0]=type?0:1;world.buildings.push_back(building);
    }
    Planner planner;planner.mutablePolicy().foodLedgerEnabled=true;planner.mutablePolicy().foodMarginPercent=150;
    planner.configure(world.profiles,1,2,6,5,7,0);planner.adoptStartingBuildings(world);
    DevelopmentAction action;action.type=BuildStandalone;action.buildingType=1;action.targetLevel=1;
    action.centerX=8;action.centerY=7;action.initialFootprint=Footprint(0,0,1,1);
    RejectionReason reason=RejectedFoodCapacity;
    REQUIRE(planner.foodCandidatePasses(world,nullptr,action,reason));
    CHECK(planner.foodCandidateDemand==4000);CHECK(planner.foodQuery.transferred==4000);
    const auto query=planner.foodQuery;CHECK(planner.foodLocationQuality(world,action)>0);
    CHECK(planner.foodQuery.transferred==query.transferred); // same cached query used for scoring
    auto* backend=new GAGCore::MemoryStreamBackend;auto* output=new GAGCore::BinaryOutputStream(backend);
    planner.save(output);backend->seekFromStart(0);auto* inputBackend=new GAGCore::MemoryStreamBackend(*backend);delete output;
    GAGCore::BinaryInputStream input(inputBackend);Planner restored;restored.mutablePolicy()=planner.policy();
    restored.configure(world.profiles,1,2,6,5,7,0);REQUIRE(restored.load(&input,VERSION_MINOR));
    CHECK_FALSE(restored.foodQueryValid);REQUIRE(restored.foodCandidatePasses(world,nullptr,action,reason));
    CHECK(restored.foodQuery.transferred==4000);CHECK(restored.foodQuery.residual==query.residual);
    // Replacement compares with the funded pre-exclusion baseline, transferring
    // the old provider's existing eight meals once instead of charging a new margin.
    action.type=UpgradeBuilding;action.buildingId=2;action.fromLevel=1;action.targetLevel=2;
    action.centerX=7;action.centerY=8;
    CHECK(restored.foodUpgradePasses(world,action,reason));CHECK(restored.foodQuery.transferred==8000);
    // Lost supply invalidates both the reservation reference and cached query.
    world.tile(8,8).protectedYield=0;restored.evaluateFoodLedger(world);
    CHECK_FALSE(restored.foodUpgradePasses(world,action,reason));CHECK(restored.foodQuery.transferred==0);
}

TEST_CASE("Food reach crosses passable nonfood deposits but respects resource obstruction" * doctest::test_suite("Maxima.Economy"))
{
 using namespace AIMaximaPlacement;
 WorldState world; world.reset(16,16);
 for(auto& tile:world.tiles) { tile.discovered=true; tile.walkable=false; }
 for(int x=3;x<=8;++x) world.tile(x,4).walkable=true;
 auto& crossing=world.tile(5,4);
 crossing.materialType=materialIndex(MaterialId::Fabric);
 crossing.materialSources=materialBit(MaterialId::Fabric);
 auto& food=world.tile(8,4); food.foodOpportunity=65536;
 food.materialType=materialIndex(MaterialId::Food);
 food.materialSources=materialBit(MaterialId::Food);
 Planner planner;
 auto reached=planner.colonyFoodTiles(world,2,4,Footprint(0,0,1,1));
 REQUIRE(std::find(reached.begin(),reached.end(),world.index(8,4))!=reached.end());
 const auto passableSignature=world.computeSignature();
 crossing.resourceBlocksGround=true;
 REQUIRE(world.computeSignature()!=passableSignature);
 reached=planner.colonyFoodTiles(world,2,4,Footprint(0,0,1,1));
 REQUIRE(reached.empty());
}

TEST_CASE("custom construction supply counts reachable mixed yields and shore access" * doctest::test_suite("Maxima.Economy"))
{
    glob2test::HeadlessGlobals globals;
    Fixture f;
    auto catalog=nlohmann::json::parse(f.game.buildingsTypes.snapshotJson());
    const int site=f.game.buildingsTypes.getTypeNum("inn",0,true);
    REQUIRE(site>=0);
    catalog["variants"][site]["properties"]["maxMaterial"][materialIndex(MaterialId::Gold)]=10;
    catalog["variants"][site]["properties"]["maxMaterial"][materialIndex(MaterialId::Paper)]=10;
    catalog["variants"][site]["semantics"]["constructionCost"]={{"gold",5},{"paper",5}};
    f.game.buildingsTypes.loadSnapshotJson(catalog.dump());f.game.configureBuildingCatalog();
    auto resources=nlohmann::json::parse(f.game.map.resourceRegistry().serialize());
    auto custom=resources["resources"][0];
    custom["key"]="test:mixed-shore-stock";
    custom.erase("requiredExperiment");
    custom["properties"]={{"ecology","uniform"},{"habitatMask",3},
        {"blocksGround",false},{"primaryMaterial","gold"},{"persistsWhenEmpty",true},{"visibleToHarvest",true}};
    custom["yields"]={{"gold",{{"capacity",7},{"initial",7},{"consumption","all"}}},
        {"paper",{{"capacity",3},{"initial",3},{"consumption","one"}}},
        {"food",{{"capacity",4},{"initial",4},{"consumption","one"}}}};
    resources["resources"].push_back(custom);
    f.game.map.installResourceDefinitions(resources.dump());
    const auto resource=f.game.map.resourceRegistry().find("test:mixed-shore-stock");
    REQUIRE(resource.has_value());
    auto* worker=f.game.addUnit(20,20,0,WORKER,0,0,0,0);REQUIRE(worker);
    f.game.map.setCellTerrain(21,20,WATER);
    f.game.map.setResource(21,20,*resource,0);
    for(int dy=-1;dy<=1;++dy)for(int dx=-1;dx<=1;++dx)
        if(dx||dy)f.game.map.setCellTerrain(40+dx,40+dy,WATER);
    f.game.map.setResource(40,40,*resource,0);
    f.game.map.fogOfWar[f.game.map.coordToIndex(21,20)]|=f.player.team->me;
    f.game.map.fogOfWar[f.game.map.coordToIndex(40,40)]|=f.player.team->me;
    f.ai->ensure_strategy();
    auto world=f.ai->collect_development_world(f.ai->context);
    CHECK(world.accessibleSupplies[materialIndex(MaterialId::Gold)]==1);
    CHECK(world.accessibleSupplies[materialIndex(MaterialId::Paper)]==3);
    CHECK((world.tile(21,20).sources()&materialBit(MaterialId::Paper))!=0);
    CHECK((world.tile(40,40).sources()&materialBit(MaterialId::Gold))==0);
    CHECK((world.tile(40,40).sources()&materialBit(MaterialId::Paper))==0);
    worker->performance[SWIM]=1;
    world=f.ai->collect_development_world(f.ai->context);
    CHECK(world.accessibleSupplies[materialIndex(MaterialId::Gold)]==2);
    CHECK(world.accessibleSupplies[materialIndex(MaterialId::Paper)]==6);
    CHECK((world.tile(40,40).sources()&materialBit(MaterialId::Gold))!=0);
    // Discovery is not current visibility. A hidden deposit cannot fund any
    // material, including the food path that bypasses additional-input counts.
    f.game.map.fogOfWar[f.game.map.coordToIndex(21,20)]&=~f.player.team->me;
    f.game.map.fogOfWar[f.game.map.coordToIndex(40,40)]&=~f.player.team->me;
    world=f.ai->collect_development_world(f.ai->context);
    CHECK(world.accessibleSupplies[materialIndex(MaterialId::Gold)]==0);
    CHECK(world.accessibleSupplies[materialIndex(MaterialId::Paper)]==0);
    CHECK(world.tile(21,20).sources()==0);
    CHECK(world.tile(21,20).materialAmount==0);
    CHECK(world.tile(21,20).foodOpportunity==0);
    CHECK(world.tile(21,20).clearableResource);
    f.game.map.fogOfWar[f.game.map.coordToIndex(21,20)]|=f.player.team->me;
    world=f.ai->collect_development_world(f.ai->context);
    CHECK(world.accessibleSupplies[materialIndex(MaterialId::Gold)]==1);
    CHECK(world.accessibleSupplies[materialIndex(MaterialId::Paper)]==3);
    CHECK((world.tile(21,20).sources()&materialBit(MaterialId::Food))!=0);
    CHECK(world.tile(21,20).materialAmount==4);
}
