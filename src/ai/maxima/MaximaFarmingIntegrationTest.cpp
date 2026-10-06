// Link with the game objects (excluding Glob2.cpp) to exercise the real runtime.
#include "EngineFixtures.h"
#include "ExperimentalFeatures.h"
#include <algorithm>
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
#include <GzipUtil.h>
#include <StreamBackend.h>
#include <iostream>
#include <memory>

using namespace AIMaximaRuntime;


namespace
{
void setYoungResource(Map& map, int x, int y, int type)
{
    map.setResourceByIndex(x,y,type,1);
    map.setResourceAmount(map.coordToIndex(x,y), 1);
}

struct Fixture
{
    Game game;
    Player player;
    std::unique_ptr<AIMaxima::Maxima> ai;
    Fixture(int team=0): game(NULL)
    {
        game.map.setSize(6,6,GRASS);game.map.setGame(&game);
        for(int i=0;i<=team;++i){game.addTeam();game.teams[i]->race.loadDefault();}
        player.setTeam(game.teams[team]);
        for(int y=0;y<64;++y)for(int x=0;x<64;++x)
            game.map.setMapDiscovered(x,y,player.team->me);
        ai.reset(new AIMaxima::Maxima(&player));
        ai->context.initialize();
    }
};

// The management radius gates protection by distance from a physical
// building. Both probed cells are on the expansion lattice, so only the
// radius, not the lattice, decides the result.
void farmManagementRadius()
{
    Fixture f;auto& ai=*f.ai;Map& map=f.game.map;
    for(int y=0;y<64;++y)map.setCellTerrain(10,y,WATER);
    auto* home=f.game.addBuilding(11,1,
        globalContainer->buildingsTypes.getTypeNum("inn",0,false),0);
    REQUIRE(home);
    setYoungResource(map,11,21,WHEAT);
    setYoungResource(map,11,23,WHEAT);
    ai.budget.farming_enabled=true;ai.budget.farming_protection_enabled=true;
    ai.budget.farming_management_radius=20;
    ai.budget.farming_wheat_fertility_min=3276;
    ai.update_farming(ai.context);
    REQUIRE(ai.farm_protection_mask[21*64+11]); // inclusive boundary
    REQUIRE(!ai.farm_protection_mask[23*64+11]);
    // A virtual building cannot anchor a farm, and loss of infrastructure
    // must not expose the already-established seed on the boundary.
    auto* originalType=home->type;
    home->type=globalContainer->buildingsTypes.getByType("warflag",0,false);
    ai.update_farming(ai.context);
    REQUIRE(ai.farm_protection_mask[21*64+11]);
    REQUIRE(!ai.farm_protection_mask[23*64+11]);
    ai.budget.farming_management_radius=0; // optimizer's unlimited control
    ai.update_farming(ai.context);
    REQUIRE(ai.farm_protection_mask[23*64+11]);
    home->type=originalType;
}

void coastalWheatCrossesFertilityDips()
{
    for(int resource:{WHEAT,WOOD})
    {
        Fixture f;auto& ai=*f.ai;Map& map=f.game.map;
        for(int y=0;y<64;++y)map.setCellTerrain(10,y,WATER);
        map.setResourceByIndex(11,20,resource,5);
        ai.budget.farming_enabled=true;ai.budget.farming_protection_enabled=true;
        ai.budget.farming_wheat_fertility_min=65536;
        ai.budget.farming_minimum_wood_fertility=65536;
        ai.budget.farming_wood_firebreak_enabled=false;
        for(bool live:{false,true})
        {
            if(live)setYoungResource(map,11,21,resource);
            ai.update_farming(ai.context);
            REQUIRE(ai.fertility_cache.at(11,21)<65536);
            REQUIRE(bool(ai.farm_protection_mask[21*64+11])==(resource==WHEAT));
            // Neither inland expansion nor distant empty coastline inherits
            // the exception, even though both are valid growing terrain.
            REQUIRE(!ai.farm_protection_mask[20*64+12]);
            REQUIRE(!ai.farm_protection_mask[30*64+11]);
        }
    }
}

void seedSurvival()
{
    for(int resource:{WHEAT,WOOD}) for(int offset:{0,44})
    {
        Fixture f;auto& ai=*f.ai;Map& map=f.game.map;
        auto index=[&](int x,int y){return map.normalizeY(y+offset)*64+map.normalizeX(x+offset);};
        for(int y=0;y<64;++y)for(int x=0;x<12;++x)
            map.setCellTerrain(x+offset,y,WATER);
        const int first=index(20,20),second=index(21,21),growth=index(22,20);
        setYoungResource(map,first%64,first/64,resource);
        setYoungResource(map,second%64,second/64,resource);
        ai.budget.farming_enabled=true;
        ai.budget.farming_protection_enabled=true;
        ai.budget.farming_wheat_fertility_min=3276;
        ai.budget.farming_minimum_wood_fertility=3276;
        ai.budget.farming_wood_firebreak_enabled=false;
        ai.update_farming(ai.context);
        REQUIRE((ai.farm_protection_mask[first] && ai.farm_protection_mask[second]));
        REQUIRE(ai.farm_protection_mask[first]);
        REQUIRE(bool(ai.wheat_farm_protection_mask[first])==(resource==WHEAT));
        // Boundary growth keeps both the existing lattice seed and aligned edge.
        setYoungResource(map,growth%64,growth/64,resource);
        ai.update_farming(ai.context);
        REQUIRE(ai.farm_protection_mask[growth]);
        REQUIRE(ai.farm_protection_mask[first]);
        // A lone odd/odd seed retains normal protection while it can expand.
        map.setNoResource(first%64,first/64,0);
        map.setNoResource(growth%64,growth/64,0);
        ai.update_farming(ai.context);
        REQUIRE(ai.farm_protection_mask[second]);
        if(resource==WOOD)
        {
            ai.budget.farming_wood_firebreak_enabled=true;
            ai.wood_firebreak_mask[second]=1;
            ai.update_farming(ai.context);
            REQUIRE(!ai.farm_protection_mask[second]);
        }
    }
}

std::shared_ptr<OrderCreate> createOrder(Context& c)
{
    c.update_building_orders();
    for(auto order:c.orders)
        if(auto create=std::dynamic_pointer_cast<OrderCreate>(order))return create;
    REQUIRE(false);return {};
}

void initialFlagStaffing()
{
    for(int workers:{0,3})
    {
        Fixture f;Context& c=f.ai->context;
        auto* request=new Construction::BuildingOrder(IntBuildingType::CLEARING_FLAG,workers);
        request->add_constraint(new Construction::SinglePosition(20,20));
        c.add_building_order(request);
        const auto create=createOrder(c);
        REQUIRE((create->unitWorking==workers && create->unitWorkingFuture==workers));
        Building* flag=f.game.addBuilding(create->posX,create->posY,create->typeNum,0,
            create->unitWorking,create->unitWorkingFuture);
        REQUIRE((flag && flag->maxUnitWorking==workers && flag->desiredMaxUnitWorking==workers));
        if(workers==0)
        {
            Unit* worker=f.game.addUnit(21,20,0,WORKER,0,0,0,0);REQUIRE(worker);
            setYoungResource(f.game.map,22,20,WHEAT);
            f.game.map.buildingGradient(flag,0);
            // Even if discovery/management is delayed past a recruitment cycle,
            // the default broad resource selector cannot recruit a worker.
            for(int tick=0;tick<70;++tick)REQUIRE(!flag->subscribeForFlagingStep());
            REQUIRE(flag->unitsWorking.empty());
        }
    }
}

void applyFlagOrders(Context& c,Building* flag)
{
    c.update_management_orders();
    for(auto order:c.orders)
    {
        if(auto size=std::dynamic_pointer_cast<OrderModifyFlag>(order))
            if(size->gid==flag->gid)flag->unitStayRange=size->range;
        if(auto move=std::dynamic_pointer_cast<OrderMoveFlag>(order))
            if(move->gid==flag->gid){flag->posX=move->x;flag->posY=move->y;}
        if(auto selector=std::dynamic_pointer_cast<OrderModifyClearingFlag>(order))
            if(selector->gid==flag->gid)
                for(int r=0;r<BASIC_COUNT;++r)flag->clearingMaterials[r]=selector->clearingMaterials[r];
    }
    c.orders.clear();
}

}


