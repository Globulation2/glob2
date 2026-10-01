// SPDX-License-Identifier: GPL-3.0-or-later
#include "EngineFixtures.h"
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
#include <TextStream.h>

TEST_CASE("JavaScript pass retains network protocol acceptance boundaries" *
		  doctest::test_suite("JavaScriptCompatibility"))
{
	CHECK(NET_PROTOCOL_VERSION == 47);
	// Exercise the production client handshake branch; transport remains
	// disconnected, and only the server-information message is injected.
	for (Uint16 version : {46, 47, 48})
	{
		CAPTURE(version);
		YOGClient client;
		client.connectionState = YOGClient::WaitingForServerInformation;
		auto info =
			std::make_shared<NetSendServerInformation>(YOGAnonymousLogin, YOGMultipleGames, 19);
		info->netVersion = version;
		client.nc.received.push(info);
		client.update();
		if (version == 47)
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

TEST_CASE("JavaScript pass retains released replay and acceptance boundaries" *
		  doctest::test_suite("JavaScriptCompatibility"))
{
	glob2test::GlobalsOptions options;
	options.loadStrings = true;
	glob2test::HeadlessGlobals globals(options);
	CHECK(REPLAY_MINIMUM_VERSION_MINOR == 123);
	CHECK(VERSION_MINOR == 124);
	ReplayReader released;
	REQUIRE(
		released.loadReplay(glob2test::inflated("javascript/released-v123.replay.gz").string()));
	CHECK(released.getNumStepsTotal() == 1500);
	for (Uint16 version : {122, 123, 124, 125})
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
		CHECK(reader.loadReplay(input, false) == (version == 123 || version == 124));
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
						  gui.game.scriptGenerations[team * 1024 + Unit::GIDtoID(unit->gid)]);
				}
			for (int slot = 0; slot < Building::MAX_COUNT; ++slot)
				if (auto *building = gui.game.teams[team]->myBuildings[slot])
				{
					++entities;
					CHECK(building->scriptIdentity > 0);
					CHECK(building->scriptIdentity ==
						  gui.game.scriptGenerations[Team::MAX_COUNT * 1024 + team * 1024 +
													 Building::GIDtoID(building->gid)]);
				}
		}
		CHECK(entities > 0);
	}
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
