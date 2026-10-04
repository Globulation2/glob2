// SPDX-License-Identifier: GPL-3.0-or-later
#include "BrushCoverage.h"
#include "Brush.h"
#include <cmath>

namespace BrushCoverage
{
Cell cellAt(double worldX, double worldY, double cornerOffset)
{
	return {int(std::floor((worldX + cornerOffset) / 32)), int(std::floor((worldY + cornerOffset) / 32))};
}
std::set<Cell> cells(unsigned figure, const std::vector<Cell> &centres)
{
	std::set<Cell> covered;
	if (centres.empty())
		return covered;
	const auto [originX, originY] = centres.front();
	const int width = BrushTool::getBrushWidth(figure), height = BrushTool::getBrushHeight(figure);
	for (const auto &[centerX, centerY] : centres)
	{
		const int left = centerX - BrushTool::getBrushDimXMinus(figure),
				  top = centerY - BrushTool::getBrushDimYMinus(figure);
		for (int y = 0; y < height; ++y)
			for (int x = 0; x < width; ++x)
				if (BrushTool::getBrushValue(figure, x, y, centerX, centerY, originX, originY))
					covered.insert({left + x, top + y});
	}
	return covered;
}
} // namespace BrushCoverage
