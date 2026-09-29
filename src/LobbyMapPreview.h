// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "GUIMapPreview.h"
#include <vector>
#include <filesystem>
#include "MapHeader.h"

class LobbyMapPreview : public MapPreview
{
  public:
	using Start = MapStart;
	struct CachedMap
	{
		std::string path;
		std::filesystem::file_time_type time;
		uintmax_t bytes;
		MapHeader header;
		MapThumbnail terrain;
		std::vector<Start> starts;
	};
	std::vector<CachedMap> cache;
	LobbyMapPreview() : MapPreview(430, 115, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED)
	{
		w = h = 180;
	}
};
