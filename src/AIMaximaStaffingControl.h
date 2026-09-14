/*
  Per-building staffing control for Maxima.

  Every inn and swarm regulates itself from what it can actually observe: how
  full of wheat it is, and how many workers it is actually getting. There is no
  colony-wide budget, no apportionment between buildings, and no estimate of
  nearby wheat. Two buildings sharing one farm cannot double-count it, because
  neither looks at the farm at all.

  The loop keeps the stock inside a band. Below the band the building is going
  hungry and wants another carrier; above it the carriers have nothing to do and
  the engine is already sending them elsewhere, so it gives one back.

  A request is only raised when the building is actually receiving the workers
  it already asked for. Otherwise the shortage is in the workforce, not in the
  request, and raising it further would wind up without effect.

  This is deliberately integral-only with a deadband: placement quality, farm
  capacity and demand models belong to the food ledger, not here.
 */

#ifndef AI_MAXIMA_STAFFING_CONTROL_H
#define AI_MAXIMA_STAFFING_CONTROL_H

#include <algorithm>

namespace AIMaxima
{
namespace StaffingControl
{

/// Fixed-point scale for the rolling averages. Integer EMAs at unit scale would
/// quantise away changes smaller than the window.
const int AverageScale=1000;

/// Save format that added this controller's state.
const int SaveVersion=94;

struct Policy
{
	Policy(): windowSamples(8), lowPermille(333), highPermille(667), slack(1),
		minimumWorkers(1), maximumWorkers(20), cooldownPasses(3) {}
	/// Length of both rolling averages, in control passes.
	int windowSamples;
	/// Stock band, in thousandths of the building's own corn capacity.
	int lowPermille;
	int highPermille;
	/// How far below its request a building may be and still count as staffed.
	int slack;
	/// Every building keeps at least this many workers.
	int minimumWorkers;
	int maximumWorkers;
	/// Passes a building must wait after changing its request before it may
	/// change again. A carrier takes several passes to affect the stock, so
	/// adjusting on every pass chases delivery noise instead of the trend.
	int cooldownPasses;
};

struct State
{
	State(): request(0), samples(0), cornAverage(0), enrolledAverage(0),
		passesSinceChange(0) {}
	/// Workers currently asked for. This is the controller's integral term, so
	/// it is carried across passes and across saves.
	int request;
	int samples;
	/// Rolling means, both at AverageScale. Corn is a per-mille fill, so its
	/// average is comparable between an inn and a swarm of any level.
	int cornAverage;
	int enrolledAverage;
	/// Passes since this building last changed its request.
	int passesSinceChange;
};

inline int advanceAverage(int average, int sample, int window, bool first)
{
	if(first || window<=1) return sample;
	// Integer EMA: deterministic on every platform, and truncation toward zero
	// cannot drift because the correction is recomputed from the live sample.
	return average+(sample-average)/window;
}

/// One control pass for one building. Returns the worker count to request.
inline int update(State& state, const Policy& policy, int corn, int capacity,
	int enrolled)
{
	const int minimum=std::max(0, policy.minimumWorkers);
	const int maximum=std::max(minimum, policy.maximumWorkers);
	if(state.request<minimum) state.request=minimum;
	if(capacity<=0)
	{
		// Nothing to regulate: no stock of its own to measure.
		state.request=std::min(state.request, maximum);
		return state.request;
	}

	const int window=std::max(1, policy.windowSamples);
	const bool first=state.samples==0;
	const int fill=std::min(AverageScale,
		int(static_cast<long long>(std::max(0, corn))*AverageScale/capacity));
	state.cornAverage=advanceAverage(state.cornAverage, fill, window, first);
	state.enrolledAverage=advanceAverage(state.enrolledAverage,
		std::max(0, enrolled)*AverageScale, window, first);
	if(state.samples<window) ++state.samples;
	if(state.passesSinceChange<policy.cooldownPasses) ++state.passesSinceChange;

	// Act only on a settled average, and only once per cooldown: a carrier
	// takes several passes to show up in the stock, so adjusting sooner would
	// chase its own last correction.
	if(state.samples>=window
	   && state.passesSinceChange>=std::max(0, policy.cooldownPasses))
	{
		const int before=state.request;
		const int staffed=(state.request-std::max(0, policy.slack))*AverageScale;
		if(state.cornAverage<policy.lowPermille && state.enrolledAverage>=staffed)
			++state.request;
		else if(state.cornAverage>policy.highPermille)
			--state.request;
		if(state.request!=before) state.passesSinceChange=0;
	}
	state.request=std::max(minimum, std::min(maximum, state.request));
	return state.request;
}

/// Field-wise serialization, found by ADL from the continuation archives.
template<class A> void fields(A& a, State& value)
{
	a("request", value.request);
	a("samples", value.samples);
	a("cornAverage", value.cornAverage);
	a("enrolledAverage", value.enrolledAverage);
	a("passesSinceChange", value.passesSinceChange);
}

}
}

#endif
