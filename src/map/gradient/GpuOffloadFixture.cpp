// SPDX-License-Identifier: GPL-3.0-or-later
// Explicit development stress fixtures, not an expansion of lobby map contracts.
#include "EngineFixtures.h"
#include "FileManager.h"
#include "BinaryStream.h"
#include "StreamBackend.h"
#include "BuildingType.h"
#include "field/GradientCosts.h"
#include "io/BenchmarkMapImport.h"
#include "io/BenchmarkFixtureAdmission.h"
#include <cstdlib>
#include <filesystem>
#include <string>
#include <tuple>
#include <thread>
#include <vector>

namespace {
struct Controllers {std::string suffix;unsigned seed;AI::ImplementationID even,odd;int units=64;bool swimmers=false;};
void writeFixtures(int exponent,const std::vector<Controllers>& controllers) {
    REQUIRE((exponent==10 || exponent==11));
    const int dimension=1<<exponent;
    REQUIRE(std::uint64_t(dimension)*dimension<=std::uint64_t(2048)*2048);
    glob2test::HeadlessGlobals globals;
    for(const auto& controller:controllers)
    for (const std::string layout : {"open", "corridors"}) {
        REQUIRE((controller.units==64 || controller.units==256));
        glob2test::HeadlessGame world({.wDec=exponent,.hDec=exponent,.teams=4,.discovered=true,
            .clearImmobile=true,.loadDefaultRace=true,.header=true,.seed=controller.seed});
        auto& map=world.game.map;
        std::vector<Uint16> swimmerGids;
        if (layout=="corridors") {
            auto edit=map.editTerrain();
            // Water barriers with alternating broad passages. These fixtures are
            // labelled constructed stress inputs, never representative generators.
            for (int x=160, wall=0;x<map.getW()-64;x+=64,++wall) {
                const int passage=wall%2 ? map.getH()-96 : 64;
                for (int y=0;y<map.getH();++y)
                    if (y<passage || y>=passage+32)
                        for (int dx=0;dx<4;++dx) map.setVertexTerrain(x+dx,y,WATER);
            }
        }
        for(int team=0;team<4;++team) {
            const int x=map.getW()/4+(team%2)*map.getW()/2+8;
            const int y=map.getH()/4+(team/2)*map.getH()/2+8;
            world.game.teams[team]->startPosX=x;
            world.game.teams[team]->startPosY=y;
            for (const auto& [name,dx,dy] : std::vector<std::tuple<const char*,int,int>>{
                {"swarm",0,0},{"inn",12,0},{"hospital",0,12}}) {
                auto* building=world.addBuilding(name,x+dx,y+dy,0,team);
                for(int material=0;material<MaterialCount;++material)
                    building->materials[material]=building->type->maxMaterial[material];
            }
            for(int unit=0;unit<controller.units;++unit) {
                const int type=unit<controller.units*3/4 ? WORKER : unit<controller.units*15/16 ? WARRIOR : EXPLORER;
                const bool swimmer=controller.swimmers && type==WORKER && unit%2==0;
                auto* created=world.addUnit(type,x+unit%16,y+24+unit/16,team,swimmer?1:0);
                if(swimmer) {
                    REQUIRE(created);
                    REQUIRE(created->performance[SWIM]>0);
                    // Default level-one workers walk at21 and swim at10: the
                    // serialized legal upgrade selects weighted class5.
                    REQUIRE(created->swimClass()==5);
                    REQUIRE(gradient_kernel::weightedClass(created->swimClass()));
                    swimmerGids.push_back(created->gid);
                }
            }
            for(int material=0;material<MaterialCount;++material)
                for(int dy=0;dy<6;++dy) for(int dx=0;dx<6;++dx) {
                    const int rx=x-48+material*12+dx,ry=y+64+dy;
                    if(material==ALGA) continue;
                    if(map.terrainTypeAt(rx,ry)==GRASS && map.getBuilding(rx,ry)==NOGBID)
                        map.setResourceByIndex(rx,ry,material,5);
                }
            world.game.teams[team]->createLists();
        }
        GameHeader header;
        header.setNumberOfPlayers(4);
        header.setRandomSeed(controller.seed);
        for(int player=0;player<4;++player)
            header.getBasePlayer(player)=BasePlayer(player,"GPU stress",player,
                BasePlayer::playerTypeFromImplementationID(player%2 ? controller.odd : controller.even));
        world.game.setGameHeader(header,true);
        world.game.setWaitingOnMask(0);
        const auto path=glob2test::artifactDir()/("gpu-"+std::to_string(dimension)+"-"+layout+controller.suffix+".game");
        REQUIRE(globalContainer->fileManager->writeAtomically(path.string(),
            [&](GAGCore::OutputStream& output){world.gui.save(&output,"gpu-offload-stress");}));
        GameGUI restored;
        GAGCore::BinaryInputStream input(glob2OpenMapOrSaveInputStreamBackend(*globalContainer->fileManager,path.string()));
        CHECK_FALSE(restored.game.load(&input));
        GAGCore::BinaryInputStream allowedInput(glob2OpenMapOrSaveInputStreamBackend(*globalContainer->fileManager,path.string()));
        {
            const ScopedBenchmarkMapImport fixtureImport(true,exponent);
            REQUIRE(restored.game.load(&allowedInput));
        }
        CHECK_FALSE(ScopedBenchmarkMapImport::allows(exponent,exponent,Map::MIN_SUPPORTED_SIZE_EXPONENT));
        CHECK_FALSE(Map::supportedDimensions(exponent,exponent));
        CHECK(restored.game.map.getW()==dimension);
        CHECK(restored.game.map.getH()==dimension);
        CHECK(restored.game.teamsCount()==4);
        CHECK(restored.game.checkSum(nullptr,nullptr,nullptr,true)==world.game.checkSum(nullptr,nullptr,nullptr,true));
        if(controller.swimmers) {
            // Half of the three-quarter worker cohort, across four teams:
            // 96 weighted swimmers for the explicit64-unit-per-team fixture.
            REQUIRE(swimmerGids.size()==std::size_t(controller.units*3/2));
            for(const auto gid:swimmerGids) {
                auto* restoredSwimmer=restored.game.getUnit(gid);
                REQUIRE(restoredSwimmer);
                CHECK(restoredSwimmer->performance[SWIM]>0);
                CHECK(restoredSwimmer->swimClass()==5);
            }
            MESSAGE("Weighted swimmer fixture expected class5 count " << swimmerGids.size()
                << "; real-game activation also requires positive minimal copied bytes, cost uploads and committed GPU fields, plus exact own-save continuation.");
        }
        MESSAGE("Constructed stress fixture " << dimension << " population " << controller.units*4 << ": " << path);
    }
}
}

