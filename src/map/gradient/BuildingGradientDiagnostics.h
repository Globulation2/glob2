// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <chrono>
#include <cstdint>
#include <ctime>
#include <fstream>
#include <mutex>
#include <stdexcept>
#include <string>
#include <vector>

// Opt-in execution evidence, never simulation state. Capture only inside ticks;
// setup, saving and teardown cannot masquerade as gameplay propagation.
class BuildingGradientDiagnostics
{
  public:
	struct Event
	{
		int gid, swim;
		const char *phase, *kind, *reason;
		std::uint32_t generation, snapshot;
		std::uint64_t start, duration, cpu, popped;
		bool completed;
	};
	static std::uint64_t now()
	{
		return std::chrono::duration_cast<std::chrono::nanoseconds>(
				   std::chrono::steady_clock::now().time_since_epoch())
			.count();
	}
	// Zero means unavailable on this platform. This excludes descheduling;
	// a rebuild scope measures its submitting thread, not initializer workers.
	static std::uint64_t threadCpuNow()
	{
#if defined(CLOCK_THREAD_CPUTIME_ID)
		timespec value{};
		if (clock_gettime(CLOCK_THREAD_CPUTIME_ID, &value) == 0)
			return std::uint64_t(value.tv_sec) * 1000000000ULL + value.tv_nsec;
#endif
		return 0;
	}

  private:
	std::ofstream eventsFile, ticksFile;
	std::mutex mutex;
	std::vector<Event> events;
	std::uint64_t started = 0, startedCpu = 0, tick = 0, dropped = 0;
	std::uint32_t initialGeneration = 0;
	unsigned buildings = 0, flags = 0, units = 0, fields = 0, unfinished = 0;
	bool active = false;
	const char *phase = "other";

  public:
	explicit BuildingGradientDiagnostics(const std::string &prefix)
		: eventsFile(prefix + "-events.csv"), ticksFile(prefix + "-ticks.csv")
	{
		if (!eventsFile || !ticksFile)
			throw std::runtime_error("cannot open building gradient diagnostics");
		eventsFile << "tick,gid,swim,phase,kind,reason,generation,snapshot_generation,start_ns,"
					  "duration_ns,cpu_ns,popped,completed\n";
		ticksFile << "tick,tick_ns,tick_cpu_ns,generation_begin,generation_end,buildings,flags,"
					 "units,fields,unfinished,dropped_events\n";
		events.reserve(4096);
	}
	bool capturing() const { return active; }
	const char *currentPhase() const { return phase; }
	void setPhase(const char *value) { phase = value; }
	void begin(std::uint64_t step, std::uint32_t generation, unsigned bs, unsigned vs, unsigned us,
			   unsigned fs, unsigned pending)
	{
		events.clear();
		dropped = 0;
		tick = step;
		initialGeneration = generation;
		buildings = bs;
		flags = vs;
		units = us;
		fields = fs;
		unfinished = pending;
		phase = "other";
		started = now();
		startedCpu = threadCpuNow();
		active = true;
	}
	void record(Event event)
	{
		if (!active)
			return;
		std::lock_guard<std::mutex> lock(mutex);
		if (events.size() < 65536)
			events.push_back(event);
		else
			++dropped;
	}
	void count(const char *kind, const char *reason, int gid = -1, int swim = -1)
	{
		if (active)
			record({gid, swim, phase, kind, reason, 0, 0, now(), 0, 0, 0, false});
	}
	void end(std::uint32_t generation)
	{
		const auto elapsed = now() - started;
		const auto cpuEnd = threadCpuNow();
		const auto cpu = startedCpu && cpuEnd >= startedCpu ? cpuEnd - startedCpu : 0;
		active = false; // All compute jobs have reached their batch barriers.
		ticksFile << tick << ',' << elapsed << ',' << cpu << ',' << initialGeneration << ','
				  << generation << ',' << buildings << ',' << flags << ',' << units << ',' << fields
				  << ',' << unfinished << ',' << dropped << '\n';
		for (const auto &e : events)
			eventsFile << tick << ',' << e.gid << ',' << e.swim << ',' << e.phase << ',' << e.kind
					   << ',' << e.reason << ',' << e.generation << ',' << e.snapshot << ','
					   << e.start - started << ',' << e.duration << ',' << e.cpu << ',' << e.popped
					   << ',' << e.completed << '\n';
	}
	void flush()
	{
		eventsFile.flush();
		ticksFile.flush();
	}
	class Scope
	{
		BuildingGradientDiagnostics *diagnostic;
		Event event;
		std::uint64_t startedCpu;

	  public:
		Scope(BuildingGradientDiagnostics *d, int gid, int swim, const char *kind,
			  const char *reason, std::uint32_t generation, std::uint32_t snapshot)
			: diagnostic(d && d->capturing() ? d : nullptr),
			  event{gid,
					swim,
					diagnostic ? diagnostic->currentPhase() : "other",
					kind,
					reason,
					generation,
					snapshot,
					diagnostic ? now() : 0,
					0,
					0,
					0,
					false},
			  startedCpu(diagnostic ? threadCpuNow() : 0)
		{
		}
		Scope(const Scope &) = delete;
		Scope &operator=(const Scope &) = delete;
		void result(std::uint64_t popped, bool completed)
		{
			event.popped = popped;
			event.completed = completed;
		}
		~Scope()
		{
			if (diagnostic)
			{
				event.duration = now() - event.start;
				const auto cpuEnd = threadCpuNow();
				event.cpu = startedCpu && cpuEnd >= startedCpu ? cpuEnd - startedCpu : 0;
				diagnostic->record(event);
			}
		}
	};
};
