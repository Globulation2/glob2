// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2007-2008 Bradley Arsenault

#pragma once

#include "GUIBase.h"
#include "MultiplayerGame.h"
#include <memory>
#include "MultiplayerGameEvent.h"
#include "MultiplayerGameEventListener.h"

class Map;
namespace GAGGUI
{
	class Text;
	class ProgressBar;
}
namespace GAGCore
{
	class DrawableSurface;
}

///Scheduled progress screen while joining a multiplayer game.
class YOGClientGameConnectionDialog:public GAGGUI::Screen, public MultiplayerGameEventListener
{
public:
	YOGClientGameConnectionDialog(std::shared_ptr<MultiplayerGame> game);
	virtual ~YOGClientGameConnectionDialog();
	virtual void onAction(GAGGUI::Widget *source, GAGGUI::Action action, int par1, int par2);
	virtual void onTimer(Uint32 tick);

	///These are the possible end values
	enum EndValue
	{
		Success,
		Failed,
		Cancelled,
	};
private:
	///This function updates the multiplayer game
	void updateGame();
	///This handles an event from the multiplayer game
	void handleMultiplayerGameEvent(std::shared_ptr<MultiplayerGameEvent> event);

	std::shared_ptr<MultiplayerGame> game;
};

