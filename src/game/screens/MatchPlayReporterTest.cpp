// SPDX-License-Identifier: GPL-3.0-or-later
// The game's side of portal gameplay events around the in-game menu and pauses
// (MatchPlayReporter, GameGUI::playStopped; docs/releases/web-portals.md).
#include "Glob2Test.h"
#include "HeadlessGlobals.h"
#include "HeadlessGame.h"
#include "GameGUI.h"
#include "MatchPlayReporter.h"
#include <SDL3/SDL.h>
#include <vector>

using GAGCore::ApplicationHost::MatchBreak;

namespace
{
struct Recorded
{
	std::vector<MatchBreak> points;
	MatchPlayReporter reporter{[this](MatchBreak point) { points.push_back(point); return false; }};
};
} // namespace

TEST_SUITE("MatchPlayReporter")
{
	TEST_CASE("each stop and resume of play is reported once")
	{
		Recorded r;
		r.reporter.started();
		r.reporter.update(false);
		// The menu opens, then the game is paused from it: still one stop.
		r.reporter.update(true);
		r.reporter.update(true);
		r.reporter.update(false);
		r.reporter.update(false);
		r.reporter.update(true);
		r.reporter.update(false);
		CHECK(r.points == std::vector<MatchBreak>{MatchBreak::Paused, MatchBreak::Resumed, MatchBreak::Paused,
												  MatchBreak::Resumed});
	}

	TEST_CASE("nothing is reported outside a started local match or after it ends")
	{
		Recorded r;
		// A relay match never calls started(): the menu and pauses send nothing.
		r.reporter.update(true);
		r.reporter.update(false);
		CHECK(r.points.empty());
		r.reporter.started();
		r.reporter.update(true);
		// Quitting from the menu: the match end stops gameplay; no Resumed follows.
		r.reporter.stopped();
		r.reporter.update(false);
		CHECK(r.points == std::vector<MatchBreak>{MatchBreak::Paused});
		// The next match starts in play.
		r.reporter.started();
		CHECK_FALSE(r.reporter.playStopped());
		r.reporter.update(true);
		CHECK(r.points == std::vector<MatchBreak>{MatchBreak::Paused, MatchBreak::Paused});
	}

	TEST_CASE("play stops for the in-game menu, its dialogs and pauses, not for side panels")
	{
		glob2test::HeadlessGlobals globals({.loadStrings = true});
		glob2test::HeadlessGame world({.header = true});
		auto &gui = world.gui;
		gui.prepareLocalPresentation(gui.frameScene);
		CHECK_FALSE(gui.playStopped());
		gui.openMainMenu();
		CHECK(gui.playStopped());
		gui.closeDialog();
		CHECK_FALSE(gui.playStopped());
		for (const auto menu : {GameGUI::IGM_SAVE, GameGUI::IGM_LOAD, GameGUI::IGM_OPTION, GameGUI::IGM_CONFIRM_LEAVE,
								GameGUI::IGM_TUTORIAL_RESTART})
		{
			gui.inGameMenu = menu;
			CHECK(gui.playStopped());
		}
		for (const auto menu : {GameGUI::IGM_ALLIANCE, GameGUI::IGM_OBJECTIVES, GameGUI::IGM_STATISTICS})
		{
			gui.inGameMenu = menu;
			CHECK_FALSE(gui.playStopped());
		}
		gui.inGameMenu = GameGUI::IGM_NONE;
		gui.gamePaused = true;
		CHECK(gui.playStopped());
		gui.gamePaused = false;
		// "Pause game" closes the menu and sends an order that lands at a later
		// tick: play counts as stopped from the request, so no start slips between.
		gui.requestPause(true);
		CHECK(gui.playStopped());
		gui.gamePaused = true;
		gui.requestPause(false);
		CHECK_FALSE(gui.playStopped());
		gui.gamePaused = false;
		CHECK_FALSE(gui.playStopped());
		// A request that never lands does not hold play stopped.
		gui.requestPause(true);
		gui.requestedPauseAt = SDL_GetTicks() - 5000;
		CHECK_FALSE(gui.playStopped());
		gui.requestedPause.reset();
		// Quitting from the menu: stopped until the match ends.
		gui.flushOutgoingAndExit = true;
		CHECK(gui.playStopped());
		gui.flushOutgoingAndExit = false;
	}

	TEST_CASE("a native host ignores pause and resume")
	{
		CHECK_FALSE(GAGCore::ApplicationHost::matchBreak(MatchBreak::Paused));
		CHECK_FALSE(GAGCore::ApplicationHost::matchBreak(MatchBreak::Resumed));
	}
}
