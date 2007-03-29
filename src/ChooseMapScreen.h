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

#ifndef __CHOOSE_MAP_SCREEN_H
#define __CHOOSE_MAP_SCREEN_H

#include "Session.h"
#include "Glob2Screen.h"
#include <GUINumber.h>

namespace GAGGUI
{
	class Button;
	class Text;
	class Number;
}
class Glob2FileList;
class MapPreview;

//! This screen is the basic screen used to selected map and games
class ChooseMapScreen : public Glob2Screen
{
	static const bool verbose = false;
public:
	enum
	{
		//! Value returned upon screen execution completion when a valid map/game is selected
		OK = 1,
		//! Value returned upon screen execution completion when the map/game selection is canceled
		CANCEL = 2,
		//! Value returned if screen is for games and delete button has been pressed
		DELETEGAME = 3,
	};
	
	//! Session info, will be used by caller upen screen execution completion
	SessionInfo sessionInfo;

protected:
	//! Title of the screen, depends on the directory given in parameter
	Text *title;
	//! The ok button
	Button *ok;
	//! The cancel button
	Button *cancel;
	//! the delete map button
	Button *deleteMap;
	//! The list of maps or games
	Glob2FileList *fileList;
	//! The widget that will show a preview of the selection map
	MapPreview *mapPreview;
	//! The textual informations about the selected map
	Text *mapName, *mapInfo, *mapVersion, *mapSize, *mapDate;
	// The number information about selected map
	Number *prestigeRatio;
	//! True when the selected map is valid
	bool validMapSelected;
	SessionGame session;

public:
	//! Constructor. Directory is the source of the listed files. extension is the file extension to show. If recurse is true, subdirectoried are shown and can be opened.
	ChooseMapScreen(const char *directory, const char *extension, bool recurse);
	//! Destructor
	virtual ~ChooseMapScreen();
	virtual void onAction(Widget *source, Action action, int par1, int par2);
	
protected:
	//! Handle called when a valid map has been selected. Tp be overriden by subclasses
	virtual void validMapSelectedhandler(void) { }
};

#endif
