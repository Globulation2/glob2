// SPDX-License-Identifier: GPL-3.0-or-later
// Capture a saved game's real Scene renderer, with the preview env enabled or off.
#include "GlobalContainer.h"
#include "GameGUI.h"
#include "Team.h"
#include "Unit.h"
#include "map/io/MapHeader.h"
#include <BinaryStream.h>
#include <Toolkit.h>
#include <cstdlib>
#include <iostream>
#include <stdexcept>

GlobalContainer *globalContainer = nullptr;

int main(int argc, char **argv)
{
    const char *save = std::getenv("SKIN_PREVIEW_SAVE");
    const char *output = std::getenv("SKIN_PREVIEW_CAPTURE");
    if (!save || !output) { std::cerr << "Set SKIN_PREVIEW_SAVE and SKIN_PREVIEW_CAPTURE\n"; return 2; }
    try
    {
        globalContainer = new GlobalContainer;
        globalContainer->parseArgs(argc,argv);
        globalContainer->settings.mute = 1;
        globalContainer->settings.autosaveGames = false;
        globalContainer->load();
        {
            GameGUI gui;
            GAGCore::BinaryInputStream stream(glob2OpenMapOrSaveInputStreamBackend(*GAGCore::Toolkit::getFileManager(), save));
            if (!gui.load(&stream,true)) throw std::runtime_error("Cannot load preview save");
            gui.localPlayer = gui.localTeamNo = 0;
            gui.adjustLocalTeam();
            gui.adjustInitialViewport();
            // A tick-zero map may have no units yet. Place a diagnostic row of
            // workers beside the colony to exercise both draw paths together.
            if (gui.game.mapHeader.getNumberOfTeams() < 2) throw std::runtime_error("Preview needs two colonies");
            const auto *team = gui.game.teams[0];
            for (int i=0; i<8; ++i)
            {
                const int x = (team->startPosX - 4 + i) & (gui.game.map.getW()-1);
                const int y = (team->startPosY + 4) & (gui.game.map.getH()-1);
                if (auto *unit = gui.game.addUnit(x,y,i<4 ? 0 : 1,WORKER,0,0,0,0))
                { unit->direction = i; gui.game.map.setMapDiscovered(x,y,Team::teamNumberToMask(0)); }
                else throw std::runtime_error("Diagnostic worker placement failed");
            }
            gui.drawAll(0);
            globalContainer->gfx->printScreen(output);
            globalContainer->gfx->nextFrame();
        }
        delete globalContainer;
        globalContainer = nullptr;
    }
    catch (const std::exception &e) { std::cerr << e.what() << '\n'; return 1; }
    return 0;
}
