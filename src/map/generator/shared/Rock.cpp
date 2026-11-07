// SPDX-License-Identifier: GPL-3.0-or-later
#include "Rock.h"
#include "GenerationWork.h"
#include "GenerationWorld.h"
#include "Morphology.h"
#include "ResourceSemantics.h"
#include <algorithm>
#include <cstdlib>
namespace MapGeneration
{
std::vector<unsigned char> rockVertices(const Torus &t, const std::vector<unsigned char> &rockTiles)
{
	std::vector<unsigned char> rock(rockTiles.size(), 0);
	for (int y = 0; y < t.h; ++y)
		for (int x = 0; x < t.w; ++x)
		{
			::MapGeneration::generationCheckpoint();
			rock.at(y * t.w + x) = rockTiles.at(t.at(x, y)) && rockTiles.at(t.at(x - 1, y)) &&
								   rockTiles.at(t.at(x, y - 1)) && rockTiles.at(t.at(x - 1, y - 1));
		}
	return rock;
}

std::vector<int> quarryNear(const Torus &t, const std::vector<unsigned char> &wall,
							const std::vector<unsigned char> &open, int centre, int radius, int depth)
{
	const std::vector<int> steps = stepsFrom(t, open);
	std::vector<int> tiles;
	const int cx = t.remainderX(centre), cy = centre / t.w;
	for (int dy = -radius; dy <= radius; ++dy)
		for (int dx = -radius; dx <= radius; ++dx)
		{
			::MapGeneration::generationCheckpoint();
			const int i = t.at(cx + dx, cy + dy);
			if (wall.at(i) && steps.at(i) >= 1 && steps.at(i) <= depth)
				tiles.push_back(i);
		}
	std::sort(tiles.begin(), tiles.end());
	return tiles;
}

std::vector<unsigned char> quarryApproach(const Torus &t, const std::vector<unsigned char> &quarry)
{
	std::vector<unsigned char> ring = dilate(t, quarry, 1);
	for (size_t i = 0; i < ring.size(); ++i)
		ring[i] = ring[i] && !quarry[i];
	return ring;
}

std::vector<unsigned char> talusApron(const Torus &t, const std::vector<unsigned char> &rock,
									  const std::vector<unsigned char> &eligible,
									  const std::vector<int> &fray, int inner, int outer, int cut)
{
	const std::vector<unsigned char> near = dilate(t, rock, inner), far = dilate(t, rock, std::max(inner, outer));
	std::vector<unsigned char> apron(rock.size(), 0);
	for (size_t i = 0; i < apron.size(); ++i)
	{
		::MapGeneration::generationCheckpoint();
		apron[i] = !rock[i] && eligible[i] && (near[i] || (far[i] && fray.at(i) > cut));
	}
	return apron;
}

std::vector<unsigned char> deepCore(const Torus &t, const std::vector<unsigned char> &water, int steps)
{
	std::vector<unsigned char> land(water.size(), 0);
	for (size_t i = 0; i < water.size(); ++i)
		land[i] = !water[i];
	const std::vector<int> fromLand = stepsFrom(t, land);
	std::vector<unsigned char> core(water.size(), 0);
	for (size_t i = 0; i < water.size(); ++i)
	{
		::MapGeneration::generationCheckpoint();
		core[i] = water[i] && fromLand[i] >= std::max(2, steps);
	}
	return core;
}

std::string quarryFault(const GenerationMap &map, const Torus &t, const std::vector<unsigned char> &quarry,
						const std::vector<int> &owner, int colonies, int least)
{
	std::vector<int> stone(std::max(0, colonies), 0);
	for (int i = 0; i < t.size(); ++i)
	{
		::MapGeneration::generationCheckpoint();
		if (!quarry.at(i))
			continue;
		const int x = t.remainderX(i), y = i / t.w;
		const bool held = permanentResourceBarrier(map, i);
		if (!held && map.terrainSupportsResourceAtByIndex(x, y, STONE))
			return "A quarry face has lost its stone at (" + std::to_string(x) + ", " + std::to_string(y) + ").";
		if (owner.at(i) >= 0 && owner.at(i) < colonies)
			stone[owner.at(i)] += held;
	}
	for (int k = 0; k < colonies; ++k)
		if (stone[k] != stone[0] || stone[k] < least)
			return "Colony " + std::to_string(k) + "'s quarry holds " + std::to_string(stone[k]) +
				   " stone, colony 0's " + std::to_string(stone[0]) + ".";
	return "";
}
} // namespace MapGeneration
