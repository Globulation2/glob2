// SPDX-License-Identifier: GPL-3.0-or-later
#include "Glob2Test.h"
#include "ClaimRouteSearch.h"
#include <random>

TEST_SUITE("ClaimRouteSearch")
{
    TEST_CASE("cached paths reach legal stances with weighted toroidal costs")
    {
        const field::Grid grid(8, 8);
        ResourceClaims::RouteSearch search;
        const auto target = unsigned(grid.index(7, 7));
        const auto cost = [](unsigned, bool diagonal) { return diagonal ? 14u : 10u; };
        auto route = search.find(grid, 0, 1000, [](unsigned) { return true; }, cost,
            [&](unsigned i) { return i == target; }, [](unsigned) { return 0u; });
        REQUIRE(route.status == ResourceClaims::RouteStatus::Found);
        REQUIRE(route.cells.size() == 1); CHECK(route.cells.front() == target); CHECK(route.cost == 14);
        // The blocked source need never be entered: stop at a harvesting stance.
        route = search.find(grid, 0, 1000, [&](unsigned i) { return i != target; }, cost,
            [](unsigned i) { return i == 6; }, [](unsigned) { return 0u; });
        REQUIRE(route.status == ResourceClaims::RouteStatus::Found);
        CHECK(route.cost == 20); CHECK(route.cells.back() == 6);
    }
    TEST_CASE("A star agrees with exhaustive distances across reused scratch")
    {
        const field::Grid grid(16, 8);
        std::mt19937 random(19287);
        ResourceClaims::RouteSearch search, reference;
        for (unsigned trial = 0; trial < 100; ++trial) {
            std::vector<unsigned> costs(grid.cells());
            for (auto& c : costs) c = random() % 5 ? 10 + random() % 50 : 0;
            const unsigned start = random() % grid.cells(), target = random() % grid.cells();
            costs[start] = costs[target] = 10;
            const auto passable = [&](unsigned i) { return costs[i] != 0; };
            const auto step = [&](unsigned i, bool diagonal) { return costs[i] * (diagonal ? 14 : 10) / 10; };
            const auto goal = [&](unsigned i) { return i == target; };
            const auto h = [&](unsigned i) {
                const unsigned dx = unsigned(std::abs(int(i % 16) - int(target % 16)));
                const unsigned dy = unsigned(std::abs(int(i / 16) - int(target / 16)));
                return 10 * std::max(std::min(dx, 16 - dx), std::min(dy, 8 - dy));
            };
            const auto actual = search.find(grid, start, 100000, passable, step, goal, h);
            const auto expected = reference.find(grid, start, 100000, passable, step, goal, [](unsigned) { return 0u; });
            REQUIRE(actual.status == expected.status); CHECK(actual.cost == expected.cost);
            unsigned sum = 0, previous = start;
            for (auto at : actual.cells) {
                CHECK(passable(at));
                const int dx = std::abs(int(at % 16) - int(previous % 16));
                const int dy = std::abs(int(at / 16) - int(previous / 16));
                CHECK((dx <= 1 || dx == 15)); CHECK((dy <= 1 || dy == 7));
                sum += step(at, dx && dy); previous = at;
            }
            CHECK(sum == actual.cost);
        }
    }
    TEST_CASE("expansion budgets never report partial paths as reached")
    {
        ResourceClaims::RouteSearch search;
        const field::Grid grid(8,8);
        const auto route=search.find(grid,0,1000,[](unsigned){return true;},
            [](unsigned,bool){return 10u;},[](unsigned i){return i==27;},[](unsigned){return 0u;},2);
        CHECK(route.status==ResourceClaims::RouteStatus::ExpansionLimit);
        CHECK(route.expansions==2); CHECK(route.cells.empty());
        const auto zero=search.find(grid,0,1000,[](unsigned){return true;},
            [](unsigned,bool){return 10u;},[](unsigned i){return i==0;},[](unsigned){return 0u;},0);
        CHECK(zero.status==ResourceClaims::RouteStatus::ExpansionLimit);
        CHECK(zero.expansions==0);
    }
    TEST_CASE("failure cost limits and empty routes are explicit")
    {
        ResourceClaims::RouteSearch search;
        const field::Grid grid(4, 4);
        const auto step = [](unsigned, bool) { return 10u; };
        const auto zero = [](unsigned) { return 0u; };
        const auto goal = [](unsigned i) { return i == 2; };
        CHECK(search.find(grid, 0, 5, [](unsigned) { return true; }, step, goal, zero).status == ResourceClaims::RouteStatus::CostLimit);
        CHECK(search.find(grid, 0, 100, [](unsigned) { return false; }, step, goal, zero).status == ResourceClaims::RouteStatus::Unreachable);
        const auto already = search.find(grid, 2, 0, [](unsigned) { return false; }, step, goal, zero);
        CHECK(already.status == ResourceClaims::RouteStatus::Found); CHECK(already.cells.empty());
        CHECK_THROWS_AS(search.find(grid, 16, 0, [](unsigned) { return true; }, step, goal, zero), std::out_of_range);
    }
}
