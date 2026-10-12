// SPDX-License-Identifier: GPL-3.0-or-later
// A deterministic copy of the existing torus benchmark fixture, serialized by
// the production writer so software and GPU workloads share the same inputs.
#include "GlobalContainer.h"
#include "GameGUI.h"
#include "TorusMapFixture.h"
#include "IntBuildingType.h"
#include "Race.h"
#include "Team.h"
#include "Unit.h"
#include "Player.h"
#include <BinaryStream.h>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <stdexcept>

GlobalContainer *globalContainer = nullptr;
int main(int argc, char **argv)
try
{
    if (argc != 3) throw std::runtime_error("Usage: export-fixture output.game unit-count");
    const int count = std::stoi(argv[2]);
    constexpr int size = 256, columns = size - 4, rows = size - 4;
    if (count < 0 || count > columns * rows) throw std::invalid_argument("Invalid population");
    GlobalContainer globals; globalContainer = &globals; globals.runNoX = true;
    globals.settings.rememberUnit = false;
    globals.buildingsTypes.init(); IntBuildingType::init(); Race::loadDefault();
    GameGUI gui; gui.init(); makeTorusMapFixture(gui.game, size, size);
    for (int team = gui.game.mapHeader.getNumberOfTeams(); team < Team::MAX_COUNT; ++team) gui.game.addTeam();
    GameHeader header; header.setRandomSeed(1); header.setNumberOfPlayers(Team::MAX_COUNT);
    for (int team = 0; team < Team::MAX_COUNT; ++team)
        header.getBasePlayer(team) = BasePlayer(team, "Benchmark", team, BasePlayer::P_LOCAL);
    gui.game.setGameHeader(header, false);
    for (int i = 0; i < count; ++i) {
        const int cell = int(static_cast<long long>(i) * columns * rows / std::max(1, count));
        if (!gui.game.addUnit(2 + cell % columns, 2 + cell / columns, i % Team::MAX_COUNT, i % 3, 0, (i * 17) & 255, 0, 0))
            throw std::runtime_error("Fixture population failed");
    }
    gui.localPlayer = gui.localTeamNo = 0;
    FILE *file = std::fopen(argv[1], "wb"); if (!file) throw std::runtime_error("Cannot open fixture output");
    GAGCore::BinaryOutputStream output(new GAGCore::FileStreamBackend(file));
    gui.save(&output, "Renderer baseline: checkerboard256, seed1"); output.flush();
    std::cout << "fixture size=256x256 seed=1 units=" << count << " checksum=" << gui.game.checkSum(nullptr,nullptr,nullptr,true) << '\n';
    return 0;
}
catch (const std::exception &error) { std::cerr << error.what() << '\n'; return 1; }
