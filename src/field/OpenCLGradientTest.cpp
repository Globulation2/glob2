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
void onWorker(const std::function<void()>& function)
{
    if constexpr(!GAGCore::ThreadSupport::available) { function(); return; }
    if(ComputeExecutor::workerSlot()) { function(); return; }
    ComputeExecutor executor; executor.configure(2);
    REQUIRE(executor.threadCount()==2);
    const ComputeExecutor::Group group{1,{[](void* p,std::size_t){ (*static_cast<const std::function<void()>*>(p))(); },const_cast<std::function<void()>*>(&function)}};
    auto ticket=executor.submit(std::span(&group,1)); executor.join(ticket);
}
void initializeForTest()
{
    using namespace gradient_kernel;
    if constexpr(!GAGCore::ThreadSupport::available) return;
    if(initializationState.load()==2) return;
    const auto previous=backend(); setBackend(Backend::OpenCL);
    auto service=std::make_shared<AdaptiveGradientPolicy>(); service->configure(3,false);
    ComputeExecutor executor; executor.configure(3); executor.setWorkerOnly(service);
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(30);
    while(initializationState.load()!=2 && std::chrono::steady_clock::now()<deadline) std::this_thread::yield();
    setBackend(previous);
    REQUIRE(initializationState.load()==2);
}

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
        initializeForTest();
        onWorker([&] {
        using namespace gradient_kernel;
        RestoreBackend restore;
        const auto initial = openCLStatus();
        INFO("OpenCL device: " << initial.device << "; fallback reason: " << initial.error);
        const bool supported=(readyPlans.load()&(1u<<unsigned(Plan::Frozen8)))!=0;
        setBackend(supported ? Backend::OpenCL : Backend::Automatic);
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
        if (supported)
        {
            const auto final = openCLStatus();
            REQUIRE_MESSAGE(final.available, final.error);
            CHECK(final.fields > initial.fields);
        }
        });
    }
    TEST_CASE("custom prepared costs preserve forbidden and deferred seeds")
    {
        initializeForTest();
        onWorker([&] {
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
        });
    }
    TEST_CASE("frozen halos retain the full global convergence budget")
    {
        initializeForTest();
        onWorker([&] {
        using namespace gradient_kernel;
        RestoreBackend restore;
        if (!(readyPlans.load()&(1u<<unsigned(Plan::Frozen8))))
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
        // Required execution uses the established frozen8 plan, without a tournament.
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
        });
    }

    TEST_CASE("immutable identities skip cost callbacks and invalidate every cost dependency")
    {
        initializeForTest();
        onWorker([&] {
        using namespace gradient_kernel;
        RestoreBackend restore;
        if (!(readyPlans.load()&(1u<<unsigned(Plan::Frozen8))))
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
        });
    }
    TEST_CASE("uniform cost aliases coexist with varied planes and invalidate on revision")
    {
        initializeForTest();
        onWorker([&] {
        using namespace gradient_kernel;
        RestoreBackend restore;
        if (!(readyPlans.load()&(1u<<unsigned(Plan::Frozen8))))
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
            REQUIRE(batchAccelerator(requests, Plan::Frozen8));
            for (const auto &context : contexts) CHECK(context.actual == context.expected);
            const auto after = openCLStatus();
            REQUIRE_MESSAGE(after.available, after.error);
            if (!revision) CHECK(after.costCacheHits > before.costCacheHits);
        }
        });
    }
    TEST_CASE("explicit scheduler batches span chunks and retire mixed fields exactly")
    {
        initializeForTest();
        onWorker([&] {
        using namespace gradient_kernel;
        RestoreBackend restore;
        if (!(readyPlans.load()&(1u<<unsigned(Plan::Frozen8))))
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
        REQUIRE(batchAccelerator(requests, Plan::Frozen8));
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
        });
    }
    TEST_CASE("independent GPU lanes finish while another lane prepares different terrain")
    {
        initializeForTest();
        onWorker([&] {
        using namespace gradient_kernel;
        RestoreBackend restore;
        if (!(readyPlans.load()&(1u<<unsigned(Plan::Frozen8)))) return;
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
            return accelerator(busyRequest, Plan::Frozen8);
        });
        CHECK(enteredFuture.wait_for(std::chrono::seconds(2)) == std::future_status::ready);
        auto other = std::async(std::launch::async, [&] {
            return accelerator(otherRequest, Plan::Frozen8);
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
        });
    }

    TEST_CASE("independent lanes keep mutable buffers and kernel arguments private across mixed grids")
    {
        initializeForTest();
        onWorker([&] {
        using namespace gradient_kernel;
        RestoreBackend restore;
        if (!(readyPlans.load()&(1u<<unsigned(Plan::Frozen8)))) return;
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
                    handled[worker][repeat] = batchAccelerator(std::span(&request,1),Plan::Frozen8);
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
        });
    }


}

