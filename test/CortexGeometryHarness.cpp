// SPDX-License-Identifier: GPL-3.0-or-later
#include "GlobalContainer.h"
#include "Game.h"
#include "Building.h"
#include "ai/cortex/CortexPlacementGeo.h"
#include <cstdio>
#include <cstdlib>

GlobalContainer* globalContainer = nullptr;

static void require(bool ok)
{
    if (!ok) { std::fprintf(stderr, "Cortex geometry differs from tile-scan oracle\n"); std::exit(1); }
}

static unsigned compare(Game& game)
{
    unsigned checks = 0;
    auto* team = game.teams[0];
    Cortex::PlacementGeometry snapshot(team, game.map);
    // Exhaustive candidates include wrapped, grown, and empty footprints.
    for (int x = -2; x < game.map.getW() + 2; ++x)
        for (int y = -2; y < game.map.getH() + 2; ++y)
            for (int w : {0, 1, 2, 3, 4, 6})
                for (int h : {0, 1, 2, 3, 4, 6})
                {
                    require(snapshot.candidateCrowdsInn(x,y,w,h) ==
                        Cortex::candidateCrowdsInn(&game,team,game.map,x,y,w,h));
                    require(snapshot.candidateOverlapsReservedExpansion(x,y,w,h) ==
                        Cortex::candidateOverlapsReservedExpansion(&game,team,game.map,x,y,w,h));
                    require(snapshot.distanceToNearestBuilding(x,y) ==
                        Cortex::distanceToNearestBuilding(&game,team,x,y));
                    ++checks;
                }
    return checks;
}

int main()
{
    GlobalContainer globals;
    globalContainer = &globals;
    globals.runNoX = true;
    globals.buildingsTypes.init();
    IntBuildingType::init();
    Game game(nullptr);
    game.map.setSize(4, 4, GRASS);
    game.map.setGame(&game);
    game.addTeam();
    unsigned checks = compare(game);
    auto add = [&](int x, int y, int type, int level, bool site) {
        auto id = globals.buildingsTypes.getTypeNum(IntBuildingType::reverseConversionMap[type], level, site);
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
    // Map-only occupants include other teams; corner tiles count on both sides.
    game.map.setBuilding(14,14,1,1,Building::GIDfrom(1,1));
    game.map.setBuilding(3,15,1,1,Building::GIDfrom(2,1));
    checks += compare(game);
    pool->buildingState = Building::DEAD;
    checks += compare(game);
    pool->buildingState = Building::ALIVE;
    std::printf("Cortex geometry: %u candidate comparisons passed\n", checks);
}
