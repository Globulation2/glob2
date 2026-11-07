// SPDX-License-Identifier: GPL-3.0-or-later
#include "CraterLakesGenerator.h"
#include "Game.h"
#include "GenerationContext.h"
#include "Grid.h"
#include "LatticeNoise.h"
#include "Map.h"
#include "Morphology.h"
#include "Pipeline.h"
#include "Planting.h"
#include "Resources.h"
#include "Rock.h"
#include "Sketch.h"
#include "StartingPositions.h"
#include "Terrain.h"
#include "TerrainTypeTable.h"
#include "Topology.h"
#include "Unit.h"
#include <algorithm>
#include <climits>
#include <cmath>
#include <cstdint>
#include <deque>
#include <random>
#include <string>
#include <vector>
using namespace MapGeneration;
// Crater lakes (id 4): Leo Wandersleb's crater map from January 2006, on the shared height-field
// pipeline (see Terrain.cpp). The field is round bowls pressed at random into high ground and then
// roughened (makeCraters in HeightMap.cpp).
//
// Lake density sets the bowl count, width * height * density / 30000, so it scales with map area
// (54 bowls at 256x256 and the default 25); lake size is each bowl's radius in tiles. The water
// weight (25, about 23% of the map at the defaults) then decides how deep the lakes fill. Because
// the water share is fixed, the two controls only share it out. Renders at 256x256, seed 1, water
// about 20% in every case: density 10 gives about 20 lakes, big and far apart; density 50 about a
// hundred small ponds; lake size 40 gives round, clean-edged lakes, since the wide bowls outweigh
// the noise; lake size 10 gives narrower bowls, and the noise then floods its own hollows into
// ragged lakes between them.
//
// Game rules: lakes are local obstacles, not barriers between colonies, so this plays as an open
// land map where water shapes the paths; farmland rings each lake, since wheat and wood regrow only
// near water.
//
// CRATER LAKES 2.0 (revision 6): the same bowls, drawn with the engine's full terrain palette.
// The design brief: a photograph of a crater-lake field shows (1) round lakes, dark in the middle
// with a pale band of shallows at the shore; (2) a broken lip of rock round each bowl a little back
// from the water, cut by gullies, with green inner slopes and a talus of pumice or scree at its
// foot; (3) a ring of dark soil on the outer flank; (4) high, dry ground between the craters, heath
// or tundra where the water never reaches. The concept stays: bowls in high ground, lakes as local
// obstacles on an open land map. So this is a post-pass over revision 5, not a redesign:
// - Revision 5 runs unchanged first: the height field, lakes, beaches, resource bands, starts,
//   starting crops, groves and opening repairs are identical for the same seed (every new draw
//   uses streams of its own). The pass then repaints only ground nobody stands on: no tile holding
//   a deposit, building or unit changes its rules, except the exchanges below (deposits under the
//   rim rock, algae out of the deep cores), which the pass makes on purpose and counts.
// - Two sketches. A mechanic sketch of canonical types (WATER, DEEP_WATER, SAND, GRASS, LOAM,
//   SHALE, SCREE, RIDGE_ROCK) that every check reads, and looks over it by composeLooks (Sketch.h),
//   so looks never change a rule.
// - Deep water (`deep-water`): water vertices kDeepReach tiles or more (straight-line, frayed) from
//   land, with specks dropped, so only the bigger lakes have a core, its outline round, and every
//   shore keeps two tiles of plain shallows. Revision 5 put the algae in the deepest sixth of the
//   water, the lake middles: the algae there move to the nearest free shallows of the same lake, as
//   many as were taken, so the algae amount holds and the algae sit where a shore is in reach. Deep
//   water waters the land like water but is slower to swim (a feel change).
// - Crater rims (`crater-rims`): a band of tiles kRimInner to kRimInner + thickness tiles in a
//   straight line (so the band follows a round lake round, not as a square) from the water of every lake of kRimBody vertices or more, outside the farmland at the shore,
//   cut by angle round the lake into sectors and drawn as runs of rock sectors parted by runs of
//   open ones (the crater type sets the run lengths), so a rim is never a closed ring. The rock is
//   Ridges (impassable, towers shoot over), never on a tile holding fruit, a building or a unit,
//   never on a neck between two lakes, never within kRimHome tiles of a swarm; wheat, wood and stone
//   it covers are removed and counted (never within kCropKeep tiles of a swarm, where the starting
//   crops are). Stone-amount sets how many arcs carry a quarry face on their outer flank (stone on
//   grass, tiles left out of the rock), nearest arc to each colony first, every face kQuarryHome
//   tiles or more from every swarm. Any arc whose rock parts the walkable ground round it is
//   dropped; validateWorld checks it again. At the rock's inner foot lies a talus (Rough pumice or
//   scree: slow, unbuildable, does not inhibit growth). The rock keeps kRimHome tiles off the homes
//   because closer in it took farmland within a colony's first 24 steps (growth potential there
//   fell 4%, the worst colony's 10%); from 24 tiles it is unchanged.
// - Outer rings: grass on the outer flank, from the rim band's outer row to kLoamDeep tiles
//   (straight-line) beyond it, is loam (Fertile: buildable crop land that waters the land itself), in
//   patches where a noise field allows.
// - Every layer leaves the home garden, kPlainHome tiles round a swarm, plain grass: the kits and
//   the starting-crop guarantee are laid on plain grass, and the garden reads as a clearing.
// - Uplands: grass kUplandWater or more steps from any water is barren heath or tundra where a
//   noise field passes, beyond the reach of the crop probe and its mirror, away from the homes.
// - Every distance from a home is straight-line from the swarm's footprint, frayed outward by a
//   noise field, so no layer draws a square round a home.
// - A crater type (`crater-type`): volcanic maars (dense thick rims of dark rock, pumice talus,
//   loam, heath and ash uplands) or glacial kettles (sparse thin rims of outcrop, scree talus, moss,
//   dark water, lush grass, tundra uplands). Looks and rim density; every layer and choice is in
//   telemetry.
namespace
{
// Streams of their own, so every draw revision 5 made is the draw it made.
constexpr const char *kRimStream = "crater-lakes-rims";
constexpr const char *kQuarryStream = "crater-lakes-quarries";
constexpr const char *kLookStream = "crater-lakes-looks";
constexpr const char *kTypeStream = "crater-lakes-type";
// Deep water: water vertices at least kDeepReach (plus up to kDeepFray) tiles from land, kept only in
// bodies of kDeepLeast vertices or more. 4.5 tiles leave every shore two tiles of pure shallows.
constexpr double kDeepReach = 4.5, kDeepFray = 1.5;
constexpr int kDeepSteps = 4, kDeepLeast = 12;
// Rims: lakes of kRimBody water vertices or more; rock tiles have every corner kRimInner to
// kRimInner + thickness tiles from water in a straight line; a sector is about kSectorLength tiles of rim; no rim
// within kNeckReach steps of a second lake's water.
constexpr int kRimBody = 80, kRimInner = 6, kSectorLength = 7, kNeckReach = 12;
// Straight-line distances kept from a swarm's footprint (each frayed outward by up to kHomeFray):
// rim rock, quarry faces, crops the rock may clear, and the home garden that every other layer and
// look leaves plain grass.
constexpr double kRimHome = 24, kQuarryHome = 24, kCropKeep = 34, kPlainHome = 12, kHomeFray = 4;
// Quarry faces: one wanted per colony plus one per kTilesPerFace tiles of map at the default stone
// amount; a face is the outer-flank tiles within kFaceHalf of its middle, at least kFaceLeast.
constexpr int kTilesPerFace = 32768, kFaceHalf = 2, kFaceLeast = 3;
// Loam lies from the rim band's outer row (one tile inside its edge, where the step band used to end) to
// kLoamDeep tiles (straight-line) beyond the band (frayed by two either way), where a noise field stays
// under kLoamGate, so the outer ring comes in patches rather than as a drawn band. Started at the band's
// edge, the narrower straight-line ring cost a tenth of the map's food (seeds 1-6: 2,271 against 2,538);
// from its outer row the total is 2,395.
constexpr int kLoamDeep = 3, kLoamGate = 40000;
// Upland pavement: grass kUplandWater steps or more from water, where noise passes kUplandCut.
constexpr int kUplandWater = 24, kUplandCut = 30000;
// The validator: straight-line tiles from a colony's workers within which no rock, fertile or rough
// ground lies (the pass keeps further: workers stand a few tiles off the swarm's footprint).
constexpr double kRockHomeCheck = 20, kGardenCheck = 8;

// The band is measured in straight-line tiles, a tile deeper than it was in eight-connected steps:
// along a diagonal a band of steps is 1.4 times as wide, and at the same depth in tiles the rims kept
// about half their rock.
struct CraterType
{
	const char *name;
	int onMost, offLeast, offSpan; // a rock run is 1..onMost sectors, an open run offLeast..offLeast+offSpan-1
	int thickness;                 // how far the rim band reaches beyond kRimInner, in straight-line tiles
	TerrainType deep, rock, rough, loam, upland, uplandSecond, beach, dune, duneSecond;
	int lush, dry; // grass lush within `lush` steps of water, dry from `dry` steps
};
constexpr CraterType kTypes[2] = {
	{"maar", 3, 1, 2, 4, DEEP_WATER, RIDGE_ROCK, PUMICE, LOAM, HEATH, VOLCANIC_ASH, SAND, DUNE_SAND,
	 WINDY_SAND_DUNE, 6, 20},
	{"kettle", 2, 2, 3, 3, DARK_WATER, OUTCROP, SCREE, MOSS, TUNDRA, HEATH, PALE_SAND, PALE_SAND, DUNE_SAND, 9,
	 24}};

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

// The tiles with two or more corners in `vertices`: what the engine blocks for rock corners.
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
// `label` receives the groups. (The same check as Watershed's, Everglades', Ring World's and
// Shattered Coast's.)
int partingGroup(const Torus &t, const std::vector<unsigned char> &walkable, const std::vector<unsigned char> &blocked,
				 std::vector<int> &label)
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
	std::vector<int> seen(static_cast<size_t>(groups), -1);
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

// The corners of tile i.
template <typename F> void forCorners(const Torus &t, int i, F f)
{
	const int x = t.remainderX(i), y = i / t.w;
	f(t.at(x, y));
	f(t.at(x + 1, y));
	f(t.at(x, y + 1));
	f(t.at(x + 1, y + 1));
}

// Every vertex's nearest lake (eight-connected steps through anything, up to `limit`), and whether a
// second lake is about as near: the necks between lakes, which keep their ground open.
struct LakeReach
{
	std::vector<int> body, steps;
	std::vector<unsigned char> neck;
};

LakeReach lakeReach(const Torus &t, const std::vector<int> &water, int limit)
{
	const int n = t.size();
	LakeReach r;
	r.body.assign(size_t(n), -1);
	r.steps.assign(size_t(n), -1);
	r.neck.assign(size_t(n), 0);
	std::deque<int> queue;
	for (int i = 0; i < n; ++i)
		if (water[i] >= 0)
		{
			r.body[i] = water[i];
			r.steps[i] = 0;
			queue.push_back(i);
		}
	while (!queue.empty())
	{
		generationCheckpoint();
		const int i = queue.front();
		queue.pop_front();
		if (r.steps[i] >= limit)
			continue;
		const int x = t.remainderX(i), y = i / t.w;
		for (int dy = -1; dy <= 1; ++dy)
			for (int dx = -1; dx <= 1; ++dx)
			{
				const int j = t.at(x + dx, y + dy);
				if (r.steps[j] < 0)
				{
					r.steps[j] = r.steps[i] + 1;
					r.body[j] = r.body[i];
					queue.push_back(j);
				}
				else if (r.body[j] != r.body[i])
					r.neck[i] = r.neck[j] = 1;
			}
	}
	return r;
}

// Straight-line distance from every swarm's footprint, and whether a tile clears a distance once
// the noise has frayed it outward.
struct HomeDistance
{
	std::vector<std::int64_t> d2;
	std::vector<int> fray;
	bool clear(int i, double reach) const
	{
		if (d2[i] < 0)
			return true;
		const double r = reach + kHomeFray * fray[i] / 65536.0;
		return double(d2[i]) >= r * r;
	}
};

// The rim plan on the finished revision-5 map. Arc masks are tiles; rock is vertices.
struct Rims
{
	std::vector<unsigned char> rockTiles; // arc tiles drawn as rock (rockVertices), deposits cleared
	std::vector<unsigned char> quarry;    // quarry faces: stone on grass, left out of the rock
	std::vector<unsigned char> rock;      // rock vertices
	int bodies = 0, arcs = 0, dropped = 0, wanted = 0, faces = 0, faceTiles = 0;
};

Rims planRims(const Map &map, const Torus &t, GenerationContext &context, const CraterType &type,
			  const std::vector<int> &body, const std::vector<int> &bodySize, const std::vector<int> &fromWater,
			  const std::vector<int> &rimReach, const TerrainSketch &mech, const HomeDistance &home, int stone)
{
	const int n = t.size(), teams = context.request.nbTeams;
	Rims out;
	out.rockTiles.assign(size_t(n), 0);
	out.quarry.assign(size_t(n), 0);
	out.rock.assign(size_t(n), 0);
	const int bodies = int(bodySize.size());
	const int outer = kRimInner + type.thickness;
	const LakeReach reach = lakeReach(t, body, kNeckReach);
	const std::vector<unsigned char> neck = dilate(t, reach.neck, 1);
	// Deposits the rock may clear: wheat, wood and stone, away from the starting crops.
	const auto clearable = [&](int i)
	{
		const int x = t.remainderX(i), y = i / t.w;
		if (!map.isResource(x, y))
			return true;
		const int kind = map.getResource(x, y).type;
		return kind == STONE || ((kind == WHEAT || kind == WOOD) && home.clear(i, kCropKeep));
	};
	// The rim band: tiles whose every corner lies kRimInner..outer tiles from water in a straight line
	// (eight-connected steps drew the band, and so the rock, as squares round round lakes), nearest one
	// lake.
	std::vector<int> rimBody(size_t(n), -1);
	std::vector<int> rimCount(static_cast<size_t>(bodies), 0);
	for (int i = 0; i < n; ++i)
	{
		generationCheckpoint();
		const int x = t.remainderX(i), y = i / t.w;
		if (!home.clear(i, kRimHome) || map.getBuilding(x, y) != NOGBID || map.getGroundUnit(x, y) != NOGUID ||
			!clearable(i))
			continue;
		bool inBand = true;
		int owner = -2;
		forCorners(t, i,
				   [&](int v)
				   {
					   inBand = inBand && rimReach[v] >= kRimInner && rimReach[v] <= outer && !neck[v] &&
								mech[v] != WATER && reach.body[v] >= 0;
					   if (owner == -2)
						   owner = reach.body[v];
					   else if (owner != reach.body[v])
						   owner = -1;
				   });
		if (!inBand || owner < 0 || bodySize[owner] < kRimBody)
			continue;
		rimBody[i] = owner;
		++rimCount[owner];
	}
	// Each lake's middle (a circular mean across the wrap) for the angle round it.
	const size_t nb = static_cast<size_t>(bodies);
	std::vector<double> cx(nb, 0), sx(nb, 0), cy(nb, 0), sy(nb, 0), midX(nb, 0), midY(nb, 0);
	constexpr double kTau = 6.283185307179586;
	for (int v = 0; v < n; ++v)
		if (body[v] >= 0)
		{
			const double ax = kTau * t.remainderX(v) / t.w, ay = kTau * (v / t.w) / t.h;
			cx[body[v]] += std::cos(ax);
			sx[body[v]] += std::sin(ax);
			cy[body[v]] += std::cos(ay);
			sy[body[v]] += std::sin(ay);
		}
	for (int b = 0; b < bodies; ++b)
	{
		double ax = std::atan2(sx[b], cx[b]), ay = std::atan2(sy[b], cy[b]);
		if (ax < 0)
			ax += kTau;
		if (ay < 0)
			ay += kTau;
		midX[b] = ax / kTau * t.w;
		midY[b] = ay / kTau * t.h;
	}
	// Sectors and their runs: rock runs of 1..onMost sectors, open runs of offLeast.. sectors, one
	// lake after another in body order, so a rim is never closed.
	const int rows = std::max(1, type.thickness);
	std::vector<int> sectors(nb, 0);
	std::vector<std::vector<int>> runOf(nb); // per sector: arc id, or -1 open
	int arcs = 0;
	for (int b = 0; b < bodies; ++b)
	{
		const int s = rimCount[b] / rows / kSectorLength;
		if (bodySize[b] < kRimBody || s < 4)
			continue;
		++out.bodies;
		sectors[b] = s;
		runOf[b].assign(size_t(s), -1);
		const int offset = int(context.bounded(kRimStream, std::uint32_t(s)));
		int done = 0;
		while (done < s)
		{
			int on = 1 + int(context.bounded(kRimStream, std::uint32_t(type.onMost)));
			on = std::min(on, s - done - 1);
			for (int k = 0; k < on; ++k)
				runOf[b][size_t((offset + done + k) % s)] = arcs;
			if (on > 0)
				++arcs;
			done += on;
			const int off = type.offLeast + int(context.bounded(kRimStream, std::uint32_t(type.offSpan)));
			done += std::max(1, off);
		}
	}
	std::vector<int> arcOf(size_t(n), -1);
	std::vector<unsigned char> arcMask(size_t(n), 0);
	for (int i = 0; i < n; ++i)
	{
		const int b = rimBody[i];
		if (b < 0 || sectors[b] == 0)
			continue;
		const double dx = t.offsetX(int(std::lround(midX[b])) % t.w, t.remainderX(i)) + 0.5,
					 dy = t.offsetY(int(std::lround(midY[b])) % t.h, i / t.w) + 0.5;
		double angle = std::atan2(dy, dx);
		if (angle < 0)
			angle += kTau;
		const int sector = std::min(sectors[b] - 1, int(angle / kTau * sectors[b]));
		const int arc = runOf[b][size_t(sector)];
		if (arc < 0)
			continue;
		arcOf[i] = arc;
		arcMask[i] = 1;
	}
	{
		const std::vector<unsigned char> blocks = blocksOfTwo(t, arcMask);
		for (int i = 0; i < n; ++i)
			if (!blocks[i])
			{
				arcMask[i] = 0;
				arcOf[i] = -1;
			}
	}
	const size_t na = static_cast<size_t>(arcs);
	std::vector<std::vector<int>> arcTiles(na);
	for (int i = 0; i < n; ++i)
		if (arcOf[i] >= 0)
			arcTiles[size_t(arcOf[i])].push_back(i);
	std::vector<unsigned char> alive(na, 0);
	for (int a = 0; a < arcs; ++a)
		alive[a] = arcTiles[a].size() >= 8;

	// Quarry faces: candidate faces on the outer flank of every arc away from the homes.
	std::vector<unsigned char> open(size_t(n), 0);
	for (int i = 0; i < n; ++i)
	{
		generationCheckpoint();
		const int x = t.remainderX(i), y = i / t.w;
		if (arcMask[i] || map.isResource(x, y) || map.getBuilding(x, y) != NOGBID)
			continue;
		bool grass = true;
		forCorners(t, i, [&](int v) { grass = grass && mech[v] == GRASS && rimReach[v] >= outer; });
		open[i] = grass;
	}
	const std::vector<int> fromOpen = stepsFrom(t, open);
	std::vector<std::vector<int>> faces(na);
	std::vector<int> faceX(na, 0), faceY(na, 0);
	for (int a = 0; a < arcs; ++a)
	{
		if (!alive[a])
			continue;
		// The flank tile nearest the arc's middle (its mean offset from its first tile).
		const int first = arcTiles[a].front();
		const int fx = t.remainderX(first), fy = first / t.w;
		double mx = 0, my = 0;
		for (int i : arcTiles[a])
		{
			mx += t.offsetX(fx, t.remainderX(i));
			my += t.offsetY(fy, i / t.w);
		}
		const int middleX = t.x(fx + int(std::lround(mx / double(arcTiles[a].size())))),
				  middleY = t.y(fy + int(std::lround(my / double(arcTiles[a].size()))));
		int centre = -1;
		std::int64_t best = INT64_MAX;
		for (int i : arcTiles[a])
		{
			bool flank = false;
			for (const auto &s : kCardinalSteps)
				flank = flank || open[t.at(t.remainderX(i) + s[0], i / t.w + s[1])];
			const std::int64_t d = t.dist2(middleX, middleY, t.remainderX(i), i / t.w);
			if (flank && d < best)
			{
				best = d;
				centre = i;
			}
		}
		if (centre < 0)
			continue;
		// quarryNear's selection (arc tiles within kFaceHalf of the centre, one step from open ground),
		// read from one flood of the open ground rather than a whole-map flood per arc.
		std::vector<int> face;
		const int ccx = t.remainderX(centre), ccy = centre / t.w;
		for (int dy = -kFaceHalf; dy <= kFaceHalf; ++dy)
			for (int dx = -kFaceHalf; dx <= kFaceHalf; ++dx)
			{
				const int i = t.at(ccx + dx, ccy + dy);
				if (arcOf[i] != a || fromOpen[i] != 1)
					continue;
				bool fit = home.clear(i, kQuarryHome);
				forCorners(t, i, [&](int v) { fit = fit && mech[v] == GRASS && fromWater[v] >= 3; });
				if (fit)
					face.push_back(i);
			}
		std::sort(face.begin(), face.end());
		if (int(face.size()) < kFaceLeast)
			continue;
		// The rock behind the face must still stand two tiles thick: count the arc tiles left in some 2x2
		// block of arc tiles that are not face tiles.
		const auto rockTile = [&](int i) { return arcOf[i] == a && !std::binary_search(face.begin(), face.end(), i); };
		int rockLeft = 0;
		for (int i : arcTiles[a])
		{
			if (!rockTile(i))
				continue;
			const int x = t.remainderX(i), y = i / t.w;
			bool block = false;
			for (int oy = -1; oy <= 0 && !block; ++oy)
				for (int ox = -1; ox <= 0 && !block; ++ox)
					block = rockTile(t.at(x + ox, y + oy)) && rockTile(t.at(x + ox + 1, y + oy)) &&
							rockTile(t.at(x + ox, y + oy + 1)) && rockTile(t.at(x + ox + 1, y + oy + 1));
			rockLeft += block;
		}
		if (rockLeft < 6)
			continue;
		faces[a] = face;
		faceX[a] = t.remainderX(centre);
		faceY[a] = centre / t.w;
	}
	// Which faces to stock: the nearest to each colony first, then the rest in a shuffled order, until
	// the stone amount's quota is met.
	out.wanted = int(scaledCount(teams + n / kTilesPerFace, stone));
	std::vector<unsigned char> chosen(na, 0);
	int picked = 0;
	for (int k = 0; k < teams && picked < out.wanted; ++k)
	{
		const int sx0 = context.bootX[k] + 2, sy0 = context.bootY[k] + 2;
		int bestArc = -1;
		std::int64_t best = INT64_MAX;
		for (int a = 0; a < arcs; ++a)
		{
			if (faces[a].empty() || chosen[a] || !alive[a])
				continue;
			const std::int64_t d = t.dist2(sx0, sy0, faceX[a], faceY[a]);
			if (d < best)
			{
				best = d;
				bestArc = a;
			}
		}
		if (bestArc >= 0)
		{
			chosen[bestArc] = 1;
			++picked;
		}
	}
	std::vector<int> rest;
	for (int a = 0; a < arcs; ++a)
		if (!faces[a].empty() && !chosen[a] && alive[a])
			rest.push_back(a);
	context.shuffle(rest.begin(), rest.end(), kQuarryStream);
	for (int a : rest)
		if (picked < out.wanted)
		{
			chosen[a] = 1;
			++picked;
		}

	// Rock, then the parting check on the terrain's own walkability: an arc whose rock (and stone)
	// parts the ground round it is dropped, and the check runs again.
	const std::vector<unsigned char> walk = walkableTiles(mech, t);
	std::vector<int> owner(size_t(n), -1);
	for (int round = 0; round <= arcs; ++round)
	{
		std::fill(out.rockTiles.begin(), out.rockTiles.end(), 0);
		std::fill(out.quarry.begin(), out.quarry.end(), 0);
		std::fill(owner.begin(), owner.end(), -1);
		std::vector<unsigned char> local(size_t(n), 0);
		for (int a = 0; a < arcs; ++a)
		{
			if (!alive[a])
				continue;
			for (int i : arcTiles[a])
				local[i] = 1;
			if (chosen[a])
				for (int i : faces[a])
				{
					local[i] = 0;
					out.quarry[i] = 1;
					owner[i] = a;
				}
		}
		const std::vector<unsigned char> blocks = blocksOfTwo(t, local);
		for (int a = 0; a < arcs; ++a)
			if (alive[a])
				for (int i : arcTiles[a])
					if (blocks[i])
					{
						out.rockTiles[i] = 1;
						owner[i] = a;
					}
		out.rock = rockVertices(t, out.rockTiles);
		std::vector<unsigned char> blocked = blockedByCorners(t, out.rock);
		for (int i = 0; i < n; ++i)
			blocked[i] = blocked[i] || out.quarry[i];
		std::vector<int> label;
		const int parting = partingGroup(t, walk, blocked, label);
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
		{
			// Cannot happen (every blocked tile belongs to an arc); draw no rock rather than risk it.
			std::fill(out.rockTiles.begin(), out.rockTiles.end(), 0);
			std::fill(out.quarry.begin(), out.quarry.end(), 0);
			std::fill(out.rock.begin(), out.rock.end(), 0);
			std::fill(alive.begin(), alive.end(), 0);
			break;
		}
	}
	for (int a = 0; a < arcs; ++a)
		if (alive[a])
		{
			++out.arcs;
			out.faces += chosen[a] && !faces[a].empty();
		}
	for (int i = 0; i < n; ++i)
		out.faceTiles += out.quarry[i];
	return out;
}

// How many vertices each layer took, and the deposits the pass exchanged.
struct LayerCounts
{
	int deep = 0, rock = 0, loam = 0, talus = 0, upland = 0, lush = 0, dry = 0, dunes = 0;
	int stoneCleared = 0, cropsCleared = 0, algaeMoved = 0, algaeLost = 0;
};

// The crater pass: deep cores, rims, talus, loam, uplands and looks over the finished revision-5 map.
bool craterPass(Game &game, GenerationContext &context, const CraterLakesOptions &o)
{
	Map &map = game.map;
	const Torus t(map);
	const int n = t.size();
	TerrainSketch mech(static_cast<size_t>(n));
	for (int i = 0; i < n; ++i)
		mech[i] = map.vertexTerrainAt(t.remainderX(i), i / t.w);
	LayerCounts counts;
	for (int team = 0; team < context.request.nbTeams; ++team)
	{
		context.telemetry.measure("crater-lakes.starts.swarm-x", context.bootX[team], team);
		context.telemetry.measure("crater-lakes.starts.swarm-y", context.bootY[team], team);
	}
	const int typeIndex =
		o.crater_type > 0 ? std::clamp(o.crater_type, 1, 2) - 1 : int(context.bounded(kTypeStream, 2));
	const CraterType &type = kTypes[typeIndex];
	std::mt19937 &random = context.stream(kLookStream);
	const std::vector<int> fray = fractalNoise(t.w, t.h, 16, 3, random);
	const std::vector<int> moor = fractalNoise(t.w, t.h, 32, 3, random);
	const std::vector<int> split = fractalNoise(t.w, t.h, 16, 3, random);

	HomeDistance home;
	{
		std::vector<unsigned char> swarms(size_t(n), 0);
		for (int team = 0; team < context.request.nbTeams; ++team)
			for (int dy = 0; dy < 4; ++dy)
				for (int dx = 0; dx < 4; ++dx)
					swarms[t.at(context.bootX[team] + dx, context.bootY[team] + dy)] = 1;
		home.d2 = distanceSquaredTo(t, swarms);
		home.fray = fray;
	}

	// The lakes: plain water vertices, eight-connected bodies.
	std::vector<unsigned char> water(size_t(n), 0);
	for (int i = 0; i < n; ++i)
		water[i] = mech[i] == WATER;
	const std::vector<int> body = connectedRegions(water, t.w, t.h, true, GridNeighbors::Eight);
	int bodies = 0;
	for (int l : body)
		bodies = std::max(bodies, l + 1);
	const size_t nb = static_cast<size_t>(bodies);
	std::vector<int> bodySize(nb, 0);
	for (int l : body)
		if (l >= 0)
			++bodySize[l];
	const std::vector<int> fromWater = stepsFrom(t, water);
	// The straight-line distance too, for the rims, the talus, the loam, the looks and the uplands:
	// step contours are squares. rimReach is it rounded to whole tiles; wetReach is frayed by noise.
	std::vector<double> wetReach(size_t(n), 0);
	std::vector<int> rimReach(size_t(n), INT_MAX);
	{
		const std::vector<std::int64_t> d2 = distanceSquaredTo(t, water);
		for (int i = 0; i < n; ++i)
		{
			wetReach[i] = d2[i] < 0 ? 1e9 : std::sqrt(double(d2[i])) + 4.0 * (fray[i] - 32768) / 32768;
			if (d2[i] >= 0)
				rimReach[i] = int(std::lround(std::sqrt(double(d2[i]))));
		}
	}

	// Corners of tiles holding a building, a unit or a deposit other than wheat, wood and stone keep
	// their terrain whatever happens; loam may lie under crops and stone (Fertile holds them).
	std::vector<unsigned char> standing(size_t(n), 0);
	for (int i = 0; i < n; ++i)
	{
		const int x = t.remainderX(i), y = i / t.w;
		const bool built = map.getBuilding(x, y) != NOGBID || map.getGroundUnit(x, y) != NOGUID;
		if (built || (map.isResource(x, y) && map.getResource(x, y).type != WHEAT &&
					  map.getResource(x, y).type != WOOD && map.getResource(x, y).type != STONE))
			forCorners(t, i, [&](int v) { standing[v] = 1; });
	}

	// The rims first: they read the revision-5 terrain and deposits.
	Rims rims;
	rims.rockTiles.assign(size_t(n), 0);
	rims.quarry.assign(size_t(n), 0);
	rims.rock.assign(size_t(n), 0);
	if (o.crater_rims)
		rims = planRims(map, t, context, type, body, bodySize, fromWater, rimReach, mech, home, o.stone);
	for (int i = 0; i < n; ++i)
		if (rims.rock[i])
		{
			mech[i] = RIDGE_ROCK;
			++counts.rock;
		}
	// Rock tiles lose their deposits: a vertex is free to repaint when no tile round it keeps one.
	std::vector<unsigned char> kept(size_t(n), 0);
	for (int i = 0; i < n; ++i)
	{
		const int x = t.remainderX(i), y = i / t.w;
		if ((map.isResource(x, y) && !rims.rockTiles[i]) || map.getBuilding(x, y) != NOGBID ||
			map.getGroundUnit(x, y) != NOGUID)
			forCorners(t, i, [&](int v) { kept[v] = 1; });
	}
	// Quarry faces and the ring before them keep plain grass corners: stone stands only on pure grass.
	std::vector<unsigned char> plain(size_t(n), 0);
	{
		const std::vector<unsigned char> ring = dilate(t, rims.quarry, 1);
		for (int i = 0; i < n; ++i)
			if (ring[i])
				forCorners(t, i, [&](int v) { plain[v] = 1; });
	}

	// Deep cores, and the algae they displace.
	std::vector<unsigned char> core(size_t(n), 0);
	if (o.deep_water)
	{
		std::vector<unsigned char> land(size_t(n), 0);
		for (int i = 0; i < n; ++i)
			land[i] = !water[i];
		const std::vector<std::int64_t> fromLand = distanceSquaredTo(t, land);
		for (int i = 0; i < n; ++i)
		{
			const double reach = kDeepReach + kDeepFray * split[i] / 65536.0;
			core[i] = water[i] && fromLand[i] >= 0 && double(fromLand[i]) >= reach * reach;
		}
		core = dropSmallRegions(t, core, kDeepLeast);
		for (int i = 0; i < n; ++i)
			if (core[i])
			{
				mech[i] = DEEP_WATER;
				++counts.deep;
			}
	}

	// Talus at the rock's inner foot, loam on the outer flank, pavement on the uplands.
	const int outer = kRimInner + type.thickness;
	const LakeReach reach = lakeReach(t, body, outer + kLoamDeep + 2);
	const std::vector<unsigned char> nearRock = dilate(t, rims.rock, 1);
	for (int i = 0; i < n; ++i)
	{
		generationCheckpoint();
		if (plain[i] || standing[i] || (kept[i] && mech[i] != GRASS))
			continue;
		const bool free = !kept[i];
		if (nearRock[i] && free && (mech[i] == GRASS || mech[i] == SAND) && fromWater[i] >= 2 &&
			rimReach[i] <= kRimInner && home.clear(i, kPlainHome))
		{
			mech[i] = SCREE;
			++counts.talus;
		}
		else if (mech[i] == GRASS && rimReach[i] >= outer - 1 && moor[i] < kLoamGate &&
				 rimReach[i] <= outer + kLoamDeep + (fray[i] - 32768) * 2 / 32768 && reach.body[i] >= 0 &&
				 bodySize[reach.body[i]] >= kRimBody && home.clear(i, kPlainHome))
		{
			// The outer ring of dark soil. Crops on it stay legal (Fertile holds the grass deposits).
			mech[i] = LOAM;
			++counts.loam;
		}
		else if (mech[i] == GRASS && free && fromWater[i] >= kUplandWater - 2 && wetReach[i] >= kUplandWater &&
				 moor[i] > kUplandCut &&
				 home.clear(i, kPlainHome))
		{
			mech[i] = SHALE;
			++counts.upland;
		}
	}

	// The looks, by crater type (composeLooks keeps each inside its rule group).
	TerrainSketch looks(size_t(n), 0);
	for (int i = 0; i < n; ++i)
	{
		generationCheckpoint();
		const bool second = split[i] > int(0.6 * 65536);
		switch (mech[i])
		{
		case GRASS:
			if (plain[i] || !home.clear(i, kPlainHome))
				break;
			if (wetReach[i] <= type.lush)
			{
				looks[i] = LUSH_GRASS;
				++counts.lush;
			}
			else if (wetReach[i] >= type.dry)
			{
				looks[i] = DRY_GRASS;
				++counts.dry;
			}
			break;
		case SAND:
			if (fromWater[i] <= 2)
				looks[i] = type.beach;
			else
			{
				looks[i] = second ? type.duneSecond : type.dune;
				++counts.dunes;
			}
			break;
		case DEEP_WATER:
			looks[i] = type.deep;
			break;
		case RIDGE_ROCK:
			looks[i] = type.rock;
			break;
		case SCREE:
			looks[i] = type.rough;
			break;
		case LOAM:
			looks[i] = type.loam;
			break;
		case SHALE:
			looks[i] = second ? type.uplandSecond : type.upland;
			break;
		default:
			break;
		}
	}

	// The exchanges: deposits the rim rock covers go; algae on a tile with a deep corner moves to the
	// nearest free shallows of its own lake.
	for (int i = 0; i < n; ++i)
	{
		if (!rims.rockTiles[i])
			continue;
		const int x = t.remainderX(i), y = i / t.w;
		if (map.isResource(x, y))
		{
			(map.getResource(x, y).type == STONE ? counts.stoneCleared : counts.cropsCleared)++;
			map.setNoResource(x, y, 0);
		}
	}
	std::vector<int> displaced(nb, 0);
	std::vector<unsigned char> deepTile(size_t(n), 0);
	for (int i = 0; i < n; ++i)
	{
		bool deep = false;
		forCorners(t, i, [&](int v) { deep = deep || core[v]; });
		deepTile[i] = deep;
		const int x = t.remainderX(i), y = i / t.w;
		if (deep && map.isResource(x, y) && map.getResource(x, y).type == ALGA)
		{
			map.setNoResource(x, y, 0);
			++displaced[size_t(std::max(0, body[i]))];
		}
	}
	const TerrainSketch finished = composeLooks(mech, looks);
	writeVertices(map, finished);
	{
		const std::vector<int> fromDeep = stepsFrom(t, deepTile);
		std::vector<std::vector<std::pair<long long, int>>> shallows(nb);
		for (int i = 0; i < n; ++i)
		{
			const int x = t.remainderX(i), y = i / t.w;
			if (deepTile[i] || body[i] < 0 || displaced[size_t(body[i])] == 0 || map.isResource(x, y) ||
				!map.isResourceAllowed(x, y, ALGA))
				continue;
			bool pure = true;
			forCorners(t, i, [&](int v) { pure = pure && finished[v] == WATER; });
			if (pure)
				shallows[size_t(body[i])].push_back({(long long)fromDeep[i] * 65536 + split[i], i});
		}
		for (int b = 0; b < bodies; ++b)
		{
			if (displaced[b] == 0)
				continue;
			std::sort(shallows[b].begin(), shallows[b].end());
			const int placed = std::min<int>(displaced[b], int(shallows[b].size()));
			for (int k = 0; k < placed; ++k)
				map.setResourceByIndex(t.remainderX(shallows[b][k].second), shallows[b][k].second / t.w, ALGA, 1);
			counts.algaeMoved += placed;
			counts.algaeLost += displaced[b] - placed;
		}
	}

	// The quarry faces' stone: one permanent deposit on every face tile.
	int quarryStone = 0;
	for (int i = 0; i < n; ++i)
		if (rims.quarry[i])
		{
			const int x = t.remainderX(i), y = i / t.w;
			if (map.isResource(x, y) && map.getResource(x, y).type != STONE)
			{
				++counts.cropsCleared;
				map.setNoResource(x, y, 0);
			}
			if (!map.isResource(x, y) && map.isResourceAllowed(x, y, STONE))
			{
				map.setResourceByIndex(x, y, STONE, 1);
				++quarryStone;
			}
		}
	// Every face tile must hold its stone: a face is on pure grass by construction, so one that cannot is
	// a defect, refused rather than drawn as a face with nothing in it.
	for (int i = 0; i < n; ++i)
		if (rims.quarry[i] && (!map.isResource(t.remainderX(i), i / t.w) ||
							   map.getResource(t.remainderX(i), i / t.w).type != STONE))
		{
			context.detail = "A quarry face at (" + std::to_string(t.remainderX(i)) + ", " + std::to_string(i / t.w) +
							 ") holds no stone.";
			return false;
		}

	context.telemetry.choice("crater-lakes.type", type.name);
	context.telemetry.choice("crater-lakes.deep-water", o.deep_water ? "on" : "off");
	context.telemetry.choice("crater-lakes.rims", o.crater_rims ? "on" : "off");
	context.telemetry.measure("crater-lakes.lakes", bodies);
	context.telemetry.measure("crater-lakes.deep.vertices", counts.deep);
	context.telemetry.measure("crater-lakes.algae.moved", counts.algaeMoved);
	context.telemetry.measure("crater-lakes.algae.lost", counts.algaeLost);
	context.telemetry.measure("crater-lakes.rim.lakes", rims.bodies);
	context.telemetry.measure("crater-lakes.rim.arcs", rims.arcs);
	context.telemetry.measure("crater-lakes.rim.dropped", rims.dropped);
	context.telemetry.measure("crater-lakes.rock.vertices", counts.rock);
	context.telemetry.measure("crater-lakes.rim.stone-cleared", counts.stoneCleared);
	context.telemetry.measure("crater-lakes.rim.crops-cleared", counts.cropsCleared);
	context.telemetry.measure("crater-lakes.quarry.wanted", rims.wanted);
	context.telemetry.measure("crater-lakes.quarry.faces", rims.faces);
	context.telemetry.measure("crater-lakes.quarry.tiles", rims.faceTiles);
	context.telemetry.measure("crater-lakes.quarry.stone", quarryStone);
	context.telemetry.measure("crater-lakes.talus.vertices", counts.talus);
	context.telemetry.measure("crater-lakes.loam.vertices", counts.loam);
	context.telemetry.measure("crater-lakes.upland.vertices", counts.upland);
	context.telemetry.measure("crater-lakes.looks.lush-vertices", counts.lush);
	context.telemetry.measure("crater-lakes.looks.dry-vertices", counts.dry);
	context.telemetry.measure("crater-lakes.looks.dune-vertices", counts.dunes);
	if (counts.algaeLost > 0)
		context.telemetry.fallback("crater-lakes.algae.shortfall", "Not enough free shallows for the displaced algae");
	if (o.crater_rims && rims.faces < rims.wanted)
		context.telemetry.fallback("crater-lakes.quarry.shortfall", "Not enough rim arcs with a grass flank away from the homes");
	if (context.telemetry.enabled() && rims.faces > 0)
	{
		// How far each colony walks to the nearest quarry face (-1 when none can be reached).
		const std::vector<unsigned char> walkable = walkableTiles(map);
		int least = INT_MAX, most = -1;
		for (int team = 0; team < context.request.nbTeams; ++team)
		{
			std::vector<unsigned char> swarm(size_t(n), 0);
			for (int dy = 0; dy < 4; ++dy)
				for (int dx = 0; dx < 4; ++dx)
					swarm[t.at(context.bootX[team] + dx, context.bootY[team] + dy)] = 1;
			const std::vector<int> steps = stepsFrom(t, swarm, walkable);
			int nearest = INT_MAX;
			for (int i = 0; i < n; ++i)
			{
				if (!rims.quarry[i])
					continue;
				for (const auto &step : kCardinalSteps)
				{
					const int s = steps[t.at(t.remainderX(i) + step[0], i / t.w + step[1])];
					if (s >= 0)
						nearest = std::min(nearest, s + 1);
				}
			}
			const int walk = nearest == INT_MAX ? -1 : nearest;
			context.telemetry.measure("crater-lakes.quarry.nearest-walk", walk, team);
			least = std::min(least, walk);
			most = std::max(most, walk);
		}
		if (least >= 0)
			context.telemetry.measure("crater-lakes.quarry.nearest-walk-spread", most - least);
	}
	return true;
}
} // namespace

