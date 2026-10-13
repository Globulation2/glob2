// SPDX-License-Identifier: GPL-3.0-or-later
#include "Glob2Test.h"
#include "ScopedEnvironment.h"
#include "field/GradientGpuValidation.h"
#include <algorithm>
#include <memory>
#include <utility>
#include <chrono>
#include <thread>
#include <cstdint>
#include "map/gradient/GradientPipeline.h"
#include "map/gradient/BuildingGradientPipeline.h"
#include <deque>
#include <vector>
#include <array>
#include <stdexcept>
#include <future>
#include <Environment.h>
#include <string>
#include <filesystem>
#include <fstream>
#include "field/GradientPropagation.h"

namespace {
using PipelineEnvironment = glob2test::ScopedEnvironment;
}

// Standalone harnesses own an executor; production uses Map's shared executor.
class TestGradientPipeline : public GradientPipeline
{
    ComputeExecutor executor;
public:
    ~TestGradientPipeline() { reset(); }
    ComputeExecutor& compute() { return executor; }
    void configure(unsigned count, unsigned delay, size_t cells, Work work,
        const std::function<std::thread(std::function<void()>)>& factory =
            [](auto f) { return GAGCore::ThreadSupport::launch(std::move(f)); })
    {
        reset(); executor.configureTelemetry(ComputeTelemetryMode::Trace); executor.configure(count+1, factory);
        GradientPipeline::configure(executor, count!=0, delay, cells, std::move(work));
    }
};

TEST_SUITE("GradientPipeline")
{
TEST_CASE("fixed publication; supersession; bounded buffers; scheduling stress; fallback; exceptions and teardown")
{
	for (unsigned workers : {0, 1, 2, 4, 8}) for (unsigned delay : {1, 2, 3, 8}) {
		std::array<std::uint16_t *, 7> slots{};
		for (auto &slot : slots) slot = new std::uint16_t[16]{};
		TestGradientPipeline pipeline;
		pipeline.configure(workers, delay, 16, [](auto &job, auto &) {
			if (job.data[0] % 3 == 0) std::this_thread::sleep_for(std::chrono::microseconds(40));
			for (int i=1; i<16; ++i) job.data[i] = job.data[0] + (*job.water)[i];
		});
		std::array<unsigned, 7> expected{};
		std::array<bool, 1001> cancelled{};
		for (unsigned tick=1; tick<=1000; ++tick) {
			pipeline.advance();
			if (tick>delay && !cancelled[tick-delay]) expected[(tick-delay)%7] = tick-delay;
			for (unsigned s=0; s<slots.size(); ++s) REQUIRE(slots[s][0] == expected[s]);
			pipeline.submit(&slots[tick%7], 0, [tick](auto &job) {
				job.data[0] = tick; job.water = std::make_shared<const std::vector<std::uint8_t>>(16,2);
			});
			if (tick%5 == 0) {
				pipeline.invalidate(&slots[tick%7]);
				for(unsigned old = tick>delay ? tick-delay+1 : 1; old<=tick; ++old)
					if (old%7 == tick%7) cancelled[old] = true;
				slots[tick%7][0] = 60000; expected[tick%7] = 60000;
			}
		}
		pipeline.finish(); // Completing work must not publish early.
		for(unsigned s=0;s<slots.size();++s) REQUIRE(slots[s][0] == expected[s]);
		REQUIRE(pipeline.metrics.maxPending <= delay);
		pipeline.reset(); // Drain before slot destruction.
		for (auto *slot : slots) delete[] slot;
	}
	// Snapshot/restore at every phase, including cancelled jobs and execution changes.
	for (unsigned phase=0; phase<8; ++phase) for (bool cancelled : {false,true}) {
		auto *field=new std::uint16_t[1]{};
		TestGradientPipeline original, restored;
		original.configure(1,8,1,[](auto &job,auto &) { job.data[0]=42; });
		original.advance(); original.submit(&field,0,[](auto &) {});
		if(cancelled) original.invalidate(&field);
		for(unsigned i=0;i<phase;++i) original.advance();
		restored.configure(0,8,1,[](auto &,auto &) { REQUIRE(false); });
		original.visitPendingSnapshots([&](const auto &snapshot) {
			REQUIRE((snapshot.remaining==8-phase && field[0]==0));
			auto data=std::make_unique<std::uint16_t[]>(1); data[0]=snapshot.data[0];
			restored.restoreCompleted({&field,0,snapshot.remaining,snapshot.superseded,std::move(data)});
		});
		original.reset(); restored.setWorkerCount(1);
		for(unsigned i=1;i<8-phase;++i) { restored.advance(); REQUIRE(field[0]==0); }
		restored.advance(); REQUIRE(field[0]==(cancelled ? 0 : 42));
		restored.reset(); delete[] field;
	}
	TestGradientPipeline fallback;
	unsigned created=0;
	fallback.configure(4, 3, 1, [](auto &job, auto &) { job.data[0]=9; },
		[&](auto fn) { if (++created == 2) throw std::runtime_error("injected creation failure"); return std::thread(fn); });
	REQUIRE((fallback.workerCount()==0 && fallback.delayTicks()==3));
	auto *slot = new std::uint16_t[1]{};
	fallback.advance(); fallback.submit(&slot, 0, [](auto &) {});
	fallback.advance(); fallback.advance(); REQUIRE(slot[0]==0);
	fallback.advance(); REQUIRE(slot[0]==9); fallback.reset(); delete[] slot;
	// Propagation failure is delivered at its fixed deadline; teardown remains safe.
	fallback.configure(2, 1, 1, [](auto &, auto &) { throw std::runtime_error("injected work failure"); });
	slot = new std::uint16_t[1]{};
	fallback.advance(); fallback.submit(&slot, 0, [](auto &) {});
	bool caught=false; try { fallback.advance(); } catch (const std::runtime_error &) { caught=true; }
	REQUIRE(caught); fallback.reset(); delete[] slot;
	MESSAGE("PASS fixed publication, supersession, bounded buffers, scheduling stress, fallback, exceptions and teardown");
}
}

TEST_SUITE("GradientPipeline") {
TEST_CASE("reservation is invalidatable before preparation and seed failures cannot strand it") {
    for (unsigned workers : {0, 2}) {
        TestGradientPipeline pipeline;
        auto *slot = new std::uint16_t[1]{};
        pipeline.configure(workers, 2, 1, [](auto &job, auto &) { job.data[0] += 1; });
        pipeline.advance();
        auto *job = pipeline.reserve(&slot, 0);
        pipeline.invalidate(&slot);
        pipeline.prepare(job, [](auto &job) { job.data[0] = 41; });
        pipeline.finish();
        pipeline.advance(); pipeline.advance();
        CHECK(slot[0] == 0);
        CHECK(pipeline.metrics.discarded == 1);
        job = pipeline.reserve(&slot, 0);
        pipeline.prepare(job, [](auto &) { throw std::runtime_error("seed failure"); });
        CHECK_THROWS_AS(pipeline.finish(), std::runtime_error);
        pipeline.reset();
        delete[] slot;
    }
}
}

TEST_SUITE("GradientPipeline") {
TEST_CASE("projected terrain leases end at completion including failed work") {
    for (int failure : {0,1,2}) {
        auto terrain=std::make_shared<SimulationSnapshot::Terrain>();
        std::weak_ptr<const SimulationSnapshot::Terrain> lifetime=terrain;
        SimulationSnapshot::Handle foundation;
        foundation.requirements=SimulationSnapshot::bit(SimulationSnapshot::Component::Terrain);
        foundation.terrain=terrain;
        auto* slot=new std::uint16_t[1]{};
        TestGradientPipeline pipeline;
        pipeline.configure(0,2,1,[&](auto& job,auto&) {
            CHECK(job.snapshotLease.has_value());
            CHECK(job.snapshotLease->terrain==lifetime.lock());
            if(failure==1) throw std::runtime_error("work failure");
        });
        pipeline.advance();
        auto* job=pipeline.reserve(&slot,0);
        job->snapshotLease=foundation.project(foundation.requirements);
        foundation={};terrain.reset();
        CHECK_FALSE(lifetime.expired());
        if(failure==2) pipeline.prepare(job,[](auto&){throw std::runtime_error("seed failure");});
        else pipeline.prepare(job,[](auto&){});
        if(failure) CHECK_THROWS_AS(pipeline.finish(),std::runtime_error);
        else pipeline.finish();
        CHECK(lifetime.expired());
        pipeline.reset();delete[] slot;
    }
}
}

