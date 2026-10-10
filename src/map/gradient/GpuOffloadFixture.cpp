// SPDX-License-Identifier: GPL-3.0-or-later
// Explicit development stress fixtures, not an expansion of lobby map contracts.
#include "EngineFixtures.h"
#include "FileManager.h"
#include "BinaryStream.h"
#include "StreamBackend.h"
#include "BuildingType.h"
#include <cstdlib>
#include <filesystem>
#include <string>
#include <tuple>
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
        REQUIRE(restored.game.load(&input));
        CHECK(restored.game.map.getW()==1024);
        CHECK(restored.game.map.getH()==1024);
        CHECK(restored.game.teamsCount()==4);
        CHECK(restored.game.checkSum(nullptr,nullptr,nullptr,true)==world.game.checkSum(nullptr,nullptr,nullptr,true));
        MESSAGE("Constructed 1024 stress fixture: " << path);
    }
}
}
