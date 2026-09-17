// SPDX-License-Identifier: GPL-3.0-or-later
#include "HedgerowCountryGenerator.h"
#include "Centrepieces.h"
#include "Drawing.h"
#include "FertilityField.h"
#include "Game.h"
#include "GenerationContext.h"
#include "GraphMaze.h"
#include "Geometry.h"
#include "Growth.h"
#include "Morphology.h"
#include "Pipeline.h"
#include "Planting.h"
#include "Sketch.h"
#include "Tessellation.h"
#include <algorithm>
#include <array>
#include <climits>
using namespace MapGeneration;

// Hedgerow Country starts connected. A spanning network of gateways joins warped fields;
// additional gateways shorten journeys, while dry wood on the remaining boundaries offers
// worker-made shortcuts. Separate sand-capped pond plots feed villages without recolonizing
// roads or cut hedges. Field interiors retain broad grass for buildings and gathering traffic.
namespace
{
// Centre-to-boundary clearance in tiles. The centred pond reaches five corners
// outward; crop renewal probes fifteen tiles on EACH axis, and the thickest hedge
// extends three tiles inward. 28 leaves rasterization slack. The final fertility
// validator, rather than this geometric estimate, establishes dry cuttable hedges.
constexpr int kClearance = 28;
constexpr int kWideFieldClearance = 34;
// Mirror games exposed starvation with the original 16-water-tile pond. Merely
// increasing starter wheat caused faster expansion followed by worse hunger for
// Maxima. A 100-water-tile pond and wider capped plots instead budget ongoing
// renewal and gathering frontage. Sand still confines both crops to the farm.
constexpr int kHomeWheat = 48; // buffer while the first inn and harvesting routes establish
constexpr int kHomeWood = 24;
// Every field's village, scaled to its field. Fields of 64 and more keep the measured village: a
// pond of radius 5 in a capped plot of 14, lanes cleared within 20 of the centre and the swarm 20
// north. Fields of 48 (maintainer review 2026-09-16: "allow a couple smaller values for field
// size") shrink it so the pond's whole growth reach (radius + 15) still clears a 24-tile half field
// and its hedges: a pond of 4 in a plot of 11, lanes cleared within 16, the swarm 16 north. The
// smaller pond holds fewer water tiles, so a 48 field's crops regrow more slowly: small fields
// trade food for the number of fields and fronts.
struct Village
{
	int pond, plot, clear, swarmDy, originDy, accessDy, warpClearance;
};
Village villageFor(int fieldSize)
{
	if (fieldSize < 64)
		return {4, 11, 16, -16, -17, -12, 22};
	return {5, 14, 20, -20, -21, -16, fieldSize > 64 ? kWideFieldClearance : kClearance};
}
// Sand blotches in each field's open ground (maintainer review 2026-09-16: "some random sand
// blotches in the outer section of each square, just to break up the monotony"): two to four
// rough discs of radius 2 to 4, clear of the plot and its beaches, kBlotchKeepOut tiles from any
// hedge or lane, and never on a village's swarm apron. Sand is walkable, holds no crop and waters
// nothing, so a blotch changes where a colony builds, never whether a hedge stays dry or a lane
// stays open.
constexpr int kBlotchKeepOut = 3, kBlotchTries = 12;

struct Layout
{
	Torus t{1, 1};
	Tessellation fields;
	TerrainSketch terrain;
	std::vector<unsigned char> hedge, road, plot;
	std::vector<PondDesign> ponds; // every field's centrepiece design
	std::vector<int> pondTurns;
	std::vector<int> starts;
	std::string failure;
};
// Keep the old geometric separation heuristic as a candidate generator. Starting from
// each field gives alternate, equally roomy arrangements without moving terrain or
// adding hidden gateways. The first candidate is exactly the old seeded arrangement.
std::vector<int> spreadHomes(const Tessellation &g, int first, int teams)
{
	std::vector<int> sites{first};
	while (int(sites.size()) < teams)
	{
		int best = -1;
		long long score = -1;
		for (int cell = 0; cell < g.cellCount(); ++cell)
		{
			long long distance = LLONG_MAX;
			for (int used : sites)
				distance = std::min(distance, g.distance2(cell, used));
			if (distance > score)
			{
				score = distance;
				best = cell;
			}
		}
		sites.push_back(best);
	}
	return sites;
}

// Score samples of actual traversable ground, not just field adjacency: the first
// graph-only experiment still missed large differences inside irregular fields.
// Distances use the shared eight-neighbour tile flood and the designed crop/hedge
// obstacles. Sample one tile per 4x4 block to keep candidate scoring bounded; this
// is an area estimate, not a claim of perfectly equal final engine territory.
//
// Tied ground is divided equally. 27720 is divisible by all possible tie counts
// (1..12), avoiding floating-point comparisons and team-index ownership bias.
using HomeScore = std::array<int, 4>;
HomeScore scoreHomes(const std::vector<std::vector<int>> &distance, const std::vector<int> &origins,
					 const std::vector<int> &samples, const std::vector<int> &sites)
{
	constexpr int share = 27720;
	std::vector<int> ground(sites.size(), 0);
	for (int tile : samples)
	{
		int closest = INT_MAX, ties = 0;
		for (int site : sites)
			if (distance[site][tile] >= 0)
				closest = std::min(closest, distance[site][tile]);
		if (closest == INT_MAX)
			continue;
		for (int site : sites)
			ties += distance[site][tile] == closest;
		for (size_t k = 0; k < sites.size(); ++k)
			if (distance[sites[k]][tile] == closest)
				ground[k] += share / ties;
	}
	int nearestMin = INT_MAX, nearestMax = 0;
	for (int a : sites)
	{
		int nearest = INT_MAX;
		for (int b : sites)
			if (a != b)
				nearest = std::min(nearest, distance[a][origins[b]]);
		nearestMin = std::min(nearestMin, nearest);
		nearestMax = std::max(nearestMax, nearest);
	}
	const int least = *std::min_element(ground.begin(), ground.end());
	const int most = *std::max_element(ground.begin(), ground.end());
	const int territoryRatio = int(10000LL * least / std::max(1, most));
	const int contactRatio = 10000 * nearestMin / std::max(1, nearestMax);
	// Neither plentiful private land nor isolation alone should win the placement search.
	// Multiplying worst/best ratios rewards both balanced expansion and similar exposure;
	// equal products prefer the more even land allocation, then a longer opening.
	return {territoryRatio * contactRatio, territoryRatio, contactRatio, nearestMin};
}

Layout design(const GenerationRequest &r, GenerationContext &c)
{
	const HedgerowCountryOptions o(r);
	const Village v = villageFor(o.fieldSize);
	Layout L;
	L.t = {1 << r.wDec, 1 << r.hDec};
	const Torus &t = L.t;
	L.fields = squareTessellation(t.w, t.h, o.fieldSize);
	auto &g = L.fields;
	if (g.columns < 2 || g.rows < 2 || r.nbTeams > g.cellCount())
	{
		L.failure = "Use at least two fields along each axis and at most one colony per field; "
					"increase map size or reduce field size or colonies.";
		return L;
	}
	std::vector<unsigned char> boundary(g.edges.size(), 1), blocked(g.cellCount(), 0),
		open(g.edges.size(), 0);
	// Larger cells permit steeper boundary angles. Their diagonal edges can enter
	// the square growth probe even while satisfying the centre's circular clearance
	// (retained failure: seed 30002, field size 80, thickness 4). Reserve the larger
	// diagonal budget there. Compact fields retain their already limited warping;
	// final exact fertility validation remains authoritative for every setting.
	const int clearance = v.warpClearance;
	const auto regularCorners = g.corners;
	warpCorners(g, warpLimit(g), boundary, 12, clearance, c, "hedgerow-warp");
	// Circular centre clearance alone misses the square corners of the engine's
	// +/-15 growth probe (bulk regression: seed 1012498260, compact fields, depth 4).
	// Check the RASTERIZED, dilated walls against every pond's entire possible
	// growth envelope before choosing wood or gateways. This is conservative: it
	// ignores sand blocking and checks even boundaries later opened or left bare.
	// It therefore protects every allowed wooded share without drilling accidental
	// holes in a wall. Pond water tiles lie within +/-5 of the field centre, so
	// +/-20 encloses their growth reach, including terrain-corner conversion slack.
	//
	// Rare unsafe warps are contracted toward the original lattice, with no new
	// random draws. Safe layouts retain their exact geometry and RNG streams.
	// Halving preserves some irregularity, and the bounded final lattice fallback
	// has >=64-tile pitch: even a seven-tile hedge lies beyond the 41-tile envelope.
	const int growthReach = v.pond + 15;
	std::vector<unsigned char> farmEnvelope(t.size(), 0);
	for (int cell = 0; cell < g.cellCount(); ++cell)
		for (int dy = -growthReach; dy <= growthReach; ++dy)
			for (int dx = -growthReach; dx <= growthReach; ++dx)
				farmEnvelope[t.at(g.centreTileX(cell) + dx, g.centreTileY(cell) + dy)] = 1;
	const int contractions = relaxWarpOutside(g, regularCorners, boundary, farmEnvelope,
		o.hedgeThickness - 1);
	if (contractions < 0)
	{
		L.failure = "Field boundaries cannot clear the pond growth envelope.";
		return L;
	}
	c.telemetry.measure("hedgerow.warp-contractions", contractions);
	// A spanning tree guarantees initial access to EVERY field, including unoccupied ones.
	// The backtracker makes winding routes; opening some extra edges adds flanks without
	// immediately making every hedge redundant. The control is the percentage of the boundaries
	// the tree left closed that get a gateway too: 0 keeps only the tree, 100 opens every
	// boundary. It counted extra edges per 100 fields until 2026-09-16, when a maintainer found it
	// seemed to do nothing: 25 opened four short lane stubs on a 256 map. The default of 25 opens
	// about as many there, and 100 now visibly opens the whole country.
	carveSpanningTree(g, c, "hedgerow-lanes", blocked, open);
	{
		std::vector<int> closed;
		for (int e = 0; e < int(g.edges.size()); ++e)
			if (!open[e])
				closed.push_back(e);
		c.shuffle(closed.begin(), closed.end(), "hedgerow-gateways");
		const size_t extra = std::min(closed.size(), (closed.size() * size_t(o.gateways) + 50) / 100);
		for (size_t k = 0; k < extra; ++k)
			open[closed[k]] = 1;
	}
	L.hedge.assign(t.size(), 0);
	L.road.assign(t.size(), 0);
	L.plot.assign(t.size(), 0);
	L.terrain.assign(t.size(), GRASS);
	int wooded = 0, gateways = 0;
	std::vector<unsigned char> woodedEdges(g.edges.size(), 0);
	for (int e = 0; e < int(g.edges.size()); ++e)
	{
		const auto ends = g.edgeEnds(e, g.edges[e].cells[0]);
		// Select whole boundaries, never independent wood tiles: random holes in a thin
		// wall would let eight-neighbour movement erase the intended clearing decision.
		const bool wood = int(c.bounded("hedgerow-boundaries", 100)) < o.woodedShare;
		if (wood)
		{
			woodedEdges[e] = 1;
			++wooded;
		}
		if (!open[e])
			continue;
		++gateways;
		const SubtilePoint middle{(ends.first.x + ends.second.x) / 2,
								  (ends.first.y + ends.second.y) / 2};
		for (int cell : g.edges[e].cells)
		{
			const auto a = tilePoint(g.cells[cell].centre);
			const auto b = tilePoint(g.imageNear(middle, g.cells[cell].centre));
			strokePath(L.road, t, {{a.x, a.y, 2.5}, {b.x, b.y, 2.5}});
		}
	}
	// The sealed centreline already prevents diagonal leaks. Dilation gives roughly
	// 3/5/7 tiles of wood at thickness settings 2/3/4, plus slanted-line rasterization.
	// These are cutting DEPTHS, not resource abundance: wood-amount scales the farm woodlots.
	L.hedge = rasterizeBoundaries(g, woodedEdges, o.hedgeThickness - 1);
	// Open village squares interrupt the sand lanes, but the contained crops cannot spread into
	// them. Ponds remain well inside the cells, beyond the growth probe of every boundary.
	for (int cell = 0; cell < g.cellCount(); ++cell)
	{
		const int x = g.centreTileX(cell), y = g.centreTileY(cell);
		for (int dy = -v.clear; dy <= v.clear; ++dy)
			for (int dx = -v.clear; dx <= v.clear; ++dx)
				L.road[t.at(x + dx, y + dy)] = 0;
		for (int dy = -v.plot; dy <= v.plot; ++dy)
			for (int dx = -v.plot; dx <= v.plot; ++dx)
			{
				const int i = t.at(x + dx, y + dy);
				const int d = std::max(std::abs(dx), std::abs(dy));
				L.terrain[i] = d >= v.plot - 1 ? SAND : GRASS;
				L.plot[i] = d < v.plot - 1;
				// A sand divider keeps the renewable woodlot out of the wheat plot.
				if (std::abs(dx) <= 1)
					L.terrain[i] = SAND;
			}
	}
	for (int i = 0; i < t.size(); ++i)
		if (L.road[i])
		{
			L.terrain[i] = SAND;
			L.hedge[i] = 0;
		}
	{
		const auto labels = labelTiles(g);
		std::vector<unsigned char> busy(t.size(), 0);
		for (int i = 0; i < t.size(); ++i)
			busy[i] = L.hedge[i] || L.road[i];
		busy = dilate(t, busy, kBlotchKeepOut);
		int blotches = 0;
		for (int cell = 0; cell < g.cellCount() && !labels.empty(); ++cell)
		{
			const int x = g.centreTileX(cell), y = g.centreTileY(cell), half = o.fieldSize / 2;
			const int wanted = 2 + int(c.bounded("hedgerow-blotches", 3));
			for (int b = 0, tries = 0; b < wanted && tries < kBlotchTries; ++tries)
			{
				const int dx = int(c.bounded("hedgerow-blotches", 2 * half + 1)) - half;
				const int dy = int(c.bounded("hedgerow-blotches", 2 * half + 1)) - half;
				const int radius = 2 + int(c.bounded("hedgerow-blotches", 3));
				const double turn = c.bounded("hedgerow-blotches", 360) * kPi / 180;
				const int i = t.at(x + dx, y + dy);
				// The swarm's apron: the rectangle the settlement searches, and its workers' ring.
				const bool apron =
					dx >= -15 && dx <= 5 && dy >= v.swarmDy - 9 && dy <= v.swarmDy + 9;
				if (labels[i] != cell || busy[i] || apron ||
					std::max(std::abs(dx), std::abs(dy)) < v.plot + radius + 3)
					continue;
				const RadialShape shape(radius, 0.35, c, "hedgerow-blotches");
				forEachTileInShape(t, x + dx, y + dy, shape, turn,
								   [&](int tile, double, double)
								   {
									   if (!busy[tile] && L.terrain[tile] == GRASS)
										   L.terrain[tile] = SAND;
								   });
				++b;
				++blotches;
			}
		}
		c.telemetry.measure("hedgerow.blotches", blotches);
	}
	layBeaches(L.terrain, t);
	// Sand is stored at corners, whereas wood occupies pure-grass tiles. Remove the road's
	// mixed-terrain shoulders from the hedge mask too; otherwise the validator would demand
	// impossible wood deposits beside every gate. This widens only the designed entrances.
	const auto grass = pureTiles(L.terrain, t, GRASS);
	for (int i = 0; i < t.size(); ++i)
		L.hedge[i] = L.hedge[i] && grass[i];
	// Equal straight-line spacing alone left some starts closest to four times as much
	// land as others in revision 1. Evaluate every geometric arrangement against the
	// routes that actually exist, while preserving the original minimum home spacing.
	const int first = int(c.bounded("hedgerow-homes", g.cellCount()));
	L.starts = spreadHomes(g, first, r.nbTeams);
	// There is no choice with one colony or with a colony in every field. Avoid tile
	// floods in those cases. At most 64 candidate origins exist at the shared map limits;
	// the largest distance cache is 64 MiB, local to this reconstructible design call.
	if (r.nbTeams > 1 && r.nbTeams < g.cellCount())
	{
		const auto water = pureTiles(L.terrain, t, WATER);
		std::vector<unsigned char> walkable(t.size(), 0);
		std::vector<int> samples, origins;
		for (int i = 0; i < t.size(); ++i)
			walkable[i] = !water[i] && !L.hedge[i] && !L.plot[i];
		for (int y = 2; y < t.h; y += 4)
			for (int x = 2; x < t.w; x += 4)
				if (walkable[t.at(x, y)])
					samples.push_back(t.at(x, y));
		std::vector<std::vector<int>> distance;
		for (int cell = 0; cell < g.cellCount(); ++cell)
		{
			// A tile on the north side of the planned swarm, consistently placed in
			// every village. Actual workers may start on another side; final reports
			// measure from those real workers after crops and settlements are present.
			const int origin = t.at(g.centreTileX(cell) - 3, g.centreTileY(cell) + v.originDy);
			origins.push_back(origin);
			distance.push_back(stepsFrom(t, tileMask(t, {origin}), walkable));
		}
		const auto separation = [&](const std::vector<int> &sites)
		{
			long long closest = LLONG_MAX;
			for (size_t a = 0; a < sites.size(); ++a)
				for (size_t b = a + 1; b < sites.size(); ++b)
					closest = std::min(closest, g.distance2(sites[a], sites[b]));
			return closest;
		};
		const long long minimumSpacing = separation(L.starts);
		const HomeScore original = scoreHomes(distance, origins, samples, L.starts);
		HomeScore best = original;
		for (int offset = 1; offset < g.cellCount(); ++offset)
		{
			const auto candidate = spreadHomes(g, (first + offset) % g.cellCount(), r.nbTeams);
			if (separation(candidate) < minimumSpacing)
				continue;
			const HomeScore score = scoreHomes(distance, origins, samples, candidate);
			if (score > best)
			{
				best = score;
				L.starts = candidate;
			}
		}
		c.telemetry.measure("hedgerow.homes.original-sampled-territory-ratio",
							original[1] / 10000.0);
		c.telemetry.measure("hedgerow.homes.selected-sampled-territory-ratio", best[1] / 10000.0);
		c.telemetry.measure("hedgerow.homes.original-contact-ratio", original[2] / 10000.0);
		c.telemetry.measure("hedgerow.homes.selected-contact-ratio", best[2] / 10000.0);
		c.telemetry.measure("hedgerow.homes.area-samples", samples.size());
		c.telemetry.choice("hedgerow.homes.selection", "walking-territory");
	}
	else
		c.telemetry.choice("hedgerow.homes.selection",
						   r.nbTeams == 1 ? "single-colony" : "all-fields");
	// Every field's pond in one of the shared centrepiece designs, all inside the pond square, so
	// the growth envelope above holds for each (maintainer review 2026-09-16: "the shape the
	// central fountain in each square varied a little bit more"). Every home draws the same design,
	// since a design's water sets how fast its crops regrow; the rest draw their own. Ponds lie
	// inside the plots the start search already excludes, so they cannot change its choice.
	{
		const auto draw = [&]
		{
			auto design = PondDesign(c.bounded("hedgerow-ponds", int(PondDesign::Count)));
			return pondDesignMinimumRadius(design) > v.pond ? PondDesign::Round : design;
		};
		const PondDesign homeDesign = draw();
		const int homeTurn = int(c.bounded("hedgerow-ponds", 2));
		L.ponds.assign(g.cellCount(), homeDesign);
		L.pondTurns.assign(g.cellCount(), homeTurn);
		for (int cell = 0; cell < g.cellCount(); ++cell)
		{
			const PondDesign design = draw();
			const int turn = int(c.bounded("hedgerow-ponds", 2));
			if (std::find(L.starts.begin(), L.starts.end(), cell) != L.starts.end())
				continue;
			L.ponds[cell] = design;
			L.pondTurns[cell] = turn;
		}
		for (int cell = 0; cell < g.cellCount(); ++cell)
			for (int dy = -v.pond; dy <= v.pond; ++dy)
				for (int dx = -v.pond; dx <= v.pond; ++dx)
					if (const char at =
							pondDesignAt(L.ponds[cell], v.pond, L.pondTurns[cell], dx, dy))
						L.terrain[t.at(g.centreTileX(cell) + dx, g.centreTileY(cell) + dy)] =
							at == 'w' ? WATER : SAND;
		layBeaches(L.terrain, t);
		c.telemetry.choice("hedgerow.ponds.home-design", pondDesignName(homeDesign));
	}
	dealStarts(c, L.starts);
	c.telemetry.measure("hedgerow.fields", g.cellCount());
	c.telemetry.measure("hedgerow.wooded-boundaries", wooded);
	c.telemetry.measure("hedgerow.gateways", gateways);
	c.telemetry.measure("hedgerow.hedge-radius", o.hedgeThickness - 1);
	return L;
}
bool generate(Game &game, GenerationContext &c)
{
	c.stage = "hedgerow layout";
	const Layout L = design(c.request, c);
	if (!L.failure.empty())
	{
		c.detail = L.failure;
		return false;
	}
	const HedgerowCountryOptions o(c.request);
	const Village v = villageFor(o.fieldSize);
	const Torus &t = L.t;
	Map &map = game.map;
	writeUndermap(map, L.terrain);
	for (int k = 0; k < c.request.nbTeams; ++k)
		game.addTeam();
	c.stage = "hedgerow villages";
	// Keep the swarm north of the capped farm, rather than inside its growing ground.
	// The 17x13 candidate rectangle gives the settlement helper room for the complete 4x4
	// footprint and starting workers. The larger surrounding grass square is expansion land.
	const auto anchor = [&](int team)
	{
		const int cell = L.starts[team];
		return MapGeneratorPoint(L.fields.centreTileX(cell) - 5,
								 L.fields.centreTileY(cell) + v.swarmDy);
	};
	if (!settleColonies(
			game, c, "hedgerow-settlements",
			[&](int team)
			{
				const auto a = anchor(team);
				std::vector<unsigned char> mask(t.size(), 0);
				for (int dy = -6; dy <= 6; ++dy)
					for (int dx = -8; dx <= 8; ++dx)
						mask[t.at(a.x + dx, a.y + dy)] = 1;
				return mask;
			},
			anchor))
		return false;
	c.stage = "hedgerow crops and wood";
	const auto reserved = swarmSurroundings(t, c);
	for (int cell = 0; cell < L.fields.cellCount(); ++cell)
	{
		const int x = L.fields.centreTileX(cell), y = L.fields.centreTileY(cell);
		// Occupied fields keep 48 wheat and 24 wood tiles even at 0% abundance.
		// Other fields scale to zero: occupying them offers irrigated room rather than free
		// starter stocks. At 300%, eligibility caps crops inside the same sand-ringed plot.
		const bool home = std::find(L.starts.begin(), L.starts.end(), cell) != L.starts.end();
		for (int side : {-1, 1})
		{
			const auto eligible = [&](int i)
			{
				return L.plot[i] && !reserved[i] &&
					   t.chebyshev(x, y, i % t.w, i / t.w) < v.plot - 1 &&
					   t.offsetX(x, i % t.w) * side > 1 && clearGround(map, i % t.w, i / t.w);
			};
			const int seed = seedNear(t, x + side * 5, y, 4, eligible);
			if (seed >= 0)
				growPatch(map, t, seed, side < 0 ? WHEAT : WOOD,
						  (home ? (side < 0 ? kHomeWheat : kHomeWood) : 0) +
							  int(scaledCount(side < 0 ? 32 : 16, side < 0 ? o.wheat : o.wood)),
						  eligible);
		}
		// setResource randomises amounts as well as sprites. Equal-sized starter plots
		// therefore used to contain unequal food stocks. A local checkerboard of 2/3
		// keeps the old mean amount (2.5) but gives matching plot shapes matching stocks.
		// Keep the existing draws and varieties; this changes neither hedge effort nor
		// the random stream consumed by furnishing subsequent fields.
		for (int dy = -v.plot; dy <= v.plot; ++dy)
			for (int dx = -v.plot; dx <= v.plot; ++dx)
			{
				const int i = t.at(x + dx, y + dy);
				auto &resource = map.getResource(i % t.w, i / t.w);
				if (L.plot[i] && (resource.type == WHEAT || resource.type == WOOD))
					resource.amount = 2 + ((dx + dy + 2 * v.plot) % 2);
			}
		// Small quarries and orchards reward occupation of additional fields. Their
		// bounded patches stay away from village footprints, hedges and crop plots.
		for (int kind = 0; kind < 2; ++kind)
		{
			const int px = x + 14, py = y - 12 + kind * 5;
			const auto eligible = [&](int i)
			{
				return !reserved[i] && !L.hedge[i] && !L.plot[i] && !L.road[i] &&
					   t.chebyshev(px, py, i % t.w, i / t.w) <= 2 &&
					   clearGround(map, i % t.w, i / t.w);
			};
			const int seed = seedNear(t, px, py, 2, eligible);
			if (seed >= 0)
				growPatch(map, t, seed, kind ? CHERRY + cell % 3 : STONE,
						  (home && !kind ? 1 : 0) + int(scaledCount(2, kind ? o.fruit : o.stone)),
						  eligible);
		}
	}
	plantCover(map, t, L.hedge, WOOD, [&](int i) { return clearGround(map, i % t.w, i / t.w); });
	// Algae in the ponds (maintainer review 2026-09-16: "a distinct lack of algae in any pond on the
	// default parameters"; the map seeded none and had no algae control).
	seedAlgae(map, c, t, "hedgerow-algae", o.algae, AlgaeBand::anyWater(25));
	// Only the existing opening backstop may clear local starter congestion. Protect every
	// hedge so resource repair cannot silently turn a difficult route into an extra gateway.
	secureStartingCrops(game, c, t, 24, 32, 0, &L.hedge);
	return true;
}
std::string validateWorld(const Game &game, const GenerationContext &c)
{
	GenerationContext replay(c.request);
	const Layout L = design(c.request, replay);
	if (const auto error = designMismatch(L, game.map, "hedgerow"); !error.empty())
		return error;
	// Test the FINISHED world: terrain corners, furnishing and settlement can invalidate a
	// sound-looking design. Fertility is the engine's exact probe field, not distance-to-water.
	// Every intended hedge tile must survive and be dry; every field must be walk-reachable
	// without chopping. This does not establish useful detour savings or enjoyable AI games.
	const auto fertility = Fertility::forMap(game.map, false);
	const auto walk =
		walkFromFirstColony(game.map, c.request.nbTeams, "the fields", "through the gateways");
	if (!walk.error.empty())
		return walk.error;
	for (int i = 0; i < L.t.size(); ++i)
	{
		const int x = i % L.t.w, y = i / L.t.w;
		if (L.hedge[i] && (fertility.at(x, y) > 0 || game.map.getResource(x, y).type != WOOD))
			return std::string(fertility.at(x, y) > 0 ? "A hedge is fertile at " : "A hedge is missing at ") +
				std::to_string(x) + "," + std::to_string(y) + "; breaches must remain permanent.";
		if (L.road[i] && (game.map.isWater(x, y) || game.map.isResource(x, y)))
			return "An existing lane is blocked.";
	}
	const int accessDy = villageFor(HedgerowCountryOptions(c.request).fieldSize).accessDy;
	for (int cell = 0; cell < L.fields.cellCount(); ++cell)
		if (walk.steps[L.t.at(L.fields.centreTileX(cell), L.fields.centreTileY(cell) + accessDy)] <
			0)
			return "A field has no initial road access.";
	return "";
}
} // namespace
HedgerowCountryOptions::HedgerowCountryOptions(const GenerationRequest &r)
	: fieldSize(r.option("field-size")), hedgeThickness(r.option("hedge-thickness")),
	  gateways(r.option("existing-gateways")), woodedShare(r.option("wooded-boundary-share")),
	  wheat(r.option("wheat-amount")), wood(r.option("wood-amount")),
	  stone(r.option("stone-amount")), algae(r.option("algae-amount")),
	  fruit(r.option("fruit-amount"))
{
}
GeneratorDefinition hedgerowCountryDefinition()
{
	return {
			"hedgerow-country",
			38,
			"Hedgerow Country",
			7,
			false,
			{{"field-size", "Field size", 48, 96, 16, 64, ControlGroup::Layout},
		 {"hedge-thickness", "Hedge thickness", 2, 4, 1, 3, ControlGroup::Terrain},
		 {"existing-gateways", "Existing gateways", 0, 100, 25, 25, ControlGroup::Layout},
		 {"wooded-boundary-share", "Wooded boundary share", 50, 100, 10, 90, ControlGroup::Terrain},
		 GeneratorControl::percentage("wheat-amount", "Wheat amount"),
		 GeneratorControl::percentage("wood-amount", "Wood amount"),
		 GeneratorControl::percentage("stone-amount", "Stone amount"),
		 GeneratorControl::percentage("algae-amount", "Algae amount"),
		 GeneratorControl::percentage("fruit-amount", "Fruit amount")},
			generate,
			true,
			designFailure<design>,
			validateWorld,
			// Thin hedge lines subdividing fields, not a forest canopy.
			{"terrain:natural", "feature:farmland", "style:tight-building",
			 "fairness:stamped-lattice"}};
}
