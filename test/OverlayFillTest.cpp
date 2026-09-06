// SPDX-License-Identifier: GPL-3.0-or-later
//
// Unit tests for the OverlayFill radial-accumulation kernels used by the
// Defence / Starving / Damage map overlays. The regression of interest is that
// spreadPoint() must weight its per-tile increment by `value` (a turret's
// attack power) — before the fix the parameter was accepted and dropped, so
// the Defence overlay only ever visualised geometric footprint, never strength.

#include <cppunit/extensions/HelperMacros.h>

#include <vector>
#include "OverlayFill.h"

class OverlayFillTest: public CPPUNIT_NS::TestCase
{
CPPUNIT_TEST_SUITE(OverlayFillTest);
		CPPUNIT_TEST(testSpreadWeightsByValue);
		CPPUNIT_TEST(testSpreadValueOneMatchesUnitBumpShape);
		CPPUNIT_TEST(testSpreadNoUint16Overflow);
	CPPUNIT_TEST_SUITE_END();

	static const int W = 24;
	static const int H = 24;

protected:
	// The central tile of a single turret's footprint receives
	// value * distance. Two turrets that differ only in `value` must produce
	// centres in exactly that ratio — this is the property the dropped
	// parameter used to violate (both centres came out identical).
	void testSpreadWeightsByValue(void)
	{
		const int distance = 5;

		std::vector<Uint32> weak(W * H, 0);
		Uint32 weakMax = 0;
		OverlayFill::spreadPoint(10, 10, /*value*/1, distance, W, H, weak, weakMax);

		std::vector<Uint32> strong(W * H, 0);
		Uint32 strongMax = 0;
		OverlayFill::spreadPoint(10, 10, /*value*/7, distance, W, H, strong, strongMax);

		const int centre = 10 * H + 10;
		// Centre increment is value * (distance - 0/distance) == value*distance.
		CPPUNIT_ASSERT_EQUAL(static_cast<Uint32>(1 * distance), weak[centre]);
		CPPUNIT_ASSERT_EQUAL(static_cast<Uint32>(7 * distance), strong[centre]);
		// Whole field scales linearly with value.
		CPPUNIT_ASSERT_EQUAL(static_cast<Uint32>(7) * weakMax, strongMax);
	}

	// With value==1 the weighted kernel reduces to a plain footprint bump, so
	// its centre matches distance and the maximum equals the centre.
	void testSpreadValueOneMatchesUnitBumpShape(void)
	{
		const int distance = 6;
		std::vector<Uint32> field(W * H, 0);
		Uint32 max = 0;
		OverlayFill::spreadPoint(12, 12, /*value*/1, distance, W, H, field, max);

		const int centre = 12 * H + 12;
		CPPUNIT_ASSERT_EQUAL(static_cast<Uint32>(distance), field[centre]);
		CPPUNIT_ASSERT_EQUAL(field[centre], max);
	}

	// A cluster of high-power turrets stacked on one tile must accumulate past
	// the Uint16 ceiling without wrapping — the reason the field is Uint32.
	void testSpreadNoUint16Overflow(void)
	{
		const int distance = 5;
		const int value = 1000;
		std::vector<Uint32> field(W * H, 0);
		Uint32 max = 0;

		// 100 overlapping turrets: centre = 100 * value * distance = 500000.
		for (int i = 0; i < 100; i++)
			OverlayFill::spreadPoint(10, 10, value, distance, W, H, field, max);

		const int centre = 10 * H + 10;
		const Uint32 expected = static_cast<Uint32>(100) * value * distance;
		CPPUNIT_ASSERT(expected > 65535u); // would have wrapped in a Uint16 field
		CPPUNIT_ASSERT_EQUAL(expected, field[centre]);
		CPPUNIT_ASSERT_EQUAL(expected, max);
	}
};
CPPUNIT_TEST_SUITE_REGISTRATION(OverlayFillTest);
