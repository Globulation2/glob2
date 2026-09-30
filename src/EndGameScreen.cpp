// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière
// Copyright (C) 2006 Bradley Arsenault

#include "EndGameScreen.h"
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
	if (type >= 6)
		return Toolkit::getStringTable()->getString(TeamStats::measurementLabel(type - 6));
	switch (type)
	{
	case EndOfGameStat::TYPE_UNITS:
		return Toolkit::getStringTable()->getString("[Units]");
	case EndOfGameStat::TYPE_BUILDINGS:
		return Toolkit::getStringTable()->getString("[Buildings]");
	case EndOfGameStat::TYPE_PRESTIGE:
		return Toolkit::getStringTable()->getString("[Prestige]");
	case EndOfGameStat::TYPE_HP:
		return Toolkit::getStringTable()->getString("[hp]");
	case EndOfGameStat::TYPE_ATTACK:
		return Toolkit::getStringTable()->getString("[Attack]");
	case EndOfGameStat::TYPE_DEFENSE:
		return Toolkit::getStringTable()->getString("[Defense]");
	default:
		assert(false);
		return "";
	}
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

double EndGameScreen::getValue(double position, int team, int type) const
{
	int s = game->teams[team]->stats.endOfGameStats.size() - 1;
	int lower = int(position * float(s));
	int upper = lower + 1;
	double mu = (position * float(s)) - lower;

	int y1 = game->teams[team]->stats.endOfGameStats[lower].value[type];
	int y2 = game->teams[team]->stats.endOfGameStats[upper].value[type];

	//Linear interpolation
	return (1 - mu) * y1 + mu * y2;
}

std::string EndGameScreen::getTimeText(int seconds)
{
	int min = int(seconds) / 60;
	int sec = int(seconds) % 60;
	std::stringstream str;
	str << min << ":" << std::setw(2) << std::setfill('0') << sec;
	return str.str();
}

std::string EndGameScreen::getRightScaleText(int value, int digits)
{
	std::stringstream str;
	str << std::setw(digits) << std::setfill('0') << value;
	return str.str();
}

void EndGameScreen::paintCurves(GAGCore::DrawableSurface &surface, fe::Rect r)
{
	const int type = selectedMetric;
	const int x = r.x, y = r.y, w = r.w, h = r.h;
	if (game->teams[0]->stats.endOfGameStats.size() < 2)
	{
		surface.drawString(x + 8, y + 16, globalContainer->standardFont, GAGCore::Toolkit::getStringTable()->getString("[Not enough recorded history yet.]"));
		return;
	}
	// find maximum
	int team, maxValue = 0;
	unsigned int pos = 0;
	for (team = 0; team < game->mapHeader.getNumberOfTeams(); team++)
		if (teamEnabled(team))
			for (pos = 0; pos < game->teams[team]->stats.endOfGameStats.size(); pos++)
				maxValue = std::max(maxValue, game->teams[team]->stats.endOfGameStats[pos].value[type]);

	//Calculate the number of digits used by the max value when rounded up to the nearest 10
	int num = 10;
	maxValue += num - (maxValue % num);
	std::stringstream maxstr;
	maxstr << maxValue;
	int max_digit_count = maxstr.str().size();

	//Compute the maximum width used by the right-scale
	int max_width = -1;
	for (int n = 0; n < num; ++n)
	{
		int value = maxValue - (maxValue * n) / num;
		std::string valueText = getRightScaleText(value, max_digit_count - 1);
		int width = globalContainer->littleFont->getStringWidth(valueText.c_str());
		max_width = std::max(width, max_width);
	}

	//Compute the maximum height used by the time-scale
	int time_period = (game->teams[0]->stats.endOfGameStats.size() * 512) / 25;
	int max_height = 0;
	for (int n = 1; n < 16; ++n)
	{
		int time = (time_period * n) / 15;
		std::string timeText = getTimeText(time);
		int height = globalContainer->littleFont->getStringHeight(timeText.c_str());
		max_height = std::max(height, max_height);
	}

	///Effective width and height
	int e_width = std::max(1, w - max_width - 8);
	int e_height = std::max(1, h - max_height - 8);

	//Draw horizontal lines to given the scale of the graphs values.
	double line_separate = double(e_height) / double(num);
	// Short landscape charts need fewer labels, not overlapping text.
	const int valueStride = std::max(1, int(std::ceil((max_height + 4) / std::max(1.0, line_separate))));
	for (int n = 0; n < num; n += valueStride)
	{
		int pos = int(double(n) * line_separate + 0.5);
		int value = maxValue - (maxValue * n) / num;
		if (n != 0)
		{
			surface.drawHorzLine(x, y + pos, e_width, Color(68, 51, 82));
			surface.drawHorzLine(x + e_width - 5, y + pos, 10, InGameTouchTheme::ink);
		}
		std::string valueText = getRightScaleText(value, max_digit_count - 1);
		int height = globalContainer->littleFont->getStringHeight(valueText.c_str());
		surface.drawString(x + e_width + 8, y + pos - height / 2, globalContainer->littleFont, valueText.c_str());
	}

	///Draw vertical lines to give the timescale
	double time_line_separate = double(e_width) / double(15);
	for (int n = 1; n < 16; n += std::max(1, 1200 / std::max(1, e_width)))
	{
		int pos = int(double(x) + time_line_separate * double(n) + 0.5);
		int time = (time_period * n) / 15;
		if (n != 15)
			surface.drawVertLine(pos, y + e_height - 5, 10, InGameTouchTheme::ink);
		std::string timeText = getTimeText(time);
		int width = globalContainer->littleFont->getStringWidth(timeText.c_str());
		surface.drawString(pos - width / 2, y + e_height + 8, globalContainer->littleFont, timeText);
	}

	// draw background
	surface.drawRect(x, y, e_width, e_height, InGameTouchTheme::border);

	int closest_position = std::numeric_limits<int>::max();
	int circle_position_value = -1;
	int circle_position_x = -1;
	int circle_position_y = -1;

	// draw curve
	if (maxValue)
	{
		for (team = 0; team < game->mapHeader.getNumberOfTeams(); team++)
		{
			if (!teamEnabled(team))
				continue;
			const Color &color = game->teams[team]->color;

			int previous_y = e_height - int(double(e_height) * getValue(0, team, type) / double(maxValue));

			for (int px = 0; px < (e_width - 2); ++px)
			{
				double value = getValue(double(px) / double(e_width - 2), team, type);
				int ny = e_height - int(double(e_height) * value / double(maxValue));
				surface.drawLine(x + px, y + previous_y, x + px + 1, y + ny, color);
				previous_y = ny;
				const int dist = std::abs(hoverX - px - 1) * 4096 + std::abs(hoverY - ny);
				if (hoverX >= 0 && hoverX < e_width && hoverY >= 0 && hoverY < e_height && dist < closest_position)
				{
					circle_position_value = int(std::floor(value + 0.5));
					circle_position_x = x + px;
					circle_position_y = y + ny;
					closest_position = dist;
				}
			}
		}
	}
	if (circle_position_x != -1)
	{
		surface.drawVertLine(circle_position_x, y, e_height, InGameTouchTheme::border);
		surface.drawCircle(circle_position_x, circle_position_y, 10, Color::white);
		std::stringstream str;
		str << circle_position_value;
		surface.drawString(circle_position_x + 10, circle_position_y + 10, globalContainer->littleFont, str.str());
	}
}

