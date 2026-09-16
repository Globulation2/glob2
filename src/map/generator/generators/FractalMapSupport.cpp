// SPDX-License-Identifier: GPL-3.0-or-later
#include "FractalMapSupport.h"
#include "Drawing.h"
#include "BalancedStarts.h"
#include "Game.h"
#include "Growth.h"
#include "Morphology.h"
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
// timber bays beside the wheat stay reachable and are rechecked after workers are placed.
// This is an economic tuning offset, not a change to the neutral site selector.
constexpr int kStarterNorthOffset = 6;
// A tile the design has already given a purpose: a home module, a crossing, an objective court
// or a crop plot. Nothing laid later — a bed, a copse, a quarry, a spot of shore wheat — may sit on it.
bool claimed(const Layout &L, int i)
{
	return L.reserved[i] || L.crossings[i] || L.objectives[i] || L.wheat[i] || L.wood[i];
}
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
	L.wheat = L.wood = L.objectives = L.crossings = L.wheatShore = L.reserved;
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
const char *homeDesignName(HomeDesign design)
{
	static const char *const names[] = {"parterre", "horseshoe", "cloister", "four-beds"};
	return names[int(design)];
}
bool layHomeEconomies(Layout &L, GenerationContext &context)
{
	const auto &t = L.t;
	// One garden per map, drawn at every home: the design sets how much water waters each crop
	// and how far a worker walks to it, so colonies on one map must share it, but a map that
	// always drew the same stamp read as monotonous across games (maintainer review 2026-09-16).
	L.homeDesign = HomeDesign(context.bounded("fractal-home-design", unsigned(HomeDesign::Count)));
	context.telemetry.choice("fractal.homes.design", homeDesignName(L.homeDesign));
	for (Home h : L.homes)
	{
		// Every rectangle below is relative to the home's centre, half-open like fillRectangle.
		const auto at = [&](int x0, int y0, int x1, int y1)
		{ return RegionBounds{h.x + x0, h.y + y0, h.x + x1, h.y + y1}; };
		const auto paint = [&](TerrainType type, int x0, int y0, int x1, int y1)
		{ fillRectangle(L.terrain, t, at(x0, y0, x1, y1), type); };
		const auto crop = [&](bool timber, int x0, int y0, int x1, int y1)
		{
			fillRectangle(L.wheat, t, at(x0, y0, x1, y1), !timber);
			fillRectangle(L.wood, t, at(x0, y0, x1, y1), timber);
		};
		// A pure-sand aisle through a crop bed limits patch width and guarantees access even after
		// every eligible crop tile has filled in. Aisles stop at the crops: run through the water
		// as well and one long canal becomes several short ponds, which waters the same crops
		// less and reads as a sand grid.
		const auto clearCrops = [&](int x0, int y0, int x1, int y1)
		{
			fillRectangle(L.wheat, t, at(x0, y0, x1, y1), 0);
			fillRectangle(L.wood, t, at(x0, y0, x1, y1), 0);
		};
		const auto aisle = [&](int x0, int y0, int x1, int y1)
		{
			paint(SAND, x0, y0, x1, y1);
			clearCrops(x0, y0, x1, y1);
		};
		// A square bed: a sand cap two corners wide, grass inside it, and a pool whose beach the
		// shoreline pass takes out of the innermost grass row, leaving crops all round it.
		const auto bed = [&](bool timber, int x0, int y0, int side)
		{
			paint(SAND, x0, y0, x0 + side, y0 + side);
			paint(GRASS, x0 + 2, y0 + 2, x0 + side - 2, y0 + side - 2);
			crop(timber, x0 + 2, y0 + 2, x0 + side - 3, y0 + side - 3);
			paint(WATER, x0 + 6, y0 + 6, x0 + side - 6, y0 + side - 6);
			clearCrops(x0 + 5, y0 + 5, x0 + side - 5, y0 + side - 5);
		};
		// Construction ground is 24×24 pure tiles (25×25 corners), surrounded by two
		// corners of sand. The ring stops resource spread while remaining walkable;
		// its northern side opens onto the service apron below.
		switch (L.homeDesign)
		{
		case HomeDesign::Parterre:
		{
			// A long wheat frontage lets several workers harvest without queuing at a single
			// deposit. Water is behind the crops, the home is in front. Crop plots have their
			// own sand cap, so renewable supplies cannot engulf the construction district.
			// Two corners of sand on every side of each water strip, matching the flanks and the
			// width of a garden path: a one-corner rim on the water side read as a thinner,
			// notched border.
			paint(SAND, -26, -29, 27, -12);
			paint(WATER, -24, -27, 25, -22);
			paint(GRASS, -24, -20, 25, -14);
			crop(false, -23, -19, 24, -15);
			// Keep timber in the two outer bays beside the wheat. Saved-state probes
			// showed an inn could be 26 walking tiles from the southern timber plot:
			// its sole feeding service closed for an upgrade, then hungry workers
			// could not finish the long wood deliveries. Nearby wood shortens that
			// construction trip without giving free buildings or changing AI policy.
			// The two-corner sand aisles separate these bays from the two inner wheat
			// beds, so later forest growth cannot replace their grain.
			crop(true, -23, -19, -16, -15);
			crop(true, 18, -19, 24, -15);
			// The southern plot is wheat too: a home's renewable timber is the two bays above,
			// and the rest comes from copses and timber bank plots out on the map, so a colony
			// that wants to build big has a reason to leave home. Five rows of water, matching
			// the northern strip: two rows fed the plot slowly and left the south of every
			// module looking dry. Crops regrow at a rate set by how much water is near them, so
			// the width of this strip is the plot's supply.
			paint(SAND, -26, 17, 27, 31);
			paint(WATER, -24, 24, 25, 29);
			paint(GRASS, -24, 19, 25, 24);
			crop(false, -23, 20, 24, 23);
			for (int offset : {-16, 0, 16})
			{
				aisle(offset, -21, offset + 2, -12);
				aisle(offset, 17, offset + 2, 24);
			}
			L.quarryX = 20;
			L.quarryY = -3;
			break;
		}
		case HomeDesign::Horseshoe:
		{
			// One canal bent round three sides of the court, with its crops on the inside of the
			// bend, and the south left as lawn. The arms end in timber, the rest is wheat.
			// The canal is narrower than a parterre's and keeps two corners of lawn outside its
			// sand: a home in a tight pocket of the map builds its first expansion on the ground
			// round the module's edge, and a canal out to the rim left it none within a day's walk.
			paint(SAND, -27, -27, 28, -12);
			paint(SAND, 15, -12, 28, 24);
			paint(SAND, -27, -12, -14, 24);
			paint(SAND, 13, 17, 15, 22);
			paint(SAND, -14, 17, -12, 22);
			paint(WATER, -25, -25, 26, -22);
			paint(WATER, 23, -22, 26, 22);
			paint(WATER, -25, -22, -22, 22);
			paint(GRASS, -20, -20, 21, -14);
			paint(GRASS, 15, -14, 21, 20);
			paint(GRASS, -20, -14, -14, 20);
			crop(false, -19, -19, 20, -15);
			crop(false, 16, -15, 20, 19);
			crop(false, -19, -15, -15, 19);
			crop(true, 16, 11, 20, 19);
			crop(true, -19, 11, -15, 19);
			for (int offset : {-10, 0, 10})
				aisle(offset, -21, offset + 2, -12);
			for (int offset : {-6, 8})
			{
				aisle(15, offset, 21, offset + 2);
				aisle(-20, offset, -14, offset + 2);
			}
			// A causeway north across the bend: colonies start by the service apron, and on a
			// crowded map the way round by the lawn left too little ground within a day's walk.
			aisle(-1, -27, 2, -14);
			L.quarryX = 6;
			L.quarryY = 21;
			break;
		}
		case HomeDesign::Cloister:
		{
			// A canal all the way round, crops on its inner bank, and a sand causeway across it on
			// each side. Two corners of lawn stay outside the canal's sand, as round a horseshoe.
			// The quarry is the well in a corner of the court: nowhere else inside is dry ground.
			paint(SAND, -27, -27, 28, 28);
			paint(WATER, -25, -25, 26, -22);
			paint(WATER, -25, 24, 26, 26);
			paint(WATER, 23, -22, 26, 24);
			paint(WATER, -25, -22, -22, 24);
			paint(GRASS, -20, -20, 21, -14);
			paint(GRASS, -20, 17, 21, 22);
			paint(GRASS, 15, -14, 21, 17);
			paint(GRASS, -20, -14, -14, 17);
			crop(false, -19, -19, 20, -15);
			crop(false, -19, 18, 20, 21);
			crop(false, 16, -15, 20, 18);
			crop(false, -19, -15, -15, 18);
			crop(true, 16, 10, 20, 21);
			crop(true, -19, 10, -15, 21);
			for (int offset : {-10, 0, 10})
				aisle(offset, -21, offset + 2, -12);
			for (int offset : {-12, 11})
				aisle(offset, 17, offset + 2, 23);
			for (int offset : {-8, 8})
			{
				aisle(15, offset, 21, offset + 2);
				aisle(-20, offset, -14, offset + 2);
			}
			// Causeways: three corners of sand straight across water, bank and crops, one on each
			// side, so the colony by the service apron has a way straight out.
			aisle(-1, -27, 2, -14);
			aisle(-1, 17, 2, 28);
			aisle(15, 0, 28, 3);
			aisle(-27, 0, -14, 3);
			L.quarryX = -12;
			L.quarryY = 12;
			break;
		}
		case HomeDesign::FourBeds:
		{
			// A short canal feeds the wheat along the service apron; a square pool bed stands in
			// each corner of the module, one of them timber, and the flanks and the south between
			// the beds stay lawn.
			paint(SAND, -13, -29, 14, -12);
			paint(WATER, -11, -27, 12, -22);
			paint(GRASS, -11, -20, 12, -14);
			crop(false, -10, -19, 11, -15);
			aisle(0, -21, 2, -12);
			bed(false, -29, -29, 16);
			bed(false, 14, -29, 16);
			bed(false, -29, 14, 16);
			bed(true, 14, 14, 16);
			L.quarryX = 6;
			L.quarryY = 21;
			break;
		}
		default:
			break;
		}
		paint(SAND, -14, -12, 15, 17);
		paint(GRASS, -12, -10, 13, 15);
		// Food-service buildings need to hug the grain, especially policies that
		// require an upgraded inn footprint within one tile of harvestable wheat, so
		// a grass apron runs straight off the wheat bed: five clear rows fit a 3x3 inn
		// (or a 4x4 swarm) against the grain. Wheat may grow over the apron, as farmland
		// does, but never into the construction court: a single row of sand corners
		// along the apron's foot makes the two tile rows either side of it unplantable,
		// which no crop can extend across. The court loses only its top row, which lies
		// inside the building grid's inset margin. Terrain is the only containment: generated
		// maps may not use the saved no-growth flag, which belongs to hand-made scenarios.
		paint(GRASS, -12, -15, 13, -9);
		paint(SAND, -14, -10, 15, -9);
		// The quarry's ground is held like an objective court, so a garden path that runs in to
		// meet the court across open lawn cannot pave the stone's tiles first.
		fillRectangle(L.objectives, t, at(L.quarryX, L.quarryY, L.quarryX + 2, L.quarryY + 3), 1);
		// The module's rim is a straight-edged rectangle: these maps are formal gardens, and a
		// two-wide path can only meet a straight edge flush.
		L.features.push_back({h.x - 29, h.y - 30, h.x + 30, h.y + 31});
	}
	// Grass may never touch water: the engine's terrain model expects a beach between them,
	// and without one the shoreline renders as a hard edge that reads as a bug. Everything
	// stamped after this call needs its own pass, which is why both maps finish their design
	// with one.
	layBeaches(L.terrain, t);
	// Every design must keep its crops inside its own sand: growth follows pure grass in eight
	// directions, so a crop that can reach past the module's reservation would, given time, cover
	// the meadow. Checked on the design, before anything else is laid round the homes.
	const auto grass = pureTiles(L.terrain, t, GRASS);
	std::vector<unsigned char> crops(t.size(), 0);
	for (int i = 0; i < t.size(); ++i)
		crops[i] = grass[i] && (L.wheat[i] || L.wood[i]);
	const auto spread = floodFrom(t, crops, grass);
	for (int i : spread.visited)
		if (!L.reserved[i])
		{
			L.failure = std::string("The ") + homeDesignName(L.homeDesign) +
						" home garden lets its crops grow out past its sand.";
			return false;
		}
	return true;
}
namespace
{
// A plot with fewer crop tiles than this is a sliver of shoreline, not somewhere worth walking.
constexpr int kLeastBankCrops = 40;
int stampBankFarm(Layout &L, RegionBounds b, bool timber)
{
	// A plot sits against the water it borrows its irrigation from: how fast a crop grows back
	// depends on how much water is near it, and a plot laid a few tiles inland off the shore
	// regrew too slowly to be worth the walk. So the box may run into the water
	// and its beach, and the plot simply takes whatever grass is inside it, sharing the shore's
	// own sand as the cap on that side. Nothing designed may be underneath: a home module, a
	// crossing or another plot still refuses the site outright.
	for (int y = b.y0 - 3; y < b.y1 + 3; ++y)
		for (int x = b.x0 - 3; x < b.x1 + 3; ++x)
		{
			const int i = L.t.at(x, y);
			if (L.reserved[i] || L.crossings[i] || L.wheat[i] || L.wood[i])
				return 0;
		}
	// One corner less on the far sides: planting is on pure tiles, not corners.
	RegionBounds crops{b.x0 + 2, b.y0 + 2, b.x1 - 3, b.y1 - 3};
	// Count the grass this site would actually plant before changing anything: against a
	// shore, a box can be mostly water, and a plot too small to be worth walking to should
	// leave the ground as it found it so the caller can try the next position.
	int planted = 0;
	for (int y = crops.y0; y < crops.y1; ++y)
		for (int x = crops.x0; x < crops.x1; ++x)
			if (L.terrain[L.t.at(x, y)] == GRASS)
				++planted;
	if (planted < kLeastBankCrops)
		return 0;
	// Decide from the ground as it was found, not as this leaves it: only what was grass
	// becomes rim or crops, so the plot can neither fill a lake nor carry a shoreline away,
	// and the crop bed is not swallowed by the rim that goes down first.
	std::vector<unsigned char> wasGrass(size_t((b.x1 - b.x0 + 2) * (b.y1 - b.y0 + 2)), 0);
	const int stride = b.x1 - b.x0 + 2;
	const auto found = [&](int x, int y) -> unsigned char &
	{ return wasGrass[size_t(y - b.y0 + 1) * stride + (x - b.x0 + 1)]; };
	for (int y = b.y0; y < b.y1; ++y)
		for (int x = b.x0; x < b.x1; ++x)
			found(x, y) = L.terrain[L.t.at(x, y)] == GRASS;
	for (int y = b.y0; y < b.y1; ++y)
		for (int x = b.x0; x < b.x1; ++x)
		{
			if (!found(x, y))
				continue;
			const int i = L.t.at(x, y);
			const bool inside =
				x >= crops.x0 && x < crops.x1 + 1 && y >= crops.y0 && y < crops.y1 + 1;
			L.terrain[i] = inside ? GRASS : SAND;
			if (inside && x < crops.x1 && y < crops.y1)
				(timber ? L.wood : L.wheat)[i] = 1;
		}
	L.features.push_back(b);
	return planted;
}
} // namespace

