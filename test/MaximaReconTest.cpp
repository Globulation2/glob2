#include "MaximaReconTest.h"
#include "../src/AIMaximaRecon.h"

#include <vector>

CPPUNIT_TEST_SUITE_REGISTRATION(MaximaReconTest);

void MaximaReconTest::testConfidenceAndEstimates()
{
	using namespace AIMaxima::Recon;
	Program program;
	program.configure(10000, 2500, 1000);
	std::vector<int> living(1, 1);
	program.beginObservation(100, living);
	for(int i=0; i<10; ++i)
		program.observeUnit(1, true, false, false, false, false);
	program.finishObservation();
	const OpponentIntel* fresh=program.opponent(1);
	CPPUNIT_ASSERT(fresh);
	CPPUNIT_ASSERT_EQUAL(100, fresh->confidence);
	CPPUNIT_ASSERT_EQUAL(10, fresh->estimatedWarriors);

	program.beginObservation(2600, living);
	program.finishObservation();
	const OpponentIntel* aged=program.opponent(1);
	CPPUNIT_ASSERT(aged);
	CPPUNIT_ASSERT_EQUAL(75, aged->confidence);
	CPPUNIT_ASSERT_EQUAL(10, aged->estimatedWarriors);
	program.beginObservation(2700, living);
	program.observeUnit(1, false, true, false, false, false);
	program.finishObservation();
	CPPUNIT_ASSERT_EQUAL(9, program.opponent(1)->estimatedWarriors);
	CPPUNIT_ASSERT_EQUAL(1, program.opponent(1)->estimatedExplorers);

	program.beginObservation(10100, living);
	program.finishObservation();
	CPPUNIT_ASSERT_EQUAL(0, program.opponent(1)->estimatedWarriors);
	program.beginObservation(12700, living);
	program.finishObservation();
	CPPUNIT_ASSERT_EQUAL(0, program.opponent(1)->confidence);
}

void MaximaReconTest::testLightweightForceSamplesPreservePeak()
{
	using namespace AIMaxima::Recon;
	Program program;
	program.configure(10000, 2500, 1000);
	std::vector<int> living(1, 1);
	program.beginObservation(0, living);
	program.observeBuilding(BuildingSighting(17, 1, 4, 8, 9, 3, 2, false, 0));
	for(int i=0; i<4; ++i)
		program.observeUnit(1, true, false, false, false, false);
	program.finishObservation();

	program.beginForceObservation(10, living);
	for(int i=0; i<8; ++i)
		program.observeUnit(1, true, false, false, false, false);
	program.finishForceObservation();
	CPPUNIT_ASSERT_EQUAL(8, program.opponent(1)->lastObservedWarriors);
	CPPUNIT_ASSERT_EQUAL(8, program.opponent(1)->estimatedWarriors);
	CPPUNIT_ASSERT_EQUAL(1, program.opponent(1)->visibleBuildings);
	CPPUNIT_ASSERT_EQUAL(0, program.opponent(1)->lastBuildingSeenTick);

	program.beginForceObservation(20, living);
	for(int i=0; i<2; ++i)
		program.observeUnit(1, true, false, false, false, false);
	program.finishForceObservation();
	CPPUNIT_ASSERT_EQUAL(2, program.opponent(1)->visibleWarriors);
	CPPUNIT_ASSERT_EQUAL(8, program.opponent(1)->lastObservedWarriors);
	CPPUNIT_ASSERT_EQUAL(8, program.opponent(1)->estimatedWarriors);

	program.beginObservation(100, living);
	program.finishObservation();
	CPPUNIT_ASSERT_EQUAL(0, program.opponent(1)->visibleWarriors);
	CPPUNIT_ASSERT_EQUAL(8, program.opponent(1)->estimatedWarriors);
	CPPUNIT_ASSERT_EQUAL(1, program.opponent(1)->knownBuildings);
	CPPUNIT_ASSERT_EQUAL(100,
		Program::confidenceForAge(2500, 2500, 10000));
	CPPUNIT_ASSERT_EQUAL(50,
		Program::confidenceForAge(6250, 2500, 10000));
	CPPUNIT_ASSERT_EQUAL(0,
		Program::confidenceForAge(10000, 2500, 10000));
}

void MaximaReconTest::testBuildingMemoryRequiresConfirmation()
{
	using namespace AIMaxima::Recon;
	Program program;
	program.configure(10000, 2500, 1000);
	std::vector<int> living(1, 2);
	program.beginObservation(0, living);
	program.observeBuilding(BuildingSighting(17, 2, 4, 8, 9, 3, 2, false, 0));
	program.finishObservation();
	CPPUNIT_ASSERT_EQUAL(1, program.opponent(2)->visibleBuildings);
	CPPUNIT_ASSERT_EQUAL(1, program.opponent(2)->knownBuildings);

	program.beginObservation(100, living);
	program.finishObservation();
	CPPUNIT_ASSERT_EQUAL(0, program.opponent(2)->visibleBuildings);
	CPPUNIT_ASSERT_EQUAL(1, program.opponent(2)->knownBuildings);

	program.beginObservation(200, living);
	program.confirmBuildingAbsent(2, 17);
	program.finishObservation();
	CPPUNIT_ASSERT_EQUAL(0, program.opponent(2)->knownBuildings);
}

