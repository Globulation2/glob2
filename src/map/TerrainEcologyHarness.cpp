// SPDX-License-Identifier: GPL-3.0-or-later
// Real production growth versus the frozen pre-terrain-refactor routine. The
// reference exists only in this opt-in test executable, never in the game.
#include "EngineFixtures.h"
#include "BinaryStream.h"
#include "FileManager.h"
#include "Toolkit.h"
#include "Utilities.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <memory>
#include <string>

namespace
{
// Frozen mutation rule as well as frozen probes: candidate habitat enforcement
// must not silently alter the baseline for unusual legacy resource placements.
void baselineIncrement(Map& map,int x,int y,int resource,int variety)
{
    auto r=map.getResource(x,y);
    if(r.type==NO_RES_TYPE)
    {
        if(map.getBuilding(x,y)!=NOGBID || map.getGroundUnit(x,y)!=NOGUID) return;
        if(map.getTerrainType(x,y)!=(resource==ALGA?WATER:GRASS)) return;
        r.type=resource;r.variety=variety;r.amount=1;r.animation=0;
        map.replaceResource(x,y,r);
        return;
    }
    if(r.type!=resource) return;
    const auto* type=globalContainer->resourcesTypes.get(r.type);
    if(!type->shrinkable) return;
    if(r.amount<type->sizesCount)++r.amount;else --r.amount;
    map.replaceResource(x,y,r);
}

void baselineGrowth(Map& map)
{
    if (map.game->gameHeader.isResourceGrowthDisabled()) return;
    map.rebuildGrowthCoverage();
    static constexpr int divisors[] = {1,2,4,8};
    const int scarcity=divisors[map.game->gameHeader.getResourceScarcityLevel()];
    const int w=map.getW(), h=map.getH();
    const int firstY=syncRand()&3;
    for (int y=firstY;y<h;y+=4)
        for(int x=syncRand()&15;x<w;x+=syncRand()&31)
        {
            const auto& r=map.getResource(x,y);
            if(r.type==NO_RES_TYPE) continue;
            const int dx=(syncRand()&15)-(syncRand()&15);
            const int dy=(syncRand()&15)-(syncRand()&15);
            bool expand=true;
            if(r.type==ALGA) expand=map.isWater(x+dx,y+dy)&&map.isSand(x+dy*2,y+dx*2);
            else if(r.type==WOOD || r.type==WHEAT)
                expand=map.isWater(x+dx,y+dy)&&!map.isSand(x-dx,y-dy);
            if(r.type==WHEAT && expand && syncRand()%3!=0) expand=false;
            if(!expand || (scarcity!=1 && syncRand()%scarcity!=0)) continue;
            if(r.amount<=(syncRand()&7))
            {
                if(map.getTile(x,y).canResourcesGrow)
                {
                    const int type=r.type, amount=r.amount;
                    baselineIncrement(map,x,y,type,r.variety);
                    map.recordNaturalGrowth(x,y,type,type,amount);
                }
            }
            else if(globalContainer->resourcesTypes.get(r.type)->expendable)
            {
                int mx,my; Unit::dxDyFromDirection(syncRand()&7,&mx,&my);
                if(map.getTile(x+mx,y+my).canResourcesGrow)
                {
                    const auto& before=map.getResource(x+mx,y+my);
                    const int type=before.type, amount=before.amount, growing=r.type;
                    baselineIncrement(map,x+mx,y+my,growing,r.variety);
                    map.recordNaturalGrowth(x+mx,y+my,growing,type,amount);
                }
            }
        }
}

template<class Range> void array(std::ostream& out,const Range& values)
{
    out<<'['; bool comma=false;
    for(const auto value:values){if(comma)out<<',';comma=true;out<<value;}
    out<<']';
}
int positiveEnv(const char* name,int fallback)
{
    const char* text=std::getenv(name);
    if(!text) return fallback;
    const int value=std::stoi(text);
    if(value<=0) throw std::runtime_error(std::string(name)+" must be positive");
    return value;
}
}

