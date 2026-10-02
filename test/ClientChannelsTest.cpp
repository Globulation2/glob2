// SPDX-License-Identifier: GPL-3.0-or-later
// The simulation/client channels in src/sim: GameEvents delivered through
// ClientEvents, script presentation through ClientCommandSink, and the SGSL
// Space acknowledgement through ClientRequests.
#include "EngineFixtures.h"
#include "EngineTiming.h"
#include "GameGUI.h"
#include "Order.h"
#include "sim/ClientCommandSink.h"
#include "sim/ClientEvents.h"
#include "sim/ClientRequests.h"

#include <string>
#include <vector>

namespace
{
	struct RecordingSink : ClientCommandSink
	{
		std::vector<std::string> calls;
		void enableBuildingsChoice(const std::string &name) override { calls.push_back("enableBuilding " + name); }
		void disableBuildingsChoice(const std::string &name) override { calls.push_back("disableBuilding " + name); }
		void enableFlagsChoice(const std::string &name) override { calls.push_back("enableFlag " + name); }
		void disableFlagsChoice(const std::string &name) override { calls.push_back("disableFlag " + name); }
		void enableGUIElement(int id) override { calls.push_back("enableElement " + std::to_string(id)); }
		void disableGUIElement(int id) override { calls.push_back("disableElement " + std::to_string(id)); }
		void setHighlight(int highlight, bool on) override
		{
			calls.push_back("highlight " + std::to_string(highlight) + (on ? " on" : " off"));
		}
		void setSwallowSpaceKey(bool value) override { calls.push_back(value ? "swallow on" : "swallow off"); }
		void showScriptText(const std::string &text) override { calls.push_back("show " + text); }
		void showScriptTextTr(const std::string &text, const std::string &) override { calls.push_back("show " + text); }
		void hideScriptText() override { calls.push_back("hide"); }
		void setScriptPresentationText(std::string text, bool) override { calls.push_back("text " + text); }
		bool isBuildingEnabled(const std::string &) override { return true; }
		bool isFlagEnabled(const std::string &) override { return true; }

		bool saw(const std::string &call) const
		{
			for (const auto &c : calls)
				if (c == call)
					return true;
			return false;
		}
	};
}

struct ClientChannelsFixture
{
	static std::deque<GameEvent> &pending(GameGUI &gui, int team) { return gui.pendingTeamEvents[team]; }
};

