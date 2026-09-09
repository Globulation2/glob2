#ifndef MAXIMA_TACTICS_TEST_H
#define MAXIMA_TACTICS_TEST_H

#include <cppunit/extensions/HelperMacros.h>

class MaximaTacticsTest : public CppUnit::TestFixture
{
	CPPUNIT_TEST_SUITE(MaximaTacticsTest);
	CPPUNIT_TEST(testClustersVisibleWorkersAcrossMapWrap);
	CPPUNIT_TEST(testThreatsReduceCandidateScore);
	CPPUNIT_TEST(testBalancedForceAndMusterRules);
	CPPUNIT_TEST(testWithdrawalRules);
	CPPUNIT_TEST(testReliefRetargetHysteresis);
	CPPUNIT_TEST(testSiegeTargetContinuity);
	CPPUNIT_TEST(testReliefForceSizingAndName);
	CPPUNIT_TEST(testTacticalLifecycleRegressions);
	CPPUNIT_TEST_SUITE_END();
public:
	void testClustersVisibleWorkersAcrossMapWrap();
	void testThreatsReduceCandidateScore();
	void testBalancedForceAndMusterRules();
	void testWithdrawalRules();
	void testTacticalLifecycleRegressions();
	void testReliefRetargetHysteresis();
	void testSiegeTargetContinuity();
	void testReliefForceSizingAndName();
};

#endif
