// SPDX-License-Identifier: GPL-3.0-or-later
// Opt-in timings of the actual simulation preparation kernels. This source also
// compiles unchanged against the pre-catalog engine for paired measurements.
#include "EngineFixtures.h"
#include "BuildingType.h"
#include "BuildingGradientSearch.h"
#include "Version.h"
#include "MapInternal.h"
#include "GlobalContainer.h"
#include <chrono>
#include <cstdio>
#include <vector>
#include <cstdlib>

TEST_SUITE("BuildingGradientBenchmark")
{
TEST_CASE("building and resource preparation kernels [benchmark][pathfinding]")
{
    glob2test::HeadlessGlobals globals;
    using Clock=std::chrono::steady_clock;
    std::printf("building_kernel,width,kind,swim,repeat,iterations,ns,digest\n");
    for (int shift : {5,7,9})
    {
        ExperimentSet experiments; experiments.set(ExperimentId::MarketsV2);
        glob2test::HeadlessGame world({.wDec=shift,.hDec=shift,.discovered=true,
            .loadDefaultRace=true,.header=true,.experiments=experiments});
        auto& map=world.game.map;
        const int width=map.getW(), size=width*width;
        auto* inn=world.addBuilding("inn",4,4);
        auto* clear=world.game.addBuilding(16,16,globalContainer->buildingsTypes.getTypeNum("clearingflag",0,false),0);
        REQUIRE(clear);
        auto* combat=world.game.addBuilding(20,20,globalContainer->buildingsTypes.getTypeNum("warflag",0,false),0);
        REQUIRE(combat);
        auto* supplier=world.addBuilding("market",24,24,1);
        supplier->resources[WHEAT]=supplier->type->maxResource[WHEAT];
        for(int y=2;y<width;y+=13) for(int x=2;x<width;x+=11)
            if(map.getBuilding(x,y)==NOGBID) map.setResource(x,y,WHEAT,5);
        std::vector<Uint16> resource(size);
        const int iterations=std::max(8,1048576/size);
        for(int swim : {0,3}) for(int kind=0;kind<5;++kind)
        {
            auto* building=kind==0 ? inn : kind==1 ? clear : combat;
            if(kind<3) REQUIRE(map.buildingGradient(building,swim));
            const auto prepare=[&] {
                if(kind<3) map.updateGlobalGradient(building,swim);
                else map.seedResourcesGradient(0,WHEAT,swim,resource.data(),kind==4);
            };
            prepare();
            for(int repeat=0;repeat<11;++repeat)
            {
                const auto started=Clock::now();
                for(int iteration=0;iteration<iterations;++iteration) prepare();
                const auto ns=std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now()-started).count();
                // Read outside the timed region so a compiler cannot discard work.
                const Uint16* field=kind<3 ? map.buildingGradient(building,swim) : resource.data();
                std::uint64_t digest=1469598103934665603ULL;
                for(int i=0;i<size;++i) digest=(digest^field[i])*1099511628211ULL;
                std::printf("building_kernel,%d,%d,%d,%d,%d,%lld,%llu\n",width,kind,swim,repeat,
                    iterations,static_cast<long long>(ns),static_cast<unsigned long long>(digest));
            }
        }
    }
}
TEST_CASE("fixed stock simulation without AI decisions [benchmark]")
{
    glob2test::HeadlessGlobals globals;
    using Clock=std::chrono::steady_clock;
    std::printf("building_sim,repeat,ticks,ns,units,buildings,health,stored\n");
    const bool trace = std::getenv("GLOB2_BENCHMARK_TRACE") != nullptr;
    for(int repeat=0;repeat<(trace ? 1 : 11);++repeat)
    {
        glob2test::HeadlessGame world({.wDec=7,.hDec=7,.teams=2,.discovered=true,
            .clearImmobile=true,.loadDefaultRace=true,.header=true,.seed=713});
        for(int team=0;team<2;++team)
        {
            const int offset=team*64;
            for(int column=0;column<3;++column)
                for(const char* name : {"swarm","inn","hospital","defencetower"})
                {
                    const int row=std::string(name)=="swarm" ? 0 : std::string(name)=="inn" ? 1 : std::string(name)=="hospital" ? 2 : 3;
                    auto* building=world.addBuilding(name,offset+4+column*12,4+row*9,0,team);
                    for(int resource=0;resource<MAX_RESOURCES;++resource)
                        building->resources[resource]=building->type->maxResource[resource];
                }
            for(int unit=0;unit<96;++unit)
                world.addUnit(unit<64 ? WORKER : unit<88 ? WARRIOR : EXPLORER,
                    offset+3+unit%24,44+unit/24,team);
            world.game.teams[team]->createLists();
        }
        for(int resource=0;resource<MAX_RESOURCES;++resource)
            for(int y=75;y<100;++y) for(int x=4+resource*15;x<13+resource*15;++x)
                world.game.map.setResource(x,y,resource,5);
        world.game.setWaitingOnMask(0);
        // Optional diagnostics are separate from accepted timing runs. Report
        // real birth/death boundaries when comparing evolving workloads.
        std::vector<bool> present(2*Unit::MAX_COUNT);
        for(int team=0;team<2;++team)for(int id=0;id<Unit::MAX_COUNT;++id)
            present[team*Unit::MAX_COUNT+id]=world.game.teams[team]->myUnits[id]!=nullptr;
        auto advance=[&](int ticks) {
            if(!trace){world.step(ticks);return;}
            for(int tick=0;tick<ticks;++tick) {
                world.step();
                if(const auto* watched=world.game.teams[1]->myUnits[88])
                    std::printf("building_unit,%u,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d\n",
                        world.game.stepCounter,watched->posX,watched->posY,watched->hp,watched->hungry,
                        int(watched->activity),int(watched->displacement),int(watched->action),watched->delta,
                        watched->attachedBuilding?int(watched->attachedBuilding->gid):-1,
                        watched->targetBuilding?int(watched->targetBuilding->gid):-1);
                for(int team=0;team<2;++team)for(int id=0;id<Unit::MAX_COUNT;++id) {
                    const auto* unit=world.game.teams[team]->myUnits[id];
                    const int index=team*Unit::MAX_COUNT+id;
                    if(present[index]!=(unit!=nullptr)) {
                        std::printf("building_event,%u,%d,%d,%s,%d\n",world.game.stepCounter,team,id,
                            unit?"birth":"death",unit?unit->typeNum:-1);
                        present[index]=unit!=nullptr;
                    }
                }
            }
        };
        advance(512);
        const auto started=Clock::now(); advance(4096);
        const auto ns=std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now()-started).count();
        int units=0,buildings=0;std::int64_t health=0,stored=0;
        for(int team=0;team<2;++team)
        {
            for(int id=0;id<Unit::MAX_COUNT;++id)
                if(auto* unit=world.game.teams[team]->myUnits[id]) {++units;health+=unit->hp;}
            for(int id=0;id<Building::MAX_COUNT;++id)
                if(auto* building=world.game.teams[team]->myBuildings[id])
                {
                    ++buildings;health+=building->hp;
                    for(int resource=0;resource<MAX_RESOURCES;++resource) stored+=building->resources[resource];
                }
        }
        std::printf("building_sim,%d,4096,%lld,%d,%d,%lld,%lld\n",repeat,
            static_cast<long long>(ns),units,buildings,static_cast<long long>(health),static_cast<long long>(stored));
    }
}