// Apply area contracts at the scheduler boundary without simulating workers.
// Count changed cells so repeated passes must prove order-level idempotence.
int applyAreaContracts(Fixture& f)
{
    int changes=0;
    Map& map=f.game.map;
    for(const auto& order:f.ai->context.managementOrders)
    {
        if(auto add=std::dynamic_pointer_cast<Management::AddArea>(order))
            for(const auto& cell:add->locations)
            {
                if(add->areaType==ForbiddenArea)
                {
                    changes+=!map.isForbidden(cell.x,cell.y,f.player.team->me);
                    map.addForbidden(cell.x,cell.y,f.player.team->teamNumber);
                }
                if(add->areaType==ClearingArea)
                {
                    changes+=!map.isClearArea(cell.x,cell.y,f.player.team->me);
                    map.addClearArea(cell.x,cell.y,f.player.team->teamNumber);
                }
            }
        if(auto remove=std::dynamic_pointer_cast<Management::RemoveArea>(order))
            for(const auto& cell:remove->locations)
            {
                if(remove->areaType==ForbiddenArea)
                {
                    changes+=map.isForbidden(cell.x,cell.y,f.player.team->me);
                    map.removeForbidden(cell.x,cell.y,f.player.team->teamNumber);
                }
                if(remove->areaType==ClearingArea)
                {
                    changes+=map.isClearArea(cell.x,cell.y,f.player.team->me);
                    map.setAreaMask(map.coordToIndex(cell.x,cell.y), &Tile::clearArea, map.getTile(cell.x,cell.y).clearArea & (~f.player.team->me));
                }
            }
    }
    f.ai->context.managementOrders.clear();
    return changes;
}

