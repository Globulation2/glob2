/*
  Copyright (C) 2007-2008 Bradley Arsenault

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 3 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
*/

#ifndef YOGClientGameConnectionDialog_h
#define YOGClientGameConnectionDialog_h

#include "GUIBase.h"
#include "MultiplayerGame.h"
#include "boost/shared_ptr.hpp"
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
	YOGClientGameConnectionDialog(GAGCore::GraphicContext *parentCtx, boost::shared_ptr<MultiplayerGame> game);
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
	void handleMultiplayerGameEvent(boost::shared_ptr<MultiplayerGameEvent> event);

	GAGGUI::Text* information;
	GAGCore::GraphicContext *parentCtx;
	boost::shared_ptr<MultiplayerGame> game;
};


#endif
