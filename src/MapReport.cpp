// SPDX-License-Identifier: GPL-3.0-or-later
#include "MapReport.h"
#include "Game.h"
#include "GenerationRequest.h"
#include "GenerationResult.h"
#include "GeneratorRegistry.h"
#include "Contact.h"
#include "FertilityField.h"
#include "Grid.h"
#include "Room.h"
#include "Topology.h"
#include "RessourceType.h"
#include "Unit.h"
#include "Building.h"
#include "Version.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <iomanip>
#include <locale>
#include <map>
#include <numeric>
#include <sstream>
#include <stdexcept>
#include <type_traits>

namespace
{
// Small typed JSON encoder; numbers remain numbers, null is explicit, and strings are escaped.
// Report objects are small aggregates, never per-tile grids.
struct Json
{
	std::string text = "null";
	Json() = default;
	Json(bool value) : text(value ? "true" : "false") {}
	Json(const char *value) : Json(std::string(value)) {}
	Json(const std::string &value)
	{
		std::ostringstream out;
		out.imbue(std::locale::classic());
		out << '"';
		for (unsigned char c : value)
		{
			if (c == '"' || c == '\\')
				out << '\\' << char(c);
			else if (c < 32)
				out << "\\u" << std::hex << std::setw(4) << std::setfill('0') << int(c);
			else
				out << char(c);
		}
		out << '"';
		text = out.str();
	}
	template <class T,
			  std::enable_if_t<std::is_arithmetic_v<T> && !std::is_same_v<T, bool>, int> = 0>
	Json(T value)
	{
		if (!std::isfinite(static_cast<double>(value)))
			throw std::runtime_error("Non-finite map statistic");
		std::ostringstream out;
		out.imbue(std::locale::classic());
		out << std::setprecision(17) << +value;
		text = out.str();
	}
	static Json object(const std::vector<std::pair<std::string, Json>> &values)
	{
		Json result;
		result.text = "{";
		for (const auto &entry : values)
		{
			if (result.text.size() > 1)
				result.text += ',';
			result.text += Json(entry.first).text + ':' + entry.second.text;
		}
		result.text += '}';
		return result;
	}
	static Json array(const std::vector<Json> &values)
	{
		Json result;
		result.text = "[";
		for (const auto &value : values)
		{
			if (result.text.size() > 1)
				result.text += ',';
			result.text += value.text;
		}
		result.text += ']';
		return result;
	}
};
std::string pretty(const std::string &json)
{
	std::string out;
	int depth = 0;
	bool quoted = false, escaped = false;
	const auto newline = [&]
	{
		out += '\n';
		out.append(depth * 2, ' ');
	};
	for (size_t i = 0; i < json.size(); ++i)
	{
		const char c = json[i];
		if (quoted)
		{
			out += c;
			if (escaped)
				escaped = false;
			else if (c == '\\')
				escaped = true;
			else if (c == '"')
				quoted = false;
		}
		else if (c == '"')
		{
			quoted = true;
			out += c;
		}
		else if (c == '{' || c == '[')
		{
			out += c;
			if (i + 1 < json.size() && (json[i + 1] == '}' || json[i + 1] == ']'))
				out += json[++i];
			else
			{
				++depth;
				newline();
			}
		}
		else if (c == '}' || c == ']')
		{
			--depth;
			newline();
			out += c;
		}
		else if (c == ',')
		{
			out += c;
			newline();
		}
		else if (c == ':')
			out += ": ";
		else
			out += c;
	}
	return out;
}
using J = Json;
using namespace MapGeneration;
const std::array<const char *, MAX_RESOURCES> resourceNames = {
	"wood", "wheat", "papyrus", "stone", "algae", "cherry", "orange", "prune"};
J distance(int n)
{
	return n < 0 ? J() : J(n);
}
J coverage(int count, int total)
{
	return J::object({{"tiles", count}, {"percent", total ? J(100.0 * count / total) : J()}});
}
J distribution(std::vector<double> values)
{
	if (values.empty())
		return J::object({{"count", 0},
						  {"min", J()},
						  {"max", J()},
						  {"mean", J()},
						  {"stddev", J()},
						  {"median", J()},
						  {"p10", J()},
						  {"p90", J()}});
	std::sort(values.begin(), values.end());
	const double mean = std::accumulate(values.begin(), values.end(), 0.0) / values.size();
	double variance = 0;
	for (double value : values)
		variance += (value - mean) * (value - mean);
	const auto percentile = [&](double p)
	{
		const double index = p * (values.size() - 1);
		const size_t lo = static_cast<size_t>(index), hi = std::min(lo + 1, values.size() - 1);
		return values[lo] + (values[hi] - values[lo]) * (index - lo);
	};
	return J::object({{"count", values.size()},
					  {"min", values.front()},
					  {"max", values.back()},
					  {"mean", mean},
					  {"stddev", std::sqrt(variance / values.size())},
					  {"median", percentile(0.5)},
					  {"p10", percentile(0.1)},
					  {"p90", percentile(0.9)}});
}
J components(const Map &map, const std::vector<unsigned char> &mask, GridNeighbors neighbors)
{
	const auto labels = connectedRegions(mask, map.getW(), map.getH(), true, neighbors);
	std::map<int, int> sizes;
	for (int label : labels)
		if (label >= 0)
			++sizes[label];
	std::vector<double> counts;
	for (const auto &entry : sizes)
		counts.push_back(entry.second);
	const int total = std::accumulate(counts.begin(), counts.end(), 0);
	const int largest = counts.empty() ? 0 : int(*std::max_element(counts.begin(), counts.end()));
	return J::object(
		{{"components", sizes.size()},
		 {"passable", coverage(total, int(mask.size()))},
		 {"largest_component_tiles", largest},
		 {"largest_component_percent_of_passable", total ? J(100.0 * largest / total) : J()},
		 {"component_sizes", distribution(counts)}});
}
J weightsJson(const StartQualityWeights &w)
{
	return J::object({{"wheat", w.wheat},
					  {"wood", w.wood},
					  {"fertility", w.fertility},
					  {"depth", w.depth},
					  {"room", w.room},
					  {"isolation", w.isolation}});
}
J scaleJson(const StartQualityScale &s)
{
	return J::object({{"catchment_steps", s.catchmentSteps},
					  {"wheat_reference", s.wheatReference},
					  {"wood_reference", s.woodReference},
					  {"fertility_reference", s.fertilityReference},
					  {"depth_reference", s.depthReference},
					  {"room_reference", s.roomReference},
					  {"isolation_reference", s.isolationReference},
					  {"threat_radius", s.threatRadius},
					  {"crowd_penalty", s.crowdPenalty},
					  {"fairness_exponent", s.fairnessExponent}});
}
J qualityJson(const StartQualityReport &report, const StartQualityWeights &weights,
			  const StartQualityScale &scale)
{
	std::vector<J> colonies;
	std::vector<double> totals;
	for (size_t i = 0; i < report.colonies.size(); ++i)
	{
		const auto &c = report.colonies[i];
		colonies.push_back(
			J::object({{"team", i},
					   {"raw", J::object({{"wheat_distance", distance(c.wheatDistance)},
										  {"wood_distance", distance(c.woodDistance)},
										  {"catchment_tiles", c.catchmentTiles},
										  {"build_sites_4x4", c.buildSites},
										  {"wheat_and_wood_amount", c.resourceAmount},
										  {"mean_fertility", c.meanFertility},
										  {"nearest_rival_distance", distance(c.rivalDistance)},
										  {"rivals_within_threat", c.rivalsWithinThreat}})},
					   {"normalized", J::object({{"wheat", c.wheat},
												 {"wood", c.wood},
												 {"fertility", c.fertility},
												 {"depth", c.depth},
												 {"room", c.room},
												 {"isolation", c.isolation}})},
					   {"total", c.total}}));
		totals.push_back(c.total);
	}
	return J::object(
		{{"measured", report.measured},
		 {"unavailable_reason",
		  report.measured ? J() : J("Every colony needs at least one ground-unit source tile")},
		 {"weights", weightsJson(weights)},
		 {"scale", scaleJson(scale)},
		 {"worst", report.measured ? J(report.worst) : J()},
		 {"best", report.measured ? J(report.best) : J()},
		 {"fairness", report.measured ? J(report.fairness) : J()},
		 {"score", report.measured ? J(report.score) : J()},
		 {"colony_totals", distribution(totals)},
		 {"colonies", J::array(colonies)}});
}
StartQualityReport canonicalQuality(Game &game)
{
	// The production scorer also stamps its fertility cache. Preserve every stored value,
	// including unusual values in old saves, so requesting JSON cannot change a map export.
	struct RestoreFertility
	{
		Map &map;
		Uint16 maximum;
		std::vector<Uint16> values;
		explicit RestoreFertility(Map &m) : map(m), maximum(m.fertilityMaximum)
		{
			values.reserve(m.tiles.size());
			for (const auto &tile : m.tiles)
				values.push_back(tile.fertility);
		}
		~RestoreFertility()
		{
			for (size_t i = 0; i < values.size(); ++i)
				map.tiles[i].fertility = values[i];
			map.fertilityMaximum = maximum;
		}
	} restore(game.map);
	return scoreStarts(game, game.teamsCount());
}
J telemetryJson(const GenerationTelemetry &telemetry)
{
	std::vector<J> records;
	for (const auto &r : telemetry.records())
		records.push_back(
			J::object({{"key", r.key},
					   {"kind", r.kind},
					   {"subject", r.subject < 0 ? J() : J(r.subject)},
					   {"value", std::visit([](const auto &v) { return J(v); }, r.value)}}));
	return J::object({{"schema_version", 1},
					  {"enabled", telemetry.enabled()},
					  {"record_limit", GenerationTelemetry::kMaxRecords},
					  {"dropped_records", telemetry.droppedRecords()},
					  {"invalid_values", telemetry.invalidValues()},
					  {"records", J::array(records)}});
}
const char *generationErrorName(GenerationError error)
{
	switch (error)
	{
	case GenerationError::None:
		return "none";
	case GenerationError::InvalidRequest:
		return "invalid_request";
	case GenerationError::NonEmptyTarget:
		return "non_empty_target";
	case GenerationError::PlacementFailed:
		return "placement_failed";
	case GenerationError::InvalidWorld:
		return "invalid_world";
	}
	return "unknown";
}
J generationJson(const GenerationRequest *request, const GenerationResult *result)
{
	if (!request)
		return J::object(
			{{"available", false},
			 {"parameters", J()},
			 {"telemetry", J()},
			 {"reason", "Map/save files do not store the complete original generator request"}});
	const auto *definition = GeneratorRegistry::builtins().find(request->method);
	std::vector<std::pair<std::string, J>> parameters, rawOptions;
	for (const auto &entry : request->options)
		rawOptions.push_back({entry.first, entry.second});
	bool resolved = definition != nullptr;
	const auto addControl = [&](const GeneratorControl &c)
	{
		const bool shared =
			c.id == "width" || c.id == "height" || c.id == "teams" || c.id == "workers";
		if (!shared && request->options.find(c.id) == request->options.end())
		{
			resolved = false;
			return;
		}
		const int v = c.get(*request);
		if (v != c.normalize(v))
			resolved = false;
		else
			parameters.push_back({c.id, c.displayValue(v)});
	};
	for (const auto &c : GenerationRequest::sharedControls())
		addControl(c);
	if (definition)
		for (const auto &c : definition->controls)
			addControl(c);
	std::vector<J> legacyAmounts;
	for (int n : request->resourceAmounts)
		legacyAmounts.push_back(n);
	return J::object(
		{{"available", true},
		 {"generator", result && !result->generatorId.empty() ? J(result->generatorId)
					   : definition                           ? J(definition->id)
															  : J()},
		 {"legacy_id", request->method},
		 {"revision", result       ? result->revision
					  : definition ? definition->revision
								   : 0},
		 {"seed", request->seed},
		 {"parameters", resolved ? J::object(parameters) : J()},
		 {"raw_request", J::object({{"method", request->method},
									{"width_exponent", request->wDec},
									{"height_exponent", request->hDec},
									{"teams", request->nbTeams},
									{"workers", request->nbWorkers},
									{"options", J::object(rawOptions)}})},
		 {"legacy_terrain_type", int(request->terrainType)},
		 {"legacy_resource_amounts", J::array(legacyAmounts)},
		 {"telemetry", result ? telemetryJson(result->telemetry) : J()},
		 {"outcome", result ? J::object({{"success", bool(*result)},
										 {"stage", result->stage},
										 {"error", generationErrorName(result->error)},
										 {"detail", result->detail}})
							: J()},
		 {"selection_quality",
		  result && *result && definition
			  ? qualityJson(result->quality, definition->qualityWeights, definition->qualityScale)
			  : J()}});
}

J movementReport(const Game &game, const StepCosts &costs,
				 const std::vector<unsigned char> &anchors)
{
	const Map &map = game.map;
	const Torus t(map);
	const int teams = game.teamsCount();
	const auto units = unitTilesByTeam(map, teams);
	const bool clearing = costs.clearable >= 0;
	std::vector<unsigned char> passable(t.size());
	for (int p = 0; p < t.size(); ++p)
		passable[p] = clearing
						  ? stepCost(map, p % t.w, p / t.w, costs) >= 0
						  : map.isHardSpaceForGroundUnit(p % t.w, p / t.w, costs.water >= 0, 0);
	std::vector<std::vector<int>> fields;
	for (const auto &sources : units)
		fields.push_back(clearing ? costsFrom(map, t, sources, costs)
								  : stepsFrom(t, tileMask(t, sources), passable));
	std::vector<J> rows, colonies;
	ContactReport contact;
	contact.cost.assign(teams, std::vector<int>(teams, -1));
	int unreachablePairs = 0;
	std::vector<double> pairDistances;
	std::vector<int> exclusive(teams, 0), contested(teams, 0);
	int tiedTiles = 0, unreachableTiles = 0;
	std::vector<double> closestCosts;
	for (int p = 0; p < t.size(); ++p)
	{
		int best = -1, ties = 0, owner = -1;
		for (int team = 0; team < teams; ++team)
		{
			const int value = fields[team][p];
			if (value < 0)
				continue;
			if (best < 0 || value < best)
			{
				best = value;
				ties = 1;
				owner = team;
			}
			else if (value == best)
				++ties;
		}
		if (best < 0)
		{
			++unreachableTiles;
			continue;
		}
		closestCosts.push_back(best);
		if (ties == 1)
			++exclusive[owner];
		else
		{
			++tiedTiles;
			for (int team = 0; team < teams; ++team)
				if (fields[team][p] == best)
					++contested[team];
		}
	}
	for (int team = 0; team < teams; ++team)
	{
		const auto &field = fields[team];
		std::vector<J> row;
		for (int other = 0; other < teams; ++other)
		{
			int best = team == other ? 0 : -1;
			if (other != team)
				for (int p : units[other])
					if (field[p] >= 0 && (best < 0 || field[p] < best))
						best = field[p];
			contact.cost[team][other] = best;
			row.push_back(distance(best));
			if (other != team)
			{
				if (best < 0)
					++unreachablePairs;
				else
					pairDistances.push_back(best);
			}
		}
		rows.push_back(J::array(row));
		int reached = 0, catchment = 0, sites = 0, closeSites = 0;
		std::array<int, MAX_RESOURCES> nearest, resourceTiles{}, nearbyTiles{};
		std::array<std::int64_t, MAX_RESOURCES> resourceAmounts{}, nearbyAmounts{};
		nearest.fill(-1);
		for (int p = 0; p < t.size(); ++p)
		{
			if (field[p] >= 0)
			{
				++reached;
				if (field[p] <= 24)
					++catchment;
				if (anchors[p])
				{
					++sites;
					if (field[p] <= 24)
						++closeSites;
				}
			}
			const auto &r = map.getResource(p);
			if (r.type >= MAX_RESOURCES || !r.amount)
				continue;
			int approach = -1;
			for (int dy = -1; dy <= 1; ++dy)
				for (int dx = -1; dx <= 1; ++dx)
				{
					if (!dx && !dy)
						continue;
					const int d = field[t.at(p % t.w + dx, p / t.w + dy)];
					if (d >= 0 && (approach < 0 || d < approach))
						approach = d;
				}
			if (approach < 0)
				continue;
			++resourceTiles[r.type];
			resourceAmounts[r.type] += r.amount;
			if (nearest[r.type] < 0 || approach + 1 < nearest[r.type])
				nearest[r.type] = approach + 1;
			if (approach <= 24)
			{
				++nearbyTiles[r.type];
				nearbyAmounts[r.type] += r.amount;
			}
		}
		std::vector<std::pair<std::string, J>> resources;
		for (int r = 0; r < MAX_RESOURCES; ++r)
			resources.push_back(
				{resourceNames[r], J::object({{"nearest_gather_cost", distance(nearest[r])},
											  {"reachable_deposit_tiles", resourceTiles[r]},
											  {"reachable_stored_amount", resourceAmounts[r]},
											  {"catchment_deposit_tiles", nearbyTiles[r]},
											  {"catchment_stored_amount", nearbyAmounts[r]}})});
		colonies.push_back(J::object({{"team", team},
									  {"source_tiles", units[team].size()},
									  {"reachable", coverage(reached, t.size())},
									  {"catchment_tiles", catchment},
									  {"reachable_build_sites_4x4", sites},
									  {"catchment_build_sites_4x4", closeSites},
									  {"exclusive_nearest_territory_tiles", exclusive[team]},
									  {"tied_nearest_territory_tiles", contested[team]},
									  {"resources", J::object(resources)}}));
	}
	std::vector<J> nearest;
	for (int n : contact.nearestRival())
		nearest.push_back(distance(n));
	return J::object(
		{{"step_costs", J::object({{"open", costs.open},
								   {"water", distance(costs.water)},
								   {"clearable_resource", distance(costs.clearable)},
								   {"eternal_resource", distance(costs.eternal)},
								   {"building", distance(costs.building)}})},
		 {"between_colonies", J::array(rows)},
		 {"nearest_rival", J::array(nearest)},
		 {"nearest_rival_spread", distance(contact.spread())},
		 {"unreachable_directed_pairs", unreachablePairs},
		 {"directed_pair_costs", distribution(pairDistances)},
		 {"connectivity", components(map, passable, GridNeighbors::Eight)},
		 {"territory", J::object({{"unreachable_tiles", unreachableTiles},
								  {"tied_tiles", tiedTiles},
								  {"cost_to_closest_colony", distribution(closestCosts)}})},
		 {"colonies", J::array(colonies)}});
}
} // namespace

