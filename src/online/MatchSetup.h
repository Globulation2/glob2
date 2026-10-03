// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 glob2 contributors

#pragma once

// MatchSetup: the engine-independent JSON description of a match
// (platform/packages/protocol, src/matchSetup.ts, is the source of truth for its
// shape). This file turns it into the GameHeader every client and the verifier run,
// so all of them build the same game from the same JSON:
//
//   MatchSetup::parse(json)  schema checks, then the cross-field ("semantic") rules
//   resolveMatchMap(...)     the map file whose decompressed bytes hash to map.hash
//   toGameHeader(map)        the GameHeader, checked against the map's team count
//
// Every human seat becomes BasePlayer::P_IP on every client and in the verifier, so
// the heavy checksum the engine enables for network games (Game::checkSum) is the
// same everywhere; the local seat is chosen with Engine's localPlayer, not the type.

#include <nlohmann/json_fwd.hpp>

#include <array>
#include <cstdint>
#include <map>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include "SimVersion.h"

class GameHeader;
class MapHeader;

namespace Online
{
	class MatchSetupError : public std::runtime_error
	{
	public:
		/// Schema: the JSON Schema in the protocol package would reject the document.
		/// Semantic: it passes the schema but breaks a cross-field rule
		/// (matchSetupProblems in the protocol package, plus unknown experiments).
		/// Map: the setup does not fit the map it names (hash, team count, format).
		enum class Stage { Schema, Semantic, Map };

		MatchSetupError(Stage stage, std::string path, const std::string& message);
		Stage stage;
		std::string path; ///< JSON pointer of the offending value
	};

	struct MatchRules
	{
		bool prestigeVictory = true;
		int suddenDeathMinutes = 0; ///< 0..1440; 0 disables the timer
		bool mapDiscovered = false;
		bool allyTeamsFixed = true;
		bool resourceGrowthDisabled = false;
		int resourceScarcityLevel = 0; ///< 0..3
		bool instantConstruction = false;
		int stockpileStartLevel = 0; ///< 0..3
		bool hungerDisabled = false;
		bool unitUpgradesDisabled = false;
		int glassCannonLevel = 0; ///< 0..2
		bool unitsFearless = false;
		bool permadeathDisabled = false;
		bool peacefulMode = false;
		int buildingHpLevel = 0; ///< 0..2

		bool operator==(const MatchRules& o) const;
	};

	struct GeneratorDescriptor
	{
		std::string generatorId;
		std::uint32_t revision = 0;
		std::map<std::string, std::int64_t> params;
		std::uint32_t seed = 0;
		int candidates = 1;
		int startingUnitLevel = 0;
		bool operator==(const GeneratorDescriptor& o) const;
	};

	struct MapSource
	{
		enum class Kind { Catalog, Upload, Generated };
		enum class Format { Map, Save };
		Kind kind = Kind::Catalog;
		std::string hash; ///< SHA-256 of the decompressed map bytes, lowercase hex
		std::optional<std::string> mapId;           ///< catalog only
		Format format = Format::Map;                 ///< upload only; Map otherwise
		std::optional<GeneratorDescriptor> generator; ///< generated only
		bool operator==(const MapSource& o) const;
	};

	struct SetupTeam
	{
		int team = 0;
		int alliance = 0; ///< GameHeader ally-team number is alliance + 1
	};

	struct SetupSeat
	{
		int seat = 0;
		bool human = true;
		int team = 0;
		std::string name;
		std::optional<std::string> accountId; ///< human only
		std::string ai = "none";              ///< AI only: CLI name (AINames.cpp)
		std::optional<std::string> aiConfig;  ///< AI only: GameHeader::setAIConfig
	};

	struct MatchSetup
	{
		static constexpr int SCHEMA_VERSION = 1;
		/// The AI ids MatchSetup accepts: AINames CLI names plus "none". JavaScript
		/// controllers are not accepted online yet.
		static const std::vector<std::string>& aiIds();

		SimVersion simVersion;
		std::uint32_t seed = 0;
		MapSource map;
		std::vector<SetupTeam> teams;
		std::vector<SetupSeat> seats;
		MatchRules rules;
		std::vector<std::string> experiments;

		/// Parses and fully validates (schema, then semantic); throws MatchSetupError.
		static MatchSetup parse(const std::string& json);
		static MatchSetup fromJson(const nlohmann::json& value);
		/// Only the schema stage, for tests that need to tell the stages apart.
		static MatchSetup fromJsonSchemaOnly(const nlohmann::json& value);
		/// The semantic rules; throws MatchSetupError(Semantic).
		void validateSemantics() const;

		nlohmann::json toJson() const;
		std::string dump() const; ///< compact, keys in schema order

		/// Bit s set for every human seat s: the relay's humanSeatMask.
		std::uint32_t humanSeatMask() const;

		/// The GameHeader for this match on the given map (its header, as loaded from
		/// the file resolveMatchMap found). Throws MatchSetupError(Map) when the team
		/// count or the file kind (map or save) does not match the setup.
		///
		/// For a save source, the seats replace every saved player record: each seat
		/// takes control of its team as saved (a seat may name any saved team, which is
		/// how reteaming works), AI seats start fresh AIs of the given kind, and teams
		/// no seat controls are cleared as on a new map. The rules, seed and
		/// experiments come from the setup like any other match; to continue a save
		/// unchanged, build the setup with fromGameHeader() from the save's header.
		GameHeader toGameHeader(const MapHeader& map) const;

		/// The setup that recreates `header` on `map` (teams, alliances, seats, rules,
		/// seed, experiments). Human players (P_LOCAL or P_IP) become human seats.
		/// Throws MatchSetupError(Semantic) for what MatchSetup cannot express: a
		/// JavaScript AI, a winning-condition list other than the default one with
		/// prestige and sudden death toggled, or a sudden-death tick that is not a
		/// whole number of minutes.
		static MatchSetup fromGameHeader(GameHeader header, const MapHeader& map, const MapSource& source,
		                                 const SimVersion& simVersion);
	};

	/// SHA-256 (lowercase hex) of the decompressed bytes of a map or save file, read
	/// through the Toolkit FileManager (a ".gz" file is inflated first). Empty if the
	/// file cannot be read.
	std::string mapContentHash(const std::string& path);

	/// Finds the map file for a setup: `candidate` when given, else the first of
	/// <cacheDirectory>/<hash>.map, .map.gz, .game, .game.gz that exists. Checks that
	/// its content hash equals setup.map.hash and that it is a save exactly when the
	/// source is an uploaded save. Throws MatchSetupError(Map) otherwise.
	std::string resolveMatchMap(const MatchSetup& setup, const std::string& candidate,
	                            const std::string& cacheDirectory = std::string());
}
