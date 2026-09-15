// SPDX-License-Identifier: GPL-3.0-or-later
#include "ContinentsGenerator.h"
#include "Biomes.h"
#include "Contact.h"
#include "Game.h"
#include "GenerationContext.h"
#include "Grid.h"
#include "Growth.h"
#include "Homes.h"
#include "Landmass.h"
#include "LatticeNoise.h"
#include "Morphology.h"
#include "Pipeline.h"
#include "Planting.h"
#include "Points.h"
#include "Raster.h"
#include "Resources.h"
#include "Roads.h"
#include "Settlements.h"
#include "Sketch.h"
#include "Territories.h"
#include "Unit.h"
#include "WorldAtlas.h"
#include <algorithm>
#include <climits>
#include <cstdint>
#include <functional>
#include <string>
#include <utility>
#include <vector>
using namespace MapGeneration;

// Continents: a real continent, drawn from the world atlas (WorldAtlas.h, baked from Natural Earth
// and the Köppen-Geiger climate map by tools/world_atlas.py), floating in an ocean that wraps
// round the torus. North America, South America, Africa, Europe, Asia or Oceania, at random or by choice,
// with the geography a player knows: the Great Lakes and Victoria and Baikal as lakes, the Sahara
// and the Gobi and the Outback as sand, the Rockies and the Andes and the Himalayas as stone, the
// taiga and the Amazon as wood, the Mississippi and the Nile and the Yangtze as rivers, and the
// plains people farm as wheat country. The colonies are dealt the best ground the continent has,
// as far from each other by land as it allows, and each is given what the geography left out.
//
// THIS IS A TOY, NOT A TOURNAMENT MAP. The geography is fixed, so no two colonies get the same
// ground, and the fairness is a best effort measured after the fact (StartQuality.h; the lobby
// keeps the best of several rolls) rather than a symmetry proved by construction. What it promises
// is that every colony can start (water in reach, a starter kit, room to build, a walk to every
// rival) and that the continent looks like itself.
//
// WHY IT PLAYS (docs/map-generators/GAME_RULES_FOR_MAP_DESIGN.md):
// - Wheat and wood regrow only within the growth probe's reach of pure water, so the fertile ground
//   is the coasts, the lakeshores and the river banks, as it is on Earth: the interior is finite
//   reserves and then desert. A colony holds its coast and fights for the next.
// - A desert is sand: walkable, unbuildable, growing nothing. The Sahara and the Outback are open
//   ground armies cross fast and nobody settles. Ice caps are sand too.
// - A range is stone in patches over 40% of its ground, below the percolation threshold where the
//   gaps still thread through: slow, winding, and a quarry at every step, but not a wall. Where the
//   scree does wall a colony in, the route opener cuts a three-wide pass.
// - A great river is a line of pure water a tile wide, a barrier to walking, with a ford every so
//   often where the noise says so. Holding a ford is holding a crossing.
// - The ocean round the continent is the torus's seam, so a continent never meets itself across
//   the wrap; islets out in it are prizes for the first colony to swim.
//
// THE STAGES. The atlas region is fitted into the map with a sea margin and resampled by majority
// to a corner grid; slivers, specks and islets too small to build on are cleaned off; classes
// become terrain (water, sand or grass), rivers are drawn and forded; then the colonies' sites are spread
// over the mainland by walking distance and walked to the middle of their ground, each dry site is
// given a pond, and beaches are laid. On the written map every kind of land is furnished with its
// biome kit (Biomes.h), every colony gets a starter kit, the routes between colonies are opened
// through scree and across rivers, and the crop guarantee has the last word.
namespace
{
// The sea kept round the continent on every side, as a share of the map's shorter side, and its
// floor: the torus's seam is open water, a beach fits, and an islet's moat has somewhere to be.
// 4 tiles on a 128 map, 8 on a 256, 16 on a 512.
constexpr int kMarginDivisor = 32;
constexpr int kMarginMinimum = 3;
// A colony's site needs a square of pure grass this many tiles out on every side: a 4x4 swarm with
// its two-tile clearing is 8 wide, so 4 out (a 9x9 square) holds it with a tile to spare. On a
// crowded small map the search relaxes to 3 and then 2 (a 5x5 square, the swarm and a ring for its
// workers) rather than refusing, and says so in the telemetry. The square is the colony's own
// whatever the territories do, so a swarm always fits.
constexpr int kSiteRoom = 4;
constexpr int kSiteRoomFloor = 2;
// Two sites must be at least this far apart by walking: two 9x9 squares side by side is 9, and a
// few tiles more keeps a kit's patches from landing in the neighbour's square. Below it the room
// relaxes to make more candidates; a spread that cannot reach even two squares' width plus one
// (2 * room + 1, so the squares never overlap) fails the request.
constexpr int kSiteSpacing = 12;
// Rivers are drawn only when a map tile is at least this many thousandths of an atlas cell: a
// river is a scar three tiles wide (water and its two beaches) whatever the map's size, and on a
// 128 map, where a tile is a fifth of a cell, the scars would take more land than the lakes do.
// 256 maps (about half a cell a tile) and up keep their rivers.
constexpr int kRiverMinimumTilesPer1000Cells = 300;
// Sites are walked to the middle of their territories this many times: the first spread puts a
// site at the edge of its ground (it was chosen for being far from the others) and two rounds are
// enough for the territories to settle; more just wobbles. A round that brings the closest pair
// of colonies nearer than this share of the spread's spacing is undone: room on every side is
// worth a shorter walk to the neighbours, but not one half as long (on a 256 map the spread's 80
// to 130 steps came down to 43 to 58 with unguarded rounds, which is a rush, not a choice).
constexpr int kRecentreRounds = 2;
constexpr int kRecentreKeepPercent = 70;
// A candidate site is judged by the fertile farmable grass within this many tiles of it: pure
// grass of the plain, forest and steppe kinds where a crop has at least kFertileChance (of
// Fertility::kScale) of regrowing, the ground a colony's first buildings and fields go on and keep
// going on. The better half of the candidates by that count is kept when it still holds two per
// colony and still spreads; otherwise every candidate competes. In the first playtest (2026-09-15,
// 4 colonies at 256, nicowar and maxima) the colonies eliminated first had started on 25 to 60
// building sites within their catchment (a site in a mountain range, where the scree then took
// 40% of the ground, or on a cramped cape) or on ground whose crops could not regrow; counting
// grass alone moved the sites inland, away from the water, and traded the one for the other.
constexpr int kRoomRadius = 10;
constexpr std::uint32_t kFertileChance = 400;
// No site within this many tiles of a river: a river is a barrier with a ford every dozen tiles,
// and a colony inside a loop of one (the Amazon's confluences, tuning run 2026-09-15) had 394
// tiles within its 24-step catchment where the fertile grass count round it said a thousand. The
// count sees ground; it does not see that a unit cannot get there.
constexpr int kRiverClearance = 6;
// A site must have room a unit can reach: at least this many walkable tiles within the 24 steps a
// young colony works in (StartQuality's catchment), on the bare sketch. The fertile grass count
// sees ground and not the way to it; a site between two river arms or on a cape can score well
// and still box its colony in (the Amazon's western loop left one with 443 tiles of catchment on
// the finished map against 1200 to 2000 for the others). A pick that fails this is skipped for
// the next farthest that passes; a spread in which none passes keeps the farthest anyway.
constexpr int kCatchmentSteps = 24;
constexpr int kCatchmentFloor = 900;
// A site's crops must be able to regrow: the mean growth chance (Fertility::kScale) over the
// square kRoomRadius round it must reach this floor at 100% oases, or a pond is dug for it, up to
// kPondsPerColony ponds. In that playtest every colony that started under about 2000 stalled once
// its kit was cut, and colonies above 2500 grew; the floor scales with `oases`, so 0 never digs
// and 200 waters a start twice as well.
constexpr std::uint32_t kFertilityFloor = 2500;
constexpr int kPondsPerColony = 3;
// A pond: this many water corners (about 20 pure tiles once the beach is laid, which waters a
// 30-tile square), dug 6 to 11 steps from the site; the next pond, when the last was not enough,
// two steps farther out each time so it lands beyond the first. 6 keeps the pond's beach (one
// corner out) clear of the swarm's clearing (4 out); the third band's 15 keeps the whole pond
// within the growth probe's reach of the swarm and the kit. Twenty-four corners and two ponds
// left a third of dry starts under the floor (bulk study, 2026-09-15).
constexpr int kPondCorners = 32;
constexpr int kPondNearest = 6;
constexpr int kPondFarthest = 11;
constexpr int kSecondPondStep = 2;
// Two clearings round a home. Within kFieldClearing (an 11x11 square) no ambient deposit is
// planted at all, fields included: on a fertile river bank the farmland kit's fields filled the
// home square itself and left a colony fifteen building sites (tuning run, 2026-09-15); the
// starter kit's own patches still go there. Within kHomeClearing (17x17) the ambient cover (a
// forest's wood, a range's scree) is not planted either, though fields are: room for a colony's
// first buildings before it cuts. The swarm's own two-tile clearing keeps everything off.
constexpr int kFieldClearing = 5;
constexpr int kHomeClearing = 8;
// Every colony's starter kit, unscaled whatever the amounts say: a colony must be able to start.
constexpr int kHomeRadius = 14;
constexpr int kKitWheat = 20, kKitWood = 16, kKitQuarry = 1;
// Fords: a river tile becomes a sand ford where a noise field of this period peaks within this
// radius along the river, so fords come about a period apart and never two together.
constexpr int kFordPeriod = 24;
constexpr int kFordSpacing = 12;
// The coast cleaning: islands under 12 tiles and pools under 6 vanish, land two tiles wide goes.
constexpr CoastCleaning kCoastCleaning{12, 6, 1};
// Islets in the open ocean: one per this much sea, tried this many times each, with this much
// water between an islet and any coast so it is only ever reached by swimming.
constexpr int kSeaPerIslet = 5000;
constexpr int kIsletAttempts = 40;
constexpr double kIsletMoat = 6;
// Routes between colonies: a clearable deposit costs three steps to cut, stone six (a pass is
// worth a long detour, but not any detour), water ten (a ford is the last resort). The lane cut is
// three wide (radius 1) so a column can use it.
constexpr StepCosts kRouteCosts{1, 3, 6, 10, -1};
constexpr int kRouteRadius = 1;

const char *const kRegionIds[] = {"north-america", "south-america", "africa",
								  "europe",        "asia",          "oceania"};
constexpr int kRegions = 6;

struct Layout
{
	Torus t{1, 1};
	const AtlasRegion *region = nullptr;
	RasterFit fit;
	std::vector<unsigned char> corners; // each undermap corner's LandClass
	std::vector<unsigned char> river;   // corners under a river
	std::vector<unsigned char> ford;    // river corners turned to sand
	TerrainSketch terrain;              // with beaches laid
	std::vector<unsigned char> pond;    // corners dug for colonies
	std::vector<unsigned char> noCover;  // the home clearings, where ambient cover is not planted
	std::vector<unsigned char> noFields; // the inner clearings, where no ambient deposit goes
	std::vector<unsigned char> mainland; // the tiles every colony stands on (walkable, one piece)
	std::vector<int> sites;              // each colony's site tile, dealt
	std::vector<int> territory;          // each tile's colony, -1 for none
	std::vector<Island> islets;
	int siteRoom = kSiteRoom;
	std::string failure;
};

/// What the map draws a class as. Lakes and sea are water; deserts and ice caps sand, since nothing
/// lives there and sand is what the game has for ground that grows nothing and holds no building;
/// everything else, mountains included, is grass, and gets its character from what is planted on
/// it.
TerrainType terrainOf(LandClass c)
{
	switch (c)
	{
	case LandClass::Ocean:
	case LandClass::Lake:
		return WATER;
	case LandClass::Desert:
	case LandClass::Ice:
		return SAND;
	default:
		return GRASS;
	}
}

/// The tiles all four of whose corners are of class `c`: what the map draws as that kind of land,
/// as pureTiles reads a sketch. A sketch of GRASS on the class's corners and WATER elsewhere asks
/// pureTiles the question directly.
std::vector<unsigned char> classTiles(const std::vector<unsigned char> &corners, const Torus &t,
									  LandClass c)
{
	TerrainSketch sketch(corners.size(), WATER);
	for (size_t i = 0; i < corners.size(); ++i)
		if (LandClass(corners[i]) == c)
			sketch[i] = GRASS;
	return pureTiles(sketch, t, GRASS);
}

// The whole layout as a pure function of the request and the context's streams: validateWorld
// builds it again and checks the finished map against it.
Layout design(const GenerationRequest &request, GenerationContext &context)
{
	const ContinentsOptions o(request);
	Layout L;
	L.t = {1 << request.wDec, 1 << request.hDec};
	const Torus &t = L.t;
	const int n = t.size(), teams = std::max(1, request.nbTeams);

	// The continent: the one asked for, or one drawn from its own stream so the choice never
	// shifts the draws the rest of the design makes.
	const int which = o.continent > 0 ? std::min(o.continent - 1, kRegions - 1)
									  : int(context.bounded("continents-choice", kRegions));
	L.region = atlasRegion(kRegionIds[which]);
	if (!L.region)
	{
		L.failure = "The world atlas has no such continent.";
		return L;
	}
	context.telemetry.choice("continents.region", L.region->id);

	// Fit it into the map inside the sea margin, turned when that makes it larger and the player
	// allows. The fit is in undermap corners, which the sketch is drawn in.
	const int margin = std::max(kMarginMinimum, std::min(t.w, t.h) / kMarginDivisor);
	L.fit = fitRaster(L.region->width, L.region->height, t.w, t.h, margin, o.orientation == 0);
	if (L.fit.width <= 0 || L.fit.height <= 0)
	{
		L.failure = "The map is too small to hold a continent.";
		return L;
	}
	const std::int64_t tilesPer1000Cells =
		std::int64_t(L.fit.num) * 1000 / std::max<std::int64_t>(1, L.fit.den);
	context.telemetry.measure("continents.fit.turned", L.fit.turned);
	context.telemetry.measure("continents.fit.tiles-per-1000-cells", tilesPer1000Cells);
	const std::vector<unsigned char> cells = decodeAtlas(*L.region);
	L.corners = resampleMajority(cells, L.fit, t, (unsigned char)LandClass::Ocean, kAtlasClassMask);
	const bool rivers = o.rivers && tilesPer1000Cells >= kRiverMinimumTilesPer1000Cells;
	if (o.rivers && !rivers)
		context.telemetry.fallback("continents.rivers.omitted",
								   "Map too small for a river's three-tile scar");
	L.river = rivers ? resampleAny(cells, L.fit, t, kAtlasRiverFlag)
					 : std::vector<unsigned char>(size_t(n), 0);

	// Clean the coast: what the majority resampling left as slivers, specks and islets nothing fits
	// on goes, and filled pools take the land round them. Lakes are cleaned with the sea (a lake
	// two tiles wide is beach and nothing else); rivers are drawn afterwards, so the cleaning
	// never eats them.
	std::vector<unsigned char> land(size_t(n), 0);
	for (int i = 0; i < n; ++i)
		land[i] = atlasLand(L.corners[i]);
	const std::vector<unsigned char> cleaned = cleanLandmass(t, land, kCoastCleaning);
	std::vector<unsigned char> filled(size_t(n), 0);
	int dropped = 0;
	for (int i = 0; i < n; ++i)
	{
		if (land[i] && !cleaned[i])
		{
			L.corners[i] = (unsigned char)LandClass::Ocean;
			++dropped;
		}
		else if (!land[i] && cleaned[i])
			filled[i] = 1;
	}
	L.corners = inheritLabels(t, L.corners, filled, (unsigned char)LandClass::Plain);
	context.telemetry.measure("continents.coast.corners-dropped", dropped);
	context.telemetry.measure("continents.coast.corners-filled",
							  std::count(filled.begin(), filled.end(), 1));

	// Terrain from classes, then the rivers: a river corner and the three corners that complete
	// its tile turn to water, so the river is a line of pure water tiles (one water corner alone
	// makes only shore, which stops nothing), and the line is bridged at its diagonal steps so a
	// unit cannot slip between two tiles that touch only at a corner. Only on land: at sea a river
	// is the sea.
	L.terrain.assign(size_t(n), WATER);
	for (int i = 0; i < n; ++i)
		L.terrain[i] = terrainOf(LandClass(L.corners[i]));
	std::vector<unsigned char> riverTiles(size_t(n), 0);
	for (int i = 0; i < n; ++i)
		riverTiles[i] = L.river[i] && cleaned[i];
	riverTiles = bridgeDiagonals(t, riverTiles);
	L.river.assign(size_t(n), 0);
	L.ford.assign(size_t(n), 0);
	int riverCount = 0;
	if (rivers)
	{
		// Fords where a noise field peaks along the river, at least kFordSpacing apart: a river
		// with no ford would cut the mainland in two for good (until swimming), and one forded
		// everywhere would be no river.
		const std::vector<int> ripple =
			periodicNoise(t.w, t.h, kFordPeriod, context.stream("continents-fords"));
		std::vector<int> valley(size_t(n), 0);
		for (int i = 0; i < n; ++i)
			valley[i] = riverTiles[i] ? -ripple[i] : 1 << 20;
		const std::vector<int> lowest = windowMinimum(t, valley, kFordSpacing);
		for (int i = 0; i < n; ++i)
		{
			if (!riverTiles[i])
				continue;
			++riverCount;
			const bool ford = valley[i] == lowest[i];
			const int x = i % t.w, y = i / t.w;
			for (int dy = 0; dy <= 1; ++dy)
				for (int dx = 0; dx <= 1; ++dx)
				{
					const int c = t.at(x + dx, y + dy);
					if (LandClass(L.corners[c]) == LandClass::Ocean ||
						LandClass(L.corners[c]) == LandClass::Lake)
						continue;
					if (ford)
						L.ford[c] = 1;
					else
					{
						L.river[c] = 1;
						L.terrain[c] = WATER;
					}
				}
		}
		// A ford is sand: walkable, and it keeps its corners out of the river.
		for (int i = 0; i < n; ++i)
			if (L.ford[i] && !L.river[i])
				L.terrain[i] = SAND;
	}
	context.telemetry.measure("continents.rivers.tiles", riverCount);
	context.telemetry.measure("continents.rivers.ford-corners",
							  std::count(L.ford.begin(), L.ford.end(), 1));

	// Islets in the open sea, before the beaches: prizes for whoever swims first (stockIslands).
	if (o.islets)
	{
		int sea = 0;
		for (int i = 0; i < n; ++i)
			sea += L.terrain[i] == WATER;
		const int wanted = sea / kSeaPerIslet;
		L.islets = raiseIslands(L.terrain, t, context,
								{"continents-islets", wanted, kIsletAttempts, kIsletMoat});
		context.telemetry.measure("continents.islets.wanted", wanted);
		context.telemetry.measure("continents.islets.actual", L.islets.size());
	}

	// Where a colony can stand: pure grass with room on every side, on the mainland. The mainland
	// is the largest piece of ground a unit can walk (anything but pure water) once rivers, which
	// are forded, and lakes, which are not, are in. Room relaxes from kSiteRoom down to the floor
	// when a small or ragged continent has too few roomy squares for its colonies.
	TerrainSketch beached = L.terrain;
	layBeaches(beached, t);
	const std::vector<unsigned char> pureWater = pureTiles(beached, t, WATER);
	const std::vector<unsigned char> pureGrass = pureTiles(beached, t, GRASS);
	std::vector<unsigned char> walkable(size_t(n), 0);
	for (int i = 0; i < n; ++i)
		walkable[i] = !pureWater[i];
	// Islets are not the mainland even when they are the largest land: they must be excluded
	// before the largest piece is taken.
	std::vector<unsigned char> ground = walkable;
	for (const Island &islet : L.islets)
		for (int i : islet.tiles)
			ground[i] = 0;
	L.mainland = largestRegion(t, ground, GridNeighbors::Eight);
	context.telemetry.measure("continents.mainland.tiles",
							  std::count(L.mainland.begin(), L.mainland.end(), 1));
	// The sites. Room relaxes from kSiteRoom down to the floor until a spread of `teams` sites at
	// least kSiteSpacing apart by walking is found; failing that, the roomiest spread whose squares
	// do not overlap is kept and the telemetry says so; failing even that, the request is refused.
	// Every level of room draws its own first sites, so the draws a request consumes depend only
	// on which levels it tried, and a validator replaying the design lands on the same sites.
	// The fertile farmable grass round every tile (windowCount), the measure a candidate is judged
	// by, from the crop growth field of the sketch as the game will draw it.
	const Fertility::Field startField = cropGrowthField(beached, t);
	std::vector<unsigned char> farmable(size_t(n), 0);
	for (int i = 0; i < n; ++i)
	{
		const LandClass c = LandClass(L.corners[i]);
		farmable[i] = pureGrass[i] &&
					  (c == LandClass::Plain || c == LandClass::Forest || c == LandClass::Steppe) &&
					  startField.at(i % t.w, i / t.w) >= kFertileChance;
	}
	const std::vector<int> roomAround = windowCount(t, farmable, kRoomRadius);
	// The candidates with `room`: every roomy square on the mainland, or with `choosy` only the
	// better half by fertile ground round them, so nobody starts in a range or on a cramped cape
	// while the plains stand empty. Empty when the choosy half would not hold two per colony.
	const std::vector<unsigned char> nearRiver = dilate(t, L.river, kRiverClearance);
	const auto candidatesWithRoom = [&](int room, bool choosy)
	{
		std::vector<unsigned char> square = erode(t, pureGrass, room);
		std::vector<int> scores;
		for (int i = 0; i < n; ++i)
		{
			square[i] = square[i] && L.mainland[i] && !nearRiver[i];
			if (square[i])
				scores.push_back(roomAround[i]);
		}
		if (!choosy)
			return std::make_pair(square, int(scores.size()));
		const int cut = scores.empty() ? 0 : percentile(scores, 50);
		std::vector<unsigned char> roomy(size_t(n), 0);
		int kept = 0;
		for (int i = 0; i < n; ++i)
		{
			roomy[i] = square[i] && roomAround[i] >= cut;
			kept += roomy[i];
		}
		if (kept < 2 * teams)
			return std::make_pair(std::vector<unsigned char>(), 0);
		return std::make_pair(roomy, kept);
	};
	// The least walk between any two sites, over walkable ground.
	const auto spacingOf = [&](const std::vector<int> &sites)
	{
		int spacing = INT_MAX;
		for (size_t a = 0; a < sites.size(); ++a)
		{
			const std::vector<int> steps = stepsFrom(t, tileMask(t, {sites[a]}), walkable);
			for (size_t b = a + 1; b < sites.size(); ++b)
				spacing = std::min(spacing, steps[sites[b]] < 0 ? 0 : steps[sites[b]]);
		}
		return sites.size() < 2 ? INT_MAX : spacing;
	};
	std::vector<unsigned char> candidates;
	int bestSpacing = -1, bestRoom = 0;
	bool bestChoosy = false;
	std::vector<int> bestSites;
	std::vector<unsigned char> bestCandidates;
	for (int room = kSiteRoom; room >= kSiteRoomFloor && bestSpacing < kSiteSpacing; --room)
		for (const bool choosy : {true, false})
		{
			auto [squares, count] = candidatesWithRoom(room, choosy);
			if (choosy)
				context.telemetry.measure("continents.sites.candidates", count, room);
			if (count < teams)
				continue;
			const std::function<bool(int)> roomy = [&](int site)
			{
				const Flood reach = floodFrom(t, tileMask(t, {site}), walkable, kCatchmentSteps);
				return int(reach.visited.size()) >= kCatchmentFloor;
			};
			int rejected = 0;
			const std::vector<int> sites = farthestSites(t, squares, walkable, teams, context,
														 "continents-sites", 6, &roomy, &rejected);
			if (int(sites.size()) < teams)
				continue;
			if (rejected > 0)
				context.telemetry.fallback("continents.sites.boxed-in",
										   "A pick had too little reachable room; kept anyway");
			const int spacing = spacingOf(sites);
			if (spacing > bestSpacing)
			{
				bestSpacing = spacing;
				bestRoom = room;
				bestChoosy = choosy;
				bestSites = sites;
				bestCandidates = squares;
			}
			if (spacing >= kSiteSpacing)
				break;
		}
	if (bestSites.empty() || bestSpacing < 2 * bestRoom + 1)
	{
		L.failure = "This continent has no room for that many colonies at this map size; use a "
					"bigger map or fewer colonies.";
		return L;
	}
	L.siteRoom = bestRoom;
	L.sites = bestSites;
	candidates = bestCandidates;
	if (bestRoom < kSiteRoom)
		context.telemetry.fallback("continents.sites.room-relaxed",
								   "Too few roomy squares; sites accept less room");
	if (bestSpacing < kSiteSpacing)
		context.telemetry.fallback("continents.sites.crowded",
								   "No spread reaches the intended spacing; the widest is kept");
	context.telemetry.choice("continents.sites.choice", bestChoosy ? "fertile-half" : "any-square");
	context.telemetry.measure("continents.sites.room", L.siteRoom);
	context.telemetry.measure("continents.sites.spacing", bestSpacing);

	// Each colony's ground grows from its own square, so the square is always its own and a swarm
	// always fits, and the territories are equal in area beyond that, wandering at their borders.
	// Then each site walks to the middle of its ground and the ground grows again, so a colony has
	// room on every side and its neighbours are a real walk away.
	const std::vector<int> border =
		periodicNoise(t.w, t.h, 12, context.stream("continents-territories"));
	const auto grow = [&]()
	{
		std::vector<std::vector<int>> seeds(static_cast<size_t>(teams));
		for (int k = 0; k < teams; ++k)
		{
			const int sx = L.sites[k] % t.w, sy = L.sites[k] / t.w;
			for (int dy = -L.siteRoom; dy <= L.siteRoom; ++dy)
				for (int dx = -L.siteRoom; dx <= L.siteRoom; ++dx)
					seeds[k].push_back(t.at(sx + dx, sy + dy));
		}
		L.territory = growTerritories(t, L.mainland, seeds, [&](int i) { return border[i] / 64; })
						  .labels;
	};
	grow();
	int recentred = 0;
	for (int round = 0; round < kRecentreRounds; ++round)
	{
		const std::vector<int> before = L.sites;
		const std::vector<int> territoryBefore = L.territory;
		L.sites = recentreSites(t, L.territory, candidates, L.sites);
		grow();
		if (spacingOf(L.sites) * 100 < bestSpacing * kRecentreKeepPercent)
		{
			L.sites = before;
			L.territory = territoryBefore;
			context.telemetry.fallback("continents.sites.recentre-undone",
									   "Recentring brought two colonies too close", round);
			break;
		}
		++recentred;
	}
	context.telemetry.measure("continents.sites.recentre-rounds", recentred);
	dealStarts(context, L.sites); // which colony gets which site is a draw, not the order
	grow();                       // the labels follow the deal
	context.telemetry.measure("continents.sites.spacing-settled", spacingOf(L.sites));

	// Water for the dry: a site whose ground could not regrow its crops (mean fertility over the
	// home square under the floor, measured on the sketch as the game will draw it, beaches and
	// all) gets a pond dug on its own territory, and a second one beyond it if the first was not
	// enough. The floor scales with the oases amount; at 0 the geography stands and a dry colony
	// starts on its finite kit. Ponds keep clear of rivers, fords, sand and other colonies' ground.
	L.pond.assign(size_t(n), 0);
	const std::uint32_t floor = std::uint32_t(scaledCount(kFertilityFloor, o.oases));
	if (floor > 0)
	{
		const auto fieldNow = [&]()
		{
			TerrainSketch drawn = L.terrain;
			layBeaches(drawn, t);
			return cropGrowthField(drawn, t);
		};
		Fertility::Field field = fieldNow();
		const std::vector<int> ripple =
			periodicNoise(t.w, t.h, 5, context.stream("continents-ponds"));
		std::vector<int> queued(size_t(n), 0);
		for (int k = 0; k < teams; ++k)
		{
			const int site = L.sites[k];
			const std::uint32_t before = meanFertilityAround(field, t, site, kRoomRadius);
			context.telemetry.measure("continents.ponds.fertility-before", before, k);
			std::uint32_t now = before;
			int ponds = 0;
			while (now < floor && ponds < kPondsPerColony)
			{
				TerrainSketch was = L.terrain;
				const int dug = digPond(
					L.terrain, t, site, kPondNearest + ponds * kSecondPondStep,
					kPondFarthest + ponds * kSecondPondStep + 1, kPondCorners,
					[&](int i)
					{
						return L.territory[i] == k && L.terrain[i] == GRASS && !L.river[i] &&
							   !L.ford[i];
					},
					[&](int i) { return ripple[i] / 65536.0; }, queued, k * kPondsPerColony + ponds + 1);
				context.telemetry.measure("continents.ponds.dug-corners", dug, k);
				if (dug == 0)
				{
					context.telemetry.fallback("continents.ponds.none",
											   "No room for a pond beside a dry site", k);
					break;
				}
				for (int i = 0; i < n; ++i)
					L.pond[i] = L.pond[i] || (L.terrain[i] != was[i]);
				++ponds;
				field = fieldNow();
				now = meanFertilityAround(field, t, site, kRoomRadius);
			}
			context.telemetry.measure("continents.ponds.fertility-after", now, k);
			if (now < floor)
				context.telemetry.fallback("continents.ponds.still-dry",
										   "Fertility under the floor after its ponds", k);
		}
	}
	// The home clearings, kept free of ambient cover, and their inner squares of ambient
	// deposits altogether, in generate().
	std::vector<unsigned char> homes(size_t(n), 0);
	for (int site : L.sites)
		homes[site] = 1;
	L.noCover = dilate(t, homes, kHomeClearing);
	L.noFields = dilate(t, homes, kFieldClearing);
	layBeaches(L.terrain, t);
	return L;
}

bool generate(Game &game, GenerationContext &context)
{
	context.stage = "continents layout";
	const ContinentsOptions o(context.request);
	const Layout L = design(context.request, context);
	if (!L.failure.empty())
	{
		context.telemetry.fallback("continents.layout.failure", L.failure);
		context.detail = L.failure;
		return false;
	}
	Map &map = game.map;
	const Torus &t = L.t;
	const int n = t.size(), teams = context.request.nbTeams;
	for (int k = 0; k < teams; ++k)
		game.addTeam();

	context.stage = "continents terrain";
	writeUndermap(map, L.terrain);

	// Every colony on its own territory's grass, its swarm at its site.
	context.stage = "continents colonies";
	const auto homeMask = [&](int team)
	{
		std::vector<unsigned char> ground(size_t(n), 0);
		for (int i = 0; i < n; ++i)
			ground[i] = L.territory[i] == team && map.isGrass(i % t.w, i / t.w);
		return ground;
	};
	const auto anchor = [&](int team)
	{ return MapGeneratorPoint(L.sites[team] % t.w - 2, L.sites[team] / t.w - 2); };
	if (!settleColonies(game, context, "continents-starts", homeMask, anchor))
		return false;

	// Every kind of land furnished with its kit, scaled to the amounts: the geography decides
	// what grows where, the amounts how much. Islets keep their own prize. Nothing goes on the
	// swarms' clearings, and the fords stay sand.
	context.stage = "continents resources";
	const ResourceAmounts amounts{o.wheat, o.wood, o.stone, o.algae, o.fruit};
	std::vector<unsigned char> reserved = swarmSurroundings(t, context);
	// The ambient layers keep off the inner clearings too; the starter kits below do not.
	std::vector<unsigned char> ambientClear = reserved;
	for (int i = 0; i < n; ++i)
		ambientClear[i] = reserved[i] || L.noFields[i];
	const BiomeTerrain bare{std::vector<unsigned char>(size_t(n), 0),
							std::vector<unsigned char>(size_t(n), 0),
							std::vector<unsigned char>(size_t(n), 0)};
	struct Land
	{
		LandClass kind;
		BiomeKit kit;
		const char *stream;
	};
	const Land lands[] = {{LandClass::Plain, farmland(), "continents-plain"},
						  {LandClass::Forest, woodland(), "continents-forest"},
						  {LandClass::Steppe, savanna(), "continents-steppe"},
						  {LandClass::Tundra, barrens(), "continents-tundra"},
						  {LandClass::Mountain, o.mountains ? highland() : savanna(),
						   "continents-mountain"}};
	for (const Land &land : lands)
	{
		// A region is the tiles all four of whose corners are the kind: what the map draws as it.
		const std::vector<unsigned char> region = classTiles(L.corners, t, land.kind);
		int tiles = 0;
		for (unsigned char r : region)
			tiles += r;
		if (context.telemetry.enabled())
			context.telemetry.measure(std::string("continents.land.") + land.kit.name + ".tiles",
									  tiles);
		if (tiles == 0)
			continue;
		furnishBiome(map, t, context, region, bare, scaledBiome(land.kit, amounts), ambientClear,
					 land.stream, &L.noCover);
	}
	stockIslands(map, context, L.islets, "continents-islets");
	seedAlgae(map, context, t, "continents-algae", o.algae,
			  AlgaeBand::shallows(1, 8).thriving(0.5));

	// The starter kit, unscaled, on each colony's own ground: wheat and wood either side of the
	// site and a quarry beyond, wherever clear ground allows. The guarantee that follows tops up a
	// colony whose kit found no room.
	for (int k = 0; k < teams; ++k)
	{
		const ShapePoint site{double(L.sites[k] % t.w), double(L.sites[k] / t.w)};
		plantOpenHomeKit(map, t, context, site, 0.0, kHomeRadius, kKitWheat, kKitWood, kKitQuarry,
						 [&](int i)
						 {
							 return L.territory[i] == k && !reserved[i] &&
									clearGround(map, i % t.w, i / t.w);
						 });
	}

	// The routes: scree, forest and rivers can close a colony off; a pass three wide is cut where
	// the cheapest way needs one, and a ford laid where only water serves. Then the crops, so a
	// colony whose route took its nearest wheat gets it back, and the swarms' clearings are
	// cleared again after that; then the routes once more, since a topped-up deposit can land on
	// the lane just cut (it did, in the bulk study of 2026-09-15: 512x256, ten colonies, wood at
	// 200% and stone at 300%, colony 5 walled off again by its own top-up). The second pass is
	// idle wherever the first still holds.
	context.stage = "continents routes";
	const bool routed = openColonyRoutes(map, context, t, kRouteCosts, kRouteRadius);
	context.telemetry.measure("continents.routes.opened", routed);
	secureStartingCrops(game, context, t);
	reopenCrampedStarts(game, context, amounts);
	const bool reopened = openColonyRoutes(map, context, t, kRouteCosts, kRouteRadius);
	context.telemetry.measure("continents.routes.reopened", reopened);
	return true;
}

std::string validateWorld(const Game &game, const GenerationContext &context)
{
	GenerationContext replay(context.request);
	const Layout L = design(context.request, replay);
	if (const std::string mismatch = designMismatch(L, game.map, "continents"); !mismatch.empty())
		return mismatch;
	// The continent is where the design put it: the mainland stays inside the sea margin, so the
	// seam is open water (an islet may sit on it, being an island) and no colony's ground wraps
	// round to meet itself.
	const Torus &t = L.t;
	for (int x = 0; x < t.w; ++x)
		if (L.mainland[t.at(x, 0)])
			return "The continent's sea margin has mainland on the map's seam.";
	for (int y = 0; y < t.h; ++y)
		if (L.mainland[t.at(0, y)])
			return "The continent's sea margin has mainland on the map's seam.";
	return walkFromFirstColony(game.map, context.request.nbTeams, "the continent", "").error;
}
} // namespace

