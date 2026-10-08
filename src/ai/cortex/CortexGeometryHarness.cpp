// SPDX-License-Identifier: GPL-3.0-or-later
#include "EngineFixtures.h"
#include "GlobalContainer.h"
#include "Game.h"
#include "Building.h"
#include "ai/cortex/CortexPlacementGeo.h"
#include "CortexBuildings.h"
#include "CortexPlacement.h"
#include "CortexFoodAvailability.h"
#include "CortexHardSpaceView.h"
#include "ai/observation/AIWorldView.h"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <limits>

namespace
{
static void require(bool ok)
{
    REQUIRE_MESSAGE(ok, "Cortex geometry differs from tile-scan oracle");
}

static unsigned compare(Game& game)
{
    unsigned checks = 0;
    auto* team = game.teams[0];
    const auto captured=AIEngine::AIWorldView::capture(game, AIEngine::AIWorldView::captureCatalog(game));
    Cortex::PlanningIntent intents;
    const auto* observedTeam=&captured->teams[0];
    Cortex::PlacementGeometry snapshot(observedTeam,*captured,intents);
    // Compare the mask against the independent per-building distance query,
    // including footprints/radii that wrap or cover the whole map.
    for (int w : {1, 2, 6, 20})
        for (int h : {1, 3, 6, 20})
            for (int gap : {0, 1, 4, 20})
            {
                const auto mask = snapshot.buildingProximityMask(w, h, gap);
                for (int y = 0; y < game.map.getH(); ++y)
                    for (int x = 0; x < game.map.getW(); ++x)
                    {
                        const int distance = snapshot.nearestBuildingEdgeDist(x, y, w, h);
                        require((mask.empty() || mask[y * game.map.getW() + x]) ==
                            (distance < 0 || distance <= gap));
                        ++checks;
                    }
            }
    // Exhaustive candidates include wrapped, grown, and empty footprints.
    for (int x = -2; x < game.map.getW() + 2; ++x)
        for (int y = -2; y < game.map.getH() + 2; ++y)
            for (int w : {0, 1, 2, 3, 4, 6})
                for (int h : {0, 1, 2, 3, 4, 6})
                {
                    require(snapshot.candidateCrowdsInn(x,y,w,h) ==
                        Cortex::candidateCrowdsInn(captured.get(),observedTeam,intents,*captured,x,y,w,h));
                    require(snapshot.candidateOverlapsReservedExpansion(x,y,w,h) ==
                        Cortex::candidateOverlapsReservedExpansion(captured.get(),observedTeam,intents,*captured,x,y,w,h));
                    require(snapshot.distanceToNearestBuilding(x,y) ==
                        Cortex::distanceToNearestBuilding(captured.get(),observedTeam,intents,x,y));
                    int edge = -1, swarm = -1, inn = -1;
                    for (int i=0;i<Building::MAX_COUNT;++i)
                    {
                        const auto* b=team->myBuildings[i];
                        if (!b || b->buildingState==Building::DEAD || !b->type) continue;
                        const int gap=Cortex::rectEdgeChebyshev(x,w,y,h,b->posX,b->type->width,
                            b->posY,b->type->height,game.map.getW(),game.map.getH());
                        if (edge<0 || gap<edge) edge=gap;
                        const int distance=game.map.warpDistMax(x,y,b->posX,b->posY);
                        if (Cortex::servesRole(*captured,*b->type,Cortex::CORTEX_BUILD_SWARM) && (swarm<0 || distance<swarm)) swarm=distance;
                        if (Cortex::servesRole(*captured,*b->type,Cortex::CORTEX_BUILD_FOOD) && (inn<0 || distance<inn)) inn=distance;
                    }
                    require(snapshot.nearestBuildingEdgeDist(x,y,w,h)==edge);
                    require(snapshot.distanceToNearestBuildingType(x,y,IntBuildingType::SWARM_BUILDING)==swarm);
                    require(snapshot.distanceToNearestBuildingType(x,y,IntBuildingType::FOOD_BUILDING)==inn);
                    ++checks;
                }
    return checks;
}
}

