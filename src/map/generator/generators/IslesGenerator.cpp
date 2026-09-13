// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière
// Copyright (C) 2008 Bradley Arsenault
#include "IslesGenerator.h"
#include "Distances.h"
#include "Game.h"
#include "GenerationContext.h"
#include "GeneratorDefinition.h"
#include "GlobalContainer.h"
#include "HeightMap.h"
#include "Map.h"
#include "Pipeline.h"
#include "Regions.h"
#include "Resources.h"
#include "StartingPositions.h"
#include "Terrain.h"
#include "Unit.h"
#include <algorithm>
#include <cmath>
using namespace MapGeneration;

// Isles (id "isles", legacy id 6): an oval island for every colony, joined to its neighbours by
// narrow land bridges.
//
// HISTORY. Bradley Arsenault added it in July 2008, two days after Concrete islands, "based on the
// Isles map" - a hand-made map of the time - using the same area-grid toolkit, and improved its
// algae and swarm placement that week. On this branch its 213-line generate was split into the
// stages below without changing its output, a queue overrun in computeDistances (crossing bridge
// lines repeat tiles) was fixed, and it gained resource amounts and Land bridges and Sandy beaches
// switches.
//
// WHAT THE MAP IS. Colonies start on islands of equal size spread evenly over the map. Where the
// straight line between two islands crosses no third island, a bridge a few tiles wide joins them,
// so the bridges are the only ground routes and fighting concentrates on them; everything else is
// open sea until swimming. With bridges off it is a pure islands map like Old islands, but with
// even spacing and equal island sizes.
//
// HOW THE TERRAIN IS MADE. Everything is a height field read at the end as water (below 90), beach
// (96 to 104) or grass (the rest). The sea starts at 50; islands and bridges are raised by distance
// from their shapes; noise of up to 45 either way roughens every coast. Because 45 is almost the
// whole gap between the sea (50) and the water line (90), coasts wander several tiles in and out.
//
// GAME RULES IT LEANS ON (docs/map-generators/GAME_RULES_FOR_MAP_DESIGN.md):
// - Water blocks walking until a colony can swim, so bridges are the only early routes.
// - Resources block movement: divideUpPlayerLands keeps fields on the coast zones and stone in the
//   interior, and the algae patch is kept off the bridges so it cannot choke one.
// - Wheat and wood regrow near water; every colony's fields are its coastal zones.
// - Grass may not touch water: controlSand after painting rings every coast and bridge in sand.
//
// What the stages of a roll hand each other: the area grid and the next free area number, each
// colony's seed point, weight and area, the spacing the dispersion found, the height field the
// islands and bridges are raised in, the last distance field, and the bridges' tiles and area.
struct Layout
{
	std::vector<int> grid;
	int areaNumber = 1;
	std::vector<MapGeneratorPoint> teamPoints;
	std::vector<int> teamWeights, teamAreaNumbers;
	int minDist = 0;
	std::vector<int> heightmap, distances;
	std::vector<MapGeneratorPoint> connectorPoints;
	int connectorArea = 0;
	explicit Layout(const Map &map)
		: grid(size_t(map.getW()) * map.getH(), 0), heightmap(size_t(map.getW()) * map.getH(), 50)
	{
	}
};

// Spread the colonies apart and give each an oval island of its own.
//
// splitUpPoints spreads one point per colony as far apart as the map allows and returns the least
// distance between any two (minDist). Each island is a circle of island-size percent of that
// distance across, so with the default 60 there is always at least 40% of the spacing of open sea
// between two islands before the noise raises their coasts. The control's range (45 to 65) keeps it
// an islands map: the coast noise pushes shores out by several tiles, so much above 65 neighbouring
// islands start to touch, and much below 45 an island is too small for a colony's fields and base.
static void layoutIslands(Game &game, GenerationContext &context, const IslesOptions &options,
						  Layout &L)
{
	const int islandSize = options.island_size;
	// Do the starting locations of the teams
	for (int i = 0; i < context.request.nbTeams; ++i)
	{
		L.teamPoints.push_back(MapGeneratorPoint(0, 0));
		L.teamWeights.push_back(1);
		L.teamAreaNumbers.push_back(L.areaNumber);
		L.areaNumber += 1;
	}
	L.minDist = splitUpPoints(game.map, context, L.grid, 0, L.teamPoints, L.teamWeights);

	// Construct the areas for the teams
	for (int i = 0; i < context.request.nbTeams; ++i)
	{
		createOval(game.map, L.grid, L.teamAreaNumbers[i], L.teamPoints[i].x, L.teamPoints[i].y,
				   L.minDist * islandSize / 100, L.minDist * islandSize / 100);
	}
}

