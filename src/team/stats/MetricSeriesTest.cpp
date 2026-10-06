// SPDX-License-Identifier: GPL-3.0-or-later
//
// Unit tests for the statistics shown after a match: the series arithmetic
// (rates, percentages, bands, axes) and the catalog that turns a team's recorded
// history into what a chart draws.

#include "Glob2Test.h"

#include "stats/MetricCatalog.h"
#include "stats/MetricSeries.h"

#include <array>
#include <cmath>
#include <deque>
#include <set>

namespace
{
using Stats::Metric;

//! A team's recorded history, built sample by sample.
struct Recorded
{
	std::deque<GameplayMeasurements> measurements;
	std::deque<std::array<int, EndOfGameStat::TYPE_NB_STATS>> sampled;
	Stats::TeamHistory history;

	GameplayMeasurements &add(Uint32 tick, int units)
	{
		measurements.emplace_back();
		measurements.back().tick = tick;
		sampled.push_back({units, 0, 0, 0, 0, 0});
		history.points.push_back({tick, &measurements.back(), sampled.back().data()});
		return measurements.back();
	}
};

//! A series' value at the `index`-th sample interval of the match. Charts leave
//! out the sample at tick 0, so positions in a series are not sample numbers.
double sampleOf(const Stats::TeamSeries &series, std::size_t band, int index)
{
	const Uint32 tick = Uint32(index) * 512;
	for (std::size_t i = 0; i < series.ticks.size(); ++i)
		if (series.ticks[i] == tick)
			return series.values[band][i];
	FAIL("no sample at tick " << tick);
	return 0;
}

const Metric &metric(const char *id)
{
	const int index = Stats::findMetric(id);
	REQUIRE(index >= 0);
	return Stats::catalog()[std::size_t(index)];
}

const int INTERVAL = Stats::SAMPLE_INTERVAL_TICKS;
} // namespace

