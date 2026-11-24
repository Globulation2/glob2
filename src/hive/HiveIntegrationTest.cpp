#include "app/ClientFeatures.h"
#include "Map.h"
#include "hive/HiveClient.h"
#include <type_traits>
#include "online/InstanceConfig.h"
#include "online/OnlineStorage.h"
#include "online/PlatformClient.h"
#include <atomic>
#include <thread>
#include <chrono>
// SPDX-License-Identifier: GPL-3.0-or-later
#include "HeadlessGlobals.h"
#include "HeadlessGame.h"
#include "GameGUI.h"
#include "DrawFixtures.h"
#include "hive/HiveObservation.h"
#include "hive/LocalCommander.h"
#include "scripting/javascript/ScriptObservations.h"
#include "scripting/javascript/ScriptOrders.h"
#include "Order.h"
#include <fstream>
#include <cstdlib>
#include "Building.h"
#include "render/scene/Scene.h"
#include "sim/snapshot/SnapshotStore.h"
#include "GameGUIToolManager.h"
#include "Ressource.h"
#include "Player.h"
#include <future>
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
TEST_CASE("Hive Mind snapshots built off the simulation thread match a direct capture" *
		  doctest::test_suite("HiveMindIntegration"))
{
	glob2test::HeadlessGlobals globals({.seed = 19});
	glob2test::HeadlessGame world({.teams = 2, .loadDefaultRace = true, .header = true});
	world.addBuilding("swarm", 4, 4);
	world.addBuilding("swarm", 24, 24, 0, 1);
	world.addUnit(WORKER, 8, 8);
	world.addUnit(WORKER, 22, 22, 1);
	for (int x = 10; x <= 12; ++x)
		world.game.map.setResourceByIndex(x, 10, WOOD, 0);
	const auto vision = world.game.teams[0]->me;
	world.game.map.setMapDiscovered(0, 0, 16, 16, vision);
	world.game.map.switchFogOfWar();
	world.game.map.setMapDiscovered(0, 0, 16, 16, vision);
	world.game.snapshots().invalidateBoundary();

	// The reference: everything captured and built on this thread.
	Script::Observations direct(world.game, 0);
	const auto reference = Hive::capture(direct, world.game, 0);
	auto same = [](const Json &a, const Json &b)
	{
		for (const char *key : {"team", "tick", "width", "height", "teams", "units", "buildings", "tiles",
								"buildingTypes", "terrainTypes", "resourceTypes"})
		{
			INFO(key);
			CHECK(a.at(key) == b.at(key));
		}
	};

	// Prepared here, built on another thread with a tile cache: the same snapshot.
	Script::Observations observations(world.game, 0);
	Hive::TileCache cache;
	auto input = Hive::prepareCapture(observations, world.game, 0);
	auto built = std::async(std::launch::async, [&] { return Hive::buildSnapshot(input, &cache); }).get();
	same(reference, built);
	CHECK(cache.tiles.size() == std::size_t(world.game.map.getW()) * world.game.map.getH());

	// A prepared capture is isolated from the game: a change after it does not leak in.
	input = Hive::prepareCapture(observations, world.game, 0);
	world.game.map.setResourceByIndex(14, 14, WOOD, 0);
	world.game.snapshots().invalidateBoundary();
	built = std::async(std::launch::async, [&] { return Hive::buildSnapshot(input, &cache); }).get();
	same(reference, built);

	// The next capture sees the change, and the cache refreshes that tile.
	const auto changed = Hive::capture(direct, world.game, 0);
	CHECK(changed.at("tiles") != reference.at("tiles"));
	input = Hive::prepareCapture(observations, world.game, 0);
	built = std::async(std::launch::async, [&] { return Hive::buildSnapshot(input, &cache); }).get();
	same(changed, built);
}
TEST_CASE("Hive Mind evaluation gameplay fixture" * doctest::test_suite("HiveMindEvaluation"))
{
	if constexpr (!ClientFeatures::Commander) return;
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
	if (request.contains("operations"))
	{
		world.game.stepCounter = 100;
		Online::MemoryStorage storage;
		Online::InstanceConfig config(storage);
		Online::PlatformClient platform(config);
		std::uint64_t clock = 1000;
		Json queued = Json::array(), results = Json::array(), wakes = Json::array();
		const std::string lease = "12345678-1234-4234-8234-123456789abc";
		Hive::ClientEnvironment environment;
		environment.storage = &storage;
		environment.now = [&] { return clock; };
		environment.worker = [&](const Json &r)
		{
			auto result = Hive::invoke(r);
			return result;
		};
		environment.request = [&](auto, const std::string &path, const Json &body, auto callback)
		{
			Online::PlatformClient::Response r;
			r.ok = true;
			if (path.ends_with("/account"))
				r.result = {{"enabled", true}, {"available", 1000000}};
			else if (path.ends_with("/poll"))
			{
				r.result = {{"lease", lease},
							{"team", 0},
							{"operations", body.value("caughtUp", false) ? queued : Json::array()},
							{"programs", Json::array()}};
				if (body.value("caughtUp", false))
					queued = Json::array();
			}
			else if (path.ends_with("/result"))
			{
				results.push_back(body);
				r.result = {{"accepted", true}};
			}
			else if (path.ends_with("/wake"))
			{
				wakes.push_back(body.at("wake"));
				r.result = {{"accepted", true}};
			}
			else
				r.result = {{"events", Json::array()}};
			callback(r);
		};
		Hive::Client client(world.gui, platform, lease, 0, environment);
		auto pump = [&]
		{
			for (int i = 0; i < 60; i++)
			{
				clock += 100;
				client.update(true);
				while (true)
				{
					auto order = world.gui.getOrder();
					if (order->getOrderType() == ORDER_NULL)
						break;
					order->sender = 0;
					world.game.executeOrder(order, -1);
				}
				std::this_thread::sleep_for(std::chrono::milliseconds(1));
			}
		};
		for (const auto &op : request.at("operations"))
		{
			if (op.contains("advance"))
				world.game.stepCounter += op.at("advance").get<unsigned>();
			else if (op.contains("addWorkers"))
			{
				for (int i = 0; i < op.at("addWorkers").get<int>(); i++)
					world.addUnit(WORKER, 15 + i, 12);
			}
			else if (op.contains("manual"))
			{
				auto order = Script::order(world.game, 0, Hive::value(op.at("manual")));
				order->sender = 0;
				world.game.executeOrder(order, -1);
			}
			else
				queued.push_back(op);
			pump();
		}
		std::ofstream target(output);
		target << Json{{"results", results},
					   {"wakes", wakes},
					   {"standing", client.standingOrders()},
					   {"snapshot", Hive::capture(observations, world.game, 0)},
					   {"checksum", world.checksum()}}
					  .dump();
		return;
	}
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
	if constexpr (!ClientFeatures::Commander) return;
	glob2test::HeadlessGlobals globals({.seed = 19});
	glob2test::HeadlessGame world(
		{.teams = 1, .discovered = true, .loadDefaultRace = true, .header = true});
	world.addBuilding("swarm", 4, 4);
	Online::MemoryStorage storage;
	Online::InstanceConfig config(storage);
	Online::PlatformClient platform(config);
	std::uint64_t clock = 1000;
	std::atomic<int> invocations = 0;
	bool dropResult = false, delayPoll = false, failCommand = false, delayControl = false;
	Online::PlatformClient::ResponseHandler heldControl;
	Online::PlatformClient::Response controlResponse;
	Json commandRequests = Json::array();
	Online::PlatformClient::ResponseHandler heldPoll;
	Online::PlatformClient::Response heldResponse;
	Json serverPrograms = Json::array();
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
				{"programs", serverPrograms}};
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
		if (path.ends_with("/command"))
		{
			commandRequests.push_back(body);
			response.ok = !failCommand;
		}
		if (delayControl && path.ends_with("/standing-orders"))
		{
			queued.push_back({{"id", lease},
							  {"request",
							   {{"kind", body.at("action")},
								{"programId", body.at("programId")},
								{"expectedRevision", body.at("expectedRevision")}}}});
			heldControl = callback;
			controlResponse = response;
			return;
		}
		if (delayPoll && path.ends_with("/poll"))
		{
			heldPoll = callback;
			heldResponse = response;
			return;
		}
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
	delayControl = true;
	client.change(id, "pause");
	world.game.stepCounter = 500;
	pump();
	CHECK(invocations == 5);
	CHECK(client.standingOrders()[0]["paused"] == true);
	REQUIRE(bool(heldControl));
	heldControl(controlResponse);
	heldControl = {};
	delayControl = false;
	CHECK_FALSE(client.controlStatus.contains(id));
	CHECK(storage.persisted > 0);
	// A delayed response must not restart the lease validity window.
	delayPoll = true;
	pump();
	REQUIRE(bool(heldPoll));
	clock += 14000;
	heldPoll(heldResponse);
	heldPoll = {};
	world.game.stepCounter = 600;
	pump();
	CHECK(invocations == 5);
	// A fresh device sees paused metadata and can remove an order without its globals.
	delayPoll = false;
	serverPrograms = Json::array({{{"definition", definition}, {"status", "active"}}});
	Online::MemoryStorage freshStorage;
	environment.storage = &freshStorage;
	Hive::Client fresh(world.gui, platform, "12345678-1234-4234-8234-123456789aba", 0, environment);
	auto freshPump = [&]
	{
		for (int i = 0; i < 40; i++)
		{
			clock += 100;
			fresh.update(true);
			std::this_thread::sleep_for(std::chrono::milliseconds(1));
		}
	};
	freshPump();
	REQUIRE(fresh.standingOrders().size() == 1);
	CHECK(fresh.standingOrders()[0]["missingCheckpoint"] == true);
	CHECK(fresh.standingOrders()[0]["paused"] == true);
	queued.push_back(
		{{"id", lease},
		 {"request", {{"kind", "resume"}, {"programId", id}, {"expectedRevision", 2}}}});
	freshPump();
	CHECK(results.back()["status"] == "failed");
	CHECK(invocations == 5);
	serverPrograms = Json::array();
	queued.push_back(
		{{"id", lease},
		 {"request", {{"kind", "remove"}, {"programId", id}, {"expectedRevision", 2}}}});
	freshPump();
	CHECK(fresh.standingOrders().empty());
	failCommand = true;
	fresh.command("Count workers", false);
	CHECK(fresh.commandDraft == "Count workers");
	fresh.command("Count workers", false);
	CHECK(commandRequests[0]["id"] == commandRequests[1]["id"]);
	fresh.command("Count workers", true);
	CHECK(commandRequests[1]["id"] != commandRequests[2]["id"]);
	failCommand = false;
	fresh.command("Count workers", true);
	CHECK(fresh.commandDraft.empty());
}

