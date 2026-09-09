#ifndef MAXIMA_FARMING_TEST_H
#define MAXIMA_FARMING_TEST_H

#include <cppunit/extensions/HelperMacros.h>

class MaximaFarmingTest : public CppUnit::TestFixture
{
	CPPUNIT_TEST_SUITE(MaximaFarmingTest);
	CPPUNIT_TEST(testExactFertilityTargetedCases);
	CPPUNIT_TEST(testAdaptivePathsMatchDirectToroidalRule);
	CPPUNIT_TEST(testExpansionCapacity);
	CPPUNIT_TEST(testWoodPressureIsMonotone);
	CPPUNIT_TEST(testProtectedWheatAdjacencyWraps);
	CPPUNIT_TEST(testSealedCoastalBarrierPorosity);
	CPPUNIT_TEST_SUITE_END();
public:
	void testExactFertilityTargetedCases();
	void testAdaptivePathsMatchDirectToroidalRule();
	void testExpansionCapacity();
	void testWoodPressureIsMonotone();
	void testProtectedWheatAdjacencyWraps();
	void testSealedCoastalBarrierPorosity();
};

#endif
