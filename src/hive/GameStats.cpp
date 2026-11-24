// SPDX-License-Identifier: GPL-3.0-or-later
#include "GameStats.h"
#include "EndGameScreen.h"
#include "EngineTiming.h"
#include "Game.h"
#include "Team.h"
#include "stats/MetricCatalog.h"
#include <StringTable.h>
#include <Toolkit.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>
#include <map>
#include <stdexcept>

namespace Hive
{
namespace
{
using Json = nlohmann::json;
//! Numbers one reply may carry across all its series; more points are thinned.
constexpr std::size_t ValueBudget = 4000;

std::string text(const std::string &key)
{
	const auto *table = GAGCore::Toolkit::getStringTable();
	return table && table->doesStringExist(key) ? table->getString(key) : key;
}
const char *groupName(Stats::Group group)
{
	static const char *const names[] = {"population", "food",	  "work",  "materials",
										"buildings",  "military", "map",   "score"};
	return names[std::min(int(group), int(std::size(names)) - 1)];
}
//! How a metric's numbers read under `view`, in the chart's terms.
std::string shownAs(const Stats::Metric &metric, const Stats::View &view, const Stats::Chart &chart)
{
	if (chart.percent)
		return "percent";
	if (view.relative)
		return metric.kind == Stats::Metric::Counter ? "per_100_units_per_minute" : "per_100_units";
	if (metric.kind == Stats::Metric::Counter && !view.total)
		return metric.scale != 1 ? "average_count" : "per_minute";
	if (metric.kind == Stats::Metric::Counter)
		return "running_total";
	if (metric.mean)
		return "mean";
	return "level";
}
Json number(double value, bool decimals)
{
	if (std::isnan(value) || std::isinf(value))
		return nullptr;
	const double rounded = decimals ? std::round(value * 100) / 100 : std::round(value);
	if (rounded == std::floor(rounded) && std::abs(rounded) < 9e15)
		return std::int64_t(rounded);
	return rounded;
}
//! Indices of at most `points` samples of `count`, evenly spread, first and last kept.
std::vector<std::size_t> spread(std::size_t count, std::size_t points)
{
	std::vector<std::size_t> picked;
	if (count <= points)
	{
		for (std::size_t i = 0; i < count; ++i)
			picked.push_back(i);
		return picked;
	}
	for (std::size_t k = 0; k < points; ++k)
	{
		const auto index = std::size_t(std::llround(double(k) * double(count - 1) / double(points - 1)));
		if (picked.empty() || picked.back() != index)
			picked.push_back(index);
	}
	return picked;
}
struct Prepared
{
	const Stats::Metric *metric;
	Stats::View view;
	Stats::Chart chart, totals;
	bool hasTotals = false;
};
} // namespace

std::string colorText(int r, int g, int b)
{
	char buffer[8];
	std::snprintf(buffer, sizeof buffer, "#%02x%02x%02x", r & 255, g & 255, b & 255);
	return buffer;
}

Json statsReport(const std::vector<Stats::Metric> &catalog, const std::vector<StatsTeam> &teams,
				 const Json &args, const std::vector<std::string> &defaults)
{
	auto find = [&](const std::string &id) -> const Stats::Metric *
	{
		const auto it = std::find_if(catalog.begin(), catalog.end(), [&](const auto &m) { return id == m.id; });
		return it == catalog.end() ? nullptr : &*it;
	};
	if (args.value("list", false))
	{
		Json metrics = Json::array();
		for (const auto &m : catalog)
		{
			Json entry = {{"id", m.id},
						  {"name", text(m.titleKey())},
						  {"group", groupName(m.group)},
						  {"kind", m.kind == Stats::Metric::Counter ? "counter" : "gauge"},
						  {"about", text(m.aboutKey())}};
			if (Stats::canSplit(m))
				entry["splits"] = true;
			metrics.push_back(std::move(entry));
		}
		return {{"metrics", metrics}, {"defaults", defaults}};
	}
	std::vector<std::string> ids = defaults;
	if (args.contains("metrics") && !args.at("metrics").empty())
		ids = args.at("metrics").get<std::vector<std::string>>();
	if (ids.size() > 16)
		throw std::runtime_error("Ask for at most 16 metrics per call");
	std::vector<Stats::TeamHistory> histories;
	for (const auto &team : teams)
		histories.push_back(Stats::historyOf(team.team, *team.stats));
	std::vector<Prepared> prepared;
	std::size_t values = 0;
	for (const auto &id : ids)
	{
		const auto *metric = find(id);
		if (!metric)
			throw std::runtime_error("Unknown metric \"" + id + "\" here; call game_stats with list: true "
								   "for the names this screen offers");
		if (std::any_of(prepared.begin(), prepared.end(), [&](const auto &p) { return p.metric == metric; }))
			continue;
		Prepared p{metric, Stats::defaultView(*metric)};
		if (args.contains("split") && Stats::canSplit(*metric))
			p.view.split = args.at("split").get<bool>();
		p.view = Stats::validView(*metric, p.view);
		p.chart = Stats::buildChart(*metric, p.view, histories);
		// A counter shown as a rate also has its running total, the figure to compare.
		if (Stats::canTotal(*metric) && !p.view.total)
		{
			Stats::View total = p.view;
			total.total = true;
			total.split = false;
			p.totals = Stats::buildChart(*metric, total, histories);
			p.hasTotals = true;
		}
		for (const auto &series : p.chart.teams)
			values += series.ticks.size() * (series.values.size() + 1);
		prepared.push_back(std::move(p));
	}
	std::size_t points = std::size_t(std::clamp(args.value("points", 60), 2, 300));
	bool thinned = false;
	if (values > 0)
	{
		std::size_t longest = 1;
		for (const auto &p : prepared)
			for (const auto &series : p.chart.teams)
				longest = std::max(longest, series.ticks.size());
		const std::size_t used = std::min(points, longest);
		const std::size_t estimate = values * used / longest;
		if (estimate > ValueBudget)
		{
			points = std::max<std::size_t>(2, used * ValueBudget / estimate);
			thinned = true;
		}
	}
	Json metrics = Json::array();
	for (const auto &p : prepared)
	{
		const auto &m = *p.metric;
		Json entry = {{"id", m.id},
					  {"name", text(m.titleKey())},
					  {"group", groupName(m.group)},
					  {"shown_as", shownAs(m, p.view, p.chart)},
					  {"unit", text(m.unitKey)}};
		if (p.chart.stacked)
		{
			Json bands = Json::array();
			for (std::size_t i = 0; i < p.chart.bandKeys.size(); ++i)
				bands.push_back(i < p.chart.bandLabelLiteral.size() && p.chart.bandLabelLiteral[i]
									? p.chart.bandKeys[i]
									: text(p.chart.bandKeys[i]));
			entry["bands"] = bands;
		}
		else if (Stats::canSplit(m))
			entry["splits"] = true;
		if (!p.chart.any)
			entry["nothing_recorded"] = true;
		if (p.chart.global)
			entry["map_wide"] = true;
		Json series = Json::array();
		for (std::size_t t = 0; t < p.chart.teams.size(); ++t)
		{
			const auto &s = p.chart.teams[t];
			Json row = {{"team", s.team}};
			// Summary over every sample, not only the ones returned.
			double latest = std::numeric_limits<double>::quiet_NaN(), peak = latest, low = latest;
			for (std::size_t i = 0; i < s.ticks.size(); ++i)
			{
				double sum = 0;
				for (const auto &band : s.values)
					sum += band[i];
				if (std::isnan(sum))
					continue;
				latest = sum;
				peak = std::isnan(peak) ? sum : std::max(peak, sum);
				low = std::isnan(low) ? sum : std::min(low, sum);
			}
			Json summary = {{"latest", number(latest, p.chart.decimals)},
							{"peak", number(peak, p.chart.decimals)},
							{"low", number(low, p.chart.decimals)}};
			if (p.hasTotals && t < p.totals.teams.size() && !p.totals.teams[t].ticks.empty())
				summary["total"] = number(p.totals.teams[t].values[0].back(), false);
			row["summary"] = summary;
			const auto picked = spread(s.ticks.size(), points);
			Json seconds = Json::array();
			for (const auto i : picked)
				seconds.push_back(s.ticks[i] / GAME_TICKS_PER_SECOND);
			row["seconds"] = seconds;
			auto take = [&](const std::vector<double> &band)
			{
				Json out = Json::array();
				for (const auto i : picked)
					out.push_back(number(band[i], p.chart.decimals));
				return out;
			};
			if (p.chart.stacked)
			{
				Json bands = Json::array();
				for (const auto &band : s.values)
					bands.push_back(take(band));
				row["band_values"] = bands;
			}
			else if (!s.values.empty())
				row["values"] = take(s.values[0]);
			series.push_back(std::move(row));
		}
		entry["teams"] = series;
		metrics.push_back(std::move(entry));
	}
	Json result = {{"metrics", metrics}, {"points", points}};
	if (thinned)
		result["points_reduced"] = "Fewer points than asked, to keep the reply small; request fewer metrics for more";
	return result;
}

Json finishedMatchStats(const EndGameScreen &screen, const Json &args)
{
	const Game &game = screen.finishedGame();
	const auto columns = EndGameScreen::overviewNames();
	std::vector<StatsTeam> teams;
	Json overview = Json::array();
	for (const auto &entry : screen.results())
	{
		const Team *team = entry.teamNum >= 0 && entry.teamNum < game.mapHeader.getNumberOfTeams()
							   ? game.teams[entry.teamNum]
							   : nullptr;
		if (!team)
			continue;
		Json details = {{"team", entry.teamNum},
						{"name", entry.name},
						{"color", colorText(entry.color.r, entry.color.g, entry.color.b)},
						{"won", team->hasWon},
						{"lost", team->hasLost || !team->isAlive}};
		if (entry.teamNum == screen.localTeamNumber())
			details["you"] = true;
		Json figures = Json::object();
		for (std::size_t i = 0; i < columns.size() && i < entry.summary.size(); ++i)
			figures[columns[i]] = number(entry.summary[i], false);
		details["overview"] = figures;
		overview.push_back(details);
		teams.push_back({entry.teamNum, &team->stats, details});
	}
	if (teams.empty())
		throw std::runtime_error("The results list no team");
	static const std::vector<std::string> defaults = {"win probability", "population", "births", "deaths",
													  "death causes",	  "conversions", "wheat stored", "warriors"};
	Json result = statsReport(screen.metrics(), teams, args, defaults);
	if (args.value("list", false))
		return result;
	const char *outcomes[] = {"victory", "defeat", "ended", "left"};
	result["mode"] = "finished";
	result["outcome"] = outcomes[int(screen.shownOutcome())];
	if (!screen.outcomeReason().empty())
		result["reason"] = screen.outcomeReason();
	result["duration_seconds"] = screen.matchSeconds();
	result["teams"] = overview;
	result["note"] = "The finished match as the results screen shows it, for every team. Each "
					 "metric has per-team summary (over all samples) and series sampled at seconds.";
	return result;
}
} // namespace Hive
