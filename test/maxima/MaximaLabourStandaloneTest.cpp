#include "../../src/ai/maxima/AIMaximaLabour.h"

#include <cassert>
#include <iostream>
#include <vector>

using namespace AIMaxima::Labour;

/// With nothing to train at, no worker is held back.
static void noTrainingBuildingMeansNoReserve()
{
	Observation o;o.workers=20;o.swarms=1;o.siteRequested=6;
	const Plan p=plan(o, Policy(), 10);
	assert(p.trainingReserve==0);
	assert(p.assignable==20);
	assert(p.swarmCap==10);
}

/// Open seats with trainable workers reserve slack, bounded by the seats.
static void reserveMatchesOpenSeats()
{
	Observation o;o.workers=20;o.swarms=1;o.trainable=12;o.trainingSlots=2;
	Policy policy;
	Plan p=plan(o, policy, 20);
	assert(p.trainingReserve==2+policy.reserveBuffer);
	o.training=2; // seats taken: only the buffer stays free
	p=plan(o, policy, 20);
	assert(p.trainingReserve==policy.reserveBuffer);
	assert(p.assignable==20-2-policy.reserveBuffer);
}

/// An opening colony holds nobody back and its swarm requests pass untouched.
static void openingIsLeftAlone()
{
	Observation o;o.workers=10;o.swarms=1;o.trainable=4;o.trainingSlots=6;
	o.innCarriers=2;o.siteRequested=6;o.swarmRequested=8;
	const Plan p=plan(o, Policy(), 0);
	assert(p.trainingReserve==0);
	assert(p.uncapped && p.swarmCap==8);
}

/// Births take what is left, never more than food funds, never below the floor.
static void swarmsAreTheResidual()
{
	Observation o;o.workers=30;o.eating=5;o.swarms=2;o.innCarriers=6;o.siteRequested=6;
	Plan p=plan(o, Policy(), 14);
	assert(p.swarmCap==13);
	o.siteRequested=18;
	p=plan(o, Policy(), 14);
	assert(p.swarmCap==9); // the 30% floor
	o.innCarriers=0;o.siteRequested=0;
	p=plan(o, Policy(), 14);
	assert(p.swarmCap==14);
}

/// Warrior births are pulled by barracks seats; hospitals and attacks follow.
static void militaryIsPulledByCapacity()
{
	// With no barracks the floor is what allows any backlog at all; pacing to
	// seats alone froze warrior production at two in every measured game.
	assert(warriorBacklogLimit(0, 8)==8);
	assert(warriorBacklogLimit(2, 8)==8);
	assert(warriorBacklogLimit(6, 8)==12);
	assert(hospitalBedsWanted(0,50)==0);
	assert(hospitalBedsWanted(1,50)==1);
	assert(hospitalBedsWanted(7,50)==4);
	assert(hospitalBedsWanted(20,30)==6);
	assert(hospitalBedsWanted(20,40)==8);
	assert(hospitalBedsWanted(20,50)==10);
	assert(hospitalBedsWanted(20,60)==12);
	assert(innsWorthBuilding(3, 2, 8, 2, 24)==3);
	assert(innsWorthBuilding(3, 4, 16, 3, 24)==4);
	assert(innsWorthBuilding(3, 4, 16, 9, 24)==5);
	// The queue rule answers to the ceiling too, however hungry the colony is.
	assert(innsWorthBuilding(3, 24, 96, 500, 24)==24);
	assert(innsWorthBuilding(30, 24, 96, 500, 24)==24);
	assert(attackStrengthSufficient(0, 0));
	assert(!attackStrengthSufficient(4*64, 4));
	assert(attackStrengthSufficient(4*110, 4));
	assert(attackStrengthSufficient(4*168, 4));
}

/// Trimming takes from the largest request first and respects the minimum.
static void trimmingIsFairAndBounded()
{
	std::vector<int> requests;requests.push_back(12);requests.push_back(4);requests.push_back(8);
	assert(trimToCap(requests, 16, 1)==8);
	assert(requests[0]+requests[1]+requests[2]==16);
	assert(requests[0]<=6 && requests[1]==4);
	std::vector<int> small(2, 1);
	assert(trimToCap(small, 0, 1)==0);
	assert(small[0]==1 && small[1]==1);
}

int main()
{
	noTrainingBuildingMeansNoReserve();
	reserveMatchesOpenSeats();
	openingIsLeftAlone();
	swarmsAreTheResidual();
	trimmingIsFairAndBounded();
	militaryIsPulledByCapacity();
	std::cout<<"MaximaLabourStandaloneTest passed"<<std::endl;
	return 0;
}
