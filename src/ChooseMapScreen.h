/*
  Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière
  for any question or comment contact us at <stephane at magnenat dot net> or <NuageBleu at gmail dot com>

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

#ifndef __CHOOSE_MAP_SCREEN_H
#define __CHOOSE_MAP_SCREEN_H

#include "MapHeader.h"
#include "GameHeader.h"
#include "Glob2Screen.h"
#include <GUINumber.h>

namespace GAGGUI
{
	class Button;
	class TextButton;
	class Text;
	class Number;
	class OnOffButton;
}
class Glob2FileList;
class MapPreview;

//! This screen is the basic screen used to selected map and games, Can have an alternate directory if desired
class ChooseMapScreen : public Glob2Screen
{
public:
	/// Constructor. Directory is the source of the listed files.
	/// extension is the file extension to show. If recurse is true,
	/// subdirectoried are shown and can be opened.
	ChooseMapScreen(const char *directory, const char *extension, bool recurse, const char* alternateDirectory=NULL, const char* alternateExtension=NULL, const char* alternateRecurse=NULL);
	//! Destructor
	virtual ~ChooseMapScreen();
	virtual void onAction(Widget *source, Action action, int par1, int par2);
	
	/// Returns the mapHeader of the map that is currently selected
	MapHeader& getMapHeader();
	
	/// Returns the gameHeader, with all of the customized options,
	/// for the currently selected map.
	GameHeader& getGameHeader();

	enum
	{
		//! Value returned upon screen execution completion when a valid map/game is selected
		OK = 1,
		//! Value returned upon screen execution completion when the map/game selection is canceled
		CANCEL = 2,
		//! Value returned if screen is for games and delete button has been pressed
		DELETEGAME = 3,
		//! Value returned if screen if the button to switch between games and maps has been pressed
		SWITCHTYPE = 4,
	};

	enum LoadableType
	{
		NONE,
		GAME,
		MAP,
		REPLAY
	};

	/// Returns the type of the currently selected loadable (NONE, GAME, MAP or REPLAY)
	LoadableType getSelectedType();

protected:
	/// Handle called when a valid map has been selected.
	/// This is to be overwritten by the derived class.
	virtual void validMapSelectedhandler(void) { }

	/// The map header of the currently selected map
	MapHeader mapHeader;
	/// The game header of the currently selected map
	GameHeader gameHeader;

private:

	enum DirectoryMode
	{
		DisplayRegular,
		DisplayAlternate,
	} currentDirectoryMode;

	LoadableType selectedType;

	//! Title of the screen, depends on the directory given in parameter
	Text *title;
	//! The ok button
	Button *ok;
	//! The cancel button
	Button *cancel;
	//! the delete map button
	Button *deleteMap;
	//! the switch type button
	TextButton *switchType;
	//! The list of maps or games
	Glob2FileList *fileList;
	//! The alternate list of maps or games
	Glob2FileList *alternateFileList;
	//! The widget that will show a preview of the selection map
	MapPreview *mapPreview;
	//! The textual informations about the selected map
	Text *mapName, *mapInfo, *mapVersion, *mapSize, *mapDate, *varPrestigeText;
	//! True when the selected map is valid
	bool validMapSelected;
	//! Default type
	LoadableType type1;
	//! Alternate type
	LoadableType type2;

	/// Called after a new mapHeader and gameHeader have been loaded.
	void updateMapInformation();

	/// Designates whether there will be verbose debugging output.
	static const bool verbose = false;
};

#endif
