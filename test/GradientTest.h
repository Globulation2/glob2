// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 glob2 contributors

#pragma once

#include <cppunit/extensions/HelperMacros.h>

// Tests for the pathfinding gradients (Map::propagateGradient,
// Map::directionByGradient, Map::swimClass) on a small toroidal grass map.
class GradientTest: public CppUnit::TestFixture
{
	CPPUNIT_TEST_SUITE( GradientTest );
		CPPUNIT_TEST( testOpenGridIsOctileOnTorus );
		CPPUNIT_TEST( testObstaclesForcePathAround );
		CPPUNIT_TEST( testWaterCostsBySwimClass );
		CPPUNIT_TEST( testUnreachableCellsStayUnreachable );
		CPPUNIT_TEST( testSeedBelowGoalPropagates );
		CPPUNIT_TEST( testDirectionPrefersCheapestTotal );
		CPPUNIT_TEST( testDirectionBlockedNeighbour );
		CPPUNIT_TEST( testSwimClassFromSpeeds );
	CPPUNIT_TEST_SUITE_END();

public:
	void testOpenGridIsOctileOnTorus();
	void testObstaclesForcePathAround();
	void testWaterCostsBySwimClass();
	void testUnreachableCellsStayUnreachable();
	void testSeedBelowGoalPropagates();
	void testDirectionPrefersCheapestTotal();
	void testDirectionBlockedNeighbour();
	void testSwimClassFromSpeeds();
};
