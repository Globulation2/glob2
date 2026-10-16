// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "EngineFixtures.h"
#include "engine/sim/snapshot/WorldSnapshot.h"

namespace glob2test
{
// Isolated map-job fixtures explicitly capture their preparation input. This
// neither creates an owner observation nor admits growth, hiring or statistics.
inline void prepareGradientBoundary(Game& game)
{
    if (!game.map.hasPendingGradientPreparation()) return;
    game.snapshotStore().invalidateBoundary();
    game.map.preparePendingGradient(game.snapshotStore().captureBoundary(game));
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
        game.map.advanceGradientPipeline();
        ++game.stepCounter;
        game.map.stagePeriodicGradientPreparation();
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
        game.map.requestBuildingField(building,swim,route);
        return game.map.lookupBuildingField(building,swim,route);
    });
}
}
