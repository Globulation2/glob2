// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "GameEventFeed.h"
#include <ViewportTransform.h>
#include <ui/Geometry.h>

struct EventFeedRowLayout
{
	GameEventFeed::Row event;
	std::vector<std::string> lines;
	GAGGUI::ui::Rect bounds;
	int x = 0, y = 0, iconY = 0, textY = 0, height = 0;
	Uint8 alpha = 255;
};
struct EventFeedLayout
{
	bool mobile = false;
	double scale = 1, originX = 0, originY = 0;
	GAGCore::ViewRect area;
	int lineHeight = 20, bottom = 0;
	std::vector<EventFeedRowLayout> rows;
};
