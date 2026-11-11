// SPDX-License-Identifier: GPL-3.0-or-later
#include "OldTownGenerator.h"
#include "Climates.h"
#include "Farmland.h"
#include "FertilityField.h"
#include "GenerationWorld.h"
#include "GenerationContext.h"
#include "Geometry.h"
#include "Grid.h"
#include "Homes.h"
#include "LatticeNoise.h"
#include "Morphology.h"
#include "Pipeline.h"
#include "Planting.h"
#include "Resources.h"
#include "Roads.h"
#include "Settlements.h"
#include "Sketch.h"
#include "Tessellation.h"
#include "Walls.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <numeric>
#include <string>
#include <vector>
using namespace MapGeneration;

// Old town: a dense medieval town of houses and narrow paved streets inside the ruins of its wall,
// with hedged fields all round it.
//
// The town is a warped grid of irregular blocks (Tessellation.h). Every street between blocks is
// paved: twice as fast to walk, nothing grows on it, and it can still be built on, so every
// building a player puts up closes a street: the players build the town's chokepoints themselves,
// and a tower on a street fires over the houses into the next. Some blocks are plazas: every
// colony's home is three of them (a fountain garden, a green for the swarm and a mason's yard), the extra plazas are paved
// market squares with a fountain and a fruit grove, and the cathedral square at the middle keeps an
// orchard of all three fruits round its fountain. Outside the town the land is laid out in farm
// rows dotted with building plots, and short country roads run in from the fields.
//
// REVISION 10 (OLD TOWN 2.0, 2026-11-11). Revisions 1 to 9 drew every block as solid stone: the
// town's walls, its tower cover and an endless quarry at once, on grass streets, with sand farm caps,
// sand tendrils and beaches. 2.0 draws it from the terrain catalogue in one of four climates:
//  - Blocks are urban fabric of five kinds: rows of houses (Ridges masses towers shoot over),
//    courtyard houses with a door and a court inside, paved workshop yards, garden plots round a
//    well-pond, and rubble lots where a house fell down.
//  - Stone is placed rather than built in: a mason's yard in every home (a stack of stone, the same
//    for every colony), faces at the robbed foot of the old town wall, and the rubble lots, the last
//    two scaled by the stone amount.
//  - The town wall stands again as a ruin: broken Ridges stretches round the town, breached wherever a
//    street or a country road meets it and where it has collapsed (a continuous wall with gates was
//    rejected in play as unfair to whoever started far from one).
//  - The farm rows' caps are barren and their crossings paths; the tendrils are country roads.
//    No beaches.
//
// FEEDBACK 2026-09-13 (revisions 2 to 9, still binding): no continuous wall, free access to the
// fields from every home; about three farm plots per colony in the fields; pools that fill their
// plazas to a tile short of the streets; fruit in many plazas; short, frequent, wavy roads in from
// the fields' edge.
namespace
{

// Every home's starting kit, unscaled whatever the amounts say: wheat and wood beside the fountain.
constexpr int kHomeWheat = 14, kHomeWood = 12;
// The mason's yard: a stack of this much stone on a block of its own beside every home, the same
// for every home.
constexpr int kMasonStone = 12;
// ...and this much wheat and wood beside it.
constexpr int kYardStore = 8;
// A home's court block is a green round its swarm: a paved court a 4x4 swarm fits on would leave its
// rim of paving against grass, which cannot be built on.
// The robbed wall: a face of stone at the wall's inner foot every this many tiles round it.
constexpr int kWallFaceSpacing = 40, kWallFaceStone = 8;
// Outside the town the farm rows keep this margin of grass from the outermost blocks; a farm plot's
// middle keeps this far inside the fields (FarmPlot: 10 wide, 4 high, a ring of 2 and a vertex more).
constexpr int kFarmMargin = 4, kFarmRim = 2, kFarmBridges = 16, kPlotMargin = 9;
// Country roads: one every this many tiles round the fields' edge, this long (plus up to
// kRoadExtra), wandering sideways by up to kRoadWander, stroked kRoadHalfWidth either side.
constexpr int kRoadSpacing = 16, kRoadLength = 10, kRoadExtra = 3;
constexpr double kRoadWander = 2.0, kRoadHalfWidth = 1.5;
// The town wall: a ring this far outside the town's edge, this thick, breached this far round
// every street and road that meets it.
constexpr double kWallRadius = 1.5, kWallHalfThickness = 1.5, kBreachReach = 2.5;

// The climates' looks for Old town: the shared four with the Obstacles look a hedge or thicket,
// since a hedge is the only Obstacles terrain the town draws (it never draws a cliff).
constexpr std::array<ClimateLook, kClimateCount> oldTownLooks()
{
	std::array<ClimateLook, kClimateCount> looks{};
	for (int k = 0; k < kClimateCount; ++k)
		looks[k] = kClimateLooks[k];
	looks[0].cliff = HEDGE;
	looks[1].cliff = THICKET;
	looks[2].cliff = THICKET;
	looks[3].cliff = HEDGE;
	return looks;
}
constexpr std::array<ClimateLook, kClimateCount> kOldTownLooks = oldTownLooks();
static_assert(terrainGroup(kOldTownLooks[0].cliff) == TerrainGroup::Obstacles &&
				  terrainGroup(kOldTownLooks[1].cliff) == TerrainGroup::Obstacles &&
				  terrainGroup(kOldTownLooks[2].cliff) == TerrainGroup::Obstacles &&
				  terrainGroup(kOldTownLooks[3].cliff) == TerrainGroup::Obstacles,
			  "Old town's hedges keep the Obstacles rules.");

enum BlockKind { Houses, Courtyard, Workshop, GardenPlot, RubbleLot, kBlockKinds };
constexpr const char *kBlockNames[kBlockKinds] = {"houses", "courtyard", "workshop", "garden", "rubble"};
constexpr int kBlockWeights[kBlockKinds] = {55, 15, 10, 10, 10};

// A fountain fills its plaza to this far short of the streets round it (FEEDBACK 2026-09-13: pools
// "big enough that they push up close against their surrounding grid cells' stone").
double fountainRadiusFor(int blockSize, int streetWidth)
{
	return std::max(2.5, blockSize / 2.0 - streetWidth / 2.0 - 1.0);
}

struct Layout
{
	Torus t{1, 1};
	Tessellation g;
	double cx = 0, cy = 0, cityRadius = 0, homeRadius = 0;
	std::vector<int> cell, kind;
	std::vector<int> homeCell, annexCell, yardCell, plazaCell;
	std::vector<double> axes;
	int cathedral = -1, climate = 0;
	std::vector<int> homeOf, masonSeed;
	std::vector<unsigned char> city, block, street, water, road, farmRegion, house, wall, rubble,
		paved, rock;
	std::vector<ShapePoint> homes, kits, wallFaces;
	Farm farm;
	double fountainRadius = 0;
	TerrainSketch sketch, finished;
	int breaches = 0, unthickened = 0;
	std::array<int, kBlockKinds> kindCounts{};
	std::string failure;
};

Layout design(const GenerationRequest &request, GenerationContext &context)
{
	const OldTownOptions o(request);
	Layout L;
	L.t = {1 << request.wDec, 1 << request.hDec};
	const Torus &t = L.t;
	const int n = t.size(), teams = std::max(1, request.nbTeams);
	L.cx = t.w / 2.0;
	L.cy = t.h / 2.0;
	L.cityRadius = o.citySize / 100.0 * std::min(t.w, t.h) / 2.0;

	// The blocks: a square tiling warped into irregular polygons. Every edge is a street.
	L.g = squareTessellation(t.w, t.h, o.blockSize);
	const std::vector<unsigned char> walls(L.g.edges.size(), 1);
	warpCorners(L.g, warpLimit(L.g) * o.warp / 100, walls, o.streetWidth + 6, o.blockSize / 4,
				context, "town-warp");
	L.cell = labelTiles(L.g);
	if (L.cell.empty())
	{
		L.failure = "The blocks could not be labelled.";
		return L;
	}
	std::vector<unsigned char> inCity(L.g.cellCount(), 0);
	int cityCells = 0;
	for (int cell = 0; cell < L.g.cellCount(); ++cell)
	{
		const ShapePoint c = tilePoint(L.g.cells[cell].centre);
		const double d = std::hypot(t.offsetX(int(L.cx), int(c.x)), t.offsetY(int(L.cy), int(c.y)));
		inCity[cell] = d + o.blockSize / 2.0 < L.cityRadius;
		cityCells += inCity[cell];
	}
	L.cathedral = L.cell[t.at(int(L.cx), int(L.cy))];
	if (cityCells < 2 * teams + 1)
	{
		L.failure = "The city has too few blocks for this many colonies; use a bigger city, smaller "
					"blocks or fewer colonies.";
		return L;
	}
	{
		// Farthest-point spreading over the city's cells, the cathedral square taken first so no
		// colony starts on it. A home is three blocks: a fountain garden, beside it towards the
		// middle of town a green for the swarm, and beside that a mason's yard.
		std::vector<long long> nearest(L.g.cellCount(), 0);
		std::vector<unsigned char> taken(L.g.cellCount(), 0);
		taken[L.cathedral] = 1;
		for (int cell = 0; cell < L.g.cellCount(); ++cell)
			nearest[cell] = L.g.distance2(cell, L.cathedral);
		const int extra = std::min(o.plazas * teams, std::max(0, (cityCells - 1 - 3 * teams) / 2));
		// (Three blocks a home where the town has room, two where the yard shares the green.)
		context.telemetry.measure("old-town.plazas.target", o.plazas * teams);
		context.telemetry.measure("old-town.plazas.fitted-target", extra);
		if (extra < o.plazas * teams)
			context.telemetry.fallback("old-town.plazas.reduced",
									   "Home pairs take priority over extra plazas in a small city.");
		for (int k = 0; k < teams + extra; ++k)
		{
			int best = -1;
			for (int cell = 0; cell < L.g.cellCount(); ++cell)
				if (inCity[cell] && !taken[cell] && (best < 0 || nearest[cell] > nearest[best]))
					best = cell;
			if (best < 0)
				break;
			taken[best] = 1;
			if (k < teams)
			{
				int annex = -1;
				for (int edge : L.g.cells[best].edges)
				{
					const int next = L.g.other(edge, best);
					if (inCity[next] && !taken[next] &&
						(annex < 0 ||
						 L.g.distance2(next, L.cathedral) < L.g.distance2(annex, L.cathedral)))
						annex = next;
				}
				if (annex < 0)
					break;
				taken[annex] = 1;
				// The mason's yard: a free neighbouring block of the court block (or of the fountain
				// block), nearest the court.
				int yardCell = -1;
				for (int from : {annex, best})
				{
					for (int edge : L.g.cells[from].edges)
					{
						const int next = L.g.other(edge, from);
						if (inCity[next] && !taken[next] &&
							(yardCell < 0 || L.g.distance2(next, annex) < L.g.distance2(yardCell, annex)))
							yardCell = next;
					}
					if (yardCell >= 0) break;
				}
				// A town too small for a yard of its own puts the yard on the green.
				if (yardCell < 0 || cityCells < 3 * teams + 1)
					yardCell = annex;
				taken[yardCell] = 1;
				L.homeCell.push_back(best);
				L.annexCell.push_back(annex);
				L.yardCell.push_back(yardCell);
				for (int cell = 0; cell < L.g.cellCount(); ++cell)
					nearest[cell] = std::min(nearest[cell], L.g.distance2(cell, annex));
			}
			else
				L.plazaCell.push_back(best);
			for (int cell = 0; cell < L.g.cellCount(); ++cell)
				nearest[cell] = std::min(nearest[cell], L.g.distance2(cell, best));
		}
		if (int(L.homeCell.size()) != teams)
		{
			L.failure = "The city has too few blocks for this many colonies; use a bigger city, "
						"smaller blocks or fewer colonies.";
			return L;
		}
		std::vector<int> order(teams);
		std::iota(order.begin(), order.end(), 0);
		dealStarts(context, order);
		std::vector<int> dealtHomes, dealtAnnexes, dealtYards;
		for (int k : order)
		{
			dealtHomes.push_back(L.homeCell[k]);
			dealtAnnexes.push_back(L.annexCell[k]);
			dealtYards.push_back(L.yardCell[k]);
		}
		L.homeCell = dealtHomes;
		L.annexCell = dealtAnnexes;
		L.yardCell = dealtYards;
	}
	L.climate = drawClimate(context, "old-town-climate");
	context.telemetry.choice("old-town.climate", kOldTownLooks[L.climate].name);

	// Streets: the band along every cell border inside the town; blocks: the rest of the town's cells.
	L.city.assign(n, 0);
	std::vector<unsigned char> border(n, 0);
	std::vector<double> radial(n, 0.0);
	for (int i = 0; i < n; ++i)
	{
		const int x = t.remainderX(i), y = i / t.w;
		radial[i] = std::hypot(t.offsetX(int(L.cx), x), t.offsetY(int(L.cy), y));
		L.city[i] = radial[i] < L.cityRadius;
		for (const auto &s : kCardinalSteps)
			if (L.cell[t.at(x + s[0], y + s[1])] != L.cell[i])
				border[i] = 1;
	}
	// The paved street is the band along every cell border, as wide as it always was: a 4x4 building
	// fits on it, so a colony can build in its streets and close them.
	L.street = dilateRound(t, border, o.streetWidth / 2.0);
	std::vector<int> plazaKind(L.g.cellCount(), 0); // 1 cathedral, 2 home fountain, 3 home green, 4 market, 5 mason's yard
	plazaKind[L.cathedral] = 1;
	for (int cell : L.homeCell) plazaKind[cell] = 2;
	for (int cell : L.annexCell) plazaKind[cell] = 3;
	for (int k = 0; k < int(L.yardCell.size()); ++k)
		if (L.yardCell[k] != L.annexCell[k]) plazaKind[L.yardCell[k]] = 5;
	for (int cell : L.plazaCell) plazaKind[cell] = 4;
	L.block.assign(n, 0);
	for (int i = 0; i < n; ++i)
		L.block[i] = L.city[i] && inCity[L.cell[i]] && !L.street[i] && !plazaKind[L.cell[i]];

	// Every block's kind, from its own stream.
	L.kind.assign(L.g.cellCount(), -1);
	for (int cell = 0; cell < L.g.cellCount(); ++cell)
	{
		if (!inCity[cell] || plazaKind[cell]) continue;
		int roll = context.bounded("town-blocks", 100), k = 0;
		while (k + 1 < kBlockKinds && roll >= kBlockWeights[k]) roll -= kBlockWeights[k++];
		L.kind[cell] = k;
		++L.kindCounts[k];
	}
	for (int k = 0; k < kBlockKinds; ++k)
		context.telemetry.measure(std::string("old-town.blocks.") + kBlockNames[k], L.kindCounts[k]);
	// A block's back: a direction per block (from its own stream) a partial house leans towards.
	std::vector<ShapePoint> back(L.g.cellCount(), ShapePoint{1, 0});
	for (int cell = 0; cell < L.g.cellCount(); ++cell)
		if (L.kind[cell] >= 0)
		{
			const double a = context.bounded("town-backs", 3600) / 3600.0 * 2 * kPi;
			back[cell] = {std::cos(a), std::sin(a)};
		}
	std::vector<unsigned char> notBlock(n, 0);
	for (int i = 0; i < n; ++i) notBlock[i] = !L.block[i];
	const std::vector<int> depth = stepsFrom(t, notBlock);

	// Fountains: a pond at the middle of every plaza; the colonies' kits round theirs.
	L.water.assign(n, 0);
	L.homeOf.assign(n, -1);
	L.homeRadius = std::max(6.0, o.blockSize / 2.0 - o.streetWidth / 2.0 - 1);
	L.fountainRadius = fountainRadiusFor(o.blockSize, o.streetWidth);
	const RadialShape fountain(L.fountainRadius, 0.25, context, "town-fountain");
	for (int k = 0; k < teams; ++k)
	{
		const ShapePoint fountainAt = tilePoint(L.g.cells[L.homeCell[k]].centre);
		const ShapePoint swarmAt = tilePoint(L.g.centreAcross(
			[&]
			{
				for (int edge : L.g.cells[L.homeCell[k]].edges)
					if (L.g.other(edge, L.homeCell[k]) == L.annexCell[k])
						return edge;
				return L.g.cells[L.homeCell[k]].edges[0];
			}(),
			L.homeCell[k]));
		L.homes.push_back(swarmAt);
		L.kits.push_back(fountainAt);
		L.axes.push_back(std::atan2(swarmAt.y - fountainAt.y, swarmAt.x - fountainAt.x) + kPi);
		fillShape(L.water, t, fountainAt.x, fountainAt.y, fountain, 0.0);
		for (int i = 0; i < n; ++i)
			if (L.cell[i] == L.homeCell[k] || L.cell[i] == L.annexCell[k] || L.cell[i] == L.yardCell[k])
				L.homeOf[i] = k;
	}
	for (int cell : L.plazaCell)
	{
		const ShapePoint c = tilePoint(L.g.cells[cell].centre);
		fillShape(L.water, t, c.x, c.y, fountain, 0.0);
	}
	{
		const ShapePoint c = tilePoint(L.g.cells[L.cathedral].centre);
		fillShape(L.water, t, c.x, c.y, fountain, 0.0);
	}
	// Garden plots' well-ponds.
	for (int cell = 0; cell < L.g.cellCount(); ++cell)
		if (L.kind[cell] == GardenPlot)
		{
			const ShapePoint c = tilePoint(L.g.cells[cell].centre);
			const RadialShape well(std::max(2.0, L.fountainRadius - 1.5), 0.2, context, "town-wells");
			fillShape(L.water, t, c.x, c.y, well, 0.0);
		}

	// Outside the town: farm rows over everything beyond the margin, along the map's axis, bridged
	// every kFarmBridges tiles, then building plots spread through the fields (FEEDBACK 2026-09-13).
	L.farmRegion.assign(n, 0);
	for (int i = 0; i < n; ++i)
		L.farmRegion[i] = radial[i] > L.cityRadius + kFarmMargin;
	TerrainSketch rows(n, GRASS);
	L.farm = layFarm(rows, t, L.farmRegion, 0.0, {L.cx, L.cy}, kFarmRim, bestFarmRows(0.0), nullptr,
					 {kFarmBridges, o.waterCrossings, o.cropCrossings}, true);
	const FarmPlot plot;
	const std::vector<unsigned char> roomy = erode(t, L.farmRegion, kPlotMargin);
	std::vector<unsigned char> keepClear(n, 0);
	for (int i = 0; i < n; ++i)
		keepClear[i] = !L.farmRegion[i];
	int plotsPlaced = 0;
	for (int p = 0; p < o.farmPlots * teams; ++p)
	{
		const std::vector<std::int64_t> clearance = distanceSquaredTo(t, keepClear);
		int site = -1;
		for (int i = 0; i < n; ++i)
			if (roomy[i] && (site < 0 || clearance[i] > clearance[site]))
				site = i;
		if (site < 0)
			break;
		const int x0 = t.remainderX(site) - plot.width / 2, y0 = site / t.w - plot.height / 2;
		stampFarmPlot(rows, t, L.farm, x0, y0, plot);
		++plotsPlaced;
		for (int dy = -kPlotMargin; dy <= kPlotMargin; ++dy)
			for (int dx = -kPlotMargin; dx <= kPlotMargin; ++dx)
				keepClear[t.at(t.remainderX(site) + dx, site / t.w + dy)] = 1;
	}
	for (int i = 0; i < n; ++i)
		if (L.farm.water[i])
			L.water[i] = 1;
	// Country roads (FEEDBACK 2026-09-13, the tendrils): short wavy roads from the fields' edge in
	// towards the town, one every kRoadSpacing tiles round it.
	L.road.assign(n, 0);
	if (o.tendrils)
	{
		const double ring = L.cityRadius + kFarmMargin + 1;
		const int count = std::max(1, int(std::lround(2 * kPi * ring / kRoadSpacing)));
		const double phase = context.bounded("town-tendrils", 3600) / 3600.0 * 2 * kPi;
		std::mt19937 &wobble = context.stream("town-tendrils");
		for (int k = 0; k < count; ++k)
		{
			const double a = phase + 2 * kPi * k / count;
			const double length = kRoadLength + context.bounded("town-tendrils", kRoadExtra + 1);
			const ShapePoint from = polarPoint(L.cx, L.cy, ring, a);
			const ShapePoint to = polarPoint(L.cx, L.cy, ring - length, a);
			strokePath(L.road, t, wanderingPath(t, from, to, kRoadHalfWidth, kRoadWander, 0.0, wobble));
		}
		for (int i = 0; i < n; ++i)
			if (L.water[i])
				L.road[i] = 0;
	}

	// THE TOWN WALL: a ring just outside the town's edge, breached round every street and road that
	// meets it, and wherever its collapse noise says it fell.
	const std::vector<int> collapse = periodicNoise(t.w, t.h, 16, context.stream("town-wall"));
	std::vector<unsigned char> openings(n, 0);
	for (int i = 0; i < n; ++i)
		openings[i] = L.road[i] || (L.street[i] && L.city[i]);
	const std::vector<unsigned char> nearOpening = dilateRound(t, openings, kBreachReach);
	std::vector<unsigned char> ring(n, 0);
	L.wall.assign(n, 0);
	const double wallAt = L.cityRadius + kWallRadius;
	for (int i = 0; i < n; ++i)
	{
		ring[i] = std::abs(radial[i] - wallAt) <= kWallHalfThickness && !L.water[i];
		L.wall[i] = ring[i] && !nearOpening[i] && collapse[i] < 46000;
	}
	{
		// Breaches: the gaps round the ring, counted by walking it in angle.
		const int steps = std::max(64, int(2 * kPi * wallAt));
		bool previous = false, first = false;
		for (int s = 0; s <= steps; ++s)
		{
			const ShapePoint p = polarPoint(L.cx, L.cy, wallAt, 2 * kPi * s / steps);
			const bool solid = L.wall[t.at(int(std::floor(p.x)), int(std::floor(p.y)))];
			if (s == 0) first = solid;
			else if (previous && !solid) ++L.breaches;
			previous = solid;
		}
		(void)first;
		context.telemetry.measure("old-town.wall.breaches", L.breaches);
		// The robbed wall's quarry faces, at its inner foot.
		const int faces = std::max(1, int(2 * kPi * wallAt / kWallFaceSpacing));
		for (int k = 0; k < faces; ++k)
			L.wallFaces.push_back(polarPoint(L.cx, L.cy, wallAt - kWallHalfThickness - 2.5, 2 * kPi * (k + 0.5) / faces));
	}

	// THE BLOCKS. Houses are wall tiles turned into Ridges below; yards and squares are paved.
	L.house.assign(n, 0);
	L.rubble.assign(n, 0);
	L.paved.assign(n, 0);
	for (int i = 0; i < n; ++i)
	{
		if (!L.block[i] || L.road[i] || L.water[i]) continue;
		const int cell = L.cell[i];
		const ShapePoint c = tilePoint(L.g.cells[cell].centre);
		const double ox = t.offsetX(int(c.x), t.remainderX(i)), oy = t.offsetY(int(c.y), i / t.w);
		const double along = ox * back[cell].x + oy * back[cell].y;
		const double across = -ox * back[cell].y + oy * back[cell].x;
		switch (L.kind[cell])
		{
		case Houses:
			// Rows of houses round the block; a large block keeps a back yard in the middle.
			L.house[i] = depth[i] <= 4;
			break;
		case Courtyard:
			// A ring of building with a court inside, its door on the block's back.
			L.house[i] = depth[i] <= 3 && !(along > 0 && std::abs(across) <= 1.5);
			break;
		case Workshop:
			// A paved yard with a workshop along its back.
			// An open yard (grass, buildable) with a workshop along its back.
			L.house[i] = along > o.blockSize * 0.18 && depth[i] <= 3;
			break;
		case GardenPlot:
			break;
		case RubbleLot:
			// What is left of a house: a corner standing on the block's back, the rest fallen.
			L.house[i] = along > o.blockSize * 0.25 && depth[i] <= 2;
			L.rubble[i] = !L.house[i] && depth[i] >= 2;
			break;
		}
	}
	// Market squares are paved round a grass grove ring.
	for (int i = 0; i < n; ++i)
	{
		const int cell = L.cell[i];
		if (!L.city[i] || L.water[i]) continue;
		if (cell >= 0 && plazaKind[cell] == 4 && !L.street[i])
		{
			const ShapePoint c = tilePoint(L.g.cells[cell].centre);
			const double d = std::hypot(t.offsetX(int(c.x), t.remainderX(i)), t.offsetY(int(c.y), i / t.w));
			L.paved[i] = d > L.fountainRadius + 3.5;
		}
	}
	L.masonSeed.assign(teams, -1);
	for (int k = 0; k < teams; ++k)
	{
		const ShapePoint yard = tilePoint(L.g.cells[L.yardCell[k]].centre);
		L.masonSeed[k] = t.at(int(std::lround(yard.x)), int(std::lround(yard.y)));
	}

	// THE SKETCH, one terrain per rule.
	TerrainSketch &s = L.sketch;
	s.assign(n, GRASS);
	for (int i = 0; i < n; ++i)
	{
		if (L.farm.sand[i]) s[i] = SAND;
		if (L.water[i]) s[i] = WATER;
	}
	dressFarmSand(s, L.farm);
	const std::vector<unsigned char> pavedCorners = tileCorners(t, L.paved);
	for (int i = 0; i < n; ++i)
	{
		if (s[i] == WATER) continue;
		if ((L.street[i] && L.city[i]) || L.road[i] || pavedCorners[i]) s[i] = TRAIL;
	}
	// Walls to vertices, leaning away from the streets, roads and squares; then three vertices
	// thick, never onto paving, water, a home, a plaza or the fields' rows.
	std::vector<unsigned char> open(n, 0), wallTiles(n, 0);
	for (int i = 0; i < n; ++i)
	{
		open[i] = (L.street[i] && L.city[i]) || L.road[i] || L.paved[i];
		wallTiles[i] = (L.house[i] && !open[i]) || L.wall[i];
	}
	const std::vector<unsigned char> keep = tileCorners(t, open);
	std::vector<unsigned char> reserved(n, 0);
	for (int i = 0; i < n; ++i)
		reserved[i] = L.homeOf[i] >= 0 || L.water[i] || (L.cell[i] >= 0 && plazaKind[L.cell[i]] && !L.street[i]) ||
			L.farmRegion[i] || L.rubble[i];
	const std::vector<unsigned char> reservedCorners = tileCorners(t, reserved);
	// A building or the wall takes every corner its tiles touch except the corners it shares with
	// open ground (a street, a road, a court), so it fills its block to the paving evenly on every
	// side and leaves the paving whole.
	const std::vector<unsigned char> wallCorners = tileCorners(t, wallTiles);
	for (int i = 0; i < n; ++i)
		if (wallCorners[i] && !keep[i] && s[i] == GRASS && (!reservedCorners[i] || L.wall[i]))
			s[i] = RIDGE_ROCK;
	const auto canGrow = [&](int i) { return s[i] == GRASS && !keep[i] && !reservedCorners[i]; };
	thickenRidges(s, t, canGrow, &L.unthickened, RIDGE_ROCK);
	L.rock.assign(n, 0);
	std::vector<unsigned char> wallVertex(n, 0);
	const std::vector<unsigned char> wallTileCorners = tileCorners(t, L.wall);
	for (int i = 0; i < n; ++i)
	{
		L.rock[i] = s[i] == RIDGE_ROCK;
		wallVertex[i] = L.rock[i] && wallTileCorners[i] && !L.city[i];
	}
	context.telemetry.measure("old-town.walls.unthickened-vertices", L.unthickened);

	// THE LOOK, painted last.
	L.finished = s;
	LookRoles roles;
	roles.tended = [&L](int i) { return L.homeOf[i] >= 0; };
	roles.farm = [&L](int i) { return L.farmRegion[i] != 0; };
	roles.rockAccent = [&wallVertex](int i) { return wallVertex[i] != 0; };
	roles.path = [&L](int i)
	{
		if (L.road[i] && !L.city[i]) return PathLook::Road;
		if (L.farmRegion[i]) return PathLook::Road;
		return (L.street[i] && L.city[i]) ? PathLook::Street : PathLook::Town;
	};
	paintClimate(L.finished, t, kOldTownLooks[L.climate], roles);

	context.telemetry.measure("old-town.farm-plots.actual", plotsPlaced);
	context.telemetry.measure("old-town.city.cells", cityCells);
	context.telemetry.measure("old-town.plazas.actual", L.plazaCell.size());
	context.telemetry.measure("old-town.fountain.radius", L.fountainRadius);
	return L;
}

bool generate(GenerationWorld &game, GenerationContext &context)
{
	context.stage = "old town layout";
	const OldTownOptions o(context.request);
	const Layout L = design(context.request, context);
	if (!L.failure.empty())
	{
		context.detail = L.failure;
		return false;
	}
	GenerationMap &map = game.map;
	const Torus &t = L.t;
	const int n = t.size(), teams = context.request.nbTeams;
	for (int k = 0; k < teams; ++k)
		game.addTeam();

	context.stage = "old town terrain";
	writeVertices(map, L.finished);

	// The mason's yards first, before anyone stands near them: the same stone in every home.
	context.stage = "old town stone";
	// Every home gets the same stack: as much as the smallest yard holds, up to kMasonStone.
	const auto yard = [&](int k)
	{
		const ShapePoint swarm = L.homes[k];
		const bool shared = L.yardCell[k] == L.annexCell[k];
		return [&, k, swarm, shared](int i)
		{
			return (shared || L.cell[i] == L.yardCell[k]) && L.homeOf[i] == k && clearGround(map, t.remainderX(i), i / t.w) &&
				   (!shared || std::hypot(t.offsetX(int(swarm.x), t.remainderX(i)), t.offsetY(int(swarm.y), i / t.w)) > 3.0);
		};
	};
	int stack = kMasonStone;
	std::vector<int> seeds(teams, -1);
	for (int k = 0; k < teams; ++k)
	{
		const int at = L.masonSeed[k];
		seeds[k] = seedNear(t, t.remainderX(at), at / t.w, 8, yard(k));
		if (seeds[k] < 0) { stack = 0; break; }
		// The yard's room: the eligible ground connected to its seed.
		std::vector<unsigned char> seen(n, 0);
		std::vector<int> queue{seeds[k]};
		seen[seeds[k]] = 1;
		const auto eligible = yard(k);
		for (size_t q = 0; q < queue.size() && int(queue.size()) < kMasonStone; ++q)
			for (const auto &step : kCardinalSteps)
			{
				const int j = t.at(t.remainderX(queue[q]) + step[0], queue[q] / t.w + step[1]);
				if (!seen[j] && eligible(j)) { seen[j] = 1; queue.push_back(j); }
			}
		stack = std::min(stack, int(queue.size()));
	}
	int masonStone = 0;
	for (int k = 0; k < teams && stack > 0; ++k)
		masonStone += growPatch(map, t, seeds[k], STONE, stack, yard(k));
	context.telemetry.measure("old-town.stone.mason-stack", stack);
	// The yard also keeps a store of grain and timber: a colony's first harvest within reach whatever
	// the houses round its home leave.
	for (int k = 0; k < teams; ++k)
	{
		const int at = L.masonSeed[k];
		plantPatchNear(map, t, KitSeed{t.remainderX(at), at / t.w, 8}, WHEAT, kYardStore, yard(k));
		plantPatchNear(map, t, KitSeed{t.remainderX(at), at / t.w, 8}, WOOD, kYardStore, yard(k));
	}
	context.telemetry.measure("old-town.stone.mason", masonStone);

	context.stage = "old town colonies";
	const auto homeMask = [&](int team)
	{
		std::vector<unsigned char> home(n, 0);
		for (int i = 0; i < n; ++i)
			home[i] = L.homeOf[i] == team && map.terrainPropertiesAt(t.remainderX(i), i / t.w).buildable &&
				!map.isResource(t.remainderX(i), i / t.w);
		return home;
	};
	const auto anchor = [&](int team)
	{
		return MapGeneratorPoint(int(std::lround(L.homes[team].x)) - 2,
								 int(std::lround(L.homes[team].y)) - 2);
	};
	if (!settleColonies(game, context, "town-starts", homeMask, anchor))
		return false;

	context.stage = "old town resources";
	const std::vector<unsigned char> reserved = swarmSurroundings(t, context);
	for (int k = 0; k < teams; ++k)
		plantHomeKit(
			map, t, context, L.kits[k], L.axes[k], L.homeRadius, kHomeWheat, kHomeWood, [&](int i)
			{ return L.homeOf[i] == k && !reserved[i] && clearGround(map, t.remainderX(i), i / t.w); });
	// The robbed wall's faces and the rubble lots, scaled by the stone amount.
	int wallStone = 0, rubbleStone = 0;
	for (const ShapePoint &p : L.wallFaces)
		wallStone += plantQuarryFaces(map, t, {KitSeed{int(p.x), int(p.y), 6}},
			int(scaledCount(kWallFaceStone, o.stone)),
			[&](int i) { return L.homeOf[i] < 0 && !L.farmRegion[i] && clearGround(map, t.remainderX(i), i / t.w); });
	{
		const std::vector<int> heaps = periodicNoise(t.w, t.h, 4, context.stream("town-rubble"));
		const int threshold = 65535 - std::min(65535, o.stone * 260);
		for (int i = 0; i < n; ++i)
			if (L.rubble[i] && heaps[i] > threshold && clearGround(map, t.remainderX(i), i / t.w))
			{
				map.setResourceByIndex(t.remainderX(i), i / t.w, STONE, 1);
				++rubbleStone;
			}
	}
	context.telemetry.measure("old-town.stone.wall", wallStone);
	context.telemetry.measure("old-town.stone.rubble", rubbleStone);
	// The cathedral square's orchard round its fountain, and a grove of one fruit in turn beside every
	// market's pool (FEEDBACK 2026-09-13: "more of them need to contain fruits").
	const auto roundPlaza = [&](int cell)
	{
		const ShapePoint c = tilePoint(L.g.cells[cell].centre);
		return [&, c](int i)
		{
			return !L.block[i] && L.homeOf[i] < 0 &&
				   std::hypot(t.offsetX(int(c.x), t.remainderX(i)), t.offsetY(int(c.y), i / t.w)) <=
					   o.blockSize / 2.0 + 2 &&
				   clearGround(map, t.remainderX(i), i / t.w);
		};
	};
	if (scaledCount(1, o.fruit) > 0)
	{
		const ShapePoint c = tilePoint(L.g.cells[L.cathedral].centre);
		const double spin = context.bounded("town-fruit", 3600) / 3600.0 * 2 * kPi;
		plantOrchard(map, t, context, c.x, c.y, L.fountainRadius + 2.5, {spin}, 4.5, 5, 1,
					 roundPlaza(L.cathedral));
		int fruit = 0;
		for (int cell : L.plazaCell)
		{
			const ShapePoint c = tilePoint(L.g.cells[cell].centre);
			const double at = context.bounded("town-fruit", 3600) / 3600.0 * 2 * kPi;
			plantRound(map, t, context, c.x, c.y, L.fountainRadius + 2.5, {at},
					   CHERRY + fruit++ % 3, 1, 5, roundPlaza(cell));
		}
	}
	// The fields outside: half the fertile row ground under wheat and a twentieth under wood, in
	// patches, the farm plots kept clear; inside the town the plazas, gardens and fountain gardens
	// get a light share.
	const Fertility::Field fertility = Fertility::forMap(map, false);
	const std::vector<int> patch = periodicNoise(t.w, t.h, 8, context.stream("town-patch"));
	const std::vector<int> split = periodicNoise(t.w, t.h, 4, context.stream("town-split"));
	const auto fields = [&](int i)
	{
		return L.farmRegion[i] && !L.farm.sand[i] && !L.farm.plot[i] &&
			   clearGround(map, t.remainderX(i), i / t.w);
	};
	int fertile = 0;
	for (int i = 0; i < n; ++i)
		fertile += fields(i) && fertility.at(t.remainderX(i), i / t.w) > 0;
	furnishGround(
		map, t, context, fertility, fields, [&](int i) { return float(patch[i]); },
		[&](int i) { return split[i]; },
		[&](int area)
		{
			return GroundAmounts{int(scaledCount(fertile * 50 / 100, o.wheat)),
								 int(scaledCount(fertile * 5 / 100, o.wood)), 0,
								 int(scaledCount(area / 3000, o.fruit))};
		},
		"town-stone", "town-fruit");
	const auto plazas = [&](int i)
	{
		return L.city[i] && !L.house[i] && !L.street[i] && !L.paved[i] && L.homeOf[i] < 0 &&
			   !L.rubble[i] && clearGround(map, t.remainderX(i), i / t.w);
	};
	int plazaFertile = 0;
	for (int i = 0; i < n; ++i)
		plazaFertile += plazas(i) && fertility.at(t.remainderX(i), i / t.w) > 0;
	furnishGround(
		map, t, context, fertility, plazas, [&](int i) { return float(patch[i]); },
		[&](int i) { return split[i]; },
		[&](int)
		{
			return GroundAmounts{int(scaledCount(plazaFertile * 15 / 100, o.wheat)),
								 int(scaledCount(plazaFertile * 10 / 100, o.wood)), 0, 0};
		},
		"town-stone", "town-fruit");
	seedAlgae(map, context, t, "town-algae", o.algae, AlgaeBand::anyWater(50));
	std::vector<unsigned char> stone(n, 0);
	for (int i = 0; i < n; ++i)
		stone[i] = map.getResource(t.remainderX(i), i / t.w).type == STONE;
	secureStartingCrops(game, context, t, 24, 32, 0, &stone);
	reopenCrampedStarts(game, context, {o.wheat, o.wood, o.stone, o.algae, o.fruit}, 24, 32, 0, &stone);
	clearFarmPlots(map, t, {L.farm});
	context.stage = "old town routes";
	openColonyRoutes(map, context, t, StepCosts{1, 3, -1, -1, -1});
	return true;
}

std::string validateWorld(const GenerationWorld &game, const GenerationContext &context)
{
	GenerationContext replay(context.request);
	const Layout L = design(context.request, replay);
	const GenerationMap &map = game.map;
	if (const std::string mismatch = designMismatch(L, map, "old town"); !mismatch.empty())
		return mismatch;
	const Torus &t = L.t;
	for (int i = 0; i < t.size(); ++i)
		if (map.vertexTerrainAt(t.remainderX(i), i / t.w) != L.finished[i])
			return "The town's terrain differs from its design at (" + std::to_string(t.remainderX(i)) +
				   ", " + std::to_string(i / t.w) + ").";
	if (wallTerrainMismatch(map, t, L.rock, TerrainGroup::Ridges) >= 0)
		return "A house or the town wall is not Ridges.";
	// Every home keeps the same stone in its mason's yard (12 where the town has room for a yard of
	// its own; a cramped town's yards share the home's green and hold what the smallest fits).
	std::vector<int> stone(context.request.nbTeams, 0);
	for (int i = 0; i < t.size(); ++i)
		if (L.homeOf[i] >= 0 && map.getResource(t.remainderX(i), i / t.w).type == STONE)
			++stone[L.homeOf[i]];
	for (int k = 0; k < int(stone.size()); ++k)
		if (stone[k] != stone[0] || stone[k] < 1)
			return "Colony " + std::to_string(k) + "'s mason's yard has lost its stone (" + std::to_string(stone[k]) + " against " + std::to_string(stone[0]) + ").";
	if (const std::string lost =
			homePondMissing(map, t, L.kits, context.request.nbTeams, "plaza", "fountain");
		!lost.empty())
		return lost;
	if (const std::string access = startingAccessFailure(map, context.request.nbTeams,
			{{MaterialId::Food, 24, "food"}, {MaterialId::Wood, 32, "wood"}, {MaterialId::Stone, 24, "stone"}});
		!access.empty())
		return access;
	return walkFromFirstColony(map, context.request.nbTeams, "the town", "through the streets").error;
}
} // namespace

