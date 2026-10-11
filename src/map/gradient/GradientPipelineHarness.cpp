// SPDX-License-Identifier: GPL-3.0-or-later
#include "Glob2Test.h"
#include "ScopedEnvironment.h"
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
#include "field/GradientBatchManifest.h"
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
        reset(); executor.configure(count+1, factory);
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
    session->establish(gradient_kernel::Family::Generic,1,gradient_kernel::Plan::Frozen8);
    TestGradientPipeline pipeline;
    pipeline.setBackendSession(session);
    auto* field=new std::uint16_t[1]{};
    for (unsigned workers : {0,2,4,0})
    {
        pipeline.configure(workers,1,1,[session](auto& job,auto& scratch) {
            if (scratch.backendSession!=session ||
                scratch.backendSession->decision(gradient_kernel::Family::Generic,1).plan!=gradient_kernel::Plan::Frozen8)
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

TEST_CASE("GradientPipeline/CPU and unknown automatic fields keep their original worker")
{
    using namespace gradient_kernel;
    for (const auto choice : {Backend::CPU, Backend::Automatic}) {
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
    session->establish(Family::Materials,1,Plan::CPU);
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

TEST_CASE("optional worker fixed-field bypass preserves validation and publication without device handoff" * doctest::test_suite("GradientPipeline"))
{
    using namespace gradient_kernel;
    if constexpr(!GAGCore::ThreadSupport::available)return;
    struct Restore {
        Backend mode=backend();unsigned mask=readyPlans.load();decltype(batchAccelerator) provider=batchAccelerator;
        std::string option=std::getenv("GLOB2_GRADIENT_WORKER_NOOP") ? std::getenv("GLOB2_GRADIENT_WORKER_NOOP") : "";
        ~Restore(){setBackend(mode);readyPlans=mask;batchAccelerator=provider;GAGCore::setProcessEnvironment("GLOB2_GRADIENT_WORKER_NOOP",option.c_str(),1);}
    } restore;
    GAGCore::setProcessEnvironment("GLOB2_GRADIENT_WORKER_NOOP","1",1);
    readyPlans=1u<<unsigned(requestedOpenCLPlan());setBackend(Backend::OpenCL);
    batchAccelerator=[](std::span<const BackendRequest>,Plan){return false;};
    const std::array<std::array<std::uint16_t,4>,3> inputs{{{{0,1,0,1}},{{0,65535,0,65535}},{{0,64000,1,0}}}};
    for(const auto& input:inputs) for(bool invalid:{false,true}) {
        if(invalid && !alreadyFixedGradient(input))continue;
        TestGradientPipeline pipeline;std::atomic<unsigned> validations{0},batches{0};
        std::thread::id prepared,validated;const auto owner=std::this_thread::get_id();
        pipeline.configure(1,2,4,[&](auto& job,auto&){
            ++validations;validated=std::this_thread::get_id();CHECK(alreadyFixedGradient(std::span(job.data.get(),4)));
            if(invalid)throw std::runtime_error("validated fixed input rejected");
        });
        pipeline.setBatchWork([&](auto jobs,auto){++batches;jobs.front()->data[1]=63000;});
        auto* field=new std::uint16_t[4]{7,7,7,7};
        pipeline.advance();pipeline.submit(&field,0,[&](auto& job){prepared=std::this_thread::get_id();std::copy(input.begin(),input.end(),job.data.get());});
        if(invalid)CHECK_THROWS_AS(pipeline.finish(),std::runtime_error);else CHECK_NOTHROW(pipeline.finish());
        CHECK(field[1]==7);pipeline.advance();CHECK(field[1]==7);
        if(invalid)CHECK_THROWS_AS(pipeline.advance(),std::runtime_error);
        else {pipeline.advance();CHECK(field[1]==(alreadyFixedGradient(input)?input[1]:63000));}
        CHECK(validations==unsigned(alreadyFixedGradient(input)));CHECK(batches==unsigned(!alreadyFixedGradient(input)));
        if(alreadyFixedGradient(input)){CHECK(prepared==validated);CHECK(validated!=owner);}
        CHECK(pipeline.cpuReason(GradientPipeline::CPUReason::Trivial)==unsigned(alreadyFixedGradient(input)&&!invalid));
        CHECK(pipeline.gpuCompleteFields()==0);
        pipeline.reset();delete[] field;
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

TEST_CASE("nullable GPU preparation recovers once on the original worker without device reentry" * doctest::test_suite("GradientPipeline"))
{
    using namespace gradient_kernel;
    if constexpr(!GAGCore::ThreadSupport::available)return;
    struct Restore {Backend mode=backend();unsigned mask=readyPlans.load();~Restore(){setBackend(mode);readyPlans=mask;}} restore;
    PipelineEnvironment noop("GLOB2_GRADIENT_WORKER_NOOP","0");
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

TEST_CASE("fixed-due GPU stalls demote accepted policy in background without diagnostic clocks" * doctest::test_suite("GradientPipeline"))
{
    using namespace gradient_kernel;
    if constexpr(!GAGCore::ThreadSupport::available)return;
    struct Diagnostics {
        std::string previous=std::getenv("GLOB2_GRADIENT_DIAGNOSTICS") ? std::getenv("GLOB2_GRADIENT_DIAGNOSTICS") : "";
        Diagnostics(){GAGCore::setProcessEnvironment("GLOB2_GRADIENT_DIAGNOSTICS","0",1);}
        ~Diagnostics(){GAGCore::setProcessEnvironment("GLOB2_GRADIENT_DIAGNOSTICS",previous.c_str(),1);}
    } diagnostics;
    struct Restore {Backend mode=backend();unsigned mask=readyPlans.load();~Restore(){setBackend(mode);readyPlans=mask;}} restore;
    const auto plan=requestedOpenCLPlan();readyPlans=1u<<unsigned(plan);setBackend(Backend::OpenCL);
    for(const unsigned phase:{0u,1u,2u}) {
        const bool stalled=phase!=0;
        struct Gate {
            std::promise<void> entered,release;std::shared_future<void> released=release.get_future().share();
            std::atomic<bool> opened{false};std::atomic<std::uint64_t> cleanupDeviceStamp{0};
            void open(){if(!opened.exchange(true))release.set_value();}
        };
        struct CleanupGate {
            std::shared_ptr<Gate> gate;std::weak_ptr<OwnedGradientField> field;
            ~CleanupGate(){
                if(auto owned=field.lock())gate->cleanupDeviceStamp=owned->deviceCompletedWallNs;
                gate->entered.set_value();gate->released.wait();
            }
        };
        auto gate=std::make_shared<Gate>();
        struct Release {std::shared_ptr<Gate> gate;~Release(){gate->open();}} cleanup{gate};
        auto entered=gate->entered.get_future();
        auto service=std::make_shared<GradientDeviceService>(GradientDeviceService::Hooks{
            []{return true;},[gate,phase](std::span<const BackendRequest> requests,Plan){
                if(phase!=2){gate->entered.set_value();gate->released.wait();}
                for(const auto& r:requests){r.gradient[0]=99;if(r.executedOnDevice)*r.executedOnDevice=true;}return true;
            }});
        service->configure(2,Backend::OpenCL);
        const auto until=std::chrono::steady_clock::now()+std::chrono::seconds(5);
        while(!service->metrics().ready && std::chrono::steady_clock::now()<until)std::this_thread::yield();
        REQUIRE(service->metrics().ready);
        TestGradientPipeline pipeline;pipeline.configure(1,1,1,[](auto&,auto&){FAIL("CPU path unexpected");});
        pipeline.setDeviceService(service);auto session=pipeline.session();session->configureLearning(true);
        const auto policy=session->learningPolicy();WorkloadKey key;
        key.width=key.height=1;key.cpuBuckets=64;key.threads=2;key.family=Family::Materials;key.limit=COST_LIMIT;
        policy->observeAccepted(key,Plan::CPU,1000000000000ull,1);REQUIRE(policy->qualify(key,plan,true));
        for(unsigned pair=0;pair<256;++pair) {
            std::optional<CpuSavingPolicy::ProbeTicket> ticket;
            for(unsigned i=0;i<CpuSavingPolicy::ProbePeriod && !ticket;++i)
                ticket=policy->beginAcceptedCapture(key,plan,1,1000000,100000000,1000000,false,0,41,true);
            REQUIRE(ticket);
            REQUIRE(policy->bindAcceptedReference(*ticket,41,1000000,true,true));
            REQUIRE(bool(policy->authorizeOptionalExecution(*ticket,41,1,100000000,false)));
            if(policy->finishAcceptedCapture(*ticket,{600000,1000000,600000,100000000,true,true,true}))break;
        }
        REQUIRE(policy->lookup(key).plan==plan);
        pipeline.setAsyncWork([gate,phase](auto& job,PlanDecision decision){
            auto field=std::make_shared<OwnedGradientField>();field->session=job.owner->session();field->decision=decision;
            if(phase==2){
                auto cleanup=std::make_shared<CleanupGate>();cleanup->gate=gate;cleanup->field=field;field->inputs=std::move(cleanup);
            }
            field->data=std::move(job.data);field->due=job.executorDue;
            field->costAt=[](const auto&,std::size_t){return LAND_STEPS;};field->cpu=[](auto&){FAIL("CPU recovery unexpected");};return field;
        });
        auto* published=new std::uint16_t[1]{};pipeline.advance();pipeline.submit(&published,0,[](auto& job){job.data[0]=77;});
        const auto entry=entered.wait_for(std::chrono::seconds(5));if(entry!=std::future_status::ready)gate->open();
        REQUIRE(entry==std::future_status::ready);
        if(!stalled) {gate->open();pipeline.finish();}
        if(phase==2){
            REQUIRE(gate->cleanupDeviceStamp.load()>0);
            CHECK(gate->cleanupDeviceStamp.load()<monotonicNs());
        }
        auto release=std::async(std::launch::async,[gate]{std::this_thread::sleep_for(std::chrono::milliseconds(20));gate->open();});
        pipeline.advance();release.get();CHECK(published[0]==99);
        if(stalled) {
            const auto stop=std::chrono::steady_clock::now()+std::chrono::seconds(5);
            while(policy->lookup(key).plan!=Plan::CPU && std::chrono::steady_clock::now()<stop)std::this_thread::yield();
            CHECK(policy->lookup(key).plan==Plan::CPU);CHECK(service->metrics().publicationStalls==1);
        } else {CHECK(policy->lookup(key).plan==plan);CHECK(service->metrics().publicationStalls==0);}
        pipeline.reset();service->stop();delete[] published;
    }
}

TEST_CASE("device registration stop and reconfigure never join a slow optional initializer" * doctest::test_suite("GradientPipeline"))
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

TEST_CASE("automatic learning follows background readiness without cached CPU fallback classification" * doctest::test_suite("GradientPipeline"))
{
    using namespace gradient_kernel;
    if constexpr(!GAGCore::ThreadSupport::available)return;
    if(!glob2::threadCpuNs())return;
    PipelineEnvironment tuning("GLOB2_GRADIENT_TUNING","1"),crossDue("GLOB2_GRADIENT_CROSS_DUE","0");
    struct Restore {Backend mode=backend();~Restore(){setBackend(mode);}} restore;
    setBackend(Backend::Automatic);
    for(bool succeeds : {false,true}) {
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
            [gate,succeeds]{gate->entered.set_value();gate->released.wait();return succeeds;},
            [](auto,auto){FAIL("unknown automatic classes must stay on CPU");return false;}});
        service->configure(2,Backend::Automatic);
        REQUIRE(entered.wait_for(std::chrono::seconds(5))==std::future_status::ready);
        CHECK_FALSE(service->learningActive());
        struct Field {std::uint16_t* data=new std::uint16_t[1]{};~Field(){delete[] data;}} field;
        TestGradientPipeline pipeline;
        pipeline.configure(0,1,1,[](auto& job,auto&){job.data[0]=77;});
        pipeline.session()->configureLearning(true);pipeline.setDeviceService(service);
        auto policy=pipeline.session()->learningPolicy();
        WorkloadKey key{1,1,BUCKETS,2,1,Family::Clear,0,0};key.limit=COST_LIMIT;
        auto submit=[&](bool expectedShape){
            pipeline.advance();pipeline.submit(&field.data,0,[&](auto& job){
                CHECK(job.captureSeedShape==expectedShape);job.data[0]=1;});
            pipeline.advance();CHECK(field.data[0]==77);
        };
        submit(false);
        service->recordAccepted(pipeline.session(),key,Plan::CPU,300,1);
        CHECK(policy->metrics().acceptedCpuNs==0);
        gate->open();
        const auto until=std::chrono::steady_clock::now()+std::chrono::seconds(5);
        while(!service->metrics().initializationNs &&
              std::chrono::steady_clock::now()<until)std::this_thread::yield();
        REQUIRE(service->metrics().initializationNs>0);
        if(succeeds){REQUIRE(service->metrics().ready);REQUIRE(service->metrics().tuningStartupClockValid);}
        else CHECK_FALSE(service->metrics().ready);
        CHECK(service->learningActive()==succeeds);
        // No refresh call: reserve must observe the changed background state.
        submit(succeeds);
        service->recordAccepted(pipeline.session(),key,Plan::CPU,300,2);
        if(succeeds) {
            while(policy->metrics().acceptedCpuNs!=300 && std::chrono::steady_clock::now()<until)std::this_thread::yield();
            CHECK(policy->metrics().acceptedCpuNs==300);
        } else CHECK(policy->metrics().acceptedCpuNs==0);
        service->stop();CHECK_FALSE(service->learningActive());submit(false);
        service->recordAccepted(pipeline.session(),key,Plan::CPU,100,3);
        CHECK(policy->metrics().acceptedCpuNs==(succeeds ? 300 : 0));
        pipeline.reset();
    }
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

TEST_CASE("deadline batch guard needs warm cadence and rejects acceleration unknown bounds and overflow" * doctest::test_suite("GradientPipeline"))
{
    using namespace gradient_kernel;
    GradientCadenceFloor cadence;GradientBatchBound bound;
    bound.maxElapsedNs=100;bound.completionMarginNs=50;
    for(unsigned tick=1;tick<16;++tick)cadence.record({tick,tick*1000,1000});
    CHECK_FALSE(cadence.valid());CHECK_FALSE(cadence.fits(18,0,1000,bound,16000));
    cadence.record({16,16000,1000});REQUIRE(cadence.valid());
    const auto revision=cadence.version();CHECK(cadence.fits(18,revision,1000,bound,16000));
    CHECK_FALSE(cadence.fits(16,revision,1000,bound,16000));
    CHECK_FALSE(cadence.fits(17,revision,1000,bound,16800));
    CHECK_FALSE(cadence.fits(18,revision,0,bound,16000));
    auto unknown=bound;unknown.maxElapsedNs=0;CHECK_FALSE(cadence.fits(18,revision,1000,unknown,16000));
    unknown.maxElapsedNs=UINT64_MAX;CHECK_FALSE(cadence.fits(18,revision,1000,unknown,16000));
    cadence.record({17,17000,200});CHECK(cadence.version()!=revision);
    CHECK_FALSE(cadence.fits(20,revision,1000,bound,17000));
    cadence.record({18,18000,0});CHECK_FALSE(cadence.valid());
    for(unsigned tick=19;tick<35;++tick)cadence.record({tick,UINT64_MAX-1,1000});
    CHECK_FALSE(cadence.fits(40,cadence.version(),1000,bound,0));
}

TEST_CASE("offline batch manifest binds exact configuration seed class and measured batch size" * doctest::test_suite("GradientPipeline"))
{
    using namespace gradient_kernel;using Json=nlohmann::json;
    OpenCLStatus backend;backend.device="fixture device";
    backend.deviceUUIDHex=std::string(32,'a');backend.deviceOrdinal=1;backend.deviceOrdinalKnown=true;
    backend.platform="platform";backend.platformVendor="vendor";backend.platformVersion="version";
    backend.deviceVendor="vendor";backend.driverVersion="driver";backend.deviceVersion="version";backend.openCLCVersion="version";
    const auto nativeIdentity=std::string(64,'a');
    Json entry={{"width",512u},{"height",512u},{"cpu_buckets",64u},{"threads",2u},{"batch",2u},
        {"limit",65534u},{"movement",0u},{"movement_modifiers",false},{"seed_density",0u},{"blocker_density",4u},
        {"family","clear"},{"plan",unsigned(Plan::Frozen8)},{"cost_revision",11u},{"cost_variant",0u},
        {"max_measured_elapsed_ns",1000000u},{"completion_margin_ns",100000u},{"measured_batches",8u}};
    Json manifest={{"schema",1u},{"native_binary_sha256",nativeIdentity},{"source","immutable offline fixture"},{"measurement","homogeneous-ready-batch"},
        {"backend",{{"device",backend.device},{"device_uuid_hex",backend.deviceUUIDHex},{"device_ordinal",backend.deviceOrdinal},
            {"platform",backend.platform},{"platform_vendor",backend.platformVendor},
            {"platform_version",backend.platformVersion},{"device_vendor",backend.deviceVendor},{"driver_version",backend.driverVersion},
            {"device_version",backend.deviceVersion},{"opencl_c_version",backend.openCLCVersion},{"check_interval",backend.checkInterval},{"poll_micros",backend.pollMicros},
            {"active_epoch",false},{"uniform_metadata",false},{"device_profiling",false},{"parity_bound",false},{"direct_seed_upload",false}}},
        {"profiles",Json::array({entry})}};
    const auto text=manifest.dump();const auto parsed=GradientBatchManifest::parse(text,backend,false,nativeIdentity);
    REQUIRE(parsed.count==1);CHECK(parsed.bounds[0].workload.batch==2);CHECK(parsed.bounds[0].costRevision==11);
    for(unsigned mode:{1u,2u}){auto diagnosticBackend=backend;diagnosticBackend.apiCpuMode=mode;
        CHECK_THROWS_AS(GradientBatchManifest::parse(text,diagnosticBackend,false,nativeIdentity),std::invalid_argument);}
    CHECK(parsed.hash==gradientManifestHash(text));CHECK(parsed.sourceHash==gradientManifestHash("immutable offline fixture"));
    CHECK_THROWS_AS(GradientBatchManifest::parse(text,backend,false,std::string(64,'b')),std::invalid_argument);
    auto changed=manifest;changed["profiles"][0]["batch"]=1u;
    CHECK_THROWS_AS(GradientBatchManifest::parse(changed.dump(),backend,false,nativeIdentity),std::invalid_argument);
    changed=manifest;changed["profiles"][0]["family"]="materials";
    CHECK_THROWS_AS(GradientBatchManifest::parse(changed.dump(),backend,false,nativeIdentity),std::invalid_argument);
    changed=manifest;changed["profiles"][0]["measured_batches"]=7u;
    CHECK_THROWS_AS(GradientBatchManifest::parse(changed.dump(),backend,false,nativeIdentity),std::invalid_argument);
    changed=manifest;changed["backend"]["check_interval"]=backend.checkInterval+1u;
    CHECK_THROWS_AS(GradientBatchManifest::parse(changed.dump(),backend,false,nativeIdentity),std::invalid_argument);
    changed=manifest;changed["backend"]["driver_version"]="different";
    CHECK_THROWS_AS(GradientBatchManifest::parse(changed.dump(),backend,false,nativeIdentity),std::invalid_argument);
    changed=manifest;changed["backend"]["device_uuid_hex"]=std::string(32,'b');
    CHECK_THROWS_AS(GradientBatchManifest::parse(changed.dump(),backend,false,nativeIdentity),std::invalid_argument);
    changed=manifest;changed["backend"]["device_ordinal"]=0u;
    CHECK_THROWS_AS(GradientBatchManifest::parse(changed.dump(),backend,false,nativeIdentity),std::invalid_argument);
    changed=manifest;changed["backend"].erase("device_uuid_hex");
    CHECK_THROWS(GradientBatchManifest::parse(changed.dump(),backend,false,nativeIdentity));
    auto unknownDevice=backend;unknownDevice.deviceUUIDHex.clear();
    CHECK_THROWS_AS(GradientBatchManifest::parse(text,unknownDevice,false,nativeIdentity),std::invalid_argument);
    unknownDevice=backend;unknownDevice.deviceOrdinalKnown=false;
    CHECK_THROWS_AS(GradientBatchManifest::parse(text,unknownDevice,false,nativeIdentity),std::invalid_argument);
    changed=manifest;changed["backend"]["direct_seed_upload"]=true;
    CHECK_THROWS_AS(GradientBatchManifest::parse(changed.dump(),backend,false,nativeIdentity),std::invalid_argument);
    auto directBackend=backend;directBackend.directSeedUpload=true;
    CHECK(GradientBatchManifest::parse(changed.dump(),directBackend,false,nativeIdentity).count==1);
    changed=manifest;changed["backend"].erase("direct_seed_upload");
    CHECK_THROWS(GradientBatchManifest::parse(changed.dump(),backend,false,nativeIdentity));
    changed=manifest;changed["profiles"].push_back(entry);
    CHECK_THROWS_AS(GradientBatchManifest::parse(changed.dump(),backend,false,nativeIdentity),std::invalid_argument);
    CHECK_THROWS_AS(GradientBatchManifest::parse(std::string(GradientBatchManifest::MaxBytes+1,' '),backend,false,nativeIdentity),std::invalid_argument);
}

TEST_CASE("ready cross-due batches need exact profile cadence seed metadata and automatic batch acceptance" * doctest::test_suite("GradientPipeline"))
{
    using namespace gradient_kernel;using Json=nlohmann::json;
    if constexpr(!GAGCore::ThreadSupport::available)return;
    PipelineEnvironment enabled("GLOB2_GRADIENT_CROSS_DUE","1"),batch("GLOB2_GRADIENT_BATCH","8");
    // Automatic work needs its accepted singleton policy and valid enclosing
    // CPU accounting. The unaccepted batch2 profile must stay independently CPU.
    PipelineEnvironment tuning("GLOB2_GRADIENT_TUNING","1");
    if(gradientNativeBuildIdentity().empty())return;
    const auto path=std::filesystem::temp_directory_path()/("glob2-ready-batch-"+std::to_string(monotonicNs())+".json");
    struct File {std::filesystem::path path;~File(){std::error_code error;std::filesystem::remove(path,error);}} file{path};
    OpenCLStatus backend;const auto plan=requestedOpenCLPlan();backend.device="fixture device";
    backend.deviceUUIDHex=std::string(32,'a');backend.deviceOrdinalKnown=true;
    backend.platform="platform";backend.platformVendor="vendor";backend.platformVersion="version";
    backend.deviceVendor="vendor";backend.driverVersion="driver";backend.deviceVersion="version";backend.openCLCVersion="version";
    Json entry={{"width",1u},{"height",1u},{"cpu_buckets",64u},{"threads",2u},{"batch",2u},{"limit",65534u},
        {"movement",0u},{"movement_modifiers",false},{"seed_density",16u},{"blocker_density",0u},{"family","clear"},
        {"plan",unsigned(plan)},{"cost_revision",42u},{"cost_variant",7u},{"max_measured_elapsed_ns",1000u},
        {"completion_margin_ns",1000u},{"measured_batches",8u}};
    Json manifest={{"schema",1u},{"native_binary_sha256",gradientNativeBuildIdentity()},{"source","ready fake-driver fixture"},{"measurement","homogeneous-ready-batch"},
        {"backend",{{"device",backend.device},{"device_uuid_hex",backend.deviceUUIDHex},{"device_ordinal",backend.deviceOrdinal},
            {"platform",backend.platform},{"platform_vendor",backend.platformVendor},
            {"platform_version",backend.platformVersion},{"device_vendor",backend.deviceVendor},{"driver_version",backend.driverVersion},
            {"device_version",backend.deviceVersion},{"opencl_c_version",backend.openCLCVersion},{"check_interval",backend.checkInterval},{"poll_micros",backend.pollMicros},
            {"active_epoch",backend.activeEpoch},{"uniform_metadata",backend.uniformMetadata},{"device_profiling",backend.deviceProfiling},
            {"parity_bound",backend.parityBound},{"direct_seed_upload",backend.directSeedUpload}}},{"profiles",Json::array({entry})}};
    {std::ofstream out(path);out<<manifest.dump();REQUIRE(bool(out));}
    const auto pathString=path.string();PipelineEnvironment profile("GLOB2_GRADIENT_BATCH_PROFILE",pathString.c_str());
    for(unsigned scenario=0;scenario<4;++scenario) {
        struct Gate {std::promise<void> entered,release;std::shared_future<void> released=release.get_future().share();
            std::atomic<bool> opened{false};void open(){if(!opened.exchange(true))release.set_value();}};
        auto gate=std::make_shared<Gate>();struct Release {std::shared_ptr<Gate> gate;~Release(){gate->open();}} cleanup{gate};
        std::atomic<unsigned> calls{0};auto entered=gate->entered.get_future();
        auto service=std::make_shared<GradientDeviceService>(GradientDeviceService::Hooks{[]{return true;},
            [gate,&calls](std::span<const BackendRequest> requests,Plan){
                if(calls.fetch_add(1)==0){gate->entered.set_value();gate->released.wait();}
                for(const auto& request:requests){request.gradient[0]=99;if(request.executedOnDevice)*request.executedOnDevice=true;}return true;
            },[backend]{return backend;}});
        service->configure(2,scenario==3 ? Backend::Automatic : Backend::OpenCL);
        const auto until=std::chrono::steady_clock::now()+std::chrono::seconds(5);
        while(!service->metrics().ready && std::chrono::steady_clock::now()<until)std::this_thread::yield();
        REQUIRE(service->metrics().ready);REQUIRE(service->metrics().crossDueReady);
        for(unsigned tick=1;tick<=17;++tick)service->recordCadence({tick,monotonicNs(),1000000000ull});
        while(!service->metrics().cadenceFloorNs && std::chrono::steady_clock::now()<until)std::this_thread::yield();
        REQUIRE(service->metrics().cadenceFloorNs==1000000000ull);
        ComputeExecutor executor;executor.configure(2);auto session=std::make_shared<BackendSession>();
        WorkloadKey singleton{1,1,64,2,1,Family::Clear,16,0};
        if(scenario==3) {
            session->configureLearning(true);const auto policy=session->learningPolicy();
            policy->observeAccepted(singleton,Plan::CPU,1000000000000ull,1);
            REQUIRE(policy->qualify(singleton,plan,true));
            for(unsigned pair=0;pair<256 && policy->lookup(singleton).plan==Plan::CPU;++pair) {
                std::optional<CpuSavingPolicy::ProbeTicket> ticket;
                for(unsigned attempt=0;attempt<CpuSavingPolicy::ProbePeriod && !ticket;++attempt)
                    ticket=policy->beginAcceptedCapture(singleton,plan,1,1000000,100000000,1000000,false,0,session->currentGeneration(),true);
                REQUIRE(bool(ticket));
                REQUIRE(policy->bindAcceptedReference(*ticket,session->currentGeneration(),1000000,true,true));
                REQUIRE(bool(policy->authorizeOptionalExecution(*ticket,session->currentGeneration(),1,100000000,false)));
                policy->finishAcceptedCapture(*ticket,{100000,1000000,600000,100000000,true,true,true});
            }
            REQUIRE(policy->lookup(singleton).plan==plan);
            auto unaccepted=singleton;unaccepted.batch=2;CHECK(policy->lookup(unaccepted).plan==Plan::CPU);
        }
        auto costOwner=std::make_shared<int>(0);
        std::array<std::shared_ptr<OwnedGradientField>,3> fields;
        struct Context {std::shared_ptr<GradientDeviceService> service;std::shared_ptr<OwnedGradientField> field;ComputeExecutor* executor;};
        std::array<Context,3> contexts;std::array<ComputeExecutor::Batch,3> tickets;
        struct Drain {ComputeExecutor& executor;std::array<ComputeExecutor::Batch,3>& tickets;std::shared_ptr<Gate> gate;
            ~Drain(){gate->open();for(auto& ticket:tickets)try{executor.join(ticket);}catch(...){}}
        } drain{executor,tickets,gate}; // Borrowed hook/context storage survives assertion unwind.
        for(unsigned i=0;i<fields.size();++i) {
            auto field=std::make_shared<OwnedGradientField>();field->session=session;field->decision={plan,0,session->currentGeneration()};
            if(scenario==3)field->decision.version=session->learningPolicy()->lookup(singleton).version;
            field->preparationCpuClockValid=true; // Fixture has a known zero-cost preparer.
            field->data=std::make_unique<std::uint16_t[]>(1);field->data[0]=77;field->grid={1,1};
            field->identity={costOwner,7,42,true};field->limit=65534;field->family=Family::Clear;field->due=10+i;field->publicationTick=20+i;
            field->workload={1,1,64,2,1,Family::Clear,16,0};field->seedShape={1,1,0,scenario!=1};
            field->costAt=[](const auto&,std::size_t){return LAND_STEPS;};field->cpu=[](auto&){FAIL("unexpected fallback");};
            fields[i]=field;contexts[i]={service,field,&executor};
            const ComputeExecutor::Group group{1,{[](void* value,std::size_t){
                auto& context=*static_cast<Context*>(value);context.field->completion=context.executor->defer();
                REQUIRE(context.service->submit(context.field));},&contexts[i]},ComputeExecutor::NoLane};
            tickets[i]=executor.submit(std::span(&group,1),field->due);
            if(i==0){const auto result=entered.wait_for(std::chrono::seconds(5));if(result!=std::future_status::ready)gate->open();REQUIRE(result==std::future_status::ready);}
        }
        while(service->metrics().submitted<3 && std::chrono::steady_clock::now()<until)std::this_thread::yield();
        if(service->metrics().submitted!=3)gate->open();
        REQUIRE(service->metrics().submitted==3);
        if(scenario==2)service->recordCadence({18,monotonicNs(),100});
        gate->open();for(auto& ticket:tickets)executor.join(ticket);
        for(const auto& field:fields)CHECK(field->data[0]==99);
        // Unknown seed shape, faster cadence, and unaccepted automatic batch2
        // each remain singleton although batch1 jobs were selected for GPU.
        CHECK(calls.load()==(scenario==0 ? 2u : 3u));
        CHECK(service->metrics().crossDueBatches==(scenario==0 ? 1u : 0u));
        service->stop();
    }
}

TEST_CASE("accepted batch-only offers execute ready members or recover without waiting to fill" * doctest::test_suite("GradientPipeline"))
{
    using namespace gradient_kernel;
    if constexpr(!GAGCore::ThreadSupport::available)return;
    PipelineEnvironment tuning("GLOB2_GRADIENT_TUNING","1"),crossDue("GLOB2_GRADIENT_CROSS_DUE","0"),batch("GLOB2_GRADIENT_BATCH","8");
    const auto plan=requestedOpenCLPlan();
    for(unsigned scenario=0;scenario<3;++scenario) {
        auto session=std::make_shared<BackendSession>();session->configureLearning(true);
        const auto policy=session->learningPolicy();
        WorkloadKey singleton{1,1,64,3,1,Family::Clear,16,0};
        auto accepted=singleton;accepted.batch=2;
        policy->observeAccepted(singleton,Plan::CPU,1000000000000ull,1);
        REQUIRE(policy->qualify(accepted,plan,true));
        for(unsigned pair=0;pair<256 && policy->lookup(accepted).plan==Plan::CPU;++pair) {
            std::optional<CpuSavingPolicy::ProbeTicket> ticket;
            for(unsigned attempt=0;attempt<CpuSavingPolicy::ProbePeriod && !ticket;++attempt)
                ticket=policy->beginAcceptedCapture(accepted,plan,1,1000000,100000000,1000000,false,0,session->currentGeneration(),true);
            REQUIRE(bool(ticket));
            REQUIRE(policy->bindAcceptedReference(*ticket,session->currentGeneration(),1000000,true,true));
            REQUIRE(bool(policy->authorizeOptionalExecution(*ticket,session->currentGeneration(),1,100000000,false)));
            policy->finishAcceptedCapture(*ticket,{100000,1000000,600000,100000000,true,true,true});
        }
        REQUIRE(policy->lookup(accepted).plan==plan);CHECK(policy->lookup(singleton).plan==Plan::CPU);
        struct Gate {std::promise<void> entered,release;std::shared_future<void> released=release.get_future().share();
            std::atomic<bool> opened{false};void open(){if(!opened.exchange(true))release.set_value();}};
        auto gate=std::make_shared<Gate>();auto entered=gate->entered.get_future();
        auto blocker=std::make_shared<GradientDeviceService>(GradientDeviceService::Hooks{[]{return true;},
            [gate](std::span<const BackendRequest> requests,Plan){gate->entered.set_value();gate->released.wait();
                for(const auto& request:requests)request.gradient[0]=99;return true;}});
        std::atomic<unsigned> calls{0},actualCount{0},recoveries{0};
        auto service=std::make_shared<GradientDeviceService>(GradientDeviceService::Hooks{[]{return true;},
            [&calls,&actualCount](std::span<const BackendRequest> requests,Plan){++calls;actualCount=unsigned(requests.size());
                for(const auto& request:requests){request.gradient[0]=99;if(request.executedOnDevice)*request.executedOnDevice=true;}return true;}});
        blocker->configure(3,Backend::OpenCL);service->configure(3,Backend::Automatic);
        auto until=std::chrono::steady_clock::now()+std::chrono::seconds(5);
        while((!service->metrics().ready || !blocker->metrics().ready) && std::chrono::steady_clock::now()<until)std::this_thread::yield();
        REQUIRE(service->metrics().ready);REQUIRE(blocker->metrics().ready);
        for(unsigned tick=1;tick<=17;++tick)service->recordCadence({tick,monotonicNs(),1000000000ull});
        while(!service->metrics().cadenceFloorNs && std::chrono::steady_clock::now()<until)std::this_thread::yield();
        REQUIRE(service->metrics().cadenceFloorNs==1000000000ull);
        ComputeExecutor executor;executor.configure(3);auto costOwner=std::make_shared<int>(0);
        struct Context {ComputeExecutor* executor;std::shared_ptr<GradientDeviceService> service;
            std::shared_ptr<OwnedGradientField> field;std::atomic<unsigned>* recoveries;bool choose=false;};
        std::array<Context,3> contexts;
        for(unsigned i=0;i<contexts.size();++i) {
            auto field=std::make_shared<OwnedGradientField>();field->session=session;field->decision={plan,0,session->currentGeneration()};
            field->data=std::make_unique<std::uint16_t[]>(1);field->data[0]=77;field->grid={1,1};field->limit=65534;
            field->preparationCpuClockValid=true;field->family=Family::Clear;field->workload=singleton;field->seedShape={1,1,0,true};
            field->identity={costOwner,7,42,true};field->costAt=[](const auto&,std::size_t){return LAND_STEPS;};
            field->due=10+i;field->publicationTick=20+i;
            field->inputs=std::make_shared<std::atomic<unsigned>*>(&recoveries);
            field->cpu=[](auto& value){auto counter=static_cast<std::atomic<unsigned>* const*>(value.inputs.get());
                (*counter)->fetch_add(1);value.data[0]=88;};
            contexts[i]={&executor,i?service:blocker,field,&recoveries,bool(i)};
        }
        std::array<ComputeExecutor::Batch,2> tickets;
        struct Drain {ComputeExecutor& executor;std::array<ComputeExecutor::Batch,2>& tickets;std::shared_ptr<Gate> gate;
            ~Drain(){gate->open();for(auto& ticket:tickets)try{executor.join(ticket);}catch(...){}}
        } drain{executor,tickets,gate};
        const auto invoke=+[](void* value,std::size_t index){auto& context=static_cast<Context*>(value)[index];
            if(context.choose){const auto choice=context.service->acceptedBatchChoice(context.field->session,context.field->workload);
                REQUIRE(bool(choice));CHECK(choice.minimumCount==2);context.field->decision=choice.decision;
                context.field->minimumBatchCount=choice.minimumCount;context.field->offeredBatchElapsedNs=choice.maximumElapsedNs;}
            context.field->completion=context.executor->defer();REQUIRE(context.service->submit(context.field));};
        const ComputeExecutor::Group first{1,{invoke,contexts.data()},ComputeExecutor::NoLane};
        tickets[0]=executor.submit(std::span(&first,1),10);
        const auto enteredStatus=entered.wait_for(std::chrono::seconds(5));if(enteredStatus!=std::future_status::ready)gate->open();
        REQUIRE(enteredStatus==std::future_status::ready);
        const unsigned count=scenario==1?1:2;
        const ComputeExecutor::Group group{count,{invoke,contexts.data()+1},ComputeExecutor::NoLane};
        tickets[1]=executor.submit(std::span(&group,1),11);
        until=std::chrono::steady_clock::now()+std::chrono::seconds(5);
        while(service->metrics().submitted<count && std::chrono::steady_clock::now()<until)std::this_thread::yield();
        REQUIRE(service->metrics().submitted==count);CHECK_FALSE(executor.finished(tickets[1]));
        // Deferred accelerator fields leave workers free for independent work.
        std::atomic<unsigned> unrelated{0};const ComputeExecutor::Group independent{1,{+[](void* value,std::size_t){
            static_cast<std::atomic<unsigned>*>(value)->fetch_add(1);},&unrelated},ComputeExecutor::NoLane};
        auto independentTicket=executor.submit(std::span(&independent,1),9);executor.join(independentTicket);CHECK(unrelated.load()==1);
        const auto windows=service->metrics().cpuEnvelopeWindows;
        if(scenario==2)service->recordCadence({18,monotonicNs(),100});
        gate->open();for(auto& ticket:tickets)executor.join(ticket);
        CHECK(calls.load()==(scenario==0?1u:0u));CHECK(actualCount.load()==(scenario==0?2u:0u));
        CHECK(recoveries.load()==(scenario==0?0u:count));
        for(unsigned i=1;i<=count;++i){CHECK(contexts[i].field->data[0]==(scenario==0?99:88));
            CHECK(contexts[i].field->due==10+i);CHECK(contexts[i].field->publicationTick==20+i);
            CHECK(contexts[i].field->fallbackReason==(scenario==0?GradientFallbackReason::None:GradientFallbackReason::BatchNotReady));}
        if(scenario!=0)CHECK(service->metrics().cpuEnvelopeWindows==windows);
        CHECK(service->metrics().crossDueBatches==(scenario==0?1u:0u));
        service->stop();blocker->stop();
    }
}

TEST_CASE("CPU envelope bridge initializes on CPU worker and deduplicates actual owner aliases" * doctest::test_suite("GradientPipeline"))
{
    if constexpr(!GAGCore::ThreadSupport::available)return;
    if(!glob2::nativeThreadId())return; // Native role identity is Linux-only today.
    using namespace gradient_kernel;
    PipelineEnvironment enabled("GLOB2_GRADIENT_CPU_ENVELOPE","1");
    // Lookup/owner registration cannot create a registry. This also holds when
    // another test has already initialized the process-owned registry.
    const auto initially=glob2::cpuEnvelopeRegistry();
    const auto before=openCLStatus().hostBytes;
    const auto ownerAttempt=glob2::registerCpuEnvelopeThread(glob2::CpuThreadRole::Owner);
    if(!initially){CHECK_FALSE(ownerAttempt);CHECK_FALSE(glob2::cpuEnvelopeRegistry());CHECK(openCLStatus().hostBytes==before);}
    ComputeExecutor executor;executor.configure(2);
    const auto until=std::chrono::steady_clock::now()+std::chrono::seconds(5);
    std::vector<std::uint64_t> ids;
    do {ids=executor.threadIds();if(ids.size()>1 && ids[1])break;std::this_thread::yield();}
    while(std::chrono::steady_clock::now()<until);
    REQUIRE(ids.size()==2);REQUIRE(ids[1]!=0);
    auto registry=glob2::cpuEnvelopeRegistry();REQUIRE(registry);
    REQUIRE(glob2::registerCpuEnvelopeThread(glob2::CpuThreadRole::Owner));
    REQUIRE(glob2::registerCpuEnvelopeThread(glob2::CpuThreadRole::OtherOwned));
    const auto complete=[](glob2::ProcessCpuEnvelope& envelope){
        for(unsigned i=0;i<4;++i)envelope.advance();
        if(envelope.finish())for(unsigned i=0;i<4;++i)envelope.advance();
    };
    glob2::ProcessCpuEnvelope live(*registry);complete(live);
    if(live.metrics().valid){
        unsigned owners=0,workers=0;
        for(std::size_t i=0;i<live.metrics().threadCount;++i){
            const auto& thread=live.metrics().threads[i];
            if(thread.tid==glob2::nativeThreadId()){
                ++owners;CHECK((thread.roles&(1u<<unsigned(glob2::CpuThreadRole::Owner)))!=0);
                CHECK((thread.roles&(1u<<unsigned(glob2::CpuThreadRole::OtherOwned)))!=0);
            }
            if(thread.tid==ids[1]){++workers;CHECK((thread.roles&(1u<<unsigned(glob2::CpuThreadRole::Worker)))!=0);}
        }
        CHECK(owners==1);CHECK(workers==1);
        CHECK(live.metrics().processCpuNs==live.metrics().knownInnerCpuNs+live.metrics().unknownUpperCpuNs);
        CHECK_FALSE(live.metrics().attributionComplete);
    }
    executor.configure(1); // Worker TLS leases retire before native exit.
    glob2::ProcessCpuEnvelope retired(*registry);complete(retired);
    for(std::size_t i=0;i<retired.metrics().threadCount;++i)CHECK(retired.metrics().threads[i].tid!=ids[1]);
}

TEST_CASE("required GPU envelopes are opt-in enclosing diagnostics and release probe storage" * doctest::test_suite("GradientPipeline"))
{
    using namespace gradient_kernel;
    if constexpr(!GAGCore::ThreadSupport::available)return;
    struct Restore {Backend mode=backend();unsigned mask=readyPlans.load();~Restore(){setBackend(mode);readyPlans=mask;}} restore;
    readyPlans=1u<<unsigned(requestedOpenCLPlan());setBackend(Backend::OpenCL);
    for(bool enabled:{false,true}){
        PipelineEnvironment envelope("GLOB2_GRADIENT_CPU_ENVELOPE",enabled ? "1" : "0");
        auto service=std::make_shared<GradientDeviceService>(GradientDeviceService::Hooks{[]{return true;},
            [](std::span<const BackendRequest> requests,Plan){
                for(const auto& request:requests){request.gradient[0]=91;if(request.executedOnDevice)*request.executedOnDevice=true;}return true;
            }});
        service->configure(2,Backend::OpenCL);
        const auto until=std::chrono::steady_clock::now()+std::chrono::seconds(5);
        while(!service->metrics().ready && std::chrono::steady_clock::now()<until)std::this_thread::yield();
        REQUIRE(service->metrics().ready);CHECK(service->metrics().cpuEnvelopeRequested==enabled);
        const auto probeBefore=openCLProbeBytes();
        TestGradientPipeline pipeline;pipeline.configure(1,1,1,[](auto&,auto&){FAIL("unexpected synchronous GPU work");});
        pipeline.setDeviceService(service);pipeline.setAsyncWork([](auto& job,PlanDecision decision){
            auto field=std::make_shared<OwnedGradientField>();field->session=job.owner->session();field->decision=decision;
            field->due=job.executorDue;field->data=std::move(job.data);field->costAt=[](const auto&,std::size_t){return LAND_STEPS;};
            field->cpu=[](auto&){FAIL("unexpected fallback");};return field;
        });
        auto* published=new std::uint16_t[1]{};
        pipeline.advance();pipeline.submit(&published,0,[](auto& job){job.data[0]=7;});
        // This fixture covers successful envelopes. Advancing immediately to
        // publication may cancel an unsubmitted/pending field and legitimately
        // recover on CPU; that path has its own cancellation fixture.
        while(service->metrics().completed+service->metrics().fallbacks!=1 &&
              std::chrono::steady_clock::now()<until)std::this_thread::yield();
        REQUIRE(service->metrics().completed==1);
        pipeline.advance();
        CHECK(published[0]==91);pipeline.reset();
        // Required completion is independent of subsequent diagnostic sampling.
        // Wait for the background diagnostic here solely to inspect the fixture.
        while(service->metrics().completed!=1 && std::chrono::steady_clock::now()<until)std::this_thread::yield();
        const auto metrics=service->metrics();REQUIRE(metrics.completed==1);
        if(enabled){
            CHECK(metrics.cpuEnvelopeRegistryReady);
            CHECK(metrics.cpuEnvelopeWindows+metrics.cpuEnvelopeInvalid+metrics.cpuEnvelopeDeclines==1);
            CHECK(metrics.cpuEnvelopeProcessNs==metrics.cpuEnvelopeKnownInnerNs+metrics.cpuEnvelopeUnknownUpperNs);
        }else{
            CHECK(metrics.cpuEnvelopeWindows==0);CHECK(metrics.cpuEnvelopeInvalid==0);CHECK(metrics.cpuEnvelopeDeclines==0);
            CHECK(metrics.cpuEnvelopeSamplerNs==0);
        }
        while(openCLProbeBytes()!=probeBefore && std::chrono::steady_clock::now()<until)std::this_thread::yield();
        CHECK(openCLProbeBytes()==probeBefore);service->stop();delete[] published;
    }
}

TEST_CASE("invalid CPU clocks preserve exact execution without fabricated automatic credits" * doctest::test_suite("GradientPipeline"))
{
    using namespace gradient_kernel;
    if constexpr(!GAGCore::ThreadSupport::available)return;
    if(!glob2::threadCpuNs())return; // Offline qualification itself needs a native clock.
    PipelineEnvironment diagnostics("GLOB2_GRADIENT_DIAGNOSTICS","1");
    PipelineEnvironment tuning("GLOB2_GRADIENT_TUNING","1");
    PipelineEnvironment crossDue("GLOB2_GRADIENT_CROSS_DUE","0");
    PipelineEnvironment noop("GLOB2_GRADIENT_WORKER_NOOP","0");
    struct Restore {Backend mode=backend();unsigned mask=readyPlans.load();~Restore(){setBackend(mode);readyPlans=mask;}} restore;
    const auto plan=requestedOpenCLPlan();readyPlans=1u<<unsigned(plan);
    for(unsigned phase=0;phase<7;++phase){
        // Worker clock: unavailable/reversed; coordinator clock: unavailable/
        // reversed. A device result already committed must never be rerun.
        const auto mode=phase==3 || phase==4 ? Backend::OpenCL : Backend::Automatic;
        setBackend(mode);
        struct Counts {mutable std::atomic<unsigned> cpu{0},gpu{0};std::atomic<std::uint64_t> workerClock{1000000000},serviceClock{1000000000};std::atomic<unsigned> clockReads{0};};
        auto counts=std::make_shared<Counts>();
        GradientDeviceService::Hooks hooks;
        hooks.initialize=[]{return true;};
        hooks.execute=[counts](std::span<const BackendRequest> requests,Plan){
            ++counts->gpu;for(const auto& r:requests){CHECK(r.gradient[0]==77);r.gradient[0]=91;
                if(r.executedOnDevice)*r.executedOnDevice=true;}return true;
        };
        if(phase>=2)hooks.cpuClock=[counts,phase]() -> std::uint64_t {
            if(phase==6){const auto read=counts->clockReads.fetch_add(1);return read==2 ? 0 : (read+1)*1000000ull;}
            return phase==2 || phase==3 ? 0ull : counts->serviceClock.fetch_sub(1000);
        };
        auto service=std::make_shared<GradientDeviceService>(std::move(hooks));service->configure(2,mode);
        const auto until=std::chrono::steady_clock::now()+std::chrono::seconds(5);
        while(!service->metrics().ready && std::chrono::steady_clock::now()<until)std::this_thread::yield();
        REQUIRE(service->metrics().ready);
        TestGradientPipeline pipeline;pipeline.configure(1,1,1,[counts](auto& job,auto&){
            CHECK(job.data[0]==77);job.data[0]=91;++counts->cpu;
        });
        // Keep these clock-failure scenarios eligible under the real singleton
        // deadline gate. Empty owner advances precede service attachment, so
        // they cannot overwrite the explicit warm cadence below.
        for(unsigned tick=0;tick<18;++tick)pipeline.advance();
        if(phase<2)pipeline.setCpuClockForTesting([counts,phase]{return phase==0 ? 0ull : counts->workerClock.fetch_sub(1000);});
        pipeline.setDeviceService(service);const auto session=pipeline.session();session->configureLearning(true);
        const auto policy=session->learningPolicy();WorkloadKey key;
        key.width=key.height=1;key.cpuBuckets=64;key.threads=2;key.family=Family::Materials;key.limit=COST_LIMIT;
        policy->observeAccepted(key,Plan::CPU,1000000000000ull,1);REQUIRE(policy->qualify(key,plan,true));
        for(unsigned pair=0;pair<256;++pair){
            std::optional<CpuSavingPolicy::ProbeTicket> ticket;
            for(unsigned i=0;i<CpuSavingPolicy::ProbePeriod && !ticket;++i)
                ticket=policy->beginAcceptedCapture(key,plan,1,1000000,100000000,1000000,false,0,41,true);
            REQUIRE(ticket);
            REQUIRE(policy->bindAcceptedReference(*ticket,41,1000000,true,true));
            REQUIRE(bool(policy->authorizeOptionalExecution(*ticket,41,1,100000000,false)));
            if(policy->finishAcceptedCapture(*ticket,{600000,1000000,600000,100000000,true,true,true}))break;
        }
        REQUIRE(policy->lookup(key).plan==plan);const auto credits=policy->metrics().acceptedCpuNs;
        if(mode==Backend::Automatic){
            for(unsigned tick=1;tick<=17;++tick)service->recordCadence({tick,monotonicNs(),1000000000ull});
            const auto cadenceUntil=std::chrono::steady_clock::now()+std::chrono::seconds(5);
            while(!service->metrics().cadenceFloorNs && std::chrono::steady_clock::now()<cadenceUntil)std::this_thread::yield();
            REQUIRE(service->metrics().cadenceFloorNs==1000000000ull);
        }else {
            // Forced OpenCL has no automatic cadence state or deadline guard.
            CHECK(service->metrics().cadenceFloorNs==0);
        }
        pipeline.setAsyncWork([counts](auto& job,PlanDecision decision){
            auto field=std::make_shared<OwnedGradientField>();field->session=job.owner->session();field->decision=decision;
            field->inputs=counts;field->due=job.executorDue;field->data=std::move(job.data);
            field->costAt=[](const auto&,std::size_t){return LAND_STEPS;};
            field->cpu=[](auto& owned){auto counts=std::static_pointer_cast<const Counts>(owned.inputs);
                CHECK(owned.data[0]==77);owned.data[0]=91;
                // The holder is immutable except for its independently atomic counters.
                counts->cpu.fetch_add(1);};return field;
        });
        auto* published=new std::uint16_t[1]{};
        pipeline.submit(&published,0,[](auto& job){job.data[0]=77;});
        // Exercise the injected service clocks, rather than canceling an as-yet
        // unsubmitted DTO at publication.
        if(phase>=2){const auto readyUntil=std::chrono::steady_clock::now()+std::chrono::seconds(5);
            while(service->metrics().completed+service->metrics().fallbacks!=1 && std::chrono::steady_clock::now()<readyUntil)std::this_thread::yield();
            REQUIRE(service->metrics().completed+service->metrics().fallbacks==1);}
        pipeline.advance();CHECK(published[0]==91);
        const auto stop=std::chrono::steady_clock::now()+std::chrono::seconds(5);
        while(policy->lookup(key).plan!=Plan::CPU && std::chrono::steady_clock::now()<stop)std::this_thread::yield();
        CHECK(policy->lookup(key).plan==Plan::CPU);CHECK(policy->metrics().acceptedCpuNs==credits);
        CHECK(counts->cpu.load()==unsigned(phase<3));CHECK(counts->gpu.load()==unsigned(phase>=3));
        if(phase<2){CHECK(pipeline.cpuReason(GradientPipeline::CPUReason::CpuClockUnavailable)==1);
            CHECK(pipeline.requiredSeedCpuNs()==0);CHECK(pipeline.requiredPropagationCpuNs()==0);}
        else {
            while(service->metrics().completed+service->metrics().fallbacks!=1 && std::chrono::steady_clock::now()<stop)std::this_thread::yield();
            const auto metrics=service->metrics();CHECK(metrics.clockInvalidMeasurements>0);
            if(phase!=6){CHECK(metrics.batchPreparationCpuNs==0);CHECK(metrics.batchSubmissionCpuNs==0);}
            else {CHECK(metrics.batchPreparationCpuNs<10000000);CHECK(metrics.batchSubmissionCpuNs==0);}
            CHECK(metrics.automaticClockUnavailable==(mode==Backend::Automatic));
            CHECK(metrics.fallbackReasons[unsigned(GradientFallbackReason::CpuClockUnavailable)]==unsigned(phase==2));
            CHECK(pipeline.gpuCompleteFields()==unsigned(phase>=3));
        }
        pipeline.reset();service->stop();delete[] published;
    }
}

TEST_CASE("pending required GPU work cancels at publication and drains through one worker recovery" * doctest::test_suite("GradientPipeline"))
{
    using namespace gradient_kernel;
    if constexpr(!GAGCore::ThreadSupport::available)return;
    struct Restore {Backend mode=backend();unsigned mask=readyPlans.load();~Restore(){setBackend(mode);readyPlans=mask;}} restore;
    PipelineEnvironment batchSize("GLOB2_GRADIENT_BATCH","1");
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
    PipelineEnvironment crossDue("GLOB2_GRADIENT_CROSS_DUE","0"),batch("GLOB2_GRADIENT_BATCH","8");
    struct Gate {
        std::promise<void> entered,release;std::shared_future<void> released=release.get_future().share();
        std::atomic<bool> opened{false};std::atomic<unsigned> cpu{0},jobs{0},calls{0};
        void open(){if(!opened.exchange(true))release.set_value();}
    };
    auto gate=std::make_shared<Gate>();struct Release{std::shared_ptr<Gate> value;~Release(){value->open();}} cleanup{gate};
    auto entered=gate->entered.get_future();
    auto service=std::make_shared<GradientDeviceService>(GradientDeviceService::Hooks{[]{return true;},
        [gate](std::span<const BackendRequest> requests,Plan){
            if(gate->calls.fetch_add(1)==0){gate->entered.set_value();gate->released.wait();}
            for(const auto& request:requests){CHECK(request.gradient[0]==77);request.gradient[0]=177;
                if(request.executedOnDevice)*request.executedOnDevice=true;}return true;
        }});
    service->configure(2,Backend::OpenCL);
    const auto readyUntil=std::chrono::steady_clock::now()+std::chrono::seconds(5);
    while(!service->metrics().ready && std::chrono::steady_clock::now()<readyUntil)std::this_thread::yield();
    REQUIRE(service->metrics().ready);
    ComputeExecutor executor;executor.configure(2);auto session=std::make_shared<BackendSession>();
    struct Context {std::shared_ptr<OwnedGradientField> field;std::shared_ptr<GradientDeviceService> service;
        ComputeExecutor* executor;std::shared_ptr<Gate> gate;};
    std::array<Context,67> contexts;
    for(unsigned i=0;i<contexts.size();++i){
        auto field=std::make_shared<OwnedGradientField>();field->session=session;
        field->decision={Plan::Frozen8,0,session->currentGeneration()};field->due=1000;
        field->data=std::make_unique<std::uint16_t[]>(1);field->data[0]=77;field->inputs=gate;
        field->costAt=[](const auto&,std::size_t){return LAND_STEPS;};
        field->cpu=[](auto& value){CHECK(value.data[0]==77);value.data[0]=177;
            ++const_cast<Gate*>(static_cast<const Gate*>(value.inputs.get()))->cpu;};
        contexts[i]={field,service,&executor,gate};
    }
    struct BatchContext {std::array<Context,67>* jobs;std::future<void>* entered;} batchContext{&contexts,&entered};
    const ComputeExecutor::Group group{67,{[](void* value,std::size_t index){auto& batch=*static_cast<BatchContext*>(value);
        auto& c=(*batch.jobs)[index];c.field->completion=c.executor->defer();
        if(!c.service->submit(c.field))c.service->recoverOnWorker(c.field);
        if(!index){const auto result=batch.entered->wait_for(std::chrono::seconds(5));
            if(result!=std::future_status::ready)c.gate->open();REQUIRE(result==std::future_status::ready);}
        ++c.gate->jobs;},&batchContext},ComputeExecutor::NoLane};
    auto ticket=executor.submit(std::span(&group,1),1000);
    struct Drain {ComputeExecutor& executor;ComputeExecutor::Batch& ticket;std::shared_ptr<Gate> gate;
        ~Drain(){gate->open();try{executor.join(ticket);}catch(...){}}
    } drain{executor,ticket,gate}; // Join before borrowed Contexts unwind on assertion failure.
    const auto filledUntil=std::chrono::steady_clock::now()+std::chrono::seconds(5);
    while(gate->jobs<contexts.size() && std::chrono::steady_clock::now()<filledUntil)std::this_thread::yield();
    if(gate->jobs!=contexts.size())gate->open();
    REQUIRE(gate->jobs==contexts.size());CHECK(service->metrics().submitted==65);CHECK(service->metrics().queued==64);
    CHECK(service->metrics().budgetDeclines==2);gate->open();
    executor.join(ticket);
    CHECK(gate->cpu==2);for(const auto& context:contexts)CHECK(context.field->data[0]==177);
    const auto doneUntil=std::chrono::steady_clock::now()+std::chrono::seconds(5);
    while(service->metrics().completed!=65 && std::chrono::steady_clock::now()<doneUntil)std::this_thread::yield();
    CHECK(service->metrics().completed==65);CHECK(service->metrics().fallbacks==2);service->stop();
}

namespace {
struct ServiceProbeDriver {
    std::atomic<unsigned> commands{0},polls{0};std::atomic<bool> terminal{false};
    std::shared_ptr<const void> borrowed; // Accessed only by the coordinator.
    struct Work final : gradient_kernel::CaptureProbeReplay::Probe {
        ServiceProbeDriver& driver;std::shared_ptr<const void> owner;
        Work(ServiceProbeDriver& d,std::shared_ptr<const void> o):driver(d),owner(std::move(o)){}
        gradient_kernel::OpenCLProbeProgress advance() noexcept override {
            driver.borrowed=owner;++driver.commands;return gradient_kernel::OpenCLProbeProgress::Quarantined;
        }
        void cancel() noexcept override {}
        std::span<const std::uint16_t> result() const noexcept override{return {};}
    };
    static bool ready(const void*)noexcept{return true;}
    static std::unique_ptr<gradient_kernel::CaptureProbeReplay::Probe> begin(const gradient_kernel::OpenCLProbeInput&,
        gradient_kernel::Plan,std::shared_ptr<const void> owner,const void* context){
        return std::make_unique<Work>(*const_cast<ServiceProbeDriver*>(static_cast<const ServiceProbeDriver*>(context)),std::move(owner));
    }
    static bool reap(const void* context)noexcept {
        auto& driver=*const_cast<ServiceProbeDriver*>(static_cast<const ServiceProbeDriver*>(context));++driver.polls;
        if(!driver.terminal)return false;driver.borrowed.reset();return true;
    }
};
std::shared_ptr<gradient_kernel::GradientCpuCapture::Record> serviceCapture(
    const std::shared_ptr<gradient_kernel::BackendSession>& session)
{
    using namespace gradient_kernel;
    auto policy=session->learningPolicy();REQUIRE(bool(policy));
    std::array<std::uint16_t,3> seeds{65535,1,0};GradientCpuCapture::Input input;
    input.original=seeds;input.key={3,1,BUCKETS,2,1,Family::Clear,255,255};input.key.limit=COST_LIMIT;
    input.tick=1;input.due=100;input.requiredGeneration=session->currentGeneration();input.alternativeReady=true;
    input.reservedCpuNs=5000000;input.captureCpuLimitNs=200000;input.slackNs=100000000000ull;input.conservativeElapsedNs=1000000;
    policy->qualify(input.key,input.alternative,true);policy->observeAccepted(input.key,Plan::CPU,1000000000000ull,1);
    GradientCpuCapture::Receipt receipt;std::unique_ptr<GradientCpuCapture> capture;
    for(unsigned i=0;i<CpuSavingPolicy::ProbePeriod && !capture;++i)capture=GradientCpuCapture::begin(policy,input,receipt);
    REQUIRE(bool(capture));GradientWorkspace workspace;
    propagateFieldCPU(seeds.data(),0,COST_LIMIT,field::Grid(3,1),workspace,[](std::size_t){return false;});
    auto record=capture->complete(seeds,{input.requiredGeneration,1000000,true,true},receipt);REQUIRE(bool(record));return record;
}
}

TEST_CASE("canceled map leaves optional transfer and policy admission owned until background proof" * doctest::test_suite("GradientPipeline"))
{
    using namespace gradient_kernel;
    if constexpr(!GAGCore::ThreadSupport::available)return;if(!glob2::threadCpuNs())return;
    PipelineEnvironment tuning("GLOB2_GRADIENT_TUNING","1"),crossDue("GLOB2_GRADIENT_CROSS_DUE","0");
    const auto baseline=openCLProbeBytes();auto driver=std::make_shared<ServiceProbeDriver>();
    struct Unblock {std::shared_ptr<ServiceProbeDriver> value;~Unblock(){value->terminal=true;}} cleanup{driver};
    GradientDeviceService::Hooks hooks;hooks.initialize=[]{return true;};
    hooks.probe={driver,sizeof(ServiceProbeDriver)+sizeof(ServiceProbeDriver::Work)+256,
        &ServiceProbeDriver::ready,&ServiceProbeDriver::begin,&ServiceProbeDriver::reap};
    auto service=std::make_shared<GradientDeviceService>(std::move(hooks));service->configure(2,Backend::Automatic);
    const auto until=std::chrono::steady_clock::now()+std::chrono::seconds(5);
    while(!service->metrics().ready && std::chrono::steady_clock::now()<until)std::this_thread::yield();REQUIRE(service->metrics().ready);
    for(unsigned i=1;i<=17;++i)service->recordCadence({i,monotonicNs(),1000000000});
    while(!service->metrics().cadenceFloorNs && std::chrono::steady_clock::now()<until)std::this_thread::yield();REQUIRE(service->metrics().cadenceFloorNs>0);
    auto session=std::make_shared<BackendSession>();session->configureLearning(true);
    auto policy=session->learningPolicy();auto record=serviceCapture(session);std::weak_ptr<BackendSession> weakSession=session;
    REQUIRE(service->submitCapture(std::move(record),session));
    while(!driver->commands && std::chrono::steady_clock::now()<until)std::this_thread::yield();REQUIRE(driver->commands==1);
    auto stopped=std::async(std::launch::async,[service]{service->stop();});
    const auto stopResult=stopped.wait_for(std::chrono::milliseconds(100));
    if(stopResult!=std::future_status::ready)driver->terminal=true; // Unblock a regressed join before future teardown.
    CHECK(stopResult==std::future_status::ready);stopped.get();session.reset();CHECK(weakSession.expired());
    CHECK(openCLProbeBytes()>baseline);CHECK(policy->metrics().probeCpuNs==0);
    const auto polls=driver->polls.load();const auto reapUntil=std::chrono::steady_clock::now()+std::chrono::seconds(5);
    while(driver->polls==polls && std::chrono::steady_clock::now()<reapUntil)std::this_thread::yield();CHECK(driver->polls>polls);
    CHECK(openCLProbeBytes()>baseline);driver->terminal=true;
    while((openCLProbeBytes()!=baseline || !service->metrics().probeRetired) && std::chrono::steady_clock::now()<reapUntil)std::this_thread::yield();
    CHECK(openCLProbeBytes()==baseline);CHECK(policy->metrics().probeCpuNs>=5000000);CHECK(policy->metrics().promoted==0);
}

TEST_CASE("failed optional accounting admission cancels capture before backend entry" * doctest::test_suite("GradientPipeline"))
{
    using namespace gradient_kernel;
    if constexpr(!GAGCore::ThreadSupport::available)return;if(!glob2::threadCpuNs())return;
    PipelineEnvironment tuning("GLOB2_GRADIENT_TUNING","1"),crossDue("GLOB2_GRADIENT_CROSS_DUE","0");
    const auto baseline=openCLProbeBytes();auto driver=std::make_shared<ServiceProbeDriver>();
    GradientDeviceService::Hooks hooks;hooks.initialize=[]{return true;};
    hooks.probe={driver,sizeof(ServiceProbeDriver)+sizeof(ServiceProbeDriver::Work)+256,
        &ServiceProbeDriver::ready,&ServiceProbeDriver::begin,&ServiceProbeDriver::reap};
    auto service=std::make_shared<GradientDeviceService>(std::move(hooks));service->configure(2,Backend::Automatic);
    const auto until=std::chrono::steady_clock::now()+std::chrono::seconds(5);
    while(!service->metrics().ready && std::chrono::steady_clock::now()<until)std::this_thread::yield();REQUIRE(service->metrics().ready);
    auto session=std::make_shared<BackendSession>();session->configureLearning(true);auto policy=session->learningPolicy();
    auto record=serviceCapture(session);const auto bytes=OpenCLProbeBudget-openCLProbeBytes();
    REQUIRE(reserveOpenCLProbeBytes(bytes));
    struct Pressure{std::size_t bytes;~Pressure(){releaseOpenCLProbeBytes(bytes);}} pressure{bytes};
    REQUIRE(service->submitCapture(std::move(record),session));
    while(!service->metrics().probesCanceled && std::chrono::steady_clock::now()<until)std::this_thread::yield();
    REQUIRE(service->metrics().probesCanceled==1);CHECK(service->metrics().probesStarted==0);CHECK(driver->commands==0);
    CHECK(policy->metrics().probeCpuNs>=5000000);CHECK(policy->metrics().promoted==0);
    CHECK(openCLProbeBytes()==baseline+bytes);service->stop();
}

TEST_CASE("isolated process shutdown abandons unresolved optional proof without joining it" * doctest::test_suite("GradientPipeline"))
{
    // Run this case alone in a fresh glob2-unit-tests subprocess with an external
    // timeout. Successful process exit is part of the proof; a same-process
    // helper cannot safely destroy the globally owned broker before other tests.
    const auto* enabled=std::getenv("GLOB2_TEST_CAPTURE_SHUTDOWN");if(!enabled || std::strcmp(enabled,"1"))return;
    using namespace gradient_kernel;
    if constexpr(!GAGCore::ThreadSupport::available)return;REQUIRE(glob2::threadCpuNs()!=0);
    PipelineEnvironment tuning("GLOB2_GRADIENT_TUNING","1"),crossDue("GLOB2_GRADIENT_CROSS_DUE","0");
    auto driver=std::make_shared<ServiceProbeDriver>();GradientDeviceService::Hooks hooks;hooks.initialize=[]{return true;};
    hooks.probe={driver,sizeof(ServiceProbeDriver)+sizeof(ServiceProbeDriver::Work)+256,
        &ServiceProbeDriver::ready,&ServiceProbeDriver::begin,&ServiceProbeDriver::reap};
    auto service=std::make_shared<GradientDeviceService>(std::move(hooks));service->configure(2,Backend::Automatic);
    const auto until=std::chrono::steady_clock::now()+std::chrono::seconds(5);
    while(!service->metrics().ready && std::chrono::steady_clock::now()<until)std::this_thread::yield();REQUIRE(service->metrics().ready);
    for(unsigned i=1;i<=17;++i)service->recordCadence({i,monotonicNs(),1000000000});
    while(!service->metrics().cadenceFloorNs && std::chrono::steady_clock::now()<until)std::this_thread::yield();REQUIRE(service->metrics().cadenceFloorNs>0);
    auto session=std::make_shared<BackendSession>();session->configureLearning(true);
    auto record=serviceCapture(session);REQUIRE(service->submitCapture(std::move(record),session));
    while(!driver->commands && std::chrono::steady_clock::now()<until)std::this_thread::yield();REQUIRE(driver->commands==1);
    service->stop();CHECK_FALSE(driver->terminal);CHECK(openCLProbeBytes()>0);
    // Intentionally leave the fake borrowed graph unresolved. The broker's
    // exit-only path must release its registration while that graph remains
    // retained, rather than waiting for a nonexistent positive event proof.
}

TEST_CASE("gradient reconfiguration preserves credit ticks and relative saved deadlines" * doctest::test_suite("GradientPipeline"))
{
    using namespace gradient_kernel;
    TestGradientPipeline pipeline;
    pipeline.configure(0,3,1,[](auto& job,auto&){++job.data[0];});
    auto session=pipeline.session();session->configureLearning(true);const auto policy=session->learningPolicy();
    REQUIRE(bool(policy));
    for(unsigned i=0;i<5;++i)pipeline.advance();
    WorkloadKey key{1,1,BUCKETS,1,1,Family::Materials,255,255};key.limit=COST_LIMIT;
    policy->observeAccepted(key,Plan::CPU,1000000000,5);REQUIRE(policy->qualify(key,Plan::Frozen8,true));
    const auto oldGeneration=session->currentGeneration();
    std::optional<CpuSavingPolicy::ProbeTicket> oldTicket;
    for(unsigned i=0;i<CpuSavingPolicy::ProbePeriod && !oldTicket;++i)
        oldTicket=policy->beginAcceptedCapture(key,Plan::Frozen8,5,1000000,100000000,1000000,false,0,oldGeneration,true);
    REQUIRE(bool(oldTicket));
    auto* slot=new std::uint16_t[1]{};
    struct Release {TestGradientPipeline& pipeline;std::uint16_t*& slot;~Release(){pipeline.reset();delete[] slot;}} release{pipeline,slot};
    auto* job=pipeline.reserve(&slot,0);CHECK(job->due==8);
    pipeline.prepare(job,[](auto& value){value.data[0]=40;});
    GradientPipeline::RestoredSnapshot saved{&slot,0,0,false,{}};
    pipeline.visitPendingSnapshots([&](const auto& snapshot){
        CHECK(snapshot.remaining==3);CHECK(snapshot.data[0]==41);CHECK(slot[0]==0);
        saved.remaining=snapshot.remaining;saved.data=std::make_unique<std::uint16_t[]>(1);saved.data[0]=snapshot.data[0];
    });
    pipeline.reset();CHECK(session->currentGeneration()!=oldGeneration);
    CHECK_FALSE(policy->bindAcceptedReference(*oldTicket,oldGeneration,1000000,true,true));
    CHECK(policy->metrics().probeCpuNs==0); // Admission survives invalidation until cleanup.
    CHECK(policy->metrics().acceptedCpuNs==1000000000);
    policy->cancelProbe(*oldTicket,100,true);CHECK(policy->metrics().probeCpuNs>=1000000);
    pipeline.configure(0,3,1,[](auto& value,auto&){++value.data[0];});
    CHECK_THROWS_AS(pipeline.reserve(&slot,0),std::logic_error);
    pipeline.advance();pipeline.restoreCompleted(std::move(saved));
    pipeline.visitPendingSnapshots([](const auto& snapshot){CHECK(snapshot.remaining==3);});
    for(unsigned i=0;i<2;++i){pipeline.advance();CHECK(slot[0]==0);}
    pipeline.advance();CHECK(slot[0]==41);
    pipeline.advance();pipeline.setWorkerCount(1);
    pipeline.deadline=[](unsigned remaining){return ComputeExecutor::boundaryDue(1000+remaining);};
    job=pipeline.reserve(&slot,0);CHECK(job->due==13);
    pipeline.prepare(job,[](auto& value){value.data[0]=67;});
    CHECK(job->executorDue==ComputeExecutor::boundaryDue(1003));
    for(unsigned i=0;i<2;++i){pipeline.advance();CHECK(slot[0]==41);}
    pipeline.advance();CHECK(slot[0]==68);
}

TEST_CASE("worker group capture replays already ready fields and canceled members never hold required tickets" * doctest::test_suite("GradientPipeline"))
{
    using namespace gradient_kernel;
    if constexpr(!GAGCore::ThreadSupport::available)return;if(!glob2::threadCpuNs())return;
    PipelineEnvironment tuning("GLOB2_GRADIENT_TUNING","1"),envelope("GLOB2_GRADIENT_CPU_ENVELOPE","1"),
        crossDue("GLOB2_GRADIENT_CROSS_DUE","0"),batch("GLOB2_GRADIENT_BATCH","2");
    struct Gate {
        std::promise<void> entered,release;std::shared_future<void> released=release.get_future().share();
        std::atomic<bool> opened{false};void open(){if(!opened.exchange(true))release.set_value();}
    };
    struct Driver {
        std::atomic<unsigned> begins{0},fields{0};
        struct Work final : CaptureBatchProbeReplay::Probe {
            std::shared_ptr<const void> owner;
            std::array<std::vector<std::uint16_t>,8> output;
            OpenCLProbeProgress advance() noexcept override{return OpenCLProbeProgress::Complete;}
            void cancel() noexcept override{}
            bool waitingForEvent() const noexcept override{return false;}
            std::span<const std::uint16_t> result(unsigned field) const noexcept override{return output[field];}
        };
        static bool ready(const void*)noexcept{return true;}
        static bool reap(const void*)noexcept{return true;}
        static std::unique_ptr<CaptureBatchProbeReplay::Probe> begin(std::span<const OpenCLProbeInput> requests,
            Plan,std::shared_ptr<const void> owner,const void* value) {
            auto& driver=*const_cast<Driver*>(static_cast<const Driver*>(value));
            ++driver.begins;driver.fields=unsigned(requests.size());
            auto work=std::make_unique<Work>();work->owner=std::move(owner);
            GradientWorkspace scratch;
            for(unsigned i=0;i<requests.size();++i) {
                const auto& request=requests[i];work->output[i].assign(request.gradient,request.gradient+request.grid.cells());
                propagateFieldCPU(work->output[i].data(),0,request.limit,request.grid,scratch,[](std::size_t){return false;});
            }
            return work;
        }
    };
    for(unsigned scenario=0;scenario<2;++scenario) {
        const auto baseline=openCLProbeBytes();const unsigned slots=scenario?3:2;
        auto driver=std::make_shared<Driver>();GradientDeviceService::Hooks hooks;hooks.initialize=[]{return true;};
        hooks.batchProbe={driver,sizeof(Driver)+sizeof(Driver::Work)+4096,&Driver::ready,&Driver::begin,&Driver::reap};
        auto service=std::make_shared<GradientDeviceService>(std::move(hooks));service->configure(slots,Backend::Automatic);
        auto until=std::chrono::steady_clock::now()+std::chrono::seconds(5);
        while(!service->metrics().ready && std::chrono::steady_clock::now()<until)std::this_thread::yield();REQUIRE(service->metrics().ready);
        for(unsigned i=1;i<=17;++i)service->recordCadence({i,monotonicNs(),1000000000});
        while(!service->metrics().cadenceFloorNs && std::chrono::steady_clock::now()<until)std::this_thread::yield();REQUIRE(service->metrics().cadenceFloorNs>0);
        auto session=std::make_shared<BackendSession>();session->configureLearning(true);auto policy=session->learningPolicy();
        WorkloadKey key{9,1,BUCKETS,slots,1,Family::Clear,255,255};key.limit=COST_LIMIT;
        REQUIRE(policy->qualify(key,Plan::Frozen8,true));auto pair=key;pair.batch=2;REQUIRE(policy->qualify(pair,Plan::Frozen8,true));
        // Synthetic funding tests service ownership only; it cannot authorize
        // native coverage, performance inference, or a production GPU choice.
        policy->observeAccepted(key,Plan::CPU,1000000000000ull,1);
        auto gate=std::make_shared<Gate>();auto entered=gate->entered.get_future();
        auto blocker=std::make_shared<GradientDeviceService>(GradientDeviceService::Hooks{[]{return true;},
            [gate](std::span<const BackendRequest> requests,Plan){gate->entered.set_value();gate->released.wait();
                for(const auto& request:requests)if(request.executedOnDevice)*request.executedOnDevice=true;return true;}});
        blocker->configure(2,Backend::OpenCL);
        while(!blocker->metrics().ready && std::chrono::steady_clock::now()<until)std::this_thread::yield();REQUIRE(blocker->metrics().ready);
        ComputeExecutor executor;executor.configure(slots);auto held=std::make_shared<OwnedGradientField>();
        held->session=std::make_shared<BackendSession>();held->decision={Plan::Frozen8,0,held->session->currentGeneration()};held->due=100;
        held->data=std::make_unique<std::uint16_t[]>(1);held->data[0]=77;held->costAt=[](const auto&,std::size_t){return LAND_STEPS;};
        struct BlockerRecovery {mutable std::atomic<unsigned> count{0};};
        auto blockerFallbacks=std::make_shared<BlockerRecovery>();
        held->inputs=blockerFallbacks;held->retainedInputBytes=sizeof(BlockerRecovery)+128;
        held->cpu=[](OwnedGradientField& field){
            ++static_cast<const BlockerRecovery*>(field.inputs.get())->count;
            CHECK(field.data[0]==77);GradientWorkspace scratch;
            propagateFieldCPU(field.data.get(),0,field.limit,field.grid,scratch,[](std::size_t){return false;});
            CHECK(field.data[0]==77);
        };
        struct Block {ComputeExecutor* executor;std::shared_ptr<GradientDeviceService> service;std::shared_ptr<OwnedGradientField> field;} block{&executor,blocker,held};
        const ComputeExecutor::Group heldGroup{1,{[](void* value,std::size_t){auto& c=*static_cast<Block*>(value);
            c.field->completion=c.executor->defer();if(!c.service->submit(c.field))c.service->recoverOnWorker(c.field);},&block},ComputeExecutor::NoLane};
        auto heldTicket=executor.submit(std::span(&heldGroup,1));
        struct Work {
            std::shared_ptr<GradientDeviceService> service;std::shared_ptr<BackendSession> session;
            WorkloadKey key;std::shared_ptr<int> source=std::make_shared<int>(7);
            std::array<std::array<std::uint16_t,9>,2> output{};
            std::atomic<unsigned> required{0};std::atomic<bool> captured{false},release{false};bool pending=false;
        } work{service,session,key};work.pending=scenario!=0;
        const ComputeExecutor::Group captureGroup{scenario?1u:2u,{[](void* value,std::size_t index){auto& work=*static_cast<Work*>(value);
            auto& seeds=work.output[index];seeds.fill(GRADIENT_UNREACHABLE);seeds[0]=GRADIENT_AT_GOAL-unsigned(index);
            GradientCpuCaptureGroup::Input input;input.original=seeds;input.key=work.key;input.sourceIdentity=work.source;
            input.weakSourceBytes=sizeof(int)+256;input.revision=21;input.variant=0;input.tick=1;input.due=100;
            input.requiredGeneration=work.session->currentGeneration();input.scalarClassic=true;
            GradientCpuCapture::Receipt receipt;std::unique_ptr<GradientDeviceService::GroupCapture> capture;
            for(unsigned i=0;i<CpuSavingPolicy::ProbePeriod && !capture;++i)capture=work.service->beginGroupCapture(work.session,input,receipt);
            REQUIRE(bool(capture));work.captured=true;
            while(work.pending && !work.release.load())std::this_thread::sleep_for(std::chrono::microseconds(100));
            GradientWorkspace scratch;const auto start=glob2::threadCpuNs();
            propagateFieldCPU(seeds.data(),0,COST_LIMIT,field::Grid(9,1),scratch,[](std::size_t){return false;});
            const auto end=glob2::threadCpuNs();++work.required;
            capture->complete(seeds,{input.requiredGeneration,end-start,true,start && end>start});capture->retire();
        },&work},ComputeExecutor::NoLane};
        ComputeExecutor::Batch captureTicket;
        struct Drain {ComputeExecutor& executor;ComputeExecutor::Batch& held;ComputeExecutor::Batch& captured;
            std::shared_ptr<Gate> gate;Work& work;~Drain(){work.release=true;gate->open();try{executor.join(captured);executor.join(held);}catch(...){}}
        } drain{executor,heldTicket,captureTicket,gate,work}; // Before borrowed worker contexts unwind.
        REQUIRE(entered.wait_for(std::chrono::seconds(5))==std::future_status::ready);
        captureTicket=executor.submit(std::span(&captureGroup,1));
        if(scenario) {
            until=std::chrono::steady_clock::now()+std::chrono::seconds(5);
            while(!work.captured && std::chrono::steady_clock::now()<until)std::this_thread::yield();REQUIRE(work.captured);
            std::atomic<unsigned> other{0};const ComputeExecutor::Group unrelated{1,{[](void* p,std::size_t){++*static_cast<std::atomic<unsigned>*>(p);},&other},ComputeExecutor::NoLane};
            auto ticket=executor.submit(std::span(&unrelated,1));executor.join(ticket);CHECK(other==1);
            auto stopped=std::async(std::launch::async,[service]{service->stop();});
            const auto result=stopped.wait_for(std::chrono::milliseconds(100));
            if(result!=std::future_status::ready){work.release=true;gate->open();}CHECK(result==std::future_status::ready);stopped.get();
            CHECK(policy->metrics().canceled==0);CHECK(policy->metrics().admitted==1);work.release=true;
        }
        executor.join(captureTicket);CHECK(work.required==(scenario?1u:2u));CHECK(driver->begins==0);
        // Required CPU results are complete before the optional broker can run.
        CHECK(work.output[0][0]==GRADIENT_AT_GOAL);gate->open();executor.join(heldTicket);
        CHECK(blockerFallbacks->count.load()==0);CHECK(held->data[0]==77);
        until=std::chrono::steady_clock::now()+std::chrono::seconds(5);
        while(!service->metrics().probeRetired && !service->metrics().probesCanceled && std::chrono::steady_clock::now()<until)
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        if(!scenario){CHECK(driver->begins==1);CHECK(driver->fields==2);CHECK(service->metrics().probesExact==1);}
        else CHECK(driver->begins==0);
        CHECK(policy->metrics().promoted==0);service->stop();blocker->stop();
        while(openCLProbeBytes()!=baseline && std::chrono::steady_clock::now()<until)std::this_thread::sleep_for(std::chrono::milliseconds(1));
        CHECK(openCLProbeBytes()==baseline);
    }
}

TEST_CASE("accepted singleton deadline guard rejects queued acceleration without demoting its plan" * doctest::test_suite("GradientPipeline"))
{
    using namespace gradient_kernel;
    if constexpr(!GAGCore::ThreadSupport::available)return;if(!glob2::threadCpuNs())return;
    PipelineEnvironment tuning("GLOB2_GRADIENT_TUNING","1"),envelope("GLOB2_GRADIENT_CPU_ENVELOPE","1"),
        crossDue("GLOB2_GRADIENT_CROSS_DUE","0"),batch("GLOB2_GRADIENT_BATCH","1");
    const auto plan=requestedOpenCLPlan();
    for(unsigned scenario=0;scenario<3;++scenario) {
        const auto mode=scenario==2?Backend::OpenCL:Backend::Automatic;
        auto session=std::make_shared<BackendSession>();session->configureLearning(true);
        const auto policy=session->learningPolicy();WorkloadKey key{1,1,BUCKETS,3,1,Family::Materials,255,255};key.limit=COST_LIMIT;
        policy->observeAccepted(key,Plan::CPU,1000000000000ull,1);REQUIRE(policy->qualify(key,plan,true));
        for(unsigned pair=0;pair<256 && policy->lookup(key).plan==Plan::CPU;++pair) {
            std::optional<CpuSavingPolicy::ProbeTicket> ticket;
            for(unsigned i=0;i<CpuSavingPolicy::ProbePeriod && !ticket;++i)
                ticket=policy->beginAcceptedCapture(key,plan,1,1000000,100000000,1000000,false,0,session->currentGeneration(),true);
            REQUIRE(bool(ticket));REQUIRE(policy->bindAcceptedReference(*ticket,session->currentGeneration(),1000000,true,true));
            REQUIRE(bool(policy->authorizeOptionalExecution(*ticket,session->currentGeneration(),1,100000000,false)));
            policy->finishAcceptedCapture(*ticket,{100000,1000000,600000,100000000,true,true,true});
        }
        const auto accepted=policy->lookup(key);REQUIRE(accepted.plan==plan);REQUIRE(accepted.maximumElapsedNs>0);
        const auto initialCredits=policy->metrics().acceptedCpuNs;
        struct Gate {std::promise<void> entered,release;std::shared_future<void> released=release.get_future().share();
            std::atomic<bool> opened{false};void open(){if(!opened.exchange(true))release.set_value();}};
        auto gate=std::make_shared<Gate>();auto entered=gate->entered.get_future();
        auto blocker=std::make_shared<GradientDeviceService>(GradientDeviceService::Hooks{[]{return true;},
            [gate](std::span<const BackendRequest>,Plan){gate->entered.set_value();gate->released.wait();return true;}});
        std::atomic<unsigned> calls{0};
        auto service=std::make_shared<GradientDeviceService>(GradientDeviceService::Hooks{[]{return true;},
            [&calls,mode](std::span<const BackendRequest> requests,Plan){++calls;
                for(const auto& request:requests){CHECK(request.gradient[0]==77);REQUIRE(bool(request.requiredControl));
                    CHECK(request.requiredControl->shouldCancel(UINT64_MAX)==(mode==Backend::Automatic));
                    if(request.executedOnDevice)*request.executedOnDevice=true;}return true;}});
        service->configure(3,mode);blocker->configure(3,Backend::OpenCL);
        auto until=std::chrono::steady_clock::now()+std::chrono::seconds(5);
        while((!service->metrics().ready || !blocker->metrics().ready) && std::chrono::steady_clock::now()<until)std::this_thread::yield();
        REQUIRE(service->metrics().ready);REQUIRE(blocker->metrics().ready);
        if(mode==Backend::Automatic) {
            for(unsigned tick=1;tick<=17;++tick)service->recordCadence({tick,monotonicNs(),1000000000ull});
            while(!service->metrics().cadenceFloorNs && std::chrono::steady_clock::now()<until)std::this_thread::yield();
            REQUIRE(service->metrics().cadenceFloorNs==1000000000ull);
        }
        ComputeExecutor executor;executor.configure(3);
        struct Recovery {mutable std::atomic<unsigned> count{0};};
        auto recovered=std::make_shared<Recovery>();
        struct Context {ComputeExecutor* executor;std::shared_ptr<GradientDeviceService> service;std::shared_ptr<OwnedGradientField> field;};
        std::array<Context,2> contexts;
        for(unsigned i=0;i<2;++i) {
            auto field=std::make_shared<OwnedGradientField>();field->session=session;
            REQUIRE(bool(field->requiredControl)); // Constructor's precharged owned control.
            field->decision={plan,i?accepted.version:0,session->currentGeneration()};
            field->data=std::make_unique<std::uint16_t[]>(1);field->data[0]=77;field->grid={1,1};field->limit=COST_LIMIT;
            field->workload=key;field->family=key.family;field->due=100+i;field->publicationTick=20+i;field->tick=17;
            field->preparationCpuClockValid=true;field->seedCpuNs=100;
            field->inputs=recovered;field->retainedInputBytes=sizeof(Recovery)+128;
            field->costAt=[](const auto&,std::size_t){return LAND_STEPS;};
            field->cpu=[](OwnedGradientField& owned){++static_cast<const Recovery*>(owned.inputs.get())->count;
                CHECK(owned.data[0]==77);GradientWorkspace scratch;
                propagateFieldCPU(owned.data.get(),0,owned.limit,owned.grid,scratch,[](std::size_t){return false;});CHECK(owned.data[0]==77);};
            contexts[i]={&executor,i?service:blocker,std::move(field)};
        }
        std::array<ComputeExecutor::Batch,2> tickets;
        struct Drain {ComputeExecutor& executor;std::array<ComputeExecutor::Batch,2>& tickets;std::shared_ptr<Gate> gate;
            ~Drain(){gate->open();for(auto& ticket:tickets)try{executor.join(ticket);}catch(...){}}
        } drain{executor,tickets,gate};
        const auto submit=+[](void* value,std::size_t){auto& context=*static_cast<Context*>(value);
            context.field->completion=context.executor->defer();REQUIRE(context.service->submit(context.field));};
        const ComputeExecutor::Group first{1,{submit,&contexts[0]},ComputeExecutor::NoLane};tickets[0]=executor.submit(std::span(&first,1),100);
        const auto enteredStatus=entered.wait_for(std::chrono::seconds(5));if(enteredStatus!=std::future_status::ready)gate->open();
        REQUIRE(enteredStatus==std::future_status::ready);
        const ComputeExecutor::Group target{1,{submit,&contexts[1]},ComputeExecutor::NoLane};tickets[1]=executor.submit(std::span(&target,1),101);
        while(service->metrics().submitted!=1 && std::chrono::steady_clock::now()<until)std::this_thread::yield();
        REQUIRE(service->metrics().submitted==1);CHECK_FALSE(executor.finished(tickets[1]));
        std::atomic<unsigned> other{0};const ComputeExecutor::Group independent{1,{+[](void* p,std::size_t){++*static_cast<std::atomic<unsigned>*>(p);},&other},ComputeExecutor::NoLane};
        auto otherTicket=executor.submit(std::span(&independent,1),99);executor.join(otherTicket);CHECK(other.load()==1);
        if(scenario)service->recordCadence({18,monotonicNs(),100});
        const auto beforeWindows=service->metrics().cpuEnvelopeWindows;
        gate->open();for(auto& ticket:tickets)executor.join(ticket);
        CHECK(calls.load()==(scenario==1?0u:1u));CHECK(recovered->count.load()==(scenario==1?1u:0u));
        const auto& field=contexts[1].field;CHECK(field->data[0]==77);CHECK(field->due==101);CHECK(field->publicationTick==21);
        CHECK(field->decision.version==accepted.version);
        CHECK(field->fallbackReason==(scenario==1?GradientFallbackReason::DeadlineSlack:GradientFallbackReason::None));
        if(scenario==1) {
            until=std::chrono::steady_clock::now()+std::chrono::seconds(5);
            while(policy->metrics().acceptedCpuNs==initialCredits && std::chrono::steady_clock::now()<until)std::this_thread::yield();
            CHECK(policy->metrics().acceptedCpuNs>initialCredits);
            CHECK(policy->lookup(key).plan==plan);CHECK(policy->lookup(key).version==accepted.version);
            CHECK(service->metrics().cpuEnvelopeWindows==beforeWindows);
            CHECK(service->metrics().fallbackReasons[unsigned(GradientFallbackReason::DeadlineSlack)]==1);
        }
        service->stop();blocker->stop();
    }
}
