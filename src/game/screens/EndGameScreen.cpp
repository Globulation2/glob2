// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière
// Copyright (C) 2006 Bradley Arsenault

#include "EndGameScreen.h"
#include "FrontendTheme.h"
#include "TeamStatChart.h"
#include "stats/MetricSeries.h"
#include "GlobalContainer.h"
#include "ReplayWriter.h"
#include "Team.h"
#include "TeamDisplay.h"
#include "Utilities.h"
#include "InGameTouchTheme.h"
#include "LoadSaveDialog.h"
#include "OnlineHandoff.h"
#include "OnlineMatch.h"
#include "ui/OnlineUI.h"
#include <SDL3/SDL.h>
#include <ApplicationHost.h>
#include <FormatableString.h>
#include <GameplayRecording.h>
#include <StringTable.h>
#include <Toolkit.h>
#include <algorithm>
#include <cassert>
#include <cmath>
#include <iterator>
#include <typeinfo>

namespace fe = Glob2UI;
using fe::Element;
using fe::Presentation;
using GAGCore::Color;

namespace
{
//! A column of the overview: a team's figure for the whole match, and the metric
//! whose chart shows how it came about.
struct OverviewColumn
{
	const char *key;	//!< Text key of the heading.
	const char *metric; //!< Catalog id (Stats::metricById).
	enum
	{
		Peak,  //!< Highest sampled value; the metric must be a sampled one.
		Final, //!< Last sampled value; the metric must be a sampled one.
		Total  //!< A counter's total at the end of the match.
	} figure;
	bool compact;	   //!< Kept on narrow layouts.
	bool moreIsBetter; //!< The highest figure is picked out.
};
const OverviewColumn overviewColumns[] = {
	{"[stat column peak units]", "population", OverviewColumn::Peak, true, true},
	{"[stat column born]", "births", OverviewColumn::Total, true, true},
	{"[stat column lost]", "deaths", OverviewColumn::Total, true, false},
	{"[stat column wheat]", "wheat harvested", OverviewColumn::Total, false, true},
	{"[stat column damage dealt]", "damage dealt", OverviewColumn::Total, false, true},
	{"[stat column damage taken]", "damage taken", OverviewColumn::Total, false, false},
	{"[stat column built]", "construction", OverviewColumn::Total, false, true},
	{"[stat column buildings lost]", "buildings lost", OverviewColumn::Total, false, false},
	{"[stat column prestige]", "prestige", OverviewColumn::Final, true, true},
};
//! The column the teams are listed by, largest first: the peak population.
const std::size_t overviewSortColumn = 0;

double overviewFigure(const OverviewColumn &column, const TeamStats &stats)
{
	const Stats::Metric &metric = Stats::metricById(column.metric);
	if (column.figure == OverviewColumn::Total)
	{
		assert(metric.value);
		return metric.value(stats.measurements);
	}
	assert(metric.sampled >= 0);
	double figure = 0;
	for (const auto &sample : stats.getEndOfGameStats())
		figure = column.figure == OverviewColumn::Peak ? std::max(figure, double(sample.value[metric.sampled])) : sample.value[metric.sampled];
	return figure;
}

//! Index into Stats::RATE_WINDOWS of `window`, or of the default window when a
//! stored setting names one that is not offered.
int rateWindowChoice(int window)
{
	const auto *found = std::find(std::begin(Stats::RATE_WINDOWS), std::end(Stats::RATE_WINDOWS), window);
	if (found == std::end(Stats::RATE_WINDOWS))
		found = std::find(std::begin(Stats::RATE_WINDOWS), std::end(Stats::RATE_WINDOWS), Stats::DEFAULT_RATE_WINDOW);
	return int(found - std::begin(Stats::RATE_WINDOWS));
}
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

EndGameScreen::EndGameScreen(GameGUI *gui) : UIScreen(fe::themeFor(fe::Surface::Results))
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
		for (const auto &column : overviewColumns)
			entry.summary.push_back(overviewFigure(column, gui->game.teams[entry.teamNum]->stats));
		auto existing = std::find_if(teams.begin(), teams.end(), [&](const auto &team) { return team.teamNum == entry.teamNum; });
		if (existing == teams.end())
			teams.push_back(entry);
		else
			existing->name += ", " + entry.name;
	}

	// Save the step and order count
	game = &(gui->game);
	durationSeconds = game->stepCounter / 25;
	if (Team *local = gui->getLocalTeam())
	{
		// Mark the player's own row, as the connection panel does ("Ana (you)").
		for (auto &team : teams)
			if (team.teamNum == local->teamNumber)
				team.name = GAGCore::FormattableString(fe::tr("[conn you %0]")).arg(team.name);
		const Description described = describe(*game, *local);
		outcome = described.outcome;
		reason = described.reason;
	}

	// Largest colony first; the order then stays put whatever is charted.
	std::stable_sort(teams.begin(), teams.end(),
					 [](const TeamEntry &a, const TeamEntry &b) { return a.summary[overviewSortColumn] > b.summary[overviewSortColumn]; });
	// Metrics with nothing to show in this match (no trade, no prestige) are
	// marked in the picker, so the player need not open them to find out.
	std::vector<Stats::TeamHistory> histories;
	for (const auto &team : teams)
		histories.push_back(Stats::historyOf(team.teamNum, game->teams[team.teamNum]->stats));
	for (const auto &entry : Stats::catalog())
		nothingToShow.push_back(!Stats::buildChart(entry, Stats::defaultView(entry), histories).any);
	// Open on what the player last looked at; the overview the first time.
	selectedMetric = Stats::findMetric(globalContainer->settings.statsMetric);
	if (const auto *chosen = metric())
	{
		view = Stats::defaultView(*chosen);
		view.window = globalContainer->settings.statsWindow;
		view = Stats::validView(*chosen, view);
	}
}

