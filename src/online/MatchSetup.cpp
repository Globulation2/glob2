// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 glob2 contributors

#include "MatchSetup.h"

#include <FileManager.h>
#include <StreamBackend.h>
#include <Toolkit.h>

#include <algorithm>
#include <cstdint>
#include <memory>
#include <nlohmann/json.hpp>
#include <regex>
#include <set>

#include "AINames.h"
#include "BasePlayer.h"
#include "Engine.h"
#include "EngineTiming.h"
#include "ExperimentalFeatures.h"
#include "GameHeader.h"
#include "MapHeader.h"
#include "Sha256.h"
#include "WinningConditions.h"

using nlohmann::json;

namespace Online
{
// MatchSetupError's constructor is in SimVersion.cpp.

bool MatchRules::operator==(const MatchRules &o) const
{
	return prestigeVictory == o.prestigeVictory && suddenDeathMinutes == o.suddenDeathMinutes &&
		   mapDiscovered == o.mapDiscovered && allyTeamsFixed == o.allyTeamsFixed &&
		   resourceGrowthDisabled == o.resourceGrowthDisabled &&
		   resourceScarcityLevel == o.resourceScarcityLevel &&
		   instantConstruction == o.instantConstruction &&
		   stockpileStartLevel == o.stockpileStartLevel && hungerDisabled == o.hungerDisabled &&
		   unitUpgradesDisabled == o.unitUpgradesDisabled &&
		   glassCannonLevel == o.glassCannonLevel && unitsFearless == o.unitsFearless &&
		   permadeathDisabled == o.permadeathDisabled && peacefulMode == o.peacefulMode &&
		   buildingHpLevel == o.buildingHpLevel && buildingGradientDelay == o.buildingGradientDelay;
}

bool GeneratorDescriptor::operator==(const GeneratorDescriptor &o) const
{
	return generatorId == o.generatorId && revision == o.revision && params == o.params &&
		   seed == o.seed && candidates == o.candidates && startingUnitLevel == o.startingUnitLevel;
}

bool MapSource::operator==(const MapSource &o) const
{
	return kind == o.kind && hash == o.hash && mapId == o.mapId && format == o.format &&
		   generator == o.generator;
}

namespace
{
constexpr int MAX_TEAMS = 12;
constexpr std::size_t MAX_NAME_BYTES = 32;
using Stage = MatchSetupError::Stage;

[[noreturn]] void schemaError(const std::string &path, const std::string &message)
{
	throw MatchSetupError(Stage::Schema, path, message);
}
[[noreturn]] void semanticError(const std::string &path, const std::string &message)
{
	throw MatchSetupError(Stage::Semantic, path, message);
}

/// Checks that `value` is an object with exactly the allowed keys and every required one.
void strictObject(const json &value, const std::string &path,
				  const std::vector<std::string> &required,
				  const std::vector<std::string> &optional = {})
{
	if (!value.is_object())
		schemaError(path, "must be an object");
	for (const auto &item : value.items())
		if (std::find(required.begin(), required.end(), item.key()) == required.end() &&
			std::find(optional.begin(), optional.end(), item.key()) == optional.end())
			schemaError(path + "/" + item.key(), "unknown property");
	for (const auto &key : required)
		if (!value.contains(key))
			schemaError(path + "/" + key, "is required");
}

std::int64_t integer(const json &value, const std::string &path, std::int64_t minimum,
					 std::int64_t maximum)
{
	if (!value.is_number_integer())
		schemaError(path, "must be an integer");
	if (value.is_number_unsigned() &&
		value.get<std::uint64_t>() > static_cast<std::uint64_t>(INT64_MAX))
		schemaError(path, "is out of range");
	const auto v = value.get<std::int64_t>();
	if (v < minimum || v > maximum)
		schemaError(path, "must be in " + std::to_string(minimum) + ".." + std::to_string(maximum));
	return v;
}

bool boolean(const json &value, const std::string &path)
{
	if (!value.is_boolean())
		schemaError(path, "must be a boolean");
	return value.get<bool>();
}

std::string string(const json &value, const std::string &path)
{
	if (!value.is_string())
		schemaError(path, "must be a string");
	return value.get<std::string>();
}

/// Unicode code points in valid UTF-8 (JSON Schema's maxLength counts these).
std::size_t codePoints(const std::string &text)
{
	std::size_t count = 0;
	for (unsigned char c : text)
		count += (c & 0xC0) != 0x80;
	return count;
}

bool matches(const std::string &text, const char *pattern)
{
	return std::regex_match(text, std::regex(pattern, std::regex::ECMAScript));
}

std::string hashString(const json &value, const std::string &path)
{
	const std::string text = string(value, path);
	Sha256::Digest ignored;
	if (!parseSha256Hex(text, ignored))
		schemaError(path, "must be 64 lowercase hex digits");
	return text;
}

std::string displayName(const json &value, const std::string &path)
{
	const std::string text = string(value, path);
	const std::size_t length = codePoints(text);
	if (length < 1 || length > MAX_NAME_BYTES)
		schemaError(path, "must be 1..32 characters");
	for (unsigned char c : text)
		if (c < 0x20 || c == 0x7f)
			schemaError(path, "must not contain control characters");
	return text;
}

std::string uuid(const json &value, const std::string &path)
{
	const std::string text = string(value, path);
	if (!matches(text, "^[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}$"))
		schemaError(path, "must be a lowercase UUID");
	return text;
}

GeneratorDescriptor parseGenerator(const json &value, const std::string &path)
{
	strictObject(value, path,
				 {"generatorId", "revision", "params", "seed", "candidates", "startingUnitLevel"});
	GeneratorDescriptor g;
	g.generatorId = string(value["generatorId"], path + "/generatorId");
	if (!matches(g.generatorId, "^[a-z0-9][a-z0-9._-]{0,63}$"))
		schemaError(path + "/generatorId", "must be a generator id");
	g.revision =
		static_cast<std::uint32_t>(integer(value["revision"], path + "/revision", 0, UINT32_MAX));
	const json &params = value["params"];
	if (!params.is_object())
		schemaError(path + "/params", "must be an object");
	for (const auto &item : params.items())
	{
		if (!matches(item.key(), "^[A-Za-z0-9_-]{1,64}$"))
			schemaError(path + "/params/" + item.key(), "invalid parameter name");
		if (!item.value().is_number_integer())
			schemaError(path + "/params/" + item.key(), "must be an integer");
		g.params[item.key()] =
			integer(item.value(), path + "/params/" + item.key(), INT64_MIN, INT64_MAX);
	}
	g.seed = static_cast<std::uint32_t>(integer(value["seed"], path + "/seed", 0, UINT32_MAX));
	g.candidates = static_cast<int>(integer(value["candidates"], path + "/candidates", 1, 64));
	g.startingUnitLevel =
		static_cast<int>(integer(value["startingUnitLevel"], path + "/startingUnitLevel", 0, 3));
	return g;
}

MapSource parseMap(const json &value, const std::string &path)
{
	if (!value.is_object() || !value.contains("kind") || !value["kind"].is_string())
		schemaError(path + "/kind", "must be catalog, upload or generated");
	const std::string kind = value["kind"].get<std::string>();
	MapSource map;
	if (kind == "catalog")
	{
		strictObject(value, path, {"kind", "hash"}, {"mapId"});
		map.kind = MapSource::Kind::Catalog;
		if (value.contains("mapId"))
			map.mapId = uuid(value["mapId"], path + "/mapId");
	}
	else if (kind == "upload")
	{
		strictObject(value, path, {"kind", "format", "hash"});
		map.kind = MapSource::Kind::Upload;
		const std::string format = string(value["format"], path + "/format");
		if (format == "map")
			map.format = MapSource::Format::Map;
		else if (format == "save")
			map.format = MapSource::Format::Save;
		else
			schemaError(path + "/format", "must be map or save");
	}
	else if (kind == "generated")
	{
		strictObject(value, path, {"kind", "generator", "hash"});
		map.kind = MapSource::Kind::Generated;
		map.generator = parseGenerator(value["generator"], path + "/generator");
	}
	else
		schemaError(path + "/kind", "must be catalog, upload or generated");
	map.hash = hashString(value["hash"], path + "/hash");
	return map;
}

MatchRules parseRules(const json &value, const std::string &path)
{
	strictObject(value, path,
				 {"prestigeVictory", "suddenDeathMinutes", "mapDiscovered", "allyTeamsFixed",
				  "resourceGrowthDisabled", "resourceScarcityLevel", "instantConstruction",
				  "stockpileStartLevel", "hungerDisabled", "unitUpgradesDisabled",
				  "glassCannonLevel", "unitsFearless", "permadeathDisabled", "peacefulMode",
				  "buildingHpLevel"},
				 {"buildingGradientDelay"});
	MatchRules r;
	auto b = [&](const char *key) { return boolean(value[key], path + "/" + key); };
	auto i = [&](const char *key, int max)
	{ return static_cast<int>(integer(value[key], path + "/" + key, 0, max)); };
	r.prestigeVictory = b("prestigeVictory");
	r.suddenDeathMinutes = i("suddenDeathMinutes", 1440);
	r.mapDiscovered = b("mapDiscovered");
	r.allyTeamsFixed = b("allyTeamsFixed");
	r.resourceGrowthDisabled = b("resourceGrowthDisabled");
	r.resourceScarcityLevel = i("resourceScarcityLevel", 3);
	r.instantConstruction = b("instantConstruction");
	r.stockpileStartLevel = i("stockpileStartLevel", 3);
	r.hungerDisabled = b("hungerDisabled");
	r.unitUpgradesDisabled = b("unitUpgradesDisabled");
	r.glassCannonLevel = i("glassCannonLevel", 2);
	r.unitsFearless = b("unitsFearless");
	r.permadeathDisabled = b("permadeathDisabled");
	r.peacefulMode = b("peacefulMode");
	r.buildingHpLevel = i("buildingHpLevel", 2);
	if (value.contains("buildingGradientDelay"))
	{
		r.buildingGradientDelay = i("buildingGradientDelay", 8);
		if (r.buildingGradientDelay != 2 && r.buildingGradientDelay != 4 &&
			r.buildingGradientDelay != 8)
			schemaError(path + "/buildingGradientDelay", "must be 2, 4 or 8");
	}
	return r;
}

SetupSeat parseSeat(const json &value, const std::string &path)
{
	if (!value.is_object() || !value.contains("kind") || !value["kind"].is_string())
		schemaError(path + "/kind", "must be human, ai or closed");
	const std::string kind = value["kind"].get<std::string>();
	SetupSeat seat;
	if (kind == "human")
	{
		strictObject(value, path, {"seat", "kind", "team", "name"}, {"accountId"});
		seat.human = true;
		if (value.contains("accountId"))
			seat.accountId = uuid(value["accountId"], path + "/accountId");
	}
	else if (kind == "ai")
	{
		strictObject(value, path, {"seat", "kind", "team", "name", "ai"}, {"aiConfig"});
		seat.human = false;
		seat.ai = string(value["ai"], path + "/ai");
		const auto &ids = MatchSetup::aiIds();
		if (std::find(ids.begin(), ids.end(), seat.ai) == ids.end())
			schemaError(path + "/ai", "unknown or unsupported AI \"" + seat.ai + "\"");
		if (value.contains("aiConfig"))
		{
			seat.aiConfig = string(value["aiConfig"], path + "/aiConfig");
			if (codePoints(*seat.aiConfig) > 4096)
				schemaError(path + "/aiConfig", "is longer than 4096 characters");
		}
	}
	else if (kind == "closed")
	{
		strictObject(value, path, {"seat", "kind", "team"});
		seat.human = false;
		seat.closed = true;
	}
	else
		schemaError(path + "/kind", "must be human, ai or closed");
	seat.seat = static_cast<int>(integer(value["seat"], path + "/seat", 0, MAX_TEAMS - 1));
	seat.team = static_cast<int>(integer(value["team"], path + "/team", 0, MAX_TEAMS - 1));
	if (!seat.closed)
		seat.name = displayName(value["name"], path + "/name");
	return seat;
}

AI::ImplementationID implementationOf(const std::string &ai)
{
	if (ai == "none")
		return AI::NONE;
	const int id = AINames::parseAIName(ai);
	if (id == AINames::AI_UNKNOWN_NAME || id == AI::JAVASCRIPT)
		semanticError("", "unsupported AI " + ai);
	return static_cast<AI::ImplementationID>(id);
}
} // namespace

const std::vector<std::string> &MatchSetup::aiIds()
{
	static const std::vector<std::string> ids = {"none",    "numbi",  "castor", "warrush", "econo",
												 "nicowar", "cortex", "maxima", "cabino"};
	return ids;
}

MatchSetup MatchSetup::fromJsonSchemaOnly(const json &value)
{
	strictObject(
		value, "",
		{"schemaVersion", "simVersion", "seed", "map", "teams", "seats", "rules", "experiments"},
		{"pauseLimit"});
	if (!value["schemaVersion"].is_number_integer() ||
		value["schemaVersion"].get<std::int64_t>() != SCHEMA_VERSION)
		schemaError("/schemaVersion", "must be " + std::to_string(SCHEMA_VERSION));
	MatchSetup setup;
	setup.simVersion = SimVersion::fromJson(value["simVersion"], "/simVersion");
	setup.seed = static_cast<std::uint32_t>(integer(value["seed"], "/seed", 0, UINT32_MAX));
	setup.map = parseMap(value["map"], "/map");

	const json &teams = value["teams"];
	if (!teams.is_array() || teams.empty() || teams.size() > MAX_TEAMS)
		schemaError("/teams", "must be an array of 1..12 teams");
	for (std::size_t i = 0; i < teams.size(); ++i)
	{
		const std::string path = "/teams/" + std::to_string(i);
		strictObject(teams[i], path, {"team", "alliance"});
		SetupTeam team;
		team.team = static_cast<int>(integer(teams[i]["team"], path + "/team", 0, MAX_TEAMS - 1));
		team.alliance =
			static_cast<int>(integer(teams[i]["alliance"], path + "/alliance", 0, MAX_TEAMS - 1));
		setup.teams.push_back(team);
	}

	const json &seats = value["seats"];
	if (!seats.is_array() || seats.empty() || seats.size() > MAX_TEAMS)
		schemaError("/seats", "must be an array of 1..12 seats");
	for (std::size_t i = 0; i < seats.size(); ++i)
		setup.seats.push_back(parseSeat(seats[i], "/seats/" + std::to_string(i)));

	setup.rules = parseRules(value["rules"], "/rules");

	const json &experiments = value["experiments"];
	if (!experiments.is_array() || experiments.size() > 64)
		schemaError("/experiments", "must be an array of at most 64 keys");
	std::set<std::string> seen;
	for (std::size_t i = 0; i < experiments.size(); ++i)
	{
		const std::string path = "/experiments/" + std::to_string(i);
		const std::string key = string(experiments[i], path);
		if (key.size() > 64 || !matches(key, "^[a-z0-9]+(-[a-z0-9]+)*$"))
			schemaError(path, "must be an experiment key");
		if (!seen.insert(key).second)
			schemaError(path, "is listed twice");
		setup.experiments.push_back(key);
	}
	if (value.contains("pauseLimit"))
	{
		const json &limit = value["pauseLimit"];
		strictObject(limit, "/pauseLimit", {"pauses", "seconds"});
		PauseLimit p;
		p.pauses = static_cast<int>(integer(limit["pauses"], "/pauseLimit/pauses", 0, 100));
		p.seconds = static_cast<int>(integer(limit["seconds"], "/pauseLimit/seconds", 0, 3600));
		setup.pauseLimit = p;
	}
	return setup;
}

void MatchSetup::validateSemantics() const
{
	if (rules.buildingGradientDelay != 2 && rules.buildingGradientDelay != 4 &&
		rules.buildingGradientDelay != 8)
		semanticError("/rules/buildingGradientDelay", "must be 2, 4 or 8");
	for (std::size_t i = 0; i < teams.size(); ++i)
		if (teams[i].team != static_cast<int>(i))
			semanticError("/teams/" + std::to_string(i) + "/team",
						  "teams must list team indices 0..n-1 in order; expected " +
							  std::to_string(i));
	std::set<std::string> accounts;
	for (std::size_t i = 0; i < seats.size(); ++i)
	{
		const std::string path = "/seats/" + std::to_string(i);
		const SetupSeat &seat = seats[i];
		if (seat.seat != static_cast<int>(i))
			semanticError(path + "/seat",
						  "seats must be numbered 0..k-1 in order; expected " + std::to_string(i));
		if (seat.team >= static_cast<int>(teams.size()))
			semanticError(path + "/team", "team " + std::to_string(seat.team) +
											  " is not one of the " + std::to_string(teams.size()) +
											  " teams");
		if (seat.name.size() > MAX_NAME_BYTES)
			semanticError(path + "/name", "name exceeds 32 UTF-8 bytes");
		if (seat.human && seat.accountId && !accounts.insert(*seat.accountId).second)
			semanticError(path + "/accountId", "an account may hold only one seat");
		if (!seat.human && !seat.closed)
			implementationOf(seat.ai);
	}
	// Closed seats follow every player seat, so players keep the numbers 0..p-1, and
	// each closes a different team that no player seat controls.
	const int players = playerCount();
	if (players == 0)
		semanticError("/seats", "a match needs at least one human or AI seat");
	std::set<int> closedTeams;
	for (std::size_t i = 0; i < seats.size(); ++i)
	{
		const SetupSeat &seat = seats[i];
		const std::string path = "/seats/" + std::to_string(i);
		if (!seat.closed)
		{
			if (static_cast<int>(i) >= players)
				semanticError(path, "closed seats must come after every human and AI seat");
			continue;
		}
		if (!teamClosed(seat.team))
			semanticError(path + "/team",
						  "team " + std::to_string(seat.team) + " is played by another seat");
		if (!closedTeams.insert(seat.team).second)
			semanticError(path + "/team", "team " + std::to_string(seat.team) + " is closed twice");
	}
	if (map.kind == MapSource::Kind::Generated && map.generator)
	{
		auto it = map.generator->params.find("teams");
		if (it != map.generator->params.end() &&
			it->second != static_cast<std::int64_t>(teams.size()))
			semanticError("/map/generator/params/teams",
						  "generator teams (" + std::to_string(it->second) +
							  ") must equal the number of setup teams (" +
							  std::to_string(teams.size()) + ")");
	}
	for (std::size_t i = 0; i < experiments.size(); ++i)
		if (!parseExperimentKey(experiments[i]))
			semanticError("/experiments/" + std::to_string(i),
						  "unknown experiment \"" + experiments[i] + "\"");
}

MatchSetup MatchSetup::fromJson(const json &value)
{
	MatchSetup setup = fromJsonSchemaOnly(value);
	setup.validateSemantics();
	return setup;
}

MatchSetup MatchSetup::parse(const std::string &text)
{
	json value;
	try
	{
		value = json::parse(text);
	}
	catch (const json::exception &error)
	{
		schemaError("", std::string("not valid JSON: ") + error.what());
	}
	return fromJson(value);
}

json MatchSetup::toJson() const
{
	json out = json::object();
	// nlohmann::json orders object keys alphabetically; dump() therefore has one
	// canonical form for a given setup, whatever order the input used.
	out["schemaVersion"] = SCHEMA_VERSION;
	out["simVersion"] = simVersion.toJson();
	out["seed"] = seed;
	json m = {{"hash", map.hash}};
	switch (map.kind)
	{
	case MapSource::Kind::Catalog:
		m["kind"] = "catalog";
		if (map.mapId)
			m["mapId"] = *map.mapId;
		break;
	case MapSource::Kind::Upload:
		m["kind"] = "upload";
		m["format"] = map.format == MapSource::Format::Save ? "save" : "map";
		break;
	case MapSource::Kind::Generated:
	{
		m["kind"] = "generated";
		const GeneratorDescriptor &g = *map.generator;
		json params = json::object();
		for (const auto &[key, v] : g.params)
			params[key] = v;
		m["generator"] = {{"generatorId", g.generatorId},
						  {"revision", g.revision},
						  {"params", params},
						  {"seed", g.seed},
						  {"candidates", g.candidates},
						  {"startingUnitLevel", g.startingUnitLevel}};
		break;
	}
	}
	out["map"] = m;
	out["teams"] = json::array();
	for (const auto &t : teams)
		out["teams"].push_back({{"team", t.team}, {"alliance", t.alliance}});
	out["seats"] = json::array();
	for (const auto &s : seats)
	{
		if (s.closed)
		{
			out["seats"].push_back({{"seat", s.seat}, {"kind", "closed"}, {"team", s.team}});
			continue;
		}
		json seat = {{"seat", s.seat},
					 {"kind", s.human ? "human" : "ai"},
					 {"team", s.team},
					 {"name", s.name}};
		if (s.human && s.accountId)
			seat["accountId"] = *s.accountId;
		if (!s.human)
		{
			seat["ai"] = s.ai;
			if (s.aiConfig)
				seat["aiConfig"] = *s.aiConfig;
		}
		out["seats"].push_back(seat);
	}
	const MatchRules &r = rules;
	out["rules"] = {{"prestigeVictory", r.prestigeVictory},
					{"suddenDeathMinutes", r.suddenDeathMinutes},
					{"mapDiscovered", r.mapDiscovered},
					{"allyTeamsFixed", r.allyTeamsFixed},
					{"resourceGrowthDisabled", r.resourceGrowthDisabled},
					{"resourceScarcityLevel", r.resourceScarcityLevel},
					{"instantConstruction", r.instantConstruction},
					{"stockpileStartLevel", r.stockpileStartLevel},
					{"hungerDisabled", r.hungerDisabled},
					{"unitUpgradesDisabled", r.unitUpgradesDisabled},
					{"glassCannonLevel", r.glassCannonLevel},
					{"unitsFearless", r.unitsFearless},
					{"permadeathDisabled", r.permadeathDisabled},
					{"peacefulMode", r.peacefulMode},
					{"buildingHpLevel", r.buildingHpLevel},
					{"buildingGradientDelay", r.buildingGradientDelay}};
	out["experiments"] = experiments;
	if (pauseLimit)
		out["pauseLimit"] = {{"pauses", pauseLimit->pauses}, {"seconds", pauseLimit->seconds}};
	return out;
}

std::string MatchSetup::dump() const
{
	return toJson().dump();
}

int MatchSetup::playerCount() const
{
	return static_cast<int>(
		std::count_if(seats.begin(), seats.end(), [](const SetupSeat &s) { return !s.closed; }));
}

bool MatchSetup::teamClosed(int team) const
{
	return std::none_of(seats.begin(), seats.end(),
						[team](const SetupSeat &s) { return !s.closed && s.team == team; });
}

std::uint32_t MatchSetup::humanSeatMask() const
{
	std::uint32_t mask = 0;
	for (const auto &s : seats)
		if (s.human)
			mask |= 1u << s.seat;
	return mask;
}

GameHeader MatchSetup::toGameHeader(const MapHeader &mapHeader) const
{
	const int mapTeams = mapHeader.getNumberOfTeams();
	if (mapTeams != static_cast<int>(teams.size()))
		throw MatchSetupError(Stage::Map, "/teams",
							  "the map has " + std::to_string(mapTeams) +
								  " teams but the setup lists " + std::to_string(teams.size()));
	const bool save = map.kind == MapSource::Kind::Upload && map.format == MapSource::Format::Save;
	if (mapHeader.getIsSavedGame() != save)
		throw MatchSetupError(Stage::Map, "/map",
							  save ? "the file is not a saved game" : "the file is a saved game");

	GameHeader header;
	for (int i = 0; i < Team::MAX_COUNT; ++i)
		header.getBasePlayer(i) = BasePlayer();
	// Closed seats come last and add no player: their teams have none, as a closed
	// colony in a custom game, and the engine clears them at the start.
	for (const SetupSeat &s : seats)
	{
		if (s.closed)
			continue;
		const auto type = s.human
							  ? BasePlayer::P_IP
							  : BasePlayer::playerTypeFromImplementationID(implementationOf(s.ai));
		header.getBasePlayer(s.seat) = BasePlayer(s.seat, s.name, s.team, type);
		header.setAIConfig(s.seat, (!s.human && s.aiConfig) ? *s.aiConfig : std::string());
	}
	header.setNumberOfPlayers(static_cast<Sint32>(playerCount()));
	for (int t = 0; t < Team::MAX_COUNT; ++t)
		header.setAllyTeamNumber(t, t < static_cast<int>(teams.size()) ? teams[t].alliance + 1
																	   : t + 1);
	header.setRandomSeed(seed);
	header.setAllyTeamsFixed(rules.allyTeamsFixed);
	header.setMapDiscovered(rules.mapDiscovered);
	header.getWinningConditions() = WinningCondition::getDefaultWinningConditions();
	WinningCondition::setPrestigeWinCondition(header.getWinningConditions(), rules.prestigeVictory);
	std::optional<Uint32> endStepTick;
	if (rules.suddenDeathMinutes != 0)
		endStepTick = static_cast<Uint32>(rules.suddenDeathMinutes) * 60 * GAME_TICKS_PER_SECOND;
	WinningCondition::setSuddenDeathWinCondition(header.getWinningConditions(), endStepTick);
	header.setResourceGrowthDisabled(rules.resourceGrowthDisabled);
	header.setResourceScarcityLevel(static_cast<Uint8>(rules.resourceScarcityLevel));
	header.setInstantConstructionEnabled(rules.instantConstruction);
	header.setStockpileStartLevel(static_cast<Uint8>(rules.stockpileStartLevel));
	header.setHungerDisabled(rules.hungerDisabled);
	header.setUnitUpgradesDisabled(rules.unitUpgradesDisabled);
	header.setGlassCannonLevel(static_cast<Uint8>(rules.glassCannonLevel));
	header.setUnitsFearless(rules.unitsFearless);
	header.setPermadeathDisabled(rules.permadeathDisabled);
	header.setPeacefulModeEnabled(rules.peacefulMode);
	header.setBuildingHpLevel(static_cast<Uint8>(rules.buildingHpLevel));
	header.setBuildingGradientDelay(rules.buildingGradientDelay);
	ExperimentSet experimentSet;
	for (const auto &key : experiments)
	{
		const auto id = parseExperimentKey(key);
		if (!id)
			semanticError("/experiments", "unknown experiment \"" + key + "\"");
		experimentSet.set(*id);
	}
	for (const auto& definition : experimentDefinitions())
		if (mapHeader.requiredTerrainExperiments.has(definition.id) && !experimentSet.has(definition.id))
			semanticError("/experiments", "missing map-required terrain experiment " + std::string(definition.key));
	header.setExperiments(experimentSet);
	return header;
}

MatchSetup MatchSetup::fromGameHeader(GameHeader header, const MapHeader &mapHeader,
									  const MapSource &source, const SimVersion &simVersion)
{
	MatchSetup setup;
	setup.simVersion = simVersion;
	setup.seed = header.getRandomSeed();
	setup.map = source;
	for (int t = 0; t < mapHeader.getNumberOfTeams(); ++t)
		setup.teams.push_back({t, std::max(0, static_cast<int>(header.getAllyTeamNumber(t)) - 1)});
	for (int p = 0; p < header.getNumberOfPlayers(); ++p)
	{
		const BasePlayer &bp = header.getBasePlayer(p);
		SetupSeat seat;
		seat.seat = p;
		seat.team = bp.teamNumber;
		seat.name = bp.name;
		seat.human = bp.type == BasePlayer::P_LOCAL || bp.type == BasePlayer::P_IP;
		if (!seat.human)
		{
			if (bp.type < BasePlayer::P_AI)
				semanticError("/seats/" + std::to_string(p), "player has no controller");
			const int id = BasePlayer::implementationIdFromPlayerType(bp.type);
			if (id == AI::JAVASCRIPT)
				semanticError("/seats/" + std::to_string(p) + "/ai",
							  "JavaScript AIs are not supported online");
			seat.ai = id == AI::NONE ? "none" : AINames::getCLIName(id);
			if (!header.getAIConfig(p).empty())
				seat.aiConfig = header.getAIConfig(p);
		}
		setup.seats.push_back(seat);
	}
	MatchRules &r = setup.rules;
	bool prestige = false;
	std::optional<Uint32> suddenDeath;
	std::vector<int> others;
	for (const auto &condition : header.getWinningConditions())
	{
		if (condition->getType() == WCPrestige)
			prestige = true;
		else if (condition->getType() == WCSuddenDeath)
			suddenDeath = static_cast<const WinningConditionSuddenDeath &>(*condition).endStepTick;
		else
			others.push_back(condition->getType());
	}
	std::vector<int> defaults;
	for (const auto &condition : WinningCondition::getDefaultWinningConditions())
		if (condition->getType() != WCPrestige)
			defaults.push_back(condition->getType());
	if (others != defaults)
		semanticError("/rules", "the winning conditions are not the standard set");
	r.prestigeVictory = prestige;
	if (suddenDeath)
	{
		const Uint32 perMinute = 60 * GAME_TICKS_PER_SECOND;
		if (*suddenDeath == 0 || *suddenDeath % perMinute != 0 || *suddenDeath / perMinute > 1440)
			semanticError("/rules/suddenDeathMinutes",
						  "sudden death is not a whole number of minutes");
		r.suddenDeathMinutes = static_cast<int>(*suddenDeath / perMinute);
	}
	r.mapDiscovered = header.isMapDiscovered();
	r.allyTeamsFixed = header.areAllyTeamsFixed();
	r.resourceGrowthDisabled = header.isResourceGrowthDisabled();
	r.resourceScarcityLevel = header.getResourceScarcityLevel();
	r.instantConstruction = header.isInstantConstructionEnabled();
	r.stockpileStartLevel = header.getStockpileStartLevel();
	r.hungerDisabled = header.isHungerDisabled();
	r.unitUpgradesDisabled = header.isUnitUpgradesDisabled();
	r.glassCannonLevel = header.getGlassCannonLevel();
	r.unitsFearless = header.isUnitsFearless();
	r.permadeathDisabled = header.isPermadeathDisabled();
	r.peacefulMode = header.isPeacefulModeEnabled();
	r.buildingHpLevel = header.getBuildingHpLevel();
	for (const auto& definition : experimentDefinitions())
		if (mapHeader.requiredTerrainExperiments.has(definition.id)) header.getExperiments().set(definition.id);
	r.buildingGradientDelay = header.getBuildingGradientDelay();
	setup.experiments = header.getExperiments().keys();
	setup.validateSemantics();
	return setup;
}

std::string mapContentHash(const std::string &path)
{
	std::unique_ptr<GAGCore::StreamBackend> backend(
		GAGCore::Toolkit::getFileManager()->openInflatingInputStreamBackend(path));
	if (!backend || !backend->isValid())
		return {};
	backend->seekFromEnd(0);
	const std::size_t size = backend->getPosition();
	backend->seekFromStart(0);
	std::string bytes(size, '\0');
	if (size && !backend->readExact(bytes.data(), size))
		return {};
	return toHex(Sha256::of(bytes));
}

std::string resolveMatchMap(const MatchSetup &setup, const std::string &candidate,
							const std::string &cacheDirectory)
{
	std::vector<std::string> paths;
	if (!candidate.empty())
		paths.push_back(candidate);
	else if (!cacheDirectory.empty())
		for (const char *suffix : {".map", ".map.gz", ".game", ".game.gz"})
			paths.push_back(cacheDirectory + "/" + setup.map.hash + suffix);
	for (const auto &path : paths)
	{
		const std::string hash = mapContentHash(path);
		if (hash.empty())
			continue;
		if (hash != setup.map.hash)
			throw MatchSetupError(Stage::Map, "/map/hash",
								  "the file " + path + " hashes to " + hash + ", not " +
									  setup.map.hash);
		const MapHeader header = Engine::loadMapHeader(path);
		const bool save = setup.map.kind == MapSource::Kind::Upload &&
						  setup.map.format == MapSource::Format::Save;
		if (header.getIsSavedGame() != save)
			throw MatchSetupError(Stage::Map, "/map",
								  save ? "the file is not a saved game"
									   : "the file is a saved game");
		return path;
	}
	throw MatchSetupError(Stage::Map, "/map/hash", "no map file with hash " + setup.map.hash);
}
} // namespace Online
