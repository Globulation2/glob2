/*
  Copyright (C) 2008 Bradley Arsenault

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

#ifndef YOGClientOptionsScreen_h
#define YOGClientOptionsScreen_h

#include <vector>
#include "GUITabScreenWindow.h"
#include "boost/shared_ptr.hpp"


namespace GAGGUI
{
	class TextInput;
	class TextArea;
	class TextButton;
	class TabScreen;
	class Widget;
	class List;
}

class YOGClient;

using namespace GAGGUI;

/// A widget that maintains the list of players, and draws an icon based
/// on whether that player is from YOG or from IRC
class YOGClientOptionsScreen : public TabScreenWindow
{
public:

	/// Constructor
	YOGClientOptionsScreen(TabScreen* parent, boost::shared_ptr<YOGClient> client);

	///Called when this tab is activated
	void onActivated();
	///Responds to widget events
	void onAction(Widget *source, Action action, int par1, int par2);
	
	enum
	{
		QUIT,
		REMOVE_BLOCKED_PLAYER,
		ADD_BLOCKED_PLAYER,
	};
private:

	///Updates the list of blocked player
	void updateBlockedPlayerList();
	///Adds a blocked player from the text box
	void updateBlockedPlayerAdd();
	///Removes a blocked player from the text move
	void updateBlockedPlayerRemove();

	boost::shared_ptr<YOGClient> client;
	
	List* blockedPlayers;
	Text* blockedPlayersText;
	TextButton* removeBlockedPlayer;
	TextInput* addBlockedPlayerText;
	TextButton* addBlockedPlayer;
};

#endif
