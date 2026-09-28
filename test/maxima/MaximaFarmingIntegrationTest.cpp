// Link with the game objects (excluding Glob2.cpp) to exercise the real runtime.
#include "../../src/GlobalContainer.h"
#include "../../src/Game.h"
#include "../../src/team/Team.h"
#include "../../src/ai/AIImplementation.h"
#include "../../src/map/Map.h"
#include "../../src/Order.h"
#include "../../src/Player.h"
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
#include <iostream>
#include <memory>

GlobalContainer* globalContainer = NULL;
using namespace AIMaximaRuntime;


namespace
{
void setYoungResource(Map& map, int x, int y, int type)
{
    map.setResource(x,y,type,1);
    map.getTile(x,y).resource.amount=1;
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
    for(int y=0;y<64;++y)map.getTile(10,y).terrain=256;
    auto* home=f.game.addBuilding(11,1,
        globalContainer->buildingsTypes.getTypeNum("inn",0,false),0);
    assert(home);
    setYoungResource(map,11,21,WHEAT);
    setYoungResource(map,11,23,WHEAT);
    ai.budget.farming_enabled=true;ai.budget.farming_protection_enabled=true;
    ai.budget.farming_management_radius=20;
    ai.budget.farming_wheat_fertility_min=3276;
    ai.update_farming(ai.context);
    assert(ai.farm_protection_mask[21*64+11]); // inclusive boundary
    assert(!ai.farm_protection_mask[23*64+11]);
    // A virtual building cannot anchor a farm, and loss of infrastructure
    // must not expose the already-established seed on the boundary.
    auto* originalType=home->type;
    home->type=globalContainer->buildingsTypes.getByType("warflag",0,false);
    ai.update_farming(ai.context);
    assert(ai.farm_protection_mask[21*64+11]);
    assert(!ai.farm_protection_mask[23*64+11]);
    ai.budget.farming_management_radius=0; // optimizer's unlimited control
    ai.update_farming(ai.context);
    assert(ai.farm_protection_mask[23*64+11]);
    home->type=originalType;
}

void coastalWheatCrossesFertilityDips()
{
    for(int resource:{WHEAT,WOOD})
    {
        Fixture f;auto& ai=*f.ai;Map& map=f.game.map;
        for(int y=0;y<64;++y)map.getTile(10,y).terrain=256;
        map.setResource(11,20,resource,5);
        ai.budget.farming_enabled=true;ai.budget.farming_protection_enabled=true;
        ai.budget.farming_wheat_fertility_min=65536;
        ai.budget.farming_minimum_wood_fertility=65536;
        ai.budget.farming_wood_firebreak_enabled=false;
        for(bool live:{false,true})
        {
            if(live)setYoungResource(map,11,21,resource);
            ai.update_farming(ai.context);
            assert(ai.fertility_cache.at(11,21)<65536);
            assert(bool(ai.farm_protection_mask[21*64+11])==(resource==WHEAT));
            // Neither inland expansion nor distant empty coastline inherits
            // the exception, even though both are valid growing terrain.
            assert(!ai.farm_protection_mask[20*64+12]);
            assert(!ai.farm_protection_mask[30*64+11]);
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
            map.getTile(x+offset,y).terrain=256;
        const int first=index(20,20),second=index(21,21),growth=index(22,20);
        setYoungResource(map,first%64,first/64,resource);
        setYoungResource(map,second%64,second/64,resource);
        ai.budget.farming_enabled=true;
        ai.budget.farming_protection_enabled=true;
        ai.budget.farming_wheat_fertility_min=3276;
        ai.budget.farming_minimum_wood_fertility=3276;
        ai.budget.farming_wood_firebreak_enabled=false;
        ai.update_farming(ai.context);
        assert(ai.farm_protection_mask[first] && ai.farm_protection_mask[second]);
        assert(ai.farm_protection_mask[first]);
        assert(bool(ai.wheat_farm_protection_mask[first])==(resource==WHEAT));
        // Boundary growth keeps both the existing lattice seed and aligned edge.
        setYoungResource(map,growth%64,growth/64,resource);
        ai.update_farming(ai.context);
        assert(ai.farm_protection_mask[growth]);
        assert(ai.farm_protection_mask[first]);
        // A lone odd/odd seed retains normal protection while it can expand.
        map.setNoResource(first%64,first/64,0);
        map.setNoResource(growth%64,growth/64,0);
        ai.update_farming(ai.context);
        assert(ai.farm_protection_mask[second]);
        if(resource==WOOD)
        {
            ai.budget.farming_wood_firebreak_enabled=true;
            ai.wood_firebreak_mask[second]=1;
            ai.update_farming(ai.context);
            assert(!ai.farm_protection_mask[second]);
        }
    }
}

std::shared_ptr<OrderCreate> createOrder(Context& c)
{
    c.update_building_orders();
    for(auto order:c.orders)
        if(auto create=std::dynamic_pointer_cast<OrderCreate>(order))return create;
    assert(false);return {};
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
        assert(create->unitWorking==workers && create->unitWorkingFuture==workers);
        Building* flag=f.game.addBuilding(create->posX,create->posY,create->typeNum,0,
            create->unitWorking,create->unitWorkingFuture);
        assert(flag && flag->maxUnitWorking==workers && flag->desiredMaxUnitWorking==workers);
        if(workers==0)
        {
            Unit* worker=f.game.addUnit(21,20,0,WORKER,0,0,0,0);assert(worker);
            setYoungResource(f.game.map,22,20,WHEAT);
            f.game.map.buildingGradient(flag,0);
            // Even if discovery/management is delayed past a recruitment cycle,
            // the default broad resource selector cannot recruit a worker.
            for(int tick=0;tick<70;++tick)assert(!flag->subscribeForFlagingStep());
            assert(flag->unitsWorking.empty());
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
                for(int r=0;r<BASIC_COUNT;++r)flag->clearingResources[r]=selector->clearingResources[r];
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
                    map.getTile(cell.x,cell.y).clearArea&=~f.player.team->me;
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

// Only the selected cells permit new growth, isolating support lifetime from
// unrelated frontier targets. Extra crops provide a harvestable food reserve.
void temporaryExpansionSupport()
{
    for(int offset:{0,44}) for(bool live:{false,true})
    {
        Fixture f;auto& ai=*f.ai;Map& map=f.game.map;
        auto index=[&](int x,int y){return map.normalizeY(y+offset)*64+map.normalizeX(x+offset);};
        for(int y=0;y<64;++y)for(int x=0;x<64;++x)
            map.getTile(x,y).canResourcesGrow=0;
        for(int y=0;y<64;++y)for(int x=0;x<12;++x)
        {int i=index(x,y);map.getTile(i%64,i/64).terrain=256;}
        const int seed=index(19,19),support=index(20,19);
        const int target=index(20,20),other=index(20,18);
        // The donor sits on a narrow shoreline; access remains on the east.
        for(int y:{18,20}){int i=index(19,y);map.getTile(i%64,i/64).terrain=256;}
        setYoungResource(map,seed%64,seed/64,WHEAT);
        for(int y:{25,27,29,31})
        {
            int i=index(21,y);setYoungResource(map,i%64,i/64,WHEAT);
            i=index(22,y);setYoungResource(map,i%64,i/64,WHEAT);
        }
        for(int i:{seed,support,target,other})map.getTile(i%64,i/64).canResourcesGrow=1;
        if(live)setYoungResource(map,support%64,support/64,WHEAT);
        configurePattern(f);
        ai.update_farming(ai.context);applyAreaContracts(f);
        assert(ai.farm_protection_mask[target] && ai.farm_protection_mask[other]);
        assert(ai.farm_protection_mask[support]);
        ai.update_farming(ai.context);assert(applyAreaContracts(f)==0);
        if(live)
        {
            // A donor remains protected even when its wheat is full.
            map.getTile(support%64,support/64).resource.amount=4;
            ai.update_farming(ai.context);applyAreaContracts(f);
            assert(ai.farm_protection_mask[support]);
        }
        // Intermediate wheat remains protected after it grows; a shared
        // support is released only when both targets have established wheat.
        setYoungResource(map,support%64,support/64,WHEAT);
        setYoungResource(map,target%64,target/64,WHEAT);
        ai.update_farming(ai.context);applyAreaContracts(f);
        assert(ai.farm_protection_mask[support]);
        setYoungResource(map,other%64,other/64,WHEAT);
        ai.update_farming(ai.context);applyAreaContracts(f);
        assert(!ai.farm_protection_mask[support]);
        assert(!map.isForbidden(support%64,support/64,f.player.team->me));
        assert(ai.farm_protection_mask[seed]);
        map.setNoResource(target%64,target/64,0);
        ai.update_farming(ai.context);applyAreaContracts(f);
        assert(ai.farm_protection_mask[support]);
        // Cache reconstruction must choose the same support from saved areas.
        const auto protectedMask=ai.farm_protection_mask;
        ai.applied_farm_protection_mask.clear();
        ai.update_farming(ai.context);assert(applyAreaContracts(f)==0);
        assert(ai.farm_protection_mask==protectedMask);
        ai.budget.recovery_active=true;
        ai.update_farming(ai.context);applyAreaContracts(f);
        assert(!ai.farm_protection_mask[support]);
        ai.budget.recovery_active=false;
        ai.budget.food_emergency=true;
        ai.update_farming(ai.context);assert(!ai.farm_protection_mask[support]);
        ai.budget.food_emergency=false;
        ai.budget.farming_management_radius=1; // no building anchor
        ai.update_farming(ai.context);assert(!ai.farm_protection_mask[support]);
        ai.budget.farming_management_radius=0;
        // An occupied or growth-disabled support cannot be reserved.
        map.getTile(support%64,support/64).groundUnit=0;
        ai.update_farming(ai.context);assert(!ai.farm_protection_mask[support]);
        map.getTile(support%64,support/64).groundUnit=NOGUID;
        map.getTile(support%64,support/64).canResourcesGrow=0;
        ai.update_farming(ai.context);assert(!ai.farm_protection_mask[support]);
        map.getTile(support%64,support/64).canResourcesGrow=1;
        // Fewer than four available crops cannot spare a live donor.
        for(int y:{25,27,29,31})
        {int i=index(22,y);map.setNoResource(i%64,i/64,0);}
        ai.update_farming(ai.context);assert(!ai.farm_protection_mask[support]);
        // Reopening the western land makes support split the local lane.
        map.setNoResource(support%64,support/64,0);
        for(int y:{18,20}){int i=index(19,y);map.getTile(i%64,i/64).terrain=0;}
        ai.update_farming(ai.context);assert(!ai.farm_protection_mask[support]);
        // No source means no target or intermediate protection obligation.
        map.setNoResource(seed%64,seed/64,0);
        map.setNoResource(other%64,other/64,0);
        ai.update_farming(ai.context);applyAreaContracts(f);
        assert(!ai.farm_protection_mask[support] && !ai.farm_protection_mask[target]);
    }
}

void woodReserveSurvivesWheatPincer()
{
    for(int offset:{0,44})
    {
        Fixture f;auto& ai=*f.ai;Map& map=f.game.map;
        auto index=[&](int x,int y){return map.normalizeY(y+offset)*64+map.normalizeX(x+offset);};
        for(int i=0;i<64*64;++i)map.getTile(i%64,i/64).canResourcesGrow=0;
        for(int y=0;y<64;++y)for(int x=0;x<12;++x)
        {int i=index(x,y);map.getTile(i%64,i/64).terrain=256;}
        const int seed=index(20,20),outlet=index(20,19),worker=index(21,18);
        for(int y=19;y<=21;++y)for(int x=19;x<=21;++x)
        {int i=index(x,y);setYoungResource(map,i%64,i/64,WHEAT);}
        map.setNoResource(seed%64,seed/64,0);
        setYoungResource(map,seed%64,seed/64,WOOD);
        map.setNoResource(outlet%64,outlet/64,0);
        for(int i:{seed,outlet})map.getTile(i%64,i/64).canResourcesGrow=1;
        assert(f.game.addUnit(worker%64,worker/64,0,WORKER,0,0,0,0));
        configurePattern(f);
        ai.budget.farming_minimum_wood_fertility=65536;
        ai.budget.farming_maintenance_clearing_enabled=true;
        ai.budget.farming_wheat_invasion_clearing_enabled=true;
        ai.budget.farming_wood_firebreak_enabled=true;
        ai.strategy.farming.wood_firebreak_fertility_min_percent=0;
        ai.strategy.farming.wood_firebreak_fertility_max_percent=100;
        ai.initialize_farming_cache(ai.context);
        auto openReserve=ai.select_wood_reserve(ai.context);
        assert(openReserve.cells[seed]==1 && openReserve.cells[outlet]==2);
        map.addForbidden(outlet%64,outlet/64,f.player.team->teamNumber);
        auto blockedOutlet=ai.select_wood_reserve(ai.context);
        assert(blockedOutlet.cells[outlet]!=2);
        map.removeForbidden(outlet%64,outlet/64,f.player.team->teamNumber);
        auto update=[&]() {
            ai.update_farming(ai.context);applyAreaContracts(f);
            ai.update_maintenance_clearing_areas(ai.context);applyAreaContracts(f);
        };
        for(int cycle=0;cycle<4;++cycle)
        {
            update();
            auto reserve=ai.select_wood_reserve(ai.context);
            assert(reserve.cells[seed]==1 && reserve.cells[outlet]==2);
            assert(map.isForbidden(seed%64,seed/64,f.player.team->me));
            assert(!map.isForbidden(outlet%64,outlet/64,f.player.team->me));
            assert(!map.isClearArea(seed%64,seed/64,f.player.team->me));
            // The seed can keep producing harvestable wood despite wheat on
            // either flank and an otherwise universal wood firebreak.
            assert(map.incResource(outlet%64,outlet/64,WOOD,0));
            update();
            assert(!map.isClearArea(outlet%64,outlet/64,f.player.team->me));
            assert(!map.isForbidden(outlet%64,outlet/64,f.player.team->me));
            while(map.isResourceTakeable(outlet%64,outlet/64,WOOD))
                map.decResource(outlet%64,outlet/64);
        }
        // Competing wheat in the outlet remains harvestable, never a new seed.
        assert(map.incResource(outlet%64,outlet/64,WHEAT,0));
        update();assert(!ai.wheat_farm_protection_mask[outlet]);
        const auto before=ai.select_wood_reserve(ai.context).cells;
        ai.applied_farm_protection_mask.clear();
        update();assert(ai.select_wood_reserve(ai.context).cells==before);
        const auto world=ai.collect_development_world(ai.context);
        assert(world.tiles[seed].woodReserve && world.tiles[outlet].woodReserve);
        assert(world.tiles[outlet].clearableResource); // harvestable is not buildable
        // Exact committed building/access reservations retain priority.
        ai.development_planner.footprintRefs.assign(64*64,0);
        ai.development_planner.footprintRefs[seed]=1;
        update();assert(!ai.farm_protection_mask[seed]);
        assert(ai.select_wood_reserve(ai.context).seeds==0);
        ai.development_planner.footprintRefs[seed]=0;
        ai.budget.farming_management_radius=1; // no physical home nearby
        update();assert(ai.select_wood_reserve(ai.context).seeds==0);
        ai.budget.farming_management_radius=0;
        ai.budget.farming_protection_enabled=false;
        update();assert(ai.select_wood_reserve(ai.context).seeds==0);
        ai.budget.farming_protection_enabled=true;
        // More eligible wood does not expand the reserve beyond two donors.
        for(int x:{24,26})
        {
            int i=index(x,x);setYoungResource(map,i%64,i/64,WOOD);
            map.getTile(i%64,i/64).canResourcesGrow=1;
            i=index(x,x-1);map.getTile(i%64,i/64).canResourcesGrow=1;
        }
        update();
        auto stable=ai.select_wood_reserve(ai.context);
        assert(stable.seeds==2);
        for(int cycle=0;cycle<3;++cycle)
        {update();assert(ai.select_wood_reserve(ai.context).cells==stable.cells);}
        map.setNoResource(seed%64,seed/64,0);
        update();
        auto replacement=ai.select_wood_reserve(ai.context);
        assert(replacement.seeds==2 && !replacement.cells[seed]);
    }
}

void woodReserveSurvivesOwnFarmProtection()
{
    Fixture f;auto& ai=*f.ai;Map& map=f.game.map;
    for(int y=0;y<64;++y)for(int x=0;x<12;++x)map.getTile(x,y).terrain=256;
    const int seed=21*64+20;
    setYoungResource(map,seed%64,seed/64,WOOD);
    assert(f.game.addUnit(24,21,0,WORKER,0,0,0,0));
    configurePattern(f);
    ai.budget.farming_minimum_wood_fertility=1;
    ai.budget.farming_wood_firebreak_enabled=false;
    for(int cycle=0;cycle<4;++cycle)
    {
        ai.update_farming(ai.context);applyAreaContracts(f);
        auto reserve=ai.select_wood_reserve(ai.context);
        assert(reserve.seeds==1);
        assert(reserve.cells[seed]==1);
    }
}

void alignedPatternTransitions()
{
    for(int resource:{WHEAT,WOOD}) for(int offset:{0,42})
    {
        Fixture f;auto& ai=*f.ai;Map& map=f.game.map;
        auto index=[&](int x,int y){return map.normalizeY(y+offset)*64+map.normalizeX(x+offset);};
        for(int y=0;y<64;++y)for(int x=0;x<12;++x)
        {int i=index(x,y);map.getTile(i%64,i/64).terrain=256;}
        for(int y=18;y<=26;++y)for(int x=18;x<=26;++x)
        {int i=index(x,y);setYoungResource(map,i%64,i/64,resource);}
        configurePattern(f);
        const int seed=index(21,23),lane=index(21,22),hole=index(20,22);
        ai.update_farming(ai.context);applyAreaContracts(f);
        const auto filled=ai.farm_protection_mask;
        assert(filled[seed] && !filled[lane]);
        for(int repeat=0;repeat<3;++repeat)
        {
            map.setNoResource(hole%64,hole/64,0);
            ai.update_farming(ai.context);applyAreaContracts(f);
            assert(ai.farm_protection_mask[seed] && !ai.farm_protection_mask[lane]);
            ai.update_farming(ai.context);
            assert(applyAreaContracts(f)==0);
            setYoungResource(map,hole%64,hole/64,resource);
            ai.update_farming(ai.context);applyAreaContracts(f);
            assert(ai.farm_protection_mask==filled);
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
        for(int y=0;y<64;++y)for(int x=0;x<64;++x)map.getTile(x,y).canResourcesGrow=1;
        for(int y=0;y<64;++y)for(int x=0;x<12;++x)
        {int i=index(x,y);map.getTile(i%64,i/64).terrain=256;}
        for(int y=18;y<=26;++y)for(int x=18;x<=26;++x)
        {int i=index(x,y);setYoungResource(map,i%64,i/64,WHEAT);}
        configurePattern(f);
        ai.budget.food_emergency=true; // isolate layout from supplemental support
        const int seed=index(21,23),interior=index(20,22),edge=index(18,22),lane=index(21,22);
        bool first=true;
        for(int amount:{1,2,3,4,5,2})
        {
            for(int i:{seed,interior,edge,lane})
            {setYoungResource(map,i%64,i/64,WHEAT);map.getTile(i%64,i/64).resource.amount=amount;}
            ai.update_farming(ai.context);
            const int changes=applyAreaContracts(f);
            if(!first)assert(changes==0);
            first=false;
            for(int i:{seed,edge})
            {
                assert(map.isForbidden(i%64,i/64,f.player.team->me));
            }
            for(int i:{interior,lane})assert(!map.isForbidden(i%64,i/64,f.player.team->me));
            ai.update_farming(ai.context);assert(applyAreaContracts(f)==0);
        }
        map.setNoResource(interior%64,interior/64,0);
        ai.update_farming(ai.context);applyAreaContracts(f);
        assert(!ai.farm_protection_mask[interior]);
        setYoungResource(map,interior%64,interior/64,WHEAT);
        ai.update_farming(ai.context);applyAreaContracts(f);
        assert(!ai.farm_protection_mask[interior]);
        // Selected seeds stay protected at maturity and after forced resource loss.
        setYoungResource(map,seed%64,seed/64,WHEAT);
        map.getTile(seed%64,seed/64).resource.amount=4;
        ai.update_farming(ai.context);applyAreaContracts(f);
        assert(map.isForbidden(seed%64,seed/64,f.player.team->me));
        map.setNoResource(seed%64,seed/64,0);
        ai.update_farming(ai.context);applyAreaContracts(f);
        assert(map.isForbidden(seed%64,seed/64,f.player.team->me));
        setYoungResource(map,seed%64,seed/64,WHEAT);
        ai.update_farming(ai.context);applyAreaContracts(f);
        assert(map.isForbidden(seed%64,seed/64,f.player.team->me));
    }
}

void firebreakManagementRadius()
{
    Fixture f;auto& ai=*f.ai;Map& map=f.game.map;
    auto* home=f.game.addBuilding(11,1,
        globalContainer->buildingsTypes.getTypeNum("inn",0,false),0);
    assert(home);
    for(int y:{21,22,63})map.setResource(11,y,WOOD,5);
    ai.budget.farming_enabled=true;
    ai.budget.farming_maintenance_clearing_enabled=true;
    ai.budget.farming_wood_firebreak_enabled=true;
    ai.strategy.farming.wood_firebreak_fertility_min_percent=0;
    ai.strategy.farming.wood_firebreak_fertility_max_percent=100;
    auto update=[&](){ai.update_maintenance_clearing_areas(ai.context);applyAreaContracts(f);};
    ai.budget.farming_management_radius=0;
    update();assert(map.isClearArea(11,22,f.player.team->me));
    ai.budget.farming_management_radius=20;
    update();
    assert(map.isClearArea(11,21,f.player.team->me));
    assert(!map.isClearArea(11,22,f.player.team->me)); // old clearing removed
    assert(map.isClearArea(11,63,f.player.team->me)); // wrapped distance
    auto* original=home->type;
    home->type=globalContainer->buildingsTypes.getByType("warflag",0,false);
    update();assert(!map.isClearArea(11,21,f.player.team->me));
    home->type=original;
}

void maintenanceProtectionAgreement()
{
    for(int offset:{0,44})
    {
        Fixture f;auto& ai=*f.ai;Map& map=f.game.map;
        auto index=[&](int x,int y){return map.normalizeY(y+offset)*64+map.normalizeX(x+offset);};
        for(int y=0;y<64;++y)for(int x=0;x<12;++x)
        {int i=index(x,y);map.getTile(i%64,i/64).terrain=256;}
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
            assert(ai.wheat_farm_protection_mask[wheat]);
            assert(!ai.farm_protection_mask[wood]);
            ai.update_maintenance_clearing_areas(ai.context);
            changes+=applyAreaContracts(f);
            assert(ai.maintenance_circulation_mask[wood]);
            assert(!map.isForbidden(wood%64,wood/64,f.player.team->me));
            if(cycle)assert(changes==0);
        }
        // Disabling the sub-policy releases its clearing obligation immediately.
        ai.budget.farming_wheat_invasion_clearing_enabled=false;
        ai.update_farming(ai.context);applyAreaContracts(f);
        ai.update_maintenance_clearing_areas(ai.context);applyAreaContracts(f);
        assert(ai.farm_protection_mask[wood]);
        assert(!ai.maintenance_circulation_mask[wood]);
        // Removing wheat must also revoke it; stale maintenance cannot latch it.
        ai.budget.farming_wheat_invasion_clearing_enabled=true;
        map.setNoResource(wheat%64,wheat/64,0);
        ai.update_farming(ai.context);applyAreaContracts(f);
        ai.update_maintenance_clearing_areas(ai.context);applyAreaContracts(f);
        assert(ai.farm_protection_mask[wood]);
        assert(!ai.maintenance_circulation_mask[wood]);
    }
}

void archipelagoHarvestDoesNotSealWheat()
{
    Game game(NULL);
    BinaryInputStream input(new FileStreamBackend(fopen("maps/Archipelago.map","rb")));
    assert(game.load(&input));
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
                    if(game.map.isResourceTakeable(x,y,WHEAT)) {
                        ++wheat;open+=!ai.farm_protection_mask[y*128+x];
                    }
            assert(wheat>0 && open>0);
        }
    }
}

void seedStabilityAcrossMaps()
{
    const char* maps[]={"Holiday_Island_2","Archipelago","Isles","Migration",
        "Garden_3","A_big_pond","Wild_River","Sand_River"};
    for(const char* name:maps) {
        Game game(NULL);std::string path=std::string("maps/")+name+".map";
        BinaryInputStream input(new FileStreamBackend(fopen(path.c_str(),"rb")));
        assert(game.load(&input));
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
            if(game.map.isResourceTakeable(x,y,WHEAT)&&ai.farm_protection_mask[y*w+x])
                seeds.push_back(y*w+x);
        assert(!seeds.empty());
        // Adversarial harvest: remove every available wheat tile each round.
        // Grow new neighbors between rounds to change patch boundaries and
        // exercise the exact feedback loop that destroyed Holiday's left farm.
        for(int round=0;round<12;++round) {
            for(int y=0;y<h;++y)for(int x=0;x<w;++x)
                if(game.map.isResourceTakeable(x,y,WHEAT)&&!ai.farm_protection_mask[y*w+x])
                    game.map.setNoResource(x,y,1);
            for(int seed:seeds) {
                int x=game.map.normalizeX(seed%w+(round%3)-1);
                int y=game.map.normalizeY(seed/w+((round/3)%3)-1);
                const Tile& c=game.map.getTile(x,y);
                if(c.terrain<16&&c.building==NOGBID&&c.resource.type==NO_RES_TYPE)
                    setYoungResource(game.map,x,y,WHEAT);
            }
            ai.timer+=64;ai.update_farming(ai.context);
            for(int seed:seeds) {
                assert(game.map.isResourceTakeable(seed%w,seed/w,WHEAT));
                assert(ai.farm_protection_mask[seed]);
            }
        }
        std::cout<<"stable seed cycles: "<<name<<" seeds="<<seeds.size()<<" rounds=12\n";
    }
}

void farmingRespectsDiscovery()
{
    Game game(NULL);
    BinaryInputStream input(new FileStreamBackend(fopen("maps/Archipelago.map","rb")));
    assert(game.load(&input));
    Player player;player.setTeam(game.teams[0]);
    AIMaxima::Maxima ai(&player);ai.context.initialize();
    ai.budget.farming_enabled=true;ai.budget.farming_protection_enabled=true;

    ai.budget.farming_economic_envelope_radius=20;
    for(int y=0;y<game.map.getH();++y)for(int x=0;x<game.map.getW();++x)
        game.map.setMapDiscovered(x,y,player.team->me);
    ai.update_farming(ai.context);
    const auto full=ai.farm_protection_mask;
    assert(std::count(full.begin(),full.end(),Uint8(1))>0);
    ai.context.managementOrders.clear();
    game.map.unsetMapDiscovered();
    for(int y=80;y<96;++y)for(int x=24;x<40;++x)
        game.map.setMapDiscovered(x,y,player.team->me);
    ai.update_farming(ai.context);
    // Protection plans and area orders apply only to discovered cells.
    // Undiscovered classification is provisional and must not issue orders.
    for(int y=80;y<96;++y)for(int x=24;x<40;++x)
        assert(ai.farm_protection_mask[y*game.map.getW()+x]==full[y*game.map.getW()+x]);
    for(const auto& order:ai.context.managementOrders)
    {
        if(auto add=std::dynamic_pointer_cast<Management::AddArea>(order))
            for(const auto& cell:add->locations)assert(game.map.isMapDiscovered(cell.x,cell.y,player.team->me));
        if(auto remove=std::dynamic_pointer_cast<Management::RemoveArea>(order))
            for(const auto& cell:remove->locations)assert(game.map.isMapDiscovered(cell.x,cell.y,player.team->me));
    }
}

int main()
{
    GlobalContainer container;globalContainer=&container;container.runNoX=true;
    container.buildingsTypes.init();
    IntBuildingType::init();

    permanentWheatLayout();firebreakManagementRadius();farmManagementRadius();coastalWheatCrossesFertilityDips();
        seedSurvival();initialFlagStaffing();
    woodReserveSurvivesWheatPincer();woodReserveSurvivesOwnFarmProtection();
        temporaryExpansionSupport();alignedPatternTransitions();maintenanceProtectionAgreement();

    archipelagoHarvestDoesNotSealWheat();seedStabilityAcrossMaps();farmingRespectsDiscovery();
    std::cout<<"aligned farming, maintenance agreement, seed survival and flag staffing passed\n";
}
