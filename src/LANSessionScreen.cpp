// SPDX-License-Identifier: GPL-3.0-or-later
#include "LANSessionScreen.h"
#include "MultiplayerGameScreen.h"
#include "MessageScreen.h"
#include "YOGClientGameListManager.h"
#include <ScreenStack.h>
#include <GUIButton.h>
#include <GUIText.h>
#include <Toolkit.h>
#include <StringTable.h>
#include <FormatableString.h>

using namespace GAGGUI;
using namespace GAGCore;
namespace {
class LANGameScreen final : public Glob2TabScreen {
    std::shared_ptr<YOGClient> client;
    std::shared_ptr<MultiplayerGame> game;
    MultiplayerGameScreen lobby;
public:
    LANGameScreen(ScreenStack& screens, std::shared_ptr<YOGClient> client, std::shared_ptr<MultiplayerGame> game)
        : Glob2TabScreen(true), client(client), game(game), lobby(this, screens, game, client) {}
    ~LANGameScreen() override {
        if (game->getMultiplayerMode() != MultiplayerGame::NoMode) game->leaveGame();
        client->setMultiplayerGame({});
    }
};
}
LANSessionScreen::LANSessionScreen(ScreenStack& screens, std::shared_ptr<YOGClient> client,
                                 std::string username, std::optional<MapHeader> hostedMap)
    : screens(screens), client(std::move(client)), username(std::move(username)), hostedMap(std::move(hostedMap))
{
    addWidget(new Text(0, 200, ALIGN_FILL, ALIGN_SCREEN_CENTERED, "standard",
        Toolkit::getStringTable()->getString("[connecting to game]")));
    addWidget(new TextButton(240, 280, 160, 35, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "standard",
        Toolkit::getStringTable()->getString("[Cancel]"), 0, 27));
}
LANSessionScreen::~LANSessionScreen() {
    if (auto game = client->getMultiplayerGame()) game->leaveGame();
    client->setMultiplayerGame({});
    client->disconnect();
}
void LANSessionScreen::onAction(Widget*, Action action, int, int) {
    if (action == BUTTON_RELEASED || action == BUTTON_SHORTCUT) endExecute(0);
}
void LANSessionScreen::fail(const char* message) {
    stage = Stage::Failed;
    screens.push(std::make_unique<MessageScreen>(Toolkit::getStringTable()->getString(message),
        std::vector<std::string>{Toolkit::getStringTable()->getString("[ok]")}),
        [this](Screen&, int) { endExecute(1); });
}
void LANSessionScreen::onTimer(Uint32 tick) {
    if (stage == Stage::Lobby || stage == Stage::Failed) return;
    if (!stageStarted) stageStarted = tick;
    client->update();
    if ((!client->isConnecting() && !client->isConnected()) || Uint32(tick - *stageStarted) >= 10000) {
        fail("[Can't connect, can't find host]"); return;
    }
    const auto connection = client->getConnectionState();
    if (stage == Stage::Greeting && connection == YOGClient::WaitingForLoginInformation) {
        client->attemptLogin(username);
        stage = Stage::Login; stageStarted = tick;
    } else if (stage == Stage::Login) {
        if (connection == YOGClient::WaitingForLoginInformation) {
            fail("[Can't connect, can't find host]"); return;
        }
        if (connection == YOGClient::ClientOnStandby) {
            stage = Stage::GameList; stageStarted = tick;
            if (hostedMap) enterLobby();
        }
    } else if (stage == Stage::GameList && !client->getGameListManager()->getGameList().empty()) {
        if (client->getGameListManager()->getGameList().front().getGameState() == YOGGameInfo::GameRunning)
            fail("[Can't join game, game has started]");
        else enterLobby();
    }
}
void LANSessionScreen::enterLobby() {
    auto game = std::make_shared<MultiplayerGame>(client);
    client->setMultiplayerGame(game);
    if (hostedMap) {
        game->createNewGame(FormattableString(Toolkit::getStringTable()->getString("[%0's game]")).arg(username));
        game->setMapHeader(*hostedMap);
    } else game->joinGame(client->getGameListManager()->getGameList().front().getGameID());
    stage = Stage::Lobby;
    screens.push(std::make_unique<LANGameScreen>(screens, client, game),
        [this](Screen&, int result) { endExecute(result); });
}