// Raise the islands in the height field, by distance from their edges.
//
// Island tiles (distance 1) rise by 100, to 150: far above the water line whatever the noise does.
// Outside, the rise falls by 10 a step, from 90 at the first step to nothing 11 steps out, so every
// island sits on a sloping shelf. A shelf tile d steps out stands at 50 + (11 - d) * 10 and is land
// when the noise lifts it to 90, which the noise (-45 to +44) can do from 2 steps out to 11: the
// coast wanders over that whole shelf, which is what makes the ovals read as natural islands.
static void raiseIslands(Game &game, Layout &L)
{
	// Construct a L.heightmap
	std::vector<MapGeneratorPoint> teamAreaPoints;
	getAllOtherPoints(game.map, L.grid, 0, teamAreaPoints);
	std::vector<MapGeneratorPoint> obstacles;

	computeDistances(game.map, teamAreaPoints, obstacles, L.distances);

	// Stamp out the team areas
	for (int x = 0; x < game.map.getW(); ++x)
	{
		for (int y = 0; y < game.map.getH(); ++y)
		{
			int d = L.distances[y * game.map.getW() + x];
			if (d > 1 && d <= 11)
				L.heightmap[y * game.map.getW() + x] += (11 - d) * 10;
			else if (d == 1)
				L.heightmap[y * game.map.getW() + x] += 100;
		}
	}
}

// Join every pair of islands whose straight line crosses no third island with a land bridge.
//
// For every pair, one random tile of each island is drawn and the line between them tested: if any
// tile within bridgeWidth - 2 of the line belongs to a third island, there is no bridge (it would
// run over that island and give it a shortcut). A bridge's tiles are only claimed where they are
// more than bridgeWidth + 1 steps from any island, so the bridge does not widen the islands'
// shores. Random endpoints make bridges leave islands at varied angles rather than centre to
// centre.
static void buildBridges(Game &game, GenerationContext &context, const IslesOptions &options,
						 Layout &L)
{
	const int bridgeWidth = options.bridge_width, bridgeRadius = bridgeWidth - 2;
	// Connect each teams area to each other players area
	L.connectorArea = L.areaNumber;
	L.areaNumber += 1;
	std::vector<MapGeneratorPoint> obstacles;
	for (int i = 0; i < context.request.nbTeams; ++i)
	{
		for (int j = i + 1; j < context.request.nbTeams; ++j)
		{
			// Choose one random point from each players area
			std::vector<MapGeneratorPoint> teamI;
			std::vector<MapGeneratorPoint> teamJ;
			getAllPoints(game.map, L.grid, L.teamAreaNumbers[i], teamI);
			getAllPoints(game.map, L.grid, L.teamAreaNumbers[j], teamJ);
			chooseRandomPoints(game.map, context, teamI, 1);
			chooseRandomPoints(game.map, context, teamJ, 1);

			// Traverse between the two points
			std::vector<MapGeneratorPoint> linePoints;
			getAllPointsLine(game.map, teamI[0].x, teamI[0].y, teamJ[0].x, teamJ[0].y, linePoints);
			// If a connection can be made without going through another teams area, then do it
			bool failed = false;
			for (unsigned int p = 0; p < linePoints.size() && !failed; ++p)
			{
				for (int x = -bridgeRadius; x <= bridgeRadius && !failed; ++x)
				{
					int nx = game.map.normalizeX(linePoints[p].x + x);
					for (int y = -bridgeRadius; y <= bridgeRadius && !failed; ++y)
					{
						int ny = game.map.normalizeY(linePoints[p].y + y);
						int g = L.grid[ny * game.map.getW() + nx];
						if (g != 0 && g != L.teamAreaNumbers[i] && g != L.teamAreaNumbers[j] &&
							g != L.connectorArea)
						{
							failed = true;
						}
					}
				}
			}
			// Make the connection. Without land bridges the same points are still drawn, so the
			// rest of the map stays as it was, but the islands are left apart.
			if (!failed && options.land_bridges)
			{
				for (unsigned int p = 0; p < linePoints.size(); ++p)
				{
					L.connectorPoints.push_back(linePoints[p]);
					for (int x = -bridgeRadius; x <= bridgeRadius; ++x)
					{
						int nx = game.map.normalizeX(linePoints[p].x + x);
						for (int y = -bridgeRadius; y <= bridgeRadius; ++y)
						{
							int ny = game.map.normalizeY(linePoints[p].y + y);
							int d = L.distances[ny * game.map.getW() + nx];
							if (d > bridgeWidth + 1)
							{
								L.grid[ny * game.map.getW() + nx] = L.connectorArea;
							}
						}
					}
				}
			}
		}
	}
	computeDistances(game.map, L.connectorPoints, obstacles, L.distances);
}

