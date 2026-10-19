// SPDX-License-Identifier: GPL-3.0-or-later
//
// Behavior lock for BrushAccumulator::applyBrush/getBitmap, focused on
// strokes that cross the torus seam. The bitmap and its AreaDimensions
// feed OrderAlterArea, which goes over the network and into the game
// checksum — so every expectation here (dimensions, origin, exact set
// bits) is wire-format-relevant and must not drift.
//
// Seam-wrapping applications used to rely on getBitmap re-deriving the
// same wrapped offset that applyBrush used to grow the bounding box; a
// mismatch would have sent BitArray::set out of bounds (BitArray aborts
// on that, so this test would fail loudly, not corrupt memory).

#include "Glob2Test.h"

#include "BitArray.h"
#include "Brush.h"
#include "Map.h"
#include "Order.h"
#include "Version.h"
#include "ai/observation/ObservationAreaOrders.h"

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

class BrushAccumulatorTest
{
public:

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
		CHECK(acc.getBitmap(&bits, &dim, &map));

		CHECK_EQ(10, dim.centerX);
		CHECK_EQ(10, dim.centerY);
		CHECK_EQ(0, dim.minX);
		CHECK_EQ(0, dim.minY);
		CHECK_EQ(1, dim.maxX);
		CHECK_EQ(1, dim.maxY);
		CHECK_EQ(static_cast<size_t>(1), bits.getBitLength());
		CHECK_EQ(true, bits.get(0));
	}

	void testNonWrappingStroke(void)
	{
		TorusMap map(5); // 32x32
		BrushAccumulator acc;
		acc.applyBrush(BrushApplication(10, 10, FIG_1X1), &map);
		acc.applyBrush(BrushApplication(12, 11, FIG_1X1), &map);

		Utilities::BitArray bits;
		BrushAccumulator::AreaDimensions dim;
		CHECK(acc.getBitmap(&bits, &dim, &map));

		CHECK_EQ(10, dim.centerX);
		CHECK_EQ(10, dim.centerY);
		CHECK_EQ(0, dim.minX);
		CHECK_EQ(0, dim.minY);
		CHECK_EQ(3, dim.maxX);
		CHECK_EQ(2, dim.maxY);
		// 3x2 bitmap: bit (0,0) from the first click, bit (2,1) from the second.
		CHECK_EQ(static_cast<size_t>(6), bits.getBitLength());
		for (size_t pos = 0; pos < 6; pos++)
			CHECK_EQ(pos == 0 || pos == 5, bits.get(pos));
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
		CHECK(acc.getBitmap(&bits, &dim, &map));

		CHECK_EQ(0, dim.centerX);
		CHECK_EQ(0, dim.centerY);
		CHECK_EQ(-2, dim.minX);
		CHECK_EQ(0, dim.minY);
		CHECK_EQ(3, dim.maxX);
		CHECK_EQ(1, dim.maxY);
		// 5x1 bitmap: cells at offsets -2, 0, +2 => array x 0, 2, 4.
		CHECK_EQ(static_cast<size_t>(5), bits.getBitLength());
		for (size_t pos = 0; pos < 5; pos++)
			CHECK_EQ(pos % 2 == 0, bits.get(pos));
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
		CHECK(acc.getBitmap(&bits, &dim, &map));

		CHECK_EQ(30, dim.centerX);
		CHECK_EQ(0, dim.minX);
		CHECK_EQ(3, dim.maxX);
		CHECK_EQ(static_cast<size_t>(3), bits.getBitLength());
		CHECK_EQ(true, bits.get(0));
		CHECK_EQ(false, bits.get(1));
		CHECK_EQ(true, bits.get(2));
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
		CHECK(acc.getBitmap(&bits, &dim, &map));

		CHECK_EQ(1, dim.centerX);
		CHECK_EQ(1, dim.centerY);
		CHECK_EQ(-5, dim.minX);
		CHECK_EQ(-5, dim.minY);
		CHECK_EQ(3, dim.maxX);
		CHECK_EQ(3, dim.maxY);
		CHECK_EQ(static_cast<size_t>(64), bits.getBitLength());
		for (int y = 0; y < 8; y++)
		{
			for (int x = 0; x < 8; x++)
			{
				// First application fills array cells [3,7]x[3,7],
				// the wrapped one fills [0,4]x[0,4]; they overlap in [3,4]^2.
				const bool inFirst = (x >= 3 && y >= 3);
				const bool inSecond = (x <= 4 && y <= 4);
				CHECK_EQ(inFirst || inSecond, bits.get(static_cast<size_t>(y * 8 + x)));
			}
		}
	}
};

