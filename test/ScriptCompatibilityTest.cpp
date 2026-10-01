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