TEST_SUITE("CortexGeometry")
{
    TEST_CASE("snapshot distances preserve wrapped inputs and discovery never bypasses legality")
    {
        glob2test::HeadlessGlobals globals;
        glob2test::HeadlessGame fixture{glob2test::GameOptions{.wDec=5, .hDec=5, .teams=1, .discovered=true, .clearImmobile=true, .loadDefaultRace=true}};
        auto& game=fixture.game;
        auto captured=AIEngine::AIWorldView::capture(game, AIEngine::AIWorldView::captureCatalog(game));
        Cortex::PlanningIntent intents;
        const std::array coordinates{std::numeric_limits<int>::min(),-4097,-33,-1,0,1,31,32,4097,std::numeric_limits<int>::max()};
        const auto axis=[](int a,int b,int period) {
            auto wrap=[&](int v){int r=v%period;return r<0?r+period:r;};
            int delta=std::abs(wrap(a)-wrap(b));return std::min(delta,period-delta);
        };
        for(int x:coordinates)for(int y:coordinates)for(int xx:coordinates)for(int yy:coordinates)
            CHECK(Cortex::warpDistMax(*captured,x,y,xx,yy)==std::max(axis(x,xx,32),axis(y,yy,32)));
        auto type=*game.buildingsTypes.get(game.buildingsTypes.getTypeNum("inn",0,false));
        type.width=2;type.height=2;type.isVirtual=false;
        CHECK(Cortex::checkRoomForBuilding(*captured,10,10,&type,0,intents));
        game.map.setBuilding(11,11,1,1,Building::GIDfrom(0,7));
        captured=AIEngine::AIWorldView::capture(game, AIEngine::AIWorldView::captureCatalog(game));
        CHECK_FALSE(Cortex::checkRoomForBuilding(*captured,10,10,&type,0,intents));
    }

	TEST_CASE("placement geometry matches the tile-scan oracle")
	{
		glob2test::HeadlessGlobals globals;
	    Game game(nullptr);
	    game.map.setSize(4, 4, GRASS);
	    game.map.setGame(&game);
	    game.addTeam();
	    unsigned checks = compare(game);
	    auto add = [&](int x, int y, int type, int level, bool site) {
	        auto id = globals->buildingsTypes.getTypeNum(IntBuildingType::reverseConversionMap[type], level, site);
	        require(id >= 0);
	        auto* b = game.addBuilding(x, y, id, 0);
	        require(b != nullptr);
	        return b;
	    };
	    add(15,15,IntBuildingType::FOOD_BUILDING,0,false);
	    add(6,6,IntBuildingType::FOOD_BUILDING,1,true);
	    auto* pool = add(2,9,IntBuildingType::SWIMSPEED_BUILDING,0,false);
	    add(10,2,IntBuildingType::WALKSPEED_BUILDING,1,false);
	    checks += compare(game);
        const auto previousTypeNum = pool->typeNum;
        auto* previousType = pool->type;
        // An absent entity is omitted by both the live oracle and extraction.
        // A null type pointer is not a valid serialized simulation state.
        const auto slot = Building::GIDtoID(pool->gid);
        game.teams[0]->myBuildings[slot] = nullptr;
        game.teams[0]->rebuildLiveLists();
        checks += compare(game);
        game.teams[0]->myBuildings[slot] = pool;
        game.teams[0]->rebuildLiveLists();
        pool->typeNum = game.buildingsTypes.getTypeNum(
            IntBuildingType::reverseConversionMap[IntBuildingType::SWARM_BUILDING], 0, false);
        pool->type = game.buildingsTypes.get(pool->typeNum);
        checks += compare(game);
        pool->typeNum = previousTypeNum;
        pool->type = previousType;
	    // Map-only occupants include other teams; corner tiles count on both sides.
	    game.map.setBuilding(14,14,1,1,Building::GIDfrom(1,1));
	    game.map.setBuilding(3,15,1,1,Building::GIDfrom(2,1));
	    checks += compare(game);
	    pool->buildingState = Building::DEAD;
	    checks += compare(game);
	    pool->buildingState = Building::ALIVE;
	    MESSAGE("Cortex geometry: " << checks << " candidate comparisons");
	}
}

