// SPDX-License-Identifier: GPL-3.0-or-later
// Opt-in bounded component stress measurements, not a before/after gameplay gate.
#include "EngineFixtures.h"
#include "ResourceRegistry.h"
#include "GradientRuntime.h"
#include <BinaryStream.h>
#include <StreamBackend.h>
#include <nlohmann/json.hpp>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>

namespace {
using Json=nlohmann::json;
using Clock=std::chrono::steady_clock;
long long elapsed(Clock::time_point start) {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now()-start).count();
}
Json catalog(unsigned count, bool multi) {
    Json definitions=Json::array();
    for(unsigned n=0;n<count;++n) {
        Json yields={{"food",{{"capacity",9},{"initial",5},{"consumption","one"}}}};
        if(multi) {
            yields["paper"]={{"capacity",9},{"initial",5},{"consumption","one"}};
            yields["gold"]={{"capacity",9},{"initial",5},{"consumption","one"}};
        }
        definitions.push_back({{"key","stress:crop-"+std::to_string(n)},
            {"properties",{{"ecology","land"},{"blocksGround",false},{"persistsWhenEmpty",true}}},
            {"yields",yields},
            {"presentation",{{"name","Stress crop"},{"sprite","data/gfx/ressource"},
                {"levels",Json::array({{{"stock",0},{"variants",Json::array({{{"frame",5}}})}}})}}}});
    }
    return {{"schemaVersion",1},{"resources",definitions}};
}
}
TEST_SUITE("ResourceRuntimeBenchmark") {
TEST_CASE("large eight-team sparse material and runtime catalog stress [benchmark][resources]") {
    glob2test::HeadlessGlobals globals;
    Json report={{"schema",1},{"kind","component-stress"},{"seed",713},
        {"limitation","Candidate component measurements only; no legacy performance or behavioral equivalence claim."},
        {"cases",Json::array()}};
    const char* destination=std::getenv("GLOB2_RESOURCE_STRESS_OUTPUT");
    if(destination) std::filesystem::create_directories(destination);
    std::map<int,std::uint64_t> singleYieldDigests;
    for(int shift:{8,9}) for(int variant=0;variant<3;++variant) {
        const unsigned definitions=variant==0?1:512;
        const bool multi=variant==2;
        glob2test::HeadlessGame world({.wDec=shift,.hDec=shift,.teams=8,.discovered=true,
            .clearImmobile=true,.loadDefaultRace=true,.header=true,.seed=713});
        auto& map=world.game.map;
        const auto cells=size_t(map.getW())*map.getH();
        auto start=Clock::now();
        map.installResourceDefinitions(catalog(definitions,multi).dump());
        const auto registryNs=elapsed(start);
        std::vector<size_t> deposits;
        start=Clock::now();
        for(int y=8;y<map.getH();y+=8) for(int x=8;x<map.getW();x+=8) {
            const auto id=map.resourceRegistry().find("stress:crop-"+std::to_string(deposits.size()%definitions));
            REQUIRE(id.has_value());
            map.setResource(x,y,resourceIndex(*id),5);
            deposits.push_back(map.coordToIndex(x,y));
        }
        const auto placementNs=elapsed(start);
        bool placedWideId=false;
        for(size_t index:deposits) placedWideId|=map.getResource(index%map.getW(),index/map.getW()).type>255;
        CHECK(placedWideId==(definitions>255));
        REQUIRE(map.hasMaterialSource(materialIndex(MaterialId::Food)));
        REQUIRE_FALSE(map.hasMaterialSource(materialIndex(MaterialId::Wood)));
        if(definitions>255) CHECK(map.resourceRegistry().size()>255);
        Json row={{"width",map.getW()},{"teams",8},{"definitions",definitions},
            {"multi_material",multi},{"deposits",deposits.size()},
            {"registry_ns",registryNs},{"placement_ns",placementNs},{"repeats",Json::array()}};
        std::uint64_t digest=0;
        for(int repeat=0;repeat<3;++repeat) {
            start=Clock::now();
            for(int team=0;team<8;++team) for(unsigned m=0;m<MaterialCount;++m) {
                const auto* field=map.getMaterialGradient(team,static_cast<MaterialId>(m),0);
                REQUIRE(field);
                digest+=field[(team*313+repeat)%cells];
            }
            const auto fieldsNs=elapsed(start);
            start=Clock::now();
            for(int scan=0;scan<4;++scan) for(size_t index=0;index<cells;++index) {
                digest+=map.materialMaskAt(index);
                digest+=map.materialAmountAt(index,MaterialId::Food);
            }
            const auto queryNs=elapsed(start);
            start=Clock::now();
            for(size_t index:deposits) {
                map.setMaterialAmount(index,MaterialId::Food,0);
                map.setMaterialAmount(index,MaterialId::Food,5);
            }
            const auto mutationNs=elapsed(start);
            start=Clock::now();
            std::vector<Uint16> scratch(cells);
            for(int team=0;team<8;++team)
                map.seedMaterialGradient(team,materialIndex(MaterialId::Food),0,scratch.data(),false);
            digest+=scratch[0];
            row["repeats"].push_back({{"repeat",repeat},{"field_request_ns",fieldsNs},
                {"source_query_ns",queryNs},{"mutation_ns",mutationNs},
                {"seed_refresh_ns",elapsed(start)},{"query_cells",cells*4}});
        }
        unsigned fields=0;
        for(int team=0;team<8;++team) for(unsigned m=0;m<MaterialCount;++m)
            for(int swim=0;swim<SWIM_CLASS_COUNT;++swim) fields+=map.materialGradients[team][m][swim]!=nullptr;
        CHECK(fields==8*(multi?3:1));
        CHECK(map.materialSourceCounts[materialIndex(MaterialId::Food)]==deposits.size());
        row["allocated_material_fields"]=fields;
        row["material_field_bytes"]=fields*cells*sizeof(Uint16);
        row["absent_shared_field_bytes"]=map.gradientRuntime->absentMaterialField.capacity()*sizeof(Uint16);
        row["consumer_cache_fields"]=map.gradientRuntime->materialFields.size();
        row["consumer_cache_bytes"]=map.materialRoutingCacheBytes();
        row["seed_cache_bytes"]=map.gradientRuntime->resourceSeeds.allocatedBytes();
        row["stock_index_bytes"]=map.resourceStockIndices.capacity()*sizeof(Uint32);
        row["stock_sidecar_bytes"]=map.resourceStocks.capacity()*sizeof(std::array<Uint16,MaterialCount>);
        row["stock_free_list_bytes"]=map.freeResourceStocks.capacity()*sizeof(Uint32);
        row["inline_resource_bytes"]=cells*sizeof(Resource);
        row["digest"]=digest;
        if(variant==0) singleYieldDigests[shift]=digest;
        if(variant==1) CHECK(digest==singleYieldDigests[shift]);
        if(destination) {
            // Seed actual colonies for optional CLI continuation, after timed kernels.
            for(int team=0;team<8;++team) {
                auto* colony=world.addBuilding("swarm",3+(team%4)*(map.getW()/4),3+(team/4)*(map.getH()/2),0,team);
                REQUIRE(colony);
                world.addUnit(WORKER,colony->posX+4,colony->posY,team);
                world.game.teams[team]->createLists();
            }
            auto* memory=new GAGCore::MemoryStreamBackend;
            GAGCore::BinaryOutputStream stream(memory);
            world.game.save(&stream,false,"Runtime resource stress");stream.flush();
            const auto bytes=memory->takeContents();
            const auto filename="stress-"+std::to_string(map.getW())+"-"+std::to_string(variant)+".game";
            std::ofstream output(std::filesystem::path(destination)/filename,std::ios::binary);
            output.write(bytes.data(),bytes.size());REQUIRE(output.good());
            row["save"]=filename;
        }
        std::cout<<"GLOB2_RESOURCE_STRESS "<<row.dump()<<'\n';
        report["cases"].push_back(row);
    }
    if(destination) {
        std::ofstream output(std::filesystem::path(destination)/"report.json");
        output<<report.dump(2)<<'\n';REQUIRE(output.good());
    }
}
}
