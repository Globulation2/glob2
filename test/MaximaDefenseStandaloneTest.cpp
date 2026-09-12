#include "../src/AIMaximaDefense.h"

#include <algorithm>
#include <cassert>

using namespace AIMaxima::Defense;

namespace
{
	Policy policy()
	{
		Policy result;
		result.innerDistance=2;
		result.bandWidth=3;
		result.pathSlack=0;
		result.probeRadius=5;
		result.maximumCrossSection=10;
		result.zoneRadius=3;
		return result;
	}

	ModeInput strip(int rows, MovementMode mode)
	{
		ModeInput result;
		result.mode=mode;
		result.width=24;
		result.height=rows;
		result.walkable.assign(result.width*result.height, 0);
		for(int y=0; y<rows; ++y)
		{
			for(int x=18; x<27; ++x)
				result.walkable[y*result.width+(x%result.width)]=1;
			result.homeSources.push_back(y*result.width+2);
		}
		EnemySources enemy(1);
		for(int y=0; y<rows; ++y)
			enemy.sources.push_back(y*result.width+18);
		result.enemies.push_back(enemy);
		return result;
	}

	ModeResult manualMode(MovementMode mode)
	{
		ModeResult result;
		result.mode=mode;
		result.width=20;
		result.height=20;
		result.walkable.assign(400, 1);
		return result;
	}

	Candidate candidate(MovementMode mode, int index, int memberships)
	{
		Candidate result;
		result.mode=mode;
		result.index=index;
		result.memberships=memberships;
		result.crossSection=4;
		result.terrainCrossSection=4;
		result.bandOffset=0;
		result.homeDistance=3;
		return result;
	}
}

int main()
{
	ModeInput open;
	open.width=31;
	open.height=31;
	open.walkable.assign(31*31, 1);
	open.homeSources.push_back(15*31+15);
	EnemySources openEnemy(2);
	openEnemy.sources.push_back(15*31+25);
	open.enemies.push_back(openEnemy);
	assert(analyzeMode(open, policy()).candidates.empty());

	const ModeResult width10=analyzeMode(strip(10, LandMode), policy());
	assert(width10.candidates.size()==1);
	assert(width10.candidates[0].crossSection==10);
	assert(analyzeMode(strip(11, LandMode), policy()).candidates.empty());

	ModeInput shared=strip(6, LandMode);
	EnemySources second=shared.enemies[0];
	second.team=3;
	shared.enemies.push_back(second);
	assert(analyzeMode(shared, policy()).candidates[0].memberships==2);

	std::vector<unsigned char> walkable(25, 1);
	std::vector<int> sources(1, 0), distance;
	computeDistanceField(5, 5, walkable, sources, distance);
	assert(distance[4]==1 && distance[20]==1);
	ModeResult footprintMode=manualMode(LandMode);
	footprintMode.width=15;
	footprintMode.height=15;
	footprintMode.walkable.assign(225, 1);
	assert(buildFootprint(footprintMode, 7*15+7, 3).size()==49);

	ModeInput land=strip(6, LandMode);
	for(int y=0; y<land.height; ++y)
		land.walkable[y*land.width+22]=0;
	assert(analyzeMode(land, policy()).candidates.empty());
	assert(analyzeMode(strip(6, AmphibiousMode), policy()).candidates.size()==1);

	assert(effectiveZoneCap(3, 0, 3, 6)==0);
	assert(effectiveZoneCap(4, 4, 3, 6)==1);
	assert(effectiveZoneCap(6, 4, 3, 6)==2);
	assert(effectiveZoneCap(9, 4, 3, 6)==3);
	assert(effectiveZoneCap(18, 4, 3, 6)==6);
	assert(effectiveZoneCap(21, 4, 3, 0)==7);
	assert(!amphibiousEligible(true, true, 3, 0));
	assert(amphibiousEligible(true, true, 4, 4));
	assert(!topologyRefreshRequired(7, 7, 2, 2, false, false,
		499, 0, 500, true));
	assert(topologyRefreshRequired(8, 7, 2, 2, false, false,
		1, 0, 500, true));
	assert(topologyRefreshRequired(7, 7, 3, 2, false, false,
		1, 0, 500, true));
	assert(topologyRefreshRequired(7, 7, 2, 2, true, false,
		1, 0, 500, true));
	assert(topologyRefreshRequired(7, 7, 2, 2, false, false,
		500, 0, 500, true));
	assert(topologyRefreshRequired(7, 7, 2, 2, false, false,
		1, 0, 500, false));

	ModeResult landMode=manualMode(LandMode);
	ModeResult amphibiousMode=manualMode(AmphibiousMode);
	landMode.candidates.push_back(candidate(LandMode, 42, 3));
	amphibiousMode.candidates.push_back(candidate(AmphibiousMode, 42, 2));
	amphibiousMode.candidates.push_back(candidate(AmphibiousMode, 250, 1));
	std::vector<ModeResult> modes;
	modes.push_back(landMode);
	modes.push_back(amphibiousMode);
	const PlanResult plan=combineModes(modes, 2, 1);
	assert(plan.selectedCount==2);
	assert(plan.candidates[0].mode==LandMode);
	assert(plan.candidates[0].state==CandidateSelected);
	assert(plan.candidates[1].state==CandidateRejectedOverlap);
	assert(plan.candidates[2].index==250);
	assert(plan.candidates[2].state==CandidateSelected);

	ModeResult orderedMode=manualMode(LandMode);
	orderedMode.candidates.push_back(candidate(LandMode, 300, 1));
	orderedMode.candidates.push_back(candidate(LandMode, 100, 1));
	modes.clear();
	modes.push_back(orderedMode);
	const PlanResult ordered=combineModes(modes, 1, 0);
	assert(ordered.candidates[0].index==100);

	{

	// Radius 64 is schema-valid and can wrap a small map multiple times.
	Policy wide=policy();
	wide.probeRadius=64;
	const ModeResult narrow=analyzeMode(strip(6, LandMode), wide);
	assert(narrow.candidates.size()==1);
	assert(narrow.candidates[0].crossSection==6);
	assert(narrow.candidates[0].terrainCrossSection==6);
	ModeInput small;
	small.width=32; small.height=32;
	small.walkable.assign(1024, 1);
	small.homeSources.push_back(0);
	EnemySources enemy(1); enemy.sources.push_back(10);
	small.enemies.push_back(enemy);
	const ModeResult largeProbe=analyzeMode(small, wide);
	wide.probeRadius=32;
	const ModeResult oneLap=analyzeMode(small, wide);
	assert(largeProbe.teams[0].corridorWidth==oneLap.teams[0].corridorWidth);
	assert(largeProbe.teams[0].terrainWidth==oneLap.teams[0].terrainWidth);
	}
	return 0;
}
