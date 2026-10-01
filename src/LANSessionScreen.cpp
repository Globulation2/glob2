// SPDX-License-Identifier: GPL-3.0-or-later
#include "LANSessionScreen.h"
#include "MultiplayerGameScreen.h"
#include "MessageScreen.h"
#include "YOGClientGameListManager.h"
#include <ScreenStack.h>
#include "SessionTabsScreen.h"
#include "YOGServer.h"
#include <Toolkit.h>
#include <StringTable.h>
#include <FormatableString.h>

using namespace GAGGUI;
using namespace GAGCore;
namespace
{
class LANGameScreen final : public SessionTabsScreen
{
	std::shared_ptr<YOGClient> client;
	std::shared_ptr<MultiplayerGame> game;
	MultiplayerGameScreen room;

  public:
	LANGameScreen(ScreenStack &screens, std::shared_ptr<YOGClient> client, std::shared_ptr<MultiplayerGame> game)
		: client(client), game(game), room(screens, game, client)
	{
		addTab(&room, true);
	}
	~LANGameScreen() override
	{
		removeTab(&room);
		if (game->getMultiplayerMode() != MultiplayerGame::NoMode)
			game->leaveGame();
		client->setMultiplayerGame({});
	}
};
} // namespace
LANSessionScreen::LANSessionScreen(ScreenStack &screens, std::shared_ptr<YOGClient> client,
								   std::string username, std::optional<MapHeader> hostedMap)
	: screens(screens), client(std::move(client)), username(std::move(username)),
	  hostedMap(std::move(hostedMap))
{
}
Glob2UI::Element LANSessionScreen::build(const Glob2UI::Presentation &p)
{
	using namespace Glob2UI;
	return page("", center(paragraph(tr("[connecting to game]"), {FontRole::Body, false, TextAlign::Center})),
				actions({{"cancel", tr("[Cancel]"), [this] { endExecute(0); }, false, SDLK_ESCAPE}}, p), p, 480);
}
LANSessionScreen::~LANSessionScreen()
{
	if (auto game = client->getMultiplayerGame())
		game->leaveGame();
	client->setMultiplayerGame({});
	client->disconnect();
}
void LANSessionScreen::fail(const char *message)
{
	stage = Stage::Failed;
	screens.push(std::make_unique<MessageScreen>(
					 Toolkit::getStringTable()->getString(message),
					 std::vector<std::string>{Toolkit::getStringTable()->getString("[ok]")}),
				 [this](GAGGUI::Screen &, int) { endExecute(1); });
}
void LANSessionScreen::onTimer(Uint32 tick)
{
	if (stage == Stage::Lobby || stage == Stage::Failed)
		return;
	if (!stageStarted)
		stageStarted = tick;
	client->update();
	if ((!client->isConnecting() && !client->isConnected()) ||
		Uint32(tick - *stageStarted) >= 10000)
	{
		stage = Stage::Failed;
        auto error = client->getConnectionError();
        screens.push(std::make_unique<MessageScreen>(error.empty() ?
            Glob2UI::tr("[lan connection unavailable]") :
            std::string(FormattableString(Glob2UI::tr("[lan connection verification failed %0]")).arg(error)),
            std::vector<std::string>{Glob2UI::tr("[ok]")}), [this](GAGGUI::Screen&, int) { endExecute(1); });
        return;
	}
	const auto connection = client->getConnectionState();
	if (stage == Stage::Greeting && connection == YOGClient::WaitingForLoginInformation)
	{
		client->attemptLogin(username);
		stage = Stage::Login;
		stageStarted = tick;
	}
	else if (stage == Stage::Login)
	{
		if (connection == YOGClient::WaitingForLoginInformation)
		{
			fail("[Can't connect, can't find host]");
			return;
		}
		if (connection == YOGClient::ClientOnStandby)
		{
			stage = Stage::GameList;
			stageStarted = tick;
			if (hostedMap)
				enterLobby();
		}
	}
	else if (stage == Stage::GameList && !client->getGameListManager()->getGameList().empty())
	{
		if (client->getGameListManager()->getGameList().front().getGameState() ==
			YOGGameInfo::GameRunning)
			fail("[Can't join game, game has started]");
		else
			enterLobby();
	}
}
void LANSessionScreen::enterLobby()
{
	auto game = std::make_shared<MultiplayerGame>(client);
	client->setMultiplayerGame(game);
	if (hostedMap)
	{
		game->createNewGame(
			FormattableString(Toolkit::getStringTable()->getString("[%0's game]")).arg(username));
		game->setMapHeader(*hostedMap);
	}
	else
		game->joinGame(client->getGameListManager()->getGameList().front().getGameID());
	stage = Stage::Lobby;
	screens.push(std::make_unique<LANGameScreen>(screens, client, game),
				 [this](GAGGUI::Screen &, int result) { endExecute(result); });
}