namespace {
// Generic building payload: a destination key, a computed value, an injected
// behaviour and a borrowed input whose lifetime must end with the computation.
struct TestBuildingJob {
    unsigned key = 0, value = 0, result = 0;
    enum class Behaviour { Normal, Slow, Throw, Gate } behaviour = Behaviour::Normal;
    std::shared_ptr<const unsigned> input;
    std::shared_future<void>* gate = nullptr;
    void releaseInputs() noexcept { input.reset(); }
};
using TestBuildingPipelineBase = BuildingGradientPipeline<TestBuildingJob>;
class TestBuildingPipeline : public TestBuildingPipelineBase
{
    ComputeExecutor executor;
public:
    ~TestBuildingPipeline() { reset(); }
    std::size_t workerCount() const { return executor.threadCount() - 1; }
    void configure(unsigned count, unsigned delay, Work work)
    {
        reset(); executor.configure(count+1);
        TestBuildingPipelineBase::configure(executor, count!=0, delay, std::move(work));
    }
};
void runTestBuildingJob(TestBuildingPipelineBase::Job& job)
{
    auto& payload = job.payload;
    if (!payload.input) throw std::logic_error("input released before execution");
    switch (payload.behaviour) {
    case TestBuildingJob::Behaviour::Slow: std::this_thread::sleep_for(std::chrono::microseconds(300)); break;
    case TestBuildingJob::Behaviour::Throw: throw std::runtime_error("injected building failure");
    case TestBuildingJob::Behaviour::Gate: payload.gate->wait(); break;
    default: break;
    }
    payload.result = payload.value + *payload.input;
}
}

TEST_CASE("gradient, building and AI batches share the maximum deferred horizon" * doctest::test_suite("GradientPipeline"))
{
    constexpr unsigned PerTick=TestBuildingPipelineBase::MaxJobsPerTick, BuildingDelay=TestBuildingPipelineBase::MaxDelay;
    for (unsigned threads : {1,2,4,8}) {
        ComputeExecutor executor; executor.configure(threads);
        GradientPipeline pipeline;
        TestBuildingPipelineBase buildings;
        auto* field=new std::uint16_t[1]{};
        pipeline.configure(executor,true,16,1,[](auto& job,auto&) { ++job.data[0]; });
        buildings.configure(executor,true,BuildingDelay,runTestBuildingJob);
        unsigned published=0;
        buildings.publish=[&](auto& job) { CHECK(job.payload.result==job.payload.value+1); ++published; return true; };
        const auto input=std::make_shared<const unsigned>(1);
        struct AIJob { unsigned value=0; ComputeExecutor::Batch batch; };
        std::array<AIJob,100> ai;
        std::size_t peak=0;
        for (unsigned tick=1; tick<ai.size(); ++tick) {
            pipeline.advance(); buildings.advance();
            if (tick>16) CHECK(field[0]==tick-16);
            if (tick>BuildingDelay) CHECK(published==(tick-BuildingDelay)*PerTick);
            const ComputeExecutor::Group group{1,{[](void* p,size_t) { ++static_cast<AIJob*>(p)->value; }, &ai[tick]},0};
            ai[tick].batch=executor.submit(std::span(&group,1),ComputeExecutor::boundaryDue(tick+8));
            pipeline.submit(&field,0,[tick](auto& job) { job.data[0]=tick-1; });
            std::vector<TestBuildingPipelineBase::Job*> staged;
            while (buildings.canReserve()) {
                auto* job=buildings.reserve(); job->payload={}; job->payload.value=tick; job->payload.input=input;
                staged.push_back(job);
            }
            CHECK(staged.size()==PerTick);
            buildings.prepare(staged); // One executor batch for the whole tick.
            if (tick>8) { executor.join(ai[tick-8].batch); CHECK(ai[tick-8].value==1); }
            CHECK(executor.liveBatches()<=ComputeExecutor::Slots);
            peak=std::max(peak,executor.liveBatches());
        }
        // A batch holds its slot from submission to its join, so the peak is
        // each producer's horizon plus one (17 + 9 + 9), within Slots.
        CHECK(peak<=(16+1)+(8+1)+(8+1));
        CHECK(peak<ComputeExecutor::Slots);
        pipeline.finish(); buildings.finish();
        for(auto& job:ai) executor.join(job.batch);
        CHECK(field[0]==83); // Finishing private work does not publish it.
        CHECK(published==(ai.size()-1-BuildingDelay)*PerTick);
        CHECK(buildings.metrics.batches==ai.size()-1);
        CHECK(buildings.metrics.maxBatch==PerTick);
        pipeline.reset(); buildings.reset(); delete[] field;
    }
}

TEST_CASE("building pipeline admits several jobs per tick, publishes in reservation order at fixed deadlines, supersedes, survives slow and failing workers" * doctest::test_suite("GradientPipeline"))
{
    using Behaviour = TestBuildingJob::Behaviour;
    constexpr unsigned PerTick = TestBuildingPipeline::MaxJobsPerTick;
    const auto input=std::make_shared<const unsigned>(7);
    for (unsigned workers : {0, 1, 2, 4}) for (unsigned delay : {1, 2, 4, 8}) {
        CAPTURE(workers); CAPTURE(delay);
        TestBuildingPipeline pipeline;
        pipeline.configure(workers, delay, runTestBuildingJob);
        CHECK_FALSE(pipeline.canReserve()); // Admission opens at the first advance.
        CHECK_THROWS_AS(pipeline.reserve(), std::logic_error);
        // Owner model: every reservation in order with its deadline.
        struct Expected { std::uint64_t due; unsigned value, key; bool superseded; };
        std::deque<Expected> model;
        std::vector<unsigned> published, expectedPublished;
        unsigned retired=0, rejected=0, expectedDiscarded=0;
        std::uint64_t now=0;
        pipeline.publish=[&](auto& job) {
            CHECK(job.done);
            CHECK(job.payload.result==job.payload.value+7);
            // The owner validates against live state; a vanished destination is rejected.
            if (job.payload.value%13==0) { ++rejected; return false; }
            published.push_back(job.payload.value);
            return true;
        };
        pipeline.retire=[&](auto& job) { CHECK_FALSE(job.payload.input); CHECK(job.due==now); ++retired; };
        for (unsigned tick=1; tick<=240; ++tick) {
            now=tick; pipeline.advance();
            while (!model.empty() && model.front().due<=tick) {
                REQUIRE(model.front().due==tick);
                if (model.front().superseded || model.front().value%13==0) ++expectedDiscarded;
                else expectedPublished.push_back(model.front().value);
                model.pop_front();
            }
            REQUIRE(published==expectedPublished); // Exactly at deadlines, in reservation order.
            REQUIRE(pipeline.metrics.discarded==expectedDiscarded);
            // A varying number of requests; admission caps them at PerTick.
            const unsigned requests=tick%7;
            std::vector<TestBuildingPipeline::Job*> staged;
            for (unsigned i=0; i<requests && pipeline.canReserve(); ++i) {
                auto* job=pipeline.reserve();
                auto& payload=job->payload;
                payload={}; payload.key=(tick+i)%5; payload.value=tick*PerTick+i; payload.input=input;
                if ((tick+i)%3==0) payload.behaviour=Behaviour::Slow;
                staged.push_back(job);
                model.push_back({tick+delay, payload.value, payload.key, false});
            }
            CHECK(staged.size()==std::min(requests, PerTick));
            if (requests>PerTick) { CHECK_FALSE(pipeline.canReserve()); CHECK_THROWS_AS(pipeline.reserve(), std::logic_error); }
            pipeline.prepare(staged);
            if (tick%4==0) {
                const unsigned key=tick%5;
                unsigned marked=0;
                for (auto& entry : model) if (entry.key==key && !entry.superseded) { entry.superseded=true; ++marked; }
                CHECK(pipeline.invalidate([key](const TestBuildingJob& job) { return job.key==key; })==marked);
            }
            REQUIRE(pipeline.pendingCount()==model.size());
            REQUIRE(pipeline.pendingCount()<=delay*PerTick);
        }
        pipeline.finish(); // Completing private work never publishes it.
        REQUIRE(published==expectedPublished);
        CHECK(pipeline.metrics.maxPending<=delay*PerTick);
        CHECK(retired==pipeline.metrics.published+pipeline.metrics.discarded);
        CHECK(pipeline.metrics.published==published.size());
        CHECK(rejected>0);
        // Teardown retires pending jobs without publishing them.
        const auto before=published.size(); const auto pendingAtDrain=pipeline.pendingCount();
        pipeline.retire=[&](auto&) { ++retired; };
        pipeline.drain();
        CHECK(published.size()==before);
        CHECK(retired==pipeline.metrics.published+pipeline.metrics.discarded+pendingAtDrain);
        CHECK(pipeline.pendingCount()==0);

        // A failure surfaces at finish and at its deadline, after the earlier jobs
        // of its tick publish; jobs of earlier ticks are unaffected.
        pipeline.configure(workers, delay, runTestBuildingJob);
        published.clear();
        pipeline.publish=[&](auto& job) { published.push_back(job.payload.value); return true; };
        pipeline.retire=[](auto&) {};
        for (unsigned tick=1; tick<=2; ++tick) {
            pipeline.advance();
            std::vector<TestBuildingPipeline::Job*> staged;
            for (unsigned i=0; i<3; ++i) {
                auto* job=pipeline.reserve(); job->payload={}; job->payload.value=tick*10+i; job->payload.input=input;
                if (tick==2 && i==1) job->payload.behaviour=Behaviour::Throw;
                staged.push_back(job);
            }
            pipeline.prepare(staged);
        }
        CHECK_THROWS_AS(pipeline.finish(), std::runtime_error);
        for (unsigned tick=3; tick<=1+delay; ++tick) pipeline.advance();
        CHECK(published==std::vector<unsigned>{10,11,12});
        CHECK_THROWS_AS(pipeline.advance(), std::runtime_error);
        CHECK(published==std::vector<unsigned>{10,11,12,20});
        pipeline.reset();

        // A slow worker never moves its deadline: the owner joins it exactly at the due tick.
        if (workers && GAGCore::ThreadSupport::available) {
            pipeline.configure(workers, delay, runTestBuildingJob);
            // Owner-only fallback cannot wait on a gate opened by another thread.
            if (!pipeline.workerCount()) continue;
            published.clear();
            pipeline.publish=[&](auto& job) { published.push_back(job.payload.value); return true; };
            std::promise<void> release; std::shared_future<void> gate=release.get_future().share();
            pipeline.advance();
            std::vector<TestBuildingPipeline::Job*> staged;
            for (unsigned i=0; i<PerTick; ++i) {
                auto* job=pipeline.reserve(); job->payload={}; job->payload.value=i; job->payload.input=input;
                if (i==0) { job->payload.behaviour=Behaviour::Gate; job->payload.gate=&gate; }
                staged.push_back(job);
            }
            pipeline.prepare(staged);
            for (unsigned i=1; i<delay; ++i) { pipeline.advance(); CHECK(published.empty()); }
            std::thread opener([&] { std::this_thread::sleep_for(std::chrono::milliseconds(20)); release.set_value(); });
            pipeline.advance(); // Waits for the gated job, then publishes all four.
            opener.join();
            CHECK(published==std::vector<unsigned>{0,1,2,3});
            pipeline.reset();
        }
    }
}

