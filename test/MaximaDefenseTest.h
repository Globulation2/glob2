#ifndef MAXIMA_DEFENSE_TEST_H
#define MAXIMA_DEFENSE_TEST_H

#include <cppunit/extensions/HelperMacros.h>

class MaximaDefenseTest : public CppUnit::TestFixture
{
	CPPUNIT_TEST_SUITE(MaximaDefenseTest);
	CPPUNIT_TEST(testOpenTerrainRejected);
	CPPUNIT_TEST(testCrossSectionBoundaryAndComponentCollapse);
	CPPUNIT_TEST(testMultiEnemyMembership);
	CPPUNIT_TEST(testToroidalDistanceAndRadiusThreeFootprint);
	CPPUNIT_TEST(testLandRejectsWaterGapAmphibiousAcceptsIt);
	CPPUNIT_TEST(testForcePolicyAndSwimmingEligibility);
	CPPUNIT_TEST(testTopologyRefreshInvalidation);
	CPPUNIT_TEST(testSharedRankingOverlapAndContinuedScan);
	CPPUNIT_TEST(testProbeLargerThanMap);
	CPPUNIT_TEST_SUITE_END();
public:
	void testOpenTerrainRejected();
	void testProbeLargerThanMap();
	void testCrossSectionBoundaryAndComponentCollapse();
	void testMultiEnemyMembership();
	void testToroidalDistanceAndRadiusThreeFootprint();
	void testLandRejectsWaterGapAmphibiousAcceptsIt();
	void testForcePolicyAndSwimmingEligibility();
	void testTopologyRefreshInvalidation();
	void testSharedRankingOverlapAndContinuedScan();
};

#endif