void configurePattern(Fixture& f)
{
    auto& b=f.ai->budget;
    b.farming_enabled=true;
    b.farming_protection_enabled=true;
    b.farming_wheat_fertility_min=3276;
    b.farming_minimum_wood_fertility=3276;
    b.farming_wood_firebreak_enabled=false;
}

// Only selected cells permit new growth, isolating the target and its harvest
// lane from unrelated frontier targets. Extra crops provide a food reserve.
void expansionLeavesHarvestLanesOpen()
{
    for(int offset:{0,44}) for(bool live:{false,true})
    {
        Fixture f;auto& ai=*f.ai;Map& map=f.game.map;
        auto index=[&](int x,int y){return map.normalizeY(y+offset)*64+map.normalizeX(x+offset);};
        for(int y=0;y<64;++y)for(int x=0;x<64;++x)
            map.setResourcesGrow(x,y, 0);
        for(int y=0;y<64;++y)for(int x=0;x<12;++x)
        {int i=index(x,y);map.setCellTerrain(i%64,i/64,WATER);}
        const int seed=index(19,19),support=index(20,19);
        const int target=index(20,20),other=index(20,18);
        // The donor sits on a narrow shoreline; access remains on the east.
        for(int y:{18,20}){int i=index(19,y);map.setCellTerrain(i%64,i/64,WATER);}
        setYoungResource(map,seed%64,seed/64,WHEAT);
        for(int y:{25,27,29,31})
        {
            int i=index(21,y);setYoungResource(map,i%64,i/64,WHEAT);
            i=index(22,y);setYoungResource(map,i%64,i/64,WHEAT);
        }
        for(int i:{seed,support,target,other})map.setResourcesGrow(i%64,i/64, 1);
        if(live)setYoungResource(map,support%64,support/64,WHEAT);
        configurePattern(f);
        ai.update_farming(ai.context);applyAreaContracts(f);
        REQUIRE((ai.farm_protection_mask[target] && ai.farm_protection_mask[other]));
        REQUIRE(!ai.farm_protection_mask[support]);
        REQUIRE(ai.farm_protection_mask[seed]);
        // Growth and depletion must not reserve the off-lattice harvest lane.
        for(bool established:{true,false})
        {
            setYoungResource(map,support%64,support/64,WHEAT);
            if(established)setYoungResource(map,target%64,target/64,WHEAT);
            else map.setNoResource(target%64,target/64,0);
            ai.update_farming(ai.context);applyAreaContracts(f);
            REQUIRE(!ai.farm_protection_mask[support]);
            REQUIRE(!map.isForbidden(support%64,support/64,f.player.team->me));
            REQUIRE(ai.farm_protection_mask[seed]);
        }
        const auto protectedMask=ai.farm_protection_mask;
        ai.applied_farm_protection_mask.clear();
        ai.update_farming(ai.context);REQUIRE(applyAreaContracts(f)==0);
        REQUIRE(ai.farm_protection_mask==protectedMask);
        // Loading an older plan releases its obsolete neighboring support.
        map.addForbidden(support%64,support/64,0);
        ai.applied_farm_protection_mask.clear();
        ai.update_farming(ai.context);applyAreaContracts(f);
        REQUIRE(!map.isForbidden(support%64,support/64,f.player.team->me));
        REQUIRE(ai.farm_protection_mask==protectedMask);
    }
}

