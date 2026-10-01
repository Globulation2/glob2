#include "Glob2Test.h"
#include "../../src/ai/maxima/AIMaximaStaffingControl.h"

#include <iostream>

namespace
{
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
	REQUIRE(request>policy.minimumWorkers);
	REQUIRE(state.cornAverage<policy.lowPermille);
}

/// A building that stays full gives carriers back, but never the last one.
static void fullBuildingShedsWorkersToTheMinimum()
{
	const Policy policy=responsive();State state;state.request=8;
	const int request=run(state, policy, 20, 20, 40);
	REQUIRE(request==policy.minimumWorkers);
	REQUIRE(request>=1);
}

/// Inside the band the request is left alone.
static void stockInsideTheBandHolds()
{
	const Policy policy=responsive();State state;state.request=4;
	const int request=run(state, policy, 10, 20, 30);
	REQUIRE(request==4);
}

/// The anti-windup rule: a building that is not receiving the workers it asked
/// for must not ask for more, because the workforce is the constraint.
static void understaffedBuildingDoesNotWindUp()
{
	const Policy policy=responsive();State state;state.request=6;
	const int request=run(state, policy, 0, 20, 40, false, 2);
	REQUIRE(request==6);
}

/// Slack tolerates being one worker short of the request.
static void slackAllowsGrowthWhenNearlyStaffed()
{
	Policy policy=responsive();policy.slack=1;
	State state;state.request=5;
	const int request=run(state, policy, 0, 20, 20, false, 4);
	REQUIRE(request>5);
}

/// Every building keeps at least one worker, whatever the stock says.
static void minimumIsAlwaysHonoured()
{
	const Policy policy=responsive();State state;
	REQUIRE(update(state, policy, 20, 20, 0)>=1);
	State fresh;
	REQUIRE(update(fresh, policy, 0, 0, 0)>=1);
}

/// The request never exceeds the engine's own ceiling.
static void requestIsCappedAtTheEngineLimit()
{
	const Policy policy=responsive();State state;
	state.request=policy.maximumWorkers;
	const int request=run(state, policy, 0, 20, 40);
	REQUIRE(request==policy.maximumWorkers);
}

/// Capacity differences are normalised: a level-one inn and a swarm at the same
/// proportional fill behave identically.
static void fillIsRelativeToCapacity()
{
	const Policy policy=responsive();State inn;State swarm;
	run(inn, policy, 9, 10, 30);
	run(swarm, policy, 18, 20, 30);
	REQUIRE(inn.request==swarm.request);
	REQUIRE(inn.cornAverage==swarm.cornAverage);
}

/// A settled average is required before the first adjustment.
static void warmupDelaysTheFirstAdjustment()
{
	Policy policy=responsive();policy.windowSamples=8;
	State state;state.request=3;
	update(state, policy, 0, 20, 3);
	REQUIRE(state.request==3);
	for(int pass=1; pass<policy.windowSamples; ++pass)
		update(state, policy, 0, 20, 3);
	REQUIRE(state.request==4);
}

/// The cooldown paces adjustments. A carrier needs several passes to show up in
/// the stock, so the loop must not correct on every pass.
static void cooldownLimitsTheAdjustmentRate()
{
	Policy paced;paced.windowSamples=2;paced.cooldownPasses=5;
	State slow;
	const int pacedRequest=run(slow, paced, 0, 20, 30);
	// Thirty passes allow at most six adjustments at one per five passes.
	REQUIRE(pacedRequest>1);
	REQUIRE(pacedRequest<=1+30/paced.cooldownPasses);

	Policy immediate=paced;immediate.cooldownPasses=0;
	State fast;
	const int fastRequest=run(fast, immediate, 0, 20, 30);
	// Without a cooldown the same stock drives many more corrections.
	REQUIRE(fastRequest>pacedRequest);
}

/// The shipped defaults space corrections out rather than averaging them into
/// sluggishness: the cooldown is what prevents oscillation.
static void defaultsArePacedForDeliveryLatency()
{
	const Policy defaults;
	REQUIRE(defaults.windowSamples>=4);
	REQUIRE(defaults.cooldownPasses>=2);
}
}

TEST_SUITE("Maxima.StaffingControl")
{
	TEST_CASE("empty building gains workers")
	{
		emptyBuildingGainsWorkers();
	}
	TEST_CASE("full building sheds workers to the minimum")
	{
		fullBuildingShedsWorkersToTheMinimum();
	}
	TEST_CASE("stock inside the band holds")
	{
		stockInsideTheBandHolds();
	}
	TEST_CASE("understaffed building does not wind up")
	{
		understaffedBuildingDoesNotWindUp();
	}
	TEST_CASE("slack allows growth when nearly staffed")
	{
		slackAllowsGrowthWhenNearlyStaffed();
	}
	TEST_CASE("minimum is always honoured")
	{
		minimumIsAlwaysHonoured();
	}
	TEST_CASE("request is capped at the engine limit")
	{
		requestIsCappedAtTheEngineLimit();
	}
	TEST_CASE("fill is relative to capacity")
	{
		fillIsRelativeToCapacity();
	}
	TEST_CASE("warmup delays the first adjustment")
	{
		warmupDelaysTheFirstAdjustment();
	}
	TEST_CASE("cooldown limits the adjustment rate")
	{
		cooldownLimitsTheAdjustmentRate();
	}
	TEST_CASE("defaults are paced for delivery latency")
	{
		defaultsArePacedForDeliveryLatency();
	}
}