TEST_SUITE("BrushAccumulator")
{
	TEST_CASE_FIXTURE(BrushAccumulatorTest, "SingleApplication") { testSingleApplication(); }
	TEST_CASE_FIXTURE(BrushAccumulatorTest, "NonWrappingStroke") { testNonWrappingStroke(); }
	TEST_CASE_FIXTURE(BrushAccumulatorTest, "OppositeSeamWrapsOneRow") { testOppositeSeamWrapsOneRow(); }
	TEST_CASE_FIXTURE(BrushAccumulatorTest, "WrapNegativeDelta") { testWrapNegativeDelta(); }
	TEST_CASE_FIXTURE(BrushAccumulatorTest, "LargeBrushAcrossSeamBothAxes") { testLargeBrushAcrossSeamBothAxes(); }
}

TEST_SUITE("BrushAccumulatorOrderBounds")
{
    TEST_CASE("OversizedStrokePreservesEveryPaintedWorldTile")
    {
        BrushAccumulator acc;
        acc.applyBrush(BrushApplication(256, 256, 7), 512, 512);
        acc.applyBrush(BrushApplication(0, 0, 7), 512, 512);
        acc.applyBrush(BrushApplication(511, 511, 7), 512, 512);
        Utilities::BitArray original, folded;
        BrushAccumulator::AreaDimensions before, after;
        REQUIRE(acc.getBitmap(&original, &before));
        REQUIRE(acc.getOrderBitmap(&folded, &after, 512));
        REQUIRE(before.maxX - before.minX > 512);
        REQUIRE(before.maxY - before.minY > 512);
        CHECK_EQ(512, after.maxX - after.minX);
        CHECK_EQ(512, after.maxY - after.minY);
        Utilities::BitArray expected;
        expected.resize(512 * 512, false);
        const int width = before.maxX - before.minX;
        for (int y = before.minY; y < before.maxY; ++y)
            for (int x = before.minX; x < before.maxX; ++x)
                if (original.get(static_cast<size_t>(y - before.minY) * width + x - before.minX))
                    expected.set(static_cast<size_t>((before.centerY + y + 1024) % 512) * 512
                        + (before.centerX + x + 1024) % 512, true);
        for (int y = 0; y < 512; ++y)
            for (int x = 0; x < 512; ++x)
                CHECK_EQ(expected.get(static_cast<size_t>((after.centerY + after.minY + y + 1024) % 512) * 512
                    + (after.centerX + after.minX + x + 1024) % 512), folded.get(static_cast<size_t>(y) * 512 + x));
    }
    TEST_CASE("OrdinaryStrokeRetainsItsExactBitmapAndBounds")
    {
        BrushAccumulator acc;
        acc.applyBrush(BrushApplication(1, 1, 7), 512, 512);
        acc.applyBrush(BrushApplication(510, 510, 7), 512, 512);
        Utilities::BitArray original, folded;
        BrushAccumulator::AreaDimensions before, after;
        REQUIRE(acc.getBitmap(&original, &before));
        REQUIRE(acc.getOrderBitmap(&folded, &after, 512));
        CHECK_EQ(before.centerX, after.centerX); CHECK_EQ(before.centerY, after.centerY);
        CHECK_EQ(before.minX, after.minX); CHECK_EQ(before.maxX, after.maxX);
        CHECK_EQ(before.minY, after.minY); CHECK_EQ(before.maxY, after.maxY);
        REQUIRE(original.getBitLength() == folded.getBitLength());
        for (size_t i = 0; i < original.getBitLength(); ++i) CHECK_EQ(original.get(i), folded.get(i));
    }
}

