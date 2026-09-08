// SPDX-License-Identifier: GPL-3.0-or-later
// A real native simulation peer for browser/native cross-play integration tests.
#include "GlobalContainer.h"
#include "YOGClient.h"
#include "YOGClientGameListManager.h"
#include "MultiplayerGame.h"
#include "MultiplayerGameEventListener.h"
#include <SDL_net.h>
#include <iostream>
#include <stdexcept>

GlobalContainer* globalContainer = nullptr;
class MatchEvents : public MultiplayerGameEventListener {
public:
    bool started = false, ended = false;
    void handleMultiplayerGameEvent(std::shared_ptr<MultiplayerGameEvent> event) override {
        if (event->getEventType() == MGEGameStarted) {
            started = true;
            std::cout << "native match started" << std::endl;
        }
        if (event->getEventType() == MGEGameEndedNormally || event->getEventType() == MGEGameExit) ended = true;
    }
};
int main(int argc, char** argv) {
    if (argc != 2) return 2;
    try {
        SDL_setenv("SDL_VIDEODRIVER", "dummy", 1);
        SDL_setenv("SDL_AUDIODRIVER", "dummy", 1);
        SDL_setenv("GLOB2_CHECKSUM_SIDECAR", "1", 1);
        globalContainer = new GlobalContainer(argv[1]);
        globalContainer->settings.screenWidth = 800;
        globalContainer->settings.screenHeight = 600;
        globalContainer->settings.screenFlags = 0;
        globalContainer->settings.mute = true;
        globalContainer->runNoX = true;
        globalContainer->load();
        globalContainer->automaticEndingGame = true;
        globalContainer->automaticEndingSteps = 250;
        if (SDLNet_Init() != 0) throw std::runtime_error("Network initialization failed");
        {
            auto client = std::make_shared<YOGClient>();
            client->connect("127.0.0.1");
            std::shared_ptr<MultiplayerGame> game;
            MatchEvents events;
            bool ready = false;
            const auto deadline = SDL_GetTicks64() + 60000;
            while (!events.ended && SDL_GetTicks64() < deadline) {
                client->update();
                if (client->getConnectionState() == YOGClient::WaitingForLoginInformation)
                    client->attemptLogin("transportguest", "fixture-only");
                if (!game && client->getLoginState() == YOGLoginSuccessful) {
                    const auto& rooms = client->getGameListManager()->getGameList();
                    if (!rooms.empty()) {
                        game = std::make_shared<MultiplayerGame>(client);
                        client->setMultiplayerGame(game);
                        game->addEventListener(&events);
                        game->joinGame(rooms.front().getGameID());
                    }
                }
                if (game && !events.ended) {
                    game->update();
                    if (game->isFullyInGame() && !ready) {
                        game->setHumanReady(true);
                        ready = true;
                        std::cout << "native peer joined order-rate=" << int(game->getGameHeader().getOrderRate()) << std::endl;
                    }
                }
                SDL_Delay(1);
            }
            if (!events.started || !events.ended) throw std::runtime_error("Native match did not complete");
            std::cout << "native match order-rate=" << int(game->getGameHeader().getOrderRate()) << std::endl;
            game->removeEventListener(&events);
            game->leaveGame();
            client->setMultiplayerGame({});
            client->disconnect();
        }
        delete globalContainer; globalContainer = nullptr;
        SDLNet_Quit();
        std::cout << "native match complete" << std::endl;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
