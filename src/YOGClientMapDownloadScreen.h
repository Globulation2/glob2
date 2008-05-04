/*
  Copyright (C) 2008 Bradley Arsenault

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

#ifndef YOGClientMapDownloadScreen_h
#define YOGClientMapDownloadScreen_h

#include "GUITabScreenWindow.h"
#include "boost/shared_ptr.hpp"
#include "YOGClientDownloadableMapListener.h"

namespace GAGGUI
{
	class TextInput;
	class TextArea;
	class TextButton;
	class TabScreen;
	class Widget;
}

class YOGClient;
class MapPreview;

using namespace GAGGUI;

///This is the main YOG screen
class YOGClientMapDownloadScreen : public TabScreenWindow, public YOGClientDownloadableMapListener
{
public:
	YOGClientMapDownloadScreen(TabScreen* parent, boost::shared_ptr<YOGClient> client);
	~YOGClientMapDownloadScreen();
	///Responds to timer events
	virtual void onTimer(Uint32 tick);
	///Responds to widget events
	void onAction(Widget *source, Action action, int par1, int par2);
	///Called when this tab is activated
	void onActivated();
	
	enum
	{
		QUIT,
		ADDMAP,
		REFRESHMAPLIST,
		DOWNLOADMAP,
	};
	
	///Updates the list of maps
	void mapListUpdated();
	///Updates the list of maps
	void mapThumbnailsUpdated();
	
private:
	///This requests the list of maps from the server
	void requestMaps();
	///This updates the map info
	void updateMapInfo();
	///This updates the visibilily
	void updateVisibility();
	///This updates the map preview
	void updateMapPreview();

	boost::shared_ptr<YOGClient> client;
	List* mapList;
	//! The widget that will show a preview of the selection map
	MapPreview *mapPreview;
	//! The textual informations about the selected map
	Text *mapName, *mapInfo, *mapVersion, *mapSize, *mapAuthor, *varPrestigeText;
	//! This is the button for adding a map
	TextButton* addMap;
	//! this button requests an update to the list
	TextButton* refresh;
	//! this button requests a map to be downloaded
	TextButton* downloadMap;
	//! A piece of text showing "loading map list"
	Text* loadingMapList;
	
	bool mapValid;
	bool mapsRequested;
	
	//! True when the selected map is valid
	bool validMapSelected;
};

#endif
