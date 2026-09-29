// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "GradientWorkspace.h"
#include <atomic>
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
#include <stdexcept>
#include <vector>

// Experimental fixed-tick publication. The simulation thread owns pending/free
// and all slot pointers; workers own only job data/water until done is signalled.
// Exactly one submit per advance. Completion time never selects publication time.
class GradientPipeline
{
public:
	struct Job {
		std::uint16_t **slot = nullptr;
		std::unique_ptr<std::uint16_t[]> data;
		std::vector<std::uint8_t> water;
		int swim = 0;
		std::uint64_t due = 0;
		bool superseded = false, done = false;
		std::exception_ptr error;
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
	using Clock = std::chrono::steady_clock;
	static std::uint64_t ns(Clock::time_point start) {
		return std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now()-start).count();
	}
	void execute(Job &job, GradientWorkspace &scratch) noexcept {
		const auto start = Clock::now();
		try { work(job, scratch); } catch (...) { job.error = std::current_exception(); }
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
	void finish() { for (auto &job : pending) { wait(*job); if(job->error) std::rethrow_exception(job->error); } }
	void reset() noexcept {
		{ std::lock_guard<std::mutex> lock(mutex); quit = true; }
		wake.notify_all();
		for (auto &thread : workers) thread.join();
		workers.clear(); ready.clear(); pending.clear(); spare.clear();
		delay = 0; tick = 0; lastSubmission = 0; quit = false;
	}
	void configure(unsigned count, unsigned ticks, std::size_t size, Work callback,
		Factory factory = [](std::function<void()> f) { return std::thread(std::move(f)); }) {
		reset(); metrics = {}; activeNs = 0; cells = size; work = std::move(callback);
#ifndef __EMSCRIPTEN__
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
#endif
		delay = ticks;
	}
	// Saving completes private work without changing publication deadlines.
	template<class Visitor> void visitPending(Visitor visitor) {
		finish();
		for (const auto &job : pending) visitor(*job, static_cast<unsigned>(job->due-tick));
	}
	std::size_t pendingCount() const { return pending.size(); }
	void restoreCompleted(std::uint16_t **slot, int swim, unsigned remaining,
		bool superseded, std::unique_ptr<std::uint16_t[]> data) {
		if (!enabled() || !slot || !*slot || !data || !remaining || remaining>delay ||
			pending.size()>=delay || (!pending.empty() && pending.back()->due>=tick+remaining))
			throw std::runtime_error("Invalid saved gradient deadline or destination");
		auto job=std::make_unique<Job>();
		job->slot=slot; job->swim=swim; job->due=tick+remaining;
		job->superseded=superseded; job->done=true; job->data=std::move(data);
		pending.push_back(std::move(job));
	}
	// Execution is local configuration, never part of saved simulation state.
	void setWorkerCount(unsigned count) {
		finish();
		auto savedPending=std::move(pending);
		auto savedSpare=std::move(spare);
		const auto savedTick=tick, savedSubmission=lastSubmission, savedActive=activeElapsedNs();
		const auto savedMetrics=metrics;
		configure(count, delay, cells, work);
		pending=std::move(savedPending); spare=std::move(savedSpare);
		tick=savedTick; lastSubmission=savedSubmission; metrics=savedMetrics; activeNs=savedActive;
	}
	// Call before the teams step; seed at the original end-of-tick map boundary.
	void advance() {
		++tick;
		while (!pending.empty() && pending.front()->due <= tick) {
			auto &job = *pending.front(); wait(job);
			if (job.error) std::rethrow_exception(job.error);
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
	template<class Seed> void submit(std::uint16_t **slot, int swim, Seed seed) {
		if (!enabled() || tick == lastSubmission || pending.size() >= delay)
			throw std::logic_error("gradient pipeline requires one submission per advanced tick");
		lastSubmission = tick;
		std::unique_ptr<Job> job;
		if (spare.empty()) { job = std::make_unique<Job>(); job->data.reset(new std::uint16_t[cells]); }
		else { job = std::move(spare.back()); spare.pop_back(); }
		job->slot=slot; job->swim=swim; job->due=tick+delay;
		job->done=false; job->superseded=false; job->error=nullptr;
		seed(*job); // Snapshot all mutable inputs before dispatch.
		auto *ptr=job.get(); pending.push_back(std::move(job));
		++metrics.jobs;
		metrics.maxPending = std::max<std::uint64_t>(metrics.maxPending, pending.size());
		if (workers.empty()) execute(*ptr, serialWorkspace);
		else { { std::lock_guard<std::mutex> lock(mutex); ready.push_back(ptr); } wake.notify_one(); }
	}
};
