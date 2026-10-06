// SPDX-License-Identifier: GPL-3.0-or-later
#include "EngineFixtures.h"
#include "BuildingType.h"
#include <nlohmann/json.hpp>
#include <chrono>
#include <array>
#include <cstdlib>
#include <cstdio>

namespace {
using Clock = std::chrono::steady_clock;
long long elapsed(Clock::time_point start)
{
    return std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now()-start).count();
}
}
TEST_SUITE("BuildingCatalogBenchmark")
{
TEST_CASE("unused definitions do not enlarge simulation work [benchmark]")
{
    glob2test::HeadlessGlobals globals;
    BuildingsTypes stock; stock.initLegacy();
    const auto original=nlohmann::json::parse(stock.snapshotJson());
    std::printf("catalog_scale,definitions,repeat,setup_ns,ticks,tick_ns,units\n");
    std::vector<Uint32> referenceInstances;
    const char* reverse=std::getenv("GLOB2_CATALOG_BENCHMARK_REVERSE");
    const auto counts=reverse && *reverse=='1'
        ? std::array<int,3>{1024,256,55} : std::array<int,3>{55,256,1024};
    for (int count : counts)
    {
        auto catalog=original;
        auto dormant=original["variants"][stock.getTypeNum("stonewall",0,false)];
        dormant["previous"]=""; dormant["next"]="";
        dormant["properties"]["type"]="";
        dormant["semantics"]["repairable"]=false;
        dormant["semantics"]["placeable"]=false;
        for (int id=55;id<count;++id)
        {
            dormant["id"]=id; dormant["key"]="dormant-"+std::to_string(id);
            catalog["variants"].push_back(dormant);
        }
        const auto json=catalog.dump();
        for (int repeat=0;repeat<11;++repeat)
        {
            glob2test::HeadlessGame world({.wDec=7,.hDec=7,.discovered=true,
                .loadDefaultRace=true,.header=true,.seed=713});
            auto start=Clock::now();
            world.game.buildingsTypes.loadSnapshotJson(json); world.game.configureBuildingCatalog();
            const auto setup=elapsed(start);
            for (int n=0;n<8;++n)
            {
                auto* inn=world.addBuilding("inn",4+n*12,4);
                inn->materials[WHEAT]=inn->type->maxMaterial[WHEAT];
            }
            for (int n=0;n<64;++n) world.addUnit(WORKER,4+n%32,12+n/32);
            world.team->createLists(); world.game.setWaitingOnMask(0); world.step(128);
            start=Clock::now(); world.step(2048); const auto duration=elapsed(start);
            // Catalog metadata and catalog-sized statistics intentionally differ;
            // every physical entity must still execute the identical workload.
            std::vector<Uint32> buildings,unitState;
            world.game.checkSum(nullptr,&buildings,&unitState,true);
            buildings.insert(buildings.end(),unitState.begin(),unitState.end());
            if(referenceInstances.empty()) referenceInstances=buildings;
            CHECK(buildings==referenceInstances);
            int units=0; for(int unit=0;unit<Unit::MAX_COUNT;++unit) units+=world.team->myUnits[unit]!=nullptr;
            std::printf("catalog_scale,%d,%d,%lld,2048,%lld,%d\n",count,repeat,setup,duration,units);
        }
    }
}
TEST_CASE("private stock routing measures warm fields invalidation and working sets [benchmark]")
{
    glob2test::HeadlessGlobals globals;
    std::printf("catalog_routes,width,budget_fields,working_fields,repeat,phase,ns,cell_bytes,digest\n");
    for (int shift : {7,9}) for (int multiple : {1,2,4,8})
    {
        glob2test::HeadlessGame world({.wDec=shift,.hDec=shift,.loadDefaultRace=true,.header=true});
        auto catalog=nlohmann::json::parse(world.game.buildingsTypes.snapshotJson());
        const int id=world.game.buildingsTypes.getTypeNum("inn",0,false);
        auto& spec=catalog["variants"][id];
        spec["semantics"]["occupiesGround"]=false;
        auto& market=spec["semantics"]["market"];
        market["suppliesDirectStock"]=true; market["suppliesDirectStockMaterials"]={"wood"};
        market["fetchesDirectStock"]=true; market["fetchesDirectStockMaterials"]={"wood"};
        market["fetchesStock"]=false; market["sharedStock"]=false;
        world.game.buildingsTypes.loadSnapshotJson(catalog.dump()); world.game.configureBuildingCatalog();
        auto& map=world.game.map;
        const int budgetFields=shift==9 ? 128 : 32;
        const int fields=budgetFields*multiple/2;
        const Uint64 fieldBytes=Uint64(map.getW())*map.getH()*sizeof(Uint16);
        map.setMaterialRoutingCacheBudget(fieldBytes*budgetFields);
        std::vector<Building*> consumers;
        for (int n=0;n<fields;++n)
        {
            auto* b=world.addBuilding("inn",(n*7)%map.getW(),(n*13)%map.getH());
            b->materials[WOOD]=10; consumers.push_back(b);
        }
        world.team->createLists();
        for (int repeat=0;repeat<3;++repeat) for (int phase=0;phase<3;++phase)
        {
            // A revision models depletion/replenishment. Warm passes use the
            // same tick so age expiry does not obscure working-set effects.
            if(phase==0 || phase==2) map.dirtyMarketGradients(0,WOOD);
            if(phase==2) consumers.front()->materials[WOOD]=0;
            Uint64 digest=0; const auto start=Clock::now();
            for(auto* consumer : consumers)
            {
                const auto* field=map.getMaterialGradient(0,WOOD,0,false,consumer);
                digest+=field[map.coordToIndex(consumer->posX,consumer->posY)];
            }
            const auto duration=elapsed(start);
            CHECK(map.materialRoutingCacheBytes()<=fieldBytes*budgetFields);
            std::printf("catalog_routes,%d,%d,%d,%d,%d,%lld,%llu,%llu\n",map.getW(),budgetFields,fields,repeat,phase,duration,
                static_cast<unsigned long long>(map.materialRoutingCacheBytes()),static_cast<unsigned long long>(digest));
            if(phase==2) consumers.front()->materials[WOOD]=10;
        }
    }
}
}
