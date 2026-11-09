// SPDX-License-Identifier: GPL-3.0-or-later
#include "Climates.h"
#include "GenerationContext.h"
#include <cmath>
namespace MapGeneration
{
int drawClimate(GenerationContext &context, const char *stream)
{
	return int(context.bounded(stream, kClimateCount));
}

void paintClimate(TerrainSketch &terrain, const Torus &t, const ClimateLook &look,
				  const LookRoles &roles)
{
	const int n = t.w * t.h;
	for (int i = 0; i < n; ++i)
	{
		generationCheckpoint();
		const int x = t.remainderX(i), y = i / t.w;
		switch (terrain[i])
		{
		case WATER:
			if (roles.deep && roles.deep(i))
				terrain[i] = look.deep;
			break;
		case GRASS:
			terrain[i] = roles.farm && roles.farm(i)       ? look.farmGrass
						 : roles.tended && roles.tended(i) ? look.homeGrass
														   : look.openGrass;
			break;
		case SAND:
			terrain[i] = look.beach;
			break;
		case CLIFFS:
			terrain[i] = look.cliff;
			break;
		case RIDGE_ROCK:
			terrain[i] = roles.rockAccent && roles.rockAccent(i) ? look.rockAccent : look.rock;
			break;
		case LAVA:
			terrain[i] = ((x * 5 + y * 11) ^ (x * y)) % 3 == 0 ? MOLTEN_LAVA_POOL : LAVA;
			break;
		case MUD:
			terrain[i] = look.rough;
			break;
		case DIRT:
			terrain[i] = look.scrub;
			break;
		case LOAM:
			terrain[i] = look.fertile;
			break;
		case TRAIL:
			switch (roles.path ? roles.path(i) : PathLook::Road)
			{
			case PathLook::Town:
				terrain[i] = look.town;
				break;
			case PathLook::Street:
				terrain[i] = look.street;
				break;
			case PathLook::Deck:
				terrain[i] = look.deck;
				break;
			case PathLook::Bridge:
				terrain[i] = look.bridge;
				break;
			case PathLook::Road:
				terrain[i] = look.road;
				break;
			}
			break;
		default:
			break;
		}
	}
}

std::vector<unsigned char> deepSea(const TerrainSketch &terrain, const Torus &t,
								   const std::function<bool(int)> &sea, int from)
{
	const int n = t.w * t.h;
	std::vector<unsigned char> land(n, 0), deep(n, 0);
	for (int i = 0; i < n; ++i)
		land[i] = terrain[i] != WATER;
	const std::vector<int> fromLand = stepsFrom(t, land);
	for (int i = 0; i < n; ++i)
		deep[i] = terrain[i] == WATER && sea(i) && fromLand[i] >= from;
	return deep;
}

int barrenBase(TerrainSketch &terrain, const Torus &t, const std::function<bool(int)> &base,
			   const std::function<bool(int)> &keep, int verge)
{
	const int n = t.w * t.h;
	std::vector<unsigned char> wall(n, 0);
	for (int i = 0; i < n; ++i)
		wall[i] = terrain[i] == CLIFFS || terrain[i] == RIDGE_ROCK;
	const std::vector<int> fromWall = stepsFrom(t, wall);
	int changed = 0;
	for (int i = 0; i < n; ++i)
		if (terrain[i] == GRASS && base(i) && !(keep && keep(i)) &&
			(fromWall[i] < 0 || fromWall[i] > verge))
		{
			terrain[i] = DIRT;
			++changed;
		}
	return changed;
}

void paveCore(TerrainSketch &terrain, const Torus &t, double cx, double cy, double radius,
			  const std::function<bool(int)> &base)
{
	const int r = int(std::ceil(radius));
	for (int dy = -r; dy <= r; ++dy)
		for (int dx = -r; dx <= r; ++dx)
		{
			const int x = int(std::lround(cx)) + dx, y = int(std::lround(cy)) + dy;
			const int i = t.at(x, y);
			if (dx * dx + dy * dy <= radius * radius && base(i) &&
				(terrain[i] == GRASS || terrain[i] == DIRT))
				terrain[i] = TRAIL;
		}
}
} // namespace MapGeneration
