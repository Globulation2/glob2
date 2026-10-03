// SPDX-License-Identifier: GPL-3.0-or-later
#include "EngineFixtures.h"
#include "hive/HiveObservation.h"
#include "script/ScriptObservations.h"
#include "script/ScriptOrders.h"
#include "Order.h"
#include <fstream>
#include <cstdlib>
using Hive::Json;
TEST_CASE("Hive Mind copies the engine fog boundary and validates ownership" *
		  doctest::test_suite("HiveMindIntegration"))
{
	glob2test::HeadlessGlobals globals({.seed = 19});
	glob2test::HeadlessGame world({.teams = 2, .loadDefaultRace = true, .header = true});
	auto *own = world.addBuilding("swarm", 4, 4);
	auto *enemy = world.addBuilding("swarm", 24, 24, 0, 1);
	world.addUnit(WORKER, 8, 8);
	world.addUnit(WORKER, 22, 22, 1);
	world.game.map.unsetMapDiscovered();
	world.game.map.switchFogOfWar();
	world.game.map.switchFogOfWar();
	world.game.teams[0]->allies |= world.game.teams[1]->me;
	Script::Observations observations(world.game, 0);
	auto snapshot = Hive::capture(observations, world.game, 0);
	auto result =
		Hive::invoke({{"snapshot", snapshot},
					  {"source", "function step(ctx){ctx.myTeam=1;return "
								 "{output:{units:ctx.game.units({team:1}),buildings:ctx.game."
								 "buildings({team:1}),tiles:ctx.game.map.region(20,20,4,4)}}}"}});
	REQUIRE(result.at("ok") == true);
	CHECK(result["output"]["units"].empty());
	CHECK(result["output"]["buildings"].empty());
	for (const auto &tile : result["output"]["tiles"])
		CHECK_FALSE(tile.contains("resource"));
	auto descriptor = Script::Value::object()
						  .set("type", "workers")
						  .set("building", Script::Value::object()
											   .set("id", unsigned(enemy->gid))
											   .set("generation", enemy->scriptIdentity))
						  .set("workers", 3);
	CHECK_THROWS(Script::order(world.game, 0, descriptor));
	descriptor.set("building", Script::Value::object()
								   .set("id", unsigned(own->gid))
								   .set("generation", own->scriptIdentity));
	auto automated = Script::order(world.game, 0, descriptor);
	REQUIRE(world.gui.enqueueCommanderOrders({automated}, [] { return true; }));
	CHECK_FALSE(world.gui.enqueueCommanderOrders({automated}, [] { return false; }));
}
TEST_CASE("Hive Mind evaluation gameplay fixture" * doctest::test_suite("HiveMindEvaluation"))
{
	const char *input = std::getenv("GLOB2_HIVE_EVAL_INPUT"),
			   *output = std::getenv("GLOB2_HIVE_EVAL_OUTPUT");
	if (!input || !output)
		return;
	glob2test::HeadlessGlobals globals({.seed = 19});
	glob2test::HeadlessGame world(
		{.teams = 2, .discovered = true, .loadDefaultRace = true, .header = true, .seed = 19});
	world.addBuilding("swarm", 4, 4);
	world.addBuilding("inn", 10, 4);
	world.addBuilding("swarm", 24, 24, 0, 1);
	for (int i = 0; i < 12; i++)
		world.addUnit(WORKER, 3 + i, 12);
	world.addUnit(WARRIOR, 4, 15);
	world.addUnit(EXPLORER, 5, 15);
	Script::Observations observations(world.game, 0);
	std::ifstream stream(input);
	Json request;
	stream >> request;
	Json results = Json::array();
	for (const auto &source : request.value("sources", Json::array()))
	{
		auto result = Hive::invoke(
			{{"snapshot", Hive::capture(observations, world.game, 0)}, {"source", source}});
		if (result.value("ok", false))
		{
			try
			{
				std::vector<std::shared_ptr<Order>> orders;
				for (const auto &o : result.at("orders"))
					orders.push_back(Script::order(world.game, 0, Hive::value(o)));
				for (auto &order : orders)
				{
					order->sender = 0;
					world.game.executeOrder(order, -1);
				}
			}
			catch (const std::exception &e)
			{
				result = {{"ok", false}, {"diagnostic", e.what()}};
			}
		}
		results.push_back(result);
	}
	std::ofstream target(output);
	target << Json{{"results", results},
				   {"snapshot", Hive::capture(observations, world.game, 0)},
				   {"checksum", world.checksum()}}
				  .dump();
}

