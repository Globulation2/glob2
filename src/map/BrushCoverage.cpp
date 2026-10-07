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
std::set<Cell> stamp(unsigned figure, Cell centre, Cell origin)
{
	std::set<Cell> covered;
	const auto [centerX, centerY] = centre;
	const int width = BrushTool::getBrushWidth(figure), height = BrushTool::getBrushHeight(figure);
	const int left = centerX - BrushTool::getBrushDimXMinus(figure),
			  top = centerY - BrushTool::getBrushDimYMinus(figure);
	for (int y = 0; y < height; ++y)
		for (int x = 0; x < width; ++x)
			if (BrushTool::getBrushValue(figure, x, y, centerX, centerY, origin.first, origin.second))
				covered.insert({left + x, top + y});
	return covered;
}
std::set<Cell> cells(unsigned figure, const std::vector<Cell> &centres)
{
	std::set<Cell> covered;
	if (centres.empty())
		return covered;
	for (const auto &centre : centres)
		covered.merge(stamp(figure, centre, centres.front()));
	return covered;
}
std::set<Cell> cornerClosure(const std::set<Cell> &covered)
{
	std::set<Cell> closed = covered, corners;
	for (const auto &[x, y] : covered)
		for (int dy = 0; dy <= 1; ++dy)
			for (int dx = 0; dx <= 1; ++dx)
				corners.insert({x + dx, y + dy});
	// A cell (x, y) reads the corners (x..x+1, y..y+1).
	for (const auto &[x, y] : corners)
		if (corners.count({x + 1, y}) && corners.count({x, y + 1}) && corners.count({x + 1, y + 1}))
			closed.insert({x, y});
	return closed;
}
} // namespace BrushCoverage