TEST_SUITE("MetricSeries")
{
	TEST_CASE("A counter's rate per minute follows its slope and shortens its window at the start")
	{
		// Samples every 512 ticks; the counter gains 10 per sample, then stalls.
		const std::vector<Uint32> ticks{0, 512, 1024, 1536, 2048, 2560};
		const std::vector<double> counter{0, 10, 20, 30, 30, 30};
		const double perSample = 10 * Stats::TICKS_PER_MINUTE / 512;

		const auto sharp = Stats::ratePerMinute(ticks, counter, 1);
		CHECK(sharp[1] == doctest::Approx(perSample));
		CHECK(sharp[3] == doctest::Approx(perSample));
		CHECK(sharp[4] == doctest::Approx(0));
		// The first sample shows the rate that follows it, not zero.
		CHECK(sharp[0] == doctest::Approx(perSample));

		const auto smooth = Stats::ratePerMinute(ticks, counter, 3);
		// A window longer than the history so far averages since the first sample.
		CHECK(smooth[2] == doctest::Approx(perSample));
		CHECK(smooth[4] == doctest::Approx(perSample * 2 / 3));
		CHECK(smooth[5] == doctest::Approx(perSample / 3));

		// A constant counter has no rate, and one that falls is not negative.
		CHECK(Stats::ratePerMinute(ticks, {5, 5, 5, 5, 5, 5}, 2)[5] == doctest::Approx(0));
		CHECK(Stats::ratePerMinute(ticks, {9, 9, 3, 3, 3, 3}, 1)[2] == doctest::Approx(0));
		CHECK(Stats::ratePerMinute({}, {}, 3).empty());
		CHECK(Stats::ratePerMinute({7}, {4}, 3) == std::vector<double>{0});
	}

	TEST_CASE("Nested thresholds become disjoint bands that add up to the whole")
	{
		// 3 at or below 25%, 5 at or below 50%, 9 at or below 75%, of 12 units; then
		// a stale total (8) smaller than a threshold (9), and no units at all.
		const std::vector<std::vector<double>> nested{{3, 3, 0}, {5, 5, 0}, {9, 9, 0}};
		auto bands = Stats::disjointBands(nested, {12, 8, 0});
		REQUIRE(bands.size() == 4);
		CHECK(bands[0][0] == 3);
		CHECK(bands[1][0] == 2);
		CHECK(bands[2][0] == 4);
		CHECK(bands[3][0] == 3);
		CHECK(bands[0][1] + bands[1][1] + bands[2][1] + bands[3][1] == 8);
		CHECK(bands[3][1] == 0);

		Stats::normaliseToPercent(bands);
		CHECK(bands[0][0] == doctest::Approx(25));
		CHECK(bands[0][0] + bands[1][0] + bands[2][0] + bands[3][0] == doctest::Approx(100));
		CHECK(bands[0][1] + bands[1][1] + bands[2][1] + bands[3][1] == doctest::Approx(100));
		CHECK(bands[0][2] + bands[1][2] + bands[2][2] + bands[3][2] == doctest::Approx(0));
	}

	TEST_CASE("Percentages stay within 0-100 when the part outruns its whole")
	{
		const auto percent = Stats::percentOf({5, 12, 3}, {10, 10, 0});
		CHECK(percent[0] == doctest::Approx(50));
		CHECK(percent[1] == doctest::Approx(100));
		CHECK(percent[2] == doctest::Approx(0));
		// Samples the whole does not reach read as zero rather than being dropped.
		CHECK(Stats::percentOf({5, 5}, {10}) == std::vector<double>{50, 0});
	}

	TEST_CASE("Bands of unequal length and negative values normalise without reading past the end")
	{
		std::vector<std::vector<double>> bands{{30, 10, -5}, {10, 30}};
		Stats::normaliseToPercent(bands);
		CHECK(bands[0][0] == doctest::Approx(75));
		CHECK(bands[1][1] == doctest::Approx(75));
		// A negative value counts as nothing; alone it leaves the sample empty.
		CHECK(bands[0][2] == doctest::Approx(0));
		REQUIRE(bands[1].size() == 2);
		std::vector<std::vector<double>> none;
		Stats::normaliseToPercent(none);
		CHECK(none.empty());
	}

	TEST_CASE("Axes use round steps and include zero")
	{
		auto axis = Stats::niceAxis(0, 52, 6);
		CHECK(axis.step == doctest::Approx(10));
		CHECK(axis.minimum == doctest::Approx(0));
		CHECK(axis.maximum == doctest::Approx(60));
		axis = Stats::niceAxis(0, 100, 5);
		CHECK(axis.step == doctest::Approx(20));
		CHECK(axis.maximum == doctest::Approx(100));
		axis = Stats::niceAxis(-7, 13, 5);
		CHECK(axis.step == doctest::Approx(5));
		CHECK(axis.minimum == doctest::Approx(-10));
		CHECK(axis.maximum == doctest::Approx(15));
		axis = Stats::niceAxis(0, 0.8, 4);
		CHECK(axis.step == doctest::Approx(0.2));
		axis = Stats::niceAxis(0, 0, 4);
		CHECK(axis.maximum > axis.minimum);

		CHECK(Stats::timeStep(8 * 60, 8) == 60);
		CHECK(Stats::timeStep(55 * 60, 8) == 600);
		CHECK(Stats::timeStep(20, 8) == 30);
		CHECK(Stats::timeText(125) == "2:05");
		CHECK(Stats::timeText(3725) == "1:02:05");
		CHECK(Stats::valueText(950, false) == "950");
		CHECK(Stats::valueText(12400, false) == "12.4k");
		CHECK(Stats::valueText(3100000, false) == "3.1M");
		CHECK(Stats::valueText(2.46, true) == "2.5");
		CHECK(Stats::valueText(3, true) == "3");
	}

	TEST_CASE("Texts switch form at their boundaries")
	{
		// Rounding never yields "10000" or "1000.0k": the suffix changes first.
		CHECK(Stats::valueText(9994, false) == "9994");
		CHECK(Stats::valueText(9996, false) == "10.0k");
		CHECK(Stats::valueText(999960, false) == "1.0M");
		// Decimals only where they say something: small values that are not whole.
		CHECK(Stats::valueText(2.46, false) == "2");
		CHECK(Stats::valueText(99.97, true) == "100");
		CHECK(Stats::valueText(150.4, true) == "150");
		CHECK(Stats::valueText(-2.46, true) == "-2.5");
		CHECK(Stats::valueText(-12400, false) == "-12.4k");

		CHECK(Stats::timeText(0) == "0:00");
		CHECK(Stats::timeText(-5) == "0:00");
		CHECK(Stats::timeText(3599) == "59:59");
		CHECK(Stats::timeText(3600) == "1:00:00");
		// Matches too long for half-hour labels fall back to whole hours.
		CHECK(Stats::timeStep(4 * 3600, 4) == 3600);
		CHECK(Stats::timeStep(5 * 3600, 4) == 2 * 3600);
		CHECK(Stats::timeStep(9 * 3600, 4) == 3 * 3600);
		CHECK(Stats::timeStep(600, 0) == 600);
		CHECK(Stats::secondsAt(0) == 0);
		CHECK(Stats::secondsAt(1499) == 59);
		CHECK(Stats::secondsAt(Uint32(Stats::TICKS_PER_MINUTE)) == 60);
	}
}