TEST_CASE("placement food snapshot matches scalar stock queries and refreshes per pass" * doctest::test_suite("CortexGeometry"))
{
    glob2test::HeadlessGlobals globals;
    Game game(nullptr);auto& map=game.map;map.setSize(4,3,GRASS);map.setGame(&game);
    auto definitions=nlohmann::json::parse(map.resourceRegistry().serialize());
    auto entry=definitions["resources"][0];
    entry.erase("requiredExperiment");
    entry["properties"]={{"primaryMaterial","wood"},{"persistsWhenEmpty",true},
        {"blocksGround",true},{"visibleToHarvest",true}};
    entry["yields"]={{"wood",{{"capacity",5},{"initial",5},{"consumption","one"}}},
        {"food",{{"capacity",4},{"initial",4},{"consumption","one"}}}};
    while(definitions["resources"].size()<260) {
        // Fixed-width keys sort after builtin names, so the last authored ID
        // really exercises the wide runtime resource index.
        entry["key"]="zz-test:cortex-food-"+std::to_string(1000+definitions["resources"].size());
        definitions["resources"].push_back(entry);
    }
    map.installResourceDefinitions(definitions.dump());
    const auto id=*map.resourceRegistry().find("zz-test:cortex-food-1259");
    REQUIRE(resourceIndex(id)>255);
    for(const auto point:std::vector<std::pair<int,int>>{{0,0},{15,7},{7,3}})
        map.setResource(point.first,point.second,id,0);
    map.setMaterialAmount(map.coordToIndex(7,3),MaterialId::Food,0);
    map.setAreaMask(map.coordToIndex(0,0),&Tile::forbidden,~Uint32(0));
    const auto compare=[&] {
        const auto observed=AIEngine::AIWorldView::capture(game,AIEngine::AIWorldView::captureCatalog(game));
        const Cortex::FoodAvailabilityView view(map.stateView());
        for(int y=-9;y<17;y+=3) for(int x=-17;x<34;x+=5) {
            for(int cap:{-1,0,1,4,20}) {
                int expected=-1;
                for(int dy=-cap;dy<=cap;++dy) for(int dx=-cap;dx<=cap;++dx)
                    if(map.materialAmountAt(map.coordToIndex(x+dx,y+dy),MaterialId::Food)>0) {
                        const int distance=std::max(std::abs(dx),std::abs(dy));
                        if(expected<0 || distance<expected) expected=distance;
                    }
                CHECK(view.nearestDistance(x,y,cap)==expected);
                CHECK(Cortex::nearestFoodSourceDistance(*observed,x,y,cap)==expected);
            }
            for(int w:{0,1,3,19}) for(int h:{0,2,10}) for(int distance:{0,1,9}) {
                bool expected=false;
                for(int dy=-distance;dy<h+distance;++dy) for(int dx=-distance;dx<w+distance;++dx)
                    expected|=map.materialAmountAt(map.coordToIndex(x+dx,y+dy),MaterialId::Food)>0;
                CHECK(view.anyWithin(x,y,w,h,distance)==expected);
                CHECK(Cortex::anyFoodSourceWithin(*observed,x,y,w,h,distance)==expected);
            }
        }
        CHECK_FALSE(view.at(7,3)); // Other stock keeps this mixed deposit alive.
    };
    compare();
    map.setMaterialAmount(map.coordToIndex(0,0),MaterialId::Food,0);
    map.setMaterialAmount(map.coordToIndex(15,7),MaterialId::Food,0);
    compare(); // New pass must see depletion; no persistent availability cache.
    map.setMaterialAmount(map.coordToIndex(15,7),MaterialId::Food,4);
    compare();
}

