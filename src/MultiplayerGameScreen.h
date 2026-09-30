// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2007 Bradley Arsenault
#pragma once
#include "AI.h"
#include "IRCTextMessageHandler.h"
#include "MapHeader.h"
#include "MultiplayerGame.h"
#include "MultiplayerGameEventListener.h"
#include "SessionTabsScreen.h"
#include "Team.h"
#include "YOGClientChatChannel.h"
#include "YOGClientChatListener.h"
#include <memory>
#include <string>
#include <vector>

namespace GAGGUI
{
class ScreenStack;
}

///The setup room for a multiplayer game, for the host and joined players alike. It
///uses the information it gets from the given MultiplayerGame and keeps IRC in sync.
class MultiplayerGameScreen : public SessionTab, public YOGClientChatListener, public MultiplayerGameEventListener
{
  public:
	MultiplayerGameScreen(GAGGUI::ScreenStack &screens, std::shared_ptr<MultiplayerGame> game, std::shared_ptr<YOGClient> client,
						  std::shared_ptr<IRCTextMessageHandler> ircChat = std::shared_ptr<IRCTextMessageHandler>());
	~MultiplayerGameScreen() override;

	enum
	{
		Cancelled,
		StartedGame,
		GameRefused,
		Kicked,
		GameCancelled,
		ServerDisconnected,
	};
	std::string title() const override;
	Glob2UI::Element build(const Glob2UI::Presentation &presentation) override;
	void onTimer(Uint32 tick) override;
	void onActivated() override;
	bool onEscape() override;

  private:
	GAGGUI::ScreenStack &screens;
	std::shared_ptr<YOGClient> client;
	void launchScheduledGame();
	void receiveTextMessage(std::shared_ptr<YOGMessage> message) override;
	void handleMultiplayerGameEvent(std::shared_ptr<MultiplayerGameEvent> event) override;
	void sendChat();
	void cancel();
	bool hosting() const { return game->getMultiplayerMode() == MultiplayerGame::HostingGame; }
	std::shared_ptr<MultiplayerGame> game;
	std::shared_ptr<YOGClientChatChannel> gameChat;
	std::shared_ptr<IRCTextMessageHandler> ircChat;
	std::string chatLog, chatDraft;
	bool ready = false;
	int downloadPercent = -1;
};
