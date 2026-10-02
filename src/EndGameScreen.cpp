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
#include "OnlineHandoff.h"
#include "OnlineMatch.h"
#include <SDL.h>
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
	durationSeconds = game->stepCounter / 25;
	if (Team *local = gui->getLocalTeam())
	{
		if (local->hasWon)
			outcome = Outcome::Victory;
		else if (local->hasLost || !local->isAlive)
			outcome = Outcome::Defeat;
		else if (!game->isGameEnded && !game->totalPrestigeReached)
			outcome = Outcome::Left;
	}

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

void EndGameScreen::setOnlineResult(std::shared_ptr<Online::OnlineMatchResult> result)
{
	online = std::move(result);
	onlineRevision = ~0u;
	invalidate();
}

namespace
{
std::string minutesText(Uint32 seconds)
{
	if (seconds < 60)
		return GAGCore::FormattableString(fe::tr("[results seconds %0]")).arg(seconds);
	return GAGCore::FormattableString(fe::tr("[results minutes %0]")).arg(seconds / 60);
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
	const char *titleKey = draw							? "[results draw]"
						   : shown == Outcome::Victory ? "[results victory]"
						   : shown == Outcome::Defeat	? "[results defeat]"
						   : shown == Outcome::Left		? "[results left]"
														: "[results match over]";
	std::string subtitle = online->label;
	if (!online->mapTitle.empty())
		subtitle += " · " + online->mapTitle;
	subtitle += " · " + minutesText(durationSeconds);
	fe::IconOptions trophy;
	trophy.size = p.touch ? 28 : 32;
	trophy.color = fe::inGameTheme().palette.accent;
	auto words = fe::column({fe::title(fe::tr(titleKey)), fe::caption(subtitle)}, {p.pt(2)});
	return fe::row({shown == Outcome::Victory && !draw ? fe::icon(fe::uiIcon(fe::UIIcon::Trophy), trophy) : nullptr,
					fe::expanded(words)},
				   {p.pt(10), fe::CrossAlign::Center});
}

Element EndGameScreen::ratingCard(const Presentation &p)
{
	using V = Online::OnlineMatchResult::Verification;
	const auto &r = *online;
	const auto palette = fe::inGameTheme().palette;
	fe::TextOptions big;
	big.role = fe::FontRole::Heading;
	fe::TextOptions greyed = big;
	greyed.color = palette.muted;
	std::vector<Element> lines;
	std::string head = r.rated ? GAGCore::FormattableString(fe::tr("[results ladder rating %0]")).arg(r.ladder.empty() ? fe::tr("[results ranked]") : r.ladder)
							   : fe::tr("[results room unrated]");
	using Phase = Online::OnlineMatchResult::Phase;
	const Phase phase = r.phase();
	// While the match still runs on the relay (someone has not left yet) or the
	// verifier replays it, say which, and say so when it takes longer than usual.
	auto pending = [&](const char *key) {
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

Element EndGameScreen::build(const Presentation &p)
{
	const bool compact = p.compact() || p.shortLandscape();
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
		if (online && !compact)
			header.push_back(fe::button("match-page", fe::tr("[results match page]"), [this] { GAGCore::ApplicationHost::openUrl(online->matchPageUrl()); },
										{.flat = true, .icon = fe::uiIcon(fe::UIIcon::ExternalLink), .iconSize = 16}));
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
	if (online && compact)
		actions.push_back({"match-page", fe::tr("[results match page]"), [this] { GAGCore::ApplicationHost::openUrl(online->matchPageUrl()); }});
	// Rematch after a quick match (Q9): an unrated room with the same players; the
	// others are invited, and one who asks after them joins the same room.
	if (online && !online->fromRoom)
		actions.push_back({"rematch",
						   online->rematchOfferedBy.empty()
							   ? fe::tr("[results rematch]")
							   : std::string(GAGCore::FormattableString(fe::tr("[results join rematch %0]")).arg(online->rematchOfferedBy)),
						   [this] { rematch(); }});
	const char *quitKey = !online ? "[quit]" : online->fromRoom ? "[results back to room]" : "[results back to online]";
	actions.push_back({"quit", fe::tr(quitKey), [this] { endExecute(QUIT); }, true, SDLK_RETURN});
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

void EndGameScreen::updateExecution(Uint32 tick)
{
	// Verification results arrive through the shared client (Online::pump).
	if (online)
		online->poll(SDL_GetTicks64());
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
