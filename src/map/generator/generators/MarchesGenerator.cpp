// SPDX-License-Identifier: GPL-3.0-or-later
#include "MarchesGenerator.h"
#include "FertilityField.h"
#include "Game.h"
#include "GenerationContext.h"
#include "Geometry.h"
#include "Grid.h"
#include "Homes.h"
#include "LatticeNoise.h"
#include "Morphology.h"
#include "Orbits.h"
#include "Pipeline.h"
#include "Planting.h"
#include "Points.h"
#include "Resources.h"
#include "Roads.h"
#include "Settlements.h"
#include "Sketch.h"
#include "Topology.h"
#include "Walls.h"
#include <algorithm>
#include <cmath>
#include <map>
#include <string>
#include <utility>
#include <vector>
using namespace MapGeneration;

// Marches: homelands on a lattice across the whole torus, with no centre and no prize in the middle.
// Every colony's home lies in its own territory, and between every two neighbouring territories runs
// a band of wild border country, the marches: wooded, with a watering hole on every stretch of
// border and an orchard of all three fruits wherever three (or four) territories meet. A colony has
// a frontier with every neighbour, and its choice is which border to push.
//
// Fairness comes from sliding the map onto itself rather than turning it: the homes sit on the
// roomiest translation lattice the torus holds (Orbits.h), so with 2, 4, 8 or 16 colonies every
// colony's neighbourhood is an exact copy of every other's (the border warp is summed over the
// lattice's orbits so the territories match too); other counts get evenly staggered rows and
// statistical fairness. A rectangular map is served as well as a square one, since nothing is
// designed round a middle.
//
// WHY IT PLAYS WELL (docs/map-generators/GAME_RULES_FOR_MAP_DESIGN.md). Every home has its own pond,
// so its fields regrow without leaving home; the marches' woods block walking until cut, so the
// border is crossed where someone chose to cut it; a watering hole is the only water on a border and
// so the only place a forward field grows; and an orchard of all three fruits at a junction is the
// strongest prize the game has, shared by three rivals at once.
namespace
{

// Every home's starting kit, unscaled whatever the amounts say, so every start is viable: wheat and
// wood patches beside the pond (12 tiles each is a few visits' worth, and they regrow beside the
// water) and a quarry of radius 2 (13 tiles of stone, which never runs out).
constexpr int kHomeWheat = 14, kHomeWood = 12, kHomeQuarry = 2;
// The land a home disc keeps from the edge of the marches: room for the disc's own rough outline
// (15% wobble of a 14-tile radius is 2 tiles) and a watering hole's pool and beach (4 tiles) never to
// meet the home's pond and beach.
constexpr int kHomeMargin = 5;
// A crowded map narrows the marches before it is refused, but never below this: two tiles of woods
// either side of the border still reads as a border.
constexpr int kNarrowestMarch = 4;
// A watering hole: a pool of radius 3 (about 25 corners of water, a beach round it) and 24 tiles of
// wheat, a forward field the size of a home kit, so holding a border stretch is worth a field.
constexpr double kPoolRadius = 3.0;
constexpr int kPoolWheat = 24;
// How far the border warp can move a border, as a share of the home spacing at full roughness: at
// 30% two neighbouring borders can never cross (they start half a spacing apart), and a home disc
// sized by geometryFor's rule always stays inside its homeland.
constexpr int kWarpPercent = 30;
// The share of the marches under wood at the default wood amount. Wood is planted in patches from a
// noise field (the gaps between patches are the natural ways through); 55% leaves the band clearly
// wooded but always crossable somewhere, and the route backstop cuts a way where it isn't.
constexpr int kWoodedPercent = 55;

struct Layout
{
	Torus t{1, 1};
	bool exact = false;
	double spacing = 0, homeRadius = 0;
	int marchWidth = 0;
	std::vector<ShapePoint> homes, kits;
	std::vector<int> territory; // every tile's homeland, including the marches
	std::vector<int> land;      // the homeland a tile is in, or -1 in the marches
	std::vector<int> homeOf;    // the home disc a tile is in, or -1
	std::vector<unsigned char> march, water;
	std::vector<int> holes, junctions; // the watering holes' and orchards' tiles
	std::string failure;
};

// The tiles of the marches where several homelands meet, grouped: each group's tile nearest its
// middle is where its prize goes.
std::vector<int> prizeSites(const Torus &t, const std::vector<unsigned char> &mask)
{
	const std::vector<int> label = connectedRegions(mask, t.w, t.h, true, GridNeighbors::Eight);
	std::vector<std::vector<int>> groups;
	for (int i = 0; i < t.size(); ++i)
		if (label[i] >= 0)
		{
			if (label[i] >= int(groups.size()))
				groups.resize(label[i] + 1);
			groups[label[i]].push_back(i);
		}
	std::vector<int> sites;
	for (const std::vector<int> &tiles : groups)
	{
		const int first = tiles.front();
		double sx = 0, sy = 0;
		for (int i : tiles)
		{
			sx += t.offsetX(first % t.w, i % t.w);
			sy += t.offsetY(first / t.w, i / t.w);
		}
		const int mx = t.x(first % t.w + int(std::lround(sx / tiles.size())));
		const int my = t.y(first / t.w + int(std::lround(sy / tiles.size())));
		int best = first;
		for (int i : tiles)
			if (t.dist2(mx, my, i % t.w, i / t.w) < t.dist2(mx, my, best % t.w, best / t.w))
				best = i;
		sites.push_back(best);
	}
	return sites;
}

Layout design(const GenerationRequest &request, GenerationContext &context)
{
	const MarchesOptions o(request);
	Layout L;
	L.t = {1 << request.wDec, 1 << request.hDec};
	const Torus &t = L.t;
	const int n = t.size(), teams = std::max(1, request.nbTeams);
	L.homeRadius = o.homeSize;

	// The homes, on the roomiest lattice the torus holds.
	const LatticeSites lattice =
		latticeSites(t.w, t.h, teams, context.bounded("marches-layout", std::uint32_t(t.w)),
					 context.bounded("marches-layout", std::uint32_t(t.h)));
	L.exact = lattice.exact;
	L.homes = lattice.sites;
	L.spacing = std::min(t.w, t.h);
	for (size_t a = 0; a < L.homes.size(); ++a)
		for (size_t b = a + 1; b < L.homes.size(); ++b)
			L.spacing =
				std::min(L.spacing, std::hypot(t.offsetX(int(L.homes[a].x), int(L.homes[b].x)),
											   t.offsetY(int(L.homes[a].y), int(L.homes[b].y))));
	// A home disc must stay inside its homeland however far the warp bends the border towards it: on
	// a crowded map the homes shrink to fit, then the marches narrow, before the map is refused.
	const int warpPercent = o.borderRoughness * kWarpPercent / 100;
	const double border = L.spacing * (0.5 - warpPercent / 100.0);
	L.marchWidth = o.marchWidth;
	L.homeRadius = std::min(L.homeRadius, std::floor(border - L.marchWidth / 2.0 - kHomeMargin));
	while (!homeHasRoom(L.homeRadius) && L.marchWidth > kNarrowestMarch)
	{
		L.marchWidth -= 2;
		L.homeRadius =
			std::min<double>(o.homeSize, std::floor(border - L.marchWidth / 2.0 - kHomeMargin));
	}
	if (!homeHasRoom(L.homeRadius))
	{
		L.failure = "Too many colonies for this map; use a bigger map or fewer colonies.";
		return L;
	}

	// Homelands: every tile's nearest home after a warp that bends the borders. On an exact lattice
	// the warp noise is laid on cells the lattice's moves are whole multiples of (latticePeriod), so
	// it is the same at every colony's image of a tile and the homelands are exact copies of one
	// another; summing plain noise over the orbits instead would average the roughness away and leave
	// the borders ruled straight. Otherwise the noise's cells are one and a half spacings across, big
	// enough to bend a border as a whole rather than fray it.
	std::vector<Site> sites;
	for (const ShapePoint &p : L.homes)
		sites.push_back({int(std::lround(p.x)), int(std::lround(p.y))});
	const int period = L.exact ? latticePeriod(translationSymmetry(t.w, t.h, teams))
							   : std::max(4, int(L.spacing) * 3 / 2);
	const std::vector<int> warpX =
		fractalNoise(t.w, t.h, period, 3, context.stream("marches-warp"));
	const std::vector<int> warpY =
		fractalNoise(t.w, t.h, period, 3, context.stream("marches-warp"));
	L.territory =
		nearestSiteLabels(t, sites, std::max(4, int(L.spacing)), warpX, warpY, warpPercent);

	// The marches: the band along every border, both sides alike.
	std::vector<unsigned char> border2(n, 0);
	for (int i = 0; i < n; ++i)
		for (const auto &s : kCardinalSteps)
			if (L.territory[t.at(i % t.w + s[0], i / t.w + s[1])] != L.territory[i])
				border2[i] = 1;
	L.march = dilateRound(t, border2, L.marchWidth / 2.0);
	L.land.assign(n, -1);
	for (int i = 0; i < n; ++i)
		if (!L.march[i])
			L.land[i] = L.territory[i];

	// The homes: a round clearing (the whole map is grass, so only its pond shows) at every site.
	L.homeOf.assign(n, -1);
	L.water.assign(n, 0);
	const RadialShape home(L.homeRadius, 0.15, context, "marches-home");
	const RadialShape pond(homePondRadius(L.homeRadius), 0.3, context, "marches-pond");
	L.kits = stampRoundHomes(t, L.homes, 0.0, home, L.homeRadius, 1, &pond, L.water, L.homeOf);

	// Watering holes: one on every stretch of border between two homelands, at its middle. On the
	// torus two homelands may share several separate stretches; each gets its own.
	std::map<std::pair<int, int>, std::vector<unsigned char>> stretches;
	for (int i = 0; i < n; ++i)
		for (const auto &s : kCardinalSteps)
		{
			const int other = L.territory[t.at(i % t.w + s[0], i / t.w + s[1])];
			if (other > L.territory[i])
			{
				auto &mask = stretches[{L.territory[i], other}];
				if (mask.empty())
					mask.assign(n, 0);
				mask[i] = 1;
			}
		}
	for (const auto &entry : stretches)
		for (int site : prizeSites(t, entry.second))
			L.holes.push_back(site);
	// Junctions: wherever three or more homelands meet within a tile; junctions within a few tiles
	// of each other (a four-way corner the warp split in two) are one orchard.
	std::vector<unsigned char> junction(n, 0);
	for (int y = 0; y < t.h; ++y)
		for (int x = 0; x < t.w; ++x)
		{
			int seen[9], count = 0;
			for (int dy = -1; dy <= 1; ++dy)
				for (int dx = -1; dx <= 1; ++dx)
				{
					const int label = L.territory[t.at(x + dx, y + dy)];
					if (std::find(seen, seen + count, label) == seen + count)
						seen[count++] = label;
				}
			junction[size_t(y) * t.w + x] = count >= 3;
		}
	if (o.orchards)
		L.junctions = prizeSites(t, dilate(t, junction, 3));
	// A pool at every watering hole.
	const RadialShape pool(kPoolRadius, 0.3, context, "marches-pool");
	for (int hole : L.holes)
		fillShape(L.water, t, hole % t.w + 0.5, hole / t.w + 0.5, pool, 0.0);
	return L;
}

// The deposits: every home's kit, farmland on each homeland's fertile ground, woods through the
// marches, wheat round every watering hole, and an orchard with a quarry at every junction.
void furnish(Map &map, const Layout &L, GenerationContext &context, const MarchesOptions &o,
			 int teams)
{
	const Torus &t = L.t;
	const int n = t.size();
	const std::vector<unsigned char> reserved = swarmSurroundings(t, context);
	for (int k = 0; k < teams; ++k)
		plantOpenHomeKit(
			map, t, context, L.kits[k], 0.0, L.homeRadius, kHomeWheat, kHomeWood, kHomeQuarry,
			[&](int i)
			{ return L.homeOf[i] == k && !reserved[i] && clearGround(map, i % t.w, i / t.w); });
	const Fertility::Field fertility = Fertility::forMap(map, false);
	const std::vector<int> patch = periodicNoise(t.w, t.h, 12, context.stream("marches-patch"));
	const std::vector<int> split = periodicNoise(t.w, t.h, 6, context.stream("marches-split"));
	// A homeland's only water is its pond, so its fertile ground (where the engine's growth probe
	// finds water, within 15 tiles) is the ring round the home; the ambient farmland is a share of
	// that ring, not of the homeland, or it would bury the home: 12% wheat and 8% wood of about a
	// thousand fertile tiles is a few patches, in the top half of a noise field so they clump.
	for (int k = 0; k < teams; ++k)
	{
		const auto eligible = [&](int i)
		{ return L.land[i] == k && !reserved[i] && clearGround(map, i % t.w, i / t.w); };
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
									 int(scaledCount(area / 900, o.stone)),
									 int(scaledCount(area / 2500, o.fruit))};
			},
			"marches-stone", "marches-fruit");
	}
	// The marches' woods: wood over the wooded share of the band, in patches from a noise field
	// (the gaps between patches are the natural ways through), and the odd outcrop. Far from water,
	// none of it grows back: a way cut through the marches stays cut.
	std::vector<int> band, levels;
	const std::vector<int> woods = periodicNoise(t.w, t.h, 9, context.stream("marches-woods"));
	const auto wild = [&](int i)
	{ return L.march[i] && !L.water[i] && clearGround(map, i % t.w, i / t.w); };
	for (int i = 0; i < n; ++i)
		if (L.march[i])
		{
			band.push_back(i);
			levels.push_back(woods[i]);
		}
	const int wooded = std::clamp(int(scaledCount(kWoodedPercent, o.wood)), 0, 100);
	const int level = percentile(levels, 100 - wooded);
	plantCover(map, t, L.march, WOOD, [&](int i) { return wild(i) && woods[i] >= level; });
	scatterClumps(context, t, band, int(scaledCount(int(band.size()) / 1200, o.stone)),
				  "marches-outcrops", wild,
				  [&](MapGeneratorPoint p) { placeResourceClump(map, context, p, STONE, 1); });
	// Wheat round every watering hole, on the first grass past the pool's beach.
	for (int hole : L.holes)
	{
		const int hx = hole % t.w, hy = hole / t.w;
		const auto near = [&](int i) { return wild(i) && t.dist2(hx, hy, i % t.w, i / t.w) <= 64; };
		if (const int seed = seedNear(t, hx, hy, 8, near); seed >= 0)
			growPatch(map, t, seed, CORN, int(scaledCount(kPoolWheat, o.wheat)), near);
	}
	// An orchard of all three fruits round every junction, with a quarry in the middle.
	for (int junction : L.junctions)
	{
		const int jx = junction % t.w, jy = junction / t.w;
		const auto near = [&](int i)
		{ return wild(i) && t.dist2(jx, jy, i % t.w, i / t.w) <= 100; };
		if (scaledCount(1, o.stone) > 0 && near(junction))
			placeResourceClump(map, context, MapGeneratorPoint(jx, jy), STONE, 1);
		if (scaledCount(1, o.fruit) > 0)
		{
			const double spin = context.bounded("marches-fruit", 3600) / 3600.0 * 2 * kPi;
			plantOrchard(map, t, context, jx, jy, 4.5, {spin}, 4.5, 4, 1, near);
		}
	}
}

