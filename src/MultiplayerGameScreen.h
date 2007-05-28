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
#include "IRC.h"

namespace GAGGUI
{
	class Text;
	class TextArea;
	class TextInput;
	class TextButton;
	class ColorButton;
}
class MultiplayersHost;
class MultiplayersJoin;

class MultiplayerGameScreen : public Glob2Screen
{
public:
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

private:
	int executionMode;

public:
	MultiplayerGameScreen(boost::shared_ptr<MultiplayerGame> client, IRC* ircPtr, MapHeader header);
	virtual ~MultiplayerGameScreen();

	void onTimer(Uint32 tick);
	void onAction(Widget *source, Action action, int par1, int par2);

	TextButton *startButton;
	std::vector<TextButton *> addAI;
	ColorButton *color[MAX_NUMBER_OF_PLAYERS];
	Text *text[MAX_NUMBER_OF_PLAYERS];
	TextButton *kickButton[MAX_NUMBER_OF_PLAYERS];
	Text *startTimer;

	TextInput *textInput;
	TextArea *chatWindow;

	bool wasSlotUsed[MAX_NUMBER_OF_PLAYERS];
	Text *notReadyText;
	Text *gameFullText;
	
	MapHeader mapHeader;
	IRC* ircPtr;
};
#endif
