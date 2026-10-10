// SPDX-License-Identifier: GPL-3.0-or-later
#include "OpenCLGradient.h"
#include "Glob2Test.h"
#include "GradientPropagation.h"
#include "TerrainGradient.h"
#include <barrier>
#include <future>
#include <queue>
#include <random>
#include <thread>

namespace
{
struct RestoreBackend
{
    gradient_kernel::Backend previous = gradient_kernel::backend();
    ~RestoreBackend() { gradient_kernel::setBackend(previous); }
};
std::vector<std::uint16_t> oracle(std::vector<std::uint16_t> out, field::Grid grid,
                                  const std::vector<gradient_kernel::EntrySteps> &costs, int cap)
{
    using Item = std::pair<unsigned, std::size_t>;
    std::priority_queue<Item, std::vector<Item>, std::greater<Item>> queue;
    for (std::size_t i = 0; i < out.size(); ++i)
        if (out[i] > 1)
            queue.emplace(65535 - out[i], i);
    while (!queue.empty())
    {
        const auto [cost, i] = queue.top();
        queue.pop();
        if (cost > unsigned(cap) || 65535u - out[i] != cost)
            continue;
        for (auto d : field::Surrounding)
        {
            const auto j = grid.index(int(i % grid.width()) + d.x, int(i / grid.width()) + d.y);
            const auto next = cost + (d.x && d.y ? costs[i].diagonal : costs[i].cardinal);
            if (out[j] && next <= unsigned(cap) && 65535 - next > out[j])
            {
                out[j] = std::uint16_t(65535 - next);
                queue.emplace(next, j);
            }
        }
    }
    return out;
}
} // namespace
TEST_SUITE("OpenCLGradient")
{
    TEST_CASE("full fields agree with heap oracle across costs caps seeds and wrapping")
    {
        using namespace gradient_kernel;
        RestoreBackend restore;
        const auto initial = openCLStatus();
        INFO("OpenCL device: " << initial.device << "; fallback reason: " << initial.error);
        setBackend(initial.available ? Backend::OpenCL : Backend::Automatic);
        std::mt19937 random(1729);
        for (const auto dimensions : {std::pair{1, 1}, {1, 32}, {32, 1}, {7, 13}, {32, 64}})
            for (int swim = 0; swim < 7; ++swim)
                for (int cap : {0, 60, COST_LIMIT})
                {
                    field::Grid grid(dimensions.first, dimensions.second);
                    std::vector<std::uint16_t> seeds(grid.cells(), 1);
                    std::vector<std::uint8_t> water(grid.cells());
                    std::vector<EntrySteps> costs(grid.cells());
                    for (std::size_t i = 0; i < grid.cells(); ++i)
                    {
                        water[i] = random() % 3 == 0;
                        if (random() % 5 == 0 || (swim == 0 && water[i]))
                            seeds[i] = 0;
                        else if (random() % 17 == 0)
                            seeds[i] = std::uint16_t(65535 - random() % 1000);
                        costs[i] =
                            weightedClass(swim) && water[i] ? entrySteps(WATER_STEP[swim]) : LAND_STEPS;
                    }
                    seeds[0] = 65535;
                    const auto expected = oracle(seeds, grid, costs, cap);
                    auto actual = seeds;
                    GradientWorkspace scratch;
                    propagateField(actual.data(), swim, cap, grid, scratch,
                                   [&](std::size_t i) { return water[i] != 0; });
                    CHECK(actual == expected);
                }
        if (initial.available)
        {
            const auto final = openCLStatus();
            REQUIRE_MESSAGE(final.available, final.error);
            CHECK(final.fields > initial.fields);
        }
    }
    TEST_CASE("custom prepared costs preserve forbidden and deferred seeds")
    {
        using namespace gradient_kernel;
        RestoreBackend restore;
        setBackend(openCLStatus().available ? Backend::OpenCL : Backend::Automatic);
        const PreparedTerrainCosts<4> profile(
            std::array<EntrySteps, 4>{{{5, 7}, {7, 10}, {13, 18}, {21, 29}}});
        field::Grid grid(31, 17);
        std::mt19937 random(71);
        std::vector<std::uint16_t> seeds(grid.cells(), 1);
        std::vector<unsigned> classes(grid.cells());
        std::vector<EntrySteps> costs(grid.cells());
        for (std::size_t i = 0; i < grid.cells(); ++i)
        {
            classes[i] = random() % 4;
            costs[i] = profile.classes[classes[i]];
            if (random() % 6 == 0)
                seeds[i] = 0;
            else if (random() % 23 == 0)
                seeds[i] = 65535 - (random() % 1200);
        }
        seeds[0] = 65535;
        seeds[1] = 65535 - 701;
        for (int cap : {0, 60, 700, COST_LIMIT})
        {
            const auto expected = oracle(seeds, grid, costs, cap);
            auto actual = seeds;
            GradientWorkspace scratch;
            propagatePreparedTerrainField(actual.data(), cap, grid, scratch, profile,
                                          [&](std::size_t i) { return classes[i]; });
            CHECK(actual == expected);
        }
        for (std::uint16_t inert : {std::uint16_t(0), std::uint16_t(1)})
        {
            std::fill(seeds.begin(), seeds.end(), inert);
            auto actual = seeds;
            GradientWorkspace scratch;
            propagateField(actual.data(), 0, COST_LIMIT, grid, scratch, [](std::size_t) { return false; });
            CHECK(actual == seeds);
        }
    }
    TEST_CASE("declined accelerators use original seeds and CPU selection bypasses them")
    {
        using namespace gradient_kernel;
        RestoreBackend restore;
        const auto previous = accelerator;
        struct RestoreAccelerator
        {
            decltype(accelerator) value;
            ~RestoreAccelerator() { accelerator = value; }
        } guard{previous};
        static int calls = 0;
        calls = 0;
        accelerator = [](const BackendRequest &, Backend)
        {
            ++calls;
            return false;
        };
        std::vector<std::uint16_t> seeds(64, 1);
        seeds[0] = 65535;
        seeds[1] = 0;
        GradientWorkspace scratch;
        auto expected = seeds;
        propagateFieldCPU(expected.data(), 0, COST_LIMIT, {8, 8}, scratch, [](std::size_t) { return false; });
        for (auto choice : {Backend::Automatic, Backend::OpenCL, Backend::CPU})
        {
            setBackend(choice);
            auto actual = seeds;
            propagateField(actual.data(), 0, COST_LIMIT, {8, 8}, scratch, [](std::size_t) { return false; });
            CHECK(actual == expected);
        }
        CHECK(calls == 2);
    }
    TEST_CASE("a failed accelerator resumes from original seeds and stays on CPU")
    {
        using namespace gradient_kernel;
        RestoreBackend restore;
        setBackend(Backend::Automatic);
        const auto previous = accelerator;
        struct RestoreAccelerator
        {
            decltype(accelerator) value;
            ~RestoreAccelerator() { accelerator = value; }
        } guard{previous};
        static int calls = 0;
        calls = 0;
        accelerator = [](const BackendRequest &r, Backend)
        {
            ++calls;
            r.session.failed.store(true);
            r.session.selection().store(Backend::CPU);
            return false;
        };
        GradientWorkspace scratch;
        scratch.backendSession->selection().store(Backend::OpenCL);
        std::vector<std::uint16_t> seeds(64, 1);
        seeds[0] = 65535;
        auto expected = seeds;
        propagateFieldCPU(expected.data(), 0, COST_LIMIT, {8, 8}, scratch, [](std::size_t) { return false; });
        for (int i = 0; i < 2; ++i)
        {
            scratch.family = i ? Family::Guard : Family::Materials;
            auto actual = seeds;
            propagateField(actual.data(), 0, COST_LIMIT, {8, 8}, scratch, [](std::size_t) { return false; });
            CHECK(actual == expected);
            CHECK(scratch.backendSession->failed.load());
        }
        CHECK(calls == 1);
    }
    TEST_CASE("a game shares placement across workspaces and rechecks changed geometry")
    {
        using namespace gradient_kernel;
        RestoreBackend restore;
        setBackend(Backend::Automatic);
        const auto before = openCLStatus();
        auto session = std::make_shared<BackendSession>();
        GradientWorkspace first, second;
        first.backendSession = session;
        second.backendSession = session;
        std::vector<std::uint16_t> seeds(256 * 256, 1);
        for (std::size_t i = 0; i < seeds.size(); i += 71)
            seeds[i] = 65535;
        for (std::size_t i = 1; i < seeds.size(); ++i)
            if (i % 23 == 0)
                seeds[i] = 0;
        auto water = [](std::size_t i) { return i % 7 == 0; };
        auto expected = seeds;
        propagateFieldCPU(expected.data(), 6, COST_LIMIT, {256, 256}, first, water);
        auto actual = seeds;
        propagateField(actual.data(), 6, COST_LIMIT, {256, 256}, first, water);
        CHECK(actual == expected);
        const auto winner = session->selection().load();
        if (before.available) CHECK(winner != Backend::Automatic);
        else CHECK(session->failed.load());
        std::vector<std::uint16_t> small(64, 1);
        small[0] = 65535;
        auto reference = small;
        propagateFieldCPU(reference.data(), 0, COST_LIMIT, {8, 8}, first, [](std::size_t) { return false; });
        propagateField(small.data(), 0, COST_LIMIT, {8, 8}, second, [](std::size_t) { return false; });
        CHECK(small == reference);
        if (before.available) {
            CHECK(session->selection().load() != Backend::Automatic);
            CHECK(openCLStatus().calibrations == before.calibrations + 2);
        }
        if (before.available)
        {
            // An explicit test choice still applies to the same geometry.
            session->selection().store(Backend::OpenCL);
            const auto fields = openCLStatus().fields;
            small.assign(64, 1);
            small[0] = 65535;
            propagateField(small.data(), 0, COST_LIMIT, {8, 8}, second, [](std::size_t) { return false; });
            CHECK(small == reference);
            CHECK(openCLStatus().fields == fields + 1);
        }
        GradientWorkspace nextGame;
        CHECK(nextGame.backendSession->selection().load() == Backend::Automatic);
        CHECK(nextGame.backendSession != session);
    }

    TEST_CASE("concurrent lanes reuse device costs and preserve exact long paths")
    {
        using namespace gradient_kernel;
        RestoreBackend restore;
        const auto before = openCLStatus();
        if (!before.available)
            return;
        setBackend(Backend::OpenCL);
        constexpr unsigned count = 8;
        std::array<std::vector<std::uint16_t>, count> actual, expected, seeds;
        std::array<field::Grid, count> grids{
            {{127, 67}, {128, 64}, {63, 129}, {128, 64}, {127, 67}, {128, 64}, {63, 129}, {128, 64}}};
        std::array<GradientWorkspace, count> scratch;
        for (unsigned f = 0; f < count; ++f)
        {
            auto grid = grids[f];
            seeds[f].assign(grid.cells(), 1);
            // Alternating slits require repeated exchanges between neighboring tiles.
            for (int y = 0; y < grid.height(); ++y)
                for (int x = 0; x < grid.width(); ++x)
                    if (x % 16 == 8 && y != (x / 16 % 2 ? 3 : grid.height() - 4))
                        seeds[f][grid.index(x, y)] = 0;
            seeds[f][grid.index(1, 1 + f)] = 65535;
            expected[f] =
                oracle(seeds[f], grid, std::vector<EntrySteps>(grid.cells(), LAND_STEPS), COST_LIMIT);
        }
        for (unsigned repeat = 0; repeat < 2; ++repeat)
        {
            std::barrier ready(count);
            std::array<std::thread, count> workers;
            for (unsigned f = 0; f < count; ++f)
                workers[f] = std::thread(
                    [&, f]
                    {
                        actual[f] = seeds[f];
                        ready.arrive_and_wait();
                        propagateField(actual[f].data(), 0, COST_LIMIT, grids[f], scratch[f],
                                       [](std::size_t) { return false; });
                    });
            for (auto &worker : workers)
                worker.join();
            for (unsigned f = 0; f < count; ++f)
                CHECK(actual[f] == expected[f]);
        }
        const auto after = openCLStatus();
        REQUIRE_MESSAGE(after.available, after.error);
        CHECK(after.costCacheHits > before.costCacheHits);
        CHECK(after.costUploads - before.costUploads < count * 2);
        CHECK(after.dispatches - before.dispatches == 8 * (after.hostChecks - before.hostChecks));
        CHECK(after.fields - before.fields == count * 2);
        // Changing a cost plane must invalidate content reuse even at identical geometry.
        auto changed = seeds[0];
        std::vector<EntrySteps> costs(grids[0].cells(), entrySteps(WATER_STEP[6]));
        const auto reference = oracle(changed, grids[0], costs, COST_LIMIT);
        propagateField(changed.data(), 6, COST_LIMIT, grids[0], scratch[0], [](std::size_t) { return true; });
        CHECK(changed == reference);
        CHECK(openCLStatus().costUploads > after.costUploads);
    }
    TEST_CASE("frozen halos retain the full global convergence budget")
    {
        using namespace gradient_kernel;
        RestoreBackend restore;
        if (!openCLStatus().available)
            return;
        setBackend(Backend::OpenCL);
        const field::Grid grid(32, 32768);
        std::vector<std::uint16_t> seeds(grid.cells(), 0);
        // Every legal edge crosses the tile boundary, so more local sweeps
        // cannot replace global exchanges. This requires over 8192 dispatches,
        // exceeding the old 65536 / localSteps bound for both frozen variants.
        for (int y = 0; y < grid.height(); ++y)
            seeds[grid.index(15 + (y & 1), y)] = 1;
        seeds[grid.index(15, 0)] = 65535;
        const auto expected = oracle(seeds, grid,
            std::vector<EntrySteps>(grid.cells(), entrySteps(WATER_STEP[1])), COST_LIMIT);
        // A fresh large-field session screens every supported native variant.
        // Do not force an index: a device may reject an individual kernel while
        // retaining other valid variants. Failed tuning must not pass this test
        // merely because propagation recovered through the CPU fallback.
        GradientWorkspace scratch;
        auto actual = seeds;
        const auto before = openCLStatus();
        propagateField(actual.data(), 1, COST_LIMIT, grid, scratch,
                       [](std::size_t) { return true; });
        const auto after = openCLStatus();
        REQUIRE_MESSAGE(after.available, after.error);
        CHECK_FALSE(scratch.backendSession->failed.load());
        CHECK(after.fields == before.fields + 1);
        CHECK(actual == expected);
    }

    TEST_CASE("immutable identities skip cost callbacks and invalidate every cost dependency")
    {
        using namespace gradient_kernel;
        RestoreBackend restore;
        if (!openCLStatus().available)
            return;
        setBackend(Backend::OpenCL);
        field::Grid grid(32, 16);
        auto owner = std::make_shared<const std::vector<std::uint8_t>>(grid.cells(), 1);
        std::weak_ptr<const std::vector<std::uint8_t>> retained = owner;
        std::vector<std::uint16_t> seeds(grid.cells(), 1);
        seeds[0] = 65535;
        seeds[1] = 0;
        GradientWorkspace scratch;
        unsigned calls = 0;
        auto run = [&](CostIdentity identity, EntrySteps step)
        {
            auto actual = seeds;
            auto expected = oracle(seeds, grid, std::vector<EntrySteps>(grid.cells(), step), COST_LIMIT);
            auto costs = [&](std::size_t)
            {
                ++calls;
                return step;
            };
            auto cpu = [&](std::uint16_t *out) { std::copy(expected.begin(), expected.end(), out); };
            REQUIRE(tryAcceleratedGradient(actual.data(), COST_LIMIT, grid, *scratch.backendSession, costs,
                                           cpu, identity));
            CHECK(actual == expected);
        };
        const auto before = openCLStatus();
        CostIdentity identity{owner, 6, 4};
        run(identity, entrySteps(WATER_STEP[6]));
        CHECK(calls == grid.cells() - 1);
        calls = 0;
        run(identity, entrySteps(WATER_STEP[6]));
        CHECK(calls == 0);
        CHECK(openCLStatus().costIdentityHits > before.costIdentityHits);
        seeds[2] = 0;
        run(identity, entrySteps(WATER_STEP[6]));
        CHECK(calls == grid.cells() - 2);
        calls = 0;
        identity.variant = 0;
        run(identity, LAND_STEPS);
        CHECK(calls == grid.cells() - 2);
        calls = 0;
        ++identity.revision;
        run(identity, LAND_STEPS);
        CHECK(calls == grid.cells() - 2);
        calls = 0;
        identity.owner = std::make_shared<const std::vector<std::uint8_t>>(*owner);
        run(identity, LAND_STEPS);
        CHECK(calls == grid.cells() - 2);
        calls = 0;
        CostIdentity complete{identity.owner, 6, 99, true};
        run(complete, entrySteps(WATER_STEP[6]));
        CHECK(calls == grid.cells());
        calls = 0;
        seeds[3] = 0;
        run(complete, entrySteps(WATER_STEP[6]));
        CHECK(calls == 0);
        complete.allCells = false;
        run(complete, entrySteps(WATER_STEP[6]));
        CHECK(calls == grid.cells() - 3);
        complete.allCells = true;
        calls = 0;
        field::Grid reshaped(16, 32);
        auto shaped = seeds;
        const auto shapedExpected =
            oracle(seeds, reshaped, std::vector<EntrySteps>(reshaped.cells(), entrySteps(WATER_STEP[6])),
                   COST_LIMIT);
        REQUIRE(tryAcceleratedGradient(
            shaped.data(), COST_LIMIT, reshaped, *scratch.backendSession,
            [&](std::size_t)
            {
                ++calls;
                return entrySteps(WATER_STEP[6]);
            },
            [&](std::uint16_t *out) { std::copy(shapedExpected.begin(), shapedExpected.end(), out); },
            complete));
        CHECK(shaped == shapedExpected);
        CHECK(calls == reshaped.cells());
        owner.reset();
        CHECK_FALSE(retained.expired()); // Cache prevents pooled address reuse.
    }
    TEST_CASE("uniform cost aliases coexist with varied planes and invalidate on revision")
    {
        using namespace gradient_kernel;
        RestoreBackend restore;
        if (!openCLStatus().available)
            return;
        setBackend(Backend::OpenCL);
        const field::Grid grid(33, 35);
        struct Context
        {
            std::shared_ptr<std::vector<EntrySteps>> costs;
            std::vector<std::uint16_t> seeds, actual, expected;
        };
        std::array<Context, 3> contexts;
        for (unsigned f = 0; f < contexts.size(); ++f)
        {
            auto &context = contexts[f];
            context.costs = std::make_shared<std::vector<EntrySteps>>(grid.cells(), EntrySteps{7, 31});
            context.seeds.assign(grid.cells(), 1);
            for (std::size_t i = 0; i < grid.cells(); ++i)
            {
                if (i % 17 == 0) context.seeds[i] = 0;
                if (f == 2 && i % 3 == 0) (*context.costs)[i] = {13, 5};
            }
            context.seeds[7] = 65535;
            context.seeds.back() = 65450;
        }
        BackendSession session;
        for (unsigned revision = 0; revision < 2; ++revision)
        {
            if (revision)
            {
                // The same owner now describes a varied plane. Its new revision
                // must replace the cached uniform classification along with the data.
                (*contexts[0].costs)[7] = {29, 11};
            }
            std::vector<BackendRequest> requests;
            for (auto &context : contexts)
            {
                context.actual = context.seeds;
                context.expected = oracle(context.seeds, grid, *context.costs, COST_LIMIT);
                requests.push_back({context.actual.data(), COST_LIMIT, grid, session, &context,
                    [](void *p, std::size_t i) { return (*static_cast<Context *>(p)->costs)[i]; },
                    [](void *p, std::uint16_t *out) {
                        auto &c = *static_cast<Context *>(p);
                        std::copy(c.expected.begin(), c.expected.end(), out);
                    }, {context.costs, 0, revision, true}});
            }
            const auto before = openCLStatus();
            REQUIRE(batchAccelerator(requests, Backend::OpenCL));
            for (const auto &context : contexts) CHECK(context.actual == context.expected);
            const auto after = openCLStatus();
            REQUIRE_MESSAGE(after.available, after.error);
            if (!revision) CHECK(after.costCacheHits > before.costCacheHits);
        }
    }
    TEST_CASE("explicit scheduler batches span chunks and retire mixed fields exactly")
    {
        using namespace gradient_kernel;
        RestoreBackend restore;
        if (!openCLStatus().available)
            return;
        setBackend(Backend::OpenCL);
        constexpr unsigned count = 10;
        struct Context
        {
            field::Grid grid;
            std::vector<std::uint16_t> seeds, expected;
            std::vector<EntrySteps> costs;
            int cap;
        };
        std::vector<Context> contexts;
        contexts.reserve(count);
        for (unsigned f = 0; f < count; ++f)
        {
            field::Grid grid(f == 1 ? 129 : 64, f == 1 ? 65 : 32);
            std::vector<std::uint16_t> seeds(grid.cells(), f == 0 ? 65535 : 1);
            std::vector<EntrySteps> costs(grid.cells());
            for (std::size_t i = 0; i < grid.cells(); ++i)
            {
                costs[i] = i % 7 == 0 ? entrySteps(WATER_STEP[6]) : LAND_STEPS;
                if (f && i % 19 == 0)
                    seeds[i] = 0;
            }
            if (f == 1)
                for (int x = 8; x < grid.width(); x += 16)
                    for (int y = 0; y < grid.height(); ++y)
                        if (y != (x / 16 % 2 ? 2 : grid.height() - 3))
                            seeds[grid.index(x, y)] = 0;
            seeds[grid.index(1, 1)] = 65535;
            seeds[grid.index(5, 5)] = 65000;
            const int cap = f % 3 == 0 ? COST_LIMIT : f % 3 == 1 ? 10000 : 60;
            contexts.push_back({grid, seeds, oracle(seeds, grid, costs, cap), costs, cap});
        }
        auto session = std::make_shared<BackendSession>();
        session->selection().store(Backend::OpenCL);
        std::vector<BackendRequest> requests;
        for (auto &context : contexts)
            requests.push_back({context.seeds.data(),
                                context.cap,
                                context.grid,
                                *session,
                                &context,
                                [](void *p, std::size_t i) { return static_cast<Context *>(p)->costs[i]; },
                                [](void *p, std::uint16_t *out)
                                {
                                    auto &c = *static_cast<Context *>(p);
                                    std::copy(c.expected.begin(), c.expected.end(), out);
                                },
                                {}});
        const auto before = openCLStatus();
        REQUIRE(batchAccelerator(requests, backend()));
        for (const auto &context : contexts)
            CHECK(context.seeds == context.expected);
        const auto after = openCLStatus();
        REQUIRE_MESSAGE(after.available, after.error);
        CHECK(after.schedulerBatches == before.schedulerBatches + 1);
        CHECK(after.batches == before.batches + 2);
        CHECK(after.maxBatchFields == 8);
        CHECK(after.retiredFields == before.retiredFields + count);
        CHECK(after.fields == before.fields + count);
        CHECK(after.tileWidth * after.tileHeight == 256);
        CHECK((after.localSteps == 2 || after.localSteps == 4 || after.localSteps == 8 || after.localSteps == 16));
        CHECK(after.tunings == before.tunings); // Small explicit fields do not trigger workload-class tuning.
    }
    TEST_CASE("GPU algorithm and local steps are tuned independently per game workload class")
    {
        using namespace gradient_kernel;
        RestoreBackend restore;
        if (!openCLStatus().available) return;
        setBackend(Backend::OpenCL);
        const field::Grid grid(256, 256);
        const std::vector<EntrySteps> costs(grid.cells(), LAND_STEPS);
        struct Context { std::vector<std::uint16_t> seeds, expected; const std::vector<EntrySteps>* costs; };
        std::array<Context, 2> contexts;
        for (unsigned f = 0; f < contexts.size(); ++f) {
            std::vector<std::uint16_t> seeds(grid.cells(), 1);
            seeds[grid.index(7 + f * 43, 11 + f * 19)] = 65535;
            contexts[f] = {seeds, oracle(seeds, grid, costs, COST_LIMIT), &costs};
        }
        BackendSession session;
        auto run = [&](Family family, std::size_t count) {
            std::vector<BackendRequest> requests;
            for (std::size_t i = 0; i < count; ++i) {
                auto& c = contexts[i];
                std::fill(c.seeds.begin(), c.seeds.end(), 1);
                c.seeds[grid.index(7 + unsigned(i) * 43, 11 + unsigned(i) * 19)] = 65535;
                requests.push_back({c.seeds.data(), COST_LIMIT, grid, session, &c,
                    [](void* p, std::size_t cell) { return (*static_cast<Context*>(p)->costs)[cell]; },
                    [](void* p, std::uint16_t* out) {
                        const auto& c = *static_cast<Context*>(p);
                        std::copy(c.expected.begin(), c.expected.end(), out);
                    }, {}, family});
            }
            REQUIRE(batchAccelerator(requests, Backend::OpenCL));
            for (std::size_t i = 0; i < count; ++i) CHECK(contexts[i].seeds == contexts[i].expected);
        };
        const auto before = openCLStatus().tunings;
        run(Family::Materials, 1);
        CHECK(session.tileSelection(Family::Materials, 1).load() > 0);
        CHECK(session.tileSelection(Family::Materials, 2).load() == 0);
        CHECK(session.tileSelection(Family::Clear, 1).load() == 0);
        run(Family::Clear, 1);
        run(Family::Materials, 2);
        CHECK(openCLStatus().tunings == before + 3);
        const auto chosen = session.tileSelection(Family::Materials, 1).load();
        run(Family::Materials, 1);
        CHECK(session.tileSelection(Family::Materials, 1).load() == chosen);
        CHECK(openCLStatus().tunings == before + 3);
        BackendSession nextGame;
        CHECK(nextGame.tileSelection(Family::Materials, 1).load() == 0);
    }
    TEST_CASE("inert fields finish without waiting for a busy GPU or selecting a class")
    {
        using namespace gradient_kernel;
        RestoreBackend restore;
        if (!openCLStatus().available) return;
        BackendSession activeSession, inertSession;
        std::promise<void> entered, release;
        auto ready=entered.get_future();auto released=release.get_future().share();
        struct Context {
            std::promise<void>* entered;
            std::shared_future<void> released;
            bool first=true;
        } context{&entered,released};
        std::vector<std::uint16_t> active(64,1), inert(64,1);
        active[0]=65535;inert[3]=0;
        const auto original=inert;
        const BackendRequest activeRequest{active.data(),COST_LIMIT,{8,8},activeSession,&context,
            [](void* p,std::size_t) {
                auto& c=*static_cast<Context*>(p);
                if(c.first){c.first=false;c.entered->set_value();c.released.wait();}
                return LAND_STEPS;
            }, [](void*,std::uint16_t*){}, {}};
        const BackendRequest inertRequest{inert.data(),COST_LIMIT,{8,8},inertSession,nullptr,
            [](void*,std::size_t){return LAND_STEPS;}, [](void*,std::uint16_t*){}, {}};
        bool activeHandled=false;
        std::thread busy([&]{activeHandled=accelerator(activeRequest,Backend::OpenCL);});
        CHECK(ready.wait_for(std::chrono::seconds(2))==std::future_status::ready);
        auto completed=std::async(std::launch::async,[&]{return accelerator(inertRequest,Backend::Automatic);});
        CHECK(completed.wait_for(std::chrono::seconds(2))==std::future_status::ready);
        release.set_value();busy.join();
        CHECK(activeHandled);
        CHECK(completed.get());
        CHECK(inert==original);
        CHECK(inertSession.selection().load()==Backend::Automatic);
        CHECK_FALSE(inertSession.failed.load());
    }

    TEST_CASE("maximally seeded fields skip cost work and class selection in singletons and batches")
    {
        using namespace gradient_kernel;
        RestoreBackend restore;
        const auto before = openCLStatus();
        if (!before.available) return;
        BackendSession session;
        unsigned callbacks = 0;
        std::vector<std::uint16_t> goals(64, 65535), empty(64, 1);
        goals[3] = 0;
        empty[7] = 0;
        const auto original = goals;
        auto costs = [](void* p, std::size_t) {
            ++*static_cast<unsigned*>(p);
            return LAND_STEPS;
        };
        const BackendRequest goalRequest{goals.data(), COST_LIMIT, {8,8}, session, &callbacks,
            costs, [](void*, std::uint16_t*) {}, {}, Family::Forbidden};
        const BackendRequest emptyRequest{empty.data(), COST_LIMIT, {8,8}, session, &callbacks,
            costs, [](void*, std::uint16_t*) {}, {}, Family::Forbidden};
        REQUIRE(accelerator(goalRequest, Backend::Automatic));
        const std::array requests{goalRequest, emptyRequest};
        REQUIRE(batchAccelerator(requests, Backend::Automatic));
        CHECK(goals == original);
        CHECK(callbacks == 0);
        CHECK(session.selection(Family::Forbidden, 1).load() == Backend::Automatic);
        CHECK(session.selection(Family::Forbidden, 2).load() == Backend::Automatic);
        CHECK(session.tileSelection(Family::Forbidden, 1).load() == 0);
        const auto after = openCLStatus();
        CHECK(after.fields == before.fields);
        CHECK(after.calibrations == before.calibrations);
        CHECK(after.batches == before.batches);
        // A nonmaximal seed can improve even when every traversable cell is
        // already seeded. It must retain ordinary propagation and validation.
        goals[8] = 65520;
        const auto expected = oracle(goals, {8,8}, std::vector<EntrySteps>(64, LAND_STEPS), COST_LIMIT);
        REQUIRE(accelerator(goalRequest, Backend::OpenCL));
        CHECK(goals == expected);
        CHECK(goals[8] > 65520);
        CHECK(callbacks > 0);
    }
    TEST_CASE("known CPU groups finish while another GPU group holds the device queue")
    {
        using namespace gradient_kernel;
        RestoreBackend restore;
        if (!openCLStatus().available) return;
        setBackend(Backend::Automatic);
        BackendSession busySession, cpuSession;
        for (unsigned count = 1; count <= 8; ++count)
            cpuSession.selection(Family::Materials, count).store(Backend::CPU);
        std::promise<void> entered, release;
        auto enteredFuture = entered.get_future();
        auto released = release.get_future().share();
        struct BusyContext {
            std::promise<void>* entered;
            std::shared_future<void> released;
            bool first = true;
        } busyContext{&entered, released};
        std::vector<std::uint16_t> seeds(64, 1);
        seeds[7] = 65535;
        const auto expected = oracle(seeds, {8,8}, std::vector<EntrySteps>(64, LAND_STEPS), COST_LIMIT);
        auto busyField = seeds;
        const BackendRequest busyRequest{busyField.data(), COST_LIMIT, {8,8}, busySession, &busyContext,
            [](void* p, std::size_t) {
                auto& context = *static_cast<BusyContext*>(p);
                if (context.first) {
                    context.first = false;
                    context.entered->set_value();
                    context.released.wait();
                }
                return LAND_STEPS;
            }, [](void*, std::uint16_t*) {}, {}, Family::Guard};
        auto busy = std::async(std::launch::async, [&] {
            return accelerator(busyRequest, Backend::OpenCL);
        });
        CHECK(enteredFuture.wait_for(std::chrono::seconds(2)) == std::future_status::ready);
        constexpr unsigned count = 8;
        std::barrier start(count);
        std::array<std::vector<std::uint16_t>, count> fields;
        std::array<std::future<bool>, count> completed;
        for (unsigned i = 0; i < count; ++i) {
            fields[i] = seeds;
            completed[i] = std::async(std::launch::async, [&, i] {
                const BackendRequest request{fields[i].data(), COST_LIMIT, {8,8}, cpuSession, nullptr,
                    [](void*, std::size_t) { return LAND_STEPS; },
                    [](void*, std::uint16_t*) {}, {}, Family::Materials};
                start.arrive_and_wait();
                const bool handled = accelerator(request, Backend::Automatic);
                // A known CPU class resumes on this original worker.
                if (!handled) {
                    GradientWorkspace scratch;
                    propagateFieldCPU(fields[i].data(), 0, COST_LIMIT, {8,8}, scratch,
                                      [](std::size_t) { return false; });
                }
                return handled;
            });
        }
        // Capture readiness before releasing the GPU. Always release it even
        // on failure, so this regression reports a failure rather than hanging.
        std::array<bool, count> ready{};
        for (unsigned i = 0; i < count; ++i)
            ready[i] = completed[i].wait_for(std::chrono::seconds(2)) == std::future_status::ready;
        release.set_value();
        CHECK(busy.get());
        CHECK(busyField == expected);
        for (unsigned i = 0; i < count; ++i) {
            CHECK(ready[i]);
            CHECK_FALSE(completed[i].get());
            CHECK(fields[i] == expected);
        }
        CHECK_FALSE(cpuSession.failed.load());
        for (unsigned size = 1; size <= 8; ++size)
            CHECK(cpuSession.selection(Family::Materials, size).load() == Backend::CPU);
    }

    TEST_CASE("explicit larger classes are discovered after the singleton class selects CPU")
    {
        using namespace gradient_kernel;
        RestoreBackend restore;
        if (!openCLStatus().available) return;
        setBackend(Backend::Automatic);
        BackendSession session;
        session.selection(Family::Materials, 1).store(Backend::CPU);
        constexpr unsigned count = 8, cells = 32 * 32;
        std::vector<std::uint16_t> seeds(cells, 1);
        seeds[7] = 65535;
        auto expected = oracle(seeds, {32,32}, std::vector<EntrySteps>(cells, LAND_STEPS), COST_LIMIT);
        const auto before = openCLStatus().calibrations;
        std::array<std::vector<std::uint16_t>, count> fields;
        std::vector<BackendRequest> requests;
        for (unsigned i = 0; i < count; ++i) {
            fields[i] = seeds;
            requests.push_back({fields[i].data(), COST_LIMIT, {32,32}, session, &expected,
                [](void*, std::size_t) { return LAND_STEPS; },
                [](void* context, std::uint16_t* output) {
                    const auto& reference = *static_cast<std::vector<std::uint16_t>*>(context);
                    std::copy(reference.begin(), reference.end(), output);
                }, {}, Family::Materials});
        }
        REQUIRE(batchAccelerator(requests, Backend::Automatic));
        for (const auto& field : fields) CHECK(field == expected);
        CHECK(session.selection(Family::Materials, count).load() != Backend::Automatic);
        CHECK(openCLStatus().calibrations == before + 1);
        CHECK(session.selection(Family::Materials, 1).load() == Backend::CPU);
        CHECK_FALSE(session.failed.load());
    }

    TEST_CASE("CPU placement bypasses device work and periodically rechecks its decision")
    {
        using namespace gradient_kernel;
        RestoreBackend restore;
        if (!openCLStatus().available) return;
        setBackend(Backend::Automatic);
        BackendSession session;
        std::vector<std::uint16_t> seeds(64, 1);
        seeds[7] = 65535;
        auto expected = oracle(seeds, {8,8}, std::vector<EntrySteps>(64, LAND_STEPS), COST_LIMIT);
        auto field = seeds;
        BackendRequest request{field.data(), COST_LIMIT, {8,8}, session, &expected,
            [](void*, std::size_t) { return LAND_STEPS; },
            [](void* context, std::uint16_t* output) {
                const auto& reference = *static_cast<std::vector<std::uint16_t>*>(context);
                std::copy(reference.begin(), reference.end(), output);
            }, {}, Family::Clear};
        REQUIRE(accelerator(request, Backend::Automatic));
        REQUIRE(field == expected);
        REQUIRE(session.selection(Family::Clear, 1).load() == Backend::CPU);
        const auto measured = openCLStatus();
        auto& timing = session.timing(Family::Clear, 1);
        CHECK(timing.recheckAfter.load() >= 32);
        for (unsigned repeat = 0; repeat < 8; ++repeat) {
            field = seeds;
            CHECK_FALSE(accelerator(request, Backend::Automatic));
            CHECK(field == seeds);
        }
        CHECK(openCLStatus().batches == measured.batches);
        CHECK(openCLStatus().calibrations == measured.calibrations);
        timing.calls.store(timing.recheckAfter.load() - 1);
        REQUIRE(accelerator(request, Backend::Automatic));
        CHECK(field == expected);
        CHECK(openCLStatus().calibrations == measured.calibrations + 1);
        CHECK_FALSE(session.failed.load());
    }

    TEST_CASE("periodic checks replace a GPU winner when the CPU workload becomes cheaper")
    {
        using namespace gradient_kernel;
        RestoreBackend restore;
        if (!openCLStatus().available) return;
        setBackend(Backend::Automatic);
        BackendSession session;
        std::vector<std::uint16_t> seeds(64, 1);
        seeds[7] = 65535;
        auto expected = oracle(seeds, {8,8}, std::vector<EntrySteps>(64, LAND_STEPS), COST_LIMIT);
        struct Context { const std::vector<std::uint16_t>* expected; bool slow = true; } context{&expected};
        auto field = seeds;
        BackendRequest request{field.data(), COST_LIMIT, {8,8}, session, &context,
            [](void*, std::size_t) { return LAND_STEPS; },
            [](void* p, std::uint16_t* output) {
                const auto& c = *static_cast<Context*>(p);
                if (c.slow) std::this_thread::sleep_for(std::chrono::milliseconds(10));
                std::copy(c.expected->begin(), c.expected->end(), output);
            }, {}, Family::Forbidden};
        REQUIRE(accelerator(request, Backend::Automatic));
        REQUIRE(field == expected);
        REQUIRE(session.selection(Family::Forbidden, 1).load() == Backend::OpenCL);
        const auto calibrations = openCLStatus().calibrations;
        context.slow = false;
        auto& timing = session.timing(Family::Forbidden, 1);
        timing.calls.store(timing.recheckAfter.load() - 1);
        field = seeds;
        REQUIRE(accelerator(request, Backend::Automatic));
        CHECK(field == expected);
        CHECK(session.selection(Family::Forbidden, 1).load() == Backend::CPU);
        CHECK(openCLStatus().calibrations == calibrations + 1);
        CHECK_FALSE(session.failed.load());
    }

    TEST_CASE("first-seen movement rechecks placement without recalibrating every alternation")
    {
        using namespace gradient_kernel;
        RestoreBackend restore;
        if (!openCLStatus().available) return;
        setBackend(Backend::Automatic);
        BackendSession session;
        std::vector<std::uint16_t> seeds(64, 1);
        seeds[7] = 65535;
        auto expected = oracle(seeds, {8,8}, std::vector<EntrySteps>(64, LAND_STEPS), COST_LIMIT);
        auto field = seeds;
        BackendRequest request{field.data(), COST_LIMIT, {8,8}, session, &expected,
            [](void*, std::size_t) { return LAND_STEPS; },
            [](void* p, std::uint16_t* output) {
                const auto& expected = *static_cast<const std::vector<std::uint16_t>*>(p);
                std::copy(expected.begin(), expected.end(), output);
            }, {}, Family::Clear};
        REQUIRE(accelerator(request, Backend::Automatic));
        REQUIRE(field == expected);
        REQUIRE(session.selection(Family::Clear).load() == Backend::CPU);
        auto& timing = session.timing(Family::Clear, 1);
        CHECK(timing.variantProbes.load() == 1);
        CHECK(session.tileSelection(Family::Clear, 1).load() == 0);
        const auto measured = openCLStatus();
        request.identity.variant = 4;
        field = seeds;
        REQUIRE(accelerator(request, Backend::Automatic));
        CHECK(field == expected);
        CHECK(openCLStatus().calibrations == measured.calibrations + 1);
        CHECK(timing.variantProbes.load() == 2);
        CHECK(timing.movements.load() == ((1u << 0) | (1u << 4)));
        for (unsigned repeat = 0; repeat < 8; ++repeat) {
            request.identity.variant = repeat % 2 ? 4 : 0;
            field = seeds;
            CHECK_FALSE(accelerator(request, Backend::Automatic));
            CHECK(field == seeds);
        }
        CHECK(openCLStatus().calibrations == measured.calibrations + 1);
        CHECK_FALSE(session.failed.load());
    }

    TEST_CASE("independent GPU lanes finish while another lane prepares different terrain")
    {
        using namespace gradient_kernel;
        RestoreBackend restore;
        if (!openCLStatus().available) return;
        BackendSession busySession, otherSession;
        const field::Grid busyGrid(16,16), otherGrid(32,16);
        auto busyCosts = std::make_shared<const std::vector<EntrySteps>>(busyGrid.cells(), LAND_STEPS);
        auto mutableCosts = std::make_shared<std::vector<EntrySteps>>(otherGrid.cells());
        for (std::size_t i = 0; i < mutableCosts->size(); ++i)
            (*mutableCosts)[i] = entrySteps(i % 7 == 0 ? 13 : 5);
        std::shared_ptr<const std::vector<EntrySteps>> otherCosts = mutableCosts;
        std::vector<std::uint16_t> busyField(busyGrid.cells(), 1), otherField(otherGrid.cells(), 1);
        busyField[7] = 65535;
        otherField[otherGrid.index(31,15)] = 65535;
        otherField[otherGrid.index(2,5)] = 65500;
        for (int y = 1; y < 15; ++y) otherField[otherGrid.index(9,y)] = 0;
        const auto busyExpected = oracle(busyField, busyGrid, *busyCosts, COST_LIMIT);
        const auto otherExpected = oracle(otherField, otherGrid, *otherCosts, COST_LIMIT);
        std::promise<void> entered, release;
        auto enteredFuture = entered.get_future();
        auto released = release.get_future().share();
        struct Context {
            const std::vector<EntrySteps>* costs;
            std::promise<void>* entered;
            std::shared_future<void> released;
            bool first = true;
        } busyContext{busyCosts.get(), &entered, released};
        const BackendRequest busyRequest{busyField.data(), COST_LIMIT, busyGrid, busySession, &busyContext,
            [](void* p, std::size_t i) {
                auto& context = *static_cast<Context*>(p);
                if (context.first) {
                    context.first = false;
                    context.entered->set_value();
                    context.released.wait();
                }
                return (*context.costs)[i];
            }, [](void*, std::uint16_t*) {}, {busyCosts,0,0,true}, Family::Guard};
        const BackendRequest otherRequest{otherField.data(), COST_LIMIT, otherGrid, otherSession,
            const_cast<std::vector<EntrySteps>*>(otherCosts.get()),
            [](void* p, std::size_t i) { return (*static_cast<const std::vector<EntrySteps>*>(p))[i]; },
            [](void*, std::uint16_t*) {}, {otherCosts,0,0,true}, Family::Materials};
        auto busy = std::async(std::launch::async, [&] {
            return accelerator(busyRequest, Backend::OpenCL);
        });
        CHECK(enteredFuture.wait_for(std::chrono::seconds(2)) == std::future_status::ready);
        auto other = std::async(std::launch::async, [&] {
            return accelerator(otherRequest, Backend::OpenCL);
        });
        // Do not query status while the held lane owns its preparation lock.
        const bool independentlyCompleted =
            other.wait_for(std::chrono::seconds(2)) == std::future_status::ready;
        release.set_value();
        CHECK(busy.get());
        CHECK(other.get());
        CHECK(independentlyCompleted);
        CHECK(busyField == busyExpected);
        CHECK(otherField == otherExpected);
        CHECK_FALSE(busySession.failed.load());
        CHECK_FALSE(otherSession.failed.load());
    }

    TEST_CASE("concurrent lanes calibrate each shared class once and preserve its choice")
    {
        using namespace gradient_kernel;
        RestoreBackend restore;
        if (!openCLStatus().available) return;
        BackendSession session;
        constexpr unsigned count = 12;
        const field::Grid grid(32,16);
        auto costs = std::make_shared<const std::vector<EntrySteps>>(grid.cells(), LAND_STEPS);
        std::vector<std::uint16_t> seeds(grid.cells(), 1);
        seeds[7] = 65535;
        seeds[grid.index(19,8)] = 0;
        const auto expected = oracle(seeds, grid, *costs, COST_LIMIT);
        std::array<std::vector<std::uint16_t>, count> fields;
        std::array<bool, count> handled{};
        const auto before = openCLStatus().calibrations;
        auto run = [&] {
            std::barrier start(count);
            std::array<std::thread, count> workers;
            for (unsigned i = 0; i < count; ++i) {
                fields[i] = seeds;
                workers[i] = std::thread([&, i] {
                    const BackendRequest request{fields[i].data(), COST_LIMIT, grid, session,
                        const_cast<std::vector<std::uint16_t>*>(&expected),
                        [](void*, std::size_t) { return LAND_STEPS; },
                        [](void* p, std::uint16_t* output) {
                            const auto& reference = *static_cast<const std::vector<std::uint16_t>*>(p);
                            std::copy(reference.begin(), reference.end(), output);
                        }, {costs,0,0,true}, Family::Markets};
                    start.arrive_and_wait();
                    // Explicit singleton groups all address the same class even
                    // when native rendezvous arrival timing would vary its size.
                    handled[i] = batchAccelerator(std::span(&request,1), Backend::Automatic);
                });
            }
            for (auto& worker : workers) worker.join();
            for (unsigned i = 0; i < count; ++i) {
                CHECK(handled[i]);
                CHECK(fields[i] == expected);
            }
        };
        run();
        CHECK(openCLStatus().calibrations == before + 1);
        const auto winner = session.selection(Family::Markets,1).load();
        CHECK(winner != Backend::Automatic);
        CHECK(session.selection(Family::Markets,2).load() == Backend::Automatic);
        run();
        CHECK(openCLStatus().calibrations == before + 1);
        CHECK(session.selection(Family::Markets,1).load() == winner);
        CHECK_FALSE(session.failed.load());
    }

    TEST_CASE("independent lanes keep mutable buffers and kernel arguments private across mixed grids")
    {
        using namespace gradient_kernel;
        RestoreBackend restore;
        if (!openCLStatus().available) return;
        constexpr unsigned count = 12, repeats = 3;
        struct Fixture {
            field::Grid grid;
            std::shared_ptr<const std::vector<EntrySteps>> costs;
            std::vector<std::uint16_t> seeds, expected;
        };
        std::vector<Fixture> fixtures;
        const std::array dimensions{std::pair{16,16}, std::pair{32,8}, std::pair{8,32},
                                    std::pair{64,16}, std::pair{17,35}, std::pair{35,17}};
        for (unsigned i = 0; i < count; ++i) {
            const auto [width,height] = dimensions[i % dimensions.size()];
            field::Grid grid(width,height);
            auto costs = std::make_shared<std::vector<EntrySteps>>(grid.cells());
            std::vector<std::uint16_t> seeds(grid.cells(), 1);
            for (std::size_t cell = 0; cell < grid.cells(); ++cell) {
                (*costs)[cell] = entrySteps((cell + i) % 5 == 0 ? 13 : 5);
                if ((cell + i * 3) % 19 == 0) seeds[cell] = 0;
            }
            seeds[grid.index(width-1,height-1)] = 65535;
            seeds[grid.index(1,1)] = 65500 - i;
            auto expected = oracle(seeds,grid,*costs,COST_LIMIT);
            fixtures.push_back({grid,costs,std::move(seeds),std::move(expected)});
        }
        BackendSession session;
        std::array<std::array<bool,repeats>,count> handled{}, exact{};
        std::array<std::thread,count> workers;
        std::barrier start(count);
        for (unsigned worker = 0; worker < count; ++worker)
            workers[worker] = std::thread([&,worker] {
                for (unsigned repeat = 0; repeat < repeats; ++repeat) {
                    const auto& fixture = fixtures[(worker+repeat) % fixtures.size()];
                    auto field = fixture.seeds;
                    const BackendRequest request{field.data(),COST_LIMIT,fixture.grid,session,
                        const_cast<std::vector<EntrySteps>*>(fixture.costs.get()),
                        [](void* p,std::size_t cell) {
                            return (*static_cast<const std::vector<EntrySteps>*>(p))[cell];
                        }, [](void*,std::uint16_t*) {}, {fixture.costs,0,0,true}, Family::Generic};
                    start.arrive_and_wait();
                    // More than one native batch's worth of simultaneous callers
                    // exercises independent queues with different buffer geometry.
                    handled[worker][repeat] = batchAccelerator(std::span(&request,1),Backend::OpenCL);
                    exact[worker][repeat] = field == fixture.expected;
                }
            });
        for (auto& worker : workers) worker.join();
        for (unsigned worker = 0; worker < count; ++worker)
            for (unsigned repeat = 0; repeat < repeats; ++repeat) {
                CHECK(handled[worker][repeat]);
                CHECK(exact[worker][repeat]);
            }
        CHECK_FALSE(session.failed.load());
    }

    TEST_CASE("concurrent singleton calibration uses its original CPU worker")
    {
        using namespace gradient_kernel;
        RestoreBackend restore;
        if (!openCLStatus().available) return;
        setBackend(Backend::Automatic);
        BackendSession session;
        constexpr unsigned count = 8, cells = 32 * 32;
        std::vector<std::uint16_t> seeds(cells, 1);
        seeds[7] = 65535;
        const auto expected = oracle(seeds, {32,32}, std::vector<EntrySteps>(cells, LAND_STEPS), COST_LIMIT);
        std::atomic<unsigned> active{0}, maximum{0};
        std::atomic<bool> wrongWorker{false};
        struct Context {
            const std::vector<std::uint16_t>* expected;
            std::thread::id worker;
            std::atomic<unsigned>* active;
            std::atomic<unsigned>* maximum;
            std::atomic<bool>* wrongWorker;
        };
        std::array<std::vector<std::uint16_t>, count> fields;
        std::array<std::thread, count> workers;
        std::barrier start(count);
        for (unsigned i = 0; i < count; ++i) {
            fields[i] = seeds;
            workers[i] = std::thread([&, i] {
                Context context{&expected, std::this_thread::get_id(), &active, &maximum, &wrongWorker};
                const BackendRequest request{fields[i].data(), COST_LIMIT, {32,32}, session, &context,
                    [](void*, std::size_t) { return LAND_STEPS; },
                    [](void* p, std::uint16_t* out) {
                        auto& c = *static_cast<Context*>(p);
                        if (std::this_thread::get_id() != c.worker) c.wrongWorker->store(true);
                        const auto running = c.active->fetch_add(1) + 1;
                        auto previous = c.maximum->load();
                        while (previous < running && !c.maximum->compare_exchange_weak(previous, running)) {}
                        // Keep a calibration sample open long enough for other
                        // waiting callers to participate; no speed assertion.
                        std::this_thread::sleep_for(std::chrono::milliseconds(2));
                        std::copy(c.expected->begin(), c.expected->end(), out);
                        c.active->fetch_sub(1);
                    }, {}, Family::Generic};
                start.arrive_and_wait();
                if (!accelerator(request, Backend::Automatic)) request.cpu(request.context, request.gradient);
            });
        }
        for (auto& worker : workers) worker.join();
        CHECK_FALSE(wrongWorker.load());
        CHECK(maximum.load() >= 1);
        CHECK_FALSE(session.failed.load());
        for (const auto& field : fields) CHECK(field == expected);
    }

    TEST_CASE("families and actual batch sizes benchmark independently and retain choices")
    {
        using namespace gradient_kernel;
        RestoreBackend restore;
        if (!openCLStatus().available) return;
        setBackend(Backend::Automatic);
        BackendSession session;
        constexpr unsigned cells = 32 * 32;
        std::vector<std::uint16_t> seeds(cells, 1);
        seeds[7] = 65535;
        std::vector<EntrySteps> costs(cells, LAND_STEPS);
        auto expected = oracle(seeds, {32, 32}, costs, COST_LIMIT);
        struct Context { const std::vector<std::uint16_t>* expected; unsigned calls = 0; };
        static unsigned cpuBatches = 0;
        cpuBatches = 0;
        const auto cpuBatch = [](std::span<const BackendRequest* const> requests,
                                 std::span<std::uint16_t* const> destinations) {
            ++cpuBatches;
            for (std::size_t i = 0; i < requests.size(); ++i)
                requests[i]->cpu(requests[i]->context, destinations[i]);
        };
        auto run = [&](Family family, unsigned count) {
            std::vector<std::vector<std::uint16_t>> fields(count, seeds);
            std::vector<Context> contexts(count, {&expected});
            std::vector<BackendRequest> requests;
            for (unsigned i = 0; i < count; ++i)
                requests.push_back({fields[i].data(), COST_LIMIT, {32, 32}, session, &contexts[i],
                    [](void*, std::size_t) { return LAND_STEPS; },
                    [](void* p, std::uint16_t* out) {
                        auto& context = *static_cast<Context*>(p);
                        ++context.calls;
                        std::copy(context.expected->begin(), context.expected->end(), out);
                    }, {}, family, cpuBatch});
            REQUIRE(batchAccelerator(requests, Backend::Automatic));
            for (const auto& field : fields) CHECK(field == expected);
        };
        auto calibrations = openCLStatus().calibrations;
        for (unsigned count = 1; count <= 8; ++count)
        {
            CHECK(session.selection(Family::Materials, count).load() == Backend::Automatic);
            const auto cpuBefore = cpuBatches;
            run(Family::Materials, count);
            CHECK(cpuBatches == cpuBefore + 3);
            CHECK(openCLStatus().calibrations == ++calibrations);
            const auto winner = session.selection(Family::Materials, count).load();
            CHECK(winner != Backend::Automatic);
            CHECK(session.selection(Family::Guard, count).load() == Backend::Automatic);
            run(Family::Materials, count);
            CHECK(openCLStatus().calibrations == calibrations);
            CHECK(session.selection(Family::Materials, count).load() == winner);
            CHECK(cpuBatches == cpuBefore + 3 + (winner == Backend::CPU ? 1 : 0));
        }
        run(Family::Guard, 2);
        CHECK(openCLStatus().calibrations == ++calibrations);
        run(Family::Markets, 1);
        CHECK(openCLStatus().calibrations == ++calibrations);
        // Existing CPU and GPU choices coexist, including in a mixed-family call.
        session.selection(Family::Guard, 2).store(Backend::OpenCL);
        session.selection(Family::Markets, 1).store(Backend::CPU);
        std::vector<std::vector<std::uint16_t>> fields(3, seeds);
        Context context{&expected};
        std::vector<BackendRequest> mixed;
        for (unsigned i = 0; i < 3; ++i)
            mixed.push_back({fields[i].data(), COST_LIMIT, {32,32}, session, &context,
                [](void*, std::size_t) { return LAND_STEPS; },
                [](void* p, std::uint16_t* out) {
                    auto& c = *static_cast<Context*>(p); ++c.calls;
                    std::copy(c.expected->begin(), c.expected->end(), out);
                }, {}, i == 2 ? Family::Markets : Family::Guard, cpuBatch});
        const auto before = openCLStatus();
        REQUIRE(batchAccelerator(mixed, Backend::Automatic));
        CHECK(openCLStatus().calibrations == before.calibrations);
        CHECK(openCLStatus().fields == before.fields + 2);
        CHECK(context.calls == 1);
        for (const auto& field : fields) CHECK(field == expected);
        BackendSession nextGame;
        CHECK(nextGame.selection(Family::Guard, 2).load() == Backend::Automatic);
        // An error disables every class, including previously GPU-selected ones.
        session.failed.store(true);
        CHECK_FALSE(batchAccelerator(mixed, Backend::OpenCL));
        CHECK(session.selection(Family::Guard, 2).load() == Backend::OpenCL);
    }

}
