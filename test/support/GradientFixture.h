// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "EngineFixtures.h"

namespace glob2test
{
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
        game.map.preparePendingGradient();
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
