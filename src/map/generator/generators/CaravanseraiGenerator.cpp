// SPDX-License-Identifier: GPL-3.0-or-later
#include "CaravanseraiGenerator.h"
#include "Bases.h"
#include "Contact.h"
#include "Drawing.h"
#include "Game.h"
#include "GenerationContext.h"
#include "Geometry.h"
#include "Grid.h"
#include "Orbits.h"
#include "Pipeline.h"
#include "Planting.h"
#include "Resources.h"
#include "Roads.h"
#include "Room.h"
#include "Routes.h"
#include "Sketch.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>
using namespace MapGeneration;

// Caravanserai: every colony starts with a finished capital - its whole base, two stocked towers,
// thirty-odd colonists and a garrison - on a disc of grass with a pond and fields enough to live
// on, and nothing else: no stone, no fruit, no algae at home. Everything else is desert: bare sand,
// walkable but unbuildable and foodless, so a unit out on it is a unit away from every inn.
// Between neighbouring capitals, half way, stand the outposts: discs of grass round a pond with a
// quarry, an orchard of the three fruits and algae, the only stone, fruit and algae on the map,
// each the same walk from the two capitals that share it. And along the way from every capital to
// its outposts, spaced a supply hop apart, lie the oases: small discs of grass with a pond, room
// for one inn and one tower, nothing more. A colony extends its reach one oasis at a time; an army
// that outruns its oases fights hungry; whoever holds a chain of oases holds the way to the
// outpost, and whoever cuts it starves the army beyond.
//
// The capitals stand on a lattice (Orbits.h); every outpost is the midpoint between two
// neighbouring capitals (Routes.h), and on flat sand straight distance is walking cost, so the
// outposts are equidistant by construction and the validator proves the walks equal. Chains are
// stepping stones along those straight ways.
//
// WHY IT PLAYS WELL (docs/map-generators/GAME_RULES_FOR_MAP_DESIGN.md). Sand holds no building and
// no crop, so the desert can never be settled, only crossed, and every forward inn stands where
// the map put an oasis; stone is permanent and the only stone is at the outposts, so upgrades
// mean holding an outpost; fruit converts a hungry enemy, and the only fruit stands where two
// colonies meet; and water is the one thing the map has none of between the discs, so the
// swimming-pool turn never comes - this is a map of walks.
namespace
{
// An oasis is a disc of radius 6 with a 2x2-vertex pond (one tile of water with its beach) at one
// side: pure grass enough for a 2x2 inn and a 2x2 tower with a walkway, which the validator counts.
// An outpost is a disc of radius 9 with a 3x3-vertex pond at its middle, its quarry and orchard
// round the pond at radius 5. The capital's pond is 2x2 vertices at the back of its disc, four
// tiles in from the edge so its beach never reaches the sand.
constexpr double kOasisRadius = 6, kOutpostRadius = 9;
constexpr int kOasisPondVertices = 2, kOutpostPondVertices = 3, kCapitalPondVertices = 2;
constexpr int kCapitalPondInset = 4;
// The room a capital needs beyond its base's reach: a walkway round the base, then the pond, four
// tiles in from the edge, whose beach spoils the tile behind it for building, so the base's back
// row stays pure grass; six tiles (the second sweep refused every capital of 14 at five).
constexpr int kCapitalBeyondBase = 6;
// How rough the discs' outlines are (RadialShape: four harmonics whose amplitudes sum to about
// three times the roughness at the worst angle). A capital is nearly round, 0.04, since its base's
// corners lie eleven tiles from its middle and the first sweep found a capital of 15 at 0.1
// dipping under them; an outpost is 0.08 so its prizes at radius 5 always stand on grass; an
// oasis, which only has to hold an inn and a tower, may be as ragged as 0.2.
constexpr double kCapitalRoughness = 0.04, kOutpostRoughness = 0.08, kOasisRoughness = 0.2;
// Every capital's fields, unscaled: wheat and wood beside its pond, where they regrow. The first
// headless play (four AIs, 20000 ticks) starved one colony down to a single worker on 24 tiles of
// wheat, so a capital now has 36 of wheat and 20 of wood, an oasis a patch of 8 wheat beside its
// pond (a caravanserai stocks food: a forward inn there has something to fill itself with) and an
// outpost 16 round its pond, the granary an outpost is held for besides its stone and fruit. All
// unscaled: they are what keeps every colony alive, not the map's ambient layer.
constexpr int kHomeWheat = 36, kHomeWood = 20, kOasisWheat = 8, kOutpostWheat = 16;
// Where the orchard and the quarry stand round an outpost's pond, and how big: the groves a tile
// clump each, the quarry a clump of radius 2 (some thirteen tiles, which never run out).
constexpr double kPrizeRadius = 5, kOrchardSpacing = 3.5;
constexpr int kQuarryRadius = 2;
// The least straight-line ground a chain must have between a capital's edge and an outpost's for
// the outpost to stand at all; below it two capitals are simply too close.
constexpr int kLeastChain = 4;
// How far an oasis keeps from anything else: half a spacing from another oasis, and its own
// radius plus a gap from any capital or outpost, so no two discs' beaches meet.
constexpr double kDiscGap = 3;
// The tolerance the validator allows between colonies' walks to an outpost or an oasis: where the
// colonists stand round the swarm (up to kGarrisonReach rings out) and which way the base faces
// account for a dozen steps; the design accounts for none.
constexpr int kWalkTolerance = 16;
// Algae in every outpost pond: one clump per 4 water tiles, so a 2x2 pond gets its clump.
constexpr int kAlgaeTilesPerClump = 4;

struct Layout
{
	Torus t{1, 1};
	std::vector<ShapePoint> homes;
	std::vector<BaseSite> sites;
	BasePlan plan;
	int capital = 0, spacing = 0;
	std::vector<ShapePoint> outposts, oases, homePonds;
	std::vector<std::pair<int, int>> pairs; // the two colonies each outpost stands between
	std::vector<int> capitalOf, outpostOf, oasisOf; // per tile, or -1
	std::vector<unsigned char> water;
	TerrainSketch sketch;
	std::string failure;
};

// Stamps a square of water vertices `size` across with its top-left at (x, y).
void stampPond(Layout &L, int x, int y, int size)
{
	for (int dy = 0; dy < size; ++dy)
		for (int dx = 0; dx < size; ++dx)
			L.water[L.t.at(x + dx, y + dy)] = 1;
}

// The sketch from the discs and ponds: sand everywhere but the discs' grass and the ponds' water.
void writeSketch(Layout &L)
{
	const int n = L.t.size();
	L.sketch.assign(n, SAND);
	for (int i = 0; i < n; ++i)
	{
		if (L.capitalOf[i] >= 0 || L.outpostOf[i] >= 0 || L.oasisOf[i] >= 0)
			L.sketch[i] = GRASS;
		if (L.water[i])
			L.sketch[i] = WATER;
	}
}

Layout design(const GenerationRequest &request, GenerationContext &context)
{
	const CaravanseraiOptions o(request);
	Layout L;
	L.t = {1 << request.wDec, 1 << request.hDec};
	const Torus &t = L.t;
	const int n = t.size(), teams = std::max(1, request.nbTeams);
	L.plan = standardBasePlan(baseTier(o.colonists), BaseKind::Finished, 2, false);

	L.homes = latticeSites(t.w, t.h, teams, context.bounded("caravanserai-layout", std::uint32_t(t.w)),
						   context.bounded("caravanserai-layout", std::uint32_t(t.h)))
				  .sites;
	dealStarts(context, L.homes);
	const double nearest = nearestSiteDistance(t, L.homes);

	// The outposts: one between every colony and each of its `outposts` nearest neighbours, at the
	// midpoint; a pair shared by two colonies is one outpost.
	// A pair whose midpoint lands on or beside a third capital (on a single row of colonies a
	// colony's third neighbour is two steps along it, and the "outpost" between them would be the
	// colony in the middle) is no route at all and is dropped.
	for (const auto &pair : nearestPairs(t, L.homes, o.outposts))
	{
		const ShapePoint middle = midpointAcross(t, L.homes[pair.first], L.homes[pair.second]);
		bool blocked = false;
		for (size_t k = 0; k < L.homes.size() && !blocked; ++k)
			blocked = int(k) != pair.first && int(k) != pair.second &&
					  siteDistance(t, middle, L.homes[k]) < o.capitalSize + kOutpostRadius + kDiscGap;
		if (blocked)
		{
			context.telemetry.fallback("caravanserai.outposts.blocked",
									   "An outpost between two colonies would stand on a third");
			continue;
		}
		L.pairs.push_back(pair);
		L.outposts.push_back(middle);
	}

	// The capital: as big as asked, never smaller than its base needs, and never so big that an
	// outpost cannot stand between two of them (then it shrinks, then the map is refused).
	const int leastCapital = L.plan.reach + kCapitalBeyondBase;
	L.capital = std::max(o.capitalSize, leastCapital);
	if (L.capital > leastCapital && L.capital > o.capitalSize)
		context.telemetry.fallback("caravanserai.capital.grown",
								   "Capital grew to hold its base, pond and fields");
	const auto chainRoom = [&]
	{ return nearest / 2 - L.capital - kOutpostRadius; };
	while (!L.pairs.empty() && chainRoom() < kLeastChain && L.capital > leastCapital)
	{
		--L.capital;
		context.telemetry.fallback("caravanserai.capital.shrunk",
								   "Capitals shrank so an outpost stands between them");
	}
	if (!L.pairs.empty() && chainRoom() < kLeastChain)
	{
		L.failure = "Too many colonies for this map; use a bigger map or fewer colonies.";
		return L;
	}

	// Each capital faces its first outpost (the way its colonists will mostly go), or any way when
	// it has none. The site is the disc's middle; the pond stands at the back.
	for (size_t k = 0; k < L.homes.size(); ++k)
	{
		int facing = int(context.bounded("caravanserai-facing", 4));
		for (const auto &pair : L.pairs)
			if (pair.first == int(k) || pair.second == int(k))
			{
				const ShapePoint other = L.homes[pair.first == int(k) ? pair.second : pair.first];
				facing = quarterTurn(headingAcross(t, L.homes[k], other));
				break;
			}
		L.sites.push_back({int(std::lround(L.homes[k].x)) % t.w, int(std::lround(L.homes[k].y)) % t.h, facing});
	}

	// The oases: stepping stones from every capital towards each of its outposts, a spacing apart
	// from the capital's edge, stopping half a spacing short of the outpost's edge, kept apart from
	// each other and from every disc. A chain may have none on a small map (the outpost is then a
	// walk across bare sand); the spacing is asked for, never negotiated, since a shorter chain is
	// the map's answer to a cramped one.
	L.spacing = o.oasisSpacing;
	const auto tooNear = [&](ShapePoint p)
	{
		for (const ShapePoint &home : L.homes)
			if (siteDistance(t, p, home) < L.capital + kOasisRadius + kDiscGap)
				return true;
		for (const ShapePoint &outpost : L.outposts)
			if (siteDistance(t, p, outpost) < kOutpostRadius + kOasisRadius + kDiscGap)
				return true;
		for (const ShapePoint &oasis : L.oases)
			if (siteDistance(t, p, oasis) < std::max(L.spacing / 2.0, 2 * kOasisRadius + kDiscGap))
				return true;
		return false;
	};
	for (size_t p = 0; p < L.pairs.size(); ++p)
		for (const int k : {L.pairs[p].first, L.pairs[p].second})
			for (const ShapePoint &stone :
				 waypointsAlong(t, L.homes[k], L.outposts[p], L.spacing, L.capital + L.spacing / 2.0,
								kOutpostRadius + L.spacing / 2.0))
				if (!tooNear(stone))
					L.oases.push_back(stone);
	context.telemetry.measure("caravanserai.capital.actual", L.capital);
	context.telemetry.measure("caravanserai.outposts.actual", int(L.outposts.size()));
	context.telemetry.measure("caravanserai.oases.actual", int(L.oases.size()));

	// The discs and ponds. Capitals first, so their ground wins where a rough edge would touch.
	L.capitalOf.assign(n, -1);
	L.outpostOf.assign(n, -1);
	L.oasisOf.assign(n, -1);
	L.water.assign(n, 0);
	const RadialShape capital(L.capital, kCapitalRoughness, context, "caravanserai-capital");
	const RadialShape outpost(kOutpostRadius, kOutpostRoughness, context, "caravanserai-outpost");
	const RadialShape oasis(kOasisRadius, kOasisRoughness, context, "caravanserai-oasis");
	for (size_t k = 0; k < L.sites.size(); ++k)
	{
		forEachTileInShape(t, L.sites[k].x, L.sites[k].y, capital, 0,
						   [&](int i, double, double) { L.capitalOf[i] = int(k); });
		// The pond by its frame tile, not its vertices: a block of vertices turned by the facing
		// lands a tile off at two of the four facings (a vertex's tile is its lower-right neighbour),
		// which the third sweep found putting a beach on a base's back row. baseFootprint gives the
		// tile the frame tile turns to; the pond's vertices are that tile's corners.
		const BaseFootprint pondTile =
			baseFootprint(L.sites[k], -(L.capital - kCapitalPondInset), 0, 1, 1);
		const int pond = t.at(L.sites[k].x + pondTile.dx, L.sites[k].y + pondTile.dy);
		stampPond(L, pond % t.w, pond / t.w, kCapitalPondVertices);
		L.homePonds.push_back({double(pond % t.w), double(pond / t.w)});
	}
	for (size_t p = 0; p < L.outposts.size(); ++p)
	{
		forEachTileInShape(t, L.outposts[p].x, L.outposts[p].y, outpost, 0, [&](int i, double, double)
						   { if (L.capitalOf[i] < 0) L.outpostOf[i] = int(p); });
		stampPond(L, int(std::lround(L.outposts[p].x)) - kOutpostPondVertices / 2,
				  int(std::lround(L.outposts[p].y)) - kOutpostPondVertices / 2, kOutpostPondVertices);
	}
	for (size_t q = 0; q < L.oases.size(); ++q)
	{
		forEachTileInShape(t, L.oases[q].x, L.oases[q].y, oasis, 0, [&](int i, double, double)
						   { if (L.capitalOf[i] < 0 && L.outpostOf[i] < 0) L.oasisOf[i] = int(q); });
		// The pond to one side of the oasis, off the line of the chain, so the way through stays
		// grass: three tiles from the middle, on the side a draw picks.
		const double side = context.bounded("caravanserai-oasis", 2) ? 1.0 : -1.0;
		stampPond(L, int(std::lround(L.oases[q].x + side * 3)) - kOasisPondVertices / 2,
				  int(std::lround(L.oases[q].y)) - kOasisPondVertices / 2, kOasisPondVertices);
	}
	writeSketch(L);

	// The bases fit their capitals, proved on the sketch as the game will see it.
	TerrainSketch beached = L.sketch;
	layBeaches(beached, t);
	const std::vector<unsigned char> pure = pureTiles(beached, t, GRASS);
	const std::vector<unsigned char> water = pureTiles(beached, t, WATER);
	std::vector<unsigned char> open(n, 0);
	for (int i = 0; i < n; ++i)
		open[i] = !water[i];
	for (size_t k = 0; k < L.sites.size(); ++k)
	{
		std::vector<unsigned char> buildable(n, 0);
		for (int i = 0; i < n; ++i)
			buildable[i] = pure[i] && L.capitalOf[i] == int(k);
		if (const std::string misfit = basePlanMisfit(t, L.plan, L.sites[k], buildable, open);
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
	context.stage = "caravanserai layout";
	const CaravanseraiOptions o(context.request);
	Map &map = game.map;
	// Desert first: everything the sketch does not paint is sand.
	map.makeHomogenMap(SAND);
	const Layout L = design(context.request, context);
	if (!L.failure.empty())
	{
		context.telemetry.fallback("caravanserai.layout.failure", L.failure);
		context.detail = L.failure;
		return false;
	}
	const Torus &t = L.t;
	const int n = t.size(), teams = context.request.nbTeams;
	for (int k = 0; k < teams; ++k)
		game.addTeam();

	context.stage = "caravanserai terrain";
	TerrainSketch terrain = L.sketch;
	layBeaches(terrain, t);
	writeUndermap(map, terrain);

	context.stage = "caravanserai colonies";
	const BaseGarrison units = baseGarrison(o.colonists, o.garrison);
	if (!raiseBases(game, context, L.plan, L.sites, units, &L.capitalOf, "caravanserai-colonists"))
		return false;

	context.stage = "caravanserai resources";
	std::vector<unsigned char> reserved = baseSurroundings(t, L.plan, L.sites);
	const std::vector<unsigned char> swarms = swarmSurroundings(t, context);
	for (int i = 0; i < n; ++i)
		reserved[i] = reserved[i] || swarms[i];
	// Every capital's fields beside its pond, on either flank of the line from the pond to the
	// swarm: the same at every facing.
	for (int k = 0; k < teams; ++k)
	{
		const KitFrame frame{int(L.homePonds[k].x), int(L.homePonds[k].y), L.sites[k].facing * kPi / 2};
		const Kit kit{frame.at(2, -4, 5), frame.at(2, 4, 5), frame.at(0, 0, 0), kHomeWheat, kHomeWood, -1};
		plantKit(map, t, context, kit,
				 [&](int i)
				 { return L.capitalOf[i] == k && !reserved[i] && clearGround(map, i % t.w, i / t.w); });
	}
	// Every outpost's prizes round its pond: the quarry towards the map's east and the orchard of
	// the three fruits towards its west, the same at every outpost, so no outpost is richer; then
	// its wheat on whatever grass is left nearest the pond. Every oasis's wheat likewise.
	for (size_t p = 0; p < L.outposts.size(); ++p)
	{
		const auto onOutpost = [&](int i)
		{ return L.outpostOf[i] == int(p) && clearGround(map, i % t.w, i / t.w); };
		if (scaledCount(1, o.stone) > 0)
			plantRound(map, t, context, L.outposts[p].x, L.outposts[p].y, kPrizeRadius, {0.0}, STONE,
					   kQuarryRadius, 3, onOutpost);
		if (scaledCount(1, o.fruit) > 0)
			plantOrchard(map, t, context, L.outposts[p].x, L.outposts[p].y, kPrizeRadius, {kPi},
						 kOrchardSpacing, 3, 1, onOutpost);
		if (const int seed = seedNear(t, int(L.outposts[p].x), int(L.outposts[p].y), 4, onOutpost);
			seed >= 0)
			growPatch(map, t, seed, WHEAT, kOutpostWheat, onOutpost);
	}
	for (size_t q = 0; q < L.oases.size(); ++q)
	{
		const auto onOasis = [&](int i)
		{ return L.oasisOf[i] == int(q) && clearGround(map, i % t.w, i / t.w); };
		if (const int seed = seedNear(t, int(L.oases[q].x), int(L.oases[q].y), 4, onOasis); seed >= 0)
			growPatch(map, t, seed, WHEAT, kOasisWheat, onOasis);
	}
	// Algae only in the outposts' ponds (a capital's or an oasis's pond has none).
	std::vector<int> pondOf(n, -1);
	for (int i = 0; i < n; ++i)
		if (map.isWater(i % t.w, i / t.w))
			for (size_t p = 0; p < L.outposts.size() && pondOf[i] < 0; ++p)
				if (t.dist2(i % t.w, i / t.w, int(L.outposts[p].x), int(L.outposts[p].y)) <= kOutpostPondVertices * kOutpostPondVertices)
					pondOf[i] = int(p);
	seedAlgae(map, context, t, "caravanserai-algae", o.algae, AlgaeBand::anyWater(kAlgaeTilesPerClump),
			  pondOf, int(L.outposts.size()));
	secureStartingCrops(game, context, t);
	reopenCrampedStarts(game, context, {o.wheat, o.wood, o.stone, o.algae, o.fruit});

	// Sand is open ground: every colony can already walk to every other. The backstop only ever
	// clears a crop that happened to stand across a capital's way out.
	context.stage = "caravanserai routes";
	openColonyRoutes(map, context, t, StepCosts::walking());
	return true;
}

std::string validateWorld(const Game &game, const GenerationContext &context)
{
	GenerationContext replay(context.request);
	const Layout L = design(context.request, replay);
	const CaravanseraiOptions o(context.request);
	const Map &map = game.map;
	const Torus &t = L.t;
	const int n = t.size(), teams = context.request.nbTeams;
	if (const std::string mismatch = designMismatch(L, map, "caravanserai"); !mismatch.empty())
		return mismatch;
	for (int k = 0; k < teams; ++k)
		if (const std::string missing = validateBase(game, t, k, L.plan, L.sites[k], o.colonists);
			!missing.empty())
			return missing;
	if (const std::string pond = homePondMissing(map, t, L.homePonds, teams, "capital", "pond");
		!pond.empty())
		return pond;
	// Every colony as far from an outpost, and from an oasis, as every other.
	if (teams > 1 && !L.outposts.empty())
	{
		std::vector<unsigned char> outposts(n, 0), oases(n, 0);
		for (int i = 0; i < n; ++i)
		{
			outposts[i] = L.outpostOf[i] >= 0 && !map.isWater(i % t.w, i / t.w);
			oases[i] = L.oasisOf[i] >= 0 && !map.isWater(i % t.w, i / t.w);
		}
		if (const std::string uneven = unevenCosts(
				costsToTarget(map, teams, outposts, StepCosts::walking()), kWalkTolerance, "an outpost");
			!uneven.empty())
			return uneven;
		if (!L.oases.empty())
			if (const std::string uneven = unevenCosts(
					costsToTarget(map, teams, oases, StepCosts::walking()), kWalkTolerance, "an oasis");
				!uneven.empty())
				return uneven;
	}
	// Every oasis has room for an inn and a tower: two free 2x2 footprints.
	const std::vector<unsigned char> buildable = buildableTiles(map);
	for (size_t q = 0; q < L.oases.size(); ++q)
	{
		std::vector<unsigned char> disc(n, 0);
		for (int i = 0; i < n; ++i)
			disc[i] = L.oasisOf[i] == int(q);
		if (buildSites(t, buildable, disc, 2) < 2)
			return "An oasis at (" + std::to_string(int(L.oases[q].x)) + ", " +
				   std::to_string(int(L.oases[q].y)) + ") has no room for an inn and a tower.";
	}
	return walkFromFirstColony(map, teams, "the desert", "across the sand").error;
}
} // namespace

CaravanseraiOptions::CaravanseraiOptions(const GenerationRequest &r)
	: colonists(r.option("colonists")), garrison(r.option("garrison") != 0),
	  capitalSize(r.option("capital-size")), oasisSpacing(r.option("oasis-spacing")),
	  outposts(r.option("outposts-per-colony")), wheat(r.option("wheat-amount")),
	  wood(r.option("wood-amount")), stone(r.option("stone-amount")),
	  algae(r.option("algae-amount")), fruit(r.option("fruit-amount"))
{
}

GeneratorDefinition caravanseraiDefinition()
{
	GeneratorDefinition d{
		"caravanserai",
		41,
		"Caravanserai",
		1,
		false,
		// A capital of 18 holds a city base with its pond and fields; 14 is a hamlet's least.
		// Oases 32 apart by default: on a 256 map with four colonies the way to an outpost is 64
		// tiles, room for one oasis; 24 puts two on it and 56 is a single stone on a big map. Two
		// outposts per colony: one towards each of its two nearest neighbours.
		{{"colonists", "Colonists", 16, 48, 4, 32, ControlGroup::Layout},
		 GeneratorControl::toggle("garrison", "Garrison", true, ControlGroup::Layout),
		 {"capital-size", "Capital size", 14, 22, 1, 18, ControlGroup::Layout},
		 {"oasis-spacing", "Oasis spacing", 24, 56, 8, 32, ControlGroup::Terrain},
		 {"outposts-per-colony", "Outposts per colony", 1, 3, 1, 2, ControlGroup::Layout},
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
	return d;
}