EndGameScreen::~EndGameScreen() = default;

EndGameScreen::Description EndGameScreen::describe(const Game &game, const Team &local)
{
	Description d;
	if (local.hasWon)
		d.outcome = Outcome::Victory;
	else if (local.hasLost || !local.isAlive)
		d.outcome = Outcome::Defeat;
	else if (!game.isGameEnded && !game.totalPrestigeReached)
		d.outcome = Outcome::Left;
	// Why: the people who left (their seat became AI `none` when their quit order
	// ran; real AIs have an implementation), the prestige goal, or the fight.
	const GameHeader &header = game.gameHeader;
	std::vector<std::string> leftNames;
	bool opponentPlayed = false;
	for (int i = 0; i < header.getNumberOfPlayers(); ++i)
	{
		const BasePlayer &player = header.getBasePlayer(i);
		const Uint32 mask = Team::teamNumberToMask(player.teamNumber);
		if (player.type == BasePlayer::P_NONE || player.teamNumber == local.teamNumber || (local.allies & mask))
			continue;
		if (player.type == BasePlayer::P_AI && !player.name.empty())
			leftNames.push_back(player.name);
		else
			opponentPlayed = true;
	}
	auto &strings = *GAGCore::Toolkit::getStringTable();
	if (d.outcome == Outcome::Victory)
	{
		if (!leftNames.empty() && !opponentPlayed)
			d.reason = leftNames.size() == 1 ? std::string(GAGCore::FormattableString(strings.getString("[conn notice left %0]")).arg(leftNames.front()))
											 : strings.getString("[results reason opponents left]");
		else if (game.totalPrestigeReached)
			d.reason = strings.getString("[Total prestige reached]");
		else if (local.winCondition == WCWinProbability)
			d.reason = strings.getString("[results reason beyond doubt]");
		else
			d.reason = strings.getString("[results reason victory]");
	}
	else if (d.outcome == Outcome::Defeat)
		d.reason = strings.getString(game.totalPrestigeReached					? "[Total prestige reached]"
									 : local.winCondition == WCWinProbability ? "[results reason beyond doubt]"
																				: "[results reason defeat]");
	else if (d.outcome == Outcome::Left)
		d.reason = strings.getString("[results reason you left]");
	return d;
}


