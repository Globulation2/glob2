// SPDX-License-Identifier: GPL-3.0-or-later
#include "PowerOfTwo.h"
#include "RingWorldGenerator.h"
#include "GenerationWorld.h"
#include "GenerationContext.h"
#include "Grid.h"
#include "Geometry.h"
#include "LatticeNoise.h"
#include "Morphology.h"
#include "Pipeline.h"
#include "Planting.h"
#include "ResourceSemantics.h"
#include "Resources.h"
#include "Roads.h"
#include "Rock.h"
#include "Settlements.h"
#include "Sketch.h"
#include "TerrainGroup.h"
#include "Topology.h"
#include "Unit.h"
#include <algorithm>
#include <array>
#include <climits>
#include <cmath>
#include <cstdint>
#include <deque>
#include <limits>
#include <string>
#include <utility>
#include <vector>
using namespace MapGeneration;

// One continental belt wraps all the way around the map along its longer axis, and the ocean on
// either side of it meets itself across the other axis's wrap. There are no corners and no back
// line: every colony has exactly two land neighbours along the belt.
//
// Everything that shapes the belt is periodic in the map's own size - whole-number harmonics for
// its centre line and width, lattice noise whose cells tile the torus for its coastline - so the
// seam can't be seen. Terrain is written straight to the map's vertices after an order-independent
// beach pass that, unlike Map::layBeaches(), leaves the water whole.
//
// WHY IT PLAYS WELL (docs/map-generators/game-rules-for-map-design.md). Every colony has the same
// situation: one neighbour each way along the belt, sea behind it, and colonies alternating between
// the two coasts by default so neighbours are not simply lined up on one shore. Contact is along
// one axis, so a colony can concentrate its defence on two fronts, and the sea at its back is a
// timer rather than a wall: once it can swim, a colony can raid across the ocean or take the
// resource islands, which ground units can never reach. Every home stands 6 tiles from the water,
// so each start has the same fertile shore within reach (wheat and wood regrow only near water).
// Lakes break up the interior with more shoreline to farm, and the last step clears a walkable
// route all the way round the ring, so deposits never start the game cutting the belt in two.
//
// THE SIZES AT THE DEFAULTS (256x256, belt 45%, roughness 50): the belt averages 115 tiles across,
// its centre line winds up to 28 tiles either way, its width swings up to 13 tiles and its coast
// wanders up to 26 more; the ocean between the coasts is at least 31 tiles.
//
// RING WORLD 2.0 (revision 4): the same belt, drawn with the engine's full terrain palette. The
// design brief: seen from the air, a continent that wraps the world reads by (1) a dark open ocean
// on both sides, paler along every coast; (2) a broken mountain spine down the middle of the land,
// the divide between its two coasts, crossed by passes; (3) a road that runs right round the world
// from town to town, crossing the spine at the passes; (4) coasts with character: rocky headlands,
// muddy bays, and green fertile rings round the lakes. Revision 3's stamp (the belt, lakes,
// islands, beaches) and its colony sites are unchanged for the same seed; over them:
// - Two sketches. The design keeps a mechanic sketch, the canonical type of each rule group (GRASS,
//   SAND, WATER, DEEP_WATER, LOAM, MUDFLAT, SCREE, TRAIL, BOULDERS, RIDGE_ROCK) that every
//   shared helper and check reads, and paints looks over it with composeLooks (Sketch.h) at the very
//   end, after every resource stage, so looks never change a rule or what a helper saw. The 2.0
//   features are planned from the stamp and the swarms alone, so validateWorld plans them again.
// - Deep ocean (`deep-water`): ocean vertices kDeepSteps or more tiles from any land in a straight
//   line are deep water: swimmable but slower, no algae, still waters the land. The coastal shallows keep the
//   beaches and the algae band; swimming across the ocean to the other coast or an island is slower.
// - The rock spine (`rock-spine`): a broken band of rock (Ridges: impassable, towers shoot over it)
//   along the belt's dry centre line, cut into segments of 16 to 30 tiles with passes of 5 to 8, and
//   cut again wherever the ring road crosses it and round every home. Every core is at least two
//   tiles thick (rockVertices) and stands kSpineDry steps from any water, so it never meets a lake or
//   a coast and can never part the belt; a light scree apron skirts it far from water.
// - A quarry tor per colony: one small outcrop of the spine's rock, stamped from one stencil turned
//   by quarter turns to face its colony, with a five-tile quarry face (stone on grass, left out of the
//   rock, a ring of grass before it) kTorWalk steps' walk from the swarm. Every colony's face holds
//   the same stone at the same walk (telemetry: ring-world.quarry.nearest-walk); if any colony's tor
//   fits nowhere, no colony gets one.
// - The ring road (`ring-road`): trail (Paths: twice the walking speed, buildable, holds nothing, so
//   no crop ever grows on it or across it) from each colony's doorstep to the next all the way round
//   the world: the cheapest walk that keeps off the wet farmland, laid before any deposit. Being
//   permanent, it keeps the belt's loop open whatever grows; openBeltRoad stays as the backstop.
// - Coasts: low shores in the bays are mudflat (Rough: slow, unbuildable, holds nothing, and unlike
//   sand it does not inhibit growth) and the tips of headlands are boulders (Obstacles), never within
//   kCoastKeep of a home. Every lake's beach is loam (Fertile: buildable crop land that waters its
//   surroundings), since sand there only held growth back, inside the home gardens too (a lakeside
//   home's kit then takes the wettest ground beside that loam); outside the gardens a frayed ring of
//   loam about kLoamDeep vertices deep surrounds it, and the gardens keep plain grass beyond the beach
//   so a lakeside home and an inland one start on comparable ground.
// - Islands themed: a stone island is volcanic (a pumice beach, Rough, round grass carrying its stone);
//   fruit and grain islands are lush green.
// - A climate, looks only (`climate`: temperate, steppe, tropical): grass graded by distance from
//   water, the beach, deep water, loam, rock, road, mud and headland looks. Every choice is in
//   telemetry.
namespace
{

// Belt tiles along the ring each colony needs; validateRequest rejects crowding below this.
constexpr int kBeltPerColony = 24;
// A dry spine this many tiles either side of the centre line is never coast or lake, so the belt
// is always one landmass that closes on itself. The coast's budget is measured from it.
constexpr double kSpineHalf = 5.0;
// Half the narrowest ocean allowed between the belt's two coasts, measured across the wrap (or 6%
// of the map's breadth, if that is more).
constexpr double kOceanHalf = 5.0;
// Steepest the belt's centre line and half-width may change per tile along it.
constexpr double kMaxBendSlope = 0.45;
constexpr double kMaxSwingSlope = 0.3;
// Land kept between a lake and the ocean and between two lakes; water kept around an island.
constexpr int kLakeShore = 5;
constexpr int kLakeGap = 5;
constexpr int kIslandMoat = 7;
// Every swarm stands this many tiles from its nearest water, so every start has the same fertile
// shore within reach.
constexpr int kHomeShore = 6;
// Every home starts identical: fixed tile counts, wheat and wood 1:1 as for any guaranteed
// placement, outside a clear ring this wide that keeps the swarm's workers free to walk out.
constexpr int kHomeWheat = 16;
constexpr int kHomeWood = 16;
constexpr int kHomeStone = 10;

constexpr int kUnreached = INT_MIN;

// --- 2.0 ----------------------------------------------------------------------------------------
//
// Streams of their own, so every draw revision 3 made is still the draw it made.
constexpr const char *kSpineStream = "ring-world-spine";
constexpr const char *kRoadStream = "ring-world-road";
constexpr const char *kCoastStream = "ring-world-coast";
constexpr const char *kLookStream = "ring-world-looks";
constexpr const char *kClimateStream = "ring-world-climate";
constexpr const char *kIsleStream = "ring-world-isles";
// Deep ocean: ocean vertices at least kDeepSteps tiles from land in a straight line, opened by kDeepOpen
// and only in bodies of kDeepLeast vertices or more, so no lone dark vertex reads as a hole.
constexpr int kDeepSteps = 6, kDeepOpen = 1, kDeepLeast = 12;
// The spine: rock runs of kSpineRunLeast plus up to kSpineRunExtra tiles along the belt, passes of
// kSpinePassLeast plus up to kSpinePassExtra between them; half a tile to a tile thicker than
// kSpineHalfThick each side of a centre line wandering kSpineWander tiles off the belt's own. No rock
// within kSpineDry steps of water, within kSpineHomeClear tiles of a swarm, or within kSpineRoadClear
// of the road or a tor. Scree skirts it up to kTalusOuter tiles out, never within kTalusDry of water.
constexpr int kSpineRunLeast = 16, kSpineRunExtra = 15, kSpinePassLeast = 5, kSpinePassExtra = 4;
constexpr double kSpineHalfThick = 1.8, kSpineThickSwing = 1.0, kSpineWander = 3.0;
constexpr int kSpineDry = 8, kSpineRoadClear = 3, kSpineLeast = 10, kTalusOuter = 2, kTalusDry = 12;
constexpr double kSpineHomeClear = 16;
// The quarry tors: the face's front row kTorWalk steps' walk from the swarm, or the nearest walk
// between kTorWalkLeast and kTorWalkMost every colony can have; every tile of the stencil and
// kTorClear round it on plain grass, at least kTorDry steps from water and kTorRoadClear from the
// road and any home's ground.
constexpr int kTorWalk = 16, kTorClear = 1, kTorDry = 3, kTorRoadClear = 2;
constexpr double kTorSpineClear = 10;
// Nothing planted within kTorOpen tiles of a tor, and the scatter cleared that far before its face.
constexpr int kTorOpen = 3;
constexpr int kTorWalkLeast = 8, kTorWalkMost = 32;
constexpr int kTorFace = 5; // stone tiles in every face
// The road keeps off ground fewer than kRoadWet steps from water (each step nearer costs
// kRoadWetCost) and kRoadHomeClear tiles round a colony's ground (kRoadHomeCost), and its doorstep
// is kRoadDoorstep tiles outside its own colony's ground: a road two tiles out split one colony's
// building room in a V and its AI stalled (calibration seed 101).
constexpr int kRoadWet = 10, kRoadWetCost = 3, kRoadHomeClear = 8, kRoadHomeCost = 40, kRoadDoorstep = 5;
// Coasts: a shore vertex is a headland when more than kHeadland of the disc of kCoastProbe tiles
// round it is ocean, a bay below kBay; a noise picks kHeadlandShare / kBayShare of them. Nothing
// within kCoastKeep tiles of a swarm's middle, which is home ground.
constexpr int kCoastProbe = 6;
constexpr double kHeadland = 0.56, kBay = 0.42, kHeadlandGate = 0.5, kBayGate = 0.5, kCoastKeep = 18;
// Lakes: grass and beach vertices within kLoamDeep steps of a lake are loam, outside the home
// gardens (kGardenRadius tiles round a swarm's middle, frayed by kGardenFray).
constexpr int kLoamDeep = 5;
constexpr double kGardenRadius = 12, kGardenFray = 3;

// The look-only climates. lush and green are the steps from water within which the grass is lush and
// still green; beyond green it is dry.
struct Climate
{
	const char *name;
	TerrainType beach, deep, loam, loamSecond, rock, road, mud, headland;
	int lush, green;
};
constexpr Climate kClimates[3] = {
	{"temperate", PALE_SAND, DEEP_WATER, SPRING_MEADOW, MOSS, RIDGE_ROCK, TRAIL, MUDFLAT, GRANITE_TORS, 8, 26},
	{"steppe", SAND, DEEP_WATER, LOAM, SPRING_MEADOW, HOODOOS, SANDSTONE, HARDPAN, SANDSTONE_BENCHES, 5, 17},
	{"tropical", SAND, DARK_WATER, MOSS, LOAM, OUTCROP, OVERGROWN_ROAD, MUD, BOULDERS, 14, 48}};

double unitDraw(std::mt19937 &rng)
{
	return rng() / 4294967296.0;
}

// Belt coordinates (Grid.h's Axes): u runs along the belt and v across it. The belt follows the
// map's longer axis, so a tall map gets one that wraps top to bottom.

// A smooth closed curve along the belt, one sample per tile. Its harmonics are whole numbers of
// cycles per map length, so it meets itself exactly at the seam. Normalised to [-1, 1]; `slope`
// receives its steepest step between neighbouring samples.
std::vector<double> closedCurve(int length, double falloff, std::mt19937 &rng, double &slope)
{
	// One harmonic per 40 tiles of ring (2 to 10), each weighted 30% to 100% at random and divided
	// by (k + 1) to the falloff: the centre line uses falloff 1.6, so it is dominated by a few long
	// bends, and the width uses 1.2, so it varies on shorter stretches too.
	const int harmonics = std::clamp(length / 40, 2, 10);
	std::vector<double> amplitude(harmonics), phase(harmonics);
	for (int k = 0; k < harmonics; ++k)
	{
		amplitude[k] = (0.3 + 0.7 * unitDraw(rng)) / std::pow(k + 1.0, falloff);
		phase[k] = 2 * kPi * unitDraw(rng);
	}
	std::vector<double> curve(length, 0.0);
	double peak = 0;
	for (int u = 0; u < length; ++u)
	{
		for (int k = 0; k < harmonics; ++k)
			curve[u] += amplitude[k] * std::sin(2 * kPi * (k + 1) * u / length + phase[k]);
		peak = std::max(peak, std::abs(curve[u]));
	}
	slope = 0;
	if (peak <= 0)
		return curve;
	for (double &sample : curve)
		sample /= peak;
	for (int u = 0; u < length; ++u)
		slope = std::max(slope, std::abs(curve[(u + 1) % length] - curve[u]));
	return curve;
}

struct Belt
{
	Axes axes;
	std::vector<double> centre, halfWidth; // per u
	std::vector<float> coast;              // per tile, in [-1, 1]
	double roughness;                      // coast displacement, in tiles, where |coast| is 1

