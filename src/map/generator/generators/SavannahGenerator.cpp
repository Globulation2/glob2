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
#include <optional>
#include <string>
using namespace MapGeneration;

// Savannah's contract is room to build and flank, with renewable home supplies and richer watering
// holes between colonies. A savannah reads as golden grass to the horizon with lone and dead trees,
// red earth, granite kopjes and watering holes that game trails lead to; the map draws each of them.
// Crops occupy grass islands sealed in barren earth, not the whole fertile countryside: growth can
// fill those islands without eating the town or the plains. Earth seals exactly as sand did (nothing
// grows on it, and it inhibits the water probe the same way) but can be built on. Tall grass and
// scrub cover only plain where the crop growth chance is zero, so they never spread; they can be
// walked through, but must be cleared to build. See docs/map-generators/savannah.md.
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
// plots keep their western seal edges at the original coordinates: moving the wheat edge
// toward town caused Numbi to place no food inn and harvest zero wheat. The radius-23
// reservation still protects town and the opposed five-tile exits.
constexpr int kStarterCrop = 20, kStarterStone = 4;
// Neutral features reserve a radius-19 disc: pond, two plots, a forward building clearing, and
// walking frontage. Another tile between discs keep saturated crops from making belts.
constexpr int kNeutralReserve = 19, kFeatureGap = 1;
// Home ponds stay at radius 5.5. A watering hole is wider, so a dark deep centre (three steps
// inside its shallows) still leaves a shallow ring for fish and algae; its plots sit a tile and a
// half further out than the home's so the larger pond does not drown them.
constexpr double kPondRadius = 5.5, kHoleRadius = 6.5, kHolePlotOffset = 11.5, kPoolRadius = 3.5;
constexpr int kDeepSteps = 3;
constexpr int kPoolSpacing = 40, kPoolClearance = 9, kPoolReserve = 8;
// Lone trees dot the plain, a candidate every twelve tiles or so, each a tree or a clump of two or
// three, or a dead tree; planted only on dry pure grass (crop growth chance zero), where the
// engine's water probe never lets a tree spread.
constexpr int kPlainTreeSpacing = 12;
// Trails are three vertices wide (one pure track tile with mixed edges) and keep two tiles of
// reservation either side so plain features do not sit on them.
constexpr int kTrailReserve = 3;
// Camp-sites: a candidate every forty tiles, half of them used at 100%, each in a small clearing.
constexpr int kCampSpacing = 40, kCampClearing = 2;
enum class Amount
{
	Wheat,
	Wood,
	Stone,
	Fruit
};
struct Plot
{
	std::vector<int> tiles;
	// A built-in resource id, or -1 with a catalogue key.
	int resource;
	const char *key;
	Amount amount;
	int baseline, minimum;
	bool renewable;
};
// A watering hole's crop field holds maize by a mud ring, or rice by a sand bank: rice regrows only
// where a pure sand tile lies beyond the water from it.
enum HoleCrop
{
	Maize,
	Rice
};
struct Hole
{
	ShapePoint at;
	double heading;
	int crop;
};
struct Layout
{
	Torus t{1, 1};
	TerrainSketch terrain;
	std::vector<int> homeOf, plotOf;
	std::vector<unsigned char> reserved, grassFree;
	std::vector<ShapePoint> homes, ponds, pools;
	std::vector<Hole> holes;
	std::vector<int> plainTrees, camps;
	std::vector<Plot> plots;
	std::string failure;
};

