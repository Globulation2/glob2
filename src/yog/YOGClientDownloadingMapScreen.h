// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2008 Bradley Arsenault

#pragma once


#include <vector>
#include "Glob2Screen.h"
#include <memory>
#include "YOGDownloadableMapInfo.h"
#include "YOGClientMapDownloader.h"

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

///This screen appears when you are downloading a map
class YOGClientDownloadingMapScreen : public Glob2Screen
{
public:

	/// Constructor
	YOGClientDownloadingMapScreen(std::shared_ptr<YOGClient> client, const YOGDownloadableMapInfo& info);

	///Responds to widget events
	void onAction(Widget *source, Action action, int par1, int par2);
	///Responds to timer events
	void onTimer(Uint32 tick);
	
	enum
	{
		CANCEL,
		CONNECTIONLOST,
		FINISHED,
	};
private:
	YOGDownloadableMapInfo info;
	MapPreview* preview;
	std::shared_ptr<YOGClient> client;
	//! The textual informations about the selected map
	Text *mapName, *mapInfo, *mapSize;
	Text *authorName;
	ProgressBar* downloadStatus;
	YOGClientMapDownloader downloader;
};





