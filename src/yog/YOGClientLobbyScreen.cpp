// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière
#include "YOGClientLobbyScreen.h"
#include "ChooseMapScreen.h"
#include "GlobalContainer.h"
#include "MessageScreen.h"
#include "MultiplayerGameScreen.h"
#include "YOGClient.h"
#include "YOGClientChatChannel.h"
#include "YOGClientCommandManager.h"
#include "YOGClientEvent.h"
#include "YOGClientGameConnectionDialog.h"
#include "YOGClientGameListManager.h"
#include "YOGClientPlayerListManager.h"
#include "YOGMessage.h"
#include <FormatableString.h>
#include <ScreenStack.h>
#include <StringTable.h>
#include <Toolkit.h>
#include <Toolkit.h>

namespace fe = Glob2UI;
using fe::Element;
using fe::Presentation;

YOGClientLobbyScreen::YOGClientLobbyScreen(GAGGUI::ScreenStack &screens, std::shared_ptr<YOGClient> client) : client(client), screens(screens)
{
	networkSprite = Toolkit::getSprite("data/gui/yog");
	lobbyChat.reset(new YOGClientChatChannel(LOBBY_CHAT_CHANNEL, client));
	ircChat.reset(new IRCTextMessageHandler);
	ircChat->addTextMessageListener(this);
	ircChat->startIRC(client->getUsername());
	client->addEventListener(this);
	client->getGameListManager()->addListener(this);
	client->getPlayerListManager()->addListener(this);
	lobbyChat->addListener(this);
	updateGameList();
	updatePlayerList();
}

YOGClientLobbyScreen::~YOGClientLobbyScreen()
{
	if (ownedGameScreen && session)
		session->removeTab(ownedGameScreen.get());
	ownedGameScreen.reset();
	client->setMultiplayerGame({});
	ircChat->removeTextMessageListener(this);
	ircChat->stopIRC();
	lobbyChat->removeListener(this);
	client->removeEventListener(this);
	client->getGameListManager()->removeListener(this);
	client->getPlayerListManager()->removeListener(this);
	Toolkit::releaseSprite("data/gui/yog");
}

std::string YOGClientLobbyScreen::title() const { return fe::tr("[Lobby]"); }

bool YOGClientLobbyScreen::onEscape()
{
	finish(Cancelled);
	return true;
}

void YOGClientLobbyScreen::sendChat()
{
	if (chatDraft.empty())
		return;
	// A client command like /block answers locally; anything else goes to the channel.
	const std::string result = client->getCommandManager()->executeClientCommand(chatDraft);
	if (!result.empty())
		receiveInternalMessage(result);
	else
	{
		std::shared_ptr<YOGMessage> message(new YOGMessage);
		message->setSender(client->getUsername());
		message->setMessage(chatDraft);
		message->setMessageType(YOGNormalMessage);
		lobbyChat->sendMessage(message);
		ircChat->sendCommand(chatDraft);
	}
	chatDraft.clear();
	refresh();
}

std::string YOGClientLobbyScreen::selectedGameInfo() const
{
	auto &strings = *Toolkit::getStringTable();
	if (selectedGame >= 0 && selectedGame < int(games.size()))
	{
		for (const auto &game : client->getGameListManager()->getGameList())
			if (games[std::size_t(selectedGame)] == game.getGameName())
				return game.getGameName() + "\n" + FormattableString(strings.getString("[Map name: %0]")).arg(game.getMapName()) + "\n" +
					   FormattableString(strings.getString("[number of players: %0 (%1 AI)]")).arg(int(game.getPlayersJoined()) + int(game.getAIJoined())).arg(int(game.getAIJoined())) + "\n" +
					   FormattableString(strings.getString("[number of teams: %0]")).arg(int(game.getNumberOfTeams()));
	}
	else if (selectedPlayer >= 0 && selectedPlayer < int(players.size()))
	{
		const auto &name = players[std::size_t(selectedPlayer)].name;
		if (client->getPlayerListManager()->doesPlayerExist(name))
		{
			const auto info = client->getPlayerListManager()->getPlayerInfo(name);
			return info.getPlayerName() + "\n" + FormattableString(strings.getString("[player rating %0]")).arg(info.getPlayerStoredInfo().getPlayerRating());
		}
	}
	return {};
}

