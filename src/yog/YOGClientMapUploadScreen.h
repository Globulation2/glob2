// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2008 Bradley Arsenault

#pragma once

#include <vector>
#include "Glob2Screen.h"
#include <memory>
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
	YOGClientMapUploadScreen(std::shared_ptr<YOGClient> client, const std::string mapFile);

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
	MapPreview* preview;
	std::shared_ptr<YOGClient> client;
	YOGClientMapUploader uploader;
	Text* uploadStatusText;
	ProgressBar* uploadStatus;
	//! The textual informations about the selected map
	Text *mapInfo, *mapVersion, *mapSize, *mapDate;
	TextInput* mapName;
	Text *authorNameText;
	TextInput* authorName;
	std::string mapFile;
	bool isUploading;
};

