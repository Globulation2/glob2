#include "MaximaDefenseTest.h"
#include "../src/AIMaximaDefense.h"

#include <algorithm>
#include <climits>

CPPUNIT_TEST_SUITE_REGISTRATION(MaximaDefenseTest);

using namespace AIMaxima::Defense;

namespace
{
	Policy testPolicy()
	{
		Policy policy;
		policy.innerDistance=2;
		policy.bandWidth=3;
		policy.pathSlack=0;
		policy.probeRadius=5;
		policy.maximumCrossSection=10;
		policy.zoneRadius=3;
		return policy;
	}

	ModeInput strip(int rows, MovementMode mode)
	{
		ModeInput input;
		input.mode=mode;
		input.width=24;
		input.height=rows;
		input.walkable.assign(input.width*input.height, 0);
		for(int y=0; y<rows; ++y)
			for(int x=18; x<27; ++x)
				input.walkable[y*input.width+(x%input.width)]=1;
		for(int y=0; y<rows; ++y)
			input.homeSources.push_back(y*input.width+2);
		EnemySources enemy(1);
		for(int y=0; y<rows; ++y)
			enemy.sources.push_back(y*input.width+18);
		input.enemies.push_back(enemy);
		return input;
	}

	ModeResult manualMode(MovementMode mode, int width, int height)
	{
		ModeResult result;
		result.mode=mode;
		result.width=width;
		result.height=height;
		result.walkable.assign(width*height, 1);
		return result;
	}

	Candidate manualCandidate(MovementMode mode, int index, int memberships,
		int width, int homeDistance)
	{
		Candidate candidate;
		candidate.mode=mode;
		candidate.index=index;
		candidate.memberships=memberships;
		candidate.crossSection=width;
		candidate.terrainCrossSection=width;
		candidate.bandOffset=0;
		candidate.homeDistance=homeDistance;
		return candidate;
	}
}

void MaximaDefenseTest::testOpenTerrainRejected()
{
	ModeInput input;
	input.width=31;
	input.height=31;
	input.walkable.assign(input.width*input.height, 1);
	input.homeSources.push_back(15*input.width+15);
	EnemySources enemy(2);
	enemy.sources.push_back(15*input.width+25);
	input.enemies.push_back(enemy);
	const ModeResult result=analyzeMode(input, testPolicy());
	CPPUNIT_ASSERT(result.candidates.empty());
}

void MaximaDefenseTest::testCrossSectionBoundaryAndComponentCollapse()
{
	const ModeResult accepted=analyzeMode(strip(10, LandMode), testPolicy());
	CPPUNIT_ASSERT_EQUAL(size_t(1), accepted.candidates.size());
	CPPUNIT_ASSERT_EQUAL(10, accepted.candidates[0].crossSection);
	CPPUNIT_ASSERT_EQUAL(10, accepted.candidates[0].terrainCrossSection);
	const ModeResult rejected=analyzeMode(strip(11, LandMode), testPolicy());
	CPPUNIT_ASSERT(rejected.candidates.empty());
}

void MaximaDefenseTest::testMultiEnemyMembership()
{
	ModeInput input=strip(6, LandMode);
	EnemySources second=input.enemies[0];
	second.team=3;
	input.enemies.push_back(second);
	const ModeResult result=analyzeMode(input, testPolicy());
	CPPUNIT_ASSERT_EQUAL(size_t(1), result.candidates.size());
	CPPUNIT_ASSERT_EQUAL(2, result.candidates[0].memberships);
}

void MaximaDefenseTest::testToroidalDistanceAndRadiusThreeFootprint()
{
	std::vector<unsigned char> walkable(25, 1);
	std::vector<int> sources(1, 0);
	std::vector<int> distance;
	computeDistanceField(5, 5, walkable, sources, distance);
	CPPUNIT_ASSERT_EQUAL(1, distance[4]);
	CPPUNIT_ASSERT_EQUAL(1, distance[20]);

	const ModeResult mode=manualMode(LandMode, 15, 15);
	const std::vector<int> footprint=buildFootprint(mode, 7*15+7, 3);
	CPPUNIT_ASSERT_EQUAL(size_t(49), footprint.size());
	CPPUNIT_ASSERT(std::find(footprint.begin(), footprint.end(), 4*15+4)
		!=footprint.end());
}

void MaximaDefenseTest::testLandRejectsWaterGapAmphibiousAcceptsIt()
{
	ModeInput land=strip(6, LandMode);
	for(int y=0; y<land.height; ++y)
		land.walkable[y*land.width+22]=0;
	ModeInput amphibious=strip(6, AmphibiousMode);
	const ModeResult landResult=analyzeMode(land, testPolicy());
	const ModeResult amphibiousResult=analyzeMode(amphibious, testPolicy());
	CPPUNIT_ASSERT(landResult.candidates.empty());
	CPPUNIT_ASSERT_EQUAL(size_t(1), amphibiousResult.candidates.size());
}