#include "hive/HiveDialog.h"
#include "Engine.h"
#include "GameGUIKeyActions.h"
#include "GameGUIDialog.h"
#include <algorithm>
#include <ScreenStack.h>
TEST_CASE("Hive Mind commander panel [display]" * doctest::test_suite("HiveMindPresentation"))
{
	if constexpr (!ClientFeatures::Commander) return;
	glob2test::HeadlessGlobals globals(
		{.display = true, .loadStrings = true, .width = 1000, .height = 800});
	// GameGUI owns the client and saves its state during destruction, so its
	// borrowed storage and platform must outlive the GUI.
	Online::MemoryStorage storage;
	Online::InstanceConfig config(storage);
	Online::PlatformClient platform(config);
	struct
	{
		GameGUI gui;
	} world;
	auto map = Engine::loadMapHeader("maps/balanced.map");
	GameHeader players;
	players.setNumberOfPlayers(1);
	players.getBasePlayer(0) = BasePlayer(0, "Commander", 0, BasePlayer::P_LOCAL);
	REQUIRE(world.gui.loadFromHeaders(map, players, true, true));
	world.gui.localTeamNo = 0;
	world.gui.localPlayer = 0;
	world.gui.adjustLocalTeam();
	world.gui.viewportX = (world.gui.game.teams[0]->startPosX - 12) & world.gui.game.map.getMaskW();
	world.gui.viewportY = (world.gui.game.teams[0]->startPosY - 8) & world.gui.game.map.getMaskH();
	Hive::ClientEnvironment environment;
	environment.storage = &storage;
	unsigned commandPosts = 0, stopPosts = 0;
	environment.request = [&](auto, const std::string &path, const auto &, auto callback)
	{
		Online::PlatformClient::Response r;
		r.ok = true;
		if (path.ends_with("/command"))
			++commandPosts;
		if (path.ends_with("/stop"))
			++stopPosts;
		r.result = {{"enabled", true}, {"available", 12500}};
		callback(r);
	};
	const std::string pid = "12345678-1234-4234-8234-123456789abd";
	Json def = {{"id", pid},
				{"revision", 1},
				{"name", "Keep food supply healthy"},
				{"description", "Watch our inns and assign workers when food needs attention."},
				{"source", "function step(){}"},
				{"intervalTicks", 25}};
	Json saved = {{"version", 1},
				  {"origin", platform.origin()},
				  {"team", world.gui.localTeamNo},
				  {"uncertain", false},
				  {"programs", Json::array({{{"definition", def},
											 {"state", Json::object()},
											 {"initialized", true},
											 {"paused", false},
											 {"nextTick", 25},
											 {"random", 1}}})}};
	storage.write("online/hive/12345678-1234-4234-8234-123456789abc-" +
					  std::to_string(world.gui.localPlayer) + ".json",
				  saved.dump());
	auto client = std::make_shared<Hive::Client>(
		world.gui, platform, "12345678-1234-4234-8234-123456789abc", 0, environment);
	client->reports = {"Keep our food supply healthy.",
					   "I have assigned more workers to the inn. I will report if our food supply "
					   "needs attention."};
	world.gui.hive = client;
	world.gui.hiveCards = std::make_unique<Hive::Dialog>(client);
	auto *screen = world.gui.hiveCards.get();
	screen->compose = [&] { world.gui.openCommander(); };
	screen->attach(*globalContainer->gfx);
	glob2test::drawGUI(world.gui,0);
	REQUIRE(screen->host().find("hive/toggle/" + pid) != nullptr);
	CHECK(screen->host().find("hive/credits") == nullptr);
	globalContainer->gfx->printScreen(glob2test::artifactDirFromWorkingDirectory() +
									  "/hive-commander.bmp");
	client->commandDraft = "Build two inns near our colony";
	world.gui.openCommander();
	CHECK(world.gui.typingCommander);
	glob2test::drawGUI(world.gui,0);
	globalContainer->gfx->printScreen(glob2test::artifactDirFromWorkingDirectory() +
									  "/hive-command-input.bmp");
	// Stop wins over Return submission while preserving the editable draft.
	SDL_Event stop{};
	stop.type = SDL_EVENT_KEY_DOWN;
	stop.key.key = SDLK_RETURN;
	stop.key.mod = SDL_KMOD_CTRL | SDL_KMOD_SHIFT;
	world.gui.processEvent(&stop);
	CHECK(stopPosts == 1);
	CHECK(commandPosts == 0);
	CHECK(world.gui.typingCommander);
	// A customized release-triggered Stop binding is honored too.
	stop.type = SDL_EVENT_KEY_UP;
	stop.key.key = SDLK_F8;
	stop.key.mod = 0;
	KeyboardShortcut releaseStop;
	releaseStop.addKeyPress(KeyPress(stop.key, false));
	releaseStop.setAction(GameGUIKeyActions::StopCommander);
	world.gui.keyboardManager.getKeyboardShortcuts().push_back(releaseStop);
	world.gui.processEvent(&stop);
	CHECK(stopPosts == 2);
	CHECK(commandPosts == 0);
	CHECK(world.gui.typingCommander);
	// Use actual event routing: visible HUD controls stay usable while typing.
	screen->invalidate();
	glob2test::drawGUI(world.gui,0);
	auto *stopButton = screen->host().find("hive/stop");
	REQUIRE(stopButton);
	SDL_Event click{};
	click.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
	click.button.button = SDL_BUTTON_LEFT;
	click.button.x = stopButton->bounds.x + stopButton->bounds.w / 2;
	click.button.y = stopButton->bounds.y + stopButton->bounds.h / 2;
	world.gui.processEvent(&click);
	click.type = SDL_EVENT_MOUSE_BUTTON_UP;
	world.gui.processEvent(&click);
	CHECK(stopPosts == 3);
	CHECK(commandPosts == 0);
	CHECK(world.gui.typingCommander);

	SDL_Event escape{};
	escape.type = SDL_EVENT_KEY_DOWN;
	escape.key.key = SDLK_ESCAPE;
	world.gui.processEvent(&escape);
	CHECK_FALSE(world.gui.typingCommander);
	CHECK(client->commandDraft == "Build two inns near our colony");
	SDL_Event outside{};
	outside.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
	outside.button.x = 900;
	outside.button.y = 300;
	CHECK_FALSE(screen->handle(outside));
	auto tap = [&](const std::string &id)
	{
		auto *node = screen->host().find(id);
		REQUIRE(node);
		screen->host().tapAt(
			{node->bounds.x + node->bounds.w / 2, node->bounds.y + node->bounds.h / 2});
		glob2test::drawGUI(world.gui,0);
	};
	tap("hive/stop");
	CHECK(stopPosts == 4);
	CHECK(client->standingOrders().size() == 1); // Stop does not cancel automation.
	client->reports = {"Earlier attack report", "Latest colony report"};
	screen->invalidate();
	glob2test::drawGUI(world.gui,0);
	auto text = [&]
	{
		std::string all;
		std::function<void(const GAGGUI::ui::Node &)> visit = [&](const auto &n)
		{
			all += n.accessibleText() + "\n";
			for (const auto &c : n.children)
				visit(*c);
		};
		visit(*screen->host().root());
		return all;
	};
	CHECK(text().find("Earlier attack report") == std::string::npos);
	tap("hive/details");
	CHECK(text().find("Earlier attack report") != std::string::npos);
	CHECK(text().find("Latest colony report") != std::string::npos);
	// Long multibyte reports remain valid at the compact preview boundary.
	tap("hive/details");
	client->reports = {std::string(179, 'x') + "防衛"};
	screen->invalidate();
	glob2test::drawGUI(world.gui,0);
	CHECK(text().find(std::string(179, 'x') + "…") != std::string::npos);
	// Stop also wins over modified Return in ordinary team chat: never send
	// a message when the player intended to stop the commander.
	SDL_Event enter{};
	enter.type = SDL_EVENT_KEY_DOWN;
	enter.key.key = SDLK_RETURN;
	world.gui.processEvent(&enter);
	stop.type = SDL_EVENT_KEY_DOWN;
	stop.key.key = SDLK_RETURN;
	stop.key.mod = SDL_KMOD_CTRL | SDL_KMOD_SHIFT;
	world.gui.processEvent(&stop);
	CHECK(stopPosts == 5);
	CHECK(commandPosts == 0);
	CHECK_FALSE(world.gui.typingCommander);
	CHECK(world.gui.getOrder()->getOrderType() == ORDER_NULL);
	SDL_Event typed{};
	typed.type = SDL_EVENT_TEXT_INPUT;
	typed.text.text = "Still editing team chat";
	world.gui.processEvent(&typed);
	world.gui.processEvent(&enter);
	CHECK(world.gui.getOrder()->getOrderType() == ORDER_TEXT_MESSAGE);
}

