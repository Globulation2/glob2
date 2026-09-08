// SPDX-License-Identifier: GPL-3.0-or-later
#include "Engine.h"
#include "GlobalContainer.h"
#include <SDL_net.h>
#include <iostream>
#include <stdexcept>

GlobalContainer* globalContainer = nullptr;
void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
int main(int argc, char** argv)
{
    require(argc == 2, "A disposable profile is required");
    SDL_setenv("SDL_VIDEODRIVER", "dummy", 1);
    SDL_setenv("SDL_AUDIODRIVER", "dummy", 1);
    globalContainer = new GlobalContainer(argv[1]);
    globalContainer->settings.screenWidth = 800;
    globalContainer->settings.screenHeight = 600;
    globalContainer->settings.screenFlags = 0;
    globalContainer->settings.mute = true;
    globalContainer->settings.gameSpeed = 0;
    globalContainer->load();
    require(SDLNet_Init() == 0, "SDL networking init failed");
    globalContainer->automaticEndingGame = true;
    globalContainer->automaticEndingSteps = 50;
    globalContainer->automaticGameGlobalEndConditions = true;
    for (bool delayed : {false, true}) {
        Engine engine;
        require(engine.initCampaign("maps/balanced.map") == Engine::EE_NO_ERROR, "Map load failed");
        Uint64 now = 1000;
        engine.beginSession(now);
        bool rejected = false;
        try { engine.beginSession(now); } catch (const std::logic_error&) { rejected = true; }
        require(rejected, "Double session start must be rejected");
        rejected = false;
        try { engine.finishSession(); } catch (const std::logic_error&) { rejected = true; }
        require(rejected, "Running session cannot be finalized");
        int iterations = 0;
        bool running;
        do {
            running = engine.stepSession(now);
            engine.drawSession();
            const Uint32 delay = engine.sessionDelay(now);
            require(delay == engine.sessionDelay(now), "Delay queries must not advance the timing budget");
            if (!delayed) require(delay == 40, "Regular callbacks must retain 25 Hz pacing");
            now += delayed ? 1000 : delay;
            require(++iterations <= 100, "Session failed to terminate");
        } while (running);
        require(iterations == 50, "Callback cadence changed simulation advancement");
        require(!engine.stepSession(now), "Completed session must not advance");
        require(!engine.finishSession(), "Finished fixture must not request another game");
        rejected = false;
        try { engine.stepSession(now); } catch (const std::logic_error&) { rejected = true; }
        require(rejected, "A finalized session must reject advancement");
    }
    delete globalContainer;
    SDLNet_Quit();
    std::cout << "PASS: explicit engine sessions, supplied clocks, pacing and completion\n";
}
