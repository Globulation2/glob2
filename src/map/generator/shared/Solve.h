// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "GenerationContext.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>
namespace MapGeneration
{
// Constraint solving: the machinery for a map that says what it wants as a number it can measure on
// a candidate world, and searches the arrangements for one that meets it.
//
// WHEN THIS IS THE RIGHT TOOL, which is worth stating first because usually it is not. A search
// earns its place when all three of these hold:
//
//   * the property is exactly measurable on a candidate, and cheap to measure again;
//   * it couples many decisions at once, so no order of construction gets it right;
//   * the decisions are few, or at least cheap to re-score.
//
// When a property can be built instead, build it: equal ground per colony is growTerritories, an
// equal lake apiece is growLakeBeside, and a search would be a worse way to get either. The two
// maps this came from measured the difference. Equilibrium points a search at the terrain itself
// and lands a little behind the hand-designed landscapes it competes with, because a search over
// terrain mostly rediscovers noise. Tug draws its country with the ordinary toolkit and points a
// search at one decision - which sites in its no-man's land carry the prizes - and without that
// search no seed at all produces a valid map. Aim it narrowly.
//
// A map using this owes its reader two things: the targets stated as numbers, and the cost with and
// without the search, so the search's worth can be read off rather than taken on trust. SolveReport
// and reportSolve below are that record.

/// A geometric cooling schedule for an annealing run. Heat falls from `from` to `to` across
/// `moves` proposals; a proposal that worsens the cost by `rise` is taken with probability
/// e^(-rise / heat), so the search wanders early and only improves late. Heat is in the same units
/// as the cost, so a schedule has to be chosen against the cost function it anneals: `from` about
/// the size of a typical bad move, `to` small enough that the last moves are strictly downhill.
struct Anneal
{
	int moves = 0;
	double from = 1.0, to = 0.01;
	/// The context stream every draw comes from, so the same seed replays the same search.
	const char *stream = "solve";
	double heat(int move) const
	{
		return moves < 2 ? to
						 : from * std::pow(to / from, double(move) / double(std::max(1, moves - 1)));
	}
};

/// What a run did: how many proposals it made and kept, and the cost before and after. The pair of
/// costs is the map's evidence that the search was worth running.
struct SolveReport
{
	int proposed = 0, taken = 0;
	/// How many times the search bettered everything it had seen, which is the figure that says
	/// whether the schedule is doing anything: a run that keeps nothing after the first few moves is
	/// either already solved or annealing far too cold.
	int kept = 0;
	double before = 0, after = 0, best = 0;
};

/// The Metropolis rule: a proposal that improves the cost is always taken, and one that worsens it
/// by `rise` is taken with probability e^(-rise / heat). The draw comes from the named stream, so
/// the decision is a function of the seed and nothing else.
bool accept(double rise, double heat, GenerationContext &, const char *stream);

/// Anneals an arrangement in place.
///
/// `propose()` makes one candidate change and returns whether it made one - false is not a
/// failure, it is how a rejection-sampled draw says it found nothing this time, and costs the run
/// only that proposal. `cost()` scores the arrangement as it currently stands. `undo()` puts back
/// exactly what the last `propose()` changed, and is called only when the proposal is refused.
///
/// `remember()` is called whenever the arrangement betters everything seen so far, and `recall()`
/// once at the end if the walk finished somewhere worse than its best. A search ends wherever its
/// last accepted move left it, and on a schedule even slightly too warm at the end that is not the
/// best place it visited: before this existed, Equilibrium's shape pass finished worse than it
/// started on five seeds out of twelve, having passed through much better arrangements on the way.
/// Keeping the best costs one snapshot per improvement, which is far rarer than a proposal.
///
/// The caller owns the arrangement; this owns only the schedule, the draws and the bookkeeping.
template <typename Propose, typename Cost, typename Undo, typename Remember, typename Recall>
SolveReport anneal(const Anneal &schedule, GenerationContext &context, Propose propose, Cost cost,
				   Undo undo, Remember remember, Recall recall)
{
	SolveReport report;
	double current = cost();
	report.before = report.after = report.best = current;
	remember();
	for (int move = 0; move < schedule.moves; ++move)
	{
		if (!propose())
			continue;
		++report.proposed;
		const double after = cost();
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

/// The same search without best-keeping, for an arrangement too expensive to snapshot or a cost
/// that is already monotone.
template <typename Propose, typename Cost, typename Undo>
SolveReport anneal(const Anneal &schedule, GenerationContext &context, Propose propose, Cost cost,
				   Undo undo)
{
	return anneal(schedule, context, propose, cost, undo, [] {}, [] {});
}

/// How unequally a quantity is shared out between claimants: the spread over the mean, so the
/// figure does not depend on the quantity's units and weighs against other residuals. 0 for a
/// single claimant. A claimant with nothing at all is not merely behind - it has no economy of that
/// thing - so that costs a whole extra point rather than being hidden by a healthy mean.
double imbalance(const std::vector<double> &shares);

/// The spread between the best and worst served claimant, in the quantity's own units.
int spread(const std::vector<int> &shares);

/// The standard telemetry for a search, under `key`: the proposals it made and kept, and the cost
/// it started and finished at. A map that records this can always answer "what did the search buy?"
/// for any seed, which is the question a reviewer should ask of a solved map.
void reportSolve(GenerationTelemetry &, const std::string &key, const SolveReport &);
} // namespace MapGeneration
