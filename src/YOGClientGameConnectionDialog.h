// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2007-2008 Bradley Arsenault

#ifndef YOGClientGameConnectionDialog_h
#define YOGClientGameConnectionDialog_h

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

///This dialog shows progress of the fertility computation
class YOGClientGameConnectionDialog:public GAGGUI::OverlayScreen, public MultiplayerGameEventListener
{
public:
	YOGClientGameConnectionDialog(GAGCore::GraphicContext *parentCtx, std::shared_ptr<MultiplayerGame> game);
	virtual ~YOGClientGameConnectionDialog();
	virtual void onAction(GAGGUI::Widget *source, GAGGUI::Action action, int par1, int par2);
	
	///This screen is modal, this executes it
	void execute();

	///These are the possible end values
	enum EndValue
	{
		Success,
		Failed,
	};
private:
	///This function updates the multiplayer game
	void updateGame();
	///This handles an event from the multiplayer game
	void handleMultiplayerGameEvent(std::shared_ptr<MultiplayerGameEvent> event);

	GAGGUI::Text* information;
	GAGCore::GraphicContext *parentCtx;
	std::shared_ptr<MultiplayerGame> game;
};


#endif
