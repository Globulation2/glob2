#ifndef MAXIMA_RECON_TEST_H
#define MAXIMA_RECON_TEST_H

#include <cppunit/extensions/HelperMacros.h>

class MaximaReconTest : public CppUnit::TestFixture
{
	CPPUNIT_TEST_SUITE(MaximaReconTest);
	CPPUNIT_TEST(testConfidenceAndEstimates);
	CPPUNIT_TEST(testLightweightForceSamplesPreservePeak);
	CPPUNIT_TEST(testBuildingMemoryRequiresConfirmation);
	CPPUNIT_TEST(testMissionScalingAndEmergency);
	CPPUNIT_TEST(testDynamicOpponentPriority);
	CPPUNIT_TEST(testFrontierSelectionIsStableAndSeparated);
	CPPUNIT_TEST(testEconomicActivityMemoryAndMissionKind);
	CPPUNIT_TEST_SUITE_END();
public:
	void testConfidenceAndEstimates();
	void testLightweightForceSamplesPreservePeak();
	void testBuildingMemoryRequiresConfirmation();
	void testMissionScalingAndEmergency();
	void testDynamicOpponentPriority();
	void testFrontierSelectionIsStableAndSeparated();
	void testEconomicActivityMemoryAndMissionKind();
};

#endif