bool bankFarm(Layout &L, RegionBounds b, bool timber)
{
	// The first choice of box is often blocked by one corner of a neighbouring plot, a
	// crossing's shoulder or a home module, and refusing there left the bank bare: Hilbert
	// placed 12 of the 30 plots it proposed and Sierpiński offered six in total, which is
	// most of why these two maps measured barren beside every other landscape.
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
				if (L.terrain[i] != GRASS || claimed(L, i))
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
			// Sand cap, crop ring, pool. Four tiles of grass between the pool and the cap:
			// the beach the shoreline pass will take out of the inner row, three rows of
			// crops, and the walking lane along them. The pool takes the rest, because the
			// water these maps were missing has to come from somewhere other than widening
			// the one channel their geometry is built around — at seven tiles instead of six
			// the Hilbert river left no room for a crossing court on a 128 map at all.
			fillRectangle(L.terrain, t, bed, SAND);
			const RegionBounds ring{bed.x0 + 2, bed.y0 + 2, bed.x1 - 2, bed.y1 - 2};
			fillRectangle(L.terrain, t, ring, GRASS);
			const RegionBounds pool{bed.x0 + 6, bed.y0 + 6, bed.x1 - 6, bed.y1 - 6};
			fillRectangle(L.terrain, t, pool, WATER);
			// Every third bed grows timber and the rest wheat, so neither crop can be cornered
			// by taking one part of the map, and a bed is worth walking to from either home.
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
			L.features.push_back(bed);
			++beds;
		}
	context.telemetry.measure("fractal.beds.placed", beds);
	context.telemetry.measure("fractal.beds.water-tiles", water);
	context.telemetry.measure("fractal.beds.crop-tiles", crops);
	return beds;
}