TEST_SUITE("GpuOffloadFixture") {
TEST_CASE("write 1024 real-game stress fixtures [benchmark][artifacts]") {
    if(!std::getenv("GLOB2_GPU_OFFLOAD_FIXTURES")) return;
    std::vector<Controllers> controllers{{"",91003,AI::MAXIMA,AI::NICOWAR}};
    // Preserve mixed-AI fixtures and their known wide-brush failure separately.
    if(const auto* bounded=std::getenv("GLOB2_GPU_OFFLOAD_BOUNDED_AI_FIXTURES");bounded && std::string(bounded)=="1")
        for(unsigned seed:{91003u,91004u})
            controllers.push_back({"-bounded-ai-seed"+std::to_string(seed),seed,AI::NUMBI,AI::CASTOR});
    if(const auto* stress=std::getenv("GLOB2_GPU_OFFLOAD_POPULATION_STRESS");stress && std::string(stress)=="1")
        controllers.push_back({"-bounded-ai-seed91005-population1024",91005,AI::NUMBI,AI::CASTOR,256});
    if(const auto* swimmers=std::getenv("GLOB2_GPU_OFFLOAD_SWIM_FIXTURES");swimmers && std::string(swimmers)=="1")
        controllers.push_back({"-bounded-ai-seed91006-swimmers",91006,AI::NUMBI,AI::CASTOR,64,true});
    writeFixtures(10,controllers);
}
TEST_CASE("write 2048 bounded-AI stress fixtures after explicit memory admission [benchmark][artifacts]") {
    const auto* enabled=std::getenv("GLOB2_GPU_OFFLOAD_2048_FIXTURES");
    if(!enabled || std::string(enabled)!="1") return;
    const auto budget=benchmark_fixture::parseMemoryBudget(std::getenv("GLOB2_BENCHMARK_FIXTURE_MEMORY_MIB"));
    const auto available=benchmark_fixture::availableMemoryMiB();
    MESSAGE("2048 fixture preflight memory budget MiB " << budget << ", available MiB " << available);
    REQUIRE(benchmark_fixture::memoryAdmission(budget,available));
    writeFixtures(11,{{"-bounded-ai-seed92003-population1024",92003,AI::NUMBI,AI::CASTOR,256}});
}
TEST_CASE("2048 fixture memory admission rejects missing malformed and insufficient budgets") {
    using namespace benchmark_fixture;
    CHECK(parseMemoryBudget(nullptr)==0);CHECK(parseMemoryBudget("")==0);
    for(const char* invalid:{"-1","+8192","8192x","18446744073709551616"}) CHECK(parseMemoryBudget(invalid)==0);
    CHECK(parseMemoryBudget("8192")==8192);
    CHECK_FALSE(memoryAdmission(0,65536));CHECK_FALSE(memoryAdmission(8191,65536));
    CHECK_FALSE(memoryAdmission(8192,0));CHECK_FALSE(memoryAdmission(8192,10239));
    CHECK(memoryAdmission(8192,10240));CHECK_FALSE(memoryAdmission(UINT64_MAX,UINT64_MAX));
}
}

