// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <array>
#include <cstdint>
#include <iosfwd>
#include <string>

// Diagnostic wall-clock measurements. Never inputs to simulation or serialization.
namespace PerformanceTelemetry
{
enum class Id : unsigned
{
#define PERF_SCOPE(id, name, stride) id,
#include "PerformanceScopes.inc"
#undef PERF_SCOPE
	Count
};
constexpr unsigned ScopeCount = static_cast<unsigned>(Id::Count);
struct Moments
{
	std::uint64_t count = 0, total = 0, maximum = 0;
	double mean = 0, m2 = 0;
	void add(std::uint64_t ns);
	void merge(const Moments &other);
};
struct Metric
{
	std::uint64_t calls = 0, self = 0;
	Moments time;
	bool selfComplete = true;
	void merge(const Metric &other);
};
struct Budget
{
	std::uint64_t count = 0, exceeded = 0, excess = 0, worst = 0, streak = 0, longest = 0;
	void add(std::uint64_t duration, std::uint64_t budget);
};
struct Actor
{
	int player = -1, team = -1, implementation = -1;
	std::uint32_t generation = 0;
	Metric window, total;
};
using Clock = std::uint64_t (*)();
std::uint64_t now();
struct Collector
{
	static constexpr unsigned MaxActors = 128;
	std::array<Metric, ScopeCount> window{}, total{};
	std::array<std::uint64_t, ScopeCount> calls{};
	std::array<Actor, MaxActors> actors{};
	std::array<unsigned, 32> actorCache{};
	unsigned actorCount = 0, droppedActors = 0, phase = 0, depth = 0, droppedScopes = 0;
	std::uint64_t started = 0, windowStart = 0, lastPresentation = 0, lastInterval = 0;
	std::uint64_t session = 0, tickStart = 0, coverageTick = 0, lastTick = 0, budgetNs = 0,
				  presentationBudgetNs = 0;
	std::uint64_t saved = 0, failed = 0, superseded = 0;
	Budget workBudget{}, frameBudget{}, totalWorkBudget{}, totalFrameBudget{};
	bool enabled = true, output = false, described = false, running = false;
	std::string mode = "startup";
	Clock clock = now;
	void reset();
	int actor(int player, int team, int implementation, std::uint32_t generation);
	void record(Id id, std::uint64_t duration);
	void merge(Id id, const Moments &durations);
	void configure(std::uint64_t tick, std::uint64_t budget, std::uint64_t frameBudget,
				   const std::string &newMode);
	void presented();
	void capture(std::uint64_t tick, bool force = false, bool final = false);
	void describe(const std::string &metadata);
	void write(std::ostream &out, const char *record, std::uint64_t tick, bool cumulative);
};
Collector &collector();

// Stack is thread-local; background workers publish explicit aggregates instead.
class Scope
{
	Collector *c = nullptr;
	Scope *parent = nullptr;
	Id id;
	std::uint64_t start = 0, children = 0, excluded = 0;
	int actorIndex = -1;
	bool complete = true;

  public:
	explicit Scope(Id id, int actor = -1);
	~Scope();
	Scope(const Scope &) = delete;
	Scope &operator=(const Scope &) = delete;
	void stop();
};
} // namespace PerformanceTelemetry
#define PERF_JOIN_INNER(a, b) a##b
#define PERF_JOIN(a, b) PERF_JOIN_INNER(a, b)
#define PERF_SCOPE_TIME(id)                                                                        \
	PerformanceTelemetry::Scope PERF_JOIN(perfScope_, __LINE__)(PerformanceTelemetry::Id::id)
