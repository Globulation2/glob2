// SPDX-License-Identifier: GPL-3.0-or-later
#include "GlacisGenerator.h"
#include "Bases.h"
#include "Building.h"
#include "BuildingType.h"
#include "Compounds.h"
#include "Contact.h"
#include "Game.h"
#include "GenerationContext.h"
#include "Geometry.h"
#include "Grid.h"
#include "LatticeNoise.h"
#include "Orbits.h"
#include "Pipeline.h"
#include "Planting.h"
#include "Resources.h"
#include "Roads.h"
#include "Routes.h"
#include "Sketch.h"
#include "Towers.h"
#include "Walls.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>
using namespace MapGeneration;

// The Glacis: every colony starts inside a finished, walled compound - swarm, inns full of wheat, a
// hospital, a school, a barracks, a racetrack, a quarry, stocked towers on the wall, thirty-odd
// colonists and a garrison - and the first quarter hour of every other landscape (build a swarm's
// worth of huts, wait for wheat) is simply skipped. What lies between the compounds is the glacis:
// a wide plain of dry grass with no water on it at all, so nothing ever grows there and nothing
// ever grows it shut, yet it is grass, so any building put up on it is a deliberate forward move
// and stands in the open under the walls' towers. Cutting across the plain, one to a band between
// the compounds' rows, run the wadis: sunken rivers two tiles wide, each with a strip of farmland
// along both banks where wheat and wood regrow, and a sand ford across them every so often. The
// wadis are the only regrowing food outside the walls and the fords the only ways over the water
// until someone builds a swimming pool, so a wadi is what a colony leaves its compound for and a
// ford is where it meets its neighbour.
//
// The compounds stand on a lattice (Orbits.h): exact translation copies for 2, 4 or 8 colonies,
// evenly spaced staggered rows otherwise, so a rectangular map is served as well as a square one
// (a wedge design would stretch the wadis fat along the long side). Wadis run along the lattice's
// rows, or between its columns when it has only one row, which is what a 512x128 map with four
// colonies gets. Fairness is by construction: every compound is the same square with the same
// base at its middle, faces a random way, and stands midway between the two wadis bounding its
// band; the validator proves the walls stand, every base is complete, every colony has the same
// number of towers and the same walk to its nearest ford, and every colony can reach the first.
//
// WHY IT PLAYS WELL (docs/map-generators/GAME_RULES_FOR_MAP_DESIGN.md). A colony that already
// works decides from the first tick: upgrade behind the wall or push out to a wadi; the plain is
// buildable but barren, so a forward inn there has to be fed from home; stone walls are permanent
// and towers shoot over them, so the gates are where fights happen; and the fords, being sand,
// can never be built over or grown shut, so the way to a neighbour stays open whatever happens.
namespace
{
// The shape of a wadi band, across the rows, in tiles: three vertices of water make two tiles of
// pure water with a mixed sand/water tile either side (walkable only at the fords) and a mixed
// grass/sand tile beyond that; the banks are the pure grass from three to six steps out from the
// water on both sides (kBankReach), where every crop lies within the growth probe's reach of the
// water and no crop stands in the beach. Water 2 + mixed 4 + banks 8 = 14 tiles.
constexpr int kWaterVertices = 3, kBankNear = 3, kBankReach = 6;
constexpr int kBandWidth = 2 + 4 + 2 * (kBankReach - kBankNear + 1);
// Ground kept between a wall and the nearest wadi feature, and between two wadis: two tiles of
// bare grass so a beach never spoils a wall tile (stone stands on pure grass only) and a crop never
// stands against the wall, and six tiles between two wadis' bank crops so the plain reads as plain.
constexpr int kWallMargin = 2, kBetweenWadis = 6;
// Along a row, the least plain between two compounds' walls: room for a column of units to pass
// and a tower to stand between them.
constexpr int kAlongGap = 8;
// A ford is three sand vertices across the water: two tiles of pure sand and a mixed tile either
// side, a way over four tiles wide, which a column of units and a stone wall's worth of blocking
// can both fit. A gate is three tiles wide for the same reason. Through the banks on both sides
// of every ford runs a lane one tile wider than the ford on each side that no crop is planted on
// (the first roll planted the banks right up to the fords and the validator found every ford
// walled in by wheat), so a ford is a way over from the first tick, not after a harvest.
constexpr int kFordVertices = 3, kGateWidth = 3, kFordLaneMargin = 1;
// The room a compound needs beyond its base's reach: one tile of walkway round the base, two rows
// against the wall for the 2x2 towers that stand flush against it, and one more for the well: its
// pond three tiles in from the wall spoils the four tiles behind it for building with its beach,
// and the base's back row must stay pure grass (the second sweep refused every compound of 12 and
// 13 for exactly that).
constexpr int kCompoundBeyondBase = 4;
// Every compound's well and kit, unscaled. The first headless play (four AIs, 20000 ticks) had
// one colony fall from 71 units to 30 with up to 21 of them starving at a time: the compound was
// dry by design, its 24 wheat tiles never grew back, and the nearest bank was 45 tiles beyond the
// gate, too far to haul wheat to the inns. So every compound has a well - a pond of 2x2 vertices
// (one tile of water in its beach) three tiles in from the back wall - with a wheat patch and a
// wood patch either side of it, five tiles out, well within the growth probe's reach, so the
// kit regrows slowly (the beach's sand slows it) and a colony that never leaves its walls lives,
// while the banks are still where the food is. The quarry is the base plan's own depot.
constexpr int kWellVertices = 2, kWellInset = 3, kKitFlank = 5;
constexpr int kHomeWheat = 24, kHomeWood = 16;
// Towers: two open 2x2 pads beside the towers for more, four tiles between sites so a wall's
// towers spread along it rather than bunching at one corner.
constexpr int kTowerPads = 2, kTowerSpacing = 4;
// The banks: seven in ten tiles under wheat and two in ten under wood at the default amounts (a
// wadi is a granary first), the rest open so a unit walks the bank. The plain: one stone outcrop
// per 3000 tiles, so upgrades out on the plain are possible but the quarry at home is the rule.
constexpr int kBankWheatPercent = 70, kBankWoodPercent = 20, kPlainTilesPerOutcrop = 3000;

struct Layout
{
	Torus t{1, 1};
	std::vector<ShapePoint> homes;
	std::vector<BaseSite> sites;
	BasePlan plan;
	int compound = 0, wadis = 0;
	bool vertical = false; // wadis run between the lattice's columns, not its rows
	CompoundMasks compounds;
	std::vector<unsigned char> water, ford, bankZone, fordLane, plain;
	std::vector<int> territory; // every tile's nearest compound, for the towers' scoring
	std::vector<ShapePoint> fords; // the middle of every ford, on its wadi's water line
	std::vector<ShapePoint> wells; // every compound's well, as its top-left vertex
	TerrainSketch sketch;
	std::string failure;
};

Layout design(const GenerationRequest &request, GenerationContext &context)
{
	const GlacisOptions o(request);
	Layout L;
	L.t = {1 << request.wDec, 1 << request.hDec};
	const Torus &t = L.t;
	const int n = t.size(), teams = std::max(1, request.nbTeams);
	L.plan = standardBasePlan(baseTier(o.colonists), BaseKind::Finished, 0, true);

	// The compounds on a lattice; which colony gets which is a draw.
	L.homes = latticeSites(t.w, t.h, teams, context.bounded("glacis-layout", std::uint32_t(t.w)),
						   context.bounded("glacis-layout", std::uint32_t(t.h)))
				  .sites;
	dealStarts(context, L.homes);
	for (const ShapePoint &home : L.homes)
		L.sites.push_back({int(std::lround(home.x)) % t.w, int(std::lround(home.y)) % t.h,
						   int(context.bounded("glacis-facing", 4))});

	// The lattice's lines across the bands: its distinct rows, or its distinct columns when there
	// is one row (a wide map with few colonies), so every band runs between two lines of compounds
	// and every compound has a wadi on either side.
	std::vector<int> rows, columns;
	for (const BaseSite &site : L.sites)
	{
		if (std::find(rows.begin(), rows.end(), site.y) == rows.end())
			rows.push_back(site.y);
		if (std::find(columns.begin(), columns.end(), site.x) == columns.end())
			columns.push_back(site.x);
	}
	L.vertical = rows.size() == 1 && columns.size() > 1;
	// A compound with a single gate faces a wadi (either of the two bounding its band, by a draw),
	// so every colony's way out leads to its food and every colony's walk to a ford is the same;
	// the first sweep found single-gate maps failing the validator's walk check whenever a gate
	// happened to face along the band. With two gates (front and back) the facing is free.
	if (o.wallGates == 1)
		for (BaseSite &site : L.sites)
		{
			const int towards = int(context.bounded("glacis-gate", 2)) * 2; // 0 or 2: +/- across
			site.facing = L.vertical ? towards : towards + 1;
		}
	std::vector<int> lines = L.vertical ? columns : rows;
	std::sort(lines.begin(), lines.end());
	const int across = L.vertical ? t.w : t.h, along = L.vertical ? t.h : t.w;
	const auto acrossOf = [&](const BaseSite &s) { return L.vertical ? s.x : s.y; };
	const auto alongOf = [&](const BaseSite &s) { return L.vertical ? s.y : s.x; };
	// The narrowest band, wall line to wall line, and the nearest two compounds on one line.
	int narrowestBand = across, nearestAlong = along;
	for (size_t r = 0; r < lines.size(); ++r)
		narrowestBand =
			std::min(narrowestBand, lines.size() == 1
										? across
										: ((lines[(r + 1) % lines.size()] - lines[r]) % across + across) % across);
	for (size_t a = 0; a < L.sites.size(); ++a)
		for (size_t b = a + 1; b < L.sites.size(); ++b)
			if (acrossOf(L.sites[a]) == acrossOf(L.sites[b]))
			{
				const int d = alongOf(L.sites[a]) - alongOf(L.sites[b]);
				nearestAlong = std::min(nearestAlong, std::min((d % along + along) % along,
																(-d % along + along) % along));
			}

	// Negotiating the compound and the wadis into the band. A compound must hold its base with the
	// towers' rows; a band must hold two half compounds, its wadis with their banks, the margins
	// and the gaps between wadis. Too tight, the wadis go first (a band with one wadi still feeds),
	// then the compound shrinks to what its base needs, and only then the map is refused.
	const int leastCompound = L.plan.reach + kCompoundBeyondBase;
	L.compound = std::max(o.compoundSize, leastCompound);
	if (L.compound > o.compoundSize)
		context.telemetry.fallback("glacis.compound.grown",
								   "Compound grew to hold its base and the towers' rows");
	L.wadis = o.wadis;
	const auto bandNeeds = [&]
	{
		return 2 * (L.compound + 1) + L.wadis * kBandWidth + 2 * kWallMargin +
			   (L.wadis - 1) * kBetweenWadis;
	};
	const auto alongNeeds = [&] { return 2 * (L.compound + 1) + kAlongGap; };
	while (bandNeeds() > narrowestBand || alongNeeds() > nearestAlong)
	{
		if (bandNeeds() > narrowestBand && L.wadis > 1)
		{
			--L.wadis;
			context.telemetry.fallback("glacis.wadis.reduced", "A band could not hold every wadi");
		}
		else if (L.compound > leastCompound)
		{
			--L.compound;
			context.telemetry.fallback("glacis.compound.shrunk",
									   "Compounds shrank to fit their bands");
		}
		else
		{
			L.failure = "Too many colonies for this map; use a bigger map or fewer colonies.";
			return L;
		}
	}
	context.telemetry.measure("glacis.compound.actual", L.compound);
	context.telemetry.measure("glacis.wadis.actual", L.wadis);
	context.telemetry.measure("glacis.lattice.lines", int(lines.size()));
	context.telemetry.measure("glacis.band.narrowest", narrowestBand);

	// The compounds: interior labelled, wall and gates masked.
	L.compounds = CompoundMasks(n);
	for (size_t k = 0; k < L.sites.size(); ++k)
		stampCompound(t, L.sites[k], L.compound, o.wallGates, kGateWidth, int(k), L.compounds);

	// The wadis: in every band, `wadis` water lines spread evenly through the ground left between
	// the two compounds' margins, each with its bank zone either side, and fords across every
	// `fordSpacing` tiles along it (a whole number of fords round the wrap, so the pattern is
	// seamless), each band's fords offset by a draw of their own so no two bands' fords line up.
	L.water.assign(n, 0);
	L.ford.assign(n, 0);
	L.bankZone.assign(n, 0);
	L.fordLane.assign(n, 0);
	const int fordCount = std::max(1, int(std::lround(double(along) / o.fordSpacing)));
	const int bandCount = int(lines.size());
	std::vector<int> fordOffsets;
	for (int r = 0; r < bandCount; ++r)
		fordOffsets.push_back(int(context.bounded("glacis-fords", std::uint32_t(along))));
	for (int r = 0; r < bandCount; ++r)
	{
		const int start = lines[r];
		const int gap = bandCount == 1 ? across : ((lines[(r + 1) % bandCount] - start) % across + across) % across;
		const int freeFrom = L.compound + 1 + kWallMargin, freeTo = gap - freeFrom;
		const double share = double(freeTo - freeFrom) / L.wadis;
		for (int j = 0; j < L.wadis; ++j)
		{
			// The water line of wadi j, as a whole offset across the band from the compounds' line.
			const int centre = start + int(std::lround(freeFrom + (j + 0.5) * share));
			for (int f = 0; f < fordCount; ++f)
			{
				const int at = (fordOffsets[r] + int(std::int64_t(f) * along / fordCount)) % along;
				L.fords.push_back(L.vertical ? ShapePoint{double(t.x(centre)), double(at)}
											 : ShapePoint{double(at), double(t.y(centre))});
			}
			for (int a = 0; a < along; ++a)
			{
				// Where along the wadi this column lies in its ford period, in units of 1/fordCount
				// of a tile, so the pattern repeats exactly round the wrap.
				const auto inPeriod = [&](int offset)
				{
					return ((a - fordOffsets[r] - offset) % along + along) % along * fordCount % along;
				};
				const bool onFord = inPeriod(0) < kFordVertices * fordCount;
				const bool onLane =
					inPeriod(-kFordLaneMargin) < (kFordVertices + 2 * kFordLaneMargin) * fordCount;
				for (int e = -(kBankReach + 1); e <= kBankReach + 1; ++e)
				{
					const int c = centre + e;
					const int i = L.vertical ? t.at(c, a) : t.at(a, c);
					if (std::abs(e) <= kWaterVertices / 2)
					{
						L.water[i] = 1;
						L.ford[i] = onFord;
					}
					else if (std::abs(e) >= kBankNear)
						L.bankZone[i] = 1;
					if (onLane && std::abs(e) > kWaterVertices / 2)
						L.fordLane[i] = 1;
				}
			}
		}
	}
	// Nothing of a wadi may stand where a compound does (the arithmetic keeps them apart; the
	// clearing is the invariant), and the plain is whatever is left.
	L.plain.assign(n, 0);
	L.territory.assign(n, -1);
	for (int i = 0; i < n; ++i)
	{
		const bool compound = L.compounds.interiorOf[i] >= 0 || L.compounds.wall[i] || L.compounds.gate[i];
		if (compound)
			L.water[i] = L.ford[i] = L.bankZone[i] = L.fordLane[i] = 0;
		L.plain[i] = !compound && !L.water[i] && !L.bankZone[i];
		int nearest = -1, best = 0;
		for (size_t k = 0; k < L.sites.size(); ++k)
		{
			const int d = t.dist2(i % t.w, i / t.w, L.sites[k].x, L.sites[k].y);
			if (nearest < 0 || d < best)
			{
				nearest = int(k);
				best = d;
			}
		}
		L.territory[i] = nearest;
	}
	// Every compound's well, after the clearing above so it is the one water inside a wall.
	// The well by its frame tile, not its vertices: a block of vertices turned by the facing lands
	// a tile off at two of the four facings (a vertex's tile is its lower-right neighbour), which
	// the third sweep found putting a beach on the barracks' back row. baseFootprint gives the tile
	// the frame tile turns to; the well's vertices are that tile's corners.
	for (const BaseSite &site : L.sites)
	{
		const BaseFootprint wellTile = baseFootprint(site, -(L.compound - kWellInset), 0, 1, 1);
		const int corner = t.at(site.x + wellTile.dx, site.y + wellTile.dy);
		L.wells.push_back({double(corner % t.w), double(corner / t.w)});
		for (int dv = 0; dv < kWellVertices; ++dv)
			for (int du = 0; du < kWellVertices; ++du)
				L.water[t.at(corner % t.w + du, corner / t.w + dv)] = 1;
	}
	L.sketch.assign(n, GRASS);
	for (int i = 0; i < n; ++i)
		L.sketch[i] = L.water[i] ? (L.ford[i] ? SAND : WATER) : GRASS;
	context.telemetry.measure("glacis.fords.actual", int(L.fords.size()));

	// The base proved to fit its compound on the sketch as the game will see it, beaches laid (it
	// does by the arithmetic above; the check is the design's own invariant, and it is what catches
	// a well's beach reaching a building's ring).
	TerrainSketch beached = L.sketch;
	layBeaches(beached, t);
	const std::vector<unsigned char> pure = pureTiles(beached, t, GRASS);
	const std::vector<unsigned char> pureWater = pureTiles(beached, t, WATER);
	for (size_t k = 0; k < L.sites.size(); ++k)
	{
		std::vector<unsigned char> buildable(n, 0), open(n, 0);
		for (int i = 0; i < n; ++i)
		{
			buildable[i] = pure[i] && L.compounds.interiorOf[i] == int(k);
			open[i] = !pureWater[i] && L.compounds.interiorOf[i] == int(k);
		}
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

// Every colony's towers: on its own interior against its wall, scored by the plain they cover
// (its own ground before the wall and any neighbour's), none within range of a rival's swarm, and
// none closing the walk from the swarm to a gate.
TowerPlan planTowers(const Map &map, const Layout &L, const GenerationContext &context,
					 const GlacisOptions &o, const std::vector<unsigned char> &reserved)
{
	const Torus &t = L.t;
	const int n = t.size(), teams = int(L.sites.size());
	std::vector<int> owner(n, -1);
	std::vector<unsigned char> buildable(n, 0), target(n, 0);
	for (int i = 0; i < n; ++i)
	{
		const int x = i % t.w, y = i / t.w;
		const int inside = L.compounds.interiorOf[i];
		owner[i] = inside >= 0 ? inside : L.territory[i];
		target[i] = inside < 0 && !L.compounds.wall[i] && !map.isWater(x, y);
		buildable[i] = inside >= 0 && map.isGrass(x, y) && !map.isResource(x, y) &&
					   map.getBuilding(x, y) == NOGBID && !reserved[i];
	}
	TowerRequest request = startingTowerRequest(o.towerLevel, o.towerCount, kTowerPads, kTowerSpacing);
	request.otherWeight = 1;
	request.ownWeight = 1;
	request.against = &L.compounds.wall;
	return chooseTowerSites(t, owner, buildable, target, swarmSurroundings(t, context, 0), teams,
							request);
}

bool generate(Game &game, GenerationContext &context)
{
	context.stage = "glacis layout";
	const GlacisOptions o(context.request);
	const Layout L = design(context.request, context);
	if (!L.failure.empty())
	{
		context.telemetry.fallback("glacis.layout.failure", L.failure);
		context.detail = L.failure;
		return false;
	}
	Map &map = game.map;
	const Torus &t = L.t;
	const int n = t.size(), teams = context.request.nbTeams;
	for (int k = 0; k < teams; ++k)
		game.addTeam();

	context.stage = "glacis terrain";
	TerrainSketch terrain = L.sketch;
	layBeaches(terrain, t);
	writeUndermap(map, terrain);

	// The walls: stone on every wall tile. A gap (a tile the beaches spoiled) would be a breach the
	// design never meant, so it fails the candidate rather than shipping an open compound.
	context.stage = "glacis walls";
	const DesignedStone walls = designedStone(map, t, L.compounds.wall);
	if (walls.gaps)
	{
		context.detail = "a compound wall has a gap at (" + std::to_string(walls.firstGap % t.w) +
						 ", " + std::to_string(walls.firstGap / t.w) + ")";
		return false;
	}
	for (int i = 0; i < n; ++i)
		if (walls.stone[i])
			map.setResource(i % t.w, i / t.w, STONE, 1);

	context.stage = "glacis colonies";
	const BaseGarrison units = baseGarrison(o.colonists, o.garrison);
	if (!raiseBases(game, context, L.plan, L.sites, units, &L.compounds.interiorOf,
					"glacis-colonists"))
		return false;
	for (int k = 0; k < teams; ++k)
		plantBaseDepots(map, context, t, L.plan, L.sites[k]);

	// Towers against the walls, after the bases (their swarms are what the towers keep out of
	// range of, and their lists already exist) and before the crops.
	context.stage = "glacis towers";
	std::vector<unsigned char> reserved = baseSurroundings(t, L.plan, L.sites);
	{
		const std::vector<unsigned char> swarms = swarmSurroundings(t, context);
		for (int i = 0; i < n; ++i)
			reserved[i] = reserved[i] || swarms[i];
	}
	TowerPlan towers = planTowers(map, L, context, o, reserved);
	if (!settleStartingTowers(game, context, towers, o.towerLevel,
							  o.towerLevel > 0 && o.towerCount > 0, &L.compounds.gate))
		return false;
	const std::vector<unsigned char> pads = towerFootprints(t, towers);
	for (int i = 0; i < n; ++i)
		reserved[i] = reserved[i] || pads[i] || L.compounds.wall[i] || L.compounds.gate[i] ||
					  L.fordLane[i];

	context.stage = "glacis resources";
	// Each compound's kit either side of its well at the back, the same at every facing.
	for (int k = 0; k < teams; ++k)
	{
		const BaseSite &site = L.sites[k];
		const KitFrame frame{site.x, site.y, site.facing * kPi / 2};
		const double back = L.compound - kWellInset;
		const Kit kit{frame.at(-back, -kKitFlank, 4), frame.at(-back, kKitFlank, 4),
					  frame.at(0, 0, 0), kHomeWheat, kHomeWood, -1};
		plantKit(map, t, context, kit,
				 [&](int i)
				 {
					 return L.compounds.interiorOf[i] == k && !reserved[i] &&
							clearGround(map, i % t.w, i / t.w);
				 });
	}
	// The banks: crops nearest the water first (where they regrow best), wheat and wood dealt
	// into patches by a noise field unrelated to the distance so they do not form two stripes.
	std::vector<unsigned char> waterTiles(n, 0);
	for (int i = 0; i < n; ++i)
		waterTiles[i] = map.isWater(i % t.w, i / t.w);
	const std::vector<int> fromWater = stepsFrom(t, waterTiles);
	const std::vector<int> split = periodicNoise(t.w, t.h, 6, context.stream("glacis-split"));
	std::vector<std::pair<int, int>> banks;
	for (int i = 0; i < n; ++i)
		if (L.bankZone[i] && !reserved[i] && map.isGrass(i % t.w, i / t.w) &&
			clearGround(map, i % t.w, i / t.w) && fromWater[i] >= 0)
			banks.push_back({fromWater[i], i});
	std::stable_sort(banks.begin(), banks.end());
	std::vector<int> bankTiles;
	for (const auto &entry : banks)
		bankTiles.push_back(entry.second);
	const int bankCount = int(bankTiles.size());
	plantFields(map, t, bankTiles, int(scaledCount(bankCount * kBankWheatPercent / 100, o.wheat)),
				int(scaledCount(bankCount * kBankWoodPercent / 100, o.wood)),
				[&](int i) { return split[i]; });
	// The plain's outcrops, and a grove of one fruit beside every ford: the prize a ford is fought
	// over, on the bank where a unit crossing lands.
	std::vector<int> plainGround;
	for (int i = 0; i < n; ++i)
		if (L.plain[i] && !reserved[i])
			plainGround.push_back(i);
	const auto openPlain = [&](int i)
	{ return L.plain[i] && !reserved[i] && clearGround(map, i % t.w, i / t.w); };
	scatterClumps(context, t, plainGround,
				  int(scaledCount(int(plainGround.size()) / kPlainTilesPerOutcrop, o.stone)),
				  "glacis-stone", openPlain,
				  [&](MapGeneratorPoint p) { placeResourceClump(map, context, p, STONE, 1); });
	if (scaledCount(1, o.fruit) > 0)
		for (const ShapePoint &ford : L.fords)
			if (const int seed = seedNear(t, int(ford.x), int(ford.y), kBankReach + 1,
										  [&](int i)
										  {
											  return L.bankZone[i] && !reserved[i] &&
													 clearGround(map, i % t.w, i / t.w);
										  });
				seed >= 0)
				placeResourceClump(map, context, MapGeneratorPoint(seed % t.w, seed / t.w),
								   CHERRY + int(context.bounded("glacis-fruit", 3)), 1);
	seedAlgae(map, context, t, "glacis-algae", o.algae, AlgaeBand::anyWater(60));
	// The walls are designed stone: never cleared by the crop guarantee, never looked past, and
	// never eaten by the cramped-start opener (a compound full of buildings is exactly what it
	// would try to open up).
	secureStartingCrops(game, context, t, 24, 32, 0, &walls.stone);
	reopenCrampedStarts(game, context, {o.wheat, o.wood, o.stone, o.algae, o.fruit}, 24, 32, 0,
						&walls.stone);

	// Gates and fords are open ground that nothing grows over; only bank crops could close a way,
	// and the cheapest way through them is opened, never through water or stone.
	context.stage = "glacis routes";
	openColonyRoutes(map, context, t, StepCosts{1, 3, -1, -1, -1});
	return true;
}

std::string validateWorld(const Game &game, const GenerationContext &context)
{
	GenerationContext replay(context.request);
	const Layout L = design(context.request, replay);
	const GlacisOptions o(context.request);
	const Map &map = game.map;
	const Torus &t = L.t;
	const int teams = context.request.nbTeams;
	if (const std::string mismatch = designMismatch(L, map, "glacis"); !mismatch.empty())
		return mismatch;
	if (const std::string broken =
			wallStanding(map, t, L.compounds.wall, L.compounds.gate, "compound wall");
		!broken.empty())
		return broken;
	if (const std::string dry = homePondMissing(map, t, L.wells, teams, "compound", "well");
		!dry.empty())
		return dry;
	int fewestTowers = -1, mostTowers = 0;
	for (int k = 0; k < teams; ++k)
	{
		if (const std::string missing = validateBase(game, t, k, L.plan, L.sites[k], o.colonists);
			!missing.empty())
			return missing;
		int towers = 0;
		for (int slot = 0; slot < Building::MAX_COUNT; ++slot)
			if (const Building *b = game.teams[k]->myBuildings[slot];
				b && b->type->shootingRange && !b->type->isBuildingSite)
				++towers;
		fewestTowers = fewestTowers < 0 ? towers : std::min(fewestTowers, towers);
		mostTowers = std::max(mostTowers, towers);
	}
	if (teams > 0 && (fewestTowers != mostTowers || (o.towerLevel > 0 && fewestTowers < 1)))
		return "The colonies do not start with the same towers.";
	ColonyWalk walk = walkFromFirstColony(map, teams, "the glacis", "over the fords");
	if (!walk.error.empty())
		return walk.error;
	// Every colony's walk to the nearest ford it can reach - by walking, whichever band and
	// whichever gate - within a ford spacing (the fords' offsets differ band by band) plus the
	// spread of where its colonists stand of every other's. (The first sweeps measured the walk
	// to the ford nearest as the crow flies, which with one gate can lie behind the compound.)
	if (!L.fords.empty() && teams > 1)
	{
		std::vector<unsigned char> fords(t.size(), 0);
		for (int i = 0; i < t.size(); ++i)
			fords[i] = L.ford[i] && !map.isWater(i % t.w, i / t.w) && !map.isResource(i % t.w, i / t.w);
		if (const std::string uneven = unevenCosts(costsToTarget(map, teams, fords, StepCosts::walking()),
												   o.fordSpacing + kGarrisonReach, "a ford");
			!uneven.empty())
			return uneven;
	}
	return "";
}
} // namespace

GlacisOptions::GlacisOptions(const GenerationRequest &r)
	: colonists(r.option("colonists")), garrison(r.option("garrison") != 0),
	  compoundSize(r.option("compound-size")), wallGates(r.option("wall-gates")),
	  wadis(r.option("wadi-count")), fordSpacing(r.option("ford-spacing")),
	  towerLevel(r.option("starting-towers")), towerCount(r.option("tower-count")),
	  wheat(r.option("wheat-amount")), wood(r.option("wood-amount")),
	  stone(r.option("stone-amount")), algae(r.option("algae-amount")),
	  fruit(r.option("fruit-amount"))
{
}

GeneratorDefinition glacisDefinition()
{
	GeneratorDefinition d{
		"glacis",
		39,
		"The Glacis",
		1,
		false,
		// Colonists 16 to 48 (a hamlet to a city, Bases.h), 32 by default; the garrison on. A
		// compound of 16 holds a city base with room to spare; 13 is the least a hamlet needs
		// with its well, and a city grows a smaller setting to 14.
		// One wadi per band by default: a second or third makes the plain a wetland. Fords every
		// 24 tiles: a compound's width apart, so no ford is far from a gate. Level-1 towers, four
		// per compound, one for each wall.
		{{"colonists", "Colonists", 16, 48, 4, 32, ControlGroup::Layout},
		 GeneratorControl::toggle("garrison", "Garrison", true, ControlGroup::Layout),
		 {"compound-size", "Compound size", 13, 20, 1, 16, ControlGroup::Layout},
		 {"wall-gates", "Wall gates", 1, 2, 1, 2, ControlGroup::Layout},
		 {"wadi-count", "Wadi count", 1, 3, 1, 1, ControlGroup::Terrain},
		 {"ford-spacing", "Ford spacing", 16, 48, 8, 24, ControlGroup::Terrain},
		 {"starting-towers", "Starting tower level", 0, 3, 1, 1, ControlGroup::Layout},
		 {"tower-count", "Towers per colony", 1, 4, 1, 4, ControlGroup::Layout},
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