bool generate(Game &game, GenerationContext &context)
{
	context.stage = "marches layout";
	const MarchesOptions o(context.request);
	const Layout L = design(context.request, context);
	if (!L.failure.empty())
	{
		context.detail = L.failure;
		return false;
	}
	Map &map = game.map;
	const Torus &t = L.t;
	const int teams = context.request.nbTeams;
	for (int k = 0; k < teams; ++k)
		game.addTeam();

	context.stage = "marches terrain";
	TerrainSketch terrain(t.size(), GRASS);
	for (int i = 0; i < t.size(); ++i)
		if (L.water[i])
			terrain[i] = WATER;
	layBeaches(terrain, t);
	writeUndermap(map, terrain);

	context.stage = "marches colonies";
	const auto homeMask = [&](int team)
	{
		std::vector<unsigned char> ground(size_t(t.size()), 0);
		for (int i = 0; i < t.size(); ++i)
			ground[i] = L.homeOf[i] == team && map.isGrass(i % t.w, i / t.w);
		return ground;
	};
	const auto anchor = [&](int team) { return homeSwarmSite(L.homes[team], 0.0, L.homeRadius); };
	if (!settleColonies(game, context, "marches-starts", homeMask, anchor))
		return false;

	context.stage = "marches resources";
	furnish(map, L, context, o, teams);
	seedAlgae(map, context, t, "marches-algae", o.algae, AlgaeBand::anyWater(30));
	secureStartingCrops(game, context, t);
	reopenCrampedStarts(game, context, {o.wheat, o.wood, o.stone, o.algae, o.fruit});
	// The marches are wooded and the fields grow; where they close a colony in, the cheapest cut
	// through the woods is opened, never a ford.
	context.stage = "marches routes";
	openColonyRoutes(map, context, t, StepCosts{1, 3, -1, -1, -1});
	return true;
}

