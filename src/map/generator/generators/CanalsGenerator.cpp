// SPDX-License-Identifier: GPL-3.0-or-later
#include "CanalsGenerator.h"
#include "Channels.h"
#include "Farmland.h"
#include "FertilityField.h"
#include "Game.h"
#include "GenerationContext.h"
#include "Geometry.h"
#include "GraphMaze.h"
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
#include "Towers.h"
#include <algorithm>
#include <cmath>
#include <string>
#include <vector>
using namespace MapGeneration;

// Canals: a lagoon city. The map is cut into blocks by a grid of narrow canals, warped so the blocks
// are irregular, and every canal is just wide enough to stop a unit and just narrow enough for a
// tower on one bank to shoot the other. Every colony starts on a block of its own with a pond and a
// kit, and a handful of sand bridges join the blocks: only the bridges a tree needs to connect the
// colonies' blocks, plus a few more at random, so most blocks are islands until someone can swim.
// Towers reach across the canals from the first minute and armies cannot, so where the first towers
// go is the opening; once swimming pools are built every canal is a road and the map turns inside
// out.
//
// The city has no centre: the colonies' blocks are the ones farthest apart on the block graph
// (spreadPockets), market blocks with orchards lie farthest from those, and fairness is statistical
// (the lobby keeps the best-scoring of several seeds).
//
// WHY IT PLAYS WELL (docs/map-generators/GAME_RULES_FOR_MAP_DESIGN.md). Water blocks walking until
// a colony can swim, so the canals are a timing rule; a tower scans square rings with no line of
// sight, and a straight canal w corners wide puts the banks' grass w + 4 tiles apart (Channels.h),
// so the default canal of 3 is reached by a level-2 tower (range 7) and not by a level-1 (range 5).
// Every bank has sand beside water within reach of every tile, so algae grows everywhere and crops
// regrow everywhere; the fight is over bridges and banks, not fields.
//
// FEEDBACK 2026-09-13 (first play): "remove the pond from the user's starting base. increase the
// default rate of bridges across the squares 1.5x, and the default amount of warping 2x. it's also a
// little boring right now: can every grid cell have one of N types? I see some of them occasionally
// have a lake in the center; let's have a few different types - the fruit type, the lake type. I
// want a couple of types that have a sand patch protecting a 4x4 spot on the square, with no
// resources, which can be used as a building spot. maybe a few of them have a sand patch. come up
// with a few more cool ideas; each cell has its own little surprise." So: home blocks have no pond
// (their canal waters them), extra bridges default to 30% (was 20) and warp to 80 (was 40), and
// every other block is dealt one of nine kinds (BlockKind) from a weighted draw: plain fields, a
// lake, an orchard round a pond, a 4x4 building pad in a ring of sand, two such pads, a quarry, a
// woodlot, a wheatfield, or a dune of bare sand.
namespace
{

// Every home's starting kit, unscaled whatever the amounts say.
constexpr int kHomeWheat = 14, kHomeWood = 12, kHomeQuarry = 2;
// A bridge is a line of sand corners kBridgeHalfWidth wide across the canal (a 1-corner line already
// carries units two tiles wide, since a tile with a sand corner is no longer pure water), reaching
// this far past the canal's edge onto each bank so it lands on grass, not beach.
constexpr double kBridgeLanding = 2.5;
// What a block holds (first play: "each cell has its own little surprise"), and the weight each kind
// is dealt with, out of 100. Plain blocks carry the ambient fields; the rest carry one thing each.
enum BlockKind
{
	Home = -1,
	Plain = 0,
	Lake,       // a pond at the middle, the fields round it
	Orchard,    // a small pond with the three fruits round it
	Pad,        // a 4x4 pad of grass in a ring of sand: a building spot nothing grows onto
	TwoPads,    // two such pads
	Quarry,     // a stone clump of radius 3
	Woodlot,    // wood over most of the block
	Wheatfield, // wheat over most of the block
	Dune        // a disc of bare sand: walkable, unbuildable, and nothing grows on it
};
constexpr int kKinds = 9;
constexpr int kKindWeight[kKinds] = {24, 12, 9, 15, 8, 8, 8, 8, 8};
// The kinds' features: the lake's and the orchard pond's radii, the pad (4 tiles square in a ring
// one corner wide; stampFarmPlot), how far apart two pads stand, the quarry's radius, the dune's
// radius, and the share of a woodlot or wheatfield under its crop.
constexpr double kLakeRadius = 3.5, kOrchardPond = 2.5, kDuneRadius = 4.0;
constexpr int kPadSize = 4, kPadRing = 1, kPadsApart = 10, kQuarryRadius = 3, kCoverPercent = 60;
// Tower pads beside each colony's starting towers, and the spacing between a colony's sites.
constexpr int kTowerPads = 2, kTowerSpacing = 6;

struct Layout
{
	Torus t{1, 1};
	Tessellation g;
	std::vector<int> cell; // every tile's block
	std::vector<int> homeCell;
	std::vector<int> kind;   // every block's BlockKind
	std::vector<int> homeOf; // the home block a tile is in, or -1
	std::vector<unsigned char> canal, bridge, water, sand, land;
	Farm pads; // the building pads, stamped as farm plots
	TerrainSketch sketch;
	std::vector<ShapePoint> homes, kits;
	double homeRadius = 0;
	std::string failure;
};

// A subtile point in tile units.
ShapePoint tilePoint(SubtilePoint p)
{
	return {p.x / 16.0, p.y / 16.0};
}

Layout design(const GenerationRequest &request, GenerationContext &context)
{
	const CanalsOptions o(request);
	Layout L;
	L.t = {1 << request.wDec, 1 << request.hDec};
	const Torus &t = L.t;
	const int n = t.size(), teams = std::max(1, request.nbTeams);

	// The blocks: a square tiling, warped. Every edge is a canal, so every edge is an obstacle the
	// warp keeps apart: opposite sides of a block stay at least the canal plus eight tiles apart, so
	// a block always keeps a few tiles of grass across its narrowest, and a block's centre keeps at
	// least a fifth of the block from its sides, so a pond fits.
	L.g = squareTessellation(t.w, t.h, o.blockSize);
	if (L.g.columns < 2 || L.g.rows < 2)
	{
		L.failure = "The canals need at least two blocks across and down; use a bigger map or "
					"smaller blocks.";
		return L;
	}
	const std::vector<unsigned char> walls(L.g.edges.size(), 1);
	warpCorners(L.g, warpLimit(L.g) * o.warp / 100, walls, o.canalWidth + 8, o.blockSize / 5,
				context, "canals-warp");
	L.cell = labelTiles(L.g);
	if (L.cell.empty())
	{
		L.failure = "The blocks could not be labelled.";
		return L;
	}

	// The home blocks: as far apart on the block graph as the tiling allows; then the market blocks,
	// each the farthest block from every home and market so far.
	const CellGraph graph = cellGraph(L.g);
	L.homeCell = spreadPockets(graph, teams);
	if (int(L.homeCell.size()) != teams)
	{
		L.failure = "Too many colonies for this many blocks; use a bigger map, smaller blocks or "
					"fewer colonies.";
		return L;
	}
	// spreadPockets always starts from block 0, so the deal decides which colony gets which block.
	dealStarts(context, L.homeCell);
	// Every other block's kind, dealt from a weighted draw in block order (first play: "each cell
	// has its own little surprise").
	L.kind.assign(L.g.cellCount(), Plain);
	for (int cell : L.homeCell)
		L.kind[cell] = Home;
	for (int cell = 0; cell < L.g.cellCount(); ++cell)
	{
		if (L.kind[cell] == Home)
			continue;
		int roll = int(context.bounded("canals-kinds", 100)), kind = 0;
		while (kind + 1 < kKinds && roll >= kKindWeight[kind])
			roll -= kKindWeight[kind++];
		L.kind[cell] = kind;
	}

	// The canals: every edge stroked canalWidth corners wide. A canal's water is canalWidth - 1 tiles
	// across, and a diagonal canal is only sealed against a diagonal step when its water is two tiles
	// thick, which is why the narrowest canal offered is 3.
	L.canal.assign(n, 0);
	for (int edge = 0; edge < int(L.g.edges.size()); ++edge)
	{
		const auto ends = L.g.edgeEnds(edge, L.g.edges[edge].cells[0]);
		const ShapePoint a = tilePoint(ends.first), b = tilePoint(ends.second);
		strokePath(L.canal, t, {{a.x, a.y, o.canalWidth / 2.0}, {b.x, b.y, o.canalWidth / 2.0}});
	}

	// The bridges: a tree of the shortest block-to-block paths from the first colony's block to every
	// other colony's, so every colony can be walked to, and then `extraBridges` percent of the blocks'
	// count in further bridges at random (openLoops), which is where the flanking routes come from.
	std::vector<unsigned char> open(L.g.edges.size(), 0);
	if (teams > 1)
	{
		std::vector<int> parentEdge(L.g.cellCount(), -1), order{L.homeCell[0]};
		std::vector<unsigned char> seen(L.g.cellCount(), 0);
		seen[L.homeCell[0]] = 1;
		for (size_t head = 0; head < order.size(); ++head)
			for (int edge : L.g.cells[order[head]].edges)
			{
				const int next = L.g.other(edge, order[head]);
				if (!seen[next])
				{
					seen[next] = 1;
					parentEdge[next] = edge;
					order.push_back(next);
				}
			}
		for (int k = 1; k < teams; ++k)
			for (int cell = L.homeCell[k]; parentEdge[cell] >= 0;)
			{
				open[parentEdge[cell]] = 1;
				cell = L.g.other(parentEdge[cell], cell);
			}
	}
	openLoops(graph, context, "canals-bridges", std::vector<unsigned char>(L.g.cellCount(), 0),
			  o.extraBridges, open);
	L.bridge.assign(n, 0);
	for (int edge = 0; edge < int(L.g.edges.size()); ++edge)
	{
		if (!open[edge])
			continue;
		const int owner = L.g.edges[edge].cells[0];
		const auto ends = L.g.edgeEnds(edge, owner);
		const ShapePoint a = tilePoint(ends.first), b = tilePoint(ends.second);
		const ShapePoint middle{(a.x + b.x) / 2, (a.y + b.y) / 2};
		// Across the canal: from the owner's centre to the neighbour's, through the edge's middle.
		const ShapePoint from = tilePoint(L.g.cells[owner].centre),
						 to = tilePoint(L.g.centreAcross(edge, owner));
		const double dx = to.x - from.x, dy = to.y - from.y, length = std::hypot(dx, dy);
		const double reach = o.canalWidth / 2.0 + kBridgeLanding;
		strokePath(
			L.bridge, t,
			{{middle.x - dx / length * reach, middle.y - dy / length * reach, kBridgeHalfWidth},
			 {middle.x + dx / length * reach, middle.y + dy / length * reach, kBridgeHalfWidth}});
	}

	// Homes: the kit round the middle of every home block, with no pond (first play; the block's canal
	// is within the growth probe's reach of all of it). The block's "radius" for the kit is what is
	// left between its centre and its canal.
	L.homeRadius = std::max(6.0, o.blockSize / 2.0 - o.canalWidth / 2.0 - 3);
	L.water = L.canal;
	L.sand.assign(n, 0);
	L.homeOf.assign(n, -1);
	L.land.assign(n, 0);
	for (int i = 0; i < n; ++i)
		L.land[i] = !L.canal[i];
	for (int k = 0; k < teams; ++k)
	{
		L.homes.push_back(tilePoint(L.g.cells[L.homeCell[k]].centre));
		L.kits.push_back(L.homes[k]);
		for (int i = 0; i < n; ++i)
			if (L.cell[i] == L.homeCell[k] && L.land[i])
				L.homeOf[i] = k;
	}
	// Every block's terrain feature, at its middle: a lake, an orchard's pond, a pad or two of grass
	// in a ring of sand (stamped as farm plots into the sketch), or a dune.
	const RadialShape lake(kLakeRadius, 0.3, context, "canals-lakes");
	const RadialShape orchardPond(kOrchardPond, 0.3, context, "canals-lakes");
	const RadialShape dune(kDuneRadius, 0.3, context, "canals-dunes");
	const FarmPlot pad{kPadSize, kPadSize, kPadRing};
	for (int cell = 0; cell < L.g.cellCount(); ++cell)
	{
		const ShapePoint c = tilePoint(L.g.cells[cell].centre);
		if (L.kind[cell] == Lake)
			fillShape(L.water, t, c.x, c.y, lake, 0.0);
		else if (L.kind[cell] == Orchard)
			fillShape(L.water, t, c.x, c.y, orchardPond, 0.0);
		else if (L.kind[cell] == Dune)
			fillShape(L.sand, t, c.x, c.y, dune, 0.0);
	}
	L.sketch.assign(n, GRASS);
	for (int i = 0; i < n; ++i)
		L.sketch[i] = L.water[i]                                   ? WATER
					  : (L.sand[i] || (L.bridge[i] && L.canal[i])) ? SAND
																   : GRASS;
	L.pads.water = L.water;
	L.pads.sand.assign(n, 0);
	L.pads.plot.assign(n, 0);
	L.pads.row.assign(n, -1);
	for (int cell = 0; cell < L.g.cellCount(); ++cell)
	{
		const ShapePoint c = tilePoint(L.g.cells[cell].centre);
		const int cx = int(std::lround(c.x)), cy = int(std::lround(c.y));
		if (L.kind[cell] == Pad)
			stampFarmPlot(L.sketch, t, L.pads, cx - kPadSize / 2, cy - kPadSize / 2, pad);
		else if (L.kind[cell] == TwoPads)
		{
			stampFarmPlot(L.sketch, t, L.pads, cx - kPadsApart / 2 - kPadSize / 2,
						  cy - kPadSize / 2, pad);
			stampFarmPlot(L.sketch, t, L.pads, cx + kPadsApart / 2 - kPadSize / 2,
						  cy - kPadSize / 2, pad);
		}
	}
	L.water = L.pads.water;
	return L;
}

// The towers every colony starts with: on its own block's bank (against the beach), covering the most
// of other blocks' land across the canal, none within range of another colony's swarm; open pads
// beside them for more.
TowerPlan planTowers(const Map &map, const Layout &L, const GenerationContext &context,
					 const CanalsOptions &o, const std::vector<unsigned char> &bank)
{
	const Torus &t = L.t;
	const int n = t.size(), teams = int(L.homes.size());
	const std::vector<unsigned char> reserved = swarmSurroundings(t, context);
	std::vector<int> owner(n, -1);
	std::vector<unsigned char> buildable(n, 0), target(n, 0);
	for (int i = 0; i < n; ++i)
	{
		const int x = i % t.w, y = i / t.w;
		if (map.isWater(x, y))
			continue;
		// Land on other blocks belongs to "everyone else": worth shooting at from any colony's bank.
		owner[i] = L.homeOf[i] >= 0 ? L.homeOf[i] : teams;
		target[i] = 1;
		buildable[i] =
			L.homeOf[i] >= 0 && map.isGrass(x, y) && !reserved[i] && !map.isResource(x, y);
	}
	TowerRequest request = startingTowerRequest(o.towers, o.towerCount, kTowerPads, kTowerSpacing);
	request.otherWeight = 1;
	request.ownWeight = 0;
	request.against = &bank;
	return chooseTowerSites(t, owner, buildable, target, swarmSurroundings(t, context, 0), teams,
							request);
}

bool generate(Game &game, GenerationContext &context)
{
	context.stage = "canals layout";
	const CanalsOptions o(context.request);
	const Layout L = design(context.request, context);
	if (!L.failure.empty())
	{
		context.detail = L.failure;
		return false;
	}
	Map &map = game.map;
	const Torus &t = L.t;
	const int n = t.size(), teams = context.request.nbTeams;
	for (int k = 0; k < teams; ++k)
		game.addTeam();

	context.stage = "canals terrain";
	TerrainSketch terrain = L.sketch;
	layBeaches(terrain, t);
	for (int i = 0; i < n; ++i)
		if (L.bridge[i] && L.canal[i])
			terrain[i] = SAND;
	writeUndermap(map, terrain);

	context.stage = "canals colonies";
	const auto homeMask = [&](int team)
	{
		std::vector<unsigned char> ground(size_t(n), 0);
		for (int i = 0; i < n; ++i)
			ground[i] = L.homeOf[i] == team && map.isGrass(i % t.w, i / t.w);
		return ground;
	};
	const auto anchor = [&](int team) { return homeSwarmSite(L.homes[team], 0.0, L.homeRadius); };
	if (!settleColonies(game, context, "canals-starts", homeMask, anchor))
		return false;

	// Towers on the banks. No tower or pad may close a colony's walk to a bridge, and every colony
	// keeps as many as the fewest got.
	context.stage = "canals towers";
	const std::vector<unsigned char> bank = beachTiles(map, t);
	TowerPlan towers = planTowers(map, L, context, o, bank);
	std::vector<unsigned char> bridges(n, 0);
	bool anyBridge = false;
	for (int i = 0; i < n; ++i)
		if (L.bridge[i] && L.canal[i] && !map.isWater(i % t.w, i / t.w))
			bridges[i] = anyBridge = true;
	if (!settleStartingTowers(game, context, towers, o.towers, o.towers > 0 && o.towerCount > 0,
							  anyBridge ? &bridges : nullptr))
		return false;
	const std::vector<unsigned char> pads = towerFootprints(t, towers);

	context.stage = "canals resources";
	const std::vector<unsigned char> reserved = swarmSurroundings(t, context);
	for (int k = 0; k < teams; ++k)
		plantOpenHomeKit(map, t, context, L.kits[k], 0.0, L.homeRadius, kHomeWheat, kHomeWood,
						 kHomeQuarry,
						 [&](int i)
						 {
							 return L.homeOf[i] == k && !reserved[i] && !pads[i] &&
									clearGround(map, i % t.w, i / t.w);
						 });
	// Every block is fertile (its canal is within the growth probe's reach of all of it), so the
	// ambient farmland is a modest share of the plain, lake and home blocks, in patches; the other
	// kinds carry their own thing instead, and the bridges' landings stay clear so no field closes
	// a bridge.
	const std::vector<unsigned char> landings = dilate(t, bridges, 2);
	const Fertility::Field fertility = Fertility::forMap(map, false);
	const std::vector<int> patch = periodicNoise(t.w, t.h, 8, context.stream("canals-patch"));
	const std::vector<int> split = periodicNoise(t.w, t.h, 5, context.stream("canals-split"));
	const auto open = [&](int i)
	{
		return L.land[i] && !reserved[i] && !pads[i] && !landings[i] && !L.pads.plot[i] &&
			   clearGround(map, i % t.w, i / t.w);
	};
	const auto eligible = [&](int i)
	{
		const int kind = L.kind[L.cell[i]];
		return open(i) && (kind == Plain || kind == Lake || kind == Home);
	};
	int fertile = 0;
	for (int i = 0; i < n; ++i)
		fertile += eligible(i) && fertility.at(i % t.w, i / t.w) > 0;
	furnishGround(
		map, t, context, fertility, eligible, [&](int i) { return float(patch[i]); },
		[&](int i) { return split[i]; },
		[&](int area)
		{
			return GroundAmounts{int(scaledCount(fertile * 12 / 100, o.wheat)),
								 int(scaledCount(fertile * 8 / 100, o.wood)),
								 int(scaledCount(area / 900, o.stone)), 0};
		},
		"canals-stone", "canals-fruit");
	// Every block's own thing (first play): the three fruits round an orchard's pond, a quarry's
	// stone, a woodlot's or wheatfield's cover on the block's most-noise share of ground.
	const std::vector<int> cover = periodicNoise(t.w, t.h, 6, context.stream("canals-cover"));
	for (int cell = 0; cell < L.g.cellCount(); ++cell)
	{
		const ShapePoint centre = tilePoint(L.g.cells[cell].centre);
		const auto inBlock = [&](int i) { return L.cell[i] == cell && open(i); };
		switch (L.kind[cell])
		{
		case Orchard:
			if (scaledCount(1, o.fruit) > 0)
			{
				const double spin = context.bounded("canals-fruit", 3600) / 3600.0 * 2 * kPi;
				plantOrchard(map, t, context, centre.x, centre.y, kOrchardPond + 2.5, {spin}, 4.5,
							 6, 1, inBlock);
			}
			break;
		case Quarry:
			if (scaledCount(1, o.stone) > 0)
				if (const int seed = seedNear(t, int(centre.x), int(centre.y), 3, inBlock);
					seed >= 0)
					placeResourceClump(map, context, MapGeneratorPoint(seed % t.w, seed / t.w),
									   STONE, kQuarryRadius);
			break;
		case Woodlot:
		case Wheatfield:
		{
			std::vector<int> levels;
			for (int i = 0; i < n; ++i)
				if (inBlock(i))
					levels.push_back(cover[i]);
			if (levels.empty())
				break;
			const int share = std::clamp(
				int(scaledCount(kCoverPercent, L.kind[cell] == Woodlot ? o.wood : o.wheat)), 0,
				100);
			const int level = percentile(levels, 100 - share);
			plantCover(map, t, L.land, L.kind[cell] == Woodlot ? WOOD : CORN,
					   [&](int i) { return inBlock(i) && cover[i] >= level; });
			break;
		}
		default:
			break;
		}
	}
	seedAlgae(map, context, t, "canals-algae", o.algae, AlgaeBand::anyWater(25));
	clearFarmPlots(map, t, {L.pads});
	secureStartingCrops(game, context, t);
	reopenCrampedStarts(game, context, {o.wheat, o.wood, o.stone, o.algae, o.fruit});
	// The bridges join every colony to the first; only deposits could close the way, so only those
	// are cleared, never a canal.
	context.stage = "canals routes";
	openColonyRoutes(map, context, t, StepCosts{1, 3, 8, -1, -1});
	return true;
}

std::string validateWorld(const Game &game, const GenerationContext &context)
{
	GenerationContext replay(context.request);
	const Layout L = design(context.request, replay);
	const Map &map = game.map;
	if (const std::string mismatch = designMismatch(L, map, "canals"); !mismatch.empty())
		return mismatch;
	const Torus &t = L.t;
	// Home blocks are dry by design (first play); every lake block keeps its lake.
	for (int cell = 0; cell < L.g.cellCount(); ++cell)
		if (L.kind[cell] == Lake)
		{
			const ShapePoint c = tilePoint(L.g.cells[cell].centre);
			if (!map.isWater(t.x(int(c.x)), t.y(int(c.y))))
				return "A lake block has lost its lake at (" + std::to_string(int(c.x)) + ", " +
					   std::to_string(int(c.y)) + ").";
		}
	return walkFromFirstColony(map, context.request.nbTeams, "the city", "over the bridges").error;
}
} // namespace