	// Signed distance across the belt from its centre line, wrapped into [-breadth/2, breadth/2].
	double across(int x, int y) const
	{
		return std::remainder(axes.v(x, y) - centre[axes.u(x, y)], double(axes.breadth()));
	}
	bool land(int x, int y) const
	{
		return std::abs(across(x, y)) <
			   halfWidth[axes.u(x, y)] + roughness * coast[size_t(y) * axes.width + x];
	}
};

// The belt's centre line bends and its half-width swings along its length; the coast is roughened
// on top of that. Swing and roughness share one budget: the coast may bulge out until the ocean is
// at its narrowest and pinch in until it reaches the spine, never further, so the belt can neither
// break nor swallow the ocean whatever the controls say.
Belt shapeBelt(const Axes &axes, GenerationContext &context, const RingWorldOptions &options)
{
	std::mt19937 &rng = context.stream("belt");
	const int length = axes.length();
	const double breadth = axes.breadth();
	const double meanHalf = options.beltWidth / 100.0 * breadth / 2;
	const double oceanHalf = std::max(kOceanHalf, 0.06 * breadth);
	const double budget =
		std::max(0.0, std::min(breadth / 2 - oceanHalf - meanHalf, meanHalf - kSpineHalf - 1));
	// The width swings by 22% of the half-width and the coast's roughness adds up to 90% of it at
	// roughness 100. If the two together would push a coast past the spine or into the opposite
	// coast's ocean, both shrink in proportion, which is what lets every control be pushed to its
	// end without breaking the ring.
	double swing = 0.22 * meanHalf;
	double roughness = options.coastRoughness / 100.0 * 0.9 * meanHalf;
	if (swing + roughness > budget)
	{
		const double shrink = budget / (swing + roughness);
		swing *= shrink;
		roughness *= shrink;
	}
	double bendSlope = 0, swingSlope = 0;
	const std::vector<double> bend = closedCurve(length, 1.6, rng, bendSlope);
	const std::vector<double> widthCurve = closedCurve(length, 1.2, rng, swingSlope);
	// A belt that doesn't wind still draws its curve, so the rest of the belt is unchanged.
	// The centre line winds up to 11% of the breadth either way, capped so it never leans more than
	// kMaxBendSlope per tile: a steeper lean would pinch the belt where it turns.
	double bendAmplitude = options.windingBelt ? 0.11 * breadth : 0.0;
	if (bendSlope > 0)
		bendAmplitude = std::min(bendAmplitude, kMaxBendSlope / bendSlope);
	if (swingSlope > 0)
		swing = std::min(swing, kMaxSwingSlope / swingSlope);

	context.telemetry.measure("ring-world.coast.displacement-budget", budget);
	context.telemetry.measure("ring-world.coast.roughness-fitted", roughness);
	context.telemetry.measure("ring-world.width.swing-fitted", swing);
	context.telemetry.measure("ring-world.bend.amplitude-fitted", bendAmplitude);
	Belt belt{axes, std::vector<double>(length), std::vector<double>(length),
			  torusNoise(axes.width, axes.height, rng), roughness};
	for (int u = 0; u < length; ++u)
	{
		belt.centre[u] = breadth / 2 + bendAmplitude * bend[u];
		belt.halfWidth[u] = meanHalf + swing * widthCurve[u];
	}
	return belt;
}

// Inland lakes for fertility. Each keeps kLakeShore tiles of land to the ocean, kLakeGap to other
// lakes, and stays clear of the spine, so no lake or chain of lakes can cut the belt. Density is
// lakes per 4096 tiles of belt.
void carveLakes(std::vector<unsigned char> &terrain, const Belt &belt, GenerationContext &context,
				int density)
{
	const int width = belt.axes.width, height = belt.axes.height;
	const size_t area = size_t(width) * height;
	std::vector<unsigned char> water(area), land(area);
	std::vector<int> dry;
	for (size_t i = 0; i < area; ++i)
	{
		water[i] = terrain[i] == WATER;
		land[i] = !water[i];
		if (land[i])
			dry.push_back(int(i));
	}
	if (density <= 0 || dry.empty())
		return;
	const std::vector<int> shore = stepsFrom(Torus{width, height}, water, land);
	// Lakes per 4096 tiles of belt: the default 2 gives about 14 on a 256 map. Each is 4 to 8 tiles
	// in radius, grown with the square root of the map's breadth over 128 (0.8 to 1.6 times), so
	// lakes stay in proportion on bigger maps without becoming seas; 40 tries each, and a lake that
	// fits nowhere is skipped.
	const int wanted = int(std::lround(density * double(dry.size()) / 4096.0));
	const double scale = std::clamp(std::sqrt(belt.axes.breadth() / 128.0), 0.8, 1.6);
	struct Lake
	{
		int x, y;
		double reach;
	};
	std::vector<Lake> lakes;
	for (int attempt = 0; int(lakes.size()) < wanted && attempt < wanted * 40; ++attempt)
	{
		const int at = dry[context.bounded("lakes", dry.size())];
		const int x = powerOfTwoRemainder(at, width), y = at / width;
		const RadialShape shape((4 + context.bounded("lakes", 5)) * scale, 0.35, context, "lakes");
		const double reach = shape.maximumRadius();
		// The centre line leans at most kMaxBendSlope per tile, so a lake's far side can sit that
		// much nearer the spine than its centre.
		if (shore[at] < reach + kLakeShore ||
			std::abs(belt.across(x, y)) < kSpineHalf + (1 + kMaxBendSlope) * reach + 1)
			continue;
		bool clear = true;
		for (const Lake &other : lakes)
		{
			const double gap = reach + other.reach + kLakeGap;
			clear = clear && Torus{width, height}.dist2(x, y, other.x, other.y) >= gap * gap;
		}
		if (!clear)
			continue;
		const int r = int(std::ceil(reach));
		for (int dy = -r; dy <= r; ++dy)
			for (int dx = -r; dx <= r; ++dx)
				if (std::hypot(double(dx), double(dy)) <
					shape.radiusAt(std::atan2(double(dy), double(dx))))
					terrain[size_t(powerOfTwoRemainder(y + dy + height, height)) * width + powerOfTwoRemainder(x + dx + width, width)] =
						WATER;
		lakes.push_back({x, y, reach});
	}
	context.telemetry.measure("ring-world.lakes.target", wanted);
	context.telemetry.measure("ring-world.lakes.actual", lakes.size());
	if (int(lakes.size()) < wanted)
		context.telemetry.fallback("ring-world.lakes.omitted",
								   "Candidate budget or clearance limited inland lakes.");
}

// Revision 3's terrain, planned from the request alone so validateWorld can plan it again: the belt,
// its lakes and islands with their beaches, and (2.0) the deep ocean core.
struct Stamp
{
	Belt belt;
	TerrainSketch terrain;
	std::vector<unsigned char> ocean, lake; // water vertices of the ocean and of the lakes
	std::vector<Island> islands;
	int wantedIslands = 0, deep = 0;
};

Stamp stampRing(const Axes &axes, GenerationContext &context, const RingWorldOptions &options)
{
	const int width = axes.width, height = axes.height;
	const Torus t{width, height};
	const size_t area = size_t(width) * height;
	Stamp stamp{shapeBelt(axes, context, options), TerrainSketch(area, WATER), {}, {}, {}, 0, 0};
	TerrainSketch &terrain = stamp.terrain;
	for (int y = 0; y < height; ++y)
		for (int x = 0; x < width; ++x)
			if (stamp.belt.land(x, y))
				terrain[size_t(y) * width + x] = GRASS;
	std::vector<unsigned char> sea(area);
	for (size_t i = 0; i < area; ++i)
		sea[i] = terrain[i] == WATER;
	carveLakes(terrain, stamp.belt, context, options.lakeDensity);
	// The control counts islands per 128x128 of map, never fewer than one when it is on at all.
	stamp.wantedIslands =
		options.resourceIslands > 0
			? std::max(1, int(std::lround(options.resourceIslands * double(t.size()) / 16384.0)))
			: 0;
	// 40 candidate draws per island; each keeps 7 tiles of open water from every coast, too wide
	// for any beach to bridge, so islands are reached only by swimming.
	stamp.islands = raiseIslands(terrain, t, context, {"islands", stamp.wantedIslands, 40, kIslandMoat});
	layBeaches(terrain, t);
	stamp.ocean.assign(area, 0);
	stamp.lake.assign(area, 0);
	for (size_t i = 0; i < area; ++i)
	{
		stamp.ocean[i] = terrain[i] == WATER && sea[i];
		stamp.lake[i] = terrain[i] == WATER && !sea[i];
	}
	if (options.deepWater)
	{
		// Straight-line distance from land: deepCore's eight-neighbour steps drew the edge as the union
		// of squares round every land vertex, which stepped like stairs along every diagonal coast.
		std::vector<unsigned char> land(area, 0);
		for (size_t i = 0; i < area; ++i)
			land[i] = !stamp.ocean[i];
		const std::vector<std::int64_t> fromLand = distanceSquaredTo(t, land);
		std::vector<unsigned char> core(area, 0);
		for (size_t i = 0; i < area; ++i)
			core[i] = stamp.ocean[i] && fromLand[i] >= std::int64_t(kDeepSteps) * kDeepSteps;
		const std::vector<unsigned char> grown = dilateRound(t, erode(t, core, kDeepOpen), kDeepOpen);
		for (size_t i = 0; i < area; ++i)
			core[i] = core[i] && grown[i];
		core = dropSmallRegions(t, core, kDeepLeast);
		for (size_t i = 0; i < area; ++i)
			if (core[i])
			{
				terrain[i] = DEEP_WATER;
				++stamp.deep;
			}
	}
	return stamp;
}

// The tiles whose four corner vertices all pass `test`, and those with at least one that does.
template <typename Test>
std::vector<unsigned char> tilesAll(const TerrainSketch &v, const Torus &t, Test test)
{
	std::vector<unsigned char> out(v.size(), 0);
	for (int y = 0; y < t.h; ++y)
		for (int x = 0; x < t.w; ++x)
			out[size_t(t.at(x, y))] = test(v[t.at(x, y)]) && test(v[t.at(x + 1, y)]) &&
									  test(v[t.at(x, y + 1)]) && test(v[t.at(x + 1, y + 1)]);
	return out;
}
template <typename Test>
std::vector<unsigned char> tilesAny(const TerrainSketch &v, const Torus &t, Test test)
{
	std::vector<unsigned char> out(v.size(), 0);
	for (int y = 0; y < t.h; ++y)
		for (int x = 0; x < t.w; ++x)
			out[size_t(t.at(x, y))] = test(v[t.at(x, y)]) || test(v[t.at(x + 1, y)]) ||
									  test(v[t.at(x, y + 1)]) || test(v[t.at(x + 1, y + 1)]);
	return out;
}
bool wetType(unsigned char v)
{
	return v == WATER || v == DEEP_WATER;
}

// Keeps only the tiles of `mask` that lie in some 2x2 block of it, so every kept tile has a rock vertex.
std::vector<unsigned char> blocksOfTwo(const Torus &t, const std::vector<unsigned char> &mask)
{
	std::vector<unsigned char> out(mask.size(), 0);
	for (int y = 0; y < t.h; ++y)
		for (int x = 0; x < t.w; ++x)
			if (mask[t.at(x, y)] && mask[t.at(x + 1, y)] && mask[t.at(x, y + 1)] && mask[t.at(x + 1, y + 1)])
				for (int dy = 0; dy <= 1; ++dy)
					for (int dx = 0; dx <= 1; ++dx)
						out[t.at(x + dx, y + dy)] = 1;
	return out;
}

// Whether some blocked group of `blocked` (8-connected) parts the ground round it: the open tiles
// beside it fall in more than one walkable component. Returns the first such group's label, or -1;
// `label` receives the groups.
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

// The 2.0 features, planned from the stamp and the colonies' swarms alone, so validateWorld can plan
// them again from the finished map's start positions. Tile masks unless named vertices.
struct Features
{
	std::vector<unsigned char> route;  // the ring road's walk
	std::vector<unsigned char> spine;  // the spine's rock tiles
	std::vector<unsigned char> tor;    // the tors' rock tiles
	std::vector<unsigned char> quarry; // the tors' quarry faces: stone on grass
	std::vector<int> owner;            // the colony each quarry tile belongs to, -1 elsewhere
	std::vector<unsigned char> keep;   // vertices round every tor that stay plain grass
	TerrainSketch mechanics;           // the finished mechanic sketch
	std::vector<unsigned char> volcanic; // vertices of volcanic islands. pumice beaches
	std::vector<int> isleTheme;        // per island: 0 stone (volcanic), 1 fruit, 2 wheat
	int roadTiles = 0, segments = 0, spineTiles = 0, tors = 0, dropped = 0, torBand = -1, torMiss = -1;
	int mud = 0, headlands = 0, loam = 0, scree = 0, ash = 0;
	bool roadOmitted = false, torsOmitted = false;
	std::string failure; // set when the features cannot be laid without cutting ground off
};

// The swarms' corners and the ground each colony's settlement stands on (the 10x10 square round its
// 4x4 swarm), as revision 3 drew it.
struct Home
{
	int x, y; // the swarm's top-left tile
	double cx() const { return x + 2.0; }
	double cy() const { return y + 2.0; }
};
std::vector<unsigned char> homeGround(const Torus &t, const std::vector<Home> &homes, int margin)
{
	std::vector<unsigned char> ground(size_t(t.size()), 0);
	for (const Home &home : homes)
		for (int dy = -3 - margin; dy <= 6 + margin; ++dy)
			for (int dx = -3 - margin; dx <= 6 + margin; ++dx)
				ground[t.at(home.x + dx, home.y + dy)] = 1;
	return ground;
}
double nearestHome(const Torus &t, const std::vector<Home> &homes, int x, int y)
{
	double best = 1e9;
	for (const Home &home : homes)
	{
		const double dx = t.offsetX(int(home.cx()), x), dy = t.offsetY(int(home.cy()), y);
		best = std::min(best, std::sqrt(dx * dx + dy * dy));
	}
	return best;
}

// The ring road: from each colony's doorstep to the next one's along the belt, round the world.
// Waypoints between colonies more than a quarter of the ring apart keep every walk going forwards.
void planRoad(Features &f, const Stamp &stamp, const Torus &t, const Axes &axes,
			  const std::vector<Home> &homes, const std::vector<int> &fromWater, GenerationContext &context)
{
	const int n = t.size(), length = axes.length();
	const std::vector<unsigned char> dry = tilesAll(stamp.terrain, t, [](unsigned char v) { return !wetType(v); });
	const std::vector<unsigned char> ground = homeGround(t, homes, kRoadDoorstep - 1), nearby = homeGround(t, homes, kRoadHomeClear);
	const std::vector<int> relief = fractalNoise(t.w, t.h, 16, 3, context.stream(kRoadStream));
	// Never within two tiles of a tor's rock or face: a road corner there would spoil the face's grass.
	std::vector<unsigned char> torAll(size_t(n), 0);
	for (int i = 0; i < n; ++i)
		torAll[i] = f.tor[i] || f.quarry[i];
	const std::vector<unsigned char> torNear = dilate(t, torAll, 2);
	const auto open = [&](int i) { return dry[i] && !ground[i] && !torNear[i]; };
	// Doorsteps: the tile kRoadDoorstep outside the colony's ground furthest from water, so the road
	// leaves the building room round the swarm whole.
	std::vector<std::pair<double, int>> stops;
	for (const Home &home : homes)
	{
		int best = -1;
		for (int dy = -3 - kRoadDoorstep; dy <= 6 + kRoadDoorstep; ++dy)
			for (int dx = -3 - kRoadDoorstep; dx <= 6 + kRoadDoorstep; ++dx)
			{
				if (std::max(std::abs(dx - 1.5), std::abs(dy - 1.5)) < 4.5 + kRoadDoorstep)
					continue;
				const int i = t.at(home.x + dx, home.y + dy);
				if (open(i) && (best < 0 || fromWater[i] > fromWater[best]))
					best = i;
			}
		if (best < 0)
		{
			f.roadOmitted = true;
			return;
		}
		stops.emplace_back(std::fmod(axes.uOf(best) + 0.0, double(length)), best);
	}
	std::sort(stops.begin(), stops.end());
	// Waypoints: no two consecutive points more than a quarter of the ring apart.
	std::vector<int> points;
	const int count = int(stops.size());
	for (int k = 0; k < count; ++k)
	{
		const int a = stops[k].second, b = stops[(k + 1) % count].second;
		points.push_back(a);
		double gap = std::fmod(stops[(k + 1) % count].first - stops[k].first + length, double(length));
		if (gap <= 0)
			gap = length;
		const int between = int(std::ceil(gap / (length / 4.0))) - 1;
		const double acrossA = stamp.belt.across(t.remainderX(a), a / t.w);
		const double acrossB = stamp.belt.across(t.remainderX(b), b / t.w);
		for (int j = 1; j <= between; ++j)
		{
			const double share = double(j) / (between + 1);
			const int u = int(std::lround(stops[k].first + gap * share)) % length;
			const double v = stamp.belt.centre[size_t(u)] + acrossA + (acrossB - acrossA) * share;
			const int target = axes.at(u, int(std::lround(v)));
			int best = -1;
			for (int r = 0; r <= 30 && best < 0; ++r)
				for (int dy = -r; dy <= r; ++dy)
					for (int dx = -r; dx <= r; ++dx)
					{
						if (std::max(std::abs(dx), std::abs(dy)) != r)
							continue;
						const int i = t.at(t.remainderX(target) + dx, target / t.w + dy);
						if (open(i) && fromWater[i] >= 3 && !nearby[i] && (best < 0 || i < best))
							best = i;
					}
			if (best < 0)
			{
				f.roadOmitted = true;
				return;
			}
			points.push_back(best);
		}
	}
	f.route.assign(size_t(n), 0);
	for (size_t k = 0; k < points.size(); ++k)
	{
		const int from = points[k], to = points[(k + 1) % points.size()];
		const std::vector<int> walk = cheapestWalk(
			t, GridNeighbors::Eight, {from}, tileMask(t, {to}),
			[&](int, int next, int dx, int dy)
			{
				if (!open(next))
					return -1;
				return (dx && dy ? 14 : 10) + std::max(0, kRoadWet - fromWater[next]) * kRoadWetCost +
					   relief[next] / 4096 + (nearby[next] ? kRoadHomeCost : 0);
			});
		if (walk.empty())
		{
			f.roadOmitted = true;
			f.route.assign(size_t(n), 0);
			return;
		}
		for (int i : walk)
			f.route[i] = 1;
	}
	f.roadTiles = int(std::count(f.route.begin(), f.route.end(), 1));
}

// One quarry tor, in its stencil's frame: b points at the colony, a runs across. Rock rows b = -4 to
// 0 and the face (b = 0, |a| <= 2) left out of it; the face's front row is b = 1.
struct TorCell
{
	int a, b;
	bool face;
};
std::vector<TorCell> torStencil()
{
	std::vector<TorCell> cells;
	const int halfWidth[5] = {2, 4, 5, 5, 5}; // rows b = -4 .. 0
	for (int b = -4; b <= 0; ++b)
		for (int a = -halfWidth[b + 4]; a <= halfWidth[b + 4]; ++a)
			cells.push_back({a, b, b == 0 && std::abs(a) <= 2});
	return cells;
}
// The map offset of stencil cell (a, b) turned to face `facing` (0 +y, 1 -y, 2 +x, 3 -x).
std::pair<int, int> torOffset(int a, int b, int facing)
{
	switch (facing)
	{
	case 0:
		return {a, b};
	case 1:
		return {-a, -b};
	case 2:
		return {b, -a};
	default:
		return {-b, a};
	}
}

// One tor per colony, all or none, each face's front row the same walk from its colony's swarm: every
// colony's fitting candidates are listed with that walk, the walk chosen that every colony comes
// nearest to (nearest kTorWalk on a tie), and each colony in turn takes its candidate nearest that
// walk, then the one furthest inland, clear of the tors already placed.
void planTors(Features &f, const Stamp &stamp, const Torus &t, const std::vector<Home> &homes,
			  const std::vector<int> &fromWater, GenerationContext &context)
{
	const int n = t.size();
	f.tor.assign(size_t(n), 0);
	f.quarry.assign(size_t(n), 0);
	f.owner.assign(size_t(n), -1);
	const std::vector<unsigned char> walk = walkableTiles(stamp.terrain, t);
	const std::vector<unsigned char> plain = tilesAll(stamp.terrain, t, [](unsigned char v) { return v == GRASS; });
	const std::vector<unsigned char> ground = homeGround(t, homes, kTorRoadClear);
	std::vector<unsigned char> island(size_t(n), 0);
	for (const Island &isle : stamp.islands)
		for (int i : isle.tiles)
			island[i] = 1;
	// Rock may stand on any dry land; the face, the rock behind it and the ground before it need plain
	// grass, so the quarry tiles and their approach keep four grass corners.
	std::vector<unsigned char> land(size_t(n), 0), ok(size_t(n), 0);
	for (int i = 0; i < n; ++i)
	{
		land[i] = walk[i] && !ground[i] && !island[i] && fromWater[i] >= kTorDry;
		ok[i] = land[i] && plain[i];
	}
	const std::vector<int> tie = periodicNoise(t.w, t.h, 16, context.stream(kSpineStream));
	const std::vector<TorCell> stencil = torStencil();
	const int lowest = kTorWalkLeast, highest = kTorWalkMost;
	struct Candidate
	{
		int centre, facing, front;
		long inland; // lower is further inland, then the tie noise
	};
	std::vector<std::vector<Candidate>> candidates(homes.size());
	for (size_t k = 0; k < homes.size(); ++k)
	{
		const Home &home = homes[k];
		std::vector<int> swarm;
		for (int dy = 0; dy < 4; ++dy)
			for (int dx = 0; dx < 4; ++dx)
				swarm.push_back(t.at(home.x + dx, home.y + dy));
		const Reach reach = reachFrom(t, swarm, walk, highest + 8);
		const double homeAcross = stamp.belt.across(t.x(home.x + 2), t.y(home.y + 2));
		std::vector<int> steps(size_t(n), -1);
		for (size_t r = 0; r < reach.tiles.size(); ++r)
			steps[reach.tiles[r]] = reach.steps[r];
		for (size_t r = 0; r < reach.tiles.size(); ++r)
		{
			const int c = reach.tiles[r];
			if (reach.steps[r] < lowest + 1 || !land[c])
				continue;
			const int cx = t.remainderX(c), cy = c / t.w;
			// On the colony's own side of the spine, clear of it.
			const double across = stamp.belt.across(cx, cy);
			if (across * homeAcross < 0 || std::abs(across) < kTorSpineClear)
				continue;
			const int hx = t.offsetX(cx, int(home.cx())), hy = t.offsetY(cy, int(home.cy()));
			const int toward = std::abs(hy) >= std::abs(hx) ? (hy > 0 ? 0 : 1) : (hx > 0 ? 2 : 3);
			for (int facing = 0; facing < 4; ++facing)
			{
				// Every tile of the stencil and kTorClear round it on allowed ground.
				bool fits = true;
				for (int b = -4 - kTorClear; b <= 1 + kTorClear && fits; ++b)
					for (int a = -5 - kTorClear; a <= 5 + kTorClear && fits; ++a)
					{
						const auto [dx, dy] = torOffset(a, b, facing);
						const int i = t.at(cx + dx, cy + dy);
						fits = b >= -1 && std::abs(a) <= 3 ? ok[i] : land[i];
					}
				if (!fits)
					continue;
				int front = INT_MAX;
				for (int a = -2; a <= 2; ++a)
				{
					const auto [dx, dy] = torOffset(a, 1, facing);
					const int s = steps[t.at(cx + dx, cy + dy)];
					if (s >= 0)
						front = std::min(front, s);
				}
				if (front < lowest || front > highest)
					continue;
				// Facing the colony, then furthest inland, then the noise.
				candidates[k].push_back({c, facing, front,
										 (facing == toward ? 0 : 1000) - long(std::min(fromWater[c], 40)) * 16 +
											 tie[c] / 4097});
			}
		}
		if (candidates[k].empty())
		{
			f.torsOmitted = true;
			return;
		}
	}
	// The common walk: the one that keeps every colony nearest it and itself nearest kTorWalk (each step
	// the worst colony misses it by costs three steps of distance from kTorWalk).
	int target = kTorWalk, worst = INT_MAX, cost = INT_MAX;
	for (int w = lowest; w <= highest; ++w)
	{
		int miss = 0;
		for (const auto &list : candidates)
		{
			int nearest = INT_MAX;
			for (const Candidate &c : list)
				nearest = std::min(nearest, std::abs(c.front - w));
			miss = std::max(miss, nearest);
		}
		const int here = 3 * miss + std::abs(w - kTorWalk);
		if (here < cost || (here == cost && std::abs(w - kTorWalk) < std::abs(target - kTorWalk)))
		{
			cost = here;
			worst = miss;
			target = w;
		}
	}
	f.torBand = target;
	f.torMiss = worst;
	std::vector<unsigned char> taken(size_t(n), 0);
	for (size_t k = 0; k < homes.size(); ++k)
	{
		const Candidate *best = nullptr;
		for (const Candidate &c : candidates[k])
		{
			const int cx = t.remainderX(c.centre), cy = c.centre / t.w;
			bool clear = true;
			for (int b = -4 - kTorClear; b <= 1 + kTorClear && clear; ++b)
				for (int a = -5 - kTorClear; a <= 5 + kTorClear && clear; ++a)
				{
					const auto [dx, dy] = torOffset(a, b, c.facing);
					clear = !taken[t.at(cx + dx, cy + dy)];
				}
			if (!clear)
				continue;
			if (!best || std::abs(c.front - target) < std::abs(best->front - target) ||
				(std::abs(c.front - target) == std::abs(best->front - target) && c.inland < best->inland))
				best = &c;
		}
		if (!best)
		{
			std::fill(f.tor.begin(), f.tor.end(), 0);
			std::fill(f.quarry.begin(), f.quarry.end(), 0);
			std::fill(f.owner.begin(), f.owner.end(), -1);
			f.torsOmitted = true;
			return;
		}
		const int cx = t.remainderX(best->centre), cy = best->centre / t.w;
		for (const TorCell &cell : stencil)
		{
			const auto [dx, dy] = torOffset(cell.a, cell.b, best->facing);
			const int i = t.at(cx + dx, cy + dy);
			if (cell.face)
			{
				f.quarry[i] = 1;
				f.owner[i] = int(k);
			}
			else
				f.tor[i] = 1;
		}
		// Tors keep their clearance apart.
		for (int b = -4 - 2 * kTorClear - 1; b <= 1 + 2 * kTorClear + 1; ++b)
			for (int a = -5 - 2 * kTorClear - 1; a <= 5 + 2 * kTorClear + 1; ++a)
			{
				const auto [dx, dy] = torOffset(a, b, best->facing);
				taken[t.at(cx + dx, cy + dy)] = 1;
			}
	}
	f.tors = int(homes.size());
}

// The broken spine along the belt's centre line, clear of water, the homes, the road and the tors.
void planSpine(Features &f, const Stamp &stamp, const Torus &t, const Axes &axes, const std::vector<Home> &homes,
			   const std::vector<int> &fromWater, GenerationContext &context)
{
	const int n = t.size(), length = axes.length();
	f.spine.assign(size_t(n), 0);
	std::mt19937 &rng = context.stream(kSpineStream);
	double slope = 0;
	const std::vector<double> wander = closedCurve(length, 1.4, rng, slope);
	const std::vector<double> thickness = closedCurve(length, 1.0, rng, slope);
	// Runs of rock and passes round the ring from a random start; the last run stops a pass short
	// of the first.
	std::vector<double> run(size_t(length), 0.0);
	const int start = int(context.bounded(kSpineStream, unsigned(length)));
	for (int at = 0; at < length;)
	{
		int rock = kSpineRunLeast + int(context.bounded(kSpineStream, kSpineRunExtra));
		rock = std::min(rock, length - kSpinePassLeast - at);
		for (int k = 0; k < rock; ++k)
		{
			// Thinner towards a run's ends, so every segment tapers.
			const int end = std::min(k, rock - 1 - k);
			run[size_t((start + at + k) % length)] = std::min(1.0, 0.55 + 0.15 * end);
		}
		at += std::max(rock, 0) + kSpinePassLeast + int(context.bounded(kSpineStream, kSpinePassExtra));
	}
	std::vector<unsigned char> clear(size_t(n), 0);
	{
		std::vector<unsigned char> obstacle(size_t(n), 0);
		for (int i = 0; i < n; ++i)
			obstacle[i] = (!f.route.empty() && f.route[i]) || (!f.tor.empty() && (f.tor[i] || f.quarry[i]));
		clear = dilate(t, obstacle, kSpineRoadClear);
	}
	std::vector<unsigned char> rock(size_t(n), 0);
	for (int y = 0; y < t.h; ++y)
		for (int x = 0; x < t.w; ++x)
		{
			const int i = t.at(x, y), u = axes.u(x, y);
			if (run[size_t(u)] <= 0 || fromWater[i] < kSpineDry || clear[i])
				continue;
			const double half = (kSpineHalfThick + kSpineThickSwing * (thickness[size_t(u)] + 1) / 2) * run[size_t(u)];
			if (std::abs(stamp.belt.across(x, y) - kSpineWander * wander[size_t(u)]) > half)
				continue;
			if (nearestHome(t, homes, x, y) < kSpineHomeClear)
				continue;
			rock[i] = 1;
		}
	f.spine = dropSmallRegions(t, blocksOfTwo(t, rock), kSpineLeast);
}

// The mechanic sketch: the stamp with every 2.0 layer over it.
void paintMechanics(Features &f, const Stamp &stamp, const Torus &t, const std::vector<Home> &homes,
					const std::vector<int> &oceanSteps, const std::vector<int> &fray, const std::vector<int> &gate,
					const std::vector<unsigned char> &dropBoulders)
{
	const int n = t.size();
	TerrainSketch &mech = f.mechanics;
	mech = stamp.terrain;
	f.mud = f.headlands = f.loam = f.scree = f.ash = 0;
	// Plain grass round every tor's face and rock, so its quarry tiles keep four grass corners.
	std::vector<unsigned char> torAll(size_t(n), 0);
	for (int i = 0; i < n; ++i)
		torAll[i] = f.tor[i] || f.quarry[i];
	f.keep = tileCorners(t, dilate(t, torAll, kTorClear));
	const std::vector<unsigned char> roadCorners =
		f.route.empty() ? std::vector<unsigned char>(size_t(n), 0) : tileCorners(t, f.route);
	const std::vector<unsigned char> roadNear = dilate(t, roadCorners, 2);
	std::vector<unsigned char> islandVertex(size_t(n), 0);
	for (const Island &isle : stamp.islands)
		for (int i : isle.tiles)
			islandVertex[i] = 1;
	islandVertex = dilate(t, islandVertex, 1);
	// Lakes: loam rings outside the gardens.
	const std::vector<int> fromLake = stepsFrom(t, stamp.lake);
	// The ring's reach is measured straight (Euclidean), so it rounds the lake rather than squaring it.
	const std::vector<std::int64_t> lakeReach = distanceSquaredTo(t, stamp.lake);
	for (int i = 0; i < n; ++i)
	{
		const double reach = kLoamDeep + 2.0 * (gate[i] - 32768) / 32768;
		if ((mech[i] != GRASS && mech[i] != SAND) || f.keep[i] || fromLake[i] < 1 || lakeReach[i] < 0 ||
			double(lakeReach[i]) > reach * reach)
			continue;
		const int x = t.remainderX(i), y = i / t.w;
		// The beach itself turns to loam everywhere (sand there only held growth back, and no grass is
		// lost); the ring beyond it stays out of the home gardens, which keep plain grass.
		const bool beach = mech[i] == SAND && fromLake[i] == 1;
		if (!beach && nearestHome(t, homes, x, y) < kGardenRadius + kGardenFray * (fray[i] - 32768) / 32768.0)
			continue;
		mech[i] = LOAM;
		++f.loam;
	}
	// Coasts: bays of mud, headlands of boulders, by the share of ocean round each beach vertex.
	const int probe = kCoastProbe, disc = [&]
	{
		int cells = 0;
		for (int dy = -probe; dy <= probe; ++dy)
			for (int dx = -probe; dx <= probe; ++dx)
				cells += dx * dx + dy * dy <= probe * probe;
		return cells;
	}();
	for (int i = 0; i < n; ++i)
	{
		if (mech[i] != SAND || oceanSteps[i] != 1 || islandVertex[i] || f.keep[i] || roadNear[i])
			continue;
		const int x = t.remainderX(i), y = i / t.w;
		if (nearestHome(t, homes, x, y) < kCoastKeep)
			continue;
		int sea = 0;
		for (int dy = -probe; dy <= probe; ++dy)
			for (int dx = -probe; dx <= probe; ++dx)
				if (dx * dx + dy * dy <= probe * probe)
					sea += stamp.ocean[t.at(x + dx, y + dy)];
		const double share = double(sea) / disc;
		if (share > kHeadland && gate[i] > (1 - kHeadlandGate) * 65535 && !dropBoulders[i])
		{
			mech[i] = BOULDERS;
			++f.headlands;
		}
		else if (share < kBay && gate[i] < kBayGate * 65535)
		{
			mech[i] = MUDFLAT;
			++f.mud;
		}
	}
	// Islands: the stone islands are volcanic, a pumice beach (Rough) round their grass. An ash ring
	// (Barren) on their ground was tried and cost 12% of the map's growth potential with no lakes: barren
	// ground inhibits like sand, and an island is all shore.
	f.volcanic.assign(size_t(n), 0);
	for (size_t k = 0; k < stamp.islands.size(); ++k)
	{
		if (f.isleTheme[k] != 0)
			continue;
		// The island's tiles and the vertices one beyond them: its ground and its beach.
		const Island &isle = stamp.islands[k];
		std::vector<int> vertices;
		for (int tile : isle.tiles)
			for (int dy = -1; dy <= 1; ++dy)
				for (int dx = -1; dx <= 1; ++dx)
					vertices.push_back(t.at(t.remainderX(tile) + dx, tile / t.w + dy));
		std::sort(vertices.begin(), vertices.end());
		vertices.erase(std::unique(vertices.begin(), vertices.end()), vertices.end());
		for (int i : vertices)
		{
			if (mech[i] == SAND)
			{
				mech[i] = SCREE;
				f.volcanic[i] = 1;
				++f.ash;
			}
		}
	}
	// Scree round the spine, far from water.
	if (!f.spine.empty())
	{
		std::vector<unsigned char> eligible(size_t(n), 0);
		const std::vector<int> fromWet = stepsFrom(t, tilesAny(stamp.terrain, t, wetType));
		for (int i = 0; i < n; ++i)
			eligible[i] = !f.keep[i] && !roadNear[i] && fromWet[i] >= kTalusDry;
		const std::vector<unsigned char> talus = talusApron(t, f.spine, eligible, fray, 0, kTalusOuter, 30000);
		for (int i = 0; i < n; ++i)
			if (talus[i] && mech[i] == GRASS)
			{
				mech[i] = SCREE;
				++f.scree;
			}
	}
	// The road.
	for (int i = 0; i < n; ++i)
		if (roadCorners[i] && !wetType(mech[i]))
			mech[i] = TRAIL;
	// The rock.
	std::vector<unsigned char> rockTiles(size_t(n), 0);
	for (int i = 0; i < n; ++i)
		rockTiles[i] = f.spine[i] || f.tor[i];
	const std::vector<unsigned char> rock = rockVertices(t, rockTiles);
	for (int i = 0; i < n; ++i)
		if (rock[i])
			mech[i] = RIDGE_ROCK;
}

Features planFeatures(const Stamp &stamp, const Torus &t, const Axes &axes, const std::vector<Home> &homes,
					  const RingWorldOptions &options, GenerationContext &context)
{
	const int n = t.size();
	Features f;
	const std::vector<unsigned char> wetTiles = tilesAny(stamp.terrain, t, wetType);
	const std::vector<int> fromWater = stepsFrom(t, wetTiles);
	// Island themes, drawn first: 0 stone (volcanic), 1 fruit, 2 wheat, one in three each.
	for (size_t k = 0; k < stamp.islands.size(); ++k)
		f.isleTheme.push_back(int(context.bounded(kIsleStream, 3)));
	// The tors first (they need the most particular ground), then the road round them, then the spine
	// clear of both.
	f.tor.assign(size_t(n), 0);
	f.quarry.assign(size_t(n), 0);
	f.owner.assign(size_t(n), -1);
	f.spine.assign(size_t(n), 0);
	f.route.assign(size_t(n), 0);
	if (options.rockSpine)
		planTors(f, stamp, t, homes, fromWater, context);
	if (options.ringRoad)
		planRoad(f, stamp, t, axes, homes, fromWater, context);
	if (f.roadOmitted || !options.ringRoad)
		f.route.assign(size_t(n), 0);
	if (options.rockSpine)
		planSpine(f, stamp, t, axes, homes, fromWater, context);
	const std::vector<int> oceanSteps = stepsFrom(t, stamp.ocean);
	std::mt19937 &looks = context.stream(kCoastStream);
	const std::vector<int> fray = periodicNoise(t.w, t.h, 16, looks);
	const std::vector<int> gate = fractalNoise(t.w, t.h, 32, 2, looks);
	// Paint, then drop any boulders or spine that part the ground round them (the stamp's own
	// walkable ground, with the quarry faces' stone counted as blocking), and paint again.
	const std::vector<unsigned char> before = walkableTiles(stamp.terrain, t);
	std::vector<unsigned char> dropBoulders(size_t(n), 0);
	// Each round drops one parting group; a sketch still parting after the last round is refused
	// rather than written (validateWorld would refuse it anyway).
	constexpr int kPartingRounds = 24;
	bool settled = false;
	for (int round = 0; round < kPartingRounds; ++round)
	{
		paintMechanics(f, stamp, t, homes, oceanSteps, fray, gate, dropBoulders);
		const std::vector<unsigned char> after = walkableTiles(f.mechanics, t);
		std::vector<unsigned char> blocked(size_t(n), 0);
		for (int i = 0; i < n; ++i)
			blocked[i] = (before[i] && !after[i]) || f.quarry[i];
		std::vector<int> label;
		const int parting = partingGroup(t, before, blocked, label);
		if (parting < 0)
		{
			settled = true;
			break;
		}
		++f.dropped;
		std::vector<unsigned char> group(size_t(n), 0);
		bool torInGroup = false;
		for (int i = 0; i < n; ++i)
			if (label[i] == parting)
			{
				group[i] = 1;
				f.spine[i] = 0;
				torInGroup = torInGroup || f.tor[i] || f.quarry[i];
			}
		const std::vector<unsigned char> corners = tileCorners(t, dilate(t, group, 1));
		for (int i = 0; i < n; ++i)
			dropBoulders[i] = dropBoulders[i] || corners[i];
		if (torInGroup)
		{
			f.torsOmitted = true;
			f.tors = 0;
			std::fill(f.tor.begin(), f.tor.end(), 0);
			std::fill(f.quarry.begin(), f.quarry.end(), 0);
			std::fill(f.owner.begin(), f.owner.end(), -1);
		}
		f.spine = dropSmallRegions(t, blocksOfTwo(t, f.spine), kSpineLeast);
	}
	if (!settled)
		f.failure = "Ring world cannot lay its rock and coasts without cutting ground off on this seed.";
	f.spineTiles = int(std::count(f.spine.begin(), f.spine.end(), 1));
	{
		const std::vector<int> parts = connectedRegions(f.spine, t.w, t.h, true, GridNeighbors::Eight);
		int most = 0;
		for (int p : parts)
			most = std::max(most, p + 1);
		f.segments = most;
	}
	return f;
}

// The looks over the mechanic sketch, by climate (composeLooks keeps each inside its rule group).
TerrainSketch lookSketch(const Features &f, const Stamp &stamp, const Torus &t, const Climate &climate,
						 GenerationContext &context)
{
	const int n = t.size();
	const TerrainSketch &mech = f.mechanics;
	std::vector<unsigned char> wet(size_t(n), 0);
	for (int i = 0; i < n; ++i)
		wet[i] = wetType(mech[i]);
	// Straight-line distance, so the grass grades in rounded bands rather than squares.
	const std::vector<std::int64_t> wetReach = distanceSquaredTo(t, wet);
	std::vector<double> fromWet(size_t(n), 0);
	for (int i = 0; i < n; ++i)
		fromWet[i] = wetReach[i] < 0 ? 1e9 : std::sqrt(double(wetReach[i]));
	std::mt19937 &random = context.stream(kLookStream);
	const std::vector<int> fray = fractalNoise(t.w, t.h, 32, 3, random);
	const std::vector<int> split = fractalNoise(t.w, t.h, 16, 3, random);
	std::vector<unsigned char> green(size_t(n), 0);
	for (size_t k = 0; k < stamp.islands.size(); ++k)
		if (f.isleTheme[k] != 0)
			for (int i : stamp.islands[k].tiles)
				green[i] = 1;
	TerrainSketch looks(size_t(n), 0);
	for (int i = 0; i < n; ++i)
	{
		const double shift = 7.0 * (fray[i] - 32768) / 32768.0;
		const bool second = split[i] > 0.6 * 65536;
		switch (mech[i])
		{
		case GRASS:
			if (green[i] || fromWet[i] <= climate.lush + shift)
				looks[i] = LUSH_GRASS;
			else if (fromWet[i] > climate.green + shift)
				looks[i] = DRY_GRASS;
			break;
		case LOAM:
			looks[i] = second ? climate.loamSecond : climate.loam;
			break;
		case SAND:
			looks[i] = climate.beach;
			break;
		case DEEP_WATER:
			looks[i] = climate.deep;
			break;
		case MUDFLAT:
			looks[i] = climate.mud;
			break;
		case BOULDERS:
			looks[i] = climate.headland;
			break;
		case RIDGE_ROCK:
			looks[i] = climate.rock;
			break;
		case TRAIL:
			looks[i] = climate.road;
			break;
		case SCREE:
			looks[i] = f.volcanic[i] ? PUMICE : SCREE;
			break;
		default:
			break;
		}
	}
	return looks;
}

// Each island carries one themed prize at its middle, as stockIslands lays it, with the theme the
// design already drew (and painted the island for).
void stockThemedIslands(GenerationMap &map, GenerationContext &context, const std::vector<Island> &islands,
						const std::vector<int> &themes)
{
	const int width = map.getW();
	constexpr int stockTypes[] = {STONE, WHEAT, CHERRY, ORANGE, PRUNE};
	const auto acceptsStock = [&](int x, int y)
	{
		for (int resource : stockTypes)
			if (!map.terrainSupportsResourceAtByIndex(x, y, resource))
				return false;
		return true;
	};
	for (size_t k = 0; k < islands.size(); ++k)
	{
		const Island &island = islands[k];
		std::vector<MapGeneratorPoint> grass;
		for (int i : island.tiles)
			if (acceptsStock(powerOfTwoRemainder(i, width), i / width))
				grass.emplace_back(powerOfTwoRemainder(i, width), i / width);
		if (grass.empty())
			continue;
		MapGeneratorPoint centre(island.x, island.y);
		if (!acceptsStock(centre.x, centre.y))
			centre = grass.at(context.bounded(kIsleStream, grass.size()));
		switch (themes[k])
		{
		case 0:
			placeResourceClump(map, context, centre, STONE, 2);
			break;
		case 1:
			placeResourceClump(map, context, centre, CHERRY + int(context.bounded(kIsleStream, 3)), 2);
			break;
		default:
			placeResourceClump(map, context, centre, WHEAT, 2);
			break;
		}
	}
}

// A swarm anchored at (x, y) needs pure grass under its 4x4 footprint and dry, unoccupied ground in
// the ring around it where its workers will stand.
bool siteFits(const GenerationMap &map, int x, int y)
{
	for (int dy = -1; dy <= 4; ++dy)
		for (int dx = -1; dx <= 4; ++dx)
		{
			const int nx = map.normalizeX(x + dx), ny = map.normalizeY(y + dy);
			const bool footprint = dx >= 0 && dx < 4 && dy >= 0 && dy < 4;
			if (footprint ? !map.terrainPropertiesAt(nx, ny).buildable : !map.terrainPropertiesAt(nx, ny).walkable)
				return false;
			if (map.getBuilding(nx, ny) != NOGBID || map.getGroundUnit(nx, ny) != NOGUID)
				return false;
		}
	return true;
}

// Colonies are dealt evenly spaced slots around the ring from a random starting point, with a
// little jitter, and alternate between the belt's two coasts (or all take one). Within its slot a
// colony takes the site whose nearest water is closest to kHomeShore tiles away, so every start
// has the same shore.
bool placeColonies(GenerationWorld &game, GenerationContext &context, const Belt &belt, bool bothCoasts)
{
	GenerationMap &map = game.map;
	const Axes &axes = belt.axes;
	const int width = axes.width, height = axes.height, length = axes.length();
	const int teams = context.request.nbTeams;
	const size_t area = size_t(width) * height;
	std::vector<unsigned char> water(area), dry(area);
	for (int y = 0; y < height; ++y)
		for (int x = 0; x < width; ++x)
		{
			water[size_t(y) * width + x] = terrainProvidesFertility(map.terrainPropertiesAt(x, y));
			dry[size_t(y) * width + x] = map.terrainPropertiesAt(x, y).walkable;
		}
	const std::vector<int> shore = stepsFrom(Torus{width, height}, water, dry);
	// Only the belt itself: an island or a stretch of rough coast cut off at sea can offer the same
	// shore, but a colony there would have no land neighbours at all. The spine is always dry, so
	// the belt is whatever land is reached from it.
	std::vector<unsigned char> spine(area, 0);
	for (int y = 0; y < height; ++y)
		for (int x = 0; x < width; ++x)
			spine[size_t(y) * width + x] =
				map.terrainPropertiesAt(x, y).walkable && std::abs(belt.across(x, y)) < kSpineHalf - 1;
	const std::vector<int> onBelt = stepsFrom(Torus{width, height}, spine, dry);
	const double slot = double(length) / teams;
	const double first = context.bounded("colonies", 3600) / 3600.0 * length;
	const int firstSide = int(context.bounded("colonies", 2));
	std::vector<MapGeneratorPoint> placed;
	for (int team = 0; team < teams; ++team)
	{
		// Colonies are spaced evenly round the ring with up to 8% of a slot of jitter, so the
		// layout is fair without looking ruled.
		const double jitter =
			(int(context.bounded("colonies", 2001)) - 1000) / 1000.0 * 0.08 * slot;
		const double target = first + team * slot + jitter;
		const double side = (firstSide + (bothCoasts ? team : 0)) % 2 ? 1.0 : -1.0;
		const double spacing = 0.5 * slot;
		int bestX = -1, bestY = -1;
		double bestScore = std::numeric_limits<double>::max();
		// Search within 12% of a slot of the colony's target first, widening to 25% and 50% only
		// when nothing fits, so the spacing stays as even as the terrain allows. A site scores 3
		// per tile away from the ideal 6-tile distance to water, 1 per window of distance from its
		// target, and 6 for being on the wrong coast: shore distance matters most, since that is
		// what makes the starts equal.
		for (double reach : {0.12, 0.25, 0.5})
		{
			if (reach > 0.12)
				context.telemetry.fallback("ring-world.start.search-widened",
										   "No suitable site fit the previous slot window.", team);
			const double window = std::max(3.0, reach * slot);
			for (int y = 0; y < height; ++y)
				for (int x = 0; x < width; ++x)
				{
					const double offset =
						std::abs(std::remainder(axes.u(x, y) + 1.5 - target, double(length)));
					if (offset > window || onBelt[size_t(y) * width + x] < 0 ||
						!siteFits(map, x, y))
						continue;
					bool crowded = false;
					for (const MapGeneratorPoint &other : placed)
						crowded = crowded || Torus{width, height}.dist2(x, y, other.x, other.y) <
												 spacing * spacing;
					if (crowded)
						continue;
					int gap = INT_MAX;
					for (int dy = 0; dy < 4; ++dy)
						for (int dx = 0; dx < 4; ++dx)
							gap = std::min(gap, shore[size_t(map.normalizeY(y + dy)) * width +
													  map.normalizeX(x + dx)]);
					const double across = belt.across(map.normalizeX(x + 2), map.normalizeY(y + 2));
					const double score = 3.0 * std::abs(gap - kHomeShore) + offset / window +
										 (across * side < 0 ? 6.0 : 0.0);
					if (score < bestScore)
					{
						bestScore = score;
						bestX = x;
						bestY = y;
					}
				}
			if (bestX >= 0)
				break;
		}
		if (bestX < 0)
		{
			context.detail = "Colony " + std::to_string(team) +
							 ": no room for a swarm on the belt near its slot";
			return false;
		}
		context.telemetry.measure("ring-world.start.score", bestScore, team);
		std::vector<unsigned char> home(area, 0);
		// The settlement's ground: the 10x10 square round the 4x4 swarm, 3 tiles each way.
		for (int dy = -3; dy <= 6; ++dy)
			for (int dx = -3; dx <= 6; ++dx)
			{
				const int nx = map.normalizeX(bestX + dx), ny = map.normalizeY(bestY + dy);
				if (map.terrainPropertiesAt(nx, ny).walkable)
					home[size_t(ny) * width + nx] = 1;
			}
		if (!placeSettlement(game, context, team, home, MapGeneratorPoint(bestX, bestY), "starts"))
			return false;
		placed.emplace_back(bestX, bestY);
	}
	return true;
}

// Every home's starter kit: a wheat and a wood patch on the most fertile ground a short walk from
// the swarm, and a little stone further inland, never on `keepOpen` (the ground round the quarry tors).
void furnishHomes(GenerationWorld &game, GenerationContext &context, const std::vector<unsigned char> &keepOpen)
{
	GenerationMap &map = game.map;
	const int width = map.getW(), height = map.getH(), teams = context.request.nbTeams;
	const Torus t{width, height};
	const size_t area = size_t(width) * height;
	std::vector<unsigned char> water(area), dry(area);
	for (int y = 0; y < height; ++y)
		for (int x = 0; x < width; ++x)
		{
			water[size_t(y) * width + x] = terrainProvidesFertility(map.terrainPropertiesAt(x, y));
			dry[size_t(y) * width + x] = map.terrainPropertiesAt(x, y).walkable;
		}
	const std::vector<unsigned char> reserved = swarmSurroundings(t, context);
	const std::vector<int> shore = stepsFrom(t, water, dry);
	const auto eligible = [&](int i)
	{ return !reserved[i] && !keepOpen[i] && clearGround(map, powerOfTwoRemainder(i, width), i / width); };
	for (int team = 0; team < teams; ++team)
	{
		std::vector<unsigned char> footprint(area, 0);
		for (int dy = 0; dy < 4; ++dy)
			for (int dx = 0; dx < 4; ++dx)
				footprint[size_t(map.normalizeY(context.bootY[team] + dy)) * width +
						  map.normalizeX(context.bootX[team] + dx)] = 1;
		const std::vector<int> walk = stepsFrom(Torus{width, height}, footprint, dry);
		// Wettest (or, for stone, driest) eligible tile in a walking band, nearest first on ties.
		const auto pick = [&](int nearest, int farthest, bool wet, int avoid)
		{
			int best = -1;
			for (int i = 0; i < int(area); ++i)
			{
				if (walk[i] < nearest || walk[i] > farthest || !eligible(i))
					continue;
				if (avoid >= 0 && t.dist2(powerOfTwoRemainder(i, width), i / width, powerOfTwoRemainder(avoid, width), avoid / width) < 36)
					continue;
				const bool better = best < 0 ||
									(wet ? shore[i] < shore[best] : shore[i] > shore[best]) ||
									(shore[i] == shore[best] && walk[i] < walk[best]);
				if (better)
					best = i;
			}
			return best;
		};
		// Wheat and wood 5 to 9 steps out on the ground nearest water, where they regrow, at least
		// 6 tiles apart so one never grows over the other; stone 7 to 12 steps out on the ground
		// farthest from water, where it takes nothing from farming.
		const int wheat = pick(5, 9, true, -1);
		if (wheat >= 0)
			growPatch(map, t, wheat, WHEAT, kHomeWheat, eligible);
		const int wood = pick(5, 9, true, wheat);
		if (wood >= 0)
			growPatch(map, t, wood, WOOD, kHomeWood, eligible);
		const int stone = pick(7, 12, false, -1);
		if (stone >= 0)
			growPatch(map, t, stone, STONE, kHomeStone, eligible);
	}
}

// Deposits may land anywhere, and stone is never cleared in play, so a band of them could wall
// the belt off. This keeps the ring open: for each colony in order around the belt, the cheapest
// walk forward to the next (resources cost one, open ground nothing; water and buildings are
// impassable) is found in the belt's unrolled cover, and only the deposits on it are cleared. The
// last walk crosses the seam back to the first colony, so together they close one loop all the way
// around the map. Almost always nothing is in the way and nothing is cleared. Tiles of `keep` (the
// quarry faces) are never walked over or cleared.
bool openBeltRoad(GenerationWorld &game, GenerationContext &context, const Axes &axes,
				  const std::vector<unsigned char> &keep)
{
	GenerationMap &map = game.map;
	const int width = map.getW(), height = map.getH(), length = axes.length();
	const int teams = context.request.nbTeams;
	const int area = width * height;
	constexpr int kLayers = 4; // windings -1 to 2 along the belt
	std::vector<std::vector<int>> workers(teams);
	for (int team = 0; team < teams; ++team)
		workers[team] = unitTilesByTeam(map, teams)[team];
	std::vector<std::pair<double, int>> order;
	for (int team = 0; team < teams; ++team)
		order.emplace_back(
			std::fmod(axes.u(context.bootX[team], context.bootY[team]) + 2.0, double(length)),
			team);
	std::sort(order.begin(), order.end());

	std::vector<int> cost(size_t(kLayers) * area), parent(size_t(kLayers) * area);
	std::vector<unsigned char> done(size_t(kLayers) * area);
	std::vector<int> goal(area);
	// The winding that puts tile i nearest to a position along the unrolled belt.
	const auto windingNear = [&](int i, double position)
	{ return int(std::lround((position - axes.u(powerOfTwoRemainder(i, width), i / width)) / length)); };
	for (int k = 0; k < teams; ++k)
	{
		const double fromU = order[k].first;
		const double toU = order[(k + 1) % teams].first + (k + 1 == teams ? length : 0);
		const int from = order[k].second, to = order[(k + 1) % teams].second;
		std::fill(cost.begin(), cost.end(), INT_MAX);
		std::fill(parent.begin(), parent.end(), -1);
		std::fill(done.begin(), done.end(), 0);
		std::fill(goal.begin(), goal.end(), kUnreached);
		std::deque<int> queue;
		for (int i : workers[from])
		{
			const int w = windingNear(i, fromU);
			if (w >= -1 && w < kLayers - 1)
			{
				cost[(w + 1) * area + i] = 0;
				queue.push_back((w + 1) * area + i);
			}
		}
		for (int i : workers[to])
			goal[i] = windingNear(i, toU);
		int reached = -1;
		while (!queue.empty() && reached < 0)
		{
			const int node = queue.front();
			queue.pop_front();
			if (done[node])
				continue;
			done[node] = 1;
			const int w = node / area - 1, i = node % area, x = powerOfTwoRemainder(i, width), y = i / width;
			if (goal[i] == w)
			{
				reached = node;
				break;
			}
			for (int dy = -1; dy <= 1; ++dy)
				for (int dx = -1; dx <= 1; ++dx)
				{
					if (!dx && !dy)
						continue;
					const int nx = map.normalizeX(x + dx), ny = map.normalizeY(y + dy);
					if (!map.terrainPropertiesAt(nx, ny).walkable || map.getBuilding(nx, ny) != NOGBID ||
						keep[size_t(ny) * width + nx])
						continue;
					const int step = axes.u(x, y) + axes.stepU(dx, dy);
					const int nw = w + (step < 0 ? -1 : step >= length ? 1 : 0);
					if (nw < -1 || nw >= kLayers - 1)
						continue;
					const int next = (nw + 1) * area + ny * width + nx;
					const int nextCost = cost[node] + (map.isResource(nx, ny) ? 1 : 0);
					if (nextCost < cost[next])
					{
						cost[next] = nextCost;
						parent[next] = node;
						if (nextCost == cost[node])
							queue.push_front(next);
						else
							queue.push_back(next);
					}
				}
		}
		if (reached < 0)
		{
			context.detail = "Colony " + std::to_string(from) +
							 " has no way along the belt to colony " + std::to_string(to);
			return false;
		}
		for (int node = reached; node >= 0; node = parent[node])
		{
			const int i = node % area;
			if (map.isResource(powerOfTwoRemainder(i, width), i / width))
				map.setNoResource(powerOfTwoRemainder(i, width), i / width, 1);
		}
	}
	return true;
}

// From each colony's swarm to the ground before its own face: the cheapest walk where a step costs ten
// (fourteen diagonally) and stepping onto a deposit forty more; the deposits on it are cleared.
void openQuarryWalks(GenerationMap &map, const Torus &t, const Features &features, const std::vector<Home> &homes)
{
	const int n = t.size();
	const std::vector<unsigned char> approach = quarryApproach(t, features.quarry);
	for (size_t k = 0; k < homes.size(); ++k)
	{
		std::vector<int> swarm;
		for (int dy = 0; dy < 4; ++dy)
			for (int dx = 0; dx < 4; ++dx)
				swarm.push_back(t.at(homes[k].x + dx, homes[k].y + dy));
		std::vector<unsigned char> goal(size_t(n), 0);
		for (int i = 0; i < n; ++i)
			if (approach[i] && !features.tor[i] && map.terrainPropertiesAt(t.remainderX(i), i / t.w).walkable)
			{
				// The ground before colony k's face: beside one of its quarry tiles.
				bool own = false;
				for (int dy = -1; dy <= 1 && !own; ++dy)
					for (int dx = -1; dx <= 1 && !own; ++dx)
						own = features.owner[t.at(t.remainderX(i) + dx, i / t.w + dy)] == int(k);
				goal[i] = own;
			}
		const std::vector<int> walk = cheapestWalk(
			t, GridNeighbors::Eight, swarm, goal,
			[&](int, int next, int dx, int dy)
			{
				const int x = t.remainderX(next), y = next / t.w;
				if (!map.terrainPropertiesAt(x, y).walkable || features.quarry[next] ||
					(map.getBuilding(x, y) != NOGBID && !std::count(swarm.begin(), swarm.end(), next)))
					return -1;
				return (dx && dy ? 14 : 10) + (map.isResource(x, y) ? 40 : 0);
			});
		for (int i : walk)
			if (!features.quarry[i] && map.isResource(t.remainderX(i), i / t.w))
				map.setNoResource(t.remainderX(i), i / t.w, 1);
	}
}

bool generate(GenerationWorld &game, GenerationContext &context)
{
	context.stage = "ring layout";
	const RingWorldOptions options(context.request);
	GenerationMap &map = game.map;
	const int width = map.getW(), height = map.getH(), teams = context.request.nbTeams;
	map.fillTerrain(WATER);
	for (int i = 0; i < teams; ++i)
		game.addTeam();
	const Axes axes = axesFor(width, height);
	if (teams < 1 || axes.length() < kBeltPerColony * teams)
	{
		context.detail = "the belt is too short for this many colonies";
		return false;
	}

	context.stage = "ring terrain";
	const Torus t{width, height};
	const int n = t.size();
	const Stamp stamp = stampRing(axes, context, options);
	context.telemetry.measure("ring-world.islands.target", stamp.wantedIslands);
	context.telemetry.measure("ring-world.islands.actual", stamp.islands.size());
	if (int(stamp.islands.size()) < stamp.wantedIslands)
		context.telemetry.fallback("ring-world.islands.omitted",
								   "Candidate budget or water clearance limited islands.");
	writeVertices(map, stamp.terrain);

	context.stage = "ring colonies";
	if (!placeColonies(game, context, stamp.belt, options.bothCoasts))
		return false;

	// 2.0: the road, the tors and the spine, the coasts, lakes and islands, planned from the stamp and
	// the swarms alone and written as the mechanic sketch before any deposit.
	context.stage = "ring 2.0 terrain";
	std::vector<Home> homes;
	for (int team = 0; team < teams; ++team)
		homes.push_back({context.bootX[team], context.bootY[team]});
	const Features features = planFeatures(stamp, t, axes, homes, options, context);
	if (!features.failure.empty())
	{
		context.detail = features.failure;
		return false;
	}
	writeVertices(map, features.mechanics);
	const int climateIndex =
		options.climate > 0 ? std::clamp(options.climate, 1, 3) - 1 : int(context.bounded(kClimateStream, 3));
	const Climate &climate = kClimates[climateIndex];
	context.telemetry.choice("ring-world.climate", climate.name);
	context.telemetry.choice("ring-world.deep-water", options.deepWater ? "on" : "off");
	context.telemetry.choice("ring-world.rock-spine", options.rockSpine ? "on" : "off");
	context.telemetry.choice("ring-world.ring-road", options.ringRoad ? "on" : "off");
	context.telemetry.measure("ring-world.deep.vertices", stamp.deep);
	context.telemetry.measure("ring-world.road.tiles", features.roadTiles);
	context.telemetry.measure("ring-world.spine.tiles", features.spineTiles);
	context.telemetry.measure("ring-world.spine.segments", features.segments);
	context.telemetry.measure("ring-world.spine.dropped", features.dropped);
	context.telemetry.measure("ring-world.tors.placed", features.tors);
	context.telemetry.measure("ring-world.tors.target-walk", features.torBand);
	context.telemetry.measure("ring-world.tors.walk-miss", features.torMiss);
	context.telemetry.measure("ring-world.mudflat.vertices", features.mud);
	context.telemetry.measure("ring-world.headland.vertices", features.headlands);
	context.telemetry.measure("ring-world.loam.vertices", features.loam);
	context.telemetry.measure("ring-world.scree.vertices", features.scree);
	context.telemetry.measure("ring-world.volcanic.vertices", features.ash);
	int volcanic = 0;
	for (int theme : features.isleTheme)
		volcanic += theme == 0;
	context.telemetry.measure("ring-world.islands.volcanic", volcanic);
	if (features.roadOmitted)
		context.telemetry.fallback("ring-world.road.omitted", "No doorstep or no walk for the ring road.");
	if (features.torsOmitted)
		context.telemetry.fallback("ring-world.tors.omitted", "Some colony's quarry tor fitted nowhere.");
	else if (features.torBand != kTorWalk || features.torMiss > 0)
		context.telemetry.fallback("ring-world.tors.walk-moved", "Not every colony's tor fitted the target walk.");

	// The quarry faces go down before any other deposit, so nothing lands on their stone.
	int quarryStone = 0;
	for (int i = 0; i < n; ++i)
		if (features.quarry[i] && map.isResourceAllowed(t.remainderX(i), i / width, STONE))
		{
			map.setResourceByIndex(t.remainderX(i), i / width, STONE, 1);
			++quarryStone;
		}
	context.telemetry.measure("ring-world.quarry.stone", quarryStone);

	context.stage = "ring resources";
	// Nothing is planted within kTorOpen tiles of a tor, and the ground before every face is cleared
	// again after the scatter, so each face is reached as its walk was planned.
	std::vector<unsigned char> torAll(size_t(n), 0);
	for (int i = 0; i < n; ++i)
		torAll[i] = features.tor[i] || features.quarry[i];
	const std::vector<unsigned char> torOpen = dilate(t, torAll, kTorOpen);
	furnishHomes(game, context, torOpen);
	stockThemedIslands(map, context, stamp.islands, features.isleTheme);
	// The same ambient layer as Fjord continent: wheat:wood 2:1, some stone, rare fruit. Algae is
	// seeded along the shallows below instead, since the shared band only scatters over land.
	scatterResources(game, context,
					 {/*wheat=*/int(scaledCount(24, options.wheat)),
					  /*wood=*/int(scaledCount(12, options.wood)),
					  /*stone=*/int(scaledCount(10, options.stone)), /*algae=*/0,
					  /*fruit=*/int(scaledCount(3, options.fruit))});
	// Algae 2 to 5 tiles off the shore, one clump per 90 tiles of that band: beside the beaches it
	// needs in order to regrow.
	seedAlgae(map, context, t, "resources", options.algae, AlgaeBand::shallows(2, 5));
	// The ground before every quarry face stays open.
	{
		const std::vector<unsigned char> front = dilate(t, features.quarry, kTorOpen);
		std::vector<unsigned char> open(size_t(n), 0);
		for (int i = 0; i < n; ++i)
			open[i] = front[i] && !features.tor[i] && !features.quarry[i];
		clearDeposits(map, t, open, &features.quarry);
	}
	// The branch-wide start promises: wheat within 24 steps and wood within 32 of every swarm, with
	// 6 tiles clear round it.
	secureStartingCrops(game, context, t, 24, 32, 6, &features.quarry);
	// The scatter above is sized by the resource amounts, and at the top of their range it can wall
	// a colony into its own clearing with nowhere to build; the reopened colony is cleared again.
	if (reopenCrampedStarts(
			game, context,
			{options.wheat, options.wood, options.stone, options.algae, options.fruit}, 24, 32, 6,
			&features.quarry))
		clearAroundSwarms(map, context, t, &features.quarry);

	// Every colony's walk to its face as planned: the walk where a deposit costs four steps, with the
	// deposits on it cleared, so no scattered field stands between a colony and its quarry.
	if (features.tors > 0)
		openQuarryWalks(map, t, features, homes);

	context.stage = "ring road";
	if (!openBeltRoad(game, context, axes, features.quarry))
		return false;

	// The looks, last: they change no rule, so nothing any stage above saw or placed changes.
	writeVertices(map, composeLooks(features.mechanics, lookSketch(features, stamp, t, climate, context)));
	if (context.telemetry.enabled())
	{
		// How far each colony walks to its nearest quarry face, on the finished map (-1 when none).
		const std::vector<unsigned char> walkable = walkableTiles(map);
		int least = INT_MAX, most = -1;
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
				if (!features.quarry[i])
					continue;
				for (const auto &step : kCardinalSteps)
				{
					const int s = steps[t.at(t.remainderX(i) + step[0], i / width + step[1])];
					if (s >= 0)
						nearest = std::min(nearest, s + 1);
				}
			}
			const int walk = nearest == INT_MAX ? -1 : nearest;
			context.telemetry.measure("ring-world.quarry.nearest-walk", walk, team);
			least = std::min(least, walk);
			most = std::max(most, walk);
		}
		if (least >= 0 && features.tors > 0)
			context.telemetry.measure("ring-world.quarry.nearest-walk-spread", most - least);
	}
	return true;
}

