// SPDX-License-Identifier: GPL-3.0-or-later
//
// Behavior lock for BrushAccumulator::applyBrush/getBitmap, focused on
// strokes that cross the torus seam. The bitmap and its AreaDimensions
// feed OrderAlterateArea, which goes over the network and into the game
// checksum — so every expectation here (dimensions, origin, exact set
// bits) is wire-format-relevant and must not drift.
//
// Seam-wrapping applications used to rely on getBitmap re-deriving the
// same wrapped offset that applyBrush used to grow the bounding box; a
// mismatch would have sent BitArray::set out of bounds (BitArray aborts
// on that, so this test would fail loudly, not corrupt memory).

#include <cppunit/extensions/HelperMacros.h>

#include "BitArray.h"
#include "Brush.h"
#include "Map.h"

namespace
{
	// Minimal torus for BrushAccumulator, which only calls getW()/getH().
	// Same fixture trick as MapQueryTest's GrassMap: bypass Map::setSize()
	// (it constructs Sector[], dragging in most of the game) and zero the
	// size fields before Map::~Map() so clear()'s else-branch asserts pass.
	struct TorusMap : Map
	{
		explicit TorusMap(int dec)
		{
			wDec = dec;
			hDec = dec;
			w = 1 << dec;
			h = 1 << dec;
			wMask = w - 1;
			hMask = h - 1;
			size = static_cast<size_t>(w) * static_cast<size_t>(h);
		}
		~TorusMap()
		{
			w = h = 0;
			wMask = hMask = 0;
			wDec = hDec = 0;
			size = 0;
		}
	};
}

class BrushAccumulatorTest : public CPPUNIT_NS::TestCase
{
CPPUNIT_TEST_SUITE(BrushAccumulatorTest);
		CPPUNIT_TEST(testSingleApplication);
		CPPUNIT_TEST(testNonWrappingStroke);
		CPPUNIT_TEST(testOppositeSeamWrapsOneRow);
		CPPUNIT_TEST(testWrapNegativeDelta);
		CPPUNIT_TEST(testLargeBrushAcrossSeamBothAxes);
	CPPUNIT_TEST_SUITE_END();

protected:
	// Figure 0 is the 1x1 brush: XMinus/YMinus = 0, XPlus/YPlus = 1.
	static const int FIG_1X1 = 0;
	// Figure 7 is the full 5x5 brush: XMinus/YMinus = 2, XPlus/YPlus = 3.
	static const int FIG_5X5 = 7;

	void testSingleApplication(void)
	{
		TorusMap map(5); // 32x32
		BrushAccumulator acc;
		acc.applyBrush(BrushApplication(10, 10, FIG_1X1), &map);

		Utilities::BitArray bits;
		BrushAccumulator::AreaDimensions dim;
		CPPUNIT_ASSERT(acc.getBitmap(&bits, &dim, &map));

		CPPUNIT_ASSERT_EQUAL(10, dim.centerX);
		CPPUNIT_ASSERT_EQUAL(10, dim.centerY);
		CPPUNIT_ASSERT_EQUAL(0, dim.minX);
		CPPUNIT_ASSERT_EQUAL(0, dim.minY);
		CPPUNIT_ASSERT_EQUAL(1, dim.maxX);
		CPPUNIT_ASSERT_EQUAL(1, dim.maxY);
		CPPUNIT_ASSERT_EQUAL(static_cast<size_t>(1), bits.getBitLength());
		CPPUNIT_ASSERT_EQUAL(true, bits.get(0));
	}

	void testNonWrappingStroke(void)
	{
		TorusMap map(5); // 32x32
		BrushAccumulator acc;
		acc.applyBrush(BrushApplication(10, 10, FIG_1X1), &map);
		acc.applyBrush(BrushApplication(12, 11, FIG_1X1), &map);

		Utilities::BitArray bits;
		BrushAccumulator::AreaDimensions dim;
		CPPUNIT_ASSERT(acc.getBitmap(&bits, &dim, &map));

		CPPUNIT_ASSERT_EQUAL(10, dim.centerX);
		CPPUNIT_ASSERT_EQUAL(10, dim.centerY);
		CPPUNIT_ASSERT_EQUAL(0, dim.minX);
		CPPUNIT_ASSERT_EQUAL(0, dim.minY);
		CPPUNIT_ASSERT_EQUAL(3, dim.maxX);
		CPPUNIT_ASSERT_EQUAL(2, dim.maxY);
		// 3x2 bitmap: bit (0,0) from the first click, bit (2,1) from the second.
		CPPUNIT_ASSERT_EQUAL(static_cast<size_t>(6), bits.getBitLength());
		for (size_t pos = 0; pos < 6; pos++)
			CPPUNIT_ASSERT_EQUAL(pos == 0 || pos == 5, bits.get(pos));
	}