TEST_SUITE("ClientChannels")
{
	TEST_CASE("LosslessQueue hands every item over once, in order")
	{
		LosslessQueue<int> queue;
		for (int i = 0; i < 5; ++i)
			queue.push(i);
		std::vector<int> seen;
		queue.drain([&](int v) { seen.push_back(v); });
		CHECK(seen == std::vector<int>{0, 1, 2, 3, 4});
		CHECK(queue.empty());
		queue.drain([&](int v) { seen.push_back(v); });
		CHECK(seen.size() == 5);
	}

	TEST_CASE("team events reach the GUI exactly once, in order, and age like Team::updateEvents")
	{
		glob2test::HeadlessGlobals globals;
		glob2test::GameOptions options;
		options.teams = 2;
		glob2test::HeadlessGame world(options);
		Team *team = world.game.teams[0];
		const Uint32 tick = world.game.stepCounter;
		team->pushGameEvent(GameEvent::unitUnderAttack(tick, 1, 2, WORKER));
		team->pushGameEvent(GameEvent::buildingCompleted(tick, 3, 4, 0));
		team->pushGameEvent(GameEvent::unitUnderAttack(tick, 5, 6, WORKER)); // cooldown: dropped
		world.game.teams[1]->pushGameEvent(GameEvent::buildingUnderAttack(tick, 7, 8, 0));
		world.step();

		CHECK_FALSE(team->getEvent()); // the simulation handed them over
		auto &mine = ClientChannelsFixture::pending(world.gui, 0);
		REQUIRE(mine.size() == 2);
		CHECK(mine[0].getEventType() == GEUnitUnderAttack);
		CHECK(mine[0].getX() == 1);
		CHECK(mine[1].getEventType() == GEBuildingCompleted);
		REQUIRE(ClientChannelsFixture::pending(world.gui, 1).size() == 1);

		// Consuming again delivers nothing twice.
		world.gui.consumeClientEvents();
		CHECK(mine.size() == 2);

		// The music pulse mirrors Team::wasRecentEvent at the end of the tick.
		const auto pulse = world.gui.clientEvents.pulse();
		REQUIRE(pulse.valid);
		CHECK(pulse.recentEvents[0][GEUnitUnderAttack] == team->wasRecentEvent(GEUnitUnderAttack));
		CHECK(pulse.recentEvents[1][GEBuildingUnderAttack] == world.game.teams[1]->wasRecentEvent(GEBuildingUnderAttack));

		// Undelivered events expire after GAME_EVENT_MAX_AGE_TICKS ticks, as they did
		// in Team::events.
		world.step(GAME_EVENT_MAX_AGE_TICKS - 1);
		CHECK(mine.size() == 2);
		world.step(2);
		CHECK(mine.empty());
	}

	TEST_CASE("scripts reach the client only through ClientCommandSink")
	{
		glob2test::GlobalsOptions globalsOptions;
		globalsOptions.loadStrings = true;
		glob2test::HeadlessGlobals globals(globalsOptions);
		glob2test::HeadlessGame world;
		RecordingSink sink;

		Script::JavaScriptMap script;
		script.step(R"(export function step(c,s){return [{type:'buildingChoice',name:'swarm',enabled:false}, {type:'guiElement',id:1,enabled:false}, {type:'message',text:'Hello'}];})",
					world.game, sink);
		CHECK(sink.saw("disableBuilding swarm"));
		CHECK(sink.saw("enableBuilding inn"));
		CHECK(sink.saw("disableElement 1"));
		CHECK(sink.saw("text Hello"));

		// SGSL: highlights and the Space wait go through the sink and requests.
		sink.calls.clear();
		ClientRequests requests;
		auto &legacy = world.game.sgslScript;
		legacy.sourceCode = R"(hilightUnits(Worker) guiDisable(Swarm) space unhilightUnits(Worker))";
		REQUIRE(legacy.compileScript(&world.game).type == ErrorReport::ET_OK);
		legacy.syncStep(world.game, sink, requests);
		CHECK(sink.saw("highlight " + std::to_string(GameGUI::HighlightWorkers) + " on"));
		CHECK(sink.saw("disableBuilding swarm"));
		CHECK(sink.saw("swallow on"));
		CHECK_FALSE(sink.saw("highlight " + std::to_string(GameGUI::HighlightWorkers) + " off"));
		sink.calls.clear();
		requests.requestScriptSpace();
		requests.requestScriptSpace(); // several presses count as one acknowledgement
		legacy.syncStep(world.game, sink, requests);
		CHECK_FALSE(requests.scriptSpacePending());
		CHECK(sink.saw("swallow off"));
		CHECK(sink.saw("highlight " + std::to_string(GameGUI::HighlightWorkers) + " off"));

		// The game's own client sink is the GUI.
		CHECK(world.game.clientSink == static_cast<ClientCommandSink *>(&world.gui));
		CHECK(world.game.clientEvents == &world.gui.clientEvents);
		CHECK(world.game.clientRequests == &world.gui.clientRequests);
	}

	TEST_CASE("order side effects reach the GUI as events")
	{
		glob2test::HeadlessGlobals globals;
		glob2test::GameOptions options;
		options.header = true;
		glob2test::HeadlessGame world(options);
		GameGUI &gui = world.gui;
		gui.localPlayer = 0;
		gui.localTeamNo = 0;

		auto pause = std::make_shared<PauseGameOrder>(true);
		pause->sender = 0;
		const Uint32 tick = world.game.stepCounter;
		gui.executeOrder(pause);
		CHECK(gui.gamePaused);
		CHECK(gui.clientEvents.empty());
		auto resume = std::make_shared<PauseGameOrder>(false);
		resume->sender = 0;
		gui.executeOrder(resume);
		CHECK_FALSE(gui.gamePaused);
		CHECK(world.game.stepCounter == tick);

		// Without a client, order execution publishes nothing.
		Game detached(nullptr);
		CHECK(detached.clientEvents == nullptr);
		detached.publishClientEvent(ClientEvent::PauseChanged{true});
	}
}
