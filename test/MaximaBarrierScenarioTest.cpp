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



#include <fstream>
#include <cstdlib>

// A real-map audit, independent of the planner's gate-validity predicate.
// Clear only the promised channels and ask whether buildings can reach an
// exterior under engine movement rules. Repeat after hostile farm regrowth.
namespace {
std::vector<int> neighbors(Map& map,int i) {
    std::vector<int> result;
    for(int dy=-1;dy<=1;++dy)for(int dx=-1;dx<=1;++dx)if(dx||dy)
        result.push_back(map.normalizeY(i/map.getW()+dy)*map.getW()+map.normalizeX(i%map.getW()+dx));
    return result;
}
std::vector<int> entrances(Map& map,Building* b) {
    std::vector<int> result;
    for(int dy=-1;dy<=b->type->height;++dy)for(int dx=-1;dx<=b->type->width;++dx)
        if(dx==-1||dy==-1||dx==b->type->width||dy==b->type->height)
            result.push_back(map.normalizeY(b->posY+dy)*map.getW()+map.normalizeX(b->posX+dx));
    return result;
}
struct Audit {int buildings=0,trapped=0,eligible=0,reachable=0,gateResources=0,protectedSeeds=0;};
Audit audit(Game& game,Player& player,AIMaxima::Maxima& ai,bool clearContracts,bool engineSnapshot=false) {
    Map& map=game.map;int w=map.getW(),n=w*map.getH();
    std::vector<Uint8> open(n,0),potential(n,0),exterior(n,0);
    Audit a;
    for(int i=0;i<n;++i){const Tile& c=map.getTile(i%w,i/w);
        bool resource=c.resource.type!=NO_RES_TYPE;
        bool permanent=resource&&globalContainer->resourcesTypes.get(c.resource.type)->eternal;
        bool channel=ai.emergency_escape_mask[i]||ai.strategic_gate_mask[i];
        bool blocked=ai.farm_protection_mask[i];
        a.gateResources+=channel&&resource;
        a.protectedSeeds+=blocked&&c.resource.type==CORN&&AIMaxima::Farming::isInteriorSeed(i%w,i/w);
        potential[i]=!map.isWater(i%w,i/w)&&c.building==NOGBID&&!permanent;
        // Protected empty frontier cells are already movement barriers to us;
        // this audit checks the desired friendly mask, not queued engine orders.
        open[i]=engineSnapshot
            ? map.isHardSpaceForGroundUnit(i%w,i/w,false,player.team->me)
            : potential[i]&&!blocked&&(!resource||(clearContracts&&channel));
        exterior[i]=potential[i]&&map.hasSand(i%w,i/w)&&ai.farming_shoreline_mask[i];
    }
    auto reached=[&](const std::vector<Uint8>& allowed){
        std::vector<Uint8> seen(n,0);std::vector<int> q;
        for(int i=0;i<n;++i)if(exterior[i]&&allowed[i]){seen[i]=1;q.push_back(i);}
        for(size_t k=0;k<q.size();++k)for(int j:neighbors(map,q[k]))
            if(allowed[j]&&!seen[j]){seen[j]=1;q.push_back(j);}
        return seen;
    };
    auto actual=reached(open),possible=reached(potential);
    for(int b=0;b<Building::MAX_COUNT;++b){auto* building=player.team->myBuildings[b];
        if(!building||building->type->isVirtual||building->type->isBuildingSite)continue;
        ++a.buildings;bool hasExit=false,canExit=false;
        for(int i:entrances(map,building)){hasExit|=actual[i];canExit|=possible[i];}
        a.eligible+=canExit;a.reachable+=hasExit;a.trapped+=canExit&&!hasExit;
    }
    return a;
}
// A separate enemy-side topology experiment: assume the proposed coastal
// barrier has physically grown, and harvestable interior cells have been cut.
// Enemy warriors ignore our ForbiddenArea, so that mask is deliberately absent.
// Closing all designated mouths reveals coastlines with a natural bypass. Such
// a coastline cannot honestly be called a sealed kill zone, even with a tower.
std::pair<int,int> matureBarrierFunnel(Game& game,Player& player,AIMaxima::Maxima& ai) {
    Map& map=game.map;
    const int w=map.getW(),n=w*map.getH();
    auto flood=[&](bool closeGates) {
        std::vector<Uint8> open(n,0),seen(n,0);
        std::vector<int> queue;
        for(int i=0;i<n;++i) {
            const Tile& c=map.getTile(i%w,i/w);
            const bool permanent=c.resource.type!=NO_RES_TYPE
                &&globalContainer->resourcesTypes.get(c.resource.type)->eternal;
            open[i]=!map.isWater(i%w,i/w)&&c.building==NOGBID&&!permanent
                &&!ai.strategic_barrier_mask[i]&&!(closeGates&&ai.strategic_gate_mask[i]);
            if(open[i]&&map.hasSand(i%w,i/w)&&ai.farming_shoreline_mask[i]) {
                seen[i]=1;queue.push_back(i);
            }
        }
        for(size_t head=0;head<queue.size();++head)
            for(int next:neighbors(map,queue[head]))if(open[next]&&!seen[next]) {
                seen[next]=1;queue.push_back(next);
            }
        return seen;
    };
    const auto opened=flood(false),closed=flood(true);
    int accessible=0,channeled=0;
    for(int id=0;id<Building::MAX_COUNT;++id) {
        Building* b=player.team->myBuildings[id];
        if(!b||b->type->isVirtual||b->type->isBuildingSite)continue;
        bool withGates=false,withoutGates=false;
        for(int tile:entrances(map,b)) {
            withGates|=opened[tile];withoutGates|=closed[tile];
        }
        accessible+=withGates;
        channeled+=withGates&&!withoutGates;
    }
    return {accessible,channeled};
}

// This is a funded placement experiment, not a claim that a struggling AI
// can afford a tower. It isolates whether its real planner chooses a useful
// firing position when the director does authorize one.
std::pair<int,int> towerPlacement(Game& game,Player& player,AIMaxima::Maxima& ai) {
    using namespace AIMaximaPlacement;
    const int w=game.map.getW();
    for(int y=0;y<game.map.getH();++y)for(int x=0;x<w;++x)game.map.setMapDiscovered(x,y,player.team->me);
    ai.configure_development_planner();auto world=ai.collect_development_world(ai.context);
    for(int r=0;r<5;++r)world.accessibleSupplies[r]=1000;
    ai.development_planner.adoptStartingBuildings(world);
    DevelopmentIntent intent;intent.buildingType=IntBuildingType::DEFENSE_BUILDING;
    intent.unmetCount=1;intent.priority=100;intent.workers=1;
    DevelopmentLimits limits;limits.newConstruction=1;
    DevelopmentAction action;
    if(!ai.development_planner.selectAction(world,{intent},limits,action))return {-1,0};
    int covered=0,overlap=0,approachesCovered=0;
    const int range=globalContainer->buildingsTypes.getByType("defencetower",0,false)->shootingRange;
    for(const auto& gate:ai.strategic_gates){bool all=true;
        for(int i:gate){bool hit=false;
            for(int dy=-1;dy<=0;++dy)for(int dx=-1;dx<=0;++dx)
                hit|=game.map.warpDistMax(action.centerX+dx,action.centerY+dy,i%w,i/w)<=range;
            all&=hit;
        }covered+=all;
#ifndef BARRIER_BASELINE
        const auto approach=ai.strategic_gate_approaches.find(gate[0]);
        if(approach!=ai.strategic_gate_approaches.end())for(int i:approach->second) {
            bool hit=false;
            for(int dy=-1;dy<=0;++dy)for(int dx=-1;dx<=0;++dx)
                hit|=game.map.warpDistMax(action.centerX+dx,action.centerY+dy,i%w,i/w)<=range;
            all&=hit;
        }
        approachesCovered+=all;
#endif
    }
    for(int i:action.parcelTiles)overlap+=ai.emergency_escape_mask[i]||ai.strategic_gate_mask[i];
#ifndef BARRIER_BASELINE
    assert(!overlap);
    if(world.tile(action.centerX,action.centerY).gateDefense>0)
        assert(approachesCovered>0);
    // Revalidation must catch a corridor appearing after selection as well.
    if(!action.parcelTiles.empty()){
        auto changed=world;for(int i:action.parcelTiles)changed.tiles[i].gateCorridor=true;
        RejectionReason reason=RejectedTerrain;
        assert(!ai.development_planner.revalidateSelection(changed,{intent},limits,action,&reason));
    }
#endif
    return {covered,overlap};
}

#ifndef BARRIER_BASELINE
// A long branch is not a giant gate mouth. In particular, its first tile can
// coincide with a gate's route key without making the two contracts identical.
void longInternalBranchUsesLocalClearing() {
    Game game(NULL);game.map.setSize(6,6,GRASS);game.map.setGame(&game);
    game.addTeam();game.teams[0]->race.loadDefault();
    Player player;player.setTeam(game.teams[0]);
    for(int y=0;y<64;++y)for(int x=0;x<64;++x)
        game.map.setMapDiscovered(x,y,player.team->me);
    assert(game.addBuilding(8,8,globalContainer->buildingsTypes.getTypeNum("inn",0,false),0));
    AIMaxima::Maxima ai(&player);ai.context.initialize();
    std::vector<int> branch;
    for(int x=30;x>=12;--x)branch.push_back(12*64+x);
    for(int x=15;x<=28;++x)game.map.setResource(x,12,WOOD,8);
    ai.strategic_gate_routes[branch[0]]={branch[0],branch[0]+1,branch[0]+2};
    const auto target=ai.gate_clearing_target(ai.context,branch,true);
    assert(!target.empty());
    for(int tile:target) {
        assert(game.map.getTile(tile%64,tile/64).resource.type==WOOD);
        assert(std::find(branch.begin(),branch.end(),tile)!=branch.end());
        assert(game.map.warpDistMax(target[0]%64,target[0]/64,tile%64,tile/64)<=1);
    }
    // Also exercise the selection boundary: the branch must receive emergency
    // priority and still arrive at the executor as a small local target.
    ai.settlement_access_routes={branch};
    ai.budget.farming_enabled=ai.budget.farming_gate_clearing_enabled=true;
    ai.manage_land_clearing(ai.context);
    assert(ai.proactive_clearing_flag>=0);
}
#endif

void scenario(const char* path,int seat,bool saved,bool strict) {
    Game game(NULL);BinaryInputStream input(new FileStreamBackend(fopen(path,"rb")));assert(game.load(&input));
    Player player;player.setTeam(game.teams[seat]);
    std::unique_ptr<AIMaxima::Maxima> fresh;
    AIMaxima::Maxima* ai=NULL;
    if(saved)for(int p=0;p<game.gameHeader.getNumberOfPlayers();++p)
        if(game.players[p]&&game.players[p]->teamNumber==seat&&game.players[p]->ai)
            ai=dynamic_cast<AIMaxima::Maxima*>(game.players[p]->ai->aiImplementation);
    if(!ai){fresh.reset(new AIMaxima::Maxima(&player));ai=fresh.get();}
    ai->context.initialize();
    if(saved)ai->director.evaluate(*ai,ai->context);
    ai->budget.farming_enabled=ai->budget.farming_protection_enabled=true;
    ai->budget.farming_barrier_enabled=ai->budget.farming_coastal_porosity_enabled=true;
    ai->budget.farming_economic_envelope_radius=20;ai->budget.farming_management_radius=16;
    ai->budget.farming_wheat_fertility_min=3276;ai->budget.farming_minimum_wood_fertility=13107;
    ai->budget.farming_gate_relocation_penalty_cap=4;ai->budget.barrier_topology_interval=500;
    ai->update_farming(ai->context);
    const auto firstGates=ai->strategic_gates;
#ifndef BARRIER_BASELINE
    // Even a full three-wide channel must not spread along the coastal wall.
    // That would create unplanned enemy entrances outside the defended mouths.
    for(int i=0;i<game.map.getW()*game.map.getH();++i)
        assert(!(ai->strategic_barrier_mask[i]&&ai->emergency_escape_mask[i]));
#endif
    const auto funnel=matureBarrierFunnel(game,player,*ai);
    const auto tower=saved?std::make_pair(-1,0):towerPlacement(game,player,*ai);

    const auto actualEngine=audit(game,player,*ai,false,true);
    auto before=audit(game,player,*ai,false),cleared=audit(game,player,*ai,true);
    int changed=0;
    // Regrowth directly into every maintained grass passage must not relocate it.
    for(int i=0;i<game.map.getW()*game.map.getH();++i){auto& c=game.map.getTile(i%game.map.getW(),i/game.map.getW());
        if(ai->emergency_escape_mask[i]&&c.terrain<16&&c.building==NOGBID&&c.resource.type==NO_RES_TYPE)
            game.map.setResource(i%game.map.getW(),i/game.map.getW(),WOOD,8);
    }
    ai->timer+=512;ai->update_farming(ai->context);
    changed=ai->strategic_gates!=firstGates;

    auto regrown=audit(game,player,*ai,true);
    std::cout<<"BARRIER_AUDIT\tmap="<<path<<"\tseat="<<seat
        <<"\tbuildings="<<before.buildings<<"\teligible="<<before.eligible
        <<"\tengine_trapped="<<actualEngine.trapped<<"\ttrapped_before="<<before.trapped<<"\ttrapped_after="<<cleared.trapped
        <<"\ttrapped_regrowth="<<regrown.trapped<<"\tgates="<<firstGates.size()
        <<"\tgate_resources="<<before.gateResources<<"\tgate_changed="<<changed
        <<"\tprotected_seeds="<<before.protectedSeeds
        <<"\tmature_accessible="<<funnel.first<<"\tmature_channeled="<<funnel.second
        <<"\ttower_gate_coverage="<<tower.first<<"\ttower_channel_overlap="<<tower.second<<"\n";
    if(strict){assert(!cleared.trapped);assert(!regrown.trapped);assert(!changed);}
}
}
int main(int argc,char** argv) {
#ifdef BARRIER_BASELINE
    const bool strict=false; // Comparative evidence may record an old failure.
#else
    const bool strict=true; // Every normal candidate run enforces the contract.
#endif
    GlobalContainer container;globalContainer=&container;container.runNoX=true;
    container.buildingsTypes.init();
    IntBuildingType::init();
#ifndef BARRIER_BASELINE
    longInternalBranchUsesLocalClearing();
#endif
    if(argc>=3){scenario(argv[1],std::atoi(argv[2]),argc>3&&std::string(argv[3])=="save",strict);return 0;}
    const char* names[]={"Holiday_Island_2","Archipelago","Isles","Migration","Garden_3","A_big_pond","Wild_River","Sand_River"};
    for(const char* name:names){std::string path=std::string("maps/")+name+".map";
        Game g(NULL);BinaryInputStream input(new FileStreamBackend(fopen(path.c_str(),"rb")));assert(g.load(&input));
        for(int seat=0;seat<g.mapHeader.getNumberOfTeams();++seat)scenario(path.c_str(),seat,false,strict);
    }
}