// Raise the bridges, add noise, and turn the height field into water, sand and grass.
static void paintTerrain(Game &game, GenerationContext &context, const IslesOptions &options,
						 Layout &L)
{
	const int bridgeWidth = options.bridge_width;
	// Stamp out the connectors: the bridge's centre line (distance 1) rises by 100 like an island,
	// and its sides fall off linearly to nothing at bridgeWidth steps, so a bridge is a ridge the
	// noise erodes into a causeway a few tiles wide with ragged edges. A narrow bridge can be
	// broken by the noise in places: the narrower the bridge, the more often it is cut.
	for (int x = 0; x < game.map.getW(); ++x)
	{
		for (int y = 0; y < game.map.getH(); ++y)
		{
			int d = L.distances[y * game.map.getW() + x];
			if (d > 1 && d <= bridgeWidth)
				L.heightmap[y * game.map.getW() + x] +=
					(bridgeWidth - d) * (100 / (bridgeWidth - 1));
			else if (d == 1)
				L.heightmap[y * game.map.getW() + x] += 100;
		}
	}

	// Use the L.heightmap to put in water, grass, and sand
	//
	// Noise of -45 to +44. The thresholds read: water under 90, beach from 96 to 104, grass
	// otherwise - including the narrow 90 to 95 band, which is grass right at the water's edge; it
	// becomes beach anyway when controlSand rings the coast. Open sea (50) can reach 94 at the
	// noise's very top, so a rare single grass speck can appear offshore; controlSand turns it to
	// sand.
	adjustHeightmapFromPerlinNoise(game.map, context, L.heightmap, 45);
	for (int x = 0; x < game.map.getW(); ++x)
	{
		for (int y = 0; y < game.map.getH(); ++y)
		{
			int total_height = L.heightmap[y * game.map.getW() + x];
			if (total_height < 90)
				game.map.setUMatPos(x, y, WATER, 1);
			else if (options.sandy_beaches && total_height > 95 && total_height < 105)
				game.map.setUMatPos(x, y, SAND, 1);
			else
				game.map.setUMatPos(x, y, GRASS, 1);
		}
	}
	game.map.controlSand();
}

