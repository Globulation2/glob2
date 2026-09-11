// SPDX-License-Identifier: GPL-3.0-or-later
#include "ContestedCommonsGenerator.h"
#include "Distances.h"
#include "Game.h"
#include "GenerationContext.h"
#include "Geometry.h"
#include "GlobalContainer.h"
#include "Regions.h"
#include "Resources.h"
#include "Settlements.h"
#include "Topology.h"
#include "Unit.h"
#include <algorithm>
#include <cmath>
#include <limits>
using namespace MapGeneration;
namespace
{
constexpr double pi = 3.14159265358979323846;
int wrapDelta(int a, int b, int size)
{
	int d = a - b;
	if (d > size / 2)
		d -= size;
	else if (d < -size / 2)
		d += size;
	return d;
}
void createJaggedIsland(Map &map, GenerationContext &context, std::vector<int> &grid, int area,
						int x, int y, int radius, double roughness)
{
	RadialShape shape(radius, roughness, context, "coast", 1.4);
	stampShape(grid, map.getW(), map.getH(), area, ShapeTransform({double(x), double(y)}, 0), shape,
			   true);
}
static bool generate(Game &game, GenerationContext &context)
{
	context.stage = "layout";
	const ContestedCommonsOptions options(context.request);
	game.map.makeHomogenMap(WATER);
	for (int i = 0; i < context.request.nbTeams; ++i)
		game.addTeam();

	const int W = game.map.getW();
	const int H = game.map.getH();

	int areaNumber = 1;
	std::vector<int> grid(W * H, 0);

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

	if (splitUpPoints(game.map, context, grid, 0, allPoints, allWeights,
					  PointSearch::WholeRegion) == 0)
		return false;

	// splitUpPoints shuffles the whole array, but weights travel with their point, so the
	// one point that kept commonsWeight is still identifiable regardless of where it landed.
	std::vector<MapGeneratorPoint> teamPoints;
	std::vector<int> teamAreaNumbers;
	MapGeneratorPoint commonsCenter(0, 0);
	for (unsigned int i = 0; i < allPoints.size(); ++i)
	{
		if (allWeights[i] == commonsWeight)
		{
			commonsCenter = allPoints[i];
		}
		else
		{
			teamPoints.push_back(allPoints[i]);
			teamAreaNumbers.push_back(areaNumber);
			areaNumber += 1;
		}
	}

	// Home islands are sized off actual team-to-team separation, not the joint figure
	// splitUpPoints returns (which is dominated by the deliberately-tighter team/commons gap).
	int minDist = std::numeric_limits<int>::max();
	for (unsigned int i = 0; i < teamPoints.size(); ++i)
		for (unsigned int j = i + 1; j < teamPoints.size(); ++j)
			minDist = std::min(
				minDist, (int)std::sqrt((double)game.map.warpDistSquare(
							 teamPoints[i].x, teamPoints[i].y, teamPoints[j].x, teamPoints[j].y)));
	if (minDist == std::numeric_limits<int>::max()) // a single team: no pair to measure
		minDist = (int)std::sqrt((double)game.map.warpDistSquare(teamPoints[0].x, teamPoints[0].y,
																 commonsCenter.x, commonsCenter.y));

	// Both islands are jagged, which means both can reach noticeably past their nominal
	// radius in a lucky direction -- comfortably more than a flat few-tile fudge factor once
	// the commons radius is large. Size everything off the worst case each jaggedness value
	// can actually produce, not off the ideal circle, so the sizing math and the moat's
	// physical enforcement (below) agree on where the real boundary is.
	const double teamJaggedness = 0.35;
	const double commonsJaggedness = 0.32;

	const int homeRadius = std::max(10, minDist * options.homeSize / 100);
	for (unsigned int i = 0; i < teamPoints.size(); ++i)
		createJaggedIsland(game.map, context, grid, teamAreaNumbers[i], teamPoints[i].x,
						   teamPoints[i].y, homeRadius, teamJaggedness);

	// Real Euclidean room at the chosen commons spot: the same metric createOval/
	// createJaggedIsland use to draw the islands, measured straight from the seed points.
	int minGapSquared = std::numeric_limits<int>::max();
	for (unsigned int i = 0; i < teamPoints.size(); ++i)
		minGapSquared =
			std::min(minGapSquared, game.map.warpDistSquare(commonsCenter.x, commonsCenter.y,
															teamPoints[i].x, teamPoints[i].y));
	const int centerGap = (int)std::sqrt((double)minGapSquared);

	// Budget the gap as: nearest team's worst-case reach, a real moat, the commons' own
	// worst-case reach, and a small flat buffer on top of all that jaggedness math.
	const int teamReach = (int)std::ceil(homeRadius * (1.0 + teamJaggedness * 1.4));
	const int moatWidth = std::max(4, homeRadius * options.moatWidth / 100);
	const int roomAvailable = std::max(0, centerGap - teamReach - moatWidth - 3);
	const int roomLimitedRadius =
		std::max(homeRadius, (int)(roomAvailable / (1.0 + commonsJaggedness * 1.4)));

	// The commons is the whole point of the map, so it should use up the room the joint
	// dispersion just bought it: aim for 4x a home island's radius (roughly what's actually
	// available once teams are pushed off it -- measured, not guessed), and let the min()
	// below be the real safety net for whatever a given layout can't quite support.
	const int desiredCommonsRadius = homeRadius * options.commonsSize / 100;
	const int commonsRadius = std::min(desiredCommonsRadius, roomLimitedRadius);
	const int bridgeCount = options.bridgeCount;

	std::vector<int> bridgeAngles;
	for (int i = 0; i < bridgeCount; ++i)
		bridgeAngles.push_back(context.bounded("layout", 360));

	int commonsAreaNumber = areaNumber;
	areaNumber += 1;
	createJaggedIsland(game.map, context, grid, commonsAreaNumber, commonsCenter.x, commonsCenter.y,
					   commonsRadius, commonsJaggedness);

	// Extend the commons island out across the moat along each bridge angle, so those strips
	// get boosted into land by the heightmap pass below just like the island itself. Bridges
	// get their own area number, kept separate from the island body, so the resource-field
	// split further down only ever touches the round island, never a narrow connecting strip.
	int bridgeAreaNumber = areaNumber;
	areaNumber += 1;
	for (unsigned int b = 0; b < bridgeAngles.size(); ++b)
	{
		double theta = bridgeAngles[b] * pi / 180.0;
		for (int r = commonsRadius - 2; r <= commonsRadius + moatWidth + 2; ++r)
		{
			int cx = commonsCenter.x + (int)round(r * cos(theta));
			int cy = commonsCenter.y + (int)round(r * sin(theta));
			for (int dx = -1; dx <= 1; ++dx)
			{
				for (int dy = -1; dy <= 1; ++dy)
				{
					int nx = game.map.normalizeX(cx + dx);
					int ny = game.map.normalizeY(cy + dy);
					if (grid[ny * W + nx] == 0)
						grid[ny * W + nx] = bridgeAreaNumber;
				}
			}
		}
	}

	// Base heightmap: noise everywhere, boosted wherever land has been carved out, so
	// every island (and every bridge strip) comes out dry and everything else stays ocean.
	std::vector<int> heights(W * H, 40);
	adjustHeightmapFromPerlinNoise(game.map, context, heights, 20);
	for (int y = 0; y < H; ++y)
		for (int x = 0; x < W; ++x)
			if (grid[y * W + x] != 0)
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
			if (grid[y * W + x] == commonsAreaNumber || grid[y * W + x] == bridgeAreaNumber)
				continue;
			int dx = wrapDelta(x, commonsCenter.x, W);
			int dy = wrapDelta(y, commonsCenter.y, H);
			double r = sqrt((double)(dx * dx + dy * dy));
			if (r >= commonsRadius - 1 && r < commonsRadius + moatWidth)
			{
				game.map.setUMatPos(x, y, WATER, 1);
				if (context.bounded("layout", 4) == 0)
					game.map.setResource(x, y, ALGA, 1);
			}
		}
	}
	game.map.controlSand();

	// Stock the commons with real fields, not a scattering of single tiles -- but the commons
	// is several times the area of a home island, so splitting it into the same handful of
	// zones a home island uses would read as one giant forest next to one giant farm. Scale
	// the zone count with its actual size, and shuffle which zone gets which resource, so it
	// comes out as a patchwork of many small groves and fields instead of two big blobs.
	{
		double areaRatio =
			(double)(commonsRadius * commonsRadius) / (double)(homeRadius * homeRadius);
		int zoneCount = (int)std::round(7.0 * std::sqrt(std::max(1.0, areaRatio)));
		zoneCount = std::max(7, std::min(24, zoneCount));

		std::vector<int> zoneWeights(zoneCount, 1);
		std::vector<int> zoneAreas;
		const int firstZoneArea = areaNumber;
		for (int z = 0; z < zoneCount; ++z)
			zoneAreas.push_back(areaNumber++);
		if (divideUpArea(game.map, context, grid, commonsAreaNumber, zoneWeights, zoneAreas))
		{
			// Which zone borders which: zoneAreas is a contiguous run of area numbers, so a
			// tile's zone index is just its area number minus the first one. Two adjacent
			// tiles with different zone indices means those two zones share a border.
			std::vector<std::vector<bool>> adjacent(zoneCount, std::vector<bool>(zoneCount, false));
			for (int y = 0; y < H; ++y)
			{
				for (int x = 0; x < W; ++x)
				{
					int a = grid[y * W + x] - firstZoneArea;
					if (a < 0 || a >= zoneCount)
						continue;
					int xr = game.map.normalizeX(x + 1);
					int b = grid[y * W + xr] - firstZoneArea;
					if (b >= 0 && b < zoneCount && b != a)
						adjacent[a][b] = adjacent[b][a] = true;
					int yd = game.map.normalizeY(y + 1);
					int c = grid[yd * W + x] - firstZoneArea;
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
			int woodTarget = std::max(1, zoneCount * 2 / 7);
			int cornTarget = std::max(1, zoneCount * 2 / 7);
			int fruitTarget = std::max(1, zoneCount / 7);

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
				getAllPoints(game.map, grid, zoneAreas[z], pts);
				if (role[z] == ROLE_QUARRY)
				{
					// One compact quarry, not a scatter: a single setResource call already
					// grows a solid square footprint from its center, which reads as an actual
					// deposit and leaves the rest of its zone clear to build on.
					if (!pts.empty())
					{
						MapGeneratorPoint quarry = pts[context.bounded("layout", pts.size())];
						game.map.setResource(quarry.x, quarry.y, STONE, 5);
					}
				}
				else if (role[z] == ROLE_WOOD)
					fillInResource(game.map, context, pts, WOOD, 2);
				else if (role[z] == ROLE_CORN)
					fillInResource(game.map, context, pts, CORN, 2);
				else if (role[z] == ROLE_FRUIT)
				{
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

	// Give each team a small starter kit, then a swarm and workers, exactly the way the
	// other generators do it: enough to bootstrap, not enough to make the commons optional.
	for (int i = 0; i < context.request.nbTeams; ++i)
	{
		// Split the island into zones and fill most of them solid, instead of scattering a
		// handful of individual tiles across the whole island: a real wheat field and a real
		// treeline, not a light dusting. Two zones are left clear for the swarm and its workers.
		std::vector<int> zoneWeights(5, 1);
		std::vector<int> zoneAreas;
		for (int z = 0; z < 5; ++z)
			zoneAreas.push_back(areaNumber++);
		std::vector<MapGeneratorPoint> homePoints;
		if (divideUpArea(game.map, context, grid, teamAreaNumbers[i], zoneWeights, zoneAreas))
		{
			std::vector<MapGeneratorPoint> pts;
			getAllPoints(game.map, grid, zoneAreas[0], pts);
			fillInResource(game.map, context, pts, WOOD, 2);
			homePoints.insert(homePoints.end(), pts.begin(), pts.end());
			pts.clear();

			getAllPoints(game.map, grid, zoneAreas[1], pts);
			fillInResource(game.map, context, pts, CORN, 2);
			homePoints.insert(homePoints.end(), pts.begin(), pts.end());
			pts.clear();

			// One compact deposit, not a scatter across the whole zone -- leaves the rest of
			// it clear to build on instead of peppering it with single-tile boulders.
			getAllPoints(game.map, grid, zoneAreas[2], pts);
			if (!pts.empty())
			{
				MapGeneratorPoint quarry = pts[context.bounded("layout", pts.size())];
				game.map.setResource(quarry.x, quarry.y, STONE, 3);
			}
			homePoints.insert(homePoints.end(), pts.begin(), pts.end());
			pts.clear();

			getAllPoints(game.map, grid, zoneAreas[3], pts);
			homePoints.insert(homePoints.end(), pts.begin(), pts.end());
			pts.clear();
			getAllPoints(game.map, grid, zoneAreas[4], pts);
			homePoints.insert(homePoints.end(), pts.begin(), pts.end());
		}
		else
		{
			// The island came out too small to subdivide -- fall back to a light scatter
			// over the whole thing rather than failing the map.
			getAllPoints(game.map, grid, teamAreaNumbers[i], homePoints);
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
		if (!placeSettlement(game, context, i, home, teamPoints[i], "starts"))
			return false;
	}

	return true;
}

} // namespace

ContestedCommonsOptions::ContestedCommonsOptions(const GenerationRequest &r)
	: homeSize(r.option("home-island-size")), commonsSize(r.option("commons-size")),
	  moatWidth(r.option("moat-width")), bridgeCount(r.option("bridge-count"))
{
}

GeneratorDefinition contestedCommonsDefinition()
{
	return {"contested-commons",
			9,
			"Contested commons",
			2,
			false,
			{{"home-island-size", "Home island size", 20, 35, 5, 25, ControlGroup::Terrain},
			 {"commons-size", "Commons size", 250, 500, 50, 400, ControlGroup::Terrain},
			 {"moat-width", "Moat width", 20, 50, 5, 35, ControlGroup::Terrain},
			 {"bridge-count", "Bridge count", 1, 5, 1, 3, ControlGroup::Layout}},
			generate};
}
