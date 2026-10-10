// SPDX-License-Identifier: GPL-3.0-or-later
// Explicit development stress fixtures, not an expansion of lobby map contracts.
#include "EngineFixtures.h"
#include "FileManager.h"
#include "BinaryStream.h"
#include "StreamBackend.h"
#include "BuildingType.h"
#include "io/BenchmarkMapImport.h"
#include <cstdlib>
#include <filesystem>
#include <string>
#include <tuple>
#include <thread>
#include <vector>

TEST_SUITE("GpuOffloadFixture")
{
TEST_CASE("write 1024 real-game stress fixtures [benchmark][artifacts]")
{
    if (!std::getenv("GLOB2_GPU_OFFLOAD_FIXTURES")) return;
    glob2test::HeadlessGlobals globals;
    for (const std::string layout : {"open", "corridors"}) {
        glob2test::HeadlessGame world({.wDec=10,.hDec=10,.teams=4,.discovered=true,
            .clearImmobile=true,.loadDefaultRace=true,.header=true,.seed=91003});
        auto& map=world.game.map;
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
            for(int unit=0;unit<64;++unit)
                world.addUnit(unit<48 ? WORKER : unit<60 ? WARRIOR : EXPLORER,
                    x+unit%16,y+24+unit/16,team);
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
        header.setRandomSeed(91003);
        for(int player=0;player<4;++player)
            header.getBasePlayer(player)=BasePlayer(player,"GPU stress",player,
                BasePlayer::playerTypeFromImplementationID(player%2 ? AI::NICOWAR : AI::MAXIMA));
        world.game.setGameHeader(header,true);
        world.game.setWaitingOnMask(0);
        const auto path=glob2test::artifactDir()/("gpu-1024-"+layout+".game");
        REQUIRE(globalContainer->fileManager->writeAtomically(path.string(),
            [&](GAGCore::OutputStream& output){world.gui.save(&output,"gpu-offload-stress");}));
        GameGUI restored;
        GAGCore::BinaryInputStream input(glob2OpenMapOrSaveInputStreamBackend(*globalContainer->fileManager,path.string()));
        CHECK_FALSE(restored.game.load(&input));
        GAGCore::BinaryInputStream allowedInput(glob2OpenMapOrSaveInputStreamBackend(*globalContainer->fileManager,path.string()));
        {
            const ScopedBenchmarkMapImport fixtureImport(true);
            REQUIRE(restored.game.load(&allowedInput));
        }
        CHECK_FALSE(ScopedBenchmarkMapImport::allows(10,10,Map::MIN_SUPPORTED_SIZE_EXPONENT));
        CHECK_FALSE(Map::supportedDimensions(10,10));
        CHECK(restored.game.map.getW()==1024);
        CHECK(restored.game.map.getH()==1024);
        CHECK(restored.game.teamsCount()==4);
        CHECK(restored.game.checkSum(nullptr,nullptr,nullptr,true)==world.game.checkSum(nullptr,nullptr,nullptr,true));
        MESSAGE("Constructed 1024 stress fixture: " << path);
    }
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
