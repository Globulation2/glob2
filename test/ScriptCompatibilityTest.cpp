#include <Environment.h>
// SPDX-License-Identifier: GPL-3.0-or-later
#include "EngineFixtures.h"
#include "FileFormatVersions.h"
#include "ScopedEnvironment.h"
#include "AuthMessages.h"
#include "OrderMessages.h"
#include "Order.h"
#include "ReplayReader.h"
#include "Version.h"
#include "YOGClient.h"
#include <BinaryStream.h>
#include <StreamBackend.h>
#include <Toolkit.h>
#include <FileManager.h>
#include <cstdio>
#include <locale>
#include <sstream>
#include <TextStream.h>

TEST_CASE("JavaScript test environment scopes restore SDL and CRT readers" *
		  doctest::test_suite("JavaScriptCompatibility"))
{
	const auto original = glob2test::profileDir();
	const auto originalCRT = []() -> std::optional<std::string> {
		if (const char *value = std::getenv("GLOB2_USER_DATA_DIR"))
			return std::string(value);
		return std::nullopt;
	}();
	const auto currentCRT = [](const char *name) -> std::optional<std::string> {
		if (const char *value = std::getenv(name))
			return std::string(value);
		return std::nullopt;
	};
	const auto outer = original / "environment-outer";
	const auto inner = original / "environment-inner";
	{
		glob2test::ScopedEnvironment outerProfile("GLOB2_USER_DATA_DIR", outer.string().c_str());
		CHECK(glob2test::profileDir() == outer);
		CHECK(currentCRT("GLOB2_USER_DATA_DIR") == outer.string());
		{
			glob2test::ScopedEnvironment innerProfile("GLOB2_USER_DATA_DIR", inner.string().c_str());
			CHECK(glob2test::profileDir() == inner);
			CHECK(currentCRT("GLOB2_USER_DATA_DIR") == inner.string());
		}
		CHECK(glob2test::profileDir() == outer);
		CHECK(currentCRT("GLOB2_USER_DATA_DIR") == outer.string());
	}
	CHECK(glob2test::profileDir() == original);
	CHECK(currentCRT("GLOB2_USER_DATA_DIR") == originalCRT);

	// Restore an absent variable through SDL's own environment, including
	// Windows and SDL2-compat. An empty cached value is also inactive.
	const auto absent = "GLOB2_TEST_SCOPED_ENV_" + original.filename().string();
	REQUIRE(SDL_getenv_unsafe(absent.c_str()) == nullptr);
	REQUIRE(!currentCRT(absent.c_str()));
	const auto currentValue = [&] {
		const char *value = SDL_getenv_unsafe(absent.c_str());
		REQUIRE(value != nullptr);
		return std::string(value);
	};
	{
		glob2test::ScopedEnvironment outerValue(absent.c_str(), "outer");
		CHECK(currentValue() == "outer");
		CHECK(currentCRT(absent.c_str()) == "outer");
		{
			glob2test::ScopedEnvironment innerValue(absent.c_str(), "inner");
			CHECK(currentValue() == "inner");
			CHECK(currentCRT(absent.c_str()) == "inner");
		}
		CHECK(currentValue() == "outer");
		CHECK(currentCRT(absent.c_str()) == "outer");
	}
	const char *restored = SDL_getenv_unsafe(absent.c_str());
	CHECK((!restored || !*restored));
	CHECK(!currentCRT(absent.c_str()));
#ifdef _WIN32
	// A previous SDL-only write can leave the process and CRT views different.
	// Preserve both, rather than replacing one original with the other.
	REQUIRE(_putenv_s(absent.c_str(), "crt-original") == 0);
	REQUIRE(GAGCore::setProcessEnvironment(absent.c_str(), "sdl-original", 1) == 0);
	{
		glob2test::ScopedEnvironment value(absent.c_str(), "temporary");
		CHECK(currentValue() == "temporary");
		CHECK(currentCRT(absent.c_str()) == "temporary");
	}
	CHECK(currentValue() == "sdl-original");
	CHECK(currentCRT(absent.c_str()) == "crt-original");
	REQUIRE(_putenv_s(absent.c_str(), "") == 0);
	REQUIRE(GAGCore::setProcessEnvironment(absent.c_str(), "", 1) == 0);
#endif
}

