// SPDX-License-Identifier: GPL-3.0-or-later
#include "AllotmentsGenerator.h"
#include "Bases.h"
#include "Contact.h"
#include "Farmland.h"
#include "Game.h"
#include "GenerationContext.h"
#include "Geometry.h"
#include "Grid.h"
#include "LatticeNoise.h"
#include "Lots.h"
#include "Morphology.h"
#include "Orbits.h"
#include "Pipeline.h"
#include "Planting.h"
#include "Resources.h"
#include "Roads.h"
#include "Sketch.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>
using namespace MapGeneration;

// Allotments: every colony starts with a whole city staked out but not built. The swarm and one
// inn stand finished and stocked; every other building of the base - inns, hospital, school,
// barracks, racetrack, two towers - is a construction site with wood stacked beside it, and thirty-
// odd colonists are waiting to be told what to finish first. Beyond the city the whole map is
// already parcelled: a lattice of one-tile sand lanes cuts the ground into blocks, and every block
// holds one lot, a pad of grass in a ring of sand that exactly one building fits on. Down every
// third lane runs a ditch of water, so the lots along it are fields whose crops regrow; alternate
// lots away from the water are woodlots; the smallest lots carry a quarry or a grove; the rest are
// open, waiting to be built on. Expansion is discrete and readable - take a lot, fill it - and two
// colonies meet where their built-out suburbs do.
//
// The cities stand on a lattice (Orbits.h), each snapped to the middle of a superblock of lanes, so
// its lanes are suppressed and its bounding lanes are its edge; the lanes wrap the torus exactly
// (Lots.h) so a rectangular map is served as well as a square one. Fairness is statistical, like
// Polder's: every city sees the same lanes and lots, and which ditch it stands nearest is where it
// fell, which the validator bounds.
//
// WHY IT PLAYS WELL (docs/map-generators/GAME_RULES_FOR_MAP_DESIGN.md). A sand lane can never grow
// shut and never be built on, so the ways between lots stay open whatever anyone builds; a pad is
// the only pure grass in its block, so every building placement is one clear choice and the AIs
// see the same sites the players do; sand beside a crop slows its regrowth, so the fields along a
// ditch are the granary and everything else is a one-off; and a base of sites rather than buildings
// turns the opening into a sequencing puzzle - which site gets the workers first - that the player
// solves with the plan already on the ground.
namespace
{
// Pad sizes: a lot holds a building, so a large lot is a 6x6 pad (a 4x4 barracks with a tile round
// it, or four 2x2s), a medium one 4x4 (one swarm-sized building exactly) and a small one 2x2 (an
// inn, a hospital, a school or a tower). The Mixed setting deals them in a fixed cycle over the
// blocks so every neighbourhood has all three.
constexpr int kLargePad = 6, kMediumPad = 4, kSmallPad = 2;
// A ditch is two water vertices down its lane: one pure tile of water between two beaches, so it
// blocks walking (a single vertex would only be a puddle every unit walks through) while spoiling
// only five tiles across. Its lane's crossings stay sand, a ford at every lane it meets.
constexpr int kDitchVertices = 2;
// The extra tiles a ditch's beach takes out of the block on each side of it: two on the far side
// of the lane vertex (the second water vertex and its beach), one on the near side.
constexpr int kDitchFar = 2, kDitchNear = 1;
// A ditch keeps this far from a city: its beach would spoil the city's edge for building and the
// city's bounding lane is where the colonists walk out.
constexpr int kDitchClearOfCity = 2;
// The blocks touching a city on each side are its home fields: large pads under wheat whatever
// else the roles say, so the first food is a lane away, each with a well at its middle - a pond of
// 2x2 vertices, one tile of water in its beach - so the wheat round it regrows. The first headless
// play (four AIs, 20000 ticks) had one colony starve to death by tick 8000: its home fields were
// then dry and their wheat, once cut, never came back, and the nearest ditch was two blocks away.
// A well costs a 6x6 pad the 4x4 of tiles its beach spoils and leaves some twenty for crops.
constexpr int kHomeFieldPad = 6, kWellVertices = 2;
// Fields: eight in ten pad tiles wheat, one in ten wood at the default amounts; a woodlot is all
// wood. The small lots not needed for anything else alternate quarry and grove.
constexpr int kFieldWheatPercent = 80, kFieldWoodPercent = 10;
// How far a colony's walk to a ditch may differ from another's before the validator complains:
// `ditch-every` lane pitches, since a city may stand anywhere between two ditch lanes on either
// axis, plus its superblock's width, since it may have to walk round its own city to reach one
// (the sweeps measured 34 tiles at a pitch of 8 with a ditch every third lane and a superblock
// of three).
constexpr int kDitchWalkTolerancePitches = 1;

enum class Lot
{
	None,   // a city's ground, or a block too small for any pad
	Open,   // a pad kept clear to build on
	Field,  // a pad under wheat (touches a ditch, or is a home field)
	Woodlot,
	Quarry,
	Grove
};

struct Layout
{
	Torus t{1, 1};
	std::vector<ShapePoint> homes;
	std::vector<BaseSite> sites;
	BasePlan plan;
	LaneGrid lanes;
	int pitch = 0, superblock = 0;
	std::vector<int> interiorOf;                // each city's ground, bounding lanes excluded
	std::vector<unsigned char> lane, ditch;     // lane vertices; ditch water vertices
	std::vector<Lot> lotOf;                     // per block (column * rows + row)
	std::vector<int> padX, padY, padSize;       // per block; size 0 when it has no pad
	Farm pads;                                  // every pad's grass (plot) and sand
	TerrainSketch sketch;
	std::string failure;
};

int blockIndex(const LaneGrid &g, int column, int row)
{
	return ((column % g.columns() + g.columns()) % g.columns()) * g.rows() +
		   (row % g.rows() + g.rows()) % g.rows();
}

Layout design(const GenerationRequest &request, GenerationContext &context)
{
	const AllotmentsOptions o(request);
	Layout L;
	L.t = {1 << request.wDec, 1 << request.hDec};
	const Torus &t = L.t;
	const int n = t.size(), teams = std::max(1, request.nbTeams);
	L.plan = standardBasePlan(baseTier(o.colonists), BaseKind::Sites, 0, false);

	L.homes = latticeSites(t.w, t.h, teams, context.bounded("allotments-layout", std::uint32_t(t.w)),
						   context.bounded("allotments-layout", std::uint32_t(t.h)))
				  .sites;
	dealStarts(context, L.homes);
	std::vector<int> facings;
	for (size_t k = 0; k < L.homes.size(); ++k)
		facings.push_back(int(context.bounded("allotments-facing", 4)));

	// The lanes, and the superblock a city takes: enough blocks that the pure grass between its
	// bounding lanes (the block widths less the lanes' spoiled tiles) holds the base. Cities must
	// keep a block between them; when they cannot, the lanes are laid closer (a smaller pitch means
	// more, smaller blocks, and a superblock of about the same tiles), and when nothing fits the map
	// is refused.
	const int laneOffsetX = int(context.bounded("allotments-lanes", std::uint32_t(t.w)));
	const int laneOffsetY = int(context.bounded("allotments-lanes", std::uint32_t(t.h)));
	std::vector<std::pair<int, int>> corners; // each city's superblock's first column and row
	for (L.pitch = o.laneSpacing;; L.pitch -= 2)
	{
		L.lanes = layLanes(t, L.pitch, laneOffsetX, laneOffsetY);
		int narrowest = std::min(t.w, t.h);
		for (int c = 0; c < L.lanes.columns(); ++c)
			narrowest = std::min(narrowest, L.lanes.columnSpan(c).count + 1);
		for (int r = 0; r < L.lanes.rows(); ++r)
			narrowest = std::min(narrowest, L.lanes.rowSpan(r).count + 1);
		// m blocks of at least `narrowest` tiles hold m * narrowest - 3 tiles of pure grass (the
		// bounding lanes spoil a tile each side, and one of them is the lane itself).
		L.superblock = 1;
		while (L.superblock * narrowest - 3 < 2 * L.plan.reach + 1)
			++L.superblock;
		corners.clear();
		bool apart = L.superblock + 1 <= L.lanes.columns() && L.superblock + 1 <= L.lanes.rows();
		for (const ShapePoint &home : L.homes)
		{
			const int column = L.lanes.column(int(home.x)) - (L.superblock - 1) / 2;
			const int row = L.lanes.row(int(home.y)) - (L.superblock - 1) / 2;
			corners.push_back({column, row});
		}
		for (size_t a = 0; a < corners.size() && apart; ++a)
			for (size_t b = a + 1; b < corners.size() && apart; ++b)
			{
				// Two superblocks are apart when a whole block lies between them on some axis.
				const auto gap = [&](int u, int v, int count)
				{
					const int d = ((u - v) % count + count) % count;
					return std::min(d, count - d);
				};
				apart = gap(corners[a].first, corners[b].first, L.lanes.columns()) > L.superblock ||
						gap(corners[a].second, corners[b].second, L.lanes.rows()) > L.superblock;
			}
		if (apart)
			break;
		if (L.pitch - 2 < 8)
		{
			L.failure = "Too many colonies for this map; use a bigger map or fewer colonies.";
			return L;
		}
		context.telemetry.fallback("allotments.lanes.narrowed",
								   "Lanes were laid closer so every city keeps a block from the next");
	}
	context.telemetry.measure("allotments.pitch.actual", L.pitch);
	context.telemetry.measure("allotments.superblock.blocks", L.superblock);
	context.telemetry.measure("allotments.lanes.columns", L.lanes.columns());
	context.telemetry.measure("allotments.lanes.rows", L.lanes.rows());

	// Each city's ground: the tiles strictly inside its superblock's bounding lanes, its site at
	// the middle of them. The inner lanes are suppressed.
	L.interiorOf.assign(n, -1);
	L.lane = laneTiles(L.lanes);
	const int columns = L.lanes.columns(), rows = L.lanes.rows();
	std::vector<unsigned char> cityBlock(size_t(columns) * rows, 0);
	for (size_t k = 0; k < corners.size(); ++k)
	{
		const int c0 = corners[k].first, r0 = corners[k].second;
		const int x0 = L.lanes.laneX[(c0 % columns + columns) % columns];
		const int y0 = L.lanes.laneY[(r0 % rows + rows) % rows];
		const int x1 = L.lanes.laneX[((c0 + L.superblock) % columns + columns) % columns];
		const int y1 = L.lanes.laneY[((r0 + L.superblock) % rows + rows) % rows];
		const int width = ((x1 - x0) % t.w + t.w) % t.w, height = ((y1 - y0) % t.h + t.h) % t.h;
		for (int dy = 1; dy < height; ++dy)
			for (int dx = 1; dx < width; ++dx)
			{
				const int i = t.at(x0 + dx, y0 + dy);
				L.interiorOf[i] = int(k);
				L.lane[i] = 0;
			}
		for (int dc = 0; dc < L.superblock; ++dc)
			for (int dr = 0; dr < L.superblock; ++dr)
				cityBlock[blockIndex(L.lanes, c0 + dc, r0 + dr)] = 1;
		L.sites.push_back({t.x(x0 + width / 2), t.y(y0 + height / 2), facings[k]});
	}

	// Ditches down every `ditchEvery`-th lane on each axis, offset by a draw so the ditches do not
	// always start at lane 0, two vertices wide (the lane's and the next), kept clear of cities and
	// of every crossing with another lane, where the sand stays as a ford.
	L.ditch.assign(n, 0);
	const int ditchColumn = int(context.bounded("allotments-ditch", std::uint32_t(o.ditchEvery)));
	const int ditchRow = int(context.bounded("allotments-ditch", std::uint32_t(o.ditchEvery)));
	std::vector<unsigned char> ditchLaneX(columns, 0), ditchLaneY(rows, 0);
	for (int c = 0; c < columns; ++c)
		ditchLaneX[c] = c % o.ditchEvery == ditchColumn;
	for (int r = 0; r < rows; ++r)
		ditchLaneY[r] = r % o.ditchEvery == ditchRow;
	std::vector<unsigned char> city(n, 0);
	for (int i = 0; i < n; ++i)
		city[i] = L.interiorOf[i] >= 0;
	const std::vector<unsigned char> nearCity = dilate(t, city, kDitchClearOfCity);
	const std::vector<unsigned char> crossings = [&]
	{
		std::vector<unsigned char> mask(n, 0);
		for (int x : L.lanes.laneX)
			for (int y : L.lanes.laneY)
				for (int dy = -1; dy <= kDitchVertices; ++dy)
					for (int dx = -1; dx <= kDitchVertices; ++dx)
						mask[t.at(x + dx, y + dy)] = 1;
		return mask;
	}();
	for (int c = 0; c < columns; ++c)
		if (ditchLaneX[c])
			for (int y = 0; y < t.h; ++y)
				for (int d = 0; d < kDitchVertices; ++d)
				{
					const int i = t.at(L.lanes.laneX[c] + d, y);
					if (!nearCity[i] && !crossings[i] && L.lane[t.at(L.lanes.laneX[c], y)])
						L.ditch[i] = 1;
				}
	for (int r = 0; r < rows; ++r)
		if (ditchLaneY[r])
			for (int x = 0; x < t.w; ++x)
				for (int d = 0; d < kDitchVertices; ++d)
				{
					const int i = t.at(x, L.lanes.laneY[r] + d);
					if (!nearCity[i] && !crossings[i] && L.lane[t.at(x, L.lanes.laneY[r])])
						L.ditch[i] = 1;
				}

	// The sketch so far: grass, lanes of sand, ditches of water. Then a pad in every block that is
	// no city's, its role decided by what it touches.
	L.sketch.assign(n, GRASS);
	for (int i = 0; i < n; ++i)
		L.sketch[i] = L.ditch[i] ? WATER : L.lane[i] ? SAND : GRASS;
	L.pads.water = L.ditch;
	L.pads.sand = L.lane;
	L.pads.plot.assign(n, 0);
	L.pads.row.assign(n, -1);
	L.lotOf.assign(size_t(columns) * rows, Lot::None);
	L.padX.assign(L.lotOf.size(), 0);
	L.padY.assign(L.lotOf.size(), 0);
	L.padSize.assign(L.lotOf.size(), 0);
	const std::vector<int> sizesFor[4] = {{}, {kSmallPad}, {kMediumPad, kSmallPad}, {kLargePad, kMediumPad, kSmallPad}};
	int fields = 0, woodlots = 0, quarries = 0, groves = 0, open = 0, smallOpen = 0;
	for (int c = 0; c < columns; ++c)
		for (int r = 0; r < rows; ++r)
		{
			const int b = blockIndex(L.lanes, c, r);
			if (cityBlock[b])
				continue;
			// A block touches a ditch when one of its four bounding lanes is a ditch lane (and
			// that ditch is not one a city silenced: a home field is a field anyway).
			const bool leftDitch = ditchLaneX[c], rightDitch = ditchLaneX[(c + 1) % columns];
			const bool topDitch = ditchLaneY[r], bottomDitch = ditchLaneY[(r + 1) % rows];
			bool homeField = false;
			for (int dc = -1; dc <= 1 && !homeField; ++dc)
				for (int dr = -1; dr <= 1 && !homeField; ++dr)
					homeField = (dc == 0) != (dr == 0) && cityBlock[blockIndex(L.lanes, c + dc, r + dr)];
			// The pad, sized by the setting, centred in the room the lanes and any ditch leave; a
			// home field is as large as its block allows, for its well.
			std::vector<int> sizes;
			if (homeField)
				sizes = sizesFor[3];
			else if (o.lotSize == 0)
				sizes = sizesFor[3 - (c + 2 * r) % 3];
			else
				sizes = sizesFor[o.lotSize];
			// stampLotPad's margin is symmetric; a ditch takes more on its far side, so the pad is
			// held back by the larger of the two on every side that has one.
			const int shrink = (leftDitch || rightDitch || topDitch || bottomDitch)
								   ? std::max(kDitchFar, kDitchNear)
								   : 0;
			L.padSize[b] = stampLotPad(L.sketch, L.lanes, L.pads, c, r, sizes, L.padX[b], L.padY[b], shrink);
			if (L.padSize[b] == 0)
				continue;
			if (homeField || leftDitch || rightDitch || topDitch || bottomDitch)
			{
				L.lotOf[b] = Lot::Field;
				++fields;
				// A home field's well: water at the pad's middle, kept in the pad register so the
				// beach is laid and the plot's tally excludes it.
				if (homeField && L.padSize[b] >= kHomeFieldPad)
					for (int dy = 0; dy < kWellVertices; ++dy)
						for (int dx = 0; dx < kWellVertices; ++dx)
						{
							const int i = t.at(L.padX[b] + L.padSize[b] / 2 - 1 + dx,
											   L.padY[b] + L.padSize[b] / 2 - 1 + dy);
							L.sketch[i] = WATER;
							L.pads.water[i] = 1;
						}
			}
			else if ((c + r) % 2 == 1 && int(context.bounded("allotments-wood", 100)) < o.woodShare)
			{
				L.lotOf[b] = Lot::Woodlot;
				++woodlots;
			}
			else if (L.padSize[b] == kSmallPad && o.lotSize == 0)
			{
				// Small open lots alternate quarry and grove: the prizes of the subdivision.
				L.lotOf[b] = (smallOpen++ % 2 == 0) ? Lot::Quarry : Lot::Grove;
				(L.lotOf[b] == Lot::Quarry ? quarries : groves) += 1;
			}
			else
			{
				L.lotOf[b] = Lot::Open;
				++open;
			}
		}
	context.telemetry.measure("allotments.lots.fields", fields);
	context.telemetry.measure("allotments.lots.woodlots", woodlots);
	context.telemetry.measure("allotments.lots.quarries", quarries);
	context.telemetry.measure("allotments.lots.groves", groves);
	context.telemetry.measure("allotments.lots.open", open);

	// The bases fit their cities: proved on the sketch as the game will see it, beaches laid.
	TerrainSketch beached = L.sketch;
	layBeaches(beached, t);
	const std::vector<unsigned char> pure = pureTiles(beached, t, GRASS);
	const std::vector<unsigned char> water = pureTiles(beached, t, WATER);
	std::vector<unsigned char> openGround(n, 0);
	for (int i = 0; i < n; ++i)
		openGround[i] = !water[i];
	for (size_t k = 0; k < L.sites.size(); ++k)
	{
		std::vector<unsigned char> buildable(n, 0);
		for (int i = 0; i < n; ++i)
			buildable[i] = pure[i] && L.interiorOf[i] == int(k);
		if (const std::string misfit = basePlanMisfit(t, L.plan, L.sites[k], buildable, openGround);
			!misfit.empty())
		{
			context.telemetry.choice("bases.fit", "rejected: " + misfit, int(k));
			L.failure = "The base does not fit its ground at this size; raise the size control or "
						"lower Colonists.";
			return L;
		}
		context.telemetry.choice("bases.fit", "fits", int(k));
	}
	return L;
}

bool generate(Game &game, GenerationContext &context)
{
	context.stage = "allotments layout";
	const AllotmentsOptions o(context.request);
	const Layout L = design(context.request, context);
	if (!L.failure.empty())
	{
		context.telemetry.fallback("allotments.layout.failure", L.failure);
		context.detail = L.failure;
		return false;
	}
	Map &map = game.map;
	const Torus &t = L.t;
	const int n = t.size(), teams = context.request.nbTeams;
	for (int k = 0; k < teams; ++k)
		game.addTeam();

	context.stage = "allotments terrain";
	TerrainSketch terrain = L.sketch;
	layBeaches(terrain, t);
	writeUndermap(map, terrain);

	context.stage = "allotments colonies";
	const BaseGarrison units = baseGarrison(o.colonists, o.garrison);
	if (!raiseBases(game, context, L.plan, L.sites, units, &L.interiorOf, "allotments-colonists"))
		return false;
	for (int k = 0; k < teams; ++k)
		plantBaseDepots(map, context, t, L.plan, L.sites[k]);

	context.stage = "allotments resources";
	// Nothing grows on a city's ground beyond its own stacks, on a lane, or on an open lot.
	std::vector<unsigned char> reserved = baseSurroundings(t, L.plan, L.sites);
	const std::vector<unsigned char> swarms = swarmSurroundings(t, context);
	Farm openPads = L.pads;
	openPads.plot.assign(n, 0);
	for (int i = 0; i < n; ++i)
		reserved[i] = reserved[i] || swarms[i] || L.lane[i] || L.interiorOf[i] >= 0;
	const std::vector<int> split = periodicNoise(t.w, t.h, 4, context.stream("allotments-split"));
	const int columns = L.lanes.columns(), rows = L.lanes.rows();
	for (int c = 0; c < columns; ++c)
		for (int r = 0; r < rows; ++r)
		{
			const int b = blockIndex(L.lanes, c, r);
			const int size = L.padSize[b];
			if (size == 0)
				continue;
			std::vector<int> pad;
			for (int dy = 0; dy < size; ++dy)
				for (int dx = 0; dx < size; ++dx)
					pad.push_back(t.at(L.padX[b] + dx, L.padY[b] + dy));
			const auto onPad = [&](int i)
			{ return L.pads.plot[i] && !reserved[i] && clearGround(map, i % t.w, i / t.w); };
			const MapGeneratorPoint middle(t.x(L.padX[b] + size / 2), t.y(L.padY[b] + size / 2));
			switch (L.lotOf[b])
			{
			case Lot::Field:
				plantFields(map, t, pad, int(scaledCount(size * size * kFieldWheatPercent / 100, o.wheat)),
							int(scaledCount(size * size * kFieldWoodPercent / 100, o.wood)),
							[&](int i) { return split[i]; });
				break;
			case Lot::Woodlot:
				growPatch(map, t, pad.front(), WOOD, int(scaledCount(size * size, o.wood)), onPad);
				break;
			case Lot::Quarry:
				if (scaledCount(1, o.stone) > 0)
					placeResourceClump(map, context, middle, STONE, 1);
				break;
			case Lot::Grove:
				if (scaledCount(1, o.fruit) > 0)
					placeResourceClump(map, context, middle,
									   CHERRY + int(context.bounded("allotments-fruit", 3)), 1);
				break;
			case Lot::Open:
				for (int i : pad)
					openPads.plot[i] = 1;
				break;
			case Lot::None:
				break;
			}
		}
	seedAlgae(map, context, t, "allotments-algae", o.algae, AlgaeBand::anyWater(40));
	clearFarmPlots(map, t, {openPads});
	secureStartingCrops(game, context, t);
	reopenCrampedStarts(game, context, {o.wheat, o.wood, o.stone, o.algae, o.fruit});

	// The lanes join everything and nothing grows on them; a home field's crops could still stand
	// across a city's way out, so the cheapest way through crops is opened, never through water.
	context.stage = "allotments routes";
	openColonyRoutes(map, context, t, StepCosts{1, 3, -1, -1, -1});
	return true;
}

std::string validateWorld(const Game &game, const GenerationContext &context)
{
	GenerationContext replay(context.request);
	const Layout L = design(context.request, replay);
	const AllotmentsOptions o(context.request);
	const Map &map = game.map;
	const Torus &t = L.t;
	const int n = t.size(), teams = context.request.nbTeams;
	if (const std::string mismatch = designMismatch(L, map, "allotments"); !mismatch.empty())
		return mismatch;
	for (int k = 0; k < teams; ++k)
		if (const std::string missing = validateBase(game, t, k, L.plan, L.sites[k], o.colonists);
			!missing.empty())
			return missing;
	// Every open lot is still open.
	for (int c = 0; c < L.lanes.columns(); ++c)
		for (int r = 0; r < L.lanes.rows(); ++r)
		{
			const int b = blockIndex(L.lanes, c, r);
			if (L.lotOf[b] != Lot::Open)
				continue;
			for (int dy = 0; dy < L.padSize[b]; ++dy)
				for (int dx = 0; dx < L.padSize[b]; ++dx)
				{
					const int x = t.x(L.padX[b] + dx), y = t.y(L.padY[b] + dy);
					if (!map.isGrass(x, y) || map.isResource(x, y))
						return "An open lot at (" + std::to_string(x) + ", " + std::to_string(y) +
							   ") is not buildable.";
				}
		}
	// Every colony as near a ditch as every other, give or take where its city fell between two
	// ditch lanes: the walk to the nearest ditch beach.
	std::vector<unsigned char> beach(n, 0);
	bool anyDitch = false;
	for (int i = 0; i < n; ++i)
		if (map.isWater(i % t.w, i / t.w))
			for (int dy = -1; dy <= 1; ++dy)
				for (int dx = -1; dx <= 1; ++dx)
				{
					const int j = t.at(i % t.w + dx, i / t.w + dy);
					if (!map.isWater(j % t.w, j / t.w) && !map.isResource(j % t.w, j / t.w))
						beach[j] = anyDitch = true;
				}
	if (anyDitch && teams > 1)
		if (const std::string uneven = unevenCosts(costsToTarget(map, teams, beach, StepCosts::walking()),
												   kDitchWalkTolerancePitches *
													   (o.ditchEvery + L.superblock) * L.pitch,
												   "a ditch");
			!uneven.empty())
			return uneven;
	return walkFromFirstColony(map, teams, "the allotments", "down the lanes").error;
}
} // namespace