std::string validate(const GenerationRequest &r)
{
	const int length = std::max(1 << r.wDec, 1 << r.hDec);
	if (r.nbTeams < 1 || length < kBeltPerColony * r.nbTeams)
		return "The belt is too short for this many colonies; use a longer map or fewer colonies.";
	return "";
}

// Floods open tiles from the sources while counting how often each step crosses the seam along the
// belt. Reaching a tile again with a different count closes a walk around the torus, which is
// exactly what it means for the flooded region to wrap. `winding` is shared between floods of
// disjoint regions so each tile is labelled once; `reached` receives this flood's tiles.
template <typename Open>
bool floodAround(const GenerationMap &map, const Axes &axes, const std::vector<int> &sources, Open open,
				 std::vector<int> &winding, std::vector<int> &reached)
{
	const int width = map.getW(), length = axes.length();
	reached.clear();
	if (sources.empty())
		return false;
	const int anchor = axes.u(powerOfTwoRemainder(sources[0], width), sources[0] / width);
	for (int i : sources)
	{
		if (winding[i] != kUnreached)
			continue;
		// Each source starts at its image nearest the first, so a colony astride the seam is
		// consistent.
		winding[i] = int(std::lround(double(anchor - axes.u(powerOfTwoRemainder(i, width), i / width)) / length));
		reached.push_back(i);
	}
	bool wraps = false;
	for (size_t head = 0; head < reached.size(); ++head)
	{
		const int i = reached[head], x = powerOfTwoRemainder(i, width), y = i / width, w = winding[i];
		for (int dy = -1; dy <= 1; ++dy)
			for (int dx = -1; dx <= 1; ++dx)
			{
				if (!dx && !dy)
					continue;
				const int nx = map.normalizeX(x + dx), ny = map.normalizeY(y + dy);
				if (!open(nx, ny))
					continue;
				const int step = axes.u(x, y) + axes.stepU(dx, dy);
				const int nw = w + (step < 0 ? -1 : step >= length ? 1 : 0);
				const int n = ny * width + nx;
				if (winding[n] == kUnreached)
				{
					winding[n] = nw;
					reached.push_back(n);
				}
				else if (winding[n] != nw)
				{
					wraps = true;
				}
			}
	}
	return wraps;
}

