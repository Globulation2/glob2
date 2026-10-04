// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 glob2 contributors

#pragma once

#include "Glob2Test.h"

// Tests for the pathfinding gradients (Map::propagateGradient,
// Map::directionByGradient, Map::swimClass) on a small toroidal grass map.
class GradientTest
{

public:
	void testOpenGridIsOctileOnTorus();
	void testObstaclesForcePathAround();
	void testWaterCostsBySwimClass();
	void testUnreachableCellsStayUnreachable();
	void testSeedBelowGoalPropagates();
	void testSeedsBeyondBucketWindow();
	void testMaxCostStopsPropagation();
	void testDirectionPrefersCheapestTotal();
	void testDirectionBlockedNeighbour();
	void testSwimClassFromSpeeds();
	void testRandomFieldsAgainstReference();
	void testMatchesLegacyKernelOnLargeMaps();
};
