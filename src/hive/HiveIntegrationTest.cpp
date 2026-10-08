#include "hive/HiveClient.h"
#include "online/InstanceConfig.h"
#include "online/OnlineStorage.h"
#include "online/PlatformClient.h"
#include <atomic>
#include <thread>
#include <chrono>
// SPDX-License-Identifier: GPL-3.0-or-later
#include "EngineFixtures.h"
#include "hive/HiveObservation.h"
#include "scripting/javascript/ScriptObservations.h"
#include "scripting/javascript/ScriptOrders.h"
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
#include <ScreenStack.h>
TEST_CASE("Hive Mind commander panel [display]" * doctest::test_suite("HiveMindPresentation"))
{
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
