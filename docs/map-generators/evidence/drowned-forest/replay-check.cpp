#include "Engine.h"
#include "GlobalContainer.h"
#include <SDL.h>
#include <SDL_net.h>
#include <cassert>
#include <cstdlib>
GlobalContainer *globalContainer = nullptr;
int main(int argc, char **argv)
{
    assert(argc == 2);
    SDL_SetMainReady();
    setenv("GLOB2_USER_DIR", argv[1], 1);
    GlobalContainer globals("glob2-drowned-replay-check");
    globalContainer = &globals;
    globals.runNoX = true;
    globals.structuredHeadless = true;
    globals.automaticEndingGame = true;
    globals.automaticEndingSteps = 4096;
    globals.automaticGameGlobalEndConditions = false;
    globals.load();
    assert(SDLNet_Init() == 0);
    setenv("GLOB2_CHECKSUM_SIDECAR", "1", 1);
    Engine engine;
    assert(engine.loadReplay("replays/game.replay") == Engine::EE_NO_ERROR);
    engine.run();
    return 0;
}