AllotmentsOptions::AllotmentsOptions(const GenerationRequest &r)
	: colonists(r.option("colonists")), garrison(r.option("garrison") != 0),
	  lotSize(r.option("lot-size")), laneSpacing(r.option("lane-spacing")),
	  ditchEvery(r.option("ditch-every")), woodShare(r.option("wood-share")),
	  wheat(r.option("wheat-amount")), wood(r.option("wood-amount")),
	  stone(r.option("stone-amount")), algae(r.option("algae-amount")),
	  fruit(r.option("fruit-amount"))
{
}

GeneratorDefinition allotmentsDefinition()
{
	GeneratorDefinition d{
		"allotments",
		35,
		"Allotments",
		1,
		false,
		// Lanes 12 apart by default: a block of 12 holds a 6x6 pad with a two-tile walkway round it
		// and a superblock of two holds a city base; 8 is the tightest a 4x4 pad fits, 16 gives
		// broad lots. A ditch every third lane puts every block within one block of water. Half the
		// odd blocks away from water are woodlots.
		{{"colonists", "Colonists", 16, 48, 4, 32, ControlGroup::Layout},
		 GeneratorControl::toggle("garrison", "Garrison", true, ControlGroup::Layout),
		 GeneratorControl::choice("lot-size", "Lot size", {"Mixed", "Small", "Medium", "Large"}, 0,
								  ControlGroup::Layout),
		 {"lane-spacing", "Lane spacing", 8, 16, 2, 12, ControlGroup::Terrain},
		 {"ditch-every", "Ditch every", 2, 4, 1, 3, ControlGroup::Terrain},
		 {"wood-share", "Wood share", 0, 100, 10, 50, ControlGroup::Resources},
		 GeneratorControl::percentage("wheat-amount", "Wheat amount"),
		 GeneratorControl::percentage("wood-amount", "Wood amount"),
		 GeneratorControl::percentage("stone-amount", "Stone amount"),
		 GeneratorControl::percentage("algae-amount", "Algae amount"),
		 GeneratorControl::percentage("fruit-amount", "Fruit amount")},
		generate,
		true,
		designFailure<design>,
		validateWorld};
	d.startingWorkers = [](const GenerationRequest &r) { return r.option("colonists"); };
	// Only the open lots are room to build, so the start scorer's reference for building room is
	// a third of the usual: a subdivision is cramped on purpose.
	d.qualityScale.roomReference = 300;
	return d;
}