void EndGameScreen::paintMeasurements(GAGCore::DrawableSurface &surface, fe::Rect r)
{
	const int type = selectedMetric;
	const int x = r.x, y = r.y, w = r.w, h = r.h;
	auto *font = globalContainer->littleFont;
	Uint64 maximum = 1;
	for (int t = 0; t < game->mapHeader.getNumberOfTeams(); ++t)
		if (teamEnabled(t))
			for (const auto &m : game->teams[t]->stats.measurementHistory)
				if (type - 6 < 16 || m.tick > game->teams[t]->stats.extendedCoverageStartTick ||
					(game->teams[t]->stats.extendedCoverageStartTick == 0 && m.tick == 0))
					maximum = std::max(maximum, TeamStats::graphValue(m, type - 6));
	const std::string scale = std::to_string(maximum);
	const int ew = std::max(1, w - font->getStringWidth(scale) - 12), eh = std::max(1, h - 24);
	surface.drawRect(x, y, ew, eh, InGameTouchTheme::border);
	surface.drawString(x + ew + 4, y, font, scale);
	surface.drawString(x + ew + 4, y + eh - 12, font, "0");
	const Uint32 end = std::max(Uint32(1), game->stepCounter);
	for (int i = 0; i <= 4; ++i)
		surface.drawString(x + ew * i / 4, y + eh + 6, font, getTimeText(static_cast<Uint64>(end) * i / 100));
	int closest = std::numeric_limits<int>::max();
	Uint64 hover = 0;
	int markX = -1, markY = -1;
	for (int t = 0; t < game->mapHeader.getNumberOfTeams(); ++t)
	{
		if (!teamEnabled(t))
			continue;
		const auto &stats = game->teams[t]->stats;
		int prevX = -1, prevY = -1;
		for (const auto &m : stats.measurementHistory)
		{
			if (type - 6 >= 16 && m.tick <= stats.extendedCoverageStartTick && !(stats.extendedCoverageStartTick == 0 && m.tick == 0))
				continue;
			const Uint64 value = TeamStats::graphValue(m, type - 6);
			const int px = static_cast<Uint64>(m.tick) * ew / end;
			const int py = eh - static_cast<long double>(value) * eh / maximum;
			if (prevX >= 0)
				surface.drawLine(x + prevX, y + prevY, x + px, y + py, game->teams[t]->color);
			else
				surface.drawCircle(x + px, y + py, 2, game->teams[t]->color);
			const int dist = std::abs(hoverX - px) * 4096 + std::abs(hoverY - py);
			if (hoverX >= 0 && hoverX < ew && hoverY >= 0 && hoverY < eh && dist < closest)
			{
				closest = dist;
				hover = value;
				markX = x + px;
				markY = y + py;
			}
			prevX = px;
			prevY = py;
		}
	}
	Uint32 earliest = end, latest = 0;
	bool missing = false, anySamples = false;
	for (int t = 0; t < game->mapHeader.getNumberOfTeams(); ++t)
	{
		if (!teamEnabled(t))
			continue;
		const auto &stats = game->teams[t]->stats;
		earliest = std::min(earliest, stats.coverageStartTick);
		latest = std::max(latest, stats.coverageStartTick);
		missing |= stats.coverageStartTick > 0;
		anySamples |= !stats.measurementHistory.empty();
	}
	if (missing || !anySamples)
	{
		std::string label = Toolkit::getStringTable()->getString(anySamples ? "[Stats since tick]" : "[Stats unavailable]");
		if (anySamples)
			label += " " + std::to_string(earliest) + (latest != earliest ? " - " + std::to_string(latest) : "");
		surface.drawString(x + 5, y + 25, font, label);
	}
	if (markX >= 0)
	{
		surface.drawVertLine(markX, y, eh, InGameTouchTheme::border);
		surface.drawCircle(markX, markY, 5, Color::white);
		surface.drawString(markX + 5, markY + 5, font, std::to_string(hover));
	}
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
