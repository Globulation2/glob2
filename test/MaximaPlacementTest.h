#ifndef MAXIMA_PLACEMENT_TEST_H
#define MAXIMA_PLACEMENT_TEST_H

#include <cppunit/extensions/HelperMacros.h>

class MaximaPlacementTest : public CppUnit::TestFixture
{
	CPPUNIT_TEST_SUITE(MaximaPlacementTest);
	CPPUNIT_TEST(testTemplatesUseTerminalGeometry);
	CPPUNIT_TEST(testStrictAndFallbackWaterTiers);
	CPPUNIT_TEST(testReservationsAndDeterministicChoice);
	CPPUNIT_TEST(testCommittedCountsIncludeUnobservedBuilds);
	CPPUNIT_TEST(testReservationsPreserveResourcesOutsideImmediateFootprint);
	CPPUNIT_TEST(testInnerBuildingsAvoidValuableFoodZones);
	CPPUNIT_TEST(testRequiredSourceAndNegativeUtilityWait);
	CPPUNIT_TEST(testLifecycleClassifiesOutcomes);
	CPPUNIT_TEST(testColonyPurposeDistanceCornAndBlockedIndependence);
	CPPUNIT_TEST(testColonyThreatAndConqueredScoring);
	CPPUNIT_TEST(testDisconnectedColonyRequiresSwimmingBuilders);
	CPPUNIT_TEST_SUITE_END();
public:
	void testTemplatesUseTerminalGeometry();
	void testStrictAndFallbackWaterTiers();
	void testReservationsAndDeterministicChoice();
	void testCommittedCountsIncludeUnobservedBuilds();
	void testReservationsPreserveResourcesOutsideImmediateFootprint();
	void testInnerBuildingsAvoidValuableFoodZones();
	void testRequiredSourceAndNegativeUtilityWait();
	void testLifecycleClassifiesOutcomes();
	void testColonyPurposeDistanceCornAndBlockedIndependence();
	void testColonyThreatAndConqueredScoring();
	void testDisconnectedColonyRequiresSwimmingBuilders();
};

#endif