TEST_CASE("building pipeline pending results survive a save-style round trip" * doctest::test_suite("GradientPipeline"))
{
    constexpr unsigned Delay=4, PerTick=TestBuildingPipeline::MaxJobsPerTick;
    const auto input=std::make_shared<const unsigned>(5);
    for (unsigned phase=0; phase<Delay; ++phase) for (unsigned workers : {0, 1, 2}) {
        CAPTURE(phase); CAPTURE(workers);
        TestBuildingPipeline original, restored;
        original.configure(workers, Delay, runTestBuildingJob);
        std::vector<std::pair<unsigned,unsigned>> reference; // (advances after the save, value)
        // Fill the horizon: tick t reserves (t%PerTick)+1 jobs; keys divisible by five are superseded.
        const unsigned saveTick=Delay+phase;
        for (unsigned tick=1; tick<=saveTick; ++tick) {
            original.advance();
            std::vector<TestBuildingPipeline::Job*> staged;
            for (unsigned i=0; i<=tick%PerTick; ++i) {
                auto* job=original.reserve(); job->payload={}; job->payload.key=tick*PerTick+i;
                job->payload.value=job->payload.key*3; job->payload.input=input;
                staged.push_back(job);
                if (tick+Delay>saveTick && job->payload.key%5!=0)
                    reference.push_back({tick+Delay-saveTick, job->payload.value});
            }
            original.prepare(staged);
            original.invalidate([](const TestBuildingJob& job) { return job.key%5==0; });
        }
        restored.configure(0, Delay, [](auto&) { throw std::logic_error("restored jobs are complete"); });
        unsigned afterSave=0, publishedAtSave=0;
        std::vector<std::pair<unsigned,unsigned>> restoredPublished, continued;
        original.publish=[&](auto& job) { continued.push_back({afterSave, job.payload.value}); return true; };
        restored.publish=[&](auto& job) {
            CHECK(job.payload.result==job.payload.value+5);
            restoredPublished.push_back({afterSave, job.payload.value});
            return true;
        };
        publishedAtSave=original.metrics.published;
        unsigned visited=0;
        original.visitPending([&](auto& job, unsigned remaining) {
            CHECK(job.done); CHECK_FALSE(job.payload.input);
            CHECK(remaining>=1); CHECK(remaining<=Delay);
            TestBuildingJob saved; saved.key=job.payload.key; saved.value=job.payload.value; saved.result=job.payload.result;
            restored.restoreCompleted(remaining, job.superseded, std::move(saved));
            ++visited;
        });
        CHECK(original.metrics.published==publishedAtSave); // Saving never publishes.
        CHECK(restored.pendingCount()==visited);
        restored.setWorkerCount(1); // Execution is configuration, not saved state.
        for (afterSave=1; afterSave<=Delay; ++afterSave) { restored.advance(); original.advance(); }
        CHECK(restoredPublished==reference);
        CHECK(continued==reference);
        CHECK(restored.pendingCount()==0);
        original.reset(); restored.reset();
    }
    // Saved deadlines are validated against the live admission bounds.
    TestBuildingPipeline pipeline;
    pipeline.configure(0, Delay, runTestBuildingJob);
    CHECK_THROWS_AS(pipeline.restoreCompleted(0, false, {}), std::runtime_error);
    CHECK_THROWS_AS(pipeline.restoreCompleted(Delay+1, false, {}), std::runtime_error);
    pipeline.restoreCompleted(2, false, {});
    CHECK_THROWS_AS(pipeline.restoreCompleted(1, false, {}), std::runtime_error); // Out of order.
    for (unsigned i=1; i<PerTick; ++i) pipeline.restoreCompleted(2, true, {});
    CHECK_THROWS_AS(pipeline.restoreCompleted(2, false, {}), std::runtime_error); // Over the per-tick cap.
    pipeline.restoreCompleted(3, false, {});
    pipeline.reset();
    TestBuildingPipeline disabled;
    disabled.configure(0, 0, runTestBuildingJob);
    CHECK_THROWS_AS(disabled.restoreCompleted(1, false, {}), std::runtime_error);
    CHECK_THROWS_AS(disabled.configure(0, TestBuildingPipeline::MaxDelay+1, runTestBuildingJob), std::invalid_argument);
}

TEST_CASE("owner-only preparation computes at submission and errors surface at the deadline" * doctest::test_suite("GradientPipeline"))
{
    ComputeExecutor executor; executor.configure(2);
    GradientPipeline pipeline;
    auto* field=new std::uint16_t[1]{};
    pipeline.configure(executor,false,2,1,[](auto&,auto&) { FAIL("failed seed must not propagate"); });
    pipeline.advance();
    bool seeded=false;
    pipeline.submit(&field,0,[&](auto&) { seeded=true; throw std::runtime_error("seed failure"); });
    // Computed inline by the owner; the executor never saw it.
    CHECK(seeded);
    CHECK(executor.metrics().deferredBatches==0);
    CHECK_THROWS_AS(pipeline.finish(),std::runtime_error);
    pipeline.advance();
    CHECK_THROWS_AS(pipeline.advance(),std::runtime_error);
    pipeline.reset(); delete[] field;
}

