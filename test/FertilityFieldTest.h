// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 glob2 contributors

#pragma once

#include <cppunit/extensions/HelperMacros.h>

// Fertility::Field computes, in closed form, the probability that Map::growResources
// lets a wheat or wood tile expand. These tests pin it to that definition: the kernel
// against the RNG that produces it, and the separable four-pass convolution (plus both
// sand paths) against a direct 961-tap evaluation.
class FertilityFieldTest: public CppUnit::TestFixture
{
	CPPUNIT_TEST_SUITE( FertilityFieldTest );
		CPPUNIT_TEST( testTriangularWeightsMatchGrowResourcesRng );
		CPPUNIT_TEST( testConvolutionMatchesDirectKernelWithoutSand );
		CPPUNIT_TEST( testSandCorrectionPathMatchesDirectKernel );
		CPPUNIT_TEST( testWaterSplatPathMatchesDirectKernel );
		CPPUNIT_TEST( testAdaptivePathMatchesExplicitPaths );
		CPPUNIT_TEST( testOpenWaterReachesFullScale );
		CPPUNIT_TEST( testSandOppositeWaterRemovesCredit );
		CPPUNIT_TEST( testNonPowerOfTwoDimensionsWrap );
		CPPUNIT_TEST( testForMapZeroesNonGrass );
		CPPUNIT_TEST( testForMapZeroesGrassNoDepositReaches );
		CPPUNIT_TEST( testForMapUngatedKeepsUnreachableGrass );
		CPPUNIT_TEST( testUsefulExpansionCapacityFolds );
		CPPUNIT_TEST( testWithinPercentBand );
	CPPUNIT_TEST_SUITE_END();

public:
	void testTriangularWeightsMatchGrowResourcesRng();
	void testConvolutionMatchesDirectKernelWithoutSand();
	void testSandCorrectionPathMatchesDirectKernel();
	void testWaterSplatPathMatchesDirectKernel();
	void testAdaptivePathMatchesExplicitPaths();
	void testOpenWaterReachesFullScale();
	void testSandOppositeWaterRemovesCredit();
	void testNonPowerOfTwoDimensionsWrap();
	void testForMapZeroesNonGrass();
	void testForMapZeroesGrassNoDepositReaches();
	void testForMapUngatedKeepsUnreachableGrass();
	void testUsefulExpansionCapacityFolds();
	void testWithinPercentBand();
};
