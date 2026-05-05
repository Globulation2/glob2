// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2008 Bradley Arsenault

#pragma once

#include <vector>
#include "GUITabScreenWindow.h"
#include <memory>


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
	YOGClientOptionsScreen(TabScreen* parent, std::shared_ptr<YOGClient> client);

	///Called when this tab is activated
	void onActivated();
	///Responds to widget events
	void onAction(Widget *source, Action action, int par1, int par2);
	
	enum
	{
		QUIT,
		REMOVEBLOCKEDPLAYER,
		ADDBLOCKEDPLAYER,
	};
private:

	///Updates the list of blocked player
	void updateBlockedPlayerList();
	///Adds a blocked player from the text box
	void updateBlockedPlayerAdd();
	///Removes a blocked player from the text move
	void updateBlockedPlayerRemove();

	std::shared_ptr<YOGClient> client;
	
	List* blockedPlayers;
	Text* blockedPlayersText;
	TextButton* removeBlockedPlayer;
	TextInput* addBlockedPlayerText;
	TextButton* addBlockedPlayer;
};

