/*
  Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière
  for any question or comment contact us at <stephane at magnenat dot net> or <NuageBleu at gmail dot com>

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

#ifndef __MULTIPLAYER_CONNECTED_SCREEN_H
#define __MULTIPLAYER_CONNECTED_SCREEN_H

#include "MultiplayersJoin.h"
#include "Glob2Screen.h"

namespace GAGGUI
{
	class Text;
	class TextInput;
	class TextArea;
}
class ColorRect;

class MultiplayersConnectedScreen:public Glob2Screen
{
	static const bool verbose = false;
public:
	enum
	{
		DISCONNECT = 3,
		DISCONNECTED = 4,
		STARTED=11
	};
	MultiplayersJoin *multiplayersJoin;

private:
	int timeCounter;
	double progress, lastProgress;
public:
	Text *startTimer;

private:
	TextInput *textInput;
	TextArea *chatWindow;
	Text *text[16];
	ColorRect *color[16];

public:
	MultiplayersConnectedScreen(MultiplayersJoin *multiplayersJoin);
	virtual ~MultiplayersConnectedScreen();
	
	void onTimer(Uint32 tick);

	void onSDLEvent(SDL_Event *event);

	void onAction(Widget *source, Action action, int par1, int par2);
};

#endif