TEST_SUITE("OpenCLGradient")
{
TEST_CASE("gradient policy rejects invalid families before eligibility or indexing")
{
    using namespace gradient_kernel;
    BackendSession policy;
    for (const auto family : {Family::Count, Family(-1), Family(1000)}) {
        CHECK_THROWS_AS(category(family, 1), std::invalid_argument);
        CHECK_THROWS_AS(policy.establish(family, 1, Plan::CPU), std::invalid_argument);
        CHECK_THROWS_AS(policy.decision(family, 1), std::invalid_argument);
        // Owner and resumable short circuits must not hide invalid input.
        CHECK_THROWS_AS(policy.choose(family, 1, Backend::CPU), std::invalid_argument);
        CHECK_THROWS_AS(policy.choose(family, 1, Backend::CPU, Operation::ResumableSearch), std::invalid_argument);
    }
    CHECK(policy.decision(Family::Generic, 1).version == 0);
}
TEST_CASE("direct gradient groups validate bounds and homogeneity before work")
{
    using namespace gradient_kernel;
    unsigned calls = 0;
    std::uint16_t value = 1;
    BackendSession policy, otherPolicy;
    const auto cpu = [](void* context, std::uint16_t* out) {
        ++*static_cast<unsigned*>(context); *out = 123;
    };
    const BackendRequest request{&value, COST_LIMIT, {1, 1}, policy, &calls,
        [](void*, std::size_t) { return LAND_STEPS; }, cpu, {}};
    std::vector<BackendRequest> requests{request};
    SUBCASE("oversized group") { for (unsigned i = 1; i < 9; ++i) requests.push_back(request); }
    SUBCASE("different families") {
        requests.push_back(request); requests.back().family = Family::Materials;
    }
    SUBCASE("different operations") {
        requests.push_back(request); requests.back().operation = Operation::ResumableSearch;
    }
    SUBCASE("different sessions") {
        requests.push_back(BackendRequest{&value, COST_LIMIT, {1, 1}, otherPolicy,
            &calls, request.costAt, cpu, {}});
    }
    SUBCASE("invalid family") {
        requests.front().family = Family::Count;
    }
    // Run on the owner: invalid contracts must be checked even when GPU and
    // observation sampling would otherwise be skipped by owner eligibility.
    CHECK_THROWS_AS(executeGradientGroup(requests, Backend::CPU), std::invalid_argument);
    CHECK(calls == 0);
    CHECK(value == 1);
    CHECK(policy.metrics().recorded == 0);
    CHECK_NOTHROW(executeGradientGroup({}, Backend::CPU));
    CHECK(calls == 0);
}
TEST_CASE("gradient batches reject invalid later families before executing any group")
{
    using namespace gradient_kernel;
    unsigned calls = 0;
    std::uint16_t value = 1;
    BackendSession policy;
    const BackendRequest request{&value, COST_LIMIT, {1, 1}, policy, &calls,
        [](void*, std::size_t) { return LAND_STEPS; },
        [](void* context, std::uint16_t*) { ++*static_cast<unsigned*>(context); }, {}};
    std::vector<BackendRequest> requests(9, request);
    requests.back().family = Family(-1);
    CHECK_THROWS_AS(executeGradientBatch(requests, Backend::CPU), std::invalid_argument);
    CHECK(calls == 0);
    CHECK_NOTHROW(executeGradientBatch({}, Backend::CPU));
}
TEST_CASE("malformed optional observations are dropped before profile processing")
{
    if constexpr(!GAGCore::ThreadSupport::available) return;
    using namespace gradient_kernel;
    BackendSession policy;
    policy.configure(2, true);
    onWorker([&] {
        GradientObservation observation;
        observation.family = Family::Count;
        policy.record(observation);
        observation.family = Family::Generic;
        observation.decision.plan = Plan::Count;
        policy.record(observation);
    });
    CHECK(policy.metrics().dropped == 2);
    CHECK(policy.metrics().recorded == 0);
}
TEST_CASE("established plans execute once without calibration and unknown work never scans costs")
{
    if constexpr(!GAGCore::ThreadSupport::available) return;
    using namespace gradient_kernel;
    RestoreBackend restore;
    const auto old=accelerator; const auto mask=readyPlans.load();
    struct RestoreProvider { decltype(accelerator) old; unsigned mask; ~RestoreProvider(){accelerator=old;readyPlans.store(mask);} } provider{old,mask};
    struct Context { unsigned cpu=0,gpu=0,costs=0; } context;
    accelerator=[](const BackendRequest& request,Plan plan) {
        CHECK(plan==Plan::Colored4); ++static_cast<Context*>(request.context)->gpu;
        request.gradient[0]=123; return true;
    };
    BackendSession policy;
    std::uint16_t value=77;
    BackendRequest request{&value,COST_LIMIT,{1,1},policy,&context,
        [](void* p,std::size_t){++static_cast<Context*>(p)->costs;return LAND_STEPS;},
        [](void* p,std::uint16_t* out){++static_cast<Context*>(p)->cpu;*out=123;},{},Family::Materials};
    readyPlans.store(0);
    onWorker([&] {
        executeGradientGroup(std::span(&request,1),Backend::Automatic);
        CHECK(context.cpu==1); CHECK(context.gpu==0); CHECK(context.costs==0);
        policy.establish(Family::Materials,1,Plan::Colored4);
        executeGradientGroup(std::span(&request,1),Backend::Automatic);
        CHECK(context.cpu==2); CHECK(context.gpu==0);
        readyPlans.store(1u<<unsigned(Plan::Colored4));
        executeGradientGroup(std::span(&request,1),Backend::Automatic);
        CHECK(context.gpu==1); CHECK(context.cpu==2); CHECK(value==123);
        request.operation=Operation::ResumableSearch;
        executeGradientGroup(std::span(&request,1),Backend::Automatic);
        CHECK(context.gpu==1); CHECK(context.cpu==3);
    });
    request.operation=Operation::CompleteField;
    executeGradientGroup(std::span(&request,1),Backend::Automatic); // owner is ineligible
    CHECK(context.gpu==1); CHECK(context.cpu==4);
}
TEST_CASE("declined established plans preserve original seeds and recover exactly once")
{
    if constexpr(!GAGCore::ThreadSupport::available) return;
    using namespace gradient_kernel;
    const auto old=accelerator; const auto mask=readyPlans.load();
    struct Restore { decltype(accelerator) old; unsigned mask; ~Restore(){accelerator=old;readyPlans.store(mask);} } restore{old,mask};
    readyPlans.store(1u<<unsigned(Plan::Frozen8));
    accelerator=[](const BackendRequest& request,Plan){request.session.fail();return false;};
    BackendSession policy; policy.establish(Family::Generic,1,Plan::Frozen8);
    const auto before=policy.decision(Family::Generic,1);
    unsigned cpu=0; std::uint16_t value=77;
    BackendRequest request{&value,60,{1,1},policy,&cpu,[](void*,std::size_t){return LAND_STEPS;},
        [](void* p,std::uint16_t* out){CHECK(*out==77);++*static_cast<unsigned*>(p);*out=123;},{}};
    onWorker([&]{executeGradientGroup(std::span(&request,1),Backend::Automatic);});
    CHECK(value==123); CHECK(cpu==1); CHECK(policy.failed.load());
    CHECK(policy.decision(Family::Generic,1).generation!=before.generation);
}
TEST_CASE("executor reconfiguration preserves pending established plan preparation")
{
    using namespace gradient_kernel;
    RestoreBackend restoreBackend;
    const auto oldState=initializationState.exchange(0);
    struct RestoreState { unsigned value; ~RestoreState(){initializationState.store(value);} } restoreState{oldState};
    setBackend(Backend::Automatic);
    BackendSession policy;
    policy.configure(1,false);
    CHECK_FALSE(policy.pending());
    policy.establish(Family::Materials,1,Plan::Frozen8);
    REQUIRE(policy.pending());
    const auto before=policy.decision(Family::Materials,1);
    policy.configure(8,false);
    CHECK(policy.pending());
    CHECK(policy.decision(Family::Materials,1).plan==Plan::Frozen8);
    CHECK(policy.decision(Family::Materials,1).generation!=before.generation);
    setBackend(Backend::CPU);
    policy.configure(8,false);
    CHECK_FALSE(policy.pending());
    policy.establish(Family::Materials,1,Plan::CPU);
    setBackend(Backend::Automatic);
    policy.configure(8,false);
    CHECK_FALSE(policy.pending());
}
TEST_CASE("passive buffers are bounded worker only and reject retired decisions and configurations")
{
    if constexpr(!GAGCore::ThreadSupport::available) return;
    using namespace gradient_kernel;
    BackendSession policy; policy.configure(2,true);
    REQUIRE(policy.metrics().retainedBytes<=512*1024);
    GradientObservation sample; sample.threads=2; sample.executionNs=19; sample.serviceNs=23; sample.queueNs=4;
    sample.decision=policy.decision(Family::Generic,1);
    policy.record(sample); policy.process(); CHECK(policy.metrics().recorded==0);
    onWorker([&] { for(unsigned i=0;i<64;++i) policy.record(sample); });
    CHECK(policy.metrics().recorded==32); CHECK(policy.metrics().dropped==32);
    onWorker([&] { policy.process(); });
    CHECK(policy.metrics().accepted==AdaptiveGradientPolicy::PassLimit);
    policy.establish(Family::Generic,1,Plan::CPU); // same algorithm, new decision version
    onWorker([&] { for(unsigned i=0;i<4;++i) policy.process(); });
    CHECK(policy.metrics().stale==24);
    sample.decision=policy.decision(Family::Generic,1);
    onWorker([&] { policy.record(sample); });
    policy.configure(3,true);
    onWorker([&] { policy.process(); });
    CHECK(policy.metrics().stale==25);
    CHECK(policy.metrics().executionNs==19*8);
    CHECK(policy.metrics().serviceNs==23*8);
    CHECK(policy.metrics().queueNs==4*8);
}
TEST_CASE("every compiled explicit kernel matches the independent oracle without tournaments")
{
    using namespace gradient_kernel;
    initializeForTest();
    const field::Grid grid(31,17);
    std::vector<std::uint16_t> seeds(grid.cells(),1);
    std::vector<EntrySteps> costs(grid.cells());
    for(std::size_t i=0;i<grid.cells();++i) {
        costs[i]=entrySteps(i%7 ? 5 : 13); if(i%13==0) seeds[i]=0;
    }
    seeds[1]=65535; seeds[77]=65400;
    const auto expected=oracle(seeds,grid,costs,COST_LIMIT);
    BackendSession session;
    const auto before=openCLStatus();
    for(unsigned plan=1;plan<unsigned(Plan::Count);++plan) {
        if(!(readyPlans.load()&(1u<<plan))) continue;
        auto actual=seeds;
        BackendRequest request{actual.data(),COST_LIMIT,grid,session,&costs,
            [](void* p,std::size_t i){return (*static_cast<std::vector<EntrySteps>*>(p))[i];},
            [](void*,std::uint16_t*){FAIL("explicit kernel must not call a CPU reference");},{}};
        REQUIRE(accelerator(request,Plan(plan))); CHECK(actual==expected);
        const auto executed = openCLStatus();
        CHECK(executed.tileWidth == PLANS[plan].tileWidth);
        CHECK(executed.tileHeight == PLANS[plan].tileHeight);
        CHECK(executed.localSteps == PLANS[plan].steps);
        CHECK(executed.colored == PLANS[plan].colored);
    }
    CHECK(openCLStatus().calibrations==before.calibrations);
    CHECK(openCLStatus().tunings==before.tunings);
}
TEST_CASE("device service calls preserve exact fields and release lane buffer budgets")
{
    using namespace gradient_kernel;
    if(!initializeOpenCL() || !(readyPlans.load()&(1u<<unsigned(Plan::Frozen8)))) return;
    const field::Grid grid(257,63);
    std::vector<std::uint16_t> seeds(grid.cells(),1);
    auto costs=std::make_shared<std::vector<EntrySteps>>(grid.cells(),LAND_STEPS);
    for(std::size_t i=0;i<grid.cells();++i) if(i%41==0) seeds[i]=0;
    seeds[3]=65535;seeds[79]=65475;
    const auto expected=oracle(seeds,grid,*costs,600);
    BackendSession session;
    const auto before=openCLStatus();
    std::thread service([&] {
        CHECK(ComputeExecutor::workerSlot()==0);
        BackendRequest request{seeds.data(),600,grid,session,costs.get(),
            [](void* p,std::size_t i){return (*static_cast<std::vector<EntrySteps>*>(p))[i];},
            [](void*,std::uint16_t*){FAIL("direct device execution cannot recover on CPU");},
            {costs,0,1,true,costs->capacity()*sizeof(EntrySteps)}};
        bool dispatched=false;request.executedOnDevice=&dispatched;
        CHECK(executeOpenCLDevice(std::span(&request,1),Plan::Frozen8));
        CHECK(dispatched);
        CHECK(seeds==expected);
        const auto running=openCLStatus();
        const std::size_t tiles=((grid.width()+15)/16)*((grid.height()+15)/16);
        // One cost plane, two field buffers and two tile masks; no eight-field
        // reservation for this singleton. Existing cache eviction may lower it.
        CHECK(running.deviceBytes<=before.deviceBytes+grid.cells()*8+tiles*8+288);
        CHECK(running.hostBytes<=OpenCLHostBudget);
        CHECK(running.deviceBytes<=OpenCLDeviceBudget);
    });
    service.join();
    const auto after=openCLStatus();
    CHECK(after.fields==before.fields+1);
    CHECK(after.deviceBytes<=before.deviceBytes+grid.cells()*sizeof(std::uint32_t));
    CHECK(after.peakHostBytes<=OpenCLHostBudget);
    CHECK(after.peakDeviceBytes<=OpenCLDeviceBudget);
}
TEST_CASE("immutable owner budget rejection preserves seeds and healthy device state")
{
    using namespace gradient_kernel;
    if(!initializeOpenCL() || !(readyPlans.load()&(1u<<unsigned(Plan::Frozen8)))) return;
    const field::Grid grid(5,7);
    auto owner=std::make_shared<std::array<std::uint8_t,1024>>();
    std::vector<std::uint16_t> seeds(grid.cells(),1);seeds[0]=65535;
    const auto original=seeds;
    BackendSession session;
    const auto before=openCLStatus();
    std::thread service([&] {
        BackendRequest request{seeds.data(),60,grid,session,nullptr,
            [](void*,std::size_t){return LAND_STEPS;},[](void*,std::uint16_t*){},
            {owner,987654321,1,true,OpenCLHostBudget}};
        bool dispatched=true;request.executedOnDevice=&dispatched;
        // Even an otherwise empty cache cannot fit an owner consuming the whole
        // budget plus its plane. This tests retained ownership independently of
        // staging capacity or cache contents established by earlier tests.
        CHECK_FALSE(executeOpenCLDevice(std::span(&request,1),Plan::Frozen8));
        CHECK_FALSE(dispatched);CHECK(seeds==original);CHECK_FALSE(session.failed.load());
        request.identity.retainedBytes=owner->size();
        CHECK(executeOpenCLDevice(std::span(&request,1),Plan::Frozen8));
        CHECK(dispatched);
    });
    service.join();
    const auto after=openCLStatus();
    CHECK(after.budgetDeclines==before.budgetDeclines+1);
    CHECK(after.fields==before.fields+1);
    CHECK(after.available);
    CHECK(after.hostBytes<=OpenCLHostBudget);
    CHECK(after.deviceBytes<=OpenCLDeviceBudget);
}
TEST_CASE("trivial gradient work is shared by CPU and reports no device dispatch")
{
    using namespace gradient_kernel;
    BackendSession session;
    std::array<std::uint16_t,4> seeds{0,65535,0,65535};
    bool dispatched=true;
    BackendRequest request{seeds.data(),60,{2,2},session,nullptr,
        [](void*,std::size_t){FAIL("trivial field needs no costs");return LAND_STEPS;},
        [](void*,std::uint16_t*){FAIL("trivial CPU field needs no propagation");},{}};
    request.executedOnDevice=&dispatched;
    executeGradientGroup(std::span(&request,1),Backend::CPU);
    CHECK_FALSE(dispatched);
    if(!initializeOpenCL() || !(readyPlans.load()&(1u<<unsigned(Plan::Frozen8)))) return;
    const auto before=openCLStatus();dispatched=true;
    REQUIRE(executeOpenCLDevice(std::span(&request,1),Plan::Frozen8));
    CHECK_FALSE(dispatched);
    const auto after=openCLStatus();
    CHECK(after.noopFields==before.noopFields+1);
    CHECK(after.dispatches==before.dispatches);
    CHECK(after.fields==before.fields);
}
TEST_CASE("uniform metadata uses scalar costs only with a complete immutable proof")
{
    using namespace gradient_kernel;
    if(!initializeOpenCL() || !(readyPlans.load()&(1u<<unsigned(Plan::Frozen8)))) return;
    const bool enabled=openCLStatus().uniformMetadata;
    const EntrySteps step{17,23};
    const std::uint32_t packed=step.cardinal|(step.diagonal<<16);
    unsigned calls=0;
    auto run=[&](field::Grid grid,bool allCells,std::uint32_t proof,bool varied) {
        auto owner=std::make_shared<std::uint8_t>();
        std::vector<EntrySteps> costs(grid.cells(),step);
        if(varied) for(std::size_t i=0;i<costs.size();i+=2) costs[i]={19,29};
        std::vector<std::uint16_t> actual(grid.cells(),1);actual[0]=65535;actual[1]=0;
        const auto expected=oracle(actual,grid,costs,600);
        BackendSession session;
        auto callback=[&](std::size_t i){++calls;return costs[i];};
        BackendRequest request{actual.data(),600,grid,session,&callback,
            [](void* p,std::size_t i){return (*static_cast<decltype(callback)*>(p))(i);},
            [](void*,std::uint16_t*){FAIL("metadata execution cannot call CPU fallback");},
            {owner,81234567,1,allCells,sizeof(*owner),proof}};
        const auto before=calls;
        REQUIRE(executeOpenCLDevice(std::span(&request,1),Plan::Frozen8));
        CHECK(actual==expected);
        const auto expectedCalls=enabled&&allCells&&proof&&(proof&65535u)&&(proof>>16)
            ? 0 : grid.cells()-std::size_t(!allCells);
        CHECK(calls-before==expectedCalls);
    };
    std::thread service([&] {
        run({7,13},true,packed,false);
        const auto before=openCLStatus();
        // Proven and discovered uniform planes alias safely across shapes.
        run({31,17},true,packed,false);
        run({13,7},true,0,false);
        if(enabled) CHECK(openCLStatus().costCacheHits>=before.costCacheHits+2);
        // Metadata with a forbidden-dependent or invalid pair is ignored.
        run({7,13},false,packed,true);
        run({13,7},true,step.cardinal,false);
        // Varied contents cannot alias the scalar uniform storage.
        run({7,13},true,0,true);
    });
    service.join();
}
TEST_CASE("shared offload host reservations are bounded and recover after release")
{
    using namespace gradient_kernel;
    const auto before=openCLStatus();
    REQUIRE(before.hostBytes<=OpenCLHostBudget);
    const auto available=OpenCLHostBudget-before.hostBytes;
    {
        REQUIRE(reserveOpenCLHostBytes(available));
        struct Release {std::size_t bytes;~Release(){releaseOpenCLHostBytes(bytes);}} release{available};
        CHECK(openCLStatus().hostBytes==OpenCLHostBudget);
        CHECK_FALSE(reserveOpenCLHostBytes(1));
        CHECK_FALSE(reserveOpenCLHostBytes(OpenCLHostBudget+1));
        CHECK(openCLStatus().hostBytes==OpenCLHostBudget);
    }
    CHECK(openCLStatus().hostBytes==before.hostBytes);
}
TEST_CASE("optional reference reservations share a bounded subset of total host storage")
{
    using namespace gradient_kernel;
    const auto before=openCLStatus();
    const auto available=std::min<std::size_t>(OpenCLProbeBudget-openCLProbeBytes(),OpenCLHostBudget-before.hostBytes);
    REQUIRE(reserveOpenCLProbeBytes(available));
    CHECK(openCLStatus().hostBytes==before.hostBytes+available);
    CHECK(openCLProbeBytes()==before.probeBytes+available);
    CHECK_FALSE(reserveOpenCLProbeBytes(OpenCLProbeBudget-available+1));
    releaseOpenCLProbeBytes(available);
    CHECK(openCLStatus().hostBytes==before.hostBytes);
    CHECK(openCLProbeBytes()==before.probeBytes);
}
}