std::string describeMap(Game &game, const GenerationRequest *request,
						const GenerationResult *generation)
{
	const Map &map = game.map;
	const Torus t(map);
	std::array<int, 6> terrain{};
	std::array<int, 4> underlying{};
	std::array<int, MAX_RESOURCES> resourceTiles{}, harvestable{};
	std::array<std::int64_t, MAX_RESOURCES> resourceAmounts{};
	int resourceOccupied = 0, unknownResources = 0, buildingTiles = 0, noGrowth = 0;
	std::vector<unsigned char> water(t.size()), land(t.size());
	std::vector<double> resourceAmountValues[MAX_RESOURCES];
	const auto fertility = Fertility::forMap(map);
	const auto potential = Fertility::forMap(map, false);
	std::vector<double> fertilityAll, fertilityGrass, potentialGrass;
	int fertileGrass = 0;
	for (int p = 0; p < t.size(); ++p)
	{
		const unsigned tile = map.getTerrain(p);
		++terrain[tile < 16    ? 0
				  : tile < 128 ? 1
				  : tile < 144 ? 2
				  : tile < 256 ? 3
				  : tile < 272 ? 4
							   : 5];
		const int um = map.getUMTerrain(p % t.w, p / t.w);
		++underlying[um >= 0 && um <= 2 ? um : 3];
		water[p] = map.isWater(p);
		land[p] = tile < 256;
		const auto &r = map.getResource(p);
		if (r.type != NO_RES_TYPE)
		{
			++resourceOccupied;
		}
		if (r.type < MAX_RESOURCES)
		{
			++resourceTiles[r.type];
			resourceAmounts[r.type] += r.amount;
			harvestable[r.type] += r.amount > 0;
			resourceAmountValues[r.type].push_back(r.amount);
		}
		else if (r.type != NO_RES_TYPE)
			++unknownResources;
		buildingTiles += map.getBuilding(p % t.w, p / t.w) != NOGBID;
		noGrowth += !map.tiles[p].canResourcesGrow;
		fertilityAll.push_back(fertility.values()[p]);
		if (map.isGrass(p))
		{
			fertilityGrass.push_back(fertility.values()[p]);
			potentialGrass.push_back(potential.values()[p]);
			fertileGrass += fertility.values()[p] > 0;
		}
	}
	std::vector<std::pair<std::string, J>> terrainJson, underlyingJson, resources;
	const char *terrainNames[] = {
		"grass", "grass_sand_border", "sand", "sand_water_border", "water", "unknown"};
	const char *underlyingNames[] = {"water", "sand", "grass", "unknown"};
	for (int i = 0; i < 6; ++i)
		terrainJson.push_back({terrainNames[i], coverage(terrain[i], t.size())});
	for (int i = 0; i < 4; ++i)
		underlyingJson.push_back({underlyingNames[i], coverage(underlying[i], t.size())});
	const ResourcesTypes resourceTypes;
	for (int r = 0; r < MAX_RESOURCES; ++r)
	{
		std::vector<unsigned char> mask(t.size());
		for (int p = 0; p < t.size(); ++p)
			mask[p] = map.getResource(p).type == r;
		resources.push_back(
			{resourceNames[r],
			 J::object({{"coverage", coverage(resourceTiles[r], t.size())},
						{"percent_of_resource_tiles",
						 resourceOccupied ? J(100.0 * resourceTiles[r] / resourceOccupied) : J()},
						{"stored_amount", resourceAmounts[r]},
						{"harvestable_tiles", harvestable[r]},
						{"eternal", bool(resourceTypes.get(r)->eternal)},
						{"clearable", bool(resourceTypes.get(r)->clearable)},
						{"amount_per_deposit", distribution(resourceAmountValues[r])},
						{"patches", components(map, mask, GridNeighbors::Eight)}})});
	}
	const auto buildable = buildableTiles(map);
	const auto anchors = buildAnchors(t, buildable);
	int buildableCount = std::accumulate(buildable.begin(), buildable.end(), 0);
	int sitesCount = std::accumulate(anchors.begin(), anchors.end(), 0);
	std::vector<J> colonies, controllers, geometry;
	for (int i = 0; i < game.teamsCount(); ++i)
	{
		const Team &team = *game.teams[i];
		std::array<int, 3> units{};
		int buildings = 0;
		for (int u = 0; u < Unit::MAX_COUNT; ++u)
			if (team.myUnits[u] && team.myUnits[u]->typeNum >= 0 && team.myUnits[u]->typeNum < 3)
				++units[team.myUnits[u]->typeNum];
		for (int b = 0; b < Building::MAX_COUNT; ++b)
			buildings += team.myBuildings[b] != nullptr;
		colonies.push_back(
			J::object({{"team", i},
					   {"alive", team.isAlive},
					   {"start", J::object({{"x", team.startPosSet ? J(team.startPosX) : J()},
											{"y", team.startPosSet ? J(team.startPosY) : J()},
											{"source", team.startPosSet}})},
					   {"units", J::object({{"workers", units[WORKER]},
											{"explorers", units[EXPLORER]},
											{"warriors", units[WARRIOR]}})},
					   {"buildings_and_flags", buildings}}));
		std::vector<J> row;
		for (int other = 0; other < game.teamsCount(); ++other)
			row.push_back(team.startPosSet && game.teams[other]->startPosSet
							  ? J(std::sqrt(double(t.dist2(team.startPosX, team.startPosY,
														   game.teams[other]->startPosX,
														   game.teams[other]->startPosY))))
							  : J());
		geometry.push_back(J::array(row));
	}
	for (int i = 0; i < game.gameHeader.getNumberOfPlayers(); ++i)
	{
		const auto &p = game.gameHeader.getBasePlayer(i);
		controllers.push_back(
			J::object({{"slot", i}, {"team", p.teamNumber}, {"type", int(p.type)}}));
	}
	const auto quality = canonicalQuality(game);
	return pretty(
			   J::object(
				   {{"schema_version", 2},
					{"report_type", "map"},
					{"engine", J::object({{"version_major", VERSION_MAJOR},
										  {"version_minor", VERSION_MINOR}})},
					{"map", J::object({{"name", request ? J() : J(game.mapHeader.getMapName())},
									   {"width", t.w},
									   {"height", t.h},
									   {"tiles", t.size()},
									   {"wrap_x", true},
									   {"wrap_y", true},
									   {"player_slots", game.teamsCount()},
									   {"controller_count", game.gameHeader.getNumberOfPlayers()},
									   {"saved_game", game.mapHeader.getIsSavedGame()},
									   {"tick", game.stepCounter},
									   {"game_seed", game.gameHeader.getRandomSeed()},
									   {"colonies", J::array(colonies)},
									   {"controllers", J::array(controllers)}})},
					{"generation", generationJson(request, generation)},
					{"definitions",
					 J::object(
						 {{"percent_denominator",
						   "All map tiles unless the field says otherwise; percentages are 0..100"},
						  {"distance",
						   "Eight-neighbor toroidal minimum cost between current ground-unit tile "
						   "sets; ignores transient unit collisions, fog, diplomacy and orders"},
						  {"unreachable",
						   "null; diagonal colony distances are zero even without sources"},
						  {"resource_access", "Minimum cost to a neighboring tile plus one; only "
											  "deposits with positive stored amount count"},
						  {"catchment_cost", 24},
						  {"resource_amount", "Stored amount is a snapshot, not lifetime supply; "
											  "eternal deposits are not finite stockpiles"},
						  {"components",
						   "Toroidal; terrain land/water are four-connected, resource "
						   "patches and movement are eight-connected"},
						  {"canonical_quality",
						   "Production scoreStarts with common default weights/scales, independent "
						   "of "
						   "generator; uses its own ground-unit passability rule"},
						  {"quality_formula",
						   "total = weighted mean of normalized factors, forced to "
						   "0 if wheat/wood unreachable; fairness = worst/best or 0 "
						   "when best=0; score = worst * fairness^exponent"},
						  {"percentiles",
						   "Linear interpolation at p*(count-1); stddev is population "
						   "standard deviation"}})},
					{"terrain", J::object(terrainJson)},
					{"underlying_terrain", J::object(underlyingJson)},
					{"resources", J::object({{"occupied", coverage(resourceOccupied, t.size())},
											 {"unknown_type_tiles", unknownResources},
											 {"types", J::object(resources)}})},
					{"space",
					 J::object(
						 {{"building_footprint", coverage(buildingTiles, t.size())},
						  {"buildable", coverage(buildableCount, t.size())},
						  {"build_sites_4x4", sitesCount},
						  {"growth_disabled", coverage(noGrowth, t.size())},
						  {"land_regions", components(map, land, GridNeighbors::Cardinal)},
						  {"water_regions", components(map, water, GridNeighbors::Cardinal)}})},
					{"fertility",
					 J::object({{"scale", Fertility::kScale},
								{"all_tiles", distribution(fertilityAll)},
								{"grass_tiles", distribution(fertilityGrass)},
								{"potential_grass_ignoring_deposit_reachability",
								 distribution(potentialGrass)},
								{"positive_grass", coverage(fertileGrass, terrain[0])}})},
					{"canonical_quality", qualityJson(quality, {}, {})},
					{"start_position_euclidean_distances", J::array(geometry)},
					{"movement",
					 J::object({{"walking", movementReport(game, StepCosts::walking(), anchors)},
								{"walking_and_swimming",
								 movementReport(game, StepCosts::swimming(), anchors)},
								{"walking_and_clearing",
								 movementReport(game, StepCosts::chopping(), anchors)}})}})
				   .text) +
		   "\n";
}

std::string describeGenerationFailure(const GenerationRequest &request,
									  const GenerationResult &result)
{
	return pretty(J::object({{"schema_version", 2},
							 {"report_type", "generation_failure"},
							 {"engine", J::object({{"version_major", VERSION_MAJOR},
												   {"version_minor", VERSION_MINOR}})},
							 {"generation", generationJson(&request, &result)}})
					  .text) +
		   "\n";
}