TEST_CASE("large fixture import bounds and thread scope never widen ordinary readers" * doctest::test_suite("GpuOffloadFixture"))
{
    CHECK_FALSE(ScopedBenchmarkMapImport::allows(10,10,Map::MIN_SUPPORTED_SIZE_EXPONENT));
    {
        const ScopedBenchmarkMapImport outer(true);
        CHECK(ScopedBenchmarkMapImport::allows(10,10,Map::MIN_SUPPORTED_SIZE_EXPONENT));
        CHECK_FALSE(ScopedBenchmarkMapImport::allows(11,10,Map::MIN_SUPPORTED_SIZE_EXPONENT));
        CHECK_FALSE(ScopedBenchmarkMapImport::allows(10,11,Map::MIN_SUPPORTED_SIZE_EXPONENT));
        CHECK_FALSE(ScopedBenchmarkMapImport::allows(3,10,Map::MIN_SUPPORTED_SIZE_EXPONENT));
        { const ScopedBenchmarkMapImport inner(false);
          CHECK_FALSE(ScopedBenchmarkMapImport::allows(10,10,Map::MIN_SUPPORTED_SIZE_EXPONENT)); }
        CHECK(ScopedBenchmarkMapImport::allows(10,10,Map::MIN_SUPPORTED_SIZE_EXPONENT));
        if constexpr(GAGCore::ThreadSupport::available) {
            bool otherThreadAllowed=true;
            auto worker=GAGCore::ThreadSupport::launch([&]{otherThreadAllowed=ScopedBenchmarkMapImport::allows(10,10,Map::MIN_SUPPORTED_SIZE_EXPONENT);});
            worker.join(); CHECK_FALSE(otherThreadAllowed);
        }
    }
    CHECK_FALSE(ScopedBenchmarkMapImport::allows(10,10,Map::MIN_SUPPORTED_SIZE_EXPONENT));
}

