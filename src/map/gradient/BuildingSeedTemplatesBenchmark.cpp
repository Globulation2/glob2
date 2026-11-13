// SPDX-License-Identifier: GPL-3.0-or-later
// Opt-in timings of building seed templates against the full seed scan.
#include "HeadlessGlobals.h"
#include "gradient/MapRouting.h"
#include "HeadlessGame.h"
#include "Building.h"
#include "BuildingSeedAudit.h"
#include "BuildingSeedTemplates.h"
#include "SeedCells.h"
#include "SnapshotGradient.h"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <vector>

TEST_SUITE("BuildingGradientBenchmark")
{
// Cold building seeds alone, full scan against templates, plus the cost of a
// full template rebuild. Each template digest must equal the scan digest.
TEST_CASE("building seed templates against the full scan [benchmark][pathfinding]")
{
    glob2test::HeadlessGlobals globals;
    using Clock=std::chrono::steady_clock;
    // The test runner's audit would also scan behind every template seed.
    const bool audit=building_seed::auditEnabled();
    building_seed::setAuditEnabled(false);
    struct Restore { bool value; ~Restore(){ building_seed::setAuditEnabled(value); } } restore{audit};
    std::printf("building_seed,width,route,swim,path,repeat,iterations,ns,digest\n");
    for (int shift : {7,9})
    {
        glob2test::HeadlessGame world({.wDec=shift,.hDec=shift,.teams=2,.discovered=true,
            .clearImmobile=true,.loadDefaultRace=true,.header=true});
        auto& map=world.game.map;
        const int width=map.getW();
        const size_t size=size_t(width)*width;
        // Mixed terrain and deposits keep the scan's branches honest.
        for(int y=0;y<width;++y) for(int x=0;x<width;++x)
        {
            const unsigned h=unsigned(x*7+y*13+(x*y)%5);
            if(h%9==0) map.setVertexTerrain(x,y,WATER); else if(h%7==0) map.setVertexTerrain(x,y,SAND);
            if(h%17==0) map.replaceResource(x,y,Resource{WOOD,0,1,0});
            if(h%29==0) map.addForbidden(x,y,0);
        }
        std::vector<Building*> buildings{world.addBuilding("inn",4,4),world.addBuilding("clearingflag",16,16),
            world.addBuilding("warflag",20,20),world.addBuilding("inn",40,40,0,1)};
        for(auto* b:buildings){ b->unitStayRange=8; b->setClearsMaterial(WOOD,true); }
        const auto plain=map.stateView();
        {
            const auto started=Clock::now();
            BuildingSeedTemplates rebuild;
            rebuild.ensure(plain);
            const auto ns=std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now()-started).count();
            std::printf("building_seed,%d,-1,-1,rebuild,0,1,%lld,0\n",width,static_cast<long long>(ns));
        }
        auto withTemplates=plain;
        withTemplates.buildingSeeds=&map.routing().buildingSeedTemplates();
        std::vector<Uint16> out(size);
        const int iterations=std::max(8,int(1048576/size));
        const auto run=[size](auto fn) { fn(0,size); };
        for(int route=0;route<3;++route) for(int swim : {0,3,AIR_MOVEMENT_CLASS})
        {
            const auto seed=gradient_preparation::captureBuildingSeed(*buildings[size_t(route)],swim,BuildingRoute(route));
            std::uint64_t scanDigest=0;
            for(int path=0;path<2;++path)
            {
                const auto& view=path ? withTemplates : plain;
                for(int repeat=0;repeat<11;++repeat)
                {
                    const auto started=Clock::now();
                    for(int iteration=0;iteration<iterations;++iteration) gradient_preparation::buildingCells(view,seed,out.data(),run);
                    const auto ns=std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now()-started).count();
                    std::uint64_t digest=1469598103934665603ULL;
                    for(size_t i=0;i<size;++i) digest=(digest^out[i])*1099511628211ULL;
                    std::printf("building_seed,%d,%d,%d,%s,%d,%d,%lld,%llu\n",width,route,swim,path?"template":"scan",repeat,
                        iterations,static_cast<long long>(ns),static_cast<unsigned long long>(digest));
                    if(!path) scanDigest=digest;
                    else CHECK(digest==scanDigest);
                }
            }
        }
    }
}
}