Element YOGClientLobbyScreen::build(const Presentation &p)
{
	const bool inGame = ownedGameScreen != nullptr;
	fe::ListOptions gameOptions;
	gameOptions.visibleRows = 6;
	gameOptions.emptyText = fe::tr("[No items]");
	gameOptions.activate = [this](int) { joinGame(); };
	auto gameList = fe::listView("lobby/games", games, selectedGame,
								 [this](int i)
								 {
									 selectedGame = i;
									 selectedPlayer = -1;
								 },
								 gameOptions);
	std::vector<std::string> playerNames;
	for (const auto &entry : players)
		playerNames.push_back(entry.name);
	fe::ListOptions playerOptions;
	playerOptions.visibleRows = 8;
	playerOptions.paintRow = [this](fe::Canvas &canvas, fe::Rect row, int index, bool)
	{
		const auto &entry = players[std::size_t(index)];
		int textX = row.x;
		if (networkSprite)
		{
			canvas.drawSprite({row.x, row.y + (row.h - 16) / 2}, networkSprite, entry.irc ? 1 : 0);
			textX += 20;
		}
		canvas.text({textX, row.y + (row.h - canvas.measurer().lineHeight(fe::FontRole::Body)) / 2}, fe::FontRole::Body,
					fe::ellipsize(canvas.measurer(), fe::FontRole::Body, entry.name, row.right() - textX), Glob2UI::frontendTheme().palette.ink);
	};
	auto playerList = fe::listView("lobby/players", playerNames, selectedPlayer,
								   [this](int i)
								   {
									   selectedPlayer = i;
									   selectedGame = -1;
								   },
								   playerOptions);
	fe::TextFieldOptions chatOptions;
	chatOptions.maxLength = 256;
	chatOptions.submit = [this](const std::string &) { sendChat(); };
	auto chat = fe::column({fe::expanded(fe::textEditor("lobby/chat", chatLog, {}, {true, 8, false, true})),
							fe::textField("lobby/input", chatDraft, [this](const std::string &v) { chatDraft = v; }, chatOptions)},
						   {p.pt(6)});
	std::vector<Element> gameSide{fe::label(fe::tr("[games]")), gameList, fe::paragraph(selectedGameInfo(), {fe::FontRole::Support, true})};
	if (!inGame)
		gameSide.push_back(fe::wrap({fe::button("lobby/join", fe::tr("[join]"), [this] { joinGame(); }, {false, false, selectedGame >= 0}),
									 fe::button("lobby/host", fe::tr("[create game]"), [this] { hostGame(); }, {true})},
									{-1, p.pt(140)}));
	auto gameColumn = fe::column(std::move(gameSide), {p.pt(6)});
	auto playerColumn = fe::column({fe::label(fe::tr("[players]")), playerList}, {p.pt(6)});
	Element body = fe::adaptive(
		[gameColumn, playerColumn, chat](const fe::LayoutContext &ctx, fe::Size available) -> fe::Element
		{
			if (available.w < ctx.presentation.pt(720))
				return fe::scroll("lobby/scroll", fe::column({gameColumn, fe::height(ctx.presentation.pt(240), chat), playerColumn}, {ctx.presentation.pt(12)}));
			return fe::row({fe::expanded(fe::column({gameColumn, fe::expanded(chat)}, {ctx.presentation.pt(10)}), 3), fe::expanded(playerColumn, 2)},
						   {ctx.presentation.pt(16), fe::CrossAlign::Stretch});
		});
	return fe::column({fe::expanded(body), fe::divider(), fe::actions({{"lobby/quit", fe::tr("[quit]"), [this] { finish(Cancelled); }, false, SDLK_ESCAPE}}, p)}, {p.pt(8)});
}

void YOGClientLobbyScreen::onTimer(Uint32)
{
	if (ownedGameScreen && ownedGameScreen->finished())
	{
		const int rc = ownedGameScreen->returnCode();
		std::shared_ptr<MultiplayerGame> game(client->getMultiplayerGame());
		auto &strings = *Toolkit::getStringTable();
		if (rc == MultiplayerGameScreen::Kicked)
			receiveInternalMessage(strings.getString("[You where kicked from the game]"));
		else if (rc == MultiplayerGameScreen::GameCancelled)
			receiveInternalMessage(strings.getString("[The host has cancelled the game]"));
		else if (rc == MultiplayerGameScreen::GameRefused)
		{
			if (game->getGameJoinState() == YOGServerGameHasAlreadyStarted)
				receiveInternalMessage(strings.getString("[Can't join game, game has started]"));
			else if (game->getGameJoinState() == YOGServerGameIsFull)
				receiveInternalMessage(strings.getString("[Can't join game, game is full]"));
			else if (game->getGameJoinState() == YOGServerGameDoesntExist)
				receiveInternalMessage(strings.getString("[Can't join game, game doesn't exist]"));
			else if (game->getGameCreationState() == YOGCreateRefusalUnknown)
				receiveInternalMessage("Game was refused by server");
		}
		if (session)
		{
			session->removeTab(ownedGameScreen.get());
			session->activate(this);
		}
		ownedGameScreen.reset();
		client->setMultiplayerGame(std::shared_ptr<MultiplayerGame>());
		refresh();
	}
	ircChat->update();
	client->update();
	if (ircChat->hasUserListBeenModified())
		updatePlayerList();
}