TEST_CASE("Hive Mind pending requests preserve drafts and stop feedback" *
		  doctest::test_suite("HiveMindReliability"))
{
	if constexpr (!ClientFeatures::Commander) return;
	glob2test::HeadlessGlobals globals;
	glob2test::HeadlessGame world({.teams = 1, .loadDefaultRace = true, .header = true});
	Online::MemoryStorage storage;
	Online::InstanceConfig config(storage);
	Online::PlatformClient platform(config);
	Hive::ClientEnvironment environment;
	environment.storage = &storage;
	Online::PlatformClient::ResponseHandler commandReply, stopReply;
	unsigned commands = 0, stops = 0;
	environment.request = [&](auto, const std::string &path, const Json &, auto callback)
	{
		if (path.ends_with("/command"))
		{
			++commands;
			commandReply = callback;
		}
		else if (path.ends_with("/stop"))
		{
			++stops;
			stopReply = callback;
		}
	};
	auto client = std::make_unique<Hive::Client>(
		world.gui, platform, "12345678-1234-4234-8234-123456789abc", 0, environment);
	client->command("Build an inn", false);
	client->command("Defend our colony", true);
	CHECK(commands == 1);
	CHECK(client->commandDraft == "Defend our colony");
	REQUIRE_FALSE(client->reports.empty());
	CHECK(client->reports.back().find("still sending") != std::string::npos);
	Hive::CommanderView &remoteView = *client;
	CHECK(remoteView.reportPresentation(client->reports.size() - 1).source == Hive::ReportSource::PreviousSending);
	Online::PlatformClient::Response ok;
	ok.ok = true;
	commandReply(ok);
	CHECK(client->commandDraft == "Defend our colony");
	client->command(client->commandDraft, true);
	CHECK(commands == 2);
	commandReply(ok);
	CHECK(client->commandDraft.empty());
	client->progress = "Assessing our defences";
	client->stop();
	client->stop();
	CHECK(stops == 1);
	CHECK(client->controlStatus["commander"] == "Stopping…");
	Online::PlatformClient::Response failure;
	failure.ok = false;
	stopReply(failure);
	CHECK(client->controlStatus["commander"].find("failed") != std::string::npos);
	CHECK_FALSE(client->progress.empty());
	client->stop();
	CHECK(stops == 2);
	stopReply(ok);
	CHECK(client->progress.empty());
	client->command("Count workers", false);
	client->stop();
	client.reset();
	// Late network completions must be harmless after leaving the match.
	commandReply(ok);
	stopReply(ok);
}

TEST_CASE("Free client never starts Commander or changes its stored checkpoint" * doctest::test_suite("HiveMindIntegration"))
{
    if constexpr (ClientFeatures::Commander) return;
    glob2test::HeadlessGlobals globals;
    glob2test::HeadlessGame world({.teams = 1, .loadDefaultRace = true, .header = true});
    Online::MemoryStorage storage;
    Online::InstanceConfig config(storage);
    Online::PlatformClient platform(config);
    Hive::ClientEnvironment environment;
    environment.storage = &storage;
    unsigned requests = 0;
    environment.request = [&](auto, const std::string &, const Json &, auto) { ++requests; };
    const std::string checkpoint = "online/hive/12345678-1234-4234-8234-123456789abc-" + std::to_string(world.gui.localPlayer) + ".json";
    REQUIRE(storage.write(checkpoint, "existing checkpoint"));
    {
        Hive::Client client(world.gui, platform, "12345678-1234-4234-8234-123456789abc", 0, environment);
        CHECK_FALSE(client.available());
        client.command("Build an inn", false);
        client.stop();
        client.change("order", "resume");
        client.update(true);
    }
    CHECK(requests == 0);
    std::string saved;
    REQUIRE(storage.read(checkpoint, saved));
    CHECK(saved == "existing checkpoint");
}

static_assert(std::is_same_v<decltype(Hive::Client::reports), std::vector<std::string>>);
TEST_CASE("Hive report presentation metadata preserves raw API, bounded history and remote provenance" *
		  doctest::test_suite("HiveMindIntegration"))
{
	std::vector<std::string> raw;
	Hive::ReportHistory history;
	const std::string same = "A standing order needs attention and has been paused.";
	history.append(raw, same, Hive::ReportSource::StandingOrderPaused);
	history.append(raw, same); // Remote content is never identified by its bytes.
	CHECK_EQ(history.at(raw, 0).source, Hive::ReportSource::StandingOrderPaused);
	CHECK_EQ(history.at(raw, 1).source, Hive::ReportSource::Remote);
	CHECK_EQ(raw[0], raw[1]);
	for (int i = 0; i < 125; ++i)
		history.append(raw, std::to_string(i), i % 2 ? Hive::ReportSource::Stopped : Hive::ReportSource::Remote);
	REQUIRE_EQ(raw.size(), Hive::ReportHistory::Limit);
	CHECK_EQ(raw.front(), "25");
	for (std::size_t i = 0; i < raw.size(); ++i)
	{
		const auto report = history.at(raw, i);
		CHECK_EQ(report.text, raw[i]);
		CHECK_EQ(report.source, std::stoi(raw[i]) % 2 ? Hive::ReportSource::Stopped : Hive::ReportSource::Remote);
	}
	raw[0] = "Externally supplied report";
	CHECK_EQ(history.at(raw, 0).source, Hive::ReportSource::Remote);
	raw.push_back(same); // Public raw API writes carry no presentation metadata.
	CHECK_EQ(history.at(raw, raw.size() - 1).source, Hive::ReportSource::Remote);
	history.append(raw, "new local", Hive::ReportSource::ConnectionChanged);
	CHECK_EQ(raw.size(), Hive::ReportHistory::Limit);
	CHECK_EQ(history.at(raw, raw.size() - 2).source, Hive::ReportSource::Remote);
	CHECK_EQ(history.at(raw, raw.size() - 1).source, Hive::ReportSource::ConnectionChanged);
}

