// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <functional>
#include <string>

class Game;
namespace GAGCore
{
class DrawableSurface;
}

//! Team statistics charts shared by the end-of-game results and the in-match
//! statistics sheet. Metrics 0-5 are the sampled EndOfGameStat curves; 6 and
//! above are gameplay measurements (TeamStats::graphValue).
class TeamStatChart
{
  public:
	struct Options
	{
		int metric = 0;
		std::function<bool(int)> shown; // Teams drawn.
		int hoverX = -1, hoverY = -1;	 // Chart-local point whose value is marked.
	};
	static void paintCurves(const Game &game, GAGCore::DrawableSurface &surface, int left, int top, int width,
							int height, const Options &options);
	static void paintMeasurements(const Game &game, GAGCore::DrawableSurface &surface, int left, int top, int width,
								  int height, const Options &options);
	//! Translated short name of a metric.
	static std::string metricName(int type);
	static std::string getTimeText(int seconds);

  private:
	static double getValue(const Game &game, double position, int team, int type);
	static std::string getRightScaleText(int value, int digits);
};
