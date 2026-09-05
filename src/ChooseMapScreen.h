// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#pragma once

#include "MapHeader.h"
#include "MapTiling.h"
#include <map>
#include "GameHeader.h"
#include "Glob2Screen.h"
#include <GUINumber.h>
#include <string>

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
	ChooseMapScreen(const char *directory, const char *extension, bool recurse, const char* alternateDirectory=NULL, const char* alternateExtension=NULL, const bool alternateRecurse=false);
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
	TextButton *switchType = nullptr;
	//! The list of maps or games
	Glob2FileList *fileList;
	//! The alternate list of maps or games
	Glob2FileList *alternateFileList = nullptr;
	//! The widget that will show a preview of the selection map
	MapPreview *mapPreview;
	//! The textual informations about the selected map: teams or bases, size, date
	Text *mapInfo, *mapSize, *mapDate;
	//! Repeat factors, colony count and bases per colony for a map, see MapTiling.h; hidden for games and replays
	Number *repeatX, *repeatY, *teamCount, *coloniesPerTeam;
	//! Captions beside the four controls
	Text *repeatXText, *repeatYText, *teamCountText, *coloniesPerTeamText;
	//! The file of the selected map, key into mapInfos
	std::string selectedMapFile;
	//! True while the preview shows the repeated map rather than the file's own thumbnail
	bool previewTiled = false;
	//! Show the repeated map in the preview when the controls ask for one, the plain map otherwise
	void updatePreview();
	//! Header and size of every map in the list, read once
	std::map<std::string, MapTiling::MapInfo> mapInfos;
	//! Header and size of a listed map, read on first use
	const MapTiling::MapInfo& infoOfMap(const std::string& fileName);
	//! Reorder the active list: maps that allow the current numbers first, the others after
	void sortMapList();
	//! The header of the selected map as it is in the file; mapHeader describes the repeated map
	MapHeader sourceMapHeader;
	//! Fill the repeat and team controls for the selected map
	void updateTilingControls();
	//! The repeat factors and team count currently chosen; the widgets only mirror them
	int tileX = 1, tileY = 1, tileTeams = 0, tileColonies = 0;
	//! True once the user set the colony count or the bases per colony; until then they follow the map
	bool teamsPicked = false, basesPicked = false;
	//! Derive mapHeader from the controls
	void applyTiling();
	//! True when the controls ask for a repeated map
	bool tilingActive() const;
	//! True when the selected map is valid
	bool validMapSelected;
	//! Default type
	LoadableType type1;
	//! Alternate type
	LoadableType type2;

	/// Called after a new mapHeader and gameHeader have been loaded.
	void updateMapInformation();

	/// Returns the file list currently shown: fileList when DisplayRegular, alternateFileList when DisplayAlternate.
	Glob2FileList* activeFileList() const;

	/// Returns the LoadableType paired with the active list: type1 when DisplayRegular, type2 when DisplayAlternate.
	LoadableType activeType() const;

	/// Maps a LoadableType to its display string ([the games]/[the maps]/[the replays]). Asserts on NONE.
	static std::string loadableTypeName(LoadableType type);

	/// Switches to newMode: flips list visibility, sets the switchType button label to the other list's type name, and fires selectionChanged() on the newly-active list.
	void setDirectoryMode(DirectoryMode newMode);

	/// Designates whether there will be verbose debugging output.
	static const bool verbose = false;
};