void woodReserveSurvivesWheatPincer()
{
    for(int offset:{0,44})
    {
        Fixture f;auto& ai=*f.ai;Map& map=f.game.map;
        auto index=[&](int x,int y){return map.normalizeY(y+offset)*64+map.normalizeX(x+offset);};
        for(int i=0;i<64*64;++i)map.setResourcesGrow(i%64,i/64, 0);
        for(int y=0;y<64;++y)for(int x=0;x<12;++x)
        {int i=index(x,y);map.setCellTerrain(i%64,i/64,WATER);}
        const int seed=index(20,20),outlet=index(20,19),worker=index(21,18);
        for(int y=19;y<=21;++y)for(int x=19;x<=21;++x)
        {int i=index(x,y);setYoungResource(map,i%64,i/64,WHEAT);}
        map.setNoResource(seed%64,seed/64,0);
        setYoungResource(map,seed%64,seed/64,WOOD);
        map.setNoResource(outlet%64,outlet/64,0);
        for(int i:{seed,outlet})map.setResourcesGrow(i%64,i/64, 1);
        REQUIRE(f.game.addUnit(worker%64,worker/64,0,WORKER,0,0,0,0));
        configurePattern(f);
        ai.budget.farming_minimum_wood_fertility=65536;
        ai.budget.farming_maintenance_clearing_enabled=true;
        ai.budget.farming_wheat_invasion_clearing_enabled=true;
        ai.budget.farming_wood_firebreak_enabled=true;
        ai.strategy.farming.wood_firebreak_fertility_min_percent=0;
        ai.strategy.farming.wood_firebreak_fertility_max_percent=100;
        ai.initialize_farming_cache(ai.context);
        auto openReserve=ai.select_wood_reserve(ai.context);
        REQUIRE((openReserve.cells[seed]==1 && openReserve.cells[outlet]==2));
        map.addForbidden(outlet%64,outlet/64,f.player.team->teamNumber);
        auto blockedOutlet=ai.select_wood_reserve(ai.context);
        REQUIRE(blockedOutlet.cells[outlet]!=2);
        map.removeForbidden(outlet%64,outlet/64,f.player.team->teamNumber);
        auto update=[&]() {
            ai.update_farming(ai.context);applyAreaContracts(f);
            ai.update_maintenance_clearing_areas(ai.context);applyAreaContracts(f);
        };
        for(int cycle=0;cycle<4;++cycle)
        {
            update();
            auto reserve=ai.select_wood_reserve(ai.context);
            REQUIRE((reserve.cells[seed]==1 && reserve.cells[outlet]==2));
            REQUIRE(map.isForbidden(seed%64,seed/64,f.player.team->me));
            REQUIRE(!map.isForbidden(outlet%64,outlet/64,f.player.team->me));
            REQUIRE(!map.isClearArea(seed%64,seed/64,f.player.team->me));
            // The seed can keep producing harvestable wood despite wheat on
            // either flank and an otherwise universal wood firebreak.
            REQUIRE(map.incResourceByIndex(outlet%64,outlet/64,WOOD,0));
            update();
            REQUIRE(!map.isClearArea(outlet%64,outlet/64,f.player.team->me));
            REQUIRE(!map.isForbidden(outlet%64,outlet/64,f.player.team->me));
            while(map.isMaterialTakeableSlot(outlet%64,outlet/64,WOOD))
                map.decResource(outlet%64,outlet/64);
        }
        // Competing wheat in the outlet remains harvestable, never a new seed.
        REQUIRE(map.incResourceByIndex(outlet%64,outlet/64,WHEAT,0));
        update();REQUIRE(!ai.wheat_farm_protection_mask[outlet]);
        const auto before=ai.select_wood_reserve(ai.context).cells;
        ai.applied_farm_protection_mask.clear();
        update();REQUIRE(ai.select_wood_reserve(ai.context).cells==before);
        const auto world=ai.collect_development_world(ai.context);
        REQUIRE((world.tiles[seed].woodReserve && world.tiles[outlet].woodReserve));
        REQUIRE(world.tiles[outlet].clearableResource); // harvestable is not buildable
        // Exact committed building/access reservations retain priority.
        ai.development_planner.footprintRefs.assign(64*64,0);
        ai.development_planner.footprintRefs[seed]=1;
        update();REQUIRE(!ai.farm_protection_mask[seed]);
        REQUIRE(ai.select_wood_reserve(ai.context).seeds==0);
        ai.development_planner.footprintRefs[seed]=0;
        ai.budget.farming_management_radius=1; // no physical home nearby
        update();REQUIRE(ai.select_wood_reserve(ai.context).seeds==0);
        ai.budget.farming_management_radius=0;
        ai.budget.farming_protection_enabled=false;
        update();REQUIRE(ai.select_wood_reserve(ai.context).seeds==0);
        ai.budget.farming_protection_enabled=true;
        // More eligible wood does not expand the reserve beyond two donors.
        for(int x:{24,26})
        {
            int i=index(x,x);setYoungResource(map,i%64,i/64,WOOD);
            map.setResourcesGrow(i%64,i/64, 1);
            i=index(x,x-1);map.setResourcesGrow(i%64,i/64, 1);
        }
        update();
        auto stable=ai.select_wood_reserve(ai.context);
        REQUIRE(stable.seeds==2);
        for(int cycle=0;cycle<3;++cycle)
        {update();REQUIRE(ai.select_wood_reserve(ai.context).cells==stable.cells);}
        map.setNoResource(seed%64,seed/64,0);
        update();
        auto replacement=ai.select_wood_reserve(ai.context);
        REQUIRE((replacement.seeds==2 && !replacement.cells[seed]));
    }
}