// Re-divide the land between the colonies and give each an algae patch just off its coast.
//
// The land is split again over grass only, so each colony's area is its island's real buildable
// ground (the bridges keep their own area). The algae patch goes on a random tile exactly 8 steps
// from that ground: out past the beach in open water, near enough that the sand is within the
// 30-tile reach algae needs to regrow, and, with bridges, kept more than a bridge width from any
// bridge so it can never grow over the route.
static bool placeAlgae(Game &game, GenerationContext &context, const IslesOptions &options,
					   Layout &L)
{
	const int bridgeWidth = options.bridge_width;
	// Reset the L.grid, and recompute within the boundaries of the various islands
	for (int x = 0; x < game.map.getW(); ++x)
	{
		for (int y = 0; y < game.map.getH(); ++y)
		{
			if (L.grid[y * game.map.getW() + x] != L.connectorArea)
				L.grid[y * game.map.getW() + x] = 0;
		}
	}
	splitUpArea(game.map, context, L.grid, 0, L.teamPoints, L.teamWeights, L.teamAreaNumbers, true);

	std::vector<int> connectorDistances = L.distances;
	std::vector<MapGeneratorPoint> obstacles;

	// For each team, find a point just off the coast and place algae there
	for (int i = 0; i < context.request.nbTeams; ++i)
	{
		std::vector<MapGeneratorPoint> sources;
		getAllPoints(game.map, L.grid, L.teamAreaNumbers[i], sources);
		computeDistances(game.map, sources, obstacles, L.distances);
		std::vector<MapGeneratorPoint> possible;
		for (int x = 0; x < game.map.getW(); ++x)
		{
			for (int y = 0; y < game.map.getH(); ++y)
			{
				int d = L.distances[y * game.map.getW() + x];
				int d2 = connectorDistances[y * game.map.getW() + x];
				if (d == 8 && (!options.land_bridges || d2 > bridgeWidth))
				{
					possible.push_back(MapGeneratorPoint(x, y));
				}
			}
		}
		if (possible.size() == 0)
		{
			return false;
		}
		int r = context.stream("layout")() % possible.size();
		// A five by five patch of algae at the default amount.
		setScaledResource(game.map, possible[r].x, possible[r].y, ALGA, 5, options.algae);
	}
	return true;
}

static bool generate(Game &game, GenerationContext &context)
{
	context.stage = "layout";
	const IslesOptions options(context.request);
	game.map.makeHomogenMap(context.request.terrainType);
	for (int i = 0; i < context.request.nbTeams; ++i)
		game.addTeam();
	Layout L(game.map);
	layoutIslands(game, context, options, L);
	raiseIslands(game, L);
	buildBridges(game, context, options, L);
	paintTerrain(game, context, options, L);
	if (!placeAlgae(game, context, options, L))
		return false;
	if (!divideUpPlayerLands(game, context, L.grid, L.teamAreaNumbers, L.areaNumber,
							 {options.wheat, options.wood, options.stone}))
	{
		return false;
	}
	// A colony's own fields are its only wheat and wood, and a field or deposit grown well past its
	// default size can also wall the colony in with nowhere left to build.
	reopenCrampedStarts(game, context, {options.wheat, options.wood, options.stone, options.algae});

	// Initialize final team info
	for (int i = 0; i < context.request.nbTeams; ++i)
	{
		game.teams[i]->createLists();
	}
	return true;
}

GeneratorDefinition islesDefinition()
{
	return {
		"isles",
		6,
		"Isles",
		2,
		false,
		// Island size is each island's diameter as a percentage of the least distance between
		// colonies (see layoutIslands for its range); bridge width is how many steps a bridge's
		// ridge spreads.
		{{"island-size", "Island size", 45, 65, 5, 60, ControlGroup::Terrain, false},
		 {"bridge-width", "Land bridge width", 3, 6, 1, 4, ControlGroup::Terrain, false},
		 // Off, every colony's island stands alone in the sea.
		 GeneratorControl::toggle("land-bridges", "Land bridges", true, ControlGroup::Terrain),
		 // Off, islands meet the sea without a band of sand.
		 GeneratorControl::toggle("sandy-beaches", "Sandy beaches", true, ControlGroup::Terrain),
		 // Wheat and wood scale each colony's fields, stone its deposits, algae its patch.
		 GeneratorControl::percentage("wheat-amount", "Wheat amount"),
		 GeneratorControl::percentage("wood-amount", "Wood amount"),
		 GeneratorControl::percentage("stone-amount", "Stone amount"),
		 GeneratorControl::percentage("algae-amount", "Algae amount")},
		generate};
}
