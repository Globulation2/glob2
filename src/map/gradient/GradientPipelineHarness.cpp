// SPDX-License-Identifier: GPL-3.0-or-later
#include "Glob2Test.h"
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
    struct State {std::atomic<unsigned> cpu{0};std::atomic<bool> worker{false};bool expectedDeviceCompletion=false;};
    auto state=std::make_shared<State>();state->expectedDeviceCompletion=deviceCompleted;std::weak_ptr<State> retained=state;
    auto service=std::make_shared<GradientDeviceService>(GradientDeviceService::Hooks{
        []{return true;},[deviceCompleted](std::span<const BackendRequest> requests,Plan){
            CHECK(requests.front().gradient[0]==77);
            REQUIRE(requests.front().deviceExecutionObserved);
            *requests.front().deviceExecutionObserved=deviceCompleted;return false;
        }});
    service->configure(2,Backend::OpenCL);
    const auto until=std::chrono::steady_clock::now()+std::chrono::seconds(5);
    while(!service->metrics().ready && std::chrono::steady_clock::now()<until)std::this_thread::yield();
    REQUIRE(service->metrics().ready);
    TestGradientPipeline pipeline;pipeline.configure(1,2,1,[](auto&,auto&){FAIL("unexpected synchronous execution");});
    pipeline.setDeviceService(service);
    pipeline.setAsyncWork([state](auto& job,PlanDecision decision){
        auto owned=std::make_shared<OwnedGradientField>();owned->inputs=state;
        owned->session=job.owner->session();owned->decision=decision;owned->data=std::move(job.data);
        owned->costAt=[](const auto&,std::size_t){return LAND_STEPS;};
        owned->cpu=[](auto& owned){
            auto& state=*const_cast<State*>(static_cast<const State*>(owned.inputs.get()));
            state.worker=ComputeExecutor::workerSlot()!=0;++state.cpu;
            CHECK(owned.data[0]==77);CHECK_FALSE(owned.executedGPU);
            CHECK(owned.deviceExecutionObserved==state.expectedDeviceCompletion);owned.data[0]=88;
        };return owned;
    });
    auto* field=new std::uint16_t[1]{};pipeline.advance();pipeline.submit(&field,0,[](auto& job){job.data[0]=77;});
    pipeline.finish();CHECK(state->cpu==1);CHECK(state->worker);CHECK(field[0]==0);
    CHECK(service->metrics().fallbacks==1);CHECK(pipeline.cpuCompleteFields()==1);
    while(deviceCompleted && service->metrics().deviceCompletedFields!=1 && std::chrono::steady_clock::now()<until)std::this_thread::yield();
    CHECK(service->metrics().deviceCompletedFields==unsigned(deviceCompleted));
    CHECK(service->metrics().fallbackAfterDeviceCompletionFields==unsigned(deviceCompleted));
    CHECK(service->metrics().executed==0);CHECK(pipeline.gpuCompleteFields()==0);
    pipeline.advance();pipeline.advance();CHECK(field[0]==88);
    pipeline.setAsyncWork({});state.reset();CHECK(retained.expired());
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
                ticket=policy->beginProbe(key,plan,1,1000000,100000000,1000000,false,0,1000000);
            REQUIRE(ticket);
            if(policy->finishProbe(*ticket,1000000,600000,1000000,600000,100000000,true,true))break;
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
    backend.platform="platform";backend.platformVendor="vendor";backend.platformVersion="version";
    backend.deviceVendor="vendor";backend.driverVersion="driver";backend.deviceVersion="version";backend.openCLCVersion="version";
    const auto nativeIdentity=std::string(64,'a');
    Json entry={{"width",512u},{"height",512u},{"cpu_buckets",64u},{"threads",2u},{"batch",2u},
        {"limit",65534u},{"movement",0u},{"movement_modifiers",false},{"seed_density",0u},{"blocker_density",4u},
        {"family","clear"},{"plan",unsigned(Plan::Frozen8)},{"cost_revision",11u},{"cost_variant",0u},
        {"max_measured_elapsed_ns",1000000u},{"completion_margin_ns",100000u},{"measured_batches",8u}};
    Json manifest={{"schema",1u},{"native_binary_sha256",nativeIdentity},{"source","immutable offline fixture"},{"measurement","homogeneous-ready-batch"},
        {"backend",{{"device",backend.device},{"platform",backend.platform},{"platform_vendor",backend.platformVendor},
            {"platform_version",backend.platformVersion},{"device_vendor",backend.deviceVendor},{"driver_version",backend.driverVersion},
            {"device_version",backend.deviceVersion},{"opencl_c_version",backend.openCLCVersion},{"check_interval",backend.checkInterval},{"poll_micros",backend.pollMicros},
            {"active_epoch",false},{"uniform_metadata",false},{"device_profiling",false},{"parity_bound",false}}},
        {"profiles",Json::array({entry})}};
    const auto text=manifest.dump();const auto parsed=GradientBatchManifest::parse(text,backend,false,nativeIdentity);
    REQUIRE(parsed.count==1);CHECK(parsed.bounds[0].workload.batch==2);CHECK(parsed.bounds[0].costRevision==11);
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
    changed=manifest;changed["profiles"].push_back(entry);
    CHECK_THROWS_AS(GradientBatchManifest::parse(changed.dump(),backend,false,nativeIdentity),std::invalid_argument);
    CHECK_THROWS_AS(GradientBatchManifest::parse(std::string(GradientBatchManifest::MaxBytes+1,' '),backend,false,nativeIdentity),std::invalid_argument);
}