void woodReserveSurvivesOwnFarmProtection()
{
    Fixture f;auto& ai=*f.ai;Map& map=f.game.map;
    for(int y=0;y<64;++y)for(int x=0;x<12;++x)map.setCellTerrain(x,y,WATER);
    const int seed=21*64+20;
    setYoungResource(map,seed%64,seed/64,WOOD);
    REQUIRE(f.game.addUnit(24,21,0,WORKER,0,0,0,0));
    configurePattern(f);
    ai.budget.farming_minimum_wood_fertility=1;
    ai.budget.farming_wood_firebreak_enabled=false;
    for(int cycle=0;cycle<4;++cycle)
    {
        ai.update_farming(ai.context);applyAreaContracts(f);
        auto reserve=ai.select_wood_reserve(ai.context);
        REQUIRE(reserve.seeds==1);
        REQUIRE(reserve.cells[seed]==1);
    }
}

void alignedPatternTransitions()
{
    for(int resource:{WHEAT,WOOD}) for(int offset:{0,42})
    {
        Fixture f;auto& ai=*f.ai;Map& map=f.game.map;
        auto index=[&](int x,int y){return map.normalizeY(y+offset)*64+map.normalizeX(x+offset);};
        for(int y=0;y<64;++y)for(int x=0;x<12;++x)
        {int i=index(x,y);map.setCellTerrain(i%64,i/64,WATER);}
        for(int y=18;y<=26;++y)for(int x=18;x<=26;++x)
        {int i=index(x,y);setYoungResource(map,i%64,i/64,resource);}
        configurePattern(f);
        const int seed=index(21,23),lane=index(21,22),hole=index(20,22);
        ai.update_farming(ai.context);applyAreaContracts(f);
        const auto filled=ai.farm_protection_mask;
        REQUIRE((filled[seed] && !filled[lane]));
        for(int repeat=0;repeat<3;++repeat)
        {
            map.setNoResource(hole%64,hole/64,0);
            ai.update_farming(ai.context);applyAreaContracts(f);
            REQUIRE((ai.farm_protection_mask[seed] && !ai.farm_protection_mask[lane]));
            ai.update_farming(ai.context);
            REQUIRE(applyAreaContracts(f)==0);
            setYoungResource(map,hole%64,hole/64,resource);
            ai.update_farming(ai.context);applyAreaContracts(f);
            REQUIRE(ai.farm_protection_mask==filled);
        }
    }
}

// Exercise the actual forbidden-area orders, quantity changes, both team
// masks and wrapped farms. A harvested interior hole must not create dense seeds.
void permanentWheatLayout()
{
    for(int team:{0,1}) for(int offset:{0,44})
    {
        Fixture f(team);auto& ai=*f.ai;Map& map=f.game.map;
        auto index=[&](int x,int y){return map.normalizeY(y+offset)*64+map.normalizeX(x+offset);};
        for(int y=0;y<64;++y)for(int x=0;x<64;++x)map.setResourcesGrow(x,y, 1);
        for(int y=0;y<64;++y)for(int x=0;x<12;++x)
        {int i=index(x,y);map.setCellTerrain(i%64,i/64,WATER);}
        for(int y=18;y<=26;++y)for(int x=18;x<=26;++x)
        {int i=index(x,y);setYoungResource(map,i%64,i/64,WHEAT);}
        configurePattern(f);
        const int seed=index(21,23),interior=index(20,22),edge=index(18,22),lane=index(21,22);
        bool first=true;
        for(int amount:{1,2,3,4,5,2})
        {
            for(int i:{seed,interior,edge,lane})
            {setYoungResource(map,i%64,i/64,WHEAT);map.setResourceAmount(map.coordToIndex(i%64,i/64), amount);}
            ai.update_farming(ai.context);
            const int changes=applyAreaContracts(f);
            if(!first)REQUIRE(changes==0);
            first=false;
            for(int i:{seed,edge})
            {
                REQUIRE(map.isForbidden(i%64,i/64,f.player.team->me));
            }
            for(int i:{interior,lane})REQUIRE(!map.isForbidden(i%64,i/64,f.player.team->me));
            ai.update_farming(ai.context);REQUIRE(applyAreaContracts(f)==0);
        }
        map.setNoResource(interior%64,interior/64,0);
        ai.update_farming(ai.context);applyAreaContracts(f);
        REQUIRE(!ai.farm_protection_mask[interior]);
        setYoungResource(map,interior%64,interior/64,WHEAT);
        ai.update_farming(ai.context);applyAreaContracts(f);
        REQUIRE(!ai.farm_protection_mask[interior]);
        // Selected seeds stay protected at maturity and after forced resource loss.
        setYoungResource(map,seed%64,seed/64,WHEAT);
        map.setResourceAmount(map.coordToIndex(seed%64,seed/64), 4);
        ai.update_farming(ai.context);applyAreaContracts(f);
        REQUIRE(map.isForbidden(seed%64,seed/64,f.player.team->me));
        map.setNoResource(seed%64,seed/64,0);
        ai.update_farming(ai.context);applyAreaContracts(f);
        REQUIRE(map.isForbidden(seed%64,seed/64,f.player.team->me));
        setYoungResource(map,seed%64,seed/64,WHEAT);
        ai.update_farming(ai.context);applyAreaContracts(f);
        REQUIRE(map.isForbidden(seed%64,seed/64,f.player.team->me));
    }
}

