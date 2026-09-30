// SPDX-License-Identifier: GPL-3.0-or-later

// Regression test for BH-007. BrushTool::handleClick used to inline its hit grid
// as `if (y>0 && x>0 && x<128)` over hand-typed literals, duplicating the layout
// that draw() re-typed independently. The grid now lives in named constants and a
// pure BrushTool::hitTest().
//
// The point of this fixture is not to spot-check the new predicate but to pin it
// against the old one: referenceHit() below is a transcription of the pre-fix
// handleClick body, and the tests brute-force both over a range that covers the
// panel and well past every edge. They must agree everywhere except the two cells
// the fix deliberately changes (x==0 and y==0, see testOffByOneIsTheOnlyChange).
//
// Brush.h only pulls in <cstddef>/<optional>/<vector> and forward-declares Color,
// and hitTest is inline, so this links nothing — no globalContainer, no SDL.

#include "Glob2Test.h"

#include <optional>

#include "Brush.h"

namespace
{
	/// The original algorithm, transcribed from the pre-BH-007 handleClick.
	/// Returns what the click would have selected, ignoring the mode-defaulting
	/// and the addRemoveEnabled policy, which both sit outside the hit grid.
	std::optional<BrushTool::Hit> referenceHit(int x, int y)
	{
		if (y>0 && x>0 && x<128)
		{
			if (y<36)
			{
				return BrushTool::Hit{ BrushTool::Hit::ModeButton, static_cast<unsigned>((x/64)+1) };
			}
			else if (y<36+64)
			{
				y -= 36;
				return BrushTool::Hit{ BrushTool::Hit::FigureButton, static_cast<unsigned>((y/32)*4 + ((x/32)%4)) };
			}
		}
		return std::nullopt;
	}

	/// True where the fix intentionally diverges from referenceHit: the old
	/// predicate used `>0` where it meant `>=0`, so the panel's top row and left
	/// column were drawn but not clickable.
	bool isOffByOneCell(int x, int y)
	{
		const bool inPanel = x >= 0 && x < BrushTool::WIDTH && y >= 0 && y < BrushTool::HEIGHT;
		return inPanel && (x == 0 || y == 0);
	}

	std::string describe(int x, int y)
	{
		return "at x=" + std::to_string(x) + " y=" + std::to_string(y);
	}
}

class BrushToolHitTest
{

public:
	/// Brute-force the new grid against the old one over the panel and far past
	/// every edge. Any divergence outside the off-by-one cells is a regression.
	void testOffByOneIsTheOnlyChange()
	{
		for (int y = -50; y < 200; ++y)
		{
			for (int x = -50; x < 200; ++x)
			{
				if (isOffByOneCell(x, y))
					continue;
				const auto now = BrushTool::hitTest(x, y);
				const auto before = referenceHit(x, y);
				CHECK_MESSAGE(now.has_value() == before.has_value(), (describe(x, y)));
				if (now)
				{
					CHECK_MESSAGE(now->kind == before->kind, (describe(x, y)));
					CHECK_MESSAGE(now->value == before->value, (describe(x, y)));
				}
			}
		}
	}

	/// The fix itself: the top row and left column are live now, and every one of
	/// them was dead before.
	void testOffByOneCellsNowHit()
	{
		for (int y = 0; y < BrushTool::HEIGHT; ++y)
		{
			for (int x = 0; x < BrushTool::WIDTH; ++x)
			{
				if (!isOffByOneCell(x, y))
					continue;
				CHECK_MESSAGE(BrushTool::hitTest(x, y).has_value(), (describe(x, y)));
				CHECK_MESSAGE(!referenceHit(x, y).has_value(), (describe(x, y)));
			}
		}
	}

	/// GameGUI's flag panel forwards clicks from the 40px zone-type strip above
	/// the tool, which arrive as y in [-39,-1]. They must select no button —
	/// handleClick still defaults the mode, which is what that path wants.
	void testZoneStripClicksMissThePanel()
	{
		for (int y = -39; y <= -1; ++y)
			for (int x = 0; x < BrushTool::WIDTH; ++x)
				CHECK_MESSAGE(!BrushTool::hitTest(x, y).has_value(), (describe(x, y)));
	}