TEST_SUITE("TerrainEcology")
{
TEST_CASE("saved-map growth calibration [benchmark][slow]")
{
    const char* corpus=std::getenv("GLOB2_TERRAIN_CALIBRATION_CORPUS");
    if(!corpus){MESSAGE("Set GLOB2_TERRAIN_CALIBRATION_CORPUS to a newline-separated map manifest");return;}
    const char* destination=std::getenv("GLOB2_TERRAIN_CALIBRATION_OUTPUT");
    REQUIRE(destination);
    std::ifstream manifest(corpus);
    REQUIRE(manifest.good());
    std::ofstream output(destination);
    REQUIRE(output.good());
    const int seeds=positiveEnv("GLOB2_TERRAIN_CALIBRATION_SEEDS",16);
    const int firstSeed=positiveEnv("GLOB2_TERRAIN_CALIBRATION_FIRST_SEED",1);
    const int ticks=positiveEnv("GLOB2_TERRAIN_CALIBRATION_TICKS",65536);
    glob2test::HeadlessGlobals globals;
    std::string mapFile;
    while(std::getline(manifest,mapFile))
    {
        if(mapFile.empty() || mapFile[0]=='#') continue;
        CAPTURE(mapFile);
        for(int seed=firstSeed;seed<firstSeed+seeds;++seed)
            for(int replenish=0;replenish<2;++replenish)
                for(int candidate=0;candidate<2;++candidate)
                {
                    Game game(nullptr);
                    std::unique_ptr<GAGCore::InputStream> input(new GAGCore::BinaryInputStream(
                        glob2OpenMapOrSaveInputStreamBackend(*GAGCore::Toolkit::getFileManager(),mapFile)));
                    REQUIRE(!input->isEndOfStream());
                    REQUIRE(game.load(input.get()));
                    REQUIRE(game.mapHeader.getNumberOfTeams()>0);
                    glob2test::BoundGameRandom random(game);
                    setSyncRandSeed(static_cast<Uint32>(seed));
                    auto& map=game.map;
                    auto& measures=game.teams[0]->stats.measurements;
                    for(auto& band:measures.growthGlobal) std::fill(std::begin(band),std::end(band),0);
                    std::array<std::uint64_t,MAX_RESOURCES> harvested{},initial{};
                    for(int i=0;i<map.getW()*map.getH();++i)
                    {const auto& r=map.getResource(i);if(r.type<MAX_RESOURCES)initial[r.type]+=r.amount;}
                    const auto start=std::chrono::steady_clock::now();
                    for(int tick=1;tick<=ticks;++tick)
                    {
                        if(candidate)map.growResources();else baselineGrowth(map);
                        // Controlled replenishment assay: remove one amount unit
                        // while retaining a seed. Full AI games separately test
                        // actual worker harvesting, extinction and access pressure.
                        if(replenish && tick%64==0)
                            for(int i=0;i<map.getW()*map.getH();++i)
                            {
                                const auto& r=map.getResource(i);
                                if(r.type<MAX_RESOURCES && r.type!=STONE && r.amount>1)
                                {map.setResourceAmount(i,r.amount-1);++harvested[r.type];}
                            }
                        if(tick!=1024 && tick!=4096 && tick!=16384 && tick!=65536 && tick!=ticks)continue;
                        std::array<std::uint64_t,MAX_RESOURCES> stock{},occupied{},added{},removed{},newTiles{};
                        for(int i=0;i<map.getW()*map.getH();++i)
                        {const auto& r=map.getResource(i);if(r.type<MAX_RESOURCES){stock[r.type]+=r.amount;++occupied[r.type];}}
                        for(int r=0;r<MAX_RESOURCES;++r)
                        {newTiles[r]=measures.growthGlobal[0][r];added[r]=measures.growthGlobal[1][r];removed[r]=measures.growthGlobal[2][r];
                         CHECK(initial[r]+added[r]==stock[r]+removed[r]+harvested[r]);}
                        output<<"{\"map\":"<<std::quoted(mapFile)<<",\"seed\":"<<seed
                            <<",\"variant\":\""<<(candidate?"candidate":"baseline")<<"\",\"scenario\":\""
                            <<(replenish?"replenishment":"unattended")<<"\",\"tick\":"<<tick<<",\"stock\":";
                        array(output,stock);output<<",\"occupied\":";array(output,occupied);
                        output<<",\"added\":";array(output,added);output<<",\"removed\":";array(output,removed);
                        output<<",\"new_tiles\":";array(output,newTiles);output<<",\"harvested\":";array(output,harvested);
                        output<<",\"elapsed_seconds\":"<<std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count()<<"}\n";
                        output.flush();
                    }
                }
    }
}
}

TEST_SUITE("TerrainEcology")
{
TEST_CASE("four growth opportunities preserve stack updates and measured conservation")
{
    glob2test::HeadlessGlobals globals;
    struct Outcome
    {
        std::array<std::uint64_t,4> values{};
        Uint32 nextRandom=0;
        bool operator==(const Outcome&) const = default;
    };
    const auto run=[&](int type,unsigned rate,int visits) {
        glob2test::HeadlessGame world(glob2test::GameOptions{.header=true,.seed=719});
        auto& game=world.game;
        auto& map=game.map;
        auto resource=map.getResource(12,12);
        resource.type=type;resource.amount=globalContainer->resourcesTypes.get(type)->sizesCount;
        resource.variety=0;
        map.replaceResource(12,12,resource);
        const int initial=resource.amount;
        glob2test::BoundGameRandom random(game);
        setSyncRandSeed(719);
        map.rebuildGrowthCoverage();
        for(int i=0;i<visits;++i)Fertility::applyGrowthOpportunities(map,12,12,rate,1);
        const auto& growth=game.teams[0]->stats.measurements.growthGlobal;
        Outcome result;
        for(int i=0;i<map.getW()*map.getH();++i)
            if(map.getResource(i).type==type)result.values[0]+=map.getResource(i).amount;
        for(int i=0;i<3;++i)result.values[i+1]=growth[i][type];
        result.nextRandom=syncRand();
        CHECK(initial+result.values[2]==result.values[0]+result.values[3]);
        CHECK(result.values[2]>0);
        CHECK(result.values[3]>0);
        return result;
    };
    for(int type:{PAPYRUS,CHERRY,WOOD})
        CHECK(run(type,4*Fertility::kRateScale,256)==run(type,Fertility::kRateScale,1024));
}

TEST_CASE("empty and prohibited resource cells do not acquire bonus growth")
{
    glob2test::HeadlessGlobals globals;
    glob2test::HeadlessGame world(glob2test::GameOptions{.header=true,.seed=719});
    auto& map=world.game.map;
    glob2test::BoundGameRandom random(world.game);
    map.rebuildGrowthCoverage();
    Fertility::applyGrowthOpportunities(map,12,12,4*Fertility::kRateScale,1);
    CHECK(map.getResource(12,12).type==NO_RES_TYPE);
    map.setCellTerrain(12,12,TRAIL);
    auto resource=map.getResource(12,12);
    resource.type=WHEAT;resource.amount=1;resource.variety=0;
    map.replaceResource(12,12,resource);
    CHECK(map.resourceGrowthField().rate(map.coordToIndex(12,12),WHEAT)==0);
    CHECK_FALSE(map.incResource(12,12,WHEAT,0));
    CHECK(map.getResource(12,12).amount==1);
}
}
