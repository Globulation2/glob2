// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 glob2 contributors

#pragma once

#include <cppunit/extensions/HelperMacros.h>

// Tests for the weighted distance fields of the alternative pathfinder
// (Map::buildWeightedField, Map::writeGradientFromCost, Map::directionByCost)
// on a small toroidal grass map.
class WeightedFieldTest: public CppUnit::TestFixture
{
	CPPUNIT_TEST_SUITE( WeightedFieldTest );
		CPPUNIT_TEST( testOpenGridIsOctileOnTorus );
		CPPUNIT_TEST( testObstaclesForcePathAround );
		CPPUNIT_TEST( testWaterCostsDoubleForSwimmers );
		CPPUNIT_TEST( testUnreachableCellsStayInfinite );
		CPPUNIT_TEST( testGradientWriteback );
		CPPUNIT_TEST( testDirectionByCostPrefersCheapestTotal );
		CPPUNIT_TEST( testDirectionByCostBlockedNeighbour );
	CPPUNIT_TEST_SUITE_END();

public:
	void testOpenGridIsOctileOnTorus();
	void testObstaclesForcePathAround();
	void testWaterCostsDoubleForSwimmers();
	void testUnreachableCellsStayInfinite();
	void testGradientWriteback();
	void testDirectionByCostPrefersCheapestTotal();
	void testDirectionByCostBlockedNeighbour();
};
