/*
  Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière
  for any question or comment contact us at nct@ysagoon.com or nuage@ysagoon.com

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

#ifndef __MULTIPLAYERS_CHOOSE_MAP_SCREEN_H
#define __MULTIPLAYERS_CHOOSE_MAP_SCREEN_H

#include "Session.h"
#include "Glob2Screen.h"
#include <GUINumber.h>

namespace GAGGUI
{
	class Button;
	class List;
	class Text;
	class TextButton;
	class Number;
}
class MapPreview;

//! This class is very similar to ChooseMapScreen but implements a dual list to be able to load both map and games from the same screen
class MultiplayersChooseMapScreen : public Glob2Screen
{
	static const bool verbose = false;
public:
	enum
	{
		OK = 1,
		CANCEL = 2, 
		TOOGLE = 8
	};
	SessionInfo sessionInfo;

private:
	Button *ok, *cancel;
	List *mapFileList;
	List *gameFileList;
	bool mapMode;
	Text *title;
	TextButton *toogleButton;
	MapPreview *mapPreview;
	Text *mapName, *mapInfo, *mapVersion, *mapSize, *mapDate/*, *methode*/;
	Number *prestigeRatio;
	bool validSessionInfo;
	bool shareOnYOG;

public:
	MultiplayersChooseMapScreen(bool shareOnYOG);
	virtual ~MultiplayersChooseMapScreen();
	void onAction(Widget *source, Action action, int par1, int par2);
	void onTimer(Uint32 tick);
};

#endif
