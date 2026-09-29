// SPDX-License-Identifier: GPL-3.0-or-later
#include "Raster.h"
#include <algorithm>
namespace MapGeneration
{
RasterFit fitRaster(int sourceWidth, int sourceHeight, int mapWidth, int mapHeight, int margin,
					bool mayTurn)
{
	RasterFit fit;
	fit.sourceWidth = sourceWidth;
	fit.sourceHeight = sourceHeight;
	const int availableW = mapWidth - 2 * margin, availableH = mapHeight - 2 * margin;
	if (sourceWidth <= 0 || sourceHeight <= 0 || availableW <= 0 || availableH <= 0)
		return fit;
	// The scale is the tighter of the two axes' ratios, kept as a fraction: num map tiles per den
	// source cells. Comparing the two ratios by cross-multiplication avoids a division whose
	// rounding could differ between platforms.
	const auto scaleFor = [&](int sw, int sh, std::int64_t &num, std::int64_t &den)
	{
		if (std::int64_t(availableW) * sh <= std::int64_t(availableH) * sw)
		{
			num = availableW;
			den = sw;
		}
		else
		{
			num = availableH;
			den = sh;
		}
	};
	std::int64_t num, den;
	scaleFor(sourceWidth, sourceHeight, num, den);
	fit.turned = false;
	if (mayTurn)
	{
		std::int64_t turnedNum, turnedDen;
		scaleFor(sourceHeight, sourceWidth, turnedNum, turnedDen);
		// Turned is better when its scale is strictly larger; upright wins a tie so a square
		// picture is never turned for nothing.
		if (turnedNum * den > num * turnedDen)
		{
			num = turnedNum;
			den = turnedDen;
			fit.turned = true;
		}
	}
	fit.num = num;
	fit.den = den;
	const int pictureW = fit.turned ? sourceHeight : sourceWidth;
	const int pictureH = fit.turned ? sourceWidth : sourceHeight;
	fit.width = int(std::max<std::int64_t>(1, pictureW * num / den));
	fit.height = int(std::max<std::int64_t>(1, pictureH * num / den));
	fit.left = (mapWidth - fit.width) / 2;
	fit.top = (mapHeight - fit.height) / 2;
	return fit;
}

RasterFit::Cells RasterFit::cellsOf(int x, int y) const
{
	Cells cells;
	if (!covers(x, y))
		return cells;
	// The tile's span in picture cells: [u * den / num, (u + 1) * den / num), at least one cell.
	const auto span = [&](int u, int limit, int &c0, int &c1)
	{
		c0 = int(std::int64_t(u) * den / num);
		c1 = int(std::int64_t(u + 1) * den / num);
		c0 = std::min(c0, limit - 1);
		c1 = std::max(c1, c0 + 1);
		c1 = std::min(c1, limit);
	};
	const int u = x - left, v = y - top;
	if (!turned)
	{
		span(u, sourceWidth, cells.x0, cells.x1);
		span(v, sourceHeight, cells.y0, cells.y1);
		return cells;
	}
	// A quarter turn clockwise: the picture's last row becomes the footprint's first column, and
	// its columns run down the footprint. Map column u reads source rows from the bottom up; map
	// row v reads source columns left to right.
	int r0, r1;
	span(u, sourceHeight, r0, r1);
	cells.y0 = sourceHeight - r1;
	cells.y1 = sourceHeight - r0;
	span(v, sourceWidth, cells.x0, cells.x1);
	return cells;
}

std::vector<unsigned char> resampleMajority(const std::vector<unsigned char> &source,
											const RasterFit &fit, const Torus &t,
											unsigned char outside, unsigned char mask)
{
	int counts[256];
	return resampleRaster(source, fit, t, outside,
						  [&](const std::vector<unsigned char> &cells)
						  {
							  std::fill(std::begin(counts), std::end(counts), 0);
							  for (unsigned char c : cells)
								  ++counts[c & mask];
							  int best = 0;
							  for (int v = 1; v < 256; ++v)
								  if (counts[v] > counts[best])
									  best = v;
							  return (unsigned char)best;
						  });
}

std::vector<unsigned char> resampleAny(const std::vector<unsigned char> &source,
									   const RasterFit &fit, const Torus &t, unsigned char flag)
{
	return resampleRaster(source, fit, t, 0,
						  [&](const std::vector<unsigned char> &cells)
						  {
							  for (unsigned char c : cells)
								  if (c & flag)
									  return (unsigned char)1;
							  return (unsigned char)0;
						  });
}
} // namespace MapGeneration