TEST_CASE("Current clients enforce network protocol acceptance boundaries" *
		  doctest::test_suite("JavaScriptCompatibility"))
{
	CHECK(NET_PROTOCOL_VERSION == 51);
	CHECK(YOG_MIN_CLIENT_NET_PROTOCOL_VERSION == 51);
	// Exercise the production client handshake branch; transport remains
	// disconnected, and only the server-information message is injected.
	for (Uint16 version : {50, 51, 52})
	{
		CAPTURE(version);
		YOGClient client;
		client.connectionState = YOGClient::WaitingForServerInformation;
		auto info =
			std::make_shared<NetSendServerInformation>(YOGAnonymousLogin, YOGMultipleGames, 19);
		info->netVersion = version;
		client.nc.received.push(info);
		client.update();
		if (version == NET_PROTOCOL_VERSION)
		{
			CHECK(client.getConnectionState() == YOGClient::WaitingForLoginInformation);
			CHECK(client.getPlayerID() == 19);
		}
		else
		{
			CHECK(client.getConnectionState() == YOGClient::NotConnected);
			CHECK(client.getLoginState() == YOGClientVersionTooOld);
		}
	}
}

TEST_CASE("JavaScript current text saves validate unused generation counters" *
		  doctest::test_suite("JavaScriptCompatibility"))
{
	glob2test::HeadlessGlobals globals;
	GameGUI original;
	GAGCore::BinaryInputStream initial(glob2OpenMapOrSaveInputStreamBackend(
		*GAGCore::Toolkit::getFileManager(),
		glob2test::inflated("javascript/profile1-initial.game.gz").string()));
	REQUIRE(original.game.load(&initial));
	auto *storage = new GAGCore::MemoryStreamBackend;
	GAGCore::TextOutputStream output(storage);
	original.game.save(&output, false, "Counter validation");
	const auto text = storage->takeContents();
	const auto position = text.rfind("value = ");
	REQUIRE(position != std::string::npos);
	const auto end = text.find(';', position);
	REQUIRE(end != std::string::npos);
	const auto load = [](const std::string &bytes) {
		GAGCore::MemoryStreamBackend backend(bytes.data(), bytes.size());
		backend.seekFromStart(0);
		GAGCore::TextInputStream input(&backend);
		GameGUI restored;
		return restored.game.load(&input);
	};
	REQUIRE(load(text));
	for (const char *value : {"", "invalid", "-1", "4294967296", "0 trailing"})
	{
		CAPTURE(value);
		auto corrupted = text;
		corrupted.replace(position + 8, end - position - 8, value);
		CHECK_THROWS_AS(load(corrupted), std::runtime_error);
	}
	auto missing = text;
	missing.erase(position, end - position + 1);
	CHECK_THROWS_AS(load(missing), std::runtime_error);
}

TEST_CASE("Team-capacity change rejects released replays and enforces acceptance boundaries" *
		  doctest::test_suite("JavaScriptCompatibility"))
{
	glob2test::GlobalsOptions options;
	options.loadStrings = true;
	glob2test::HeadlessGlobals globals(options);
	CHECK(REPLAY_MINIMUM_VERSION_MINOR == 127);
	CHECK(VERSION_MINOR == 128);
	CHECK(FILE_FORMAT_VERSION_JAVASCRIPT == 125);
	CHECK(FILE_FORMAT_VERSION_EXPERIMENTS == 124);
	ReplayReader released;
	CHECK_FALSE(
		released.loadReplay(glob2test::inflated("javascript/released-v123.replay.gz").string()));
	for (Uint16 version : {122, 123, 124, 125, 126, 127, 128, 129})
	{
		CAPTURE(version);
		auto *memory = new GAGCore::MemoryStreamBackend;
		GAGCore::BinaryOutputStream output(memory);
		output.writeUint16(VERSION_MAJOR, "versionMajor");
		output.writeUint16(version, "versionMinor");
		output.writeUint32(0, "replayStepsSinceLastOrder");
		NetSendOrder(std::make_shared<NullOrder>()).encodeData(&output);
		auto bytes = memory->takeContents();
		auto *input = new GAGCore::BinaryInputStream(
			new GAGCore::MemoryStreamBackend(bytes.data(), bytes.size()));
		input->seekFromStart(0);
		ReplayReader reader;
		CHECK(reader.loadReplay(input, false) == (version >= REPLAY_MINIMUM_VERSION_MINOR && version <= VERSION_MINOR));
	}
}