	void testOppositeSeamWrapsOneRow(void)
	{
		// Stroke centered on the seam cell (0,0): one click wraps backwards
		// across the seam (x=30 -> offset -2), one forwards (x=2 -> offset +2).
		TorusMap map(5); // 32x32
		BrushAccumulator acc;
		acc.applyBrush(BrushApplication(0, 0, FIG_1X1), &map);
		acc.applyBrush(BrushApplication(30, 0, FIG_1X1), &map);
		acc.applyBrush(BrushApplication(2, 0, FIG_1X1), &map);

		Utilities::BitArray bits;
		BrushAccumulator::AreaDimensions dim;
		CPPUNIT_ASSERT(acc.getBitmap(&bits, &dim, &map));

		CPPUNIT_ASSERT_EQUAL(0, dim.centerX);
		CPPUNIT_ASSERT_EQUAL(0, dim.centerY);
		CPPUNIT_ASSERT_EQUAL(-2, dim.minX);
		CPPUNIT_ASSERT_EQUAL(0, dim.minY);
		CPPUNIT_ASSERT_EQUAL(3, dim.maxX);
		CPPUNIT_ASSERT_EQUAL(1, dim.maxY);
		// 5x1 bitmap: cells at offsets -2, 0, +2 => array x 0, 2, 4.
		CPPUNIT_ASSERT_EQUAL(static_cast<size_t>(5), bits.getBitLength());
		for (size_t pos = 0; pos < 5; pos++)
			CPPUNIT_ASSERT_EQUAL(pos % 2 == 0, bits.get(pos));
	}

	void testWrapNegativeDelta(void)
	{
		// Center near the high edge, second click past the seam: the raw
		// delta 0-30 = -30 must wrap to +2, not extend the box to the left.
		TorusMap map(5); // 32x32
		BrushAccumulator acc;
		acc.applyBrush(BrushApplication(30, 5, FIG_1X1), &map);
		acc.applyBrush(BrushApplication(0, 5, FIG_1X1), &map);

		Utilities::BitArray bits;
		BrushAccumulator::AreaDimensions dim;
		CPPUNIT_ASSERT(acc.getBitmap(&bits, &dim, &map));

		CPPUNIT_ASSERT_EQUAL(30, dim.centerX);
		CPPUNIT_ASSERT_EQUAL(0, dim.minX);
		CPPUNIT_ASSERT_EQUAL(3, dim.maxX);
		CPPUNIT_ASSERT_EQUAL(static_cast<size_t>(3), bits.getBitLength());
		CPPUNIT_ASSERT_EQUAL(true, bits.get(0));
		CPPUNIT_ASSERT_EQUAL(false, bits.get(1));
		CPPUNIT_ASSERT_EQUAL(true, bits.get(2));
	}

	void testLargeBrushAcrossSeamBothAxes(void)
	{
		// 5x5 brushes wrapping both axes on a small 16x16 map. The second
		// application's raw delta is +13 on each axis, which wraps to -3;
		// its brush extends the box to min (-5,-5) while the first click's
		// box already reached max (3,3), so the bitmap is 8x8 — wider than
		// half the map. Every index must stay inside those 64 bits.
		TorusMap map(4); // 16x16
		BrushAccumulator acc;
		acc.applyBrush(BrushApplication(1, 1, FIG_5X5), &map);
		acc.applyBrush(BrushApplication(14, 14, FIG_5X5), &map);

		Utilities::BitArray bits;
		BrushAccumulator::AreaDimensions dim;
		CPPUNIT_ASSERT(acc.getBitmap(&bits, &dim, &map));

		CPPUNIT_ASSERT_EQUAL(1, dim.centerX);
		CPPUNIT_ASSERT_EQUAL(1, dim.centerY);
		CPPUNIT_ASSERT_EQUAL(-5, dim.minX);
		CPPUNIT_ASSERT_EQUAL(-5, dim.minY);
		CPPUNIT_ASSERT_EQUAL(3, dim.maxX);
		CPPUNIT_ASSERT_EQUAL(3, dim.maxY);
		CPPUNIT_ASSERT_EQUAL(static_cast<size_t>(64), bits.getBitLength());
		for (int y = 0; y < 8; y++)
		{
			for (int x = 0; x < 8; x++)
			{
				// First application fills array cells [3,7]x[3,7],
				// the wrapped one fills [0,4]x[0,4]; they overlap in [3,4]^2.
				const bool inFirst = (x >= 3 && y >= 3);
				const bool inSecond = (x <= 4 && y <= 4);
				CPPUNIT_ASSERT_EQUAL(inFirst || inSecond,
					bits.get(static_cast<size_t>(y * 8 + x)));
			}
		}
	}
};

CPPUNIT_TEST_SUITE_REGISTRATION( BrushAccumulatorTest );
