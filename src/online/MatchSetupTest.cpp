// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 glob2 contributors
//
// Contract tests for MatchSetup -> GameHeader and the simulation version.
//
// The fixtures are the JSON contract of the platform's protocol package
// (platform/packages/protocol/fixtures, the source of truth). test/fixtures/protocol
// holds a copy of the MatchSetup and SimVersion entries so these tests run before the
// platform workspace is on this branch; once it is, the tests read the source and
// also check that the copy has not drifted from it.

#include "EngineTiming.h"
#include "EngineFixtures.h"

#include <algorithm>
#include <filesystem>
#include <nlohmann/json.hpp>
#include <set>

#include "AI.h"
#include "AINames.h"
#include "BasePlayer.h"
#include "BuildingType.h"
#include "ExperimentalFeatures.h"
#include "GameHeader.h"
#include "MapHeader.h"
#include "MatchSetup.h"
#include "Sha256.h"
#include "SimVersion.h"
#include "Version.h"
#include "WinningConditions.h"

namespace fs = std::filesystem;
using nlohmann::json;
using namespace Online;

namespace
{
fs::path fixtureRoot()
{
	const fs::path source = glob2test::sourceRoot() / "platform/packages/protocol/fixtures";
	if (fs::exists(source / "manifest.json"))
		return source;
	return glob2test::fixture("protocol");
}

struct Fixture
{
	std::string file, schema, stage;
	bool valid;
};

std::vector<Fixture> fixtures(const std::set<std::string>& schemas)
{
	const json manifest = json::parse(glob2test::readFile(fixtureRoot() / "manifest.json"));
	std::vector<Fixture> out;
	for (const auto& entry : manifest.at("fixtures"))
		if (schemas.count(entry.at("schema").get<std::string>()))
			out.push_back({entry.at("file").get<std::string>(), entry.at("schema").get<std::string>(),
			               entry.value("stage", ""), entry.at("valid").get<bool>()});
	return out;
}

MapHeader mapWithTeams(int teams, bool saved = false)
{
	MapHeader map;
	map.setNumberOfTeams(teams);
	map.setIsSavedGame(saved);
	return map;
}

bool hasCondition(GameHeader& header, WinningConditionType type)
{
	for (const auto& condition : header.getWinningConditions())
		if (condition->getType() == type)
			return true;
	return false;
}

std::string sha256Hex(const std::string& text)
{
	return toHex(Sha256::of(text));
}
}

