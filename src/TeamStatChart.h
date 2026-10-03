// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "stats/MetricCatalog.h"
#include <GraphicContext.h>
#include <string>
#include <vector>

class Game;

//! Team statistics charts shared by the end-of-game results and the in-match
//! statistics sheet. What is drawn comes from the metric catalog
//! (stats/MetricCatalog.h): lines for levels and rates, one stacked panel per
//! team for metrics shown split into bands.
class TeamStatChart
{
  public:
	struct Team
	{
		int team = 0;
		GAGCore::Color color;
		std::string name;
	};
	struct Options
	{
		const Stats::Metric *metric = nullptr;
		Stats::View view;
		std::vector<Team> teams; //!< Teams drawn, in legend order.
		int highlighted = -1;	 //!< Team number drawn heavier, the others dimmed.
		int hoverX = -1, hoverY = -1; //!< Chart-local point whose values are read out.
	};
	//! Paints the chart for `options.metric` into the rectangle, on the dark plot
	//! background its colours are chosen for (`background`, which the caller fills).
	//! With too little recorded history for the metric it says so instead.
	static void paint(const Game &game, GAGCore::DrawableSurface &surface, int left, int top, int width, int height,
					  const Options &options);
	//! Translated name of a metric, and the longer explanation of what it measures.
	static std::string title(const Stats::Metric &metric);
	static std::string about(const Stats::Metric &metric);
	//! What the value axis shows under this view: "units per minute, 2 min average".
	static std::string axisTitle(const Stats::Metric &metric, const Stats::View &view);
	//! A reading as text, with its percent sign.
	static std::string readingText(const Stats::Reading &reading);
	//! The metrics of one group as lines of text, name left and current value
	//! right: the in-game statistics panel. Counters read as their rate per minute.
	static void paintReadings(GAGCore::DrawableSurface &surface, int left, int top, int width, const TeamStats &stats,
							  Stats::Group group);
	//! Number of groups the in-game panel pages through: every group before the
	//! score, whose one metric (prestige) is on the top bar.
	static constexpr int readingGroups = int(Stats::Group::Score);
	//! Colour of band `index` of a split chart.
	static GAGCore::Color bandColor(const Stats::Chart &chart, std::size_t index);
	//! The plot background the line and band colours are chosen for.
	static const GAGCore::Color background;
};
