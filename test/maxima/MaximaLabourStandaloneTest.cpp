#include "Glob2Test.h"
#include "../../src/ai/maxima/AIMaximaLabour.h"

#include <iostream>
#include <vector>

namespace
{
using namespace AIMaxima::Labour;

/// With nothing to train at, no worker is held back.
static void noTrainingBuildingMeansNoReserve()
{
	Observation o;o.workers=20;o.swarms=1;o.siteRequested=6;
	const Plan p=plan(o, Policy(), 10);
	REQUIRE(p.trainingReserve==0);
	REQUIRE(p.assignable==20);
	REQUIRE(p.swarmCap==10);
}

/// Open seats with trainable workers reserve slack, bounded by the seats.
static void reserveMatchesOpenSeats()
{
	Observation o;o.workers=20;o.swarms=1;o.trainable=12;o.trainingSlots=2;
	Policy policy;
	Plan p=plan(o, policy, 20);
	REQUIRE(p.trainingReserve==2+policy.reserveBuffer);
	o.training=2; // seats taken: only the buffer stays free
	p=plan(o, policy, 20);
	REQUIRE(p.trainingReserve==policy.reserveBuffer);
	REQUIRE(p.assignable==20-2-policy.reserveBuffer);
}

/// An opening colony holds nobody back and its swarm requests pass untouched.
static void openingIsLeftAlone()
{
	Observation o;o.workers=10;o.swarms=1;o.trainable=4;o.trainingSlots=6;
	o.innCarriers=2;o.siteRequested=6;o.swarmRequested=8;
	const Plan p=plan(o, Policy(), 0);
	REQUIRE(p.trainingReserve==0);
	REQUIRE((p.uncapped && p.swarmCap==8));
}

/// Births take what is left, never more than food funds, never below the floor.
static void swarmsAreTheResidual()
{
	Observation o;o.workers=30;o.eating=5;o.swarms=2;o.innCarriers=6;o.siteRequested=6;
	Plan p=plan(o, Policy(), 14);
	REQUIRE(p.swarmCap==13);
	o.siteRequested=18;
	p=plan(o, Policy(), 14);
	REQUIRE(p.swarmCap==9); // the 30% floor
	o.innCarriers=0;o.siteRequested=0;
	p=plan(o, Policy(), 14);
	REQUIRE(p.swarmCap==14);
}

/// Warrior births are pulled by barracks seats; hospitals and attacks follow.
static void militaryIsPulledByCapacity()
{
	// With no barracks the floor is what allows any backlog at all; pacing to
	// seats alone froze warrior production at two in every measured game.
	REQUIRE(warriorBacklogLimit(0, 8)==8);
	REQUIRE(warriorBacklogLimit(2, 8)==8);
	REQUIRE(warriorBacklogLimit(6, 8)==12);
	REQUIRE(hospitalBedsWanted(0,50)==0);
	REQUIRE(hospitalBedsWanted(1,50)==1);
	REQUIRE(hospitalBedsWanted(7,50)==4);
	REQUIRE(hospitalBedsWanted(20,30)==6);
	REQUIRE(hospitalBedsWanted(20,40)==8);
	REQUIRE(hospitalBedsWanted(20,50)==10);
	REQUIRE(hospitalBedsWanted(20,60)==12);
	REQUIRE(innsWorthBuilding(3, 2, 8, 2, 24)==3);
	REQUIRE(innsWorthBuilding(3, 4, 16, 3, 24)==4);
	REQUIRE(innsWorthBuilding(3, 4, 16, 9, 24)==5);
	// The queue rule answers to the ceiling too, however hungry the colony is.
	REQUIRE(innsWorthBuilding(3, 24, 96, 500, 24)==24);
	REQUIRE(innsWorthBuilding(30, 24, 96, 500, 24)==24);
	REQUIRE(attackStrengthSufficient(0, 0));
	REQUIRE(!attackStrengthSufficient(4*64, 4));
	REQUIRE(attackStrengthSufficient(4*110, 4));
	REQUIRE(attackStrengthSufficient(4*168, 4));
}

/// Trimming takes from the largest request first and respects the minimum.
static void trimmingIsFairAndBounded()
{
	std::vector<int> requests;requests.push_back(12);requests.push_back(4);requests.push_back(8);
	REQUIRE(trimToCap(requests, 16, 1)==8);
	REQUIRE(requests[0]+requests[1]+requests[2]==16);
	REQUIRE((requests[0]<=6 && requests[1]==4));
	std::vector<int> small(2, 1);
	REQUIRE(trimToCap(small, 0, 1)==0);
	REQUIRE((small[0]==1 && small[1]==1));
}
}

TEST_SUITE("Maxima.Labour")
{
	TEST_CASE("no training building means no reserve")
	{
		noTrainingBuildingMeansNoReserve();
	}
	TEST_CASE("reserve matches open seats")
	{
		reserveMatchesOpenSeats();
	}
	TEST_CASE("opening is left alone")
	{
		openingIsLeftAlone();
	}
	TEST_CASE("swarms are the residual")
	{
		swarmsAreTheResidual();
	}
	TEST_CASE("trimming is fair and bounded")
	{
		trimmingIsFairAndBounded();
	}
	TEST_CASE("military is pulled by capacity")
	{
		militaryIsPulledByCapacity();
	}
}
