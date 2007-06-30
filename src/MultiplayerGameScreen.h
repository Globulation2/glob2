/*
  Copyright (C) 2007 Bradley Arsenault

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 2 of the License, or
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
#include "NetTextMessageHandler.h"
#include "MultiplayerGameEventListener.h"


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
class MultiplayerGameScreen : public Glob2Screen, public NetTextMessageListener, public MultiplayerGameEventListener
{
public:
	///The screen must be provided with the text message handler and the multiplayer game
	MultiplayerGameScreen(boost::shared_ptr<MultiplayerGame> game, boost::shared_ptr<NetTextMessageHandler> textMessage);
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

	void handleTextMessage(const std::string& message, NetTextMessageType type);

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

	boost::shared_ptr<NetTextMessageHandler> textMessage;
};
#endif
