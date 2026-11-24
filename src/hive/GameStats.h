// SPDX-License-Identifier: GPL-3.0-or-later
// The MCP game_stats tool: a team's recorded statistics as the game's own panels
// chart them (stats/MetricCatalog.h), downsampled for an agent. In a match it
// reads only the player's colony (LocalCommander::stats); on the results screen
// every team of the finished match (finishedMatchStats).
#pragma once
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

class EndGameScreen;
class TeamStats;
namespace Stats
{
struct Metric;
}
namespace Hive
{
//! One team to report: its recorded history and what describes it.
struct StatsTeam
{
	int team = 0;
	const TeamStats *stats = nullptr; //!< Must not record while the report is built.
	nlohmann::json details = nlohmann::json::object(); //!< Name, colour, outcome...
};
//! Statistics of `teams` over the metrics of `catalog`, as game_stats returns
//! them. `args` holds the tool's arguments (list, metrics, points, split); the
//! metrics named in `defaults` are reported when it names none. Throws
//! std::runtime_error for an unknown metric.
nlohmann::json statsReport(const std::vector<Stats::Metric> &catalog, const std::vector<StatsTeam> &teams,
						   const nlohmann::json &args, const std::vector<std::string> &defaults);
//! game_stats on the results screen: every team of the finished match, with
//! the outcome, the overview figures and the metrics the screen charts.
nlohmann::json finishedMatchStats(const EndGameScreen &screen, const nlohmann::json &args);
//! Colour as "#rrggbb".
std::string colorText(int r, int g, int b);
} // namespace Hive
