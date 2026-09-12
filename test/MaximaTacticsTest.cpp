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

void MaximaTacticsTest::testTacticalLifecycleRegressions()
{

	CPPUNIT_ASSERT_EQUAL(std::string("siege"), std::string(missionKindName(MissionSiege)));

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
