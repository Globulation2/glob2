// SPDX-License-Identifier: GPL-3.0-or-later
#include "EngineFixtures.h"
#include "AIOrderScheduler.h"
#include "Order.h"
#include <BinaryStream.h>
#include <StreamBackend.h>
#include <atomic>
#include <chrono>
#include <thread>

namespace
{
std::shared_ptr<const AIEngine::AIWorldView> observation(Uint32 tick)
{
	SimulationSnapshot::Handle handle;
	handle.tick = tick;
	handle.width = handle.height = 1;
	return std::make_shared<AIEngine::AIWorldView>(std::move(handle));
}
AIEngine::Command nullCommand() { return {{ORDER_NULL}, {}, {}}; }
}

TEST_SUITE("AIOrderScheduler")
{
	TEST_CASE("all delays preserve every FIFO poll and fixed player ordered deadlines")
	{
		for (unsigned delay = 0; delay <= 8; ++delay)
			for (unsigned workers : {0u, 1u, 4u})
			{
				CAPTURE(delay); CAPTURE(workers);
				AIEngine::OrderScheduler scheduler;
				scheduler.configure(delay, workers);
				std::array<Uint32, 2> next{};
				for (Uint32 tick = 0; tick < 20 + delay; ++tick)
				{
					if (tick < 20) for (unsigned player : {1u, 0u})
						scheduler.submit({player, 1, tick, tick, 0}, observation(tick), [&, player](const auto& world) {
							if (workers && player == 1) std::this_thread::sleep_for(std::chrono::milliseconds(1));
							if (next[player]++ != world.tick) throw std::logic_error("controller lane overtook a poll");
							return nullCommand();
						});
					const auto due = scheduler.takeDue(tick);
					if (tick < delay) CHECK(due.empty());
					else {
						REQUIRE(due.size() == 2);
						CHECK(due[0].request.player == 0); CHECK(due[1].request.player == 1);
						CHECK(due[0].request.observedTick == tick - delay);
						CHECK(due[1].dueTick == tick);
					}
					CHECK(scheduler.pendingCount() <= 2 * delay);
				}
				CHECK(next[0] == 20); CHECK(next[1] == 20);
				CHECK(scheduler.metrics.maximumPending <= 2 * (delay + 1));
			}
	}
	TEST_CASE("save drains computation and retains future outputs without recomputing")
	{
		AIEngine::OrderScheduler scheduler;
		scheduler.configure(8, 2);
		std::atomic<unsigned> calls{0};
		scheduler.submit({0, 4, 0, Uint64(1) << 40, 0}, observation(0), [&](const auto&) {
			++calls;
			auto command = nullCommand();
			command.resourceEnrollments.push_back({0, 1, 0, 0, std::make_shared<const std::vector<Uint16>>(4, 42)});
			return command;
		});
		CHECK(scheduler.takeDue(0).empty());
		auto* backend = new GAGCore::MemoryStreamBackend;
		GAGCore::BinaryOutputStream output(backend);
		scheduler.save(&output); output.flush();
		const auto bytes = backend->takeContents();
		CHECK(calls == 1); CHECK(scheduler.pendingCount() == 1);
		AIEngine::OrderScheduler restored;
		GAGCore::BinaryInputStream input(new GAGCore::MemoryStreamBackend(bytes.data(), bytes.size()));
		input.seekFromStart(0); REQUIRE(restored.load(&input));
		std::array<std::pair<Uint32, Uint64>, 32> actorState{};
		actorState[0] = {4, (Uint64(1) << 40) + 1};
		CHECK(restored.validateRestoredState(1, 0, actorState, 2, 2, 1));
		CHECK_FALSE(restored.validateRestoredState(1, 0, actorState, 3, 2, 1));
		actorState[0].first = 5;
		CHECK_FALSE(restored.validateRestoredState(1, 0, actorState, 2, 2, 1));
		actorState[0].first = 4; --actorState[0].second;
		CHECK_FALSE(restored.validateRestoredState(1, 0, actorState, 2, 2, 1));
		for (Uint32 tick = 1; tick < 8; ++tick) CHECK(restored.takeDue(tick).empty());
		const auto due = restored.takeDue(8);
		REQUIRE(due.size() == 1); CHECK(calls == 1);
		CHECK(due[0].request.pollSequence == (Uint64(1) << 40));
		REQUIRE(due[0].command.resourceEnrollments.size() == 1);
		CHECK(*due[0].command.resourceEnrollments[0].initialField == std::vector<Uint16>(4, 42));
	}
    TEST_CASE("saved repeated enrollments retain the first initialization tick")
    {
        for(unsigned delay:{1u,8u}) {
            AIEngine::OrderScheduler scheduler;scheduler.configure(delay,0);
            const auto plane=std::make_shared<const std::vector<Uint16>>(4,42);
            for(Uint32 tick=0;tick<delay;++tick) {
                scheduler.submit({0,1,tick,tick,0},observation(tick),[plane](const auto&) {
                    auto command=nullCommand();command.resourceEnrollments.push_back({0,1,0,0,plane});return command;
                });
                CHECK(scheduler.takeDue(tick).empty());
            }
            auto* backend=new GAGCore::MemoryStreamBackend;
            GAGCore::BinaryOutputStream output(backend);scheduler.save(&output);output.flush();
            const auto bytes=backend->takeContents();
            GAGCore::BinaryInputStream input(new GAGCore::MemoryStreamBackend(bytes.data(),bytes.size()));input.seekFromStart(0);
            AIEngine::OrderScheduler restored;REQUIRE(restored.load(&input));
            std::shared_ptr<const std::vector<Uint16>> loadedPlane;
            for(Uint32 tick=delay;tick<delay*2;++tick) {
                const auto due=restored.takeDue(tick);REQUIRE(due.size()==1);
                REQUIRE(due.front().command.resourceEnrollments.size()==1);
                CHECK(due.front().command.resourceEnrollments.front().observedTick==0);
                const auto& current=due.front().command.resourceEnrollments.front().initialField;
                CHECK(*current==*plane);
                if(loadedPlane) CHECK(current==loadedPlane);
                else loadedPlane=current;
            }
        }
        for(bool duplicate:{false,true}) {
            AIEngine::OrderScheduler malformed;malformed.configure(8,0);
            malformed.submit({0,1,0,0,0},observation(0),[duplicate](const auto&) {
                auto command=nullCommand();
                command.resourceEnrollments.push_back({0,1,0,duplicate?0u:1u,std::make_shared<const std::vector<Uint16>>(4,42)});
                if(duplicate) command.resourceEnrollments.push_back(command.resourceEnrollments.front());
                return command;
            });
            malformed.takeDue(0);
            auto* backend=new GAGCore::MemoryStreamBackend;
            GAGCore::BinaryOutputStream output(backend);malformed.save(&output);output.flush();const auto bytes=backend->takeContents();
            GAGCore::BinaryInputStream input(new GAGCore::MemoryStreamBackend(bytes.data(),bytes.size()));input.seekFromStart(0);
            AIEngine::OrderScheduler restored;CHECK_FALSE(restored.load(&input));
        }
    }
    TEST_CASE("same initialization identity cannot restore contradictory planes") {
        AIEngine::OrderScheduler scheduler;scheduler.configure(8,0);
        for(Uint32 tick=0;tick<2;++tick) {
            scheduler.submit({0,1,tick,tick,0},observation(tick),[tick](const auto&) {
                auto command=nullCommand();
                command.resourceEnrollments.push_back({0,1,0,0,std::make_shared<const std::vector<Uint16>>(4,42+tick)});
                return command;
            });
            scheduler.takeDue(tick);
        }
        auto* backend=new GAGCore::MemoryStreamBackend;
        GAGCore::BinaryOutputStream output(backend);scheduler.save(&output);output.flush();const auto bytes=backend->takeContents();
        GAGCore::BinaryInputStream input(new GAGCore::MemoryStreamBackend(bytes.data(),bytes.size()));input.seekFromStart(0);
        AIEngine::OrderScheduler restored;CHECK_FALSE(restored.load(&input));
    }

	TEST_CASE("failure joins work and cannot partially publish or lose the original error")
	{
		AIEngine::OrderScheduler scheduler;
		scheduler.configure(0, 2);
		std::atomic<bool> completed{false};
		scheduler.submit({0, 1, 0}, observation(0), [](const auto&) -> AIEngine::Command { throw std::runtime_error("decision failed"); });
		scheduler.submit({1, 1, 0}, observation(0), [&](const auto&) { completed = true; return nullCommand(); });
		CHECK_THROWS_WITH(scheduler.takeDue(0), "decision failed");
		CHECK(completed); CHECK(scheduler.pendingCount() == 2);
		CHECK_THROWS_WITH(scheduler.takeDue(0), "decision failed");
	}
	TEST_CASE("cancellation joins the old controller generation before replacement")
	{
		AIEngine::OrderScheduler scheduler;
		scheduler.configure(4, 2);
		std::atomic<unsigned> calls{0};
		scheduler.submit({0, 1, 0}, observation(0), [&](const auto&) { ++calls; return nullCommand(); });
		scheduler.cancel(0, 1);
		CHECK(calls == 1); CHECK(scheduler.pendingCount() == 0);
		scheduler.submit({0, 2, 0}, observation(0), [](const auto&) { return nullCommand(); });
		for (unsigned tick = 0; tick < 4; ++tick) CHECK(scheduler.takeDue(tick).empty());
		REQUIRE(scheduler.takeDue(4).size() == 1);
	}
	TEST_CASE("restore refuses truncated duplicate and out of window pending requests")
	{
		AIEngine::OrderScheduler scheduler;
		scheduler.configure(8, 0);
		scheduler.submit({0, 1, 0, 0, 0}, observation(0), [](const auto&) { return nullCommand(); });
		scheduler.takeDue(0);
		scheduler.submit({0, 1, 1, 1, 0}, observation(1), [](const auto&) { return nullCommand(); });
		scheduler.takeDue(1);
		auto* backend = new GAGCore::MemoryStreamBackend;
		GAGCore::BinaryOutputStream output(backend);
		scheduler.save(&output); output.flush();
		const auto valid = backend->takeContents();
		const auto restore = [](const std::string& bytes) {
			AIEngine::OrderScheduler restored;
			GAGCore::BinaryInputStream input(new GAGCore::MemoryStreamBackend(bytes.data(), bytes.size()));
			input.seekFromStart(0);
			return restored.load(&input);
		};
		REQUIRE(restore(valid));
		CHECK_THROWS_AS(restore(valid.substr(0, valid.size() - 1)), std::ios_base::failure);
		const auto put32 = [](std::string& bytes, std::size_t offset, Uint32 value) {
			for (unsigned i = 0; i < 4; ++i) bytes.at(offset + i) = char(value >> (24 - 8 * i));
		};
		// Binary sections carry no labels. Header: delay, two clock options,
		// one six-word controller record, pending count; each null entry is 48 bytes.
		constexpr std::size_t controllerGeneration = 22, firstPending = 46, secondPending = firstPending + 48;
		auto bad = valid; put32(bad, controllerGeneration, 0);
		CHECK_FALSE(restore(bad));
		bad = valid; put32(bad, 5, 10); // observed requests older than the eight-tick window
		CHECK_FALSE(restore(bad));
		bad = valid;
		put32(bad, secondPending + 8, 0); put32(bad, secondPending + 12, 8);
		put32(bad, secondPending + 16, 0);
		CHECK_FALSE(restore(bad)); // duplicate controller tick and sequence
		bad = valid; put32(bad, firstPending + 16, 2);
		CHECK_FALSE(restore(bad)); // pending sequence ahead of the latest submitted request
	}

}
