// SPDX-License-Identifier: GPL-3.0-or-later
#include "RingWorldGenerator.h"
#include "Game.h"
#include "GenerationContext.h"
#include "Grid.h"
#include "Geometry.h"
#include "LatticeNoise.h"
#include "Pipeline.h"
#include "Planting.h"
#include "Resources.h"
#include "Settlements.h"
#include "Sketch.h"
#include "Unit.h"
#include <algorithm>
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
// seam can't be seen. Terrain is written straight to the undermap with an order-independent beach
// pass rather than Map::controlSand(), whose in-place raster scan makes shorelines depend on scan
// order.
//
// WHY IT PLAYS WELL (docs/map-generators/GAME_RULES_FOR_MAP_DESIGN.md). Every colony has the same
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

double unitDraw(std::mt19937 &rng)
{
	return rng() / 4294967296.0;
}

// Belt coordinates: u runs along the belt and v across it. The belt follows the map's longer
// axis, so a tall map gets one that wraps top to bottom.
struct Axes
{
	int width, height;
	bool alongX;
	int length() const { return alongX ? width : height; }
	int breadth() const { return alongX ? height : width; }
	int u(int x, int y) const { return alongX ? x : y; }
	int v(int x, int y) const { return alongX ? y : x; }
	int stepU(int dx, int dy) const { return alongX ? dx : dy; }
};

Axes axesFor(int width, int height)
{
	return {width, height, width >= height};
}

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
		const int x = at % width, y = at / width;
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
					terrain[size_t((y + dy + height) % height) * width + (x + dx + width) % width] =
						WATER;
		lakes.push_back({x, y, reach});
	}
}

// A swarm anchored at (x, y) needs pure grass under its 4x4 footprint and dry, unoccupied ground in
// the ring around it where its workers will stand.
bool siteFits(const Map &map, int x, int y)
{
	for (int dy = -1; dy <= 4; ++dy)
		for (int dx = -1; dx <= 4; ++dx)
		{
			const int nx = map.normalizeX(x + dx), ny = map.normalizeY(y + dy);
			const bool footprint = dx >= 0 && dx < 4 && dy >= 0 && dy < 4;
			if (footprint ? !map.isGrass(nx, ny) : map.isWater(nx, ny))
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
bool placeColonies(Game &game, GenerationContext &context, const Belt &belt, bool bothCoasts)
{
	Map &map = game.map;
	const Axes &axes = belt.axes;
	const int width = axes.width, height = axes.height, length = axes.length();
	const int teams = context.request.nbTeams;
	const size_t area = size_t(width) * height;
	std::vector<unsigned char> water(area), dry(area);
	for (int y = 0; y < height; ++y)
		for (int x = 0; x < width; ++x)
		{
			water[size_t(y) * width + x] = map.isWater(x, y);
			dry[size_t(y) * width + x] = !map.isWater(x, y);
		}
	const std::vector<int> shore = stepsFrom(Torus{width, height}, water, dry);
	// Only the belt itself: an island or a stretch of rough coast cut off at sea can offer the same
	// shore, but a colony there would have no land neighbours at all. The spine is always dry, so
	// the belt is whatever land is reached from it.
	std::vector<unsigned char> spine(area, 0);
	for (int y = 0; y < height; ++y)
		for (int x = 0; x < width; ++x)
			spine[size_t(y) * width + x] =
				!map.isWater(x, y) && std::abs(belt.across(x, y)) < kSpineHalf - 1;
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
		std::vector<unsigned char> home(area, 0);
		// The settlement's ground: the 10x10 square round the 4x4 swarm, 3 tiles each way.
		for (int dy = -3; dy <= 6; ++dy)
			for (int dx = -3; dx <= 6; ++dx)
			{
				const int nx = map.normalizeX(bestX + dx), ny = map.normalizeY(bestY + dy);
				if (!map.isWater(nx, ny))
					home[size_t(ny) * width + nx] = 1;
			}
		if (!placeSettlement(game, context, team, home, MapGeneratorPoint(bestX, bestY), "starts"))
			return false;
		placed.emplace_back(bestX, bestY);
	}
	return true;
}

// Every home's starter kit: a wheat and a wood patch on the most fertile ground a short walk from
// the swarm, and a little stone further inland.
void furnishHomes(Game &game, GenerationContext &context)
{
	Map &map = game.map;
	const int width = map.getW(), height = map.getH(), teams = context.request.nbTeams;
	const Torus t{width, height};
	const size_t area = size_t(width) * height;
	std::vector<unsigned char> water(area), dry(area);
	for (int y = 0; y < height; ++y)
		for (int x = 0; x < width; ++x)
		{
			water[size_t(y) * width + x] = map.isWater(x, y);
			dry[size_t(y) * width + x] = !map.isWater(x, y);
		}
	const std::vector<unsigned char> reserved = swarmSurroundings(t, context);
	const std::vector<int> shore = stepsFrom(t, water, dry);
	const auto eligible = [&](int i)
	{ return !reserved[i] && clearGround(map, i % width, i / width); };
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
				if (avoid >= 0 && t.dist2(i % width, i / width, avoid % width, avoid / width) < 36)
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
			growPatch(map, t, wheat, CORN, kHomeWheat, eligible);
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
// around the map. Almost always nothing is in the way and nothing is cleared.
bool openBeltRoad(Game &game, GenerationContext &context, const Axes &axes)
{
	Map &map = game.map;
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
	{ return int(std::lround((position - axes.u(i % width, i / width)) / length)); };
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
			const int w = node / area - 1, i = node % area, x = i % width, y = i / width;
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
					if (map.isWater(nx, ny) || map.getBuilding(nx, ny) != NOGBID)
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
			if (map.isResource(i % width, i / width))
				map.setNoResource(i % width, i / width, 1);
		}
	}
	return true;
}