void EndGameScreen::paintBackground(fe::Canvas &canvas)
{
	auto *surface = canvas.surface();
	if (FrontendTheme::current && surface)
		FrontendTheme::current->background(surface, false);
	else
		canvas.fillRect({0, 0, canvas.size().w, canvas.size().h}, theme().palette.paper);
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

const Stats::Metric *EndGameScreen::metric() const
{
	return selectedMetric >= 0 && selectedMetric < int(Stats::catalog().size()) ? &Stats::catalog()[std::size_t(selectedMetric)] : nullptr;
}

void EndGameScreen::selectMetric(int chosen)
{
	if (chosen < OVERVIEW || chosen >= int(Stats::catalog().size()))
		return;
	selectedMetric = chosen;
	const auto *now = metric();
	GAGCore::Recording::recorder().event("statistics_metric", now ? now->id : "overview");
	globalContainer->settings.statsMetric = now ? now->id : "";
	if (now)
	{
		// Each metric opens in the view that suits it; the averaging window is kept.
		const int window = view.window;
		view = Stats::defaultView(*now);
		view.window = window;
	}
	hoverX = hoverY = -1;
	invalidate();
}

void EndGameScreen::setView(Stats::View chosen)
{
	if (const auto *now = metric())
	{
		view = Stats::validView(*now, chosen);
		globalContainer->settings.statsWindow = view.window;
		invalidate();
	}
}

void EndGameScreen::highlightTeam(int teamNum)
{
	highlighted = teamNum;
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
	if (action == OPEN_GROUP_LIST || action == OPEN_METRIC_LIST)
	{
		// The drop-downs of metricChoices(); absent on wide layouts.
		if (auto *node = host().find(action == OPEN_GROUP_LIST ? "group" : "metric"))
			host().tapAt({node->bounds.x + node->bounds.w / 2, node->bounds.y + node->bounds.h / 2});
	}
	else if (action == TOGGLE_EXPANDED_CHART)
		expandChart(!expandedChart);
	else if (action == TOGGLE_TEAM_FILTERS)
		showTeamFilters(!teamFiltersOpen);
	else if (action >= TOGGLE_TEAM_FIRST)
		toggleTeam(action - TOGGLE_TEAM_FIRST);
	else if (action == QUIT)
		endExecute(QUIT);
	else if (action == SAVE_REPLAY)
		saveReplay("replays", "replay");
}

int EndGameScreen::enabledTeams() const
{
	return int(std::count_if(teams.begin(), teams.end(), [](const auto &team) { return team.enabled; }));
}

Element EndGameScreen::teamRows(const Presentation &p)
{
	// The chart's legend and its filter in one: a chip per team, wrapping onto as
	// many rows as the teams need, so a full match still leaves the chart its room.
	std::vector<Element> chips;
	for (std::size_t i = 0; i < teams.size(); ++i)
	{
		const auto &team = teams[i];
		chips.push_back(fe::row({fe::swatch(team.color, 12),
								 fe::expanded(fe::toggle("team/" + std::to_string(i), team.name, team.enabled, [this, i](bool) { toggleTeam(int(i)); }))},
								{p.pt(6), fe::CrossAlign::Center}));
	}
	fe::WrapOptions wrap;
	wrap.gap = p.pt(4);
	wrap.minChildWidth = p.pt(190);
	return fe::wrap(std::move(chips), wrap);
}

Element EndGameScreen::metricSidebar(const Presentation &p)
{
	// Every metric in view at once, under its group, with the overview on top.
	std::vector<Element> rows;
	auto entry = [&](int index, const std::string &title, const std::string &about)
	{
		fe::ButtonOptions options;
		options.flat = options.alignLeft = true;
		options.selected = selectedMetric == index;
		options.minHeight = 22;
		options.role = fe::FontRole::Support;
		options.tooltip = about;
		rows.push_back(fe::button("metric/" + std::to_string(index), title, [this, index] { selectMetric(index); }, options));
	};
	entry(OVERVIEW, fe::tr("[stat overview]"), fe::tr("[stat overview about]"));
	Stats::Group group = Stats::Group::Count;
	const auto &metrics = Stats::catalog();
	for (std::size_t i = 0; i < metrics.size(); ++i)
	{
		if (metrics[i].group != group)
		{
			group = metrics[i].group;
			rows.push_back(fe::spacer(p.pt(6)));
			rows.push_back(fe::caption(fe::tr(Stats::groupKey(group))));
		}
		entry(int(i), pickerTitle(int(i)), TeamStatChart::about(metrics[i]));
	}
	fe::ScrollOptions scroll;
	scroll.shrinkToContent = false;
	return fe::scroll("metrics", fe::column(std::move(rows), {p.pt(1)}), scroll);
}

Element EndGameScreen::metricChoices(const Presentation &p)
{
	// Narrow layouts: the group, then the metrics of that group, so neither list
	// is long. The metric list explains the selected metric.
	const auto &metrics = Stats::catalog();
	const auto *now = metric();
	std::vector<std::string> groups{fe::tr("[stat overview]")};
	for (int g = 0; g < int(Stats::Group::Count); ++g)
		groups.push_back(fe::tr(Stats::groupKey(Stats::Group(g))));
	auto first = [&metrics](Stats::Group group)
	{
		for (std::size_t i = 0; i < metrics.size(); ++i)
			if (metrics[i].group == group)
				return int(i);
		return OVERVIEW;
	};
	std::vector<Element> controls;
	controls.push_back(fe::expanded(fe::choice("group", groups, now ? int(now->group) + 1 : 0,
											   [this, first](int chosen) { selectMetric(chosen == 0 ? OVERVIEW : first(Stats::Group(chosen - 1))); })));
	if (now)
	{
		std::vector<std::string> names;
		std::vector<int> indices;
		for (std::size_t i = 0; i < metrics.size(); ++i)
			if (metrics[i].group == now->group)
			{
				names.push_back(pickerTitle(int(i)));
				indices.push_back(int(i));
			}
		const int selected = int(std::find(indices.begin(), indices.end(), selectedMetric) - indices.begin());
		fe::ChoiceOptions options;
		options.help = TeamStatChart::about(*now);
		controls.push_back(fe::expanded(fe::choice("metric", names, selected, [this, indices](int chosen) { selectMetric(indices[std::size_t(chosen)]); }, options), 2));
	}
	// Side by side they would each be too narrow for their text on a phone.
	if (p.dialog.w < p.pt(520))
	{
		for (auto &control : controls)
			control = fe::row({std::move(control)});
		return fe::column(std::move(controls), {p.pt(4)});
	}
	return fe::row(std::move(controls), {p.pt(8), fe::CrossAlign::Center});
}

std::string EndGameScreen::pickerTitle(int index) const
{
	const std::string title = TeamStatChart::title(Stats::catalog()[std::size_t(index)]);
	return nothingToShow[std::size_t(index)] ? std::string(GAGCore::FormattableString(fe::tr("[stat %0 nothing]")).arg(title)) : title;
}

Element EndGameScreen::viewControls(const Presentation &p)
{
	const auto *now = metric();
	if (!now)
		return nullptr;
	std::vector<Element> controls;
	auto change = [this](auto edit)
	{
		return [this, edit](auto value)
		{
			Stats::View next = view;
			edit(next, value);
			setView(next);
		};
	};
	if (Stats::canTotal(*now))
		controls.push_back(fe::segments("view/measure", {fe::tr("[stat view per minute]"), fe::tr("[stat view total]")}, view.total ? 1 : 0,
										change([](Stats::View &v, int chosen) { v.total = chosen == 1; })));
	// Rates, and levels that are averaged like them, choose how far back.
	if ((now->kind == Stats::Metric::Counter && !view.total) || now->smoothed)
	{
		std::vector<std::string> labels;
		for (int window : Stats::RATE_WINDOWS)
			labels.push_back(GAGCore::FormattableString(fe::tr("[stat view %0 min]")).arg(Stats::windowMinutes(window)));
		controls.push_back(fe::segments("view/window", labels, rateWindowChoice(view.window),
										change([](Stats::View &v, int chosen) { v.window = Stats::RATE_WINDOWS[chosen]; })));
	}
	if (Stats::canSplit(*now))
		controls.push_back(fe::toggle("view/split", fe::tr(now->splitKey), view.split, change([](Stats::View &v, bool on) { v.split = on; })));
	if (Stats::canRelative(*now, view))
	{
		const char *key = view.split ? "[stat view percentages]" : now->kind == Stats::Metric::Gauge ? "[stat view percent of units]" : "[stat view per 100 units]";
		controls.push_back(fe::toggle("view/relative", fe::tr(key), view.relative, change([](Stats::View &v, bool on) { v.relative = on; })));
	}
	if (Stats::canShare(*now, view))
		controls.push_back(fe::toggle("view/share", fe::tr("[stat view share]"), view.share, change([](Stats::View &v, bool on)
																									   {
																										   v.share = on;
																										   v.relative = v.relative && !on;
																									   })));
	if (!view.split && !now->global && enabledTeams() > 2)
	{
		// With many lines, one can be picked out: drawn heavier, the rest dimmed.
		std::vector<std::string> names{fe::tr("[stat view highlight none]")};
		std::vector<int> numbers{-1};
		int selected = 0;
		for (const auto &team : teams)
			if (team.enabled)
			{
				if (team.teamNum == highlighted)
					selected = int(names.size());
				names.push_back(team.name);
				numbers.push_back(team.teamNum);
			}
		controls.push_back(fe::choice("view/highlight", names, selected, [this, numbers](int chosen) { highlightTeam(numbers[std::size_t(chosen)]); }));
	}
	if (controls.empty())
		return nullptr;
	fe::WrapOptions wrap;
	wrap.gap = p.pt(8);
	wrap.minChildWidth = p.pt(170);
	return fe::wrap(std::move(controls), wrap);
}

Element EndGameScreen::overview(const Presentation &p, bool compact)
{
	// One row per team with its figures for the whole match; the best of each
	// column stands out, and a column's heading opens its chart.
	const int column = p.pt(compact ? 64 : 92), gap = p.pt(6);
	// As many columns as fit beside a readable team name, the essential ones
	// first. A column in which no team has anything (prestige in a match without
	// prestige) is left out.
	const int room = p.dialog.w - (compact ? 0 : p.pt(242)) - p.pt(compact ? 20 : 32) - p.pt(compact ? 110 : 170);
	std::size_t fits = std::size_t(std::max(2, room / (column + gap)));
	std::vector<bool> wanted(std::size(overviewColumns), false);
	for (bool essential : {true, false})
		for (std::size_t c = 0; c < std::size(overviewColumns) && fits > 0; ++c)
			if (overviewColumns[c].compact == essential &&
				std::any_of(teams.begin(), teams.end(), [c](const TeamEntry &team) { return team.summary[c] != 0; }))
			{
				wanted[c] = true;
				--fits;
			}
	std::vector<std::size_t> shown;
	for (std::size_t c = 0; c < wanted.size(); ++c)
		if (wanted[c])
			shown.push_back(c);
	fe::TextOptions head;
	head.role = fe::FontRole::Support;
	head.muted = true;
	std::vector<Element> header{fe::expanded(fe::label(fe::tr("[results team]"), head))};
	for (std::size_t c : shown)
	{
		fe::ButtonOptions options;
		options.flat = options.alignLeft = true;
		options.role = fe::FontRole::Support;
		options.minHeight = 24;
		const int index = Stats::findMetric(overviewColumns[c].metric);
		options.tooltip = TeamStatChart::about(Stats::metricById(overviewColumns[c].metric));
		header.push_back(fe::width(column, fe::button("overview/" + std::to_string(c), fe::tr(overviewColumns[c].key), [this, index] { selectMetric(index); }, options)));
	}
	std::vector<Element> rows;
	for (const auto &team : teams)
	{
		std::vector<Element> cells{fe::swatch(team.color, 12), fe::expanded(fe::label(team.name))};
		for (std::size_t c : shown)
		{
			double best = 0;
			for (const auto &other : teams)
				best = std::max(best, other.summary[c]);
			fe::TextOptions options;
			if (overviewColumns[c].moreIsBetter && teams.size() > 1 && best > 0 && team.summary[c] == best)
				options.color = theme().palette.success;
			// Indented like the heading's button text, so figures sit under their heading.
			cells.push_back(fe::width(column, fe::padding({p.pt(8), 0, 0, 0}, fe::label(Stats::valueText(team.summary[c], false), options))));
		}
		rows.push_back(fe::row(std::move(cells), {gap, fe::CrossAlign::Center}));
	}
	fe::ScrollOptions scroll;
	scroll.shrinkToContent = false;
	std::vector<Element> parts;
	// How it ended and how long it took. Online matches say so in their banner.
	if (!online)
	{
		const char *titleKey = outcome == Outcome::Victory ? "[results victory]" : outcome == Outcome::Ended ? "[results match over]" : "[results defeat]";
		parts.push_back(fe::heading(fe::tr(titleKey)));
		parts.push_back(fe::caption((reason.empty() ? std::string() : reason + " · ") + Glob2UI::durationText(durationSeconds)));
	}
	parts.push_back(fe::row(std::move(header), {gap, fe::CrossAlign::Center}));
	parts.push_back(fe::divider());
	parts.push_back(fe::expanded(fe::scroll("overview", fe::column(std::move(rows), {p.pt(6)}), scroll)));
	parts.push_back(fe::paragraph(fe::tr("[stat overview about]"), {fe::FontRole::Support, true}));
	return fe::column(std::move(parts), {p.pt(4)});
}

void EndGameScreen::setOnlineResult(std::shared_ptr<Online::OnlineMatchResult> result)
{
	online = std::move(result);
	onlineRevision = ~0u;
	invalidate();
}

namespace
{
// The same wording as the hub's Recent matches and the profile.
std::string minutesText(Uint32 seconds)
{
	return Glob2UI::durationText(seconds);
}
std::string ratingText(double value)
{
	return std::to_string(int(std::lround(value)));
}
} // namespace

Element EndGameScreen::onlineBanner(const Presentation &p)
{
	// The platform's outcome wins once it has one: a verified result, or a draw
	// (alliances tied at the top, e.g. equal prestige at the sudden-death timer),
	// which the engine's own end condition may still call a win.
	Outcome shown = outcome;
	if (online->outcome == "won")
		shown = Outcome::Victory;
	else if (online->outcome == "lost" || online->outcome == "abandoned")
		shown = Outcome::Defeat;
	const bool draw = online->outcome == "draw";
	// Leaving counts as a loss (the Leave match confirmation says so); the reason
	// line says it was a departure.
	const char *titleKey = draw														? "[results draw]"
						   : shown == Outcome::Victory								? "[results victory]"
						   : shown == Outcome::Defeat || shown == Outcome::Left ? "[results defeat]"
																					: "[results match over]";
	std::string subtitle = online->label;
	if (!online->mapTitle.empty())
		subtitle += " · " + online->mapTitle;
	subtitle += " · " + minutesText(durationSeconds);
	fe::IconOptions trophy;
	trophy.size = p.touch ? 28 : 32;
	trophy.color = theme().palette.accent;
	std::vector<Element> lines{fe::title(fe::tr(titleKey))};
	// The platform's verdict can differ from what this game saw (a draw); then the
	// local reason would contradict the title.
	const bool agrees = shown == outcome || (outcome == Outcome::Left && shown == Outcome::Defeat);
	if (!reason.empty() && !draw && agrees)
		lines.push_back(fe::paragraph(reason, {fe::FontRole::Body, true}));
	lines.push_back(fe::caption(subtitle));
	auto words = fe::column(std::move(lines), {p.pt(2)});
	return fe::row({shown == Outcome::Victory && !draw ? fe::icon(fe::uiIcon(fe::UIIcon::Trophy), trophy) : nullptr,
					fe::expanded(words)},
				   {p.pt(10), fe::CrossAlign::Center});
}

Element EndGameScreen::ratingCard(const Presentation &p)
{
	using V = Online::OnlineMatchResult::Verification;
	const auto &r = *online;
	const auto palette = theme().palette;
	fe::TextOptions big;
	big.role = fe::FontRole::Heading;
	fe::TextOptions greyed = big;
	greyed.color = palette.muted;
	std::vector<Element> lines;
	std::string head = r.rated ? GAGCore::FormattableString(fe::tr("[results ladder rating %0]")).arg(r.ladder.empty() ? fe::tr("[results ranked]") : r.ladder)
					   : fe::tr(r.fromRoom ? "[results room unrated]" : "[results quick unrated]");
	using Phase = Online::OnlineMatchResult::Phase;
	const Phase phase = r.phase();
	// While the match still runs on the relay (someone has not left yet) or the
	// verifier replays it, say which, and say so when it takes longer than usual.
	auto pending = [&](const char *key) {
		// Whoever left already has their result (a loss); the others may play on for
		// a long time, so the record follows without a spinner to wait on.
		if (outcome == Outcome::Left && phase == Phase::Waiting)
		{
			lines.push_back(fe::paragraph(fe::tr("[results final after end]"), {fe::FontRole::Support, true}));
			return;
		}
		lines.push_back(fe::row({fe::icon(fe::uiIcon(fe::UIIcon::Spinner), {16, palette.muted}),
								 fe::label(fe::tr(key), {fe::FontRole::Body})},
								{p.pt(4), fe::CrossAlign::Center}));
		const char *detail = phase == Phase::Waiting ? (r.slow ? "[results waiting slow]" : "[results waiting detail]")
												  : (r.slow ? "[results verifying slow]" : "[results verifying detail]");
		if (!p.compact() || r.slow)
			lines.push_back(fe::paragraph(fe::tr(detail), {fe::FontRole::Support, true}));
	};
	if (!r.rated)
	{
		lines.push_back(fe::label(head, big));
		if (phase == Phase::Done)
			lines.push_back(fe::caption(fe::tr("[results saved to history]")));
		else
			pending(phase == Phase::Waiting ? "[results waiting for players]" : "[results recording]");
	}
	else if (r.verification == V::Verified && r.ratingAfter)
	{
		lines.push_back(fe::caption(head));
		std::string value = ratingText(*r.ratingAfter);
		if (r.ratingBefore)
		{
			const int delta = int(std::lround(*r.ratingAfter - *r.ratingBefore));
			value += std::string("  ") + (delta >= 0 ? "+" : "−") + std::to_string(std::abs(delta));
		}
		lines.push_back(fe::label(value, big));
		lines.push_back(fe::row({fe::icon(fe::uiIcon(fe::UIIcon::ShieldCheck), {16, palette.success}),
								 fe::caption(fe::tr(r.provisional ? "[results verified provisional]" : "[results verified]"), false)},
								{p.pt(4), fe::CrossAlign::Center}));
	}
	else if (r.verification == V::Unverifiable || r.verification == V::Diverged || r.verification == V::NotApplicable)
	{
		lines.push_back(fe::caption(head));
		lines.push_back(fe::label((r.ratingBefore ? ratingText(*r.ratingBefore) + "  " : std::string()) + fe::tr("[results no change]"), big));
		lines.push_back(fe::paragraph(fe::tr("[results unverifiable]"), {fe::FontRole::Support, true}));
	}
	else
	{
		lines.push_back(fe::caption(head));
		const bool won = outcome == Outcome::Victory && r.outcome != "draw";
		std::optional<double> expected = won ? r.ratingExpectedWin : r.ratingExpectedLoss;
		if (r.ratingBefore && expected)
			lines.push_back(fe::label(ratingText(*r.ratingBefore) + " → " + ratingText(*expected) + "?", greyed));
		pending(phase == Phase::Waiting ? "[results waiting for players]" : "[results verifying]");
	}
	fe::CardOptions options;
	options.color = palette.field;
	options.border = palette.line;
	options.padding = p.pt(10);
	options.shadow = false;
	return fe::card(fe::column(std::move(lines), {p.pt(2)}), options);
}

Element EndGameScreen::compactHeader(const Presentation &p)
{
	// The metric drop-downs, and beside them the way to the team filters, which
	// compact layouts keep folded away.
	std::vector<Element> header;
	header.push_back(fe::expanded(metricChoices(p)));
	if (metric())
	{
		fe::ButtonOptions teamOptions;
		teamOptions.selected = teamFiltersOpen;
		header.push_back(fe::button("teams", GAGCore::FormattableString(fe::tr("[Teams %0/%1]")).arg(enabledTeams()).arg(teams.size()),
									[this] { showTeamFilters(!teamFiltersOpen); }, teamOptions));
	}
	return fe::row(std::move(header), {p.pt(8), fe::CrossAlign::Center});
}

Element EndGameScreen::chartCanvas(const Presentation &p)
{
	fe::CanvasOptions options;
	options.accessibleText = TeamStatChart::title(*metric());
	// The readout follows the pointer, and on touch the last tap.
	auto inspectAt = [this](fe::Point local)
	{
		hoverX = local.x;
		hoverY = local.y;
	};
	options.hover = inspectAt;
	options.tap = [inspectAt](fe::Point local, fe::Host &) { inspectAt(local); };
	return fe::canvas("chart", {p.pt(320), p.pt(160)}, [this](fe::Canvas &c, fe::Rect r, const fe::Frame &) { paintChart(c, r); }, options);
}

Element EndGameScreen::actionBar(const Presentation &p, bool compact)
{
	std::vector<fe::MenuAction> actions;
	if (metric())
		actions.push_back({"expand", fe::tr(expandedChart ? "[Back to chart]" : "[Expand chart]"), [this] { expandChart(!expandedChart); }});
	if (globalContainer->replayWriter && globalContainer->replayWriter->isValid())
		actions.push_back({"save-replay", fe::tr("[save replay]"), [this] { saveReplay("replays", "replay"); }});
	// Wide layouts have the match page under the metric list.
	if (online && compact)
		actions.push_back({"match-page", fe::tr("[results match page]"), [this] { openMatchPage(); }});
	// Rematch after a quick match (Q9): an unrated room with the same players; the
	// others are invited, and one who asks after them joins the same room.
	if (online && !online->fromRoom)
		actions.push_back({"rematch",
						   online->rematchOfferedBy.empty()
							   ? fe::tr("[results rematch]")
							   : std::string(GAGCore::FormattableString(fe::tr("[results join rematch %0]")).arg(online->rematchOfferedBy)),
						   [this] { rematch(); }});
	const char *quitKey = !online ? "[quit]" : online->fromRoom ? "[results back to room]" : "[results back to online]";
	// After a quick match the usual next step is another one in the same queue.
	const bool queueAgain = online && !online->fromRoom;
	actions.push_back({"quit", fe::tr(quitKey), [this] { endExecute(QUIT); }, !queueAgain, queueAgain ? SDLK_UNKNOWN : SDLK_RETURN});
	if (queueAgain)
		actions.push_back({"queue-again", fe::tr("[results find another match]"), [this] { findAnotherMatch(); }, true, SDLK_RETURN});
	return fe::actions(std::move(actions), p);
}

void EndGameScreen::openMatchPage()
{
	GAGCore::ApplicationHost::openUrl(online->matchPageUrl());
}

Element EndGameScreen::build(const Presentation &p)
{
	// Below about 900 points there is no room for the metric list beside the chart.
	const bool compact = p.compact() || p.shortLandscape() || p.dialog.w < p.pt(900);
	std::vector<Element> parts;
	if (online && (!expandedChart || !compact))
	{
		if (compact)
		{
			parts.push_back(onlineBanner(p));
			parts.push_back(ratingCard(p));
		}
		else
			parts.push_back(fe::row({fe::expanded(onlineBanner(p)), fe::maxWidth(p.pt(420), ratingCard(p))},
									{p.pt(12), fe::CrossAlign::Center}));
	}
	const auto *now = metric();
	// The expanded chart gives up what surrounds it: the metric list and the team
	// chips, and on compact layouts the online result and the view controls too.
	const bool chartOnly = expandedChart && now;
	std::vector<Element> content;
	if (!chartOnly && compact)
		content.push_back(compactHeader(p));
	if (!now)
		content.push_back(fe::expanded(overview(p, compact)));
	else
	{
		// A map-wide metric is one line for everybody: there are no teams to pick.
		if (!chartOnly && !now->global && (!compact || teamFiltersOpen))
		{
			auto rows = teamRows(p);
			if (compact)
				content.push_back(fe::constrained({0, 0, fe::Constraints::Unbounded, p.pt(160)}, fe::scroll("teams/scroll", rows)));
			else
				content.push_back(rows);
		}
		// What the chart measures, in a sentence or two, above it like a title.
		// Compact layouts keep the plot its room: their metric list explains it.
		if (!chartOnly && !compact)
			content.push_back(fe::column({fe::label(TeamStatChart::title(*now), {fe::FontRole::Body}),
										  fe::paragraph(TeamStatChart::about(*now), {fe::FontRole::Support, true})},
										 {p.pt(2)}));
		else
			content.push_back(fe::caption(TeamStatChart::title(*now)));
		if (!chartOnly || !compact)
			content.push_back(viewControls(p));
		content.push_back(fe::expanded(chartCanvas(p)));
	}
	auto main = fe::column(std::move(content), {p.pt(8)});
	if (compact || chartOnly)
		parts.push_back(fe::expanded(main));
	else
	{
		std::vector<Element> side{fe::expanded(metricSidebar(p))};
		if (online)
			side.push_back(fe::button("match-page", fe::tr("[results match page]"), [this] { openMatchPage(); },
									  {.flat = true, .icon = fe::uiIcon(fe::UIIcon::ExternalLink), .iconSize = 16}));
		parts.push_back(fe::expanded(fe::row({fe::width(p.pt(230), fe::column(std::move(side), {p.pt(4)})), fe::expanded(main)}, {p.pt(12)})));
	}
	parts.push_back(actionBar(p, compact));
	// A paper card over the colony background, as the Online hub and the room have.
	fe::CardOptions page;
	page.padding = p.pt(compact ? 10 : 16);
	return fe::card(fe::column(std::move(parts), {p.pt(8)}), page);
}

void EndGameScreen::paintChart(fe::Canvas &canvas, fe::Rect r)
{
	chartBounds = r;
	auto *surface = canvas.surface();
	if (!surface)
		return;
	canvas.fillRounded(r, 6, TeamStatChart::background);
	if (std::none_of(teams.begin(), teams.end(), [](const auto &team) { return team.enabled; }))
	{
		canvas.text({r.x + 8, r.y + 8}, fe::FontRole::Body, fe::tr("[Select a team to show its history.]"), TeamStatChart::ink);
		return;
	}
	canvas.pushClip(r);
	TeamStatChart::paint(*game, *surface, r.x, r.y, r.w, r.h, chartOptions());
	canvas.popClip();
}

// The chart itself is shared with the in-match statistics sheet.
TeamStatChart::Options EndGameScreen::chartOptions() const
{
	TeamStatChart::Options options;
	options.metric = metric();
	options.view = view;
	for (const auto &team : teams)
		if (team.enabled)
			options.teams.push_back({team.teamNum, team.color, team.name});
	options.highlighted = highlighted;
	options.hoverX = hoverX;
	options.hoverY = hoverY;
	return options;
}

void EndGameScreen::saveReplay(const char *dir, const char *ext)
{
	if (replaySave)
		return;
	replaySave = std::make_unique<LoadSaveDialog>(dir, ext, false, Toolkit::getStringTable()->getString("[save replay]"), "",
												  replayFilenameToName, glob2NameToFilename, fe::Surface::Results);
	if (gfx)
		replaySave->attach(*gfx);
	GAGCore::ApplicationHost::screenChanged(typeid(*replaySave).name());
}

void EndGameScreen::rematch()
{
	if (!online)
		return;
	Online::RematchRequest request;
	request.matchId = online->matchId;
	if (!Online::requestRematch(request))
		return;
	endExecute(QUIT);
}

void EndGameScreen::findAnotherMatch()
{
	if (!online || !Online::requestQueueAgain())
		return;
	endExecute(QUIT);
}

void EndGameScreen::updateExecution(Uint32 tick)
{
	// Verification results arrive through the shared client (Online::pump).
	if (online)
		online->poll(SDL_GetTicks());
	if (online && online->revision != onlineRevision)
	{
		onlineRevision = online->revision;
		invalidate();
	}
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
