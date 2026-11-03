// SPDX-License-Identifier: GPL-3.0-or-later
#include <PerformanceTelemetry.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <utility>
#include <bit>
#include <cstring>
#include <string_view>

namespace PerformanceTelemetry
{
namespace
{
constexpr Descriptor descriptors[] = {
#define PERF_SCOPE(id, name, stride) {name, stride},
#include <PerformanceScopes.inc>
#undef PERF_SCOPE
};
thread_local Scope *top = nullptr;
unsigned index(Id id)
{
	return static_cast<unsigned>(id);
}
const char *measurementSource(unsigned id)
{
	if (Id(id) == Id::GPUTime)
		return "gpu_elapsed_query";
	if (Id(id) == Id::GPURenderPass)
		return "gpu_timestamp_query";
	return std::strcmp(unit(id), "ns") ? "counter_or_gauge" : "monotonic_wall";
}
const char *measurementPopulation(unsigned id)
{
	if (Id(id) == Id::GPUTime)
		return "sampled_gpu_frames";
	if (Id(id) == Id::GPURenderPass)
		return "sampled_gpu_pass_sums";
	if (Id(id) == Id::RenderMemory)
		return "presented_frame_gauge";
	if (id >= index(Id::RenderPackets) && id <= index(Id::DeviceCapture))
		return "presented_frame_counter_deltas";
	if (Id(id) == Id::RenderDeferred || Id(id) == Id::SceneReused)
		return "observed_frame_events";
	return "completed_operations";
}
void printBudget(std::ostream &out, const char *name, const Budget &b)
{
	out << ' ' << name << ".count=" << b.count << ' ' << name << ".overruns=" << b.exceeded << ' '
		<< name << ".excess_ns=" << b.excess << ' ' << name << ".worst_ns=" << b.worst << ' '
		<< name << ".longest_streak=" << b.longest;
}
void printMetric(std::ostream &out, const Metric &m, unsigned stride, const char *units = "ns")
{
	out << " calls=" << m.calls << " samples=" << m.time.count << " stride=" << stride;
	if (!m.time.count)
	{
		out << " mean_ns=na stddev_ns=na max_ns=na total_ns=na self_ns=na";
		return;
	}
	if (std::strcmp(units, "ns"))
	{
		out << " unit=" << units << " total_value=" << m.time.total << " mean_value=" << m.time.mean
			<< " max_value=" << m.time.maximum;
		return;
	}
	out << " mean_ns=" << m.time.mean
		<< " stddev_ns=" << std::sqrt(std::max(0.0, m.time.m2 / m.time.count))
		<< " max_ns=" << m.time.maximum << " observed_total_ns=" << m.time.total
		<< " m2_ns2=" << m.time.m2;
	if (stride == 1 && m.calls == m.time.count)
		out << " total_ns=" << m.time.total;
	else if (stride == 1)
		out << " total_ns=na";
	else
		out << " estimated_total_ns=" << m.time.mean * m.calls;
	out << " cpu_samples=" << m.cpuSamples << " cpu_total_ns=";
	if (m.cpuSamples)
		out << m.cpuNs;
	else
		out << "na";
	out << " p50_ns=" << m.time.percentile(.5) << " p95_ns=" << m.time.percentile(.95)
		<< " p99_ns=" << m.time.percentile(.99);
	out << " histogram=\"";
	for (unsigned i = 0; i < m.time.histogram.size(); ++i)
	{
		if (i)
			out << ',';
		out << m.time.histogram[i];
	}
	out << "\"";
	if (m.selfComplete && stride == 1 && m.calls == m.time.count)
		out << " self_ns=" << m.self
			<< " self_cpu_ns=" << (m.cpuSamples == m.time.count ? std::to_string(m.selfCPU) : "na");
	else
		out << " self_ns=na self_cpu_ns=na";
}
} // namespace
std::uint64_t now()
{
	return std::chrono::duration_cast<std::chrono::nanoseconds>(
			   std::chrono::steady_clock::now().time_since_epoch())
		.count();
}
const Descriptor &descriptor(unsigned id)
{
	return descriptors[id];
}
const char *unit(unsigned id)
{
	const std::string_view name(descriptors[id].name);
	if (name.ends_with(".wait_tick") || name.ends_with(".incomplete_deadline") ||
		Id(id) == Id::TickWaited)
		return "count";
	switch (Id(id))
	{
	case Id::RenderMemory:
	case Id::RenderUploads:
		return "bytes";
	case Id::RenderPackets:
	case Id::RenderBatches:
	case Id::CaptureDrops:
	case Id::CaptureCount:
	case Id::RenderDeferred:
	case Id::SceneReused:
	case Id::GPUSkipped:
	case Id::GPUInvalid:
	case Id::CausalDropped:
	case Id::GPUUnresolved:
		return "count";
	default:
		return "ns";
	}
}
const char *category(unsigned id)
{
	const std::string_view name(descriptors[id].name);
	if (name.starts_with("compute."))
		return "executor";
	if (name.starts_with("render."))
		return "rendering";
	if (name.starts_with("pacing."))
		return "pacing";
	if (name.starts_with("gradient.") || name.starts_with("path."))
		return "gradients";
	if (name.starts_with("ai."))
		return "ai";
	if (name.starts_with("save."))
		return "saves";
	return "engine";
}
Collector::Collector() = default;
Collector::~Collector() = default;
Collector::Collector(Collector &&) noexcept = default;
Collector &Collector::operator=(Collector &&) noexcept = default;
std::uint64_t Moments::percentile(double fraction) const
{
	if (!count)
		return 0;
	const auto target = std::uint64_t(std::ceil(std::clamp(fraction, 0.0, 1.0) * count));
	std::uint64_t sum = 0;
	for (unsigned i = 0; i < histogram.size(); ++i)
	{
		sum += histogram[i];
		if (sum >= std::max<std::uint64_t>(1, target))
			return std::min(maximum, i == 64 ? ~std::uint64_t(0)
									 : i     ? (std::uint64_t(1) << i) - 1
											 : 0);
	}
	return maximum;
}
void Window::merge(const Window &other)
{
	tickEnd = other.tickEnd;
	endNs = other.endNs;
	windows += other.windows;
	flags |= other.flags;
	for (unsigned i = 0; i < ScopeCount; ++i)
		metrics[i].merge(other.metrics[i]);
	work.merge(other.work);
	frames.merge(other.frames);
	ticks.merge(other.ticks);
	if (mode != other.mode)
	{
		mode = {};
		std::strcpy(mode.data(), "mixed");
	}
	if (budgetNs != other.budgetNs)
		budgetNs = 0;
	if (presentationBudgetNs != other.presentationBudgetNs)
		presentationBudgetNs = 0;
}
History::History()
{
	for (std::size_t i = 0; i < Capacity; ++i)
		order[i] = std::uint16_t(i);
}
Window &History::appendSlot()
{
	if (size == Capacity)
	{
		// Merge the adjacent pair with the finest combined coverage. This
		// preserves detail across the whole match instead of repeatedly folding
		// its early hours into one exponentially large bucket. Ties prefer age.
		std::size_t best = 0;
		auto span = [&](std::size_t i)
		{
			const auto &a = window(i), &b = window(i + 1);
			return std::max(b.endNs - a.startNs,
							(b.tickEnd - a.tickStart) * std::uint64_t(33333333));
		};
		auto finest = span(0);
		for (std::size_t i = 1; i + 1 < size; ++i)
			if (span(i) < finest)
			{
				best = i;
				finest = span(i);
			}
		window(best).merge(window(best + 1));
		const auto freed = order[best + 1];
		std::move(order.begin() + best + 2, order.begin() + size, order.begin() + best + 1);
		order[--size] = freed;
	}
	++revision;
	return window(size++);
}
void Moments::add(std::uint64_t ns)
{
	++count;
	++histogram[std::bit_width(ns)];
	total += ns;
	maximum = std::max(maximum, ns);
	const double delta = double(ns) - mean;
	mean += delta / count;
	m2 += delta * (double(ns) - mean);
}
void Moments::merge(const Moments &b)
{
	if (!b.count)
		return;
	if (!count)
	{
		*this = b;
		return;
	}
	for (unsigned i = 0; i < histogram.size(); ++i)
		histogram[i] += b.histogram[i];
	const double delta = b.mean - mean;
	const auto n = count + b.count;
	m2 += b.m2 + delta * delta * (double(count) * b.count / n);
	mean += delta * (double(b.count) / n);
	count = n;
	total += b.total;
	maximum = std::max(maximum, b.maximum);
}
void Metric::merge(const Metric &b)
{
	cpuNs += b.cpuNs;
	cpuSamples += b.cpuSamples;
	selfCPU += b.selfCPU;
	calls += b.calls;
	self += b.self;
	time.merge(b.time);
	selfComplete &= b.selfComplete;
}
void Budget::add(std::uint64_t duration, std::uint64_t budget)
{
	if (!budget)
		return;
	++count;
	if (duration > budget)
	{
		++exceeded;
		excess += duration - budget;
		worst = std::max(worst, duration - budget);
		longest = std::max(longest, ++streak);
	}
	else
		streak = 0;
}
namespace
{
thread_local Collector *boundCollector = nullptr;
}
Collector &collector()
{
	if (boundCollector)
		return *boundCollector;
	// Keep only a pointer in TLS. An inline Collector would reserve its entire
	// histogram storage for every OS thread, even threads bound to another sink.
	static thread_local std::unique_ptr<Collector> c;
	if (!c)
		c = std::make_unique<Collector>();
	return *c;
}
void bindWorkerCollector()
{
	static Collector unused;
	static const bool initialized = []
	{
		unused.enabled = false;
		return true;
	}();
	(void)initialized;
	bindCollector(&unused);
}
void bindCollector(Collector *c)
{
	boundCollector = c;
}
void Budget::merge(const Budget &other)
{
	count += other.count;
	exceeded += other.exceeded;
	excess += other.excess;
	worst = std::max(worst, other.worst);
	longest = std::max(longest, other.longest);
	streak = other.streak;
}
void Collector::slowTick(const SlowTick &tick)
{
	// A min-heap rejects ordinary ticks in constant time. The zero-initialized
	// array is already a valid heap, including after a collector reset/absorb.
	const auto slower = [](const auto &a, const auto &b) { return a.wallNs > b.wallNs; };
	if (tick.wallNs <= slowTicks.front().wallNs)
		return;
	std::pop_heap(slowTicks.begin(), slowTicks.end(), slower);
	slowTicks.back() = tick;
	std::push_heap(slowTicks.begin(), slowTicks.end(), slower);
}
void Collector::absorb(Collector &other)
{
	if (!enabled)
		return;
	windowFlags |= std::exchange(other.windowFlags, 0u);
	for (auto &tick : other.slowTicks)
		if (tick.wallNs)
		{
			slowTick(tick);
			tick = {};
		}
	for (unsigned i = 0; i < ScopeCount; ++i)
	{
		if (other.window[i].calls)
		{
			window[i].merge(other.window[i]);
			threads[i] |= 2;
			other.window[i] = {};
		}
	}
	for (unsigned i = 0; i < other.actorCount; ++i)
	{
		auto &a = other.actors[i];
		if (a.window.calls)
		{
			const int slot = actor(a.player, a.team, a.implementation, a.generation);
			if (slot >= 0)
				actors[unsigned(slot)].window.merge(a.window);
		}
		a.window = {};
	}
	tickBudget.merge(other.tickBudget);
	totalTickBudget.merge(other.tickBudget);
	const auto tickStreak = other.tickBudget.streak;
	other.tickBudget = {};
	other.tickBudget.streak = tickStreak;
	workBudget.merge(other.workBudget);
	totalWorkBudget.merge(other.workBudget);
	const auto streak = other.workBudget.streak;
	other.workBudget = {};
	other.workBudget.streak = streak;
	droppedScopes += std::exchange(other.droppedScopes, 0u);
	droppedActors += std::exchange(other.droppedActors, 0u);
	other.budgetNs = budgetNs;
	other.phase = phase;
}
void Collector::reset()
{
	const auto next = session + 1;
	const auto oldClock = clock;
	*this = Collector{};
	clock = oldClock;
	session = next;
	enabled = true;
	output = std::getenv("GLOB2_TEAM_TIMELINE") != nullptr;
	started = windowStart = clock();
}
int Collector::actor(int player, int team, int implementation, std::uint32_t generation)
{
	if (!enabled)
		return -1;
	if (player >= 0 && player < 32 && actorCache[player] < actorCount)
	{
		const auto slot = actorCache[player];
		const auto &a = actors[slot];
		if (a.player == player && a.team == team && a.implementation == implementation &&
			a.generation == generation)
			return int(slot);
	}
	// Controller replacement is rare; numeric lookup stays allocation-free.
	for (unsigned i = actorCount; i > 0; --i)
	{
		const auto &a = actors[i - 1];
		if (a.player == player && a.team == team && a.implementation == implementation &&
			a.generation == generation)
			return int(i - 1);
	}
	if (actorCount == MaxActors)
	{
		++droppedActors;
		return -1;
	}
	auto &a = actors[actorCount];
	a.player = player;
	a.team = team;
	a.implementation = implementation;
	a.generation = generation;
	if (player >= 0 && player < 32)
		actorCache[player] = actorCount;
	return int(actorCount++);
}
void Collector::record(Id id, std::uint64_t duration)
{
	if (!enabled)
		return;
	auto &m = window[index(id)];
	++m.calls;
	m.time.add(duration);
	m.self += duration;
	threads[index(id)] |= 1;
}
void Collector::import(Id id, const Metric &metric)
{
	window[index(id)].merge(metric);
	threads[index(id)] |= 4;
}
void Collector::gpuResult(std::uint64_t frame, std::uint64_t start, std::uint64_t duration,
						  bool renderPasses)
{
	if (start < started)
		return;
	const auto id = renderPasses ? Id::GPURenderPass : Id::GPUTime;
	if (start >= windowStart)
		record(id, duration);
	else if (history)
	{
		for (std::size_t i = 0; i < history->size; ++i)
		{
			auto &w = history->window(i);
			if (start - started >= w.startNs && start - started < w.endNs)
			{
				Metric m;
				m.calls = 1;
				m.time.add(duration);
				m.self = duration;
				w.metrics[index(id)].merge(m);
				total[index(id)].merge(m);
				++history->revision;
				break;
			}
		}
	}
	// Late samples are separate observations, never revisions of an exported interval.
	if (output)
		std::cout << "GLOB2_PERF_GPU_RESULT session=" << session << " frame=" << frame
				  << " scope=" << descriptor(index(id)).name
				  << " elapsed_start_ns=" << start - started << " duration_ns=" << duration
				  << " schema_version=2\n";
}
void Collector::merge(Id id, const Moments &durations)
{
	if (!enabled)
		return;
	auto &m = window[index(id)];
	threads[index(id)] |= 1;
	m.calls += durations.count;
	m.time.merge(durations);
	m.self += durations.total;
}
void Collector::configure(std::uint64_t tick, std::uint64_t budget, std::uint64_t frameBudget,
						  const std::string &newMode)
{
	if (!enabled)
		return;
	if (!running)
	{
		coverageTick = tickStart = lastTick = tick;
	}
	if (!running || budget != budgetNs || frameBudget != presentationBudgetNs || mode != newMode)
	{
		if (running || window[index(Id::Load)].calls || window[index(Id::Generation)].calls)
			capture(tick, true);
		mode = newMode;
		budgetNs = budget;
		presentationBudgetNs = frameBudget;
		lastPresentation = lastInterval = 0;
		workBudget.streak = this->frameBudget.streak = 0;
		totalWorkBudget.streak = totalFrameBudget.streak = 0;
		tickBudget.streak = totalTickBudget.streak = 0;
		tickStart = tick;
		running = true;
	}
}
void Collector::presented()
{
	if (!enabled)
		return;
	const auto t = clock();
	if (lastPresentation)
	{
		const auto interval = t - lastPresentation;
		record(Id::FrameInterval, interval);
		frameBudget.add(interval, presentationBudgetNs);
		totalFrameBudget.add(interval, presentationBudgetNs);
		if (lastInterval)
			record(Id::Jitter,
				   interval > lastInterval ? interval - lastInterval : lastInterval - interval);
		lastInterval = interval;
	}
	lastPresentation = t;
}
void Collector::describe(const std::string &metadata)
{
	if (!enabled || !output || described)
		return;
	described = true;
	std::cout << "GLOB2_PERF_SESSION session=" << session << " clock=steady_elapsed unit=ns "
			  << metadata << '\n';
	for (unsigned i = 0; i < ScopeCount; ++i)
		std::cout << "GLOB2_PERF_SCHEMA session=" << session << " scope=" << descriptors[i].name
				  << " stride=" << descriptors[i].stride
				  << " stddev=population schema_version=2 unit=" << unit(i)
				  << " category=" << category(i) << " source=" << measurementSource(i)
				  << " cpu_source=unavailable aggregation=inclusive "
					 "percentile=histogram_upper_bound population="
				  << measurementPopulation(i) << '\n';
}
void Collector::write(std::ostream &out, const char *record, std::uint64_t tick, bool cumulative)
{
	const auto flags = out.flags();
	const auto precision = out.precision();
	out << std::dec << std::defaultfloat << std::setprecision(17);
	const auto elapsed = clock() - started;
	const auto prefix = [&]()
	{
		out << record << " session=" << session
			<< " tick_start=" << (cumulative ? coverageTick : tickStart) << " tick=" << tick
			<< " elapsed_start_ns=" << (cumulative ? 0 : windowStart - started)
			<< " elapsed_ns=" << elapsed << " mode=" << (cumulative ? "all" : mode)
			<< " budget_ns=" << (cumulative ? 0 : budgetNs)
			<< " presentation_budget_ns=" << (cumulative ? 0 : presentationBudgetNs)
			<< " boundaries=" << (cumulative ? totalFlags : windowFlags);
	};
	for (unsigned i = 0; i < ScopeCount; ++i)
	{
		const auto &m = cumulative ? total[i] : window[i];
		if (!m.calls)
			continue;
		prefix();
		out << " scope=" << descriptors[i].name;
		if (i == index(Id::SaveHash) || i == index(Id::SaveWrite) || i == index(Id::SaveQueue))
			out << " thread=save_worker_or_fallback";
		else
			out << " thread="
				<< (threads[i] & 4    ? "compute_or_owner"
					: threads[i] == 2 ? "simulation"
					: threads[i] == 3 ? "main+simulation"
									  : "main");
		printMetric(out, m, descriptors[i].stride, unit(i));
		out << '\n';
	}
	for (unsigned i = 0; i < actorCount; ++i)
	{
		const auto &a = actors[i];
		const auto &m = cumulative ? a.total : a.window;
		if (!m.calls)
			continue;
		prefix();
		out << " scope=ai.player player=" << a.player << " team=" << a.team
			<< " implementation=" << a.implementation << " generation=" << a.generation;
		printMetric(out, m, 1);
		out << '\n';
	}
	prefix();
	out << " scope=budgets";
	printBudget(out, "work", cumulative ? totalWorkBudget : workBudget);
	printBudget(out, "presentation", cumulative ? totalFrameBudget : frameBudget);
	printBudget(out, "tick", cumulative ? totalTickBudget : tickBudget);
	out << " saved_total=" << saved << " failed_total=" << failed
		<< " superseded_total=" << superseded << " dropped_scope_samples=" << droppedScopes
		<< " dropped_actor_records=" << droppedActors << '\n';
	out.flags(flags);
	out.precision(precision);
}
void Collector::capture(std::uint64_t tick, bool force, bool final)
{
	if (!enabled)
		return;
	const auto t = clock();
	if (!force && tick / 512 == lastTick / 512 && t - windowStart < 1000000000ULL)
		return;
	const auto outputStart = clock();
	if (!history)
		history = std::make_unique<History>();
	// Write into the retained slot directly: no large temporary or second copy.
	auto &retained = history->appendSlot();
	retained.tickStart = tickStart;
	retained.tickEnd = tick;
	retained.startNs = windowStart - started;
	retained.endNs = t - started;
	retained.windows = 1;
	retained.flags = windowFlags;
	retained.budgetNs = budgetNs;
	retained.presentationBudgetNs = presentationBudgetNs;
	retained.metrics = window;
	retained.work = workBudget;
	retained.frames = frameBudget;
	retained.ticks = tickBudget;
	retained.mode = {};
	std::strncpy(retained.mode.data(), mode.c_str(), retained.mode.size() - 1);
	history->slowTicks = slowTicks;
	if (output)
		write(std::cout, "GLOB2_PERF_SAMPLE", tick, false);
	for (unsigned i = 0; i < ScopeCount; ++i)
	{
		if (!window[i].calls)
			continue;
		total[i].merge(window[i]);
		window[i] = {};
	}
	for (unsigned i = 0; i < actorCount; ++i)
	{
		actors[i].total.merge(actors[i].window);
		actors[i].window = {};
	}
	const auto tickStreak = tickBudget.streak;
	tickBudget = {};
	tickBudget.streak = tickStreak;
	const auto workStreak = workBudget.streak, frameStreak = frameBudget.streak;
	workBudget = {};
	frameBudget = {};
	workBudget.streak = workStreak;
	frameBudget.streak = frameStreak;
	totalFlags |= windowFlags;
	windowFlags = 0;
	lastTick = tick;
	tickStart = tick;
	windowStart = clock();
	++phase;
	{
		// Attribute the interval export to the following interval; no recursive export.
		const auto duration = clock() - outputStart;
		auto &outputMetric = (final ? total : window)[index(Id::Output)];
		++outputMetric.calls;
		outputMetric.time.add(duration);
		outputMetric.self += duration;
	}
	if (final && output)
	{
		write(std::cout, "GLOB2_PERF_FINAL", tick, true);
		for (const auto &slow : slowTicks)
			if (slow.wallNs)
			{
				std::cout << "GLOB2_PERF_SLOW_TICK session=" << session << " tick=" << slow.tick
						  << " wall_ns=" << slow.wallNs
						  << " cpu_ns=" << (slow.cpuNs ? std::to_string(slow.cpuNs) : "na")
						  << " wait_ns=" << slow.waitNs << " owner_join_ns=" << slow.ownerJoinNs
						  << " lock_ns=" << slow.lockNs;
				for (unsigned i = 0; i < slow.phaseWall.size(); ++i)
					std::cout << " phase" << i << "_wall_ns=" << slow.phaseWall[i] << " phase" << i
							  << "_cpu_ns="
							  << (slow.cpuNs ? std::to_string(slow.phaseCPU[i]) : "na");
				std::cout << '\n';
			}
	}
}
Scope::Scope(Id scope, int actor) : id(scope), actorIndex(actor)
{
	auto &current = collector();
	if (!current.enabled)
		return;
	c = &current;
	parent = top;
	const auto i = index(id);
	auto &m = c->window[i];
	++m.calls;
	c->threads[i] |= 1;
	if (c->depth >= 64)
	{
		++c->droppedScopes;
		m.selfComplete = false;
		if (parent)
			parent->complete = false;
		c = nullptr;
		return;
	}
	const auto call = c->calls[i]++;
	const auto stride = descriptors[i].stride;
	if (stride != 1)
	{
		if (parent)
			parent->complete = false;
		m.selfComplete = false;
		if (call % stride != c->phase % stride)
		{
			c = nullptr;
			return;
		}
	}
	++c->depth;
	start = c->clock();
	top = this;
}
Scope::~Scope()
{
	stop();
}
void Scope::stop()
{
	if (!c)
		return;
	const auto elapsed = c->clock() - start;
	const auto duration = id == Id::Work ? elapsed - std::min(elapsed, excluded) : elapsed;
	auto &m = c->window[index(id)];
	m.time.add(duration);
	m.self += elapsed - std::min(elapsed, children);
	m.selfComplete &= complete;
	if (actorIndex >= 0)
	{
		auto &a = c->actors[unsigned(actorIndex)].window;
		++a.calls;
		a.time.add(duration);
		a.self += elapsed - std::min(elapsed, children);
		a.selfComplete &= complete;
	}
	if (id == Id::Work)
	{
		c->workBudget.add(duration, c->budgetNs);
		c->totalWorkBudget.add(duration, c->budgetNs);
	}
	if (id == Id::Sleep || id == Id::NetworkSleep || id == Id::Present)
	{
		for (auto *ancestor = parent; ancestor; ancestor = ancestor->parent)
			if (ancestor->id == Id::Work)
			{
				ancestor->excluded += elapsed;
				break;
			}
	}
	if (parent)
	{
		parent->children += elapsed;
		parent->complete &= complete;
	}
	--c->depth;
	top = parent;
	c = nullptr;
}
} // namespace PerformanceTelemetry
