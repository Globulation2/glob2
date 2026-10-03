// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2007 Bradley Arsenault
#pragma once
#include "IRCTextMessageHandler.h"
#include "MultiplayerGame.h"
#include "RoomBackend.h"
#include "SessionTabsScreen.h"
#include <memory>
#include <string>

namespace GAGGUI
{
class ScreenStack;
}

///The setup room for a multiplayer game, for the host and joined players alike. It
///shows and edits the room through a RoomBackend: LanRoom for LAN games, YogRoom for
///the YOG lobby (which also keeps IRC in sync).
class MultiplayerGameScreen : public SessionTab
{
  public:
	MultiplayerGameScreen(GAGGUI::ScreenStack &screens, std::shared_ptr<RoomBackend> room);
	/// The YOG lobby's room (wraps the game in a YogRoom).
	MultiplayerGameScreen(GAGGUI::ScreenStack &screens, std::shared_ptr<MultiplayerGame> game, std::shared_ptr<YOGClient> client,
						  std::shared_ptr<IRCTextMessageHandler> ircChat = std::shared_ptr<IRCTextMessageHandler>());
	~MultiplayerGameScreen() override;

	enum
	{
		Cancelled = RoomBackend::Cancelled,
		StartedGame = RoomBackend::StartedGame,
		GameRefused = RoomBackend::GameRefused,
		Kicked = RoomBackend::Kicked,
		GameCancelled = RoomBackend::GameCancelled,
		ServerDisconnected = RoomBackend::ServerDisconnected,
	};
	std::string title() const override;
	Glob2UI::Element build(const Glob2UI::Presentation &presentation) override;
	void onTimer(Uint32 tick) override;
	void onActivated() override;
	bool onEscape() override;
	RoomBackend &backend() { return *room; }

  private:
	GAGGUI::ScreenStack &screens;
	std::shared_ptr<RoomBackend> room;
	void launchScheduledGame();
	void sendChat();
	void cancel();
	std::string chatLog, chatDraft;
	bool ready = false;
	int downloadPercent = -1;
};
