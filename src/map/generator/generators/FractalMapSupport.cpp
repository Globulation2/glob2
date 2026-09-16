// SPDX-License-Identifier: GPL-3.0-or-later
#include "FractalMapSupport.h"
#include "Drawing.h"
#include "BalancedStarts.h"
#include "Game.h"
#include "Growth.h"
#include "Pipeline.h"
#include "Planting.h"
#include "Room.h"
#include "Settlements.h"
#include <algorithm>
#include <climits>
#include <numeric>
namespace FractalMaps
{
namespace
{
// Opening-game probes found food-limited colonies while their first inn upgraded.
// Move the swarm six tiles toward its contained wheat instead of enlarging crops
// into the building court. The full swarm still fits inside the 24x24 court; the
// southern timber plot stays reachable and is rechecked after workers are placed.
// This is an economic tuning offset, not a change to the neutral site selector.
constexpr int kStarterNorthOffset = 6;
} // namespace

void initialize(Layout &L, const GenerationRequest &r)
{
	// Guard before shifting or allocating, including calls made outside the catalog.
	if (r.wDec < 7 || r.wDec > 9 || r.hDec < 7 || r.hDec > 9 || r.nbTeams < 2 || r.nbTeams > 8)
	{
		L.failure = "Fractal maps support 128–512 tiles per axis and 2–8 colonies.";
		return;
	}
	L.t = Torus(1 << r.wDec, 1 << r.hDec);
	L.terrain.assign(L.t.size(), GRASS);
	L.reserved.assign(L.t.size(), 0);
	L.wheat = L.wood = L.objectives = L.crossings = L.growthRestricted = L.reserved;
}
bool reserveHomes(Layout &L, GenerationContext &context, const std::vector<Home> &preferred,
				  GenerationTelemetry *observations)
{
	const auto &t = L.t;
	// Hilbert retries use fresh deterministic search streams at each uniform order.
	// Send their existing counters to the parent report without sharing RNG state.
	auto &telemetry = observations ? *observations : context.telemetry;
	std::vector<Home> candidates;
	std::vector<Home> offered = preferred;
	if (offered.empty())
		for (int y = 0; y < t.h; y += 8)
			for (int x = 0; x < t.w; x += 8)
				offered.push_back({x, y});
	// Whole modules, including their external lane and beach margin, must be grass before
	// reservation. This rejects impossible layouts instead of erasing a fold after generation.
	for (Home site : offered)
	{
		const int x = site.x, y = site.y;
		bool fits = true;
		for (int dy = -kHomeHalf; fits && dy <= kHomeHalf; ++dy)
			for (int dx = -kHomeHalf; fits && dx <= kHomeHalf; ++dx)
				fits = L.terrain[t.at(x + dx, y + dy)] == GRASS;
		if (fits)
			candidates.push_back({x, y});
	}
	// Seeded traversal changes which equally roomy perimeter/pocket wins without introducing
	// sub-tile jitter that could turn a valid home into an occasional seed-dependent rejection.
	context.shuffle(candidates.begin(), candidates.end(), "fractal-home-search");
	std::vector<int> tiles;
	for (Home h : candidates)
		tiles.push_back(t.at(h.x, h.y));
	const auto selected =
		selectSeparatedSites(t, tiles, context.request.nbTeams, 2 * kHomeHalf + 4);
	std::vector<Home> best;
	for (int index : selected.indices)
		best.push_back(candidates[index]);
	telemetry.measure("fractal.homes.search-attempts", selected.attempts);
	telemetry.measure("fractal.homes.search-relaxation", 0);
	telemetry.measure("fractal.homes.candidates", candidates.size());
	telemetry.measure("fractal.homes.fitted", best.size());
	if (best.size() != size_t(context.request.nbTeams))
	{
		L.failure = "Cannot fit " + std::to_string(context.request.nbTeams) +
					" separated 56×56 home/farm modules on this terrain; enlarge the map or reduce "
					"colonies/folds/lake size.";
		return false;
	}
	// One spare module, when it fits without changing the terrain, lets the finished-
	// world scorer reject an exposed/poorly connected site. It becomes a real expansion
	// farm and building court, not unused candidate debris. Never lower recursion just
	// to obtain this optional spare; the requested colony count is the hard fit target.
	for (Home p : candidates)
	{
		bool separate = true;
		for (Home h : best)
			separate &= t.chebyshev(p.x, p.y, h.x, h.y) >= 2 * kHomeHalf + 4;
		if (separate)
		{
			best.push_back(p);
			break;
		}
	}
	telemetry.measure("fractal.homes.spare", int(best.size()) - context.request.nbTeams);
	L.homes = best;
	for (Home h : L.homes)
		fillRectangle(L.reserved, t, {h.x - 30, h.y - 30, h.x + 31, h.y + 31}, 1);
	return true;
}
bool overlapsHome(const Layout &L, RegionBounds bounds, int margin)
{
	for (int y = bounds.y0 - margin; y < bounds.y1 + margin; ++y)
		for (int x = bounds.x0 - margin; x < bounds.x1 + margin; ++x)
			if (L.reserved[L.t.at(x, y)])
				return true;
	return false;
}
void layHomeEconomies(Layout &L)
{
	const auto &t = L.t;
	for (Home h : L.homes)
	{
		// Construction ground is 24×24 pure tiles (25×25 corners), surrounded by two
		// corners of sand. The ring stops resource spread while remaining walkable;
		// its northern opening is the persistently protected service apron below.
		fillRectangle(L.terrain, t, {h.x - 14, h.y - 12, h.x + 15, h.y + 17}, SAND);
		fillRectangle(L.terrain, t, {h.x - 12, h.y - 10, h.x + 13, h.y + 15}, GRASS);
		// A long wheat frontage lets several workers harvest without queuing at a single
		// deposit. Water is behind the crops, the home is in front. Crop plots have their
		// own sand cap, so renewable supplies cannot engulf the construction district.
		fillRectangle(L.terrain, t, {h.x - 26, h.y - 28, h.x + 27, h.y - 12}, SAND);
		fillRectangle(L.terrain, t, {h.x - 24, h.y - 26, h.x + 25, h.y - 22}, WATER);
		fillRectangle(L.terrain, t, {h.x - 24, h.y - 20, h.x + 25, h.y - 14}, GRASS);
		fillRectangle(L.wheat, t, {h.x - 23, h.y - 19, h.x + 24, h.y - 15}, 1);
		// Keep timber in the two outer bays beside the wheat. Saved-state probes
		// showed an inn could be 26 walking tiles from the southern timber plot:
		// its sole feeding service closed for an upgrade, then hungry workers
		// could not finish the long wood deliveries. Nearby wood shortens that
		// construction trip without giving free buildings or changing AI policy.
		// The existing two-corner sand aisles separate these bays from the two
		// inner wheat beds, so later forest growth cannot replace their grain.
		// The union of crop masks is unchanged: protected bridge geometry and the
		// 24x24 court do not move. Southern timber remains the bulk expansion plot.
		for (const RegionBounds bay : {RegionBounds{h.x - 23, h.y - 19, h.x - 16, h.y - 15},
									   RegionBounds{h.x + 18, h.y - 19, h.x + 24, h.y - 15}})
		{
			fillRectangle(L.wheat, t, bay, 0);
			fillRectangle(L.wood, t, bay, 1);
		}
		// Wood has a separate, narrower plot: enough renewable timber for upgrades, but
		// not so much forest that it competes with food or forces clearing before building.
		fillRectangle(L.terrain, t, {h.x - 26, h.y + 17, h.x + 27, h.y + 29}, SAND);
		fillRectangle(L.terrain, t, {h.x - 24, h.y + 25, h.x + 25, h.y + 27}, WATER);
		fillRectangle(L.terrain, t, {h.x - 24, h.y + 19, h.x + 25, h.y + 23}, GRASS);
		fillRectangle(L.wood, t, {h.x - 23, h.y + 20, h.x + 24, h.y + 22}, 1);
		// Crossing each crop frontage with three pure-sand aisles limits patch width and
		// guarantees access even after every eligible crop tile has filled in.
		for (int offset : {-16, 0, 16})
		{
			fillRectangle(L.terrain, t, {h.x + offset, h.y - 28, h.x + offset + 2, h.y - 12}, SAND);
			fillRectangle(L.terrain, t, {h.x + offset, h.y + 17, h.x + offset + 2, h.y + 29}, SAND);
		}
		// Food-service buildings need to hug the grain, especially policies that
		// require an upgraded inn footprint within one tile of harvestable wheat.
		// A multi-corner sand barrier cannot satisfy that distance. Join a grass
		// apron to the main court and protect its TILE footprint with the existing
		// saved no-growth flag instead. Five clear rows fit a 3x3 inn (or a 4x4
		// swarm) and access; the 24x24 main court remains available below it.
		// The flag is installed after rasterization, never by clearing grown crops.
		fillRectangle(L.terrain, t, {h.x - 12, h.y - 15, h.x + 13, h.y - 9}, GRASS);
		fillRectangle(L.growthRestricted, t, {h.x - 12, h.y - 15, h.x + 12, h.y - 10});
		// The module's outer edge used to be a crisp rectangle, so every colony on both maps
		// opened inside the same stamped yellow box. Fray it: each tile along the outside of
		// the cap may push a few tiles further out over open grass. Sand is only ever added,
		// and only inside the reserved 61x61 the home already owns, so every containment the
		// plots depend on is exactly as it was and nothing else can have claimed that ground.
		// The pattern comes from the home's own coordinates, so it varies with the seed and
		// between colonies without drawing from any random stream.
		const auto fray = [&](int x, int y, int dx, int dy)
		{
			const unsigned hash = unsigned(x * 73856093) ^ unsigned(y * 19349663);
			// One or two tiles on about two thirds of the perimeter. Sand is not building
			// ground, so a deeper fray eats the expansion anchors every module is checked
			// for: at four tiles it cost eight-colony 256 maps their ninth module.
			for (int step = 1; step <= int(hash % 3u); ++step)
			{
				const int i = t.at(x + dx * step, y + dy * step);
				if (std::abs(x + dx * step - h.x) > 29 || std::abs(y + dy * step - h.y) > 29 ||
					L.terrain[i] != GRASS)
					break;
				L.terrain[i] = SAND;
			}
		};
		for (int x = h.x - 26; x < h.x + 27; ++x)
		{
			fray(x, h.y - 28, 0, -1);
			fray(x, h.y + 28, 0, 1);
		}
		for (int y = h.y - 28; y < h.y + 29; ++y)
		{
			fray(h.x - 26, y, -1, 0);
			fray(h.x + 26, y, 1, 0);
		}
	}
	layBeaches(L.terrain, t);
}
namespace
{
bool stampBankFarm(Layout &L, RegionBounds b, bool timber)
{
	// A small expansion plot borrows irrigation from the lake/river rather than adding
	// another artificial channel. Its sand rim contains all later crop spread; the
	// surrounding grass remains available for buildings and a two-way gathering lane.
	for (int y = b.y0 - 3; y < b.y1 + 3; ++y)
		for (int x = b.x0 - 3; x < b.x1 + 3; ++x)
		{
			const int i = L.t.at(x, y);
			if (L.terrain[i] != GRASS || L.reserved[i] || L.crossings[i] || L.wheat[i] || L.wood[i])
				return false;
		}
	fillRectangle(L.terrain, L.t, b, SAND);
	RegionBounds crops{b.x0 + 2, b.y0 + 2, b.x1 - 2, b.y1 - 2};
	fillRectangle(L.terrain, L.t, crops, GRASS);
	// One corner less on the far sides: planting is on pure tiles, not corners.
	--crops.x1;
	--crops.y1;
	fillRectangle(timber ? L.wood : L.wheat, L.t, crops, 1);
	return true;
}
} // namespace

bool bankFarm(Layout &L, RegionBounds b, bool timber)
{
	// The first choice of box is often blocked by one corner of a neighbouring plot, a
	// crossing's shoulder or a home module, and refusing there left the bank bare: Hilbert
	// placed 12 of the 30 plots it proposed and Sierpiński offered six in total, which is
	// most of why these two maps measured barren beside every other landscape (2026-09-16).
	// So slide along the bank before giving up, then try the same positions two tiles
	// smaller. Deterministic, order-independent, and every candidate still has to be clean
	// unreserved grass: this widens the search, it never weakens what a plot may sit on.
	const bool horizontal = b.x1 - b.x0 >= b.y1 - b.y0;
	for (const int shrink : {0, 2})
	{
		const RegionBounds inset{b.x0 + shrink, b.y0 + shrink, b.x1 - shrink, b.y1 - shrink};
		if (inset.x1 - inset.x0 < 8 || inset.y1 - inset.y0 < 8)
			break;
		for (const int slide : {0, -5, 5, -10, 10, -15, 15})
		{
			const int dx = horizontal ? slide : 0, dy = horizontal ? 0 : slide;
			if (stampBankFarm(L, {inset.x0 + dx, inset.y0 + dy, inset.x1 + dx, inset.y1 + dy},
							  timber))
				return true;
		}
	}
	return false;
}

int gardenBeds(Layout &L, GenerationContext &context, int spacing, int half, int margin)
{
	const auto &t = L.t;
	// A bed is the home module's idea at one quarter the size: a square pool, a ring of crops
	// the pool waters, and a sand cap that contains them. It needs `margin` of clean grass all
	// round so the walk past it survives; on Hilbert that margin is most of a pocket between
	// two folds, which is why its beds are small. A bed that does not fit is simply not placed,
	// never forced, and nothing here may touch a home module, a crossing or an objective court.
	const auto clear = [&](RegionBounds b)
	{
		for (int y = b.y0 - margin; y < b.y1 + margin; ++y)
			for (int x = b.x0 - margin; x < b.x1 + margin; ++x)
			{
				const int i = t.at(x, y);
				if (L.terrain[i] != GRASS || L.reserved[i] || L.crossings[i] || L.objectives[i] ||
					L.wheat[i] || L.wood[i])
					return false;
			}
		return true;
	};
	int beds = 0, water = 0, crops = 0;
	for (int cy = spacing / 2; cy < t.h; cy += spacing)
		for (int cx = spacing / 2; cx < t.w; cx += spacing)
		{
			// Jitter keeps the beds off a perfect grid without letting them wander into each
			// other; the lattice itself is what makes them read as part of the design.
			const int jx = int(context.bounded("garden-beds", spacing / 3)) - spacing / 6;
			const int jy = int(context.bounded("garden-beds", spacing / 3)) - spacing / 6;
			const int x = cx + jx, y = cy + jy;
			const RegionBounds bed{x - half, y - half, x + half, y + half};
			if (!clear(bed))
				continue;
			// Sand cap, crop ring, pool. Three tiles of grass between the pool and the cap:
			// the crops, and a lane to walk along them. The pool takes the rest, because the
			// water these maps were missing has to come from somewhere other than widening
			// the one channel their geometry is built around — at seven tiles instead of six
			// the Hilbert river left no room for a crossing court on a 128 map at all.
			fillRectangle(L.terrain, t, bed, SAND);
			const RegionBounds ring{bed.x0 + 2, bed.y0 + 2, bed.x1 - 2, bed.y1 - 2};
			fillRectangle(L.terrain, t, ring, GRASS);
			const RegionBounds pool{bed.x0 + 5, bed.y0 + 5, bed.x1 - 5, bed.y1 - 5};
			fillRectangle(L.terrain, t, pool, WATER);
			// Alternate the beds between food and timber so neither crop can be cornered by
			// taking one part of the map, and so a bed is worth walking to from either home.
			auto &crop = beds % 3 == 2 ? L.wood : L.wheat;
			for (int py = ring.y0; py < ring.y1 - 1; ++py)
				for (int px = ring.x0; px < ring.x1 - 1; ++px)
				{
					const int i = t.at(px, py);
					if (L.terrain[i] != GRASS)
						continue;
					crop[i] = 1;
					++crops;
				}
			water += (pool.x1 - pool.x0) * (pool.y1 - pool.y0);
			++beds;
		}
	context.telemetry.measure("fractal.beds.placed", beds);
	context.telemetry.measure("fractal.beds.water-tiles", water);
	context.telemetry.measure("fractal.beds.crop-tiles", crops);
	return beds;
}

void stampCrossings(Layout &L, const CrossingSelection &selection, GenerationContext &context)
{
	for (size_t k = 0; k < selection.selected.size(); ++k)
	{
		const auto &c = selection.selected[k];
		// Seven undermap corners across yields at least six traversable tiles. Sand makes
		// the crossing permanent against crop spread; shoulders meet the existing banks.
		strokePath(L.crossings, L.t, {{c.from.x, c.from.y, 3.5}, {c.to.x, c.to.y, 3.5}});
		context.telemetry.measure("fractal.crossing.benefit-estimate", selection.benefits[k], c.id);
		context.telemetry.measure("fractal.crossing.level", c.level, c.id);
	}
	for (int i = 0; i < L.t.size(); ++i)
		if (L.crossings[i])
			L.terrain[i] = SAND;
	context.telemetry.measure("fractal.crossings.mandatory", selection.mandatory);
	context.telemetry.measure("fractal.crossings.local", selection.local);
	context.telemetry.measure("fractal.crossings.major", selection.major);
	context.telemetry.measure("fractal.crossings.local-shortfall", selection.localShortfall);
	context.telemetry.measure("fractal.crossings.major-shortfall", selection.majorShortfall);
}

bool furnishAndSettle(Game &game, GenerationContext &context, const Layout &L)
{
	const auto &t = L.t;
	Map &map = game.map;
	context.stage = "fractal terrain and contained farms";
	writeUndermap(map, L.terrain);
	const int protectedTiles = preventResourceGrowth(map, L.growthRestricted);
	if (protectedTiles < 0)
	{
		context.detail = "Invalid farm service-court growth mask.";
		return false;
	}
	context.telemetry.measure("fractal.homes.growth-protected-tiles", protectedTiles);

	const int wheatAmount = context.request.option("wheat-amount"),
			  woodAmount = context.request.option("wood-amount");
	// Starter minima live only on their contained renewable plots. Amount controls change
	// how fully those plots begin stocked; no global emergency planter may create crops on
	// the home or cut an undesigned route. Later growth can fill the same fixed plot area.
	for (int i = 0; i < t.size(); ++i)
	{
		if (!map.isGrass(i % t.w, i / t.w))
			continue;
		const int type = L.wheat[i] ? WHEAT : L.wood[i] ? WOOD : -1;
		if (type < 0)
			continue;
		const int amount = type == WHEAT ? wheatAmount : woodAmount;
		const int density =
			L.reserved[i] ? std::min(100, 50 + amount / 6) : std::min(100, amount / 3);
		if (int(context.bounded("fractal-farm-stock", 100)) < density)
			map.setResource(i % t.w, i / t.w, type, 1);
	}
	for (Home h : L.homes)
	{
		// Quarry is outside both farm plots and the central building district. A 2×3 patch
		// has multiple gathering edges but does not make a wall. This opening minimum is
		// deliberately retained at zero ambient stone, like the renewable food and wood.
		for (int y = -3; y < 0; ++y)
			for (int x = 20; x < 22; ++x)
				map.setResource(t.x(h.x + x), t.y(h.y + y), STONE, 1);
	}
	// Ambient deposits stay in designated objective courts. This prevents high resource
	// sliders from turning the map's open land lanes into transient resource walls.
	for (int i = 0; i < t.size(); ++i)
	{
		if (!L.objectives[i] || L.reserved[i] || L.crossings[i] ||
			!clearGround(map, i % t.w, i / t.w))
			continue;
		const int x = i % t.w, y = i / t.w;
		if (x % 8 >= 3 || y % 8 >= 3)
			continue; // permanent gathering lanes between 3×3 patches
		const int kind = ((x / 8) + (y / 8)) % 4;
		const int type = kind < 3 ? CHERRY + kind : STONE;
		const int amount = context.request.option(kind < 3 ? "fruit-amount" : "stone-amount");
		if (int(context.bounded("fractal-objectives", 300)) < amount)
			map.setResource(x, y, type, 1);
	}
	// Outside those courts the open land carried nothing at all, which is most of why these
	// two maps measured barren beside every other landscape: a quarter the resource tiles of
	// the median map, and three quarters of the ground bare (2026-09-16). It now carries
	// deposits on the same 8-lattice the courts use, so 3x3 patches always alternate with
	// permanent gathering lanes and no slider can build a wall across the land. Only timber,
	// fruit and stone, and timber and fruit only where the ground cannot grow them back: a
	// renewable patch out here would spread into the routes the design promises, which is
	// what the courts-only rule was protecting. Food stays inside the contained plots.
	std::vector<unsigned char> ambient(t.size(), 0);
	int ambientTiles = 0;
	for (int i = 0; i < t.size(); ++i)
	{
		const int x = i % t.w, y = i / t.w;
		if (L.objectives[i] || L.reserved[i] || L.crossings[i] || L.wheat[i] || L.wood[i])
			continue;
		// 3x3 patches with five clear tiles between them, the same lattice the objective
		// courts use: a patch costs building anchors as well as giving resources, and the
		// gaps are what keep a module's expansion room and the walking lanes open.
		if (x % 8 >= 3 || y % 8 >= 3 || !clearGround(map, x, y))
			continue;
		const int kind = ((x / 8) * 3 + (y / 8) * 5) % 8;
		const int type = kind < 3 ? STONE : kind < 6 ? WOOD : CHERRY + (kind - 6);
		const int amount = context.request.option(
			type == STONE ? "stone-amount" : type == WOOD ? "wood-amount" : "fruit-amount");
		if (int(context.bounded("fractal-ambient", 100)) >= amount)
			continue;
		map.setResource(x, y, type, 1);
		ambient[i] = 1;
		++ambientTiles;
	}
	// These are finite: the engine's saved no-growth flag holds every ambient patch where it
	// was put, so a slider cannot grow one into the lanes between them, and the renewable
	// economy stays where the design contains it — the home plots, the bank plots and the
	// beds. Out here a patch is a thing you go and take, not a thing that spreads.
	if (preventResourceGrowth(map, ambient) < 0)
	{
		context.detail = "Invalid ambient deposit mask.";
		return false;
	}
	context.telemetry.measure("fractal.ambient.deposit-tiles", ambientTiles);
	seedAlgae(map, context, t, "fractal-algae", context.request.option("algae-amount"),
			  AlgaeBand::anyWater(100));

	context.stage = "fractal finished-terrain start evaluation";
	context.telemetry.measure("fractal.homes.starter-north-offset", kStarterNorthOffset);
	const auto open = groundUnitTiles(map);
	const auto anchors = buildAnchors(t, buildableTiles(map));
	const auto fertility = Fertility::forMap(map, false);
	const auto swimming = groundUnitTiles(map, true);
	std::vector<double> quality(L.homes.size(), 0);
	std::vector<std::vector<int>> contacts(L.homes.size(), std::vector<int>(L.homes.size(), -1));
	std::vector<int> order(L.homes.size());
	std::iota(order.begin(), order.end(), 0);
	// Evaluate actual resources, buildability and expansion before the team deal. Reservations
	// already guarantee geometry; this is a hard viability check, not a seed-selection retry.
	for (size_t h = 0; h < L.homes.size(); ++h)
	{
		const Home home = L.homes[h];
		const auto walk =
			floodFrom(t, tileMask(t, {t.at(home.x, home.y - kStarterNorthOffset)}), open);
		const auto swim =
			stepsFrom(t, tileMask(t, {t.at(home.x, home.y - kStarterNorthOffset)}), swimming);
		for (size_t other = 0; other < L.homes.size(); ++other)
		{
			const Home target = L.homes[other];
			const int tile = t.at(target.x, target.y - kStarterNorthOffset);
			contacts[h][other] = walk.steps[tile];
			if (walk.steps[tile] < 0 || swim[tile] < 0)
			{
				context.detail = "A candidate home is disconnected on finished terrain.";
				return false;
			}
			context.telemetry.measure("fractal.home.walk-contact", walk.steps[tile], int(h));
			context.telemetry.measure("fractal.home.swim-contact", swim[tile], int(h));
		}
		auto frontage = resourceFrontages(map, walk, 48, &fertility);
		const int wheat = frontage[WHEAT].edges, wood = frontage[WOOD].edges;
		const int quarry = frontage[STONE].edges;
		const int renewableWheat = frontage[WHEAT].renewableEdges;
		const int renewableWood = frontage[WOOD].renewableEdges;
		int expansion = 0;
		for (int i : walk.visited)
			if (walk.steps[i] <= 48 && !L.reserved[i] && anchors[i])
				++expansion;
		context.telemetry.measure("fractal.home.wheat-frontage", wheat, int(h));
		context.telemetry.measure("fractal.home.wood-frontage", wood, int(h));
		context.telemetry.measure("fractal.home.expansion-anchors", expansion, int(h));
		context.telemetry.measure("fractal.home.renewable-wheat-frontage", renewableWheat, int(h));
		context.telemetry.measure("fractal.home.renewable-wood-frontage", renewableWood, int(h));
		// Saturation points are tuning weights, not resource guarantees: food
		// gets a broader frontage target (100 edges) than timber (60), while
		// 500 expansion anchors is roughly one generous nearby meadow. Multiple
		// edges can border the same crop; this measures harvesting frontage.
		quality[h] = std::min(1.0, renewableWheat / 100.0) + std::min(1.0, renewableWood / 60.0) +
					 std::min(1.0, expansion / 500.0);
		if (!wheat || !wood || !quarry || !renewableWheat || !renewableWood || expansion < 16)
		{
			context.detail = "Home " + std::to_string(h) +
							 " lacks reachable food, wood, quarry or expansion room.";
			return false;
		}
	}
	// Exhaust the at-most-nine ways to omit the spare. Maximize the weakest finished
	// economy, then penalize uneven contact distances. Saturated room/resource terms
	// prevent a vast empty meadow from compensating for poor food access. These are
	// bounded design heuristics, not a claim of statistical fairness or human fun.
	if (order.size() > size_t(context.request.nbTeams))
	{
		int omitted = -1;
		double bestScore = -1e30;
		for (size_t skip = 0; skip < order.size(); ++skip)
		{
			double worst = 3;
			int closest = INT_MAX, longest = 0;
			for (size_t a = 0; a < order.size(); ++a)
				if (a != skip)
				{
					worst = std::min(worst, quality[a]);
					int contact = INT_MAX;
					for (size_t b = 0; b < order.size(); ++b)
						if (b != a && b != skip)
							contact = std::min(contact, contacts[a][b]);
					closest = std::min(closest, contact);
					longest = std::max(longest, contact);
				}
			// A 128-step disparity costs one whole saturated economy term. Use
			// tile distances rather than map percentages so rectangles and seam
			// routes are judged by the journey workers actually have to make.
			const double score = worst - (longest - closest) / 128.0;
			if (score > bestScore)
			{
				bestScore = score;
				omitted = int(skip);
			}
		}
		context.telemetry.measure("fractal.homes.omitted-site", omitted);
		order.erase(order.begin() + omitted);
	}
	context.shuffle(order.begin(), order.end(), "fractal-team-deal");
	for (int team = 0; team < context.request.nbTeams; ++team)
	{
		game.addTeam();
		const Home h = L.homes[order[team]];
		std::vector<unsigned char> home(t.size(), 0);
		fillRectangle(home, t, {h.x - 12, h.y - 10, h.x + 12, h.y + 14}, 1);
		if (!placeSettlement(game, context, team, home,
							 {t.x(h.x - 2), t.y(h.y - 2 - kStarterNorthOffset)},
							 "fractal-settlements"))
			return false;
		context.telemetry.measure("fractal.home.team", team, order[team]);
	}
	return true;
}
std::string validate(const Game &game, const GenerationContext &context, const Layout &L)
{
	if (const auto error = designMismatch(L, game.map, "fractal map"); !error.empty())
		return error;
	const Map &map = game.map;
	const auto &t = L.t;
	const auto walk = walkFromFirstColony(map, context.request.nbTeams, "fractal land",
										  "over the designed routes");
	if (!walk.error.empty())
		return walk.error;
	const auto finishedOpen = groundUnitTiles(map);
	for (size_t team = 0; team < walk.workers.size(); ++team)
	{
		const auto access = floodFrom(t, tileMask(t, walk.workers[team]), finishedOpen, 32);
		auto frontage = resourceFrontages(map, access, 32);
		if (!frontage[WHEAT].edges || !frontage[WOOD].edges || !frontage[STONE].edges)
			return "Colony " + std::to_string(team) +
				   " lost opening resource access after settlement.";
	}
	// Over-approximate all future crop spread by flooding every pure-grass tile from
	// today's wheat/wood. Ignore buildings in this flood: even demolition must not let
	// crops enter a home court. Sand caps must contain the components geometrically.
	const auto spread = cropSpreadEnvelope(map).steps;
	for (Home h : L.homes)
		for (int y = -10; y < 14; ++y)
			for (int x = -12; x < 12; ++x)
				if (spread[t.at(h.x + x, h.y + y)] >= 0)
					return "A farm's grass component can spread into its home construction court.";
	// Validate complete nonoverlapping 4×4 buildings on a six-tile grid, with two-tile
	// circulation between them. Units can move away; buildings and deposits cannot. This
	// is a feasible arrangement, rather than counting overlapping placement anchors.
	const auto buildable = potentialBuildingTiles(map);
	for (size_t k = 0; k < L.homes.size(); ++k)
	{
		const Home h = L.homes[k];
		const BuildingGrid grid{{h.x - 12, h.y - 10, h.x + 12, h.y + 14}, 4, 4, 2, 1};
		const auto arrangement =
			arrangeBuildingGrid(t, buildable, finishedOpen, grid, {t.at(h.x - 13, h.y)});
		if (!arrangement.failure.empty() || arrangement.footprints.size() < 10)
			return "Home " + std::to_string(k) +
				   " cannot fit ten separate buildings with circulation: " + arrangement.failure;
		// Barracks/swimming training buildings grow from 4x4 to 6x6 around
		// their engine-defined centre (BuildingTypesUpgrade.cpp, decLeft/Top=-3).
		// Check an alternative layout reserving the full upgraded envelopes, not
		// ten 4x4 anchors whose two-tile gaps upgrades would consume. Six such
		// cells can host a mixed core of inns, schools and training buildings;
		// smaller initial footprints stay inside their reserved future envelopes.
		const BuildingGrid serviceGrid{{h.x - 13, h.y - 16, h.x + 13, h.y - 10}, 3, 3, 2, 1};
		const auto service = arrangeBuildingGrid(t, buildable, finishedOpen, serviceGrid,
												 {t.at(h.x - 13, h.y - 12)});
		if (!service.failure.empty() || service.footprints.size() < 2)
			return "Home " + std::to_string(k) + " lacks two accessible grown-inn service pads.";
		const BuildingGrid upgradeGrid{grid.bounds, 6, 6, 2, 1};
		const auto upgrades =
			arrangeBuildingGrid(t, buildable, finishedOpen, upgradeGrid, {t.at(h.x - 13, h.y)});
		if (!upgrades.failure.empty() || upgrades.footprints.size() < 6)
			return "Home " + std::to_string(k) +
				   " cannot fit six complete upgrade envelopes with circulation: " +
				   upgrades.failure;
	}
	for (int i = 0; i < t.size(); ++i)
	{
		if (L.crossings[i] && map.isWater(i % t.w, i / t.w))
			return "A designed crossing was lost at (" + std::to_string(i % t.w) + "," +
				   std::to_string(i / t.w) + ") during terrain rasterization.";
		if (L.growthRestricted[i] &&
			(map.canResourcesGrow(i % t.w, i / t.w) || map.isResource(i % t.w, i / t.w)))
			return "A farm service court lost its persistent crop protection.";
	}
	return "";
}
std::vector<GeneratorControl> resourceControls()
{
	return {GeneratorControl::percentage("wheat-amount", "Wheat amount"),
			GeneratorControl::percentage("wood-amount", "Wood amount"),
			GeneratorControl::percentage("stone-amount", "Stone amount"),
			GeneratorControl::percentage("algae-amount", "Algae amount"),
			GeneratorControl::percentage("fruit-amount", "Fruit amount")};
}
} // namespace FractalMaps