TEST_CASE("ready cross-due batches need exact profile cadence seed metadata and automatic batch acceptance" * doctest::test_suite("GradientPipeline"))
{
    using namespace gradient_kernel;using Json=nlohmann::json;
    if constexpr(!GAGCore::ThreadSupport::available)return;
    struct Environment {
        const char* key;std::string previous;
        Environment(const char* key,const char* value):key(key),previous(std::getenv(key)?std::getenv(key):""){GAGCore::setProcessEnvironment(key,value,1);}
        ~Environment(){GAGCore::setProcessEnvironment(key,previous.c_str(),1);}
    } enabled("GLOB2_GRADIENT_CROSS_DUE","1"),batch("GLOB2_GRADIENT_BATCH","8");
    if(gradientNativeBuildIdentity().empty())return;
    const auto path=std::filesystem::temp_directory_path()/("glob2-ready-batch-"+std::to_string(monotonicNs())+".json");
    struct File {std::filesystem::path path;~File(){std::error_code error;std::filesystem::remove(path,error);}} file{path};
    OpenCLStatus backend;const auto plan=requestedOpenCLPlan();backend.device="fixture device";
    backend.platform="platform";backend.platformVendor="vendor";backend.platformVersion="version";
    backend.deviceVendor="vendor";backend.driverVersion="driver";backend.deviceVersion="version";backend.openCLCVersion="version";
    Json entry={{"width",1u},{"height",1u},{"cpu_buckets",64u},{"threads",2u},{"batch",2u},{"limit",65534u},
        {"movement",0u},{"movement_modifiers",false},{"seed_density",16u},{"blocker_density",0u},{"family","clear"},
        {"plan",unsigned(plan)},{"cost_revision",42u},{"cost_variant",7u},{"max_measured_elapsed_ns",1000u},
        {"completion_margin_ns",1000u},{"measured_batches",8u}};
    Json manifest={{"schema",1u},{"native_binary_sha256",gradientNativeBuildIdentity()},{"source","ready fake-driver fixture"},{"measurement","homogeneous-ready-batch"},
        {"backend",{{"device",backend.device},{"platform",backend.platform},{"platform_vendor",backend.platformVendor},
            {"platform_version",backend.platformVersion},{"device_vendor",backend.deviceVendor},{"driver_version",backend.driverVersion},
            {"device_version",backend.deviceVersion},{"opencl_c_version",backend.openCLCVersion},{"check_interval",backend.checkInterval},{"poll_micros",backend.pollMicros},
            {"active_epoch",backend.activeEpoch},{"uniform_metadata",backend.uniformMetadata},{"device_profiling",backend.deviceProfiling},
            {"parity_bound",backend.parityBound}}},{"profiles",Json::array({entry})}};
    {std::ofstream out(path);out<<manifest.dump();REQUIRE(bool(out));}
    const auto pathString=path.string();Environment profile("GLOB2_GRADIENT_BATCH_PROFILE",pathString.c_str());
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
        auto costOwner=std::make_shared<int>(0);
        std::array<std::shared_ptr<OwnedGradientField>,3> fields;
        struct Context {std::shared_ptr<GradientDeviceService> service;std::shared_ptr<OwnedGradientField> field;ComputeExecutor* executor;};
        std::array<Context,3> contexts;std::array<ComputeExecutor::Batch,3> tickets;
        for(unsigned i=0;i<fields.size();++i) {
            auto field=std::make_shared<OwnedGradientField>();field->session=session;field->decision={plan,0,session->currentGeneration()};
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
        gate->open();for(const auto& ticket:tickets)executor.join(ticket);
        for(const auto& field:fields)CHECK(field->data[0]==99);
        // Unknown seed shape, faster cadence, and unaccepted automatic batch2
        // each remain singleton although batch1 jobs were selected for GPU.
        CHECK(calls.load()==(scenario==0 ? 2u : 3u));
        CHECK(service->metrics().crossDueBatches==(scenario==0 ? 1u : 0u));
        service->stop();
    }
}

TEST_CASE("CPU envelope bridge initializes on CPU worker and deduplicates actual owner aliases" * doctest::test_suite("GradientPipeline"))
{
    if constexpr(!GAGCore::ThreadSupport::available)return;
    if(!glob2::nativeThreadId())return; // Native role identity is Linux-only today.
    using namespace gradient_kernel;
    Environment enabled("GLOB2_GRADIENT_CPU_ENVELOPE","1");
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
                ++owners;CHECK(thread.roles&(1u<<unsigned(glob2::CpuThreadRole::Owner)));
                CHECK(thread.roles&(1u<<unsigned(glob2::CpuThreadRole::OtherOwned)));
            }
            if(thread.tid==ids[1]){++workers;CHECK(thread.roles&(1u<<unsigned(glob2::CpuThreadRole::Worker)));}
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
        Environment envelope("GLOB2_GRADIENT_CPU_ENVELOPE",enabled ? "1" : "0");
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
        pipeline.advance();pipeline.submit(&published,0,[](auto& job){job.data[0]=7;});pipeline.advance();
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