void MaximaReconTest::testMissionScalingAndEmergency()
{
	using namespace AIMaxima::Recon;
	CPPUNIT_ASSERT_EQUAL(1, Program::desiredMissionCount(1, 1, 90, false));
	CPPUNIT_ASSERT_EQUAL(1, Program::desiredMissionCount(3, 3, 10, false));
	CPPUNIT_ASSERT_EQUAL(2, Program::desiredMissionCount(3, 3, 30, false));
	CPPUNIT_ASSERT_EQUAL(3, Program::desiredMissionCount(3, 3, 60, false));
	CPPUNIT_ASSERT_EQUAL(1, Program::desiredMissionCount(3, 0, 90, false));
	CPPUNIT_ASSERT_EQUAL(0, Program::desiredMissionCount(3, 3, 90, true));
	CPPUNIT_ASSERT_EQUAL(0, Program::desiredMissionCount(0, 0, 90, false));
	ReconReport report;
	report.opponents[1].alive=true;
	report.opponents[2].alive=true;
	report.opponents[3].alive=true;
	CPPUNIT_ASSERT_EQUAL(3, Program::staleOrUnseenEnemies(report, 2000, 1000));
	for(int team=1; team<=3; ++team)
	{
		report.opponents[team].buildings[team]=BuildingSighting(
			team, team, 0, team, team, 1, 1, false, 2000);
		report.opponents[team].lastBuildingSeenTick=2000;
	}
	CPPUNIT_ASSERT_EQUAL(0, Program::staleOrUnseenEnemies(report, 2000, 1000));
	report.opponents[2].lastBuildingSeenTick=900;
	CPPUNIT_ASSERT_EQUAL(1, Program::staleOrUnseenEnemies(report, 2000, 1000));
}

void MaximaReconTest::testDynamicOpponentPriority()
{
	using namespace AIMaxima::Recon;
	Program program;
	program.configure(10000, 2500, 1000);
	std::vector<int> living;
	living.push_back(1);
	living.push_back(2);
	program.beginObservation(0, living);
	program.observeBuilding(BuildingSighting(11, 1, 0, 2, 2, 2, 2, false, 0));
	program.finishObservation();
	program.beginObservation(500, living);
	program.observeBuilding(BuildingSighting(22, 2, 0, 18, 18, 2, 2, false, 500));
	program.finishObservation();

	std::vector<unsigned char> discovered(24*24, 1);
	const std::vector<MissionObjective> first=Program::planObjectives(
		program.report(), 1, 24, 24, discovered, 12);
	CPPUNIT_ASSERT_EQUAL(size_t(1), first.size());
	CPPUNIT_ASSERT_EQUAL(1, first[0].targetTeam);

	const std::vector<MissionObjective> both=Program::planObjectives(
		program.report(), 2, 24, 24, discovered, 12);
	CPPUNIT_ASSERT_EQUAL(size_t(2), both.size());
	CPPUNIT_ASSERT(both[0].targetTeam!=both[1].targetTeam);
}

void MaximaReconTest::testFrontierSelectionIsStableAndSeparated()
{
	using namespace AIMaxima::Recon;
	ReconReport report;
	report.tick=0;
	report.opponents[1].alive=true;
	report.opponents[2].alive=true;
	std::vector<unsigned char> discovered(20*20, 0);
	for(int y=8; y<=11; ++y)
		for(int x=8; x<=11; ++x)
			discovered[y*20+x]=1;

	const std::vector<MissionObjective> first=Program::planObjectives(
		report, 2, 20, 20, discovered, 3);
	const std::vector<MissionObjective> second=Program::planObjectives(
		report, 2, 20, 20, discovered, 3);
	CPPUNIT_ASSERT_EQUAL(size_t(2), first.size());
	CPPUNIT_ASSERT_EQUAL(first[0].x, second[0].x);
	CPPUNIT_ASSERT_EQUAL(first[0].y, second[0].y);
	CPPUNIT_ASSERT(first[0].x!=first[1].x || first[0].y!=first[1].y);
	CPPUNIT_ASSERT(Program::frontierScore(first[0].x, first[0].y,
		20, 20, discovered, 3)>0);
	CPPUNIT_ASSERT(Program::exploredPercentInRadius(0, 0,
		20, 20, discovered, 1)>=0);
}

void MaximaReconTest::testEconomicActivityMemoryAndMissionKind()
{
	using namespace AIMaxima::Recon;
	Program program;
	program.configure(10000, 2500, 1000);
	std::vector<int> living(1, 3);
	program.beginObservation(700, living);
	program.observeEconomicActivity(3, 17, 19);
	program.finishObservation();
	const OpponentIntel* intel=program.opponent(3);
	CPPUNIT_ASSERT(intel);
	CPPUNIT_ASSERT_EQUAL(700, intel->lastEconomicSeenTick);
	CPPUNIT_ASSERT_EQUAL(17, intel->lastEconomicX);
	CPPUNIT_ASSERT_EQUAL(19, intel->lastEconomicY);

	MissionObjective watch(3, false, 17, 19, 100, true);
	CPPUNIT_ASSERT(watch.economicWatch);
	ReconMission mission(42, watch.targetTeam, watch.frontier,
		watch.x, watch.y, 700, watch.economicWatch);
	CPPUNIT_ASSERT(mission.economicWatch);
	CPPUNIT_ASSERT_EQUAL(3, mission.targetTeam);
}