bool generate(Game &game, GenerationContext &context)
{
	context.stage = "ring layout";
	const RingWorldOptions options(context.request);
	Map &map = game.map;
	const int width = map.getW(), height = map.getH(), teams = context.request.nbTeams;
	map.makeHomogenMap(WATER);
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
	const Belt belt = shapeBelt(axes, context, options);
	TerrainSketch terrain(size_t(width) * height, WATER);
	for (int y = 0; y < height; ++y)
		for (int x = 0; x < width; ++x)
			if (belt.land(x, y))
				terrain[size_t(y) * width + x] = GRASS;
	carveLakes(terrain, belt, context, options.lakeDensity);
	// The control counts islands per 128x128 of map, never fewer than one when it is on at all.
	const int wantedIslands =
		options.resourceIslands > 0
			? std::max(1, int(std::lround(options.resourceIslands * double(t.size()) / 16384.0)))
			: 0;
	// 40 candidate draws per island; each keeps 7 tiles of open water from every coast, too wide
	// for any beach to bridge, so islands are reached only by swimming.
	const std::vector<Island> islands =
		raiseIslands(terrain, t, context, {"islands", wantedIslands, 40, kIslandMoat});
	layBeaches(terrain, t);
	writeUndermap(map, terrain);

	context.stage = "ring colonies";
	if (!placeColonies(game, context, belt, options.bothCoasts))
		return false;

	context.stage = "ring resources";
	furnishHomes(game, context);
	stockIslands(map, context, islands, "islands");
	// The same ambient layer as Fjord continent: corn:wood 2:1, some stone, rare fruit. Algae is
	// seeded along the shallows below instead, since the shared band only scatters over land.
	scatterResources(game, context,
					 {/*corn=*/int(scaledCount(24, options.wheat)),
					  /*wood=*/int(scaledCount(12, options.wood)),
					  /*stone=*/int(scaledCount(10, options.stone)), /*algae=*/0,
					  /*fruit=*/int(scaledCount(3, options.fruit))});
	// Algae 2 to 5 tiles off the shore, one clump per 90 tiles of that band: beside the beaches it
	// needs in order to regrow.
	seedAlgae(map, context, t, "resources", options.algae, AlgaeBand::shallows(2, 5));
	// The branch-wide start promises: wheat within 24 steps and wood within 32 of every swarm, with
	// 6 tiles clear round it.
	secureStartingCrops(game, context, t, 24, 32, 6);
	// The scatter above is sized by the resource amounts, and at the top of their range it can wall
	// a colony into its own clearing with nowhere to build; the reopened colony is cleared again.
	if (reopenCrampedStarts(game, context,
							{options.wheat, options.wood, options.stone, options.algae,
							 options.fruit},
							24, 32, 6))
		clearAroundSwarms(map, context, t);

	context.stage = "ring road";
	return openBeltRoad(game, context, axes);
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
bool floodAround(const Map &map, const Axes &axes, const std::vector<int> &sources, Open open,
				 std::vector<int> &winding, std::vector<int> &reached)
{
	const int width = map.getW(), length = axes.length();
	reached.clear();
	if (sources.empty())
		return false;
	const int anchor = axes.u(sources[0] % width, sources[0] / width);
	for (int i : sources)
	{
		if (winding[i] != kUnreached)
			continue;
		// Each source starts at its image nearest the first, so a colony astride the seam is
		// consistent.
		winding[i] = int(std::lround(double(anchor - axes.u(i % width, i / width)) / length));
		reached.push_back(i);
	}
	bool wraps = false;
	for (size_t head = 0; head < reached.size(); ++head)
	{
		const int i = reached[head], x = i % width, y = i / width, w = winding[i];
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
std::string validateWorld(const Game &game, const GenerationContext &context)
{
	const Map &map = game.map;
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
		{ return !map.isWater(x, y) && !map.isResource(x, y) && map.getBuilding(x, y) == NOGBID; },
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
		if (winding[i] == kUnreached && map.isWater(i % width, i / width))
			oceanWraps = floodAround(
				map, axes, {i}, [&map](int x, int y) { return map.isWater(x, y); }, winding,
				reached);
	if (!oceanWraps)
		return "The seas beside the belt do not meet around the map.";
	return "";
}
} // namespace

RingWorldOptions::RingWorldOptions(const GenerationRequest &r)
	: beltWidth(r.option("belt-width")), coastRoughness(r.option("coast-roughness")),
	  lakeDensity(r.option("lake-density")), resourceIslands(r.option("resource-islands")),
	  windingBelt(r.option("winding-belt") != 0), bothCoasts(r.option("both-coasts") != 0),
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
			1,
			false,
			// Belt width is the share of the map's breadth the belt covers on average; lake density
			// is lakes per 4096 tiles of belt; resource islands are counted per 128x128 of map.
			{{"belt-width", "Belt width", 30, 70, 5, 45, ControlGroup::Terrain},
			 {"coast-roughness", "Coast roughness", 0, 100, 5, 50, ControlGroup::Terrain},
			 {"lake-density", "Lake density", 0, 8, 1, 2, ControlGroup::Terrain},
			 {"resource-islands", "Resource islands", 0, 10, 1, 2, ControlGroup::Resources},
			 // Off, the belt's centre line runs straight round the map.
			 GeneratorControl::toggle("winding-belt", "Winding belt", true, ControlGroup::Terrain),
			 // Off, every colony starts on the same coast of the belt.
			 GeneratorControl::toggle("both-coasts", "Colonies on both coasts", true,
									  ControlGroup::Layout),
			 // The ambient scatter's wheat, wood, stone and fruit and the shallows' algae. Every
			 // home's starter patches and each island's prize stay as they are.
			 GeneratorControl::percentage("wheat-amount", "Wheat amount"),
			 GeneratorControl::percentage("wood-amount", "Wood amount"),
			 GeneratorControl::percentage("stone-amount", "Stone amount"),
			 GeneratorControl::percentage("algae-amount", "Algae amount"),
			 GeneratorControl::percentage("fruit-amount", "Fruit amount")},
			generate,
			true,
			validate,
			validateWorld};
}
