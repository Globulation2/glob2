// SPDX-License-Identifier: GPL-3.0-or-later
#include "Glob2Test.h"
#include <memory>
#include <utility>
#include <chrono>
#include <thread>
#include <cstdint>
#include "map/gradient/GradientPipeline.h"
#include <array>
#include <stdexcept>

TEST_SUITE("GradientPipeline")
{
TEST_CASE("fixed publication; supersession; bounded buffers; scheduling stress; fallback; exceptions and teardown")
{
	for (unsigned workers : {0, 1, 2, 4, 8}) for (unsigned delay : {1, 2, 3, 8}) {
		std::array<std::uint16_t *, 7> slots{};
		for (auto &slot : slots) slot = new std::uint16_t[16]{};
		GradientPipeline pipeline;
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
			pipeline.submit(&slots[tick%7], 0, [&](auto &job) {
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
		GradientPipeline original, restored;
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
	GradientPipeline fallback;
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
        GradientPipeline pipeline;
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
        CHECK_THROWS_AS(pipeline.prepare(job, [](auto &) { throw std::runtime_error("seed failure"); }), std::runtime_error);
        CHECK_THROWS_AS(pipeline.finish(), std::runtime_error);
        pipeline.reset();
        delete[] slot;
    }
}
}

TEST_SUITE("GradientPipeline") {
TEST_CASE("exact prepared reuse preserves private results deadlines and supersession") {
    for (unsigned workers : {0u,1u,4u}) {
        auto* field=new std::uint16_t[4]{};
        std::atomic<unsigned> calls{0};
        auto water=std::make_shared<const std::vector<std::uint8_t>>(4,1);
        GradientPipeline pipeline;
        pipeline.configure(workers,3,4,[&](auto& job,auto&) {
            ++calls;
            for(unsigned i=0;i<4;++i) job.data[i]+=(*job.water)[i];
        });
        pipeline.enableResultReuseForPureWork();
        auto submit=[&](unsigned seed) {
            pipeline.submit(&field,0,[&](auto& job) {
                std::fill_n(job.data.get(),4,seed); job.water=water;
            });
        };
        pipeline.advance(); submit(10); pipeline.finish();
        pipeline.advance(); pipeline.advance(); CHECK(field[0]==0);
        pipeline.advance(); CHECK(field[0]==11); CHECK(calls==1);
        field[0]=999; // Synchronous consumers may replace or modify the published field.
        submit(10);
        pipeline.visitPendingSnapshots([&](const auto& snapshot) {
            CHECK(snapshot.remaining==3); CHECK(snapshot.data[0]==11); CHECK(field[0]==999);
        });
        CHECK(calls==1);
        pipeline.invalidate(&field); field[0]=888;
        pipeline.advance(); pipeline.advance(); pipeline.advance();
        CHECK(field[0]==888); CHECK(pipeline.metrics.discarded==1);
        submit(11); pipeline.finish(); CHECK(calls==2);
        pipeline.advance(); pipeline.advance(); pipeline.advance(); CHECK(field[0]==12);
        // Equal contents with a different immutable owner conservatively miss.
        water=std::make_shared<const std::vector<std::uint8_t>>(4,1);
        submit(11); pipeline.finish(); CHECK(calls==3);
        pipeline.advance(); pipeline.advance(); pipeline.advance(); CHECK(field[0]==12);
        water=std::make_shared<const std::vector<std::uint8_t>>(4,2);
        submit(11); pipeline.finish(); CHECK(calls==4);
        pipeline.advance(); pipeline.advance(); pipeline.advance(); CHECK(field[0]==13);
        CHECK(pipeline.metrics.jobs==5); CHECK(pipeline.metrics.published==4);
        pipeline.reset(); delete[] field;
    }
}

TEST_CASE("worker reconfiguration cannot install an old result into a readmitted entry") {
    auto* field=new std::uint16_t[1]{};
    std::atomic<unsigned> calls{0};
    GradientPipeline pipeline;
    pipeline.configure(1,3,1,[&](auto& job,auto&) { ++calls; ++job.data[0]; });
    pipeline.enableResultReuseForPureWork();
    auto submit=[&](unsigned value) { pipeline.submit(&field,0,[&](auto& job) { job.data[0]=value; }); };
    pipeline.advance(); submit(10);
    pipeline.setWorkerCount(4); // Keeps old completed job, drops its cache association.
    pipeline.advance(); submit(20); pipeline.finish();
    pipeline.advance(); pipeline.advance(); CHECK(field[0]==11);
    submit(20); // Old publication must not make seed20 appear to have result11.
    pipeline.finish();
    CHECK(calls==3);
    pipeline.advance(); CHECK(field[0]==21);
    pipeline.advance(); pipeline.advance(); CHECK(field[0]==21);
    submit(20); pipeline.finish(); CHECK(calls==3);
    pipeline.reset(); delete[] field;
}

TEST_CASE("reuse is opt in and failed work is never remembered") {
    for (bool reuse : {false,true}) {
        auto* field=new std::uint16_t[1]{};
        unsigned calls=0;
        GradientPipeline pipeline;
        pipeline.configure(0,1,1,[&](auto& job,auto&) {
            ++calls;
            if(job.data[0]==99) throw std::runtime_error("propagation failure");
            ++job.data[0];
        });
        if(reuse) pipeline.enableResultReuseForPureWork();
        pipeline.advance(); pipeline.submit(&field,0,[](auto& job) { job.data[0]=4; });
        pipeline.advance(); CHECK(field[0]==5);
        pipeline.submit(&field,0,[](auto& job) { job.data[0]=4; });
        pipeline.advance(); CHECK(calls==(reuse ? 1u : 2u));
        pipeline.submit(&field,0,[](auto& job) { job.data[0]=99; });
        CHECK_THROWS_AS(pipeline.finish(),std::runtime_error);
        CHECK_THROWS_AS(pipeline.advance(),std::runtime_error);
        CHECK(field[0]==5);
        pipeline.reset(); delete[] field;
    }
}
}

TEST_SUITE("GradientPipeline") {
TEST_CASE("reuse compares all propagation identities and scalar options") {
    auto* field=new std::uint16_t[1]{};
    unsigned calls=0,swim=0,buckets=64;
    bool modified=false;
    auto registry=TerrainRegistry::builtins()->importJson(R"({"schemaVersion":1,"terrains":[{"key":"test:reuse","name":"Reuse fixture","base":"grass","appearance":"sand","properties":{}}]})");
    std::shared_ptr<const TerrainMovementSnapshot> profiles;
    std::shared_ptr<const std::vector<TerrainType>> terrain;
    GradientPipeline pipeline;
    pipeline.configure(0,1,1,[&](auto& job,auto&) { ++calls; ++job.data[0]; });
    pipeline.enableResultReuseForPureWork();
    pipeline.advance();
    auto submit=[&] {
        pipeline.submit(&field,swim,[&](auto& job) {
            job.data[0]=4; job.terrainBuckets=buckets; job.modifiedCosts=modified;
            job.registry=registry; job.profiles=profiles; job.terrain=terrain;
        });
        pipeline.advance(); CHECK(field[0]==5);
    };
    submit(); submit(); CHECK(calls==1);
    ++swim; submit(); CHECK(calls==2);
    buckets=128; submit(); CHECK(calls==3);
    modified=true; submit(); CHECK(calls==4);
    terrain=std::make_shared<const std::vector<TerrainType>>(1,GRASS);
    submit(); CHECK(calls==5);
    terrain=std::make_shared<const std::vector<TerrainType>>(1,WATER);
    submit(); CHECK(calls==6);
    profiles=std::make_shared<const TerrainMovementSnapshot>();
    submit(); submit(); CHECK(calls==7);
    profiles=std::make_shared<const TerrainMovementSnapshot>();
    submit(); CHECK(calls==8);
    registry=TerrainRegistry::deserialize(registry->serialize());
    submit(); CHECK(calls==9);
    registry.reset(); profiles.reset(); terrain.reset();
    submit(); submit(); CHECK(calls==10);
    pipeline.reset(); delete[] field;
}

TEST_CASE("reuse admission is pinned and restored snapshots have no cache association") {
    std::array<std::uint16_t*,40> fields{};
    for(auto& field:fields) field=new std::uint16_t[1]{};
    unsigned calls=0;
    GradientPipeline pipeline;
    pipeline.configure(0,1,1,[&](auto& job,auto&) { ++calls; ++job.data[0]; });
    pipeline.enableResultReuseForPureWork(); pipeline.advance();
    for(unsigned pass=0;pass<2;++pass) for(auto& field:fields) {
        pipeline.submit(&field,0,[](auto& job) { job.data[0]=7; });
        pipeline.advance(); CHECK(field[0]==8);
    }
    CHECK(calls==48); // First32 stay admitted; remaining8 execute on each pass.
    auto saved=std::make_unique<std::uint16_t[]>(1); saved[0]=55;
    pipeline.restoreCompleted({&fields[0],0,1,false,std::move(saved)});
    pipeline.advance(); CHECK(fields[0][0]==55);
    pipeline.submit(&fields[0],0,[](auto& job) { job.data[0]=7; });
    pipeline.advance(); CHECK(fields[0][0]==8); CHECK(calls==48);
    pipeline.reset();
    pipeline.configure(0,2,1,[&](auto&,auto&) { ++calls; });
    pipeline.enableResultReuseForPureWork();
    saved=std::make_unique<std::uint16_t[]>(1); saved[0]=66;
    pipeline.restoreCompleted({&fields[0],0,2,false,std::move(saved)});
    pipeline.advance(); CHECK(fields[0][0]==8);
    pipeline.advance(); CHECK(fields[0][0]==66); CHECK(calls==48);
    pipeline.reset(); for(auto* field:fields) delete[] field;
}
}

TEST_SUITE("GradientPipeline") {
TEST_CASE("reuse byte budget limits large-map admission before the field-slot bound") {
    constexpr std::size_t cells=512*512;
    std::array<std::uint16_t*,17> fields{};
    for(auto& field:fields) field=new std::uint16_t[cells]{};
    unsigned calls=0;
    GradientPipeline pipeline;
    pipeline.configure(0,1,cells,[&](auto& job,auto&) { ++calls; ++job.data[0]; });
    pipeline.enableResultReuseForPureWork(); pipeline.advance();
    for(unsigned pass=0;pass<2;++pass) for(auto& field:fields) {
        pipeline.submit(&field,0,[&](auto& job) { std::fill_n(job.data.get(),cells,7); });
        pipeline.advance(); CHECK(field[0]==8); CHECK(field[cells-1]==7);
    }
    // Two 512KiB arrays per admitted field:16 admitted fields consume16MiB.
    // The seventeenth continues normal execution and cannot evict an admitted field.
    CHECK(calls==18);
    pipeline.reset(); for(auto* field:fields) delete[] field;
}
}

TEST_SUITE("GradientPipeline") {
TEST_CASE("completed leased entries stay unavailable until their fixed publication tick") {
    for(unsigned workers:{0u,1u,4u}) {
        auto* field=new std::uint16_t[1]{};
        std::atomic<unsigned> calls{0};
        GradientPipeline pipeline;
        pipeline.configure(workers,3,1,[&](auto& job,auto&) { ++calls; ++job.data[0]; });
        pipeline.enableResultReuseForPureWork();
        auto submit=[&] { pipeline.submit(&field,0,[](auto& job) { job.data[0]=7; }); };
        pipeline.advance(); submit();
        pipeline.visitPendingSnapshots([&](const auto& snapshot) {
            CHECK(snapshot.remaining==3); CHECK(snapshot.data[0]==8);
        });
        CHECK(field[0]==0);
        pipeline.advance(); submit(); pipeline.finish(); CHECK(calls==2);
        pipeline.advance(); submit(); pipeline.finish(); CHECK(calls==3);
        pipeline.advance(); CHECK(field[0]==8);
        submit(); pipeline.finish(); CHECK(calls==3); // First lease released at tick4 only.
        pipeline.invalidate(&field); field[0]=123;
        pipeline.advance(); pipeline.advance(); pipeline.advance();
        CHECK(field[0]==123); CHECK(pipeline.metrics.discarded==3);
        CHECK(pipeline.metrics.jobs==4); CHECK(pipeline.metrics.published==1);
        pipeline.reset(); delete[] field;
    }
}

TEST_CASE("overlapping uncached failures are not hidden by a completed leased result") {
    auto* field=new std::uint16_t[1]{};
    std::atomic<unsigned> calls{0};
    GradientPipeline pipeline;
    pipeline.configure(4,3,1,[&](auto& job,auto&) {
        ++calls;
        if(job.data[0]==99) throw std::runtime_error("overlapping propagation failure");
        ++job.data[0];
    });
    pipeline.enableResultReuseForPureWork();
    pipeline.advance(); pipeline.submit(&field,0,[](auto& job) { job.data[0]=7; });
    pipeline.finish();
    pipeline.advance(); pipeline.submit(&field,0,[](auto& job) { job.data[0]=99; });
    CHECK_THROWS_AS(pipeline.finish(),std::runtime_error);
    CHECK(calls==2); CHECK(field[0]==0);
    pipeline.advance(); pipeline.advance(); CHECK(field[0]==8);
    CHECK_THROWS_AS(pipeline.advance(),std::runtime_error);
    CHECK(field[0]==8);
    pipeline.reset(); delete[] field;
}
}
