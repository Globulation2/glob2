// SPDX-License-Identifier: GPL-3.0-or-later
#include "Climates.h"
#include "GenerationContext.h"
#include "Grid.h"
#include <algorithm>
#include <cmath>
namespace MapGeneration
{
namespace
{
// A lava body's rings, counted in vertices in from its edge: embers on the outermost, then lava,
// and a round molten core about the deepest point, its radius this share of the body's depth, on a
// body at least kMoltenDepth deep (a thin moat or flow is embers and lava only).
constexpr int kEmberRing = 2;
constexpr int kMoltenDepth = 5;
constexpr double kMoltenShare = 0.35;

// The look of every LAVA vertex, in concentric rings like Lava shield's lake: ember field at the
// edge, lava, and molten lava in a small circle at the middle. All three share the Lava group's
// rules, so the rings change nothing but the picture.
std::vector<TerrainType> lavaRings(const TerrainSketch &terrain, const Torus &t)
{
	const int n = t.w * t.h;
	std::vector<unsigned char> edge(n, 0);
	bool any = false;
	for (int i = 0; i < n; ++i)
	{
		edge[i] = terrain[i] != LAVA;
		any = any || terrain[i] == LAVA;
	}
	std::vector<TerrainType> look(n, LAVA);
	if (!any)
		return look;
	const std::vector<int> depth = stepsFrom(t, edge);
	// Each body (8-connected), its deepest vertices' middle, by offsets from the first one found so
	// a body across the wrap is not split.
	std::vector<int> body(n, -1), stack;
	for (int start = 0; start < n; ++start)
	{
		generationCheckpoint();
		if (terrain[start] != LAVA || body[start] >= 0)
			continue;
		std::vector<int> members;
		body[start] = start;
		stack.assign(1, start);
		int deepest = 0;
		while (!stack.empty())
		{
			const int i = stack.back();
			stack.pop_back();
			members.push_back(i);
			deepest = std::max(deepest, depth[i]);
			const int x = t.remainderX(i), y = i / t.w;
			for (int dy = -1; dy <= 1; ++dy)
				for (int dx = -1; dx <= 1; ++dx)
				{
					const int j = t.at(x + dx, y + dy);
					if (terrain[j] == LAVA && body[j] < 0)
					{
						body[j] = start;
						stack.push_back(j);
					}
				}
		}
		const int sx = t.remainderX(start), sy = start / t.w;
		double cx = 0, cy = 0;
		int count = 0;
		for (const int i : members)
			if (depth[i] == deepest)
			{
				cx += t.offsetX(sx, t.remainderX(i));
				cy += t.offsetY(sy, i / t.w);
				++count;
			}
		cx /= count;
		cy /= count;
		const double core = kMoltenShare * deepest;
		for (const int i : members)
		{
			if (depth[i] <= kEmberRing)
				look[i] = EMBER_FIELD;
			else if (deepest >= kMoltenDepth &&
					 std::hypot(t.offsetX(sx, t.remainderX(i)) - cx, t.offsetY(sy, i / t.w) - cy) <= core)
				look[i] = MOLTEN_LAVA_POOL;
		}
	}
	return look;
}
} // namespace

int drawClimate(GenerationContext &context, const char *stream)
{
	return int(context.bounded(stream, kClimateCount));
}

void paintClimate(TerrainSketch &terrain, const Torus &t, const ClimateLook &look,
				  const LookRoles &roles)
{
	const int n = t.w * t.h;
	const std::vector<TerrainType> lava = lavaRings(terrain, t);
	for (int i = 0; i < n; ++i)
	{
		generationCheckpoint();
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
			terrain[i] = lava[i];
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
