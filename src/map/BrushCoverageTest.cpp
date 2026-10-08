// SPDX-License-Identifier: GPL-3.0-or-later

// BrushCoverage previews a stroke on touch screens before it is applied. These
// cases pin it to the brush masks and checkerboard parity that the zone and
// editor brush operations use, so the preview shows exactly what will paint.

#include "Glob2Test.h"

#include "Brush.h"
#include "BrushCoverage.h"
#include <set>

TEST_SUITE("BrushCoverage")
{
	TEST_CASE("a single-cell brush covers its centre")
	{
		const auto cells = BrushCoverage::cells(0, {{5, 7}});
		CHECK(cells.size() == 1);
		CHECK(cells.count({5, 7}) == 1);
		CHECK(BrushCoverage::cells(0, {}).empty());
	}

	TEST_CASE("the plus brush covers a plus")
	{
		const auto cells = BrushCoverage::cells(1, {{10, 10}});
		const std::set<BrushCoverage::Cell> plus{{10, 9}, {9, 10}, {10, 10}, {11, 10}, {10, 11}};
		CHECK(cells == plus);
	}

	TEST_CASE("a stroke covers the union of its centres")
	{
		// Two overlapping 5x5 squares one cell apart cover a 6x5 block.
		const auto cells = BrushCoverage::cells(7, {{0, 0}, {1, 0}});
		CHECK(cells.size() == 30);
		CHECK((cells.count({-2, -2}) && cells.count({3, 2})));
		CHECK(cells.count({4, 0}) == 0);
	}

	TEST_CASE("every figure matches its mask, including checkerboard parity")
	{
		const std::vector<BrushCoverage::Cell> centres{{20, 30}, {21, 30}, {22, 31}, {23, 33}};
		for (unsigned figure = 0; figure < BrushTool::BRUSH_COUNT; ++figure)
		{
			std::set<BrushCoverage::Cell> expected;
			for (const auto &[cx, cy] : centres)
				for (int y = 0; y < BrushTool::getBrushHeight(figure); ++y)
					for (int x = 0; x < BrushTool::getBrushWidth(figure); ++x)
						if (BrushTool::getBrushValue(figure, x, y, cx, cy, 20, 30))
							expected.insert({cx - BrushTool::getBrushDimXMinus(figure) + x,
											 cy - BrushTool::getBrushDimYMinus(figure) + y});
			CHECK_MESSAGE(BrushCoverage::cells(figure, centres) == expected, "figure " << figure);
		}
		// A checkerboard shifts with the parity of the stroke origin.
		CHECK(BrushCoverage::cells(4, {{2, 2}}) != BrushCoverage::cells(4, {{3, 2}, {2, 2}}));
	}

	TEST_CASE("world pixels map to the cell under them")
	{
		CHECK(BrushCoverage::cellAt(31.9, 32) == BrushCoverage::Cell(0, 1));
		CHECK(BrushCoverage::cellAt(-0.5, 0) == BrushCoverage::Cell(-1, 0));
		CHECK(BrushCoverage::cellAt(16, 16, 16) == BrushCoverage::Cell(1, 1));
	}

	TEST_CASE("a stamp is one centre of a stroke aligned to its origin")
	{
		for (unsigned figure = 0; figure < BrushTool::BRUSH_COUNT; ++figure)
		{
			CHECK(BrushCoverage::stamp(figure, {6, 6}, {6, 6}) == BrushCoverage::cells(figure, {{6, 6}}));
			auto both = BrushCoverage::stamp(figure, {4, 4}, {4, 4});
			both.merge(BrushCoverage::stamp(figure, {9, 5}, {4, 4}));
			CHECK(both == BrushCoverage::cells(figure, {{4, 4}, {9, 5}}));
		}
	}
}