// Placement policy stays here; rasterization, containment and fertility-ranked planting are shared.
void plot(Layout &L, GenerationContext &context, ShapePoint at, double radius, int resource,
		  const char *key, Amount amount, int baseline, int minimum, bool renewable,
		  const char *stream, TerrainType seal = DIRT)
{
	std::vector<int> corners;
	const RadialShape outline(radius, 0.18, context, stream);
	forEachTileInShape(L.t, at.x, at.y, outline, 0,
					   [&](int i, double, double) { corners.push_back(i); });
	Plot p{stampContainedPlot(L.terrain, L.t, corners, 2, seal), resource, key, amount, baseline,
		   minimum, renewable};
	for (int i : p.tiles)
		L.plotOf[i] = int(L.plots.size());
	L.plots.push_back(std::move(p));
}
void reserve(std::vector<unsigned char> &mask, const Torus &t, ShapePoint at, int radius)
{
	for (int dy = -radius; dy <= radius; ++dy)
		for (int dx = -radius; dx <= radius; ++dx)
			if (dx * dx + dy * dy <= radius * radius)
				mask[t.at(int(at.x) + dx, int(at.y) + dy)] = 1;
}
void reserve(Layout &L, ShapePoint at, int radius)
{
	reserve(L.reserved, L.t, at, radius);
}
bool clearOf(const Layout &L, ShapePoint at, int radius)
{
	for (int dy = -radius; dy <= radius; ++dy)
		for (int dx = -radius; dx <= radius; ++dx)
			if (dx * dx + dy * dy <= radius * radius && L.reserved[L.t.at(int(at.x) + dx, int(at.y) + dy)])
				return false;
	return true;
}
// A vertex is a corner of the four tiles up and left of it; changing it changes all four.
bool cornerOfPlot(const Layout &L, int x, int y)
{
	for (int dy = -1; dy <= 0; ++dy)
		for (int dx = -1; dx <= 0; ++dx)
			if (L.plotOf[L.t.at(x + dx, y + dy)] >= 0)
				return true;
	return false;
}
void pond(Layout &L, GenerationContext &context, ShapePoint at, double radius)
{
	// The mild radial ripple leaves a multi-tile pure-water core after beaches. Plot seals are
	// stamped first; water wins at overlaps, then final validation checks every plot's grass.
	const RadialShape outline(radius, 0.25, context, "savannah-pond-outlines");
	fillShape(L.terrain, L.t, at.x, at.y, outline, 0, WATER);
	L.ponds.push_back(at);
}
// The ground round a watering hole, a band `inner` to `inner + width` tiles from its centre, on
// grass or clay corners of no plot: trampled mud by a maize hole, a sand bank by a rice hole, the
// bottom of a dried pool. The band leaves the forward-inn side (local west) clear within `innGap`
// radians.
int bank(Layout &L, const Hole &h, double inner, double width, TerrainType type, double innGap)
{
	const Torus &t = L.t;
	const int reach = int(inner + width) + 1;
	int laid = 0;
	for (int dy = -reach; dy <= reach; ++dy)
		for (int dx = -reach; dx <= reach; ++dx)
		{
			const double d = Numeric::hypot(dx, dy);
			if (d < inner || d > inner + width)
				continue;
			double turn = Numeric::atan2(dy, dx) - h.heading;
			while (turn > kPi)
				turn -= 2 * kPi;
			while (turn < -kPi)
				turn += 2 * kPi;
			if (kPi - std::abs(turn) < innGap)
				continue;
			const int x = int(h.at.x) + dx, y = int(h.at.y) + dy, i = t.at(x, y);
			if ((L.terrain[i] != GRASS && L.terrain[i] != CLAY) || cornerOfPlot(L, x, y))
				continue;
			L.terrain[i] = type;
			++laid;
		}
	return laid;
}
// A game trail: dirt track three vertices wide from `from` to `to` on the shortest wrapped way,
// bowed `bow` tiles to one side at its middle. Only grass corners of no plot, outside every
// colony's reservation but `owner`'s (-1 for none), take track; a trail crossing water or a plot
// simply has a gap there.
int trail(Layout &L, ShapePoint from, ShapePoint to, double bow, int owner)
{
	const Torus &t = L.t;
	double dx = to.x - from.x, dy = to.y - from.y;
	dx -= t.w * Numeric::round(dx / t.w);
	dy -= t.h * Numeric::round(dy / t.h);
	const double length = Numeric::hypot(dx, dy);
	if (length < 1)
		return 0;
	const double nx = -dy / length, ny = dx / length;
	int laid = 0;
	for (int s = 0; s <= int(length * 2); ++s)
	{
		const double u = s / (length * 2);
		const double off = bow * Numeric::sin(kPi * u);
		const double px = from.x + dx * u + nx * off, py = from.y + dy * u + ny * off;
		for (int oy = -1; oy <= 1; ++oy)
			for (int ox = -1; ox <= 1; ++ox)
			{
				if (ox * ox + oy * oy > 1)
					continue;
				const int x = int(Numeric::lround(px)) + ox, y = int(Numeric::lround(py)) + oy;
				const int i = t.at(x, y);
				bool foreign = false;
				for (size_t k = 0; k < L.homes.size() && !foreign; ++k)
					foreign = int(k) != owner &&
							  t.dist2(x, y, int(L.homes[k].x), int(L.homes[k].y)) <=
								  kHomeReserve * kHomeReserve;
				// Trails stop at a watering hole's rim: its disc is the inn clearing and its fields.
				for (const Hole &h : L.holes)
					foreign = foreign || t.dist2(x, y, int(h.at.x), int(h.at.y)) <
											 (kNeutralReserve - 1) * (kNeutralReserve - 1);
				if (foreign || L.terrain[i] != GRASS || cornerOfPlot(L, x, y))
					continue;
				L.terrain[i] = DIRT_TRACK;
				++laid;
			}
		reserve(L, {px, py}, kTrailReserve);
	}
	return laid;
}
// Where a trail from `from` meets a watering hole: the point of its reservation's rim facing it.
ShapePoint holeRim(const Torus &t, const Hole &h, ShapePoint from)
{
	double dx = from.x - h.at.x, dy = from.y - h.at.y;
	dx -= t.w * Numeric::round(dx / t.w);
	dy -= t.h * Numeric::round(dy / t.h);
	const double d = std::max(1.0, Numeric::hypot(dx, dy));
	return {h.at.x + dx / d * kNeutralReserve, h.at.y + dy / d * kNeutralReserve};
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
	L.grassFree.assign(t.size(), 0);
	std::vector<unsigned char> unusedWater(t.size(), 0);
	// Only the home ownership outlines share a shape; sites, crop outlines and ponds vary.
	const RadialShape home(kHomeRadius, 0.08, context, "savannah-home-outlines");
	stampRoundHomes(t, L.homes, 0, home, kHomeRadius, 0, nullptr, unusedWater, L.homeOf);
	for (ShapePoint h : L.homes)
	{
		reserve(L, h, kHomeReserve);
		// Western half stays clear for town/upgrade margins. Eastern plots are separated by
		// the pond and their own earth caps; offsets keep both crops within ordinary worker reach.
		plot(L, context, {h.x + 6, h.y - 11}, 6.5, WHEAT, nullptr, Amount::Wheat, 20, kStarterCrop,
			 true, "savannah-home-plots");
		plot(L, context, {h.x + 6, h.y + 10}, 5, WOOD, nullptr, Amount::Wood, 8, kStarterCrop, true,
			 "savannah-home-plots");
		pond(L, context, {h.x + 9, h.y}, kPondRadius);
		// Stone is permanent; four single tiles are a quarry, not a clump that can wall an exit.
		plot(L, context, {h.x + 18, h.y}, 2, STONE, nullptr, Amount::Stone, 0, kStarterStone, false,
			 "savannah-quarries");
	}
	// Reserve two opposed five-tile approaches on the western town side, extending beyond home
	// reservations. A two-tile buffer accounts for feature seal vertices spoiling neighbouring tiles.
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
	// later candidates are checked against newly reserved holes. Shuffling before stable
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
		ShapePoint at{double(t.remainderX(i)), double(i / t.w)};
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
	int placed = 0, rice = 0;
	for (const Candidate &c : candidates)
	{
		if (placed == wanted)
			break;
		if (!clearOf(L, c.at, kNeutralReserve + kFeatureGap))
			continue;
		reserve(L, c.at, kNeutralReserve);
		// Separately seeded headings vary resource approaches instead of repeating north/south
		// farm pairs. Disc reservations work at every heading; final tiles handle raster rounding.
		const Hole hole{c.at, context.bounded("savannah-pond-headings", 3600) * (2 * kPi / 3600),
						int(context.bounded("savannah-hole-crops", 2))};
		const AxisFrame frame{c.at.x, c.at.y, hole.heading};
		rice += hole.crop == Rice;
		plot(L, context, frame.at(0, -kHolePlotOffset), 5, -1, hole.crop == Rice ? "rice" : "maize",
			 Amount::Wheat, 38, 0, true, "savannah-neutral-plots");
		plot(L, context, frame.at(0, kHolePlotOffset), 5, WOOD, nullptr, Amount::Wood, 28, 0, true,
			 "savannah-neutral-plots");
		// Fruit sits east; the entire western side stays free for forward inns and their upgrades.
		for (int f = 0; f < 3; ++f)
			plot(L, context, frame.at(12, (f - 1) * 7), 2, CHERRY + f, nullptr, Amount::Fruit, 2, 0,
				 false, "savannah-orchards");
		pond(L, context, c.at, kHoleRadius);
		L.holes.push_back(hole);
		++placed;
	}
	context.telemetry.measure("savannah.ponds.requested", wanted);
	context.telemetry.measure("savannah.ponds.placed", placed);
	context.telemetry.measure("savannah.holes.rice", rice);
	if (placed < wanted)
		context.telemetry.fallback("savannah.ponds.omitted",
								   "Reserved homes and approaches exhausted pond space");
	if (!placed)
	{
		L.failure =
			"No neutral watering hole fits clear of homes and approaches; use fewer colonies.";
		return L;
	}
	// A maize hole is ringed by trampled mud; a rice hole by a sand bank wide enough to hold pure
	// sand tiles, the far shore rice needs. Both stay off the inn side and every plot's corners.
	int mud = 0, sandBank = 0;
	for (const Hole &h : L.holes)
		if (h.crop == Rice)
			sandBank += bank(L, h, kHoleRadius - 0.5, 3, SAND, 0.6);
		else
			mud += bank(L, h, kHoleRadius + 0.5, 2.5, MUD, 0.9);
	context.telemetry.measure("savannah.holes.sand-bank-vertices", sandBank);
	// Game trails run from the outer end of each home approach to the rim of the nearest watering
	// hole, and down the approach itself towards town.
	int track = 0;
	if (o.trails)
		for (size_t k = 0; k < L.homes.size(); ++k)
			for (int side : {-1, 1})
			{
				const ShapePoint h = L.homes[k];
				const ShapePoint end{h.x - 9, h.y + side * 26};
				track += trail(L, {h.x - 9, h.y + side * 18}, end, 0, int(k));
				const Hole *nearest = nullptr;
				int best = 0;
				for (const Hole &hole : L.holes)
				{
					const int d = t.dist2(int(end.x), int(end.y), int(hole.at.x), int(hole.at.y));
					if (!nearest || d < best)
					{
						nearest = &hole;
						best = d;
					}
				}
				const double bow = int(context.bounded("savannah-trail-bends", 9)) - 4;
				// Out on the plain no town is the trail's own: it never cuts across one.
				track += trail(L, end, holeRim(t, *nearest, end), bow, -1);
			}
	context.telemetry.measure("savannah.trails.vertices", track);
	// Dried pools on the open plain, clear of every feature: a clay pan round a mud bottom, the dry
	// season's watering holes. They hold no water, so the plain round them stays dry for grass.
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
		const RadialShape outline(kPoolRadius + 1.5, 0.25, context, "savannah-pool-outlines");
		fillShape(L.terrain, t, s.x, s.y, outline, 0, CLAY);
		mud += bank(L, Hole{{double(s.x), double(s.y)}, 0, Maize}, 0, kPoolRadius - 1.5, MUD, 0);
		L.pools.push_back({double(s.x), double(s.y)});
		++pools;
	}
	context.telemetry.measure("savannah.pools.placed", pools);
	context.telemetry.measure("savannah.pools.proposed", proposedPools);
	// Plain features: groves, kopjes and potato patches, widely spaced. Their geometry is
	// independent of amount controls, including zero. Seals stop even later irrigation from
	// letting them invade the plains. A kopje is a granite core with stone on its flanks and a
	// scree skirt; the kopje control sets its share of features (Few 15%, Normal 25%, Many 40%).
	const int kopjeShare[] = {15, 25, 40};
	int groves = 0, kopjes = 0, potatoes = 0, proposedGroves = 0;
	const auto rock = periodicNoise(t.w, t.h, 5, context.stream("savannah-kopje-rock"));
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
		const int roll = int(context.bounded("savannah-clump-kinds", 100));
		const ShapePoint at{double(s.x), double(s.y)};
		if (roll < kopjeShare[o.kopjes])
		{
			plot(L, context, at, 4, STONE, nullptr, Amount::Stone, 4, 0, false,
				 "savannah-grove-outlines", SCREE);
			const RadialShape core(2.2, 0.3, context, "savannah-kopje-cores");
			forEachTileInShape(t, at.x, at.y, core, 0,
							   [&](int i, double, double)
							   { L.terrain[i] = rock[i] > 39000 ? OUTCROP : RIDGE_ROCK; });
			++kopjes;
		}
		else if (roll < kopjeShare[o.kopjes] + 15)
		{
			// Potatoes grow away from water, so a sealed patch is renewable food on the dry plain.
			plot(L, context, at, 3.5, -1, "potatoes", Amount::Wheat, 8, 0, false,
				 "savannah-grove-outlines");
			++potatoes;
		}
		else
		{
			plot(L, context, at, 3, WOOD, nullptr, Amount::Wood, 7, 0, false,
				 "savannah-grove-outlines");
			++groves;
		}
	}
	context.telemetry.measure("savannah.plains.clumps", groves + kopjes + potatoes);
	context.telemetry.measure("savannah.plains.groves", groves);
	context.telemetry.measure("savannah.plains.kopjes", kopjes);
	context.telemetry.measure("savannah.plains.potato-patches", potatoes);
	context.telemetry.measure("savannah.mud-vertices", mud);
	// Lone trees and camp-sites on the plain: candidates only. generate() plants those that land
	// on suitable ground.
	for (const Site &s : spreadPoints(t, kPlainTreeSpacing, context, "savannah-plain-trees"))
		if (const int i = t.at(s.x, s.y); !L.reserved[i] && L.plotOf[i] < 0)
			L.plainTrees.push_back(i);
	for (const Site &s : spreadPoints(t, kCampSpacing, context, "savannah-camps"))
		if (const int i = t.at(s.x, s.y); !L.reserved[i] && L.plotOf[i] < 0)
		{
			L.camps.push_back(i);
			reserve(L.grassFree, t, {double(s.x), double(s.y)}, kCampClearing);
		}
	context.telemetry.measure("savannah.plains.tree-candidates", L.plainTrees.size());
	context.telemetry.measure("savannah.plains.camp-candidates", L.camps.size());
	context.telemetry.measure("savannah.plains.proposed-clumps", proposedGroves);
	if (groves + kopjes + potatoes < proposedGroves)
		context.telemetry.fallback("savannah.plains.omitted-clumps",
								   "Clump reservations intersected existing features");
	std::vector<unsigned char> eligible(t.size(), 0);
	for (int i = 0; i < t.size(); ++i)
		eligible[i] = !L.reserved[i];
	// Period 14 gives patches several tiles across, not construction-destroying speckle. Dry
	// patches are laid as sand and then turned to cracked clay pans with gravel.
	const auto dry = periodicNoise(t.w, t.h, 14, context.stream("savannah-dry-patches"));
	sprinkleSand(L.terrain, t, eligible, o.dryPatches / 100.0, 3, [&](int i) { return dry[i]; });
	const auto earth = periodicNoise(t.w, t.h, 6, context.stream("savannah-earth"));
	for (int i = 0; i < t.size(); ++i)
		if (L.terrain[i] == SAND && eligible[i])
			L.terrain[i] = earth[i] > 42000 ? GRAVEL : CLAY;
		else if (L.terrain[i] == DIRT && earth[i] > 36000)
			L.terrain[i] = CLAY;
	layBeaches(L.terrain, t);
	// Each watering hole's open water, three steps inside its shallows, is deep.
	{
		std::vector<unsigned char> dryLand(t.size(), 0);
		for (int i = 0; i < t.size(); ++i)
			dryLand[i] = L.terrain[i] != WATER;
		const auto offshore = stepsFrom(t, dryLand);
		int deep = 0;
		for (const Hole &h : L.holes)
		{
			const int reach = int(kHoleRadius) + 2;
			for (int dy = -reach; dy <= reach; ++dy)
				for (int dx = -reach; dx <= reach; ++dx)
				{
					const int i = t.at(int(h.at.x) + dx, int(h.at.y) + dy);
					if (L.terrain[i] == WATER && offshore[i] >= kDeepSteps)
					{
						L.terrain[i] = DEEP_WATER;
						++deep;
					}
				}
		}
		context.telemetry.measure("savannah.holes.deep-vertices", deep);
	}
	// Beaches can trim the pond-facing edge of a plot, and a kopje's core covers its centre.
	// Eligibility is the final pure grass, not the pre-beach stamp. Seals only remove growth
	// connections; starter floors below still fail if too little renewable ground survives.
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