#include "Player.h"
TEST_CASE("local Hive operation journal preflight cadence and teardown" *
		  doctest::test_suite("HiveMindIntegration"))
{
	glob2test::HeadlessGlobals globals({.seed = 19});
	glob2test::HeadlessGame world({.teams = 2, .loadDefaultRace = true, .header = true});
	world.addBuilding("swarm", 4, 4);
	world.gui.localPlayer = 0;
	world.gui.localTeamNo = 0;
	world.game.players[0]->type = BasePlayer::P_LOCAL;
	auto worker = [](Json input)
	{
		std::promise<Json> ready;
		ready.set_value(Hive::invoke(input));
		return ready.get_future();
	};
	Hive::LocalCommander commander(world.gui, worker);
	Hive::CommanderView &localView = commander;
	const std::string authored = "A standing order needs attention and has been paused.";
	commander.reports.push_back(authored);
	CHECK(localView.reportPresentation(0).text == authored);
	CHECK(localView.reportPresentation(0).source == Hive::ReportSource::Remote);
	CHECK(localView.reportPresentation(1).text.empty());
	CHECK(localView.reportPresentation(1).source == Hive::ReportSource::Remote);
	commander.reports.clear();
	const auto session = commander.status().at("session");
	auto run = Json{{"session", session},
					{"operation_key", "once"},
					{"source", "function step(ctx){return {output:{tick:ctx.tick},orders:[]}}"}};
	CHECK(commander.call("hive_run", run).at("status") == "queued");
	CHECK(commander.call("hive_run", run).at("status") == "queued");
	CHECK_THROWS(commander.call(
		"hive_run",
		Json{{"session", session}, {"operation_key", "once"}, {"source", "function step(ctx){}"}}));
	commander.update();
	commander.update();
	const auto result =
		commander.call("hive_result", {{"session", session}, {"operation_key", "once"}});
	CHECK(result.at("status") == "completed");
	CHECK(result.at("submitted") == false);
	CHECK(commander.call("hive_run", run) == result);
	auto install = Json{{"session", session},
						{"operation_key", "install"},
						{"action", "install"},
						{"name", "count"},
						{"interval_ticks", 25},
						{"source", "let n=0;function step(ctx){return {output:++n,orders:[]}}"}};
	commander.call("hive_program", install);
	commander.update();
	commander.update();
	auto standing = commander.standingOrders();
	REQUIRE(standing.size() == 1);
	CHECK(standing[0].at("revision") == 1);
	world.game.stepCounter += 25;
	commander.update();
	commander.update();
	standing = commander.standingOrders();
	CHECK(standing[0]["recent"]["output"] == 1);
	commander.update();
	CHECK(commander.standingOrders()[0]["recent"]["output"] == 1);
	auto replace = install;
	replace["action"] = "replace";
	replace["operation_key"] = "bad-replace";
	replace["expected_revision"] = 1;
	replace["source"] = "invalid javascript !";
	commander.call("hive_program", replace);
	commander.update();
	commander.update();
	CHECK(commander.call("hive_result", {{"session", session}, {"operation_key", "bad-replace"}})
			  .at("status") == "failed");
	CHECK(commander.standingOrders()[0].at("revision") == 1);
	commander.change("count", "pause");
	CHECK(commander.standingOrders()[0].at("paused") == true);
	replace["operation_key"] = "paused-replace";
	replace["source"] = install["source"];
	commander.call("hive_program", replace);
	commander.update();
	commander.update();
	CHECK(commander.standingOrders()[0].at("paused") == true);
	CHECK(commander.standingOrders()[0].at("revision") == 2);
	const auto reportsBeforePause = commander.reports.size();
	commander.change("count", "resume");
	world.game.stepCounter += 25;
	commander.update();
	commander.change("count", "pause");
	commander.update();
	CHECK(commander.reports.size() == reportsBeforePause);
	commander.change("count", "resume");
	CHECK(commander.standingOrders()[0].at("paused") == false);
	commander.change("count", "cancel");
	CHECK(commander.standingOrders().empty());
	install["operation_key"] = "reinstall";
	commander.call("hive_program", install);
	commander.update();
	commander.update();
	CHECK(commander.standingOrders()[0].at("revision") == 3);
	replace["operation_key"] = "stale-replace";
	replace["expected_revision"] = 2;
	commander.call("hive_program", replace);
	commander.update();
	commander.update();
	CHECK(commander.call("hive_result", {{"session", session}, {"operation_key", "stale-replace"}})
			  .at("status") == "failed");
	CHECK(commander.standingOrders()[0].at("revision") == 3);
	commander.change("count", "cancel");
	auto oversized = run;
	oversized["operation_key"] = "oversized";
	oversized["source"] = std::string(131073, ' ');
	CHECK_THROWS(commander.call("hive_run", oversized));
	auto invalidBatch = run;
	invalidBatch["operation_key"] = "invalid-batch";
	invalidBatch["source"] =
		"function step(ctx){const b=ctx.game.buildings({team:ctx.myTeam})[0];return "
		"{orders:[{type:'workers',building:{id:b.id,generation:b.generation},workers:3},{type:'"
		"workers',building:{id:999999,generation:1},workers:3}]}}";
	commander.call("hive_run", invalidBatch);
	commander.update();
	commander.update();
	// Each order stands alone: the valid one is sent, the stale reference is rejected.
	const auto batch = commander.call("hive_result", {{"session", session}, {"operation_key", "invalid-batch"}});
	CHECK(batch.at("status") == "completed");
	CHECK(batch.at("submitted") == true);
	CHECK(batch.at("order_count") == 1);
	REQUIRE(batch.at("rejected").size() == 1);
	CHECK(batch.at("rejected")[0].at("order") == 1);
	CHECK(batch.at("rejected")[0].at("type") == "workers");
	for (int mode = 0; mode < 5; ++mode)
	{
		Hive::LocalCommander probe(world.gui, worker);
		CHECK(probe.call("hive_status", Json::object()).at("available").get<bool>());
		const auto otherType = world.game.gameHeader.getBasePlayer(1).type;
		if (mode == 0) world.gui.networkMatch.active = true;
		if (mode == 1) globalContainer->replaying = true;
		if (mode == 2) globalContainer->liveSpectating = true;
		if (mode == 3) world.game.players[0]->type = BasePlayer::P_AI;
		if (mode == 4) world.game.gameHeader.getBasePlayer(1).type = BasePlayer::P_IP;
		CHECK_FALSE(probe.call("hive_status", Json::object()).at("available").get<bool>());
		CHECK_THROWS(probe.call("hive_run", run));
		world.gui.networkMatch.active = false;
		globalContainer->replaying = false;
		globalContainer->liveSpectating = false;
		world.game.players[0]->type = BasePlayer::P_LOCAL;
		world.game.gameHeader.getBasePlayer(1).type = otherType;
	}
	commander.clear();
	CHECK_FALSE(commander.available());
	CHECK_THROWS(commander.call("hive_run", run));
}

TEST_CASE("local Hive contains malformed worker responses and pauses affected programs" *
		  doctest::test_suite("HiveMindIntegration"))
{
	glob2test::HeadlessGlobals globals({.seed = 19});
	glob2test::HeadlessGame world({.loadDefaultRace = true, .header = true});
	world.addBuilding("swarm", 4, 4);
	world.gui.localPlayer = world.gui.localTeamNo = 0;
	world.game.players[0]->type = BasePlayer::P_LOCAL;
	const std::vector<Json> malformed{
		Json::array(), nullptr, true, "bad", {{"ok", "bad"}},
		{{"ok", false}, {"error", 1}}, {{"ok", false}, {"diagnostic", Json::object()}},
		{{"ok", true}, {"orders", Json::object()}}, {{"ok", true}, {"wakes", "bad"}},
		{{"ok", true}, {"installedInitialized", "bad"}}, {{"ok", true}, {"random", "bad"}},
		{{"ok", true}, {"random", -1}}, {{"ok", false}}};
	for (const auto &bad : malformed)
	{
		CAPTURE(bad.dump());
		bool fail = true;
		Hive::LocalCommander commander(world.gui, [&](Json input)
		{
			std::promise<Json> ready;
			ready.set_value(fail ? bad : Hive::invoke(input));
			return ready.get_future();
		});
		const auto session = commander.status().at("session");
		const std::string source = "function step(){return {output:'healthy',orders:[]}}";
		commander.call("hive_run", {{"session", session}, {"operation_key", "bad"}, {"source", source}});
		CHECK_NOTHROW(commander.update());
		CHECK_NOTHROW(commander.update());
		const auto result = commander.call("hive_result", {{"session", session}, {"operation_key", "bad"}});
		CHECK(result.at("status") == "failed");
		CHECK_FALSE(result.at("error").get<std::string>().empty());
		CHECK(result.at("submitted") == false);
		CHECK(world.gui.getOrder()->getOrderType() == ORDER_NULL);
		fail = false;
		commander.call("hive_program", {{"session", session}, {"operation_key", "install"},
			{"action", "install"}, {"name", "guard"}, {"source", source}});
		commander.update(); commander.update();
		REQUIRE(commander.standingOrders().size() == 1);
		fail = true;
		world.game.stepCounter += 25;
		CHECK_NOTHROW(commander.update());
		CHECK_NOTHROW(commander.update());
		const auto program = commander.standingOrders().at(0);
		CHECK(program.at("paused") == true);
		CHECK(program.at("recent").at("status") == "failed");
		CHECK_FALSE(program.at("recent").at("error").get<std::string>().empty());
		CHECK(world.gui.getOrder()->getOrderType() == ORDER_NULL);
		fail = false;
		commander.call("hive_run", {{"session", session}, {"operation_key", "recovery"}, {"source", source}});
		commander.update(); commander.update();
		CHECK(commander.call("hive_result", {{"session", session}, {"operation_key", "recovery"}}).at("output") == "healthy");
	}
	// A late malformed RNG field must not publish an otherwise valid order or
	// commit the invocation's globals before pausing the program.
	bool corruptRandom = false;
	Hive::LocalCommander commander(world.gui, [&](Json input)
	{
		auto result = Hive::invoke(input);
		if (corruptRandom) result["random"] = "bad";
		std::promise<Json> ready; ready.set_value(std::move(result));
		return ready.get_future();
	});
	const auto session = commander.status().at("session");
	commander.call("hive_program", {{"session", session}, {"operation_key", "atomic-install"},
		{"action", "install"}, {"name", "atomic"},
		{"source", "let n=0;function step(ctx){n++;const b=ctx.game.buildings({team:ctx.myTeam})[0];"
		 "return {output:n,orders:[{type:'workers',building:b,workers:3}]}}"}});
	commander.update(); commander.update();
	REQUIRE(commander.standingOrders().size() == 1);
	corruptRandom = true;
	world.game.stepCounter += 25;
	commander.update();
	CHECK_NOTHROW(commander.update());
	CHECK(commander.standingOrders()[0].at("paused") == true);
	CHECK(world.gui.getOrder()->getOrderType() == ORDER_NULL);
	corruptRandom = false;
	commander.change("atomic", "resume");
	world.game.stepCounter += 25;
	commander.update(); commander.update();
	CHECK(commander.standingOrders()[0].at("recent").at("output") == 1);
	CHECK(world.gui.getOrder()->getOrderType() == ORDER_MODIFY_BUILDING);
	CHECK(world.gui.getOrder()->getOrderType() == ORDER_NULL);

}

TEST_CASE("local Hive retires changed player context while a worker is pending" *
		  doctest::test_suite("HiveMindIntegration"))
{
	glob2test::HeadlessGlobals globals({.seed = 19});
	glob2test::HeadlessGame world({.teams = 2, .loadDefaultRace = true, .header = true});
	world.addBuilding("swarm", 4, 4);
	world.gui.localPlayer = world.gui.localTeamNo = 0;
	world.game.players[0]->type = BasePlayer::P_LOCAL;
	std::promise<Json> pending;
	Json input;
	Hive::LocalCommander commander(world.gui, [&](Json request)
	{
		input = std::move(request);
		return pending.get_future();
	});
	const auto session = commander.status().at("session");
	commander.call("hive_run", {{"session", session}, {"operation_key", "late"},
		{"source", "function step(ctx){const b=ctx.game.buildings({team:ctx.myTeam})[0];"
		 "return {orders:[{type:'workers',building:b,workers:3}]}}"}});
	commander.update();
	REQUIRE_FALSE(input.is_null());
	world.gui.localTeamNo = 1;
	commander.update();
	CHECK_FALSE(commander.available());
	world.gui.localTeamNo = 0;
	pending.set_value(Hive::invoke(input));
	commander.update();
	CHECK_FALSE(commander.available());
	CHECK(world.gui.getOrder()->getOrderType() == ORDER_NULL);
	CHECK_THROWS(commander.call("hive_result", {{"session", session}, {"operation_key", "late"}}));
}

