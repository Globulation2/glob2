// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <set>
#include <utility>
#include <vector>

// Map cells a brush stroke covers, using the same figure masks and
// checkerboard parity as the brush operations it previews. Presentation code
// uses it to show a stroke before release; it never reads or changes the map.
namespace BrushCoverage
{
using Cell = std::pair<int, int>; // Map column and row; the caller decides wrapping.
//! Cell under a world-pixel position; cornerOffset selects tile-corner brushes.
Cell cellAt(double worldX, double worldY, double cornerOffset = 0);
//! Cells covered by `figure` centred on each of `centres`. The first centre is
//! the stroke origin that aligns the checkerboard figures.
std::set<Cell> cells(unsigned figure, const std::vector<Cell> &centres);
//! Cells of one stamp of `figure` centred on `centre`, with `origin` the
//! stroke origin that aligns the checkerboard figures.
std::set<Cell> stamp(unsigned figure, Cell centre, Cell origin);
} // namespace BrushCoverage