TEST_CASE("shared preparation executes off the owner and retains its deadline" * doctest::test_suite("GradientPipeline"))
{
    ComputeExecutor executor; executor.configure(2);
    if (executor.threadCount()<2) return;
    GradientPipeline pipeline;
    auto* field=new std::uint16_t[1]{};
    const auto owner=std::this_thread::get_id();
    std::promise<void> started, release;
    auto startedSignal=started.get_future(); auto releaseSignal=release.get_future();
    bool onWorker=false;
    pipeline.configure(executor,true,2,1,[](auto& job,auto&) { ++job.data[0]; });
    pipeline.advance();
    pipeline.submit(&field,0,[&](auto& job) {
        onWorker=std::this_thread::get_id()!=owner;
        started.set_value(); releaseSignal.wait(); job.data[0]=41;
    });
    CHECK(startedSignal.wait_for(std::chrono::seconds(5))==std::future_status::ready);
    CHECK(field[0]==0);
    release.set_value(); pipeline.finish();
    CHECK(onWorker); CHECK(field[0]==0);
    pipeline.advance(); CHECK(field[0]==0);
    pipeline.advance(); CHECK(field[0]==42);
    pipeline.reset(); delete[] field;
}

TEST_CASE("backend selection is shared across owner and resized worker scratch" * doctest::test_suite("GradientPipeline"))
{
    auto session=std::make_shared<gradient_kernel::BackendSession>();
    TestGradientPipeline pipeline;
    pipeline.setBackendSession(session);
    auto* field=new std::uint16_t[1]{};
    for (unsigned workers : {0,2,4,0})
    {
        pipeline.configure(workers,1,1,[session](auto& job,auto& scratch) {
            if (scratch.backendSession!=session ||
                !scratch.backendSession->currentGeneration())
                throw std::logic_error("gradient workspace lost game backend selection");
            job.data[0]=42;
        });
        pipeline.advance(); pipeline.submit(&field,0,[](auto&){});
        pipeline.finish(); pipeline.advance();
        CHECK(field[0]==42);
        pipeline.reset();
    }
    delete[] field;
}

TEST_CASE("GradientPipeline/worker GPU submissions finish ahead of deadlines and preserve errors and immutable leases")
{
    using namespace gradient_kernel;
    const auto oldBackend = backend();
    const auto oldBatch = batchAccelerator;
    struct Restore
    {
        Backend choice;
        decltype(batchAccelerator) provider;
        unsigned mask;
        ~Restore()
        {
            setBackend(choice);
            batchAccelerator = provider;
            readyPlans.store(mask);
        }
    } restore{oldBackend, oldBatch, readyPlans.load()};
    readyPlans.store(1u<<unsigned(Plan::Frozen8));
    setBackend(Backend::OpenCL);
    batchAccelerator = [](std::span<const BackendRequest>, Plan)
    { return false; }; // Eligibility only; callback below owns the test work.
    for (unsigned workers : {1, 4})
    {
        TestGradientPipeline pipeline;
        std::array<std::uint16_t *, 4> slots{};
        for (auto &slot : slots)
            slot = new std::uint16_t[1]{};
        std::atomic<unsigned> batches{0}, singleCalls{0};
        std::mutex completionMutex;
        std::condition_variable completed;
        pipeline.configure(workers, 4, 1, [&](auto &, auto &) { ++singleCalls; });
        pipeline.setBatchWork(
            [&](std::span<GradientPipeline::Job *const> jobs, std::span<GradientWorkspace> scratch)
            {
                CHECK(jobs.size() == 1);
                CHECK(scratch.size() == jobs.size());
                for (auto *job : jobs)
                {
                    REQUIRE(job->water);
                    job->data[0] += 100;
                }
                { std::lock_guard lock(completionMutex); ++batches; }
                completed.notify_all();
            });
        for (unsigned tick = 0; tick < 4; ++tick)
        {
            pipeline.advance();
            pipeline.submit(&slots[tick], 0,
                            [tick](auto &job)
                            {
                                job.data[0] = tick;
                                job.water = std::make_shared<const std::vector<std::uint8_t>>(1, 1);
                            });
        }
        for (auto *slot : slots)
            CHECK(slot[0] == 0);
        // Completion must happen on workers before the owner reaches a deadline.
        {
            std::unique_lock lock(completionMutex);
            CHECK(completed.wait_for(lock, std::chrono::seconds(2), [&] { return batches == 4; }));
        }
        pipeline.advance(); // Publication still waits for each original deadline.
        CHECK(slots[0][0] == 100);
        for (unsigned i = 1; i < 4; ++i)
            CHECK(slots[i][0] == 0);
        CHECK(batches == 4);
        CHECK(singleCalls == 0);
        for (unsigned i = 1; i < 4; ++i)
        {
            pipeline.advance();
            CHECK(slots[i][0] == 100 + i);
        }
        pipeline.reset();
        for (auto *slot : slots)
            delete[] slot;
        auto *failed = new std::uint16_t[1]{};
        pipeline.configure(workers, 2, 1, [](auto &, auto &) {});
        pipeline.setBatchWork([](auto, auto) { throw std::runtime_error("batch failure"); });
        pipeline.advance();
        pipeline.submit(&failed, 0, [](auto &job) { job.data[0] = 42; });
        CHECK_THROWS_AS(pipeline.finish(), std::runtime_error);
        CHECK(failed[0] == 0);
        pipeline.reset();
        delete[] failed;
        std::array<std::uint16_t *, 2> delayed{};
        for (auto &slot : delayed)
            slot = new std::uint16_t[1]{};
        pipeline.configure(workers, 3, 1, [](auto &, auto &) {});
        pipeline.setBatchWork(
            [](std::span<GradientPipeline::Job *const> jobs, auto)
            {
                for (auto *job : jobs)
                    job->data[0] += 100;
            });
        pipeline.advance();
        pipeline.submit(&delayed[0], 0,
                        [](auto &job)
                        {
                            std::this_thread::sleep_for(std::chrono::milliseconds(3));
                            job.data[0] = 21;
                        });
        pipeline.advance();
        pipeline.submit(&delayed[1], 0, [](auto &) { throw std::runtime_error("later seed failure"); });
        pipeline.advance();
        CHECK(delayed[0][0] == 0);
        CHECK(delayed[1][0] == 0);
        CHECK_NOTHROW(pipeline.advance());
        CHECK(delayed[0][0] == 121);
        CHECK(delayed[1][0] == 0);
        CHECK_THROWS_AS(pipeline.advance(), std::runtime_error); // Failure keeps the later field's deadline.
        pipeline.reset();
        for (auto *slot : delayed)
            delete[] slot;
    }
}

TEST_CASE("GradientPipeline/CPU fields keep their original worker")
{
    using namespace gradient_kernel;
    for (const auto choice : {Backend::CPU}) {
    const auto previous = backend();
    const auto provider = batchAccelerator;
    struct Restore {
        Backend previous; decltype(batchAccelerator) provider;
        ~Restore() { setBackend(previous); batchAccelerator = provider; }
    } restore{previous, provider};
    setBackend(choice);
    batchAccelerator = [](std::span<const BackendRequest>, Plan) { return false; };
    TestGradientPipeline pipeline;
    std::promise<void> completion;
    auto finished = completion.get_future();
    const auto owner = std::this_thread::get_id();
    std::thread::id worker, seeded;
    unsigned batches = 0;
    pipeline.configure(1, 4, 1, [&](auto& job, auto&) {
        worker = std::this_thread::get_id();
        job.data[0] = 123;
        completion.set_value();
    });
    pipeline.setBatchWork([&](auto, auto) { ++batches; });
    auto session = std::make_shared<BackendSession>();
    pipeline.setBackendSession(session);
    auto* field = new std::uint16_t[1]{};
    pipeline.advance();
    auto* job = pipeline.reserve(&field, 0);
    pipeline.prepare(job, [&](auto& prepared) { seeded = std::this_thread::get_id(); prepared.data[0] = 1; });
    CHECK(finished.wait_for(std::chrono::seconds(2)) == std::future_status::ready);
    pipeline.finish();
    CHECK(worker != owner);
    CHECK(worker == seeded);
    CHECK(batches == 0);
    CHECK(field[0] == 0);
    for (unsigned tick = 0; tick < 4; ++tick) pipeline.advance();
    CHECK(field[0] == 123);
    pipeline.reset();
    delete[] field;
    }
}

