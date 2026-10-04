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

//! A team of the finished match, as the results list it.
struct TeamEntry
{
	int teamNum;
	//! Whole-match figures for the overview, one per overview column
	//! (overviewColumns in EndGameScreen.cpp), in the same order.
	std::vector<double> summary;
	GAGCore::Color color;
	std::string name;
	bool enabled = true;
};

//! The results after a match: an overview of every team, then one metric at a
//! time as a chart over the match, picked from the grouped metric catalog
//! (stats/MetricCatalog.h), with the teams to show chosen by the player, and a
//! replay save.
class EndGameScreen : public GAGGUI::ui::UIScreen
{
	friend class GameGUITouchHarness;

  public:
	const char *recordingId() const override { return "end_game"; }
	//! Return values passed by the screen's buttons
	enum ButtonId
	{
		QUIT = 38,
		SAVE_REPLAY = 39
	};
	explicit EndGameScreen(GameGUI *gui);
	~EndGameScreen() override;
	Glob2UI::Element build(const Glob2UI::Presentation &presentation) override;
	void updateExecution(Uint32 tick) override;

	// Semantic entry points shared with harnesses and captures.
	//! Shows a metric of Stats::catalog() by index, or the overview for OVERVIEW.
	void selectMetric(int metric);
	static constexpr int OVERVIEW = -1;
	//! Changes how the selected metric is shown; choices it does not offer are dropped.
	void setView(Stats::View view);
	const Stats::View &currentView() const { return view; }
	//! Draws one team heavier than the rest, or none for -1.
	void highlightTeam(int teamNum);
	void toggleTeam(int row);
	void expandChart(bool expanded);
	void showTeamFilters(bool open);
	// Inspect the chart at a viewport point (the value marker follows it). The
	// chart must have been painted, which is what places it in the viewport.
	void inspect(int x, int y);
	//! Action codes of activateResultControl(), besides the ButtonIds.
	enum ResultControl
	{
		OPEN_GROUP_LIST = 100,		 //!< Compact layouts: the list of metric groups.
		TOGGLE_EXPANDED_CHART = 101,
		TOGGLE_TEAM_FILTERS = 102,
		OPEN_METRIC_LIST = 103,		 //!< Compact layouts: the metrics of the selected group.
		TOGGLE_TEAM_FIRST = 200		 //!< Plus the team's row.
	};
	void activateResultControl(int action);
	//! Quick matches: opens (or joins) the unrated rematch room (Online::requestRematch)
	//! and leaves the results; the room opens over the online screens.
	void rematch();
	//! Quick matches: leaves the results and searches the same queue again
	//! (Online::requestQueueAgain).
	void findAnotherMatch();
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

	std::vector<TeamEntry> teams;
	//! Index into Stats::catalog(), or OVERVIEW.
	int selectedMetric = OVERVIEW;
	//! How the selected metric is shown; always one of the views it offers.
	Stats::View view;
	//! Team number drawn heavier than the rest, or -1.
	int highlighted = -1;
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
	//! The selected metric, or null on the overview.
	const Stats::Metric *metric() const;
	//! Per metric of the catalog: nothing happened in this match for it to show.
	std::vector<bool> nothingToShow;
	//! A metric's name in the picker, saying so when it has nothing to show.
	std::string pickerTitle(int index) const;
	int enabledTeams() const;
	void paintChart(Glob2UI::Canvas &canvas, Glob2UI::Rect r);
	TeamStatChart::Options chartOptions() const;
	// Parts of build(). Wide layouts list the metrics in a sidebar; compact ones
	// pick them from two drop-downs in a header row.
	Glob2UI::Element metricSidebar(const Glob2UI::Presentation &p);
	Glob2UI::Element metricChoices(const Glob2UI::Presentation &p);
	Glob2UI::Element compactHeader(const Glob2UI::Presentation &p);
	Glob2UI::Element teamRows(const Glob2UI::Presentation &p);
	Glob2UI::Element viewControls(const Glob2UI::Presentation &p);
	Glob2UI::Element chartCanvas(const Glob2UI::Presentation &p);
	Glob2UI::Element overview(const Glob2UI::Presentation &p, bool compact);
	Glob2UI::Element actionBar(const Glob2UI::Presentation &p, bool compact);
	void openMatchPage();
	void saveReplay(const char *dir, const char *ext);
};
