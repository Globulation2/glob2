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

#ifndef YOGClientMapUploadScreen_h
#define YOGClientMapUploadScreen_h

#include <vector>
#include "Glob2Screen.h"
#include "boost/shared_ptr.hpp"
#include "YOGClientMapUploader.h"

namespace GAGGUI
{
	class Text;
	class TextInput;
	class TextArea;
	class TextButton;
	class TabScreen;
	class Widget;
	class List;
	class ProgressBar;
}

class YOGClient;
class MapPreview;

using namespace GAGGUI;

/// A widget that maintains the list of players, and draws an icon based
/// on whether that player is from YOG or from IRC
class YOGClientMapUploadScreen : public Glob2Screen
{
public:

	/// Constructor
	YOGClientMapUploadScreen(boost::shared_ptr<YOGClient> client, const std::string mapFile);

	///Responds to widget events
	void onAction(Widget *source, Action action, int par1, int par2);
	///Responds to timer events
	void onTimer(Uint32 tick);
	
	enum
	{
		CANCEL,
		UPLOAD,
		UPLOADFAILED,
		UPLOADFINISHED,
		CONNECTIONLOST,
	};
private:
	enum
	{
	};
	
	MapPreview* preview;
	boost::shared_ptr<YOGClient> client;
	YOGClientMapUploader uploader;
	Text* uploadStatusText;
	ProgressBar* uploadStatus;
	//! The textual informations about the selected map
	Text *mapInfo, *mapVersion, *mapSize, *mapDate, *varPrestigeText;
	TextInput* mapName;
	Text *authorNameText;
	TextInput* authorName;
	std::string mapFile;
	bool isUploading;
};

#endif