TEST_CASE("owned GPU service initializes with two slots and leaves the sole worker free until fixed publication" * doctest::test_suite("GradientPipeline"))
{
    using namespace gradient_kernel;
    if constexpr(!GAGCore::ThreadSupport::available) return;
    struct Restore {
        Backend mode=backend(); unsigned mask=readyPlans.load();
        ~Restore(){setBackend(mode);readyPlans=mask;}
    } restore;
    readyPlans=1u<<unsigned(requestedOpenCLPlan()); setBackend(Backend::OpenCL);
    struct State {
        std::mutex mutex; std::condition_variable changed;
        bool entered=false, release=false;
        std::atomic<unsigned> cpu{0},gpu{0};
        std::atomic<bool> initializedOnWorker{false},cpuOnWorker{false};
    };
    auto state=std::make_shared<State>();
    auto service=std::make_shared<GradientDeviceService>(GradientDeviceService::Hooks{
        [state]{state->initializedOnWorker=ComputeExecutor::workerSlot()!=0;return true;},
        [state](std::span<const BackendRequest> requests,Plan){
            {std::unique_lock lock(state->mutex);state->entered=true;state->changed.notify_one();
             state->changed.wait(lock,[&]{return state->release;});}
            for(const auto& request:requests){request.gradient[0]+=100;if(request.executedOnDevice)*request.executedOnDevice=true;++state->gpu;}
            return true;
        }});
    service->configure(2,Backend::OpenCL);
    const auto until=std::chrono::steady_clock::now()+std::chrono::seconds(5);
    while(!service->metrics().ready && std::chrono::steady_clock::now()<until) std::this_thread::yield();
    REQUIRE(service->metrics().ready); CHECK_FALSE(state->initializedOnWorker);
    TestGradientPipeline pipeline;
    pipeline.configure(1,3,1,[](auto&,auto&){FAIL("GPU owned field reached synchronous callback");});
    pipeline.setDeviceService(service);
    pipeline.setAsyncWork([state](auto& job,PlanDecision decision){
        auto owned=std::make_shared<OwnedGradientField>();
        owned->inputs=state;owned->session=job.owner->session();owned->decision=decision;
        owned->due=job.executorDue;owned->data=std::move(job.data);
        owned->costAt=[](const auto&,std::size_t){return LAND_STEPS;};
        owned->cpu=[](auto& owned){
            auto& state=*const_cast<State*>(static_cast<const State*>(owned.inputs.get()));
            state.cpuOnWorker=ComputeExecutor::workerSlot()!=0;++state.cpu;owned.data[0]+=100;
        };
        return owned;
    });
    auto* published=new std::uint16_t[1]{};
    pipeline.advance();pipeline.submit(&published,0,[](auto& job){job.data[0]=23;});
    {
        std::unique_lock lock(state->mutex);
        const bool entered=state->changed.wait_for(lock,std::chrono::seconds(5),[&]{return state->entered;});
        if(!entered){state->release=true;state->changed.notify_one();}
        REQUIRE(entered);
    }
    std::atomic<unsigned> unrelated{0};
    const ComputeExecutor::Group other{1,{[](void* value,std::size_t){++*static_cast<std::atomic<unsigned>*>(value);},&unrelated}};
    auto batch=pipeline.compute().submit(std::span(&other,1));pipeline.compute().join(batch);
    CHECK(unrelated==1);CHECK(published[0]==0);CHECK(service->metrics().retainedHostBytes>0);
    {std::lock_guard lock(state->mutex);state->release=true;}state->changed.notify_one();
    unsigned visited=0;
    pipeline.visitPendingSnapshots([&](auto snapshot){
        ++visited;CHECK(snapshot.data[0]==123);CHECK(snapshot.remaining==3);CHECK_FALSE(snapshot.superseded);
    });
    CHECK(visited==1);CHECK(published[0]==0);CHECK(state->gpu==1);CHECK(state->cpu==0);
    CHECK(service->metrics().retainedHostBytes==0);
    pipeline.advance();pipeline.advance();CHECK(published[0]==0);
    pipeline.advance();CHECK(published[0]==123);CHECK(pipeline.gpuCompleteFields()==1);
    pipeline.reset();service->stop();delete[] published;
}

TEST_CASE("GPU decline recovers original seeds exactly once on a worker and retains inputs until recovery" * doctest::test_suite("GradientPipeline"))
{
    using namespace gradient_kernel;
    if constexpr(!GAGCore::ThreadSupport::available) return;
    struct Restore {Backend mode=backend();unsigned mask=readyPlans.load();~Restore(){setBackend(mode);readyPlans=mask;}} restore;
    readyPlans=1u<<unsigned(requestedOpenCLPlan());setBackend(Backend::OpenCL);
    for(bool deviceCompleted:{false,true}){
    struct State {std::atomic<unsigned> cpu{0};std::atomic<bool> worker{false},attempted{false};bool expectedDeviceCompletion=false;};
    auto state=std::make_shared<State>();state->expectedDeviceCompletion=deviceCompleted;
    // Backend test hooks also retain State. Observe only the independent owned
    // recovery-input graph, so hook lifetime cannot mask its completion release.
    struct Inputs {std::shared_ptr<State> state;};
    auto inputs=std::make_shared<Inputs>(Inputs{state});std::weak_ptr<Inputs> retained=inputs;
    auto service=std::make_shared<GradientDeviceService>(GradientDeviceService::Hooks{
        []{return true;},[deviceCompleted,state](std::span<const BackendRequest> requests,Plan){
            CHECK(requests.front().gradient[0]==77);
            REQUIRE(requests.front().deviceExecutionObserved);
            *requests.front().deviceExecutionObserved=deviceCompleted;state->attempted=true;return false;
        }});
    service->configure(2,Backend::OpenCL);
    const auto until=std::chrono::steady_clock::now()+std::chrono::seconds(5);
    while(!service->metrics().ready && std::chrono::steady_clock::now()<until)std::this_thread::yield();
    REQUIRE(service->metrics().ready);
    TestGradientPipeline pipeline;pipeline.configure(1,2,1,[](auto&,auto&){FAIL("unexpected synchronous execution");});
    pipeline.setDeviceService(service);
    pipeline.setAsyncWork([inputs](auto& job,PlanDecision decision){
        auto owned=std::make_shared<OwnedGradientField>();owned->inputs=inputs;
        owned->session=job.owner->session();owned->decision=decision;owned->data=std::move(job.data);
        owned->costAt=[](const auto&,std::size_t){return LAND_STEPS;};
        owned->cpu=[](auto& owned){
            auto& state=*static_cast<const Inputs*>(owned.inputs.get())->state;
            state.worker=ComputeExecutor::workerSlot()!=0;++state.cpu;
            CHECK(owned.data[0]==77);CHECK_FALSE(owned.executedGPU);
            CHECK(owned.deviceExecutionObserved==state.expectedDeviceCompletion);owned.data[0]=88;
        };return owned;
    });
    auto* field=new std::uint16_t[1]{};pipeline.advance();pipeline.submit(&field,0,[](auto& job){job.data[0]=77;});
    while(!state->attempted && std::chrono::steady_clock::now()<until)std::this_thread::yield();
    REQUIRE(state->attempted);
    pipeline.finish();CHECK(state->cpu==1);CHECK(state->worker);CHECK(field[0]==0);
    CHECK(service->metrics().fallbacks==1);CHECK(pipeline.cpuCompleteFields()==1);
    while(deviceCompleted && service->metrics().deviceCompletedFields!=1 && std::chrono::steady_clock::now()<until)std::this_thread::yield();
    CHECK(service->metrics().deviceCompletedFields==unsigned(deviceCompleted));
    CHECK(service->metrics().fallbackAfterDeviceCompletionFields==unsigned(deviceCompleted));
    CHECK(service->metrics().executed==0);CHECK(pipeline.gpuCompleteFields()==0);
    pipeline.advance();pipeline.advance();CHECK(field[0]==88);
    pipeline.setAsyncWork({});inputs.reset();CHECK(retained.expired());
    pipeline.reset();service->stop();delete[] field;
    }
}

TEST_CASE("one-slot device configuration creates no driver activity" * doctest::test_suite("GradientPipeline"))
{
    using namespace gradient_kernel;
    std::atomic<unsigned> initialized{0};
    GradientDeviceService service({[&]{++initialized;return true;},{}});
    service.configure(1,Backend::OpenCL);service.stop();
    CHECK(initialized==0);CHECK_FALSE(service.metrics().running);
}

