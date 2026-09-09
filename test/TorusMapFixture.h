// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "Game.h"
#include <cassert>

// A real map with a regular terrain pattern for aspect-ratio visual checks.
// Used only by rendering tests/benchmarks; no simulation or desktop input.
inline void makeTorusMapFixture(Game &game, int width, int height)
{
    assert(width >= 64 && height >= 64 && width <= 512 && height <= 512);
    assert(!(width & (width - 1)) && !(height & (height - 1)));
    int wDec = 0, hDec = 0;
    while ((1 << wDec) < width) ++wDec;
    while ((1 << hDec) < height) ++hDec;
    game.map.setSize(wDec, hDec, GRASS);
    game.map.setGame(&game);
    game.addTeam();
    game.gameHeader.setNumberOfPlayers(1);
    for (int y = 0; y < height; ++y)
        for (int x = 0; x < width; ++x)
            game.map.setUMatPos(x, y, ((x / 4 + y / 4) & 1) ? SAND : GRASS, 1);
}
