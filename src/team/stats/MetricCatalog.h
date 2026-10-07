// SPDX-License-Identifier: GPL-3.0-or-later
// The statistics a player can look at, in one table: what each one measures,
// where its numbers come from in the recorded history, and how it is best
// shown. The end-of-game results and the in-game statistics panels all read this
// catalog, so a metric has one name, one explanation and one definition.
//
// Everything here reads history the simulation already records
// (GameplayMeasurements and EndOfGameStat, TeamStat.h); nothing is collected or
// saved for the catalog.
#pragma once
#include "TeamStat.h"
#include <functional>
#include <string>
#include <vector>

class BuildingsTypes;

namespace Stats
{
//! Ticks between two samples of a team's history (TeamStats::step).
inline constexpr int SAMPLE_INTERVAL_TICKS = END_OF_GAME_STAT_INTERVAL_MASK + 1;

//! Groups of the catalog, in display order.
enum class Group
{
	Population,
	Food,
	Work,
	Materials,
	Buildings,
	Military,
	Map,
	Score,
	Count
};
//! Text key of a group's name.
const char *groupKey(Group group);

//! Reads one figure out of a team's measurements at one sample.
using Extract = std::function<double(const GameplayMeasurements &)>;

//! One part of a metric that can be shown split up (deaths by cause, harvest by
//! material).
struct Band
{
	std::string labelKey;
	Extract value = nullptr;
	bool literalLabel = false; //!< Catalog-authored label, already human-readable.
	int material = -1; //!< Material slot, or -1 for other kinds of bands.
};

//! One entry of the catalog. A metric's value comes either from the sampled
//! EndOfGameStat history (`sampled`) or from the measurements (`value`); its
//! bands, when it has any, always come from the measurements.
struct Metric
{
	enum Kind
	{
		Gauge,	//!< A level at the moment of each sample (units alive, food stored).
		Counter //!< A running total since the start (units born); shown as a rate.
	};
	//! Stable name: the settings file and the text keys "[stat <id>]" and
	//! "[stat <id> about]" are derived from it.
	const char *id = "";
	Group group = Group::Population;
	Kind kind = Gauge;
	//! Text key of what is counted ("units", "wheat"), for the value axis.
	const char *unitKey = "[stat unit units]";
	//! EndOfGameStat::Type the value is read from, or -1 for a measurement.
	int sampled = -1;
	//! The value in the measurements; unused when `sampled` is set.
	Extract value = nullptr;
	//! Counter subtracted from `value` (births minus deaths); the chart then
	//! runs above and below zero.
	Extract minus = nullptr;
	//! Counter `value` is a percentage of, over the averaging window (hits per
	//! shot).
	Extract ratioOf = nullptr; //!< For a level, the level it is a percentage of.
	//! The parts the metric can be split into; empty when it has none.
	std::vector<Band> bands;
	//! The bands are the metric: it is always shown split. Bands of the
	//! population (`remainderKey`) open as percentages; other parts of a whole
	//! open in their own units, so a team's scale shows beside its mix.
	bool composition = false;
	//! Bands are nested unit counts (at or below 25%, 50%, 75%); the remainder
	//! of the population forms a last band named by `remainderKey`.
	const char *remainderKey = nullptr;
	//! Can be shown relative to the team's population: a gauge as a percentage
	//! of its units, a counter per 100 units.
	bool perUnits = false;
	//! Opens relative to the population rather than as a count.
	bool perUnitsByDefault = false;
	//! Can be shown as each team's share of all teams.
	bool shareable = false;
	//! Recorded only since extended statistics exist (save format 108).
	bool extended = false;
	//! The same for every team (growth across the whole map): one line.
	bool global = false;
	//! A counter of events too rare for a rate to read well (a building lost
	//! every few minutes): it opens as its running total.
	bool rare = false;
	//! A level that jumps from sample to sample: averaged over the view's
	//! window, like a rate.
	bool smoothed = false;
	//! Recorded only since worker time use, combat-death places and the defence
	//! snapshot exist (save format 133).
	bool labour = false;
	//! `ratioOf` counts what `value` sums: the metric is their quotient (the
	//! mean distance of a walk), not a percentage.
	bool mean = false;
	//! Factor on a counter's rate. Worker time is counted in worker-ticks, so
	//! its rate per minute times 1 / TICKS_PER_MINUTE is the number of workers
	//! doing something, on average. Such a counter has no meaningful total.
	double scale = 1;
	//! A split metric whose bands are better read as shares of their whole than
	//! in their own units: it opens as percentages.
	bool percentByDefault = false;
	//! Text key of the control that splits it into its bands ("By material").
	const char *splitKey = "[stat view split]";