TEST_CASE("scoped fixture import rejects malformed dimensions before sizing storage" * doctest::test_suite("GpuOffloadFixture"))
{
    for(const auto [width,height]:std::vector<std::pair<int,int>>{{11,10},{10,11},{3,10},{10,3}}) {
        auto* backend=new GAGCore::MemoryStreamBackend;
        GAGCore::BinaryOutputStream output(backend);
        output.writeEnterSection("Map"); output.write("MapB",4,"signatureStart");
        output.writeSint32(width,"wDec"); output.writeSint32(height,"hDec");
        output.writeLeaveSection(); output.flush();
        GAGCore::BinaryInputStream input(new GAGCore::MemoryStreamBackend(backend->getBuffer(),backend->getPosition()));
        MapHeader header; Map loaded;
        const ScopedBenchmarkMapImport scope(true);
        CHECK_FALSE(loaded.load(&input,header));
        CHECK(loaded.getW()==0); CHECK(loaded.getH()==0);
    }
}

TEST_CASE("2048 benchmark scope is explicit bounded nested and confined to its thread" * doctest::test_suite("GpuOffloadFixture")) {
    CHECK_FALSE(Map::supportedDimensions(11,11));
    {const ScopedBenchmarkMapImport ordinary(true);
     CHECK_FALSE(ScopedBenchmarkMapImport::allows(11,11,Map::MIN_SUPPORTED_SIZE_EXPONENT));
     {const ScopedBenchmarkMapImport stress(true,11);
      CHECK(ScopedBenchmarkMapImport::allows(11,11,Map::MIN_SUPPORTED_SIZE_EXPONENT));
      CHECK_FALSE(ScopedBenchmarkMapImport::allows(12,11,Map::MIN_SUPPORTED_SIZE_EXPONENT));
      CHECK_FALSE(ScopedBenchmarkMapImport::allows(11,12,Map::MIN_SUPPORTED_SIZE_EXPONENT));
      CHECK_FALSE(ScopedBenchmarkMapImport::allows(3,11,Map::MIN_SUPPORTED_SIZE_EXPONENT));
      if constexpr(GAGCore::ThreadSupport::available) {
          bool inherited=true;auto worker=GAGCore::ThreadSupport::launch([&]{inherited=ScopedBenchmarkMapImport::allows(11,11,Map::MIN_SUPPORTED_SIZE_EXPONENT);});
          worker.join();CHECK_FALSE(inherited);
      }
      {const ScopedBenchmarkMapImport invalid(true,12);
       CHECK_FALSE(ScopedBenchmarkMapImport::allows(10,10,Map::MIN_SUPPORTED_SIZE_EXPONENT));}
      CHECK(ScopedBenchmarkMapImport::allows(11,11,Map::MIN_SUPPORTED_SIZE_EXPONENT));}
     CHECK_FALSE(ScopedBenchmarkMapImport::allows(11,11,Map::MIN_SUPPORTED_SIZE_EXPONENT));}
    CHECK_FALSE(ScopedBenchmarkMapImport::allows(11,11,Map::MIN_SUPPORTED_SIZE_EXPONENT));
    for(const auto [width,height]:std::vector<std::pair<int,int>>{{12,11},{11,12},{3,11},{11,3}}) {
        auto* backend=new GAGCore::MemoryStreamBackend;GAGCore::BinaryOutputStream output(backend);
        output.writeEnterSection("Map");output.write("MapB",4,"signatureStart");
        output.writeSint32(width,"wDec");output.writeSint32(height,"hDec");output.writeLeaveSection();output.flush();
        GAGCore::BinaryInputStream input(new GAGCore::MemoryStreamBackend(backend->getBuffer(),backend->getPosition()));
        MapHeader header;Map loaded;const ScopedBenchmarkMapImport stress(true,11);
        CHECK_FALSE(loaded.load(&input,header));CHECK(loaded.getW()==0);CHECK(loaded.getH()==0);
    }
}
