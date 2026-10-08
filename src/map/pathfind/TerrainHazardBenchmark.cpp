// SPDX-License-Identifier: GPL-3.0-or-later
// Opt-in measurements: no wall-clock performance assertions.
#include "EngineFixtures.h"
#include "BinaryStream.h"
#include "FileManager.h"
#include "StreamBackend.h"
#include "MapInternal.h"
#include <chrono>
#include <ctime>
#include <cstdlib>
#include <nlohmann/json.hpp>

namespace
{
// Terrain is stored on vertices: a cell is wholly of one terrain only when all
// four of its corners are.
void paintCell(Map& map,int x,int y,TerrainType type)
{
    map.paintVertices({{x,y},{x+1,y},{x,y+1},{x+1,y+1}},type,false);
}

template<class Work>
void measure(const char* kind, const char* layout, int width, Work work)
{
    using Clock=std::chrono::steady_clock;
    auto batch=[&](unsigned count) {
        const auto cpu=std::clock();
        const auto wall=Clock::now();
        for(unsigned i=0;i<count;++i) work();
        return std::pair<double,double>{double(std::clock()-cpu)/CLOCKS_PER_SEC,
            std::chrono::duration<double>(Clock::now()-wall).count()};
    };
    unsigned count=1;
    while(batch(count).first<0.03 && count<262144) count*=2;
    for(int repeat=0;repeat<7;++repeat) {
        const auto [cpu,wall]=batch(count);
        std::printf("hazard_bench,%s,%s,%d,%d,%u,%.3f,%.3f\n",kind,layout,width,repeat,count,
            cpu*1e9/count,wall*1e9/count);
    }
}
}

