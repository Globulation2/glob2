// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2008 Bradley Arsenault
#pragma once
#include "SessionTabsScreen.h"
#include "YOGClientDownloadableMapListener.h"
#include <memory>
#include <string>
#include <vector>

namespace GAGGUI
{
class ScreenStack;
}
class YOGClient;
class MapPreview;
class YOGDownloadableMapInfo;

///Maps shared on YOG: browse, rate, download and upload.
class YOGClientMapDownloadScreen : public SessionTab, public YOGClientDownloadableMapListener
{
  public:
	YOGClientMapDownloadScreen(GAGGUI::ScreenStack &screens, std::shared_ptr<YOGClient> client);
	~YOGClientMapDownloadScreen() override;
	std::string title() const override;
	Glob2UI::Element build(const Glob2UI::Presentation &presentation) override;
	void onTimer(Uint32 tick) override;
	void onActivated() override;

	enum
	{
		QUIT,
		ADDMAP,
		REFRESHMAPLIST,
		DOWNLOADMAP,
		SUBMITRATING,
		SORTMETHOD,
	};

	void mapListUpdated() override;
	void mapThumbnailsUpdated() override;

  private:
	void requestMaps();
	void updateMapPreview();
	void uploadMap();
	void downloadSelected();
	std::string selectedMap() const;
	std::shared_ptr<YOGClient> client;
	GAGGUI::ScreenStack &screens;
	std::vector<std::string> mapNames;
	int selected = -1;
	int sortMethod = 0;
	int rating = 5;
	std::unique_ptr<MapPreview> mapPreview;
	bool mapsRequested = false;
	bool waiting = false;
};

///This class will sort a list of YOGDownloadableMapInfo
class MapListSorter
{
  public:
	enum SortMethod
	{
		Name,
		Size,
		Rating,
	};
	explicit MapListSorter(SortMethod sortmethod);
	bool operator()(const YOGDownloadableMapInfo &lhs, const YOGDownloadableMapInfo &rhs);

  private:
	SortMethod sortMethod;
};