void YOGClientLobbyScreen::handleYOGClientEvent(std::shared_ptr<YOGClientEvent> event)
{
	const Uint8 type = event->getEventType();
	auto &strings = *Toolkit::getStringTable();
	if (type == YEConnectionLost)
		screens.push(std::make_unique<MessageScreen>(strings.getString("[YESTS_CONNECTION_LOST]"), std::vector<std::string>{strings.getString("[ok]")}),
					 [this](GAGGUI::Screen &, int) { finish(ConnectionLost); });
	else if (type == YEPlayerBanned)
		screens.push(std::make_unique<MessageScreen>(strings.getString("[Your username was banned]"), std::vector<std::string>{strings.getString("[ok]")}));
	else if (type == YEIPBanned)
		screens.push(std::make_unique<MessageScreen>(strings.getString("[Your IP address was temporarily banned]"), std::vector<std::string>{strings.getString("[ok]")}));
}

void YOGClientLobbyScreen::handleIRCTextMessage(const std::string &message)
{
	chatLog += "[IRC] " + message + "\n";
	refresh();
}

void YOGClientLobbyScreen::receiveTextMessage(std::shared_ptr<YOGMessage> message)
{
	chatLog += message->formatForReading() + "\n";
	refresh();
}

void YOGClientLobbyScreen::receiveInternalMessage(const std::string &message)
{
	chatLog += message + "\n";
	refresh();
}

void YOGClientLobbyScreen::gameListUpdated() { updateGameList(); }
void YOGClientLobbyScreen::playerListUpdated() { updatePlayerList(); }

void YOGClientLobbyScreen::hostGame()
{
	screens.push(std::make_unique<ChooseMapScreen>("maps", "map", false, "games", "game", false),
				 [this](GAGGUI::Screen &selection, int rc)
				 {
					 if (rc != ChooseMapScreen::OK)
						 return;
					 auto game = std::make_shared<MultiplayerGame>(client);
					 client->setMultiplayerGame(game);
					 const std::string name = FormattableString(Toolkit::getStringTable()->getString("[%0's game]")).arg(client->getUsername());
					 game->createNewGame(name);
					 game->setMapHeader(static_cast<ChooseMapScreen &>(selection).getMapHeader());
					 showGame(game);
				 });
}

void YOGClientLobbyScreen::showGame(std::shared_ptr<MultiplayerGame> game)
{
	ownedGameScreen = std::make_unique<MultiplayerGameScreen>(screens, game, client, ircChat);
	if (session)
	{
		session->addTab(ownedGameScreen.get());
		session->activate(ownedGameScreen.get());
	}
	refresh();
}

void YOGClientLobbyScreen::joinGame()
{
	if (selectedGame < 0 || selectedGame >= int(games.size()))
		return;
	std::shared_ptr<MultiplayerGame> game(new MultiplayerGame(client));
	client->setMultiplayerGame(game);
	Uint16 id = 0;
	for (const auto &info : client->getGameListManager()->getGameList())
		if (games[std::size_t(selectedGame)] == info.getGameName())
		{
			id = info.getGameID();
			break;
		}
	game->joinGame(id);
	screens.push(std::make_unique<YOGClientGameConnectionDialog>(game),
				 [this, game](GAGGUI::Screen &, int result)
				 {
					 if (result == YOGClientGameConnectionDialog::Success)
						 showGame(game);
					 else
					 {
						 game->leaveGame();
						 client->setMultiplayerGame({});
					 }
				 });
}

void YOGClientLobbyScreen::updateGameList()
{
	const std::string previous = selectedGame >= 0 && selectedGame < int(games.size()) ? games[std::size_t(selectedGame)] : "";
	games.clear();
	for (const auto &game : client->getGameListManager()->getGameList())
		if (game.getGameState() == YOGGameInfo::GameOpen)
			games.push_back(game.getGameName());
	selectedGame = -1;
	for (std::size_t i = 0; i < games.size(); ++i)
		if (games[i] == previous)
			selectedGame = int(i);
	refresh();
}

void YOGClientLobbyScreen::updatePlayerList()
{
	players.clear();
	for (const auto &player : client->getPlayerListManager()->getPlayerList())
		players.push_back({player.getPlayerName(), false});
	// IRC entries, minus users already on YOG.
	for (const auto &user : ircChat->getUsers())
		if (user.compare(0, 5, "[YOG]") != 0)
			players.push_back({user, true});
	selectedPlayer = std::min(selectedPlayer, int(players.size()) - 1);
	refresh();
}