TEST_SUITE("TerrainHazardBenchmark")
{
TEST_CASE("idle decisions at fixed origins across safe and trapped terrain [benchmark]")
{
    glob2test::HeadlessGlobals globals;
    std::printf("hazard_bench,kind,layout,width,repeat,iterations,cpu_ns_per_call,wall_ns_per_call\n");
    for(int shift : {5,7,9}) for(const std::string layout :
        {"grass","safe-near-ice","ice-edge","ice-patch","all-ice","unreachable-safety"}) {
        const bool exposed=layout!="grass" && layout!="safe-near-ice";
        glob2test::HeadlessGame world({.wDec=shift,.hDec=shift,
            .terrain=(layout=="all-ice" || layout=="unreachable-safety")?ICE:GRASS,
            .loadDefaultRace=true,.header=true,.seed=419});
        auto& map=world.game.map;
        const int x=map.getW()/4,y=map.getH()/4;
        {
            auto edit=map.editTerrain();
            // The ice cell is two cells away: the cell between mixes grass and
            // ice corners and is therefore hazardous.
            if(layout=="safe-near-ice") paintCell(map,x+2,y,ICE);
            if(layout=="ice-edge") paintCell(map,x,y,ICE);
            if(layout=="ice-patch") {
                const int radius=map.getW()/8;
                std::vector<std::pair<int,int>> vertices;
                for(int dy=-radius;dy<=radius+1;++dy) for(int dx=-radius;dx<=radius+1;++dx)
                    vertices.push_back({x+dx,y+dy});
                map.paintVertices(vertices,ICE,false);
            }
            if(layout=="unreachable-safety") {
                // Grass corners make a safe cell; the mixed grass/water cells
                // around it stay walkable and safe, and a ring of all-water
                // cells separates them from the ice.
                const int safe=map.getW()*3/4;
                for(int dy=-2;dy<=3;++dy) for(int dx=-2;dx<=3;++dx)
                    map.setVertexTerrain(safe+dx,safe+dy,
                        dx>=0 && dx<=1 && dy>=0 && dy<=1?GRASS:WATER);
            }
        }
        auto* unit=world.addUnit(WORKER,x,y);
        REQUIRE(unit);
        REQUIRE((map.terrainPropertiesAt(x,y).groundHealthQ8<0)==exposed);
        // Fixed origin isolates the cost of one decision. A real successful
        // escape changes its next origin; this intentionally stresses retries.
        // Invalidate outside the timer, then measure first-query rebuild separately.
        for(int repeat=0;repeat<7;++repeat) {
            const auto original=map.vertexTerrainAt(x,y);
            map.setVertexTerrain(x,y,original==GRASS?ICE:GRASS);
            map.setVertexTerrain(x,y,original);
            const auto cpu=std::clock();
            const auto wall=std::chrono::steady_clock::now();
            map.pathfindRandom(unit);
            const double cpuNs=double(std::clock()-cpu)*1e9/CLOCKS_PER_SEC;
            const double wallNs=std::chrono::duration<double>(std::chrono::steady_clock::now()-wall).count()*1e9;
            std::printf("hazard_bench,idle-cold,%s,%d,%d,1,%.3f,%.3f\n",
                layout.c_str(),map.getW(),repeat,cpuNs,wallNs);
        }
        measure("idle",layout.c_str(),map.getW(),[&]{map.pathfindRandom(unit);});
    }
}

TEST_CASE("shared terrain fields across ice coverage and custom profiles [benchmark]")
{
    glob2test::HeadlessGlobals globals;
    for(int shift : {7,9}) for(const std::string layout :
        {"grass","ice-10pct","ice-50pct","all-ice","custom-damage"}) {
        Map map;
        map.setSize(shift,shift,GRASS);
        if(layout=="custom-damage") {
            nlohmann::json definitions=nlohmann::json::array();
            for(int d=0;d<240;++d) definitions.push_back({{"key","bench:damage"+std::to_string(d)},
                {"name","Damage"},{"base","grass"},{"appearance","ice"},
                {"properties",{{"groundHealthQ8",-d}}}});
            map.importTerrainDefinitions(nlohmann::json{{"schemaVersion",1},{"terrains",definitions}}.dump());
        }
        {
            // Coverage counts vertices. Custom damage rates come in 8x8 vertex
            // blocks, which keeps the distinct corner combinations well inside
            // the cell rule table's capacity.
            std::vector<TerrainType> vertices(map.vertexTerrainState().begin(),map.vertexTerrainState().end());
            for(int y=0;y<map.getH();++y) for(int x=0;x<map.getW();++x) {
                const unsigned hash=unsigned(x)*73856093u ^ unsigned(y)*19349663u;
                auto& vertex=vertices[size_t(y)*map.getW()+x];
                if(layout=="all-ice" || (layout=="ice-10pct" && hash%10==0)
                    || (layout=="ice-50pct" && hash%2==0)) vertex=ICE;
                if(layout=="custom-damage") {
                    const unsigned block=unsigned(x/8)*73856093u ^ unsigned(y/8)*19349663u;
                    vertex=*map.terrainRegistry().find("bench:damage"+std::to_string(block%240));
                }
            }
            map.assignVertexTerrain(vertices);
        }
        std::vector<Uint16> cells(map.getW()*map.getH());
        measure("field",layout.c_str(),map.getW(),[&]{
            std::fill(cells.begin(),cells.end(),GRADIENT_UNREACHABLE);
            cells[0]=GRADIENT_AT_GOAL;
            map.propagateGradient(cells.data(),0);
        });
        REQUIRE(cells.back()>GRADIENT_UNREACHABLE);
    }
}

TEST_CASE("write mature game fixtures with controlled ice coverage [benchmark][artifacts]")
{
    const char* source=std::getenv("GLOB2_HAZARD_BENCH_SAVE");
    if(!source || !*source) source="games/cross-replay.game";
    glob2test::HeadlessGlobals globals;
    for(const std::string layout : {"control","sparse-ice","patchwork-ice"}) {
        GameGUI world;
        GAGCore::BinaryInputStream input(glob2OpenMapOrSaveInputStreamBackend(*globalContainer->fileManager,source));
        REQUIRE(world.game.load(&input));
        auto& map=world.game.map;
        unsigned changed=0,eligible=0;
        {
            auto edit=map.editTerrain();
            // Ice replaces grass vertices whose cell (the one they are the
            // top-left corner of) is open grass.
            for(int y=0;y<map.getH();++y) for(int x=0;x<map.getW();++x) {
                if(map.terrainTypeAt(x,y)!=GRASS || map.getBuilding(x,y)!=NOGUID
                    || map.getResource(x,y).type!=NO_RES_TYPE) continue;
                ++eligible;
                const unsigned hash=unsigned(x)*73856093u ^ unsigned(y)*19349663u;
                if((layout=="sparse-ice" && hash%10==0)
                    || (layout=="patchwork-ice" && ((x/12+y/12)%4==0))) {
                    map.setVertexTerrain(x,y,ICE); ++changed;
                }
            }
        }
        const auto path=glob2test::artifactDir()/(layout+".game");
        REQUIRE(globalContainer->fileManager->writeAtomically(path.string(),
            [&](GAGCore::OutputStream& output){world.save(&output,"hazard-performance");}));
        std::printf("hazard_fixture,%s,%d,%u,%u,%s\n",layout.c_str(),map.getW(),eligible,changed,path.c_str());
    }
}
}
