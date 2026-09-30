// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2008 Bradley Arsenault
#pragma once
#include "YOGClientMapDownloader.h"
#include "YOGDownloadableMapInfo.h"
#include "ui/FrontendUI.h"
#include <memory>

class YOGClient;
class MapPreview;
namespace GAGGUI
{
class ScreenStack;
}

///Shown while a map downloads from YOG.
class YOGClientDownloadingMapScreen : public Glob2UI::Screen
{
  public:
	YOGClientDownloadingMapScreen(GAGGUI::ScreenStack &screens, std::shared_ptr<YOGClient> client, const YOGDownloadableMapInfo &info);
	~YOGClientDownloadingMapScreen() override;
	Glob2UI::Element build(const Glob2UI::Presentation &presentation) override;
	void onTimer(Uint32 tick) override;

	enum
	{
		CANCEL,
		CONNECTIONLOST,
		FINISHED,
	};

  protected:
	void onEscape() override { cancel(); }

  private:
	void cancel();
	GAGGUI::ScreenStack &screens;
	YOGDownloadableMapInfo info;
	std::unique_ptr<MapPreview> preview;
	std::shared_ptr<YOGClient> client;
	std::string mapName, mapInfo, mapSize, authorName;
	int downloadPercent = -1;
	YOGClientMapDownloader downloader;
};
