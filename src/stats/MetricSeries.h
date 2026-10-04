// SPDX-License-Identifier: GPL-3.0-or-later
// Arithmetic on sampled statistics: turning the counters and snapshots a team
// records every 512 ticks into the series a chart shows, and the round axes and
// short texts the chart labels them with. No drawing and no game state, so it is
// tested on its own (test/MetricSeriesTest.cpp).
#pragma once
#include "EngineTiming.h"
#include <SDL3/SDL_stdinc.h>
#include <string>
#include <vector>

namespace Stats
{
//! Simulation ticks per minute of game time.
inline constexpr double TICKS_PER_MINUTE = 60.0 * GAME_TICKS_PER_SECOND;

//! Whole seconds of game time elapsed at `tick`.
inline int secondsAt(Uint32 tick)
{
	return int(tick / GAME_TICKS_PER_SECOND);
}

//! Per-minute rate of a cumulative counter, averaged over the trailing `window`
//! samples. The window is shortened at the start of the series, so early values
//! are the average since the first sample rather than sagging towards zero. A
//! counter that goes down (a loaded save restarting coverage) gives zero.
std::vector<double> ratePerMinute(const std::vector<Uint32> &ticks, const std::vector<double> &cumulative, int window);

//! `values` as a percentage of `whole`, sample by sample, kept within 0-100. The
//! two are sampled up to 31 ticks apart, so a part can briefly exceed its whole.
std::vector<double> percentOf(const std::vector<double> &values, const std::vector<double> &whole);

//! `values` as a percentage of `whole` where nothing is known when the whole is
//! zero (hits per shot while no shot was fired): those samples are NaN, which a
//! chart leaves as a gap rather than drawing as 0%.
std::vector<double> ratioPercent(const std::vector<double> &values, const std::vector<double> &whole);

//! `values` divided by `count`, sample by sample: the mean of what a pair of
//! counters summed and counted (distance walked per walk). NaN, a gap, where
//! nothing was counted.
std::vector<double> meanOf(const std::vector<double> &values, const std::vector<double> &count);

//! Each value averaged with up to `window - 1` values before it. Levels that
//! jump about from sample to sample (units inside an inn) read as a trend.
std::vector<double> movingAverage(const std::vector<double> &values, int window);

//! Turns nested thresholds (units at or below 25%, 50%, 75%) and the total into
//! disjoint bands (0-25, 25-50, 50-75, above 75). `nested` holds one series per
//! threshold, lowest first; the result has one more series than `nested`.
std::vector<std::vector<double>> disjointBands(const std::vector<std::vector<double>> &nested,
											   const std::vector<double> &total);

//! Scales the bands of each sample to add up to 100: each band becomes its
//! percentage of the sum of all bands at that sample. Negative values count as
//! zero, and a sample whose bands are all zero stays zero.
void normaliseToPercent(std::vector<std::vector<double>> &bands);

struct Axis
{
	double minimum = 0, maximum = 1, step = 1;
};
//! A value axis with round gridline steps (1, 2 or 5 times a power of ten) that
//! covers `low`..`high` with at most `maxSteps` steps. Zero is always included,
//! and an empty range becomes 0..1. With `whole`, steps are never below 1, so an
//! axis of counts does not label fractions of a unit.
Axis niceAxis(double low, double high, int maxSteps, bool whole = false);

//! Spacing in seconds between time labels over `seconds` of game time: the
//! smallest of 30 s, 1, 2, 5, 10, 15 or 30 minutes that gives no more than
//! `maxLabels` labels, or else as many whole hours as it takes.
int timeStep(int seconds, int maxLabels);

//! "12:05", or "1:02:05" from an hour on.
std::string timeText(int seconds);

//! A value as short text: "950", "12.4k", "3.1M"; one decimal below 100 when
//! `decimals` is set (rates and percentages).
std::string valueText(double value, bool decimals);

//! A gridline's value on an axis that reaches `axisMaximum`: every label of the
//! axis uses the same scale ("5k", "10k", not "5000", "10.0k").
std::string tickText(double value, double axisMaximum, bool decimals);
} // namespace Stats