// The ring's defining guarantees, checked on the finished world rather than trusted. Walking from
// colony 0's workers (water, buildings and every resource block the way; units don't, since they
// move) must reach every other colony and must close a loop around the map along the belt. And the
// seas on either side of the belt must be one ocean: some body of water wraps around the map
// beside it, so the belt can't have swallowed the ocean anywhere.
std::string validateWorld(const GenerationWorld &game, const GenerationContext &context)
{
	const GenerationMap &map = game.map;
	const int width = map.getW(), height = map.getH(), teams = context.request.nbTeams;
	const Axes axes = axesFor(width, height);
	const size_t area = size_t(width) * height;
	std::vector<std::vector<int>> workers(std::max(teams, 1));
	for (int team = 0; team < teams; ++team)
		workers[team] = unitTilesByTeam(map, teams)[team];
	if (teams < 1 || workers[0].empty())
		return "Colony 0 has no workers to walk the belt.";
	std::vector<int> winding(area, kUnreached), reached;
	const bool beltWraps = floodAround(
		map, axes, workers[0], [&map](int x, int y)
		{ return map.terrainPropertiesAt(x, y).walkable && !map.isResource(x, y) && map.getBuilding(x, y) == NOGBID; },
		winding, reached);
	for (int team = 1; team < teams; ++team)
	{
		bool arrived = false;
		for (int i : workers[team])
			arrived = arrived || winding[i] != kUnreached;
		if (!arrived)
			return "Colony " + std::to_string(team) + " cannot walk to colony 0 along the belt.";
	}
	if (!beltWraps)
		return "The walkable belt does not reach all the way around the map.";

	std::fill(winding.begin(), winding.end(), kUnreached);
	bool oceanWraps = false;
	for (int i = 0; i < int(area) && !oceanWraps; ++i)
		if (winding[i] == kUnreached && map.terrainPropertiesAt(powerOfTwoRemainder(i, width), i / width).swimmable)
			oceanWraps = floodAround(
				map, axes, {i}, [&map](int x, int y) { return map.terrainPropertiesAt(x, y).swimmable; },
				winding, reached);
	if (!oceanWraps)
		return "The seas beside the belt do not meet around the map.";

	const Torus t(map);
	const int n = t.size();
	const auto at = [&](int i)
	{ return "(" + std::to_string(t.remainderX(i)) + ", " + std::to_string(i / width) + ")"; };
	// Deep water keeps its shallows: every vertex round a deep one is water.
	for (int i = 0; i < n; ++i)
	{
		if (terrainGroup(map.vertexTerrainAt(t.remainderX(i), i / width)) != TerrainGroup::DeepWater)
			continue;
		for (int dy = -1; dy <= 1; ++dy)
			for (int dx = -1; dx <= 1; ++dx)
			{
				const TerrainGroup g =
					terrainGroup(map.vertexTerrainAt(t.x(t.remainderX(i) + dx), t.y(i / width + dy)));
				if (g != TerrainGroup::Water && g != TerrainGroup::DeepWater)
					return "Deep water touches land at " + at(i) + ".";
			}
	}
	// No deposit on a tile with a road corner (the engine refuses one there, but a check is cheap).
	for (int y = 0; y < height; ++y)
		for (int x = 0; x < width; ++x)
		{
			if (!map.isResource(x, y))
				continue;
			for (int dy = 0; dy <= 1; ++dy)
				for (int dx = 0; dx <= 1; ++dx)
					if (terrainGroup(map.vertexTerrainAt(t.x(x + dx), t.y(y + dy))) == TerrainGroup::Paths)
						return "A deposit stands on the road at " + at(t.at(x, y)) + ".";
		}
	// No body of rock or boulders (blocked tiles with such a corner, terrain only) parts the walkable
	// ground round it.
	std::vector<unsigned char> walkable(size_t(n), 0), rock(size_t(n), 0);
	for (int y = 0; y < height; ++y)
		for (int x = 0; x < width; ++x)
		{
			const int i = t.at(x, y);
			walkable[i] = map.terrainPropertiesAt(x, y).walkable;
			bool hard = false;
			for (int dy = 0; dy <= 1; ++dy)
				for (int dx = 0; dx <= 1; ++dx)
				{
					const TerrainGroup g = terrainGroup(map.vertexTerrainAt(t.x(x + dx), t.y(y + dy)));
					hard = hard || g == TerrainGroup::Ridges || g == TerrainGroup::Obstacles;
				}
			rock[i] = hard && !walkable[i] && !map.terrainPropertiesAt(x, y).swimmable;
		}
	std::vector<int> label;
	if (const int parting = partingGroup(t, walkable, rock, label); parting >= 0)
		for (int i = 0; i < n; ++i)
			if (label[i] == parting)
				return "Rock at " + at(i) + " cuts ground off.";
	// The plan again, from the request and the start positions: the quarry faces and the road.
	const RingWorldOptions options(context.request);
	if (!options.rockSpine && !options.ringRoad)
		return "";
	GenerationContext replay(context.request);
	const Stamp stamp = stampRing(axes, replay, options);
	std::vector<Home> homes;
	for (int team = 0; team < teams; ++team)
		homes.push_back({game.teamStart(team).x, game.teamStart(team).y});
	const Features features = planFeatures(stamp, t, axes, homes, options, replay);
	if (!features.failure.empty())
		return features.failure;
	if (const std::string fault =
			quarryFault(map, t, features.quarry, features.owner, teams, features.tors > 0 ? kTorFace : 0);
		!fault.empty())
		return fault;
	for (int i = 0; i < n; ++i)
		if (features.route[i])
			for (int dy = 0; dy <= 1; ++dy)
				for (int dx = 0; dx <= 1; ++dx)
					if (terrainGroup(map.vertexTerrainAt(t.x(t.remainderX(i) + dx), t.y(i / width + dy))) !=
						TerrainGroup::Paths)
						return "The ring road is not road at " + at(i) + ".";
	return "";
}
} // namespace