TEST_CASE("proven gradient mismatch resolves tickets without recovery or publication" * doctest::test_suite("GradientPipeline"))
{
    using namespace gradient_kernel;
    if constexpr(!GAGCore::ThreadSupport::available)return;
    struct Restore {Backend mode=backend();unsigned mask=readyPlans.load();~Restore(){setBackend(mode);readyPlans=mask;}} restore;
    readyPlans=1u<<unsigned(requestedOpenCLPlan());setBackend(Backend::OpenCL);
    std::atomic<bool> attempted{false};
    struct State {mutable std::atomic<unsigned> cpu{0};};
    auto state=std::make_shared<State>();
    auto service=std::make_shared<GradientDeviceService>(GradientDeviceService::Hooks{
        []{return true;},[&](std::span<const BackendRequest>,Plan)->bool{
            attempted=true;throw GradientProofFailure("injected proven field mismatch");
        }});
    service->configure(2,Backend::OpenCL);
    const auto until=std::chrono::steady_clock::now()+std::chrono::seconds(5);
    while(!service->metrics().ready && std::chrono::steady_clock::now()<until)std::this_thread::yield();
    REQUIRE(service->metrics().ready);
    TestGradientPipeline pipeline;pipeline.configure(1,2,1,[](auto&,auto&){FAIL("unexpected CPU execution");});
    pipeline.setDeviceService(service);
    pipeline.setAsyncWork([&](auto& job,PlanDecision decision){
        auto owned=std::make_shared<OwnedGradientField>();owned->session=job.owner->session();
        owned->inputs=state;
        owned->decision=decision;owned->data=std::move(job.data);
        owned->costAt=[](const auto&,std::size_t){return LAND_STEPS;};
        owned->cpu=[](auto& field){++static_cast<const State*>(field.inputs.get())->cpu;};return owned;
    });
    auto* field=new std::uint16_t[1]{};
    pipeline.advance();pipeline.submit(&field,0,[](auto& job){job.data[0]=77;});
    while(!attempted && std::chrono::steady_clock::now()<until)std::this_thread::yield();
    REQUIRE(attempted);
    CHECK_THROWS_AS(pipeline.finish(),GradientProofFailure);
    CHECK(state->cpu==0);CHECK(field[0]==0);CHECK(service->metrics().fallbacks==0);
    CHECK(service->metrics().executed==0);CHECK(pipeline.cpuCompleteFields()==0);
    pipeline.reset();service->stop();delete[] field;
}

TEST_CASE("availability changes during seeding do not reinterpret captured CPU execution" * doctest::test_suite("GradientPipeline"))
{
    using namespace gradient_kernel;
    struct Restore {Backend mode=backend();unsigned mask=readyPlans.load();~Restore(){setBackend(mode);readyPlans=mask;}} restore;
    setBackend(Backend::OpenCL);
    TestGradientPipeline pipeline;
    pipeline.configure(1,2,1,[](auto& job,auto&){CHECK(job.cpuOnly);job.data[0]=88;});
    pipeline.session()->setExternalInitialization(true);readyPlans=0;
    std::atomic<unsigned> asynchronous{0};
    pipeline.setAsyncWork([&](auto&,PlanDecision)->std::shared_ptr<OwnedGradientField>{++asynchronous;return {};});
    auto* field=new std::uint16_t[1]{};
    pipeline.advance();pipeline.submit(&field,0,[](auto& job){
        job.data[0]=77;readyPlans=1u<<unsigned(requestedOpenCLPlan());
    });
    pipeline.finish();CHECK(asynchronous==0);CHECK(field[0]==0);
    pipeline.advance();pipeline.advance();CHECK(field[0]==88);
    pipeline.reset();delete[] field;
}

TEST_CASE("nullable GPU preparation recovers once on the original worker without device reentry" * doctest::test_suite("GradientPipeline"))
{
    using namespace gradient_kernel;
    if constexpr(!GAGCore::ThreadSupport::available)return;
    struct Restore {Backend mode=backend();unsigned mask=readyPlans.load();~Restore(){setBackend(mode);readyPlans=mask;}} restore;
    readyPlans=1u<<unsigned(requestedOpenCLPlan());setBackend(Backend::OpenCL);
    struct State {std::atomic<unsigned> attempted{0},cpu{0},gpu{0};std::atomic<bool> worker{true};};
    auto state=std::make_shared<State>();
    auto service=std::make_shared<GradientDeviceService>(GradientDeviceService::Hooks{[]{return true;},
        [state](std::span<const BackendRequest>,Plan){++state->gpu;return false;}});
    service->configure(2,Backend::OpenCL);
    const auto readyUntil=std::chrono::steady_clock::now()+std::chrono::seconds(5);
    while(!service->metrics().ready&&std::chrono::steady_clock::now()<readyUntil)std::this_thread::yield();
    REQUIRE(service->metrics().ready);
    TestGradientPipeline pipeline,restored;
    pipeline.configure(1,2,1,[state](auto& job,auto&){
        CHECK(job.cpuOnly==(job.data[0]!=90));
        if(job.data[0]!=90&&!ComputeExecutor::workerSlot())state->worker=false;
        ++state->cpu;job.data[0]+=11;
    });
    pipeline.setDeviceService(service);
    pipeline.setAsyncWork([state](auto& job,PlanDecision)->std::shared_ptr<OwnedGradientField>{
        CHECK(ComputeExecutor::workerSlot()!=0);CHECK((job.data[0]==77||job.data[0]==78));
        ++state->attempted;return {};
    });
    std::array<std::uint16_t*,2> fields{new std::uint16_t[1]{},new std::uint16_t[1]{}};
    std::array<std::uint16_t*,2> resumed{new std::uint16_t[1]{},new std::uint16_t[1]{}};
    for(unsigned i=0;i<2;++i){pipeline.advance();pipeline.submit(&fields[i],0,[i](auto& job){job.data[0]=77+i;});}
    const auto attemptedUntil=std::chrono::steady_clock::now()+std::chrono::seconds(5);
    while(state->attempted!=2&&std::chrono::steady_clock::now()<attemptedUntil)std::this_thread::yield();
    REQUIRE(state->attempted==2);pipeline.finish();
    CHECK(state->worker);CHECK(state->cpu==2);CHECK(state->gpu==0);
    CHECK(service->metrics().submitted==0);CHECK(pipeline.cpuCompleteFields()==2);
    CHECK(pipeline.cpuReason(GradientPipeline::CPUReason::OwnedInputDecline)==2);
    CHECK(pipeline.cpuReason(GradientPipeline::CPUReason::Unavailable)==0);
    CHECK(fields[0][0]==0);CHECK(fields[1][0]==0);
    restored.configure(0,2,1,[](auto&,auto&){FAIL("saved completed CPU recovery recomputed");});restored.advance();restored.advance();
    unsigned index=0;
    pipeline.visitPendingSnapshots([&](const auto& snapshot){
        REQUIRE(index<2);CHECK(snapshot.remaining==1+index);CHECK(snapshot.data[0]==88+index);
        auto data=std::make_unique<std::uint16_t[]>(1);data[0]=snapshot.data[0];
        restored.restoreCompleted({&resumed[index],0,snapshot.remaining,snapshot.superseded,std::move(data)});++index;
    });
    CHECK(index==2);pipeline.advance();restored.advance();CHECK(fields[0][0]==88);CHECK(resumed[0][0]==88);CHECK(fields[1][0]==0);
    pipeline.advance();restored.advance();
    for(unsigned i=0;i<2;++i){CHECK(fields[i][0]==88+i);CHECK(resumed[i][0]==fields[i][0]);}
    CHECK(state->cpu==2);CHECK(state->gpu==0);
    // Published jobs are pooled. A declined job must not poison its next
    // ordinary generation with a stale recovery-only routing flag.
    setBackend(Backend::CPU);
    pipeline.submit(&fields[1],0,[](auto& job){CHECK_FALSE(job.cpuOnly);job.data[0]=90;});
    pipeline.finish();CHECK(state->cpu==3);CHECK(state->gpu==0);CHECK(fields[1][0]==89);
    pipeline.advance();pipeline.advance();CHECK(fields[1][0]==101);
    pipeline.reset();restored.reset();service->stop();for(auto* p:fields)delete[] p;for(auto* p:resumed)delete[] p;
}

TEST_CASE("device registration stop and reconfigure never join a slow background initializer" * doctest::test_suite("GradientPipeline"))
{
    using namespace gradient_kernel;
    if constexpr(!GAGCore::ThreadSupport::available) return;
    struct Gate {
        std::promise<void> entered,release;
        std::shared_future<void> released=release.get_future().share();
        std::atomic<bool> opened{false};
        void open(){if(!opened.exchange(true))release.set_value();}
    };
    auto gate=std::make_shared<Gate>();
    struct Release {std::shared_ptr<Gate> gate;~Release(){gate->open();}} release{gate};
    auto entered=gate->entered.get_future();
    auto service=std::make_shared<GradientDeviceService>(GradientDeviceService::Hooks{
        [gate]{gate->entered.set_value();gate->released.wait();return true;},{}});
    service->configure(2,Backend::OpenCL);
    REQUIRE(entered.wait_for(std::chrono::seconds(5))==std::future_status::ready);
    auto stopped=std::async(std::launch::async,[service]{
        service->stop();service->configure(2,Backend::CPU);return service->metrics();
    });
    const auto progress=stopped.wait_for(std::chrono::milliseconds(100));
    CHECK(progress==std::future_status::ready);
    // Always release before future destruction, including a regression failure.
    gate->open();const auto metrics=stopped.get();
    CHECK_FALSE(metrics.running);CHECK_FALSE(metrics.ready);
}

