// SPDX-License-Identifier: GPL-3.0-or-later
#include "SavannahGenerator.h"
#include "Contact.h"
#include "Drawing.h"
#include "Farmland.h"
#include "Game.h"
#include "GenerationContext.h"
#include "Growth.h"
#include "Homes.h"
#include "LatticeNoise.h"
#include "Morphology.h"
#include "Orbits.h"
#include "Pipeline.h"
#include "Planting.h"
#include "Points.h"
#include "Room.h"
#include "Sketch.h"
#include <algorithm>
#include <string>
using namespace MapGeneration;

// Savannah's contract is room to build and flank, with renewable home supplies and richer ponds
// between colonies, pools and lone trees dotting the plain between them. Crops occupy sealed
// grass islands in sand, not the whole fertile countryside:
// growth can fill those islands without eating the town or the plains. This is a terrain design,
// not a new growth rule. See docs/map-generators/SAVANNAH.md for budgets and validation evidence.
namespace
{
// A radius-20 home plus three tiles of untouched margin seats an ordinary town; centres 48 apart
// leave at least two tiles between reservations. Area alone is insufficient on odd-count lattices.
constexpr int kHomeRadius = 20, kHomeReserve = 23, kHomeSpacing = 48, kAreaPerHome = 4096;
// Four-tile site offsets break the lattice without spending its guaranteed spacing budget.
constexpr int kHomeJitter = 4;
// Equal starter floors are deposit tile counts, not percentages.
// Crops get growth room beyond their initial deposits after mixed-AI games reduced 236 wheat
// tiles to 66/93 and, on a later candidate, 182 wood to zero by tick 24,000. Both enlarged
// plots keep their western sand edges at the original coordinates: moving the wheat edge
// toward town caused Numbi to place no food inn and harvest zero wheat. The radius-23
// reservation still protects town and the opposed five-tile exits.
constexpr int kStarterCrop = 20, kStarterStone = 4;
// Neutral features reserve a radius-19 disc: pond, two plots, a forward building clearing, and
// walking frontage. Another tile between discs keep saturated crops from making belts.
constexpr int kNeutralReserve = 19, kFeatureGap = 1;
// Water the map wants to be seen to have: watering holes one per 128-square more than the first
// calibration (Sparse 2, Normal 3, Many 4), ponds of radius 5.5 rather than 4.5, and small pools
// on the open plain about forty tiles apart. A pool waters the plain round it, but the plain holds
// no crop to grow there, so the containment argument does not change.
constexpr double kPondRadius = 5.5, kPoolRadius = 3.5;
constexpr int kPoolSpacing = 40, kPoolClearance = 9, kPoolReserve = 8;
// Lone trees dot the plain, a candidate every twelve tiles or so, each a tree or a clump of two or
// three, planted only on dry pure grass (crop growth chance zero), where the engine's water probe
// never lets a tree spread: scenery and a little finite wood, never a thicket that closes the plain.
constexpr int kPlainTreeSpacing = 12;
struct Plot
{
	std::vector<int> tiles;
	int resource, baseline, minimum;
	bool renewable;
};
struct Layout
{
	Torus t{1, 1};
	TerrainSketch terrain;
	std::vector<int> homeOf, plotOf;
	std::vector<unsigned char> reserved;
	std::vector<ShapePoint> homes, ponds, pools;
	std::vector<int> plainTrees;
	std::vector<Plot> plots;
	std::string failure;
};

// Placement policy stays here; rasterization, containment and fertility-ranked planting are shared.
void plot(Layout &L, GenerationContext &context, ShapePoint at, double radius, int resource,
		  int baseline, int minimum, bool renewable, const char *stream)
{
	std::vector<int> corners;
	const RadialShape outline(radius, 0.18, context, stream);
	forEachTileInShape(L.t, at.x, at.y, outline, 0,
					   [&](int i, double, double) { corners.push_back(i); });
	Plot p{stampContainedPlot(L.terrain, L.t, corners), resource, baseline, minimum, renewable};
	for (int i : p.tiles)
		L.plotOf[i] = int(L.plots.size());
	L.plots.push_back(std::move(p));
}
void reserve(Layout &L, ShapePoint at, int radius)
{
	const Torus &t = L.t;
	for (int dy = -radius; dy <= radius; ++dy)
		for (int dx = -radius; dx <= radius; ++dx)
			if (dx * dx + dy * dy <= radius * radius)
				L.reserved[t.at(int(at.x) + dx, int(at.y) + dy)] = 1;
}
void pond(Layout &L, GenerationContext &context, ShapePoint at, double radius)
{
	// The mild radial ripple leaves a multi-tile pure-water core after beaches. Plot sand is
	// stamped first; water wins at overlaps, then final validation checks every plot's grass.
	const RadialShape outline(radius, 0.25, context, "savannah-pond-outlines");
	fillShape(L.terrain, L.t, at.x, at.y, outline, 0, WATER);
	L.ponds.push_back(at);
}
Layout design(const GenerationRequest &r, GenerationContext &context)
{
	Layout L;
	// Service scalar validation precedes this callback, including engine dimension/team limits.
	if (r.wDec < 7 || r.hDec < 7 || std::abs(r.wDec - r.hDec) > 1)
	{
		L.failure = "Savannah needs sides of at least 128 tiles and an aspect ratio at most 2:1.";
		return L;
	}
	L.t = {1 << r.wDec, 1 << r.hDec};
	const Torus &t = L.t;
	if (r.nbTeams < 1 || t.size() / r.nbTeams < kAreaPerHome)
	{
		L.failure =
			"Savannah needs at least 4096 tiles per colony; enlarge the map or use fewer colonies.";
		return L;
	}
	const SavannahOptions o(r);
	L.homes = latticeSites(t.w, t.h, r.nbTeams, context.bounded("savannah-homes", t.w),
						   context.bounded("savannah-homes", t.h))
				  .sites;
	if (nearestSiteDistance(t, L.homes) < kHomeSpacing)
	{
		L.failure = "Savannah cannot fit home centres 48 tiles apart; use fewer colonies.";
		return L;
	}
	const int moved =
		jitterSites(t, L.homes, context, "savannah-home-offsets", kHomeJitter, kHomeSpacing);
	context.telemetry.measure("savannah.homes.jitter-accepted", moved);
	context.telemetry.measure("savannah.homes.spacing", nearestSiteDistance(t, L.homes));
	dealStarts(context, L.homes);
	L.terrain.assign(t.size(), GRASS);
	L.homeOf.assign(t.size(), -1);
	L.plotOf.assign(t.size(), -1);
	L.reserved.assign(t.size(), 0);
	std::vector<unsigned char> unusedWater(t.size(), 0);
	// Only the home ownership outlines share a shape; sites, crop outlines and ponds vary.
	const RadialShape home(kHomeRadius, 0.08, context, "savannah-home-outlines");
	stampRoundHomes(t, L.homes, 0, home, kHomeRadius, 0, nullptr, unusedWater, L.homeOf);
	for (ShapePoint h : L.homes)
	{
		reserve(L, h, kHomeReserve);
		// Western half stays clear for town/upgrade margins. Eastern plots are separated by
		// the pond and their own sand caps; offsets keep both crops within ordinary worker reach.
		plot(L, context, {h.x + 6, h.y - 11}, 6.5, WHEAT, 20, kStarterCrop, true,
			 "savannah-home-plots");
		plot(L, context, {h.x + 6, h.y + 10}, 5, WOOD, 8, kStarterCrop, true,
			 "savannah-home-plots");
		pond(L, context, {h.x + 9, h.y}, kPondRadius);
		// Stone is permanent; four single tiles are a quarry, not a clump that can wall an exit.
		plot(L, context, {h.x + 18, h.y}, 2, STONE, 0, kStarterStone, false, "savannah-quarries");
	}
	// Reserve two opposed five-tile approaches on the western town side, extending beyond home
	// reservations. A two-tile buffer accounts for feature sand vertices spoiling neighbouring tiles.
	for (ShapePoint h : L.homes)
		for (int dy = -26; dy <= 26; ++dy)
			for (int dx = -13; dx <= -5; ++dx)
				L.reserved[t.at(int(h.x) + dx, int(h.y) + dy)] = 1;
	const int wanted = std::max(1, (o.wateringHoles + 2) * t.size() / (128 * 128));
	struct Candidate
	{
		ShapePoint at;
		int score;
	};
	std::vector<Candidate> candidates;
	// Enumerate eligible integer sites instead of hoping random darts hit a narrow gap on
	// crowded maps. A single distance transform excludes home/approach collisions cheaply;
	// later candidates are checked against newly reserved ponds. Shuffling before stable
	// score sorting varies ties without making feasibility depend on luck in a dart budget.
	std::vector<int> order;
	const auto fromReserved = distanceSquaredTo(t, L.reserved);
	const int clearanceRadius = kNeutralReserve + kFeatureGap;
	for (int i = 0; i < t.size(); ++i)
		if (fromReserved[i] > clearanceRadius * clearanceRadius)
			order.push_back(i);
	context.shuffle(order.begin(), order.end(), "savannah-pond-sites");
	context.telemetry.measure("savannah.ponds.eligible-centres", order.size());
	for (int i : order)
	{
		ShapePoint at{double(i % t.w), double(i / t.w)};
		int first = t.size(), second = t.size();
		for (ShapePoint h : L.homes)
		{
			const int distance = t.dist2(int(at.x), int(at.y), int(h.x), int(h.y));
			if (distance < first)
			{
				second = first;
				first = distance;
			}
			else
				second = std::min(second, distance);
		}
		// Minimise the sum to the nearest pair: locally contested frontiers rather than a
		// single central objective. Shuffled tile order breaks equal-score ties reproducibly.
		candidates.push_back({at, r.nbTeams == 1 ? first : first + second});
	}
	std::stable_sort(candidates.begin(), candidates.end(),
					 [](const Candidate &a, const Candidate &b) { return a.score < b.score; });
	int placed = 0;
	for (const Candidate &c : candidates)
	{
		if (placed == wanted)
			break;
		bool fits = true;
		const int reach = kNeutralReserve + kFeatureGap;
		for (int dy = -reach; dy <= reach && fits; ++dy)
			for (int dx = -reach; dx <= reach && fits; ++dx)
				if (dx * dx + dy * dy <= reach * reach)
					fits = !L.reserved[t.at(int(c.at.x) + dx, int(c.at.y) + dy)];
		if (!fits)
			continue;
		reserve(L, c.at, kNeutralReserve);
		// Separately seeded headings vary resource approaches instead of repeating north/south
		// farm pairs. Disc reservations work at every heading; final tiles handle raster rounding.
		const AxisFrame frame{c.at.x, c.at.y,
							  context.bounded("savannah-pond-headings", 3600) * (2 * kPi / 3600)};
		plot(L, context, frame.at(0, -10), 5, WHEAT, 38, 0, true, "savannah-neutral-plots");
		plot(L, context, frame.at(0, 10), 5, WOOD, 28, 0, true, "savannah-neutral-plots");
		// Fruit sits east; the entire western side stays free for forward inns and their upgrades.
		for (int f = 0; f < 3; ++f)
			plot(L, context, frame.at(11, (f - 1) * 7), 2, CHERRY + f, 2, 0, false,
				 "savannah-orchards");
		pond(L, context, c.at, kPondRadius);
		++placed;
	}
	context.telemetry.measure("savannah.ponds.requested", wanted);
	context.telemetry.measure("savannah.ponds.placed", placed);
	if (placed < wanted)
		context.telemetry.fallback("savannah.ponds.omitted",
								   "Reserved homes and approaches exhausted pond space");
	if (!placed)
	{
		L.failure =
			"No neutral watering hole fits clear of homes and approaches; use fewer colonies.";
		return L;
	}
	// Pools on the open plain, clear of every feature: water to be seen, not farmed.
	int pools = 0, proposedPools = 0;
	for (const Site &s : spreadPoints(t, kPoolSpacing, context, "savannah-pool-sites"))
	{
		++proposedPools;
		bool fits = true;
		for (int dy = -kPoolClearance; dy <= kPoolClearance && fits; ++dy)
			for (int dx = -kPoolClearance; dx <= kPoolClearance && fits; ++dx)
				fits = !L.reserved[t.at(s.x + dx, s.y + dy)];
		if (!fits)
			continue;
		reserve(L, {double(s.x), double(s.y)}, kPoolReserve);
		const RadialShape outline(kPoolRadius, 0.25, context, "savannah-pool-outlines");
		fillShape(L.terrain, t, s.x, s.y, outline, 0, WATER);
		L.pools.push_back({double(s.x), double(s.y)});
		++pools;
	}
	context.telemetry.measure("savannah.pools.placed", pools);
	context.telemetry.measure("savannah.pools.proposed", proposedPools);
	// Dry groves and outcrops are optional, widely spaced features. Their geometry is independent
	// of amount controls, including zero. The caps stop even later irrigation from invading plains.
	int groves = 0, proposedGroves = 0;
	for (const Site &s : spreadPoints(t, 22, context, "savannah-grove-sites"))
	{
		++proposedGroves;
		bool fits = true;
		for (int dy = -8; dy <= 8 && fits; ++dy)
			for (int dx = -8; dx <= 8 && fits; ++dx)
				fits = !L.reserved[t.at(s.x + dx, s.y + dy)];
		if (!fits)
			continue;
		reserve(L, {double(s.x), double(s.y)}, 7);
		const bool quarry = groves % 5 == 4; // One in five accepted clumps is a small outcrop.
		plot(L, context, {double(s.x), double(s.y)}, quarry ? 2 : 3, quarry ? STONE : WOOD,
			 quarry ? 2 : 7, 0, false, "savannah-grove-outlines");
		++groves;
	}
	context.telemetry.measure("savannah.plains.clumps", groves);
	// Lone trees on the plain: candidates only. generate() plants those on dry pure grass.
	for (const Site &s : spreadPoints(t, kPlainTreeSpacing, context, "savannah-plain-trees"))
		if (const int i = t.at(s.x, s.y); !L.reserved[i] && L.plotOf[i] < 0)
			L.plainTrees.push_back(i);
	context.telemetry.measure("savannah.plains.tree-candidates", L.plainTrees.size());
	context.telemetry.measure("savannah.plains.proposed-clumps", proposedGroves);
	if (groves < proposedGroves)
		context.telemetry.fallback("savannah.plains.omitted-clumps",
								   "Clump reservations intersected existing features");
	std::vector<unsigned char> eligible(t.size(), 0);
	for (int i = 0; i < t.size(); ++i)
		eligible[i] = !L.reserved[i];
	// Period 14 gives patches several tiles across, not construction-destroying speckle.
	const auto dry = periodicNoise(t.w, t.h, 14, context.stream("savannah-dry-patches"));
	sprinkleSand(L.terrain, t, eligible, o.dryPatches / 100.0, 3, [&](int i) { return dry[i]; });
	layBeaches(L.terrain, t);
	// Beaches can trim the pond-facing edge of a plot. Eligibility is the final pure grass,
	// not the pre-beach stamp. Sand only removes growth connections; starter floors below
	// still fail if too little renewable ground survives. Never plant onto stale sketch tiles.
	const auto grass = pureTiles(L.terrain, t, GRASS);
	int trimmed = 0;
	for (Plot &p : L.plots)
	{
		for (int i : p.tiles)
			if (!grass[i])
			{
				L.plotOf[i] = -1;
				++trimmed;
			}
		p.tiles.erase(
			std::remove_if(p.tiles.begin(), p.tiles.end(), [&](int i) { return !grass[i]; }),
			p.tiles.end());
	}
	context.telemetry.measure("savannah.plots.beach-trimmed-tiles", trimmed);
	return L;
}

bool generate(Game &game, GenerationContext &context)
{
	context.stage = "savannah layout";
	const Layout L = design(context.request, context);
	if (!L.failure.empty())
	{
		context.detail = L.failure;
		return false;
	}
	context.stage = "savannah terrain";
	writeUndermap(game.map, L.terrain);
	for (int k = 0; k < context.request.nbTeams; ++k)
		game.addTeam();
	context.stage = "savannah colonies";
	// Crops/quarries must never become fallback swarm or worker sites, even at zero abundance.
	if (!settleColonies(
			game, context, "savannah-settlements",
			[&](int k)
			{
				auto mask = homeGrassMask(game.map, L.t, L.homeOf, k);
				for (int i = 0; i < L.t.size(); ++i)
					if (L.plotOf[i] >= 0)
						mask[i] = 0;
				return mask;
			},
			[&](int k) { return homeSwarmSite(L.homes[k], 0, kHomeRadius); }))
		return false;
	context.stage = "savannah resources";
	const SavannahOptions o(context.request);
	const auto fertility = cropGrowthField(L.terrain, L.t);
	for (size_t k = 0; k < L.plots.size(); ++k)
	{
		const Plot &p = L.plots[k];
		const int percent = p.resource == WHEAT   ? o.wheat
							: p.resource == WOOD  ? o.wood
							: p.resource == STONE ? o.stone
												  : o.fruit;
		const int wanted = p.minimum + int(scaledCount(p.baseline, percent));
		const int planted =
			plantContainedPlot(game.map, L.t, p.tiles, fertility, p.resource, wanted, p.renewable);
		context.telemetry.measure("savannah.plot.requested", wanted, int(k));
		context.telemetry.measure("savannah.plot.planted", planted, int(k));
		if (planted < wanted)
			context.telemetry.fallback("savannah.plot.saturated", "Eligible plot tiles exhausted",
									   int(k));
		if (planted < p.minimum)
		{
			context.detail = "An essential Savannah starter plot cannot hold its supply budget.";
			return false;
		}
	}
	// Lone trees where nothing can grow: dry pure grass (growth chance zero) outside every plot
	// and reservation. A candidate is a tree or a clump of two or three (the candidate's index
	// says which), so they read as trees on the preview. The wood amount scales how many
	// candidates are planted; at zero there are none.
	int trees = 0, clumps = 0;
	const int wantedClumps = int(scaledCount(int(L.plainTrees.size()), o.wood));
	const auto dryGrass = [&](int x, int y)
	{
		const int i = L.t.at(x, y);
		return !L.reserved[i] && L.plotOf[i] < 0 && fertility.at(L.t.x(x), L.t.y(y)) == 0 &&
			   game.map.isGrass(L.t.x(x), L.t.y(y)) && clearGround(game.map, L.t.x(x), L.t.y(y));
	};
	for (size_t k = 0; k < L.plainTrees.size() && clumps < wantedClumps; ++k)
	{
		const int i = L.plainTrees[k];
		const int x = i % L.t.w, y = i / L.t.w;
		if (!dryGrass(x, y))
			continue;
		game.map.setResource(x, y, WOOD, 1);
		++trees;
		++clumps;
		const int extra = int(k % 3);
		const int dx[] = {1, 0}, dy[] = {0, 1};
		for (int e = 0; e < extra; ++e)
			if (dryGrass(x + dx[e], y + dy[e]))
			{
				game.map.setResource(L.t.x(x + dx[e]), L.t.y(y + dy[e]), WOOD, 1);
				++trees;
			}
	}
	context.telemetry.measure("savannah.plains.tree-clumps", clumps);
	context.telemetry.measure("savannah.plains.trees", trees);
	seedAlgae(game.map, context, L.t, "savannah-algae", o.algae, AlgaeBand::anyWater(30));
	// No generic crop/route repair: unrestricted topups would break containment. Essential
	// shortfalls fail, while optional deposits saturate inside their pre-reserved plots.
	return true;
}
std::string validateWorld(const Game &game, const GenerationContext &context)
{
	GenerationContext replay(context.request);
	const Layout L = design(context.request, replay);
	if (const auto error = designMismatch(L, game.map, "Savannah"); !error.empty())
		return error;
	const Map &map = game.map;
	const Torus &t = L.t;
	// Lone trees on dry ground are the one deposit allowed outside a plot: they cannot spread.
	const auto fertility = cropGrowthField(L.terrain, t);
	if (const auto error = containedPlotsMismatch(map, t, L.plotOf, &fertility); !error.empty())
		return error;
	if (const auto error =
			homePondMissing(map, t, L.ponds, int(L.ponds.size()), "watering hole", "pure water");
		!error.empty())
		return error;
	const auto walk = walkFromFirstColony(map, context.request.nbTeams, "the plains",
										  "without clearing or swimming");
	if (!walk.error.empty())
		return walk.error;
	const auto buildable = buildableTiles(map);
	// Neutral ponds need actual room for a forward inn with upgrade margins. The adjacent
	// sand and crop plots do not count; all footprint tiles must be reached on final land.
	for (size_t p = L.homes.size(); p < L.ponds.size(); ++p)
	{
		std::vector<unsigned char> shore(t.size(), 0);
		for (int i = 0; i < t.size(); ++i)
			shore[i] = walk.steps[i] >= 0 && t.dist2(int(L.ponds[p].x), int(L.ponds[p].y), i % t.w,
													 i / t.w) <= kNeutralReserve * kNeutralReserve;
		if (buildSites(t, buildable, shore, 8) == 0)
			return "A neutral Savannah pond lacks reachable forward-inn room.";
	}
	// The ends of BOTH home approaches must join the same broad plains network. Erosion
	// by two is the shared five-tile passage test (2r+1); cardinal core connectivity avoids
	// counting a diagonal touching of two five-wide pads as a full-width passage.
	std::vector<unsigned char> passable(t.size(), 0);
	for (int i = 0; i < t.size(); ++i)
		passable[i] = stepCost(map, i % t.w, i / t.w, StepCosts::walking()) >= 0;
	const auto broad = erode(t, passable, 2);
	const int firstExit = t.at(int(L.homes[0].x) - 9, int(L.homes[0].y) - 24);
	const auto broadRegions = connectedRegions(broad, t.w, t.h, true);
	for (int k = 0; k < context.request.nbTeams; ++k)
	{
		const auto costs = costsFrom(map, t, walk.workers[k], StepCosts::walking());
		std::vector<unsigned char> local(t.size(), 0);
		bool wheat = false, wood = false;
		for (int i = 0; i < t.size(); ++i)
		{
			local[i] = L.homeOf[i] == k && costs[i] >= 0 && costs[i] <= 24;
			const int type = map.getResource(i % t.w, i / t.w).type;
			if ((type != WHEAT && type != WOOD) || !fertility.at(i % t.w, i / t.w))
				continue;
			for (int dy = -1; dy <= 1; ++dy)
				for (int dx = -1; dx <= 1; ++dx)
				{
					const int d = costs[t.at(i % t.w + dx, i / t.w + dy)];
					if (d >= 0 && d <= (type == WHEAT ? 24 : 32))
					{
						wheat |= type == WHEAT;
						wood |= type == WOOD;
					}
				}
		}
		if (!wheat || !wood)
			return "A Savannah colony lacks reachable renewable wheat or wood.";
		// 8x8 anchors include a 4x4 building plus two tiles on every side for upgrades/traffic.
		// Sixteen overlapping anchors are a minimum contiguous-room check, not sixteen buildings.
		if (buildSites(t, buildable, local, 8) < 16)
			return "A Savannah home lacks reachable building and upgrade room.";
		const int hx = int(L.homes[k].x), hy = int(L.homes[k].y);
		for (int side : {-1, 1})
		{
			const int exit = t.at(hx - 9, hy + side * 24);
			if (!broad[exit] || !broad[firstExit] || broadRegions[exit] != broadRegions[firstExit])
				return "A Savannah approach cannot join the five-tile-wide plains network.";
			for (int along = 5; along <= 24; ++along)
				for (int across = -11; across <= -7; ++across)
				{
					const int i = t.at(hx + across, hy + side * along);
					if (stepCost(map, i % t.w, i / t.w, StepCosts::walking()) < 0 || costs[i] < 0)
						return "A Savannah home lost a five-tile approach to the shared plains.";
				}
		}
	}
	return "";
}
} // namespace
SavannahOptions::SavannahOptions(const GenerationRequest &r)
	: wateringHoles(r.option("watering-holes")), dryPatches(r.option("dry-patches")),
	  wheat(r.option("wheat-amount")), wood(r.option("wood-amount")),
	  stone(r.option("stone-amount")), algae(r.option("algae-amount")),
	  fruit(r.option("fruit-amount"))
{
}
GeneratorDefinition savannahDefinition()
{
	return {"savannah",
			45,
			"Savannah",
			2,
			false,
			{GeneratorControl::choice("watering-holes", "Watering holes",
									  {"Sparse", "Normal", "Many"}, 1, ControlGroup::Terrain),
			 {"dry-patches", "Dry patches", 0, 20, 1, 8, ControlGroup::Terrain},
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
