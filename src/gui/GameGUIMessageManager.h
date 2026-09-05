// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2007 Bradley Arsenault

#pragma once

#include <string>
#include <list>
#include "GUIBase.h"

namespace GAGGUI
{
	class List;
}

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
	///This draws the message at the given x,y pixel cordinates, and updates the timer
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


///This class represents a self-contained Overlay screen that allows for scrolling message history
class InGameScrollableHistory : public OverlayScreen
{
public:
	/// InGameScrollableHistory constructor
	InGameScrollableHistory(GraphicContext *context, const std::list<InGameMessage>& messageHistory);
	/// InGameScrollableText destructor
	virtual ~InGameScrollableHistory() { }

	///Handles an event
	virtual void onAction(Widget *source, Action action, int par1, int par2);

	///Handles timer presses
	void onTimer(Uint32 tick);
protected:
	/// Updates the messageList from the history
	void updateList();
	/// The list of messages
	const std::list<InGameMessage>& history;
	/// The last known size of the history, to count for changes
	size_t lastSize;
	/// The widget
	List *messageList;	
};


