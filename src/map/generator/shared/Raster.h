// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "Grid.h"
#include <cstdint>
#include <vector>
namespace MapGeneration
{
// A picture laid onto the torus: a source raster (a continent from the world atlas, a hand-drawn
// logo, a photograph's brightness) fitted into the map and resampled tile by tile. Everything is
// integer arithmetic, so the same picture lands on the same tiles on every platform.
//
// The fit keeps the picture's aspect ratio and centres it inside the map less a margin on every
// side; the margin is sea round a continent, so the torus's seam is open water and the picture
// never touches itself across the wrap. A picture taller than it is wide may be turned a quarter
// turn to fit a wide map larger. The picture is never stretched: a 2:1 map holds a square picture
// at the map's shorter side, and the rest is the caller's to fill.

struct RasterFit
{
	int sourceWidth = 0, sourceHeight = 0;
	/// The picture's footprint on the map: its top-left tile and size in tiles.
	int left = 0, top = 0, width = 0, height = 0;
	/// Turned a quarter turn clockwise: the picture's rows run down the map's columns.
	bool turned = false;
	/// Source cells per map tile as the fraction den / num (num map tiles per den source cells).
	std::int64_t num = 1, den = 1;

	/// The source cells map tile (x, y) covers: [x0, x1) by [y0, y1) in source cells, empty when the
	/// tile lies outside the footprint. A tile always covers at least one cell, so a picture smaller
	/// than its footprint is enlarged rather than left with holes.
	struct Cells
	{
		int x0 = 0, x1 = 0, y0 = 0, y1 = 0;
		bool empty() const { return x0 >= x1 || y0 >= y1; }
	};
	Cells cellsOf(int x, int y) const;
	/// Whether tile (x, y) lies in the footprint.
	bool covers(int x, int y) const
	{
		return x >= left && x < left + width && y >= top && y < top + height;
	}
};

/// The largest fit of a sourceWidth x sourceHeight picture inside a mapWidth x mapHeight map that
/// keeps `margin` tiles clear on every side, centred; turned when `mayTurn` and turning makes it
/// larger. A map too small for any footprint gives a 0 x 0 fit (test `width`).
RasterFit fitRaster(int sourceWidth, int sourceHeight, int mapWidth, int mapHeight, int margin,
					bool mayTurn);

/// The picture resampled onto the map, one value per tile: `reduce(cells)` turns the source cells a
/// tile covers (row-major within the tile's source rectangle) into that tile's value, and tiles
/// outside the footprint get `outside`.
template <typename Reduce>
std::vector<unsigned char> resampleRaster(const std::vector<unsigned char> &source,
										  const RasterFit &fit, const Torus &t,
										  unsigned char outside, Reduce reduce)
{
	std::vector<unsigned char> result(size_t(t.size()), outside);
	std::vector<unsigned char> cells;
	for (int y = fit.top; y < fit.top + fit.height; ++y)
		for (int x = fit.left; x < fit.left + fit.width; ++x)
		{
			const RasterFit::Cells box = fit.cellsOf(x, y);
			if (box.empty())
				continue;
			cells.clear();
			for (int sy = box.y0; sy < box.y1; ++sy)
				for (int sx = box.x0; sx < box.x1; ++sx)
					cells.push_back(source[size_t(sy) * fit.sourceWidth + sx]);
			result[t.at(x, y)] = reduce(cells);
		}
	return result;
}

/// Each tile's most common value among its source cells' low bits (`mask`), the lowest value on a
/// tie: a coast tile with more sea than land cells is sea, so shrinking a picture never fattens it.
std::vector<unsigned char> resampleMajority(const std::vector<unsigned char> &source,
											const RasterFit &fit, const Torus &t,
											unsigned char outside, unsigned char mask);

/// Whether any of a tile's source cells has `flag` set: a river a cell wide survives shrinking as a
/// line of tiles, where a majority would lose it.
std::vector<unsigned char> resampleAny(const std::vector<unsigned char> &source,
									   const RasterFit &fit, const Torus &t, unsigned char flag);
} // namespace MapGeneration
