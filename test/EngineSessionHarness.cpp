// SPDX-License-Identifier: GPL-3.0-or-later
#include "Engine.h"
#include "GameSessionScreen.h"
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
            SDL_Event sentinel{};
            sentinel.type = SDL_USEREVENT;
            sentinel.user.code = 7821;
            require(SDL_PushEvent(&sentinel) == 1, "Cannot enqueue host event");
            running = engine.stepSession(now, {});
            SDL_Event retained{};
            require(SDL_PeepEvents(&retained, 1, SDL_GETEVENT, SDL_USEREVENT, SDL_USEREVENT) == 1 &&
                    retained.user.code == 7821, "Explicit session input must not consume the host queue");
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
    {
        auto engine = std::make_unique<Engine>();
        require(engine->initCampaign("maps/balanced.map") == Engine::EE_NO_ERROR, "Stack fixture load failed");
        GAGGUI::ScreenStack screens(*globalContainer->gfx);
        screens.push(std::make_unique<GameSessionScreen>(screens, std::move(engine)));
        unsigned frames = 0;
        while (screens.running()) {
            screens.frame(1000 + frames * 40, {});
            require(++frames <= 60, "Stack-driven session failed to finish");
        }
        require(frames == 51 && screens.result() == GAGGUI::Screen::QUIT_APPLICATION,
                "The stack must drive one session step per frame and defer its completion");
    }
    delete globalContainer;
    SDLNet_Quit();
    std::cout << "PASS: explicit engine sessions, supplied clocks, pacing and completion\n";
}