TEST_CASE("worker initialization failure leaves CPU available without inline retries" * doctest::test_suite("OpenCLGradient"))
{
    if constexpr(!GAGCore::ThreadSupport::available) return;
    using namespace gradient_kernel;
    RestoreBackend restoreBackend;
    const auto old=prepareAccelerator;
    const auto state=initializationState.load(),mask=readyPlans.load();
    struct Restore { decltype(prepareAccelerator) old; unsigned state,mask;
        ~Restore(){prepareAccelerator=old;initializationState.store(state);readyPlans.store(mask);}
    } restore{old,state,mask};
    static unsigned attempts=0; attempts=0;
    prepareAccelerator=[] {CHECK(ComputeExecutor::workerSlot()!=0);++attempts;throw std::runtime_error("injected compiler failure");};
    initializationState.store(0);readyPlans.store(0);setBackend(Backend::OpenCL);
    BackendSession policy;policy.configure(2,false);
    policy.process();CHECK(attempts==0);CHECK(policy.pending());
    onWorker([&] {
        CHECK(policy.choose(Family::Generic,1,Backend::OpenCL).plan==Plan::CPU);
        policy.process();CHECK(attempts==1);CHECK_FALSE(policy.pending());
        CHECK(policy.choose(Family::Generic,1,Backend::OpenCL).plan==Plan::CPU);
        policy.process();CHECK(attempts==1);
    });
}