static bool generate(Game &game, GenerationContext &context)
{
	const CraterLakesOptions options(context.request);
	const HeightFieldOptions terrain = HeightFieldOptions::fromRequest(context.request, false);
	if (!generateHeightField(
			game, context, terrain,
			[&](HeightMap &hm, unsigned w, unsigned h, float smoothing)
			{
				context.telemetry.measure("crater-lakes.bowls.requested",
										  w * h * options.lake_density / 30000);
				context.telemetry.measure("crater-lakes.bowl.radius", options.lake_size);
				hm.makeCraters(w * h * options.lake_density / 30000, options.lake_size, smoothing);
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
	// 2.0: everything above is revision 5, draw for draw; the crater pass repaints the ground nobody
	// stands on.
	context.stage = "craters";
	return craterPass(game, context, options);
}

// Nothing here designs an economy: the height field decides where land is and placeStarts picks
// the best spots it left. So the only promise this landscape can make is the floor every colony
// needs — wheat and wood it can walk to, and room for a first base — and the service rolls
// another seed when a field does not leave one. 2.0 adds the pass's own promises, read from the
// finished terrain by rule group: every deep vertex keeps kDeepSteps - 1 steps of water round it
// (the shallows), no rock, fertile or rough ground near a colony, and no rim parts the walkable
// ground round it.
static std::string validateWorld(const Game &game, const GenerationContext &context)
{
	const Map &map = game.map;
	const int teams = context.request.nbTeams;
	if (std::string floor = startingFloorFailure(map, teams); !floor.empty())
		return floor;
	const Torus t(map);
	const int n = t.size();
	const auto at = [&](int i) { return "(" + std::to_string(t.remainderX(i)) + ", " + std::to_string(i / t.w) + ")"; };
	std::vector<unsigned char> land(size_t(n), 0), ridges(size_t(n), 0);
	std::vector<TerrainGroup> group(static_cast<size_t>(n));
	for (int i = 0; i < n; ++i)
	{
		group[i] = terrainGroup(map.vertexTerrainAt(t.remainderX(i), i / t.w));
		land[i] = group[i] != TerrainGroup::Water && group[i] != TerrainGroup::DeepWater;
		ridges[i] = group[i] == TerrainGroup::Ridges;
	}
	const std::vector<int> fromLand = stepsFrom(t, land);
	for (int i = 0; i < n; ++i)
		if (group[i] == TerrainGroup::DeepWater && fromLand[i] < kDeepSteps - 1)
			return "Deep water at " + at(i) + " has no shallows between it and the shore.";
	std::vector<unsigned char> workers(size_t(n), 0);
	for (const auto &tiles : unitTilesByTeam(map, teams))
		for (int i : tiles)
			workers[i] = 1;
	// Straight-line distance from the nearest worker (-1 everywhere when there are none).
	const std::vector<std::int64_t> fromWorkers = distanceSquaredTo(t, workers);
	const auto near = [&](int i, double reach) { return fromWorkers[i] >= 0 && double(fromWorkers[i]) < reach * reach; };
	for (int i = 0; i < n; ++i)
	{
		if (group[i] == TerrainGroup::Fertile && near(i, kGardenCheck))
			return "Fertile ground at " + at(i) + " lies too near a colony.";
		if (group[i] == TerrainGroup::Rough && near(i, kGardenCheck))
			return "Rough ground at " + at(i) + " lies too near a colony.";
	}
	std::vector<unsigned char> walkable(size_t(n), 0), rock(size_t(n), 0);
	for (int y = 0; y < t.h; ++y)
		for (int x = 0; x < t.w; ++x)
		{
			const int i = t.at(x, y);
			const TerrainProperties &rules = map.terrainPropertiesAt(x, y);
			walkable[i] = rules.walkable;
			const bool corner = ridges[i] || ridges[t.at(x + 1, y)] || ridges[t.at(x, y + 1)] || ridges[t.at(x + 1, y + 1)];
			if (corner && near(i, kRockHomeCheck))
				return "Rock at " + at(i) + " stands too near a colony.";
			rock[i] = corner && !rules.walkable && !rules.swimmable;
		}
	std::vector<int> label;
	if (const int parting = partingGroup(t, walkable, rock, label); parting >= 0)
		for (int i = 0; i < n; ++i)
			if (label[i] == parting)
				return "Rock at " + at(i) + " cuts ground off.";
	return "";
}

GeneratorDefinition craterLakesDefinition()
{
	std::vector<GeneratorControl> controls{
		GeneratorControl{"water", "Water weight", 0, 100, 1, 25, ControlGroup::Terrain, false, true}
			.withSearchRange(15, 35),
		GeneratorControl{"sand", "Sand weight", 0, 100, 1, 3, ControlGroup::Terrain, false, true}
			.withSearchRange(1, 6),
		GeneratorControl{"grass", "Grass weight", 0, 100, 1, 75, ControlGroup::Terrain, false, true}
			.withSearchRange(60, 90),
		GeneratorControl{"desert", "Desert weight", 0, 100, 1, 0, ControlGroup::Terrain, false,
						 true}
			.withSearchRange(0, 5),
		GeneratorControl{"smoothing", "Smoothing", 1, 8, 1, 6, ControlGroup::Terrain, false}
			.withSearchRange(4, 7),
		GeneratorControl{"lake-density", "Lake density", 10, 50, 5, 25, ControlGroup::Terrain,
						 false}
			.withSearchRange(15, 35),
		GeneratorControl{"lake-size", "Lake size", 10, 40, 5, 25, ControlGroup::Terrain, false}
			.withSearchRange(15, 30),
		// Volcanic maars: dense rims of dark rock, ash and pumice slopes, loam. Glacial kettles: sparse
		// thin rims of outcrop, tundra and scree slopes, moss, dark water. Rim density and looks.
		GeneratorControl::choice("crater-type", "Crater type", {"Random", "Volcanic maars", "Glacial kettles"}, 0,
								 ControlGroup::Terrain)
			.withSearchValues({1, 2}),
		// On, the bigger lakes have a core of deep water: slower to swim, no algae, two tiles of
		// shallows along every shore, where the algae are. Routes on foot are the same either way.
		GeneratorControl::toggle("deep-water", "Deep water", true, ControlGroup::Terrain)
			.withSearchValues({0, 1}),
		// On, broken arcs of rock stand on the bigger lakes' rims, never a closed ring and never near
		// a home, some with a quarry face of stone (how many follows the stone amount).
		GeneratorControl::toggle("crater-rims", "Crater rims", true, ControlGroup::Layout)
			.withSearchValues({0, 1}),
		GeneratorControl{"fruit", "Fruit", 0, 64, 1, 4, ControlGroup::Resources, false}
			.withSearchRange(2, 8)};
	for (auto &c : heightFieldResourceControls())
		controls.push_back(std::move(c));
	controls.push_back(
		GeneratorControl{"repeat", "Repeat landscape", 0, 5, 1, 0, ControlGroup::Layout, true}
			.withSearchRange(0, 1));
	return {
			"crater-lakes",
			4,
			"Crater lakes",
			6,
			false,
			std::move(controls),
			generate,
			true,
			nullptr,
			validateWorld,
			{"terrain:natural", "feature:lakes", "style:wide-open"}};
}
