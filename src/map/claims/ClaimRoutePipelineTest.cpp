// SPDX-License-Identifier: GPL-3.0-or-later
#include "EngineFixtures.h"
#include "ClaimRoutePipeline.h"

TEST_SUITE("ClaimRoutePipeline")
{
    TEST_CASE("background requests enforce their deterministic expansion budget")
    {
        glob2test::HeadlessGlobals globals;
        glob2test::HeadlessGame fixture({.loadDefaultRace=true, .header=true});
        const auto world = SimulationSnapshot::capture(fixture.game, SimulationSnapshot::captureCatalog(fixture.game));
        for (unsigned threads : {1u, 3u}) {
            ComputeExecutor executor; executor.configure(threads);
            ResourceClaims::RoutePipeline pipeline(executor);
            ResourceClaims::RouteInput input{world, {}, 0, 10};
            input.maximumExpansions = 1;
            pipeline.submit(1, 1, 10, input);
            const auto ready = pipeline.publish(11);
            REQUIRE(ready.size() == 1);
            CHECK(ready[0].route.status == ResourceClaims::RouteStatus::ExpansionLimit);
            CHECK(ready[0].route.expansions == 1);
            CHECK(ready[0].route.cells.empty());
            input.maximumExpansions = 0;
            CHECK_THROWS_AS(pipeline.submit(1, 2, 11, input), std::invalid_argument);
        }
    }
    TEST_CASE("saving a fully cancelled batch retains request identity and its sealed tick")
    {
        glob2test::HeadlessGlobals globals;
        glob2test::HeadlessGame fixture({.loadDefaultRace=true, .header=true});
        const auto world = SimulationSnapshot::capture(fixture.game, SimulationSnapshot::captureCatalog(fixture.game));
        ComputeExecutor executor; executor.configure(1);
        ResourceClaims::RoutePipeline original(executor), restored(executor);
        ResourceClaims::RouteInput input{world, {}, 0, 10};
        const auto first = original.submit(1, 1, 10, input);
        original.cancel(1);
        const auto pending = original.exportPending();
        REQUIRE(pending.empty());
        restored.restore(pending);
        restored.restoreControl(original.requestSequence(), original.sealedSubmissionTick());
        CHECK_THROWS_AS(restored.submit(1, 2, 10, input), std::logic_error);
        CHECK(original.publish(11).empty());
        const auto a = original.submit(1, 2, 11, input);
        const auto b = restored.submit(1, 2, 11, input);
        CHECK(a == b); CHECK(a.request > first.request);
        CHECK_THROWS_AS(restored.restoreControl(0, std::nullopt), std::invalid_argument);
    }
    TEST_CASE("thousands of same tick paths share one executor batch")
    {
        glob2test::HeadlessGlobals globals;
        glob2test::HeadlessGame fixture({.loadDefaultRace=true, .header=true});
        const auto world = SimulationSnapshot::capture(fixture.game, SimulationSnapshot::captureCatalog(fixture.game));
        ComputeExecutor executor; executor.configure(3);
        ResourceClaims::RoutePipeline pipeline(executor);
        ResourceClaims::RouteInput input{world, {}, 0, 0};
        for (unsigned unit = 0; unit < 2000; ++unit) pipeline.submit(unit, 1, 10, input);
        CHECK(executor.liveBatches() == 0);
        pipeline.flush();
        CHECK(executor.liveBatches() == 1);
        CHECK_THROWS_AS(pipeline.submit(3000, 1, 10, input), std::logic_error);
        CHECK_THROWS_AS(pipeline.submit(3000, 1, 11, input), std::logic_error);
        CHECK(pipeline.publish(10).empty());
        const auto ready = pipeline.publish(11);
        REQUIRE(ready.size() == 2000);
        for (unsigned i = 0; i < ready.size(); ++i) {
            CHECK(ready[i].identity.unit == i);
            CHECK(ready[i].route.status == ResourceClaims::RouteStatus::Found);
        }
        CHECK(executor.liveBatches() == 0);
    }
    TEST_CASE("worker count does not change fixed publication or saved pending paths")
    {
        glob2test::HeadlessGlobals globals;
        glob2test::HeadlessGame fixture({.loadDefaultRace=true, .header=true});
        const auto world = SimulationSnapshot::capture(fixture.game, SimulationSnapshot::captureCatalog(fixture.game));
        ResourceClaims::Route expected;
        for (unsigned threads : {1u, 3u}) {
            ComputeExecutor executor; executor.configure(threads);
            ResourceClaims::RoutePipeline pipeline(executor);
            ResourceClaims::RouteInput input{world, {}, 0, 10};
            const auto identity = pipeline.submit(123, 456, 12, input);
            CHECK(identity.unit == 123); CHECK(identity.task == 456);
            CHECK(pipeline.publish(12).empty());
            const auto saved = pipeline.exportPending();
            REQUIRE(saved.size() == 1); CHECK(saved[0].due == 13);
            CHECK(pipeline.publish(12).empty()); // Save join did not publish.
            const auto completed = pipeline.publish(13);
            REQUIRE(completed.size() == 1);
            CHECK(completed[0].identity == identity);
            REQUIRE(completed[0].route.status == ResourceClaims::RouteStatus::Found);
            if (threads == 1) expected = completed[0].route;
            else {
                CHECK(completed[0].route.cells == expected.cells);
                CHECK(completed[0].route.cost == expected.cost);
                CHECK(completed[0].route.expansions == expected.expansions);
            }
            ResourceClaims::RoutePipeline restored(executor);
            restored.restore(saved);
            CHECK(restored.publish(12).empty());
            const auto resumed = restored.publish(13);
            REQUIRE(resumed.size() == 1); CHECK(resumed[0].route.cells == completed[0].route.cells);
            CHECK(resumed[0].identity == identity);
        }
    }
    TEST_CASE("partially canceled groups retain only surviving saved results")
    {
        glob2test::HeadlessGlobals globals;
        glob2test::HeadlessGame fixture({.loadDefaultRace=true, .header=true});
        const auto world = SimulationSnapshot::capture(fixture.game, SimulationSnapshot::captureCatalog(fixture.game));
        ComputeExecutor executor; executor.configure(3);
        ResourceClaims::RoutePipeline pipeline(executor);
        ResourceClaims::RouteInput input{world, {}, 0, 10};
        for (unsigned unit : {1u, 2u, 3u}) pipeline.submit(unit, 1, 10, input);
        pipeline.flush(); pipeline.cancel(2);
        const auto saved = pipeline.exportPending();
        REQUIRE(saved.size() == 2);
        CHECK(saved[0].identity.unit == 1); CHECK(saved[1].identity.unit == 3);
        ResourceClaims::RoutePipeline restored(executor); restored.restore(saved);
        CHECK(restored.publish(10).empty());
        const auto actual = restored.publish(11); REQUIRE(actual.size() == 2);
        CHECK(actual[0].identity.unit == 1); CHECK(actual[1].identity.unit == 3);
        REQUIRE(pipeline.publish(11).size() == 2);
        {
            ResourceClaims::RoutePipeline abandoned(executor);
            abandoned.submit(4, 1, 12, input); // Never flushed: no live worker borrows it.
            CHECK(executor.liveBatches() == 0);
        }
        {
            ResourceClaims::RoutePipeline abandoned(executor);
            abandoned.submit(4, 1, 12, input); abandoned.flush();
            CHECK(executor.liveBatches() == 1);
        }
        CHECK(executor.liveBatches() == 0);
    }
    TEST_CASE("cancelled task results cannot publish and invalid inputs fail before submission")
    {
        glob2test::HeadlessGlobals globals;
        glob2test::HeadlessGame fixture({.loadDefaultRace=true, .header=true});
        const auto world = SimulationSnapshot::capture(fixture.game, SimulationSnapshot::captureCatalog(fixture.game));
        ComputeExecutor executor; executor.configure(2);
        ResourceClaims::RoutePipeline pipeline(executor);
        ResourceClaims::RouteInput input{world, {}, 0, 10};
        pipeline.submit(1, 1, 10, input); pipeline.cancel(1);
        CHECK(pipeline.exportPending().empty()); CHECK(pipeline.publish(11).empty());
        input.start = unsigned(world.width * world.height);
        CHECK_THROWS_AS(pipeline.submit(1, 2, 11, input), std::invalid_argument);
        input.start = 0;
        CHECK_THROWS_AS(pipeline.submit(1, 2, 9, input), std::invalid_argument);
        input.profile.movement = 255;
        CHECK_THROWS_AS(pipeline.submit(1, 2, 11, input), std::invalid_argument);
    }
    TEST_CASE("admission and restored paths reject unbounded or corrupt work")
    {
        glob2test::HeadlessGlobals globals;
        glob2test::HeadlessGame fixture({.loadDefaultRace=true, .header=true});
        const auto world = SimulationSnapshot::capture(fixture.game, SimulationSnapshot::captureCatalog(fixture.game));
        ComputeExecutor executor; executor.configure(1);
        ResourceClaims::RoutePipeline pipeline(executor, 1);
        ResourceClaims::RouteInput input{world, {}, 0, 10};
        pipeline.submit(1, 1, 10, input);
        CHECK_FALSE(pipeline.admissionAvailable());
        CHECK_THROWS_AS(pipeline.submit(2, 1, 10, input), std::length_error);
        const auto saved = pipeline.exportPending();
        REQUIRE(saved.size() == 1);
        ResourceClaims::RoutePipeline restored(executor);
        auto damaged = saved; damaged[0].route.cells.push_back(unsigned(world.width * world.height));
        CHECK_THROWS_AS(restored.restore(damaged), std::invalid_argument);
        damaged = saved; damaged[0].due = 0;
        CHECK_THROWS_AS(restored.restore(damaged), std::invalid_argument);
        damaged = saved; damaged[0].route.status = ResourceClaims::RouteStatus(255);
        CHECK_THROWS_AS(restored.restore(damaged), std::invalid_argument);
        restored.restore(saved);
        CHECK(restored.publish(10).empty()); REQUIRE(restored.publish(11).size() == 1);
        REQUIRE(pipeline.publish(11).size() == 1); CHECK(pipeline.admissionAvailable());
    }
}
