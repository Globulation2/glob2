// SPDX-License-Identifier: GPL-3.0-or-later
#include "Engine.h"
#include "GlobalContainer.h"
#include <SDL_net.h>
#include <cstdio>
#include <stdexcept>
#include <filesystem>

GlobalContainer* globalContainer=nullptr;
int main()
{
    if(!SDL_getenv("GLOB2_USER_DATA_DIR")) {
        std::fprintf(stderr,"An isolated GLOB2_USER_DATA_DIR is required\n");return 2;
    }
    SDL_setenv("SDL_AUDIODRIVER","dummy",1);
    try {
        globalContainer=new GlobalContainer("glob2-mobile-scene");
        globalContainer->settings.screenWidth=800;
        globalContainer->settings.screenHeight=600;
        globalContainer->settings.screenFlags=GAGCore::GraphicContext::PORTABLEGPU;
        globalContainer->settings.mute=true;
        globalContainer->load();
        if(SDLNet_Init()!=0) throw std::runtime_error(SDL_GetError());
        globalContainer->automaticEndingGame=true;
        globalContainer->automaticEndingSteps=50;
        globalContainer->automaticGameGlobalEndConditions=true;
        {
            Engine engine;
            if(engine.initCampaign("maps/balanced.map")!=Engine::EE_NO_ERROR) throw std::runtime_error("Fixture load failed");
            Uint64 now=1000;
            engine.beginSession(now);
            unsigned frames=0;
            while(engine.stepSession(now,{})) {
                if(++frames>1000) throw std::runtime_error("Fixture stalled");
                if(frames==40) globalContainer->gfx->printScreen("portable-scene.bmp");
                engine.drawSession();
                now+=40;
            }
            if(!std::filesystem::exists(std::filesystem::path(SDL_getenv("GLOB2_USER_DATA_DIR"))/"portable-scene.bmp"))
                throw std::runtime_error("Scene screenshot was not produced");
            engine.finishSession();
        }
        delete globalContainer;globalContainer=nullptr;SDLNet_Quit();
        std::puts("PASS: portable renderer loaded and drew a complete 50-tick game scene");
        return 0;
    } catch(const std::exception& error) {std::fprintf(stderr,"FAIL: %s\n",error.what());return 1;}
}