TEST_CASE("footprint preparation diagnostic phases [benchmark][pathfinding]")
{
    glob2test::HeadlessGlobals globals;
    using Clock=std::chrono::steady_clock;
    std::printf("footprint_phase,width,phase,swim,repeat,iterations,ns,digest\n");
    for (int shift : {5,7,9})
    {
        ExperimentSet experiments; experiments.set(ExperimentId::MarketsV2);
        glob2test::HeadlessGame world({.wDec=shift,.hDec=shift,.discovered=true,
            .loadDefaultRace=true,.header=true,.experiments=experiments});
        auto& map=world.game.map;
        const int width=map.getW(), size=width*width;
        auto* inn=world.addBuilding("inn",4,4);
        REQUIRE(world.game.addBuilding(16,16,globalContainer->buildingsTypes.getTypeNum("clearingflag",0,false),0));
        REQUIRE(world.game.addBuilding(20,20,globalContainer->buildingsTypes.getTypeNum("warflag",0,false),0));
        auto* supplier=world.addBuilding("market",24,24,1);
        supplier->resources[WHEAT]=supplier->type->maxResource[WHEAT];
        for(int y=2;y<width;y+=13) for(int x=2;x<width;x+=11)
            if(map.getBuilding(x,y)==NOGBID) map.setResource(x,y,WHEAT,5);
        const int iterations=std::max(8,1048576/size);
        for(int swim : {0,3})
        {
            REQUIRE(map.buildingGradient(inn,swim));
            Uint16* gradient=inn->globalGradient[swim];
            const Uint16 gid=inn->gid;
            const Uint32 teamMask=inn->owner->me;
            BuildingGradientSearch search;
            const auto seed=[&] {
                map.initializeGradientCells([&](size_t begin,size_t end) {
                    for(size_t i=begin;i<end;++i) {
                        const auto& tile=map.getTile(i);
                        if(tile.building!=NOGBID)
                            gradient[i]=tile.building==gid ? GRADIENT_AT_GOAL : GRADIENT_FORBIDDEN;
                        else if((tile.forbidden&teamMask) || tile.resource.type!=NO_RES_TYPE ||
                            map.isImmobileUnit(i & map.wMask, i >> map.wDec) ||
                            (!map.terrainPropertiesAt(i).walkable && !(swim>0 && map.terrainPropertiesAt(i).swimmable)))
                            gradient[i]=GRADIENT_FORBIDDEN;
                        else gradient[i]=GRADIENT_UNREACHABLE;
                    }
                });
            };
            for(int phase=0;phase<4;++phase) {
                seed();
                const auto prepare=[&] {
                    if(phase==0) map.updateGlobalGradient(inn,swim);
                    else if(phase==1) {
#if VERSION_MINOR >= 137
                        map.updateGlobalGradient(inn,swim,BuildingRoute::Footprint);
#else
                        map.updateGlobalGradient(inn,swim);
#endif
                    }
                    else if(phase==2) seed();
                    else search.begin(map,gradient,swim);
                };
                prepare();
                for(int repeat=0;repeat<11;++repeat) {
                    const auto started=Clock::now();
                    for(int i=0;i<iterations;++i) prepare();
                    const auto ns=std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now()-started).count();
                    std::uint64_t digest=1469598103934665603ULL;
                    for(int i=0;i<size;++i) digest=(digest^gradient[i])*1099511628211ULL;
                    std::printf("footprint_phase,%d,%d,%d,%d,%d,%lld,%llu\n",width,phase,swim,repeat,
                        iterations,static_cast<long long>(ns),static_cast<unsigned long long>(digest));
                }
            }
        }
    }
}

}