void MaximaDefenseTest::testForcePolicyAndSwimmingEligibility()
{
	CPPUNIT_ASSERT_EQUAL(0, effectiveZoneCap(3, 0, 3, 6));
	CPPUNIT_ASSERT_EQUAL(1, effectiveZoneCap(4, 4, 3, 6));
	CPPUNIT_ASSERT_EQUAL(1, effectiveZoneCap(5, 4, 3, 6));
	CPPUNIT_ASSERT_EQUAL(2, effectiveZoneCap(6, 4, 3, 6));
	CPPUNIT_ASSERT_EQUAL(3, effectiveZoneCap(9, 4, 3, 6));
	CPPUNIT_ASSERT_EQUAL(6, effectiveZoneCap(18, 4, 3, 6));
	CPPUNIT_ASSERT_EQUAL(7, effectiveZoneCap(21, 4, 3, 0));
	CPPUNIT_ASSERT(!amphibiousEligible(true, true, 3, 0));
	CPPUNIT_ASSERT(amphibiousEligible(true, true, 4, 4));
	CPPUNIT_ASSERT(!amphibiousEligible(false, true, 20, 4));
	CPPUNIT_ASSERT(!amphibiousEligible(true, false, 20, 4));
}

void MaximaDefenseTest::testTopologyRefreshInvalidation()
{
	CPPUNIT_ASSERT(!topologyRefreshRequired(7, 7, 2, 2, false, false,
		449, 0, 500, true));
	CPPUNIT_ASSERT(topologyRefreshRequired(8, 7, 2, 2, false, false,
		1, 0, 500, true));
	CPPUNIT_ASSERT(topologyRefreshRequired(7, 7, 3, 2, false, false,
		1, 0, 500, true));
	CPPUNIT_ASSERT(topologyRefreshRequired(7, 7, 2, 2, true, false,
		1, 0, 500, true));
	CPPUNIT_ASSERT(topologyRefreshRequired(7, 7, 2, 2, false, false,
		500, 0, 500, true));
	CPPUNIT_ASSERT(topologyRefreshRequired(7, 7, 2, 2, false, false,
		1, 0, 500, false));
}

void MaximaDefenseTest::testSharedRankingOverlapAndContinuedScan()
{
	ModeResult land=manualMode(LandMode, 20, 20);
	ModeResult amphibious=manualMode(AmphibiousMode, 20, 20);
	land.candidates.push_back(manualCandidate(LandMode, 42, 3, 4, 3));
	amphibious.candidates.push_back(
		manualCandidate(AmphibiousMode, 42, 2, 4, 3));
	amphibious.candidates.push_back(
		manualCandidate(AmphibiousMode, 250, 1, 4, 3));
	std::vector<ModeResult> modes;
	modes.push_back(land);
	modes.push_back(amphibious);
	const PlanResult plan=combineModes(modes, 2, 1);
	CPPUNIT_ASSERT_EQUAL(2, plan.selectedCount);
	CPPUNIT_ASSERT_EQUAL(CandidateSelected, plan.candidates[0].state);
	CPPUNIT_ASSERT_EQUAL(LandMode, plan.candidates[0].mode);
	CPPUNIT_ASSERT_EQUAL(CandidateRejectedOverlap, plan.candidates[1].state);
	CPPUNIT_ASSERT_EQUAL(CandidateSelected, plan.candidates[2].state);
	CPPUNIT_ASSERT_EQUAL(250, plan.candidates[2].index);

	ModeResult tiedLand=manualMode(LandMode, 20, 20);
	ModeResult tiedAmphibious=manualMode(AmphibiousMode, 20, 20);
	tiedLand.candidates.push_back(manualCandidate(LandMode, 80, 1, 5, 4));
	tiedAmphibious.candidates.push_back(
		manualCandidate(AmphibiousMode, 80, 1, 5, 4));
	modes.clear();
	modes.push_back(tiedAmphibious);
	modes.push_back(tiedLand);
	const PlanResult tie=combineModes(modes, 1, 1);
	CPPUNIT_ASSERT_EQUAL(LandMode, tie.candidates[0].mode);
	CPPUNIT_ASSERT_EQUAL(CandidateSelected, tie.candidates[0].state);

	ModeResult rowMajor=manualMode(LandMode, 20, 20);
	rowMajor.candidates.push_back(manualCandidate(LandMode, 300, 1, 5, 4));
	rowMajor.candidates.push_back(manualCandidate(LandMode, 100, 1, 5, 4));
	modes.clear();
	modes.push_back(rowMajor);
	const PlanResult ordered=combineModes(modes, 1, 0);
	CPPUNIT_ASSERT_EQUAL(100, ordered.candidates[0].index);
}

void MaximaDefenseTest::testProbeLargerThanMap()
{

	// Radius 64 is schema-valid and can wrap a small map multiple times.
	Policy wide=testPolicy();
	wide.probeRadius=64;
	const ModeResult narrow=analyzeMode(strip(6, LandMode), wide);
	CPPUNIT_ASSERT(narrow.candidates.size()==1);
	CPPUNIT_ASSERT(narrow.candidates[0].crossSection==6);
	CPPUNIT_ASSERT(narrow.candidates[0].terrainCrossSection==6);
	ModeInput small;
	small.width=32; small.height=32;
	small.walkable.assign(1024, 1);
	small.homeSources.push_back(0);
	EnemySources enemy(1); enemy.sources.push_back(10);
	small.enemies.push_back(enemy);
	const ModeResult largeProbe=analyzeMode(small, wide);
	wide.probeRadius=32;
	const ModeResult oneLap=analyzeMode(small, wide);
	CPPUNIT_ASSERT(largeProbe.teams[0].corridorWidth==oneLap.teams[0].corridorWidth);
	CPPUNIT_ASSERT(largeProbe.teams[0].terrainWidth==oneLap.teams[0].terrainWidth);
}
