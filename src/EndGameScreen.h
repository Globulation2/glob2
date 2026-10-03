// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière
// Copyright (C) 2006 Bradley Arsenault

#pragma once

#include "GameGUI.h"
#include "ui/FrontendUI.h"
#include "TeamStatChart.h"
#include <memory>
#include <string>
#include <vector>

class LoadSaveDialog;
namespace Online
{
class OnlineMatchResult;
}

struct TeamEntry
{
	int teamNum;
	Uint64 endVal[36]{};
	GAGCore::Color color;
	std::string name;
	bool enabled = true;
};

//! The results after a match: one metric at a time as a chart over the match,
//! with the teams to show chosen by the player, and a replay save.
class EndGameScreen : public GAGGUI::ui::UIScreen
{
	friend class GameGUITouchHarness;

  public:
	//! Return values passed by the screen's buttons
	enum ButtonId
	{
		//! stat selector buttons use their int as id
		STAT_BUTTON_FIRST = 0,
		//! per-team toggle buttons use TEAM_TOGGLE_FIRST + row index
		TEAM_TOGGLE_FIRST = EndOfGameStat::TYPE_NB_STATS,
		QUIT = 38,
		SAVE_REPLAY = 39
	};
	explicit EndGameScreen(GameGUI *gui);
	~EndGameScreen() override;
	Glob2UI::Element build(const Glob2UI::Presentation &presentation) override;
	void updateExecution(Uint32 tick) override;

	// Semantic entry points shared with harnesses and captures.
	void selectMetric(int metric);
	void toggleTeam(int row);
	void expandChart(bool expanded);
	void showTeamFilters(bool open);
	// Inspect the chart at a viewport point (the value marker follows it).
	void inspect(int x, int y);
	// Legacy action codes: 100 opens the metric picker, 101 toggles the expanded
	// chart, 102 the team filters, 200+ toggles a team row, else a ButtonId.
	void activateResultControl(int action);
	//! Quick matches: opens (or joins) the unrated rematch room (Online::requestRematch)
	//! and leaves the results; the room opens over the online screens.
	void rematch();
	bool metricPickerOpen() { return host().popupOpen(); }
	//! Online matches: the outcome banner and the rating card, updated live while
	//! the server verifies the result (docs/multiplayer/client.md).
	void setOnlineResult(std::shared_ptr<Online::OnlineMatchResult> result);
	const std::shared_ptr<Online::OnlineMatchResult> &onlineResult() const { return online; }
	//! Harness: the outcome the banner names when the local game cannot tell.
	enum class Outcome
	{
		Victory,
		Defeat,
		Ended,
		Left
	};
	void setOutcome(Outcome value) { outcome = value; invalidate(); }
	//! Harness: the reason line the banner shows.
	void setReason(std::string value) { reason = std::move(value); invalidate(); }
	//! The local outcome and its one-line reason ("Guest-5285 left the match.",
	//! "Your colony was defeated."), from the finished game.
	struct Description
	{
		Outcome outcome = Outcome::Ended;
		std::string reason;
	};
	static Description describe(const Game &game, const Team &local);
	Outcome shownOutcome() const { return outcome; }
	const std::string &outcomeReason() const { return reason; }

  protected:
	void paintBackground(Glob2UI::Canvas &canvas) override;
	Glob2UI::Rect available(const Glob2UI::Presentation &presentation, const Glob2UI::Metrics &metrics) override;
	void onEscape() override;
	bool interceptEvent(const SDL_Event &event) override;
	void afterPaint(Glob2UI::Canvas &canvas) override;
	void viewportResized(int oldWidth, int oldHeight, int width, int height) override;

	//! resort players
	void sortAndSet(int type);
	//! Translated short name of a stat type, used for its selector button and the graph label
	static std::string statTypeName(int type);
	std::vector<TeamEntry> teams;
	int selectedMetric = 0;
	bool expandedChart = false;
	bool teamFiltersOpen = false;
	//! pointer to the game, necessary for correctly saving replays
	Game *game;

  private:
	std::unique_ptr<LoadSaveDialog> replaySave;
	std::shared_ptr<Online::OnlineMatchResult> online;
	unsigned onlineRevision = ~0u;
	Outcome outcome = Outcome::Ended;
	//! Why the match ended this way ("Guest-5285 left the match."), under the outcome.
	std::string reason;
	Uint32 durationSeconds = 0;
	Glob2UI::Element onlineBanner(const Glob2UI::Presentation &p);
	Glob2UI::Element ratingCard(const Glob2UI::Presentation &p);
	// Chart-local pointer position, or -1 when outside.
	int hoverX = -1, hoverY = -1;
	Glob2UI::Rect chartBounds;
	bool teamEnabled(int teamNum) const;
	void paintChart(Glob2UI::Canvas &canvas, Glob2UI::Rect r);
	void paintCurves(GAGCore::DrawableSurface &surface, Glob2UI::Rect r);
	void paintMeasurements(GAGCore::DrawableSurface &surface, Glob2UI::Rect r);
	TeamStatChart::Options chartOptions() const;
	void saveReplay(const char *dir, const char *ext);
	Glob2UI::Element teamRows(const Glob2UI::Presentation &p);
};
