// SPDX-License-Identifier: GPL-3.0-or-later
#include "PlantationsGenerator.h"
#include "Channels.h"
#include "Drawing.h"
#include "Farmland.h"
#include "Game.h"
#include "GenerationContext.h"
#include "Geometry.h"
#include "GraphMaze.h"
#include "Grid.h"
#include "LatticeNoise.h"
#include "Morphology.h"
#include "Pipeline.h"
#include "Planting.h"
#include "Points.h"
#include "Resources.h"
#include "Settlements.h"
#include "Sketch.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>
using namespace MapGeneration;

// Plantations: an archipelago of small farm islands. Every island is a plantation: a square plot of
// grass ringed with sand at its heart, the only ground a building can stand on, and wheat or wood
// over everything between that ring and the beach. The sea waters the crops: the engine's growth probe
// reaches fifteen tiles (Growth.h's kCropProbeReach) and no tile of an island is more than about ten
// from the water, so the whole crop band regrows without a ditch, a pond or a channel, the way Polder's
// rows need one. Two sand lanes run from every plot to the shore, so a worker walks out through a
// solid field and a swimmer walks in. Nothing joins the islands: units swim.
//
// A plot is smaller than a base (8 tiles square by default: a swarm, a pool and one or two more
// buildings), so a colony holds several islands from the first minute (FEEDBACK 2026-09-15, the
// concept: "the protected building plots should be smaller than a normal player base, so you need to
// combine several of them"): its home island, where the swarm and a completed swimming pool stand on
// the plot, and two more granted islands, each with a completed pool and an inn ("player should be
// granted buildings on a few islands right from the start, as well as a pool on each one so workers
// can learn to swim"). Building space is tight on purpose: what a colony spends a plot's last tiles
// on, and which island it claims next, is the game. The rest of the islands are neutral plantations
// of wheat, of wood or of both, every one with a free plot, with orchard islets of the three fruits
// and rock islets of stone among them; one rock islet lies beside every colony's islands, so stone is
// a short swim away and never a long one.
//
// WHY IT PLAYS WELL (docs/map-generators/GAME_RULES_FOR_MAP_DESIGN.md). Buildings need pure grass and
// crops spread only onto pure grass, so the sand ring keeps every plot open however the fields grow;
// pure water stops a unit that cannot swim, so the straits between islands are a timing rule (the
// pools are the clock) and a swimming army lands on a beach with no forward base behind it; a tower
// scans square rings without line of sight, and the default strait of 4 corners puts the banks 8
// tiles apart (Channels.h's bankToBank), so only a level-3 tower (range 9) reaches the next island's
// first tile of grass and neighbours cannot shell each other's fields from the start. Every island is
// its own farm with beaches all round, so algae thrives in every strait and no crop ever runs short:
// the scarcity is room and the cost is the swim.
//
// THE LAYOUT. Island sites are dart-thrown at a spacing that fits an island and a strait, relaxed so
// the cells come out even (Points.h), and every cell's border becomes a strait of exactly the asked
// width (Channels.h's straitsBetweenCells); each island is the rest of its cell inside a rough disc
// round its site, so a big cell gives a round island and a small one a polygonal one. The colonies'
// home islands are the "full" islands (room for the plot and the whole crop band) farthest apart on
// the cell graph (farthestCells), dealt to the colonies at random (dealStarts); each home is dealt its
// granted islands from its nearest neighbours with a plot, as many for every colony as for any other
// (claimNeighbourCells), then a rock islet beside them. Fairness is statistical, and the lobby keeps
// the best-scoring of several seeds.
//
// THE SIZES AT THE DEFAULTS (plot 8, farm width 4, strait 4). An island is a rounded square (a
// superellipse, insideOutline) so the square plot's corners keep a crop band too: from a plot's
// middle, 4 vertices of grass, the ring vertex, two tiles of mixed ground the ring spoils, 4 tiles of
// crops and the beach along an axis, and the same from the plot block's corner along a diagonal, which
// sets the radius at 12 along the axes and about 13.5 at the corners (landRadiusFor); an island is
// some 27 across and a site every 35 tiles (spacingFor), so a 256 map holds about 50 islands and a
// 128 map a dozen; a 64 map holds four once the crop band has shrunk to 2, room for two colonies with
// no neutral island between them (the design shrinks the farm width, then the granted count, before it
// refuses).
namespace
{

// THE PLOT. Its ring is one undermap vertex of sand (kPlotRing, as Canals' pads): a tile takes its
// terrain from its four corners, so the ring spoils the two tiles either side of it (kRingBand) for
// crops and buildings alike, a walkable band round every plot where workers wait and units pass, while
// a single sand vertex is already enough to stop the crops, which spread only onto pure grass
// (Map::growResources). A plot narrower than 8 cannot seat a swarm (4x4) and a pool (4x4) side by
// side, which is why 8 is the least offered.
constexpr int kPlotRing = 1, kRingBand = 2;
// THE COAST. layBeaches turns the outermost land vertex to sand, and the tile inside it shares that
// vertex, so the crop band ends kCoastBand tiles short of the water; the first water vertex lies one
// more out. So an island's land reaches plotHalf + kRingBand + farm + kCoastBand vertices from the
// plot's middle along an axis (landRadiusFor), and the middle's clearance (Morphology.h, steps to the
// first water vertex) is what the outline's inscribed square allows (nominalRoomFor).
constexpr int kCoastBand = 1;
// A plot needs the room of an island with at least this much crop band along its sides to be a
// plantation at all; an island with less is a field (crops, no plot). An island is "full", fit for a
// home, when its middle has the room of the outline as asked, less kFullSlack: the wobble and the
// cells the outlines are clipped to rarely give exactly the nominal radius, and one tile of crop band
// is not a difference a player feels. Granted islands need only a plot: a colony's outer islands are
// its room to build, and a thinner crop band there is no loss (256 maps with twelve colonies had
// colonies with no full neighbour at all, which left every colony with nothing).
constexpr int kLeastFarm = 2, kFullSlack = 1;
// Sites are thrown this many tiles further apart than an island and a strait strictly need, so a cell
// that came out small still holds its island; more slack means fewer, rounder islands.
constexpr int kCellSlack = 2;
// The outline: a superellipse |x|^p + |y|^p = r^p of exponent kSquareness is a square with rounded
// corners, its corners kCornerReach (2 to the power 1/2 - 1/p) times as far from the middle as its
// sides; 3 reads as an island and keeps the plot's corners in crops, where a disc (2) would leave them
// on the beach and a squarer 4 would make every island a block. Islets are plain discs. The coast
// wobbles by up to kCoastAmplitude of the radius at roughness 100, and the cells' borders wander by up
// to kBorderWarpPercent of the spacing (nearestSiteLabels' warp). 0.3 keeps every island's plot
// reachable from its site: a bigger wobble cuts the crop band to nothing on one side.
constexpr double kSquareness = 3.0, kIsletSquareness = 2.0;
constexpr double kCornerReach = 1.1225; // 2^(1/2 - 1/3)
constexpr double kCoastAmplitude = 0.3;
constexpr int kBorderWarpPercent = 30;
// Lloyd relaxation rounds after the darts: three even the cells out without moving a site so far that
// nearestSiteLabels' bucket search (which assumes sites about a spacing apart) misses its nearest.
constexpr int kRelaxRounds = 3;
// Every plantation gets this many sand lanes from its plot to the shore: two, on the sides that face
// its nearest neighbours, so the swim to the next island lands on a lane and the plot is reached from
// two sides when a field is solid; a home gets one per granted island when it holds more (the bulk
// study's causeway failures were all homes with three granted islands and two lanes).
// kCausewayHalfWidth is the sand line's half width across the strait for a causeway: one vertex of
// sand already carries units two tiles wide (Channels.h's bridgeAcross).
constexpr int kLanes = 2;
constexpr double kCausewayHalfWidth = 0.75;
// Islets: a rock islet is a disc of this radius, a grass core of about three tiles that holds a stone
// clump of kRockRadius; an orchard islet a little bigger, so three groves of one fruit each stand
// round its middle kGroveRing tiles out. Neither carries a plot or a crop.
constexpr double kRockIsletRadius = 4.5, kOrchardIsletRadius = 5.5, kGroveRing = 2.0;
constexpr int kRockRadius = 2;
// Of the neutral islands, one in kIsletShare becomes an orchard islet and one in kIsletShare a rock
// islet at an amount of 100 (scaled by the fruit and stone amounts); the rock islet beside every
// colony is on top of those and unscaled, the map's equivalent of a starter quarry.
constexpr int kIsletShare = 8;
// The neutral plantations' kinds, dealt by weight out of 100: mixed islands (both crops) are the most
// useful to hold, wheat feeds and wood builds.
constexpr int kMixedWeight = 40, kWheatWeight = 35;
// How much of a plantation's crop band is planted at an amount of 100: enough that the island reads as
// a field from the first minute (the concept: every tile outside the plot "should contain either wheat
// or wood"), with gaps to harvest from and for the growth to fill. The home island's cover is unscaled,
// so an amount of 0 still leaves every colony its starting crops and the guarantee that follows never
// has to put a deposit on the plot.
constexpr int kCoverPercent = 85;
// Noise periods, in tiles, for the patches the cover is laid in and for the wheat/wood split of a mixed
// island: patches a few tiles across, and halves of an island rather than a chequerboard.
constexpr int kPatchPeriod = 6, kSplitPeriod = 9;
// Algae: one clump per this many water tiles at an amount of 100. The sea is most of the map and every
// tile of it is beside a beach, so it grows everywhere; 60 keeps the straits readable.
constexpr int kAlgaeTilesPerClump = 60;

enum IslandKind
{
	Water = 0, // a cell whose island came out empty
	Field,     // crops only: the island is too small for a plot
	Mixed,     // a plantation of wheat and wood in halves
	Wheat,     // a plantation of wheat
	Wood,      // a plantation of wood
	Rock,      // a stone islet
	Orchard    // an islet with a grove of each fruit
};
constexpr int kKinds = 7;
constexpr const char *kKindNames[kKinds] = {"water", "field", "mixed",  "wheat",
											"wood",  "rock",  "orchard"};

// The island of one cell.
struct Island
{
	Site site{0, 0};
	int kind = Water;
	int owner = -1;             // the colony holding it, or -1
	bool home = false;          // its owner's home island
	int plotX = -1, plotY = -1; // top-left grass tile of its plot, or -1 without one
	int room = 0;               // its middle's clearance, in steps to the first water vertex
	std::vector<int> laneEnd;   // the beach tile every lane ends on
};

struct Layout
{
	Torus t{1, 1};
	int plotSize = 0, farmWidth = 0, straitWidth = 0, granted = 0, spacing = 0;
	bool rockIslets = true; // a rock islet beside every colony, unless the map cannot fit them
	std::vector<Site> sites;
	std::vector<int> cell;                                 // every tile's cell
	std::vector<Island> islands;                           // one per cell
	std::vector<int> homeCell;                             // every colony's home cell
	std::vector<std::vector<int>> grantedCells, rockCells; // per colony
	std::vector<unsigned char> land, lane;
	Farm plots;           // every plot's grass and ring, stamped as farm plots
	TerrainSketch sketch; // the terrain as designed, beaches and causeways included
	std::string failure;
};

// Whether the point (dx, dy) from an outline's middle lies inside it: a superellipse of the given
// exponent and radius along the axes (kSquareness for an island, kIsletSquareness for an islet).
bool insideOutline(double dx, double dy, double radius, double exponent)
{
	return std::pow(std::fabs(dx) / radius, exponent) +
			   std::pow(std::fabs(dy) / radius, exponent) <=
		   1.0;
}
// The island's land radius along the axes from the plot's middle, in vertices: whichever of the side
// and the corner needs more. Along an axis the plot block (half the plot, the ring's band), the crop
// band and the coast lie end to end; along a diagonal the block's corner is its half side times root
// two away and the band and the coast lie beyond it, where the outline reaches kCornerReach times the
// axis radius.
int landRadiusFor(int plotSize, int farmWidth)
{
	const int block = plotSize / 2 + kRingBand;
	const double side = block + farmWidth + kCoastBand;
	const double corner = (block * std::sqrt(2.0) + farmWidth + kCoastBand) / kCornerReach;
	// Rounded, not raised: a corner need of 12.02 is a radius of 12, since a tile's centre is
	// what the outline is tested against and the crop band is measured to the tile.
	return int(std::lround(std::max(side, corner)));
}
// The clearance (Morphology.h: steps to the first water vertex) the nominal outline gives its middle:
// one more than the widest square of land it holds, found on the diagonal. The room a full island has.
int nominalRoomFor(int landRadius)
{
	int k = 0;
	while (insideOutline(k + 1, k + 1, landRadius, kSquareness))
		++k;
	return k + 1;
}
// The spacing between island sites: an island's full width corner to corner (2r + 1 vertices), the
// strait, and slack.
int spacingFor(int plotSize, int farmWidth, int strait)
{
	return 2 * int(std::ceil(landRadiusFor(plotSize, farmWidth) * kCornerReach)) + 1 + strait +
		   kCellSlack;
}

// Where a lane leaves a plot: the point where a ray from the plot's middle along `heading` crosses the
// ring (the square of half side plotHalf + kPlotRing), so the lane's sand starts on the ring and never
// on the plot's grass.
ShapePoint laneStart(int cx, int cy, int plotSize, double heading)
{
	const double edge = plotSize / 2.0 + kPlotRing;
	const double along =
		edge / std::max(std::fabs(std::cos(heading)), std::fabs(std::sin(heading)));
	return {cx + along * std::cos(heading), cy + along * std::sin(heading)};
}

Layout design(const GenerationRequest &request, GenerationContext &context)
{
	const PlantationsOptions o(request);
	Layout L;
	L.t = {1 << request.wDec, 1 << request.hDec};
	const Torus &t = L.t;
	const int n = t.size(), teams = std::max(1, request.nbTeams);

	// SIZING. Everything is as asked while the map is expected to hold every colony's islands (home,
	// granted, a rock islet), counting one island per spacing squared of map; a map too small for that
	// climbs down a ladder, a step at a time and each step noted, before it refuses: the farm width to
	// kLeastFarm, the plot to 8 (the least that seats a swarm and a pool), the strait to 4 (the least
	// that seals), then the granted islands to none, and last the rock islets, so a crowded map is
	// still an archipelago of homes rather than a refusal (the bulk study's refusals were nearly all
	// maps with a 64-tile side or a 128 map with eight or twelve colonies). The estimate is rough (the
	// darts' minimum spacing, the relaxation), so once the seed's actual cells are known the granted
	// count and the rock islets are trimmed again below. A 128 map with four colonies typically gets
	// islands with a crop band of 2 and one granted island each.
	L.plotSize = o.plotSize;
	L.farmWidth = o.farmWidth;
	L.straitWidth = o.straitWidth;
	L.granted = o.islandsPerColony - 1;
	const auto expectedCells = [&]
	{
		const int spacing = spacingFor(L.plotSize, L.farmWidth, L.straitWidth);
		return std::int64_t(t.w) * t.h / (std::int64_t(spacing) * spacing);
	};
	// Every colony's home, granted islands and rock islet; neutral islands are whatever is left, and a
	// 64 map with two colonies has none.
	const auto cellsNeeded = [&]
	{ return std::int64_t(teams) * (L.granted + 1 + (L.rockIslets ? 1 : 0)); };
	const auto climbDown = [&](int &value, int floor, const char *key, const char *why)
	{
		const int before = value;
		while (expectedCells() < cellsNeeded() && value > floor)
			--value;
		if (value < before)
			context.telemetry.fallback(key, why);
	};
	climbDown(L.farmWidth, kLeastFarm, "plantations.layout.farm-shrunk",
			  "Farm width reduced so every colony's islands fit the map");
	climbDown(L.plotSize, 8, "plantations.layout.plot-shrunk",
			  "Plot size reduced so every colony's islands fit the map");
	climbDown(L.straitWidth, 4, "plantations.layout.strait-narrowed",
			  "Strait narrowed so every colony's islands fit the map");
	climbDown(L.granted, 0, "plantations.layout.granted-reduced",
			  "Fewer islands per colony so every colony fits the map");
	if (expectedCells() < cellsNeeded())
	{
		L.rockIslets = false;
		context.telemetry.fallback("plantations.layout.rock-islets-dropped",
								   "No rock islet beside every colony: the map holds only homes");
	}
	if (expectedCells() < cellsNeeded())
	{
		L.failure = "Too many colonies for this map; use a bigger map or fewer colonies.";
		return L;
	}
	L.spacing = spacingFor(L.plotSize, L.farmWidth, L.straitWidth);
	context.telemetry.measure("plantations.plot.size", L.plotSize);
	context.telemetry.measure("plantations.farm.width", L.farmWidth);
	context.telemetry.measure("plantations.spacing", L.spacing);
	context.telemetry.measure("plantations.granted.requested", o.islandsPerColony - 1);

	// THE CELLS. Sites by dart throwing at the spacing, relaxed so the cells are even; every tile's
	// nearest site with the borders warped by the roughness, so islands are not all polygons of the
	// same family.
	L.sites =
		relaxPoints(t, spreadPoints(t, L.spacing, context, "plantations-sites"), kRelaxRounds);
	const int cells = int(L.sites.size());
	context.telemetry.measure("plantations.cells.actual", cells);
	while (cells < cellsNeeded() && L.granted > 0)
	{
		--L.granted;
		context.telemetry.fallback(
			"plantations.layout.granted-trimmed",
			"The seed threw fewer islands than the estimate; fewer per colony");
	}
	if (cells < cellsNeeded() && L.rockIslets)
	{
		L.rockIslets = false;
		context.telemetry.fallback(
			"plantations.layout.rock-islets-dropped",
			"The seed threw fewer islands than the estimate; no rock islets");
	}
	if (cells < cellsNeeded())
	{
		L.failure = "Too many colonies for this map; use a bigger map or fewer colonies.";
		return L;
	}
	L.cell = nearestSiteLabels(t, L.sites, L.spacing, context, "plantations-warp", 150,
							   kBorderWarpPercent * o.coastRoughness / 100);
	const CellGraph graph = cellGraph(t, L.sites, siteNeighbours(t, L.cell, cells));

	// THE ISLANDS. A strait of the asked width along every cell border, and each cell's island the rest
	// of the cell inside a rounded square of the nominal land radius round its site, its edge wobbled
	// by a RadialShape drawn per island; then every island's room and, where a plot fits, its plot at
	// the island's roomiest tile (nearest the site on a tie).
	const std::vector<unsigned char> strait = straitsBetweenCells(t, L.cell, L.straitWidth);
	const int landRadius = landRadiusFor(L.plotSize, L.farmWidth);
	const double amplitude = kCoastAmplitude * o.coastRoughness / 100.0;
	L.land.assign(n, 0);
	L.islands.resize(cells);
	const auto stampIsland = [&](int s, double radius, double exponent)
	{
		const RadialShape coast(radius, amplitude, context, "plantations-coast");
		const int reach = int(std::ceil(radius * kCornerReach * (1 + amplitude))) + 1;
		for (int dy = -reach; dy <= reach; ++dy)
			for (int dx = -reach; dx <= reach; ++dx)
			{
				const int i = t.at(L.sites[s].x + dx, L.sites[s].y + dy);
				if (L.cell[i] != s || strait[i])
					continue;
				// The wobble scales the whole outline at this angle, corners and sides alike.
				const double r = coast.radiusAt(std::atan2(double(dy), double(dx)));
				if (insideOutline(dx, dy, r, exponent))
					L.land[i] = 1;
			}
	};
	for (int s = 0; s < cells; ++s)
	{
		L.islands[s].site = L.sites[s];
		stampIsland(s, landRadius, kSquareness);
	}
	// A plot fits where the island has the room the nominal outline would have with a crop band of
	// kLeastFarm (its corners then reach the beach and its sides keep a few tiles of crops); a full
	// island has the room of the outline as asked. Both give or take kFullSlack.
	const int leastRoom = nominalRoomFor(landRadiusFor(L.plotSize, kLeastFarm)) - kFullSlack;
	const int fullRoom = nominalRoomFor(landRadius) - kFullSlack;
	context.telemetry.measure("plantations.room.nominal", nominalRoomFor(landRadius));
	std::vector<unsigned char> full(cells, 0), hasPlot(cells, 0), hasLand(cells, 0);
	const auto measureIslands = [&]
	{
		const std::vector<int> room = clearance(t, L.land);
		std::vector<int> best(cells, -1);
		for (int i = 0; i < n; ++i)
		{
			if (!L.land[i])
				continue;
			const int s = L.cell[i];
			int &b = best[s];
			if (b < 0 || room[i] > room[b] ||
				(room[i] == room[b] && t.dist2(i % t.w, i / t.w, L.sites[s].x, L.sites[s].y) <
										   t.dist2(b % t.w, b / t.w, L.sites[s].x, L.sites[s].y)))
				b = i;
		}
		for (int s = 0; s < cells; ++s)
		{
			Island &island = L.islands[s];
			hasLand[s] = best[s] >= 0;
			island.room = hasLand[s] ? room[best[s]] : 0;
			hasPlot[s] = island.room >= leastRoom;
			full[s] = hasPlot[s] && island.room >= fullRoom;
			island.plotX = hasPlot[s] ? t.x(best[s] % t.w - L.plotSize / 2) : -1;
			island.plotY = hasPlot[s] ? t.y(best[s] / t.w - L.plotSize / 2) : -1;
		}
	};
	measureIslands();
	if (context.telemetry.enabled())
	{
		context.telemetry.measure("plantations.islands.full",
								  std::count(full.begin(), full.end(), 1));
		context.telemetry.measure("plantations.islands.with-plot",
								  std::count(hasPlot.begin(), hasPlot.end(), 1));
	}

	// THE COLONIES. Homes on the full islands farthest apart, dealt to the colonies; granted islands
	// from each home's nearest neighbours with a plot, equally; a rock islet beside each colony's islands, or
	// failing that the nearest cell with land at all.
	L.homeCell = farthestCells(graph, teams, full);
	if (int(L.homeCell.size()) != teams)
	{
		// A crowded map (a 64-tile side, a dozen colonies on 128) squeezes its cells until few islands
		// are full; homes then take any island with a plot, the crop band thinner on one side.
		context.telemetry.fallback(
			"plantations.homes.partial-islands",
			"Too few full islands for every colony; homes on any plot island");
		L.homeCell = farthestCells(graph, teams, hasPlot);
	}
	if (int(L.homeCell.size()) != teams)
	{
		L.failure =
			"The islands are too small for their plots; use a bigger map, a smaller plot or "
			"fewer colonies.";
		return L;
	}
	dealStarts(context, L.homeCell);
	std::vector<unsigned char> grantable(hasPlot);
	for (int cell : L.homeCell)
		grantable[cell] = 0;
	L.grantedCells = claimNeighbourCells(graph, L.homeCell, L.granted, grantable);
	if (!L.grantedCells.empty() && int(L.grantedCells[0].size()) < L.granted)
	{
		context.telemetry.fallback("plantations.granted.shortfall",
								   "Not every colony had enough full neighbours; all got fewer");
		L.granted = int(L.grantedCells[0].size());
	}
	context.telemetry.measure("plantations.granted.actual", L.granted);
	for (int k = 0; k < teams; ++k)
	{
		L.islands[L.homeCell[k]].owner = k;
		L.islands[L.homeCell[k]].home = true;
		L.islands[L.homeCell[k]].kind = Mixed;
		for (size_t g = 0; g < L.grantedCells[k].size(); ++g)
		{
			Island &island = L.islands[L.grantedCells[k][g]];
			island.owner = k;
			// The first granted island leans to wheat and the second to wood, so a colony's islands
			// read differently and it has a reason to work both; any more alternate.
			island.kind = g % 2 == 0 ? Wheat : Wood;
		}
	}
	L.rockCells.assign(teams, {});
	for (int k = 0; k < teams && L.rockIslets; ++k)
	{
		std::vector<int> held{L.homeCell[k]};
		held.insert(held.end(), L.grantedCells[k].begin(), L.grantedCells[k].end());
		int best = -1;
		const auto consider = [&](int cell)
		{
			if (!hasLand[cell] || L.islands[cell].owner >= 0 || L.islands[cell].kind == Rock)
				return;
			if (best < 0 ||
				graph.distance2(L.homeCell[k], cell) < graph.distance2(L.homeCell[k], best))
				best = cell;
		};
		for (int cell : held)
			for (int edge : graph.cellEdges[cell])
				consider(graph.other(edge, cell));
		if (best < 0)
		{
			context.telemetry.fallback(
				"plantations.rock.far",
				"No free cell beside this colony's islands; using the nearest", k);
			for (int cell = 0; cell < cells; ++cell)
				consider(cell);
		}
		if (best >= 0)
		{
			L.islands[best].kind = Rock;
			L.rockCells[k].push_back(best);
		}
		context.telemetry.measure("plantations.rock.beside", best >= 0, k);
	}

	// THE NEUTRAL ISLANDS. Shuffled, then dealt: orchard and rock islets (scaled by the fruit and stone
	// amounts, out of the neutral count), then plantations by weight where a plot fits, fields
	// elsewhere. Islets that could not have held a plot come first in the deal, so a small island
	// becomes an islet rather than a field whenever the amounts want one.
	std::vector<int> neutral;
	for (int s = 0; s < cells; ++s)
		if (hasLand[s] && L.islands[s].owner < 0 && L.islands[s].kind == Water)
			neutral.push_back(s);
	context.shuffle(neutral.begin(), neutral.end(), "plantations-kinds");
	std::stable_sort(neutral.begin(), neutral.end(),
					 [&](int a, int b) { return hasPlot[a] < hasPlot[b]; });
	const int orchards =
		int(scaledCount((int(neutral.size()) + kIsletShare / 2) / kIsletShare, o.fruit));
	const int rocks =
		int(scaledCount((int(neutral.size()) + kIsletShare / 2) / kIsletShare, o.stone));
	context.telemetry.measure("plantations.islets.orchards-requested", orchards);
	context.telemetry.measure("plantations.islets.rocks-requested", rocks);
	for (size_t k = 0; k < neutral.size(); ++k)
	{
		Island &island = L.islands[neutral[k]];
		if (int(k) < orchards)
			island.kind = Orchard;
		else if (int(k) < orchards + rocks)
			island.kind = Rock;
		else if (!hasPlot[neutral[k]])
			island.kind = Field;
		else
		{
			const int roll = int(context.bounded("plantations-kinds", 100));
			island.kind = roll < kMixedWeight                  ? Mixed
						  : roll < kMixedWeight + kWheatWeight ? Wheat
															   : Wood;
		}
	}
	// Islets are small: their cells' land is stamped again as a small disc, and the islands measured
	// again so a rock islet's plot (if it had one) is forgotten and every islet's room is its own.
	for (int s = 0; s < cells; ++s)
		if (L.islands[s].kind == Rock || L.islands[s].kind == Orchard)
		{
			for (int i = 0; i < n; ++i)
				if (L.cell[i] == s)
					L.land[i] = 0;
			stampIsland(s, L.islands[s].kind == Rock ? kRockIsletRadius : kOrchardIsletRadius,
						kIsletSquareness);
		}
	measureIslands();
	for (int s = 0; s < cells; ++s)
	{
		Island &island = L.islands[s];
		if (island.kind == Rock || island.kind == Orchard || island.kind == Field || !hasLand[s])
			island.plotX = island.plotY = -1;
		if (!hasLand[s])
			island.kind = Water;
	}
	if (context.telemetry.enabled())
	{
		int counts[kKinds] = {};
		for (const Island &island : L.islands)
			++counts[island.kind];
		for (int kind = 0; kind < kKinds; ++kind)
			context.telemetry.measure(std::string("plantations.island-kind.") + kKindNames[kind] +
										  ".count",
									  counts[kind]);
	}

	// THE SKETCH. Land is grass and the rest sea; every plot is stamped with its ring (stampFarmPlot),
	// then the lanes: two per plantation, from the ring out to the beach, towards the two nearest
	// neighbouring islands' plots (a home's towards its granted islands, a granted island's first
	// towards its home), so a lane's end faces the next island's lane across the strait.
	L.sketch.assign(n, WATER);
	for (int i = 0; i < n; ++i)
		if (L.land[i])
			L.sketch[i] = GRASS;
	L.plots.water.assign(n, 0);
	L.plots.sand.assign(n, 0);
	L.plots.plot.assign(n, 0);
	L.plots.row.assign(n, -1);
	const FarmPlot plot{L.plotSize, L.plotSize, kPlotRing};
	for (Island &island : L.islands)
		if (island.plotX >= 0)
			stampFarmPlot(L.sketch, t, L.plots, island.plotX, island.plotY, plot);
	L.lane.assign(n, 0);
	const auto plotMiddle = [&](const Island &island)
	{ return ShapePoint{island.plotX + L.plotSize / 2.0, island.plotY + L.plotSize / 2.0}; };
	const auto headingTo = [&](const Island &from, const Island &to)
	{
		const ShapePoint a = plotMiddle(from), b = plotMiddle(to);
		return std::atan2(t.offsetY(int(a.y), int(b.y)), t.offsetX(int(a.x), int(b.x)));
	};
	for (int s = 0; s < cells; ++s)
	{
		Island &island = L.islands[s];
		if (island.plotX < 0)
			continue;
		// The lanes' targets, nearest first: a home's granted islands, then any other plot island
		// beside it; a granted island's home, then the same.
		std::vector<int> targets;
		if (island.home)
			targets = L.grantedCells[island.owner];
		else if (island.owner >= 0)
			targets.push_back(L.homeCell[island.owner]);
		std::vector<int> beside;
		for (int edge : graph.cellEdges[s])
		{
			const int other = graph.other(edge, s);
			if (L.islands[other].plotX >= 0 &&
				std::find(targets.begin(), targets.end(), other) == targets.end())
				beside.push_back(other);
		}
		std::stable_sort(beside.begin(), beside.end(), [&](int a, int b)
						 { return graph.distance2(s, a) < graph.distance2(s, b); });
		targets.insert(targets.end(), beside.begin(), beside.end());
		// A home traces a lane to every granted island (a causeway needs both ends), at least kLanes.
		const int lanes =
			island.home ? std::max(kLanes, int(L.grantedCells[island.owner].size())) : kLanes;
		const ShapePoint middle = plotMiddle(island);
		for (size_t k = 0; k < targets.size() && int(island.laneEnd.size()) < lanes; ++k)
		{
			const double heading = headingTo(island, L.islands[targets[k]]);
			const int end =
				traceRay(L.lane, t, laneStart(int(middle.x), int(middle.y), L.plotSize, heading),
						 heading, 2.0 * landRadius * kCornerReach,
						 [&](int i) { return L.cell[i] != s || !L.land[i]; });
			if (end >= 0)
				island.laneEnd.push_back(end);
		}
		// An island with no neighbour to face (a lone cell) gets its lanes east and west.
		for (int k = int(island.laneEnd.size()); k < lanes; ++k)
		{
			const double heading = k * kPi;
			const int end =
				traceRay(L.lane, t, laneStart(int(middle.x), int(middle.y), L.plotSize, heading),
						 heading, 2.0 * landRadius * kCornerReach,
						 [&](int i) { return L.cell[i] != s || !L.land[i]; });
			if (end >= 0)
				island.laneEnd.push_back(end);
		}
	}
	for (int i = 0; i < n; ++i)
		if (L.lane[i] && L.sketch[i] == GRASS)
			L.sketch[i] = SAND;
	layBeaches(L.sketch, t);
	// CAUSEWAYS (off by default): a line of sand across the strait from a home's lane to each granted
	// island's facing lane, so a colony's own islands are one piece of ground and no unit need swim
	// to work them; the straits between colonies stay water. A causeway is laid after the beaches,
	// like any bridge, and its sand needs none of its own.
	int causeways = 0;
	if (o.causeways)
		for (int k = 0; k < teams; ++k)
		{
			const Island &home = L.islands[L.homeCell[k]];
			for (size_t g = 0; g < L.grantedCells[k].size(); ++g)
			{
				const Island &outer = L.islands[L.grantedCells[k][g]];
				if (g >= home.laneEnd.size() || outer.laneEnd.empty())
					continue;
				const int a = home.laneEnd[g], b = outer.laneEnd[0];
				const ShapePoint from{double(a % t.w), double(a / t.w)};
				const ShapePoint to{from.x + t.offsetX(a % t.w, b % t.w),
									from.y + t.offsetY(a / t.w, b / t.w)};
				causeways += bridgeAcross(L.sketch, t, from, to, kCausewayHalfWidth) > 0;
			}
		}
	context.telemetry.measure("plantations.causeways.laid", causeways);
	return L;
}

bool generate(Game &game, GenerationContext &context)
{
	context.stage = "plantations layout";
	const PlantationsOptions o(context.request);
	const Layout L = design(context.request, context);
	if (!L.failure.empty())
	{
		context.telemetry.fallback("plantations.layout.failure", L.failure);
		context.detail = L.failure;
		return false;
	}
	Map &map = game.map;
	const Torus &t = L.t;
	const int n = t.size(), teams = context.request.nbTeams;
	for (int k = 0; k < teams; ++k)
		game.addTeam();

	context.stage = "plantations terrain";
	writeUndermap(map, L.sketch);

	// THE GRANTED BUILDINGS, before the colonies: a completed swimming pool on every island a colony
	// holds - on the home plot's bottom-right corner, where the swarm (top-left) leaves it, on a granted
	// plot's top-left - an inn on the home plot's top-right corner (first play, 2026-09-15: with the
	// swarm and the pool already on the plot, Maxima and Nicowar fed eighty units through the one inn
	// they managed to fit, and a third of them went hungry; the inn is the building every colony
	// needs first and the plot has exactly one 2x2 corner for it) and, with outpost inns on, an inn
	// on each granted plot's bottom-right corner.
	// They go down before the swarm and its workers so that no worker, dropped at random on a free
	// tile beside the swarm, stands where the pool must go (the first play lost a seed in four to
	// that), and so Team::createLists, run when the swarm goes down, takes them in with it. A home
	// without a pool would strand its colony on one island, so that is a failed candidate; a missing
	// outpost building is only noted.
	context.stage = "plantations buildings";
	const auto plotGrass = [&](const Island &island)
	{
		std::vector<unsigned char> ground(size_t(n), 0);
		for (int dy = 0; dy < L.plotSize; ++dy)
			for (int dx = 0; dx < L.plotSize; ++dx)
				ground[t.at(island.plotX + dx, island.plotY + dy)] = 1;
		return ground;
	};
	for (int k = 0; k < teams; ++k)
	{
		const Island &home = L.islands[L.homeCell[k]];
		const int half = L.plotSize / 2;
		const int pool = placeBuilding(game, k, "swimmingpool", 0, home.plotX + half + 2,
									   home.plotY + half + 2, L.plotSize, plotGrass(home));
		context.telemetry.measure("plantations.home.pool", pool >= 0, k);
		int pools = 1, inns = 0;
		if (placeBuilding(game, k, "inn", 0, home.plotX + L.plotSize - 1, home.plotY + 1,
						  L.plotSize, plotGrass(home)) >= 0)
			++inns;
		else
			context.telemetry.fallback("plantations.home.inn-missing",
									   "No room for an inn on the home plot", k);
		if (pool < 0)
		{
			context.detail =
				"Colony " + std::to_string(k) + "'s home plot has no room for its pool.";
			return false;
		}
		for (int cell : L.grantedCells[k])
		{
			const Island &outer = L.islands[cell];
			const std::vector<unsigned char> allowed = plotGrass(outer);
			if (placeBuilding(game, k, "swimmingpool", 0, outer.plotX + 2, outer.plotY + 2,
							  L.plotSize, allowed) >= 0)
				++pools;
			else
				context.telemetry.fallback("plantations.outpost.pool-missing",
										   "No room for a pool on a granted plot", k);
			if (o.outpostInns)
			{
				if (placeBuilding(game, k, "inn", 0, outer.plotX + L.plotSize - 1,
								  outer.plotY + L.plotSize - 1, L.plotSize, allowed) >= 0)
					++inns;
				else
					context.telemetry.fallback("plantations.outpost.inn-missing",
											   "No room for an inn on a granted plot", k);
			}
		}
		context.telemetry.measure("plantations.pools.placed", pools, k);
		context.telemetry.measure("plantations.inns.placed", inns, k);
	}

	// THE COLONIES. Every swarm stands at its home plot's top-left corner, beside the pool already on
	// the bottom-right, and the rest of the plot stays open; the home ground the swarm and its workers
	// may use is the plot's grass and the walkable band its ring spoils.
	context.stage = "plantations colonies";
	const auto homeMask = [&](int team)
	{
		const Island &home = L.islands[L.homeCell[team]];
		std::vector<unsigned char> ground = dilate(t, plotGrass(home), kRingBand);
		for (int i = 0; i < n; ++i)
			ground[i] =
				ground[i] && L.cell[i] == L.homeCell[team] && !map.isWater(i % t.w, i / t.w);
		return ground;
	};
	const auto anchor = [&](int team)
	{
		const Island &home = L.islands[L.homeCell[team]];
		return MapGeneratorPoint(home.plotX, home.plotY);
	};
	if (!settleColonies(game, context, "plantations-starts", homeMask, anchor))
		return false;

	// THE CROPS. Every plantation's crop band - its pure grass off the plot, the lanes and the swarm's
	// surroundings - under wheat, wood or both in patches: a mixed island is split into a wheat half
	// and a wood half by a broad noise field, and each crop covers kCoverPercent of its candidates (the
	// patchiest first) scaled by its amount, except on the home islands, whose cover is unscaled so a
	// colony always starts with both crops a few steps from its swarm.
	context.stage = "plantations resources";
	const std::vector<unsigned char> reserved = swarmSurroundings(t, context);
	const std::vector<int> patch =
		periodicNoise(t.w, t.h, kPatchPeriod, context.stream("plantations-patch"));
	const std::vector<int> split =
		periodicNoise(t.w, t.h, kSplitPeriod, context.stream("plantations-split"));
	std::vector<std::vector<int>> wheatCandidates(L.islands.size()),
		woodCandidates(L.islands.size());
	for (int i = 0; i < n; ++i)
	{
		const int s = L.cell[i];
		const Island &island = L.islands[s];
		const bool plantation = island.kind == Mixed || island.kind == Wheat ||
								island.kind == Wood || island.kind == Field;
		if (!plantation || !L.land[i] || L.plots.plot[i] || reserved[i] ||
			!clearGround(map, i % t.w, i / t.w))
			continue;
		const bool wheat = island.kind == Wheat ||
						   ((island.kind == Mixed || island.kind == Field) && split[i] >= 32768);
		(wheat ? wheatCandidates : woodCandidates)[s].push_back(i);
	}
	int wheatPlanted = 0, woodPlanted = 0;
	for (size_t s = 0; s < L.islands.size(); ++s)
	{
		const bool home = L.islands[s].home;
		wheatPlanted +=
			plantCoverShare(map, t, wheatCandidates[s], WHEAT,
							home ? kCoverPercent : int(scaledCount(kCoverPercent, o.wheat)),
							[&](int i) { return patch[i]; });
		woodPlanted +=
			plantCoverShare(map, t, woodCandidates[s], WOOD,
							home ? kCoverPercent : int(scaledCount(kCoverPercent, o.wood)),
							[&](int i) { return patch[i]; });
	}
	context.telemetry.measure("plantations.crops.wheat-planted", wheatPlanted);
	context.telemetry.measure("plantations.crops.wood-planted", woodPlanted);
	// THE ISLETS. A stone clump at a rock islet's roomiest tile; three groves round an orchard islet's
	// middle, turned by a random spin so the same fruit is not always north.
	const std::vector<int> room = clearance(t, L.land);
	int rocksStocked = 0, orchardsStocked = 0;
	for (size_t s = 0; s < L.islands.size(); ++s)
	{
		const Island &island = L.islands[s];
		if (island.kind != Rock && island.kind != Orchard)
			continue;
		const auto onIslet = [&](int i)
		{ return L.cell[i] == int(s) && L.land[i] && clearGround(map, i % t.w, i / t.w); };
		std::vector<unsigned char> islet(size_t(n), 0);
		for (int i = 0; i < n; ++i)
			islet[i] = onIslet(i);
		const int middle = roomiestTile(t, islet, room, island.site.x, island.site.y);
		if (middle < 0)
		{
			context.telemetry.fallback("plantations.islet.bare", "An islet has no grass to stock",
									   int(s));
			continue;
		}
		const int mx = middle % t.w, my = middle / t.w;
		if (island.kind == Rock)
			rocksStocked +=
				placeResourceClump(map, context, MapGeneratorPoint(mx, my), STONE, kRockRadius) > 0;
		else
		{
			const double spin = context.bounded("plantations-fruit", 3600) / 3600.0 * 2 * kPi;
			orchardsStocked += plantOrchard(map, t, context, mx, my, kGroveRing, {spin},
											2 * kPi * kGroveRing / 3, 3, 1, onIslet) > 0;
		}
	}
	context.telemetry.measure("plantations.islets.rocks-stocked", rocksStocked);
	context.telemetry.measure("plantations.islets.orchards-stocked", orchardsStocked);
	seedAlgae(map, context, t, "plantations-algae", o.algae,
			  AlgaeBand::anyWater(kAlgaeTilesPerClump));
	// The plots stay clear of everything the layers above may have dropped on them, and every colony
	// keeps its crops within reach (a backstop: the home island's cover already puts both a few steps
	// from the swarm). No cramped-start relief here: a plot is the room by design, and the relief would
	// clear the crop band to make more, which is exactly what the map withholds.
	clearFarmPlots(map, t, {L.plots});
	secureStartingCrops(game, context, t);
	clearFarmPlots(map, t, {L.plots});
	return true;
}

std::string validateWorld(const Game &game, const GenerationContext &context)
{
	GenerationContext replay(context.request);
	const Layout L = design(context.request, replay);
	const Map &map = game.map;
	if (const std::string mismatch = designMismatch(L, map, "plantations"); !mismatch.empty())
		return mismatch;
	const PlantationsOptions o(context.request);
	const Torus &t = L.t;
	const int n = t.size(), teams = context.request.nbTeams;
	// Every plot is buildable ground: pure grass with no deposit on any of its tiles.
	for (int i = 0; i < n; ++i)
		if (L.plots.plot[i] && (!map.isGrass(i % t.w, i / t.w) || map.isResource(i % t.w, i / t.w)))
			return "A plot is not clear grass at (" + std::to_string(i % t.w) + ", " +
				   std::to_string(i / t.w) + ").";
	// Every plantation carries wheat and wood and nothing else.
	for (int i = 0; i < n; ++i)
	{
		const int kind = L.islands[L.cell[i]].kind;
		if (!L.land[i] || (kind != Mixed && kind != Wheat && kind != Wood && kind != Field))
			continue;
		if (map.isResource(i % t.w, i / t.w))
		{
			const int type = map.getResource(i % t.w, i / t.w).type;
			if (type != WHEAT && type != WOOD)
				return "A plantation carries a deposit that is neither wheat nor wood at (" +
					   std::to_string(i % t.w) + ", " + std::to_string(i / t.w) + ").";
		}
	}
	// Every colony kept the pools it was granted: one on its home and one per granted island.
	for (int k = 0; k < teams; ++k)
		if (countBuildings(game, k, "swimmingpool") < 1 + int(L.grantedCells[k].size()))
			return "Colony " + std::to_string(k) + " has lost a swimming pool.";
	// No colony can walk to another: the straits hold, causeways or not.
	if (const std::string leak = coloniesApart(map, teams, "across a strait"); !leak.empty())
		return leak;
	// With causeways, every colony walks to each of its granted plots.
	if (o.causeways)
	{
		const std::vector<std::vector<int>> units = unitTilesByTeam(map, teams);
		const std::vector<unsigned char> ground = groundUnitTiles(map);
		for (int k = 0; k < teams; ++k)
		{
			if (units[k].empty())
				continue;
			const std::vector<int> steps = stepsFrom(t, tileMask(t, units[k]), ground);
			for (int cell : L.grantedCells[k])
			{
				const Island &outer = L.islands[cell];
				bool reached = false;
				for (int dy = -kRingBand; dy < L.plotSize + kRingBand && !reached; ++dy)
					for (int dx = -kRingBand; dx < L.plotSize + kRingBand && !reached; ++dx)
						reached = steps[t.at(outer.plotX + dx, outer.plotY + dy)] >= 0;
				if (!reached)
					return "Colony " + std::to_string(k) +
						   " cannot walk its causeway to a granted island.";
			}
		}
	}
	return "";
}
} // namespace

