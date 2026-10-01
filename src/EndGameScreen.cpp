// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière
// Copyright (C) 2006 Bradley Arsenault

#include "EndGameScreen.h"
#include "TeamStatChart.h"
#include "GlobalContainer.h"
#include "ReplayWriter.h"
#include "Team.h"
#include "TeamDisplay.h"
#include "Utilities.h"
#include "gui/InGameTouchTheme.h"
#include "gui/LoadSaveDialog.h"
#include <ApplicationHost.h>
#include <FormatableString.h>
#include <StringTable.h>
#include <Toolkit.h>
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <limits>
#include <sstream>
#include <typeinfo>

namespace fe = Glob2UI;
using fe::Element;
using fe::Presentation;
using GAGCore::Color;

namespace
{
const Color background(34, 24, 49);

//! This function is used to sort the player array
struct MoreScore
{
	int type;
	bool operator()(const TeamEntry &t1, const TeamEntry &t2)
	{
		if (t1.endVal[type] == t2.endVal[type])
		{
			if (t1.teamNum == t2.teamNum)
				return t1.name > t2.name;
			return t1.teamNum > t2.teamNum;
		}
		return t1.endVal[type] > t2.endVal[type];
	}
};
} // namespace

//! LoadSaveDialog name-extractor callback for the save-replay dialog: turn a
//! full virtual path like "replays/My_Game.replay" into the display name
//! "My Game". Directory prefix and extension are only removed when actually
//! present, so a stray file in replays/ degrades to showing its raw name
//! instead of throwing (erase(npos) used to crash the dialog).
std::string replayFilenameToName(const std::string &fullfilename)
{
	std::string filename = Utilities::stripSuffix(Utilities::stripPrefix(fullfilename, "replays/"), ".replay");
	std::replace(filename.begin(), filename.end(), '_', ' ');
	return filename;
}

EndGameScreen::EndGameScreen(GameGUI *gui) : UIScreen(fe::inGameTheme())
{
	// We're no longer replaying a game
	globalContainer->replaying = false;

	// set teams entries for later sort
	for (int i = 0; i < gui->game.gameHeader.getNumberOfPlayers(); i++)
	{
		struct TeamEntry entry;
		entry.name = gui->game.gameHeader.getBasePlayer(i).name;
		entry.teamNum = gui->game.gameHeader.getBasePlayer(i).teamNumber;
		entry.color = gui->game.teams[entry.teamNum]->color;
		int endIndex = gui->game.teams[entry.teamNum]->stats.endOfGameStats.size() - 1;
		for (int j = 0; j < EndOfGameStat::TYPE_NB_STATS; j++)
		{
			entry.endVal[j] = endIndex >= 0 ? gui->game.teams[entry.teamNum]->stats.endOfGameStats[endIndex].value[j] : 0;
		}
		for (int j = 0; j < 30; ++j)
			entry.endVal[j + 6] = TeamStats::graphValue(gui->game.teams[entry.teamNum]->stats.measurements, j);
		auto existing = std::find_if(teams.begin(), teams.end(), [&](const auto &team) { return team.teamNum == entry.teamNum; });
		if (existing == teams.end())
			teams.push_back(entry);
		else
			existing->name += ", " + entry.name;
	}

	// Save the step and order count
	game = &(gui->game);

	sortAndSet(EndOfGameStat::TYPE_UNITS);
}

EndGameScreen::~EndGameScreen() = default;

double EndGameScreen::textScale(const Presentation &p) const { return fe::frontendTextScale(p); }

void EndGameScreen::paintBackground(fe::Canvas &canvas)
{
	canvas.fillRect({0, 0, canvas.size().w, canvas.size().h}, background);
}

fe::Rect EndGameScreen::available(const Presentation &p, const fe::Metrics &m)
{
	return p.dialog.inset(m.halfGap);
}

void EndGameScreen::onEscape()
{
	if (!replaySave)
		endExecute(QUIT);
}

std::string EndGameScreen::statTypeName(int type)
{
	return TeamStatChart::metricName(type);
}

void EndGameScreen::sortAndSet(int type)
{
	// Selection belongs to team identity, not the visible rank or widget order.
	MoreScore moreScore;
	moreScore.type = type;
	std::stable_sort(teams.begin(), teams.end(), moreScore);
}

void EndGameScreen::selectMetric(int metric)
{
	if (metric < 0 || metric >= 36)
		return;
	selectedMetric = metric;
	sortAndSet(metric);
	invalidate();
}