	std::string titleKey() const { return std::string("[stat ") + id + "]"; }
	std::string aboutKey() const { return std::string("[stat ") + id + " about]"; }
};

//! Every metric, grouped and in display order.
const std::vector<Metric> &catalog();
//! Per-game building bands, with owned labels and concrete variant counters.
std::vector<Metric> catalogForBuildings(const BuildingsTypes& buildings);
//! Index into catalog() of the metric with this id, or -1.
int findMetric(const std::string &id);
//! The metric with this id, for ids written in the code: it must exist.
const Metric &metricById(const std::string &id);

//! Rate averaging windows offered to the player, in samples: about 1, 2 and 5
//! minutes.
inline constexpr int RATE_WINDOWS[] = {3, 6, 15};
inline constexpr int DEFAULT_RATE_WINDOW = 6;
//! Longest averaging window a view accepts, in samples.
inline constexpr int MAX_RATE_WINDOW = 60;
//! Whole minutes that `samples` samples span, at least one. Samples are 512
//! ticks apart: three to a minute, near enough.
int windowMinutes(int samples);

//! How the player has chosen to look at a metric.
struct View
{
	bool total = false;				  //!< Counters: running total rather than rate per minute.
	int window = DEFAULT_RATE_WINDOW; //!< Rates: samples averaged over.
	bool split = false;				  //!< Stacked bands, one panel per team.
	bool relative = false;			  //!< Split: percent of the whole. Otherwise per population.
	bool share = false;				  //!< Each team's share of all teams.
};
//! The view a metric opens in.
View defaultView(const Metric &metric);
//! Which choices a metric offers. Splitting rules out the share of all teams,
//! and the share rules out showing a value per population, so the last two
//! depend on the rest of the view.
bool canTotal(const Metric &metric);
bool canSplit(const Metric &metric);
bool canRelative(const Metric &metric, const View &view);
bool canShare(const Metric &metric, const View &view);
//! `view` with the choices this metric does not offer switched off, a
//! composition kept split, and the window within 1..MAX_RATE_WINDOW.
View validView(const Metric &metric, View view);

//! One moment of a team's recorded history.
struct Point
{
	Uint32 tick = 0;
	const GameplayMeasurements *measurements = nullptr; //!< Null before measurement coverage.
	const int *sampled = nullptr;						//!< EndOfGameStat::value, or null.
};
struct TeamHistory
{
	int team = 0;
	std::vector<Point> points; //!< In tick order.
	//! Tick from which measurements hold the extended statistics
	//! (Metric::extended), or 0 when they always did.
	Uint32 extendedCoverageStartTick = 0;
	//! The same for worker time use and what was added with it (Metric::labour).
	Uint32 labourCoverageStartTick = 0;
};
//! A team's sampled values and measurements joined by tick. The pointers refer
//! into `stats`, which must outlive the history and not record meanwhile.
TeamHistory historyOf(int team, const TeamStats &stats);

struct TeamSeries
{
	int team = 0;
	//! Ticks of the samples that cover the metric; values run parallel to them.
	std::vector<Uint32> ticks;
	//! One series per band; a single series when not stacked.
	std::vector<std::vector<double>> values;
};
//! What a chart draws for one metric and view.
struct Chart
{
	bool stacked = false;  //!< Bands stacked in one panel per team.
	bool percent = false;  //!< Values are percentages.
	bool decimals = false; //!< Values are averages or ratios, not whole counts.
	bool global = false;   //!< One series for the whole map rather than one per team.
	bool ordered = false;  //!< Bands run from worst to best (hunger), not unrelated kinds.
	bool any = false;	   //!< Some value is not zero: there is something to see.
	std::vector<bool> bandLabelLiteral;
	std::vector<std::string> bandKeys; //!< Text keys of the bands; empty unless stacked.
	std::vector<TeamSeries> teams;	   //!< A single entry when `global`.
	double low = 0, high = 0;		   //!< Range of the values (stacked: of the stack), zero included.
};
//! The series of `metric` for each of `teams`, under `view` once validated
//! (validView).
Chart buildChart(const Metric &metric, const View &view, const std::vector<TeamHistory> &teams);

//! A moment worth pointing out on the time axis. Derived from the 512-tick
//! samples, so it is the first sample at which the event shows (about 20 s).
struct Marker
{
	Uint32 tick = 0;
	int team = 0;
	const char *key = ""; //!< Text key of what happened.
};
//! The moments of one team, in tick order: its first combat death, its first
//! building destroyed, and each time it lost its last unit.
std::vector<Marker> markers(const TeamHistory &history);

//! The value of a metric now, for text panels: a counter's rate per minute over
//! `view.window` (or its total), a gauge's level or percentage. Always a single
//! value: a composition reads as its headline value (the hungry share, total
//! deaths) and the share of all teams does not apply. `available` is false when
//! the history does not cover the metric yet.
struct Reading
{
	double value = 0;
	bool available = false;
	bool percent = false;
	bool decimals = false;
};
Reading latestReading(const Metric &metric, const View &view, const TeamHistory &history);
} // namespace Stats
