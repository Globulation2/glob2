// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "GenerationContext.h"
#include <algorithm>
#include <array>
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

/// A cost built out of named terms rather than one number.
///
/// A solved map with one summed cost can say how badly it did and never which target it missed,
/// so tuning it is guesswork: weights get nudged by feel, and a term that is never the binding one
/// goes on being nudged anyway. Naming the terms costs nothing at run time and turns that into
/// evidence - every map reports, per seed, what each target came out at and what each contributed.
///
/// Storage is inline and fixed, so building one is free and the hot loop of a search can use the
/// same code that reports to telemetry. There is no second expression of the weights to drift.
struct Objective
{
	struct Term
	{
		const char *name = "";
		double weight = 0, residual = 0;
		double weighted() const { return weight * residual; }
	};
	static constexpr int kMaxTerms = 8;
	std::array<Term, kMaxTerms> terms{};
	int count = 0;

	/// Adds a term. `residual` is how far the map is from that target, in whatever units make the
	/// target readable; `weight` converts it into the cost's units.
	Objective &add(const char *name, double weight, double residual)
	{
		if (count < kMaxTerms)
			terms[count++] = {name, weight, residual};
		return *this;
	}
	double total() const
	{
		double sum = 0;
		for (int i = 0; i < count; ++i)
			sum += terms[i].weighted();
		return sum;
	}
	/// What a named term came out at, or `missing` if this objective has no such term. A chosen
	/// candidate carries every measurement that chose it, so a caller wanting one of them back can
	/// read it off rather than tracking a shadow copy alongside the search.
	double residual(const char *name, double missing = 0) const
	{
		for (int i = 0; i < count; ++i)
			if (std::string(terms[i].name) == name)
				return terms[i].residual;
		return missing;
	}
};

/// A target drawn per seed instead of fixed.
///
/// This is the answer to a solved map whose seeds all look alike. With fixed targets every seed is
/// handed the identical problem, and a search is very good at finding the same answer to the same
/// question: the layout moves about but the character never does, and twelve seeds come out looking
/// like twelve photographs of one map. Drawing the target is what makes one seed an inland sea and
/// the next a chain of lakes, one a country of big open fields and the next a patchwork.
///
/// The range is the map's design and should be chosen so that both ends are a map worth playing;
/// where in the range a seed lands is the map's variety.
///
/// This is the bare draw. Prefer Brief below, which draws from the map's own stream and records what
/// it drew, so a reader looking at an odd seed can always see what it was asked for before wondering
/// whether the search failed. Reach for this one only where there is no brief to hang it on.
double drawnTarget(GenerationContext &, const char *stream, double least, double most);

/// Which of a map's optional targets this seed is solving to.
///
/// Drawing a target's value widens a map's character; dropping a target altogether widens it much
/// further. A term that is off is not a weak preference but permission - the search is free to do
/// whatever else it likes in that dimension, and what comes out is a different kind of map rather
/// than the same map loosened. One seed cares where the water pinches the routes and the next does
/// not care at all, and the two do not look related.
///
/// Only ever put the optional ones in here. A map's invariants - that every colony is joined to the
/// rest, has ground to build on and a crop it can reach - are not character, and a seed that turned
/// one of them off would not be a varied map but a broken one. The distinction is the whole reason
/// this is a named set rather than a coin flipped over every term.
///
/// `least` and `most` bound how many are drawn, so a seed can neither be handed nothing to solve
/// nor be asked for everything at once, which is the arrangement that makes every seed alike.
class Emphases
{
  public:
	/// Nothing emphasised: a map that has not drawn its optional terms yet, or has none.
	Emphases() = default;
	Emphases(GenerationContext &, const char *stream, std::vector<const char *> optional, int least,
			 int most);
	/// Whether this seed solves to that target. An unknown name is not emphasised.
	bool on(const char *name) const;
	/// The weight to give a term: its full weight when emphasised this seed, nothing when not.
	double weight(const char *name, double full) const { return on(name) ? full : 0.0; }
	const std::vector<const char *> &active() const { return chosen; }
	/// Records which targets this seed was given, so an unusual map can be read as the brief it was
	/// solving rather than mistaken for a search that went wrong.
	void report(GenerationTelemetry &, const std::string &key) const;

  private:
	std::vector<const char *> chosen;
};

/// What a map was asked to make this seed: the targets it drew and the optional ones it chose.
///
/// Both halves of this were conventions before they were a type, and conventions are the things
/// that rot. Every drawn target was followed by a hand-written telemetry line recording it - in all
/// seven places the first two maps drew one - and the stream and the telemetry key were spelled out
/// separately at every call, always as `<map>-brief` and `<map>.brief.<name>`. Nothing enforced
/// either, so a reader chasing an odd seed had to trust that nobody had forgotten a line, and two
/// strings that must agree were free to drift apart. Holding the map's name once makes the record a
/// consequence of drawing rather than a thing to remember.
///
/// Draw the whole brief in one place at the top of a design, so what a seed was asked for can be
/// read without following the design through.
class Brief
{
  public:
	Brief(GenerationContext &context, std::string map) : context(context), map(std::move(map)) {}

	/// A target for this seed, drawn from `least` to `most` and recorded. The range is the map's
	/// design and both ends should be worth playing; where in it a seed lands is the map's variety.
	double target(const char *name, double least, double most);

	/// Which of the optional terms this seed solves to, drawn and recorded. Only ever the optional
	/// ones: a map's invariants are not character, and a seed that switched one off would not be a
	/// varied map but a broken one. See Emphases, which this is.
	void choose(std::vector<const char *> optional, int least, int most);

	/// Whether this seed solves to that target. An unknown name is not emphasised.
	bool on(const char *name) const { return emphases.on(name); }
	/// A term's weight: its full weight when this seed emphasises it, nothing when not.
	double weight(const char *name, double full) const { return emphases.weight(name, full); }

  private:
	GenerationContext &context;
	std::string map;
	Emphases emphases;
};

/// Records what each of an objective's targets came out at, under `key`: every term's residual and
/// what it contributed to the total. This is the report that says which constraint is binding.
void reportObjective(GenerationTelemetry &, const std::string &key, const Objective &);

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