TEST_SUITE("MatchSetup")
{
    TEST_CASE("New match rules default to eight tick AI decisions")
    {
        CHECK(MatchRules{}.aiOrderDelay == 8);
    }

    TEST_CASE("AI order delay is optional bounded integer data and round trips through headers")
    {
        glob2test::HeadlessGlobals globals;
        auto document=json::parse(glob2test::readFile(fixtureRoot()/"valid/MatchSetup/room-closed-seats.json"));
        document["rules"].erase("aiOrderDelay");
        const auto absent=MatchSetup::fromJson(document);
        CHECK(absent.rules.aiOrderDelay==0);
        CHECK(absent.toGameHeader(mapWithTeams(4)).getAIOrderDelay()==0);
        for(unsigned delay : {0u,8u}) {
            CAPTURE(delay);
            document["rules"]["aiOrderDelay"]=delay;
            const auto setup=MatchSetup::fromJson(document);
            CHECK(setup.rules.aiOrderDelay==delay);
            CHECK(MatchSetup::parse(setup.dump()).rules.aiOrderDelay==delay);
            auto map=mapWithTeams(4);
            auto header=setup.toGameHeader(map);
            CHECK(header.getAIOrderDelay()==delay);
            const auto restored=MatchSetup::fromGameHeader(header,map,setup.map,setup.simVersion);
            CHECK(restored.rules.aiOrderDelay==delay);
            CHECK(restored.toJson()["rules"]["aiOrderDelay"]==delay);
        }
        for(const auto& invalid : {json(-1),json(9),json(0.5),json(8.0),json("4"),json(true),json(nullptr)}) {
            CAPTURE(invalid);
            document["rules"]["aiOrderDelay"]=invalid;
            CHECK_THROWS_AS(MatchSetup::fromJsonSchemaOnly(document),MatchSetupError);
        }
    }

    TEST_CASE("Building gradient delay is optional bounded integer data and round trips through headers")
    {
        CHECK(MatchRules{}.buildingGradientDelay == 8);
        CHECK(MatchRules{}.buildingGradientDelay == int(GameHeader::DEFAULT_BUILDING_GRADIENT_DELAY));
        glob2test::HeadlessGlobals globals;
        auto document=json::parse(glob2test::readFile(fixtureRoot()/"valid/MatchSetup/room-closed-seats.json"));
        document["rules"].erase("buildingGradientDelay");
        const auto absent=MatchSetup::fromJson(document);
        CHECK(absent.rules.buildingGradientDelay==8);
        CHECK(absent.toGameHeader(mapWithTeams(4)).getBuildingGradientDelay()==8);
        for(unsigned delay : {1u,2u,4u}) {
            CAPTURE(delay);
            document["rules"]["buildingGradientDelay"]=delay;
            const auto setup=MatchSetup::fromJson(document);
            CHECK(setup.rules.buildingGradientDelay==delay);
            CHECK(MatchSetup::parse(setup.dump()).rules.buildingGradientDelay==delay);
            CHECK_FALSE(setup.rules==absent.rules);
            auto map=mapWithTeams(4);
            auto header=setup.toGameHeader(map);
            CHECK(header.getBuildingGradientDelay()==delay);
            const auto restored=MatchSetup::fromGameHeader(header,map,setup.map,setup.simVersion);
            CHECK(restored.rules.buildingGradientDelay==delay);
            CHECK(restored.toJson()["rules"]["buildingGradientDelay"]==delay);
        }
        for(const auto& invalid : {json(0),json(-1),json(9),json(1.5),json(4.0),json("4"),json(true),json(nullptr)}) {
            CAPTURE(invalid);
            document["rules"]["buildingGradientDelay"]=invalid;
            CHECK_THROWS_AS(MatchSetup::fromJsonSchemaOnly(document),MatchSetupError);
        }
    }

	TEST_CASE("embedded catalogs carry dynamic experiments independently of installed definitions")
	{
		glob2test::HeadlessGlobals globals;
		BuildingsTypes catalog;
		catalog.initLegacy();
		json snapshot = json::parse(catalog.snapshotJson());
		snapshot["experiments"].push_back({{"key", "catalog-fixture"}, {"label", "Fixture"}, {"help", "Fixture gate."}});
		catalog.loadSnapshotJson(snapshot.dump());
		json document = json::parse(glob2test::readFile(fixtureRoot() / "valid/MatchSetup/room-closed-seats.json"));
		document["buildingCatalog"] = {{"snapshot", catalog.snapshotJson()}, {"hash", catalog.fingerprint()}};
		document["experiments"] = {"catalog-fixture"};
		const MatchSetup setup = MatchSetup::fromJson(document);
		const MapHeader map = mapWithTeams(4);
		GameHeader header = setup.toGameHeader(map);
		CHECK(header.getBuildingCatalogSnapshot() == catalog.snapshotJson());
		CHECK(header.getExperiments().has("catalog-fixture"));
		CHECK(!knownExperimentKey("catalog-fixture"));
        const auto restored=MatchSetup::fromGameHeader(header,map,setup.map,setup.simVersion).toJson();
        // Account IDs and closed lobby slots are not simulation-header state.
        // Every durable setup field, especially embedded experiment identity,
        // must survive this conversion unchanged.
        auto durable=document;durable.erase("seats");
        auto restoredDurable=restored;restoredDurable.erase("seats");
        CHECK(restoredDurable==durable);
        REQUIRE(restored["seats"].size()==2);
        CHECK(restored["seats"][0]["name"]=="Alice");
        CHECK(restored["seats"][1]["team"]==2);
		document["buildingCatalog"]["hash"] = std::string(64, '0');
		CHECK_THROWS_AS(MatchSetup::fromJson(document), MatchSetupError);
		document["buildingCatalog"]["hash"] = catalog.fingerprint();
		document["buildingCatalog"]["snapshot"] = catalog.snapshotJson() + "\n";
		CHECK_THROWS_AS(MatchSetup::fromJson(document), MatchSetupError);
		document.erase("buildingCatalog");
		CHECK_THROWS_AS(MatchSetup::fromJson(document), MatchSetupError);
	}

	TEST_CASE("catalog rules identity partitions ratings without changing the executable identity")
	{
		const SimVersion engine{135, 55, std::string(64, 'a')};
		CHECK(catalogRulesVersion(engine, "") == engine);
		const auto first = catalogRulesVersion(engine, std::string(64, 'b'));
		CHECK(first.versionMinor == engine.versionMinor);
		CHECK(first.netProtocol == engine.netProtocol);
		CHECK(first.dataHash == sha256Hex("glob2-building-rules-v1\n" + engine.key() + "\n" + std::string(64, 'b')));
		CHECK(first != engine);
		CHECK(first != catalogRulesVersion(engine, std::string(64, 'c')));
		CHECK_THROWS_AS(catalogRulesVersion(engine, "bad-hash"), std::invalid_argument);
	}

	TEST_CASE("SHA-256 matches the FIPS 180-4 test vectors")
	{
		CHECK(sha256Hex("") == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
		CHECK(sha256Hex("abc") == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
		CHECK(sha256Hex("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq") ==
		      "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
		Sha256 million;
		const std::string chunk(1000, 'a');
		for (int i = 0; i < 1000; ++i)
			million.update(chunk);
		CHECK(toHex(million.finish()) == "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");
		Sha256::Digest digest;
		CHECK(parseSha256Hex(sha256Hex("abc"), digest));
		CHECK_FALSE(parseSha256Hex("BA7816BF8F01CFEA414140DE5DAE2223B00361A396177A9CB410FF61F20015AD", digest));
		CHECK_FALSE(parseSha256Hex("abc", digest));
	}

	TEST_CASE("every MatchSetup and SimVersion contract fixture is accepted or rejected at its stage")
	{
		glob2test::HeadlessGlobals globals;
		const auto cases = fixtures({"MatchSetup", "SimVersion"});
		REQUIRE(cases.size() >= 20);
		int valid = 0, schema = 0, semantic = 0;
		for (const auto& f : cases)
		{
			INFO(f.file);
			const std::string text = glob2test::readFile(fixtureRoot() / f.file);
			const json document = json::parse(text);
			if (f.schema == "SimVersion")
			{
				if (f.valid)
				{
					const SimVersion v = SimVersion::fromJson(document);
					CHECK(v.toJson() == document);
					SimVersion parsed;
					CHECK(SimVersion::parseKey(v.key(), parsed));
					CHECK(parsed == v);
					++valid;
				}
				else
				{
					CHECK_THROWS_AS(SimVersion::fromJson(document), MatchSetupError);
					++schema;
				}
				continue;
			}
			if (f.valid)
			{
				MatchSetup setup;
				CHECK_NOTHROW(setup = MatchSetup::parse(text));
				// Older setup documents omit the optional delays. Canonical output
				// spells out their engine defaults while preserving every other field.
				json canonical = document;
				if (!canonical["rules"].contains("aiOrderDelay"))
					canonical["rules"]["aiOrderDelay"] = 0;
				if (!canonical["rules"].contains("buildingGradientDelay"))
					canonical["rules"]["buildingGradientDelay"] = 8;
				CHECK(setup.toJson() == canonical);
				CHECK(MatchSetup::parse(setup.dump()).toJson() == canonical);
				++valid;
				continue;
			}
			try
			{
				MatchSetup::parse(text);
				FAIL("accepted an invalid fixture");
			}
			catch (const MatchSetupError& error)
			{
				INFO(error.what());
				if (f.stage == "schema")
				{
					CHECK(error.stage == MatchSetupError::Stage::Schema);
					++schema;
				}
				else
				{
					CHECK(f.stage == "semantic");
					CHECK(error.stage == MatchSetupError::Stage::Semantic);
					// The schema stage alone accepts it: the stages are distinct.
					CHECK_NOTHROW(MatchSetup::fromJsonSchemaOnly(document));
					++semantic;
				}
			}
		}
		CHECK(valid >= 4);
		CHECK(schema >= 10);
		CHECK(semantic >= 6);
	}

	TEST_CASE("the vendored fixtures match the protocol package when it is present")
	{
		const fs::path source = glob2test::sourceRoot() / "platform/packages/protocol/fixtures";
		if (!fs::exists(source / "manifest.json"))
			return; // the platform workspace is not on this branch yet
		const fs::path copy = glob2test::fixture("protocol");
		// The copy holds the MatchSetup and SimVersion cases only; other schemas may
		// gain fixtures without touching it.
		auto entries = [](const fs::path& root) {
			std::vector<json> out;
			// Keep the parsed manifest alive: a range-for over a member of a
			// temporary dangles (the temporary dies before the loop body runs).
			const json manifest = json::parse(glob2test::readFile(root / "manifest.json"));
			for (const auto& entry : manifest.at("fixtures"))
				if (entry.at("schema") == "MatchSetup" || entry.at("schema") == "SimVersion")
					out.push_back(entry);
			return out;
		};
		CHECK(entries(copy) == entries(source));
		for (const auto& entry : fs::recursive_directory_iterator(copy))
			if (entry.is_regular_file() && entry.path().filename() != "manifest.json")
			{
				const auto relative = fs::relative(entry.path(), copy);
				INFO(relative.generic_string());
				CHECK(glob2test::readFile(entry.path()) == glob2test::readFile(source / relative));
			}
	}

	TEST_CASE("valid setups become the GameHeader every client and the verifier run")
	{
		glob2test::HeadlessGlobals globals;
		for (const auto& f : fixtures({"MatchSetup"}))
		{
			if (!f.valid)
				continue;
			INFO(f.file);
			const MatchSetup setup = MatchSetup::parse(glob2test::readFile(fixtureRoot() / f.file));
			const bool save = setup.map.kind == MapSource::Kind::Upload && setup.map.format == MapSource::Format::Save;
			const MapHeader map = mapWithTeams(static_cast<int>(setup.teams.size()), save);
			GameHeader header = setup.toGameHeader(map);
			// Closed seats are not players: the players are the human and AI seats.
			REQUIRE(header.getNumberOfPlayers() == setup.playerCount());
			CHECK(header.getRandomSeed() == setup.seed);
			for (const auto& seat : setup.seats)
			{
				if (seat.closed)
				{
					// As a closed colony in a custom game: no player controls the team.
					CHECK(seat.seat >= setup.playerCount());
					for (int p = 0; p < header.getNumberOfPlayers(); ++p)
						CHECK(header.getBasePlayer(p).teamNumber != seat.team);
					continue;
				}
				const BasePlayer& bp = header.getBasePlayer(seat.seat);
				CHECK(bp.number == seat.seat);
				CHECK(bp.teamNumber == seat.team);
				CHECK(bp.name == seat.name);
				if (seat.human)
					CHECK(bp.type == BasePlayer::P_IP); // never P_LOCAL, on any client
				else
				{
					REQUIRE(bp.type >= BasePlayer::P_AI);
					const int id = BasePlayer::implementationIdFromPlayerType(bp.type);
					CHECK(seat.ai == (id == AI::NONE ? std::string("none") : AINames::getCLIName(id)));
					CHECK(header.getAIConfig(seat.seat) == seat.aiConfig.value_or(""));
				}
			}
			for (const auto& team : setup.teams)
				CHECK(header.getAllyTeamNumber(team.team) == team.alliance + 1);
			const MatchRules& r = setup.rules;
			CHECK(hasCondition(header, WCPrestige) == r.prestigeVictory);
			CHECK(hasCondition(header, WCSuddenDeath) == (r.suddenDeathMinutes != 0));
			for (const auto& condition : header.getWinningConditions())
				if (condition->getType() == WCSuddenDeath)
					CHECK(static_cast<const WinningConditionSuddenDeath&>(*condition).endStepTick ==
					      Uint32(r.suddenDeathMinutes) * 60 * GAME_TICKS_PER_SECOND);
			CHECK(header.isMapDiscovered() == r.mapDiscovered);
			CHECK(header.areAllyTeamsFixed() == r.allyTeamsFixed);
			CHECK(header.isResourceGrowthDisabled() == r.resourceGrowthDisabled);
			CHECK(header.getResourceScarcityLevel() == r.resourceScarcityLevel);
			CHECK(header.isInstantConstructionEnabled() == r.instantConstruction);
			CHECK(header.getStockpileStartLevel() == r.stockpileStartLevel);
			CHECK(header.isHungerDisabled() == r.hungerDisabled);
			CHECK(header.isUnitUpgradesDisabled() == r.unitUpgradesDisabled);
			CHECK(header.getGlassCannonLevel() == r.glassCannonLevel);
			CHECK(header.isUnitsFearless() == r.unitsFearless);
			CHECK(header.isPermadeathDisabled() == r.permadeathDisabled);
			CHECK(header.isPeacefulModeEnabled() == r.peacefulMode);
			CHECK(header.getBuildingHpLevel() == r.buildingHpLevel);
			CHECK(header.getExperiments().keys() == setup.experiments);

			// GameHeader -> MatchSetup recreates the setup, apart from account ids,
			// which a GameHeader does not carry, and closed seats: a team without a
			// player stays implicit, which means the same.
			MatchSetup back = MatchSetup::fromGameHeader(header, map, setup.map, setup.simVersion);
			MatchSetup expected = setup;
			expected.seats.erase(std::remove_if(expected.seats.begin(), expected.seats.end(),
			                                    [](const SetupSeat& s) { return s.closed; }),
			                     expected.seats.end());
			for (auto& seat : expected.seats)
				seat.accountId.reset();
			// Nor the pause limit: the turn session enforces it, not the game.
			expected.pauseLimit.reset();
			CHECK(back.playerCount() == setup.playerCount());
			for (const auto& team : setup.teams)
				CHECK(back.teamClosed(team.team) == setup.teamClosed(team.team));
			CHECK(back.toJson() == expected.toJson());

			// The map must have exactly the listed teams, and be a save exactly when
			// the source says so.
			CHECK_THROWS_AS(setup.toGameHeader(mapWithTeams(static_cast<int>(setup.teams.size()) + 1, save)),
			                MatchSetupError);
			CHECK_THROWS_AS(setup.toGameHeader(mapWithTeams(static_cast<int>(setup.teams.size()), !save)),
			                MatchSetupError);
		}
	}

	TEST_CASE("map-required terrain experiments propagate to setup and cannot be omitted")
	{
		glob2test::HeadlessGlobals globals;
		const auto document = glob2test::readFile(fixtureRoot() / "valid/MatchSetup/room-closed-seats.json");
		const MatchSetup setup = MatchSetup::parse(document);
		MapHeader map = mapWithTeams(4);
		GameHeader header = setup.toGameHeader(map);
		map.requiredTerrainExperiments.set(ExperimentId::IceTerrain);
		map.requiredTerrainExperiments.set(ExperimentId::TrailTerrain);
		CHECK_THROWS_AS(setup.toGameHeader(map),MatchSetupError);
		const MatchSetup required = MatchSetup::fromGameHeader(header,map,setup.map,setup.simVersion);
		const GameHeader restored = required.toGameHeader(map);
		CHECK(restored.hasExperiment(ExperimentId::IceTerrain));
		CHECK(restored.hasExperiment(ExperimentId::TrailTerrain));
	}

	TEST_CASE("closed seats close their team and follow every player seat")
	{
		glob2test::HeadlessGlobals globals;
		json document = json::parse(glob2test::readFile(fixtureRoot() / "valid/MatchSetup/room-closed-seats.json"));
		const MatchSetup setup = MatchSetup::parse(document.dump());
		REQUIRE(setup.seats.size() == 4);
		CHECK(setup.playerCount() == 2);
		CHECK(setup.humanSeatMask() == 0b11u);
		CHECK_FALSE(setup.teamClosed(0));
		CHECK(setup.teamClosed(1));
		CHECK_FALSE(setup.teamClosed(2));
		CHECK(setup.teamClosed(3));
		// The remaining players keep their map teams: seat 1 still plays team 2.
		GameHeader header = setup.toGameHeader(mapWithTeams(4));
		CHECK(header.getNumberOfPlayers() == 2);
		CHECK(header.getBasePlayer(1).teamNumber == 2);
		CHECK(header.getAllyTeamNumber(3) == setup.teams[3].alliance + 1);

		auto stageOf = [](const json& value) {
			try
			{
				MatchSetup::parse(value.dump());
			}
			catch (const MatchSetupError& error)
			{
				return error.stage == MatchSetupError::Stage::Schema ? std::string("schema") : error.path;
			}
			return std::string("accepted");
		};
		// A closed seat has no name or AI.
		json named = document;
		named["seats"][2]["name"] = "Nobody";
		CHECK(stageOf(named) == "schema");
		// Every player seat comes before the closed ones.
		json early = document;
		std::swap(early["seats"][1], early["seats"][2]);
		early["seats"][1]["seat"] = 1;
		early["seats"][2]["seat"] = 2;
		CHECK(stageOf(early) == "/seats/2");
		// A team a player controls cannot be closed, nor a team closed twice.
		json played = document;
		played["seats"][2]["team"] = 0;
		CHECK(stageOf(played) == "/seats/2/team");
		json twice = document;
		twice["seats"][3]["team"] = 1;
		CHECK(stageOf(twice) == "/seats/3/team");
		// A match needs a player.
		json nobody = document;
		nobody["seats"] = json::array({{{"seat", 0}, {"kind", "closed"}, {"team", 0}}});
		CHECK(stageOf(nobody) == "/seats");
	}

	TEST_CASE("unknown experiments, JavaScript AIs and unexpressible headers are refused")
	{
		glob2test::HeadlessGlobals globals;
		json document = json::parse(glob2test::readFile(fixtureRoot() / "valid/MatchSetup/catalog-1v1.json"));
		json unknown = document;
		unknown["experiments"] = {"no-such-experiment"};
		try
		{
			MatchSetup::parse(unknown.dump());
			FAIL("accepted an unknown experiment");
		}
		catch (const MatchSetupError& error)
		{
			CHECK(error.stage == MatchSetupError::Stage::Semantic);
			CHECK(error.path == "/experiments/0");
		}
		for (const auto& definition : experimentDefinitions())
		{
			json known = document;
			known["experiments"] = {definition.key};
			CHECK_NOTHROW(MatchSetup::parse(known.dump()));
		}

		const MatchSetup setup = MatchSetup::parse(document.dump());
		const MapHeader map = mapWithTeams(2);
		GameHeader header = setup.toGameHeader(map);
		header.getBasePlayer(1).type = BasePlayer::playerTypeFromImplementationID(AI::JAVASCRIPT);
		CHECK_THROWS_AS(MatchSetup::fromGameHeader(header, map, setup.map, setup.simVersion), MatchSetupError);
		header = setup.toGameHeader(map);
		WinningCondition::setSuddenDeathWinCondition(header.getWinningConditions(), 1234);
		CHECK_THROWS_AS(MatchSetup::fromGameHeader(header, map, setup.map, setup.simVersion), MatchSetupError);
		header = setup.toGameHeader(map);
		header.getWinningConditions().pop_front();
		CHECK_THROWS_AS(MatchSetup::fromGameHeader(header, map, setup.map, setup.simVersion), MatchSetupError);
		// Human seats map to P_IP whatever the source header used.
		header = setup.toGameHeader(map);
		header.getBasePlayer(0).type = BasePlayer::P_LOCAL;
		const MatchSetup fromLocal = MatchSetup::fromGameHeader(header, map, setup.map, setup.simVersion);
		CHECK(fromLocal.seats[0].human);
		CHECK(fromLocal.toGameHeader(map).getBasePlayer(0).type == BasePlayer::P_IP);
	}

	TEST_CASE("a map file is found and checked by the hash of its decompressed bytes")
	{
		glob2test::HeadlessGlobals globals;
		const std::string path = (glob2test::sourceRoot() / "maps/SmallForTwo.map.gz").string();
		const std::string hash = mapContentHash(path);
		REQUIRE(hash.size() == 64);
		const fs::path inflated = glob2test::inflated("maps/SmallForTwo.map.gz");
		CHECK(mapContentHash(inflated.string()) == hash);
		CHECK(hash == toHex(Sha256::of(glob2test::readFile(inflated))));

		MatchSetup setup = MatchSetup::parse(glob2test::readFile(fixtureRoot() / "valid/MatchSetup/catalog-1v1.json"));
		setup.map.hash = hash;
		CHECK(resolveMatchMap(setup, path) == path);
		glob2test::TempDir cache("map-cache");
		fs::copy_file(path, cache.path / (hash + ".map.gz"));
		// Compared as paths: Windows accepts either separator in the returned name.
		CHECK(fs::path(resolveMatchMap(setup, "", cache.path.string())) == cache.path / (hash + ".map.gz"));
		setup.map.hash = std::string(64, '0');
		CHECK_THROWS_AS(resolveMatchMap(setup, path), MatchSetupError);
		CHECK_THROWS_AS(resolveMatchMap(setup, "", cache.path.string()), MatchSetupError);
	}

	TEST_CASE("the simulation data hash covers every data file the simulation loads")
	{
		glob2test::HeadlessGlobals globals;
		// The list is complete: every strategy, Nicowar and USL runtime file.
		std::set<std::string> onDisk;
		const fs::path root = glob2test::sourceRoot();
		for (const char* directory : {"data/maxima", "data/usl/Language/Runtime", "data/usl/Glob2/Runtime"})
			for (const auto& entry : fs::directory_iterator(root / directory))
			{
				const auto extension = entry.path().extension().string();
				if (extension == ".strategy" || extension == ".usl")
					onDisk.insert(fs::relative(entry.path(), root).generic_string());
			}
		onDisk.insert("data/nicowar.default.txt");
		onDisk.insert("data/nicowar.txt");
		onDisk.insert("data/buildings/manifest.json");
        onDisk.insert("data/resources/registry.json");
		const auto buildingManifest = json::parse(glob2test::readFile(root / "data/buildings/manifest.json"));
		for (const auto& name : buildingManifest.at("files"))
			onDisk.insert("data/buildings/" + name.get<std::string>());
		const auto& listed = simDataFiles();
		CHECK(std::set<std::string>(listed.begin(), listed.end()) == onDisk);
		CHECK(std::is_sorted(listed.begin(), listed.end()));

		// The encoding: line endings are normalised, and missing differs from empty.
		CHECK(simDataHashOf({{"a", "x\r\ny\n"}}) == simDataHashOf({{"a", "x\ny\n"}}));
		CHECK(simDataHashOf({{"a", "x\ry\n"}}) != simDataHashOf({{"a", "x\ny\n"}}));
		CHECK(simDataHashOf({{"a", "", false}}) != simDataHashOf({{"a", ""}}));
		CHECK(simDataHashOf({{"a", "1"}, {"b", "2"}}) != simDataHashOf({{"a", "12"}, {"b", ""}}));
		std::string expected;
		{
			Sha256 h;
			h.update(std::string("a", 1));
			const std::uint8_t encoding[] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 'x'};
			h.update(encoding, sizeof encoding);
			expected = toHex(h.finish());
		}
		CHECK(simDataHashOf({{"a", "x"}}) == expected);

		// This build's version: the real files, and the same value every time.
		// SIM_REVISION is hashed first, as a pseudo-file, so a bump changes the key.
		CHECK(simRevisionEntry(1).path == "#sim-revision");
		CHECK(simRevisionEntry(12).content == "12");
		CHECK(simRevisionEntry().content == std::to_string(SIM_REVISION));
		CHECK(simRevisionEntry().path < listed.front());
		CHECK(simDataHashOf({simRevisionEntry(1), {"a", "x"}}) != simDataHashOf({simRevisionEntry(2), {"a", "x"}}));
		std::vector<SimDataFile> files{simRevisionEntry()};
		for (const auto& path : listed)
			files.push_back({path, glob2test::readFile(root / path)});
		const SimVersion version = currentSimVersion();
		CHECK(version.versionMinor == VERSION_MINOR);
		CHECK(version.netProtocol == NET_PROTOCOL_VERSION);
		CHECK(version.dataHash == simDataHashOf(files));
		CHECK(currentSimVersion() == version);
		SimVersion parsed;
		REQUIRE(SimVersion::parseKey(version.key(), parsed));
		CHECK(parsed == version);
		CHECK_FALSE(SimVersion::parseKey("125-49-ABC", parsed));
		CHECK_FALSE(SimVersion::parseKey("125-" + version.dataHash, parsed));
		MESSAGE("sim version " << version.key());
	}
}

TEST_CASE("Scripted map sources preserve exact provenance and enforce team count" *
		  doctest::test_suite("MatchSetup"))
{
	auto document =
		json::parse(glob2test::readFile(fixtureRoot() / "valid/MatchSetup/room-closed-seats.json"));
	const auto count = document["teams"].size();
	document["map"] = {{"kind", "scripted"},
					   {"hash", std::string(64, 'a')},
					   {"chosenSeed", 91},
					   {"generator",
						{{"libraryId", "11111111-1111-4111-8111-111111111111"},
						 {"versionId", "22222222-2222-4222-8222-222222222222"},
						 {"packageHash", std::string(64, 'b')},
						 {"fileHash", std::string(64, 'c')},
						 {"generatorId", "author:landscape"},
						 {"revision", 2},
						 {"seed", 19},
						 {"candidates", 1},
						 {"startingUnitLevel", 0},
						 {"params", {{"teams", count}, {"width", 7}, {"height", 7}}}}}};
	const auto setup = Online::MatchSetup::fromJson(document);
	CHECK(setup.map.kind == Online::MapSource::Kind::Scripted);
	CHECK(setup.toJson()["map"] == document["map"]);
	document["map"]["generator"]["params"]["teams"] = count + 1;
	CHECK_THROWS(Online::MatchSetup::fromJson(document));
}
