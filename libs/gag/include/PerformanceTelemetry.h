// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <array>
#include <cstdint>
#include <iosfwd>
#include <string>
#include <memory>
#include <vector>
#include <limits>
#include <utility>

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
	std::array<std::uint64_t, 65> histogram{};
	std::uint64_t percentile(double fraction) const;
};
struct Metric
{
	std::uint64_t calls = 0, self = 0;
	Moments time;
	bool selfComplete = true;
	// Reserved export compatibility fields; always-on collection uses wall time.
	std::uint64_t cpuNs = 0, cpuSamples = 0, selfCPU = 0;
	void merge(const Metric &other);
};
struct Budget
{
	std::uint64_t count = 0, exceeded = 0, excess = 0, worst = 0, streak = 0, longest = 0;
	void add(std::uint64_t duration, std::uint64_t budget);
	void merge(const Budget &other);
};
struct Actor
{
	int player = -1, team = -1, implementation = -1;
	std::uint32_t generation = 0;
	Metric window, total;
};
using Clock = std::uint64_t (*)();
std::uint64_t now();
struct Descriptor
{
	const char *name;
	unsigned stride;
};
const char *unit(unsigned id);
const char *category(unsigned id);
const Descriptor &descriptor(unsigned id);
struct SlowTick
{
	std::uint64_t tick = 0, wallNs = 0, cpuNs = 0, waitNs = 0, ownerJoinNs = 0, lockNs = 0;
	std::array<std::uint64_t, 8> phaseWall{}, phaseCPU{};
};
enum BoundaryFlag : unsigned
{
	RendererChanged = 1,
	PresentationGap = 2,
	Suspended = 4,
	Resumed = 8
};
struct History;

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
	Budget workBudget{}, frameBudget{}, totalWorkBudget{}, totalFrameBudget{}, tickBudget{},
		totalTickBudget{};
	std::uint64_t lastTickBudget = 0;
	bool enabled = true, output = false, described = false, running = false;
	std::string mode = "startup";
	unsigned windowFlags = 0, totalFlags = 0;
	Clock clock = now;
	std::unique_ptr<History> history;
	std::array<SlowTick, 64> slowTicks{};
	void slowTick(const SlowTick &tick);
	std::array<std::uint64_t, 16> renderCounters{};
	std::uintptr_t renderDevice = 0;
	std::uint64_t renderGeneration = 0, displayedSceneTick = ~std::uint64_t(0);
	Collector();
	~Collector();
	Collector(Collector &&) noexcept;
	Collector &operator=(Collector &&) noexcept;
	//! Per scope: 1 = measured on this collector's thread, 2 = absorbed from the
	//! simulation thread (see absorb); exported as the record's thread attribution.
	std::array<std::uint8_t, ScopeCount> threads{};
	void reset();
	//! Move the simulation thread's current window into this collector and hand it
	//! this session's budget and sampling phase. The other thread must be paused.
	void absorb(Collector &other);
	int actor(int player, int team, int implementation, std::uint32_t generation);
	void record(Id id, std::uint64_t duration);
	void import(Id id, const Metric &metric);
	void gpuResult(std::uint64_t frame, std::uint64_t start, std::uint64_t duration,
				   bool renderPasses = false);
	void merge(Id id, const Moments &durations);
	void configure(std::uint64_t tick, std::uint64_t budget, std::uint64_t frameBudget,
				   const std::string &newMode);
	void presented();
	void capture(std::uint64_t tick, bool force = false, bool final = false);
	void describe(const std::string &metadata);
	void write(std::ostream &out, const char *record, std::uint64_t tick, bool cumulative);
};
// Fixed-size history; compaction preserves coverage and mergeable distributions.
struct Window
{
	std::uint64_t tickStart = 0, tickEnd = 0, startNs = 0, endNs = 0, windows = 1;
	std::uint64_t budgetNs = 0, presentationBudgetNs = 0;
	unsigned flags = 0;
	std::array<Metric, ScopeCount> metrics{};
	Budget work, frames, ticks;
	std::array<char, 24> mode{};
	void merge(const Window &other);
};
struct History
{
	static constexpr std::size_t ByteLimit = 12 * 1024 * 1024;
	static constexpr std::size_t Capacity =
		(ByteLimit - 64 * sizeof(SlowTick) - 128) / (sizeof(Window) + sizeof(std::uint16_t));
	std::array<SlowTick, 64> slowTicks{};
	std::size_t size = 0;
	std::uint64_t revision = 0;
	History();
	Window &window(std::size_t i) { return slots[order[i]]; }
	const Window &window(std::size_t i) const { return slots[order[i]]; }
	// Reserve a logical tail slot. The caller overwrites the complete window.
	Window &appendSlot();
	void append(Window &&value) { appendSlot() = std::move(value); }

  private:
	std::array<Window, Capacity> slots{};
	std::array<std::uint16_t, Capacity> order{};
};
static_assert(History::Capacity >= 8);
static_assert(sizeof(History) <= 12 * 1024 * 1024);
Collector &collector();
//! Make collector() return c on the calling thread (null restores its own). The
//! simulation thread records into a collector its runner owns, so the main thread
//! can absorb it while the simulation is parked and after the thread ends.
void bindCollector(Collector *c);
// Worker callbacks publish explicit aggregates; suppress implicit thread-local histories.
void bindWorkerCollector();

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
