#include "GenerationWork.h"
#include "GenerationNumeric.h"
// SPDX-License-Identifier: GPL-3.0-or-later
#include "Channels.h"
#include "Map.h"
#include "Morphology.h"
#include "TerrainType.h"
#include "Topology.h"
#include "Walls.h"
#include <algorithm>
#include <cmath>
namespace MapGeneration
{
int widestChannelTowersCross(int level, int depth)
{
	const int range = kTowerRange[std::clamp(level, 1, 3) - 1];
	return std::max(0, range - bankToBank(0) - (std::max(1, depth) - 1));
}

int narrowestChannelTowersMiss(int level)
{
	return widestChannelTowersCross(level, 1) + 1;
}

std::vector<unsigned char> beachTiles(const TerrainSketch &sketch, const Torus &t)
{
	const std::vector<unsigned char> grass = pureTiles(sketch, t, GRASS);
	const std::vector<unsigned char> water = pureTiles(sketch, t, WATER);
	std::vector<unsigned char> beach(sketch.size(), 0);
	for (size_t i = 0; i < beach.size(); ++i)
	{
		::MapGeneration::generationCheckpoint();
		beach.at(i) = !grass.at(i) && !water.at(i);
	}
	return beach;
}

std::vector<unsigned char> beachTiles(const Map &map, const Torus &t)
{
	std::vector<unsigned char> beach(size_t(t.size()), 0);
	for (int y = 0; y < t.h; ++y)
	{
		::MapGeneration::generationCheckpoint();
		for (int x = 0; x < t.w; ++x)
		{
			::MapGeneration::generationCheckpoint();
			beach.at(size_t(y) * t.w + x) = map.terrainPropertiesAt(x, y).shoreline;
		}
	}
	return beach;
}

int bridgeAcross(TerrainSketch &sketch, const Torus &t, ShapePoint from, ShapePoint to,
				 double halfWidth)
{
	std::vector<unsigned char> deck(sketch.size(), 0);
	strokePath(deck, t, {{from.x, from.y, halfWidth}, {to.x, to.y, halfWidth}});
	int laid = 0;
	for (size_t i = 0; i < sketch.size(); ++i)
	{
		::MapGeneration::generationCheckpoint();
		if (deck.at(i) && sketch.at(i) == WATER)
		{
			sketch.at(i) = SAND;
			++laid;
		}
	}
	return laid;
}

std::vector<unsigned char> straitsBetweenCells(const Torus &t, const std::vector<int> &labels,
											   int corners)
{
	const int width = std::max(4, corners);
	std::vector<unsigned char> seam(size_t(t.size()), 0);
	if (width % 2 == 1)
		seam = labelBorders(t, labels);
	else
		// Both sides of every border: a tile is seam when any of its eight neighbours lies in another
		// cell, so the seam is two tiles thick along a straight border.
		for (int y = 0; y < t.h; ++y)
		{
			::MapGeneration::generationCheckpoint();
			for (int x = 0; x < t.w; ++x)
			{
				::MapGeneration::generationCheckpoint();
				const int i = y * t.w + x;
				if (labels.at(i) < 0)
					continue;
				for (int dy = -1; dy <= 1 && !seam.at(i); ++dy)
				{
					::MapGeneration::generationCheckpoint();
					for (int dx = -1; dx <= 1 && !seam.at(i); ++dx)
					{
						::MapGeneration::generationCheckpoint();
						seam.at(i) = labels.at(t.at(x + dx, y + dy)) != labels.at(i);
					}
				}
			}
		}
	std::vector<unsigned char> water =
		dilate(t, seam, width % 2 == 1 ? (width - 1) / 2 : (width - 2) / 2);
	for (int i = 0; i < t.size(); ++i)
	{
		::MapGeneration::generationCheckpoint();
		if (labels.at(i) < 0)
			water.at(i) = 1;
	}
	return water;
}

std::vector<int> crossingsPerLabel(const Torus &t, const std::vector<unsigned char> &bridges,
								   const std::vector<int> &labelled, int labels)
{
	const std::vector<int> piece = connectedRegions(bridges, t.w, t.h, true, GridNeighbors::Eight);
	int pieces = 0;
	for (int p : piece)
	{
		::MapGeneration::generationCheckpoint();
		pieces = std::max(pieces, p + 1);
	}
	generationAllocation(std::uint64_t(std::max(0, labels)) *
						 (sizeof(std::vector<unsigned char>) + std::uint64_t(pieces)));
	std::vector<std::vector<unsigned char>> touches(size_t(std::max(0, labels)),
													std::vector<unsigned char>(size_t(pieces), 0));
	for (int y = 0; y < t.h; ++y)
	{
		::MapGeneration::generationCheckpoint();
		for (int x = 0; x < t.w; ++x)
		{
			::MapGeneration::generationCheckpoint();
			const int p = piece.at(size_t(y) * t.w + x);
			if (p < 0)
				continue;
			for (int dy = -1; dy <= 1; ++dy)
			{
				::MapGeneration::generationCheckpoint();
				for (int dx = -1; dx <= 1; ++dx)
				{
					::MapGeneration::generationCheckpoint();
					const int label = labelled.at(t.at(x + dx, y + dy));
					if (label >= 0 && label < labels)
						touches.at(label).at(p) = 1;
				}
			}
		}
	}
	std::vector<int> count(size_t(std::max(0, labels)), 0);
	for (int label = 0; label < labels; ++label)
	{
		::MapGeneration::generationCheckpoint();
		count.at(label) = int(std::count(touches.at(label).begin(), touches.at(label).end(), 1));
	}
	return count;
}

void SandFord::offsets(const Torus &t, double px, double py, double &along, double &across) const
{
	const double dx = ChannelDetail::centred(px - x, t.w), dy = ChannelDetail::centred(py - y, t.h);
	along = dx * alongX + dy * alongY;
	across = dx * acrossX + dy * acrossY;
}

bool SandFord::covers(const Torus &t, double px, double py, double alongMargin,
					  double acrossMargin) const
{
	double along = 0, across = 0;
	offsets(t, px, py, along, across);
	return std::abs(along) <= halfWidth + alongMargin && std::abs(across) <= span + acrossMargin;
}

void stampFord(TerrainSketch &terrain, const Torus &t, const SandFord &f)
{
	// Corners are tested by their offset from the ford's centre, the ford's own frame: everything
	// within its half width along and its span across that is water becomes sand.
	const int reach = int(::MapGeneration::Numeric::ceil(f.span + f.halfWidth)) + 1;
	const int cx = int(::MapGeneration::Numeric::floor(f.x)),
			  cy = int(::MapGeneration::Numeric::floor(f.y));
	for (int dy = -reach; dy <= reach; ++dy)
	{
		::MapGeneration::generationCheckpoint();
		for (int dx = -reach; dx <= reach; ++dx)
		{
			::MapGeneration::generationCheckpoint();
			const double ox = cx + dx - f.x, oy = cy + dy - f.y;
			if (std::abs(ox * f.alongX + oy * f.alongY) > f.halfWidth ||
				std::abs(ox * f.acrossX + oy * f.acrossY) > f.span)
				continue;
			unsigned char &corner = terrain.at(size_t(t.at(cx + dx, cy + dy)));
			if (corner == WATER)
				corner = SAND;
		}
	}
}

SandFord fordAlong(const Torus &t, const std::vector<ShapePoint> &centreline,
				   const std::vector<double> &radius, int index, bool closed, double halfWidth,
				   double reach)
{
	const ShapePoint tangent = ChannelDetail::tangentAt(t, centreline, index, closed);
	SandFord f;
	f.x = centreline.at(size_t(index)).x;
	f.y = centreline.at(size_t(index)).y;
	f.alongX = tangent.x;
	f.alongY = tangent.y;
	f.acrossX = -tangent.y;
	f.acrossY = tangent.x;
	f.span = radius.at(size_t(index)) + reach;
	f.halfWidth = halfWidth;
	return f;
}

std::string fordWalkabilityFault(const Map &map, const Torus &t, const SandFord &f)
{
	const auto walkable = [&](double x, double y)
	{
		return map
			.terrainPropertiesAt(t.at(int(::MapGeneration::Numeric::floor(x)),
									  int(::MapGeneration::Numeric::floor(y))))
			.walkable;
	};
	for (int a = -1; a <= 1; ++a)
	{
		::MapGeneration::generationCheckpoint();
		for (double s = -f.span; s <= f.span + 1e-9; s += 0.5)
		{
			::MapGeneration::generationCheckpoint();
			if (!walkable(f.x + f.alongX * a + f.acrossX * s, f.y + f.alongY * a + f.acrossY * s))
				return "A ford" + ChannelDetail::where(t, f.x, f.y) + " is not walkable.";
		}
	}
	for (int side : {-1, 1})
	{
		::MapGeneration::generationCheckpoint();
		const double s = side * (f.span + 1.0);
		if (!walkable(f.x + f.acrossX * s, f.y + f.acrossY * s))
			return "A ford" + ChannelDetail::where(t, f.x, f.y) + " has no walkable bank.";
	}
	return "";
}

bool fordLandingWalkable(const Map &map, const Torus &t, const SandFord &f, int side)
{
	const double s = side * (f.span + 1.0);
	const int at = ChannelDetail::tileOf(t, f.x + f.acrossX * s, f.y + f.acrossY * s);
	for (int dy = -1; dy <= 1; ++dy)
	{
		::MapGeneration::generationCheckpoint();
		for (int dx = -1; dx <= 1; ++dx)
		{
			::MapGeneration::generationCheckpoint();
			const int x = t.x(t.remainderX(at) + dx), y = t.y(at / t.w + dy);
			if (map.terrainPropertiesAt(x, y).walkable && !map.isResource(x, y) &&
				map.getBuilding(x, y) == NOGBID)
				return true;
		}
	}
	return false;
}
} // namespace MapGeneration