TEST_CASE("JavaScript pass assigns valid identities to released saves" *
		  doctest::test_suite("JavaScriptCompatibility"))
{
	glob2test::HeadlessGlobals globals;
	CHECK(MINIMUM_VERSION_MINOR == 58);
	for (const char *fixture :
		 {"team-stats/version88.game.gz",
		  "team-stats/telemetry-expansion-validation/checkpoint-1024-v108.game.gz",
		  "ai-random-streams/numbi-castor-v121.game.gz"})
	{
		CAPTURE(fixture);
		GameGUI gui;
		GAGCore::BinaryInputStream input(glob2OpenMapOrSaveInputStreamBackend(
			*GAGCore::Toolkit::getFileManager(), glob2test::fixture(fixture).string()));
		REQUIRE(gui.game.load(&input));
		unsigned entities = 0;
		for (int team = 0; team < gui.game.teamsCount(); ++team)
		{
			for (int slot = 0; slot < Unit::MAX_COUNT; ++slot)
				if (auto *unit = gui.game.teams[team]->myUnits[slot])
				{
					++entities;
					CHECK(unit->scriptIdentity > 0);
					CHECK(unit->scriptIdentity ==
						  gui.game.scriptGenerations[Game::scriptGenerationIndex(false, team, Unit::GIDtoID(unit->gid))]);
				}
			for (int slot = 0; slot < Building::MAX_COUNT; ++slot)
				if (auto *building = gui.game.teams[team]->myBuildings[slot])
				{
					++entities;
					CHECK(building->scriptIdentity > 0);
					CHECK(building->scriptIdentity ==
						  gui.game.scriptGenerations[Game::scriptGenerationIndex(true, team, Building::GIDtoID(building->gid))]);
				}
		}
		CHECK(entities > 0);
	}
}