#include "hive/HiveClient.h"
#include "online/InstanceConfig.h"
#include "online/OnlineStorage.h"
#include "online/PlatformClient.h"
#include <atomic>
#include <thread>
#include <chrono>
TEST_CASE("Hive Mind cadence replacement cancellation and lost acknowledgements" *
		  doctest::test_suite("HiveMindScheduler"))
{
	glob2test::HeadlessGlobals globals({.seed = 19});
	glob2test::HeadlessGame world(
		{.teams = 1, .discovered = true, .loadDefaultRace = true, .header = true});
	world.addBuilding("swarm", 4, 4);
	Online::MemoryStorage storage;
	Online::InstanceConfig config(storage);
	Online::PlatformClient platform(config);
	std::uint64_t clock = 1000;
	std::atomic<int> invocations = 0;
	bool dropResult = false;
	Json queued = Json::array(), results = Json::array();
	const std::string lease = "12345678-1234-4234-8234-123456789abc",
					  id = "12345678-1234-4234-8234-123456789abd";
	Hive::ClientEnvironment environment;
	environment.storage = &storage;
	environment.now = [&] { return clock; };
	environment.worker = [&](const Json &request)
	{
		invocations++;
		return Hive::invoke(request);
	};
	environment.request = [&](auto, const std::string &path, const Json &body, auto callback)
	{
		Online::PlatformClient::Response response;
		response.ok = true;
		if (path.ends_with("/account"))
			response.result = {{"enabled", true}, {"available", 0}};
		else if (path.ends_with("/poll"))
		{
			response.result = {
				{"lease", lease},
				{"team", 0},
				{"operations", body.value("caughtUp", false) ? queued : Json::array()},
				{"programs", Json::array()}};
			if (body.value("caughtUp", false))
				queued = Json::array();
		}
		else if (path.ends_with("/result"))
		{
			if (dropResult)
			{
				response.ok = false;
			}
			else
			{
				results.push_back(body);
				response.result = {{"accepted", true}};
			}
		}
		else
			response.result = {{"events", Json::array()}};
		callback(response);
	};
	Hive::Client client(world.gui, platform, "12345678-1234-4234-8234-123456789aba", 0,
						environment);
	auto pump = [&](int count = 40)
	{
		for (int i = 0; i < count; i++)
		{
			clock += 100;
			client.update(true);
			std::this_thread::sleep_for(std::chrono::milliseconds(1));
		}
	};
	auto definition = Json{{"id", id},
						   {"revision", 1},
						   {"name", "Observe"},
						   {"description", "Watch colony"},
						   {"source", "let n=0;function step(){return {output:++n}}"},
						   {"intervalTicks", 25}};
	queued.push_back({{"id", lease}, {"request", {{"kind", "install"}, {"program", definition}}}});
	dropResult = true;
	pump();
	REQUIRE(client.standingOrders().size() == 1);
	CHECK(invocations == 1); // preflight only until acknowledged
	dropResult = false;
	pump();
	CHECK(invocations == 2);
	pump();
	CHECK(invocations == 2); // simulation paused: no wall-clock scheduling
	world.game.stepCounter = 200;
	pump();
	CHECK(invocations == 3);
	pump();
	CHECK(invocations == 3); // no catch-up burst
	definition["revision"] = 2;
	definition["source"] = "function step(){throw Error('bad replacement')}";
	queued.push_back(
		{{"id", id},
		 {"request", {{"kind", "replace"}, {"program", definition}, {"expectedRevision", 1}}}});
	pump();
	REQUIRE(client.standingOrders().size() == 1);
	CHECK(client.standingOrders()[0]["revision"] == 1);
	world.game.stepCounter = 225;
	pump();
	CHECK(invocations == 5); // old program survived
	client.change(id, "pause");
	world.game.stepCounter = 500;
	pump();
	CHECK(invocations == 5);
	CHECK(client.standingOrders()[0]["paused"] == true);
	CHECK(storage.persisted > 0);
}

#include "hive/HiveDialog.h"
#include <ScreenStack.h>
TEST_CASE("Hive Mind commander panel [display]" * doctest::test_suite("HiveMindPresentation"))
{
	glob2test::HeadlessGlobals globals(
		{.display = true, .loadStrings = true, .width = 1000, .height = 800});
	glob2test::HeadlessGame world({.teams = 1, .loadDefaultRace = true, .header = true});
	Online::MemoryStorage storage;
	Online::InstanceConfig config(storage);
	Online::PlatformClient platform(config);
	Hive::ClientEnvironment environment;
	environment.storage = &storage;
	environment.request = [](auto, const auto &, const auto &, auto callback)
	{
		Online::PlatformClient::Response r;
		r.ok = true;
		r.result = {{"enabled", true}, {"available", 12500}};
		callback(r);
	};
	auto client = std::make_shared<Hive::Client>(
		world.gui, platform, "12345678-1234-4234-8234-123456789abc", 0, environment);
	client->reports = {"Keep our food supply healthy.",
					   "I have assigned more workers to the inn. I will report if our food supply "
					   "needs attention."};
	Hive::Dialog dialog(client);
	auto *screen = &dialog;
	screen->attach(*globalContainer->gfx);
	screen->draw(SDL_GetTicks());
	screen->draw(SDL_GetTicks() + 40);
	REQUIRE(screen->host().find("hive/send") != nullptr);
	REQUIRE(screen->host().find("hive/stop") != nullptr);
	globalContainer->gfx->printScreen(glob2test::artifactDirFromWorkingDirectory() +
									  "/hive-commander.bmp");
}
