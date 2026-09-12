#include "../src/AIMaximaStaffingControl.h"

#include <cassert>
#include <iostream>

using namespace AIMaxima::StaffingControl;

/// Most cases exercise the decision rules rather than the pacing, so they use a
/// short window and no cooldown. The pacing itself is covered separately.
static Policy responsive()
{
	Policy policy;policy.windowSamples=4;policy.cooldownPasses=0;
	return policy;
}

/// Drive one building for a number of passes at a fixed stock and staffing.
static int run(State& state, const Policy& policy, int corn, int capacity,
	int passes, bool enrolledMatchesRequest=true, int enrolled=0)
{
	int request=state.request;
	for(int pass=0; pass<passes; ++pass)
		request=update(state, policy, corn, capacity,
			enrolledMatchesRequest?state.request:enrolled);
	return request;
}

/// A building that stays empty while fully staffed asks for more carriers.
static void emptyBuildingGainsWorkers()
{
	const Policy policy=responsive();State state;
	const int request=run(state, policy, 1, 20, 20);
	assert(request>policy.minimumWorkers);
	assert(state.cornAverage<policy.lowPermille);
}

/// A building that stays full gives carriers back, but never the last one.
static void fullBuildingShedsWorkersToTheMinimum()
{
	const Policy policy=responsive();State state;state.request=8;
	const int request=run(state, policy, 20, 20, 40);
	assert(request==policy.minimumWorkers);
	assert(request>=1);
}

/// Inside the band the request is left alone.
static void stockInsideTheBandHolds()
{
	const Policy policy=responsive();State state;state.request=4;
	const int request=run(state, policy, 10, 20, 30);
	assert(request==4);
}

/// The anti-windup rule: a building that is not receiving the workers it asked
/// for must not ask for more, because the workforce is the constraint.
static void understaffedBuildingDoesNotWindUp()
{
	const Policy policy=responsive();State state;state.request=6;
	const int request=run(state, policy, 0, 20, 40, false, 2);
	assert(request==6);
}

/// Slack tolerates being one worker short of the request.
static void slackAllowsGrowthWhenNearlyStaffed()
{
	Policy policy=responsive();policy.slack=1;
	State state;state.request=5;
	const int request=run(state, policy, 0, 20, 20, false, 4);
	assert(request>5);
}

/// Every building keeps at least one worker, whatever the stock says.
static void minimumIsAlwaysHonoured()
{
	const Policy policy=responsive();State state;
	assert(update(state, policy, 20, 20, 0)>=1);
	State fresh;
	assert(update(fresh, policy, 0, 0, 0)>=1);
}

/// The request never exceeds the engine's own ceiling.
static void requestIsCappedAtTheEngineLimit()
{
	const Policy policy=responsive();State state;
	state.request=policy.maximumWorkers;
	const int request=run(state, policy, 0, 20, 40);
	assert(request==policy.maximumWorkers);
}

/// Capacity differences are normalised: a level-one inn and a swarm at the same
/// proportional fill behave identically.
static void fillIsRelativeToCapacity()
{
	const Policy policy=responsive();State inn;State swarm;
	run(inn, policy, 9, 10, 30);
	run(swarm, policy, 18, 20, 30);
	assert(inn.request==swarm.request);
	assert(inn.cornAverage==swarm.cornAverage);
}

/// A settled average is required before the first adjustment.
static void warmupDelaysTheFirstAdjustment()
{
	Policy policy=responsive();policy.windowSamples=8;
	State state;state.request=3;
	update(state, policy, 0, 20, 3);
	assert(state.request==3);
	for(int pass=1; pass<policy.windowSamples; ++pass)
		update(state, policy, 0, 20, 3);
	assert(state.request==4);
}

/// The cooldown paces adjustments. A carrier needs several passes to show up in
/// the stock, so the loop must not correct on every pass.
static void cooldownLimitsTheAdjustmentRate()
{
	Policy paced;paced.windowSamples=2;paced.cooldownPasses=5;
	State slow;
	const int pacedRequest=run(slow, paced, 0, 20, 30);
	// Thirty passes allow at most six adjustments at one per five passes.
	assert(pacedRequest>1);
	assert(pacedRequest<=1+30/paced.cooldownPasses);

	Policy immediate=paced;immediate.cooldownPasses=0;
	State fast;
	const int fastRequest=run(fast, immediate, 0, 20, 30);
	// Without a cooldown the same stock drives many more corrections.
	assert(fastRequest>pacedRequest);
}

/// The shipped defaults space corrections out rather than averaging them into
/// sluggishness: the cooldown is what prevents oscillation.
static void defaultsArePacedForDeliveryLatency()
{
	const Policy defaults;
	assert(defaults.windowSamples>=4);
	assert(defaults.cooldownPasses>=2);
}

int main()
{
	emptyBuildingGainsWorkers();
	fullBuildingShedsWorkersToTheMinimum();
	stockInsideTheBandHolds();
	understaffedBuildingDoesNotWindUp();
	slackAllowsGrowthWhenNearlyStaffed();
	minimumIsAlwaysHonoured();
	requestIsCappedAtTheEngineLimit();
	fillIsRelativeToCapacity();
	warmupDelaysTheFirstAdjustment();
	cooldownLimitsTheAdjustmentRate();
	defaultsArePacedForDeliveryLatency();
	std::cout<<"MaximaStaffingControlStandaloneTest: PASS"<<std::endl;
	return 0;
}