TEST_CASE("JavaScript upgrade retains genuine master124 experiments and continuation" *
		  doctest::test_suite("JavaScriptCompatibility"))
{
	glob2test::HeadlessGlobals globals;
	GameGUI legacy;
	GAGCore::BinaryInputStream input(glob2OpenMapOrSaveInputStreamBackend(
		*GAGCore::Toolkit::getFileManager(),
		glob2test::inflated("javascript/master-v124-experiments.game.gz").string()));
	REQUIRE(legacy.game.load(&input));
	REQUIRE(legacy.game.mapHeader.getVersionMinor() == 124);
	CHECK(legacy.game.gameHeader.getRandomSeed() == 19);
	REQUIRE(legacy.game.gameHeader.hasExperiment(ExperimentId::GuardAreaBalancing));
	CHECK(legacy.game.mapscript.getMapScriptMode() == MapScript::USL);
	const auto checkIdentities = [](const Game &game) {
		unsigned units = 0, buildings = 0;
		for (int team = 0; team < game.teamsCount(); ++team)
		{
			for (int slot = 0; slot < Unit::MAX_COUNT; ++slot)
				if (auto *unit = game.teams[team]->myUnits[slot])
				{
					++units;
					CHECK(unit->scriptIdentity > 0);
					CHECK(unit->scriptIdentity == game.scriptGenerations[Game::scriptGenerationIndex(false, team, slot)]);
				}
			for (int slot = 0; slot < Building::MAX_COUNT; ++slot)
				if (auto *building = game.teams[team]->myBuildings[slot])
				{
					++buildings;
					CHECK(building->scriptIdentity > 0);
					CHECK(building->scriptIdentity ==
						game.scriptGenerations[Game::scriptGenerationIndex(true, team, slot)]);
				}
		}
		CHECK(units > 0);
		CHECK(buildings > 0);
	};
	checkIdentities(legacy.game);
	legacy.game.map.finishGradientPipeline();
	auto *storage = new GAGCore::MemoryStreamBackend;
	GAGCore::BinaryOutputStream output(storage);
	legacy.game.save(&output, false, "Master124 upgraded");
	const auto checkpoint = storage->takeContents();
	glob2test::writeFile(glob2test::artifactDir() / "master124-upgraded.game", checkpoint);
	const auto identities = legacy.game.scriptGenerations;
	const auto initialTick = legacy.game.stepCounter;
	const auto continueGame = [&](Game &game, const char *name) {
		std::ostringstream records;
		records.imbue(std::locale::classic());
		for (unsigned tick = 0; tick < 64; ++tick)
		{
			CAPTURE(tick);
			std::vector<Uint32> world, buildings, units;
			game.checkSum(&world, &buildings, &units);
			REQUIRE(!world.empty());
			// MapHeader includes the on-disk format in its checksum. Its sole
			// field here is excluded for the 124 -> current comparison; retain all
			// remaining world, team, player, map and entity checksum fields.
			world.erase(world.begin());
			records << game.stepCounter;
			for (const auto *fields : {&world, &buildings, &units})
			{
				records << ' ' << fields->size();
				for (auto value : *fields) records << ' ' << value;
			}
			records << '\n';
			game.syncStep(-1);
		}
		CHECK(game.stepCounter == initialTick + 64);
		const auto result = records.str();
		glob2test::writeFile(glob2test::artifactDir() / name, result);
		return result;
	};
	const auto legacyTrace = continueGame(legacy.game, "master124-world-fields.value");
	// Reload resets the production RNG to the snapshot, rather than letting
	// the first continuation's process-global stream influence the second.
	GAGCore::BinaryInputStream upgradedInput(
		new GAGCore::MemoryStreamBackend(checkpoint.data(), checkpoint.size()));
	upgradedInput.seekFromStart(0);
	GameGUI upgraded;
	REQUIRE(upgraded.game.load(&upgradedInput));
	CHECK(upgraded.game.mapHeader.getVersionMinor() == VERSION_MINOR);
	CHECK(upgraded.game.gameHeader.getExperiments() == legacy.game.gameHeader.getExperiments());
	CHECK(upgraded.game.scriptGenerations == identities);
	checkIdentities(upgraded.game);
	const auto upgradedTrace = continueGame(upgraded.game, "upgraded-world-fields.value");
	CHECK(upgradedTrace == legacyTrace);
	CHECK(upgraded.game.scriptGenerations == legacy.game.scriptGenerations);
}

TEST_CASE("JavaScript current saves reject truncated generation tables" *
		  doctest::test_suite("JavaScriptCompatibility"))
{
	glob2test::HeadlessGlobals globals;
	const auto bytes = glob2test::readFile(
		glob2test::inflated("javascript/profile1-initial.game.gz"));
	GAGCore::BinaryInputStream complete(
		new GAGCore::MemoryStreamBackend(bytes.data(), bytes.size()));
	complete.seekFromStart(0);
	GameGUI original;
	REQUIRE(original.game.load(&complete));
	const auto counterEnd = complete.getPosition();
	REQUIRE(counterEnd > 4);
	// The last counter is unused and zero. Partial file reads previously
	// left zero bytes in place and bypassed the live-identity validation.
	for (size_t removed : {1, 2, 3, 4})
	{
		CAPTURE(removed);
		const auto truncated = bytes.substr(0, counterEnd - removed);
		const auto path = glob2test::artifactDir() /
			("truncated-generations-" + std::to_string(removed) + ".bin");
		glob2test::writeFile(path, truncated);
		for (bool file : {false, true})
		{
			CAPTURE(file);
			GAGCore::StreamBackend *backend;
			if (file)
			{
				auto *handle = std::fopen(path.string().c_str(), "rb");
				REQUIRE(handle);
				backend = new GAGCore::FileStreamBackend(handle);
			}
			else
				backend = new GAGCore::MemoryStreamBackend(truncated.data(), truncated.size());
			GAGCore::BinaryInputStream input(backend);
			input.seekFromStart(0);
			GameGUI target;
			CHECK_THROWS_AS(target.game.load(&input), std::ios_base::failure);
		}
	}
}