TEST_CASE("application shutdown joins background initialization and later registrations restart" * doctest::test_suite("GradientPipeline"))
{
    using namespace gradient_kernel;
    if constexpr(!GAGCore::ThreadSupport::available) return;
    struct Gate {
        std::promise<void> entered,release;
        std::shared_future<void> released=release.get_future().share();
        std::atomic<bool> opened{false};
        void open(){if(!opened.exchange(true))release.set_value();}
    };
    auto gate=std::make_shared<Gate>();
    struct Release {std::shared_ptr<Gate> gate;~Release(){gate->open();}} release{gate};
    auto entered=gate->entered.get_future();
    GradientDeviceService first({[gate]{gate->entered.set_value();gate->released.wait();return true;},{}});
    first.configure(2,Backend::OpenCL);
    REQUIRE(entered.wait_for(std::chrono::seconds(5))==std::future_status::ready);
    auto shutdown=std::async(std::launch::async,[]{shutdownGradientDeviceCoordinator();});
    CHECK(shutdown.wait_for(std::chrono::milliseconds(100))==std::future_status::timeout);
    // Release before retrieving the future even when a check fails.
    gate->open();shutdown.get();
    CHECK_FALSE(first.metrics().running);
    CHECK(first.metrics().coordinatorThreads==0);
    std::promise<void> restarted;
    auto ready=restarted.get_future();
    struct Drain {~Drain(){shutdownGradientDeviceCoordinator();}} drain;
    GradientDeviceService second({[&restarted]{restarted.set_value();return true;},{}});
    second.configure(2,Backend::OpenCL);
    REQUIRE(ready.wait_for(std::chrono::seconds(5))==std::future_status::ready);
    shutdownGradientDeviceCoordinator();
    CHECK_FALSE(second.metrics().running);
    CHECK(second.metrics().coordinatorThreads==0);
    shutdownGradientDeviceCoordinator(); // Repeated teardown remains harmless.
}

TEST_CASE("two device registrations share exactly one process coordinator" * doctest::test_suite("GradientPipeline"))
{
    using namespace gradient_kernel;
    if constexpr(!GAGCore::ThreadSupport::available) return;
    struct Ids {std::thread::id first,second;};
    auto ids=std::make_shared<Ids>();
    GradientDeviceService first({[ids]{ids->first=std::this_thread::get_id();return true;},{}});
    GradientDeviceService second({[ids]{ids->second=std::this_thread::get_id();return true;},{}});
    first.configure(2,Backend::OpenCL);second.configure(3,Backend::OpenCL);
    const auto until=std::chrono::steady_clock::now()+std::chrono::seconds(5);
    while((!first.metrics().ready || !second.metrics().ready) && std::chrono::steady_clock::now()<until)
        std::this_thread::yield();
    REQUIRE(first.metrics().ready);REQUIRE(second.metrics().ready);
    CHECK(ids->first==ids->second);CHECK(ids->first!=std::this_thread::get_id());
    CHECK(first.metrics().coordinatorThreads==1);CHECK(second.metrics().coordinatorThreads==1);
    if(glob2::nativeThreadId()) {
        CHECK(first.metrics().coordinatorThreadId!=0);
        CHECK(first.metrics().coordinatorThreadId==second.metrics().coordinatorThreadId);
        CHECK(first.metrics().coordinatorThreadId!=glob2::nativeThreadId());
    }
    first.stop();CHECK_FALSE(first.metrics().running);CHECK(second.metrics().ready);
    second.stop();
}

TEST_CASE("pending required GPU work cancels at publication and drains through one worker recovery" * doctest::test_suite("GradientPipeline"))
{
    using namespace gradient_kernel;
    if constexpr(!GAGCore::ThreadSupport::available)return;
    struct Restore {Backend mode=backend();unsigned mask=readyPlans.load();~Restore(){setBackend(mode);readyPlans=mask;}} restore;
    setBackend(Backend::OpenCL);readyPlans=1u<<unsigned(requestedOpenCLPlan());
    for(unsigned scenario=0;scenario<4;++scenario) {
        struct State {
            std::atomic<bool> entered{false},onWorker{false},timedOut{false};
            std::atomic<unsigned> calls{0},cpu{0};
            std::atomic<std::uint16_t> privateReadback{77};
        };
        auto state=std::make_shared<State>();
        auto service=std::make_shared<GradientDeviceService>(GradientDeviceService::Hooks{
            []{return true;},[state](std::span<const BackendRequest> requests,Plan){
                ++state->calls;CHECK(requests.size()==1);CHECK(requests[0].gradient[0]==77);
                auto control=requests[0].requiredControl;REQUIRE(bool(control));
                state->entered=true;
                const auto until=std::chrono::steady_clock::now()+std::chrono::seconds(5);
                while(!control->shouldCancel(monotonicNs()) && std::chrono::steady_clock::now()<until)
                    control->waitFor(std::chrono::microseconds(100));
                state->timedOut=!control->shouldCancel(monotonicNs());
                // Simulate a pending driver read into independently owned staging.
                // The original seeds are never a driver destination.
                return false;
            }});
        service->configure(2,Backend::OpenCL);
        const auto readyUntil=std::chrono::steady_clock::now()+std::chrono::seconds(5);
        while(!service->metrics().ready && std::chrono::steady_clock::now()<readyUntil)std::this_thread::yield();
        REQUIRE(service->metrics().ready);
        TestGradientPipeline pipeline;pipeline.configure(1,2,1,[](auto&,auto&){FAIL("unexpected synchronous GPU re-entry");});
        pipeline.setDeviceService(service);
        pipeline.setAsyncWork([state](auto& job,PlanDecision decision){
            auto field=std::make_shared<OwnedGradientField>();field->inputs=state;
            field->session=job.owner->session();field->decision=decision;field->due=job.executorDue;
            field->data=std::move(job.data);field->costAt=[](const auto&,std::size_t){return LAND_STEPS;};
            field->cpu=[](auto& field){auto& state=*const_cast<State*>(static_cast<const State*>(field.inputs.get()));
                CHECK(field.data[0]==77);++state.cpu;state.onWorker=ComputeExecutor::workerSlot()!=0;field.data[0]=88;};
            return field;
        });
        auto* published=new std::uint16_t[1]{};
        pipeline.advance();pipeline.submit(&published,0,[](auto& job){job.data[0]=77;});
        const auto enteredUntil=std::chrono::steady_clock::now()+std::chrono::seconds(5);
        while(!state->entered && std::chrono::steady_clock::now()<enteredUntil)std::this_thread::yield();
        REQUIRE(state->entered);
        std::atomic<unsigned> unrelated{0};
        const ComputeExecutor::Group other{1,{[](void* value,std::size_t){++*static_cast<std::atomic<unsigned>*>(value);},&unrelated}};
        auto ticket=pipeline.compute().submit(std::span(&other,1));pipeline.compute().join(ticket);
        CHECK(unrelated==1);CHECK(published[0]==0);CHECK(state->cpu==0);
        if(scenario==0) {pipeline.advance();CHECK(published[0]==0);pipeline.advance();CHECK(published[0]==88);}
        else if(scenario==1) {
            unsigned snapshots=0;pipeline.visitPendingSnapshots([&](auto saved){++snapshots;CHECK(saved.data[0]==88);CHECK(saved.remaining==2);});
            CHECK(snapshots==1);CHECK(published[0]==0);pipeline.advance();pipeline.advance();CHECK(published[0]==88);
        }
        else if(scenario==2) {pipeline.reset();CHECK(published[0]==0);}
        else {service->configure(2,Backend::OpenCL);pipeline.finish();pipeline.advance();pipeline.advance();CHECK(published[0]==88);}
        CHECK_FALSE(state->timedOut);CHECK(state->calls==1);CHECK(state->cpu==1);CHECK(state->onWorker);
        // A late driver staging write cannot race or overwrite CPU publication.
        state->privateReadback=99;CHECK(published[0]==(scenario==2 ? 0 : 88));
        pipeline.reset();service->stop();delete[] published;
    }
}

