// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstdint>
#include <array>
#include <iosfwd>
#include "HiringJobs.h"
#include <span>
#include <vector>
#include <atomic>
class Building;
class Unit;
class Team;

namespace HiringDiagnostics
{
enum class Phase : unsigned
{
	Attempt,
	CarryingScan,
	FetchScan,
	FlagScan,
	Selection,
	Subscription,
	Swap,
	FieldInitialization,
	FieldExtension,
	Count
};
enum class Counter : unsigned
{
	Visits,
	Qualifications,
	BuildingQueries,
	ResourceQueries,
	Hires,
	FailedAttempts,
	Busy,
	Rejected,
	Slots,
	Count
};
struct Row
{
	struct Timing
	{
		std::uint64_t calls = 0, ns = 0;
	};
	std::array<Timing, unsigned(Phase::Count)> phases{};
	std::array<std::uint64_t, unsigned(Counter::Count)> counters{};
	BuildingRef building;
	unsigned tick = 0;
	int type = -1;
	std::uint64_t configuration = 0;
};
extern thread_local Row *active;
extern std::atomic<bool> collecting;
// Explicit session configuration; diagnostic state never enters the game.
void configure(bool enabled);
bool enabled();
inline void add(Counter counter, std::uint64_t count = 1)
{
	if (collecting.load(std::memory_order_relaxed) && active)
		active->counters[unsigned(counter)] += count;
}
void write(std::ostream &output);
class PeriodicSwapScope
{
	unsigned team;
	std::uint64_t start = 0;

  public:
	explicit PeriodicSwapScope(const Team &);
	~PeriodicSwapScope();
};
struct CaptureMetrics
{
	std::uint64_t ns = 0, logicalBytes = 0, copiedBytes = 0, fundingGrowths = 0, observations = 0;
};
CaptureMetrics captureMetrics();
void recordCapture(std::uint64_t ns, std::uint64_t bytes, std::uint64_t growths,
				   std::uint64_t copiedBytes);
class Scope
{
	Row *row = nullptr;
	Row *previous = nullptr;
	Phase phase;
	std::uint64_t start = 0;

  public:
	Scope(const Building &building, Phase phase);
	explicit Scope(Phase phase); // Expensive route work attributed to its caller.
	~Scope();
	Scope(const Scope &) = delete;
	Scope &operator=(const Scope &) = delete;
};
} // namespace HiringDiagnostics
