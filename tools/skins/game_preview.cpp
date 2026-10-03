// SPDX-License-Identifier: GPL-3.0-or-later
// Capture a saved game's real Scene renderer, with the preview env enabled or off.
#include "GlobalContainer.h"
#include "GameGUI.h"
#include "Team.h"
#include "Unit.h"
#include "Race.h"
#include "Building.h"
#include "render/ColonySkinPreview.h"
#include "map/io/MapHeader.h"
#include <BinaryStream.h>
#include <Toolkit.h>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include "online/SkinDownloads.h"
#include "online/OnlineStorage.h"
#include <nlohmann/json.hpp>
#include <fstream>
#include <SDL3/SDL.h>

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
            std::unique_ptr<Online::OnlineStorage> skinStorage;
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
            const char *moderationCapture = std::getenv("SKIN_PREVIEW_MODERATION_CAPTURE");
            if (moderationCapture)
            {
                globalContainer->settings.clouds = false;
                globalContainer->settings.cloudShadows = false;
                const int bx = (team->startPosX + 4) & (gui.game.map.getW()-1);
                const int by = team->startPosY;
                if (!gui.game.addBuilding(bx, by, globalContainer->buildingsTypes.getFinishedTypeNum("inn"), 0))
                    throw std::runtime_error("Diagnostic inn placement failed");
                for (int y=by; y<by+4; ++y)
                    for (int x=bx; x<bx+4; ++x)
                        gui.game.map.setMapDiscovered(x,y,Team::teamNumberToMask(0));
            }
            if(const char *assignmentPath=std::getenv("SKIN_PREVIEW_ASSIGNMENT"))
            {
                const char *cache=std::getenv("SKIN_PREVIEW_CACHE");
                if(!cache)throw std::runtime_error("Set SKIN_PREVIEW_CACHE for authorized rendering");
                std::ifstream input(assignmentPath);
                const auto assignment=nlohmann::json::parse(input);
                std::vector<Online::SkinDownloads::Ticket> tickets;
                for(const auto &skin:assignment.at("colonySkins"))tickets.push_back({skin.at("team"),skin.at("assertion")});
                skinStorage=Online::makeDirectoryStorage(cache);
                gui.setColonySkins(std::make_unique<Online::SkinDownloads>(*skinStorage,assignment.at("origin"),assignment.at("matchId"),std::move(tickets)));
                for(int frame=0;frame<180;++frame){gui.drawAll(0);globalContainer->gfx->nextFrame();SDL_Delay(16);}
            }
            if (moderationCapture)
            {
                const char *progress = std::getenv("SKIN_PREVIEW_PROGRESS");
                if (!progress) throw std::runtime_error("Set SKIN_PREVIEW_PROGRESS for moderation capture");
                const auto start = SDL_GetTicks();
                int stage = 0;
                std::optional<std::uint32_t> originalColor;
                while (stage < 3 && SDL_GetTicks()-start < 180000)
                {
                    gui.drawAll(0);
                    const auto color = gui.view.render.skinPreview().buildingColor(0);
                    if ((stage==0 && color) || (stage==1 && !color) || (stage==2 && color))
                    {
                        if (stage==0) originalColor=color;
                        if (stage==2 && color!=originalColor) throw std::runtime_error("Restored building color changed");
                        const char *names[]={"authorized","removed","restored"};
                        globalContainer->gfx->printScreen(std::string(moderationCapture)+"-"+names[stage]+".bmp");
                        nlohmann::json status={{"stage",names[stage]},{"elapsedMs",SDL_GetTicks()-start}};
                        if(color)status["buildingColor"]=*color;
                        { std::ofstream state(progress); state << status.dump() << '\n'; }
                        std::cout << status.dump() << std::endl;
                        ++stage;
                    }
                    globalContainer->gfx->nextFrame();
                    SDL_Delay(33);
                }
                if(stage!=3)throw std::runtime_error("Timed out waiting for moderation removal and restoration");
            }
            gui.drawAll(0);
            globalContainer->gfx->printScreen(output);
            globalContainer->gfx->nextFrame();
            if (const char *hidden = std::getenv("SKIN_PREVIEW_HIDDEN_CAPTURE"))
            {
                globalContainer->settings.showColonySkins = false;
                gui.drawAll(0);
                globalContainer->gfx->printScreen(hidden);
                globalContainer->gfx->nextFrame();
                globalContainer->settings.showColonySkins = true;
                gui.drawAll(0);
                globalContainer->gfx->printScreen(std::string(hidden) + ".restored.bmp");
                globalContainer->gfx->nextFrame();
            }
        }
        delete globalContainer;
        globalContainer = nullptr;
    }
    catch (const std::exception &e) { std::cerr << e.what() << '\n'; return 1; }
    return 0;
}
