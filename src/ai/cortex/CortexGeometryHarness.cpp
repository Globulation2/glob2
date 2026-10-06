// SPDX-License-Identifier: GPL-3.0-or-later
#include "EngineFixtures.h"
#include "GlobalContainer.h"
#include "Game.h"
#include "Building.h"
#include "ai/cortex/CortexPlacementGeo.h"
#include "CortexBuildings.h"
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
    Cortex::World observed(*captured);
    auto* observedTeam=observed.teams[0];
    Cortex::PlacementGeometry snapshot(observedTeam, observed.map);
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
                        Cortex::candidateCrowdsInn(&observed,observedTeam,observed.map,x,y,w,h));
                    require(snapshot.candidateOverlapsReservedExpansion(x,y,w,h) ==
                        Cortex::candidateOverlapsReservedExpansion(&observed,observedTeam,observed.map,x,y,w,h));
                    require(snapshot.distanceToNearestBuilding(x,y) ==
                        Cortex::distanceToNearestBuilding(&observed,observedTeam,x,y));
                    int edge = -1, swarm = -1, inn = -1;
                    for (int i=0;i<Building::MAX_COUNT;++i)
                    {
                        const auto* b=team->myBuildings[i];
                        if (!b || b->buildingState==Building::DEAD || !b->type) continue;
                        const int gap=Cortex::rectEdgeChebyshev(x,w,y,h,b->posX,b->type->width,
                            b->posY,b->type->height,game.map.getW(),game.map.getH());
                        if (edge<0 || gap<edge) edge=gap;
                        const int distance=game.map.warpDistMax(x,y,b->posX,b->posY);
                        if (Cortex::servesRole(game,*b->type,Cortex::CORTEX_BUILD_SWARM) && (swarm<0 || distance<swarm)) swarm=distance;
                        if (Cortex::servesRole(game,*b->type,Cortex::CORTEX_BUILD_FOOD) && (inn<0 || distance<inn)) inn=distance;
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
        Cortex::World observed(*captured);
        const std::array coordinates{std::numeric_limits<int>::min(),-4097,-33,-1,0,1,31,32,4097,std::numeric_limits<int>::max()};
        const auto axis=[](int a,int b,int period) {
            auto wrap=[&](int v){int r=v%period;return r<0?r+period:r;};
            int delta=std::abs(wrap(a)-wrap(b));return std::min(delta,period-delta);
        };
        for(int x:coordinates)for(int y:coordinates)for(int xx:coordinates)for(int yy:coordinates)
            CHECK(observed.map.warpDistMax(x,y,xx,yy)==std::max(axis(x,xx,32),axis(y,yy,32)));
        auto type=*game.buildingsTypes.get(game.buildingsTypes.getTypeNum("inn",0,false));
        type.width=2;type.height=2;type.isVirtual=false;
        CHECK(observed.checkRoomForBuilding(10,10,&type,0));
        game.map.setBuilding(11,11,1,1,Building::GIDfrom(0,7));
        captured=AIEngine::AIWorldView::capture(game, AIEngine::AIWorldView::captureCatalog(game));
        Cortex::World blocked(*captured);
        CHECK_FALSE(blocked.checkRoomForBuilding(10,10,&type,0));
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
        checks += compare(game);
        game.teams[0]->myBuildings[slot] = pool;
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