TEST_CASE("local Hive retires paused programs on an idle player context change" *
		  doctest::test_suite("HiveMindIntegration"))
{
	glob2test::HeadlessGlobals globals({.seed = 19});
	glob2test::HeadlessGame world({.teams = 2, .loadDefaultRace = true, .header = true});
	world.gui.localPlayer = world.gui.localTeamNo = 0;
	world.game.players[0]->type = BasePlayer::P_LOCAL;
	Hive::LocalCommander commander(world.gui, [](Json input)
	{
		std::promise<Json> result;
		result.set_value(Hive::invoke(input));
		return result.get_future();
	});
	const auto session = commander.status().at("session");
	commander.call("hive_program", {{"session", session}, {"operation_key", "install"},
		{"action", "install"}, {"name", "paused"}, {"source", "function step(ctx){return {orders:[]}}"}});
	commander.update();
	commander.update();
	REQUIRE(commander.call("hive_result", {{"session", session}, {"operation_key", "install"}}).at("status") == "completed");
	commander.change("paused", "pause");
	REQUIRE(commander.standingOrders().size() == 1);
	world.gui.localPlayer = 1;
	commander.update();
	CHECK_FALSE(commander.available());
	CHECK(commander.standingOrders().empty());
	world.gui.localPlayer = 0;
	commander.update();
	CHECK_FALSE(commander.available());
}

TEST_CASE("local Hive cards own finger sequences and relinquish cancelled captures [display]" *
		  doctest::test_suite("HiveMindPresentation"))
{
	glob2test::HeadlessGlobals globals({.display = true, .loadStrings = true, .width = 640, .height = 480});
	struct View final : Hive::CommanderView
	{
		int stops = 0;
		bool available() const override { return true; }
		bool local() const override { return true; }
		Json standingOrders() const override { return Json::array(); }
		void stop() override { ++stops; }
		void change(const std::string &, const std::string &) override {}
	};
	auto view = std::make_shared<View>();
	Hive::Dialog dialog(view);
	dialog.attach(*globals->gfx);
	dialog.update(0);
	const auto stop = dialog.host().bounds("hive/stop").center();
	const GAGGUI::ui::Point outside{639, 0};
	Uint64 clock = SDL_MS_TO_NS(1000);
	auto finger = [&](Uint32 type, GAGGUI::ui::Point p)
	{
		SDL_Event event{};
		event.type = type;
		event.tfinger.timestamp = clock += SDL_MS_TO_NS(16);
		event.tfinger.touchID = 7;
		event.tfinger.fingerID = 11;
		event.tfinger.x = float(p.x) / 640;
		event.tfinger.y = float(p.y) / 480;
		return dialog.handle(event);
	};
	CHECK(finger(SDL_EVENT_FINGER_DOWN, stop));
	CHECK(finger(SDL_EVENT_FINGER_MOTION, outside));
	CHECK(finger(SDL_EVENT_FINGER_UP, outside));
	CHECK(view->stops == 0);
	// A drag starts the Host's ordinary elastic/coasting motion. Its next
	// contact stops that motion rather than activating a button. Retire the
	// first gesture before testing an independent tap/capture sequence.
	CHECK(dialog.host().animating());
	dialog.cancelInput();
	dialog.host().layoutIfNeeded();
	CHECK(finger(SDL_EVENT_FINGER_DOWN, stop));
	CHECK(finger(SDL_EVENT_FINGER_UP, stop));
	CHECK(view->stops == 1);
	dialog.update(1);
	CHECK(finger(SDL_EVENT_FINGER_DOWN, stop));
	dialog.cancelInput(); // A higher-priority card or modal took ownership.
	CHECK_FALSE(finger(SDL_EVENT_FINGER_MOTION, outside));
	CHECK_FALSE(finger(SDL_EVENT_FINGER_UP, stop));
	CHECK(view->stops == 1);

	CHECK(finger(SDL_EVENT_FINGER_DOWN, stop));
	CHECK_FALSE(finger(SDL_EVENT_FINGER_DOWN, outside)); // Reused device ID, missed old UP.
	CHECK_FALSE(finger(SDL_EVENT_FINGER_UP, stop));
	CHECK(view->stops == 1);
	CHECK(finger(SDL_EVENT_FINGER_DOWN, stop));
	SDL_Event lost{};
	lost.type = SDL_EVENT_WINDOW_FOCUS_LOST;
	CHECK_FALSE(dialog.handle(lost));
	CHECK_FALSE(finger(SDL_EVENT_FINGER_UP, stop));
	CHECK(view->stops == 1);
}

TEST_CASE("local Hive cards retain native scroll ownership through momentum [display]" *
		  doctest::test_suite("HiveMindPresentation"))
{
	glob2test::HeadlessGlobals globals({.display = true, .loadStrings = true, .width = 640, .height = 480});
	struct View final : Hive::CommanderView
	{
		bool available() const override { return true; }
		bool local() const override { return true; }
		Json standingOrders() const override { return Json::array(); }
		void stop() override {}
		void change(const std::string &, const std::string &) override {}
	};
	Hive::Dialog dialog(std::make_shared<View>());
	dialog.attach(*globals->gfx);
	dialog.update(0);
	const auto point = dialog.host().bounds("hive/stop").center();
	GAGCore::GestureScrollEvent scroll;
	scroll.sequence = 17;
	scroll.x = point.x;
	scroll.y = point.y;
	scroll.phase = GAGCore::ScrollGesturePhase::Began;
	CHECK(dialog.handle(GAGCore::gestureScrollEvent(scroll)));
	scroll.x = 639; scroll.y = 0;
	scroll.phase = GAGCore::ScrollGesturePhase::Changed; scroll.dy = -8;
	CHECK(dialog.handle(GAGCore::gestureScrollEvent(scroll)));
	scroll.phase = GAGCore::ScrollGesturePhase::Ended;
	CHECK(dialog.handle(GAGCore::gestureScrollEvent(scroll)));
	scroll.phase = GAGCore::ScrollGesturePhase::None;
	scroll.momentum = GAGCore::ScrollGesturePhase::Began;
	CHECK(dialog.handle(GAGCore::gestureScrollEvent(scroll)));
	scroll.momentum = GAGCore::ScrollGesturePhase::Ended;
	CHECK(dialog.handle(GAGCore::gestureScrollEvent(scroll)));
	++scroll.sequence;
	scroll.phase = GAGCore::ScrollGesturePhase::Began;
	scroll.momentum = GAGCore::ScrollGesturePhase::None;
	CHECK_FALSE(dialog.handle(GAGCore::gestureScrollEvent(scroll)));
	scroll.x = point.x; scroll.y = point.y;
	++scroll.sequence;
	CHECK(dialog.handle(GAGCore::gestureScrollEvent(scroll)));
	dialog.cancelInput();
	scroll.phase = GAGCore::ScrollGesturePhase::Changed;
	CHECK_FALSE(dialog.handle(GAGCore::gestureScrollEvent(scroll)));
}

TEST_CASE("Hive cards remain inspectable and actionable above the text composer [display]" *
		  doctest::test_suite("HiveMindPresentation"))
{
	glob2test::HeadlessGlobals globals({.display = true, .loadStrings = true, .width = 1000, .height = 800});
	glob2test::HeadlessGame world({.discovered = true, .loadDefaultRace = true, .header = true});
	auto &gui = world.gui;
	// HeadlessGame deliberately skips client initialization; this fixture paints
	// the full desktop HUD and needs the same initialized presentation as loading.
	gui.init();
	gui.localPlayer = gui.localTeamNo = 0;
	gui.adjustLocalTeam();
	gui.prepareLocalPresentation();
	struct View final : Hive::CommanderView
	{
		int stops = 0;
		bool available() const override { return true; }
		bool local() const override { return true; }
		Json standingOrders() const override { return Json::array(); }
		void stop() override { ++stops; }
		void change(const std::string &, const std::string &) override {}
	};
	auto view = std::make_shared<View>();
	gui.hiveCards = std::make_unique<Hive::Dialog>(view);
	gui.hiveCards->attach(*globals->gfx);
	SDL_Event key{};
	key.type = SDL_EVENT_KEY_DOWN;
	key.key.key = SDLK_RETURN;
	gui.processEvent(&key);
	glob2test::drawGUI(gui, 0);
	const auto owners = gui.inspectionHosts();
	REQUIRE(owners.size() == 2);
	const auto composer = std::find_if(owners.begin(), owners.end(), [](auto *host) { return host->find("chat"); });
	REQUIRE(composer != owners.end());
	auto *composerHost = *composer;
	auto composerPresent = [&]
	{
		const auto current = gui.inspectionHosts();
		return std::find(current.begin(), current.end(), composerHost) != current.end();
	};
	CHECK(std::find(owners.begin(), owners.end(), &gui.hiveCards->host()) != owners.end());
	auto snapshot = gui.hiveCards->host().inspect();
	auto command = GAGGUI::ui::semanticDispatch();
	command->surface = snapshot.instance;
	command->key = "hive/stop";
	std::function<void(const std::vector<GAGGUI::ui::ControlSnapshot> &)> find = [&](const auto &nodes)
	{
		for (const auto &node : nodes)
		{
			if (node.key == command->key) command->revision = node.revision;
			find(node.children);
		}
	};
	find(snapshot.controls);
	REQUIRE(command->revision > 0);
	auto semantic = command->event();
	gui.processEvent(&semantic);
	CHECK(command->consumed);
	CHECK(command->error.empty());
	CHECK(view->stops == 1);
	CHECK(composerPresent());

	const auto point = gui.hiveCards->host().bounds("hive/stop").center();
	SDL_Event click{};
	click.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
	click.button.button = SDL_BUTTON_LEFT;
	click.button.x = point.x;
	click.button.y = point.y;
	gui.processEvent(&click);
	click.type = SDL_EVENT_MOUSE_BUTTON_UP;
	gui.processEvent(&click);
	CHECK(view->stops == 2);
	CHECK(composerPresent());

	key.type = SDL_EVENT_KEY_DOWN;
	key.key.key = SDLK_ESCAPE;
	gui.processEvent(&key); // Close chat.
	key.type = SDL_EVENT_KEY_UP;
	gui.processEvent(&key);
	key.type = SDL_EVENT_KEY_DOWN;
	gui.processEvent(&key); // Open the game menu.
	CHECK_FALSE(gui.hiveCardsVisible());
	const auto modalOwners = gui.inspectionHosts();
	REQUIRE(modalOwners.size() == 1);
	CHECK(modalOwners.front() != &gui.hiveCards->host());
}

