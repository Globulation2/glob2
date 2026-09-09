#include "MaximaTacticsTest.h"
#include "../src/AIMaximaTactics.h"

#include <string>

CPPUNIT_TEST_SUITE_REGISTRATION(MaximaTacticsTest);

using namespace AIMaxima::Tactics;

void MaximaTacticsTest::testClustersVisibleWorkersAcrossMapWrap()
{
	Program program;
	program.beginObservation(100);
	program.observeWorker(WorkerSighting(1, 2, 0, 5, 100, true, false, 2));
	program.observeWorker(WorkerSighting(2, 2, 31, 5, 100, false, true, 4));
	program.observeWorker(WorkerSighting(3, 2, 1, 6, 100, true, true, 5));
	RaidRules rules;
	rules.width=32;
	rules.height=32;
	rules.tick=100;
	program.finishObservation(rules);
	CPPUNIT_ASSERT_EQUAL(size_t(1), program.raidCandidates().size());
	CPPUNIT_ASSERT_EQUAL(3, program.raidCandidates()[0].workers);
	CPPUNIT_ASSERT_EQUAL(2, program.raidCandidates()[0].team);
}

void MaximaTacticsTest::testThreatsReduceCandidateScore()
{
	Program safe;
	safe.beginObservation(1);
	Program guarded;
	guarded.beginObservation(1);
	for(int i=0; i<3; ++i)
	{
		WorkerSighting worker(i, 1, 10+i, 10, 1, true, true, 3);
		safe.observeWorker(worker);
		guarded.observeWorker(worker);
	}
	guarded.observeThreat(ThreatSighting(9, 1, 11, 12, 20));
	RaidRules rules;
	rules.width=32;
	rules.height=32;
	rules.tick=1;
	safe.finishObservation(rules);
	guarded.finishObservation(rules);
	CPPUNIT_ASSERT(safe.raidCandidates()[0].score>
		guarded.raidCandidates()[0].score);
	CPPUNIT_ASSERT_EQUAL(1, guarded.raidCandidates()[0].defenders);
}

void MaximaTacticsTest::testBalancedForceAndMusterRules()
{
	CPPUNIT_ASSERT_EQUAL(6, Program::desiredRaidForce(3, 3, 6, 10));
	CPPUNIT_ASSERT_EQUAL(10, Program::desiredRaidForce(12, 3, 6, 10));
	CPPUNIT_ASSERT(!Program::musterReady(8, 5, 8, 75));
	CPPUNIT_ASSERT(Program::musterReady(8, 6, 8, 75));
}

void MaximaTacticsTest::testWithdrawalRules()
{
	CPPUNIT_ASSERT(!Program::raidUnsafe(1, 8, 2, 50));
	CPPUNIT_ASSERT(Program::raidUnsafe(4, 8, 2, 50));
	CPPUNIT_ASSERT(!Program::casualtiesRequireWithdrawal(7, 8, 4, 25));
	CPPUNIT_ASSERT(Program::casualtiesRequireWithdrawal(6, 8, 4, 25));
	CPPUNIT_ASSERT(Program::casualtiesRequireWithdrawal(3, 8, 4, 25));
}

void MaximaTacticsTest::testReliefRetargetHysteresis()
{
	CPPUNIT_ASSERT(Program::reliefRetargetAllowed(
		false, true, 10000, 6, 10, 10, 20));
	CPPUNIT_ASSERT(Program::reliefRetargetAllowed(
		true, true, 25, 6, 10, 10, 20));
	CPPUNIT_ASSERT(!Program::reliefRetargetAllowed(
		true, true, 10000, 6, 29, 10, 20));
	CPPUNIT_ASSERT(Program::reliefRetargetAllowed(
		true, true, 10000, 6, 30, 10, 20));
}

void MaximaTacticsTest::testSiegeTargetContinuity()
{
	CPPUNIT_ASSERT_EQUAL(SiegeTargetTracked,
		Program::siegeTargetContinuity(true, 42, -1));
	CPPUNIT_ASSERT_EQUAL(SiegeTargetLost,
		Program::siegeTargetContinuity(false, 42, -1));
	CPPUNIT_ASSERT_EQUAL(SiegeTargetLost,
		Program::siegeTargetContinuity(false, 42, 42));
	CPPUNIT_ASSERT_EQUAL(SiegeTargetReplacementAvailable,
		Program::siegeTargetContinuity(false, 42, 43));
}