TEST_CASE("bounded required device queue recovers excess fields exactly once" * doctest::test_suite("GradientPipeline"))
{
    using namespace gradient_kernel;
    if constexpr(!GAGCore::ThreadSupport::available)return;
    struct Gate {
        std::promise<void> entered,release;std::shared_future<void> released=release.get_future().share();
        std::atomic<bool> opened{false};std::atomic<unsigned> cpu{0},jobs{0},calls{0};std::uint64_t lastDue=0;
        void open(){if(!opened.exchange(true))release.set_value();}
    };
    auto gate=std::make_shared<Gate>();struct Release{std::shared_ptr<Gate> value;~Release(){value->open();}} cleanup{gate};
    auto entered=gate->entered.get_future();
    auto service=std::make_shared<GradientDeviceService>(GradientDeviceService::Hooks{[]{return true;},
        [gate](std::span<const BackendRequest> requests,Plan){
            if(gate->calls.fetch_add(1)==0){gate->entered.set_value();gate->released.wait();}
            CHECK(requests.size()<=8);
            for(const auto& request:requests){
                const auto& field=*static_cast<const OwnedGradientField*>(request.context);
                CHECK(field.due>=gate->lastDue);gate->lastDue=field.due;
                CHECK(request.gradient[0]==77);request.gradient[0]=177;
                if(request.executedOnDevice)*request.executedOnDevice=true;}return true;
        }});
    service->configure(2,Backend::OpenCL);
    const auto readyUntil=std::chrono::steady_clock::now()+std::chrono::seconds(5);
    while(!service->metrics().ready && std::chrono::steady_clock::now()<readyUntil)std::this_thread::yield();
    REQUIRE(service->metrics().ready);
    ComputeExecutor executor;executor.configure(2);auto session=std::make_shared<BackendSession>();
    struct Context {std::shared_ptr<OwnedGradientField> field;std::shared_ptr<GradientDeviceService> service;
		ComputeExecutor *executor;
		std::shared_ptr<Gate> gate;
	};
	std::array<Context, 67> contexts;
	for (unsigned i = 0; i < contexts.size(); ++i)
	{
		auto field = std::make_shared<OwnedGradientField>();
		field->session = session;
		field->decision = {Plan::Frozen8,
						   session->currentGeneration(),
						   {Backend::OpenCL, Algorithm::Frozen8, false}};
		field->due = i ? 1065 - i : 1000;
		field->data = std::make_unique<std::uint16_t[]>(1);
		field->data[0] = 77;
		field->inputs = gate;
		field->costAt = [](const auto &, std::size_t) { return LAND_STEPS; };
		field->cpu = [](auto &value)
		{
			CHECK(value.data[0] == 77);
			value.data[0] = 177;
			++const_cast<Gate *>(static_cast<const Gate *>(value.inputs.get()))->cpu;
		};
		contexts[i] = {field, service, &executor, gate};
	}
	struct BatchContext
	{
		std::array<Context, 67> *jobs;
		std::future<void> *entered;
	} batchContext{&contexts, &entered};
	const ComputeExecutor::Group group{67,
									   {[](void *value, std::size_t index)
										{
											auto &batch = *static_cast<BatchContext *>(value);
											auto &c = (*batch.jobs)[index];
											c.field->completion = c.executor->defer();
											if (!c.service->submit(c.field))
												c.service->recoverOnWorker(c.field);
											if (!index)
											{
												const auto result = batch.entered->wait_for(
													std::chrono::seconds(5));
												if (result != std::future_status::ready)
													c.gate->open();
												REQUIRE(result == std::future_status::ready);
											}
											++c.gate->jobs;
										},
										&batchContext},
									   ComputeExecutor::NoLane};
	auto ticket = executor.submit(std::span(&group, 1), 1000);
	struct Drain
	{
		ComputeExecutor &executor;
		ComputeExecutor::Batch &ticket;
		std::shared_ptr<Gate> gate;
		~Drain()
		{
			gate->open();
			try
			{
				executor.join(ticket);
			}
			catch (...)
			{
			}
		}
	} drain{executor, ticket, gate}; // Join before borrowed Contexts unwind on assertion failure.
	const auto filledUntil = std::chrono::steady_clock::now() + std::chrono::seconds(5);
	while (gate->jobs < contexts.size() && std::chrono::steady_clock::now() < filledUntil)
		std::this_thread::yield();
	if (gate->jobs != contexts.size())
		gate->open();
	REQUIRE(gate->jobs == contexts.size());
	CHECK(service->metrics().submitted == 65);
	CHECK(service->metrics().queued == 64);
	CHECK(service->metrics().budgetDeclines == 2);
	gate->open();
	executor.join(ticket);
	CHECK(gate->cpu == 2);
	for (unsigned i = 0; i < contexts.size(); ++i)
	{
		CHECK(contexts[i].field->data[0] == 177);
		CHECK(contexts[i].field->due == (i ? 1065 - i : 1000));
	}
	const auto doneUntil=std::chrono::steady_clock::now()+std::chrono::seconds(5);
    while(service->metrics().completed!=65 && std::chrono::steady_clock::now()<doneUntil)std::this_thread::yield();
    CHECK(service->metrics().completed==65);CHECK(service->metrics().fallbacks==2);
    CHECK(service->metrics().maxBatch==8);service->stop();
}

TEST_CASE("malformed captured configurations decline and recover once" * doctest::test_suite("GradientPipeline"))
{
    using namespace gradient_kernel;
    if constexpr(!GAGCore::ThreadSupport::available)return;
    std::atomic<unsigned> dispatched{0},recovered{0};
    auto service=std::make_shared<GradientDeviceService>(GradientDeviceService::Hooks{
        []{return true;},[&](auto,auto){++dispatched;return true;}});
    service->configure(2,Backend::OpenCL);
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(5);
    while(!service->metrics().ready && std::chrono::steady_clock::now()<deadline)std::this_thread::yield();
    REQUIRE(service->metrics().ready);
    ComputeExecutor executor;executor.configure(2);
    auto session=std::make_shared<BackendSession>();
    const std::array<Configuration,5> malformed{{
        {Backend::CPU,Algorithm::Buckets,false},
        {Backend::OpenCL,Algorithm::Jacobi,false},
        {Backend::Vulkan,Algorithm::Frozen8,false},
        {Backend::Count,Algorithm::Frozen8,false},
        {Backend::OpenCL,Algorithm(255),false}}};
    struct Context {ComputeExecutor* executor;std::shared_ptr<GradientDeviceService> service;
        std::shared_ptr<OwnedGradientField> field;};
    std::array<Context,5> contexts;
    for(std::size_t i=0;i<contexts.size();++i){
        auto field=std::make_shared<OwnedGradientField>();field->session=session;
        field->decision={Plan::Frozen8,session->currentGeneration(),malformed[i]};
        field->data=std::make_unique<std::uint16_t[]>(1);field->data[0]=77;
        field->costAt=[](const auto&,std::size_t){return LAND_STEPS;};
        field->inputs=std::shared_ptr<const void>(&recovered,[](const void*){});
        field->cpu=[](auto& value){
            CHECK(value.data[0]==77);value.data[0]=177;
            ++*const_cast<std::atomic<unsigned>*>(static_cast<const std::atomic<unsigned>*>(value.inputs.get()));
        };
        contexts[i]={&executor,service,field};
    }
    const ComputeExecutor::Group group{contexts.size(),{[](void* value,std::size_t index){
        auto& c=static_cast<Context*>(value)[index];c.field->completion=c.executor->defer();
        CHECK_FALSE(c.service->submit(c.field));
        CHECK(c.field->fallbackReason==GradientFallbackReason::InvalidRequest);
        CHECK_FALSE(c.field->admitted.load());
        c.service->recoverOnWorker(c.field);c.service->recoverOnWorker(c.field);
    },contexts.data()},ComputeExecutor::NoLane};
    auto ticket=executor.submit(std::span(&group,1),0);executor.join(ticket);
    CHECK(dispatched==0);CHECK(recovered==contexts.size());
    CHECK(service->metrics().submitted==0);CHECK(service->metrics().declined==contexts.size());
    for(const auto& c:contexts){CHECK(c.field->resolved.load());CHECK(c.field->data[0]==177);}
    service->stop();
}
