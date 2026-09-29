// SPDX-License-Identifier: GPL-3.0-or-later
#include <PerformanceTelemetry.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <iomanip>
#include <iostream>

namespace PerformanceTelemetry
{
namespace
{
struct Descriptor
{
	const char *name;
	unsigned stride;
};
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
void printBudget(std::ostream &out, const char *name, const Budget &b)
{
	out << ' ' << name << ".count=" << b.count << ' ' << name << ".overruns=" << b.exceeded << ' '
		<< name << ".excess_ns=" << b.excess << ' ' << name << ".worst_ns=" << b.worst << ' '
		<< name << ".longest_streak=" << b.longest;
}
void printMetric(std::ostream &out, const Metric &m, unsigned stride)
{
	out << " calls=" << m.calls << " samples=" << m.time.count << " stride=" << stride;
	if (!m.time.count)
	{
		out << " mean_ns=na stddev_ns=na max_ns=na total_ns=na self_ns=na";
		return;
	}
	out << " mean_ns=" << m.time.mean
		<< " stddev_ns=" << std::sqrt(std::max(0.0, m.time.m2 / m.time.count))
		<< " max_ns=" << m.time.maximum << " observed_total_ns=" << m.time.total;
	if (stride == 1 && m.calls == m.time.count)
		out << " total_ns=" << m.time.total;
	else if (stride == 1)
		out << " total_ns=na";
	else
		out << " estimated_total_ns=" << m.time.mean * m.calls;
	if (m.selfComplete && stride == 1 && m.calls == m.time.count)
		out << " self_ns=" << m.self;
	else
		out << " self_ns=na";
}
} // namespace
std::uint64_t now()
{
	return std::chrono::duration_cast<std::chrono::nanoseconds>(
			   std::chrono::steady_clock::now().time_since_epoch())
		.count();
}
void Moments::add(std::uint64_t ns)
{
	++count;
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
Collector &collector()
{
	static thread_local Collector c;
	return c;
}
void Collector::reset()
{
	const auto next = session + 1;
	const auto oldClock = clock;
	*this = Collector{};
	clock = oldClock;
	session = next;
	enabled = std::getenv("GLOB2_PERF_DISABLE") == nullptr;
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
}
void Collector::merge(Id id, const Moments &durations)
{
	if (!enabled)
		return;
	auto &m = window[index(id)];
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
				  << " stride=" << descriptors[i].stride << " stddev=population\n";
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
			<< " presentation_budget_ns=" << (cumulative ? 0 : presentationBudgetNs);
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
			out << " thread=main";
		printMetric(out, m, descriptors[i].stride);
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
	if (!force && tick / 512 == lastTick / 512 && t - windowStart < 5000000000ULL)
		return;
	const auto outputStart = output ? clock() : 0;
	if (output)
		write(std::cout, "GLOB2_PERF_SAMPLE", tick, false);
	for (unsigned i = 0; i < ScopeCount; ++i)
	{
		total[i].merge(window[i]);
		window[i] = {};
	}
	for (unsigned i = 0; i < actorCount; ++i)
	{
		actors[i].total.merge(actors[i].window);
		actors[i].window = {};
	}
	const auto workStreak = workBudget.streak, frameStreak = frameBudget.streak;
	workBudget = {};
	frameBudget = {};
	workBudget.streak = workStreak;
	frameBudget.streak = frameStreak;
	lastTick = tick;
	tickStart = tick;
	windowStart = clock();
	++phase;
	if (output)
	{
		// Attribute the interval export to the following interval; no recursive export.
		const auto duration = clock() - outputStart;
		if (final)
		{
			Metric m;
			m.calls = 1;
			m.time.add(duration);
			m.self = duration;
			total[index(Id::Output)].merge(m);
		}
		else
			record(Id::Output, duration);
	}
	if (final && output)
		write(std::cout, "GLOB2_PERF_FINAL", tick, true);
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
