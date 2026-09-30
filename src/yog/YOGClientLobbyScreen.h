// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2007 Bradley Arsenault
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière
#pragma once
#include "IRCTextMessageHandler.h"
#include "SessionTabsScreen.h"
#include "YOGClientChatListener.h"
#include "YOGClientEventListener.h"
#include "YOGClientGameListListener.h"
#include "YOGClientPlayerListListener.h"
#include <memory>
#include <string>
#include <vector>

namespace GAGGUI
{
class ScreenStack;
}
class YOGClient;
class YOGClientChatChannel;
class MultiplayerGameScreen;

///The main YOG lobby: games to join, players online and the lobby chat.
class YOGClientLobbyScreen : public SessionTab, public YOGClientEventListener, public YOGClientChatListener, public IRCTextMessageListener,
							 public YOGClientGameListListener, public YOGClientPlayerListListener
{
  public:
	///The client must be logged in when this is called.
	YOGClientLobbyScreen(GAGGUI::ScreenStack &screens, std::shared_ptr<YOGClient> client);
	~YOGClientLobbyScreen() override;
	std::string title() const override;
	Glob2UI::Element build(const Glob2UI::Presentation &presentation) override;
	void onTimer(Uint32 tick) override;
	bool onEscape() override;
	void handleYOGClientEvent(std::shared_ptr<YOGClientEvent> event) override;
	void handleIRCTextMessage(const std::string &message) override;
	void receiveTextMessage(std::shared_ptr<YOGMessage> message) override;
	void receiveInternalMessage(const std::string &message);
	void gameListUpdated() override;
	void playerListUpdated() override;

	///The end codes of the session this tab belongs to
	enum
	{
		ConnectionLost,
		Cancelled,
	};

  private:
	void hostGame();
	void joinGame();
	void showGame(std::shared_ptr<class MultiplayerGame> game);
	void sendChat();
	void updateGameList();
	void updatePlayerList();
	std::string selectedGameInfo() const;

	struct PlayerEntry
	{
		std::string name;
		bool irc = false;
	};
	std::vector<std::string> games;
	std::vector<PlayerEntry> players;
	int selectedGame = -1, selectedPlayer = -1;
	std::string chatLog, chatDraft;

	std::shared_ptr<YOGClient> client;
	std::shared_ptr<YOGClientChatChannel> lobbyChat;
	std::shared_ptr<IRCTextMessageHandler> ircChat;
	GAGGUI::ScreenStack &screens;
	std::unique_ptr<MultiplayerGameScreen> ownedGameScreen;
	GAGCore::Sprite *networkSprite = nullptr;
};
