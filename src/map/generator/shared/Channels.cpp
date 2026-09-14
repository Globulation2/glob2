// SPDX-License-Identifier: GPL-3.0-or-later
#include "Channels.h"
#include "Map.h"
#include "TerrainType.h"
#include "Topology.h"
#include "Walls.h"
#include <algorithm>
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
} // namespace MapGeneration