ContinentsOptions::ContinentsOptions(const GenerationRequest &r)
	: continent(r.option("continent")), orientation(r.option("orientation")),
	  mountains(r.option("mountains") != 0), rivers(r.option("rivers") != 0),
	  islets(r.option("islets") != 0), oases(r.option("oases")), wheat(r.option("wheat-amount")),
	  wood(r.option("wood-amount")), stone(r.option("stone-amount")),
	  algae(r.option("algae-amount")), fruit(r.option("fruit-amount"))
{
}

GeneratorDefinition continentsDefinition()
{
	return {"continents",
			44, // 33 was Patchwork, retired 2026-09-13 and never reused
			"Continents",
			1,
			false,
			{GeneratorControl::choice("continent", "Continent",
									  {"Random", "North America", "South America", "Africa",
									   "Europe", "Asia", "Oceania"},
									  0),
			 GeneratorControl::choice("orientation", "Orientation", {"Turn to fit", "Upright"}, 0),
			 GeneratorControl::toggle("mountains", "Mountain ranges", true),
			 GeneratorControl::toggle("rivers", "Great rivers", true),
			 GeneratorControl::toggle("islets", "Islets", true),
			 GeneratorControl::percentage("oases", "Oases", 200),
			 GeneratorControl::percentage("wheat-amount", "Wheat amount"),
			 GeneratorControl::percentage("wood-amount", "Wood amount"),
			 GeneratorControl::percentage("stone-amount", "Stone amount"),
			 GeneratorControl::percentage("algae-amount", "Algae amount"),
			 GeneratorControl::percentage("fruit-amount", "Fruit amount")},
			generate,
			true,
			designFailure<design>,
			validateWorld};
}
