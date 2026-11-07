// SPDX-License-Identifier: GPL-3.0-or-later
#include "PortageLakesGenerator.h"
#include "Contact.h"
#include "Drawing.h"
#include "Farmland.h"
#include "GenerationWorld.h"
#include "GenerationContext.h"
#include "Growth.h"
#include "Landmass.h"
#include "LatticeNoise.h"
#include "Morphology.h"
#include "Pipeline.h"
#include "Planting.h"
#include "Points.h"
#include "ResourceSemantics.h"
#include "Roads.h"
#include "Rock.h"
#include "Room.h"
#include "ScoredSettlements.h"
#include "Sketch.h"
#include "TerrainCatalog.h"
#include "Topology.h"
#include <climits>
#include <functional>
#include <algorithm>
#include <array>
#include <queue>
#include <numeric>
#include <map>
#include <set>
using namespace MapGeneration;

// Portage Lakes: crooked lakes, dry wooded ridges and sheltered farming bays.
// Cutting a ridge opens a public shortcut; swimming opens a different connection.
// The landscape is asymmetric. Complete settlement proposals are compared, but the
// start score is a heuristic, never a claim of exact fairness. Structural wood is
// dry on the FINAL terrain; open wheat fields follow the shore, with contained timber plots.
//
// DESIGN 2.0 (revision 8, 2026-11-07, after Bajada, Braided River, Caravanserai and Rain Shadow
// 2.0). The concept is unchanged: Boundary-Waters lake country, long glacial lakes in a forest of
// dry ridges and granite knots, portage trails between the lakes, farms in the bays. What changes
// is that the engine's terrain palette now carries the picture and the play:
// - Rock knots, the stone-resource blobs in the deepest forest, are Ridges-group rock (rockVertices;
//   impassable, towers shoot over). Parts too thin to make rock stay wooded, so the forest keeps
//   its walls. Every home gets a granite knob of its own, its quarry face 9 to 22 tiles away
//   (nearest first; it may stand in the edge of the woods, never with trees before its face), one
//   stencil turned by quarter turns to face the town, with a quarry face of the same stone for every
//   colony and a scree skirt behind. A smaller stencil serves every colony when one cannot fit the
//   large one, and when even that fails every home keeps revision 7's stone patch instead, so the
//   knobs never refuse a map revision 7 made. Forest knots get ambient quarry faces by
//   `stone-amount` and a frayed scree apron where open ground meets them.
// - Mud makes the shore meadows grow, which thins the forest by the lakes; when that costs the swim
//   its shortcut, the layout is furnished again with revision 7's plain beaches.
// - Trails and portage approaches are Paths (TRAIL; boardwalk where they meet wet ground): twice
//   the walking speed, buildable, nothing grows on them and, unlike the old sand, they inhibit no
//   crops, so the opening network pays and the portage cut's payoff is measured in walking time.
// - Bay farms stay grass. Loam fields (Fertile, watering the land at 48/256) were built and dropped:
//   the loam watered the dry forest round the bays (structural wood must stand on dry ground), and the
//   forest shrank until a compact map lost its portage, the shortcut the map is about.
// - Stretches of lake beach far from the homes are MUD (Rough: slow, holds nothing like the sand
//   it replaces, but inhibits nothing), so shore meadows there grow better.
// - Lake centres are DEEP_WATER (deepCore, three steps from land), keeping a shallow margin for
//   beaches and algae; swimming across a lake is slower, which is a feel change (`deep-lakes`).
// - Two sketches: the mechanics (canonical rule-group types every helper and check reads) and the
//   looks joined by composeLooks: dry grass on the wooded ridges, lush grass by the water, plain
//   grass in home gardens, kit plots and quarry approaches; a look-only `shield` (granite or
//   greenstone) picks the rock, the deep water and the shore mud artwork.
namespace
{
// The look-only bedrock of the Canadian Shield: everything plays alike.
struct Shield
{
	const char *name;
	TerrainType rock, talus, deep, margin;
};
constexpr std::array<Shield, 2> kShields{{
	{"granite", RIDGE_ROCK, SCREE, DEEP_WATER, MUD},
	{"greenstone", OUTCROP, SCREE, DARK_WATER, MUDFLAT},
}};
// Deep cores lie at least this many steps from any land vertex.
constexpr int kDeepSteps = 3;
const char *const kNoSwim = "Portage Lakes cannot fit a useful swimming shortcut on this seed.";
// Home knobs: their quarry face is this far (Chebyshev) from the home, nearest first.
constexpr int kKnobNear = 9, kKnobFar = 22;
struct Plot
{
	std::vector<int> tiles;
	int kind, minimum, baseline;
	int court = -1;
	std::vector<int> access{};
	int serviceHome = -1;
	int lake = -1;
};
struct Crossing
{
	int a = -1, b = -1, before = 0, after = 0;
	std::vector<int> cut;
};
struct Layout
{
	Torus t{1, 1};
	TerrainSketch terrain;
	std::vector<unsigned char> forest, rock, roads, reserved, lakeWater;
	std::vector<int> plotOf, candidates, homes;
	std::vector<Plot> plots;
	std::vector<std::vector<int>> proposals, countryClumps;
	std::vector<int> lakeCentres, algaePools, expansions, shoreFields;
	Crossing portage, swim;
	std::vector<Crossing> portages;
	bool compact = false;
	std::string failure;
	// 2.0: the looks painted over the mechanics, the quarry faces (tiles) and who owns each (-1 for
	// ambient faces), the home knobs' tiles, the shield, and counts kept for telemetry.
	TerrainSketch looks;
	// keep: expansion clearings and their paths, which no later stage may block.
	std::vector<unsigned char> quarry, knob, keep;
	std::vector<int> quarryOf, knobDistance;
	int shield = 0, knobFace = 0, ambientFaces = 0, ambientWanted = 0, rockKnots = 0;
};
std::string validateRequest(const GenerationRequest &r)
{
	if (const auto error = denseColonySizeFailure(r); !error.empty())
		return error;
	if (r.wDec < 6 || r.hDec < 6 || r.wDec > 9 || r.hDec > 9)
		return "Portage Lakes needs sides between 64 and 512 tiles.";
	if (r.nbTeams > std::min(r.wDec == DENSE_COLONY_MAP_EXPONENT && r.hDec == DENSE_COLONY_MAP_EXPONENT
		? TEAM_MAX_COUNT : DENSE_COLONY_BASE_LIMIT,
		std::max(2, (1 << r.wDec) * (1 << r.hDec) / 4096)))
		return "Too many colonies for this map; use a bigger map or fewer colonies.";
	return "";
}
void disc(std::vector<unsigned char> &mask, const Torus &t, int p, int radius)
{
	for (int y = -radius; y <= radius; ++y)
		for (int x = -radius; x <= radius; ++x)
			if (x * x + y * y <= radius * radius)
				mask[t.at(t.remainderX(p) + x, p / t.w + y)] = 1;
}
std::vector<unsigned char> land(const Layout &L)
{
	auto walkable = walkableTiles(L.terrain, L.t);
	// The home knobs (placed before the routes) are rock and quarry stone: nobody walks through them.
	const bool knobs = !L.knob.empty();
	for (int i = 0; i < L.t.size(); ++i)
		walkable[i] = walkable[i] && !L.forest[i] && !L.rock[i] && !(knobs && (L.knob[i] || L.quarryOf[i] >= 0));
	return walkable;
}
// Endpoint queries need only the first arrival, not a complete catchment. Keep
// reachFrom's eight-neighbour topology, admitted source and 256-step limit.
int distance(const Torus &t, const std::vector<unsigned char> &open, int a, int b)
{
	thread_local std::vector<int> steps, queue;
	if (steps.size() != size_t(t.size()))
		steps.assign(t.size(), -1);
	queue.clear();
	queue.push_back(a);
	steps[a] = 0;
	int result = -1;
	for (size_t head = 0; head < queue.size(); ++head)
	{
		const int i = queue[head], d = steps[i];
		if (i == b)
		{
			result = d;
			break;
		}
		if (d >= 256)
			continue;
		for (int dy = -1; dy <= 1; ++dy)
			for (int dx = -1; dx <= 1; ++dx)
			{
				if (!dx && !dy)
					continue;
				const int q = t.at(t.remainderX(i) + dx, i / t.w + dy);
				if (steps[q] < 0 && open[q])
				{
					steps[q] = d + 1;
					queue.push_back(q);
				}
			}
	}
	for (int i : queue)
		steps[i] = -1;
	return result;
}

// A bay query only observes tiles within radius of its centre. Copy enough
// surrounding corners for every 15-tile water/sand probe, including the extra
// corner needed to classify a pure tile. The interior values are exactly those
// of the full torus, including bays that cross its coordinate seam.
struct BayGrowth
{
	Torus t;
	int centre, half;
	bool whole = false;
	Fertility::Field field;
	BayGrowth(const TerrainSketch &terrain, const Torus &torus, int site, int radius)
		: t(torus), centre(site), half(radius + kCropProbeReach + 2)
	{
		Torus window(2 * half + 1, 2 * half + 1);
		if (window.size() >= t.size())
		{
			whole = true;
			field = cropGrowthField(terrain, t);
			return;
		}
		TerrainSketch drawn(window.size());
		for (int y = 0; y < window.h; ++y)
			for (int x = 0; x < window.w; ++x)
				drawn[y * window.w + x] =
					terrain[t.at(t.remainderX(centre) - half + x, centre / t.w - half + y)];
		field = cropGrowthField(drawn, window);
	}
	std::uint32_t at(int tile) const
	{
		if (whole)
			return field.at(t.remainderX(tile), tile / t.w);
		return field.at(half + t.offsetX(t.remainderX(centre), t.remainderX(tile)),
						half + t.offsetY(centre / t.w, tile / t.w));
	}
};
Layout landscape(const GenerationRequest &r, GenerationContext &c)
{
	Layout L;
	L.t = {1 << r.wDec, 1 << r.hDec};
	const auto &t = L.t;
	const PortageLakesOptions o(r);
	L.compact = std::min(t.w, t.h) == 64;
	L.terrain.assign(t.size(), GRASS);
	L.plotOf.assign(t.size(), -1);
	L.forest.assign(t.size(), 0);
	L.rock = L.forest;
	L.roads = L.forest;
	L.reserved = L.forest;
	const int spacing = L.compact ? 64 : 88;
	const auto axis = axesFor(t.w, t.h);
	auto centres = relaxPoints(t, spreadPoints(t, spacing, c, "portage-lake-sites", 90), 2);
	if (L.compact && t.size() == 4096)
		centres.resize(1);
	const double aspect = o.elongation / 100.;
	const double grain =
		axis.heading(1, 0) + (t.w == t.h ? c.bounded("portage-lake-heading", 180) * kPi / 180 : 0);
	std::vector<int> shapes{0, 1, 2};
	c.shuffle(shapes.begin(), shapes.end(), "portage-shape-family");
	for (size_t k = 0; k < centres.size(); ++k)
	{
		auto s = centres[k];
		double heading = grain + (int(c.bounded("portage-lake-shape", 101)) - 50) * 0.014;
		double reach = L.compact ? 21 : 40;
		if (centres.size() > 1)
			for (size_t j = 0; j < centres.size(); ++j)
				if (j != k)
					reach = std::min(
						reach,
						std::sqrt(double(t.dist2(s.x, s.y, centres[j].x, centres[j].y))) * .42);
		const double length = reach * std::sqrt(aspect / 2.0);
		const double width = (L.compact ? 4.0 : 7.0) / std::sqrt(aspect / 2.0);
		int variant = L.compact ? int(c.bounded("portage-lake-shape", 3)) : shapes[k % 3];
		double bend = (int(c.bounded("portage-lake-shape", 101)) - 50) / 50.;
		if (!L.compact)
			bend = (bend < 0 ? -1 : 1) * (.65 + std::abs(bend) * .5);
		AxisFrame frame{double(s.x), double(s.y), heading};
		std::vector<ShapePoint> nodes;
		for (int j = 0; j < 5; ++j)
		{
			double u = (j - 2) * length / 2;
			double v =
				bend * width * 2.0 * (variant == 1 ? std::sin(j * kPi / 2) : std::sin(j * kPi / 4));
			nodes.push_back(frame.at(u, v));
		}
		auto path = splinePath(nodes, 1.5);
		for (size_t j = 0; j < path.size(); ++j)
			path[j].halfWidth = width * (.45 + .55 * std::sin(kPi * (j + .5) / path.size()));
		strokePath(L.terrain, t, path, WATER);
		if (variant == 2 && !L.compact)
		{
			double middle = bend * width * 2;
			auto fork =
				bezierPath(nodes[2], frame.at(length * .2, middle - width * 1.4),
						   frame.at(length * .65, middle - width * 2.8), width * .65, 1.5, 16);
			strokePath(L.terrain, t, fork, WATER);
		}
		L.lakeCentres.push_back(t.at(int(std::lround(nodes[2].x)), int(std::lround(nodes[2].y))));
		c.telemetry.choice("portage-lakes.lake.shape",
						   variant == 0   ? "curved"
						   : variant == 1 ? "s-shaped"
										  : "forked",
						   int(k));
	}
	layBeaches(L.terrain, t);
	L.lakeWater = pureTiles(L.terrain, t, WATER);
	// Swimming should connect separate shores, not expose one continuous water highway.
	const auto regions = connectedRegions(L.lakeWater, t.w, t.h, true, GridNeighbors::Eight);
	std::vector<int> lakeSizes;
	for (int region : regions)
		if (region >= 0)
		{
			if (size_t(region) >= lakeSizes.size())
				lakeSizes.resize(region + 1);
			++lakeSizes[region];
		}
	const int waterTiles = std::accumulate(lakeSizes.begin(), lakeSizes.end(), 0);
	const int largest =
		lakeSizes.empty() ? 0 : *std::max_element(lakeSizes.begin(), lakeSizes.end());
	const int usefulLakes = std::count_if(lakeSizes.begin(), lakeSizes.end(),
										  [&](int size) { return size >= (L.compact ? 20 : 80); });
	c.telemetry.measure("portage-lakes.lakes.components", usefulLakes);
	c.telemetry.measure("portage-lakes.lakes.largest-share",
						waterTiles ? 100.0 * largest / waterTiles : 0);
	if (!usefulLakes ||
		(centres.size() > 1 && (usefulLakes < 2 || largest * 100 > waterTiles * 70)))
	{
		L.failure = "Portage Lakes needs separate swimming lakes on this seed.";
		return L;
	}

	auto grass = pureTiles(L.terrain, t, GRASS);
	auto shoreDistance = stepsFrom(t, L.lakeWater);
	auto room = buildAnchors(t, grass, 9);
	for (int i = 0; i < t.size(); ++i)
	{
		int anchor = t.at(t.remainderX(i) - 4, i / t.w - 4);
		if (room[anchor] && shoreDistance[i] >= 10 && shoreDistance[i] <= (L.compact ? 19 : 15))
			L.candidates.push_back(i);
	}
	c.shuffle(L.candidates.begin(), L.candidates.end(), "portage-bay-candidates");
	std::vector<unsigned char> candidates = tileMask(t, L.candidates);
	const auto open = walkableTiles(L.terrain, t);
	for (int p = 0; p < 4; ++p)
	{
		auto homes = farthestSites(t, candidates, open, r.nbTeams, c, "portage-start-proposals", 2);
		if (int(homes.size()) == r.nbTeams)
		{
			dealStarts(c, homes);
			L.proposals.push_back(homes);
		}
	}
	if (L.proposals.empty())
		L.failure = "Portage Lakes cannot fit shoreline settlements on this seed.";
	c.telemetry.measure("portage-lakes.lakes.placed", centres.size());
	c.telemetry.choice("portage-lakes.layout", L.compact ? "compact" : "full");
	// Looks only, from a stream of its own so no geometry moves.
	L.shield = o.shield > 0 ? std::min<int>(o.shield, kShields.size()) - 1
							: int(c.bounded("portage-shield", kShields.size()));
	c.telemetry.choice("portage-lakes.shield", kShields[L.shield].name);
	return L;
}

bool makePlot(Layout &L, GenerationContext &c, int centre, int radius, int kind, int minimum,
			  int baseline, bool narrowBay = false, int serviceHome = -1,
			  const std::vector<int> *shoreDistance = nullptr)
{
	const auto &t = L.t;
	// Grow a patch ALONG the shore, clipped by existing ground and reservations.
	// This makes farms fit bays instead of drawing a row of identical round gardens.
	const int cx = t.remainderX(centre), cy = centre / t.w;
	const double phase = c.bounded("portage-field-shapes", 6283) / 1000.;
	auto eligible = [&](int i)
	{
		int dx = t.offsetX(cx, t.remainderX(i)), dy = t.offsetY(cy, i / t.w);
		if (dx * dx + dy * dy > radius * radius * 5)
			return false;
		if (L.terrain[i] != GRASS || (shoreDistance && ((*shoreDistance)[i] < 0 || (*shoreDistance)[i] > 8)))
			return false;
		const int margin = shoreDistance ? 1 : 2;
		for (int y = -margin; y <= margin; ++y)
			for (int x = -margin; x <= margin; ++x)
			{
				int q = t.at(t.remainderX(i) + x, i / t.w + y);
				if (L.reserved[q] || L.rock[q] || L.roads[q] ||
					(shoreDistance ? L.terrain[q] == WATER : L.terrain[q] != GRASS))
					return false;
			}
		return true;
	};
	if (!eligible(centre))
		return false;
	int wx = 0, wy = 1, best = INT_MAX;
	for (int y = -22; y <= 22; ++y)
		for (int x = -22; x <= 22; ++x)
			if (L.terrain[t.at(cx + x, cy + y)] == WATER && x * x + y * y < best)
			{
				best = x * x + y * y;
				wx = x;
				wy = y;
			}
	const double length = std::max(1., std::hypot(wx, wy));
	const double nx = wx / length, ny = wy / length;
	std::vector<int> corners;
	std::set<int> queued{centre};
	using Node = std::pair<double, int>;
	std::priority_queue<Node, std::vector<Node>, std::greater<Node>> frontier;
	frontier.push({0, centre});
	const int wanted = kind >= CHERRY ? radius * radius * 3 : int(radius * radius * 4.5);
	while (!frontier.empty() && int(corners.size()) < wanted)
	{
		int i = frontier.top().second;
		frontier.pop();
		if (!eligible(i))
			continue;
		corners.push_back(i);
		for (auto step : kCardinalSteps)
		{
			int q = t.at(t.remainderX(i) + step[0], i / t.w + step[1]);
			if (!queued.insert(q).second)
				continue;
			int x = t.offsetX(cx, t.remainderX(q)), y = t.offsetY(cy, q / t.w);
			double across = x * nx + y * ny, along = -x * ny + y * nx;
			frontier.push(
				{across * across * 1.9 + along * along * .55 + (shoreDistance ? 0 : 4 * std::sin(along * .4 + phase)),
				 q});
		}
	}
	if (int(corners.size()) < int(radius * radius * (narrowBay ? 1.5 : 2.0)))
		return false;
	if (kind >= CHERRY)
	{
		for (int i : corners)
			disc(L.reserved, t, i, 1);
		L.plots.push_back({corners, kind, minimum, baseline});
		return true;
	}
	auto original = L.terrain;
	std::vector<int> tiles;
	if (kind == WHEAT)
	{
		// Keep the shore-shaped field interior, without carving a sand fence around it.
		// The surrounding meadow remains ordinary grass; actual trails are reserved later.
		const auto inside = tileMask(t, corners);
		for (int i : corners)
			if (inside[t.at(t.remainderX(i) + 1, i / t.w)] &&
				inside[t.at(t.remainderX(i), i / t.w + 1)] &&
				inside[t.at(t.remainderX(i) + 1, i / t.w + 1)])
				tiles.push_back(i);
	}
	else
		tiles = stampContainedPlot(L.terrain, t, corners, shoreDistance ? 1 : 2);
	int court = -1;
	std::vector<int> access;
	if (minimum > 0 || kind == WHEAT)
	{
		BayGrowth fertility(L.terrain, t, centre, int(std::ceil(radius * std::sqrt(5.))));
		// Leave opening room for a farmhouse/inn within the initial field footprint.
		if (kind == WHEAT)
		{
			auto inside = tileMask(t, tiles);
			auto anchors = buildAnchors(t, inside, 4);
			std::uint64_t best = 0;
			int bestDistance = INT_MAX;
			for (int a : tiles)
				if (anchors[a])
				{
					std::uint64_t nearby = 0;
					for (int y = -1; y <= 4; ++y)
						for (int x = -1; x <= 4; ++x)
							if (x < 0 || x == 4 || y < 0 || y == 4)
							{
								int q = t.at(t.remainderX(a) + x, a / t.w + y);
								if (inside[q])
									nearby += fertility.at(q);
							}
					int distance = 0;
					if (serviceHome >= 0)
						for (int dy : {0, 3})
							for (int dx : {0, 3})
								distance = std::max(
									distance, t.chebyshev(t.remainderX(serviceHome), serviceHome / t.w,
														  t.remainderX(a) + dx, a / t.w + dy));
					if (nearby &&
						(distance < bestDistance || (distance == bestDistance && nearby > best)))
					{
						bestDistance = distance;
						best = nearby;
						court = a;
					}
				}
			if (court < 0)
			{
				L.terrain = std::move(original);
				return false;
			}
			// An empty pocket surrounded by grain is not initially accessible.
			// Leave the shortest unseeded notch through this field to its open edge.
			std::map<int, int> parent;
			std::vector<int> queue;
			for (int y = 0; y < 4; ++y)
				for (int x = 0; x < 4; ++x)
				{
					int q = t.at(t.remainderX(court) + x, court / t.w + y);
					parent[q] = -1;
					queue.push_back(q);
				}
			int exit = -1;
			for (size_t head = 0; head < queue.size() && (exit < 0 || serviceHome >= 0); ++head)
			{
				int i = queue[head];
				for (auto step : kCardinalSteps)
				{
					int q = t.at(t.remainderX(i) + step[0], i / t.w + step[1]);
					if (!inside[q])
					{
						if (exit < 0 ||
							(serviceHome >= 0 &&
							 t.dist2(t.remainderX(serviceHome), serviceHome / t.w, t.remainderX(i), i / t.w) <
								 t.dist2(t.remainderX(serviceHome), serviceHome / t.w, t.remainderX(exit),
										 exit / t.w)))
							exit = i;
						if (serviceHome < 0)
							break;
						continue;
					}
					if (parent.emplace(q, i).second)
						queue.push_back(q);
				}
			}
			for (int i = exit; i >= 0; i = parent.at(i))
				access.push_back(i);
		}
		int fertile = 0, shoreTiles = 0;
		std::uint64_t potential = 0;
		for (int i : tiles)
		{
			const bool inCourt =
				court >= 0 && t.x(t.remainderX(i) - t.remainderX(court)) < 4 && t.y(i / t.w - court / t.w) < 4;
			const bool inAccess = std::find(access.begin(), access.end(), i) != access.end();
			const auto value = inCourt || inAccess ? 0 : fertility.at(i);
			fertile += value > 0;
			shoreTiles += value > 0 && shoreDistance && (*shoreDistance)[i] <= 3;
			potential += value;
		}
		if ((shoreDistance && (shoreTiles < 6 || fertile < 24 || potential < 2 * Fertility::kScale)) ||
			(minimum > 0 &&
			 (fertile < minimum ||
			  (kind == WHEAT && potential < (t.size() == 4096 ? 2 : 6) * Fertility::kScale))))
		{
			L.terrain = std::move(original);
			return false;
		}
	}
	for (int i : tiles)
		L.plotOf[i] = int(L.plots.size());
	for (int i : corners)
		disc(L.reserved, t, i, 3);
	L.plots.push_back({tiles, kind, minimum, baseline, court, access, serviceHome});
	return true;
}

bool furnishBay(Layout &L, GenerationContext &c, int home, bool starter)
{
	const auto &t = L.t;
	const int hx = t.remainderX(home), hy = home / t.w;
	const int radii[2] = {L.compact ? (t.size() == 4096 ? 6 : 7)
									: (t.size() / int(L.homes.size()) < 8192 ? 7 : 9),
						  L.compact ? 3 : 4};
	for (int kind = 0; kind < 2; ++kind)
	{
		struct Candidate
		{
			int tile;
			long long score;
		};
		std::vector<Candidate> candidates;
		BayGrowth field(L.terrain, t, home, 18);
		for (int dy = -18; dy <= 18; ++dy)
			for (int dx = -18; dx <= 18; ++dx)
			{
				int i = t.at(hx + dx, hy + dy), d = dx * dx + dy * dy;
				if (d < 64 || d > 324 || L.reserved[i] || L.terrain[i] != GRASS)
					continue;
				candidates.push_back({i, static_cast<long long>(field.at(i)) * 1000 - d * 700});
			}
		std::stable_sort(candidates.begin(), candidates.end(),
						 [](auto a, auto b) { return a.score > b.score; });
		bool placed = false;
		for (int pass = 0; pass < 2 && !placed; ++pass)
			for (auto candidate : candidates)
				if (makePlot(L, c, candidate.tile, radii[kind], kind ? WOOD : WHEAT,
							 starter ? (kind ? 12 : (L.compact ? 20 : 32)) : 0,
							 kind ? 12 : (L.compact ? 96 : 128), pass == 1,
							 starter ? home : -1))
				{
					placed = true;
					break;
				}
		if (!placed)
			return false;
	}
	return true;
}

// Give every existing lake additional farming ground, independently of where
// the settlement search happened to place towns or neutral bays.
bool extraShoreFields(Layout &L, GenerationContext &c, bool opening)
{
	const auto &t = L.t;
	auto owner = connectedRegions(L.lakeWater, t.w, t.h, true, GridNeighbors::Eight);
	std::vector<int> sizes, queue, depth(t.size(), -1);
	for (int i = 0; i < t.size(); ++i)
		if (owner[i] >= 0)
		{
			if (int(sizes.size()) <= owner[i])
				sizes.resize(owner[i] + 1);
			++sizes[owner[i]];
			queue.push_back(i);
			depth[i] = 0;
		}
	std::vector<std::vector<int>> candidates(sizes.size());
	for (size_t head = 0; head < queue.size(); ++head)
	{
		const int i = queue[head];
		if (depth[i] >= 2 && depth[i] <= 5 && L.terrain[i] == GRASS && !L.reserved[i])
			candidates[owner[i]].push_back(i);
		if (depth[i] == 8)
			continue;
		for (int dy = -1; dy <= 1; ++dy)
			for (int dx = -1; dx <= 1; ++dx)
			{
				const int q = t.at(t.remainderX(i) + dx, i / t.w + dy);
				if (depth[q] >= 0)
					continue;
				depth[q] = depth[i] + 1;
				owner[q] = owner[i];
				queue.push_back(q);
			}
	}
	L.shoreFields.resize(sizes.size(), 0);
	const int wanted = opening || L.compact ? 1 : 2;
	for (size_t lake = 0; lake < sizes.size(); ++lake)
	{
		if (sizes[lake] < (L.compact ? 20 : 80))
			continue;
		auto &sites = candidates[lake];
		c.shuffle(sites.begin(), sites.end(), "portage-extra-shore-fields");
		int &placed = L.shoreFields[lake];
		for (int radius : {L.compact ? 5 : 6, 4, 3})
		{
			for (int site : sites)
			{
				if (placed == wanted)
					break;
				if (makePlot(L, c, site, radius, WHEAT, 0, L.compact ? 64 : 96, true, -1, &depth))
				{
					L.plots.back().lake = int(lake);
					++placed;
				}
			}
		}
		if (!opening)
			c.telemetry.measure("portage-lakes.shore-fields.extra", placed, int(lake));
		if (!placed)
			return false;
	}
	return true;
}

// Expansion destinations have a real, unobstructed construction clearing. Their
// crops are optional rewards; the room remains useful even at zero fruit abundance.
bool expansion(Layout &L, int endpoint)
{
	const auto &t = L.t;
	const auto grass = pureTiles(L.terrain, t, GRASS);
	const int radius = L.compact ? 2 : 3;
	int best = -1, bestDistance = INT_MAX;
	for (int dy = -10; dy <= 10; ++dy)
		for (int dx = -10; dx <= 10; ++dx)
		{
			int p = t.at(t.remainderX(endpoint) + dx, endpoint / t.w + dy), d = dx * dx + dy * dy;
			if (d >= bestDistance)
				continue;
			bool fits = true;
			for (int y = -radius; y <= radius && fits; ++y)
				for (int x = -radius; x <= radius && fits; ++x)
				{
					int i = t.at(t.remainderX(p) + x, p / t.w + y);
					if (!grass[i] || L.reserved[i] || L.roads[i] ||
						std::find(L.portage.cut.begin(), L.portage.cut.end(), i) !=
							L.portage.cut.end())
						fits = false;
				}
			if (fits)
			{
				best = p;
				bestDistance = d;
			}
		}
	if (best < 0)
		return false;
	std::vector<unsigned char> blocked(t.size(), 0);
	for (int i = 0; i < t.size(); ++i)
		blocked[i] = L.terrain[i] == WATER || L.plotOf[i] >= 0 || L.reserved[i];
	for (int i : L.portage.cut)
		blocked[i] = 1;
	auto path = cheapestRoute(t, {endpoint}, tileMask(t, {best}), blocked, L.forest);
	if (path.empty())
		return false;
	for (int i : path)
	{
		L.forest[i] = L.rock[i] = 0;
		L.reserved[i] = 1;
		disc(L.keep, t, i, 1);
	}
	for (int y = -radius; y <= radius; ++y)
		for (int x = -radius; x <= radius; ++x)
		{
			int i = t.at(t.remainderX(best) + x, best / t.w + y);
			L.forest[i] = L.rock[i] = 0;
			L.reserved[i] = 1;
		}
	disc(L.keep, t, best, radius + 2);
	L.expansions.push_back(best);
	return true;
}
// Find a cut through a dry ridge. Endpoints and the three-tile corridor are kept
// away from homes and crops. Approaches are carved, leaving exactly depth rows of
// structural wood. The ordinary way around it remains open to every colony.
bool findPortage(Layout &L, GenerationContext &c, const PortageLakesOptions &o)
{
	const auto &t = L.t;
	const auto growth = cropGrowthField(L.terrain, t);
	auto open = land(L);
	auto grass = pureTiles(L.terrain, t, GRASS);
	struct Proposal
	{
		int a, b, cx, cy, dx, dy, len;
	};
	std::vector<Proposal> proposals;
	for (int i = 0; i < t.size(); i += 3)
		if (L.forest[i] && !L.reserved[i])
			for (auto d : std::array<std::array<int, 2>, 2>{{{1, 0}, {0, 1}}})
			{
				int dx = d[0], dy = d[1], left = 0, right = 0;
				while (left < 24 && L.forest[t.at(t.remainderX(i) - dx * left, i / t.w - dy * left)])
					++left;
				while (right < 24 && L.forest[t.at(t.remainderX(i) + dx * right, i / t.w + dy * right)])
					++right;
				if (left + right - 1 < o.depth || left >= 24 || right >= 24)
					continue;
				int a = t.at(t.remainderX(i) - dx * (left + 2), i / t.w - dy * (left + 2));
				int b = t.at(t.remainderX(i) + dx * (right + 2), i / t.w + dy * (right + 2));
				bool safe = open[a] && open[b];
				for (int j = -left - 2; j <= right + 2 && safe; ++j)
					for (int side = -2; side <= 2; ++side)
					{
						int q = t.at(t.remainderX(i) + dx * j - dy * side, i / t.w + dy * j + dx * side);
						if (!grass[q] || L.reserved[q] || L.rock[q])
							safe = false;
					}
				if (safe && left + right + 4 >= o.depth + (L.compact ? 8 : 14))
					proposals.push_back({a, b, t.remainderX(i), i / t.w, dx, dy, left + right + 4});
			}
	c.shuffle(proposals.begin(), proposals.end(), "portage-cut-candidates");
	std::vector<std::pair<int, Proposal>> ranked;
	for (size_t k = 0; k < std::min<size_t>(proposals.size(), 96); ++k)
	{
		auto p = proposals[k];
		bool dryPlug = true;
		for (int j = p.len / 2 - o.depth / 2; j < p.len / 2 - o.depth / 2 + o.depth; ++j)
			for (int side = -1; side <= 1; ++side)
			{
				int q =
					t.at(t.remainderX(p.a) + p.dx * j - p.dy * side, p.a / t.w + p.dy * j + p.dx * side);
				dryPlug &= growth.at(t.remainderX(q), q / t.w) == 0;
			}
		if (!dryPlug)
			continue;
		int before = distance(t, open, p.a, p.b);
		int gain = before - p.len;
		if (before > 0 && gain >= std::max(L.compact ? 4 : 8, before / (L.compact ? 10 : 5)))
			ranked.push_back({gain, p});
	}
	std::stable_sort(ranked.begin(), ranked.end(),
					 [](const auto &a, const auto &b) { return a.first > b.first; });
	for (const auto &[gain, p] : ranked)
	{
		auto saved = L;
		L.portage = {p.a, p.b, gain + p.len, p.len, {}};
		int ax = t.remainderX(p.a), ay = p.a / t.w;
		int length = p.len;
		std::vector<unsigned char> approach(t.size(), 0);
		int mid = length / 2;
		for (int j = 0; j <= length; ++j)
			for (int side = -1; side <= 1; ++side)
			{
				int q = t.at(ax + p.dx * j - p.dy * side, ay + p.dy * j + p.dx * side);
				if (j >= mid - o.depth / 2 && j < mid - o.depth / 2 + o.depth)
				{
					L.forest[q] = 1;
					L.portage.cut.push_back(q);
				}
				else
				{
					L.forest[q] = 0;
					L.roads[q] = 1;
					approach[q] = 1;
				}
			}
		// The approaches are trail (Paths: fast, never overgrown). A trail corner must not touch the
		// plug's grass tiles, or the plug's wood would lose its ground.
		auto protect = tileMask(t, L.portage.cut);
		for (int i = 0; i < t.size(); ++i)
			if (approach[i])
			{
				bool touches = false;
				for (int dy = -1; dy <= 0; ++dy)
					for (int dx = -1; dx <= 0; ++dx)
						touches |= protect[t.at(t.remainderX(i) + dx, i / t.w + dy)];
				if (!touches)
					L.terrain[i] = TRAIL;
			}
		if (!expansion(L, L.portage.a) || !expansion(L, L.portage.b))
		{
			L = std::move(saved);
			continue;
		}
		c.telemetry.measure("portage-lakes.portage.depth", o.depth, int(L.portages.size()));
		c.telemetry.measure("portage-lakes.portage.wood-tiles", L.portage.cut.size(),
							int(L.portages.size()));
		c.telemetry.measure("portage-lakes.portage.walk-before", L.portage.before,
							int(L.portages.size()));
		c.telemetry.measure("portage-lakes.portage.walk-after", L.portage.after,
							int(L.portages.size()));
		return true;
	}
	return false;
}
// A home knob in its own frame: u across the face, v away from the home. Face tiles (stone on
// grass) take the side towards the town; the rest is rock at least two tiles thick. One stencil,
// turned by quarter turns, for every colony, so every home's quarry holds the same stone.
struct KnobTile
{
	int u, v;
	bool face;
};
std::vector<KnobTile> knobStencil(bool small)
{
	std::vector<KnobTile> tiles;
	const int half = small ? 2 : 3, deep = small ? 3 : 4;
	for (int v = 0; v <= deep; ++v)
		for (int u = -half; u <= half; ++u)
		{
			const bool edgeRow = v == 0 || v == deep, corner = std::abs(u) == half;
			if (edgeRow && corner)
				continue;
			tiles.push_back({u, v, v == 0 || (v == 1 && corner)});
		}
	return tiles;
}
// Every home's knob, nearest first by the face's Chebyshev distance from the home, then by how
// squarely it faces the town. When any home cannot fit the large stencil, every home takes the
// small one; false when even that cannot fit.
bool placeKnobs(Layout &L, GenerationContext &c)
{
	const auto &t = L.t;
	const auto grass = pureTiles(L.terrain, t, GRASS);
	std::vector<unsigned char> wet(t.size(), 0);
	for (int i = 0; i < t.size(); ++i)
		wet[i] = L.terrain[i] == WATER || L.terrain[i] == MUD;
	const auto fromWater = stepsFrom(t, wet);
	// Room for a knob: open meadow, not the general reservation (which also holds every field's wide
	// margin) but what must stay free: the home clearings, two tiles round every plot, the trails,
	// the portage plugs and their margins, the landings, clearings and their paths, the algae pools.
	std::vector<unsigned char> taboo(t.size(), 0);
	for (int h : L.homes)
		disc(taboo, t, h, 7);
	for (const auto &plot : L.plots)
		for (int i : plot.tiles)
			disc(taboo, t, i, 1);
	for (const auto &crossing : L.portages)
	{
		for (int i : crossing.cut)
			disc(taboo, t, i, 3);
		for (int i : {crossing.a, crossing.b})
			if (i >= 0)
				disc(taboo, t, i, 3);
	}
	for (int i : {L.swim.a, L.swim.b})
		if (i >= 0)
			disc(taboo, t, i, 3);
	for (int p : L.algaePools)
		disc(taboo, t, p, 6);
	std::vector<unsigned char> clear(t.size(), 0);
	for (int i = 0; i < t.size(); ++i)
		clear[i] = grass[i] && !taboo[i] && !L.keep[i] && !L.roads[i] && L.plotOf[i] < 0 && !L.rock[i] &&
				   fromWater[i] >= 3;
	for (bool small : {false, true})
	{
		const auto stencil = knobStencil(small);
		std::vector<unsigned char> taken(t.size(), 0);
		std::vector<std::vector<std::pair<int, bool>>> knobs;
		std::vector<int> distances;
		bool all = true;
		for (int home : L.homes)
		{
			const int hx = t.remainderX(home), hy = home / t.w;
			int bestScore = INT_MAX, bestDistance = -1;
			std::vector<std::pair<int, bool>> best;
			for (int d = kKnobNear; d <= kKnobFar && best.empty(); ++d)
				for (int dy = -d; dy <= d; ++dy)
					for (int dx = -d; dx <= d; ++dx)
					{
						if (std::max(std::abs(dx), std::abs(dy)) != d)
							continue;
						const int score = std::min(std::abs(dx), std::abs(dy));
						if (score >= bestScore)
							continue;
						const bool alongX = std::abs(dx) >= std::abs(dy);
						const int sign = (alongX ? dx : dy) < 0 ? -1 : 1;
						std::vector<std::pair<int, bool>> tiles;
						bool fits = true;
						for (const auto &k : stencil)
						{
							const int x = hx + dx + (alongX ? sign * k.v : k.u);
							const int y = hy + dy + (alongX ? k.u : sign * k.v);
							for (int my = -1; my <= 1 && fits; ++my)
								for (int mx = -1; mx <= 1 && fits; ++mx)
								{
									// Woods may stand where the knob goes (it becomes rock) and round
									// it, but the ground before the quarry face must be open.
									const int q = t.at(x + mx, y + my);
									fits = clear[q] && !taken[q] && (!k.face || (mx == 0 && my == 0) || !L.forest[q]);
								}
							if (!fits)
								break;
							tiles.push_back({t.at(x, y), k.face});
						}
						if (fits)
						{
							bestScore = score;
							bestDistance = d;
							best = std::move(tiles);
						}
					}
			if (best.empty())
			{
				all = false;
				break;
			}
			// Knobs keep two tiles apart, so no two homes' quarries share an approach.
			std::vector<unsigned char> mine(t.size(), 0);
			for (const auto &[tile, face] : best)
				mine[tile] = 1;
			for (int i = 0; i < t.size(); ++i)
				if (mine[i])
					for (int my = -2; my <= 2; ++my)
						for (int mx = -2; mx <= 2; ++mx)
							taken[t.at(t.remainderX(i) + mx, i / t.w + my)] = 1;
			knobs.push_back(std::move(best));
			distances.push_back(bestDistance);
		}
		if (!all)
			continue;
		for (size_t k = 0; k < knobs.size(); ++k)
		{
			int faces = 0;
			for (const auto &[tile, face] : knobs[k])
			{
				(face ? L.quarry : L.knob)[tile] = 1;
				L.forest[tile] = 0;
				if (face)
				{
					L.quarryOf[tile] = int(k);
					++faces;
				}
				disc(L.reserved, t, tile, 1);
			}
			L.knobFace = faces;
			c.telemetry.measure("portage-lakes.knob.distance", distances[k], int(k));
		}
		L.knobDistance = distances;
		c.telemetry.choice("portage-lakes.knob.stencil", small ? "small" : "large");
		c.telemetry.measure("portage-lakes.knob.face-tiles", L.knobFace);
		return true;
	}
	return false;
}

// The 2.0 terrain over the finished layout: forest knots and home knobs become rock with quarry
// faces, scree aprons where open ground meets the forest knots, deep lake cores, and the looks.
void finishTerrain(Layout &L, GenerationContext &c, const PortageLakesOptions &o,
				   const std::vector<unsigned char> &homeBuffer)
{
	const auto &t = L.t;
	const int n = t.size();
	// Ambient quarry faces on the forest knots, by stone-amount: a face of up to three tiles on the
	// side of a knot that open ground touches, never within two tiles of water, its tiles and the
	// ring before it on plain grass.
	{
		const auto open = land(L);
		std::vector<unsigned char> wet(n, 0);
		for (int i = 0; i < n; ++i)
			wet[i] = L.terrain[i] == WATER || L.terrain[i] == MUD;
		const auto fromWater = stepsFrom(t, wet);
		const auto knots = connectedRegions(L.rock, t.w, t.h, true, GridNeighbors::Eight);
		std::vector<int> sizes;
		for (int i = 0; i < n; ++i)
			if (knots[i] >= 0)
			{
				if (int(sizes.size()) <= knots[i])
					sizes.resize(knots[i] + 1);
				++sizes[knots[i]];
			}
		const auto openBeside = [&](int i)
		{
			for (auto step : kCardinalSteps)
				if (open[t.at(t.remainderX(i) + step[0], i / t.w + step[1])])
					return true;
			return false;
		};
		// A face in the woods (trees before it, to be cut) is the second choice: most knots sit deep in
		// the forest, and without them the faces could not follow stone-amount.
		const auto woodsBeside = [&](int i)
		{
			for (auto step : kCardinalSteps)
			{
				const int q = t.at(t.remainderX(i) + step[0], i / t.w + step[1]);
				if (L.forest[q] && !L.rock[q])
					return true;
			}
			return false;
		};
		std::vector<int> candidates, wooded;
		for (int i = 0; i < n; ++i)
			if (L.rock[i] && sizes[knots[i]] >= 6)
			{
				if (openBeside(i))
					candidates.push_back(i);
				else if (woodsBeside(i))
					wooded.push_back(i);
			}
		c.shuffle(candidates.begin(), candidates.end(), "portage-quarry-faces");
		c.shuffle(wooded.begin(), wooded.end(), "portage-quarry-faces");
		const size_t openCandidates = candidates.size();
		candidates.insert(candidates.end(), wooded.begin(), wooded.end());
		L.rockKnots = int(std::count_if(sizes.begin(), sizes.end(), [](int s) { return s >= 6; }));
		L.ambientWanted = int(std::min<std::int64_t>(L.rockKnots, scaledCount((L.rockKnots + 3) / 4, o.stone)));
		std::vector<unsigned char> faced(sizes.size(), 0);
		const auto plainCorners = [&](int i)
		{
			for (int corner = 0; corner < 4; ++corner)
				if (L.terrain[t.at(t.remainderX(i) + (corner & 1), i / t.w + (corner >> 1))] != GRASS)
					return false;
			return true;
		};
		for (size_t k = 0; k < candidates.size(); ++k)
		{
			const int centre = candidates[k];
			const bool inWoods = k >= openCandidates;
			if (L.ambientFaces >= L.ambientWanted)
				break;
			if (faced[knots[centre]])
				continue;
			std::vector<int> face;
			for (int dy = -1; dy <= 1; ++dy)
				for (int dx = -1; dx <= 1; ++dx)
				{
					const int q = t.at(t.remainderX(centre) + dx, centre / t.w + dy);
					if (L.rock[q] && knots[q] == knots[centre] && (inWoods ? woodsBeside(q) : openBeside(q)))
						face.push_back(q);
				}
			bool fits = true;
			for (int q : face)
				for (int my = -1; my <= 1 && fits; ++my)
					for (int mx = -1; mx <= 1 && fits; ++mx)
					{
						const int r = t.at(t.remainderX(q) + mx, q / t.w + my);
						fits = plainCorners(r) && fromWater[r] >= 3 && L.plotOf[r] < 0 && !L.quarry[r];
					}
			if (!fits)
				continue;
			faced[knots[centre]] = 1;
			for (int q : face)
			{
				L.quarry[q] = 1;
				L.rock[q] = 0;
			}
			++L.ambientFaces;
		}
	}
	// Rock: a vertex is rock where its four tiles are rock tiles. A forest-knot tile the rock cannot
	// block (a sliver under two tiles thick) keeps its wood when it can, so the forest keeps its walls.
	std::vector<unsigned char> rockTiles(n, 0);
	for (int i = 0; i < n; ++i)
		rockTiles[i] = (L.rock[i] || L.knob[i]) && !L.quarry[i];
	const auto rock = rockVertices(t, rockTiles);
	std::vector<unsigned char> blocked(n, 0);
	int rockArea = 0;
	for (int i = 0; i < n; ++i)
	{
		int corners = 0;
		for (int corner = 0; corner < 4; ++corner)
			corners += rock[t.at(t.remainderX(i) + (corner & 1), i / t.w + (corner >> 1))];
		blocked[i] = corners >= 2;
		rockArea += blocked[i];
		if (L.rock[i] && !blocked[i])
		{
			L.rock[i] = 0;
			L.forest[i] = corners == 0;
		}
	}
	for (int i = 0; i < n; ++i)
		if (rock[i])
			L.terrain[i] = RIDGE_ROCK;
	// Scree aprons round the forest knots, on open ground only: never a corner of a wooded tile (its
	// wood would lose its ground), a field, a trail, a quarry or its approach, or a home's buffer.
	int scree = 0;
	{
		std::vector<unsigned char> forestRock(n, 0), eligible(n, 0);
		for (int i = 0; i < n; ++i)
			forestRock[i] = blocked[i] && !L.knob[i];
		const auto approach = dilate(t, L.quarry, 1);
		for (int v = 0; v < n; ++v)
		{
			if (L.terrain[v] != GRASS || homeBuffer[v])
				continue;
			bool ok = true;
			for (int touch = 0; touch < 4 && ok; ++touch)
			{
				const int q = t.at(t.remainderX(v) - (touch & 1), v / t.w - (touch >> 1));
				ok = !L.forest[q] && L.plotOf[q] < 0 && !L.roads[q] && !approach[q] && !L.knob[q] &&
					 !L.reserved[q];
			}
			eligible[v] = ok;
		}
		const auto fray = periodicNoise(t.w, t.h, 6, c.stream("portage-scree"));
		const auto apron = talusApron(t, forestRock, eligible, fray, 1, 2, 36000);
		for (int v = 0; v < n; ++v)
			if (apron[v])
			{
				L.terrain[v] = SCREE;
				++scree;
			}
		// The home knobs stand in open meadow: a skirt of scree one corner wide round their back and
		// sides (never on the quarry's side), the same stencil's skirt for every home wherever the
		// meadow allows it.
		std::vector<unsigned char> knob(n, 0);
		for (int i = 0; i < n; ++i)
			knob[i] = L.knob[i] || L.quarryOf[i] >= 0;
		const auto skirt = dilate(t, knob, 1);
		for (int v = 0; v < n; ++v)
		{
			if (L.terrain[v] != GRASS)
				continue;
			bool ok = true;
			for (int touch = 0; touch < 4 && ok; ++touch)
			{
				const int q = t.at(t.remainderX(v) - (touch & 1), v / t.w - (touch >> 1));
				ok = skirt[q] && !approach[q] && !L.forest[q] && L.plotOf[q] < 0 && !L.roads[q];
			}
			if (ok)
			{
				L.terrain[v] = SCREE;
				++scree;
			}
		}
	}
	// Deep lake cores, the algae pools left shallow.
	int deep = 0;
	if (o.deep)
	{
		std::vector<unsigned char> lake(n, 0);
		for (int i = 0; i < n; ++i)
		{
			lake[i] = L.terrain[i] == WATER;
			for (int p : L.algaePools)
				lake[i] = lake[i] && t.dist2(t.remainderX(p), p / t.w, t.remainderX(i), i / t.w) > 64;
		}
		const auto core = deepCore(t, lake, kDeepSteps);
		for (int i = 0; i < n; ++i)
			if (core[i])
			{
				L.terrain[i] = DEEP_WATER;
				++deep;
			}
	}
	// The looks, inside each vertex's rule group: dry grass on the wooded ridges and wherever nothing
	// would grow, lush grass by the water and in the best meadows, plain grass in home gardens, the
	// kit plots and every quarry's approach; the shield's rock, talus, deep water and mud; trail
	// turned to boardwalk where it meets wet ground.
	const Shield &shield = kShields[L.shield];
	const auto growth = cropGrowthField(L.terrain, t);
	std::vector<unsigned char> wet(n, 0), soggy(n, 0), plain(n, 0);
	for (int i = 0; i < n; ++i)
	{
		wet[i] = L.terrain[i] == WATER || L.terrain[i] == DEEP_WATER;
		soggy[i] = wet[i] || L.terrain[i] == MUD;
	}
	const auto nearWater = stepsFrom(t, wet);
	const auto nearSoggy = dilate(t, soggy, 2);
	const auto approach = dilate(t, L.quarry, 1);
	// A vertex is under the forest when any tile it is a corner of is wooded.
	std::vector<unsigned char> underForest(n, 0);
	for (int i = 0; i < n; ++i)
		if (L.forest[i])
			for (int corner = 0; corner < 4; ++corner)
				underForest[t.at(t.remainderX(i) + (corner & 1), i / t.w + (corner >> 1))] = 1;
	const auto nearForest = dilate(t, underForest, 2);
	for (int i = 0; i < n; ++i)
	{
		const int plot = L.plotOf[i];
		if (approach[i] || (plot >= 0 && (L.plots[plot].kind == WOOD || L.plots[plot].serviceHome >= 0)))
			for (int corner = 0; corner < 4; ++corner)
				plain[t.at(t.remainderX(i) + (corner & 1), i / t.w + (corner >> 1))] = 1;
	}
	const auto fray = periodicNoise(t.w, t.h, 8, c.stream("portage-looks"));
	const auto garden = [&](int i)
	{
		const double reach = 8 + 6.0 * fray[i] / 65536;
		for (int h : L.homes)
			if (t.dist2(t.remainderX(h), h / t.w, t.remainderX(i), i / t.w) <= reach * reach)
				return true;
		return false;
	};
	L.looks.assign(n, 0);
	int mud = 0;
	for (int i = 0; i < n; ++i)
		switch (L.terrain[i])
		{
		case GRASS:
		{
			if (plain[i] || garden(i))
				break;
			// The forest floor of the dry ridges is dry grass; open dry ground only in patches, so a
			// clearing still reads as meadow rather than steppe.
			const auto g = growth.at(t.remainderX(i), i / t.w);
			if (g == 0 && (underForest[i] || (nearForest[i] && fray[i] > 30000)))
				L.looks[i] = DRY_GRASS;
			else if (nearWater[i] <= 3 || g >= Fertility::kScale * 45 / 100)
				L.looks[i] = LUSH_GRASS;
			break;
		}
		case MUD:
			L.looks[i] = shield.margin;
			++mud;
			break;
		case RIDGE_ROCK:
			L.looks[i] = shield.rock;
			break;
		case SCREE:
			L.looks[i] = shield.talus;
			break;
		case DEEP_WATER:
			L.looks[i] = shield.deep;
			break;
		case TRAIL:
			if (nearSoggy[i])
				L.looks[i] = BOARDWALK;
			break;
		default:
			break;
		}
	c.telemetry.measure("portage-lakes.rock.knots", L.rockKnots);
	c.telemetry.measure("portage-lakes.rock.blocked-tiles", rockArea);
	c.telemetry.measure("portage-lakes.quarries.ambient-wanted", L.ambientWanted);
	c.telemetry.measure("portage-lakes.quarries.ambient", L.ambientFaces);
	c.telemetry.measure("portage-lakes.scree.vertices", scree);
	c.telemetry.measure("portage-lakes.lakes.deep-vertices", deep);
	c.telemetry.measure("portage-lakes.shore.final-mud-vertices", mud);
}

Layout furnish(const Layout &base, const std::vector<int> &homes, GenerationContext &c, bool wetShores = true,
			   bool knobsFirst = true)
{
	Layout L = base;
	const auto &t = L.t;
	const PortageLakesOptions o(c.request);
	L.homes = homes;
	L.quarry.assign(t.size(), 0);
	L.knob.assign(t.size(), 0);
	L.keep.assign(t.size(), 0);
	L.quarryOf.assign(t.size(), -1);
	for (int h : homes)
		disc(L.reserved, t, h, 6);
	for (int h : homes)
		if (!furnishBay(L, c, h, true))
		{
			L.failure = "Portage Lakes cannot fit renewable shore fields beside every settlement.";
			return L;
		}
	// Every home's granite knob and its quarry face, on open meadow near the town, right after the home
	// bays and before the algae pools, the lakes' extra shore fields, the neutral bays, the trails and the
	// portages take the meadow: placed last, the knobs found no room beside every home on 6 of 16 maps.
	// When even the small stencil cannot fit beside every home, every home keeps revision 7's stone
	// patch instead (one rule for every colony), so the knobs never refuse a map revision 7 made. When the
	// knobs placed first leave too little for the rest (furnishLakes), they are placed last instead.
	const auto knobs = [&]
	{
		if (!placeKnobs(L, c))
		{
			c.telemetry.choice("portage-lakes.knob.stencil", "none: stone patches");
			c.telemetry.fallback("portage-lakes.knob.stencil", "no knob stencil fits beside every home: stone patches");
		}
	};
	if (knobsFirst)
		knobs();
	// Extra farming bays are found in the remaining landscape, never stamped towns.
	// Separate algae pools never connect to the swimming lakes.
	for (int h : homes)
	{
		if (t.size() == 4096 && !L.algaePools.empty())
			break;
		bool placed = false;
		for (int reach = 18; reach <= 26 && !placed; reach += 8)
			for (int dy = -reach; dy <= reach && !placed; ++dy)
				for (int dx = -reach; dx <= reach && !placed; ++dx)
				{
					if (dx * dx + dy * dy < 100 || dx * dx + dy * dy > reach * reach)
						continue;
					int p = t.at(t.remainderX(h) + dx, h / t.w + dy);
					bool fits = true;
					for (int yy = -5; yy <= 5; ++yy)
						for (int xx = -5; xx <= 5; ++xx)
						{
							int q = t.at(t.remainderX(p) + xx, p / t.w + yy);
							if (L.reserved[q] || L.terrain[q] != GRASS)
								fits = false;
						}
					if (!fits)
						continue;
					RadialShape shape(L.compact ? 2.5 : 3.5, .15, c, "portage-pool-shapes");
					fillShape(L.terrain, t, t.remainderX(p), p / t.w, shape, 0, WATER);
					disc(L.reserved, t, p, 6);
					L.algaePools.push_back(p);
					placed = true;
				}
	}
	// On full maps, budget one field per lake before optional bays and routes
	// consume the remaining shore. Compact maps must fit their swimming landing
	// first; their single extra field fits around the finished network.
	if (!L.compact && !extraShoreFields(L, c, true))
	{
		L.failure = "Portage Lakes cannot fit additional shore fields beside every lake.";
		return L;
	}
	std::vector<int> neutralBays;
	int neutral = 0, neutralTries = 0;
	// A failed bay restores L; iterate the immutable base, never L’s replaced vector.
	for (int h : base.candidates)
	{
		if (L.compact)
			break;
		bool away = true;
		for (int home : homes)
			away &=
				t.dist2(t.remainderX(h), h / t.w, t.remainderX(home), home / t.w) > (L.compact ? 20 * 20 : 32 * 32);
		if (!away || L.reserved[h])
			continue;
		if (++neutralTries > 64)
			break;
		auto saved = L;
		disc(L.reserved, t, h, 5);
		if (furnishBay(L, c, h, false))
		{
			for (int f = 0; f < 3; ++f)
				for (int radius = 8; radius <= 17; ++radius)
					if (makePlot(L, c, t.at(t.remainderX(h) + radius, h / t.w + (f - 1) * 6), 2, CHERRY + f,
								 0, 2))
						break;
			neutralBays.push_back(h);
			++neutral;
			if (neutral >= std::max(1, int(homes.size())))
				break;
		}
		else
			L = std::move(saved);
	}
	layBeaches(L.terrain, t);
	std::vector<unsigned char> homeBuffer(t.size(), 0);
	for (int h : homes)
		disc(homeBuffer, t, h, L.compact ? 12 : (t.size() / int(homes.size()) < 8192 ? 18 : 24));
	// Wet shore margins: stretches of lake beach far from every home turn to mud, slow and holding
	// nothing like the beach, but inhibiting no crops. Never beside the algae pools, whose growth
	// needs the beach's shore support, nor in a home's buffer, where people walk and build. Mud waters
	// the shore meadows, so the forest there thins; when that costs the swim its shortcut the layout is
	// furnished again with plain beaches (furnishLakes).
	if (wetShores)
	{
		const auto mud = periodicNoise(t.w, t.h, L.compact ? 10 : 14, c.stream("portage-shore-mud"));
		std::vector<unsigned char> margin(t.size(), 0);
		// A knob stands 3 steps from water or mud (placeKnobs); no mud comes nearer than that.
		std::vector<unsigned char> knob(t.size(), 0);
		for (int i = 0; i < t.size(); ++i)
			knob[i] = L.knob[i] || L.quarryOf[i] >= 0;
		const auto nearKnob = dilate(t, knob, 2);
		for (int i = 0; i < t.size(); ++i)
		{
			if (L.terrain[i] != SAND || homeBuffer[i] || mud[i] < 38000 || nearKnob[i])
				continue;
			bool lake = false, pool = false;
			for (int dy = -1; dy <= 1; ++dy)
				for (int dx = -1; dx <= 1; ++dx)
					lake |= L.terrain[t.at(t.remainderX(i) + dx, i / t.w + dy)] == WATER;
			for (int p : L.algaePools)
				pool |= t.dist2(t.remainderX(p), p / t.w, t.remainderX(i), i / t.w) <= 12 * 12;
			margin[i] = lake && !pool;
		}
		int count = 0;
		for (int i = 0; i < t.size(); ++i)
			if (margin[i])
			{
				L.terrain[i] = MUD;
				++count;
			}
		c.telemetry.measure("portage-lakes.shore.mud-vertices", count);
	}
	auto grass = pureTiles(L.terrain, t, GRASS);
	auto growth = cropGrowthField(L.terrain, t);
	auto noise = periodicNoise(t.w, t.h, L.compact ? 20 : 36, c.stream("portage-woodland"));
	std::vector<unsigned char> dry(t.size(), 0);
	for (int i = 0; i < t.size(); ++i)
		dry[i] = grass[i] && !L.reserved[i] && !homeBuffer[i] && growth.at(t.remainderX(i), i / t.w) == 0;
	L.forest = noisyShare(dry, noise, 75);
	// Small rock knots in the deepest forest leave the wooded route options dominant.
	auto rockNoise = periodicNoise(t.w, t.h, 9, c.stream("portage-rock"));
	for (int i = 0; i < t.size(); ++i)
		if (L.forest[i] && rockNoise[i] > 56000)
		{
			L.rock[i] = 1;
			L.forest[i] = 0;
		}
	// Start with all towns joined by broad sand paths. Crops, lakes and rocks are protected.
	auto protectedTiles = tileMask(t, {});
	for (int i = 0; i < t.size(); ++i)
		protectedTiles[i] = L.plotOf[i] >= 0 || L.rock[i];
	{
		// The home knobs and a tile round them (the ground before the quarry face) stay off the trails.
		std::vector<unsigned char> knob(t.size(), 0);
		for (int i = 0; i < t.size(); ++i)
			knob[i] = L.knob[i] || L.quarryOf[i] >= 0;
		const auto round = dilate(t, knob, 1);
		for (int i = 0; i < t.size(); ++i)
			protectedTiles[i] = protectedTiles[i] || round[i];
	}
	for (int h : homes)
		disc(protectedTiles, t, h, 5);
	auto endpoints = homes;
	// Route endpoints sit outside the building reservation, on its accessible land.
	std::vector<std::vector<int>> doorsteps;
	for (int h : homes)
	{
		std::vector<int> options;
		for (int y = -12; y <= 12; ++y)
			for (int x = -12; x <= 12; ++x)
				if (x * x + y * y >= 81 && x * x + y * y <= 144)
					options.push_back(t.at(t.remainderX(h) + x, h / t.w + y));
		doorsteps.push_back(std::move(options));
	}
	struct Edge
	{
		int a, b, d;
	};
	std::vector<Edge> edges;
	for (size_t a = 0; a < endpoints.size(); ++a)
		for (size_t b = a + 1; b < endpoints.size(); ++b)
			edges.push_back({int(a), int(b),
							 t.dist2(t.remainderX(endpoints[a]), endpoints[a] / t.w, t.remainderX(endpoints[b]),
									 endpoints[b] / t.w)});
	std::stable_sort(edges.begin(), edges.end(), [](auto a, auto b) { return a.d < b.d; });
	DisjointSets joins(int(endpoints.size()));
	int trailCount = 0;
	// The routes are found and reserved as sand, then every corner they painted becomes trail.
	const TerrainSketch beforeTrails = L.terrain;
	auto routeNoise = periodicNoise(t.w, t.h, 24, c.stream("portage-trail-bends"));
	std::vector<int> costs(t.size());
	for (int i = 0; i < t.size(); ++i)
		costs[i] = 3 + routeNoise[i] / 1024;
	for (auto e : edges)
	{
		bool mandatory = joins.unite(e.a, e.b);
		bool extra = c.bounded("portage-extra-trails", 100) < unsigned(o.trails);
		if (!mandatory && !extra)
			continue;
		auto goal = tileMask(t, doorsteps[e.b]);
		auto route = reserveSandRoute(L.terrain, t, doorsteps[e.a], goal, protectedTiles, 0, &costs,
									  GridNeighbors::Eight);
		if (route.empty())
		{
			if (mandatory)
			{
				L.failure = "Portage Lakes cannot connect its shoreline trails.";
				return L;
			}
			continue;
		}
		for (int i : route)
			disc(L.roads, t, i, 1);
		++trailCount;
	}
	// With one or two colonies the home graph has no spare edges. Optional trails
	// instead open approaches to found neutral bays, without forcing those bays
	// into the mandatory home network or altering the larger games' road layout.
	if (homes.size() <= 2)
	{
		if (neutralBays.empty())
			for (int candidate : L.candidates)
			{
				if (L.reserved[candidate])
					continue;
				bool separated = true;
				for (int home : homes)
					separated &=
						t.dist2(t.remainderX(candidate), candidate / t.w, t.remainderX(home), home / t.w) >= 400;
				for (int bay : neutralBays)
					separated &=
						t.dist2(t.remainderX(candidate), candidate / t.w, t.remainderX(bay), bay / t.w) >= 256;
				if (separated)
					neutralBays.push_back(candidate);
				if (neutralBays.size() == 2)
					break;
			}
		for (size_t k = 0; k < homes.size(); ++k)
			for (int bay : neutralBays)
			{
				if (c.bounded("portage-bay-trails", 100) >= unsigned(o.trails))
					continue;
				auto saved = L.terrain;
				std::vector<int> goals;
				for (int y = -12; y <= 12; ++y)
					for (int x = -12; x <= 12; ++x)
						if (x * x + y * y >= 81 && x * x + y * y <= 144)
							goals.push_back(t.at(t.remainderX(bay) + x, bay / t.w + y));
				auto route = reserveSandRoute(L.terrain, t, doorsteps[k], tileMask(t, goals),
											  protectedTiles, 0, &costs, GridNeighbors::Eight);
				int newlyOpened = 0;
				for (int i : route)
					newlyOpened += L.forest[i] && !L.roads[i];
				if (newlyOpened == 0)
				{
					L.terrain = std::move(saved);
					continue;
				}
				for (int i : route)
					disc(L.roads, t, i, 1);
				++trailCount;
				c.telemetry.measure("portage-lakes.trails.new-woodland-tiles", newlyOpened,
									trailCount - 1);
			}
	}
	c.telemetry.measure("portage-lakes.trails.placed", trailCount);
	{
		int trail = 0;
		for (int i = 0; i < t.size(); ++i)
			if (L.terrain[i] == SAND && beforeTrails[i] != SAND)
			{
				L.terrain[i] = TRAIL;
				++trail;
			}
		c.telemetry.measure("portage-lakes.trails.vertices", trail);
	}
	for (int i = 0; i < t.size(); ++i)
		if (L.roads[i])
		{
			L.forest[i] = 0;
			L.rock[i] = 0;
		}
	// Crop-proof terrain can alter fertility. Never leave any accidentally watered ridge trees.
	growth = cropGrowthField(L.terrain, t);
	grass = pureTiles(L.terrain, t, GRASS);
	for (int i = 0; i < t.size(); ++i)
		if (!grass[i] || growth.at(t.remainderX(i), i / t.w) > 0)
			L.forest[i] = L.rock[i] = 0;
	const int minimumPortages = L.compact ? 1 : 2;
	const int wantedPortages = L.compact ? 1 : std::clamp(t.size() / 65536, 2, 4);
	for (int p = 0; p < wantedPortages; ++p)
	{
		if (!findPortage(L, c, o))
		{
			if (p >= minimumPortages)
			{
				c.telemetry.fallback("portage-lakes.portages.extra",
									 "no additional useful clearing fits", p);
				break;
			}
			L.failure = "Portage Lakes cannot fit a useful dry woodland shortcut on this seed.";
			return L;
		}
		L.portages.push_back(L.portage);
		for (int i : L.portage.cut)
			disc(L.reserved, t, i, 3);
	}
	L.portage = L.portages.front();
	c.telemetry.measure("portage-lakes.portages.placed", L.portages.size());
	auto walk = land(L);
	auto swim = walk;
	auto water = pureTiles(L.terrain, t, WATER);
	for (int i = 0; i < t.size(); ++i)
		if (water[i])
			swim[i] = 1;
	int best = 0;
	for (int centre : L.lakeCentres)
		for (int direction = 0; direction < 8; ++direction)
		{
			double angle = direction * kPi / 8, dx = std::cos(angle), dy = std::sin(angle);
			std::array<int, 2> ends{{-1, -1}};
			for (int side = 0; side < 2; ++side)
				for (int j = 1; j < 52; ++j)
				{
					int sign = side ? 1 : -1;
					int q = t.at(t.remainderX(centre) + int(std::lround(sign * dx * j)),
								 centre / t.w + int(std::lround(sign * dy * j)));
					bool fits = true;
					int radius = L.compact ? 2 : (t.size() / int(homes.size()) < 8192 ? 3 : 4);
					for (int y = -radius; y <= radius && fits; ++y)
						for (int x = -radius; x <= radius && fits; ++x)
						{
							int v = t.at(t.remainderX(q) + x, q / t.w + y);
							fits = grass[v] && !L.reserved[v] && !L.rock[v] &&
								   (!homeBuffer[v] || L.compact);
						}
					if (fits && walk[q])
					{
						ends[side] = q;
						break;
					}
				}
			int a = ends[0], b = ends[1];
			if (a < 0 || b < 0 || a == b)
				continue;
			int before = distance(t, walk, a, b), after = distance(t, swim, a, b);
			if (after > 0 && before - after > best)
			{
				best = before - after;
				L.swim = {a, b, before, after, {}};
			}
		}
	if (L.swim.a >= 0 && (!expansion(L, L.swim.a) || !expansion(L, L.swim.b)))
		best = 0;
	if (best < (L.compact ? 4 : 8))
		L.failure = kNoSwim;
	c.telemetry.measure("portage-lakes.swim.walk-before", L.swim.before);
	c.telemetry.measure("portage-lakes.swim.walk-after", L.swim.after);
	c.telemetry.measure("portage-lakes.bays.neutral", neutral);
	for (size_t k = 0; k < L.expansions.size(); ++k)
	{
		c.telemetry.measure("portage-lakes.expansion.x", t.remainderX(L.expansions[k]), int(k));
		c.telemetry.measure("portage-lakes.expansion.y", L.expansions[k] / t.w, int(k));
	}
	if (!L.failure.empty())
		return L;
	if (!extraShoreFields(L, c, false))
	{
		L.failure = "Portage Lakes cannot fit additional shore fields beside every lake.";
		return L;
	}
	grass = pureTiles(L.terrain, t, GRASS);
	// Shore expansion may clear ordinary woodland, but reserved portage plugs
	// and their margins were excluded from every new field.
	for (int i = 0; i < t.size(); ++i)
		if (!grass[i] || L.plotOf[i] >= 0)
			L.forest[i] = 0;
	if (!knobsFirst)
		knobs();
	// Break up the largest empty lawns with small fruit copses and rocky outcrops.
	// Keep broad aisles around every clump and stay away from all promised routes,
	// lake landings, home room and working fields. These add no enclosing sand.
	std::vector<unsigned char> empty(t.size());
	for (int i = 0; i < t.size(); ++i)
		empty[i] = grass[i] && !L.forest[i] && !L.rock[i] && !L.reserved[i] &&
			!L.roads[i] && L.plotOf[i] < 0;
	std::vector<unsigned char> keepClear(t.size(), 0);
	for (int h : L.homes) disc(keepClear, t, h, 20);
	for (int h : L.expansions) disc(keepClear, t, h, 12);
	for (int i = 0; i < t.size(); ++i) empty[i] &= !keepClear[i];
	const auto room = clearance(t, empty);
	std::vector<int> centres;
	for (int i = 0; i < t.size(); ++i) if (room[i] >= 7) centres.push_back(i);
	c.shuffle(centres.begin(), centres.end(), "portage-country-clumps");
	std::stable_sort(centres.begin(), centres.end(), [&](int a, int b) { return room[a] > room[b]; });
	std::vector<int> used;
	for (int i : centres)
	{
		bool apart = true;
		for (int other : used) apart &= t.dist2(t.remainderX(i), i / t.w, t.remainderX(other), other / t.w) >= 24 * 24;
		if (!apart) continue;
		used.push_back(i);
		std::vector<int> clump;
		const int radius = 2 + c.bounded("portage-country-clumps", 2);
		for (int dy = -radius; dy <= radius; ++dy)
			for (int dx = -radius; dx <= radius; ++dx)
				if (dx * dx + dy * dy <= radius * radius && !(dx == radius && dy >= 0))
					clump.push_back(t.at(t.remainderX(i) + dx, i / t.w + dy));
		L.countryClumps.push_back(std::move(clump));
		if (used.size() >= size_t(t.size() / 1800)) break;
	}
	c.telemetry.measure("portage-lakes.country.clumps", L.countryClumps.size());
	finishTerrain(L, c, o, homeBuffer);
	return L;
}

// Rewind every stream drawn from since `before`, and drop the telemetry recorded since.
void rewind(GenerationContext &c, const GenerationContext &before)
{
	for (const auto &[name, state] : c.namedStreams())
	{
		const auto saved = before.namedStreams().find(name);
		const std::mt19937 rewound =
			saved != before.namedStreams().end() ? saved->second
												 : std::mt19937(GenerationContext::deriveSeed(c.request.seed, name));
		c.stream(name) = rewound;
	}
	c.telemetry = before.telemetry;
}

// Furnish with wet shores, or, when the mud's watering costs the swim its shortcut, with revision 7's
// plain beaches from the same stream state. With `knobsFirst` the home knobs go first, and when that
// layout fails the whole furnishing is done again from the same stream state with the knobs placed last,
// as first built; without it the knobs go last. Generation and validation both come through here.
Layout furnishLakes(const Layout &base, const std::vector<int> &homes, GenerationContext &c, bool knobsFirst)
{
	const auto shores = [&](bool knobsFirst)
	{
		GenerationContext before(c);
		Layout L = furnish(base, homes, c, true, knobsFirst);
		if (L.failure != kNoSwim)
		{
			c.telemetry.choice("portage-lakes.shore.margins", "mud");
			return L;
		}
		rewind(c, before);
		L = furnish(base, homes, c, false, knobsFirst);
		c.telemetry.choice("portage-lakes.shore.margins", "plain beaches for the swim shortcut");
		return L;
	};
	GenerationContext start(c);
	if (!knobsFirst)
		return shores(false);
	Layout L = shores(true);
	if (L.failure.empty())
		return L;
	rewind(c, start);
	L = shores(false);
	c.telemetry.fallback("portage-lakes.knob.order", "the knobs placed first left no room: placed last");
	return L;
}

// The quickest eight-neighbour walk from a to b over `open` tiles, each step costing its length over
// the speed of the tile stepped onto (ground speed, 256 = plain), in plain tiles; -1 when b cannot be
// reached. With a `limit`, the search stops once every remaining walk would cost at least that much
// and returns the limit: enough to know a walk is that long, without flooding the rest of the map.
double travelTime(const GenerationMap &map, const std::vector<unsigned char> &open, int a, int b, double limit = -1)
{
	const Torus t(map);
	std::vector<double> best(t.size(), -1);
	using Node = std::pair<double, int>;
	std::priority_queue<Node, std::vector<Node>, std::greater<Node>> frontier;
	frontier.push({0, a});
	best[a] = 0;
	while (!frontier.empty())
	{
		const auto [cost, i] = frontier.top();
		frontier.pop();
		if (i == b)
			return cost;
		if (limit >= 0 && cost >= limit)
			return limit;
		if (cost > best[i])
			continue;
		for (int dy = -1; dy <= 1; ++dy)
			for (int dx = -1; dx <= 1; ++dx)
			{
				if (!dx && !dy)
					continue;
				const int q = t.at(t.remainderX(i) + dx, i / t.w + dy);
				if (!open[q])
					continue;
				const int speed = std::max(1, int(map.terrainPropertiesAt(t.remainderX(q), q / t.w).groundSpeedQ8));
				const double next = cost + (dx && dy ? 1.41421356 : 1.0) * 256.0 / speed;
				if (best[q] < 0 || next < best[q])
				{
					best[q] = next;
					frontier.push({next, q});
				}
			}
	}
	return -1;
}

std::string mechanismFailure(const GenerationMap &map, const Layout &L, bool checkOpeningAccess = false,
							 GenerationContext *record = nullptr)
{
	const auto &t = L.t;
	auto walking = groundUnitTiles(map);
	auto workers = unitTilesByTeam(map, int(L.homes.size()));
	if (workers.empty() || workers[0].empty())
		return "A Portage Lakes settlement has no workers.";
	auto connected = floodFrom(t, tileMask(t, workers[0]), walking).steps;
	// Opening courts may grow over when unused, so this belongs to initial
	// materialization rather than the persistent terrain/growth contract.
	if (checkOpeningAccess)
		for (const auto &plot : L.plots)
			if (plot.court >= 0 && connected[plot.court] < 0)
				return "Portage Lakes starter inn court is disconnected from its workers.";

	auto anchors = buildAnchors(t, potentialBuildingTiles(map), 4);
	for (int p : L.expansions)
	{
		if (connected[p] < 0)
			return "A Portage Lakes expansion is disconnected from the colonies.";
		bool room = false;
		for (int y = -3; y <= 1; ++y)
			for (int x = -3; x <= 1; ++x)
				room |= anchors[t.at(t.remainderX(p) + x, p / t.w + y)];
		if (!room)
			return "A Portage Lakes expansion has no building room.";
	}
	for (int p : {L.portage.a, L.portage.b, L.swim.a, L.swim.b})
		if (p < 0 || connected[p] < 0)
			return "A Portage Lakes landing is disconnected from the colonies.";
	for (const auto &p : L.portages)
	{
		if (p.a < 0 || p.b < 0 || connected[p.a] < 0 || connected[p.b] < 0)
			return "A Portage Lakes landing is disconnected from the colonies.";
		auto opened = walking;
		for (int i : p.cut)
			opened[i] = 1;
		int before = distance(t, walking, p.a, p.b), after = distance(t, opened, p.a, p.b);
		if (after < 0 ||
			before - after < std::max(L.compact ? 4 : 8, before / (L.compact ? 10 : 5)))
			return "A Portage Lakes shortcut does not shorten its finished route.";
	}
	auto cut = walking;
	for (const auto &p : L.portages)
		for (int i : p.cut)
			cut[i] = 1;
	auto swimming = groundUnitTiles(map, true);
	auto combined = swimming;
	for (const auto &p : L.portages)
		for (int i : p.cut)
			combined[i] = 1;
	for (size_t k = 0; k < L.portages.size(); ++k)
	{
		const auto &p = L.portages[k];
		auto otherOptions = combined;
		for (int i : p.cut)
			otherOptions[i] = walking[i];
		const int before = distance(t, otherOptions, p.a, p.b);
		const int after = distance(t, combined, p.a, p.b);
		if (before < 0 || after < 0 || before <= after)
			return "Portage Lakes shortcuts duplicate the same connection.";
		if (record)
			record->telemetry.measure("portage-lakes.portage.saving-after-other-options",
									  before - after, int(k));
	}
	for (int kind = 0; kind < 2; ++kind)
	{
		const auto &p = kind ? L.swim : L.portage;
		int before = distance(t, kind ? cut : swimming, p.a, p.b),
			after = distance(t, combined, p.a, p.b);
		if (before < 0 || after < 0 || before <= after)
			return "Portage Lakes shortcuts duplicate the same connection.";
		if (record)
			record->telemetry.measure(kind ? "portage-lakes.swim.saving-after-cut"
										   : "portage-lakes.portage.saving-after-swim",
									  before - after);
	}
	const int minimum = L.compact ? 4 : 8, divisor = L.compact ? 10 : 5;
	const bool telemetry = record && record->telemetry.enabled();
	for (int kind = 0; kind < 2; ++kind)
	{
		const auto &p = kind ? L.swim : L.portage;
		if (p.a < 0 || p.b < 0 || !walking[p.a] || !walking[p.b])
			return "A Portage Lakes shortcut has an obstructed landing.";
		int before = distance(t, walking, p.a, p.b),
			after = distance(t, kind ? swimming : cut, p.a, p.b);
		if (before < 0 || after < 0 || before - after < std::max(minimum, before / divisor))
			return "A Portage Lakes shortcut does not shorten its finished route.";
		if (telemetry)
			record->telemetry.measure(kind ? "portage-lakes.swim.final-saving"
										   : "portage-lakes.portage.final-saving",
									  before - after);
		// The same crossing in walking time (tiles at plain speed): trails are fast, deep water and mud
		// slow, so this is what the cut or the swim actually saves a worker. The swim must save time by the
		// same rule as steps: deep lake cores swim at 192/256 and the walk round may follow fast trails, so
		// a swim that saves steps can save little time. The portage cut opens dry ground, never slower than
		// the walk round it, so its time is telemetry only.
		if (!kind && !telemetry)
			continue;
		const double fast = travelTime(map, kind ? swimming : cut, p.a, p.b);
		if (fast < 0)
			return "A Portage Lakes shortcut does not shorten its finished route.";
		// Without telemetry the walk round need only be followed until it is long enough to pass.
		const double enough = std::max(fast + minimum, fast * divisor / (divisor - 1.0));
		const double slow = travelTime(map, walking, p.a, p.b, telemetry ? -1 : enough);
		if (kind && (slow < 0 || slow - fast < std::max(double(minimum), slow / divisor)))
			return "Portage Lakes swimming does not save its crossing any walking time.";
		if (telemetry)
		{
			record->telemetry.measure(kind ? "portage-lakes.swim.time-before" : "portage-lakes.portage.time-before", slow);
			record->telemetry.measure(kind ? "portage-lakes.swim.time-after" : "portage-lakes.portage.time-after", fast);
		}
	}
	return "";
}
// Both checkerboard harvest policies retain a usable inn location beside every
// starter field. This checks planted wheat, not merely fertility or distant room.
int farmServiceSites(const GenerationMap &map, const Torus &t, const Plot &plot)
{
	if (plot.court < 0)
		return 0;
	int sites[2] = {0, 0};
	for (int oy = 0; oy < 2; ++oy)
		for (int ox = 0; ox < 2; ++ox)
		{
			const int x = t.remainderX(plot.court) + ox, y = plot.court / t.w + oy;
			if (!map.isHardSpaceForBuilding(x, y, 3, 3))
				continue;
			int adjacent[2] = {0, 0}, nearby[2] = {0, 0};
			for (int dy = -5; dy < 7; ++dy)
				for (int dx = -5; dx < 7; ++dx)
				{
					const int xx = t.x(x + dx), yy = t.y(y + dy);
					if (map.getResource(xx, yy).type != WHEAT)
						continue;
					const int parity = (xx + yy) & 1;
					++nearby[parity];
					adjacent[parity] += dx >= -1 && dx < 4 && dy >= -1 && dy < 4;
				}
			for (int p = 0; p < 2; ++p)
				sites[p] += adjacent[p] && nearby[p] >= 5;
		}
	return std::min(sites[0], sites[1]);
}

// Wheat is intentionally free to spread through neighboring meadows. Renewable timber
// remains contained, keeping the faster-growing wood off the open fields and trails.
// Ground wood can grow onto, by its rules rather than its look (lush, dry and plain grass alike).
bool woodGround(const GenerationMap &map, int x, int y)
{
	const auto &rules = map.terrainPropertiesAt(x, y);
	return rules.resourcesGrow && rules.walkable && (rules.allowedResources & (1u << WOOD));
}
std::string timberPlotsMismatch(const GenerationMap &map, const Layout &L, const Fertility::Field &growth)
{
	const auto &t = L.t;
	for (int i = 0; i < t.size(); ++i)
	{
		generationCheckpoint();
		const int x = t.remainderX(i), y = i / t.w, plot = L.plotOf[i];
		const bool timber = plot >= 0 && L.plots[plot].kind == WOOD;
		if (map.getResource(i).type == WOOD && growth.at(x, y) > 0 && !timber)
			return "Renewable Portage Lakes timber has escaped its shore plot.";
		if (!timber)
			continue;
		if (!woodGround(map, x, y))
			return "A Portage Lakes timber plot has lost its grass.";
		for (int dy = -1; dy <= 1; ++dy)
			for (int dx = -1; dx <= 1; ++dx)
			{
				const int next = t.at(x + dx, y + dy);
				if (woodGround(map, t.remainderX(next), next / t.w) && L.plotOf[next] != plot)
					return "A Portage Lakes timber plot has a grass growth connection across its margin.";
			}
	}
	return "";
}

bool materialize(GenerationWorld &game, GenerationContext &c, const Layout &L)
{
	if (!L.failure.empty())
	{
		c.detail = L.failure;
		return false;
	}
	const auto &t = L.t;
	const PortageLakesOptions o(c.request);
	writeVertices(game.map, composeLooks(L.terrain, L.looks));
	// Every quarry face holds its stone before anything else is planted.
	for (int i = 0; i < t.size(); ++i)
		if (L.quarry[i] && game.map.isResourceAllowed(t.remainderX(i), i / t.w, STONE))
			game.map.setResourceByIndex(t.remainderX(i), i / t.w, STONE, 1);
	for (size_t k = 0; k < L.homes.size(); ++k)
		game.addTeam();
	for (size_t k = 0; k < L.homes.size(); ++k)
	{
		int h = L.homes[k];
		std::vector<unsigned char> mask(t.size(), 0);
		disc(mask, t, h, 6);
		if (!placeSettlement(game, c, int(k), mask, {t.x(t.remainderX(h) - 2), t.y(h / t.w - 2)},
							 "portage-settlements"))
			return false;
	}
	auto growth = cropGrowthField(L.terrain, t);
	for (size_t p = 0; p < L.plots.size(); ++p)
	{
		const auto &plot = L.plots[p];
		int percent = plot.kind == WHEAT ? o.wheat : plot.kind == WOOD ? o.wood : o.fruit;
		int requested = plot.minimum + scaledCount(plot.baseline, percent);
		auto plantable = plot.tiles;
		if (plot.court >= 0)
			plantable.erase(std::remove_if(plantable.begin(), plantable.end(),
										   [&](int i)
										   {
											   return (t.x(t.remainderX(i) - t.remainderX(plot.court)) < 4 &&
													   t.y(i / t.w - plot.court / t.w) < 4) ||
													  std::find(plot.access.begin(),
																plot.access.end(),
																i) != plot.access.end();
										   }),
							plantable.end());
		int count = 0;
		if (plot.serviceHome >= 0 && plot.court >= 0)
		{
			// Food haulers discover both sides of the home-facing court rather than
			// stopping at a distant rich tip and leaving the inn site unexplored.
			std::vector<int> rim;
			for (int i : plantable)
			{
				int dx = t.offsetX(t.remainderX(plot.court), t.remainderX(i));
				int dy = t.offsetY(plot.court / t.w, i / t.w);
				if (dx >= -1 && dx <= 4 && dy >= -1 && dy <= 4)
					rim.push_back(i);
			}
			count = plantContainedPlot(game.map, t, rim, growth, plot.kind, requested, true);
		}
		count += plantContainedPlot(game.map, t, plantable, growth, plot.kind, requested - count,
									plot.kind == WHEAT || plot.kind == WOOD);
		// Scarce sowing can concentrate at a rich tip away from the inn. Re-site
		// the same seed budget around its rim; never add an abundance exception.
		if (count > 0 && plot.court >= 0 && !farmServiceSites(game.map, t, plot))
		{
			for (int i : plantable)
				if (game.map.getResource(t.remainderX(i), i / t.w).type == WHEAT)
					game.map.setNoResource(t.remainderX(i), i / t.w, 0);
			std::vector<int> rim;
			for (int i : plantable)
			{
				int dx = t.offsetX(t.remainderX(plot.court), t.remainderX(i));
				int dy = t.offsetY(plot.court / t.w, i / t.w);
				if (dx >= -1 && dx <= 4 && dy >= -1 && dy <= 4)
					rim.push_back(i);
			}
			const int budget = count;
			count = plantContainedPlot(game.map, t, rim, growth, WHEAT, budget, true);
			count +=
				plantContainedPlot(game.map, t, plantable, growth, WHEAT, budget - count, true);
			c.telemetry.measure("portage-lakes.farm.resown", 1, int(p));
		}
		if (plot.lake >= 0)
			c.telemetry.measure("portage-lakes.shore-field.lake", plot.lake, int(p));
		c.telemetry.measure("portage-lakes.plot.requested", requested, int(p));
		c.telemetry.measure("portage-lakes.plot.planted", count, int(p));
		if (plot.kind == WOOD || plot.kind == WHEAT)
			c.telemetry.measure(plot.kind == WOOD ? "portage-lakes.timber.planted"
												  : "portage-lakes.wheat.planted",
								count, int(p));
		std::uint64_t potential = 0;
		for (int i : plantable)
			potential += growth.at(t.remainderX(i), i / t.w);
		if (plot.court >= 0)
		{
			const int sites = farmServiceSites(game.map, t, plot);
			c.telemetry.measure("portage-lakes.farm.service-sites", sites, int(p));
			c.telemetry.measure("portage-lakes.farm.court-x", t.remainderX(plot.court), int(p));
			c.telemetry.measure("portage-lakes.farm.court-y", plot.court / t.w, int(p));
			if (!sites && (plot.minimum > 0 || count > 0))
			{
				c.detail = "Portage Lakes starter grain has no adjacent inn site.";
				return false;
			}
		}
		c.telemetry.measure("portage-lakes.plot.growth-potential",
							double(potential) / Fertility::kScale, int(p));
		if (count < plot.minimum || (plot.minimum > 0 && plot.kind == WHEAT &&
									 potential < (t.size() == 4096 ? 2 : 6) * Fertility::kScale))
		{
			c.detail = "Portage Lakes has too little renewable starter farmland.";
			return false;
		}
	}
	for (int i = 0; i < t.size(); ++i)
		if (L.forest[i] && clearGround(game.map, t.remainderX(i), i / t.w))
			game.map.setResourceByIndex(t.remainderX(i), i / t.w, WOOD, 1);
	for (size_t k = 0; k < L.countryClumps.size(); ++k)
	{
		const int kind = k % 3 == 0 ? STONE : CHERRY + int(k % 3);
		plantFieldInteriors(game.map, t, L.countryClumps[k], kind,
			scaledCount(int(L.countryClumps[k].size()), kind == STONE ? o.stone : o.fruit));
	}
	for (int pool : L.algaePools)
	{
		auto waterEligible = [&](int i)
		{
			return t.dist2(t.remainderX(pool), pool / t.w, t.remainderX(i), i / t.w) <= 36 &&
				   game.map.terrainSupportsResourceAtByIndex(t.remainderX(i), i / t.w, ALGA);
		};
		growPatch(game.map, t, pool, ALGA, 8 + scaledCount(8, o.algae), waterEligible);
	}
	// Without knobs, every home's stone is revision 7's patch near the town.
	if (!L.knobFace)
		for (int h : L.homes)
		{
			auto eligible = [&](int i)
			{
				return !L.reserved[i] && L.plotOf[i] < 0 && !L.forest[i] && !L.rock[i] && !L.roads[i] &&
					   t.dist2(t.remainderX(h), h / t.w, t.remainderX(i), i / t.w) >= 64 &&
					   clearGround(game.map, t.remainderX(i), i / t.w);
			};
			int q = seedNear(t, t.remainderX(h), h / t.w, 16, eligible);
			if (q >= 0)
				growPatch(game.map, t, q, STONE, 4 + scaledCount(3, o.stone), eligible);
		}
	// Every home's stone is its own knob's quarry face (unscaled, the same for every colony). Record
	// the walk from each home to its own face and to the nearest face of any kind.
	if (c.telemetry.enabled())
	{
		const auto walking = groundUnitTiles(game.map);
		const auto workers = unitTilesByTeam(game.map, int(L.homes.size()));
		std::vector<unsigned char> own(t.size(), 0), any(t.size(), 0);
		for (size_t k = 0; k < L.homes.size(); ++k)
		{
			std::fill(own.begin(), own.end(), 0);
			std::fill(any.begin(), any.end(), 0);
			for (int i = 0; i < t.size(); ++i)
				if (L.quarry[i])
					for (int dy = -1; dy <= 1; ++dy)
						for (int dx = -1; dx <= 1; ++dx)
						{
							const int q = t.at(t.remainderX(i) + dx, i / t.w + dy);
							if (!walking[q])
								continue;
							any[q] = 1;
							own[q] |= L.quarryOf[i] == int(k);
						}
			const auto steps = floodFrom(t, tileMask(t, workers[k]), walking).steps;
			int ownWalk = -1, anyWalk = -1;
			for (int i = 0; i < t.size(); ++i)
				if (steps[i] >= 0)
				{
					if (own[i] && (ownWalk < 0 || steps[i] < ownWalk))
						ownWalk = steps[i];
					if (any[i] && (anyWalk < 0 || steps[i] < anyWalk))
						anyWalk = steps[i];
				}
			c.telemetry.measure("portage-lakes.quarries.walk-own", ownWalk, int(k));
			c.telemetry.measure("portage-lakes.quarries.walk-nearest", anyWalk, int(k));
		}
		int homeStone = 0;
		for (int i = 0; i < t.size(); ++i)
			homeStone += L.quarryOf[i] == 0 && permanentResourceBarrier(game.map, size_t(i));
		c.telemetry.measure("portage-lakes.quarries.home-stone", homeStone);
	}
	for (size_t k = 0; k < L.expansions.size(); ++k)
	{
		int h = L.expansions[k];
		auto eligible = [&](int i)
		{
			int d = t.chebyshev(t.remainderX(h), h / t.w, t.remainderX(i), i / t.w);
			return d >= 6 && d <= 9 && !L.reserved[i] && L.plotOf[i] < 0 && !L.roads[i] &&
				   !L.forest[i] && !L.rock[i] && clearGround(game.map, t.remainderX(i), i / t.w);
		};
		int seed = seedNear(t, t.remainderX(h), h / t.w, 9, eligible);
		if (seed >= 0)
			growPatch(game.map, t, seed, CHERRY + int(k % 3), scaledCount(3, o.fruit), eligible);
	}
	if (auto e = timberPlotsMismatch(game.map, L, growth); !e.empty())
	{
		c.detail = e;
		return false;
	}
	if (auto e = startingAccessFailure(game.map, c.request.nbTeams,
									   {{MaterialId::Food, 12, "food"},
										{MaterialId::Wood, 32, "wood"},
										{MaterialId::Stone, 24, "stone"},
										{MaterialId::Algae, L.compact ? 64 : 48, "algae"}});
		!e.empty())
	{
		c.detail = e;
		return false;
	}
	if (auto e = mechanismFailure(game.map, L, true, &c); !e.empty())
	{
		c.detail = e;
		return false;
	}
	return true;
}

// Keep the four-way settlement comparison, with bounded alternative landscapes when
// none of the complete proposals passes. Failed attempts never touch the target game.
constexpr int landscapeAttempts = 24;
bool generate(GenerationWorld &game, GenerationContext &c)
{
	for (int attempt = 0; attempt < landscapeAttempts; ++attempt)
	{
		c.stage = "portage lakes landscape";
		Layout base = landscape(c.request, c);
		if (!base.failure.empty())
		{
			c.detail = base.failure;
			continue;
		}
		// Furnishing has its own named streams. Each proposal starts from the same stream
		// state; only the winning complete world is materialized into the caller's game.
		c.stage = "portage lakes settlements";
		bool knobsFirst = true;
		auto builder = [&](GenerationWorld &trial, GenerationContext &probe, const std::vector<int> &sites)
		{ return materialize(trial, probe, furnishLakes(base, sites, probe, knobsFirst)); };
		auto check = [](const StartQualityReport &q)
		{
			for (const auto &s : q.colonies)
				if (s.wheatDistance < 0 || s.wheatDistance > 12 || s.woodDistance < 0 ||
					s.woodDistance > 32 || s.buildSites < 16 ||
					s.reachableRivals < int(q.colonies.size()) - 1)
					return std::string(
						"Portage Lakes has an isolated or cramped shoreline settlement.");
			return std::string();
		};
		auto selected = chooseScoredSettlements(c, base.proposals, builder, check);
		// When no proposal works with the knobs placed first (a finished map can still fail its checks
		// after a furnishing that succeeded), every proposal is tried again with the knobs placed last, as
		// first built, so the early knobs never refuse a landscape the late order accepted.
		if (!selected.failure.empty())
		{
			knobsFirst = false;
			selected = chooseScoredSettlements(c, base.proposals, builder, check);
			if (selected.failure.empty())
				c.telemetry.fallback("portage-lakes.knob.order",
									 "no proposal worked with the knobs placed first: placed last");
		}
		if (!selected.failure.empty())
		{
			c.detail = selected.failure;
			continue;
		}
		c.telemetry.measure("portage-lakes.landscape.selected", attempt);
		return builder(game, c, selected.sites);
	}
	return false;
}
std::string validateWorld(const GenerationWorld &game, const GenerationContext &c)
{
	GenerationContext replay(c.request);
	std::vector<int> homes;
	const Torus actual(game.map);
	for (int k = 0; k < c.request.nbTeams; ++k)
		homes.push_back(actual.at(game.teamStart(k).x + 2, game.teamStart(k).y + 2));
	Layout L;
	bool matched = false;
	for (int attempt = 0; attempt < landscapeAttempts && !matched; ++attempt)
	{
		auto base = landscape(c.request, replay);
		if (std::find(base.proposals.begin(), base.proposals.end(), homes) == base.proposals.end())
			continue;
		for (bool knobsFirst : {true, false})
		{
			if (matched)
				break;
			GenerationContext probe(replay);
			auto candidate = furnishLakes(base, homes, probe, knobsFirst);
			if (!candidate.failure.empty())
				continue;
			matched = true;
			// The mechanics and the looks both: the drawn map is exactly the design.
			const auto drawn = composeLooks(candidate.terrain, candidate.looks);
			for (int i = 0; i < actual.size(); ++i)
				matched &= drawn[i] == game.map.vertexTerrainAt(actual.remainderX(i), i / actual.w);
			if (matched)
				L = std::move(candidate);
		}
	}
	if (!matched)
		return "Portage Lakes terrain no longer matches its design.";
	const auto &t = L.t;
	const auto &map = game.map;
	auto growth = Fertility::forMap(map, false);
	if (auto e = timberPlotsMismatch(map, L, growth); !e.empty())
		return e;
	// Every quarry face holds its stone, and every home's knob the same stone.
	if (auto e = quarryFault(map, t, L.quarry, L.quarryOf, L.knobFace ? c.request.nbTeams : 0, 5); !e.empty())
		return e;
	// Trails are paths: nothing ever stands on a tile one of their corners touches.
	for (int i = 0; i < t.size(); ++i)
		if (terrainGroup(TerrainType(L.terrain[i])) == TerrainGroup::Paths)
			for (int touch = 0; touch < 4; ++touch)
			{
				const int q = t.at(t.remainderX(i) - (touch & 1), i / t.w - (touch >> 1));
				if (map.isResource(t.remainderX(q), q / t.w))
					return "A Portage Lakes trail has something standing on it at (" +
						   std::to_string(t.remainderX(q)) + ", " + std::to_string(q / t.w) + ").";
			}

	for (const auto &crossing : L.portages)
		for (int i : crossing.cut)
			if (map.getResource(t.remainderX(i), i / t.w).type != WOOD || growth.at(t.remainderX(i), i / t.w) != 0)
				return "A Portage Lakes woodland shortcut is missing or can regrow.";
	if (auto e = mechanismFailure(map, L); !e.empty())
		return e;
	if (auto e = startingAccessFailure(map, c.request.nbTeams,
									   {{MaterialId::Food, 12, "food"},
										{MaterialId::Wood, 32, "wood"},
										{MaterialId::Stone, 24, "stone"},
										{MaterialId::Algae, L.compact ? 64 : 48, "algae"}});
		!e.empty())
		return e;
	return walkFromFirstColony(map, c.request.nbTeams, "the lake country",
							   "without cutting or swimming")
		.error;
}
} // namespace
PortageLakesOptions::PortageLakesOptions(const GenerationRequest &r)
	: elongation(r.option("lake-elongation")), depth(r.option("portage-depth")),
	  trails(r.option("extra-trails")), deep(r.option("deep-lakes")), shield(r.option("shield")),
	  wheat(r.option("wheat-amount")),
	  wood(r.option("wood-amount")), stone(r.option("stone-amount")),
	  algae(r.option("algae-amount")), fruit(r.option("fruit-amount"))
{
}
GeneratorDefinition portageLakesDefinition()
{
	return {"portage-lakes",
			65,
			"Portage Lakes",
			8,
			false,
			{GeneratorControl{"lake-elongation", "Lake elongation", 125, 300, 25, 200,
							  ControlGroup::Terrain}
				 .withSearchRange(150, 250),
			 GeneratorControl{"portage-depth", "Portage depth", 2, 8, 1, 4, ControlGroup::Layout}
				 .withSearchRange(2, 6),
			 GeneratorControl{"extra-trails", "Extra trails", 0, 100, 25, 25, ControlGroup::Layout}
				 .withSearchRange(25, 75),
			 // 2.0: deep lake centres (slower swimming, no algae) behind a shallow margin.
			 GeneratorControl::toggle("deep-lakes", "Deep lakes", true, ControlGroup::Terrain)
				 .withSearchValues({0, 1}),
			 // Looks only: granite and greenstone play exactly alike.
			 GeneratorControl::choice("shield", "Bedrock", {"Random", "Granite", "Greenstone"}, 0,
									  ControlGroup::Terrain)
				 .withSearchValues({1, 2}),
			 GeneratorControl::percentage("wheat-amount", "Wheat amount"),
			 GeneratorControl::percentage("wood-amount", "Wood amount"),
			 GeneratorControl::percentage("stone-amount", "Stone amount"),
			 GeneratorControl::percentage("algae-amount", "Algae amount"),
			 GeneratorControl::percentage("fruit-amount", "Fruit amount")},
			generate,
			true,
			validateRequest,
			validateWorld,
			{"terrain:natural", "feature:lakes", "feature:forest", "style:wide-open"}};
}
