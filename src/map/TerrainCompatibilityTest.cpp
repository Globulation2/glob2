// SPDX-License-Identifier: GPL-3.0-or-later
#include "EngineFixtures.h"
#include "MapInternal.h"
#include <BinaryStream.h>
#include <TextStream.h>
#include <StreamBackend.h>
#include "Version.h"
#include "gradient/BuildingGradientSearch.h"
#include "render/scene/SceneMap.h"
#include <algorithm>
#include <queue>
#include <random>
#include <memory>
#include <fstream>

TEST_SUITE("PrototypeTerrain")
{
TEST_CASE("mixed costs agree with Dijkstra eager and resumed fields")
{
    glob2test::HeadlessGlobals globals;
    Map map;
    map.setSize(4,4,GRASS);
    std::mt19937 random(323);
    for (int run=0;run<12;++run) for (int swim=0;swim<SWIM_CLASS_COUNT;++swim)
    {
        std::vector<Uint16> seeds(256,GRADIENT_UNREACHABLE);
        for(size_t i=0;i<256;++i) {
            const Uint16 terrain[]={0,256,272,288};
            map.setTerrain(int(i)%16,int(i)/16,terrain[random()%4]);
            if ((swim==0 && map.isWater(unsigned(i))) || random()%9==0) seeds[i]=GRADIENT_FORBIDDEN;
        }
        seeds[0]=GRADIENT_AT_GOAL;
        auto eager=seeds, resumed=seeds;
        std::vector<int> distances(256,1000000);
        using Item=std::pair<int,size_t>;
        std::priority_queue<Item,std::vector<Item>,std::greater<Item>> queue;
        queue.push({0,0});distances[0]=0;
        while(!queue.empty()) {
            const auto [cost,i]=queue.top();queue.pop();
            if(cost!=distances[i])continue;
            for(int dy=-1;dy<=1;++dy)for(int dx=-1;dx<=1;++dx) {
                if(!dx&&!dy)continue;
                const auto n=map.coordToIndex(int(i)%16+dx,int(i)/16+dy);
                if(seeds[n]==GRADIENT_FORBIDDEN)continue;
                const int next=cost+map.stepCost(dx,dy,i,swim);
                if(next<distances[n]) {distances[n]=next;queue.push({next,n});}
            }
        }
        map.propagateGradient(eager.data(),swim);
        BuildingGradientSearch search;
        search.begin(map,resumed.data(),swim);
        // Exercise partial layer completion, then the full field.
        search.resolve(127);search.finish();
        CHECK(eager==resumed);
        for(size_t i=0;i<256;++i) {
            const auto expected=seeds[i]==GRADIENT_FORBIDDEN ? GRADIENT_FORBIDDEN
                : distances[i]==1000000 ? GRADIENT_UNREACHABLE : GRADIENT_AT_GOAL-distances[i];
            REQUIRE(eager[i]==expected);
        }
        auto old=map.frozenMovementSnapshot();
        map.setTerrain(2,2,map.isIce(2,2)?288:272);
        auto changed=map.frozenMovementSnapshot();
        CHECK(old!=changed);
        CHECK((*old)[34]!=(*changed)[34]);
        map.clear();
        CHECK_FALSE(map.hasPrototypeTerrain());
        map.setSize(4,4,GRASS);
    }
}

TEST_CASE("scene layers and binary text saves preserve terrain and continuation [save-format]")
{
    glob2test::HeadlessGlobals globals;
    for(bool text:{false,true}) {
        glob2test::HeadlessGame source(glob2test::GameOptions{.header=true});
        auto &map=source.game.map;
        source.game.gameHeader.setResourceGrowthDisabled(true);
        map.setUMatPos(4,4,ICE,3);
        map.setUMatPos(12,12,COBBLESTONE,5);
        auto *unit=source.addUnit(WORKER,4,4);
        REQUIRE(unit);
        REQUIRE(map.incResource(20,20,WHEAT,0));
        map.getResourceGradient(0,WHEAT,0);
        map.configureGradientPipeline(2,3);
        source.step(4);
        SceneMap scene;
        scene.extract(map);
        Uint16 drawn[3],expected[3];
        const int count=map.prototypeTerrainLayers(4,4,expected);
        REQUIRE(count>0);
        REQUIRE(scene.prototypeTerrainLayers(4,4,drawn)==count);
        CHECK(std::equal(expected,expected+count,drawn));
        REQUIRE(scene.isHardSpaceForBuilding(12,12,1,1));
        auto *storage=new GAGCore::MemoryStreamBackend;
        std::unique_ptr<GAGCore::OutputStream> out(text
            ? static_cast<GAGCore::OutputStream*>(new GAGCore::TextOutputStream(storage))
            : static_cast<GAGCore::OutputStream*>(new GAGCore::BinaryOutputStream(storage)));
        source.game.save(out.get(),false,"Prototype terrain");out->flush();
        const auto bytes=storage->takeContents();
        std::ofstream saved(glob2test::artifactDir() / (text ? "prototype-save.txt" : "prototype-save.g2map"), std::ios::binary);
        saved.write(bytes.data(), bytes.size());saved.close();
        auto *backend=new GAGCore::MemoryStreamBackend;
        backend->write(bytes.data(),bytes.size());backend->seekFromStart(0);
        std::unique_ptr<GAGCore::InputStream> in(text
            ? static_cast<GAGCore::InputStream*>(new GAGCore::TextInputStream(backend))
            : static_cast<GAGCore::InputStream*>(new GAGCore::BinaryInputStream(backend)));
        GameGUI restored;
        REQUIRE(restored.game.load(in.get()));
        CHECK(restored.game.map.hasPrototypeTerrain());
        CHECK(restored.game.map.hasCobblestone());
        CHECK(restored.game.map.minStepCost(0)==5);
        CHECK(restored.game.map.getUMTerrain(4,4)==ICE);
        CHECK(restored.game.map.getUMTerrain(12,12)==COBBLESTONE);
        for(int tick=0;tick<80;++tick) {
            source.step();
            {glob2test::BoundGameRandom randomScope(restored.game);restored.game.syncStep(0);}
            CHECK(restored.game.checkSum()==source.game.checkSum());
            CHECK(restored.game.getUnit(unit->gid)->hp==unit->hp);
        }
        map.setUMatPos(4,4,GRASS,3);
        REQUIRE(scene.prototypeTerrainLayers(4,4,drawn)==count);
        CHECK(std::equal(expected,expected+count,drawn));
    }
}
}