void MaximaTacticsTest::testReliefForceSizingAndName()
{
	std::vector<int> powers;
	powers.push_back(9);
	powers.push_back(7);
	powers.push_back(5);
	powers.push_back(3);
	CPPUNIT_ASSERT_EQUAL(3, Program::forceForPower(powers, 16, 3, 4));
	CPPUNIT_ASSERT_EQUAL(0, Program::forceForPower(powers, 30, 2, 4));
	CPPUNIT_ASSERT_EQUAL(std::string("relief"),
		std::string(missionKindName(MissionRelief)));
}

void MaximaTacticsTest::testTacticalLifecycleRegressions()
{

	// Offensive quorum is purely proportional, rounds upward, and remains tunable.
	CPPUNIT_ASSERT(!Program::musterLaunchAllowed(MissionSiege, 11, 5, 11, 11, 50));
	CPPUNIT_ASSERT(Program::musterLaunchAllowed(MissionSiege, 6, 6, 11, 11, 50));
	CPPUNIT_ASSERT(Program::musterLaunchAllowed(MissionSiege, 5, 5, 10, 11, 50));
	CPPUNIT_ASSERT(!Program::musterLaunchAllowed(MissionSiege, 8, 8, 11, 11, 75));
	CPPUNIT_ASSERT(Program::musterLaunchAllowed(MissionSiege, 9, 9, 11, 11, 75));
	CPPUNIT_ASSERT(!Program::musterLaunchAllowed(MissionSiege, 30, 23, 30, 11, 80));
	CPPUNIT_ASSERT(Program::musterLaunchAllowed(MissionSiege, 30, 24, 30, 11, 80));
	CPPUNIT_ASSERT(!Program::musterLaunchAllowed(MissionRaid, 6, 2, 6, 6, 50));
	CPPUNIT_ASSERT(Program::musterLaunchAllowed(MissionRaid, 3, 3, 6, 6, 50));
	CPPUNIT_ASSERT(Program::musterLaunchAllowed(MissionRelief, 6, 0, 10, 6, 80));
	CPPUNIT_ASSERT(!Program::musterReady(0, 0, 0, 50));
	CPPUNIT_ASSERT(!Program::musterReady(0, 0, 11, 50));
	CPPUNIT_ASSERT(!Program::musterReady(5, 6, 11, 50));

	// A nearby moving cluster remains trackable even if its score falls; a
	// distant replacement still needs to clear the existing switch margin.
	CPPUNIT_ASSERT(Program::raidRetargetAllowed(true, 1, 6, 300, 300, 60));
	CPPUNIT_ASSERT(Program::raidRetargetAllowed(true, 36, 6, 250, 300, 60));
	CPPUNIT_ASSERT(!Program::raidRetargetAllowed(true, 37, 6, 359, 300, 60));
	CPPUNIT_ASSERT(Program::raidRetargetAllowed(true, 37, 6, 360, 300, 60));
	CPPUNIT_ASSERT(Program::raidRetargetAllowed(false, 100, 6, 250, 300, 60));

	std::map<int, int> quarantine;
	quarantine[42]=100;
	CPPUNIT_ASSERT(Program::targetQuarantined(42, 99, true, quarantine));
	CPPUNIT_ASSERT(!Program::targetQuarantined(42, 100, true, quarantine));
	CPPUNIT_ASSERT(!Program::targetQuarantined(42, 101, true, quarantine));
	CPPUNIT_ASSERT(!Program::targetQuarantined(43, 99, true, quarantine));
	CPPUNIT_ASSERT(!Program::targetQuarantined(42, 99, false, quarantine));

	Mission siege;
	siege.targetGid=42;
	siege.lastTargetHp=10;
	siege.lastProgressTick=100;
	siege.retargetSiege(43, 7, 8, 200);
	CPPUNIT_ASSERT(siege.targetGid==43 && siege.targetX==7 && siege.targetY==8);
	CPPUNIT_ASSERT(siege.lastTargetHp==-1 && siege.lastProgressTick==200);
	// Repeated updates to the same target must not hide a real stall.
	siege.lastTargetHp=500;
	siege.retargetSiege(43, 7, 8, 300);
	CPPUNIT_ASSERT(siege.lastTargetHp==500 && siege.lastProgressTick==200);
}