CanalsOptions::CanalsOptions(const GenerationRequest &r)
	: blockSize(r.option("block-size")), canalWidth(r.option("canal-width")),
	  warp(r.option("warp")), extraBridges(r.option("extra-bridges")),
	  towers(r.option("starting-towers")), towerCount(r.option("tower-count")),
	  wheat(r.option("wheat-amount")), wood(r.option("wood-amount")),
	  stone(r.option("stone-amount")), algae(r.option("algae-amount")),
	  fruit(r.option("fruit-amount"))
{
}

GeneratorDefinition canalsDefinition()
{
	return {
		"canals",
		29,
		"Canals",
		3,
		false,
		// Blocks of 24 give a 256 map about a hundred blocks; a canal of 3 corners (two tiles of
		// water) is sealed against diagonal steps and is reached by a level-2 tower, which is what
		// the colonies start with; a fifth again in extra bridges keeps most blocks islands.
		{{"block-size", "Block size", 16, 40, 2, 24, ControlGroup::Layout},
		 {"canal-width", "Canal width", 3, 5, 1, 3, ControlGroup::Terrain},
		 // FEEDBACK 2026-09-13: warp 80 and extra bridges 30 (were 40 and 20).
		 {"warp", "Warp", 0, 100, 10, 80, ControlGroup::Terrain},
		 {"extra-bridges", "Extra bridges", 0, 100, 10, 30, ControlGroup::Layout},
		 {"starting-towers", "Starting tower level", 0, 3, 1, 2, ControlGroup::Layout},
		 {"tower-count", "Towers per colony", 1, 4, 1, 2, ControlGroup::Layout},
		 GeneratorControl::percentage("wheat-amount", "Wheat amount"),
		 GeneratorControl::percentage("wood-amount", "Wood amount"),
		 GeneratorControl::percentage("stone-amount", "Stone amount"),
		 GeneratorControl::percentage("algae-amount", "Algae amount"),
		 GeneratorControl::percentage("fruit-amount", "Fruit amount")},
		generate,
		true,
		[](const GenerationRequest &r) -> std::string
		{
			GenerationContext probe(r);
			return design(r, probe).failure;
		},
		validateWorld};
}
