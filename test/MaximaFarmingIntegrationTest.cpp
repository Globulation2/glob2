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


namespace
{
struct Fixture
{
    Game game;
    Player player;
    std::unique_ptr<AIMaxima::Maxima> ai;
    Fixture(): game(NULL)
    {
        game.map.setSize(6,6,GRASS);game.map.setGame(&game);
        game.addTeam();game.teams[0]->race.loadDefault();
        player.setTeam(game.teams[0]);
        for(int y=0;y<64;++y)for(int x=0;x<64;++x)
            game.map.setMapDiscovered(x,y,player.team->me);
        ai.reset(new AIMaxima::Maxima(&player));
        ai->context.initialize();
    }
};

// Low fertility may slow growth, but must not interrupt a coastal wheat
// wall or revoke its protection when the frontier becomes a live crop.
void farmManagementRadius()
{
    Fixture f;auto& ai=*f.ai;Map& map=f.game.map;
    for(int y=0;y<64;++y)map.getTile(10,y).terrain=256;
    auto* home=f.game.addBuilding(11,1,
        globalContainer->buildingsTypes.getTypeNum("inn",0,false),0);
    assert(home);
    map.setResource(11,21,CORN,5);
    map.setResource(11,22,CORN,5);
    ai.budget.farming_enabled=true;ai.budget.farming_protection_enabled=true;
    ai.budget.farming_management_radius=20;
    ai.budget.farming_wheat_fertility_min=3276;
    ai.update_farming(ai.context);
    assert(ai.farm_protection_mask[21*64+11]); // inclusive boundary
    assert(!ai.farm_protection_mask[22*64+11]);
    // A virtual building cannot anchor a farm, and loss of infrastructure
    // must not expose the already-established seed on the boundary.
    auto* originalType=home->type;
    home->type=globalContainer->buildingsTypes.getByType("warflag",0,false);
    ai.update_farming(ai.context);
    assert(ai.farm_protection_mask[21*64+11]);
    assert(!ai.farm_protection_mask[22*64+11]);
    ai.budget.farming_management_radius=0; // optimizer's unlimited control
    ai.update_farming(ai.context);
    assert(ai.farm_protection_mask[22*64+11]);
    home->type=originalType;
}

void coastalWheatCrossesFertilityDips()
{
    for(int resource:{CORN,WOOD})
    {
        Fixture f;auto& ai=*f.ai;Map& map=f.game.map;
        for(int y=0;y<64;++y)map.getTile(10,y).terrain=256;
        map.setResource(11,20,resource,5);
        ai.budget.farming_enabled=true;ai.budget.farming_protection_enabled=true;
        ai.budget.farming_barrier_enabled=false;
        ai.budget.farming_coastal_porosity_enabled=false;
        ai.budget.farming_wheat_fertility_min=65536;
        ai.budget.farming_minimum_wood_fertility=65536;
        ai.budget.farming_wood_firebreak_enabled=false;
        for(bool live:{false,true})
        {
            if(live)map.setResource(11,21,resource,1);
            ai.update_farming(ai.context);
            assert(ai.fertility_cache.at(11,21)<65536);
            assert(bool(ai.farm_protection_mask[21*64+11])==(resource==CORN));
            // Neither inland expansion nor distant empty coastline inherits
            // the exception, even though both are valid growing terrain.
            assert(!ai.farm_protection_mask[20*64+12]);
            assert(!ai.farm_protection_mask[30*64+11]);
        }
    }
}

void shorelineRequiresWater()
{
    for(int resource:{CORN,WOOD}) for(int offset:{0,42})
    for(int sandTerrain:{108,128}) for(bool live:{false,true})
    for(int diagonal:{0,1})
    {
        Fixture f;auto& ai=*f.ai;Map& map=f.game.map;
        auto index=[&](int x,int y){return map.normalizeY(y+offset)*64+map.normalizeX(x+offset);};
        auto terrain=[&](int x,int y,int value){int i=index(x,y);map.getTile(i%64,i/64).terrain=value;};
        for(int y=0;y<64;++y)for(int x=0;x<12;++x)terrain(x,y,256);
        for(int y=19;y<=23;++y)for(int x=19;x<=21;++x)
        {int i=index(x,y);map.setResource(i%64,i/64,resource,1);}
        const int target=index(22,21);
        if(live)map.setResource(target%64,target/64,resource,1);
        terrain(23,21+diagonal,sandTerrain);
        ai.budget.farming_enabled=true;ai.budget.farming_protection_enabled=true;
        ai.budget.farming_barrier_enabled=false;
        ai.budget.farming_coastal_porosity_enabled=false;
        ai.budget.farming_wheat_fertility_min=3276;
        ai.budget.farming_minimum_wood_fertility=3276;
        ai.budget.farming_wood_firebreak_enabled=false;
        ai.update_farming(ai.context);
        assert(ai.fertility_cache.at(target%64,target/64)>=3276);
        // Inland sand must not close a cross-parity landward opening, whether
        // it holds a live crop or is an empty pre-growth frontier.
        assert(!ai.farm_protection_mask[target]);
        // Connecting that sand to actual water makes the whole beach
        // coastal. Diagonal contact and map wrapping count.
        terrain(24,21+diagonal,sandTerrain);terrain(25,22+diagonal,256);

        ai.fertility_cache=AIMaxima::Farming::ExactFertilityCache();
        ai.context.get_gradient_manager().invalidate();
        ai.update_farming(ai.context);
        assert(ai.farm_protection_mask[target]);
    }
}

void wideBeachSurvivesHarvestedInterior()
{
    for(int resource:{CORN,WOOD}) for(int offset:{0,48})
    for(int beachWidth:{2,4,7}) for(bool live:{false,true}) for(int direction=0;direction<4;++direction)
    {
        Fixture f;auto& ai=*f.ai;Map& map=f.game.map;
        auto index=[&](int x,int y){
            if(direction&1)x=-x;
            if(direction>=2)std::swap(x,y);
            return map.normalizeY(y+offset)*64+map.normalizeX(x+offset);
        };
        auto terrain=[&](int x,int y,int value){int i=index(x,y);map.getTile(i%64,i/64).terrain=value;};
        const int coast=12+beachWidth;
        // Pick a cross-parity coast cell so only the shoreline contract can
        // protect it. The same geometry crosses the map seam at offset 48.
        const int row=(coast&1)?20:21;
        for(int y=0;y<64;++y)
        {
            for(int x=0;x<12;++x)terrain(x,y,256);
            for(int x=12;x<coast;++x)terrain(x,y,x==coast-1?108:128);
        }
        for(int y=row-3;y<=row+3;++y)for(int x=coast;x<=coast+5;++x)
        {int i=index(x,y);map.setResource(i%64,i/64,resource,1);}
        const int target=index(coast,row);
        ai.budget.farming_enabled=true;ai.budget.farming_protection_enabled=true;
        ai.budget.farming_barrier_enabled=false;
        ai.budget.farming_coastal_porosity_enabled=false;
        ai.budget.farming_wheat_fertility_min=3276;
        ai.budget.farming_minimum_wood_fertility=3276;
        ai.budget.farming_wood_firebreak_enabled=false;
        if(!live)map.setNoResource(target%64,target/64,0);
        // Workers harvesting behind the coast must not turn its physical
        // beach backing into a sparse landward edge.
        int hole=index(coast+1,row);
        map.setNoResource(hole%64,hole/64,0);
        ai.update_farming(ai.context);
        assert(ai.fertility_cache.at(target%64,target/64)>=3276);
        if(!ai.farm_protection_mask[target])
            std::cerr<<"Unprotected wide beach: resource="<<resource<<" width="
                <<beachWidth<<" live="<<live<<" offset="<<offset<<"\n";
        assert(ai.farm_protection_mask[target]);
        map.setResource(hole%64,hole/64,resource,1);
        ai.update_farming(ai.context);
        assert(ai.farm_protection_mask[target]);
        ai.emergency_escape_mask[target]=1;
        ai.update_farming(ai.context);
        assert(!ai.farm_protection_mask[target]);
    }
}

void interiorHolesAreNotOuterEdges()
{
    for(int resource:{CORN,WOOD}) for(int offset:{0,42})
    for(bool live:{false,true})
    {
        Fixture f;auto& ai=*f.ai;Map& map=f.game.map;
        auto index=[&](int x,int y){return map.normalizeY(y+offset)*64+map.normalizeX(x+offset);};
        auto crop=[&](int x,int y){int i=index(x,y);map.setResource(i%64,i/64,resource,1);};
        auto empty=[&](int x,int y){int i=index(x,y);map.setNoResource(i%64,i/64,0);};
        for(int y=0;y<64;++y)for(int x=0;x<12;++x)
        {int i=index(x,y);map.getTile(i%64,i/64).terrain=256;}
        for(int y=18;y<=26;++y)for(int x=18;x<=26;++x)crop(x,y);
        const int target=index(22,21);
        if(live)empty(21,21);
        else {
            // The empty cell has a landward seed, but no immediately
            // waterward seed. The old frontier rule calls it an outer edge.
            empty(22,21);
            for(int y=20;y<=22;++y)empty(21,y);
        }
        ai.budget.farming_enabled=true;ai.budget.farming_protection_enabled=true;
        ai.budget.farming_barrier_enabled=false;
        ai.budget.farming_coastal_porosity_enabled=false;
        ai.budget.farming_wheat_fertility_min=3276;
        ai.budget.farming_minimum_wood_fertility=3276;
        ai.budget.farming_wood_firebreak_enabled=false;
        ai.update_farming(ai.context);
        assert(!ai.farm_protection_mask[target]);
        // An actual water-facing edge and its next expansion row remain
        // continuous even though neither directly touches shoreline sand.
        assert(ai.farm_protection_mask[index(18,22)]);
        assert(ai.farm_protection_mask[index(17,21)]);
        // Expose the same target to water: it now really is an outer edge.
        for(int y=18;y<=26;++y)for(int x=18;x<=21;++x)empty(x,y);
        ai.update_farming(ai.context);
        assert(ai.farm_protection_mask[target]);
    }
}

void seedSurvival()
{
    for(int resource:{CORN,WOOD}) for(int offset:{0,44})
    {
        Fixture f;auto& ai=*f.ai;Map& map=f.game.map;
        auto index=[&](int x,int y){return map.normalizeY(y+offset)*64+map.normalizeX(x+offset);};
        for(int y=0;y<64;++y)for(int x=0;x<12;++x)
            map.getTile(x+offset,y).terrain=256;
        const int first=index(20,20),second=index(21,21),growth=index(22,20);
        map.setResource(first%64,first/64,resource,1);
        map.setResource(second%64,second/64,resource,1);
        ai.budget.farming_enabled=true;
        ai.budget.farming_protection_enabled=true;
        ai.budget.farming_barrier_enabled=false;
        ai.budget.farming_wheat_fertility_min=3276;
        ai.budget.farming_minimum_wood_fertility=3276;
        ai.budget.farming_wood_firebreak_enabled=false;
        ai.update_farming(ai.context);
        assert(ai.farm_protection_mask[first] && ai.farm_protection_mask[second]);
        assert(ai.farm_protection_mask[first]);
        assert(bool(ai.wheat_farm_protection_mask[first])==(resource==CORN));
        // Boundary growth keeps both the existing lattice seed and aligned edge.
        map.setResource(growth%64,growth/64,resource,1);
        ai.update_farming(ai.context);
        assert(ai.farm_protection_mask[growth]);
        assert(ai.farm_protection_mask[first]);
        // A lone odd/odd seed retains normal protection while it can expand.
        map.setNoResource(first%64,first/64,0);
        map.setNoResource(growth%64,growth/64,0);
        ai.update_farming(ai.context);
        assert(ai.farm_protection_mask[second]);
        // Emergency circulation still has authority to consume the last seed.
        ai.emergency_escape_mask[second]=1;
        ai.update_farming(ai.context);
        assert(!ai.farm_protection_mask[second]);
        ai.emergency_escape_mask[second]=0;
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
            f.game.map.setResource(22,20,CORN,1);
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

void approachClearing()
{
    for(int offset:{0,44})
    {
        Fixture f;auto& ai=*f.ai;Context& c=ai.context;Map& map=f.game.map;
        auto index=[&](int x,int y){return map.normalizeY(y+offset)*64+map.normalizeX(x+offset);};
        // A room with two three-wide exits. Both gate openings are empty, but
        // wheat bars both core-side approaches, including two successive rows.
        for(int y=0;y<64;++y)for(int x=0;x<64;++x)
        {
            bool land=(x>=15&&x<=25&&y>=21&&y<=39)
                ||(x>=19&&x<=21&&y>=10&&y<=50);
            const int tile=index(x,y);map.getTile(tile%64,tile/64).terrain=land?0:256;
        }
        Unit* worker=f.game.addUnit(map.normalizeX(20+offset),map.normalizeY(30+offset),
            0,WORKER,0,0,0,0);assert(worker);
        ai.initialize_farming_cache(c);
        ai.strategic_gates={{index(20,20),index(19,20),index(21,20)},
                            {index(20,40),index(19,40),index(21,40)}};
        for(int gate=0;gate<2;++gate)
            for(int y=(gate?33:20);y<=(gate?40:27);++y)for(int x=19;x<=21;++x)
            {
                const int tile=index(x,y);
                ai.strategic_gate_routes[ai.strategic_gates[gate][0]].push_back(tile);
                ai.emergency_escape_mask[tile]=1;
            }
        for(int y:{24,25,35,36})for(int x=15;x<=25;++x)
        {
            const int tile=index(x,y);map.setResource(tile%64,tile/64,CORN,1);
        }
        ai.budget.farming_enabled=true;ai.budget.farming_gate_clearing_enabled=true;
        ai.budget.farming_gate_clearing_radius=1;
        ai.budget.recovery_active=true;
        ai.budget.farming_min_workers_for_clearing=24;
        ai.budget.farming_maintenance_clearing_enabled=true;
        ai.budget.farming_allow_proactive_clearing=false;
        assert(!ai.barrier_gate_is_clear(c,ai.strategic_gates[0]));
        assert(!ai.barrier_gate_is_clear(c,ai.strategic_gates[1]));
        assert(ai.barrier_gate_is_resource_clearable(c,ai.strategic_gates[0]));
        ai.manage_land_clearing(c);
        const int id=ai.proactive_clearing_flag;assert(id>=0);
        const auto create=createOrder(c);
        assert(create->unitWorking==0);
        // Target must be the room-facing row, within the worker's compartment.
        assert(create->posY==map.normalizeY(25+offset) || create->posY==map.normalizeY(35+offset));
        Building* flag=f.game.addBuilding(create->posX,create->posY,create->typeNum,0,
            create->unitWorking,create->unitWorkingFuture);assert(flag);
        c.orders.clear();c.buildings.tick();
        const int selectedGate=create->posY==map.normalizeY(25+offset)?0:1;
        int passes=0;
        while(!ai.barrier_gate_is_clear(c,ai.strategic_gates[selectedGate]))
        {
            assert(++passes<20);
            ai.manage_land_clearing(c);assert(ai.proactive_clearing_flag==id);
            applyFlagOrders(c,flag);
            assert(flag->unitStayRange<=2);
            assert(flag->clearingResources[CORN] && !flag->clearingResources[WOOD]);
            assert(map.buildingGradient(flag,0));
            // Simulate harvesting only route resources inside the engine radius.
            int harvested=0;
            for(int tile:ai.strategic_gate_routes[ai.strategic_gates[selectedGate][0]])
                if(map.isResource(tile%64,tile/64) && map.warpDistSquare(
                    tile%64,tile/64,flag->posX,flag->posY)<=flag->unitStayRange*flag->unitStayRange)
                {map.setNoResource(tile%64,tile/64,0);++harvested;}
            assert(harvested>0);
        }
        // One usable route ends the emergency. The remaining costly route is
        // still deferred during recovery, rather than keeping the flag staffed.
        ai.manage_land_clearing(c);assert(ai.proactive_clearing_flag==-1);
        assert(!ai.barrier_gate_is_clear(c,ai.strategic_gates[1-selectedGate]));
        // A physical/permanent obstruction cannot be promised as clearable.
        const int tile=ai.strategic_gate_routes[ai.strategic_gates[selectedGate][0]].front();
        map.setResource(tile%64,tile/64,STONE,1);
        assert(!ai.barrier_gate_is_resource_clearable(c,ai.strategic_gates[selectedGate]));
        ai.budget.farming_barrier_enabled=false;ai.update_barrier_topology(c);
        assert(ai.strategic_gate_routes.empty());
    }
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
                    map.addForbidden(cell.x,cell.y,0);
                }
                if(add->areaType==ClearingArea)
                {
                    changes+=!map.isClearArea(cell.x,cell.y,f.player.team->me);
                    map.addClearArea(cell.x,cell.y,0);
                }
            }
        if(auto remove=std::dynamic_pointer_cast<Management::RemoveArea>(order))
            for(const auto& cell:remove->locations)
            {
                if(remove->areaType==ForbiddenArea)
                {
                    changes+=map.isForbidden(cell.x,cell.y,f.player.team->me);
                    map.removeForbidden(cell.x,cell.y,0);
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
    b.farming_barrier_enabled=false;
    b.farming_coastal_porosity_enabled=false;
    b.farming_wheat_fertility_min=3276;
    b.farming_minimum_wood_fertility=3276;
    b.farming_wood_firebreak_enabled=false;
}

void alignedPatternTransitions()
{
    for(int resource:{CORN,WOOD}) for(int offset:{0,42})
    {
        Fixture f;auto& ai=*f.ai;Map& map=f.game.map;
        auto index=[&](int x,int y){return map.normalizeY(y+offset)*64+map.normalizeX(x+offset);};
        for(int y=0;y<64;++y)for(int x=0;x<12;++x)
        {int i=index(x,y);map.getTile(i%64,i/64).terrain=256;}
        for(int y=18;y<=26;++y)for(int x=18;x<=26;++x)
        {int i=index(x,y);map.setResource(i%64,i/64,resource,1);}
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
            map.setResource(hole%64,hole/64,resource,1);
            ai.update_farming(ai.context);applyAreaContracts(f);
            assert(ai.farm_protection_mask==filled);
        }
        // A hard gate still overrides a protected interior seed.
        ai.emergency_escape_mask[seed]=1;
        ai.update_farming(ai.context);applyAreaContracts(f);
        assert(!ai.farm_protection_mask[seed]);
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
        map.setResource(wheat%64,wheat/64,CORN,1);
        map.setResource(wood%64,wood/64,WOOD,1);
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

void gateObstructionContracts()
{
    Fixture f;auto& ai=*f.ai;auto& c=ai.context;Map& map=f.game.map;
    const std::vector<int> gate={20*64+20,20*64+19,20*64+21};
    const int approach=21*64+20;
    ai.strategic_gate_routes[gate[0]]={gate[0],gate[1],gate[2],approach};
    assert(ai.barrier_gate_is_clear(c,gate));
    assert(!ai.barrier_gate_is_resource_clearable(c,gate));
    assert(!ai.barrier_gate_is_clear(c,{gate[0]}));
    for(int resource:{WOOD,CORN,STONE})
    {
        map.setResource(20,21,resource,1);
        assert(!ai.barrier_gate_is_clear(c,gate));
        assert(ai.barrier_gate_is_resource_clearable(c,gate)==(resource!=STONE));
    }
    map.setNoResource(20,21,0);
    map.getTile(20,21).terrain=256;
    assert(!ai.barrier_gate_is_clear(c,gate));
    assert(!ai.barrier_gate_is_resource_clearable(c,gate));
    map.getTile(20,21).terrain=0;
    map.getTile(20,21).building=Building::MAX_COUNT-1;
    assert(!ai.barrier_gate_is_clear(c,gate));
    assert(!ai.barrier_gate_is_resource_clearable(c,gate));
    map.getTile(20,21).building=NOGBID;
    Unit* worker=f.game.addUnit(20,21,0,WORKER,0,0,0,0);assert(worker);
    assert(ai.barrier_gate_is_clear(c,gate));
    auto* request=new Construction::BuildingOrder(IntBuildingType::CLEARING_FLAG,0);
    request->add_constraint(new Construction::SinglePosition(20,21));
    c.add_building_order(request);
    const auto create=createOrder(c);
    Building* flag=f.game.addBuilding(create->posX,create->posY,create->typeNum,0,0,0);
    assert(flag && flag->type->isVirtual);
    map.getTile(20,21).building=flag->gid;
    assert(ai.barrier_gate_is_clear(c,gate));
    // A resource can coexist with a virtual flag even though the map editor's
    // painting helper refuses occupied cells.
    map.getTile(20,21).resource.type=WOOD;
    map.getTile(20,21).resource.amount=1;
    assert(ai.barrier_gate_is_resource_clearable(c,gate));
    map.getTile(20,21).building=NOGBID;
}


void archipelagoHarvestDoesNotSealWheat()
{
    Game game(NULL);
    BinaryInputStream input(new FileStreamBackend(fopen("maps/Archipelago.map","rb")));
    assert(game.load(&input));
    Player player;player.setTeam(game.teams[0]);
    AIMaxima::Maxima ai(&player);ai.context.initialize();
    ai.budget.farming_enabled=true;ai.budget.farming_protection_enabled=true;
    ai.budget.farming_coastal_porosity_enabled=true;
    ai.budget.farming_wheat_fertility_min=3276;
    for(int round=0;round<8;++round) {
        if(round%2)game.map.setNoResource(48,97,1);
        else game.map.setResource(48,97,CORN,1);
        ai.update_farming(ai.context);
        for(int patch=0;patch<2;++patch) {
            int wheat=0,open=0;
            for(int y=patch?103:95;y<=(patch?106:99);++y)
                for(int x=patch?55:48;x<=(patch?61:53);++x)
                    if(game.map.isResourceTakeable(x,y,CORN)) {
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
        ai.budget.farming_coastal_porosity_enabled=true;
        ai.budget.farming_wheat_fertility_min=3276;
        ai.budget.farming_barrier_enabled=true;
        ai.budget.farming_economic_envelope_radius=20;
        const int w=game.map.getW(),h=game.map.getH();
        ai.update_farming(ai.context);
        std::vector<int> seeds;
        for(int y=1;y<h;y+=2)for(int x=1;x<w;x+=2)
            if(game.map.isResourceTakeable(x,y,CORN)&&ai.farm_protection_mask[y*w+x])
                seeds.push_back(y*w+x);
        assert(!seeds.empty());
        // Adversarial harvest: remove every available wheat tile each round.
        // Grow new neighbors between rounds to change patch boundaries and
        // exercise the exact feedback loop that destroyed Holiday's left farm.
        for(int round=0;round<12;++round) {
            for(int y=0;y<h;++y)for(int x=0;x<w;++x)
                if(game.map.isResourceTakeable(x,y,CORN)&&!ai.farm_protection_mask[y*w+x])
                    game.map.setNoResource(x,y,1);
            for(int seed:seeds) {
                int x=game.map.normalizeX(seed%w+(round%3)-1);
                int y=game.map.normalizeY(seed/w+((round/3)%3)-1);
                const Tile& c=game.map.getTile(x,y);
                if(c.terrain<16&&c.building==NOGBID&&c.resource.type==NO_RES_TYPE)
                    game.map.setResource(x,y,CORN,1);
            }
            ai.timer+=64;ai.update_farming(ai.context);
            for(int seed:seeds) {
                assert(game.map.isResourceTakeable(seed%w,seed/w,CORN));
                assert(ai.farm_protection_mask[seed]);
            }
        }
        std::cout<<"stable seed cycles: "<<name<<" seeds="<<seeds.size()<<" rounds=12\n";
    }
}

void farmingIgnoresDiscovery()
{
    Game game(NULL);
    BinaryInputStream input(new FileStreamBackend(fopen("maps/Archipelago.map","rb")));
    assert(game.load(&input));
    Player player;player.setTeam(game.teams[0]);
    AIMaxima::Maxima ai(&player);ai.context.initialize();
    ai.budget.farming_enabled=true;ai.budget.farming_protection_enabled=true;
    ai.budget.farming_barrier_enabled=true;ai.budget.farming_coastal_porosity_enabled=true;
    ai.budget.farming_economic_envelope_radius=20;
    for(int y=0;y<game.map.getH();++y)for(int x=0;x<game.map.getW();++x)
        game.map.setMapDiscovered(x,y,player.team->me);
    ai.update_farming(ai.context);
    const auto full=ai.farm_protection_mask;
    const auto fullGates=ai.strategic_gates;
    const auto signature=ai.compute_farming_topology_signature(ai.context);
    assert(std::count(full.begin(),full.end(),Uint8(1))>0);
    game.map.unsetMapDiscovered();
    for(int y=80;y<96;++y)for(int x=24;x<40;++x)
        game.map.setMapDiscovered(x,y,player.team->me);
    assert(ai.compute_farming_topology_signature(ai.context)==signature);
    ai.update_barrier_topology(ai.context);
    ai.update_farming(ai.context);
    assert(ai.farm_protection_mask==full);
    assert(ai.strategic_gates==fullGates);
}

void strategicWideBeach()
{
    for(int offset:{0,50})
    {
        Fixture f;auto& ai=*f.ai;Map& map=f.game.map;
        auto index=[&](int x,int y){return map.normalizeY(y+offset)*64+map.normalizeX(x+offset);};
        for(int y=0;y<64;++y)for(int x=0;x<=10;++x)
        {int i=index(x,y);map.getTile(i%64,i/64).terrain=x<6?256:128;}
        for(int y=25;y<=27;++y)for(int x=25;x<=27;++x)
        {int i=index(x,y);map.getTile(i%64,i/64).terrain=128;}
        int home=index(18,30);
        assert(f.game.addBuilding(home%64,home/64,
            globalContainer->buildingsTypes.getTypeNum("inn",0,false),0));
        ai.budget.farming_enabled=true;ai.budget.farming_barrier_enabled=true;
        ai.budget.farming_economic_envelope_radius=20;
        ai.initialize_farming_cache(ai.context);
        ai.update_barrier_topology(ai.context);
        assert(ai.strategic_gates.size()==2);
        std::set<int> gates;
        for(const auto& gate:ai.strategic_gates) {
            assert(gate.size()==3);
            for(int i:gate) {
                assert(gates.insert(i).second);
                assert(i%64==map.normalizeX(11+offset));
                assert(!ai.strategic_barrier_mask[i]);
            }
        }
        assert(std::count(ai.strategic_barrier_mask.begin(),
            ai.strategic_barrier_mask.end(),Uint8(1))>0);
        // Exercise the complete protection pipeline, not merely topology.
        // A selected shoreline without a farm must remain open.
        const auto selected=ai.strategic_barrier_mask;
        ai.budget.farming_protection_enabled=true;
        ai.budget.farming_coastal_porosity_enabled=true;
        ai.update_farming(ai.context);
        ai.context.update_management_orders();
        f.game.gameHeader.setNumberOfPlayers(1);
        f.game.players[0]=&f.player;
        for(auto order:ai.context.orders) {order->sender=0;f.game.executeOrder(order,0);}
        f.game.players[0]=NULL;
        ai.context.orders.clear();
        for(int i=0;i<64*64;++i) {
            if(selected[i] && !ai.has_hard_farming_contract(i)) {
                assert(!ai.farm_protection_mask[i]);
                assert(!(map.getTile(i%64,i/64).forbidden & f.player.team->me));
            }
            if(ai.strategic_gate_mask[i] || ai.emergency_escape_mask[i]) {
                assert(!ai.farm_protection_mask[i]);
                assert(!(map.getTile(i%64,i/64).forbidden & f.player.team->me));
            }
        }
        for(int y=24;y<=28;++y)for(int x=24;x<=28;++x)
            assert(!ai.strategic_barrier_mask[index(x,y)]);
    }
    Game game(NULL);
    GAGCore::BinaryInputStream input(new GAGCore::FileStreamBackend(
        fopen("maps/Holiday_Island_2.map","rb")));
    assert(game.load(&input));
    Player player;player.setTeam(game.teams[0]);
    AIMaxima::Maxima ai(&player);ai.context.initialize();
    for(int y=0;y<game.map.getH();++y)for(int x=0;x<game.map.getW();++x)
        game.map.setMapDiscovered(x,y,player.team->me);
    ai.budget.farming_enabled=true;ai.budget.farming_barrier_enabled=true;
    ai.budget.farming_economic_envelope_radius=20;
    ai.initialize_farming_cache(ai.context);
    ai.update_barrier_topology(ai.context);
    assert(ai.strategic_gates.size()==2);
    std::cout<<"Holiday Island: barrier tiles="<<std::count(
        ai.strategic_barrier_mask.begin(),ai.strategic_barrier_mask.end(),Uint8(1))
        <<" gates="<<ai.strategic_gates.size()<<"\n";
}

void bigPondBarrierProtection()
{
    Game game(NULL);
    FILE* file=fopen("maps/A_big_pond.map","rb");assert(file);
    GAGCore::BinaryInputStream input(new GAGCore::FileStreamBackend(file));
    assert(game.load(&input));
    Player player;player.setTeam(game.teams[0]);
    AIMaxima::Maxima ai(&player);auto& c=ai.context;c.initialize();
    Map& map=game.map;const int w=map.getW(),h=map.getH();
    for(int y=0;y<h;++y)for(int x=0;x<w;++x)map.setMapDiscovered(x,y,player.team->me);
    ai.budget.farming_enabled=true;ai.budget.farming_protection_enabled=true;
    ai.budget.farming_barrier_enabled=true;
    ai.budget.farming_coastal_porosity_enabled=false;
    ai.budget.farming_maintenance_clearing_enabled=false;
    ai.budget.farming_wheat_fertility_min=3276;
    ai.budget.farming_minimum_wood_fertility=65536u*20/100;
    ai.budget.farming_wood_firebreak_enabled=false;
    ai.update_farming(c);
    // Independently label sand components that touch water, then check every
    // eligible stocked coast cell, rather than only one favorable strip.
    std::vector<Uint8> coastal(w*h,0);
    std::vector<int> queue;
    for(int y=0;y<h;++y)for(int x=0;x<w;++x)if(map.isWater(x,y))
    {coastal[y*w+x]=1;queue.push_back(y*w+x);}
    for(size_t head=0;head<queue.size();++head)
    {
        int x=queue[head]%w,y=queue[head]/w;
        for(int dy=-1;dy<=1;++dy)for(int dx=-1;dx<=1;++dx)
        {
            int nx=map.normalizeX(x+dx),ny=map.normalizeY(y+dy),i=ny*w+nx;
            if(!coastal[i]&&map.hasSand(nx,ny)){coastal[i]=1;queue.push_back(i);}
        }
    }
    const auto cached_shore=ai.farming_shoreline_mask;
    const auto* cached_shore_storage=ai.farming_shoreline_mask.data();
    std::vector<int> whole_coast;
    for(int y=0;y<h;++y)for(int x=0;x<w;++x)
    {
        const int i=y*w+x;const Tile& cell=map.getTile(x,y);
        if(!map.isGrass(x,y)||cell.resource.amount==0
            ||(cell.resource.type!=CORN&&cell.resource.type!=WOOD)
            ||ai.has_hard_farming_contract(i))continue;
        const Uint32 minimum=cell.resource.type==CORN?3276:65536u*20/100;
        if(ai.fertility_cache.at(x,y)<minimum)continue;
        bool beach=false;
        for(int dy=-1;dy<=1;++dy)for(int dx=-1;dx<=1;++dx)
        {
            const int nx=map.normalizeX(x+dx),ny=map.normalizeY(y+dy);
            beach=beach||(map.hasSand(nx,ny)&&coastal[ny*w+nx]);
        }
        if(beach){assert(ai.farm_protection_mask[i]);whole_coast.push_back(i);}
    }
    assert(whole_coast.size()>100);
    // Simulate harvesting behind the physical coast. Keep coastal crops and
    // their terrain, but remove neighboring interior crops from both types.
    for(int y=0;y<h;++y)for(int x=0;x<w;++x)
    {
        if(std::find(whole_coast.begin(),whole_coast.end(),y*w+x)!=whole_coast.end())continue;
        if(map.getTile(x,y).resource.type==CORN||map.getTile(x,y).resource.type==WOOD)
            map.setNoResource(x,y,0);
    }
    ai.context.get_gradient_manager().invalidate();
    ai.update_farming(c);
    for(int i:whole_coast)assert(ai.farm_protection_mask[i]);
    assert(ai.farming_shoreline_mask==cached_shore);
    assert(ai.farming_shoreline_mask.data()==cached_shore_storage);
    std::cout<<"A big pond: "<<whole_coast.size()
        <<" eligible coastal resources protected before and after interior harvesting\n";
    // This map has a wide beach: the farm edge need not touch a sand tile
    // that itself touches water. Check the actual mixed-crop strip at x=75.
    std::vector<int> shore_resources;
    for(int y=4;y<=13;++y)
    {
        const int index=y*w+75;
        assert(map.getTile(75,y).resource.type==CORN);
        assert(ai.fertility_cache.at(75,y)>=3276);
        assert(ai.farm_protection_mask[index]);
        shore_resources.push_back(index);
    }
    ai.budget.farming_coastal_porosity_enabled=true;
    ai.update_farming(c);
    int retained=0;
    for(int index:shore_resources)retained+=ai.farm_protection_mask[index]!=0;
    // Access elsewhere on this island no longer excuses enclosed pockets.
    // Their enclosing contour may become porous, while retaining crop seeds.
    assert(retained>0);
    const auto first=ai.farm_protection_mask;
    ai.update_farming(c);assert(ai.farm_protection_mask==first);
    std::cout<<"A big pond: mixed shoreline strip protected "<<retained
        <<"/"<<shore_resources.size()<<", stable with coastal porosity enabled\n";
}

int main()
{
    GlobalContainer container;globalContainer=&container;container.runNoX=true;
    container.buildingsTypes.init();
    IntBuildingType::init();

    firebreakManagementRadius();farmManagementRadius();coastalWheatCrossesFertilityDips();shorelineRequiresWater();interiorHolesAreNotOuterEdges();
    seedSurvival();initialFlagStaffing();approachClearing();
    alignedPatternTransitions();maintenanceProtectionAgreement();gateObstructionContracts();
    wideBeachSurvivesHarvestedInterior();
    archipelagoHarvestDoesNotSealWheat();seedStabilityAcrossMaps();farmingIgnoresDiscovery();strategicWideBeach();bigPondBarrierProtection();
    std::cout<<"aligned farming, maintenance agreement, gate contracts, seed survival and flag staffing passed\n";
}
