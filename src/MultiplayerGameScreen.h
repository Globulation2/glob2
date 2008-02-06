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

#ifndef __MultiplayerGameScreen_h
#define __MultiplayerGameScreen_h

#include <vector>
#include "Glob2Screen.h"
#include "MultiplayerGame.h"
#include "AI.h"
#include "MapHeader.h"
#include "YOGChatChannel.h"
#include "YOGChatListener.h"
#include "MultiplayerGameEventListener.h"
#include "IRCTextMessageHandler.h"

namespace GAGGUI
{
	class Text;
	class TextArea;
	class TextInput;
	class TextButton;
	class ColorButton;
}

///This screen is the setup screen for a multiplayer game. It functions both for the host
///and the joined player. It uses the information it gets from the given MultiplayerGame.
///This doesn't continue dispaying irc, it merely keeps it up to date and turns it on/off
///when starting and finishing games
class MultiplayerGameScreen : public Glob2Screen, public YOGChatListener, public MultiplayerGameEventListener
{
public:
	///The screen must be provided with the client, the irc connection and the multiplayer game
	MultiplayerGameScreen(boost::shared_ptr<MultiplayerGame> game, boost::shared_ptr<YOGClient> client, boost::shared_ptr<IRCTextMessageHandler> ircChat = boost::shared_ptr<IRCTextMessageHandler>());
	virtual ~MultiplayerGameScreen();

	enum
	{
		Cancelled,
		StartedGame,
		GameRefused,
		Kicked,
		GameCancelled,
	};

private:
	enum
	{
		START = 1,
		CANCEL = 2,
		STARTED=11,
		
		COLOR_BUTTONS=32,
		CLOSE_BUTTONS=64,
		
		ADD_AI = 100
	};

	enum { MAX_NUMBER_OF_PLAYERS = 16};

	void onTimer(Uint32 tick);
	void onAction(Widget *source, Action action, int par1, int par2);

	void recieveTextMessage(boost::shared_ptr<YOGMessage> message);

	void handleMultiplayerGameEvent(boost::shared_ptr<MultiplayerGameEvent> event);

	///This function will update the list of joined players
	void updateJoinedPlayers();

	TextButton *startButton;
	std::vector<TextButton *> addAI;
	ColorButton *color[MAX_NUMBER_OF_PLAYERS];
	Text *text[MAX_NUMBER_OF_PLAYERS];
	TextButton *kickButton[MAX_NUMBER_OF_PLAYERS];
	Text *startTimer;

	TextInput *textInput;
	TextArea *chatWindow;

	boost::shared_ptr<MultiplayerGame> game;

	bool wasSlotUsed[MAX_NUMBER_OF_PLAYERS];
	Text *notReadyText;
	Text *gameFullText;

	boost::shared_ptr<YOGChatChannel> gameChat;
	boost::shared_ptr<IRCTextMessageHandler> ircChat;
};
#endif