// The catalogue deposits Savannah places, resolved once from the map's resource registry.
struct Catalogue
{
	int tallGrass, scrub, deadTrees, campSite, maize, rice, potatoes, fish;
};
std::optional<Catalogue> catalogue(const Map &map)
{
	const auto &registry = map.resourceRegistry();
	int ids[8];
	const char *keys[] = {"tall-grass", "scrub",   "dead-trees", "camp-site",
						  "maize",      "rice",    "potatoes",   "fish"};
	for (int k = 0; k < 8; ++k)
	{
		const auto id = registry.find(keys[k]);
		if (!id)
			return std::nullopt;
		ids[k] = int(resourceIndex(*id));
	}
	return Catalogue{ids[0], ids[1], ids[2], ids[3], ids[4], ids[5], ids[6], ids[7]};
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
	const auto found = catalogue(game.map);
	if (!found)
	{
		context.detail = "Savannah needs the landscape resource catalogue (tall grass, maize, fish).";
		return false;
	}
	const Catalogue &c = *found;
	context.stage = "savannah terrain";
	writeVertices(game.map, L.terrain);
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
		const int percent = p.amount == Amount::Wheat  ? o.wheat
							: p.amount == Amount::Wood  ? o.wood
							: p.amount == Amount::Stone ? o.stone
														: o.fruit;
		const int resource = p.key == nullptr					? p.resource
							 : std::string(p.key) == "maize"	? c.maize
							 : std::string(p.key) == "rice"		? c.rice
																: c.potatoes;
		const int wanted = p.minimum + int(scaledCount(p.baseline, percent));
		const int planted =
			plantContainedPlot(game.map, L.t, p.tiles, fertility, resource, wanted, p.renewable);
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
	// Plain ground where nothing can grow: dry pure grass (growth chance zero) outside every plot
	// and reservation. Deposits here are scenery and finite stock: they never spread.
	const auto dryPlain = [&](int x, int y, int resource)
	{
		const int i = L.t.at(x, y);
		return !L.reserved[i] && L.plotOf[i] < 0 && fertility.at(L.t.x(x), L.t.y(y)) == 0 &&
			   game.map.terrainSupportsResourceAtByIndex(L.t.x(x), L.t.y(y), resource) &&
			   clearGround(game.map, L.t.x(x), L.t.y(y));
	};
	// Camp-sites first, each in its own small clearing in the grass: finite food, wood and cloth.
	int camps = 0;
	const int wantedCamps = int(scaledCount(int(L.camps.size()) / 2, o.camps));
	for (size_t k = 0; k < L.camps.size() && camps < wantedCamps; ++k)
	{
		const int i = L.camps[k];
		const int x = L.t.remainderX(i), y = i / L.t.w;
		if (!dryPlain(x, y, c.campSite))
			continue;
		game.map.setResourceByIndex(x, y, c.campSite, 1);
		++camps;
	}
	context.telemetry.measure("savannah.plains.camps", camps);
	// Lone trees: a candidate is a tree or a clump of two or three, or a dead tree (the candidate's
	// index says which), so they read as trees on the preview. The wood amount scales how many
	// candidates are planted; at zero there are none.
	int trees = 0, clumps = 0, dead = 0;
	const int wantedClumps = int(scaledCount(int(L.plainTrees.size()), o.wood));
	for (size_t k = 0; k < L.plainTrees.size() && clumps < wantedClumps; ++k)
	{
		const int i = L.plainTrees[k];
		const int x = L.t.remainderX(i), y = i / L.t.w;
		if (k % 4 == 3)
		{
			if (!dryPlain(x, y, c.deadTrees))
				continue;
			game.map.setResourceByIndex(x, y, c.deadTrees, 1);
			++dead;
			++clumps;
			continue;
		}
		if (!dryPlain(x, y, WOOD))
			continue;
		game.map.setResourceByIndex(x, y, WOOD, 1);
		++trees;
		++clumps;
		const int extra = int(k % 3);
		const int dx[] = {1, 0}, dy[] = {0, 1};
		for (int e = 0; e < extra; ++e)
			if (dryPlain(x + dx[e], y + dy[e], WOOD))
			{
				game.map.setResourceByIndex(L.t.x(x + dx[e]), L.t.y(y + dy[e]), WOOD, 1);
				++trees;
			}
	}
	context.telemetry.measure("savannah.plains.tree-clumps", clumps);
	context.telemetry.measure("savannah.plains.trees", trees);
	context.telemetry.measure("savannah.plains.dead-trees", dead);
	// Golden grass: the grassland share of the dry plain, ranked by smooth noise so it lies in
	// sweeps, takes tall grass; the next band down thins to scrub. Grass is geometry, not an
	// amount: it is friction on expansion, and the wood it yields is incidental.
	{
		const auto sweep = periodicNoise(L.t.w, L.t.h, 11, context.stream("savannah-grassland"));
		const auto thin = periodicNoise(L.t.w, L.t.h, 3, context.stream("savannah-scrub"));
		const auto fray = periodicNoise(L.t.w, L.t.h, 5, context.stream("savannah-grass-edges"));
		// Watered ground ends along the growth probe's square; grass stops up to four tiles short
		// of it on noise, so its edge never draws that square.
		std::vector<unsigned char> watered(L.t.size(), 0);
		for (int i = 0; i < L.t.size(); ++i)
			watered[i] = fertility.at(L.t.remainderX(i), i / L.t.w) > 0;
		const auto fromWatered = stepsFrom(L.t, watered);
		std::vector<std::pair<int, int>> plain;
		for (int i = 0; i < L.t.size(); ++i)
			if (!L.grassFree[i] && fromWatered[i] > fray[i] * 5 / 65536 &&
				dryPlain(L.t.remainderX(i), i / L.t.w, c.tallGrass))
				plain.push_back({-sweep[i], i});
		std::sort(plain.begin(), plain.end());
		const size_t grassTiles = plain.size() * o.grassland / 100;
		const size_t scrubBand = std::min(plain.size(), grassTiles + plain.size() / 8);
		int tall = 0, scrub = 0;
		for (size_t k = 0; k < scrubBand; ++k)
		{
			const int i = plain[k].second, x = L.t.remainderX(i), y = i / L.t.w;
			if (k < grassTiles)
			{
				game.map.setResourceByIndex(x, y, c.tallGrass, 1);
				++tall;
			}
			else if (thin[i] > 36000 && o.grassland > 0)
			{
				game.map.setResourceByIndex(x, y, c.scrub, 1);
				++scrub;
			}
		}
		context.telemetry.measure("savannah.plains.dry-tiles", plain.size());
		context.telemetry.measure("savannah.plains.tall-grass", tall);
		context.telemetry.measure("savannah.plains.scrub", scrub);
	}
	// Fish in each watering hole's shallows, on water tiles beside walkable shore so workers can
	// reach them. Fish spread only through water, so they need no seal.
	{
		const auto water = pureTiles(L.terrain, L.t, WATER);
		std::vector<int> shallows;
		for (const Hole &h : L.holes)
		{
			const int reach = int(kHoleRadius) + 2;
			for (int dy = -reach; dy <= reach; ++dy)
				for (int dx = -reach; dx <= reach; ++dx)
				{
					const int x = int(h.at.x) + dx, y = int(h.at.y) + dy, i = L.t.at(x, y);
					if (!water[i] || !game.map.terrainSupportsResourceAtByIndex(L.t.x(x), L.t.y(y), c.fish))
						continue;
					bool shore = false;
					for (int ny = -1; ny <= 1 && !shore; ++ny)
						for (int nx = -1; nx <= 1 && !shore; ++nx)
							shore = stepCost(game.map, L.t.x(x + nx), L.t.y(y + ny),
											 StepCosts::walking()) >= 0;
					if (shore)
						shallows.push_back(i);
				}
		}
		context.shuffle(shallows.begin(), shallows.end(), "savannah-fish");
		const int wantedFish =
			std::min(int(shallows.size()), int(scaledCount(4 * int(L.holes.size()), o.fish)));
		for (int k = 0; k < wantedFish; ++k)
			game.map.setResourceByIndex(L.t.remainderX(shallows[k]), shallows[k] / L.t.w, c.fish, 1);
		context.telemetry.measure("savannah.holes.fish", wantedFish);
	}
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
	// Dry-ground deposits are the only spreading ones allowed outside a plot: they cannot spread.
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
	// earth, mud and crop plots do not count; all footprint tiles must be reached on final land.
	for (size_t p = L.homes.size(); p < L.ponds.size(); ++p)
	{
		std::vector<unsigned char> shore(t.size(), 0);
		for (int i = 0; i < t.size(); ++i)
			shore[i] = walk.steps[i] >= 0 && t.dist2(int(L.ponds[p].x), int(L.ponds[p].y), t.remainderX(i),
													 i / t.w) <= kNeutralReserve * kNeutralReserve;
		if (buildSites(t, buildable, shore, 8) == 0)
			return "A neutral Savannah pond lacks reachable forward-inn room.";
	}
	// The ends of BOTH home approaches must join the same broad plains network. Erosion
	// by two is the shared five-tile passage test (2r+1); cardinal core connectivity avoids
	// counting a diagonal touching of two five-wide pads as a full-width passage.
	std::vector<unsigned char> passable(t.size(), 0);
	for (int i = 0; i < t.size(); ++i)
		passable[i] = stepCost(map, t.remainderX(i), i / t.w, StepCosts::walking()) >= 0;
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
			const int type = map.getResource(t.remainderX(i), i / t.w).type;
			if ((type != WHEAT && type != WOOD) || !fertility.at(t.remainderX(i), i / t.w))
				continue;
			for (int dy = -1; dy <= 1; ++dy)
				for (int dx = -1; dx <= 1; ++dx)
				{
					const int d = costs[t.at(t.remainderX(i) + dx, i / t.w + dy)];
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
					if (stepCost(map, t.remainderX(i), i / t.w, StepCosts::walking()) < 0 || costs[i] < 0)
						return "A Savannah home lost a five-tile approach to the shared plains.";
				}
		}
	}
	return "";
}
} // namespace
SavannahOptions::SavannahOptions(const GenerationRequest &r)
	: wateringHoles(r.option("watering-holes")), dryPatches(r.option("dry-patches")),
	  grassland(r.option("grassland")), kopjes(r.option("kopjes")),
	  trails(r.option("game-trails") != 0), wheat(r.option("wheat-amount")),
	  wood(r.option("wood-amount")), stone(r.option("stone-amount")),
	  algae(r.option("algae-amount")), fruit(r.option("fruit-amount")),
	  fish(r.option("fish-amount")), camps(r.option("camp-amount"))
{
}
GeneratorDefinition savannahDefinition()
{
	return {"savannah",
			45,
			"Savannah",
			4,
			false,
			{GeneratorControl::choice("watering-holes", "Watering holes",
									  {"Sparse", "Normal", "Many"}, 1, ControlGroup::Terrain)
				 .withSearchValues({0, 1, 2}),
			 GeneratorControl{"dry-patches", "Dry patches", 0, 20, 1, 8, ControlGroup::Terrain}
				 .withSearchRange(3, 12),
			 GeneratorControl{"grassland", "Grassland", 0, 100, 5, 70, ControlGroup::Terrain}
				 .withSearchRange(40, 90),
			 GeneratorControl::choice("kopjes", "Kopjes", {"Few", "Normal", "Many"}, 1,
									  ControlGroup::Terrain)
				 .withSearchValues({0, 1, 2}),
			 GeneratorControl::toggle("game-trails", "Game trails", true).withSearchValues({0, 1}),
			 GeneratorControl::percentage("wheat-amount", "Wheat amount"),
			 GeneratorControl::percentage("wood-amount", "Wood amount"),
			 GeneratorControl::percentage("stone-amount", "Stone amount"),
			 GeneratorControl::percentage("algae-amount", "Algae amount"),
			 GeneratorControl::percentage("fruit-amount", "Fruit amount"),
			 GeneratorControl::percentage("fish-amount", "Fish amount"),
			 GeneratorControl::percentage("camp-amount", "Camp-site amount")},
			generate,
			true,
			designFailure<design>,
			validateWorld,
			{"terrain:natural", "style:wide-open"}};
}
