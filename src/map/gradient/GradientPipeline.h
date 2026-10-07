// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "field/GradientWorkspace.h"
#include "map/TerrainType.h"
#include "map/TerrainRegistry.h"
#include <atomic>
#include <array>
#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <exception>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <ThreadSupport.h>
#include <stdexcept>
#include <vector>
#include <utility>

// Fixed-tick publication. The simulation thread owns pending/free
// and all slot pointers; workers own only job data/water until done is signalled.
// Exactly one submit per advance. Completion time never selects publication time.
class GradientPipeline
{
public:
	struct Job {
		std::uint16_t **slot = nullptr;
		std::unique_ptr<std::uint16_t[]> data;
		std::shared_ptr<const std::vector<std::uint8_t>> water; // Test callback compatibility.
		std::shared_ptr<const std::vector<TerrainType>> terrain;
		std::shared_ptr<const TerrainRegistry> registry;
		std::shared_ptr<const TerrainMovementSnapshot> profiles;
		unsigned terrainBuckets = 64;
		bool modifiedCosts = false;
		int swim = 0;
		std::uint64_t due = 0;
		bool superseded = false, done = false;
		std::exception_ptr error;
        // Lease association assigned by preparation before propagation worker enqueue.
        unsigned reuseIndex = 32;
        std::uint64_t reuseGeneration = 0, reuseEpoch = 0;
	};
	// Stable save boundary. A view is valid only during visitPendingSnapshots;
	// the owning queue and worker state remain private to the pipeline.
	struct PendingSnapshot {
		std::uint16_t **slot;
		const std::uint16_t *data;
		unsigned remaining;
		bool superseded;
	};
	struct RestoredSnapshot {
		std::uint16_t **slot;
		int swim;
		unsigned remaining;
		bool superseded;
		std::unique_ptr<std::uint16_t[]> data;
	};
	using Work = std::function<void(Job &, GradientWorkspace &)>;
	using Factory = std::function<std::thread(std::function<void()>)>;
	struct Metrics { std::uint64_t jobs=0, published=0, discarded=0, waitNs=0, maxPending=0; } metrics;
private:
	std::deque<std::unique_ptr<Job>> pending;
	std::vector<std::unique_ptr<Job>> spare;
	std::deque<Job *> ready;
	std::vector<std::thread> workers;
	std::mutex mutex;
	std::condition_variable wake, completed;
	bool quit = false;
	unsigned delay = 0;
	std::uint64_t tick = 0, lastSubmission = 0;
	std::size_t cells = 0;
	Work work;
	GradientWorkspace serialWorkspace;
	std::atomic<std::uint64_t> activeNs{0};
    // Bounded, pinned admission avoids cycling an LRU through large field sets.
    // Weak identities never retain old map snapshots or catalog allocations.
    static constexpr unsigned ReuseSlots = 32;
    static constexpr std::size_t ReuseBytes = 16 * 1024 * 1024;
    template<class T> struct Identity {
        std::weak_ptr<const T> value;
        bool present = false;
        void set(const std::shared_ptr<const T>& input) { value=input; present=bool(input); }
        bool matches(const std::shared_ptr<const T>& input) const {
            if (!present) return !input;
            const auto owned=value.lock();
            return owned && owned==input && !owned.owner_before(input) && !input.owner_before(owned);
        }
    };
    struct ReuseEntry {
        std::uint16_t **slot=nullptr;
        int swim=0;
        unsigned buckets=0;
        bool modified=false, valid=false, leased=false;
        std::uint64_t generation=0;
        std::unique_ptr<std::uint16_t[]> seeds, result;
        Identity<std::vector<std::uint8_t>> water;
        Identity<std::vector<TerrainType>> terrain;
        Identity<TerrainRegistry> registry;
        Identity<TerrainMovementSnapshot> profiles;
        bool matches(const Job& job, std::size_t cells) const {
            return valid && buckets==job.terrainBuckets && modified==job.modifiedCosts &&
                water.matches(job.water) && terrain.matches(job.terrain) &&
                registry.matches(job.registry) && profiles.matches(job.profiles) &&
                std::equal(seeds.get(),seeds.get()+cells,job.data.get());
        }
    };
    std::array<ReuseEntry,ReuseSlots> reuse;
    unsigned reuseCount=0;
    std::uint64_t reuseEpoch=0;
    bool reuseEnabled=false, reuseAdmissionFailed=false;
    // Preparation admits and leases metadata only. A lease lasts until the
    // fixed publication deadline, irrespective of when a worker completes.
    void leasePrepared(Job& job) {
        if (!reuseEnabled || !cells || cells>ReuseBytes/(2*sizeof(std::uint16_t))) return;
        unsigned index=0;
        for (;index<reuseCount;++index)
            if (reuse[index].slot==job.slot && reuse[index].swim==job.swim) break;
        if (index==reuseCount) {
            if (reuseAdmissionFailed || reuseCount==ReuseSlots ||
                reuseCount>=ReuseBytes/(2*sizeof(std::uint16_t)*cells)) return;
            try {
                auto seeds=std::make_unique<std::uint16_t[]>(cells);
                auto result=std::make_unique<std::uint16_t[]>(cells);
                auto& entry=reuse[index];
                entry.seeds=std::move(seeds); entry.result=std::move(result);
                entry.slot=job.slot; entry.swim=job.swim;
                ++reuseCount;
            } catch (const std::bad_alloc&) {
                reuseAdmissionFailed=true;
                return;
            }
        }
        auto& entry=reuse[index];
        if (entry.leased) return; // Never wait for or share another job's cache storage.
        entry.leased=true; ++entry.generation;
        job.reuseIndex=index; job.reuseGeneration=entry.generation; job.reuseEpoch=reuseEpoch;
    }
    void releaseLease(const Job& job) {
        if (job.reuseEpoch!=reuseEpoch || job.reuseIndex>=reuseCount) return;
        auto& entry=reuse[job.reuseIndex];
        if (entry.generation==job.reuseGeneration) entry.leased=false;
    }
    // A propagation worker exclusively owns the leased arrays/identities until
    // done is signalled. Preparation/publication touch only lease metadata;
    // reset joins workers before destroying storage. No live field is read.
    void executeWithReuse(Job& job, GradientWorkspace& scratch) {
        auto* entry=job.reuseIndex<ReuseSlots && job.reuseEpoch==reuseEpoch ?
            &reuse[job.reuseIndex] : nullptr;
        if (entry && entry->matches(job,cells)) {
            std::copy_n(entry->result.get(),cells,job.data.get());
            return;
        }
        if (entry) {
            entry->valid=false;
            std::copy_n(job.data.get(),cells,entry->seeds.get());
            entry->buckets=job.terrainBuckets; entry->modified=job.modifiedCosts;
            entry->water.set(job.water); entry->terrain.set(job.terrain);
            entry->registry.set(job.registry); entry->profiles.set(job.profiles);
        }
        work(job,scratch);
        if (entry) {
            std::copy_n(job.data.get(),cells,entry->result.get());
            entry->valid=true;
        }
    }
	using Clock = std::chrono::steady_clock;
	static std::uint64_t ns(Clock::time_point start) {
		return std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now()-start).count();
	}
	void execute(Job &job, GradientWorkspace &scratch) noexcept {
		const auto start = Clock::now();
		try
		{
			executeWithReuse(job, scratch);
			job.water.reset();
			job.terrain.reset();
			job.registry.reset();
			job.profiles.reset();
		}
		catch (...)
		{
			job.error = std::current_exception();
		}
		activeNs.fetch_add(ns(start), std::memory_order_relaxed);
		{ std::lock_guard<std::mutex> lock(mutex); job.done = true; }
		completed.notify_one();
	}
	void wait(Job &job) {
		const auto start = Clock::now();
		std::unique_lock<std::mutex> lock(mutex);
		completed.wait(lock, [&] { return job.done; });
		metrics.waitNs += ns(start);
	}
