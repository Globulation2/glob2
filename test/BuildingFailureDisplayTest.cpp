// SPDX-License-Identifier: GPL-3.0-or-later
//
// Regression test for the failure-reason display gate in
// GameGUI::drawBuildingFailureReasons(). The old code initialised its
// "otherFailure" flag to true and only ever set it true again, so the gate was
// dead and the per-reason rows were shown for every failing building — even
// when the sole failure was "units not available", which is a building's normal
// unremarkable state. The decision is now the pure, SDL-free helper
// shouldShowBuildingFailureReasons(), so this test needs no SDL/GameGUI linkage;
// it just pins the truth table for when the block is shown.
//
// Index 0 mirrors Building::UnitNotAvailable; the remaining indices are the
// "real obstruction" reasons (too-low-level, can't-access, too-far, ...).

#include <cppunit/extensions/HelperMacros.h>

#include <cstdint>

#include "BuildingFailureDisplay.h"

namespace
{
	// Matches Building::UnitCantWorkReasonSize / Building::UnitNotAvailable.
	constexpr unsigned kReasonCount = 8;
	constexpr unsigned kAvailability = 0;

	bool show(const uint32_t (&counts)[kReasonCount])
	{
		return shouldShowBuildingFailureReasons(counts, kReasonCount, kAvailability);
	}
}

class BuildingFailureDisplayTest: public CPPUNIT_NS::TestCase
{
CPPUNIT_TEST_SUITE(BuildingFailureDisplayTest);
		CPPUNIT_TEST(testNoFailuresHidden);
		CPPUNIT_TEST(testOnlyUnavailableHidden);
		CPPUNIT_TEST(testRealObstructionShown);
		CPPUNIT_TEST(testUnavailablePlusRealShown);
		CPPUNIT_TEST(testLastReasonShown);
	CPPUNIT_TEST_SUITE_END();

protected:
	// Nothing is failing: no rows.
	void testNoFailuresHidden(void)
	{
		uint32_t counts[kReasonCount] = {0, 0, 0, 0, 0, 0, 0, 0};
		CPPUNIT_ASSERT(show(counts) == false);
	}

	// Only "units not available" is positive: the normal state, no rows.
	void testOnlyUnavailableHidden(void)
	{
		uint32_t counts[kReasonCount] = {5, 0, 0, 0, 0, 0, 0, 0};
		CPPUNIT_ASSERT(show(counts) == false);
	}

	// A real obstruction with no not-available count: rows shown.
	void testRealObstructionShown(void)
	{
		uint32_t counts[kReasonCount] = {0, 0, 0, 3, 0, 0, 0, 0};
		CPPUNIT_ASSERT(show(counts) == true);
	}

	// Not-available together with a real obstruction: rows shown.
	void testUnavailablePlusRealShown(void)
	{
		uint32_t counts[kReasonCount] = {5, 0, 2, 0, 0, 0, 0, 0};
		CPPUNIT_ASSERT(show(counts) == true);
	}

	// The gate scans the whole array, including the last reason index.
	void testLastReasonShown(void)
	{
		uint32_t counts[kReasonCount] = {0, 0, 0, 0, 0, 0, 0, 1};
		CPPUNIT_ASSERT(show(counts) == true);
	}
};

CPPUNIT_TEST_SUITE_REGISTRATION(BuildingFailureDisplayTest);