TEST_CASE("local Hive publishes player API separately from local control rules" *
          doctest::test_suite("HiveMindIntegration"))
{
    const auto api = Hive::playerAPI();
    const auto player = api.at("contract").get<std::string>();
    const auto control = api.at("local_control_contract").get<std::string>();
    CHECK(player.find("ctx.game.units") != std::string::npos);
    CHECK(player.find("new UUID") == std::string::npos);
    CHECK(player.find("paid follow-up") == std::string::npos);
    CHECK(control.find("hive_run") != std::string::npos);
    CHECK(control.find("expected_revision") != std::string::npos);
    CHECK(control.find("Replacement resets globals") != std::string::npos);
    CHECK(control.find("local_events") != std::string::npos);
}

TEST_CASE("local Hive failures name their source and cards summarize results" *
		  doctest::test_suite("HiveMindIntegration"))
{
	glob2test::HeadlessGlobals globals({.seed = 19});
	glob2test::HeadlessGame world({.loadDefaultRace = true, .header = true});
	world.addBuilding("swarm", 4, 4);
	world.gui.localPlayer = world.gui.localTeamNo = 0;
	world.game.players[0]->type = BasePlayer::P_LOCAL;
	Hive::LocalCommander commander(world.gui, [](Json input)
	{
		std::promise<Json> ready;
		ready.set_value(Hive::invoke(input));
		return ready.get_future();
	});
	const auto session = commander.status().at("session");
	const auto settle = [&] { commander.update(); commander.update(); };
	const auto result = [&](const char *key)
	{ return commander.call("hive_result", {{"session", session}, {"operation_key", key}}); };

	commander.call("hive_run", {{"session", session}, {"operation_key", "boom-run"},
		{"source", "function step(){throw new Error('boom');}"}});
	settle();
	REQUIRE_FALSE(commander.reports.empty());
	// The report names the operation and drops the stack trace; the journal keeps it.
	CHECK(commander.reports.back() == "boom-run: Error: boom");
	CHECK(result("boom-run").at("error").get<std::string>().find("at step") != std::string::npos);

	// A misspelled order type is named instead of failing on a field it never needed.
	commander.call("hive_run", {{"session", session}, {"operation_key", "teleport"},
		{"source", "function step(){return {orders:[{type:'teleport'}]};}"}});
	settle();
	// An unusable order is rejected on its own, and named; the run still completes.
	CHECK_FALSE(result("teleport").contains("error")); // absent on success
	REQUIRE(result("teleport").at("rejected").size() == 1);
	CHECK(result("teleport").at("rejected")[0].at("error") == "Unsupported order type 'teleport'");

	commander.call("hive_program", {{"session", session}, {"operation_key", "bad-install"},
		{"action", "install"}, {"name", "broken"}, {"source", "function step(){throw 1;}"}});
	settle();
	CHECK(commander.reports.back() == "broken: 1");
	CHECK(commander.standingOrders().empty());

	commander.call("hive_program", {{"session", session}, {"operation_key", "install"},
		{"action", "install"}, {"name", "count"},
		{"source", "let n=0;function step(ctx){n++;return {output:{n,tick:ctx.tick}};}"}});
	settle();
	world.game.stepCounter += 25;
	settle();
	const auto recent = commander.standingOrders().at(0).at("recent");
	REQUIRE(recent.at("status") == "completed");
	// Cards show the program's own output as a readable line, not JSON.
	CHECK(Hive::recentSummary(recent) == "n 1 · tick " + std::to_string(world.game.stepCounter));
	CHECK(Hive::recentSummary({{"output", {{"summary", "Inns 4, army gathering"}, {"pop", 30}}}}) ==
		  "Inns 4, army gathering");
	CHECK(Hive::recentSummary({{"output", {{"events", {1, 2, 3}}, {"ok", true}, {"nested", {{"a", 1}}}}}}) ==
		  "events 3 · ok yes");
	CHECK(Hive::recentSummary({{"output", "plain text"}}) == "plain text");
	CHECK(Hive::recentSummary({{"status", "failed"}, {"error", "TypeError: x\n    at step"},
							   {"output", nullptr}}) == "TypeError: x");
	// Online cards store {tick, output}.
	CHECK(Hive::recentSummary({{"tick", 75}, {"output", {{"swarms", 1}}}}) == "swarms 1");
	CHECK(Hive::recentSummary(nullptr).empty());
	CHECK(Hive::recentSummary({{"status", "completed"}, {"output", nullptr}}).empty());
}

TEST_CASE("local Hive standing-order reads reject a previous match session" *
		  doctest::test_suite("HiveMindIntegration"))
{
	glob2test::HeadlessGlobals globals({.seed = 19});
	glob2test::HeadlessGame world({.loadDefaultRace = true, .header = true});
	world.gui.localPlayer = world.gui.localTeamNo = 0;
	world.game.players[0]->type = BasePlayer::P_LOCAL;
	const auto worker = [](Json input)
	{
		std::promise<Json> ready;
		ready.set_value(Hive::invoke(input));
		return ready.get_future();
	};
	Json previous;
	{
		Hive::LocalCommander earlier(world.gui, worker);
		previous = earlier.status().at("session");
	}
	Hive::LocalCommander current(world.gui, worker);
	const auto session = current.status().at("session");
	REQUIRE(session != previous);
	current.call("hive_program", {{"session", session}, {"operation_key", "install"},
		{"action", "install"}, {"name", "keep"}, {"source", "function step(){return {};}"}});
	current.update(); current.update();
	REQUIRE(current.standingOrders().size() == 1);
	// An agent still holding the earlier match's session must not read this
	// match's programs as its own.
	CHECK_THROWS_WITH(current.call("hive_program", {{"action", "list"}, {"session", previous}}),
					  "Match session expired; inspect hive_status");
	CHECK_THROWS_WITH(current.call("hive_program",
					  {{"action", "inspect"}, {"session", previous}, {"name", "keep"}}),
					  "Match session expired; inspect hive_status");
	CHECK(current.call("hive_program", {{"action", "list"}, {"session", session}})
			  .at("programs").size() == 1);
	CHECK(current.call("hive_program", {{"action", "list"}}).at("programs").size() == 1);
	// The list is short: outputs and events come with inspect.
	const auto listed = current.call("hive_program", {{"action", "list"}});
	for (const auto &program : listed.at("programs"))
	{
		CHECK_FALSE(program.contains("description"));
		if (program.contains("recent"))
			CHECK_FALSE(program.at("recent").contains("output"));
	}
}

TEST_CASE("local Hive program and order errors name the current revision, field and range" *
		  doctest::test_suite("HiveMindIntegration"))
{
	glob2test::HeadlessGlobals globals({.seed = 19});
	glob2test::HeadlessGame world({.loadDefaultRace = true, .header = true});
	world.addBuilding("swarm", 4, 4);
	world.gui.localPlayer = world.gui.localTeamNo = 0;
	world.game.players[0]->type = BasePlayer::P_LOCAL;
	Hive::LocalCommander commander(world.gui, [](Json input)
	{
		std::promise<Json> ready;
		ready.set_value(Hive::invoke(input));
		return ready.get_future();
	});
	const auto session = commander.status().at("session");
	const auto settle = [&] { commander.update(); commander.update(); };
	const auto result = [&](const char *key)
	{ return commander.call("hive_result", {{"session", session}, {"operation_key", key}}); };
	const auto contains = [](const Json &record, const std::string &text)
	{
		INFO(record.dump());
		return record.value("error", std::string()).find(text) != std::string::npos;
	};
	const std::string source = "function step(){return {};}";
	commander.call("hive_program", {{"session", session}, {"operation_key", "install"}, {"action", "install"},
									{"name", "keep"}, {"source", source}});
	settle();
	REQUIRE(commander.standingOrders().size() == 1);
	// Installing over an existing name says so, and how to replace it; it fails at once.
	commander.call("hive_program", {{"session", session}, {"operation_key", "again"}, {"action", "install"},
									{"name", "keep"}, {"source", source}});
	CHECK(result("again").at("status") == "failed");
	CHECK(contains(result("again"), "already installed (revision 1)"));
	CHECK(contains(result("again"), "use action replace with expected_revision 1"));
	// A stale replace names the current revision.
	commander.call("hive_program", {{"session", session}, {"operation_key", "stale"}, {"action", "replace"},
									{"name", "keep"}, {"expected_revision", 5}, {"source", source}});
	CHECK(result("stale").at("status") == "failed");
	CHECK(contains(result("stale"), "expected_revision 5 is stale"));
	CHECK(contains(result("stale"), "now at revision 1"));
	commander.call("hive_program", {{"session", session}, {"operation_key", "missing"}, {"action", "replace"},
									{"name", "other"}, {"expected_revision", 1}, {"source", source}});
	CHECK(contains(result("missing"), "No program named \"other\""));
	// A correct replace still applies.
	commander.call("hive_program", {{"session", session}, {"operation_key", "replace"}, {"action", "replace"},
									{"name", "keep"}, {"expected_revision", 1}, {"source", source}});
	settle();
	CHECK(result("replace").at("status") == "completed");
	CHECK(commander.standingOrders().at(0).at("revision") == 2);
	commander.change("keep", "cancel");

	// Helper and order arguments outside their range say which, the range and the value.
	commander.call("hive_run", {{"session", session}, {"operation_key", "radius"},
		{"source", "function step(ctx){ctx.game.map.findPlacement('inn',{x:5,y:5,radius:100});return {};}"}});
	settle();
	CHECK(result("radius").at("status") == "failed");
	CHECK(contains(result("radius"), "findPlacement radius must be a whole number 0..64 (got 100"));
	commander.call("hive_run", {{"session", session}, {"operation_key", "workers"},
		{"source", "function step(ctx){const b=ctx.game.buildings({team:ctx.myTeam})[0];"
				   "return {orders:[{type:'workers',building:{id:b.id,generation:b.generation},workers:999}]};}"}});
	settle();
	const auto workers = result("workers");
	INFO(workers.dump());
	REQUIRE(workers.contains("rejected"));
	CHECK(workers.at("rejected")[0].at("error").get<std::string>().find(
			  "workers must be a whole number 0..20 (got 999)") != std::string::npos);
}