TEST_CASE("placement food snapshot matches toroidal scalar distances on thin maps" * doctest::test_suite("CortexGeometry"))
{
    glob2test::HeadlessGlobals globals;
    for(const auto dimensions:std::vector<std::pair<int,int>>{{0,0},{0,3},{3,0},{1,1},{4,2}}) {
        Game game(nullptr);auto& map=game.map;
        map.setSize(dimensions.first,dimensions.second,GRASS);map.setGame(&game);
        const auto food=map.resourceRegistry().find("wheat");
        REQUIRE(food.has_value());
        const auto compare=[&] {
            const Cortex::FoodAvailabilityView view(map.stateView());
            for(int y=-map.getH()-1;y<=map.getH()+1;++y)
                for(int x=-map.getW()-1;x<=map.getW()+1;++x) {
                    int expected=-1;
                    for(int sy=0;sy<map.getH();++sy) for(int sx=0;sx<map.getW();++sx)
                        if(map.materialAmountAt(map.coordToIndex(sx,sy),MaterialId::Food)>0) {
                            int dx=std::abs((x&(map.getW()-1))-sx);
                            int dy=std::abs((y&(map.getH()-1))-sy);
                            dx=std::min(dx,map.getW()-dx);dy=std::min(dy,map.getH()-dy);
                            const int distance=std::max(dx,dy);
                            if(expected<0 || distance<expected) expected=distance;
                        }
                    for(int cap:{-1,0,1,3,40})
                        CHECK(view.nearestDistance(x,y,cap)==(expected>=0 && expected<=cap?expected:-1));
                }
        };
        compare(); // No source, including a cap larger than either dimension.
        map.setResource(map.getW()-1,map.getH()-1,*food,0);
        compare();
        map.setResource(0,0,*food,0);
        compare(); // Aliased neighbors and tied sources do not change distance.
    }
}

TEST_CASE("lazy placement hard space matches canonical rectangles and refreshes per pass" * doctest::test_suite("CortexGeometry"))
{
    glob2test::HeadlessGlobals globals;
    for(const auto dimensions:std::vector<std::pair<int,int>>{{0,0},{0,3},{3,0},{4,3}}) {
        Game game(nullptr);auto& map=game.map;
        map.setSize(dimensions.first,dimensions.second,GRASS);map.setGame(&game);
        auto definitions=nlohmann::json::parse(map.resourceRegistry().serialize());
        auto entry=definitions["resources"][0];entry.erase("requiredExperiment");
        entry["key"]="test:ground-only";
        entry["properties"]={{"blocksGround",true},{"blocksBuilding",false},{"persistsWhenEmpty",true}};
        definitions["resources"].push_back(entry);
        entry["key"]="test:building-only";
        entry["properties"]["blocksGround"]=false;entry["properties"]["blocksBuilding"]=true;
        definitions["resources"].push_back(entry);map.installResourceDefinitions(definitions.dump());
        const auto ground=map.resourceRegistry().find("test:ground-only");
        const auto building=map.resourceRegistry().find("test:building-only");
        REQUIRE(ground.has_value());REQUIRE(building.has_value());
        const auto compare=[&] {
            const auto cells = map.cellView(); Cortex::HardSpaceView view(cells);
            for(int y=-2;y<map.getH()+2;++y) for(int x=-2;x<map.getW()+2;++x)
                for(int w:{0,1,3,19}) for(int h:{0,1,4,11})
                    CHECK(view.rectangle(x,y,w,h)==map.isHardSpaceForBuilding(x,y,w,h));
        };
        compare();
        map.setResource(0,0,*ground,0);map.setGroundUnit(0,0,0);
        map.setAreaMask(map.coordToIndex(0,0),&Tile::forbidden,~Uint32(0));
        {const auto cells = map.cellView(); Cortex::HardSpaceView view(cells);CHECK(view.at(0,0));} // Units, fog and paint ignored.
        compare();
        map.setResource(0,0,*building,0);
        {const auto cells = map.cellView(); Cortex::HardSpaceView view(cells);CHECK_FALSE(view.at(0,0));}
        compare();
        // Restore the authored ground-blocking resource before adding occupancy;
        // the placement API correctly rejects it while the earlier unit remains.
        map.setGroundUnit(0,0,NOGUID);
        map.setResource(0,0,*ground,0);map.setBuilding(0,0,1,1,0);
        {const auto cells = map.cellView(); Cortex::HardSpaceView view(cells);CHECK_FALSE(view.at(0,0));} // No ignored occupant.
        compare();
        map.setBuilding(0,0,1,1,NOGBID);map.paintCell(0,0,WATER);
        {const auto cells = map.cellView(); Cortex::HardSpaceView view(cells);CHECK_FALSE(view.at(0,0));}
        compare();
        map.paintCell(0,0,GRASS);
        {const auto cells = map.cellView(); Cortex::HardSpaceView view(cells);CHECK(view.at(0,0));} // A fresh pass sees mutations.
        compare();
    }
}