int gardenPaths(Layout &L, GenerationContext &context)
{
	// A formal garden's paths are clean, neat and tidy: straight edges, one width, square
	// corners, and every path meeting what it joins flush, with nothing jutting past a junction
	// and nothing missing from one. Each edge is an L or a Z of horizontal and
	// vertical legs laid over open grass, two tiles wide throughout — the path's line and the
	// tile beside it, a full 2x2 at every bend. A route is only accepted when both of its tiles
	// meet the feature at each end on the same line, when a bridge is met straight along its
	// own axis, and when the path keeps a tile of clear grass from any rim or beach it passes,
	// so it meets things head-on and never grazes along them. Sand, so nothing grows over it.
	const auto &t = L.t;
	const int n = int(L.features.size());
	L.featureAxis.resize(size_t(n), 0);
	if (n < 2)
		return 0;
	const auto inside = [&](int x, int y, const RegionBounds &b)
	{
		const int w = b.x1 - b.x0, h = b.y1 - b.y0;
		return ((x - b.x0) % t.w + t.w) % t.w < w && ((y - b.y0) % t.h + t.h) % t.h < h;
	};
	std::vector<unsigned char> owned(size_t(t.size()), 0), path(size_t(t.size()), 0);
	for (const auto &b : L.features)
		fillRectangle(owned, t, b, 1);
	// Ground a path may be laid over: grass that is no crop, court or other feature's box.
	const auto open = [&](int i)
	{
		return path[i] || (L.terrain[i] == GRASS && !owned[i] && !L.wheat[i] && !L.wood[i] &&
						   !L.objectives[i]);
	};
	// Grass that is no crop or court: what a path end may run over inside its own feature's
	// box, and the only ground a path may pass beside without grazing it.
	const auto paveable = [&](int i)
	{ return path[i] || (L.terrain[i] == GRASS && !L.wheat[i] && !L.wood[i] && !L.objectives[i]); };
	const auto centre = [&](const RegionBounds &b)
	{ return std::pair<int, int>{(b.x0 + b.x1) / 2, (b.y0 + b.y1) / 2}; };
	const auto delta = [&](int from, int to, int size)
	{
		int d = ((to - from) % size + size) % size;
		return d > size / 2 ? d - size : d;
	};
	// A centre-line tile of a route and the legs it belongs to: bit 1 horizontal, bit 2 vertical.
	struct Step
	{
		int x, y, legs;
	};
	// The partner of a centre-line tile across the path's width, on a leg stepping by dx.
	const auto partner = [&](const Step &s, int dx)
	{ return dx != 0 ? t.at(s.x, s.y + 1) : t.at(s.x + 1, s.y); };
	const auto trace = [&](int a, int b, const std::vector<std::pair<int, int>> &corners)
	{
		std::vector<Step> steps;
		bool left = false, reached = false;
		int lastDx = 0, lastDy = 0, firstDx = 0, firstDy = 0;
		for (size_t leg = 0; leg + 1 < corners.size() && !reached; ++leg)
		{
			const auto [x0, y0] = corners[leg];
			const auto [x1, y1] = corners[leg + 1];
			const int dx = (x1 > x0) - (x1 < x0), dy = (y1 > y0) - (y1 < y0);
			const int length = std::abs(x1 - x0) + std::abs(y1 - y0);
			if (length == 0)
				continue;
			const int legBit = dx != 0 ? 1 : 2;
			if (!steps.empty() && (lastDx != dx || lastDy != dy))
				steps.back().legs |= legBit; // the bend tile carries both legs
			for (int k = (leg == 0 ? 0 : 1); k <= length; ++k)
			{
				const int x = x0 + dx * k, y = y0 + dy * k, i = t.at(x, y);
				if (inside(x, y, L.features[size_t(b)]))
				{
					if (!paveable(i))
					{
						reached = true;
						break;
					}
				}
				else if (!left && inside(x, y, L.features[size_t(a)]))
				{
					if (!paveable(i))
						steps.clear();
					else
					{
						if (steps.empty())
						{
							firstDx = dx;
							firstDy = dy;
						}
						steps.push_back({x, y, legBit});
					}
					lastDx = dx;
					lastDy = dy;
					continue;
				}
				else
				{
					left = true;
					if (!open(i))
						return std::vector<Step>{};
				}
				if (steps.empty())
				{
					firstDx = dx;
					firstDy = dy;
				}
				steps.push_back({x, y, legBit});
				lastDx = dx;
				lastDy = dy;
			}
		}
		if (!reached || steps.size() < 3)
			return std::vector<Step>{};
		// Bridges are met straight along their own axis, at both ends of the route.
		const auto axisOk = [&](int feature, int dx, int dy)
		{
			const int axis = L.featureAxis[size_t(feature)];
			return axis == 0 || (axis == 1 && dx != 0) || (axis == 2 && dy != 0);
		};
		if (!axisOk(a, firstDx, firstDy) || !axisOk(b, lastDx, lastDy))
			return std::vector<Step>{};
		// Flush at both ends: the tile beside the path's line must also stop against the
		// feature, on the same line, and be open itself.
		const Step &head = steps.front(), &tail = steps.back();
		const int headPartner = partner(head, firstDx), tailPartner = partner(tail, lastDx);
		const int hpx = headPartner % t.w, hpy = headPartner / t.w, tpx = tailPartner % t.w,
				  tpy = tailPartner / t.w;
		if (!paveable(headPartner) || !paveable(tailPartner) ||
			paveable(t.at(hpx - firstDx, hpy - firstDy)) || paveable(t.at(tpx + lastDx, tpy + lastDy)))
			return std::vector<Step>{};
		// Clearance: away from its two ends, nothing but grass or path round the path's width.
		for (size_t k = 2; k + 2 < steps.size(); ++k)
			for (int dy = -1; dy <= 2; ++dy)
				for (int dx = -1; dx <= 2; ++dx)
					if (!paveable(t.at(steps[k].x + dx, steps[k].y + dy)))
						return std::vector<Step>{};
		// And the path's own second tile along its length must be open ground too.
		for (size_t k = 0; k < steps.size(); ++k)
		{
			const int horizontalLeg = steps[k].legs & 1;
			const int j = horizontalLeg ? t.at(steps[k].x, steps[k].y + 1) : t.at(steps[k].x + 1, steps[k].y);
			if (!paveable(j))
				return std::vector<Step>{};
		}
		return steps;
	};
	// Candidate edges: each feature to its six nearest, by the Manhattan distance a square
	// path actually walks, shortest first.
	struct Edge
	{
		int length, a, b;
	};
	std::vector<Edge> edges;
	for (int a = 0; a < n; ++a)
	{
		const auto [ax, ay] = centre(L.features[size_t(a)]);
		std::vector<Edge> closest;
		for (int b = 0; b < n; ++b)
			if (b != a)
			{
				const auto [bx, by] = centre(L.features[size_t(b)]);
				closest.push_back({std::abs(delta(ax, bx, t.w)) + std::abs(delta(ay, by, t.h)), a, b});
			}
		std::sort(closest.begin(), closest.end(),
				  [](const Edge &l, const Edge &r)
				  { return l.length != r.length ? l.length < r.length : l.b < r.b; });
		for (size_t k = 0; k < closest.size() && k < 6; ++k)
			edges.push_back(closest[k]);
	}
	std::sort(edges.begin(), edges.end(),
			  [](const Edge &l, const Edge &r)
			  {
				  return l.length != r.length ? l.length < r.length
											  : std::make_pair(l.a, l.b) < std::make_pair(r.a, r.b);
			  });
	std::vector<int> root(static_cast<size_t>(n));
	std::iota(root.begin(), root.end(), 0);
	const auto find = [&](int v)
	{
		while (root[size_t(v)] != v)
			v = root[size_t(v)] = root[size_t(root[size_t(v)])];
		return v;
	};
	// A crossing's two landings are joined to the tree separately: each bank must be met.
	int joined = 0;
	std::vector<Step> laid;
	for (const auto &e : edges)
	{
		if (find(e.a) == find(e.b))
			continue;
		const auto [ax, ay] = centre(L.features[size_t(e.a)]);
		const auto [cbx, cby] = centre(L.features[size_t(e.b)]);
		const int bx = ax + delta(ax, cbx, t.w), by = ay + delta(ay, cby, t.h);
		const int mx = (ax + bx) / 2, my = (ay + by) / 2;
		std::vector<Step> route;
		for (const auto &corners : std::vector<std::vector<std::pair<int, int>>>{
				 {{ax, ay}, {bx, ay}, {bx, by}},
				 {{ax, ay}, {ax, by}, {bx, by}},
				 {{ax, ay}, {mx, ay}, {mx, by}, {bx, by}},
				 {{ax, ay}, {ax, my}, {bx, my}, {bx, by}}})
			if (route = trace(e.a, e.b, corners); !route.empty())
				break;
		if (route.empty())
			continue;
		for (const auto &st : route)
		{
			path[t.at(st.x, st.y)] = 1;
			laid.push_back(st);
		}
		root[size_t(find(e.a))] = find(e.b);
		++joined;
	}
	// Two tiles wide throughout: the line, the tile beside it for each leg the tile is on, and
	// the diagonal at a bend so every corner is a full square.
	std::vector<unsigned char> surface = path;
	for (const auto &st : laid)
	{
		if (st.legs & 1)
			surface[t.at(st.x, st.y + 1)] = 1;
		if (st.legs & 2)
			surface[t.at(st.x + 1, st.y)] = 1;
		if ((st.legs & 3) == 3)
			surface[t.at(st.x + 1, st.y + 1)] = 1;
	}
	int sand = 0;
	for (int i = 0; i < t.size(); ++i)
		if (surface[i] && L.terrain[i] == GRASS)
		{
			L.terrain[i] = SAND;
			++sand;
		}
	for (const auto &[a, b] : L.featureLinks)
		if (a < n && b < n && find(a) != find(b))
			root[size_t(find(a))] = find(b);
	int components = 0;
	for (int v = 0; v < n; ++v)
		components += find(v) == v;
	context.telemetry.measure("fractal.paths.features", n);
	context.telemetry.measure("fractal.paths.edges", joined);
	context.telemetry.measure("fractal.paths.sand-tiles", sand);
	context.telemetry.measure("fractal.paths.unjoined-groups", components - 1);
	return components - 1;
}