void EndGameScreen::toggleTeam(int row)
{
	if (row < 0 || row >= int(teams.size()))
		return;
	teams[std::size_t(row)].enabled = !teams[std::size_t(row)].enabled;
	invalidate();
}

void EndGameScreen::expandChart(bool expanded)
{
	expandedChart = expanded;
	if (expanded)
		teamFiltersOpen = false;
	invalidate();
}

void EndGameScreen::showTeamFilters(bool open)
{
	teamFiltersOpen = open;
	invalidate();
}

void EndGameScreen::inspect(int x, int y)
{
	if (chartBounds.contains(fe::Point{x, y}))
	{
		hoverX = x - chartBounds.x;
		hoverY = y - chartBounds.y;
	}
	else
		hoverX = hoverY = -1;
}

void EndGameScreen::activateResultControl(int action)
{
	if (action == 100)
	{
		if (auto *node = host().find("metric"))
			host().tapAt({node->bounds.x + node->bounds.w / 2, node->bounds.y + node->bounds.h / 2});
	}
	else if (action == 101)
		expandChart(!expandedChart);
	else if (action == 102)
		showTeamFilters(!teamFiltersOpen);
	else if (action >= 200)
		toggleTeam(action - 200);
	else if (action == QUIT)
		endExecute(QUIT);
	else if (action == SAVE_REPLAY)
		saveReplay("replays", "replay");
	else if (action >= TEAM_TOGGLE_FIRST && action < int(TEAM_TOGGLE_FIRST + teams.size()))
		toggleTeam(action - TEAM_TOGGLE_FIRST);
	else if (action >= STAT_BUTTON_FIRST && action < STAT_BUTTON_FIRST + EndOfGameStat::TYPE_NB_STATS)
		selectMetric(action - STAT_BUTTON_FIRST);
}

bool EndGameScreen::teamEnabled(int teamNum) const
{
	for (const auto &team : teams)
		if (team.teamNum == teamNum)
			return team.enabled;
	return false;
}

Element EndGameScreen::teamRows(const Presentation &p)
{
	std::vector<Element> rows;
	for (std::size_t i = 0; i < teams.size(); ++i)
	{
		const auto &team = teams[i];
		rows.push_back(fe::row({fe::swatch(team.color, 12),
								fe::expanded(fe::toggle("team/" + std::to_string(i), team.name, team.enabled, [this, i](bool) { toggleTeam(int(i)); }))},
							   {p.pt(6), fe::CrossAlign::Center}));
	}
	fe::WrapOptions grid;
	grid.minChildWidth = p.pt(180);
	return fe::wrap(std::move(rows), grid);
}

Element EndGameScreen::build(const Presentation &p)
{
	const bool compact = p.compact() || p.shortLandscape();
	std::vector<Element> parts;
	if (!expandedChart || !compact)
	{
		std::vector<std::string> options;
		for (int i = 0; i < 36; ++i)
			options.push_back(statTypeName(i));
		std::vector<Element> header;
		header.push_back(fe::expanded(fe::choice("metric", options, selectedMetric, [this](int metric) { selectMetric(metric); })));
		if (compact)
		{
			const int enabled = int(std::count_if(teams.begin(), teams.end(), [](const auto &t) { return t.enabled; }));
			fe::ButtonOptions teamOptions;
			teamOptions.selected = teamFiltersOpen;
			header.push_back(fe::button("teams", GAGCore::FormattableString(fe::tr("[Teams %0/%1]")).arg(enabled).arg(teams.size()),
										[this] { showTeamFilters(!teamFiltersOpen); }, teamOptions));
		}
		parts.push_back(fe::row(std::move(header), {p.pt(8), fe::CrossAlign::Center}));
	}
	if (!expandedChart && (!compact || teamFiltersOpen))
	{
		auto rows = teamRows(p);
		if (compact)
			parts.push_back(fe::constrained({0, 0, fe::Constraints::Unbounded, p.pt(160)}, fe::scroll("teams/scroll", rows)));
		else
			parts.push_back(rows);
	}
	fe::CanvasOptions chartOptions;
	chartOptions.accessibleText = statTypeName(selectedMetric);
	chartOptions.hover = [this](fe::Point local)
	{
		hoverX = local.x;
		hoverY = local.y;
	};
	chartOptions.tap = [this](fe::Point local, fe::Host &)
	{
		hoverX = local.x;
		hoverY = local.y;
	};
	parts.push_back(fe::expanded(fe::canvas("chart", {p.pt(320), p.pt(160)}, [this](fe::Canvas &c, fe::Rect r, const fe::Frame &) { paintChart(c, r); }, chartOptions)));
	parts.push_back(fe::caption(GAGCore::FormattableString(fe::tr("[Elapsed time · %0]")).arg(statTypeName(selectedMetric))));
	const bool save = globalContainer->replayWriter && globalContainer->replayWriter->isValid();
	std::vector<fe::MenuAction> actions;
	actions.push_back({"expand", fe::tr(expandedChart ? "[Back to chart]" : "[Expand chart]"), [this] { expandChart(!expandedChart); }});
	if (save)
		actions.push_back({"save-replay", fe::tr("[save replay]"), [this] { saveReplay("replays", "replay"); }});
	actions.push_back({"quit", fe::tr("[quit]"), [this] { endExecute(QUIT); }, true, SDLK_RETURN});
	parts.push_back(fe::actions(std::move(actions), p));
	return fe::column(std::move(parts), {p.pt(8)});
}