	void testModeRow()
	{
		for (int y = 0; y < BrushTool::MODE_ROW_HEIGHT; ++y)
		{
			for (int x = 0; x < BrushTool::WIDTH; ++x)
			{
				const auto hit = BrushTool::hitTest(x, y);
				CHECK_MESSAGE(hit.has_value(), (describe(x, y)));
				CHECK_MESSAGE(hit->kind == BrushTool::Hit::ModeButton, (describe(x, y)));
				const unsigned expected = (x < BrushTool::MODE_BUTTON_WIDTH)
					? static_cast<unsigned>(BrushTool::MODE_ADD)
					: static_cast<unsigned>(BrushTool::MODE_DEL);
				CHECK_EQ(expected, hit->value);
			}
		}
	}

	/// Every figure is reachable, and each occupies exactly its own cell.
	void testFigureGrid()
	{
		for (unsigned figure = 0; figure < BrushTool::BRUSH_COUNT; ++figure)
		{
			const int column = static_cast<int>(figure) % BrushTool::FIGURE_COLUMNS;
			const int row = static_cast<int>(figure) / BrushTool::FIGURE_COLUMNS;
			for (int dy = 0; dy < BrushTool::FIGURE_CELL_SIZE; ++dy)
			{
				for (int dx = 0; dx < BrushTool::FIGURE_CELL_SIZE; ++dx)
				{
					const int x = column * BrushTool::FIGURE_CELL_SIZE + dx;
					const int y = BrushTool::MODE_ROW_HEIGHT + row * BrushTool::FIGURE_CELL_SIZE + dy;
					const auto hit = BrushTool::hitTest(x, y);
					CHECK_MESSAGE(hit.has_value(), (describe(x, y)));
					CHECK_MESSAGE(hit->kind == BrushTool::Hit::FigureButton, (describe(x, y)));
					CHECK_EQ(figure, hit->value);
				}
			}
		}
	}

	void testEdges()
	{
		// Corners of the panel are inside.
		CHECK(BrushTool::hitTest(0, 0).has_value());
		CHECK(BrushTool::hitTest(BrushTool::WIDTH - 1, 0).has_value());
		CHECK(BrushTool::hitTest(0, BrushTool::HEIGHT - 1).has_value());
		CHECK(BrushTool::hitTest(BrushTool::WIDTH - 1, BrushTool::HEIGHT - 1).has_value());

		// One pixel past each is outside.
		CHECK(!BrushTool::hitTest(-1, 0).has_value());
		CHECK(!BrushTool::hitTest(0, -1).has_value());
		CHECK(!BrushTool::hitTest(BrushTool::WIDTH, 0).has_value());
		CHECK(!BrushTool::hitTest(0, BrushTool::HEIGHT).has_value());

		// The mode row / figure grid seam.
		CHECK(BrushTool::hitTest(0, BrushTool::MODE_ROW_HEIGHT - 1)->kind == BrushTool::Hit::ModeButton);
		CHECK(BrushTool::hitTest(0, BrushTool::MODE_ROW_HEIGHT)->kind == BrushTool::Hit::FigureButton);

		// The last figure reaches the bottom-right corner. The map editor's
		// widget rect used to be 4px short of this, making it unreachable.
		const auto corner = BrushTool::hitTest(BrushTool::WIDTH - 1, BrushTool::HEIGHT - 1);
		CHECK_EQ(BrushTool::BRUSH_COUNT - 1, corner->value);
	}

	/// The literals the old code re-typed at each site.
	void testLayoutConstants()
	{
		CHECK_EQ(128, BrushTool::WIDTH);
		CHECK_EQ(100, BrushTool::HEIGHT);
		CHECK_EQ(64, BrushTool::MODE_BUTTON_WIDTH);
		CHECK_EQ(8u, BrushTool::BRUSH_COUNT);
	}
};

TEST_SUITE("BrushToolHit")
{
	TEST_CASE_FIXTURE(BrushToolHitTest, "OffByOneIsTheOnlyChange") { testOffByOneIsTheOnlyChange(); }
	TEST_CASE_FIXTURE(BrushToolHitTest, "OffByOneCellsNowHit") { testOffByOneCellsNowHit(); }
	TEST_CASE_FIXTURE(BrushToolHitTest, "ZoneStripClicksMissThePanel") { testZoneStripClicksMissThePanel(); }
	TEST_CASE_FIXTURE(BrushToolHitTest, "ModeRow") { testModeRow(); }
	TEST_CASE_FIXTURE(BrushToolHitTest, "FigureGrid") { testFigureGrid(); }
	TEST_CASE_FIXTURE(BrushToolHitTest, "Edges") { testEdges(); }
	TEST_CASE_FIXTURE(BrushToolHitTest, "LayoutConstants") { testLayoutConstants(); }
}
