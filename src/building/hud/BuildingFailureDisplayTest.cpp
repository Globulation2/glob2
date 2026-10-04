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

#include "Glob2Test.h"

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

	bool mark(const uint32_t (&counts)[kReasonCount], int working, int desired)
	{
		return shouldShowFailingUnitMarkers(counts, kReasonCount, kAvailability, working, desired);
	}
}

class BuildingFailureDisplayTest
{
public:

protected:
	// Nothing is failing: no rows.
	void testNoFailuresHidden(void)
	{
		uint32_t counts[kReasonCount] = {0, 0, 0, 0, 0, 0, 0, 0};
		CHECK(show(counts) == false);
	}

	// Only "units not available" is positive: the normal state, no rows.
	void testOnlyUnavailableHidden(void)
	{
		uint32_t counts[kReasonCount] = {5, 0, 0, 0, 0, 0, 0, 0};
		CHECK(show(counts) == false);
	}

	// A real obstruction with no not-available count: rows shown.
	void testRealObstructionShown(void)
	{
		uint32_t counts[kReasonCount] = {0, 0, 0, 3, 0, 0, 0, 0};
		CHECK(show(counts) == true);
	}

	// Not-available together with a real obstruction: rows shown.
	void testUnavailablePlusRealShown(void)
	{
		uint32_t counts[kReasonCount] = {5, 0, 2, 0, 0, 0, 0, 0};
		CHECK(show(counts) == true);
	}

	// The gate scans the whole array, including the last reason index.
	void testLastReasonShown(void)
	{
		uint32_t counts[kReasonCount] = {0, 0, 0, 0, 0, 0, 0, 1};
		CHECK(show(counts) == true);
	}
	// A building still short of its ratio shows badges exactly when it shows
	// rows: the marker gate adds a condition, it never relaxes one.
	void testMarkersFollowTheRows(void)
	{
		uint32_t real[kReasonCount] = {5, 0, 2, 0, 0, 0, 0, 0};
		uint32_t onlyUnavailable[kReasonCount] = {5, 0, 0, 0, 0, 0, 0, 0};
		CHECK(mark(real, 1, 3) == true);
		CHECK(mark(onlyUnavailable, 1, 3) == false);
	}

	// Staffed to its ratio, the building has stopped asking for units and its
	// tallies are the leftovers of its last scan: no rows, so no badges.
	void testMarkersHiddenWhenNotAsking(void)
	{
		uint32_t real[kReasonCount] = {5, 0, 2, 0, 0, 0, 0, 0};
		CHECK(mark(real, 3, 3) == false);
	}

	// The ratio can be dragged below what is already hired.
	void testMarkersHiddenWhenOverstaffed(void)
	{
		uint32_t real[kReasonCount] = {0, 4, 0, 0, 0, 0, 0, 0};
		CHECK(mark(real, 5, 2) == false);
	}
};

TEST_SUITE("BuildingFailureDisplay")
{
	TEST_CASE_FIXTURE(BuildingFailureDisplayTest, "NoFailuresHidden") { testNoFailuresHidden(); }
	TEST_CASE_FIXTURE(BuildingFailureDisplayTest, "OnlyUnavailableHidden") { testOnlyUnavailableHidden(); }
	TEST_CASE_FIXTURE(BuildingFailureDisplayTest, "RealObstructionShown") { testRealObstructionShown(); }
	TEST_CASE_FIXTURE(BuildingFailureDisplayTest, "UnavailablePlusRealShown") { testUnavailablePlusRealShown(); }
	TEST_CASE_FIXTURE(BuildingFailureDisplayTest, "LastReasonShown") { testLastReasonShown(); }
	TEST_CASE_FIXTURE(BuildingFailureDisplayTest, "MarkersFollowTheRows") { testMarkersFollowTheRows(); }
	TEST_CASE_FIXTURE(BuildingFailureDisplayTest, "MarkersHiddenWhenNotAsking") { testMarkersHiddenWhenNotAsking(); }
	TEST_CASE_FIXTURE(BuildingFailureDisplayTest, "MarkersHiddenWhenOverstaffed") { testMarkersHiddenWhenOverstaffed(); }
}