void EndGameScreen::paintChart(fe::Canvas &canvas, fe::Rect r)
{
	chartBounds = r;
	auto *surface = canvas.surface();
	if (!surface)
		return;
	if (std::none_of(teams.begin(), teams.end(), [](const auto &team) { return team.enabled; }))
	{
		canvas.text({r.x + 8, r.y + 8}, fe::FontRole::Body, fe::tr("[Select a team to show its history.]"), InGameTouchTheme::ink);
		return;
	}
	// Leave room for the scale labels on the right and below.
	const fe::Rect plot{r.x, r.y, std::max(1, r.w - 64), std::max(1, r.h - 24)};
	InGameTouchTheme::TextStyle chartText(globalContainer->littleFont);
	InGameTouchTheme::TextStyle chartLabels(globalContainer->standardFont);
	canvas.pushClip(r);
	if (selectedMetric >= 6)
		paintMeasurements(*surface, {plot.x, plot.y, plot.w + 64, plot.h + 24});
	else
		paintCurves(*surface, {plot.x, plot.y, plot.w + 64, plot.h + 24});
	canvas.popClip();
}

// The chart itself is shared with the in-match statistics sheet.
TeamStatChart::Options EndGameScreen::chartOptions() const
{
	return {selectedMetric, [this](int team) { return teamEnabled(team); }, hoverX, hoverY};
}

void EndGameScreen::paintCurves(GAGCore::DrawableSurface &surface, fe::Rect r)
{
	TeamStatChart::paintCurves(*game, surface, r.x, r.y, r.w, r.h, chartOptions());
}

void EndGameScreen::paintMeasurements(GAGCore::DrawableSurface &surface, fe::Rect r)
{
	TeamStatChart::paintMeasurements(*game, surface, r.x, r.y, r.w, r.h, chartOptions());
}

void EndGameScreen::saveReplay(const char *dir, const char *ext)
{
	if (replaySave)
		return;
	replaySave = std::make_unique<LoadSaveDialog>(dir, ext, false, Toolkit::getStringTable()->getString("[save replay]"), "",
												  replayFilenameToName, glob2NameToFilename);
	if (gfx)
		replaySave->attach(*gfx);
	GAGCore::ApplicationHost::screenChanged(typeid(*replaySave).name());
}

void EndGameScreen::updateExecution(Uint32 tick)
{
	UIScreen::updateExecution(tick);
	if (!replaySave)
		return;
	replaySave->update(tick);
	if (replaySave->pollPersistence())
	{
		replaySave.reset();
		GAGCore::ApplicationHost::screenChanged(typeid(*this).name());
		return;
	}
	if (!replaySave->finished())
		return;
	if (replaySave->result() == LoadSaveDialog::OK)
	{
		if (!globalContainer->replayWriter || !globalContainer->replayWriter->write(replaySave->getFileName()))
			replaySave->showSaveFailure();
		else
			replaySave->beginPersistence(GAGCore::ApplicationHost::persistStorage());
	}
	else
	{
		replaySave.reset();
		GAGCore::ApplicationHost::screenChanged(typeid(*this).name());
	}
}

bool EndGameScreen::interceptEvent(const SDL_Event &event)
{
	if (!replaySave)
		return false;
	replaySave->event(event);
	return true;
}

void EndGameScreen::afterPaint(fe::Canvas &)
{
	if (replaySave)
		replaySave->draw(SDL_GetTicks());
}

void EndGameScreen::viewportResized(int oldWidth, int oldHeight, int width, int height)
{
	UIScreen::viewportResized(oldWidth, oldHeight, width, height);
	if (replaySave)
		replaySave->cancelInput();
}