TEST_SUITE("MetricSeries views")
{
	TEST_CASE("Averaged levels, ratios with nothing to measure, and whole-number axes")
	{
		const auto average = Stats::movingAverage({0, 6, 0, 6, 12}, 2);
		CHECK(average[0] == doctest::Approx(0));
		CHECK(average[1] == doctest::Approx(3));
		CHECK(average[4] == doctest::Approx(9));
		CHECK(Stats::movingAverage({4, 8}, 10)[1] == doctest::Approx(6));

		// No shots fired is not 0% of shots hitting: it is a gap.
		const auto ratio = Stats::ratioPercent({0, 3}, {0, 4});
		CHECK(std::isnan(ratio[0]));
		CHECK(ratio[1] == doctest::Approx(75));

		// A count of one or two buildings is not labelled in fifths of a building.
		CHECK(Stats::niceAxis(0, 1, 5).step == doctest::Approx(0.2));
		CHECK(Stats::niceAxis(0, 1, 5, true).step == doctest::Approx(1));
		CHECK(Stats::niceAxis(0, 40, 4, true).step == doctest::Approx(10));

		// Every label of an axis is on the scale of its largest.
		CHECK(Stats::tickText(5000, 35000, false) == "5k");
		CHECK(Stats::tickText(35000, 35000, false) == "35k");
		CHECK(Stats::tickText(2500, 10000, false) == "2.5k");
		CHECK(Stats::tickText(5000, 9000, false) == "5000");
		CHECK(Stats::tickText(1500000, 3000000, false) == "1.5M");
	}

	TEST_CASE("Metrics open in the view that reads best for them")
	{
		// Rare events open as running totals, with the rate one toggle away.
		for (const char *id : {"construction", "buildings lost", "starvation", "conversions"})
		{
			CAPTURE(id);
			CHECK(Stats::defaultView(metric(id)).total);
			CHECK(Stats::canTotal(metric(id)));
		}
		CHECK_FALSE(Stats::defaultView(metric("births")).total);
		// Bands of the population open as percentages of it; other parts of a whole
		// open in their own units, so a colony's scale shows beside its mix.
		CHECK(Stats::defaultView(metric("hunger")).relative);
		CHECK_FALSE(Stats::defaultView(metric("death causes")).relative);
		CHECK_FALSE(Stats::defaultView(metric("spending")).relative);
	}

	TEST_CASE("A chart knows when it has nothing to show, and leaves gaps out of its range")
	{
		Recorded team;
		for (int i = 0; i < 4; ++i)
			team.add(Uint32(i) * 512, 10);
		const Metric &traded = metric("traded");
		CHECK_FALSE(Stats::buildChart(traded, Stats::defaultView(traded), {team.history}).any);
		const Metric &population = metric("population");
		CHECK(Stats::buildChart(population, Stats::defaultView(population), {team.history}).any);
		// No shots at all: every sample is a gap, the range stays empty and a text
		// panel has nothing to read.
		const Metric &accuracy = metric("accuracy");
		const auto chart = Stats::buildChart(accuracy, Stats::defaultView(accuracy), {team.history});
		CHECK_FALSE(chart.any);
		CHECK(chart.high == doctest::Approx(0));
		CHECK_FALSE(Stats::latestReading(accuracy, Stats::defaultView(accuracy), team.history).available);
	}
}

