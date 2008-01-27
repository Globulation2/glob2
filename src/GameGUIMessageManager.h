/*
  Copyright (C) 2007 Bradley Arsenault

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

#ifndef GameGUIMessageManager_h
#define GameGUIMessageManager_h

#include <string>
#include "SDL.h"
#include <list>
#include "GUIBase.h"
#include "GUIList.h"

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
	Uint32 lastTime;
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
	
	///Add a message to the history
	void addMessage(const InGameMessage& message);

	///Draws all messages that need to be drawn starting at x,y
	void drawAllMessages(int x, int y);

	///Creates an InGameScrollableHistory, does not take ownership for it
	InGameScrollableHistory* createScrollableHistoryScreen();
private:

	std::list<InGameMessage> history;
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


#endif