public:
	~GradientPipeline() { reset(); }
	bool enabled() const { return delay != 0; }
	unsigned workerCount() const { return workers.size(); }
	unsigned delayTicks() const { return delay; }
	std::uint64_t activeElapsedNs() const { return activeNs.load(std::memory_order_relaxed); }
    // Internal opt-in: callback must depend only on the captured immutable inputs
    // and configure-time geometry. Generic callbacks retain normal execution.
    void enableResultReuseForPureWork() { reuseEnabled=true; }
	void finish() { for (auto &job : pending) { wait(*job); if(job->error) std::rethrow_exception(job->error); } }
	void reset() noexcept {
		{ std::lock_guard<std::mutex> lock(mutex); quit = true; }
		wake.notify_all();
		for (auto &thread : workers) thread.join();
		workers.clear(); ready.clear(); pending.clear(); spare.clear();
		delay = 0; tick = 0; lastSubmission = 0; quit = false;
        for (auto& entry:reuse) entry=ReuseEntry{};
        reuseCount=0; ++reuseEpoch; reuseEnabled=false; reuseAdmissionFailed=false;
	}
	void configure(unsigned count, unsigned ticks, std::size_t size, Work callback,
		Factory factory = [](std::function<void()> f) { return GAGCore::ThreadSupport::launch(std::move(f)); }) {
		reset(); metrics = {}; activeNs = 0; cells = size; work = std::move(callback);
		if constexpr (GAGCore::ThreadSupport::available)
		{
			try {
				workers.reserve(count);
				for (unsigned n=0; n<count; ++n) workers.push_back(factory([this] {
					GradientWorkspace scratch;
					for (;;) {
						Job *job;
						{
							std::unique_lock<std::mutex> lock(mutex);
							wake.wait(lock, [&] { return quit || !ready.empty(); });
							if (ready.empty()) return;
							job = ready.front(); ready.pop_front();
						}
						execute(*job, scratch);
					}
				}));
			} catch (...) { reset(); } // Same publication schedule with serial execution.
		}
		delay = ticks;
	}
	// Saving completes private work without changing publication deadlines.
	template<class Visitor> void visitPendingSnapshots(Visitor visitor) {
		finish();
		for (const auto &job : pending)
			visitor(PendingSnapshot{job->slot, job->data.get(),
				static_cast<unsigned>(job->due-tick), job->superseded});
	}
	std::size_t pendingCount() const { return pending.size(); }
	void restoreCompleted(RestoredSnapshot snapshot) {
		if (!enabled() || !snapshot.slot || !*snapshot.slot || !snapshot.data ||
			!snapshot.remaining || snapshot.remaining>delay || pending.size()>=delay ||
			(!pending.empty() && pending.back()->due>=tick+snapshot.remaining))
			throw std::runtime_error("Invalid saved gradient deadline or destination");
		auto job=std::make_unique<Job>();
		job->slot=snapshot.slot; job->swim=snapshot.swim; job->due=tick+snapshot.remaining;
		job->superseded=snapshot.superseded; job->done=true; job->data=std::move(snapshot.data);
		pending.push_back(std::move(job));
	}
	// Execution is local configuration, never part of saved simulation state.
	void setWorkerCount(unsigned count) {
		finish();
		auto savedPending=std::move(pending);
		auto savedSpare=std::move(spare);
		const auto savedTick=tick, savedSubmission=lastSubmission, savedActive=activeElapsedNs();
		const auto savedMetrics=metrics;
        const bool savedReuse=reuseEnabled;
		configure(count, delay, cells, work);
        reuseEnabled=savedReuse;
		pending=std::move(savedPending); spare=std::move(savedSpare);
		tick=savedTick; lastSubmission=savedSubmission; metrics=savedMetrics; activeNs=savedActive;
	}
	// Publish before the teams step; preparation observes the completed previous tick.
	void advance() {
		++tick;
		while (!pending.empty() && pending.front()->due <= tick) {
			auto &job = *pending.front(); wait(job);
			if (job.error) std::rethrow_exception(job.error);
            releaseLease(job);
			if (!job.superseded) {
				auto *old = *job.slot; *job.slot = job.data.release(); job.data.reset(old);
				++metrics.published;
			} else ++metrics.discarded;
			spare.push_back(std::move(pending.front())); pending.pop_front();
		}
	}
	// Main-thread synchronous refreshes supersede any older snapshot of this slot.
	void invalidate(std::uint16_t **slot) {
		for (auto &job : pending) if (job->slot == slot) job->superseded = true;
	}
	Job *reserve(std::uint16_t **slot, int swim) {
		if (!enabled() || tick == lastSubmission || pending.size() >= delay)
			throw std::logic_error("gradient pipeline requires one submission per advanced tick");
		std::unique_ptr<Job> job;
		if (spare.empty()) { job = std::make_unique<Job>(); job->data.reset(new std::uint16_t[cells]); }
		else { job = std::move(spare.back()); spare.pop_back(); }
		job->slot=slot; job->swim=swim; job->due=tick+delay;
		job->done=false; job->superseded=false; job->error=nullptr;
        job->reuseIndex=ReuseSlots; job->reuseGeneration=0; job->reuseEpoch=0;
		auto *ptr=job.get(); pending.push_back(std::move(job));
		lastSubmission = tick;
		++metrics.jobs;
		metrics.maxPending = std::max<std::uint64_t>(metrics.maxPending, pending.size());
		return ptr;
	}
	// The owner reserves before the read-only batch; only this job's private
	// inputs are written during preparation. Queue membership stays unchanged.
	template<class Seed> void prepare(Job *ptr, Seed &&seed) {
		try {
			seed(*ptr);
            leasePrepared(*ptr);
			if (workers.empty()) execute(*ptr, serialWorkspace);
			else { { std::lock_guard<std::mutex> lock(mutex); ready.push_back(ptr); } wake.notify_one(); }
		}
		catch (...) {
			{ std::lock_guard<std::mutex> lock(mutex); ptr->error=std::current_exception(); ptr->done=true; }
			completed.notify_one();
			throw;
		}
	}
	template<class Seed> void submit(std::uint16_t **slot, int swim, Seed &&seed) {
		prepare(reserve(slot, swim), std::forward<Seed>(seed));
	}
};