std::string validateWorld(const Game &game, const GenerationContext &context)
{
	GenerationContext replay(context.request);
	const Layout L = design(context.request, replay);
	const Map &map = game.map;
	if (const std::string mismatch = designMismatch(L, map, "marches"); !mismatch.empty())
		return mismatch;
	const Torus &t = L.t;
	for (int k = 0; k < context.request.nbTeams; ++k)
	{
		bool pond = false;
		for (int dy = -2; dy <= 2 && !pond; ++dy)
			for (int dx = -2; dx <= 2 && !pond; ++dx)
				pond = map.isWater(t.x(int(L.kits[k].x) + dx), t.y(int(L.kits[k].y) + dy));
		if (!pond)
			return "Colony " + std::to_string(k) + "'s home has lost its pond.";
	}
	return walkFromFirstColony(map, context.request.nbTeams, "the marches", "through the marches")
		.error;
}
} // namespace

MarchesOptions::MarchesOptions(const GenerationRequest &r)
	: homeSize(r.option("home-size")), marchWidth(r.option("march-width")),
	  borderRoughness(r.option("border-roughness")), orchards(r.option("orchards") != 0),
	  wheat(r.option("wheat-amount")), wood(r.option("wood-amount")),
	  stone(r.option("stone-amount")), algae(r.option("algae-amount")),
	  fruit(r.option("fruit-amount"))
{
}

GeneratorDefinition marchesDefinition()
{
	return {"marches",
			25,
			"Marches",
			1,
			false,
			{{"home-size", "Home size", 10, 24, 1, 14, ControlGroup::Layout},
			 {"march-width", "March width", 6, 24, 2, 12, ControlGroup::Terrain},
			 {"border-roughness", "Border roughness", 0, 100, 10, 40, ControlGroup::Terrain},
			 // Off, the junctions carry no orchard and the watering holes are the only prizes.
			 GeneratorControl::toggle("orchards", "Orchards", true, ControlGroup::Layout),
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
