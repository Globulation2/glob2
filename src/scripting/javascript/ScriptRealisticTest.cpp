// SPDX-License-Identifier: GPL-3.0-or-later
#include "HeadlessGlobals.h"
#include "Map.h"
#include "HeadlessGame.h"
#include "ScriptCorpus.h"
#include "ScriptRuntime.h"
#include "ScriptObservations.h"
#include "ScriptOrders.h"
#include "MersenneTwister.h"
#include "Order.h"
#include "Player.h"
#include "Building.h"
#include <cmath>

TEST_CASE("JavaScript realistic economic planner and scenario survey [sim-trace]" *
		  doctest::test_suite("JavaScriptRealistic"))
{
	glob2test::HeadlessGlobals globals({.seed = 19});
	glob2test::HeadlessGame world({.teams = 2, .discovered = true, .loadDefaultRace = true});
	auto *home = world.addBuilding("swarm", 6, 6);
	world.addBuilding("inn", 12, 12);
	world.addBuilding("swarm", 24, 24, 0, 1);
	for (int i = 0; i < 8; ++i)
		world.addUnit(i % NB_UNIT_TYPE, 20 + i, 18 + i % 3, i % 2);
	for (int y = 0; y < 16; ++y)
		for (int x = 0; x < 16; ++x)
		{
			auto tile = world.game.map.getTile(x, y);
			tile.fertility = (x * 17 + y * 31) % 256;
			tile.resource.type = (x + y) % BASIC_COUNT;
			tile.resource.amount = (x * 3 + y * 7) % 12;
			world.game.map.replaceTile(x, y, tile);
		}
	world.game.map.setMapDiscovered(0, 0, 32, 32, ~0u);
	world.game.players[0] = new Player(0, "script", world.game.teams[0], BasePlayer::P_LOCAL);
	world.game.gameHeader.setNumberOfPlayers(1);
	world.game.gameHeader.getBasePlayer(0) = *world.game.players[0];
	for (int team : {0, -1})
	{
		CAPTURE(team);
		Script::Observations observations(world.game, team);
		MersenneTwister random;
		random.seed(19);
		Script::Host host;
		host.team = team;
		host.width = 32;
		host.height = 32;
		host.random = [&] { return random(); };
		host.query = [&](const auto &name, const auto &args, const auto &budget)
		{ return observations.query(name, args, budget); };
		auto source = glob2test::readFile(glob2test::fixture(
			team < 0 ? "javascript/map-realistic.js" : "javascript/ai-realistic.js"));
		// A second controller, never reloaded and with its own copy of the random
		// stream, is the reference the reloaded one must keep matching.
		MersenneTwister shadowRandom;
		shadowRandom.seed(19);
		auto shadowHost = host;
		shadowHost.random = [&] { return shadowRandom(); };
		auto shadowState = Script::Value::object();
		auto shadow = Script::makeRuntime();
		auto state = Script::Value::object();
		auto runtime = Script::makeRuntime();
		for (int tick = 0; tick < 4; ++tick)
		{
			CAPTURE(tick);
			host.tick = world.game.stepCounter;
			observations.observe();
			auto result = runtime->invoke(source, state, tick == 0, host);
			auto data = runtime->inspectGlobals();
			auto uninterrupted = shadow->invoke(source, shadowState, tick == 0, shadowHost);
			shadowState = std::move(uninterrupted.state);
			CHECK(result.effects.encode() == uninterrupted.effects.encode());
			CHECK(data.encode() == shadow->inspectGlobals().encode());
			CHECK(data.get("calls").number == tick + 1);
			auto label = std::string(team < 0 ? "realistic-survey-" : "realistic-economy-") +
						 std::to_string(tick);
			glob2test::script::retain(
				label, Script::Value::object().set("state", data).set("effects", result.effects));
			if (team >= 0)
			{
				auto accepted = Script::order(world.game, team, result.effects);
				REQUIRE(accepted->getOrderType() == ORDER_MODIFY_BUILDING);
				accepted->sender = 0;
				world.game.executeOrder(accepted, -1);
				CHECK(home->maxUnitWorking == data.get("metrics").get("workers").number);
				const auto &scores = data.get("scores").items;
				REQUIRE(scores.size() > 0);
				CHECK(scores.size() <= 8);
				// Ranked best first; every candidate is a visible stock near home.
				for (size_t i = 0; i < scores.size(); ++i)
				{
					CHECK(scores[i].get("value").number > 0);
					CHECK(scores[i].get("distance").number <= std::hypot(4.0, 4.0));
					if (i)
						CHECK(scores[i - 1].get("value").number >= scores[i].get("value").number);
				}
			}
			else
			{
				// Three real colonies, each within the torus and at full health.
				const auto &colonies = data.get("colonies").items;
				CHECK(colonies.size() == 3);
				for (const auto &colony : colonies)
				{
					CHECK(colony.get("distance").number >= 0);
					CHECK(colony.get("distance").number < std::hypot(32.0, 32.0));
					CHECK(colony.get("health").number > 0);
					CHECK(colony.get("health").number <= 1);
				}
				CHECK(data.get("density").number > 0);
			}
			state = Script::Value::decode(result.state.encode());
			if (tick == 1)
				runtime = Script::makeRuntime();
			world.step();
		}
	}
}