// Reaches the HUD's pending order the way a click's caller would.
struct HiveToolsTestAccess
{
	static GameGUIToolManager &tools(GameGUI &gui) { return gui.toolManager; }
};

TEST_CASE("local game tools report resource fields, numeric status and initial placement settings" *
		  doctest::test_suite("HiveMindIntegration"))
{
	glob2test::HeadlessGlobals globals({.seed = 19});
	PresentationFrame published; // Outlives the GUI that points at it.
	glob2test::HeadlessGame world({.wDec = 6, .hDec = 6, .teams = 2, .clearImmobile = true,
								   .loadDefaultRace = true, .header = true});
	REQUIRE(world.addBuilding("swarm", 4, 4));
	world.addUnit(WORKER);
	world.addUnit(WORKER);
	world.addUnit(WARRIOR);
	// Wood the colony can see: a 3x3 field, a 2-tile field and a lone tree. The map's
	// right half (x >= 32) stays unexplored.
	for (int y = 10; y <= 12; ++y)
		for (int x = 10; x <= 12; ++x)
			world.game.map.setResourceByIndex(x, y, WOOD, 0);
	world.game.map.setResourceByIndex(20, 4, WOOD, 0);
	world.game.map.setResourceByIndex(21, 4, WOOD, 0);
	world.game.map.setResourceByIndex(4, 20, WOOD, 0);
	const auto vision = world.game.teams[0]->me;
	world.game.map.setMapDiscovered(0, 0, 32, 64, vision);
	world.game.map.switchFogOfWar();
	world.game.map.setMapDiscovered(0, 0, 32, 64, vision);
	world.gui.localPlayer = world.gui.localTeamNo = 0;
	world.gui.adjustLocalTeam();
	world.game.players[0]->type = BasePlayer::P_LOCAL;
	world.game.snapshots().invalidateBoundary();
	world.gui.rebuildBuildingChoices(false);
	world.gui.prepareLocalPresentation(published);
	world.gui.setPublishedScene(&published);
	Hive::LocalCommander commander(world.gui, [](Json input)
	{
		std::promise<Json> ready;
		ready.set_value(Hive::invoke(input));
		return ready.get_future();
	});

	// Resources: the largest fields of each type, from what the colony has seen.
	auto state = commander.call("game_world", Json::object());
	INFO(state.at("resources").dump());
	REQUIRE(state.at("resources").contains("trees"));
	const auto &wood = state.at("resources").at("trees");
	CHECK(wood.at("tiles") == 12);
	CHECK(wood.at("field_count") == 3);
	REQUIRE(wood.at("fields").size() == 3);
	CHECK(wood.at("fields")[0].at("tiles") == 9);
	CHECK(wood.at("fields")[1].at("tiles") == 2);
	CHECK(wood.at("fields")[2].at("tiles") == 1);
	CHECK(state.at("resources_scope").at("resource_radius") == 24);
	// With an area, only what lies inside it, and the area is echoed.
	const Json area = {{"x", 18}, {"y", 0}, {"width", 8}, {"height", 8}};
	state = commander.call("game_world", {{"area", area}});
	CHECK(state.at("resources_scope").at("area") == area);
	REQUIRE(state.at("resources").contains("trees"));
	CHECK(state.at("resources").at("trees").at("tiles") == 2);
	// Nothing seen there yet says so.
	state = commander.call("game_world", {{"area", {{"x", 40}, {"y", 40}, {"width", 8}, {"height", 8}}}});
	CHECK(state.at("resources").empty());
	CHECK(state.at("resources_note").get<std::string>().find("None seen yet") != std::string::npos);

	// Status as numbers, consistent with the units by type.
	state = commander.call("game_world", Json::object());
	const auto &status = state.at("status");
	INFO(status.dump());
	const auto &free = status.at("units_free");
	CHECK(free.at("workers").at("total") == state.at("colony").at("units").at("worker"));
	CHECK(free.at("warriors").at("total") == 1);
	CHECK(free.at("explorers").at("total") == 0);
	CHECK(free.at("workers").at("free").get<int>() >= 0);
	CHECK(free.at("workers").at("free").get<int>() <= free.at("workers").at("total").get<int>());
	CHECK((status.at("win_chance").is_null() || status.at("win_chance").is_number_integer()));
	CHECK(status.at("hud_sampled").at("workers").is_string()); // the top bar's text, labelled
	CHECK(state.at("session") == commander.status().at("session"));

	// Initial settings go in the create order itself, capped as the panel caps them.
	HiveToolsTestAccess::tools(world.gui).deactivateTool();
	while (HiveToolsTestAccess::tools(world.gui).getOrder())
		;
	auto placed = commander.call("game_place", {{"building", "inn"}, {"x", 14}, {"y", 20}, {"workers", 4}, {"builders", 2}});
	INFO(placed.dump());
	auto order = std::dynamic_pointer_cast<OrderCreate>(HiveToolsTestAccess::tools(world.gui).getOrder());
	REQUIRE(order);
	CHECK(order->unitWorking == placed.at("settings").at("builders"));
	CHECK(order->unitWorkingFuture == placed.at("settings").at("workers"));
	CHECK(placed.at("settings").at("builders").get<int>() <= 2);
	placed = commander.call("game_place", {{"building", "warflag"}, {"x", 20}, {"y", 20}, {"workers", 7}, {"range", 1000},
										  {"priority", "high"}});
	order = std::dynamic_pointer_cast<OrderCreate>(HiveToolsTestAccess::tools(world.gui).getOrder());
	REQUIRE(order);
	REQUIRE(order->flagRadius);
	CHECK(*order->flagRadius == placed.at("settings").at("range"));
	CHECK(placed.at("settings").at("range_capped") == true);
	CHECK(order->unitWorking == placed.at("settings").at("workers"));
	CHECK(placed.at("settings").at("priority") == "high");
	auto refusal = [&](Json args)
	{
		try
		{
			commander.call("game_place", std::move(args));
		}
		catch (const std::exception &error)
		{
			return std::string(error.what());
		}
		return std::string("placed");
	};
	CHECK(refusal({{"building", "inn"}, {"x", 24}, {"y", 24}, {"range", 3}}).find("range is a flag's setting") !=
		  std::string::npos);
	CHECK(refusal({{"building", "warflag"}, {"x", 24}, {"y", 24}, {"builders", 3}}).find("builders") !=
		  std::string::npos);
	CHECK(refusal({{"building", "inn"}, {"x", 24}, {"y", 24}, {"workers", 99}}).find("0..20 (got 99)") !=
		  std::string::npos);
	CHECK(refusal({{"building", "inn"}, {"x", 24}, {"y", 24}, {"priority", "urgent"}}).find("priority must be") !=
		  std::string::npos);
	// "No room" names what was in the way, and suggests near only when it was not passed.
	const auto blocked = refusal({{"building", "inn"}, {"x", 10}, {"y", 10}, {"near", true}, {"search_radius", 1}});
	CHECK(blocked.find("No room for") != std::string::npos);
	CHECK(blocked.find("of 9 spots tried") != std::string::npos);
	CHECK(blocked.find("Pass near") == std::string::npos);
	CHECK(refusal({{"building", "inn"}, {"x", 10}, {"y", 10}}).find("Pass near") != std::string::npos);
	const auto unexplored = refusal({{"building", "inn"}, {"x", 40}, {"y", 10}});
	CHECK(unexplored.find("unexplored") != std::string::npos);
	CHECK(unexplored.find("Explore there first") != std::string::npos);
	while (HiveToolsTestAccess::tools(world.gui).getOrder())
		;

	// Chat refuses control characters and blank text; zones say whether they paint or erase.
	auto chatError = [&](const std::string &text)
	{
		try
		{
			commander.call("game_chat", {{"text", text}});
		}
		catch (const std::exception &error)
		{
			return std::string(error.what());
		}
		return std::string("sent");
	};
	CHECK(chatError("one\ntwo").find("control character (U+000A) at byte 3") != std::string::npos);
	CHECK(chatError(std::string("a\0b", 3)).find("control character (U+0000)") != std::string::npos);
	CHECK(chatError("   ").find("only spaces") != std::string::npos);
	CHECK(chatError("").find("empty") != std::string::npos);
	CHECK(chatError("hello") == "sent");
	const auto erased = commander.call("game_zone", {{"zone", "guard"}, {"x", 2}, {"y", 2}, {"width", 3},
													 {"height", 3}, {"mode", "erase"}});
	CHECK(erased.at("mode") == "erase");
	CHECK(erased.at("note").get<std::string>().find("erased") != std::string::npos);
	CHECK(erased.at("note").get<std::string>().find("painted") == std::string::npos);
	CHECK(commander.call("game_zone", {{"zone", "guard"}, {"x", 2}, {"y", 2}, {"width", 3}, {"height", 3}})
			  .at("note").get<std::string>().find("painted") != std::string::npos);

	// include returns only the named sections; game_stats series: false only summaries.
	const auto filtered = commander.call("game_world", {{"include", {"status", "alerts"}}});
	CHECK(filtered.contains("status"));
	CHECK(filtered.contains("alerts"));
	CHECK(filtered.contains("tick"));
	CHECK_FALSE(filtered.contains("placeable"));
	CHECK_FALSE(filtered.contains("note"));
	CHECK(filtered.size() <= 5);
	CHECK_THROWS_WITH(commander.call("game_world", {{"include", {"nope"}}}), doctest::Contains("sections: "));
	const auto summaries = commander.call("game_stats", {{"metrics", {"births"}}, {"series", false}});
	INFO(summaries.dump());
	for (const auto &metric : summaries.at("metrics"))
		for (const auto &row : metric.at("teams"))
		{
			CHECK(row.contains("summary"));
			CHECK_FALSE(row.contains("seconds"));
		}
}