OldTownOptions::OldTownOptions(const GenerationRequest &r)
	: citySize(r.option("city-size")), blockSize(r.option("block-size")),
	  streetWidth(r.option("street-width")), warp(r.option("warp")), plazas(r.option("plazas")),
	  farmPlots(r.option("farm-plots")), tendrils(r.option("tendrils") != 0),
	  waterCrossings(r.option("water-crossings") != 0), cropCrossings(r.option("crop-crossings") != 0),
	  wheat(r.option("wheat-amount")), wood(r.option("wood-amount")),
	  stone(r.option("stone-amount")), algae(r.option("algae-amount")),
	  fruit(r.option("fruit-amount"))
{
}

GeneratorDefinition oldTownDefinition()
{
	return {
		"old-town",
		31,
		"Old town",
		10, // 10: Old town 2.0, urban blocks, paved streets, placed stone, ruined wall, climates
		false,
		{GeneratorControl{"city-size", "City size", 40, 90, 5, 70, ControlGroup::Layout}
			 .withSearchRange(55, 80),
		 GeneratorControl{"block-size", "Block size", 10, 20, 1, 14, ControlGroup::Layout}
			 .withSearchRange(12, 18),
		 GeneratorControl{"street-width", "Street width", 3, 7, 1, 4, ControlGroup::Terrain}
			 .withSearchRange(4, 6),
		 GeneratorControl{"warp", "Warp", 0, 100, 10, 50, ControlGroup::Terrain}.withSearchRange(
			 20, 80),
		 GeneratorControl{"plazas", "Plazas", 0, 4, 1, 2, ControlGroup::Layout}.withSearchRange(1,
																								3),
		 GeneratorControl{"farm-plots", "Farm plots", 0, 6, 1, 3, ControlGroup::Layout}
			 .withSearchRange(3, 6),
		 waterCrossingsControl(), cropCrossingsControl(),
		 GeneratorControl::toggle("tendrils", "Tendril roads", true, ControlGroup::Terrain)
			 .withSearchValues({0, 1}),
		 GeneratorControl::percentage("wheat-amount", "Wheat amount"),
		 GeneratorControl::percentage("wood-amount", "Wood amount"),
		 GeneratorControl::percentage("stone-amount", "Stone amount"),
		 GeneratorControl::percentage("algae-amount", "Algae amount"),
		 GeneratorControl::percentage("fruit-amount", "Fruit amount")},
		generate,
		true,
		designFailure<design>,
		validateWorld,
		{"terrain:urban", "feature:stone-walls", "feature:lakes", "feature:farmland", "style:tight-building"}};
}
