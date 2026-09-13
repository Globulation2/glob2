// SPDX-License-Identifier: GPL-3.0-or-later
#include "ContestedCommonsGenerator.h"
#include "Distances.h"
#include "Game.h"
#include "GenerationContext.h"
#include "Geometry.h"
#include "GlobalContainer.h"
#include "Grid.h"
#include "Regions.h"
#include "Resources.h"
#include "Settlements.h"
#include "Topology.h"
#include "Unit.h"
#include <algorithm>
#include <cmath>
#include <limits>
using namespace MapGeneration;

// Contested commons (id "contested-commons", legacy id 9): small home islands round one large rich
// island in the middle, the commons, behind a moat crossed by a few bridges.
//
// HISTORY. Written at the start of this branch (September 2026) on the 2008 area-grid toolkit, and
// made the lobby's first and default landscape. Its point spreading first ran into a fixed
// evaluation budget that refused more than four colonies at 256x256 and any colony count at 512;
// the whole-region search was rewritten to keep each tile's nearest two weighted distances, which
// generates every map it used to and the ones it refused. It gained resource amounts and switches
// later, and its generate was split into the five stages below.
//
// WHAT THE MAP IS, AND WHY. Every colony has a home island big enough to start an economy on but
// too small to win on: a wheat field, a treeline, a small quarry and room for a base. The commons
// holds everything worth fighting over - many fields and groves, a big quarry, and fruit of all
// three kinds - behind a forced ring of water with a few bridges. So the whole game is about the
// commons: who gets onto it first, who holds the bridges, and later who swims round them.
//
// GAME RULES IT LEANS ON (docs/map-generators/GAME_RULES_FOR_MAP_DESIGN.md):
// - Water blocks walking until a colony can swim: the moat makes the bridges chokepoints, and the
//   moat is forced to water so no colony's peninsula can reach the commons on foot unfairly.
// - Fruit is a weapon: fruit groves exist only on the commons, so holding it is how a colony wins
//   hungry enemy units to its inns.
// - Stone never runs out: one big quarry on the commons is a permanent strategic site.
// - Resources block movement: the commons' fields are small patches bordered by open zones, so the
//   island stays walkable and no single forest walls off part of it.
// - Grass may not touch water: controlSand rings every coast after painting.
//
// Fairness: every home island is the same size and every colony is kept the same distance from the
// commons by the joint spread in disperseSeeds, but island shapes, zone layouts and bridge angles
// are random, so it is fair statistically; the lobby keeps the best-scoring of several seeds.
namespace
{
void createJaggedIsland(Map &map, GenerationContext &context, std::vector<int> &grid, int area,
						int x, int y, int radius, double roughness)
{
	stampRoughDisc(grid, map.getW(), map.getH(), area, x, y, radius, roughness, context, "coast");
}
// What the stages of a roll hand each other: the working area grid and the next free area
// number, where the colonies and the commons were seeded, and how big everything came out.
struct Layout
{
	std::vector<int> grid;
	int areaNumber = 1;
	std::vector<MapGeneratorPoint> teamPoints;
	std::vector<int> teamAreaNumbers;
	MapGeneratorPoint commonsCenter{0, 0};
	int homeRadius = 0, commonsRadius = 0, moatWidth = 0;
	int commonsAreaNumber = 0, bridgeAreaNumber = 0;
	explicit Layout(const Map &map) : grid(size_t(map.getW()) * map.getH(), 0) {}
};

// Spread the colony seeds and the commons seed jointly, then tell them apart.
static bool disperseSeeds(Game &game, GenerationContext &context, Layout &L)
{
	// Spread the team seeds AND the commons seed jointly, as one dispersion problem, instead
	// of placing the teams first and hunting for a leftover gap afterward: the commons is an
	// (N+1)th point in the exact same "push apart until nobody can move to a better spot"
	// search, but with a lighter weight than the teams. In the per-point best-response score
	// (nearest-neighbor distance squared, times that neighbor's weight) a heavier neighbor's
	// term is easier to satisfy and a lighter one's is the one still binding, so every team
	// preferentially keeps its distance from the low-weight commons over from the other
	// (high-weight, already-"satisfied") teams. That's a real trade: teams end up a bit
	// closer to each other than a teams-only spread would put them, in exchange for a much
	// bigger gap around the shared island -- which is the point.
	const int teamWeight = 2;
	const int commonsWeight = 1;
	std::vector<MapGeneratorPoint> allPoints;
	std::vector<int> allWeights;
	for (int i = 0; i < context.request.nbTeams; ++i)
	{
		allPoints.push_back(MapGeneratorPoint(0, 0));
		allWeights.push_back(teamWeight);
	}
	allPoints.push_back(MapGeneratorPoint(0, 0)); // the commons seed
	allWeights.push_back(commonsWeight);

	if (splitUpPoints(game.map, context, L.grid, 0, allPoints, allWeights,
					  PointSearch::WholeRegion) == 0)
		return false;

	// splitUpPoints shuffles the whole array, but weights travel with their point, so the
	// one point that kept commonsWeight is still identifiable regardless of where it landed.
	for (unsigned int i = 0; i < allPoints.size(); ++i)
	{
		if (allWeights[i] == commonsWeight)
		{
			L.commonsCenter = allPoints[i];
		}
		else
		{
			L.teamPoints.push_back(allPoints[i]);
			L.teamAreaNumbers.push_back(L.areaNumber);
			L.areaNumber += 1;
		}
	}
	return true;
}

// Size the home islands and the commons off the room the dispersion bought, draw them into the
// grid and lay the bridge strips across the moat.
static void sizeIslands(Game &game, GenerationContext &context,
						const ContestedCommonsOptions &options, Layout &L)
{
	const int W = game.map.getW();
	// Home islands are sized off actual team-to-team separation, not the joint figure
	// splitUpPoints returns (which is dominated by the deliberately-tighter team/commons gap).
	int minDist = std::numeric_limits<int>::max();
	for (unsigned int i = 0; i < L.teamPoints.size(); ++i)
		for (unsigned int j = i + 1; j < L.teamPoints.size(); ++j)
			minDist = std::min(minDist, (int)std::sqrt((double)game.map.warpDistSquare(
											L.teamPoints[i].x, L.teamPoints[i].y, L.teamPoints[j].x,
											L.teamPoints[j].y)));
	if (minDist == std::numeric_limits<int>::max()) // a single team: no pair to measure
		minDist = (int)std::sqrt((double)game.map.warpDistSquare(
			L.teamPoints[0].x, L.teamPoints[0].y, L.commonsCenter.x, L.commonsCenter.y));

	// Jaggedness is RadialShape's roughness: harmonics whose amplitudes add up to at most roughness
	// times stampRoughDisc's amplitude cap of 1.4, so a shape of radius r reaches at most
	// r * (1 + roughness * 1.4) - hence the 1.4 below. 0.35 and 0.32 give coasts with clear
	// headlands and bays (up to about 50% and 45% past the radius) without spikes thin enough to be
	// pure sand; the commons is a touch smoother because it is much larger, and the same roughness
	// on a big radius makes bigger bays.
	//
	// Both islands are jagged, which means both can reach noticeably past their nominal
	// radius in a lucky direction -- comfortably more than a flat few-tile fudge factor once
	// the commons radius is large. Size everything off the worst case each jaggedness value
	// can actually produce, not off the ideal circle, so the sizing math and the moat's
	// physical enforcement (below) agree on where the real boundary is.
	const double teamJaggedness = options.jaggedCoasts ? 0.35 : 0.0;
	const double commonsJaggedness = options.jaggedCoasts ? 0.32 : 0.0;

	// Home size is a share of the closest colony-to-colony distance (25% by default), so homes
	// scale with how far apart the spread could put the colonies; never under 10 tiles of radius, a
	// floor that keeps homes usable on a small or crowded map.
	L.homeRadius = std::max(10, minDist * options.homeSize / 100);
	for (unsigned int i = 0; i < L.teamPoints.size(); ++i)
		createJaggedIsland(game.map, context, L.grid, L.teamAreaNumbers[i], L.teamPoints[i].x,
						   L.teamPoints[i].y, L.homeRadius, teamJaggedness);

	// Real Euclidean room at the chosen commons spot: the same metric createOval/
	// createJaggedIsland use to draw the islands, measured straight from the seed points.
	int minGapSquared = std::numeric_limits<int>::max();
	for (unsigned int i = 0; i < L.teamPoints.size(); ++i)
		minGapSquared =
			std::min(minGapSquared, game.map.warpDistSquare(L.commonsCenter.x, L.commonsCenter.y,
															L.teamPoints[i].x, L.teamPoints[i].y));
	const int centerGap = (int)std::sqrt((double)minGapSquared);

	// Budget the gap as: nearest team's worst-case reach, a real moat, the commons' own
	// worst-case reach, and a small flat buffer on top of all that jaggedness math.
	const int teamReach = (int)std::ceil(L.homeRadius * (1.0 + teamJaggedness * 1.4));
	// The moat is a share of the home radius (35% by default) but never under 4 tiles, so after the
	// beaches controlSand adds on both shores a clear band of open water remains.
	L.moatWidth = std::max(4, L.homeRadius * options.moatWidth / 100);
	const int roomAvailable = std::max(0, centerGap - teamReach - L.moatWidth - 3);
	const int roomLimitedRadius =
		std::max(L.homeRadius, (int)(roomAvailable / (1.0 + commonsJaggedness * 1.4)));

	// The commons is the whole point of the map, so it should use up the room the joint
	// dispersion just bought it: aim for 4x a home island's radius (roughly what's actually
	// available once teams are pushed off it -- measured, not guessed), and let the min()
	// below be the real safety net for whatever a given layout can't quite support.
	const int desiredCommonsRadius = L.homeRadius * options.commonsSize / 100;
	L.commonsRadius = std::min(desiredCommonsRadius, roomLimitedRadius);
	const int bridgeCount = options.bridgeCount;

	std::vector<int> bridgeAngles;
	for (int i = 0; i < bridgeCount; ++i)
		bridgeAngles.push_back(context.bounded("layout", 360));

	L.commonsAreaNumber = L.areaNumber;
	L.areaNumber += 1;
	createJaggedIsland(game.map, context, L.grid, L.commonsAreaNumber, L.commonsCenter.x,
					   L.commonsCenter.y, L.commonsRadius, commonsJaggedness);

	// Extend the commons island out across the moat along each bridge angle, so those strips
	// get boosted into land by the heightmap pass below just like the island itself. Bridges
	// get their own area number, kept separate from the island body, so the resource-field
	// split further down only ever touches the round island, never a narrow connecting strip.
	L.bridgeAreaNumber = L.areaNumber;
	L.areaNumber += 1;
	// Each bridge is a strip 3 tiles wide along a random angle, from 2 tiles inside the commons'
	// radius to 2 tiles past the moat, so it always joins solid land at both ends whatever the
	// coast's jaggedness does. Random angles mean bridges need not face any particular colony, so a
	// colony may be far from its nearest bridge: part of the contest.
	for (unsigned int b = 0; b < bridgeAngles.size() && options.moatBridges; ++b)
	{
		double theta = bridgeAngles[b] * kPi / 180.0;
		for (int r = L.commonsRadius - 2; r <= L.commonsRadius + L.moatWidth + 2; ++r)
		{
			int cx = L.commonsCenter.x + (int)round(r * cos(theta));
			int cy = L.commonsCenter.y + (int)round(r * sin(theta));
			for (int dx = -1; dx <= 1; ++dx)
			{
				for (int dy = -1; dy <= 1; ++dy)
				{
					int nx = game.map.normalizeX(cx + dx);
					int ny = game.map.normalizeY(cy + dy);
					if (L.grid[ny * W + nx] == 0)
						L.grid[ny * W + nx] = L.bridgeAreaNumber;
				}
			}
		}
	}
}

// Turn the grid into terrain: noise, islands raised to land, the moat forced back to water.
static void paintTerrain(Game &game, GenerationContext &context,
						 const ContestedCommonsOptions &options, const Layout &L)
{
	const int W = game.map.getW();
	const int H = game.map.getH();
	// Base heightmap: noise everywhere, boosted wherever land has been carved out, so every island
	// (and every bridge strip) comes out dry and everything else stays ocean.
	//
	// Sea is 40 plus noise of -20 to +19, at most 59: always under the water line (70). Claimed
	// land rises by 60 to 80..119: land everywhere, sand where the noise dips it under 85 and grass
	// above, so island edges get patches of wider beach, and elsewhere the one-tile sand ring
	// controlSand adds. The 60 rise is what guarantees an island's shape: no claimed tile can ever
	// be water, and no unclaimed one can ever be land.
	std::vector<int> heights(W * H, 40);
	adjustHeightmapFromPerlinNoise(game.map, context, heights, 20);
	for (int y = 0; y < H; ++y)
		for (int x = 0; x < W; ++x)
			if (L.grid[y * W + x] != 0)
				heights[y * W + x] += 60;

	for (int x = 0; x < W; ++x)
	{
		for (int y = 0; y < H; ++y)
		{
			int h = heights[y * W + x];
			if (h < 70)
				game.map.setUMatPos(x, y, WATER, 1);
			else if (h < 85)
				game.map.setUMatPos(x, y, SAND, 1);
			else
				game.map.setUMatPos(x, y, GRASS, 1);
		}
	}

	// Force the moat ring: this band is water no matter what the noise -- or a team's own
	// jagged coastline -- says, so the commons always reads as an island you have to cross a
	// chokepoint to reach. Only the commons body and its bridges are exempt here: a team's
	// island is deliberately just as jagged as the commons, so a peninsula can wander into
	// this ring by chance, and only checking "is this tile claimed by anything" would let
	// that team walk straight onto the commons while everyone else has to swim or use a
	// bridge. Flooding it back closes that gap without another team ever being able to tell.
	for (int y = 0; y < H; ++y)
	{
		for (int x = 0; x < W; ++x)
		{
			if (L.grid[y * W + x] == L.commonsAreaNumber || L.grid[y * W + x] == L.bridgeAreaNumber)
				continue;
			double r = sqrt((double)Torus{W, H}.dist2(x, y, L.commonsCenter.x, L.commonsCenter.y));
			if (r >= L.commonsRadius - 1 && r < L.commonsRadius + L.moatWidth)
			{
				game.map.setUMatPos(x, y, WATER, 1);
				// A quarter of the ring gets algae. The algae amount thins or thickens that with
				// draws from a stream of its own, so the layout's draws stay the same.
				bool seeded = context.bounded("layout", 4) == 0;
				if (options.algae < 100)
					seeded = seeded && int(context.bounded("commons-algae", 100)) < options.algae;
				else if (options.algae > 100)
					seeded =
						seeded || int(context.bounded("commons-algae", 300)) < options.algae - 100;
				if (seeded)
					game.map.setResource(x, y, ALGA, 1);
			}
		}
	}
	game.map.controlSand();
}

// Split the commons into zones and give each a role: wood, wheat, the quarry, fruit, or open.
static void stockCommons(Game &game, GenerationContext &context,
						 const ContestedCommonsOptions &options, Layout &L)
{
	const int W = game.map.getW();
	const int H = game.map.getH();
	// Stock the commons with real fields, not a scattering of single tiles -- but the commons
	// is several times the area of a home island, so splitting it into the same handful of
	// zones a home island uses would read as one giant forest next to one giant farm. Scale
	// the zone count with its actual size, and shuffle which zone gets which resource, so it
	// comes out as a patchwork of many small groves and fields instead of two big blobs.
	{
		double areaRatio =
			(double)(L.commonsRadius * L.commonsRadius) / (double)(L.homeRadius * L.homeRadius);
		// Seven zones per home island's worth of radius (the square root of the area ratio): a
		// commons four home radii across gets 28, capped at 24 so each zone keeps enough tiles to
		// be a real field, and never under 7, the fewest that still hold two wood, two wheat, a
		// quarry, a grove and an open zone.
		int zoneCount = (int)std::round(7.0 * std::sqrt(std::max(1.0, areaRatio)));
		zoneCount = std::max(7, std::min(24, zoneCount));

		std::vector<int> zoneWeights(zoneCount, 1);
		std::vector<int> zoneAreas;
		const int firstZoneArea = L.areaNumber;
		for (int z = 0; z < zoneCount; ++z)
			zoneAreas.push_back(L.areaNumber++);
		if (divideUpArea(game.map, context, L.grid, L.commonsAreaNumber, zoneWeights, zoneAreas))
		{
			// Which zone borders which: zoneAreas is a contiguous run of area numbers, so a
			// tile's zone index is just its area number minus the first one. Two adjacent
			// tiles with different zone indices means those two zones share a border.
			std::vector<std::vector<bool>> adjacent(zoneCount, std::vector<bool>(zoneCount, false));
			for (int y = 0; y < H; ++y)
			{
				for (int x = 0; x < W; ++x)
				{
					int a = L.grid[y * W + x] - firstZoneArea;
					if (a < 0 || a >= zoneCount)
						continue;
					int xr = game.map.normalizeX(x + 1);
					int b = L.grid[y * W + xr] - firstZoneArea;
					if (b >= 0 && b < zoneCount && b != a)
						adjacent[a][b] = adjacent[b][a] = true;
					int yd = game.map.normalizeY(y + 1);
					int c = L.grid[yd * W + x] - firstZoneArea;
					if (c >= 0 && c < zoneCount && c != a)
						adjacent[a][c] = adjacent[c][a] = true;
				}
			}

			// Roughly 2/7 of the zones become wood, 2/7 corn, one zone (any zone) becomes the
			// single quarry, ~1/7 get a scattered fruit grove, and the rest are left open. Roles
			// are assigned zone by zone in random order, each one picking randomly among the
			// roles that no already-decided *neighboring* zone already has -- so a big forest
			// or farm can never accidentally form just because two same-role zones happened to
			// land next to each other; every patch is bordered by something else.
			enum
			{
				ROLE_OPEN = 0,
				ROLE_WOOD = 1,
				ROLE_CORN = 2,
				ROLE_FRUIT = 3,
				ROLE_QUARRY = 4
			};
			// The amount controls scale how many zones take each role.
			int woodTarget = int(scaledCount(std::max(1, zoneCount * 2 / 7), options.wood));
			int cornTarget = int(scaledCount(std::max(1, zoneCount * 2 / 7), options.wheat));
			int fruitTarget = int(scaledCount(std::max(1, zoneCount / 7), options.fruit));

			std::vector<int> order(zoneCount);
			for (int z = 0; z < zoneCount; ++z)
				order[z] = z;
			for (int z = zoneCount - 1; z > 0; --z)
			{
				int j = context.bounded("layout", z + 1);
				std::swap(order[z], order[j]);
			}

			std::vector<int> role(zoneCount, -1);
			role[order[0]] = ROLE_QUARRY;
			int woodCount = 0, cornCount = 0, fruitCount = 0;
			for (int oi = 1; oi < zoneCount; ++oi)
			{
				int z = order[oi];
				std::vector<bool> blocked(4, false); // by role, excluding QUARRY
				for (int other = 0; other < zoneCount; ++other)
					if (adjacent[z][other] && role[other] >= ROLE_OPEN && role[other] <= ROLE_FRUIT)
						blocked[role[other]] = true;

				std::vector<int> candidates;
				if (!blocked[ROLE_WOOD] && woodCount < woodTarget)
					candidates.push_back(ROLE_WOOD);
				if (!blocked[ROLE_CORN] && cornCount < cornTarget)
					candidates.push_back(ROLE_CORN);
				if (!blocked[ROLE_FRUIT] && fruitCount < fruitTarget)
					candidates.push_back(ROLE_FRUIT);
				if (!blocked[ROLE_OPEN])
					candidates.push_back(ROLE_OPEN);
				if (candidates.empty())
					candidates.push_back(ROLE_OPEN); // every role taken by a neighbor: stay open

				int chosen = candidates[context.bounded("layout", candidates.size())];
				role[z] = chosen;
				if (chosen == ROLE_WOOD)
					woodCount++;
				else if (chosen == ROLE_CORN)
					cornCount++;
				else if (chosen == ROLE_FRUIT)
					fruitCount++;
			}

			for (int z = 0; z < zoneCount; ++z)
			{
				std::vector<MapGeneratorPoint> pts;
				getAllPoints(game.map, L.grid, zoneAreas[z], pts);
				if (role[z] == ROLE_QUARRY)
				{
					// One compact quarry, not a scatter: a single setResource call already grows a
					// solid square footprint from its center, which reads as an actual deposit and
					// leaves the rest of its zone clear to build on. Map::setResource's size n
					// covers a square of (n / 2) * 2 + 1 tiles a side, so 5 is a 5x5 quarry: nearly
					// three times the area of a home's 3x3 quarry, the commons' permanent stone.
					if (!pts.empty())
					{
						MapGeneratorPoint quarry = pts[context.bounded("layout", pts.size())];
						setScaledResource(game.map, quarry.x, quarry.y, STONE, 5, options.stone);
					}
				}
				else if (role[z] == ROLE_WOOD)
					fillInResource(game.map, context, pts, WOOD, 2);
				else if (role[z] == ROLE_CORN)
					fillInResource(game.map, context, pts, CORN, 2);
				else if (role[z] == ROLE_FRUIT)
				{
					// One fruit tree per 8 tiles of the zone, of random kinds: a grove open enough
					// to walk through, since fruit can never be cleared, and with several zones
					// likely to hold all three kinds between them.
					std::vector<MapGeneratorPoint> fruitPts = pts;
					chooseRandomPoints(game.map, context, fruitPts,
									   std::max(2, (int)pts.size() / 8));
					for (unsigned int i = 0; i < fruitPts.size(); ++i)
						game.map.setResource(fruitPts[i].x, fruitPts[i].y,
											 CHERRY + context.bounded("layout", 3), 1);
				}
				// else: left open.
			}
		}
	}
}

// Each colony's starter kit, swarm and workers on its own island.
static bool settleHomes(Game &game, GenerationContext &context,
						const ContestedCommonsOptions &options, Layout &L)
{
	const int W = game.map.getW();
	const int H = game.map.getH();
	// Give each team a small starter kit, then a swarm and workers, exactly the way the
	// other generators do it: enough to bootstrap, not enough to make the commons optional.
	for (int i = 0; i < context.request.nbTeams; ++i)
	{
		// Split the island into zones and fill most of them solid, instead of scattering a handful
		// of individual tiles across the whole island: a real wheat field and a real treeline, not
		// a light dusting. Two zones are left clear for the swarm and its workers. Five equal
		// zones: a treeline, a wheat field, a zone with one 3x3 quarry, and two left open for the
		// swarm, its workers and the first buildings. fillInResource drops a 1x1 or 3x3 square on
		// every tile of a zone, so a field zone is filled solid - a real field rather than a
		// scatter.
		std::vector<int> zoneWeights(5, 1);
		std::vector<int> zoneAreas;
		for (int z = 0; z < 5; ++z)
			zoneAreas.push_back(L.areaNumber++);
		std::vector<MapGeneratorPoint> homePoints;
		if (divideUpArea(game.map, context, L.grid, L.teamAreaNumbers[i], zoneWeights, zoneAreas))
		{
			std::vector<MapGeneratorPoint> pts;
			getAllPoints(game.map, L.grid, zoneAreas[0], pts);
			fillInResource(game.map, context, pts, WOOD, 2);
			homePoints.insert(homePoints.end(), pts.begin(), pts.end());
			pts.clear();

			getAllPoints(game.map, L.grid, zoneAreas[1], pts);
			fillInResource(game.map, context, pts, CORN, 2);
			homePoints.insert(homePoints.end(), pts.begin(), pts.end());
			pts.clear();

			// One compact deposit, not a scatter across the whole zone -- leaves the rest of
			// it clear to build on instead of peppering it with single-tile boulders.
			getAllPoints(game.map, L.grid, zoneAreas[2], pts);
			if (!pts.empty())
			{
				MapGeneratorPoint quarry = pts[context.bounded("layout", pts.size())];
				game.map.setResource(quarry.x, quarry.y, STONE, 3);
			}
			homePoints.insert(homePoints.end(), pts.begin(), pts.end());
			pts.clear();

			getAllPoints(game.map, L.grid, zoneAreas[3], pts);
			homePoints.insert(homePoints.end(), pts.begin(), pts.end());
			pts.clear();
			getAllPoints(game.map, L.grid, zoneAreas[4], pts);
			homePoints.insert(homePoints.end(), pts.begin(), pts.end());
		}
		else
		{
			// The island came out too small to subdivide -- fall back to a light scatter
			// over the whole thing rather than failing the map.
			getAllPoints(game.map, L.grid, L.teamAreaNumbers[i], homePoints);
			if (homePoints.empty())
				return false;
			std::vector<MapGeneratorPoint> cornPts = homePoints;
			chooseRandomPoints(game.map, context, cornPts, 4);
			fillInResource(game.map, context, cornPts, CORN, 2);
			std::vector<MapGeneratorPoint> woodPts = homePoints;
			chooseRandomPoints(game.map, context, woodPts, 4);
			fillInResource(game.map, context, woodPts, WOOD, 2);
			MapGeneratorPoint quarry = homePoints[context.bounded("layout", homePoints.size())];
			game.map.setResource(quarry.x, quarry.y, STONE, 3);
		}
		if (homePoints.empty())
			return false;

		std::vector<unsigned char> home(size_t(W) * H, 0);
		for (const auto &point : homePoints)
			home[point.y * W + point.x] = 1;
		if (!placeSettlement(game, context, i, home, L.teamPoints[i], "starts"))
			return false;
	}
	return true;
}

static bool generate(Game &game, GenerationContext &context)
{
	context.stage = "layout";
	const ContestedCommonsOptions options(context.request);
	game.map.makeHomogenMap(WATER);
	for (int i = 0; i < context.request.nbTeams; ++i)
		game.addTeam();
	Layout L(game.map);
	if (!disperseSeeds(game, context, L))
		return false;
	sizeIslands(game, context, options, L);
	paintTerrain(game, context, options, L);
	stockCommons(game, context, options, L);
	return settleHomes(game, context, options, L);
}

} // namespace

