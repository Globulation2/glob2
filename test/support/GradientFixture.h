// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "GameFixtures.h"
#include "gradient/MapRouting.h"
#include "engine/sim/snapshot/WorldSnapshot.h"

namespace glob2test
{
// Isolated map-job fixtures explicitly capture their preparation input. This
// neither creates an owner observation nor admits growth, hiring or statistics.
inline void prepareGradientBoundary(Game& game)
{
    if (!game.map.routing().hasPendingGradientPreparation()) return;
    game.snapshotStore().invalidateBoundary();
    game.map.routing().preparePendingGradient(game.snapshotStore().captureBoundary(game));
}

// Build custom terrain before match entities exist. The editor authoring API
// accepts a detached map; always restore its Game even if definitions reject.
inline void importTerrainBeforeMatch(Game& game,std::string_view definitions)
{
    for (int team=0;team<game.teamsCount();++team) {
        REQUIRE(game.teams[team]->liveUnits.empty());
        REQUIRE(game.teams[team]->liveBuildings.empty());
    }
    auto& map=game.map;
    {
        struct RestoreGame {
            Map& map; Game* previous;
            ~RestoreGame() { map.game=previous; }
        } restore{map,map.game};
        map.game=nullptr;
        map.importTerrainDefinitions(definitions);
    }
    map.setGame(&game);
}

// Field-specific fixtures can advance completed-world publication boundaries
// without moving units. The read must enroll demand and return a field pointer.
// Real game/continuation tests should advance normal simulation ticks instead.
template<class Read> auto awaitGradient(Game& game, Read read)
{
    auto field=read();
    for (unsigned boundary=0; !field && boundary<4096; ++boundary)
    {
        ++game.stepCounter;
        game.map.routing().advanceGradientPipeline();
        game.map.routing().stagePeriodicGradientPreparation();
        prepareGradientBoundary(game);
        field=read();
    }
    REQUIRE_MESSAGE(field!=nullptr,"gradient did not publish within the fixture boundary limit");
    return field;
}

inline const Uint16* awaitBuildingGradient(Game& game, Building* building, int swim,
    BuildingRoute route=BuildingRoute::Automatic)
{
    return awaitGradient(game,[&] {
        game.map.routing().requestBuildingField(building,movementAccessFromLegacy(swim),route);
        return game.map.routing().lookupBuildingField(building,movementAccessFromLegacy(swim),route);
    });
}

// AI planning semantics for a material target, the oracle that AI observation
// adapters mirror: reachable by the published field, then the destination the
// published plane records at (x,y) (deliberately the published field's age,
// with no current-stock validation), else the legacy gradient ascent. Workers
// instead use Map::projectMaterialDestination.
inline bool materialPlanningTarget(Map& map,int team,int material,MovementAccess access,int x,int y,
    Sint32* targetX,Sint32* targetY,int* distance=nullptr,bool markets=false,const Building* consumer=nullptr)
{
    if (map.routing().queryMaterialDistance(team,material,access,x,y,distance,markets,consumer)!=RouteResult::Reachable) return false;
    const auto* field=map.routing().getMaterialGradientSlot(team,material,access,markets,consumer);
    if (const auto destinations=map.routing().materialGradientDestinations(field)) {
        const auto index=map.coordToIndex(x,y);
        *targetX=destinations->x[index]; *targetY=destinations->y[index];
        return true;
    }
    map.routing().getGlobalGradientDestination(field,x,y,targetX,targetY);
    return true;
}
// No neighbour of (x,y) holds a strictly higher gradient value.
inline bool isGradientPeak(const Map& map,const Uint16* gradient,int x,int y)
{
    const auto here=gradient[map.coordToIndex(x,y)];
    for (int dy=-1;dy<=1;++dy) for (int dx=-1;dx<=1;++dx)
        if ((dx || dy) && gradient[map.coordToIndex(x+dx,y+dy)]>here) return false;
    return true;
}
}
