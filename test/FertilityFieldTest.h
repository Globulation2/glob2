// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 glob2 contributors

#pragma once

#include "Glob2Test.h"

// Fertility::Field computes, in closed form, the probability that Map::growResources
// lets a wheat or wood tile expand. These tests pin it to that definition: the kernel
// against the RNG that produces it, and the separable four-pass convolution (plus both
// sand paths) against a direct 961-tap evaluation.
class FertilityFieldTest
{

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
