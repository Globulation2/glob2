// SPDX-License-Identifier: GPL-3.0-or-later
#include "OpenCLGradient.h"
#include "OpenCLCpuAttribution.h"
#include "Glob2Test.h"
#include "GradientPropagation.h"
#include "TerrainGradient.h"
#include <barrier>
#include <cstdlib>
#include <cstring>
#include <future>
#include <queue>
#include <random>
#include <thread>

namespace
{
struct AttributionClock {
    std::array<std::uint64_t,8> values{};unsigned at=0;
    static std::uint64_t read(void* value) noexcept {
        auto& clock=*static_cast<AttributionClock*>(value);
        return clock.at<clock.values.size() ? clock.values[clock.at++] : 0;
    }
};
struct ProbeClock {
    mutable std::uint64_t next=1000;
    mutable unsigned reads=0;
    unsigned failRead=~0u;
    std::uint64_t invalidValue=0;
    static std::uint64_t read(const void* value) noexcept {
        auto& clock=*static_cast<const ProbeClock*>(value);
        return clock.reads++==clock.failRead ? clock.invalidValue : ++clock.next;
    }
};
void onWorker(const std::function<void()>& function)
{
    if constexpr(!GAGCore::ThreadSupport::available) { function(); return; }
    if(ComputeExecutor::workerSlot()) { function(); return; }
    ComputeExecutor executor; executor.configure(2);
    REQUIRE(executor.threadCount()==2);
    const ComputeExecutor::Group group{1,{[](void* p,std::size_t){ (*static_cast<const std::function<void()>*>(p))(); },const_cast<std::function<void()>*>(&function)}};
    auto ticket=executor.submit(std::span(&group,1)); executor.join(ticket);
}

TEST_CASE("OpenCL CPU scopes conserve nested CPU and expose invalid readings")
{
    using namespace gradient_kernel;
    OpenCLApiCpuCounters measured;
    OpenCLCpuAttribution timer(measured);timer.mode=1;
    AttributionClock clock{{100,110,120,130,150,200}};
    timer.setClockForTesting(&clock,AttributionClock::read);
    {
        OpenCLCpuAttribution::Scope parent(timer,OpenCLCpuCategory::Other);
        {
            OpenCLCpuAttribution::Scope preparation(timer,OpenCLCpuCategory::Preparation);
            OpenCLCpuAttribution::Scope upload(timer,OpenCLCpuCategory::Upload);
        }
    }
    CHECK(measured.ns[unsigned(OpenCLCpuCategory::Other)]==60);
    CHECK(measured.ns[unsigned(OpenCLCpuCategory::Preparation)]==30);
    CHECK(measured.ns[unsigned(OpenCLCpuCategory::Upload)]==10);
    CHECK(measured.coveredNs==100);CHECK(measured.clockReads==6);
    CHECK(measured.invalidScopes==0);CHECK(measured.reconciliationErrors==0);
    measured={};clock={{100,110,109,200}};
    {
        OpenCLCpuAttribution::Scope parent(timer,OpenCLCpuCategory::Other);
        OpenCLCpuAttribution::Scope child(timer,OpenCLCpuCategory::KernelEnqueue);
    }
    CHECK(measured.invalidScopes==2);CHECK(measured.coveredNs==0);
    CHECK(measured.ns[unsigned(OpenCLCpuCategory::KernelEnqueue)]==0);
    measured={};clock={{100,110,200,150}};
    {
        OpenCLCpuAttribution::Scope parent(timer,OpenCLCpuCategory::Other);
        OpenCLCpuAttribution::Scope child(timer,OpenCLCpuCategory::Fill);
    }
    CHECK(measured.reconciliationErrors==1);CHECK(measured.coveredNs==0);
    CHECK(measured.ns[unsigned(OpenCLCpuCategory::Fill)]==90);
    measured={};clock={{0,200}};
    {OpenCLCpuAttribution::Scope invalid(timer,OpenCLCpuCategory::CheckRead);}
    CHECK(measured.invalidScopes==1);CHECK(measured.coveredNs==0);
    measured={};clock={{100,200}};
    measured.ns[unsigned(OpenCLCpuCategory::OutputCopy)]=UINT64_MAX;
    {OpenCLCpuAttribution::Scope overflow(timer,OpenCLCpuCategory::OutputCopy);}
    CHECK(measured.reconciliationErrors==1);CHECK(measured.coveredNs==0);
    CHECK(measured.ns[unsigned(OpenCLCpuCategory::OutputCopy)]==UINT64_MAX);
}
TEST_CASE("OpenCL empty CPU brackets match clock count and off mode reads nothing")
{
    using namespace gradient_kernel;
    OpenCLApiCpuCounters counters;
    OpenCLCpuAttribution timer(counters);timer.mode=2;
    AttributionClock clock{{100,101,200,203}};timer.setClockForTesting(&clock,AttributionClock::read);
    {
        OpenCLCpuAttribution::Scope parent(timer,OpenCLCpuCategory::Other);
        OpenCLCpuAttribution::Scope child(timer,OpenCLCpuCategory::Arguments);
    }
    CHECK(counters.clockReads==4);CHECK(counters.controlBracketNs==4);
    CHECK(counters.coveredNs==0);
    for(const auto value:counters.ns)CHECK(value==0);
    CHECK(counters.calls[unsigned(OpenCLCpuCategory::Arguments)]==1);
    const auto control=counters;
    counters={};timer.mode=1;clock={{100,200,203,300}};
    {
        OpenCLCpuAttribution::Scope parent(timer,OpenCLCpuCategory::Other);
        OpenCLCpuAttribution::Scope child(timer,OpenCLCpuCategory::Arguments);
    }
    CHECK(counters.clockReads==control.clockReads);CHECK(counters.calls==control.calls);
    CHECK(counters.coveredNs==200);CHECK(counters.controlBracketNs==0);
    const auto before=counters;timer.mode=0;
    {OpenCLCpuAttribution::Scope off(timer,OpenCLCpuCategory::KernelEnqueue);}
    CHECK(counters.clockReads==before.clockReads);CHECK(counters.calls==before.calls);
    CHECK(clock.at==4);
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
    for(unsigned scenario=0;scenario<5;++scenario) {
    CAPTURE(scenario);
    unsigned calls = 0;
    std::uint16_t value = 1;
    BackendSession policy, otherPolicy;
    const auto cpu = [](void* context, std::uint16_t* out) {
        ++*static_cast<unsigned*>(context); *out = 123;
    };
    const BackendRequest request{&value, COST_LIMIT, {1, 1}, policy, &calls,
        [](void*, std::size_t) { return LAND_STEPS; }, cpu, {}};
    std::vector<BackendRequest> requests{request};
    if(scenario==0) { for (unsigned i = 1; i < 9; ++i) requests.push_back(request); }
    else if(scenario==1) {
        requests.push_back(request); requests.back().family = Family::Materials;
    }
    else if(scenario==2) {
        requests.push_back(request); requests.back().operation = Operation::ResumableSearch;
    }
    else if(scenario==3) {
        requests.push_back(BackendRequest{&value, COST_LIMIT, {1, 1}, otherPolicy,
            &calls, request.costAt, cpu, {}});
    }
    else {
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
TEST_CASE("isolated completed kernels remain observed when transactional output is declined")
{
    // Intentionally quarantines the backend. Run ONLY this case in a separate
    // process with GLOB2_TEST_OPENCL_EXECUTION_OBSERVATION_ROLLBACK=1.
    const auto* isolated=std::getenv("GLOB2_TEST_OPENCL_EXECUTION_OBSERVATION_ROLLBACK");
    if(!isolated || std::strcmp(isolated,"1"))return;
    using namespace gradient_kernel;
    REQUIRE(initializeOpenCL());REQUIRE((readyPlans.load()&(1u<<unsigned(Plan::Frozen8)))!=0);
    BackendSession session;const field::Grid grid(31,17);
    std::vector<std::uint16_t> seeds(grid.cells(),1);seeds[0]=65535;seeds[19]=65400;seeds[13]=0;
    const auto original=seeds;
    struct Context {BackendSession* session;unsigned calls=0;} context{&session};
    bool committed=true,observed=false;
    BackendRequest request{seeds.data(),700,grid,session,&context,
        [](void* p,std::size_t){auto& c=*static_cast<Context*>(p);if(!c.calls++)c.session->fail();return LAND_STEPS;},nullptr,{}};
    request.executedOnDevice=&committed;request.deviceExecutionObserved=&observed;
    const auto before=openCLStatus();CHECK_FALSE(executeOpenCLDevice(std::span(&request,1),Plan::Frozen8));
    const auto after=openCLStatus();CHECK(context.calls>0);CHECK(session.failed.load());CHECK(seeds==original);
    CHECK_FALSE(committed);CHECK(observed);CHECK(after.dispatches>before.dispatches);CHECK(after.hostChecks>before.hostChecks);
    CHECK(after.deviceObservedFields==before.deviceObservedFields+1);CHECK(after.committedFields==before.committedFields);
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
        bool committed=false,observed=false;
        BackendRequest request{actual.data(),COST_LIMIT,grid,session,&costs,
            [](void* p,std::size_t i){return (*static_cast<std::vector<EntrySteps>*>(p))[i];},
            [](void*,std::uint16_t*){FAIL("explicit kernel must not call a CPU reference");},{}};
        request.executedOnDevice=&committed;request.deviceExecutionObserved=&observed;
        REQUIRE(accelerator(request,Plan(plan))); CHECK(actual==expected);CHECK(committed);CHECK(observed);
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
    bool dispatched=true,observed=true;
    BackendRequest request{seeds.data(),60,{2,2},session,nullptr,
        [](void*,std::size_t){FAIL("trivial field needs no costs");return LAND_STEPS;},
        [](void*,std::uint16_t*){FAIL("trivial CPU field needs no propagation");},{}};
    request.executedOnDevice=&dispatched;
    request.deviceExecutionObserved=&observed;
    executeGradientGroup(std::span(&request,1),Backend::CPU);
    CHECK_FALSE(dispatched);
    CHECK_FALSE(observed);
    if(!initializeOpenCL() || !(readyPlans.load()&(1u<<unsigned(Plan::Frozen8)))) return;
    const auto before=openCLStatus();dispatched=observed=true;
    REQUIRE(executeOpenCLDevice(std::span(&request,1),Plan::Frozen8));
    CHECK_FALSE(dispatched);
    CHECK_FALSE(observed);
    const auto after=openCLStatus();
    CHECK(after.noopFields==before.noopFields+1);
    CHECK(after.dispatches==before.dispatches);
    CHECK(after.fields==before.fields);
    CHECK(after.deviceObservedFields==before.deviceObservedFields);
    CHECK(after.committedFields==before.committedFields);
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
TEST_CASE("optional device advances preserve seeds interleave required work and release storage")
{
    using namespace gradient_kernel;
    if(!initializeOpenCL() || !(readyPlans.load()&(1u<<unsigned(Plan::Frozen8)))) return;
    if(openCLStatus().activeEpoch || openCLStatus().parityBound || openCLStatus().directSeedUpload || openCLStatus().apiCpuMode) return; // Required-only experimental modes.
    std::thread coordinator([&] {
        for(auto dimensions:{std::pair{1,17},std::pair{17,1},std::pair{7,13}})
        for(int cap:{0,40,700}) {
            const field::Grid grid(dimensions.first,dimensions.second);
            auto seeds=std::make_shared<std::vector<std::uint16_t>>(grid.cells(),1);
            auto costs=std::make_shared<std::vector<EntrySteps>>(grid.cells(),EntrySteps{5,7});
            (*seeds)[0]=65535;(*seeds)[1]=0;(*seeds)[5]=65501;
            const auto original=*seeds;
            const auto expected=oracle(original,grid,*costs,cap);
            BackendSession session;
            BackendRequest request{seeds->data(),cap,grid,session,costs.get(),
                [](void* p,std::size_t i){return (*static_cast<std::vector<EntrySteps>*>(p))[i];},
                [](void*,std::uint16_t*){FAIL("probe cannot call CPU fallback");},
                {costs,92187654,1,true,costs->capacity()*sizeof(EntrySteps)}};
            // Required execution makes the immutable cost plane resident.
            auto warm=original;request.gradient=warm.data();
            REQUIRE(executeOpenCLDevice(std::span(&request,1),Plan::Frozen8));CHECK(warm==expected);
            request.gradient=seeds->data();
            const auto sourceBytes=sizeof(request)+sizeof(session)+sizeof(*seeds)+seeds->capacity()*sizeof(std::uint16_t);
            REQUIRE(reserveOpenCLProbeBytes(sourceBytes));
            struct SourceLease {std::size_t bytes;~SourceLease(){releaseOpenCLProbeBytes(bytes);}} sourceLease{sourceBytes};
            const auto reserved=openCLProbeBytes();
            auto probe=beginOpenCLProbe(request,Plan::Frozen8,seeds);REQUIRE(probe);
            auto progress=OpenCLProbeProgress::Pending;
            bool interleaved=false;
            for(unsigned attempt=0;attempt<100000 && progress==OpenCLProbeProgress::Pending;++attempt) {
                const auto before=probe->metrics().dispatches;
                progress=probe->advance(3);
                CHECK(probe->metrics().dispatches<=before+1);
                CHECK(*seeds==original);
                if(!interleaved && probe->metrics().dispatches) {
                    auto required=original;auto other=request;other.gradient=required.data();
                    REQUIRE(executeOpenCLDevice(std::span(&other,1),Plan::Frozen8));CHECK(required==expected);
                    interleaved=true;
                }
                if(progress==OpenCLProbeProgress::Pending) std::this_thread::yield();
            }
            REQUIRE(progress==OpenCLProbeProgress::Complete);
            CHECK(std::vector<std::uint16_t>(probe->result().begin(),probe->result().end())==expected);
            CHECK(interleaved);
            probe.reset();CHECK(openCLProbeBytes()==reserved);
            // Cancel while a device dispatch is outstanding. Reap without
            // touching the source plane or producing a completed result.
            auto canceled=beginOpenCLProbe(request,Plan::Frozen8,seeds);REQUIRE(canceled);
            for(unsigned attempt=0;attempt<100000 && !canceled->metrics().dispatches;++attempt) {
                REQUIRE(canceled->advance(3)==OpenCLProbeProgress::Pending);
            }
            REQUIRE(canceled->metrics().dispatches==1);canceled->cancel();
            progress=OpenCLProbeProgress::Pending;
            for(unsigned attempt=0;attempt<100000 && progress==OpenCLProbeProgress::Pending;++attempt) {
                progress=canceled->advance();if(progress==OpenCLProbeProgress::Pending) std::this_thread::yield();
            }
            REQUIRE(progress==OpenCLProbeProgress::Declined);CHECK(canceled->result().empty());
            CHECK(*seeds==original);CHECK_FALSE(session.failed.load());
            canceled.reset();CHECK(openCLProbeBytes()==reserved);
        }
    });
    coordinator.join();
}
TEST_CASE("optional probe clocks reject invalid setup before driver initialization")
{
#if !defined(SDL_PLATFORM_EMSCRIPTEN) && !defined(SDL_PLATFORM_ANDROID) && !defined(SDL_PLATFORM_IOS)
    using namespace gradient_kernel;
    BackendSession session;std::uint16_t original=77;
    auto clock=std::make_shared<ProbeClock>();
    BackendRequest request{&original,100,{1,1},session,nullptr,[](void*,std::size_t){return LAND_STEPS;},nullptr,{clock,381721,1,true}};
    const auto reserved=openCLProbeBytes();
    const auto invalid=openCLStatus().threadCPUInvalidMeasurements;
    clock->failRead=0;
    CHECK_FALSE(beginOpenCLProbe(request,Plan::Frozen8,clock,{clock,ProbeClock::read}));
    CHECK(clock->reads==1);CHECK(openCLProbeBytes()==reserved);CHECK(original==77);
    CHECK_FALSE(beginOpenCLProbe(request,Plan::Frozen8,clock,{{},ProbeClock::read}));
    CHECK(clock->reads==1);CHECK(openCLProbeBytes()==reserved);
    // An invalid setup endpoint must also be diagnosed when validation declines
    // before plan readiness. No initialization/driver work is needed here.
    clock->failRead=clock->reads+1;clock->invalidValue=1;
    CHECK_FALSE(beginOpenCLProbe(request,Plan::CPU,clock,{clock,ProbeClock::read}));
    CHECK(openCLStatus().threadCPUInvalidMeasurements==invalid+3);
    CHECK(openCLProbeBytes()==reserved);CHECK_FALSE(session.failed.load());
#endif
}
TEST_CASE("owned optional probe clocks cancel invalid copy and pending transfer measurements")
{
    using namespace gradient_kernel;
    const auto* enabled=std::getenv("GLOB2_TEST_OPENCL_PROBE_CLOCK");
    if(!enabled || std::strcmp(enabled,"1"))return;
    REQUIRE(initializeOpenCL());const auto status=openCLStatus();
    REQUIRE_FALSE(status.parityBound);REQUIRE_FALSE(status.directSeedUpload);REQUIRE(status.apiCpuMode==0);
    REQUIRE((readyPlans.load()&(1u<<unsigned(Plan::Frozen8)))!=0);
    struct Capture {
        std::vector<std::uint16_t> seeds=std::vector<std::uint16_t>(257,1);
        std::shared_ptr<std::vector<EntrySteps>> costs=std::make_shared<std::vector<EntrySteps>>(257,LAND_STEPS);
        BackendSession session;
        std::shared_ptr<ProbeClock> clock=std::make_shared<ProbeClock>();
        std::size_t reservation=0;
        ~Capture(){if(reservation)releaseOpenCLProbeBytes(reservation);}
    };
    std::thread coordinator([] {
        const auto baseline=openCLProbeBytes();
        auto owner=std::make_shared<Capture>();owner->seeds[0]=65535;owner->seeds.back()=65000;
        const auto original=owner->seeds;
        const auto bytes=sizeof(Capture)+owner->seeds.capacity()*sizeof(std::uint16_t)+sizeof(ProbeClock)+128;
        REQUIRE(reserveOpenCLProbeBytes(bytes));owner->reservation=bytes;
        BackendRequest request{owner->seeds.data(),700,{1,257},owner->session,owner->costs.get(),
            [](void* value,std::size_t i){return (*static_cast<std::vector<EntrySteps>*>(value))[i];},nullptr,
            {owner->costs,381722,1,true,sizeof(std::vector<EntrySteps>)+owner->costs->capacity()*sizeof(EntrySteps)}};
        auto warm=original;request.gradient=warm.data();
        REQUIRE(executeOpenCLDevice(std::span(&request,1),Plan::Frozen8));
        request.gradient=owner->seeds.data();const auto reserved=openCLProbeBytes();
        // Valid start, bad setup end: the newly allocated private lane is freed
        // before admission returns, and the healthy device/session stay usable.
        owner->clock->failRead=owner->clock->reads+1;owner->clock->invalidValue=1;
        CHECK_FALSE(beginOpenCLProbe(request,Plan::Frozen8,owner,{owner->clock,ProbeClock::read}));
        CHECK(openCLProbeBytes()==reserved);CHECK(owner->seeds==original);
        for(unsigned scenario=0;scenario<7;++scenario){
            owner->clock->failRead=~0u;
            auto probe=beginOpenCLProbe(request,Plan::Frozen8,owner,{owner->clock,ProbeClock::read});REQUIRE(probe);
            const auto target=scenario==4 ? OpenCLProbe::Stage::Dispatch : OpenCLProbe::Stage::Copy;
            for(unsigned attempt=0;attempt<100000 && probe->metrics().stage!=target;++attempt)
                REQUIRE(probe->advance()==OpenCLProbeProgress::Pending);
            REQUIRE(probe->metrics().stage==target);
            const auto reads=owner->clock->reads;
            // Cases: bad start; zero/reversed first 64-cell checkpoint; bad
            // copy endpoint; bad endpoint after enqueue with pending event;
            // between-advance reversal; checkpoint reversal still >=start.
            const auto offset=(scenario==0 || scenario==5) ? 0u : (scenario==3 || scenario==6) ? 2u : 1u;
            owner->clock->failRead=reads+offset;
            owner->clock->invalidValue=scenario==2 ? 1 : scenario==5 ? owner->clock->next-1 :
                scenario==6 ? owner->clock->next+1 : 0;
            auto progress=probe->advance(scenario==3 ? 64 : 4096);
            if(scenario==4){CHECK(progress==OpenCLProbeProgress::Pending);CHECK(probe->metrics().dispatches==1);}
            else {CHECK(progress==OpenCLProbeProgress::Declined);CHECK(probe->metrics().dispatches==0);}
            if(scenario==1 || scenario==2) CHECK(owner->clock->reads==reads+3);
            if(scenario==6) CHECK(owner->clock->reads==reads+4);
            REQUIRE(probe->metrics().invalidCpuClocks>0);
            for(unsigned attempt=0;attempt<1000000 && progress==OpenCLProbeProgress::Pending;++attempt){
                progress=probe->advance();if(progress==OpenCLProbeProgress::Pending)std::this_thread::yield();
            }
            CHECK(progress==OpenCLProbeProgress::Declined);CHECK(probe->result().empty());
            CHECK(owner->seeds==original);CHECK_FALSE(owner->session.failed.load());
            probe.reset();CHECK(openCLProbeBytes()==reserved);
        }
        // A recovered clock supplies another exact optional result; invalid
        // measurements do not trigger CPU recovery or quarantine the device.
        owner->clock->failRead=~0u;
        auto recovered=beginOpenCLProbe(request,Plan::Frozen8,owner,{owner->clock,ProbeClock::read});REQUIRE(recovered);
        auto progress=OpenCLProbeProgress::Pending;
        for(unsigned attempt=0;attempt<1000000 && progress==OpenCLProbeProgress::Pending;++attempt){
            progress=recovered->advance();if(progress==OpenCLProbeProgress::Pending)std::this_thread::yield();
        }
        CHECK(progress==OpenCLProbeProgress::Complete);
        CHECK(std::vector<std::uint16_t>(recovered->result().begin(),recovered->result().end())==warm);
        CHECK(recovered->metrics().invalidCpuClocks==0);recovered.reset();owner.reset();
        CHECK(openCLProbeBytes()==baseline);
    });coordinator.join();
}
TEST_CASE("isolated cold optional costs are exact private bounded and cancelable")
{
    using namespace gradient_kernel;
    const auto* enabled=std::getenv("GLOB2_TEST_OPENCL_COLD_COSTS");
    if(!enabled || std::strcmp(enabled,"1"))return;
    REQUIRE(initializeOpenCL());const auto configured=openCLStatus();
    REQUIRE(configured.probeColdCosts);REQUIRE_FALSE(configured.parityBound);
    REQUIRE_FALSE(configured.directSeedUpload);REQUIRE(configured.apiCpuMode==0);
    struct Costs {
        std::vector<EntrySteps> values;
        std::size_t calls=0;
        static EntrySteps at(void* value,std::size_t i) {
            auto& cost=*static_cast<Costs*>(value);++cost.calls;return cost.values[i];
        }
    };
    struct Capture {
        std::vector<std::uint16_t> seeds;
        std::shared_ptr<Costs> costs=std::make_shared<Costs>();
        BackendSession session;
        std::shared_ptr<ProbeClock> clock=std::make_shared<ProbeClock>();
        std::size_t reservation=0;
        ~Capture(){if(reservation)releaseOpenCLProbeBytes(reservation);}
    };
    std::thread coordinator([&] {
        const auto baseline=openCLProbeBytes();
        const auto makeCapture=[](field::Grid grid,unsigned variation) {
            auto owner=std::make_shared<Capture>();owner->seeds.assign(grid.cells(),1);
            owner->costs->values.assign(grid.cells(),LAND_STEPS);
            for(std::size_t i=0;i<grid.cells();++i){
                if(i%13==3)owner->seeds[i]=0;
                if(i%3==variation%3)owner->costs->values[i]={19+variation,29+variation};
            }
            owner->seeds[variation%grid.cells()]=65535;owner->seeds.back()=65001;
            if(variation==2)owner->seeds[0]=1; // Removed/moved full-strength goal.
            const auto bytes=sizeof(Capture)+owner->seeds.capacity()*sizeof(std::uint16_t)+sizeof(Costs)+
                owner->costs->values.capacity()*sizeof(EntrySteps)+sizeof(ProbeClock)+256;
            REQUIRE(reserveOpenCLProbeBytes(bytes));owner->reservation=bytes;return owner;
        };
        const auto makeRequest=[](const std::shared_ptr<Capture>& owner,field::Grid grid,int cap,unsigned revision) {
            return BackendRequest{owner->seeds.data(),cap,grid,owner->session,owner->costs.get(),Costs::at,nullptr,
                {owner->costs,492771,revision,true,sizeof(Costs)+owner->costs->values.capacity()*sizeof(EntrySteps)}};
        };
        const auto drain=[](OpenCLProbe& probe,OpenCLProbeProgress progress) {
            for(unsigned attempt=0;attempt<1000000 && progress==OpenCLProbeProgress::Pending;++attempt){
                progress=probe.advance(64);if(progress==OpenCLProbeProgress::Pending)std::this_thread::yield();
            }
            return progress;
        };
        unsigned completed=0,interleaved=0;
        for(unsigned plan=1;plan<PLANS.size();++plan){
            REQUIRE((readyPlans.load()&(1u<<plan))!=0);
            for(auto shape:{std::pair{1,257},std::pair{257,1},std::pair{17,19}})
            for(int cap:{0,700})for(unsigned revision=1;revision<=3;++revision){
                const field::Grid grid(shape.first,shape.second);auto owner=makeCapture(grid,revision-1);
                auto request=makeRequest(owner,grid,cap,revision);const auto original=owner->seeds;
                const auto expected=oracle(original,grid,owner->costs->values,cap);
                const auto reserved=openCLProbeBytes();const auto uploads=openCLStatus().costUploads;
                auto probe=beginOpenCLProbe(request,Plan(plan),owner,{owner->clock,ProbeClock::read});REQUIRE(probe);
                bool required=false;auto progress=OpenCLProbeProgress::Pending;
                for(unsigned attempt=0;attempt<1000000 && progress==OpenCLProbeProgress::Pending;++attempt){
                    const auto before=probe->metrics();progress=probe->advance(64);
                    const auto after=probe->metrics();CHECK(after.costCellsPrepared<=before.costCellsPrepared+64);
                    CHECK(after.dispatches<=before.dispatches+1);CHECK(owner->seeds==original);
                    if(!required && plan==unsigned(Plan::Frozen8) && cap==700 && revision==1 &&
                        after.stage==OpenCLProbe::Stage::CostsPack && after.costCellsPrepared==64){
                        // Same cold identity on required lane: it completes its
                        // own preparation while optional packing is unfinished.
                        auto actual=original;auto requiredRequest=request;requiredRequest.gradient=actual.data();
                        REQUIRE(executeOpenCLDevice(std::span(&requiredRequest,1),Plan::Frozen8));CHECK(actual==expected);
                        required=true;++interleaved;
                    }
                    if(progress==OpenCLProbeProgress::Pending)std::this_thread::yield();
                }
                REQUIRE(progress==OpenCLProbeProgress::Complete);const auto metrics=probe->metrics();
                CHECK(metrics.coldCostPlane);CHECK(metrics.costCellsPrepared==grid.cells());
                CHECK(metrics.costPreparedBytes>=grid.cells()*sizeof(std::uint32_t));CHECK(metrics.costUploads==1);
                CHECK(metrics.invalidCpuClocks==0);CHECK(metrics.dispatches>0);
                CHECK(owner->costs->calls==grid.cells()*(required ? 2 : 1));
                CHECK(openCLStatus().costUploads==uploads+(required ? 2 : 1));
                CHECK(std::vector<std::uint16_t>(probe->result().begin(),probe->result().end())==expected);
                CHECK(owner->seeds==original);probe.reset();CHECK(openCLProbeBytes()==reserved);++completed;
            }
        }
        CHECK(completed==(PLANS.size()-1)*18);CHECK(interleaved==3);
        if(configured.uniformMetadata){
            const field::Grid grid(17,19);auto owner=makeCapture(grid,0);
            owner->costs->values.assign(grid.cells(),LAND_STEPS);
            auto request=makeRequest(owner,grid,700,90);
            request.identity.packedUniformCost=LAND_STEPS.cardinal|(LAND_STEPS.diagonal<<16);
            const auto original=owner->seeds;const auto expected=oracle(original,grid,owner->costs->values,700);
            auto scalar=beginOpenCLProbe(request,Plan::Frozen8,owner,{owner->clock,ProbeClock::read});REQUIRE(scalar);
            CHECK(drain(*scalar,OpenCLProbeProgress::Pending)==OpenCLProbeProgress::Complete);
            CHECK(scalar->metrics().costCellsPrepared==0);CHECK(scalar->metrics().costPreparedBytes==sizeof(std::uint32_t));
            CHECK(scalar->metrics().costUploads==1);CHECK(owner->costs->calls==0);
            CHECK(std::vector<std::uint16_t>(scalar->result().begin(),scalar->result().end())==expected);
            CHECK(owner->seeds==original);
        }
        // Independent cold identity per case. No required warm-up can mask a
        // canceled pack/upload, bad owner metadata, callback or budget decline.
        for(unsigned scenario=0;scenario<10;++scenario){
            const field::Grid grid(17,19);auto owner=makeCapture(grid,scenario);
            auto request=makeRequest(owner,grid,700,100+scenario);const auto original=owner->seeds;
            const auto reserved=openCLProbeBytes();
            if(scenario==0){request.identity.allCells=false;CHECK_FALSE(beginOpenCLProbe(request,Plan::Frozen8,owner));}
            else {
                if(scenario==1)request.identity.retainedBytes=0;
                if(scenario==8)owner->costs->values[10]={0,1};
                auto probe=beginOpenCLProbe(request,Plan::Frozen8,owner,{owner->clock,ProbeClock::read});REQUIRE(probe);
                if((scenario>=2 && scenario<=4) || scenario==9){
                    const auto target=scenario==3 ? OpenCLProbe::Stage::CostsWait : OpenCLProbe::Stage::CostsPack;
                    for(unsigned attempt=0;attempt<100000 && probe->metrics().stage!=target;++attempt)
                        REQUIRE(probe->advance(64)==OpenCLProbeProgress::Pending);
                    REQUIRE(probe->metrics().stage==target);
                    if(scenario==4)owner->session.configure(2,false);
                    else if(scenario==9){owner->clock->failRead=owner->clock->reads+1;owner->clock->invalidValue=0;}
                    else probe->cancel();
                    CHECK(drain(*probe,OpenCLProbeProgress::Pending)==OpenCLProbeProgress::Declined);
                    if(scenario==9)CHECK(probe->metrics().invalidCpuClocks>0);
                } else if(scenario>=5 && scenario<=7){
                    REQUIRE(probe->advance(64)==OpenCLProbeProgress::Pending); // CostsReserve before any cost allocation.
                    if(scenario==5){
                        const auto available=OpenCLProbeBudget-openCLProbeBytes();REQUIRE(reserveOpenCLProbeBytes(available));
                        CHECK(probe->advance(64)==OpenCLProbeProgress::Declined);releaseOpenCLProbeBytes(available);
                    } else if(scenario==6){
                        const auto available=OpenCLHostBudget-openCLStatus().hostBytes;REQUIRE(reserveOpenCLHostBytes(available));
                        CHECK(probe->advance(64)==OpenCLProbeProgress::Declined);releaseOpenCLHostBytes(available);
                    } else {
                        const auto before=openCLStatus().deviceBytes;
                        auto pressure=reserveOpenCLDeviceBytesForDiagnostics(OpenCLDeviceBudget-before);REQUIRE(pressure);
                        CHECK(drain(*probe,OpenCLProbeProgress::Pending)==OpenCLProbeProgress::Declined);
                        pressure.reset();CHECK(openCLStatus().deviceBytes==before);
                    }
                } else CHECK(drain(*probe,OpenCLProbeProgress::Pending)==OpenCLProbeProgress::Declined);
                CHECK(probe->result().empty());probe.reset();
            }
            CHECK(owner->seeds==original);CHECK_FALSE(owner->session.failed.load());CHECK(openCLProbeBytes()==reserved);
        }
        CHECK(openCLProbeBytes()==baseline);
    });coordinator.join();
}
TEST_CASE("isolated epoch probes mirror required windows and preserve exact owned cancellation")
{
    using namespace gradient_kernel;
    const auto* activated=std::getenv("GLOB2_TEST_OPENCL_EPOCH_PROBE");
    if(!activated || std::strcmp(activated,"1"))return;
    REQUIRE(initializeOpenCL());const auto configured=openCLStatus();
    REQUIRE(configured.activeEpoch);REQUIRE_FALSE(configured.parityBound);
    REQUIRE_FALSE(configured.directSeedUpload);REQUIRE(configured.apiCpuMode==0);
    struct Capture {
        std::shared_ptr<std::vector<std::uint16_t>> seeds;
        std::shared_ptr<std::vector<EntrySteps>> costs;
        std::shared_ptr<BackendSession> session=std::make_shared<BackendSession>();
        std::size_t reserved=0;
        ~Capture(){if(reserved)releaseOpenCLProbeBytes(reserved);}
    };
    std::thread coordinator([&] {
        const auto baseline=openCLProbeBytes();unsigned completed=0,requiredInterleaves=0,canceled=0;
        for(unsigned plan=1;plan<PLANS.size();++plan){
            REQUIRE((readyPlans.load()&(1u<<plan))!=0);
            for(auto dimensions:{std::pair{1,1},std::pair{1,257},std::pair{257,1},std::pair{7,13},std::pair{31,33}})
            for(int cap:{0,700}){
                const field::Grid grid(dimensions.first,dimensions.second);
                auto holder=std::make_shared<Capture>();
                holder->seeds=std::make_shared<std::vector<std::uint16_t>>(grid.cells(),1);
                holder->costs=std::make_shared<std::vector<EntrySteps>>(grid.cells(),LAND_STEPS);
                for(std::size_t i=0;i<grid.cells();++i)if(i%5==0)(*holder->costs)[i]={19,29};
                holder->seeds->front()=65535;holder->seeds->back()=65001;
                if(grid.cells()>2)(*holder->seeds)[grid.cells()/2]=0;
                const auto original=*holder->seeds;
                const auto expected=oracle(original,grid,*holder->costs,cap);
                const auto sourceBytes=sizeof(Capture)+sizeof(BackendSession)+sizeof(*holder->seeds)+
                    holder->seeds->capacity()*sizeof(std::uint16_t)+sizeof(*holder->costs)+
                    holder->costs->capacity()*sizeof(EntrySteps)+128;
                REQUIRE(reserveOpenCLProbeBytes(sourceBytes));holder->reserved=sourceBytes;
                BackendRequest request{holder->seeds->data(),cap,grid,*holder->session,holder->costs.get(),
                    [](void* value,std::size_t cell){return (*static_cast<std::vector<EntrySteps>*>(value))[cell];},
                    nullptr,{holder->costs,919123,1,true,sizeof(*holder->costs)+holder->costs->capacity()*sizeof(EntrySteps)}};
                auto required=original;auto warm=request;warm.gradient=required.data();
                const auto before=openCLStatus();REQUIRE(executeOpenCLDevice(std::span(&warm,1),Plan(plan)));
                const auto after=openCLStatus();CHECK(required==expected);
                const auto requiredDispatches=after.dispatches-before.dispatches;
                const auto reserved=openCLProbeBytes();auto probe=beginOpenCLProbe(request,Plan(plan),holder);REQUIRE(probe);
                auto progress=OpenCLProbeProgress::Pending;bool interleaved=false;
                for(unsigned attempt=0;attempt<1000000 && progress==OpenCLProbeProgress::Pending;++attempt){
                    const auto dispatched=probe->metrics().dispatches;progress=probe->advance(17);
                    CHECK(probe->metrics().dispatches<=dispatched+1);CHECK(*holder->seeds==original);
                    if(!interleaved && probe->metrics().dispatches){
                        // Required lane changes both plan and retained buffer
                        // size while the probe's private dispatch is outstanding.
                        for(auto shape:{field::Grid{33,31},field::Grid{1,7}}){
                            auto costs=std::make_shared<std::vector<EntrySteps>>(shape.cells(),EntrySteps{7,11});
                            std::vector<std::uint16_t> input(shape.cells(),1);input.front()=65535;input.back()=65401;
                            const auto output=oracle(input,shape,*costs,700);
                            BackendSession session;BackendRequest other{input.data(),700,shape,session,costs.get(),
                                [](void* value,std::size_t cell){return (*static_cast<std::vector<EntrySteps>*>(value))[cell];},
                                nullptr,{costs,719123,1,true,sizeof(*costs)+costs->capacity()*sizeof(EntrySteps)}};
                            REQUIRE(executeOpenCLDevice(std::span(&other,1),Plan::Frozen8));CHECK(input==output);++requiredInterleaves;
                        }
                        interleaved=true;
                    }
                    if(progress==OpenCLProbeProgress::Pending)std::this_thread::yield();
                }
                REQUIRE(progress==OpenCLProbeProgress::Complete);CHECK(interleaved);
                CHECK(std::vector<std::uint16_t>(probe->result().begin(),probe->result().end())==expected);
                const auto metrics=probe->metrics();CHECK(metrics.activeEpoch);
                CHECK(metrics.checkInterval==configured.checkInterval);CHECK(metrics.dispatches==requiredDispatches);
                CHECK(metrics.dispatches>0);CHECK(metrics.hostChecks==(metrics.dispatches+configured.checkInterval-1)/configured.checkInterval);
                CHECK(metrics.tileMaskInitializations==2);CHECK(metrics.tileMaskClears==0);
                CHECK(metrics.kernelArgumentUpdates==12+5*metrics.dispatches);
                probe.reset();CHECK(openCLProbeBytes()==reserved);++completed;
                if(plan==unsigned(Plan::Frozen8) && grid.width()==7 && grid.height()==13 && cap==700){
                    using Stage=OpenCLProbe::Stage;
                    for(const auto [stage,generationCancel]:std::array{
                        std::pair{Stage::InitialWait,false},std::pair{Stage::KernelWait,false},
                        std::pair{Stage::CheckWait,false},std::pair{Stage::OutputWait,false},std::pair{Stage::KernelWait,true}}){
                        auto pending=beginOpenCLProbe(request,Plan(plan),holder);REQUIRE(pending);
                        progress=OpenCLProbeProgress::Pending;bool reached=false;
                        for(unsigned attempt=0;attempt<1000000 && progress==OpenCLProbeProgress::Pending;++attempt){
                            progress=pending->advance(17);
                            if(pending->metrics().stage==stage){reached=true;break;}
                            if(progress==OpenCLProbeProgress::Pending)std::this_thread::yield();
                        }
                        REQUIRE(reached);REQUIRE(progress==OpenCLProbeProgress::Pending);
                        if(generationCancel)holder->session->configure(2,false);else pending->cancel();
                        for(unsigned attempt=0;attempt<1000000 && progress==OpenCLProbeProgress::Pending;++attempt){
                            progress=pending->advance();if(progress==OpenCLProbeProgress::Pending)std::this_thread::yield();
                        }
                        REQUIRE(progress==OpenCLProbeProgress::Declined);CHECK(pending->result().empty());
                        CHECK(*holder->seeds==original);CHECK_FALSE(holder->session->failed.load());
                        pending.reset();CHECK(openCLProbeBytes()==reserved);++canceled;
                    }
                }
            }
        }
        CHECK(completed==(PLANS.size()-1)*10);CHECK(requiredInterleaves==completed*2);CHECK(canceled==5);
        CHECK(openCLProbeBytes()==baseline);
    });
    coordinator.join();
}
TEST_CASE("experimental mask and parity bindings preserve every plan across retirement and buffer reuse")
{
    using namespace gradient_kernel;
    if(!initializeOpenCL() || (!openCLStatus().activeEpoch && !openCLStatus().parityBound)) return;
    struct Context {
        field::Grid grid;
        std::shared_ptr<std::vector<EntrySteps>> costs;
        std::vector<std::uint16_t> original,actual,expected;
        int cap;
    };
    // A singleton and narrow fields retire before the larger irregular field.
    // Fresh seed patterns and a smaller middle batch reuse the same allocated
    // buffers/cost planes before restoring the larger irregular batch.
    BackendSession session;
    for(unsigned plan=1;plan<unsigned(Plan::Count);++plan) {
        if(!(readyPlans.load()&(1u<<plan))) continue;
        std::vector<Context> fields;
        for(auto dimensions:{std::pair{1,1},std::pair{1,83},std::pair{83,1},
                             std::pair{17,31},std::pair{257,131},std::pair{65,19}}) {
            const field::Grid grid(dimensions.first,dimensions.second);
            auto costs=std::make_shared<std::vector<EntrySteps>>(grid.cells());
            for(std::size_t cell=0;cell<grid.cells();++cell)
                (*costs)[cell]=cell%17 ? EntrySteps{5,7} : EntrySteps{13,18};
            fields.push_back({grid,costs,{},{},{},grid.cells()<100 ? 40 : COST_LIMIT});
        }
        for(unsigned repeat=0;repeat<3;++repeat) {
            CAPTURE(plan);CAPTURE(repeat);
            std::vector<BackendRequest> requests;
            for(auto& field:fields) {
                field.original.assign(field.grid.cells(),1);
                for(std::size_t cell=0;cell<field.grid.cells();++cell)
                    if((cell+repeat)%19==0) field.original[cell]=0;
                field.original[repeat%field.grid.cells()]=65535;
                if(field.grid.cells()==1) field.original[0]=65501;
                else field.original[field.grid.cells()/2]=65000;
                field.expected=oracle(field.original,field.grid,*field.costs,field.cap);
                field.actual=field.original;
                requests.push_back({field.actual.data(),field.cap,field.grid,session,field.costs.get(),
                    [](void* p,std::size_t cell){return (*static_cast<std::vector<EntrySteps>*>(p))[cell];},
                    [](void*,std::uint16_t*){FAIL("epoch mask test must execute the exact device plan");},
                    {field.costs,918201,1,true,field.costs->capacity()*sizeof(EntrySteps)}});
            }
            const auto before=openCLStatus();
            const auto count=repeat==1 ? std::size_t(3) : fields.size();
            REQUIRE(executeOpenCLDevice(std::span(requests).first(count),Plan(plan)));
            for(std::size_t i=0;i<fields.size();++i)
                CHECK(fields[i].actual==(i<count ? fields[i].expected : fields[i].original));
            const auto after=openCLStatus();
            const auto dispatches=after.dispatches-before.dispatches;
            CHECK(after.activeEpoch==before.activeEpoch);CHECK(after.parityBound==before.parityBound);
            CHECK(after.retiredFields==before.retiredFields+count);
            CHECK(after.tileMaskInitializations==before.tileMaskInitializations+(after.activeEpoch ? 2 : 1));
            CHECK(after.tileMaskClears==before.tileMaskClears+(after.activeEpoch ? 0 : dispatches));
            const auto arguments=after.parityBound ? 32 : 12+4*dispatches;
            CHECK(after.kernelArgumentUpdates==before.kernelArgumentUpdates+arguments+(after.activeEpoch ? dispatches : 0));
            CHECK(after.dispatches>before.dispatches);CHECK_FALSE(session.failed.load());
        }
    }
    // The epoch counterpart has a separate fresh-process lifecycle gate above.
    // Remaining required-only configurations must still decline optional work.
    const auto configuration=openCLStatus();
    if(configuration.parityBound || configuration.directSeedUpload || configuration.apiCpuMode){
        auto seeds=std::make_shared<std::vector<std::uint16_t>>(2,1);(*seeds)[0]=65535;
        const auto original=*seeds;
        const auto bytes=openCLProbeBytes();
        BackendRequest request{seeds->data(),100,{2,1},session,nullptr,
            [](void*,std::size_t){return LAND_STEPS;},nullptr,{seeds,98232,1,true}};
        CHECK_FALSE(beginOpenCLProbe(request,Plan::Frozen8,seeds));
        CHECK(*seeds==original);CHECK(openCLProbeBytes()==bytes);CHECK_FALSE(session.failed.load());
    }
}
TEST_CASE("singleton direct seed uploads preserve exact arrays commands and mixed batch staging")
{
    using namespace gradient_kernel;
    if(!initializeOpenCL() || !openCLStatus().directSeedUpload)return;
    REQUIRE(openCLStatus().directSeedUploadRequested);
    BackendSession session;
    struct Field {
        field::Grid grid;
        std::shared_ptr<std::vector<EntrySteps>> costs;
        std::vector<std::uint16_t> original,actual,expected;
        int cap;
    };
    for(unsigned plan=1;plan<unsigned(Plan::Count);++plan) {
        if(!(readyPlans.load()&(1u<<plan)))continue;
        std::vector<Field> fields;
        // Growth, narrow toroidal grids and subsequent smaller reuse all read
        // output into the separate reusable staging buffer, never original seeds.
        for(auto dimensions:{std::pair{1,1},std::pair{1,37},std::pair{37,1},
                             std::pair{31,17},std::pair{257,131},std::pair{7,13}}) {
            const field::Grid grid(dimensions.first,dimensions.second);
            auto costs=std::make_shared<std::vector<EntrySteps>>(grid.cells());
            for(std::size_t i=0;i<grid.cells();++i)(*costs)[i]=i%11 ? EntrySteps{5,7} : EntrySteps{13,18};
            fields.push_back({grid,costs,{},{},{},grid.cells()==1 ? 0 : 700});
        }
        for(unsigned repeat=0;repeat<2;++repeat)for(auto& field:fields) {
            field.original.assign(field.grid.cells(),1);
            for(std::size_t i=0;i<field.original.size();++i)if((i+repeat)%19==0)field.original[i]=0;
            field.original[repeat%field.original.size()]=field.original.size()==1 ? 65501 : 65535;
            if(field.original.size()>1)field.original[field.original.size()/2]=65401;
            field.actual=field.original;field.expected=oracle(field.original,field.grid,*field.costs,field.cap);
            bool executed=false,observed=false;
            BackendRequest request{field.actual.data(),field.cap,field.grid,session,field.costs.get(),
                [](void* p,std::size_t i){return (*static_cast<std::vector<EntrySteps>*>(p))[i];},nullptr,
                {field.costs,8199271,1,true,field.costs->capacity()*sizeof(EntrySteps)}};
            request.executedOnDevice=&executed;request.deviceExecutionObserved=&observed;
            const auto before=openCLStatus();REQUIRE(executeOpenCLDevice(std::span(&request,1),Plan(plan)));
            CHECK(field.actual==field.expected);CHECK(executed);CHECK(observed);CHECK_FALSE(session.failed.load());
            const auto after=openCLStatus();const auto bytes=field.grid.cells()*sizeof(std::uint16_t);
            CHECK(after.directSeedUploads==before.directSeedUploads+1);
            CHECK(after.directSeedUploadedBytes==before.directSeedUploadedBytes+bytes);
            CHECK(after.seedUploadedBytes==before.seedUploadedBytes+bytes);
            CHECK(after.seedCopiedBytes==before.seedCopiedBytes);
            CHECK(after.outputCopiedBytes==before.outputCopiedBytes+bytes);
            CHECK(after.batches==before.batches+1);CHECK(after.fields==before.fields+1);
            const auto dispatches=after.dispatches-before.dispatches;
            REQUIRE(dispatches>0);CHECK(after.hostChecks>before.hostChecks);
            CHECK(after.tileMaskInitializations==before.tileMaskInitializations+(after.activeEpoch ? 2 : 1));
            CHECK(after.tileMaskClears==before.tileMaskClears+(after.activeEpoch ? 0 : dispatches));
            const auto arguments=after.parityBound ? 32 : 12+4*dispatches;
            CHECK(after.kernelArgumentUpdates==before.kernelArgumentUpdates+arguments+(after.activeEpoch ? dispatches : 0));
        }
        // Nine fields include a singleton tail chunk. It MUST still use the
        // mixed-batch staging path, preserving the whole-batch rollback contract.
        std::vector<BackendRequest> requests;
        std::size_t cells=0;
        while(fields.size()<9){auto copy=fields[fields.size()%5];fields.push_back(std::move(copy));}
        for(auto& field:fields) {
            field.actual=field.original;cells+=field.grid.cells();
            requests.push_back({field.actual.data(),field.cap,field.grid,session,field.costs.get(),
                [](void* p,std::size_t i){return (*static_cast<std::vector<EntrySteps>*>(p))[i];},nullptr,
                {field.costs,8199271,1,true,field.costs->capacity()*sizeof(EntrySteps)}});
        }
        const auto before=openCLStatus();REQUIRE(executeOpenCLDevice(requests,Plan(plan)));
        for(const auto& field:fields)CHECK(field.actual==field.expected);
        const auto after=openCLStatus();CHECK(after.directSeedUploads==before.directSeedUploads);
        CHECK(after.directSeedUploadedBytes==before.directSeedUploadedBytes);
        CHECK(after.seedCopiedBytes==before.seedCopiedBytes+cells*sizeof(std::uint16_t));
        CHECK(after.seedUploadedBytes==before.seedUploadedBytes+cells*sizeof(std::uint16_t));
        CHECK(after.outputCopiedBytes==before.outputCopiedBytes+cells*sizeof(std::uint16_t));
        CHECK(after.batches==before.batches+2);CHECK(after.fields==before.fields+9);
    }
    auto seeds=std::make_shared<std::vector<std::uint16_t>>(2,1);(*seeds)[0]=65535;
    const auto original=*seeds;const auto bytes=openCLProbeBytes();
    BackendRequest probe{seeds->data(),100,{2,1},session,nullptr,[](void*,std::size_t){return LAND_STEPS;},nullptr,{seeds,872392,1,true}};
    CHECK_FALSE(beginOpenCLProbe(probe,Plan::Frozen8,seeds));CHECK(*seeds==original);CHECK(openCLProbeBytes()==bytes);
}

TEST_CASE("opt-in API CPU scopes observe real required commands without changing results")
{
    using namespace gradient_kernel;
    initializeForTest();const auto initial=openCLStatus();
    if(!initial.available || !initial.apiCpuMode)return;
    REQUIRE(initial.apiCpuConfigured);
    const field::Grid grid(7,13);
    std::vector<std::uint16_t> seeds(grid.cells(),1);seeds[0]=65535;seeds[11]=0;seeds.back()=65000;
    std::vector<EntrySteps> costs(grid.cells(),LAND_STEPS);
    const auto expected=oracle(seeds,grid,costs,40);
    BackendSession session;auto owner=std::make_shared<unsigned>(1);
    BackendRequest request{seeds.data(),40,grid,session,&costs,
        [](void* context,std::size_t cell){return (*static_cast<std::vector<EntrySteps>*>(context))[cell];}};
    request.identity={owner,1,1,true};
    const auto before=openCLStatus();
    REQUIRE(executeOpenCLDevice(std::span(&request,1),requestedOpenCLPlan()));
    CHECK(seeds==expected);
    const auto after=openCLStatus();
    const auto calls=[&](OpenCLCpuCategory category){return after.apiCpu.calls[unsigned(category)]-before.apiCpu.calls[unsigned(category)];};
    CHECK(calls(OpenCLCpuCategory::KernelEnqueue)==after.dispatches-before.dispatches);
    CHECK(calls(OpenCLCpuCategory::Arguments)==after.kernelArgumentUpdates-before.kernelArgumentUpdates);
    CHECK(calls(OpenCLCpuCategory::CheckRead)==after.hostChecks-before.hostChecks);
    CHECK(calls(OpenCLCpuCategory::Fill)==after.tileMaskInitializations-before.tileMaskInitializations+
        after.tileMaskClears-before.tileMaskClears+after.hostChecks-before.hostChecks);
    CHECK(calls(OpenCLCpuCategory::OutputRead)==1);CHECK(calls(OpenCLCpuCategory::OutputCopy)==1);
    CHECK(calls(OpenCLCpuCategory::Upload)>=2);
    std::uint64_t scopes=0,categoryCpu=0;
    for(unsigned i=0;i<unsigned(OpenCLCpuCategory::Count);++i){
        scopes+=after.apiCpu.calls[i]-before.apiCpu.calls[i];categoryCpu+=after.apiCpu.ns[i]-before.apiCpu.ns[i];
    }
    CHECK(after.apiCpu.clockReads-before.apiCpu.clockReads==scopes*2);
    CHECK(after.apiCpu.invalidScopes==before.apiCpu.invalidScopes);
    CHECK(after.apiCpu.reconciliationErrors==before.apiCpu.reconciliationErrors);
    if(initial.apiCpuMode==1)CHECK(categoryCpu==after.apiCpu.coveredNs-before.apiCpu.coveredNs);
    else {CHECK(categoryCpu==0);CHECK(after.apiCpu.coveredNs==before.apiCpu.coveredNs);}
    CHECK_FALSE(beginOpenCLProbe(request,requestedOpenCLPlan(),owner)); // Unqualified diagnostic overhead.
}
TEST_CASE("isolated direct seed upload rollback preserves originals after device work")
{
    // This deliberately fails a session after initial admission and quarantines
    // the backend. Run ONLY this case in a separate native test process with
    // GLOB2_TEST_OPENCL_DIRECT_UPLOAD_ROLLBACK=1 and direct-upload mode enabled.
    const auto* isolated=std::getenv("GLOB2_TEST_OPENCL_DIRECT_UPLOAD_ROLLBACK");
    if(!isolated || std::strcmp(isolated,"1"))return;
    using namespace gradient_kernel;
    REQUIRE(initializeOpenCL());REQUIRE(openCLStatus().directSeedUpload);
    REQUIRE((readyPlans.load()&(1u<<unsigned(Plan::Frozen8)))!=0);
    BackendSession session;const field::Grid grid(31,17);
    std::vector<std::uint16_t> seeds(grid.cells(),1);seeds[0]=65535;seeds[19]=65400;seeds[13]=0;
    const auto original=seeds;
    struct Context {BackendSession* session;unsigned calls=0;} context{&session};
    bool executed=true,observed=false;
    BackendRequest request{seeds.data(),700,grid,session,&context,
        [](void* p,std::size_t){auto& c=*static_cast<Context*>(p);if(!c.calls++)c.session->fail();return LAND_STEPS;},nullptr,{}};
    request.executedOnDevice=&executed;request.deviceExecutionObserved=&observed;
    const auto before=openCLStatus();CHECK_FALSE(executeOpenCLDevice(std::span(&request,1),Plan::Frozen8));
    const auto after=openCLStatus();CHECK(context.calls>0);CHECK(session.failed.load());CHECK(seeds==original);CHECK_FALSE(executed);CHECK(observed);
    CHECK(after.deviceObservedFields==before.deviceObservedFields+1);CHECK(after.committedFields==before.committedFields);
    CHECK(after.directSeedUploads==before.directSeedUploads+1);
    CHECK(after.directSeedUploadedBytes==before.directSeedUploadedBytes+grid.cells()*sizeof(std::uint16_t));
    CHECK(after.seedUploadedBytes==before.seedUploadedBytes+grid.cells()*sizeof(std::uint16_t));
    CHECK(after.seedCopiedBytes==before.seedCopiedBytes);CHECK(after.outputCopiedBytes==before.outputCopiedBytes);
    CHECK(after.dispatches>before.dispatches);CHECK(after.hostChecks>before.hostChecks);
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