void stampCrossings(Layout &L, const CrossingSelection &selection, GenerationContext &context)
{
	for (size_t k = 0; k < selection.selected.size(); ++k)
	{
		const auto &c = selection.selected[k];
		// Seven undermap corners across yields at least six traversable tiles.
		strokePath(L.crossings, L.t, {{c.from.x, c.from.y, 3.5}, {c.to.x, c.to.y, 3.5}});
		// A landing is the last grass tile before the shore on the crossing's own axis, offset
		// so a two-wide path is centred on the bridge. A path must arrive along that axis and
		// end flush against the bridge's sand, so no stub juts past the junction.
		const bool horizontal = std::abs(c.to.x - c.from.x) >= std::abs(c.to.y - c.from.y);
		const int first = int(L.features.size());
		for (const auto &[end, other] : {std::pair{c.from, c.to}, std::pair{c.to, c.from}})
		{
			const int step = horizontal ? ((other.x > end.x) - (other.x < end.x))
										: ((other.y > end.y) - (other.y < end.y));
			int x = horizontal ? int(std::lround(end.x)) : int(std::lround(end.x)) - 1;
			int y = horizontal ? int(std::lround(end.y)) - 1 : int(std::lround(end.y));
			int lastX = x, lastY = y;
			for (int k = 0; k < 64; ++k)
			{
				if (L.terrain[L.t.at(x, y)] != GRASS)
					break;
				lastX = x;
				lastY = y;
				(horizontal ? x : y) += step;
			}
			L.features.push_back({lastX, lastY, lastX + 1, lastY + 1});
			L.featureAxis.resize(L.features.size(), 0);
			L.featureAxis.back() = horizontal ? 1 : 2;
		}
		L.featureLinks.push_back({first, first + 1});
		context.telemetry.measure("fractal.crossing.benefit-estimate", selection.benefits[k], c.id);
		context.telemetry.measure("fractal.crossing.level", c.level, c.id);
	}
	// The stroke is the route; the sand is only its surface over water. The crossing mask
	// still covers the whole stroke — landings on the bank and across Gardens' orchard
	// island — so nothing the design lays later may sit on the way through, and every crop
	// that could reach it is contained already. But the paint stops at the water's edge.
	// Painting the whole stroke drew a sand bar out past the lake onto the land and
	// straight across the orchard island, and a causeway that crosses the thing it was
	// built to reach reads as a diagram, not a garden. Access is the reason a
	// crossing exists; it is not a licence to draw over the map it serves.
	for (int i = 0; i < L.t.size(); ++i)
		if (L.crossings[i] && L.terrain[i] == WATER)
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
		// Quarry is outside the farm plots, on lawn the design leaves dry (in a cloister, whose
		// canal leaves no lawn, it is the well in a corner of the court). A 2×3 patch
		// has multiple gathering edges but does not make a wall. This opening minimum is
		// deliberately retained at zero stone amount, like the renewable food and wood.
		for (int y = L.quarryY; y < L.quarryY + 3; ++y)
			for (int x = L.quarryX; x < L.quarryX + 2; ++x)
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
		// All three fruits and nothing else. A court sits on the central island or beside a
		// crossing — the places both sides have to come to — and fruit is what an inn turns
		// into a reason to hold ground, so this is where it belongs rather than sprinkled
		// over the whole map. Stone goes out to the quarries below instead.
		const int type = CHERRY + ((x / 8) + (y / 8)) % 3;
		// Full at the default amount: at a third, a court holding forty tiles of fruit across a
		// whole map was not a prize anyone crosses a bridge for.
		if (int(context.bounded("fractal-objectives", 100)) < context.request.option("fruit-amount"))
			map.setResource(x, y, type, 1);
	}
	// Spots of wheat along the shore of the water the design names — Hilbert's river,
	// Gardens' central lake — so the banks of the map's centrepiece carry food of their own.
	// A spot is a 3x3 of wheat right against the beach, at least a court's width from the
	// next, and never on anything the design owns. It grows and spreads like any farmland:
	// how fast wheat regrows depends on how much water its random probe finds, so a spot
	// laid back from the shore barely grew, and the beaches and paths round it are sand, so
	// it can spread along the bank without shutting a route.
	{
		const auto nearShore = dilate(t, L.wheatShore, 4);
		std::vector<int> centres;
		const int spacing = 20;
		for (int i = 0; i < t.size(); ++i)
		{
			if (!nearShore[i] || L.wheatShore[i])
				continue;
			const int cx = i % t.w, cy = i / t.w;
			bool clear = true;
			for (int dy = -1; clear && dy <= 1; ++dy)
				for (int dx = -1; clear && dx <= 1; ++dx)
				{
					const int x = t.x(cx + dx), y = t.y(cy + dy), j = t.at(x, y);
					clear = map.isGrass(x, y) && clearGround(map, x, y) && !claimed(L, j);
				}
			if (!clear)
				continue;
			bool apart = true;
			for (const int c : centres)
				apart &= t.chebyshev(cx, cy, c % t.w, c / t.w) >= spacing;
			if (apart)
				centres.push_back(i);
		}
		int planted = 0;
		for (const int c : centres)
			for (int dy = -1; dy <= 1; ++dy)
				for (int dx = -1; dx <= 1; ++dx)
				{
					const int x = t.x(c % t.w + dx), y = t.y(c / t.w + dy);
					if (int(context.bounded("fractal-shore-wheat", 100)) >=
						context.request.option("wheat-amount"))
						continue;
					map.setResource(x, y, WHEAT, 1);
					++planted;
				}
		context.telemetry.measure("fractal.shore-wheat.spots", int(centres.size()));
		context.telemetry.measure("fractal.shore-wheat.tiles", planted);
	}
	// Outside the courts and plots the open land once carried nothing at all, which is most of
	// why these two maps measured barren beside every other landscape: a quarter the resource
	// tiles of the median map, and three quarters of the ground bare. It carries timber on the
	// same 8-lattice the courts use, so 3x3 copses always alternate with permanent gathering
	// lanes and no slider can build a wall across the land. Timber only: stone belongs at the
	// quarries, fruit in the contested courts, and food inside the contained plots and along the
	// shore. Scattering all three everywhere read as confetti rather than as somewhere with
	// places worth going.
	// The growth field of the finished terrain, water from beds, plots and lakes included.
	const auto dryGround = Fertility::forMap(map, false);
	int ambientTiles = 0;
	for (int i = 0; i < t.size(); ++i)
	{
		const int x = i % t.w, y = i / t.w;
		if (claimed(L, i))
			continue;
		// Only on dry ground, where the growth probe can never find water: a copse there
		// cannot extend, so it stays the size it was planted. On fertile ground a scattered
		// copse is a forest with a delay — one 50,000-tick game grew the fractal maps to a
		// third wood. Generated maps may not hold it back with the saved no-growth flag, so
		// where the ground is fertile there is simply no copse.
		if (dryGround.at(x, y) != 0)
			continue;
		// 3x3 patches with five clear tiles between them, the same lattice the objective
		// courts use: a patch costs building anchors as well as giving resources, and the
		// gaps are what keep a module's expansion room and the walking lanes open.
		if (x % 8 >= 3 || y % 8 >= 3 || !clearGround(map, x, y))
			continue;
		// One cell of the lattice in eight, scattered rather than gridded: dense enough to
		// read as woods on the way somewhere, sparse enough that the land between the
		// design's features still looks like land. Every other cell put five times as much
		// timber on the map as food, and even at half that it read as sprinkled everywhere.
		if (((x / 8) * 3 + (y / 8) * 7) % 8)
			continue;
		if (int(context.bounded("fractal-ambient", 100)) >= context.request.option("wood-amount"))
			continue;
		map.setResource(x, y, WOOD, 1);
		++ambientTiles;
	}
	context.telemetry.measure("fractal.ambient.deposit-tiles", ambientTiles);
	// Stone is the one thing worth a journey. Rather than a patch every eight tiles, a handful
	// of real quarries go as far from every home as the map allows: the ground between homes
	// on a torus, which is also the ground you have to hold to work them. Each is a clump the
	// size of a building court, and the six-tile opening quarry inside every module is what
	// keeps a colony from being stuck before it gets there.
	std::vector<int> quarries;
	const int wanted = 2 + context.request.nbTeams / 2;
	for (int round = 0; round < wanted; ++round)
	{
		int best = -1, bestScore = -1;
		for (int y = 4; y < t.h; y += 8)
			for (int x = 4; x < t.w; x += 8)
			{
				const int i = t.at(x, y);
				if (claimed(L, i) || !clearGround(map, x, y) || !map.isGrass(x, y))
					continue;
				int score = INT_MAX;
				for (Home h : L.homes)
					score = std::min(score, t.chebyshev(x, y, h.x, h.y));
				for (int other : quarries)
					score = std::min(score, t.chebyshev(x, y, other % t.w, other / t.w));
				if (score > bestScore)
				{
					bestScore = score;
					best = i;
				}
			}
		if (best < 0 || bestScore < 24)
			break;
		quarries.push_back(best);
	}
	int quarryStone = 0;
	for (const int site : quarries)
	{
		const int cx = site % t.w, cy = site / t.w;
		for (int dy = -4; dy <= 4; ++dy)
			for (int dx = -4; dx <= 4; ++dx)
			{
				// A rounded clump, and never on a lane the design owns.
				if (dx * dx + dy * dy > 20)
					continue;
				const int x = t.x(cx + dx), y = t.y(cy + dy), i = t.at(x, y);
				if (claimed(L, i) || !map.isGrass(x, y) || !clearGround(map, x, y))
					continue;
				if (int(context.bounded("fractal-quarries", 100)) >=
					context.request.option("stone-amount"))
					continue;
				map.setResource(x, y, STONE, 1);
				++quarryStone;
			}
	}
	// Stone never extends (its resource type is not expendable), so a quarry needs nothing to
	// keep it the size it was placed.
	context.telemetry.measure("fractal.quarries.placed", int(quarries.size()));
	context.telemetry.measure("fractal.quarries.stone-tiles", quarryStone);
	if (!quarries.empty())
	{
		int nearest = INT_MAX;
		for (const int site : quarries)
			for (Home h : L.homes)
				nearest = std::min(nearest, t.chebyshev(site % t.w, site / t.w, h.x, h.y));
		context.telemetry.measure("fractal.quarries.nearest-home-distance", nearest);
	}
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
			std::string lacking;
			for (const auto &[missing, name] :
				 {std::pair{!wheat || !renewableWheat, " food"}, {!wood || !renewableWood, " wood"},
				  {!quarry, " quarry"}, {expansion < 16, " expansion room"}})
				if (missing)
					lacking += name;
			context.detail = "Home " + std::to_string(h) + " lacks reachable" + lacking + ".";
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