TEST_SUITE("MetricCatalog worker time and defence")
{
	using M = GameplayMeasurements;

	TEST_CASE("Worker time reads as workers on average, and as shares of their time")
	{
		// Ten workers every tick: six filling a swarm (harvesting), three idle, one eating.
		Recorded team;
		for (int i = 0; i < 4; ++i)
		{
			auto &m = team.add(Uint32(i) * 512, 10);
			m.filling[M::SWARM_JOB][M::HARVESTING] = Uint64(i) * 512 * 6;
			m.labour[M::IDLE] = Uint64(i) * 512 * 3;
			m.labour[M::EAT_INSIDE] = Uint64(i) * 512 * 1;
		}
		const Metric &time = metric("worker time");
		// It opens as shares of the workers' time...
		auto chart = Stats::buildChart(time, Stats::defaultView(time), {team.history});
		CHECK(chart.stacked);
		CHECK(chart.percent);
		// Bands, bottom to top: working, at a flag, training, eating, healing,
		// waiting with no inn or hospital free, other, idle.
		REQUIRE(chart.bandKeys.size() == 8);
		CHECK(sampleOf(chart.teams[0], 0, 3) == doctest::Approx(60)); // working
		CHECK(sampleOf(chart.teams[0], 3, 3) == doctest::Approx(10)); // eating
		CHECK(sampleOf(chart.teams[0], 7, 3) == doctest::Approx(30)); // idle
		// ...and otherwise as the number of workers doing each thing.
		Stats::View workers = Stats::defaultView(time);
		workers.relative = false;
		chart = Stats::buildChart(time, workers, {team.history});
		CHECK_FALSE(chart.percent);
		CHECK(sampleOf(chart.teams[0], 0, 3) == doctest::Approx(6));
		CHECK(sampleOf(chart.teams[0], 7, 3) == doctest::Approx(3));
		// A text panel reads the share of time spent working.
		const auto reading = Stats::latestReading(time, Stats::defaultView(time), team.history);
		CHECK(reading.percent);
		CHECK(reading.value == doctest::Approx(60));

		const Metric &idle = metric("idle workers");
		chart = Stats::buildChart(idle, Stats::defaultView(idle), {team.history});
		CHECK(chart.percent);
		CHECK(sampleOf(chart.teams[0], 0, 2) == doctest::Approx(30));
	}

	TEST_CASE("Distances are means of what was summed and counted, with gaps where nothing was")
	{
		Recorded team;
		for (int i = 0; i < 4; ++i)
		{
			auto &m = team.add(Uint32(i) * 512, 10);
			// Nothing harvested until the second interval, then 12 squares a sample.
			m.harvestSamples[M::INN_JOB] = i < 2 ? 0 : Uint64(i - 1) * 100;
			m.harvestDistance[M::INN_JOB] = i < 2 ? 0 : Uint64(i - 1) * 1200;
		}
		const Metric &distance = metric("haul distance");
		Stats::View view = Stats::defaultView(distance);
		view.window = 1;
		const auto chart = Stats::buildChart(distance, view, {team.history});
		CHECK_FALSE(chart.percent);
		CHECK(chart.decimals);
		CHECK(std::isnan(sampleOf(chart.teams[0], 0, 1)));
		CHECK(sampleOf(chart.teams[0], 0, 3) == doctest::Approx(12));
		CHECK(chart.high == doctest::Approx(12));
	}

	TEST_CASE("The defence snapshot charts warriors by place, their state and the balance at home")
	{
		Recorded team;
		for (int i = 0; i < 3; ++i)
		{
			auto &m = team.add(Uint32(i) * 512, 30);
			m.warriors[M::HOME] = 6;
			m.warriors[M::AWAY] = 4;
			m.warriorLevels[M::HOME] = 12;
			m.warriorLevels[M::AWAY] = 18;
			m.warriorsHurt = 2;
			m.intruders = 9;
		}
		const Metric &warriors = metric("warriors");
		CHECK(sampleOf(Stats::buildChart(warriors, Stats::defaultView(warriors), {team.history}).teams[0], 0, 2) == doctest::Approx(10));
		// Two skills counted from 0, 30 levels over 10 warriors: 1.5 each above the
		// lowest level, so 2.5 on the game's 1 to 4 scale.
		const Metric &level = metric("warrior level");
		CHECK(sampleOf(Stats::buildChart(level, Stats::defaultView(level), {team.history}).teams[0], 0, 2) == doctest::Approx(2.5));
		// On a flag, seeking healing, free: shares of the warriors.
		const Metric &duties = metric("warrior duties");
		const auto chart = Stats::buildChart(duties, Stats::defaultView(duties), {team.history});
		CHECK(chart.stacked);
		CHECK(chart.percent);
		CHECK(sampleOf(chart.teams[0], 0, 2) == doctest::Approx(0));
		CHECK(sampleOf(chart.teams[0], 1, 2) == doctest::Approx(20));
		CHECK(sampleOf(chart.teams[0], 2, 2) == doctest::Approx(80));
		// Six warriors with twelve levels at home against nine untrained intruders.
		const Metric &margin = metric("home defence");
		const auto ahead = Stats::buildChart(margin, Stats::defaultView(margin), {team.history});
		CHECK(sampleOf(ahead.teams[0], 0, 2) == doctest::Approx(9));
	}

	TEST_CASE("Hungry workers with no inn free are waiting, not eating")
	{
		Recorded team;
		for (int i = 0; i < 3; ++i)
		{
			auto &m = team.add(Uint32(i) * 512, 10);
			m.labour[M::EAT_INSIDE] = Uint64(i) * 512 * 2;
			m.labour[M::EAT_NO_INN] = Uint64(i) * 512 * 6;
			m.labour[M::IDLE] = Uint64(i) * 512 * 2;
		}
		const Metric &time = metric("worker time");
		auto chart = Stats::buildChart(time, Stats::defaultView(time), {team.history});
		CHECK(sampleOf(chart.teams[0], 3, 2) == doctest::Approx(20)); // eating
		CHECK(sampleOf(chart.teams[0], 5, 2) == doctest::Approx(60)); // no inn free
		// The eating breakdown counts workers: none walking, two inside, six waiting.
		const Metric &eating = metric("eating time");
		chart = Stats::buildChart(eating, Stats::defaultView(eating), {team.history});
		CHECK_FALSE(chart.percent);
		CHECK(sampleOf(chart.teams[0], 1, 2) == doctest::Approx(2));
		CHECK(sampleOf(chart.teams[0], 2, 2) == doctest::Approx(6));
	}

	TEST_CASE("Samples from before a save gained worker time are left out")
	{
		Recorded team;
		for (int i = 0; i < 6; ++i)
			team.add(Uint32(i) * 512, 10).labour[M::IDLE] = Uint64(i) * 512;
		team.history.labourCoverageStartTick = 1536;
		const Metric &idle = metric("idle workers");
		CHECK(Stats::buildChart(idle, Stats::defaultView(idle), {team.history}).teams[0].ticks.front() == 2048);
		// Older measurements are unaffected.
		const Metric &births = metric("births");
		CHECK(Stats::buildChart(births, Stats::defaultView(births), {team.history}).teams[0].ticks.front() == 512);
	}
}