void firebreakManagementRadius()
{
    Fixture f;auto& ai=*f.ai;Map& map=f.game.map;
    auto* home=f.game.addBuilding(11,1,
        globalContainer->buildingsTypes.getTypeNum("inn",0,false),0);
    REQUIRE(home);
    for(int y:{21,22,63})map.setResourceByIndex(11,y,WOOD,5);
    ai.budget.farming_enabled=true;
    ai.budget.farming_maintenance_clearing_enabled=true;
    ai.budget.farming_wood_firebreak_enabled=true;
    ai.strategy.farming.wood_firebreak_fertility_min_percent=0;
    ai.strategy.farming.wood_firebreak_fertility_max_percent=100;
    auto update=[&](){ai.update_maintenance_clearing_areas(ai.context);applyAreaContracts(f);};
    ai.budget.farming_management_radius=0;
    update();REQUIRE(map.isClearArea(11,22,f.player.team->me));
    ai.budget.farming_management_radius=20;
    update();
    REQUIRE(map.isClearArea(11,21,f.player.team->me));
    REQUIRE(!map.isClearArea(11,22,f.player.team->me)); // old clearing removed
    REQUIRE(map.isClearArea(11,63,f.player.team->me)); // wrapped distance
    auto* original=home->type;
    home->type=globalContainer->buildingsTypes.getByType("warflag",0,false);
    update();REQUIRE(!map.isClearArea(11,21,f.player.team->me));
    home->type=original;
}

void maintenanceProtectionAgreement()
{
    for(int offset:{0,44})
    {
        Fixture f;auto& ai=*f.ai;Map& map=f.game.map;
        auto index=[&](int x,int y){return map.normalizeY(y+offset)*64+map.normalizeX(x+offset);};
        for(int y=0;y<64;++y)for(int x=0;x<12;++x)
        {int i=index(x,y);map.setCellTerrain(i%64,i/64,WATER);}
        const int wheat=index(18,21),wood=index(18,22);
        setYoungResource(map,wheat%64,wheat/64,WHEAT);
        setYoungResource(map,wood%64,wood/64,WOOD);
        configurePattern(f);
        ai.budget.farming_maintenance_clearing_enabled=true;
        ai.budget.farming_wheat_invasion_clearing_enabled=true;
        for(int cycle=0;cycle<3;++cycle)
        {
            ai.update_farming(ai.context);
            int changes=applyAreaContracts(f);
            REQUIRE(ai.wheat_farm_protection_mask[wheat]);
            REQUIRE(!ai.farm_protection_mask[wood]);
            ai.update_maintenance_clearing_areas(ai.context);
            changes+=applyAreaContracts(f);
            REQUIRE(ai.maintenance_circulation_mask[wood]);
            REQUIRE(!map.isForbidden(wood%64,wood/64,f.player.team->me));
            if(cycle)REQUIRE(changes==0);
        }
        // Disabling the sub-policy releases its clearing obligation immediately.
        ai.budget.farming_wheat_invasion_clearing_enabled=false;
        ai.update_farming(ai.context);applyAreaContracts(f);
        ai.update_maintenance_clearing_areas(ai.context);applyAreaContracts(f);
        REQUIRE(ai.farm_protection_mask[wood]);
        REQUIRE(!ai.maintenance_circulation_mask[wood]);
        // Removing wheat must also revoke it; stale maintenance cannot latch it.
        ai.budget.farming_wheat_invasion_clearing_enabled=true;
        map.setNoResource(wheat%64,wheat/64,0);
        ai.update_farming(ai.context);applyAreaContracts(f);
        ai.update_maintenance_clearing_areas(ai.context);applyAreaContracts(f);
        REQUIRE(ai.farm_protection_mask[wood]);
        REQUIRE(!ai.maintenance_circulation_mask[wood]);
    }
}

