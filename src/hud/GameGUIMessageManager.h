// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2007 Bradley Arsenault

#pragma once

#include <string>
#include <list>
#include "GAGColor.h"
#include <SDL3/SDL_stdinc.h>

// These directives predate the header's slimming; the namespaces are declared so they
// stay valid without the GUI headers. The history dialog is in InGameScrollableHistory.h.
namespace GAGGUI {}
namespace GAGCore {}
using namespace GAGGUI;
using namespace GAGCore;

class InGameScrollableHistory;

///This class represents a message that is displayed to the user in the game
///and stored in a history menu for the user. Note: This message must only
///take up a single line
class InGameMessage
{
public:
	///Constructs an in game message with the text, the color, and a time to be displayed, in ms
	InGameMessage(const std::string& text, const GAGCore::Color& color, int time=8000);
	
	///Returns the text in this message
	std::string getText() const;
protected:
	friend class GameGUIMessageManager;
	///This draws the message at the given x,y pixel coordinates, and updates the timer
	void draw(int x, int y);
	int timeLeft;
private:
	Uint64 lastTime;
	std::string text;
	GAGCore::Color color;
};

///This class handles text messages (including game events), and the text message history,
///for GameGUI
class GameGUIMessageManager
{
public:
	///Constructs a GameGUIMessageManager
	GameGUIMessageManager();
	
	///Add a message to the history of game messages
	void addGameMessage(const InGameMessage& message);
	
	///Add a message to the history of chat messages
	void addChatMessage(const InGameMessage& message);
	

	///Draws all messages that need to be drawn starting at x,y
	void drawAllGameMessages(int x, int y);
	
	///Draws all chat messages that need to be drawn starting at x,y
	void drawAllChatMessages(int x, int y);

	///Creates an InGameScrollableHistory, does not take ownership for it
	InGameScrollableHistory* createScrollableHistoryScreen();
private:

	std::list<InGameMessage> historyGame;
	std::list<InGameMessage> historyChat;
};