RingWorldOptions::RingWorldOptions(const GenerationRequest &r)
	: beltWidth(r.option("belt-width")), coastRoughness(r.option("coast-roughness")),
	  lakeDensity(r.option("lake-density")), resourceIslands(r.option("resource-islands")),
	  windingBelt(r.option("winding-belt") != 0), bothCoasts(r.option("both-coasts") != 0),
	  deepWater(r.option("deep-water") != 0), rockSpine(r.option("rock-spine") != 0),
	  ringRoad(r.option("ring-road") != 0), climate(r.option("climate")),
	  wheat(r.option("wheat-amount")), wood(r.option("wood-amount")),
	  stone(r.option("stone-amount")), algae(r.option("algae-amount")),
	  fruit(r.option("fruit-amount"))
{
}

GeneratorDefinition ringWorldDefinition()
{
	return {"ring-world",
			16,
			"Ring world",
			4,
			false,
			// Belt width is the share of the map's breadth the belt covers on average; lake density
			// is lakes per 4096 tiles of belt; resource islands are counted per 128x128 of map.
			{GeneratorControl{"belt-width", "Belt width", 30, 70, 5, 45, ControlGroup::Terrain}
				 .withSearchRange(40, 60),
			 GeneratorControl{"coast-roughness", "Coast roughness", 0, 100, 5, 50, ControlGroup::Terrain}
				 .withSearchRange(20, 75),
			 GeneratorControl{"lake-density", "Lake density", 0, 8, 1, 2, ControlGroup::Terrain}
				 .withSearchRange(1, 4),
			 GeneratorControl{"resource-islands", "Resource islands", 0, 10, 1, 2, ControlGroup::Resources}
				 .withSearchRange(1, 5),
			 // Off, the belt's centre line runs straight round the map.
			 GeneratorControl::toggle("winding-belt", "Winding belt", true, ControlGroup::Terrain)
				 .withSearchValues({0, 1}),
			 // Off, every colony starts on the same coast of the belt.
			 GeneratorControl::toggle("both-coasts", "Colonies on both coasts", true, ControlGroup::Layout)
				 .withSearchValues({0, 1}),
			 // On, the open ocean six or more tiles from any land is deep water: swimming across to
			 // the other coast or an island is slower and no algae grows there. The coastal shallows
			 // and every route on foot are the same either way.
			 GeneratorControl::toggle("deep-water", "Deep ocean", true, ControlGroup::Terrain)
				 .withSearchValues({0, 1}),
			 // On, a broken spine of rock runs down the middle of the belt, crossed by frequent
			 // passes, and every colony has a small rock tor with a quarry face of stone the same walk
			 // from its swarm. Off, no rock and no quarries.
			 GeneratorControl::toggle("rock-spine", "Rock spine", true, ControlGroup::Layout)
				 .withSearchValues({0, 1}),
			 // On, a road (twice the walking speed, buildable, nothing grows on it) runs from colony
			 // to colony all the way round the world.
			 GeneratorControl::toggle("ring-road", "Ring road", true, ControlGroup::Layout)
				 .withSearchValues({0, 1}),
			 // Looks only: Temperate (spring meadows, grey rock, granite headlands), Steppe (dry
			 // grass, hoodoos, sandstone roads) or Tropical (lush grass, dark water, mossy banks).
			 // All play alike.
			 GeneratorControl::choice("climate", "Climate", {"Random", "Temperate", "Steppe", "Tropical"}, 0,
									  ControlGroup::Terrain)
				 .withSearchValues({1, 2, 3}),
			 // The ambient scatter's wheat, wood, stone and fruit and the shallows' algae. Every
			 // home's starter patches, each island's prize and the quarry faces stay as they are.
			 GeneratorControl::percentage("wheat-amount", "Wheat amount"),
			 GeneratorControl::percentage("wood-amount", "Wood amount"),
			 GeneratorControl::percentage("stone-amount", "Stone amount"),
			 GeneratorControl::percentage("algae-amount", "Algae amount"),
			 GeneratorControl::percentage("fruit-amount", "Fruit amount")},
			generate,
			true,
			validate,
			validateWorld,
			// The belt's start-bearing stretches are periodic along its length, not arranged around
			// one shared centre, so this is a stamped lattice rather than a repeated wedge.
			{"terrain:natural", "feature:ocean", "feature:lakes", "feature:mountains", "style:wide-open",
			 "fairness:stamped-lattice"}};
}
