// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2007-2008 Bradley Arsenault
#pragma once
#include "MultiplayerGame.h"
#include "MultiplayerGameEvent.h"
#include "MultiplayerGameEventListener.h"
#include "ui/FrontendUI.h"
#include <memory>

///Scheduled progress screen while joining a multiplayer game.
class YOGClientGameConnectionDialog : public Glob2UI::Screen, public MultiplayerGameEventListener
{
  public:
	const char *recordingId() const override { return "yogclient_game_connection_dialog"; }
	explicit YOGClientGameConnectionDialog(std::shared_ptr<MultiplayerGame> game);
	~YOGClientGameConnectionDialog() override;
	Glob2UI::Element build(const Glob2UI::Presentation &presentation) override;
	void onTimer(Uint32 tick) override;

	///These are the possible end values
	enum EndValue
	{
		Success,
		Failed,
		Cancelled,
	};

  protected:
	void onEscape() override { endExecute(Cancelled); }

  private:
	///This function updates the multiplayer game
	void updateGame();
	///This handles an event from the multiplayer game
	void handleMultiplayerGameEvent(std::shared_ptr<MultiplayerGameEvent> event) override;

	std::shared_ptr<MultiplayerGame> game;
};
