// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 glob2 contributors
#pragma once
#include "ui/FrontendUI.h"
#include <ScreenStack.h>
#include <memory>
#include <string>

namespace Online
{
class OnlineMatch;
}
class MapPreview;

// "Starting match" (multiplayer mock-up 4A): the steps between a match.start and the
// first tick, each player's progress, and Leave. When the relay's first turns arrive
// it hands the engine to GameSessionScreen; when that ends (results screen closed) it
// ends with the game's result, so the room or hub beneath shows again.
class MatchStartScreen : public Glob2UI::Screen
{
  public:
	enum
	{
		LEFT = 0,
		PLAYED = 1,
		FAILED = 2
	};
	MatchStartScreen(GAGGUI::ScreenStack &screens, std::shared_ptr<Online::OnlineMatch> match);
	~MatchStartScreen() override;
	Glob2UI::Element build(const Glob2UI::Presentation &presentation) override;
	void onTimer(Uint32 tick) override;
	Uint32 executionDelay(Uint32, Uint32) override { return 15; }

	// Semantic entry points for harnesses.
	void leave();
	Online::OnlineMatch &match() { return *flow; }

  protected:
	void onEscape() override { leave(); }

  private:
	GAGGUI::ScreenStack &screens;
	std::shared_ptr<Online::OnlineMatch> flow;
	std::unique_ptr<MapPreview> preview;
	std::string previewFile;
	int lastStep = -1;
	bool playing = false;
	Uint32 lastRefresh = 0;
	int tip = 0;
	Glob2UI::Element steps(const Glob2UI::Presentation &p);
	Glob2UI::Element players(const Glob2UI::Presentation &p);
};
