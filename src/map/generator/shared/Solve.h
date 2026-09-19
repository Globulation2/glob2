// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "GenerationContext.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>
namespace MapGeneration
{
// Heuristic search over a caller-owned arrangement. Construct hard guarantees where possible,
// search the remaining coupled choices, and validate the finished world independently. A lower
// objective is evidence about the stated preferences, not proof of feasibility or gameplay balance.
// See docs/map-generators/CONSTRAINT_SEARCH.md for composition with the rest of the toolkit.

/// Geometric cooling over attempted moves, including draws that cannot produce a candidate.
/// Temperatures use the objective's units. Named streams and draw order are part of map identity;
/// floating-point scoring is not a promise of identical maps on different platforms.
struct Anneal
{
	int moves = 0;
	double from = 1.0, to = 0.01;
	const char *stream = "solve";

	void validate() const;
	double heat(int move) const
	{
		return moves < 2 ? to
						 : from * std::pow(to / from, double(move) / double(std::max(1, moves - 1)));
	}
};

struct SolveReport
{
	int attempted = 0; // all scheduled draws; attempted - proposed could not produce a candidate
	int proposed = 0, taken = 0, kept = 0; // candidates, accepted moves, new best states
	double before = 0, after = 0, best = 0;
};

/// Metropolis acceptance. Zero heat permits only non-worsening moves. Invalid numeric inputs
/// throw rather than silently corrupting a search; improving moves consume no random draw.
bool accept(double rise, double heat, GenerationContext &, const char *stream);

inline double finiteCost(double value)
{
	if (!std::isfinite(value))
		throw std::domain_error("Map search requires a finite cost");
	return value;
}

/// Low-level callback interface. `propose()` applies one candidate or returns false WITHOUT
/// changing scored state. `undo()` restores the previous arrangement and its scoring caches.
/// `remember()` snapshots the initial/new best state; `recall()` restores it, including caches.
/// `cost()` may refresh scratch measurements, but must consume no RNG or alter the arrangement.
/// Callbacks must not throw after partially applying a move if the caller intends to reuse state.
/// A rejected proposal's scratch measurements need not describe the restored state: callers should
/// recompute their final diagnostics, outside this loop. No additional cost calls or draws are hidden.
template <typename Propose, typename Cost, typename Undo, typename Remember, typename Recall>
SolveReport anneal(const Anneal &schedule, GenerationContext &context, Propose propose, Cost cost,
				   Undo undo, Remember remember, Recall recall)
{
	schedule.validate();
	SolveReport report;
	double current = finiteCost(cost());
	report.before = report.after = report.best = current;
	remember();
	for (int move = 0; move < schedule.moves; ++move)
	{
		++report.attempted;
		if (!propose())
			continue;
		++report.proposed;
		const double after = finiteCost(cost());
		if (accept(after - current, schedule.heat(move), context, schedule.stream))
		{
			current = after;
			++report.taken;
			if (current < report.best)
			{
				report.best = current;
				++report.kept;
				remember();
			}
		}
		else
			undo();
	}
	if (current > report.best)
		recall();
	report.after = report.best;
	return report;
}

/// Prefer this interface for searches with coupled arrangement/cache/snapshot state. The local
/// state type supplies propose(), cost(), undo(), remember(), recall() with the contracts above.
/// No inheritance, copying of the state, or allocation is imposed by the search driver.
template <typename State>
SolveReport anneal(const Anneal &schedule, GenerationContext &context, State &state)
{
	return anneal(schedule, context, [&] { return state.propose(); }, [&] { return state.cost(); },
		[&] { state.undo(); }, [&] { state.remember(); }, [&] { state.recall(); });
}

/// A bounded, allocation-free set of named soft preferences. Names are borrowed and must outlive
/// the objective (normally string literals). Negative weights are permitted for explicit rewards.
/// Invalid terms, duplicate names and overflow fail visibly instead of dropping part of a score.
class Objective
{
  public:
	struct Term
	{
		std::string_view name;
		double weight = 0, residual = 0;
		double weighted() const { return weight * residual; }
	};
	static constexpr int kMaxTerms = 8;
	Objective &add(std::string_view name, double weight, double residual);
	double total() const;
	/// Required lookup: a miss is a programming error. Use findResidual for an optional term.
	double residual(std::string_view name) const;
	std::optional<double> findResidual(std::string_view name) const;
	const Term *begin() const { return terms.data(); }
	const Term *end() const { return terms.data() + count; }

  private:
	std::array<Term, kMaxTerms> terms{};
	int count = 0;
};

/// Draw optional per-seed character and record it under <map>.brief.*. Keep hard requirements out
/// of choose(): connectivity, home capacity and starter access must not depend on an emphasis draw.
/// Draw the brief together before construction. Changing names or draw order changes existing maps.
class Brief
{
  public:
	Brief(GenerationContext &context, std::string map) : context(context), map(std::move(map)) {}
	double target(const char *name, double least, double most);
	void choose(std::vector<const char *> optional, int least, int most);
	bool on(const char *name) const;
	double weight(const char *name, double full) const { return on(name) ? full : 0.0; }

  private:
	GenerationContext &context;
	std::string map;
	std::vector<const char *> chosen;
};

void reportObjective(GenerationTelemetry &, const std::string &key, const Objective &);
/// Range / mean, plus a one-point penalty when any claimant has nothing. All-zero shares return 1;
/// fewer than two claimants return 0. Intended for nonnegative resource/access quantities.
double imbalance(const std::vector<double> &shares);
int spread(const std::vector<int> &shares);
void reportSolve(GenerationTelemetry &, const std::string &key, const SolveReport &);
} // namespace MapGeneration
