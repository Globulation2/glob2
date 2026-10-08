// SPDX-License-Identifier: GPL-3.0-or-later
// The simulation/client channels in src/engine/sim: GameEvents delivered through
// ClientEvents, script presentation through ClientCommandSink, and the SGSL
// Space acknowledgement through ClientRequests.
#include "EngineFixtures.h"
#include "EngineTiming.h"
#include "GameGUI.h"
#include "Order.h"
#include "sim/ClientCommandSink.h"
#include "sim/ClientEvents.h"
#include "sim/ClientOrderQueue.h"
#include "sim/ClientRequests.h"
#include "sim/ScriptClientChannel.h"

#include <string>
#include <atomic>
#include <thread>
#include <ThreadSupport.h>
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
    TEST_CASE("client orders cross the producer consumer boundary once in FIFO order")
    {
        if (!GAGCore::ThreadSupport::available) return;
        ClientOrderQueue orders;
        std::thread producer([&] {
            for (int i=0;i<10000;++i) orders.push_back(std::make_shared<OrderChangePriority>(0,i));
        });
        for (int i=0;i<10000;)
        {
            auto order=orders.take();
            if (!order) { std::this_thread::yield(); continue; }
            CHECK(std::static_pointer_cast<OrderChangePriority>(order)->priority == i++);
        }
        producer.join();
        CHECK(orders.empty());
    }
    TEST_CASE("flag coalescing preserves incarnation and queue position")
    {
        ClientOrderQueue orders;
        auto first=std::make_shared<OrderMoveFlag>(4,1,1,false);
        first->clientTarget=BuildingRef{4,1}; orders.moveFlag(first);
        orders.push_back(std::make_shared<NullOrder>());
        auto replacement=std::make_shared<OrderMoveFlag>(4,2,2,true);
        replacement->clientTarget=BuildingRef{4,1}; orders.moveFlag(replacement);
        auto newcomer=std::make_shared<OrderMoveFlag>(4,3,3,false);
        newcomer->clientTarget=BuildingRef{4,2}; orders.moveFlag(newcomer);
        CHECK(orders.size()==3);
        CHECK(orders.take()==replacement);
        CHECK(orders.take()->getOrderType()==ORDER_NULL);
        CHECK(orders.take()==newcomer);
    }

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

	TEST_CASE("queue callbacks can publish the next bounded batch")
	{
		LosslessQueue<int> queue;
		queue.push(1);
		std::vector<int> seen;
		queue.drain([&](int value) { seen.push_back(value); queue.push(2); });
		CHECK(seen == std::vector<int>{1});
		CHECK(queue.size() == 1);
		queue.drain([&](int value) { seen.push_back(value); });
		CHECK(seen == std::vector<int>{1, 2});
		CHECK(queue.empty());
	}

	TEST_CASE("cleared queues discard pending events and accept a fresh batch")
	{
		LosslessQueue<int> queue;
		queue.push(1);
		queue.push(2);
		queue.clear();
		CHECK(queue.empty());
		std::vector<int> seen;
		queue.drain([&](int value) { seen.push_back(value); });
		CHECK(seen.empty());
		queue.clear();
		queue.push(3);
		queue.drain([&](int value) { seen.push_back(value); });
		CHECK(seen == std::vector<int>{3});
		CHECK(queue.empty());
	}

	TEST_CASE("simultaneous producers retain their order without lost or duplicate events")
	{
		if constexpr (!GAGCore::ThreadSupport::available) return;
		LosslessQueue<std::pair<unsigned, unsigned>> queue;
		std::atomic<unsigned> finished{0};
		constexpr unsigned count = 10000;
		auto produce = [&](unsigned producer) {
			for (unsigned i = 0; i < count; ++i) queue.push({producer, i});
			finished.fetch_add(1, std::memory_order_release);
		};
		std::thread first(produce, 0), second(produce, 1);
		std::array<unsigned, 2> received{};
		bool ordered = true;
		do {
			queue.drain([&](const auto &value) {
				ordered &= value.second == received[value.first]++;
			});
			std::this_thread::yield();
		} while (finished.load(std::memory_order_acquire) != 2 || !queue.empty());
		first.join();
		second.join();
		CHECK(ordered);
		CHECK(received == std::array<unsigned, 2>{count, count});
	}

	TEST_CASE("concurrent event delivery preserves every value and coherent pulses")
	{
		if constexpr (!GAGCore::ThreadSupport::available) return;
		ClientEvents events;
		std::atomic<bool> done{false};
		constexpr unsigned count = 10000;
		std::thread producer([&] {
			for (unsigned i = 0; i < count; ++i)
			{
				events.push(ClientEvent::BuildingRemoved{Uint16(i)});
				ClientEvents::TickPulse pulse;
				pulse.valid = true;
				pulse.tick = i;
				for (auto &team : pulse.recentEvents) team.fill((i & 1) != 0);
				events.publishPulse(pulse);
			}
			done.store(true, std::memory_order_release);
		});
		unsigned received = 0;
		bool ordered = true, coherent = true;
		do {
			events.drain([&](ClientEventVariant&& event) {
				ordered &= std::get<ClientEvent::BuildingRemoved>(event).gid == received++;
			});
			const auto pulse = events.pulse();
			if (pulse.valid)
				for (const auto &team : pulse.recentEvents)
					for (bool value : team) coherent &= value == bool(pulse.tick & 1);
			std::this_thread::yield();
		} while (!done.load(std::memory_order_acquire) || !events.empty());
		producer.join();
		CHECK(ordered);
		CHECK(coherent);
		CHECK(received == count);
	}

	TEST_CASE("script queries observe commands before the client consumes them")
	{
		RecordingSink sink;
		ClientEvents events;
		ScriptClientChannel channel(&sink);
		channel.start(events, {{"inn-a", "inn", true}, {"inn-b", "inn", true}}, {{"warflag", "war", false}});
		channel.disableBuildingsChoice("inn");
		channel.enableFlagsChoice("warflag");
		channel.showScriptText("hello");
		CHECK_FALSE(channel.isBuildingEnabled("inn-a"));
		CHECK_FALSE(channel.isBuildingEnabled("inn-b"));
		CHECK_FALSE(channel.isBuildingEnabled("unknown"));
		CHECK(channel.isFlagEnabled("war"));
		CHECK(sink.calls.empty());
		channel.enableBuildingsChoice("inn-b");
		CHECK_FALSE(channel.isBuildingEnabled("inn")); // first matching alias, as before
		CHECK(channel.isBuildingEnabled("inn-b"));
		events.drain([&](ClientEventVariant&& event) { std::get<ScriptPresentation>(event).apply(sink); });
		CHECK(sink.calls == std::vector<std::string>{"disableBuilding inn", "enableFlag warflag", "show hello", "enableBuilding inn-b"});
		channel.stop();
		channel.hideScriptText();
		CHECK(sink.calls.back() == "hide");
		CHECK(events.empty());
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

		// All interpreters use the same stable endpoint; serial mode forwards to the GUI.
		CHECK(world.game.clientSink == static_cast<ClientCommandSink *>(&world.game.scriptClient));
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