TEST_SUITE("BrushAccumulatorOrderCodec")
{
    TEST_CASE("RectangularTorusFoldsOnlyOversizedAxisAndRoundTripsAddAndDelete")
    {
        BrushAccumulator acc;
        acc.applyBrush(BrushApplication(128, 256, 7), 256, 512);
        acc.applyBrush(BrushApplication(128, 0, 7), 256, 512);
        acc.applyBrush(BrushApplication(128, 511, 7), 256, 512);
        Utilities::BitArray original;
        BrushAccumulator::AreaDimensions before;
        REQUIRE(acc.getBitmap(&original, &before));
        REQUIRE(before.maxY - before.minY > 512);
        for (const auto mode : {BrushTool::MODE_ADD, BrushTool::MODE_DEL})
        {
            auto order = AIEngine::observationAreaOrder<OrderAlterGuardArea>(0, mode, acc);
            std::vector<Uint8> bytes{order->getOrderType()};
            const auto* data = order->getData();
            bytes.insert(bytes.end(), data, data + order->getDataLength());
            // Unit builds provide an intentionally narrow Order::getOrder stub;
            // exercise the real guard-area payload decoder directly.
            CHECK_EQ(ORDER_ALTER_GUARD_AREA, bytes.front());
            auto decoded = OrderAlterGuardArea::deserialize(bytes.data() + 1, bytes.size() - 1, VERSION_MINOR);
            REQUIRE(decoded);
            auto* area = dynamic_cast<OrderAlterGuardArea*>(decoded.get());
            REQUIRE(area);
            CHECK_EQ(mode, area->type);
            CHECK_EQ(before.minX, area->minX); CHECK_EQ(before.maxX, area->maxX);
            CHECK_EQ(before.minY, area->minY); CHECK_EQ(before.minY + 512, area->maxY);
            const int width = before.maxX - before.minX;
            // The decoded mask must describe exactly the same world cells for
            // either operation, including both aliases of a seam-crossing brush.
            for (int y = 0; y < 512; ++y)
                for (int x = 0; x < width; ++x)
                {
                    bool expected = false;
                    for (int oldY = y; oldY < before.maxY - before.minY; oldY += 512)
                        expected |= original.get(static_cast<size_t>(oldY) * width + x);
                    CHECK_EQ(expected, area->mask.get(static_cast<size_t>(y) * width + x));
                }
        }
    }
    TEST_CASE("OrdinaryObservationOrderHasIdenticalWireBytes")
    {
        BrushAccumulator acc;
        acc.applyBrush(BrushApplication(1, 1, 7), 512, 512);
        acc.applyBrush(BrushApplication(510, 510, 7), 512, 512);
        Utilities::BitArray mask;
        BrushAccumulator::AreaDimensions dimensions;
        REQUIRE(acc.getBitmap(&mask, &dimensions));
        OrderAlterGuardArea previous(0, BrushTool::MODE_ADD, dimensions.centerX, dimensions.centerY,
            dimensions.maxX - dimensions.minX, dimensions.maxY - dimensions.minY, mask);
        previous.minX = dimensions.minX; previous.maxX = dimensions.maxX;
        previous.minY = dimensions.minY; previous.maxY = dimensions.maxY;
        auto current = AIEngine::observationAreaOrder<OrderAlterGuardArea>(0, BrushTool::MODE_ADD, acc);
        REQUIRE(previous.getDataLength() == current->getDataLength());
        const auto* oldData = previous.getData(); const auto* newData = current->getData();
        CHECK_EQ(previous.getOrderType(), current->getOrderType());
        for (int i = 0; i < previous.getDataLength(); ++i) CHECK_EQ(oldData[i], newData[i]);
    }
}
