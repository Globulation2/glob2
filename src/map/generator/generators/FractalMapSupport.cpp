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
		fillRectangle(L.terrain, t, {h.x - 24, h.y - 27, h.x + 25, h.y - 22}, WATER);
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
		// 24x24 court do not move. They are the home's only renewable timber since the
		// southern plot became wheat.
		for (const RegionBounds bay : {RegionBounds{h.x - 23, h.y - 19, h.x - 16, h.y - 15},
									   RegionBounds{h.x + 18, h.y - 19, h.x + 24, h.y - 15}})
		{
			fillRectangle(L.wheat, t, bay, 0);
			fillRectangle(L.wood, t, bay, 1);
		}
		// The southern plot is wheat too (2026-09-16): a home's renewable timber is the two
		// bays above, and the rest comes from copses and timber bank plots out on the map, so
		// a colony that wants to build big has a reason to leave home. The finished-world
		// check still refuses any home without reachable renewable wood.
		fillRectangle(L.terrain, t, {h.x - 26, h.y + 17, h.x + 27, h.y + 29}, SAND);
		// Five rows of water, matching the wheat side: two rows fed the timber slowly and left
		// the south of every module looking dry (2026-09-16). Crops regrow at a rate set by how
		// much water is near them, so the width of this strip is the timber supply.
		fillRectangle(L.terrain, t, {h.x - 24, h.y + 24, h.x + 25, h.y + 29}, WATER);
		fillRectangle(L.terrain, t, {h.x - 24, h.y + 19, h.x + 25, h.y + 23}, GRASS);
		fillRectangle(L.wheat, t, {h.x - 23, h.y + 20, h.x + 24, h.y + 22}, 1);
		// Crossing each crop frontage with three pure-sand aisles limits patch width and
		// guarantees access even after every eligible crop tile has filled in. The aisles stop
		// at the crops: run through the water as well and each strip becomes four short ponds
		// instead of one long one, which waters the same crops less and reads as a sand grid.
		for (int offset : {-16, 0, 16})
		{
			fillRectangle(L.terrain, t, {h.x + offset, h.y - 21, h.x + offset + 2, h.y - 12}, SAND);
			fillRectangle(L.terrain, t, {h.x + offset, h.y + 17, h.x + offset + 2, h.y + 24}, SAND);
		}
		// Food-service buildings need to hug the grain, especially policies that
		// require an upgraded inn footprint within one tile of harvestable wheat, so
		// a grass apron runs straight off the wheat bed: five clear rows fit a 3x3 inn
		// (or a 4x4 swarm) against the grain. Wheat may grow over the apron, as farmland
		// does, but never into the construction court: a single row of sand corners
		// along the apron's foot makes the two tile rows either side of it unplantable,
		// which no crop can extend across. The court loses only its top row, which lies
		// inside the building grid's inset margin. Generated maps may not use the saved
		// no-growth flag at all — it belongs to hand-made scenarios such as the tutorial —
		// and this apron used to be held clear with it (removed 2026-09-16).
		fillRectangle(L.terrain, t, {h.x - 12, h.y - 15, h.x + 13, h.y - 9}, GRASS);
		fillRectangle(L.terrain, t, {h.x - 14, h.y - 10, h.x + 15, h.y - 9}, SAND);
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
		L.features.push_back({h.x - 29, h.y - 30, h.x + 30, h.y + 31});
	}
	// Grass may never touch water: the engine's terrain model expects a beach between them,
	// and without one the shoreline renders as a hard edge that reads as a bug. Everything
	// stamped after this call needs its own pass, which is why both maps finish their design
	// with one (2026-09-16: garden beds and bank plots shipped a day without it).
	layBeaches(L.terrain, t);
}
namespace
{
// A plot with fewer crop tiles than this is a sliver of shoreline, not somewhere worth walking.
constexpr int kLeastBankCrops = 40;
int stampBankFarm(Layout &L, RegionBounds b, bool timber)
{
	// A plot sits against the water it borrows its irrigation from: how fast a crop grows back
	// depends on how much water is near it, and a plot laid a few tiles inland off the shore
	// regrew too slowly to be worth the walk (2026-09-16). So the box may run into the water
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
	// A formal garden's paths run straight and meet square: every leg here is horizontal or
	// vertical, and an edge between two features is either one bend (an L) or two (a Z), never
	// a staircase and never a diagonal. Paths go only over open grass, so one can lead up to a
	// bed, a plot, a lake or a home but can never be drawn across one. They are sand two corners
	// wide — the same slim line a module's own rim is drawn in — which also makes them
	// permanent: nothing can grow over a sand path, so the ways between the gardens survive the
	// overgrowth that shuts ordinary grass lanes.
	const auto &t = L.t;
	const int n = int(L.features.size());
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
	for (Home h : L.homes)
		fillRectangle(owned, t, {h.x - 30, h.y - 30, h.x + 31, h.y + 31}, 1);
	// A crossing's protected stroke is seven corners wide, wider than its landing, so land
	// that belongs to a crossing is open to paths: a path running along a bridge's own land
	// approach is the one place a path is most wanted, and refusing it left every bridge
	// unreachable from the side. Its water, and the orchard island inside a lake's footprint,
	// stay closed through the terrain and ownership tests.
	const auto open = [&](int i)
	{
		return path[i] || (L.terrain[i] == GRASS && !owned[i] && !L.wheat[i] && !L.wood[i] &&
						   !L.objectives[i]);
	};
	const auto centre = [&](const RegionBounds &b)
	{ return std::pair<int, int>{(b.x0 + b.x1) / 2, (b.y0 + b.y1) / 2}; };
	const auto delta = [&](int from, int to, int size)
	{
		int d = ((to - from) % size + size) % size;
		return d > size / 2 ? d - size : d;
	};
	// Ground a path end may be laid over inside a feature's own box: grass that is not a crop,
	// a court or a crossing's paving. A feature's box is larger than its visible edge — a
	// home's box takes in its frayed margin — so a path that stopped at the box stopped a few
	// tiles short of the sand it was meant to meet (2026-09-16).
	const auto paveable = [&](int i)
	{ return path[i] || (L.terrain[i] == GRASS && !L.wheat[i] && !L.wood[i] && !L.objectives[i]); };
	// Walk a polyline of straight legs from feature a to feature b. Inside a, only the stretch
	// after its last sand or water tile belongs to the path, so the path starts against a's
	// edge; between the boxes every tile must be open ground; inside b, the path runs on until
	// it meets b's first sand or water tile. Returns the tiles of the path, or nothing when the
	// route crosses anything else.
	const auto trace = [&](int a, int b, const std::vector<std::pair<int, int>> &corners)
	{
		std::vector<int> tiles;
		bool left = false;
		for (size_t leg = 0; leg + 1 < corners.size(); ++leg)
		{
			const auto [x0, y0] = corners[leg];
			const auto [x1, y1] = corners[leg + 1];
			const int dx = (x1 > x0) - (x1 < x0), dy = (y1 > y0) - (y1 < y0);
			const int steps = std::abs(x1 - x0) + std::abs(y1 - y0);
			for (int k = (leg == 0 ? 0 : 1); k <= steps; ++k)
			{
				const int x = x0 + dx * k, y = y0 + dy * k, i = t.at(x, y);
				if (inside(x, y, L.features[size_t(b)]))
				{
					if (!paveable(i))
						return tiles;
					tiles.push_back(i);
					continue;
				}
				if (!left)
				{
					if (inside(x, y, L.features[size_t(a)]))
					{
						if (paveable(i))
							tiles.push_back(i);
						else
							tiles.clear();
						continue;
					}
					left = true;
				}
				if (!open(i))
					return std::vector<int>{};
				tiles.push_back(i);
			}
		}
		// Ran out of legs without meeting b's edge: b's box was reached only through open
		// ground to its centre, which a real feature always has an edge before.
		return tiles;
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
		std::vector<Edge> near;
		for (int b = 0; b < n; ++b)
			if (b != a)
			{
				const auto [bx, by] = centre(L.features[size_t(b)]);
				near.push_back({std::abs(delta(ax, bx, t.w)) + std::abs(delta(ay, by, t.h)), a, b});
			}
		std::sort(near.begin(), near.end(),
				  [](const Edge &l, const Edge &r)
				  { return l.length != r.length ? l.length < r.length : l.b < r.b; });
		for (size_t k = 0; k < near.size() && k < 6; ++k)
			edges.push_back(near[k]);
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
	std::vector<unsigned char> reached(static_cast<size_t>(n), 0);
	// A crossing's two landings are deliberately NOT joined before the tree is built. Joined,
	// the tree needed only one path into the pair, so one bank of every bridge got a path and
	// the other got none (2026-09-16). Separate, the water between them refuses every route
	// across, so each bank joins the features on its own side and every bridge is met at both
	// ends. The crossing counts as a join only when reporting what is left unjoined.
	int joined = 0, pathTiles = 0;
	for (const auto &e : edges)
	{
		if (find(e.a) == find(e.b))
			continue;
		const auto [ax, ay] = centre(L.features[size_t(e.a)]);
		const auto [cbx, cby] = centre(L.features[size_t(e.b)]);
		const int bx = ax + delta(ax, cbx, t.w), by = ay + delta(ay, cby, t.h);
		const int mx = (ax + bx) / 2, my = (ay + by) / 2;
		// An L either way round, then a Z either way round through the midpoint.
		std::vector<int> route;
		for (const auto &corners : std::vector<std::vector<std::pair<int, int>>>{
				 {{ax, ay}, {bx, ay}, {bx, by}},
				 {{ax, ay}, {ax, by}, {bx, by}},
				 {{ax, ay}, {mx, ay}, {mx, by}, {bx, by}},
				 {{ax, ay}, {ax, my}, {bx, my}, {bx, by}}})
			if (route = trace(e.a, e.b, corners); !route.empty())
				break;
		if (route.empty())
			continue;
		for (const int i : route)
			if (!path[i])
			{
				path[i] = 1;
				++pathTiles;
			}
		reached[size_t(e.a)] = reached[size_t(e.b)] = 1;
		root[size_t(find(e.a))] = find(e.b);
		++joined;
	}
	// Pave the approach to every crossing a path arrived at, so the path meets the bridge.
	for (int f = 0; f < n && f < int(L.featureApproach.size()); ++f)
		if (reached[size_t(f)])
			for (const int i : L.featureApproach[size_t(f)])
				if (L.terrain[i] == GRASS)
					path[i] = 1;
	// Two corners wide: the line itself and the corner beside it, only where that is open too.
	std::vector<unsigned char> surface = path;
	for (int i = 0; i < t.size(); ++i)
		if (path[i])
		{
			const int x = i % t.w, y = i / t.w;
			for (const int j : {t.at(x + 1, y), t.at(x, y + 1)})
				if (open(j))
					surface[j] = 1;
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
		// A landing is the approach to the crossing: the ground from the end of the stroke in
		// to the first tile that is not grass, the shore where the surface begins. A path may
		// meet it anywhere along that approach, and the approach is paved only if one does, so
		// the path runs right up to the bridge — stopping at the stroke's end left a gap that
		// read as a dead end, and stopping at the shore made the landing too small to reach.
		const auto approach = [&](ShapePoint from, ShapePoint to)
		{
			std::vector<int> tiles;
			const double length = std::max(1.0, std::hypot(to.x - from.x, to.y - from.y));
			for (double d = 0; d <= length; d += 1.0)
			{
				const int x = int(std::lround(from.x + (to.x - from.x) * d / length));
				const int y = int(std::lround(from.y + (to.y - from.y) * d / length));
				const int i = L.t.at(x, y);
				if (L.terrain[i] != GRASS)
					break;
				if (tiles.empty() || tiles.back() != i)
					tiles.push_back(i);
			}
			return tiles;
		};
		const int first = int(L.features.size());
		for (const auto &[end, other] : {std::pair{c.from, c.to}, std::pair{c.to, c.from}})
		{
			auto tiles = approach(end, other);
			// The box spans the approach and one tile round it, so a path meeting it from any
			// side stops against the paving rather than short of it.
			int x0 = int(end.x), y0 = int(end.y), x1 = x0 + 1, y1 = y0 + 1;
			for (const int i : tiles)
			{
				int x = i % L.t.w, y = i / L.t.w;
				// Unwrap onto the end's side of the seam before extending the box.
				if (x - int(end.x) > L.t.w / 2) x -= L.t.w;
				if (int(end.x) - x > L.t.w / 2) x += L.t.w;
				if (y - int(end.y) > L.t.h / 2) y -= L.t.h;
				if (int(end.y) - y > L.t.h / 2) y += L.t.h;
				x0 = std::min(x0, x);
				y0 = std::min(y0, y);
				x1 = std::max(x1, x + 1);
				y1 = std::max(y1, y + 1);
			}
			L.features.push_back({x0 - 1, y0 - 1, x1 + 1, y1 + 1});
			L.featureApproach.resize(L.features.size());
			L.featureApproach.back() = std::move(tiles);
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
	// built to reach reads as a diagram, not a garden (2026-09-16). Access is the reason a
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
		// All three fruits and nothing else. A court sits on the central island or beside a
		// crossing — the places both sides have to come to — and fruit is what an inn turns
		// into a reason to hold ground, so this is where it belongs rather than sprinkled
		// over the whole map. Stone left these courts at the same time and went out to the
		// quarries below.
		const int type = CHERRY + ((x / 8) + (y / 8)) % 3;
		// Full at the default amount, where it used to be a third: a court holding forty tiles
		// of fruit across a whole map is not a prize anyone crosses a bridge for.
		if (int(context.bounded("fractal-objectives", 100)) < context.request.option("fruit-amount"))
			map.setResource(x, y, type, 1);
	}
	// Outside those courts the open land carried nothing at all, which is most of why these
	// two maps measured barren beside every other landscape: a quarter the resource tiles of
	// the median map, and three quarters of the ground bare (2026-09-16). It now carries
	// timber on the same 8-lattice the courts use, so 3x3 copses always alternate with
	// permanent gathering lanes and no slider can build a wall across the land. Timber only:
	// stone belongs at the quarries, fruit in the contested courts, and food inside the
	// contained plots. A first pass scattered all three everywhere and the map read as
	// confetti rather than as somewhere with places worth going.
	// Spots of wheat along the shore of the water the design names — Hilbert's river,
	// Gardens' central lake — so the banks of the map's centrepiece carry food of their own.
	// A spot is a 3x3 of wheat right against the beach, at least a court's width from the
	// next, and never on anything the design owns. It grows and spreads like any farmland:
	// how fast wheat regrows depends on how much water its random probe finds, so a spot
	// laid back from the shore barely grew, and the beaches and paths round it are sand, so
	// it can spread along the bank without shutting a route (2026-09-16).
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
					clear = map.isGrass(x, y) && clearGround(map, x, y) && !L.reserved[j] &&
							!L.crossings[j] && !L.objectives[j] && !L.wheat[j] && !L.wood[j];
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
	// The growth field of the finished terrain, water from beds, plots and lakes included.
	const auto dryGround = Fertility::forMap(map, false);
	std::vector<unsigned char> ambient(t.size(), 0);
	int ambientTiles = 0;
	for (int i = 0; i < t.size(); ++i)
	{
		const int x = i % t.w, y = i / t.w;
		if (L.objectives[i] || L.reserved[i] || L.crossings[i] || L.wheat[i] || L.wood[i])
			continue;
		// Only on dry ground, where the growth probe can never find water: a copse there
		// cannot extend, so it stays the size it was planted. On fertile ground a scattered
		// copse is a forest with a delay — one 50,000-tick game grew the fractal maps to a
		// third wood (2026-09-16). Generated maps may not hold it back with the saved
		// no-growth flag, so where the ground is fertile there is simply no copse.
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
		ambient[i] = 1;
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
				if (L.reserved[i] || L.crossings[i] || L.objectives[i] || L.wheat[i] || L.wood[i] ||
					!clearGround(map, x, y) || !map.isGrass(x, y))
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
				if (L.reserved[i] || L.crossings[i] || L.objectives[i] || L.wheat[i] ||
					L.wood[i] || !map.isGrass(x, y) || !clearGround(map, x, y))
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