TEST_CASE("local Hive names sessions, missing arguments, limits and program cadence" *
		  doctest::test_suite("HiveMindIntegration"))
{
	glob2test::HeadlessGlobals globals({.seed = 19});
	PresentationFrame published; // Outlives the GUI that points at it.
	glob2test::HeadlessGame world({.wDec = 6, .hDec = 6, .teams = 2, .discovered = true, .clearImmobile = true,
								   .loadDefaultRace = true, .header = true});
	REQUIRE(world.addBuilding("swarm", 4, 4));
	world.addUnit(WORKER);
	world.gui.localPlayer = world.gui.localTeamNo = 0;
	world.gui.adjustLocalTeam();
	world.game.players[0]->type = BasePlayer::P_LOCAL;
	world.game.snapshots().invalidateBoundary();
	world.gui.rebuildBuildingChoices(false);
	world.gui.prepareLocalPresentation(published);
	world.gui.setPublishedScene(&published);
	Hive::LocalCommander commander(world.gui, [](Json input)
	{
		std::promise<Json> ready;
		ready.set_value(Hive::invoke(input));
		return ready.get_future();
	});
	const auto session = commander.status().at("session");
	const auto settle = [&] { commander.update(); commander.update(); };
	const auto result = [&](const char *key)
	{ return commander.call("hive_result", {{"session", session}, {"operation_key", key}}); };
	const auto error = [&](const std::string &tool, const Json &args)
	{
		try
		{
			commander.call(tool, args);
		}
		catch (const std::exception &e)
		{
			return std::string(e.what());
		}
		return std::string("no error");
	};
	const auto run = [&](const char *key, const std::string &source)
	{
		commander.call("hive_run", {{"session", session}, {"operation_key", key}, {"source", source}});
		settle();
		const auto record = result(key);
		INFO(record.dump());
		return record;
	};

	// The limits section reads as prose in the table of contents, not "}".
	const auto contents = Hive::apiDocs(Json::object());
	for (const auto &section : contents.at("sections"))
		if (section.at("section") == "limits")
			CHECK(section.at("summary").get<std::string>().find("limits of local Hive scripts") != std::string::npos);
	// A session no match had is unknown, not expired; every missing argument is named.
	CHECK(error("hive_result", {{"session", "nope"}, {"operation_key", "k"}}).find("Unknown session") !=
		  std::string::npos);
	const auto missing = error("hive_program", {{"action", "install"}, {"name", "x"}});
	CHECK(missing.find("session, operation_key, source") != std::string::npos);
	commander.call("hive_program", {{"session", session}, {"operation_key", "cancel-ghost"}, {"action", "cancel"},
									{"name", "ghost"}});
	CHECK(result("cancel-ghost").at("error").get<std::string>().find("\"ghost\"") != std::string::npos);

	// Orders off the map are rejected with its size; production ratios name the field.
	auto record = run("off-map", "function step(ctx){const s=ctx.game.buildings({team:'mine'})[0];"
								 "return {orders:[{type:'create',building:'inn',x:500,y:5},"
								 "{type:'production',building:s,ratios:[20,0,0]}]};}");
	REQUIRE(record.at("rejected").size() == 2);
	CHECK(record.at("rejected")[0].at("error").get<std::string>().find("outside the 64x64 map") != std::string::npos);
	CHECK(record.at("rejected")[1].at("error").get<std::string>().find("ratios[0] (worker) must be a whole number 0..16 (got 20)") !=
		  std::string::npos);

	// map.distance reads x/y of whole records here, and names a missing coordinate.
	record = run("distance", "function step(ctx){const u=ctx.game.units({team:'mine'})[0];"
							 "return {output:ctx.game.map.distance(u,{x:u.x+3,y:u.y+4})};}");
	CHECK(record.at("output") == 5);
	record = run("distance-missing", "function step(ctx){return {output:ctx.game.map.distance({x:1},{x:2,y:3})};}");
	CHECK(record.at("error").get<std::string>().find("map.distance a.y must be a whole number -32768..32767 (got nothing)") !=
		  std::string::npos);
	// Unknown filter keys are refused; records carry type names; remembered is a filter.
	record = run("filter", "function step(ctx){return {output:ctx.game.units({tem:1})};}");
	CHECK(record.at("error").get<std::string>().find("unknown key \"tem\"") != std::string::npos);
	record = run("names", "function step(ctx){return {output:[ctx.game.units({team:'mine'})[0].typeName,"
						  "ctx.game.buildings({team:'mine'})[0].familyName,"
						  "ctx.game.buildings({team:'enemies',remembered:true}).length]};}");
	CHECK(record.at("output") == Json{"worker", "swarm", 0});
	record = run("unknown-type", "function step(ctx){return {output:ctx.game.buildingType('tower')};}");
	CHECK(record.at("error").get<std::string>().find("names: ") != std::string::npos);

	// The data limit names itself, the amount, the call and the native-helper rule, and a
	// caught crossing still fails the run, saying so.
	record = run("data", "function step(ctx){const s='x'.repeat(600000);"
						 "ctx.game.buildingType('inn',{pad:s});ctx.game.buildingType('inn',{pad:s});return {};}");
	auto text = record.at("error").get<std::string>();
	CHECK(text.find("Script data exceeds limit") != std::string::npos);
	CHECK(text.find("of 1048576 bytes") != std::string::npos);
	CHECK(text.find("crossed in buildingType") != std::string::npos);
	CHECK(text.find("script_data_bytes_per_run") != std::string::npos);
	record = run("data-caught", "function step(ctx){const s='x'.repeat(600000);"
								"try{ctx.game.buildingType('inn',{pad:s});ctx.game.buildingType('inn',{pad:s});}catch(e){}"
								"return {output:'after'};}");
	text = record.at("error").get<std::string>();
	CHECK(text.find("first crossed in buildingType") != std::string::npos);
	// The internal entry wrapper never shows in a stack trace.
	record = run("stack", "function step(){throw new Error('boom');}");
	CHECK(record.at("error").get<std::string>().find("__glob2_step") == std::string::npos);

	// Programs: no last run before the first; afterwards the next tick is not before it.
	commander.call("hive_program", {{"session", session}, {"operation_key", "install"}, {"action", "install"},
									{"name", "later"},
									{"source", "let n=0;function step(ctx){if(n++>0)throw new Error('later');return {};}"}});
	settle();
	REQUIRE(commander.standingOrders().size() == 1);
	CHECK(commander.standingOrders()[0].at("last_run_tick").is_null());
	world.game.stepCounter += 25;
	settle();
	auto program = commander.standingOrders()[0];
	INFO(program.dump());
	REQUIRE(program.at("last_run_tick").is_number());
	CHECK(program.at("nextTick").get<unsigned>() >= program.at("last_run_tick").get<unsigned>());
	CHECK(program.contains("order_lag_ticks"));
	CHECK_FALSE(program.at("recent").contains("error"));
	// The second run fails and pauses it; the alert says paused until it is resumed.
	world.game.stepCounter += 25;
	settle();
	REQUIRE(commander.standingOrders()[0].at("paused") == true);
	const auto failedAlert = [&]
	{
		const auto reply = commander.call("game_world", {{"detail", "alerts"}});
		for (const auto &alert : reply.at("alerts"))
			if (alert.at("kind") == "hive_program_failed")
				return alert;
		return Json();
	};
	REQUIRE(failedAlert().is_object());
	CHECK(failedAlert().at("paused") == true);
	CHECK(failedAlert().at("program") == "later");
	commander.change("later", "resume");
	CHECK(failedAlert().at("paused") == false);
	commander.change("later", "cancel");
	CHECK(failedAlert().at("program_state") == "removed");
}
