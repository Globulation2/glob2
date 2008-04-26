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
class YOGClientMapDownloadScreen : public TabScreenWindow
{
public:
	YOGClientMapDownloadScreen(TabScreen* parent, boost::shared_ptr<YOGClient> client);
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
	};
	
private:
	///This requests the list of maps from the server
	void requestMaps();

	boost::shared_ptr<YOGClient> client;
	List* mapList;
	//! The widget that will show a preview of the selection map
	MapPreview *mapPreview;
	//! The textual informations about the selected map
	Text *mapName, *mapInfo, *mapVersion, *mapSize, *mapDate, *varPrestigeText;
	//! This is the button for adding a map
	TextButton* addMap;
	
	//! True when the selected map is valid
	bool validMapSelected;
};

#endif
