// SPDX-License-Identifier: GPL-3.0-or-later
#include "SwampGenerator.h"
#include "GenerationWorld.h"
#include "GenerationContext.h"
#include "GenerationWork.h"
#include "HeightFieldLooks.h"
#include "LatticeNoise.h"
#include "Morphology.h"
#include "Pipeline.h"
#include "Planting.h"
#include "Rock.h"
#include "Sketch.h"
#include "StartingPositions.h"
#include "Terrain.h"
#include "TerrainCatalog.h"
#include "Topology.h"
#include "Unit.h"
#include "TerrainGroupTable.h"
#include <algorithm>
#include <climits>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>
using namespace MapGeneration;
// Swamp (id 1): Leo Wandersleb's swamp from January 2006, on the shared height-field pipeline (see
// Terrain.cpp). The field is almost pure noise (makeSwamp), so the water is scattered ponds and
// sloughs everywhere rather than one shape.
//
// Swamp has no sand or desert weight: water is water / (1 + water + grass) of the map, 36% at the
// defaults, and the rest is grass (the 1 only guards against both weights at 0). Its only sand is
// the beaches layBeaches has to lay, because grass may not touch water, so every pond is ringed
// in sand. With water everywhere, wheat and wood can regrow almost anywhere, and a swamp's economy
// is rich; the cost is that movement and building room are broken up by ponds a colony must walk
// round until it can swim. Smoothing defaults to 6 (features about 30 tiles) so ponds are big
// enough to read, and grass between them wide enough to build on.
//
// SWAMP 2.0 (revision 6): the same pond-riddled lowland, drawn with the engine's full terrain
// palette. The design brief: a photograph of a lowland swamp or bog shows (1) still ponds, black or
// deep blue in the middle with a pale shallow rim; (2) mud on the far shores where nothing has
// trodden it into a beach; (3) carpets of moss in the hollows, the low ground the ponds flood; (4)
// tree islands, thickets on the hummocks, the highest ground between the ponds; and (5) here and
// there an old boardwalk across a neck of water where the walk round is long. What it must not
// become is Everglades: that map is a designed lattice of pools with ring clearings and levees and
// its moss covers the wet reaches wholesale; this one keeps Leo's noise field, and every new layer
// follows that field's own relief (hollows, shores, hummocks). So it is a post-pass, not a redesign:
// - Revision 5 runs unchanged first: the height field, ponds, beaches, resource bands, starts, the
//   starting-crop top-up, groves and the opening repairs are identical for the same seed (every new
//   draw uses streams of its own, and the field is copied out of the builder, not drawn again). The
//   pass then repaints only ground nobody stands on, keeping every deposit legal, so nothing that
//   ran before it has to know about the new terrain.
// - Two sketches. The pass keeps a mechanic sketch of canonical types (WATER, DEEP_WATER, SAND,
//   GRASS, MUDFLAT, MOSS, THICKET, BOARDWALK) that every check reads, and paints looks over it with
//   composeLooks (Sketch.h), so looks never change a rule.
// - Deep pond cores (`deep-water`): water vertices kDeepReach tiles from land in a straight line, so
//   a core's outline is round, opened and with specks dropped, never at an algae tile's corner, so only the bigger ponds have
//   a core and every shore keeps two tiles of plain shallows for algae, beaches and swimming access.
//   Deep water waters land like water but is slower to swim: crossing a big pond takes longer than
//   in revision 5 (a feel change).
// - Mud (`mudflats`): that share of the beach sand of the far shores, chosen by smooth noise so it
//   lies in long stretches, is mud (Rough: slow, unbuildable, holds nothing, and unlike sand it does
//   not inhibit growth). Never within kMudHome tiles of a swarm, so a home's own shores, the
//   footpaths and building ring its first workers use every trip, stay firm sand (slow mud there is
//   a real cost to any player), and never within kMudAlgae tiles of algae, whose regrowth wants
//   sand on its shore.
// - Moss (`moss`, off by default): grass in the hollows, the lowest kMossLow of the land by the field, where a
//   patch field passes, is moss (Fertile: buildable crop land that waters its neighbours at
//   48/256), so the low ground round some ponds farms a little better. Never within kMossHome tiles
//   of a swarm, which keeps the home garden plain (the edge between grass and moss is not
//   buildable). Moss under a deposit keeps it legal (Fertile holds what grass holds). The map is
//   not shaped round AI quirks: Maxima and Nicowar reading Fertile ground as water is an AI bug
//   being fixed in the AIs. Off by default because the growth tool scores every grass/moss edge
//   tile as no farmland and so loses about a tenth of the map's food to moss (6% within 24 steps
//   of a home).
// - Thicket hammocks (`hammocks`): rough knots of thicket (Obstacles: impassable, block arrows) on
//   the hummocks, the highest land by the field, drawn as tiles at least two thick (rockVertices),
//   on vacant grass kHammockShore steps from water and kHammockHome tiles from every swarm. A
//   hammock whose blocked tiles would part the walkable ground round it is dropped, and the
//   validator checks the same on the finished map, so they break up open grass but never cut any
//   ground off.
// - Boardwalk causeways (`causeways`): for each colony, the straight crossing of at most kCauseSpan
//   water tiles from ground within kCauseNear steps of its swarm that saves the longest walk round,
//   if it saves at least kCauseSave steps, is boardwalk (Paths: twice as fast, buildable, holds
//   nothing). Both ends must already be walkable from the swarm: a causeway shortens a detour, it
//   never joins ground that was apart. Rare by construction; each one's saving is in telemetry.
// - A climate, looks only (Temperate bog, Tropical mangrove): one look per rule group, map-wide, for
//   the grass, the sand, the deep water, mud, moss and thicket. Revision 7 dropped the second beach
//   sand, the lush/dry grass bands by relief and the plain-grass gardens round the swarms: the pass
//   runs after placeStarts, the starting-crop top-up and openStartsBuriedByResources, and paints
//   looks over a mechanic sketch, so a home's grass can take the climate's look like the rest.
namespace
{
// Streams of their own, so every draw revision 5 made is the draw it made.
constexpr const char *kHammockStream = "swamp-hammocks";
constexpr const char *kMudStream = "swamp-mud";
constexpr const char *kMossStream = "swamp-moss";
constexpr const char *kClimateStream = "swamp-climate";
// Deep water: water vertices at least kDeepReach tiles from land in a straight line (eight-connected
// steps drew every core as a rectangle), opened by kDeepOpen and kept only in bodies of kDeepLeast
// vertices or more. Four tiles in a straight line are never under three steps (kDeepSteps - 1, what
// validateWorld checks), which leaves two tiles of pure shallows.
constexpr int kDeepReach = 4, kDeepSteps = 4, kDeepOpen = 1, kDeepLeast = 12;
// Mud: beach sand within kMudBeach vertex steps of water, kMudHome tiles or more from every swarm's
// footprint and kMudAlgae tiles or more from every algae tile.
constexpr int kMudBeach = 2, kMudHome = 12, kMudAlgae = 6;
// Moss: grass below the kMossLow quantile of the land's field values, where the patch field passes
// kMossPatch (of 65535), kMossHome tiles or more from every swarm, in patches of kMossLeast
// vertices or more.
constexpr double kMossLow = 0.36;
constexpr int kMossPatch = 26000, kMossHome = 12, kMossLeast = 24;
// Hammocks: seeds on land above the kHammockHigh quantile, local maxima kHammockSpacing apart; a
// rough knot kHammockLow to kHammockHigh tiles in radius, on vacant pure grass kHammockShore vertex
// steps from water and kHammockHome tiles from every swarm; fewer than kHammockLeast tiles left
// after its two-thick trim and it is not laid.
constexpr double kHammockRise = 0.78, kHammockLow = 3.0, kHammockHigh = 5.5;
constexpr int kHammockShore = 3, kHammockHome = 20, kHammockSpacing = 16, kHammockLeast = 10;
// Causeways: from ground within kCauseNear walking steps of a swarm, straight across at most
// kCauseSpan water tiles, saving at least kCauseSave steps; causeways kCauseApart tiles apart.
constexpr int kCauseNear = 40, kCauseSpan = 6, kCauseSave = 30, kCauseApart = 16;
// Deep water keeps this far from algae, so an algae bed is a rounded shoal in the dark water.
constexpr double kDeepAlgae = 2.5;
// What validateWorld checks, from every colony's workers (who stand a few tiles from the swarm's
// footprint): no Rough within kRoughCheck, no Fertile within kFertileCheck, no Obstacles within
// kHomeCheck.
constexpr int kRoughCheck = 6, kFertileCheck = 6, kHomeCheck = 12;

// The look-only climates: one artwork per mechanic.
struct Climate
{
	const char *name;
	TerrainType grass, sand, deep, mud, fertile, thicket;
};
constexpr Climate kClimates[2] = {
	{"temperate", GRASS, PALE_SAND, DARK_WATER, MUD, MOSS, THICKET},
	{"tropical", LUSH_GRASS, SAND, DEEP_WATER, MUDFLAT, LOAM, HEDGE}};

// Keeps only the tiles of `mask` that lie in some 2x2 block of it, so every kept tile has a rock vertex.
std::vector<unsigned char> blocksOfTwo(const Torus &t, const std::vector<unsigned char> &mask)
{
	std::vector<unsigned char> out(mask.size(), 0);
	for (int y = 0; y < t.h; ++y)
		for (int x = 0; x < t.w; ++x)
		{
			generationCheckpoint();
			if (mask[t.at(x, y)] && mask[t.at(x + 1, y)] && mask[t.at(x, y + 1)] && mask[t.at(x + 1, y + 1)])
				for (int dy = 0; dy <= 1; ++dy)
					for (int dx = 0; dx <= 1; ++dx)
						out[t.at(x + dx, y + dy)] = 1;
		}
	return out;
}

// The tiles with two or more corners in `vertices`: what the engine blocks for obstacle corners.
std::vector<unsigned char> blockedByCorners(const Torus &t, const std::vector<unsigned char> &vertices)
{
	std::vector<unsigned char> blocked(vertices.size(), 0);
	for (int y = 0; y < t.h; ++y)
		for (int x = 0; x < t.w; ++x)
			blocked[t.at(x, y)] =
				vertices[t.at(x, y)] + vertices[t.at(x + 1, y)] + vertices[t.at(x, y + 1)] + vertices[t.at(x + 1, y + 1)] >= 2;
	return blocked;
}

// Whether some blocked group of `blocked` (8-connected) parts the ground round it: the open tiles
// beside it fall in more than one walkable component. Returns the first such group's label, or -1;
// `label` receives the groups. (The same check as Shattered Coast's, Everglades' and Ring World's.)
int partingGroup(const Torus &t, const std::vector<unsigned char> &walkable,
				 const std::vector<unsigned char> &blocked, std::vector<int> &label)
{
	const int n = t.size();
	std::vector<unsigned char> open(size_t(n), 0);
	for (int i = 0; i < n; ++i)
		open[i] = walkable[i] && !blocked[i];
	const std::vector<int> component = connectedRegions(open, t.w, t.h, true, GridNeighbors::Eight);
	label = connectedRegions(blocked, t.w, t.h, true, GridNeighbors::Eight);
	int groups = 0;
	for (int l : label)
		groups = std::max(groups, l + 1);
	std::vector<int> seen(size_t(groups), -1);
	for (int i = 0; i < n; ++i)
	{
		generationCheckpoint();
		if (label[i] < 0)
			continue;
		const int x = t.remainderX(i), y = i / t.w;
		for (int dy = -1; dy <= 1; ++dy)
			for (int dx = -1; dx <= 1; ++dx)
			{
				const int j = t.at(x + dx, y + dy);
				if (!open[j])
					continue;
				if (seen[label[i]] < 0)
					seen[label[i]] = component[j];
				else if (seen[label[i]] != component[j])
					return label[i];
			}
	}
	return -1;
}

bool wet(unsigned char v)
{
	return v == WATER || v == DEEP_WATER;
}

// The field's value at every vertex, through the tiling, and the land's quantiles of it.
struct Relief
{
	std::vector<float> at;    // per vertex
	std::vector<float> land;  // the land vertices' values, sorted
	float quantile(double q) const
	{
		if (land.empty())
			return 0;
		const size_t k = size_t(std::clamp(q, 0.0, 1.0) * double(land.size() - 1));
		return land[k];
	}
};

Relief reliefOf(const Torus &t, const std::vector<float> &field, const HeightFieldTiling &tiling,
				const TerrainSketch &mech)
{
	Relief r;
	r.at.assign(size_t(t.size()), 0);
	for (int y = 0; y < t.h; ++y)
		for (int x = 0; x < t.w; ++x)
		{
			generationCheckpoint();
			const int i = t.at(x, y);
			r.at[i] = field.at(size_t(x % int(tiling.w)) + size_t(y % int(tiling.h)) * tiling.w);
			if (!wet(mech[i]))
				r.land.push_back(r.at[i]);
		}
	std::sort(r.land.begin(), r.land.end());
	return r;
}

// The hammocks, planned on the finished revision-5 map: tiles drawn as thicket (rockVertices).
struct Hammocks
{
	std::vector<unsigned char> tiles;
	int wanted = 0, placed = 0, dropped = 0;
};

Hammocks planHammocks(const GenerationMap &map, const Torus &t, GenerationContext &context, const SwampOptions &o,
					  const TerrainSketch &mech, const Relief &relief)
{
	const int n = t.size();
	Hammocks out;
	out.tiles.assign(size_t(n), 0);
	out.wanted = int(std::lround(double(o.hammocks) * n / (128.0 * 128.0)));
	if (out.wanted <= 0)
		return out;
	std::vector<unsigned char> water(size_t(n), 0);
	for (int i = 0; i < n; ++i)
		water[i] = wet(mech[i]);
	const std::vector<int> shore = stepsFrom(t, water);
	const std::vector<unsigned char> homes = swarmSurroundings(t, context, kHammockHome);
	const float rise = relief.quantile(kHammockRise);
	std::vector<unsigned char> candidate(size_t(n), 0);
	for (int y = 0; y < t.h; ++y)
		for (int x = 0; x < t.w; ++x)
		{
			generationCheckpoint();
			const int i = t.at(x, y);
			bool grass = true;
			for (int dy = 0; dy <= 1; ++dy)
				for (int dx = 0; dx <= 1; ++dx)
				{
					const int v = t.at(x + dx, y + dy);
					grass = grass && mech[v] == GRASS && shore[v] >= kHammockShore;
				}
			candidate[i] = grass && !homes[i] && !map.isResource(x, y) && map.getBuilding(x, y) == NOGBID &&
						   map.getGroundUnit(x, y) == NOGUID;
		}
	const std::vector<int> room = clearance(t, candidate);
	// Seeds: the hummocks' tops, local maxima of the field over the candidates, highest first.
	std::vector<int> negated(static_cast<size_t>(n));
	for (int i = 0; i < n; ++i)
		negated[i] = -int(std::lround(relief.at[i] * 1e6));
	const std::vector<int> top = windowMinimum(t, negated, kHammockSpacing / 2);
	std::vector<std::pair<int, int>> seeds;
	for (int i = 0; i < n; ++i)
		if (candidate[i] && room[i] >= 2 && negated[i] == top[i] && relief.at[i] >= rise)
			seeds.push_back({negated[i], i});
	std::sort(seeds.begin(), seeds.end());
	struct Placed
	{
		int centre;
		std::vector<int> tiles;
	};
	std::vector<Placed> placed;
	for (const auto &seed : seeds)
	{
		const int c = seed.second;
		if (int(placed.size()) >= out.wanted)
			break;
		const int cx = t.remainderX(c), cy = c / t.w;
		bool apart = true;
		for (const Placed &p : placed)
			apart = apart && t.dist2(cx, cy, t.remainderX(p.centre), p.centre / t.w) >= kHammockSpacing * kHammockSpacing;
		if (!apart)
			continue;
		// Three draws per seed tried, whatever comes of it.
		const double radius = std::min(kHammockLow + (kHammockHigh - kHammockLow) * context.bounded(kHammockStream, 1001) / 1000.0,
									   room[c] + 1.5);
		const double phase = context.bounded(kHammockStream, 628) / 100.0;
		const int lobes = 2 + int(context.bounded(kHammockStream, 3));
		std::vector<unsigned char> local(size_t(n), 0);
		std::vector<int> tiles;
		const int box = int(radius * 1.3) + 1;
		for (int dy = -box; dy <= box; ++dy)
			for (int dx = -box; dx <= box; ++dx)
			{
				const double angle = std::atan2(double(dy), double(dx));
				const double reach = radius * (1.0 + 0.28 * std::sin(lobes * angle + phase));
				if (dx * dx + dy * dy > reach * reach)
					continue;
				const int i = t.at(cx + dx, cy + dy);
				if (!candidate[i])
					continue;
				local[i] = 1;
				tiles.push_back(i);
			}
		const std::vector<unsigned char> blocks = blocksOfTwo(t, local);
		std::vector<int> kept;
		for (int i : tiles)
			if (blocks[i])
				kept.push_back(i);
		if (int(kept.size()) < kHammockLeast)
			continue;
		placed.push_back({c, kept});
	}
	// Drop any hammock whose blocked tiles part the walkable ground round it, then check again. The
	// ground is the terrain's (crops are cleared, so they part nothing), as validateWorld reads it.
	const std::vector<unsigned char> walk = walkableTiles(mech, t);
	std::vector<unsigned char> alive(placed.size(), 1);
	std::vector<int> owner(size_t(n), -1);
	for (int round = 0; round <= int(placed.size()); ++round)
	{
		std::fill(out.tiles.begin(), out.tiles.end(), 0);
		std::fill(owner.begin(), owner.end(), -1);
		for (size_t q = 0; q < placed.size(); ++q)
			if (alive[q])
				for (int i : placed[q].tiles)
				{
					out.tiles[i] = 1;
					owner[i] = int(q);
				}
		std::vector<int> label;
		const int parting = partingGroup(t, walk, blockedByCorners(t, rockVertices(t, out.tiles)), label);
		if (parting < 0)
			break;
		bool any = false;
		for (int i = 0; i < n; ++i)
			if (label[i] == parting && owner[i] >= 0)
			{
				alive[size_t(owner[i])] = 0;
				any = true;
			}
		++out.dropped;
		if (!any)
			break; // cannot happen: every blocked tile belongs to some hammock
	}
	for (size_t q = 0; q < placed.size(); ++q)
		out.placed += alive[q];
	return out;
}

// One boardwalk causeway: the vertices it paints and the walk it was planned to save.
struct Causeway
{
	int team = -1, saving = 0, measured = 0, cx = 0, cy = 0;
	std::vector<int> vertices;
};

// The causeways over the mechanic sketch (deep cores, mud and moss already in it): at most one per
// colony, each the straight crossing that saves the longest walk round from ground near its swarm.
std::vector<Causeway> planCauseways(const GenerationMap &map, const Torus &t, GenerationContext &context,
									TerrainSketch &mech, const std::vector<unsigned char> &held)
{
	const int n = t.size(), teams = context.request.nbTeams;
	const auto walkOf = [&](const TerrainSketch &sketch)
	{
		std::vector<unsigned char> walk = walkableTiles(sketch, t);
		for (int i = 0; i < n; ++i)
		{
			const int x = t.remainderX(i), y = i / t.w;
			if (walk[i] && map.isResource(x, y))
			{
				const int type = map.getResource(x, y).type;
				// Stone and fruit are never cleared; crops are, so they do not count as walls here.
				walk[i] = type != STONE && type != CHERRY && type != ORANGE && type != PRUNE;
			}
		}
		return walk;
	};
	const auto swarmOf = [&](int team)
	{
		std::vector<unsigned char> swarm(size_t(n), 0);
		for (int dy = 0; dy < 4; ++dy)
			for (int dx = 0; dx < 4; ++dx)
				swarm[t.at(context.bootX[team] + dx, context.bootY[team] + dy)] = 1;
		return swarm;
	};
	std::vector<Causeway> laid;
	const int steps4[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
	for (int team = 0; team < teams; ++team)
	{
		generationCheckpoint();
		const std::vector<unsigned char> walk = walkOf(mech);
		const std::vector<int> steps = stepsFrom(t, swarmOf(team), walk);
		Causeway best;
		best.saving = kCauseSave - 1;
		for (int a = 0; a < n; ++a)
		{
			generationCheckpoint();
			if (!walk[a] || steps[a] < 0 || steps[a] > kCauseNear)
				continue;
			const int ax = t.remainderX(a), ay = a / t.w;
			for (const auto &d : steps4)
			{
				int b = -1, span = 0;
				bool water = true;
				for (int k = 1; k <= kCauseSpan + 1 && water; ++k)
				{
					const int j = t.at(ax + d[0] * k, ay + d[1] * k);
					if (walk[j])
					{
						if (k >= 2)
						{
							b = j;
							span = k - 1;
						}
						break;
					}
					// A crossing tile holds nothing and every corner is shallow water or a beach.
					const int jx = t.remainderX(j), jy = j / t.w;
					water = !map.isResource(jx, jy);
					for (int dy = 0; dy <= 1 && water; ++dy)
						for (int dx = 0; dx <= 1 && water; ++dx)
						{
							const unsigned char v = mech[t.at(jx + dx, jy + dy)];
							water = v == WATER || v == SAND || v == MUDFLAT;
						}
				}
				if (b < 0 || steps[b] < 0)
					continue;
				const int saving = steps[b] - steps[a] - (span + 1);
				if (saving <= best.saving)
					continue;
				// The vertices: two rows (or columns) under the crossing tiles, so they are pure
				// boardwalk and the tiles either side walk too.
				std::vector<int> vertices;
				bool free = true;
				for (int k = 1; k <= span + 1 && free; ++k)
					for (int side = 0; side <= 1 && free; ++side)
					{
						const int vx = d[0] != 0 ? ax + d[0] * k + (d[0] > 0 ? 0 : 1) : ax + side;
						const int vy = d[1] != 0 ? ay + d[1] * k + (d[1] > 0 ? 0 : 1) : ay + side;
						const int v = t.at(vx, vy);
						free = !held[v] && mech[v] != DEEP_WATER;
						vertices.push_back(v);
					}
				if (!free)
					continue;
				const int mx = ax + d[0] * (span + 1) / 2, my = ay + d[1] * (span + 1) / 2;
				bool apart = true;
				for (const Causeway &c : laid)
					apart = apart && t.dist2(mx, my, c.cx, c.cy) >= kCauseApart * kCauseApart;
				if (!apart)
					continue;
				best.team = team;
				best.saving = saving;
				best.vertices = vertices;
				best.cx = t.x(mx);
				best.cy = t.y(my);
			}
		}
		if (best.team < 0)
			continue;
		// Measure the walk it actually saves on the sketch with it laid.
		TerrainSketch with = mech;
		for (int v : best.vertices)
			with[v] = BOARDWALK;
		const std::vector<int> after = stepsFrom(t, swarmOf(team), walkOf(with));
		long long before = 0, now = 0;
		int reached = 0;
		for (int i = 0; i < n; ++i)
			if (steps[i] >= 0 && after[i] >= 0)
			{
				before += steps[i];
				now += after[i];
				++reached;
			}
		best.measured = reached ? int((before - now) * 100 / reached) : 0; // hundredths of a step, mean
		mech = with;
		laid.push_back(best);
	}
	return laid;
}

// How many vertices each layer took.
struct LayerCounts
{
	int deep = 0, mud = 0, moss = 0, thicket = 0, boardwalk = 0;
};

TerrainSketch lookSketch(const TerrainSketch &mech, const Climate &climate)
{
	TerrainSketch looks(mech.size(), 0);
	for (size_t i = 0; i < mech.size(); ++i)
	{
		generationCheckpoint();
		switch (mech[i])
		{
		case GRASS:
			looks[i] = climate.grass;
			break;
		case SAND:
			looks[i] = climate.sand;
			break;
		case DEEP_WATER:
			looks[i] = climate.deep;
			break;
		case MUDFLAT:
			looks[i] = climate.mud;
			break;
		case MOSS:
			looks[i] = climate.fertile;
			break;
		case THICKET:
			looks[i] = climate.thicket;
			break;
		default:
			break;
		}
	}
	return looks;
}

// The bog pass: hammocks, deep cores, mud, moss, causeways and looks over the finished revision-5
// map.
void bogPass(GenerationWorld &game, GenerationContext &context, const SwampOptions &o, const std::vector<float> &field,
			 const HeightFieldTiling &tiling)
{
	GenerationMap &map = game.map;
	const Torus t(map);
	const int n = t.size(), teams = context.request.nbTeams;
	TerrainSketch mech(static_cast<size_t>(n));
	for (int i = 0; i < n; ++i)
		mech[i] = map.vertexTerrainAt(t.remainderX(i), i / t.w);
	const Relief relief = reliefOf(t, field, tiling, mech);
	LayerCounts counts;
	for (int team = 0; team < teams; ++team)
	{
		context.telemetry.measure("swamp.starts.swarm-x", context.bootX[team], team);
		context.telemetry.measure("swamp.starts.swarm-y", context.bootY[team], team);
	}

	// Corners of tiles holding a deposit (algae, in the water), a building or a unit keep their
	// rules; `occupied` is the same for buildings and units alone (moss may lie under a deposit,
	// since Fertile ground holds everything grass holds).
	std::vector<unsigned char> held(size_t(n), 0), occupied(size_t(n), 0), algae(size_t(n), 0);
	for (int i = 0; i < n; ++i)
	{
		const int x = t.remainderX(i), y = i / t.w;
		const bool resource = map.isResource(x, y);
		const bool standing = map.getBuilding(x, y) != NOGBID || map.getGroundUnit(x, y) != NOGUID;
		algae[i] = resource && map.getResource(x, y).type == ALGA;
		if (resource || standing)
			for (int dy = 0; dy <= 1; ++dy)
				for (int dx = 0; dx <= 1; ++dx)
				{
					held[t.at(x + dx, y + dy)] = 1;
					if (standing)
						occupied[t.at(x + dx, y + dy)] = 1;
				}
	}

	// The hammocks first: they read the revision-5 terrain and deposits.
	const Hammocks hammocks = planHammocks(map, t, context, o, mech, relief);
	{
		const std::vector<unsigned char> thicket = rockVertices(t, hammocks.tiles);
		for (int i = 0; i < n; ++i)
			if (thicket[i])
			{
				mech[i] = THICKET;
				++counts.thicket;
			}
	}

	std::vector<unsigned char> water(size_t(n), 0);
	for (int i = 0; i < n; ++i)
		water[i] = mech[i] == WATER;
	if (o.deepWater)
	{
		std::vector<unsigned char> land(size_t(n), 0);
		for (int i = 0; i < n; ++i)
			land[i] = !water[i];
		const std::vector<std::int64_t> fromLand = distanceSquaredTo(t, land);
		std::vector<unsigned char> core(size_t(n), 0);
		for (int i = 0; i < n; ++i)
			core[i] = water[i] && (fromLand[i] < 0 || fromLand[i] >= std::int64_t(kDeepReach) * kDeepReach);
		const std::vector<unsigned char> shoal = dilateRound(t, algae, kDeepAlgae);
		for (int i = 0; i < n; ++i)
			core[i] = core[i] && !held[i] && !shoal[i];
		const std::vector<unsigned char> grown = dilateRound(t, erode(t, core, kDeepOpen), kDeepOpen);
		for (int i = 0; i < n; ++i)
			core[i] = core[i] && grown[i];
		core = dropSmallRegions(t, core, kDeepLeast);
		for (int i = 0; i < n; ++i)
			if (core[i])
			{
				mech[i] = DEEP_WATER;
				++counts.deep;
			}
	}

	if (o.mudflats > 0)
	{
		const std::vector<int> fromWater = stepsFrom(t, water);
		const std::vector<unsigned char> homes = swarmSurroundings(t, context, kMudHome);
		const std::vector<unsigned char> nearAlgae = dilate(t, algae, kMudAlgae);
		const std::vector<int> noise = periodicNoise(t.w, t.h, 9, context.stream(kMudStream));
		std::vector<std::pair<int, int>> ranked;
		for (int i = 0; i < n; ++i)
			if (mech[i] == SAND && fromWater[i] <= kMudBeach && !held[i] && !homes[i] && !nearAlgae[i])
				ranked.push_back({-noise[i], i});
		std::sort(ranked.begin(), ranked.end());
		const size_t share = ranked.size() * size_t(std::clamp(o.mudflats, 0, 100)) / 100;
		for (size_t k = 0; k < share; ++k)
		{
			mech[ranked[k].second] = MUDFLAT;
			++counts.mud;
		}
	}

	if (o.moss)
	{
		const std::vector<unsigned char> homes = swarmSurroundings(t, context, kMossHome + 1);
		const std::vector<int> patch = fractalNoise(t.w, t.h, 32, 3, context.stream(kMossStream));
		const float low = relief.quantile(kMossLow);
		std::vector<unsigned char> thicketNear(size_t(n), 0);
		for (int i = 0; i < n; ++i)
			thicketNear[i] = mech[i] == THICKET;
		thicketNear = dilate(t, thicketNear, 1);
		std::vector<unsigned char> moss(size_t(n), 0);
		for (int i = 0; i < n; ++i)
			moss[i] = mech[i] == GRASS && !occupied[i] && !homes[i] && !thicketNear[i] && relief.at[i] < low &&
					  patch[i] > kMossPatch;
		moss = dropSmallRegions(t, moss, kMossLeast);
		for (int i = 0; i < n; ++i)
			if (moss[i])
			{
				mech[i] = MOSS;
				++counts.moss;
			}
	}

	std::vector<Causeway> causeways;
	if (o.causeways)
		causeways = planCauseways(map, t, context, mech, held);
	for (const Causeway &c : causeways)
		counts.boardwalk += int(c.vertices.size());

	const int climateIndex =
		o.climate > 0 ? std::clamp(o.climate, 1, 2) - 1 : int(context.bounded(kClimateStream, 2));
	const Climate &climate = kClimates[climateIndex];
	writeVertices(map, composeLooks(mech, lookSketch(mech, climate)));

	context.telemetry.choice("swamp.climate", climate.name);
	context.telemetry.choice("swamp.deep-water", o.deepWater ? "on" : "off");
	context.telemetry.measure("swamp.deep.vertices", counts.deep);
	context.telemetry.measure("swamp.mudflats.share", o.mudflats);
	context.telemetry.measure("swamp.mud.vertices", counts.mud);
	context.telemetry.measure("swamp.moss.vertices", counts.moss);
	context.telemetry.measure("swamp.hammocks.wanted", hammocks.wanted);
	context.telemetry.measure("swamp.hammocks.placed", hammocks.placed);
	context.telemetry.measure("swamp.hammocks.dropped", hammocks.dropped);
	context.telemetry.measure("swamp.hammocks.thicket-vertices", counts.thicket);
	if (hammocks.placed < hammocks.wanted)
		context.telemetry.fallback("swamp.hammocks.short", "Fewer hammocks fitted the hummocks than were wanted");
	context.telemetry.measure("swamp.causeways.laid", int(causeways.size()));
	context.telemetry.measure("swamp.causeways.vertices", counts.boardwalk);
	for (const Causeway &c : causeways)
	{
		context.telemetry.measure("swamp.causeways.planned-saving", c.saving, c.team);
		context.telemetry.measure("swamp.causeways.mean-saving-hundredths", c.measured, c.team);
	}
	if (context.telemetry.enabled())
	{
		// How far each colony walks to the nearest stone (-1 when none can be reached).
		const std::vector<unsigned char> walkable = walkableTiles(map);
		for (int team = 0; team < teams; ++team)
		{
			std::vector<unsigned char> swarm(size_t(n), 0);
			for (int dy = 0; dy < 4; ++dy)
				for (int dx = 0; dx < 4; ++dx)
					swarm[t.at(context.bootX[team] + dx, context.bootY[team] + dy)] = 1;
			const std::vector<int> steps = stepsFrom(t, swarm, walkable);
			int nearest = INT_MAX;
			for (int i = 0; i < n; ++i)
			{
				const int x = t.remainderX(i), y = i / t.w;
				if (!map.isResource(x, y) || map.getResource(x, y).type != STONE)
					continue;
				for (const auto &step : kCardinalSteps)
				{
					const int s = steps[t.at(x + step[0], y + step[1])];
					if (s >= 0)
						nearest = std::min(nearest, s + 1);
				}
			}
			context.telemetry.measure("swamp.stone.nearest-walk", nearest == INT_MAX ? -1 : nearest, team);
		}
	}
}
} // namespace

static bool generate(GenerationWorld &game, GenerationContext &context)
{
	const SwampOptions options(context.request);
	const HeightFieldOptions terrain = HeightFieldOptions::fromRequest(context.request, true);
	// The field, copied out as the builder made it, for the bog pass to read the relief by.
	std::vector<float> field;
	if (!generateHeightField(game, context, terrain,
							 [&](HeightMap &hm, unsigned w, unsigned h, float smoothing)
							 {
								 context.telemetry.choice("swamp.field.shape", "noise");
								 context.telemetry.measure("swamp.field.smoothing", smoothing);
								 hm.makeSwamp(smoothing);
								 field.resize(size_t(w) * h);
								 for (unsigned i = 0; i < w * h; ++i)
									 field[i] = hm(i);
							 }))
		return false;
	context.stage = "starts";
	if (!placeStarts(game, context))
		return false;
	// The resource bands are painted from map-wide noise with no awareness of where any colony
	// starts, so they can wall one into a pocket with nowhere to build — a raised amount widens
	// them, but the defaults manage it too. placeStarts has already carved out the swarm's own
	// rectangle by now, so nothing needs to be kept clear for it.
	openStartsBuriedByResources(game, context);
	// 2.0: everything above is revision 5, draw for draw; the bog pass repaints the ground nobody
	// stands on.
	context.stage = "bog";
	bogPass(game, context, options, field, heightFieldTiling(game.map.getW(), game.map.getH(), options.repeat));
	return true;
}

// Nothing here designs an economy: the height field decides where land is and placeStarts picks
// the best spots it left. So the only promise this landscape can make is the floor every colony
// needs — wheat and wood it can walk to, and room for a first base — and the service rolls
// another seed when a field does not leave one.
// 2.0 adds the bog pass's own promises, read from the finished terrain by rule group: every deep
// vertex keeps kDeepSteps - 1 steps of water round it (the shallows), no mud, moss or thicket stands
// near a colony, no thicket parts the walkable ground round it, and every deposit stands on ground
// that holds it.
static std::string validateWorld(const GenerationWorld &game, const GenerationContext &context)
{
	const GenerationMap &map = game.map;
	const int teams = context.request.nbTeams;
	if (std::string floor = startingFloorFailure(map, teams); !floor.empty())
		return floor;
	const Torus t(map);
	const int n = t.size();
	const auto at = [&](int i)
	{ return "(" + std::to_string(t.remainderX(i)) + ", " + std::to_string(i / t.w) + ")"; };
	std::vector<unsigned char> land(size_t(n), 0), obstacle(size_t(n), 0);
	for (int i = 0; i < n; ++i)
	{
		const TerrainGroup group = terrainGroup(map.vertexTerrainAt(t.remainderX(i), i / t.w));
		land[i] = group != TerrainGroup::Water && group != TerrainGroup::DeepWater;
		obstacle[i] = group == TerrainGroup::Obstacles;
	}
	const std::vector<int> fromLand = stepsFrom(t, land);
	for (int i = 0; i < n; ++i)
		if (terrainGroup(map.vertexTerrainAt(t.remainderX(i), i / t.w)) == TerrainGroup::DeepWater &&
			fromLand[i] < kDeepSteps - 1)
			return "Deep water at " + at(i) + " has no shallows between it and the shore.";
	std::vector<unsigned char> workers(size_t(n), 0);
	for (const auto &tiles : unitTilesByTeam(map, teams))
		for (int i : tiles)
			workers[i] = 1;
	const std::vector<unsigned char> nearRough = dilate(t, workers, kRoughCheck),
									 nearFertile = dilate(t, workers, kFertileCheck),
									 nearHome = dilate(t, workers, kHomeCheck);
	for (int i = 0; i < n; ++i)
	{
		const TerrainGroup group = terrainGroup(map.vertexTerrainAt(t.remainderX(i), i / t.w));
		if (group == TerrainGroup::Rough && nearRough[i])
			return "Mud at " + at(i) + " lies too near a colony.";
		if (group == TerrainGroup::Fertile && nearFertile[i])
			return "Moss at " + at(i) + " lies too near a colony.";
		if (group == TerrainGroup::Obstacles && nearHome[i])
			return "Thicket at " + at(i) + " lies too near a colony.";
	}
	std::vector<unsigned char> walkable(size_t(n), 0), blocked(size_t(n), 0);
	for (int y = 0; y < t.h; ++y)
		for (int x = 0; x < t.w; ++x)
		{
			const int i = t.at(x, y);
			const TerrainProperties &rules = map.terrainPropertiesAt(x, y);
			walkable[i] = rules.walkable;
			const bool corner = obstacle[i] || obstacle[t.at(x + 1, y)] || obstacle[t.at(x, y + 1)] ||
								obstacle[t.at(x + 1, y + 1)];
			blocked[i] = corner && !rules.walkable && !rules.swimmable;
			if (map.isResource(x, y))
			{
				const int type = map.getResource(x, y).type;
				if (!(rules.allowedResources & (1u << type)))
					return "A deposit at " + at(i) + " stands on ground that cannot hold it.";
			}
		}
	std::vector<int> label;
	if (const int parting = partingGroup(t, walkable, blocked, label); parting >= 0)
		for (int i = 0; i < n; ++i)
			if (label[i] == parting)
				return "A hammock at " + at(i) + " cuts ground off.";
	return "";
}

GeneratorDefinition swampDefinition()
{
	std::vector<GeneratorControl> controls{
		GeneratorControl{"water", "Water weight", 0, 100, 1, 35, ControlGroup::Terrain, false, true}
			.withSearchRange(25, 45),
		GeneratorControl{"grass", "Grass weight", 0, 100, 1, 60, ControlGroup::Terrain, false, true}
			.withSearchRange(55, 85),
		GeneratorControl{"smoothing", "Smoothing", 1, 8, 1, 6, ControlGroup::Terrain, false}
			.withSearchRange(5, 7),
		GeneratorControl{"fruit", "Fruit", 0, 64, 1, 4, ControlGroup::Resources, false}
			.withSearchRange(2, 8),
		// 2.0. On, the bigger ponds have a core of deep water: slower to swim, no algae, a band of
		// shallows along every shore. Routes on foot are the same either way.
		GeneratorControl::toggle("deep-water", "Deep water", true, ControlGroup::Terrain).withSearchValues({0, 1}),
		// The share of the beaches (12 tiles or more from every home, away from algae) that is mud:
		// slow, unbuildable, and unlike sand it lets crops grow up to the shore.
		GeneratorControl{"mudflats", "Mudflats", 0, 100, 10, 40, ControlGroup::Terrain}.withSearchRange(20, 60),
		// On, the low ground round some ponds, beyond each home's garden, is moss: crop land that
		// waters its neighbours a little. Off by default: the growth tool loses food to the
		// grass/moss edges (not farmland in its count), 10% of the map's and 6% within 24 steps.
		GeneratorControl::toggle("moss", "Moss", false, ControlGroup::Terrain).withSearchValues({0, 1}),
		// Thicket hammocks (impassable tree islands on the highest ground) per 128x128 tiles of map,
		// 20 tiles or more from every home; none ever cuts ground off.
		GeneratorControl{"hammocks", "Hammocks", 0, 8, 1, 3, ControlGroup::Layout}.withSearchRange(1, 5),
		// On, a colony whose walk round a neck of water near home is long gets a boardwalk across it.
		GeneratorControl::toggle("causeways", "Causeways", true, ControlGroup::Layout).withSearchValues({0, 1}),
		// Looks only: Temperate (a bog: black water, pale sand, mud, moss) or Tropical (a mangrove
		// swamp: blue water, mudflats, dark canopy, lush grass). Both play alike.
		GeneratorControl::choice("climate", "Climate", {"Random", "Temperate", "Tropical"}, 0, ControlGroup::Terrain)
			.withSearchValues({1, 2})};
	for (auto &c : heightFieldResourceControls())
	{
		::MapGeneration::generationCheckpoint();
		controls.push_back(std::move(c));
	}
	controls.push_back(
		GeneratorControl{"repeat", "Repeat landscape", 0, 5, 1, 0, ControlGroup::Layout, true}
			.withSearchRange(0, 1));
	return {
			"swamp",
			1,
			"Swamp",
			7, // 7: one look per role
			false,
			std::move(controls),
			generate,
			true,
			nullptr,
			validateWorld,
			{"terrain:natural", "feature:lakes", "feature:swamp", "style:wide-open"}};
}
