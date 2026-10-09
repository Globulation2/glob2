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

// Standalone harnesses own an executor; production uses Map's shared executor.
class TestGradientPipeline : public GradientPipeline
{
    ComputeExecutor executor;
public:
    ~TestGradientPipeline() { reset(); }
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