TEST_SUITE("MetricCatalog")
{
	TEST_CASE("Every metric has a unique id, a group and a source for its value")
	{
		std::set<std::string> ids;
		for (const auto &m : Stats::catalog())
		{
			CAPTURE(m.id);
			CHECK(ids.insert(m.id).second);
			CHECK(int(m.group) < int(Stats::Group::Count));
			CHECK((m.sampled >= 0 || m.value));
			for (const auto &band : m.bands)
				CHECK((!band.labelKey.empty() && band.value));
			// A composition opens split, as percentages, and stays split.
			const auto view = Stats::defaultView(m);
			CHECK(view.split == m.composition);
			Stats::View flat = view;
			flat.split = false;
			CHECK(Stats::validView(m, flat).split == m.composition);
			// The default view is one the metric offers.
			const auto valid = Stats::validView(m, view);
			CHECK(valid.split == view.split);
			CHECK(valid.relative == view.relative);
			// What a metric is relative to, or split into, has to be recorded for it.
			if (m.remainderKey)
				CHECK(m.bands.size() >= 1);
			if (m.composition)
				CHECK(m.bands.size() >= 2);
			if (m.perUnitsByDefault)
				CHECK(m.perUnits);
			// A ratio or a difference is of measurements: of two counters (hits per
			// shot) or of two levels (hurt warriors among warriors).
			if (m.ratioOf || m.minus)
				CHECK((m.sampled < 0 && m.value));
			if (m.mean)
				CHECK(m.ratioOf);
			// Worker time has no total a player could read.
			if (m.scale != 1)
				CHECK_FALSE(Stats::canTotal(m));
			CHECK(&Stats::metricById(m.id) == &m);
		}
		CHECK(Stats::findMetric("no such metric") == -1);
	}

	TEST_CASE("The averaging windows offered are whole minutes and include the default")
	{
		std::set<int> minutes;
		bool hasDefault = false;
		for (int window : Stats::RATE_WINDOWS)
		{
			CHECK(window >= 1);
			CHECK(window <= Stats::MAX_RATE_WINDOW);
			CHECK(minutes.insert(Stats::windowMinutes(window)).second);
			hasDefault |= window == Stats::DEFAULT_RATE_WINDOW;
		}
		CHECK(hasDefault);
		CHECK(Stats::View().window == Stats::DEFAULT_RATE_WINDOW);
		CHECK(Stats::windowMinutes(Stats::DEFAULT_RATE_WINDOW) == 2);
		// The shortest window is still called a minute.
		CHECK(Stats::windowMinutes(1) == 1);
	}

	TEST_CASE("A view keeps only the choices its metric offers, and those that go together")
	{
		Stats::View all;
		all.total = all.split = all.relative = all.share = true;
		all.window = 1000;

		auto buildings=metric("buildings");
		buildings.bands.push_back({"fixture variant",[](const GameplayMeasurements&) { return 0.; },true});

		// A level that can be split and shared: splitting wins over the share, and a
		// split chart can always be shown as percentages.
		auto view = Stats::validView(buildings, all);
		CHECK(view.window == Stats::MAX_RATE_WINDOW);
		CHECK_FALSE(view.total);
		CHECK(view.split);
		CHECK_FALSE(view.share);
		CHECK(view.relative);

		// Unsplit, the share stays; "buildings" has no per-population form.
		all.split = false;
		view = Stats::validView(buildings, all);
		CHECK(view.share);
		CHECK_FALSE(view.relative);
		CHECK(Stats::buildChart(buildings, all, {}).percent);

		// A counter that is per population but not shareable keeps total and relative.
		view = Stats::validView(metric("deaths"), all);
		CHECK(view.total);
		CHECK(view.relative);
		CHECK_FALSE(view.share);

		// A ratio has no running total, and nothing else to choose.
		view = Stats::validView(metric("accuracy"), all);
		CHECK_FALSE(view.total);
		CHECK_FALSE(view.split);
		CHECK_FALSE(view.relative);
		CHECK_FALSE(view.share);

		// A composition stays split whatever is asked, but may show absolute values.
		Stats::View none;
		none.window = -3;
		view = Stats::validView(metric("death causes"), none);
		CHECK(view.split);
		CHECK_FALSE(view.relative);
		CHECK(view.window == 1);
		CHECK_FALSE(Stats::canSplit(metric("death causes")));
		CHECK(Stats::canRelative(metric("death causes"), view));
	}

	TEST_CASE("Counters chart as smoothed rates and as totals")
	{
		Recorded team;
		for (int i = 0; i < 5; ++i)
			team.add(Uint32(i) * 512, 20).births[WORKER] = Uint64(i) * 4;
		const Metric &births = metric("births");
		Stats::View view = Stats::defaultView(births);
		view.window = 1;
		auto chart = Stats::buildChart(births, view, {team.history});
		REQUIRE(chart.teams.size() == 1);
		CHECK_FALSE(chart.stacked);
		CHECK(chart.decimals);
		CHECK(sampleOf(chart.teams[0], 0, 3) == doctest::Approx(4 * Stats::TICKS_PER_MINUTE / 512));

		view.total = true;
		chart = Stats::buildChart(births, view, {team.history});
		CHECK(sampleOf(chart.teams[0], 0, 4) == doctest::Approx(16));
		CHECK(chart.high == doctest::Approx(16));

		// Split by unit type: one band per type, here all workers.
		view.split = true;
		chart = Stats::buildChart(births, view, {team.history});
		CHECK(chart.stacked);
		REQUIRE(chart.bandKeys.size() == NB_UNIT_TYPE);
		CHECK(sampleOf(chart.teams[0], WORKER, 4) == doctest::Approx(16));
		CHECK(sampleOf(chart.teams[0], WARRIOR, 4) == doctest::Approx(0));
	}

	TEST_CASE("Net metrics run below zero")
	{
		Recorded team;
		for (int i = 0; i < 4; ++i)
		{
			auto &m = team.add(Uint32(i) * 512, 20);
			m.births[WORKER] = Uint64(i);
			m.deaths[WORKER][GameplayMeasurements::COMBAT] = Uint64(i) * 3;
		}
		const Metric &net = metric("net growth");
		const auto chart = Stats::buildChart(net, Stats::defaultView(net), {team.history});
		CHECK(chart.low < 0);
		CHECK(sampleOf(chart.teams[0], 0, 3) == doctest::Approx(-2 * Stats::TICKS_PER_MINUTE / 512));
	}

	TEST_CASE("Hunger is drawn as bands of the population that add up to 100%")
	{
		Recorded team;
		for (int i = 0; i < 3; ++i)
		{
			auto &m = team.add(Uint32(i) * 512, 40);
			m.lowFood[0][WORKER] = 4;
			m.lowFood[1][WORKER] = 10;
			m.lowFood[2][WORKER] = 20;
		}
		const Metric &hunger = metric("hunger");
		const auto chart = Stats::buildChart(hunger, Stats::defaultView(hunger), {team.history});
		CHECK(chart.stacked);
		CHECK(chart.percent);
		REQUIRE(chart.bandKeys.size() == 4);
		const auto &bands = chart.teams[0].values;
		CHECK(bands[0][1] == doctest::Approx(10));
		CHECK(bands[1][1] == doctest::Approx(15));
		CHECK(bands[2][1] == doctest::Approx(25));
		CHECK(bands[3][1] == doctest::Approx(50));
		CHECK(chart.high == doctest::Approx(100));

		// In a text panel it reads as the hungry share of the population.
		const auto reading = Stats::latestReading(hunger, Stats::defaultView(hunger), team.history);
		CHECK(reading.available);
		CHECK(reading.percent);
		CHECK(reading.value == doctest::Approx(25));
	}

	TEST_CASE("Samples from before a save gained coverage are left out")
	{
		Recorded team;
		for (int i = 0; i < 6; ++i)
			team.add(Uint32(i) * 512, 10).lowHP[0][WORKER] = 1;
		// The first two samples have no measurements at all (an older save)...
		team.history.points[0].measurements = team.history.points[1].measurements = nullptr;
		// ...and extended statistics start later still.
		team.history.extendedCoverageStartTick = 1536;

		const Metric &births = metric("births");
		CHECK(Stats::buildChart(births, Stats::defaultView(births), {team.history}).teams[0].ticks.front() == 1024);
		const Metric &health = metric("health");
		CHECK(Stats::buildChart(health, Stats::defaultView(health), {team.history}).teams[0].ticks.front() == 2048);
		// Sampled values cover the whole match regardless, from the first sample
		// after the start: at tick 0 nothing has been counted yet.
		const Metric &population = metric("population");
		CHECK(Stats::buildChart(population, Stats::defaultView(population), {team.history}).teams[0].ticks.front() == 512);

		Stats::TeamHistory empty;
		CHECK_FALSE(Stats::latestReading(births, Stats::defaultView(births), empty).available);
	}

	TEST_CASE("Shares compare teams sample by sample")
	{
		Recorded a, b;
		a.history.team = 0;
		b.history.team = 1;
		for (int i = 0; i < 3; ++i)
		{
			a.add(Uint32(i) * 512, 30);
			b.add(Uint32(i) * 512, 10);
		}
		const Metric &population = metric("population");
		Stats::View view = Stats::defaultView(population);
		view.share = true;
		const auto chart = Stats::buildChart(population, view, {a.history, b.history});
		CHECK(chart.percent);
		CHECK(sampleOf(chart.teams[0], 0, 2) == doctest::Approx(75));
		CHECK(sampleOf(chart.teams[1], 0, 2) == doctest::Approx(25));
	}

	TEST_CASE("Tower accuracy is hits per shot over the window")
	{
		Recorded team;
		for (int i = 0; i < 4; ++i)
		{
			auto &m = team.add(Uint32(i) * 512, 10);
			m.shots[GameplayMeasurements::TOWER] = Uint64(i) * 10;
			m.impacts[GameplayMeasurements::TOWER][GameplayMeasurements::UNIT] = Uint64(i) * 4;
		}
		const Metric &accuracy = metric("accuracy");
		CHECK_FALSE(Stats::canTotal(accuracy));
		const auto chart = Stats::buildChart(accuracy, Stats::defaultView(accuracy), {team.history});
		CHECK(chart.percent);
		CHECK(sampleOf(chart.teams[0], 0, 3) == doctest::Approx(40));
	}

	TEST_CASE("Text panels read the latest value: a rate once there are two samples")
	{
		Recorded team;
		const Metric &births = metric("births");
		const Metric &population = metric("population");
		Stats::View rate = Stats::defaultView(births);
		rate.window = 2;
		Stats::View total = rate;
		total.total = true;

		team.add(0, 20).births[WORKER] = 3;
		// One sample: a level and a total can be read, a rate cannot.
		CHECK(Stats::latestReading(population, Stats::defaultView(population), team.history).value == doctest::Approx(20));
		CHECK_FALSE(Stats::latestReading(births, rate, team.history).available);
		auto reading = Stats::latestReading(births, total, team.history);
		CHECK(reading.available);
		CHECK(reading.value == doctest::Approx(3));
		CHECK_FALSE(reading.decimals);

		team.add(INTERVAL, 30).births[WORKER] = 5;
		team.add(2 * INTERVAL, 40).births[WORKER] = 5;
		team.add(3 * INTERVAL, 50).births[WORKER] = 11;
		// The rate over the last two samples: 6 births in two intervals.
		reading = Stats::latestReading(births, rate, team.history);
		CHECK(reading.available);
		CHECK(reading.decimals);
		CHECK_FALSE(reading.percent);
		CHECK(reading.value == doctest::Approx(6 * Stats::TICKS_PER_MINUTE / (2 * INTERVAL)));
		CHECK(Stats::latestReading(births, total, team.history).value == doctest::Approx(11));

		// Splitting and sharing do not apply to a single value.
		Stats::View split = rate;
		split.split = split.share = true;
		CHECK(Stats::latestReading(births, split, team.history).value == doctest::Approx(reading.value));
		Stats::View share = Stats::defaultView(population);
		share.share = true;
		reading = Stats::latestReading(population, share, team.history);
		CHECK(reading.value == doctest::Approx(50));
		CHECK_FALSE(reading.percent);

		// A composition of a counter reads as the rate of its whole.
		const Metric &causes = metric("death causes");
		for (std::size_t i = 0; i < team.measurements.size(); ++i)
			team.measurements[i].deaths[WORKER][GameplayMeasurements::STARVATION] = Uint64(i) * 2;
		reading = Stats::latestReading(causes, Stats::defaultView(causes), team.history);
		CHECK(reading.available);
		CHECK_FALSE(reading.percent);
		CHECK(reading.value == doctest::Approx(2 * Stats::TICKS_PER_MINUTE / INTERVAL));
	}

	TEST_CASE("Counters relative to the population are per 100 units and may exceed 100")
	{
		Recorded team;
		for (int i = 0; i < 3; ++i)
			team.add(Uint32(i * INTERVAL), i == 2 ? 0 : 4).deaths[WORKER][GameplayMeasurements::COMBAT] = Uint64(i) * 8;
		const Metric &deaths = metric("deaths");
		Stats::View view = Stats::defaultView(deaths);
		view.total = view.relative = true;
		const auto chart = Stats::buildChart(deaths, view, {team.history});
		CHECK_FALSE(chart.percent);
		CHECK(chart.decimals);
		CHECK(sampleOf(chart.teams[0], 0, 1) == doctest::Approx(200));
		// No units left to relate to.
		CHECK(sampleOf(chart.teams[0], 0, 2) == doctest::Approx(0));
	}

	TEST_CASE("Shares are taken by tick when teams do not have the same samples")
	{
		Recorded a, b;
		a.history.team = 3;
		b.history.team = 5;
		for (int i = 0; i < 4; ++i)
			a.add(Uint32(i * INTERVAL), 30);
		// The second team has only the last two samples.
		for (int i = 2; i < 4; ++i)
			b.add(Uint32(i * INTERVAL), 90);
		const Metric &population = metric("population");
		Stats::View view = Stats::defaultView(population);
		view.share = true;
		const auto chart = Stats::buildChart(population, view, {a.history, b.history});
		REQUIRE(chart.teams.size() == 2);
		CHECK(chart.teams[0].team == 3);
		CHECK(chart.teams[1].team == 5);
		CHECK(sampleOf(chart.teams[0], 0, 1) == doctest::Approx(100));
		CHECK(sampleOf(chart.teams[0], 0, 2) == doctest::Approx(25));
		REQUIRE(chart.teams[1].ticks.size() == 2);
		CHECK(chart.teams[1].ticks[0] == Uint32(2 * INTERVAL));
		CHECK(sampleOf(chart.teams[1], 0, 2) == doctest::Approx(75));
	}

	TEST_CASE("A map-wide metric is one series however many teams recorded it")
	{
		Recorded a, b;
		b.history.team = 1;
		for (int i = 0; i < 3; ++i)
		{
			a.add(Uint32(i * INTERVAL), 10).growthGlobal[1][WHEAT] = Uint64(i) * 6;
			b.add(Uint32(i * INTERVAL), 10).growthGlobal[1][WHEAT] = Uint64(i) * 6;
		}
		const Metric &growth = metric("growth map");
		Stats::View view = Stats::defaultView(growth);
		view.total = true;
		const auto chart = Stats::buildChart(growth, view, {a.history, b.history});
		CHECK(chart.global);
		REQUIRE(chart.teams.size() == 1);
		CHECK(sampleOf(chart.teams[0], 0, 2) == doctest::Approx(12));
		CHECK(Stats::buildChart(growth, view, {}).teams.empty());
	}

	TEST_CASE("Markers name the first combat death, the first building destroyed and each wipe-out")
	{
		using M = GameplayMeasurements;
		Recorded team;
		team.history.team = 4;
		// Units per sample: none yet, alive, wiped out, still gone, back, wiped out again.
		const int units[] = {0, 12, 0, 0, 3, 0};
		for (int i = 0; i < 6; ++i)
		{
			auto &m = team.add(Uint32(i * INTERVAL), units[i]);
			// Counters keep their value once reached.
			m.deaths[WARRIOR][M::COMBAT] = i >= 1 ? 2 : 0;
			m.deaths[WORKER][M::STARVATION] = 7;
			m.removed[M::DESTROYED][IntBuildingType::FOOD_BUILDING][1] = i >= 4 ? 1 : 0;
			m.removed[M::DEMOLISHED][IntBuildingType::FOOD_BUILDING][1] = 5;
		}
		const auto found = Stats::markers(team.history);
		std::vector<std::pair<Uint32, std::string>> seen;
		for (const auto &marker : found)
		{
			CHECK(marker.team == 4);
			seen.push_back({marker.tick / Uint32(INTERVAL), marker.key});
		}
		// A colony that never had units is not wiped out at the start; starvation
		// and demolition are not combat.
		const std::vector<std::pair<Uint32, std::string>> expected{{1, "[stat marker first combat death]"},
																	 {2, "[stat marker wiped out]"},
																	 {4, "[stat marker first building destroyed]"},
																	 {5, "[stat marker wiped out]"}};
		CHECK(seen == expected);

		// Sampled values alone still show a wipe-out; measurements alone the rest.
		for (auto &point : team.history.points)
			point.measurements = nullptr;
		CHECK(Stats::markers(team.history).size() == 2);
		Recorded measuredOnly;
		measuredOnly.add(0, 5).deaths[WORKER][M::COMBAT] = 1;
		measuredOnly.history.points[0].sampled = nullptr;
		REQUIRE(Stats::markers(measuredOnly.history).size() == 1);
		CHECK(Stats::markers(Stats::TeamHistory()).empty());
	}
}