PlantationsOptions::PlantationsOptions(const GenerationRequest &r)
	: plotSize(r.option("plot-size")), farmWidth(r.option("farm-width")),
	  straitWidth(r.option("strait-width")), islandsPerColony(r.option("islands-per-colony")),
	  coastRoughness(r.option("coast-roughness")), outpostInns(r.option("outpost-inns") != 0),
	  causeways(r.option("causeways") != 0), wheat(r.option("wheat-amount")),
	  wood(r.option("wood-amount")), stone(r.option("stone-amount")),
	  algae(r.option("algae-amount")), fruit(r.option("fruit-amount"))
{
}

GeneratorDefinition plantationsDefinition()
{
	GeneratorDefinition definition{
		"plantations",
		34,
		"Plantations",
		1,
		false,
		// A plot of 8 seats a swarm and a pool with half the plot left (8 is the least that seats
		// both); 4 tiles of crops round it keep an island small enough to swim round and fertile to
		// its middle; a strait of 4 corners is sealed against diagonal steps and out of reach of a
		// level-2 tower (Channels.h); three islands per colony is a base's worth of plots.
		{{"plot-size", "Plot size", 8, 12, 1, 8, ControlGroup::Layout},
		 {"farm-width", "Farm width", 3, 8, 1, 4, ControlGroup::Terrain},
		 {"strait-width", "Strait width", 4, 8, 1, 4, ControlGroup::Terrain},
		 {"islands-per-colony", "Islands per colony", 1, 4, 1, 3, ControlGroup::Layout},
		 {"coast-roughness", "Coast roughness", 0, 100, 10, 40, ControlGroup::Terrain},
		 GeneratorControl::toggle("outpost-inns", "Outpost inns", true, ControlGroup::Layout),
		 GeneratorControl::toggle("causeways", "Causeways", false, ControlGroup::Layout),
		 GeneratorControl::percentage("wheat-amount", "Wheat amount"),
		 GeneratorControl::percentage("wood-amount", "Wood amount"),
		 GeneratorControl::percentage("stone-amount", "Stone amount"),
		 GeneratorControl::percentage("algae-amount", "Algae amount"),
		 GeneratorControl::percentage("fruit-amount", "Fruit amount")},
		generate,
		true,
		designFailure<design>,
		validateWorld};
	// Room is scarce by design: a home plot holds a handful of building sites after its swarm and
	// pool, so the ranking measures room against that rather than a whole meadow, and a colony's
	// rivals are always out of walking reach, so isolation tells the candidates nothing apart.
	definition.qualityScale.roomReference = 24;
	definition.qualityWeights.isolation = 0.0;
	definition.qualityWeights.room = 0.15;
	definition.qualityWeights.fertility = 0.29;
	return definition;
}
