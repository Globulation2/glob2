// SPDX-License-Identifier: GPL-3.0-or-later
// Team statistics charts, shared by the end-of-game results and the in-match
// statistics sheet. Moved from EndGameScreen without changing what is drawn.
#include "TeamStatChart.h"
#include "Game.h"
#include "GlobalContainer.h"
#include "Team.h"
#include "gui/InGameTouchTheme.h"
#include <StringTable.h>
#include <Toolkit.h>
#include <algorithm>
#include <cassert>
#include <cmath>
#include <iomanip>
#include <limits>
#include <sstream>
using namespace GAGCore;

std::string TeamStatChart::metricName(int type)
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

double TeamStatChart::getValue(const Game &game, double position, int team, int type)
{
	int s = game.teams[team]->stats.endOfGameStats.size() - 1;
	int lower = int(position * float(s));
	int upper = lower + 1;
	double mu = (position * float(s)) - lower;

	int y1 = game.teams[team]->stats.endOfGameStats[lower].value[type];
	int y2 = game.teams[team]->stats.endOfGameStats[upper].value[type];

	//Linear interpolation
	return (1 - mu) * y1 + mu * y2;
}

std::string TeamStatChart::getTimeText(int seconds)
{
	int min = int(seconds) / 60;
	int sec = int(seconds) % 60;
	std::stringstream str;
	str << min << ":" << std::setw(2) << std::setfill('0') << sec;
	return str.str();
}

std::string TeamStatChart::getRightScaleText(int value, int digits)
{
	std::stringstream str;
	str << std::setw(digits) << std::setfill('0') << value;
	return str.str();
}

void TeamStatChart::paintCurves(const Game &game, GAGCore::DrawableSurface &surface, int left, int top, int width, int height,
								const Options &options)
{
	const int type = options.metric;
	const int x = left, y = top, w = width, h = height;
	if (game.teams[0]->stats.endOfGameStats.size() < 2)
	{
		surface.drawString(x + 8, y + 16, globalContainer->standardFont, GAGCore::Toolkit::getStringTable()->getString("[Not enough recorded history yet.]"));
		return;
	}
	// find maximum
	int team, maxValue = 0;
	unsigned int pos = 0;
	for (team = 0; team < game.mapHeader.getNumberOfTeams(); team++)
		if (options.shown(team))
			for (pos = 0; pos < game.teams[team]->stats.endOfGameStats.size(); pos++)
				maxValue = std::max(maxValue, game.teams[team]->stats.endOfGameStats[pos].value[type]);

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
	int time_period = (game.teams[0]->stats.endOfGameStats.size() * 512) / 25;
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
		// The top value sits on the chart's top edge: keep it inside, not half cut off.
		surface.drawString(x + e_width + 8, std::max(y, y + pos - height / 2), globalContainer->littleFont, valueText.c_str());
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
		// Teams with the same values would draw one line over the other: each line is
		// two pixels wide and carries markers at staggered places, so every team's
		// colour shows even where the curves coincide.
		int shownCount = 0;
		for (int t = 0; t < game.mapHeader.getNumberOfTeams(); t++)
			shownCount += options.shown(t) ? 1 : 0;
		const int markerSpacing = 36;
		int shownIndex = 0;
		for (team = 0; team < game.mapHeader.getNumberOfTeams(); team++)
		{
			if (!options.shown(team))
				continue;
			const Color &color = game.teams[team]->color;
			const int markerPhase = 6 + (shownIndex * markerSpacing) / std::max(1, shownCount);
			++shownIndex;

			int previous_y = e_height - int(double(e_height) * getValue(game, 0, team, type) / double(maxValue));

			for (int px = 0; px < (e_width - 2); ++px)
			{
				double value = getValue(game, double(px) / double(e_width - 2), team, type);
				int ny = e_height - int(double(e_height) * value / double(maxValue));
				surface.drawLine(x + px, y + previous_y, x + px + 1, y + ny, color);
				surface.drawLine(x + px, y + previous_y - 1, x + px + 1, y + ny - 1, color);
				if (px % markerSpacing == markerPhase % markerSpacing)
				{
					surface.drawFilledRect(x + px - 3, y + ny - 4, 7, 7, color);
					surface.drawRect(x + px - 3, y + ny - 4, 7, 7, InGameTouchTheme::ink);
				}
				previous_y = ny;
				const int dist = std::abs(options.hoverX - px - 1) * 4096 + std::abs(options.hoverY - ny);
				if (options.hoverX >= 0 && options.hoverX < e_width && options.hoverY >= 0 && options.hoverY < e_height && dist < closest_position)
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

void TeamStatChart::paintMeasurements(const Game &game, GAGCore::DrawableSurface &surface, int left, int top, int width, int height,
									  const Options &options)
{
	const int type = options.metric;
	const int x = left, y = top, w = width, h = height;
	auto *font = globalContainer->littleFont;
	Uint64 maximum = 1;
	for (int t = 0; t < game.mapHeader.getNumberOfTeams(); ++t)
		if (options.shown(t))
			for (const auto &m : game.teams[t]->stats.measurementHistory)
				if (type - 6 < 16 || m.tick > game.teams[t]->stats.extendedCoverageStartTick ||
					(game.teams[t]->stats.extendedCoverageStartTick == 0 && m.tick == 0))
					maximum = std::max(maximum, TeamStats::graphValue(m, type - 6));
	const std::string scale = std::to_string(maximum);
	const int ew = std::max(1, w - font->getStringWidth(scale) - 12), eh = std::max(1, h - 24);
	surface.drawRect(x, y, ew, eh, InGameTouchTheme::border);
	surface.drawString(x + ew + 4, y, font, scale);
	surface.drawString(x + ew + 4, y + eh - 12, font, "0");
	const Uint32 end = std::max(Uint32(1), game.stepCounter);
	for (int i = 0; i <= 4; ++i)
		surface.drawString(x + ew * i / 4, y + eh + 6, font, getTimeText(static_cast<Uint64>(end) * i / 100));
	int closest = std::numeric_limits<int>::max();
	Uint64 hover = 0;
	int markX = -1, markY = -1;
	for (int t = 0; t < game.mapHeader.getNumberOfTeams(); ++t)
	{
		if (!options.shown(t))
			continue;
		const auto &stats = game.teams[t]->stats;
		int prevX = -1, prevY = -1;
		for (const auto &m : stats.measurementHistory)
		{
			if (type - 6 >= 16 && m.tick <= stats.extendedCoverageStartTick && !(stats.extendedCoverageStartTick == 0 && m.tick == 0))
				continue;
			const Uint64 value = TeamStats::graphValue(m, type - 6);
			const int px = static_cast<Uint64>(m.tick) * ew / end;
			const int py = eh - static_cast<long double>(value) * eh / maximum;
			if (prevX >= 0)
				surface.drawLine(x + prevX, y + prevY, x + px, y + py, game.teams[t]->color);
			else
				surface.drawCircle(x + px, y + py, 2, game.teams[t]->color);
			const int dist = std::abs(options.hoverX - px) * 4096 + std::abs(options.hoverY - py);
			if (options.hoverX >= 0 && options.hoverX < ew && options.hoverY >= 0 && options.hoverY < eh && dist < closest)
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
	for (int t = 0; t < game.mapHeader.getNumberOfTeams(); ++t)
	{
		if (!options.shown(t))
			continue;
		const auto &stats = game.teams[t]->stats;
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

