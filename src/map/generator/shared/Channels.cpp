// SPDX-License-Identifier: GPL-3.0-or-later
#include "Channels.h"
#include "Map.h"
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
		beach[i] = !grass[i] && !water[i];
	return beach;
}

std::vector<unsigned char> beachTiles(const Map &map, const Torus &t)
{
	std::vector<unsigned char> beach(size_t(t.size()), 0);
	for (int y = 0; y < t.h; ++y)
		for (int x = 0; x < t.w; ++x)
			beach[size_t(y) * t.w + x] = !map.isGrass(x, y) && !map.isWater(x, y);
	return beach;
}

int bridgeAcross(TerrainSketch &sketch, const Torus &t, ShapePoint from, ShapePoint to,
				 double halfWidth)
{
	std::vector<unsigned char> deck(sketch.size(), 0);
	strokePath(deck, t, {{from.x, from.y, halfWidth}, {to.x, to.y, halfWidth}});
	int laid = 0;
	for (size_t i = 0; i < sketch.size(); ++i)
		if (deck[i] && sketch[i] == WATER)
		{
			sketch[i] = SAND;
			++laid;
		}
	return laid;
}

std::vector<int> crossingsPerLabel(const Torus &t, const std::vector<unsigned char> &bridges,
								   const std::vector<int> &labelled, int labels)
{
	const std::vector<int> piece = connectedRegions(bridges, t.w, t.h, true, GridNeighbors::Eight);
	int pieces = 0;
	for (int p : piece)
		pieces = std::max(pieces, p + 1);
	std::vector<std::vector<unsigned char>> touches(size_t(std::max(0, labels)),
													std::vector<unsigned char>(size_t(pieces), 0));
	for (int y = 0; y < t.h; ++y)
		for (int x = 0; x < t.w; ++x)
		{
			const int p = piece[size_t(y) * t.w + x];
			if (p < 0)
				continue;
			for (int dy = -1; dy <= 1; ++dy)
				for (int dx = -1; dx <= 1; ++dx)
				{
					const int label = labelled[t.at(x + dx, y + dy)];
					if (label >= 0 && label < labels)
						touches[label][p] = 1;
				}
		}
	std::vector<int> count(size_t(std::max(0, labels)), 0);
	for (int label = 0; label < labels; ++label)
		count[label] = int(std::count(touches[label].begin(), touches[label].end(), 1));
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
	const int reach = int(std::ceil(f.span + f.halfWidth)) + 1;
	const int cx = int(std::floor(f.x)), cy = int(std::floor(f.y));
	for (int dy = -reach; dy <= reach; ++dy)
		for (int dx = -reach; dx <= reach; ++dx)
		{
			const double ox = cx + dx - f.x, oy = cy + dy - f.y;
			if (std::abs(ox * f.alongX + oy * f.alongY) > f.halfWidth ||
				std::abs(ox * f.acrossX + oy * f.acrossY) > f.span)
				continue;
			unsigned char &corner = terrain[size_t(t.at(cx + dx, cy + dy))];
			if (corner == WATER)
				corner = SAND;
		}
}

SandFord fordAlong(const Torus &t, const std::vector<ShapePoint> &centreline,
				   const std::vector<double> &radius, int index, bool closed, double halfWidth,
				   double reach)
{
	const ShapePoint tangent = ChannelDetail::tangentAt(t, centreline, index, closed);
	SandFord f;
	f.x = centreline[size_t(index)].x;
	f.y = centreline[size_t(index)].y;
	f.alongX = tangent.x;
	f.alongY = tangent.y;
	f.acrossX = -tangent.y;
	f.acrossY = tangent.x;
	f.span = radius[size_t(index)] + reach;
	f.halfWidth = halfWidth;
	return f;
}

bool fordLandingWalkable(const Map &map, const Torus &t, const SandFord &f, int side)
{
	const double s = side * (f.span + 1.0);
	const int at = ChannelDetail::tileOf(t, f.x + f.acrossX * s, f.y + f.acrossY * s);
	for (int dy = -1; dy <= 1; ++dy)
		for (int dx = -1; dx <= 1; ++dx)
		{
			const int x = t.x(at % t.w + dx), y = t.y(at / t.w + dy);
			if (!map.isWater(x, y) && !map.isResource(x, y) && map.getBuilding(x, y) == NOGBID)
				return true;
		}
	return false;
}
} // namespace MapGeneration