void archipelagoHarvestDoesNotSealWheat()
{
    Game game(NULL);
    BinaryInputStream input(GAGCore::openInflatingFileStreamBackend((glob2test::sourceRoot() / "maps/Archipelago.map.gz").string()));
    REQUIRE(game.load(&input));
    Player player;player.setTeam(game.teams[0]);
    AIMaxima::Maxima ai(&player);ai.context.initialize();
    ai.budget.farming_enabled=true;ai.budget.farming_protection_enabled=true;
    ai.budget.farming_wheat_fertility_min=3276;
    for(int round=0;round<8;++round) {
        if(round%2)game.map.setNoResource(48,97,1);
        else setYoungResource(game.map,48,97,WHEAT);
        ai.update_farming(ai.context);
        for(int patch=0;patch<2;++patch) {
            int wheat=0,open=0;
            for(int y=patch?103:95;y<=(patch?106:99);++y)
                for(int x=patch?55:48;x<=(patch?61:53);++x)
                    if(game.map.isMaterialTakeableSlot(x,y,WHEAT)) {
                        ++wheat;open+=!ai.farm_protection_mask[y*128+x];
                    }
            REQUIRE((wheat>0 && open>0));
        }
    }
}

void seedStabilityAcrossMaps()
{
    const char* maps[]={"Holiday_Island_2","Archipelago","Isles","Migration",
        "Garden_3","A_big_pond","Wild_River","Sand_River"};
    for(const char* name:maps) {
        Game game(NULL);std::string path=(glob2test::sourceRoot() / ("maps/"+std::string(name)+".map.gz")).string();
        BinaryInputStream input(GAGCore::openInflatingFileStreamBackend(path));
        REQUIRE(game.load(&input));
        Player player;player.setTeam(game.teams[0]);
        AIMaxima::Maxima ai(&player);ai.context.initialize();
        ai.budget.farming_enabled=true;ai.budget.farming_protection_enabled=true;
        ai.budget.farming_wheat_fertility_min=3276;
        ai.budget.farming_economic_envelope_radius=20;
        const int w=game.map.getW(),h=game.map.getH();
        // Selected cells survive neighboring harvests at every resource amount.
        ai.update_farming(ai.context);
        std::vector<int> seeds;
        for(int y=1;y<h;y+=2)for(int x=1;x<w;x+=2)
            if(game.map.isMaterialTakeableSlot(x,y,WHEAT)&&ai.farm_protection_mask[y*w+x])
                seeds.push_back(y*w+x);
        REQUIRE(!seeds.empty());
        // Adversarial harvest: remove every available wheat tile each round.
        // Grow new neighbors between rounds to change patch boundaries and
        // exercise the exact feedback loop that destroyed Holiday's left farm.
        for(int round=0;round<12;++round) {
            for(int y=0;y<h;++y)for(int x=0;x<w;++x)
                if(game.map.isMaterialTakeableSlot(x,y,WHEAT)&&!ai.farm_protection_mask[y*w+x])
                    game.map.setNoResource(x,y,1);
            for(int seed:seeds) {
                int x=game.map.normalizeX(seed%w+(round%3)-1);
                int y=game.map.normalizeY(seed/w+((round/3)%3)-1);
                const Tile& c=game.map.getTile(x,y);
                if((game.map.terrainPropertiesAt(x,y).allowedResources & (1u<<WHEAT))&&c.building==NOGBID&&c.resource.type==NO_RES_TYPE)
                    setYoungResource(game.map,x,y,WHEAT);
            }
            ai.timer+=64;ai.update_farming(ai.context);
            for(int seed:seeds) {
                REQUIRE(game.map.isMaterialTakeableSlot(seed%w,seed/w,WHEAT));
                REQUIRE(ai.farm_protection_mask[seed]);
            }
        }
        std::cout<<"stable seed cycles: "<<name<<" seeds="<<seeds.size()<<" rounds=12\n";
    }
}

