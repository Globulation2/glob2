// Experimental harness: linked against either the shipping or candidate growth object.
#include "EngineFixtures.h"
#include "ResourceGrowth.h"
#include "gradient/GradientRuntime.h"
#include <nlohmann/json.hpp>
#include <fstream>
#include <cstdlib>
#include <bit>
using Json = nlohmann::json;
namespace {
ResourceId setupSeedWorld(Map &map, const std::string &layout, unsigned seed, unsigned rate)
{
    auto catalog = Json::parse(map.resourceRegistry().serialize());
    auto crop = catalog["resources"][1];
    crop["key"] = "seed-rule-crop";
    crop["properties"]["ecology"] = layout == "grass-control" ? "uniform" : "land";
    crop["yields"]["paper"] = {{"capacity",5},{"initial",2},{"growthRate",rate},{"consumption","one"}};
    map.installResourceDefinitions(Json{{"schemaVersion",1},{"resources",{crop}}}.dump());
    const auto id = *map.resourceRegistry().find("seed-rule-crop");
    for (int y=0; y<map.getH(); ++y) for (int x=0; x<map.getW(); ++x)
    {
        TerrainType terrain = GRASS;
        const int u=(x+int(seed*7))%64, v=(y+int(seed*11))%64;
        if (layout=="river") terrain=((x-y/8+int(seed)+map.getW())%32<3) ? WATER : GRASS;
        if (layout=="islands") {
            const int a=u%32,b=v%32;
            terrain=(a<5 || b<5 || a>27 || b>27) ? WATER : ((a<8 || b<8 || a>24 || b>24) ? SAND : GRASS);
        }
        if (layout=="dry") terrain=(u<3 && v<3) ? WATER : ((u>32 && v>16) ? SAND : GRASS);
        if (layout=="mixed") terrain=(u<4 || (u>45 && v<20)) ? WATER : ((u>24 && u<40 && v>32) ? SAND : GRASS);
        map.setCellTerrain(x,y,terrain);
        if (layout=="mixed" && x>map.getW()/3 && x<map.getW()/2 && y>map.getH()/3 && y<map.getH()/2)
            map.setResourcesGrow(x,y,false);
    }
    for (int y=int(seed%4); y<map.getH(); y+=4) for (int x=int((seed/4)%4); x<map.getW(); x+=4)
        if (map.getTerrainType(x,y)==GRASS) map.setResource(x,y,id,0);
    return id;
}
std::array<Uint64,3> inventory(Map &map)
{
    std::array<Uint64,3> result{};
    for (size_t i=0;i<map.cellCount();++i) {
        result[0]+=map.materialAmountAt(i,MaterialId::Food);
        result[1]+=map.materialAmountAt(i,MaterialId::Paper);
        result[2]+=map.getResource(i).type!=NO_RES_TYPE;
    }
    return result;
}
Uint64 physicalHash(Map &map)
{
    Uint64 hash=14695981039346656037ull;
    for(size_t i=0;i<map.cellCount();++i) {
        for(Uint64 n : {Uint64(map.getResource(i).getUint32()),Uint64(map.materialAmountAt(i,MaterialId::Food)),Uint64(map.materialAmountAt(i,MaterialId::Paper))})
            hash=(hash^n)*1099511628211ull;
    }
    return hash;
}
}
TEST_CASE("one per material seed rules on varied terrain [benchmark][resources]" * doctest::test_suite("SeedRuleExperiment"))
{
    glob2test::HeadlessGlobals globals;
    const char *output=std::getenv("GLOB2_SEED_RULE_OUTPUT");
    REQUIRE(output);
    const bool candidate=std::getenv("GLOB2_SEED_RULE_CANDIDATE");
    const unsigned seeds=std::getenv("GLOB2_SEED_RULE_PILOT") ? 1 : 20;
    Json report={{"candidate",candidate},{"delay",8},{"ticks",512},{"seeds",seeds},{"samples",Json::array()}};
    for(const std::string layout : {"grass-control","river","islands","dry","mixed"})
    for(unsigned rate : {ResourceRateScale,ResourceRateScale/4,0u})
    for(unsigned seed=1;seed<=seeds;++seed)
    {
        std::vector<Uint32> sharedChecksums;
        Json sharedTrace;
        // Full matrix uses shared execution; seed1 also checks exact owner continuation.
        for(bool shared : {true,false}) {
            if(!shared && seed!=1) continue;
            INFO(layout," rate=",rate," seed=",seed," shared=",shared);
            glob2test::HeadlessGame world({.wDec=layout=="grass-control"?6:layout=="mixed"?8:7,
                .hDec=layout=="grass-control"?6:7,.teams=2,.loadDefaultRace=true});
            GameHeader header;header.setNumberOfPlayers(0);header.setRandomSeed(seed);world.game.setGameHeader(header,true);
            auto &map=world.game.map;
            setupSeedWorld(map,layout,seed,rate);
            map.configureCompute(shared?4:1,0);map.configureResourceGrowth(8,shared);
            const auto initial=inventory(map);
            REQUIRE(initial[2]>0);
            Json trace=Json::array();
            for(unsigned tick=0;tick<512;++tick) {
                world.step();
                if(seed==1) {
                    const auto checksum=world.game.checkSum(nullptr,nullptr,nullptr,true);
                    if(shared) sharedChecksums.push_back(checksum);
                    else REQUIRE(checksum==sharedChecksums[tick]);
                }
                if((tick+1)%64==0) {
                    const auto current=inventory(map);
                    const auto &stats=world.game.teams[0]->stats.measurements;
                    const auto &other=world.game.teams[1]->stats.measurements;
                    Uint64 tiles=0;
                    for(auto material : {MaterialId::Food,MaterialId::Paper}) {
                        const auto m=materialIndex(material);const unsigned index=material==MaterialId::Food?0:1;
                        REQUIRE(current[index]==initial[index]+stats.growthGlobal[1][m]);
                        REQUIRE(stats.growthGlobal[2][m]==0);
                        REQUIRE(stats.growthGlobal[1][m]==other.growthGlobal[1][m]);
                        tiles+=stats.growthGlobal[0][m];
                    }
                    REQUIRE(current[2]==initial[2]+tiles);
                    const auto &metrics=map.resourceGrowthMetrics();
                    REQUIRE(metrics.stockAdded==current[0]+current[1]-initial[0]-initial[1]);
                    REQUIRE(metrics.publishedProposals==metrics.accepted+metrics.rejected);
                    trace.push_back({{"tick",tick+1},{"stocks",current},{"hash",physicalHash(map)}});
                }
            }
            map.finishResourceGrowth();
            const auto metrics=map.resourceGrowthMetrics();
            if(shared) sharedTrace=trace;else REQUIRE(trace==sharedTrace);
            report["samples"].push_back({{"layout",layout},{"rate",rate},{"seed",seed},{"shared",shared},
                {"width",map.getW()},{"height",map.getH()},{"initial",initial},{"trace",trace},
                {"sampled",metrics.sampled},{"proposals",metrics.proposals},{"accepted",metrics.accepted},
                {"rejected",metrics.rejected},{"stockAdded",metrics.stockAdded},{"tilesAdded",metrics.tilesAdded}});
            // Save each completed run so interrupted experiments retain attributable evidence.
            std::ofstream out(output);out<<report.dump(2);REQUIRE(out.good());
        }
    }
}
TEST_CASE("experimental seed marker and destination removal" * doctest::test_suite("SeedRuleExperiment"))
{
    if(!std::getenv("GLOB2_SEED_RULE_CANDIDATE")) return;
    glob2test::HeadlessGlobals globals;
    glob2test::HeadlessGame world({.header=true});auto &map=world.game.map;
    const auto id=setupSeedWorld(map,"grass-control",1,0);
    const auto at=map.coordToIndex(20,20);
    map.replaceResource(at,Resource{});
    ResourceGrowth::Metrics metrics;ResourceGrowth::Batch batch;
    batch.proposals={{Uint32(at),Uint16(resourceIndex(id)),255,1}};
    ResourceGrowth::apply(map,batch,metrics);
    CHECK(map.materialAmountAt(at,MaterialId::Food)==1);CHECK(map.materialAmountAt(at,MaterialId::Paper)==1);
    // A replenishment in flight becomes a complete seed if its destination vanishes.
    map.replaceResource(at,Resource{});
    batch.proposals={{Uint32(at),Uint16(resourceIndex(id)),Uint8(materialIndex(MaterialId::Food)),1}};
    ResourceGrowth::apply(map,batch,metrics);
    CHECK(map.materialAmountAt(at,MaterialId::Food)==1);CHECK(map.materialAmountAt(at,MaterialId::Paper)==1);
    ResourceGrowth::apply(map,batch,metrics);
    CHECK(map.materialAmountAt(at,MaterialId::Food)==2);CHECK(map.materialAmountAt(at,MaterialId::Paper)==1);
    // A competing seed to the same type remains bounded and adds only one of each.
    batch.proposals={{Uint32(at),Uint16(resourceIndex(id)),255,1}};
    for(int i=0;i<6;++i) ResourceGrowth::apply(map,batch,metrics);
    CHECK(map.materialAmountAt(at,MaterialId::Food)==5);CHECK(map.materialAmountAt(at,MaterialId::Paper)==5);
    map.setResource(20,20,static_cast<ResourceId>(STONE),0);
    const auto before=map.getResource(at).getUint32();ResourceGrowth::apply(map,batch,metrics);
    CHECK(map.getResource(at).getUint32()==before);
    CHECK(metrics.publishedProposals==metrics.accepted+metrics.rejected);
}