ContestedCommonsOptions::ContestedCommonsOptions(const GenerationRequest &r)
	: homeSize(r.option("home-island-size")), commonsSize(r.option("commons-size")),
	  moatWidth(r.option("moat-width")), bridgeCount(r.option("bridge-count")),
	  moatBridges(r.option("moat-bridges") != 0), jaggedCoasts(r.option("jagged-coasts") != 0),
	  wheat(r.option("wheat-amount")), wood(r.option("wood-amount")),
	  stone(r.option("stone-amount")), algae(r.option("algae-amount")),
	  fruit(r.option("fruit-amount"))
{
}

GeneratorDefinition contestedCommonsDefinition()
{
	return {"contested-commons",
			9,
			"Contested commons",
			2,
			false,
			// Home island size is a percentage of the closest colony spacing; commons size a
			// percentage of the home radius (limited by the room the spread leaves); moat width a
			// percentage of the home radius.
			{{"home-island-size", "Home island size", 20, 35, 5, 25, ControlGroup::Terrain},
			 {"commons-size", "Commons size", 250, 500, 50, 400, ControlGroup::Terrain},
			 {"moat-width", "Moat width", 20, 50, 5, 35, ControlGroup::Terrain},
			 {"bridge-count", "Bridge count", 1, 5, 1, 3, ControlGroup::Layout},
			 // Off, no bridge crosses the commons' moat.
			 GeneratorControl::toggle("moat-bridges", "Moat bridges", true, ControlGroup::Layout),
			 // Off, the home islands and the commons are smooth rounds.
			 GeneratorControl::toggle("jagged-coasts", "Jagged coastlines", true,
									  ControlGroup::Terrain),
			 // How many of the commons' zones are wheat, wood and fruit, the size of its quarry and
			 // the moat's algae. Every home island's own starter fields stay as they are.
			 GeneratorControl::percentage("wheat-amount", "Wheat amount"),
			 GeneratorControl::percentage("wood-amount", "Wood amount"),
			 GeneratorControl::percentage("stone-amount", "Stone amount"),
			 GeneratorControl::percentage("algae-amount", "Algae amount"),
			 GeneratorControl::percentage("fruit-amount", "Fruit amount")},
			generate};
}
