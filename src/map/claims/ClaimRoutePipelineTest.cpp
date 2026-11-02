// SPDX-License-Identifier: GPL-3.0-or-later
#include "EngineFixtures.h"
#include "ClaimRoutePipeline.h"

TEST_SUITE("ClaimRoutePipeline")
{
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