void farmingRespectsDiscovery()
{
    Game game(NULL);
    BinaryInputStream input(GAGCore::openInflatingFileStreamBackend((glob2test::sourceRoot() / "maps/Archipelago.map.gz").string()));
    REQUIRE(game.load(&input));
    Player player;player.setTeam(game.teams[0]);
    AIMaxima::Maxima ai(&player);ai.context.initialize();
    ai.budget.farming_enabled=true;ai.budget.farming_protection_enabled=true;

    ai.budget.farming_economic_envelope_radius=20;
    for(int y=0;y<game.map.getH();++y)for(int x=0;x<game.map.getW();++x)
        game.map.setMapDiscovered(x,y,player.team->me);
    ai.update_farming(ai.context);
    const auto full=ai.farm_protection_mask;
    REQUIRE(std::count(full.begin(),full.end(),Uint8(1))>0);
    ai.context.managementOrders.clear();
    game.map.unsetMapDiscovered();
    for(int y=80;y<96;++y)for(int x=24;x<40;++x)
        game.map.setMapDiscovered(x,y,player.team->me);
    ai.update_farming(ai.context);
    // Protection plans and area orders apply only to discovered cells.
    // Undiscovered classification is provisional and must not issue orders.
    for(int y=80;y<96;++y)for(int x=24;x<40;++x)
        REQUIRE(ai.farm_protection_mask[y*game.map.getW()+x]==full[y*game.map.getW()+x]);
    for(const auto& order:ai.context.managementOrders)
    {
        if(auto add=std::dynamic_pointer_cast<Management::AddArea>(order))
            for(const auto& cell:add->locations)REQUIRE(game.map.isMapDiscovered(cell.x,cell.y,player.team->me));
        if(auto remove=std::dynamic_pointer_cast<Management::RemoveArea>(order))
            for(const auto& cell:remove->locations)REQUIRE(game.map.isMapDiscovered(cell.x,cell.y,player.team->me));
    }
}

TEST_SUITE("Maxima.FarmingIntegration")
{
	TEST_CASE("farm zones use the final wheat protection plan including empty frontier cells")
	{
		glob2test::HeadlessGlobals globals;
		Fixture f;auto& ai=*f.ai;auto& map=f.game.map;
		for(int y=0; y<64; ++y) map.setTerrain(10,y,256);
		for(int y=20; y<24; ++y) for(int x=11; x<15; ++x) setYoungResource(map,x,y,WHEAT);
		for(int y=40; y<44; ++y) for(int x=11; x<15; ++x) setYoungResource(map,x,y,WOOD);
		ai.budget.farming_enabled=true;ai.budget.farming_protection_enabled=true;
		ai.budget.farming_management_radius=0;
		ai.update_farming(ai.context);
		const auto original=ai.farm_protection_mask;
		const auto wheat=ai.wheat_farm_protection_mask;
		std::set<int> expected;
		int frontier=0;
		for(int i=0; i<64*64; ++i)
			if(original[i] && wheat[i] && map.canPaintFarmArea(i%64,i/64))
			{
				expected.insert(i);
				frontier+=map.getResource(i%64,i/64).type==NO_RES_TYPE;
			}
		REQUIRE(!expected.empty()); REQUIRE(frontier>0);
		ai.context.managementOrders.clear();
		f.game.gameHeader.getExperiments().set(ExperimentId::FarmAreas);
		ai.update_farming(ai.context);
		CHECK(ai.farm_protection_mask==original);
		std::set<int> actual;
		for(const auto& order:ai.context.managementOrders)
			if(auto add=std::dynamic_pointer_cast<Management::AddArea>(order))
				if(add->areaType==FarmArea)
					for(const auto& cell:add->locations) actual.insert(cell.y*64+cell.x);
		CHECK(actual==expected);
	}
	TEST_CASE("permanent wheat layout") { glob2test::HeadlessGlobals globals; permanentWheatLayout(); }
	TEST_CASE("firebreak management radius") { glob2test::HeadlessGlobals globals; firebreakManagementRadius(); }
	TEST_CASE("farm management radius") { glob2test::HeadlessGlobals globals; farmManagementRadius(); }
	TEST_CASE("coastal wheat crosses fertility dips") { glob2test::HeadlessGlobals globals; coastalWheatCrossesFertilityDips(); }
	TEST_CASE("seed survival") { glob2test::HeadlessGlobals globals; seedSurvival(); }
	TEST_CASE("initial flag staffing") { glob2test::HeadlessGlobals globals; initialFlagStaffing(); }
	TEST_CASE("wood reserve survives wheat pincer") { glob2test::HeadlessGlobals globals; woodReserveSurvivesWheatPincer(); }
	TEST_CASE("wood reserve survives own farm protection") { glob2test::HeadlessGlobals globals; woodReserveSurvivesOwnFarmProtection(); }
	TEST_CASE("expansion leaves harvest lanes open") { glob2test::HeadlessGlobals globals; expansionLeavesHarvestLanesOpen(); }
	TEST_CASE("aligned pattern transitions") { glob2test::HeadlessGlobals globals; alignedPatternTransitions(); }
	TEST_CASE("maintenance protection agreement") { glob2test::HeadlessGlobals globals; maintenanceProtectionAgreement(); }
	TEST_CASE("archipelago harvest does not seal wheat") { glob2test::HeadlessGlobals globals; archipelagoHarvestDoesNotSealWheat(); }
	TEST_CASE("seed stability across maps") { glob2test::HeadlessGlobals globals; seedStabilityAcrossMaps(); }
	TEST_CASE("farming respects discovery") { glob2test::HeadlessGlobals globals; farmingRespectsDiscovery(); }
}
