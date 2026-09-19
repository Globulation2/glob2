// SPDX-License-Identifier: GPL-3.0-or-later
#include "PortageLakesGenerator.h"
#include "Contact.h"
#include "Drawing.h"
#include "Farmland.h"
#include "Game.h"
#include "GenerationContext.h"
#include "Growth.h"
#include "Landmass.h"
#include "LatticeNoise.h"
#include "Morphology.h"
#include "Pipeline.h"
#include "Planting.h"
#include "Points.h"
#include "Roads.h"
#include "Room.h"
#include "ScoredSettlements.h"
#include "Sketch.h"
#include "Topology.h"
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
// dry on the FINAL terrain; renewable crops live in sand-contained shore plots.
namespace
{
struct Plot
{
	std::vector<int> tiles;
	int kind, minimum, baseline;
	int court = -1;
	std::vector<int> access{};
	int serviceHome = -1;
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
	std::vector<std::vector<int>> proposals;
	std::vector<int> lakeCentres, algaePools, expansions;
	Crossing portage, swim;
	std::vector<Crossing> portages;
	bool compact = false;
	std::string failure;
};
std::string validateRequest(const GenerationRequest &r)
{
	if (r.wDec < 6 || r.hDec < 6 || r.wDec > 9 || r.hDec > 9)
		return "Portage Lakes needs sides between 64 and 512 tiles.";
	if (r.nbTeams > std::min(12, std::max(2, (1 << r.wDec) * (1 << r.hDec) / 4096)))
		return "Too many colonies for this map; use a bigger map or fewer colonies.";
	return "";
}
void disc(std::vector<unsigned char> &mask, const Torus &t, int p, int radius)
{
	for (int y = -radius; y <= radius; ++y)
		for (int x = -radius; x <= radius; ++x)
			if (x * x + y * y <= radius * radius)
				mask[t.at(p % t.w + x, p / t.w + y)] = 1;
}
std::vector<unsigned char> land(const Layout &L)
{
	auto water = pureTiles(L.terrain, L.t, WATER);
	for (int i = 0; i < L.t.size(); ++i)
		water[i] = !water[i] && !L.forest[i] && !L.rock[i];
	return water;
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
				const int q = t.at(i % t.w + dx, i / t.w + dy);
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
					terrain[t.at(centre % t.w - half + x, centre / t.w - half + y)];
		field = cropGrowthField(drawn, window);
	}
	std::uint32_t at(int tile) const
	{
		if (whole)
			return field.at(tile % t.w, tile / t.w);
		return field.at(half + t.offsetX(centre % t.w, tile % t.w),
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
		int anchor = t.at(i % t.w - 4, i / t.w - 4);
		if (room[anchor] && shoreDistance[i] >= 10 && shoreDistance[i] <= (L.compact ? 19 : 15))
			L.candidates.push_back(i);
	}
	c.shuffle(L.candidates.begin(), L.candidates.end(), "portage-bay-candidates");
	std::vector<unsigned char> candidates = tileMask(t, L.candidates), open(t.size());
	for (int i = 0; i < t.size(); ++i)
		open[i] = !L.lakeWater[i];
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
	return L;
}

bool makePlot(Layout &L, GenerationContext &c, int centre, int radius, int kind, int minimum,
			  int baseline, bool narrowBay = false, int serviceHome = -1)
{
	const auto &t = L.t;
	// Grow a patch ALONG the shore, clipped by existing ground and reservations.
	// This makes farms fit bays instead of drawing a row of identical round gardens.
	const int cx = centre % t.w, cy = centre / t.w;
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
	const double phase = c.bounded("portage-field-shapes", 6283) / 1000.;
	auto eligible = [&](int i)
	{
		int dx = t.offsetX(cx, i % t.w), dy = t.offsetY(cy, i / t.w);
		if (dx * dx + dy * dy > radius * radius * 5)
			return false;
		for (int y = -2; y <= 2; ++y)
			for (int x = -2; x <= 2; ++x)
			{
				int q = t.at(i % t.w + x, i / t.w + y);
				if (L.reserved[q] || L.terrain[q] != GRASS)
					return false;
			}
		return true;
	};
	if (!eligible(centre))
		return false;
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
			int q = t.at(i % t.w + step[0], i / t.w + step[1]);
			if (!queued.insert(q).second)
				continue;
			int x = t.offsetX(cx, q % t.w), y = t.offsetY(cy, q / t.w);
			double across = x * nx + y * ny, along = -x * ny + y * nx;
			frontier.push(
				{across * across * 1.9 + along * along * .55 + 4 * std::sin(along * .4 + phase),
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
	auto tiles = stampContainedPlot(L.terrain, t, corners);
	int court = -1;
	std::vector<int> access;
	if (minimum > 0)
	{
		BayGrowth fertility(L.terrain, t, centre, int(std::ceil(radius * std::sqrt(5.))));
		// Leave opening room for a farmhouse/inn INSIDE the crop boundary.
		// Outside grass cannot adjoin the crop across its sand containment.
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
								int q = t.at(a % t.w + x, a / t.w + y);
								if (inside[q])
									nearby += fertility.at(q);
							}
					int distance = 0;
					if (serviceHome >= 0)
						for (int dy : {0, 3})
							for (int dx : {0, 3})
								distance = std::max(
									distance, t.chebyshev(serviceHome % t.w, serviceHome / t.w,
														  a % t.w + dx, a / t.w + dy));
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
			// Leave the shortest unseeded notch through this field to its sand rim.
			std::map<int, int> parent;
			std::vector<int> queue;
			for (int y = 0; y < 4; ++y)
				for (int x = 0; x < 4; ++x)
				{
					int q = t.at(court % t.w + x, court / t.w + y);
					parent[q] = -1;
					queue.push_back(q);
				}
			int exit = -1;
			for (size_t head = 0; head < queue.size() && (exit < 0 || serviceHome >= 0); ++head)
			{
				int i = queue[head];
				for (auto step : kCardinalSteps)
				{
					int q = t.at(i % t.w + step[0], i / t.w + step[1]);
					if (!inside[q])
					{
						if (exit < 0 ||
							(serviceHome >= 0 &&
							 t.dist2(serviceHome % t.w, serviceHome / t.w, i % t.w, i / t.w) <
								 t.dist2(serviceHome % t.w, serviceHome / t.w, exit % t.w,
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
		int fertile = 0;
		std::uint64_t potential = 0;
		for (int i : tiles)
		{
			const bool inCourt =
				court >= 0 && t.x(i % t.w - court % t.w) < 4 && t.y(i / t.w - court / t.w) < 4;
			const bool inAccess = std::find(access.begin(), access.end(), i) != access.end();
			const auto value = inCourt || inAccess ? 0 : fertility.at(i);
			fertile += value > 0;
			potential += value;
		}
		if (fertile < minimum ||
			(kind == WHEAT && potential < (t.size() == 4096 ? 2 : 6) * Fertility::kScale))
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
	const int hx = home % t.w, hy = home / t.w;
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
							 starter ? (kind ? 12 : (L.compact ? 20 : 32)) : 0, kind ? 12 : 64,
							 pass == 1, starter && L.compact && t.size() > 4096 ? home : -1))
				{
					placed = true;
					break;
				}
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
			int p = t.at(endpoint % t.w + dx, endpoint / t.w + dy), d = dx * dx + dy * dy;
			if (d >= bestDistance)
				continue;
			bool fits = true;
			for (int y = -radius; y <= radius && fits; ++y)
				for (int x = -radius; x <= radius && fits; ++x)
				{
					int i = t.at(p % t.w + x, p / t.w + y);
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
		L.forest[i] = L.rock[i] = 0;
	for (int y = -radius; y <= radius; ++y)
		for (int x = -radius; x <= radius; ++x)
		{
			int i = t.at(best % t.w + x, best / t.w + y);
			L.forest[i] = L.rock[i] = 0;
			L.reserved[i] = 1;
		}
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
				while (left < 24 && L.forest[t.at(i % t.w - dx * left, i / t.w - dy * left)])
					++left;
				while (right < 24 && L.forest[t.at(i % t.w + dx * right, i / t.w + dy * right)])
					++right;
				if (left + right - 1 < o.depth || left >= 24 || right >= 24)
					continue;
				int a = t.at(i % t.w - dx * (left + 2), i / t.w - dy * (left + 2));
				int b = t.at(i % t.w + dx * (right + 2), i / t.w + dy * (right + 2));
				bool safe = open[a] && open[b];
				for (int j = -left - 2; j <= right + 2 && safe; ++j)
					for (int side = -2; side <= 2; ++side)
					{
						int q = t.at(i % t.w + dx * j - dy * side, i / t.w + dy * j + dx * side);
						if (!grass[q] || L.reserved[q] || L.rock[q])
							safe = false;
					}
				if (safe && left + right + 4 >= o.depth + (L.compact ? 8 : 14))
					proposals.push_back({a, b, i % t.w, i / t.w, dx, dy, left + right + 4});
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
					t.at(p.a % t.w + p.dx * j - p.dy * side, p.a / t.w + p.dy * j + p.dx * side);
				dryPlug &= growth.at(q % t.w, q / t.w) == 0;
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
		int ax = p.a % t.w, ay = p.a / t.w;
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
		// Corner sand must not touch the plug's grass tiles.
		auto protect = tileMask(t, L.portage.cut);
		for (int i = 0; i < t.size(); ++i)
			if (approach[i])
			{
				bool touches = false;
				for (int dy = -1; dy <= 0; ++dy)
					for (int dx = -1; dx <= 0; ++dx)
						touches |= protect[t.at(i % t.w + dx, i / t.w + dy)];
				if (!touches)
					L.terrain[i] = SAND;
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
Layout furnish(const Layout &base, const std::vector<int> &homes, GenerationContext &c)
{
	Layout L = base;
	const auto &t = L.t;
	const PortageLakesOptions o(c.request);
	L.homes = homes;
	for (int h : homes)
		disc(L.reserved, t, h, 6);
	for (int h : homes)
		if (!furnishBay(L, c, h, true))
		{
			L.failure = "Portage Lakes cannot fit renewable shore fields beside every settlement.";
			return L;
		}
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
					int p = t.at(h % t.w + dx, h / t.w + dy);
					bool fits = true;
					for (int yy = -5; yy <= 5; ++yy)
						for (int xx = -5; xx <= 5; ++xx)
						{
							int q = t.at(p % t.w + xx, p / t.w + yy);
							if (L.reserved[q] || L.terrain[q] != GRASS)
								fits = false;
						}
					if (!fits)
						continue;
					RadialShape shape(L.compact ? 2.5 : 3.5, .15, c, "portage-pool-shapes");
					fillShape(L.terrain, t, p % t.w, p / t.w, shape, 0, WATER);
					disc(L.reserved, t, p, 6);
					L.algaePools.push_back(p);
					placed = true;
				}
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
				t.dist2(h % t.w, h / t.w, home % t.w, home / t.w) > (L.compact ? 20 * 20 : 32 * 32);
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
					if (makePlot(L, c, t.at(h % t.w + radius, h / t.w + (f - 1) * 6), 2, CHERRY + f,
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
	auto grass = pureTiles(L.terrain, t, GRASS);
	auto growth = cropGrowthField(L.terrain, t);
	auto noise = periodicNoise(t.w, t.h, L.compact ? 20 : 36, c.stream("portage-woodland"));
	std::vector<unsigned char> dry(t.size(), 0), homeBuffer(t.size(), 0);
	for (int h : homes)
		disc(homeBuffer, t, h, L.compact ? 12 : (t.size() / int(homes.size()) < 8192 ? 18 : 24));
	for (int i = 0; i < t.size(); ++i)
		dry[i] = grass[i] && !L.reserved[i] && !homeBuffer[i] && growth.at(i % t.w, i / t.w) == 0;
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
					options.push_back(t.at(h % t.w + x, h / t.w + y));
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
							 t.dist2(endpoints[a] % t.w, endpoints[a] / t.w, endpoints[b] % t.w,
									 endpoints[b] / t.w)});
	std::stable_sort(edges.begin(), edges.end(), [](auto a, auto b) { return a.d < b.d; });
	DisjointSets joins(int(endpoints.size()));
	int trailCount = 0;
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
						t.dist2(candidate % t.w, candidate / t.w, home % t.w, home / t.w) >= 400;
				for (int bay : neutralBays)
					separated &=
						t.dist2(candidate % t.w, candidate / t.w, bay % t.w, bay / t.w) >= 256;
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
							goals.push_back(t.at(bay % t.w + x, bay / t.w + y));
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
		if (!grass[i] || growth.at(i % t.w, i / t.w) > 0)
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
					int q = t.at(centre % t.w + int(std::lround(sign * dx * j)),
								 centre / t.w + int(std::lround(sign * dy * j)));
					bool fits = true;
					int radius = L.compact ? 2 : (t.size() / int(homes.size()) < 8192 ? 3 : 4);
					for (int y = -radius; y <= radius && fits; ++y)
						for (int x = -radius; x <= radius && fits; ++x)
						{
							int v = t.at(q % t.w + x, q / t.w + y);
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
		L.failure = "Portage Lakes cannot fit a useful swimming shortcut on this seed.";
	c.telemetry.measure("portage-lakes.swim.walk-before", L.swim.before);
	c.telemetry.measure("portage-lakes.swim.walk-after", L.swim.after);
	c.telemetry.measure("portage-lakes.bays.neutral", neutral);
	for (size_t k = 0; k < L.expansions.size(); ++k)
	{
		c.telemetry.measure("portage-lakes.expansion.x", L.expansions[k] % t.w, int(k));
		c.telemetry.measure("portage-lakes.expansion.y", L.expansions[k] / t.w, int(k));
	}
	return L;
}

std::string mechanismFailure(const Map &map, const Layout &L, bool checkOpeningAccess = false,
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
				room |= anchors[t.at(p % t.w + x, p / t.w + y)];
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
	for (int kind = 0; kind < 2; ++kind)
	{
		const auto &p = kind ? L.swim : L.portage;
		if (p.a < 0 || p.b < 0 || !walking[p.a] || !walking[p.b])
			return "A Portage Lakes shortcut has an obstructed landing.";
		int before = distance(t, walking, p.a, p.b),
			after = distance(t, kind ? swimming : cut, p.a, p.b);
		if (before < 0 || after < 0 || before - after < std::max(minimum, before / divisor))
			return "A Portage Lakes shortcut does not shorten its finished route.";
		if (record)
		{
			record->telemetry.measure(kind ? "portage-lakes.swim.final-saving"
										   : "portage-lakes.portage.final-saving",
									  before - after);
		}
	}
	return "";
}
// Both checkerboard harvest policies retain a usable inn location beside every
// starter field. This checks planted wheat, not merely fertility or distant room.
int farmServiceSites(const Map &map, const Torus &t, const Plot &plot)
{
	if (plot.court < 0)
		return 0;
	int sites[2] = {0, 0};
	for (int oy = 0; oy < 2; ++oy)
		for (int ox = 0; ox < 2; ++ox)
		{
			const int x = plot.court % t.w + ox, y = plot.court / t.w + oy;
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

bool materialize(Game &game, GenerationContext &c, const Layout &L)
{
	if (!L.failure.empty())
	{
		c.detail = L.failure;
		return false;
	}
	const auto &t = L.t;
	const PortageLakesOptions o(c.request);
	writeUndermap(game.map, L.terrain);
	for (size_t k = 0; k < L.homes.size(); ++k)
		game.addTeam();
	for (size_t k = 0; k < L.homes.size(); ++k)
	{
		int h = L.homes[k];
		std::vector<unsigned char> mask(t.size(), 0);
		disc(mask, t, h, 6);
		if (!placeSettlement(game, c, int(k), mask, {t.x(h % t.w - 2), t.y(h / t.w - 2)},
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
											   return (t.x(i % t.w - plot.court % t.w) < 4 &&
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
				int dx = t.offsetX(plot.court % t.w, i % t.w);
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
		if (plot.court >= 0 && !farmServiceSites(game.map, t, plot))
		{
			for (int i : plantable)
				if (game.map.getResource(i % t.w, i / t.w).type == WHEAT)
					game.map.setNoResource(i % t.w, i / t.w, 0);
			std::vector<int> rim;
			for (int i : plantable)
			{
				int dx = t.offsetX(plot.court % t.w, i % t.w);
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
		c.telemetry.measure("portage-lakes.plot.requested", requested, int(p));
		c.telemetry.measure("portage-lakes.plot.planted", count, int(p));
		if (plot.kind == WOOD || plot.kind == WHEAT)
			c.telemetry.measure(plot.kind == WOOD ? "portage-lakes.timber.planted"
												  : "portage-lakes.wheat.planted",
								count, int(p));
		std::uint64_t potential = 0;
		for (int i : plantable)
			potential += growth.at(i % t.w, i / t.w);
		if (plot.court >= 0)
		{
			const int sites = farmServiceSites(game.map, t, plot);
			c.telemetry.measure("portage-lakes.farm.service-sites", sites, int(p));
			c.telemetry.measure("portage-lakes.farm.court-x", plot.court % t.w, int(p));
			c.telemetry.measure("portage-lakes.farm.court-y", plot.court / t.w, int(p));
			if (!sites)
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
		if ((L.forest[i] || L.rock[i]) && clearGround(game.map, i % t.w, i / t.w))
			game.map.setResource(i % t.w, i / t.w, L.rock[i] ? STONE : WOOD, 1);
	for (int pool : L.algaePools)
	{
		auto waterEligible = [&](int i)
		{
			return t.dist2(pool % t.w, pool / t.w, i % t.w, i / t.w) <= 36 &&
				   game.map.isWater(i % t.w, i / t.w);
		};
		growPatch(game.map, t, pool, ALGA, 8 + scaledCount(8, o.algae), waterEligible);
	}
	for (int h : L.homes)
	{
		auto eligible = [&](int i)
		{
			return !L.reserved[i] && L.plotOf[i] < 0 && !L.forest[i] && !L.rock[i] && !L.roads[i] &&
				   t.dist2(h % t.w, h / t.w, i % t.w, i / t.w) >= 64 &&
				   clearGround(game.map, i % t.w, i / t.w);
		};
		int q = seedNear(t, h % t.w, h / t.w, 16, eligible);
		if (q >= 0)
			growPatch(game.map, t, q, STONE, 4 + scaledCount(3, o.stone), eligible);
	}
	for (size_t k = 0; k < L.expansions.size(); ++k)
	{
		int h = L.expansions[k];
		auto eligible = [&](int i)
		{
			int d = t.chebyshev(h % t.w, h / t.w, i % t.w, i / t.w);
			return d >= 6 && d <= 9 && !L.reserved[i] && L.plotOf[i] < 0 && !L.roads[i] &&
				   !L.forest[i] && !L.rock[i] && clearGround(game.map, i % t.w, i / t.w);
		};
		int seed = seedNear(t, h % t.w, h / t.w, 9, eligible);
		if (seed >= 0)
			growPatch(game.map, t, seed, CHERRY + int(k % 3), scaledCount(3, o.fruit), eligible);
	}
	if (auto e = containedPlotsMismatch(game.map, t, L.plotOf, &growth); !e.empty())
	{
		c.detail = e;
		return false;
	}
	if (auto e = startingAccessFailure(game.map, c.request.nbTeams,
									   {{WHEAT, 12, "wheat"},
										{WOOD, 32, "wood"},
										{STONE, 24, "stone"},
										{ALGA, L.compact ? 64 : 48, "algae"}});
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
bool generate(Game &game, GenerationContext &c)
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
		auto builder = [&](Game &trial, GenerationContext &probe, const std::vector<int> &sites)
		{ return materialize(trial, probe, furnish(base, sites, probe)); };
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
std::string validateWorld(const Game &game, const GenerationContext &c)
{
	GenerationContext replay(c.request);
	std::vector<int> homes;
	const Torus actual(game.map);
	for (int k = 0; k < c.request.nbTeams; ++k)
		homes.push_back(actual.at(game.teams[k]->startPosX + 2, game.teams[k]->startPosY + 2));
	Layout L;
	bool matched = false;
	for (int attempt = 0; attempt < landscapeAttempts && !matched; ++attempt)
	{
		auto base = landscape(c.request, replay);
		if (std::find(base.proposals.begin(), base.proposals.end(), homes) == base.proposals.end())
			continue;
		GenerationContext probe(replay);
		auto candidate = furnish(base, homes, probe);
		if (!candidate.failure.empty())
			continue;
		matched = true;
		for (int i = 0; i < actual.size(); ++i)
			matched &= candidate.terrain[i] == game.map.getUMTerrain(i % actual.w, i / actual.w);
		if (matched)
			L = std::move(candidate);
	}
	if (!matched)
		return "Portage Lakes terrain no longer matches its design.";
	const auto &t = L.t;
	const auto &map = game.map;
	auto growth = Fertility::forMap(map, false);
	if (auto e = containedPlotsMismatch(map, t, L.plotOf, &growth); !e.empty())
		return e;

	for (const auto &crossing : L.portages)
		for (int i : crossing.cut)
			if (map.getResource(i % t.w, i / t.w).type != WOOD || growth.at(i % t.w, i / t.w) != 0)
				return "A Portage Lakes woodland shortcut is missing or can regrow.";
	if (auto e = mechanismFailure(map, L); !e.empty())
		return e;
	if (auto e = startingAccessFailure(map, c.request.nbTeams,
									   {{WHEAT, 12, "wheat"},
										{WOOD, 32, "wood"},
										{STONE, 24, "stone"},
										{ALGA, L.compact ? 64 : 48, "algae"}});
		!e.empty())
		return e;
	return walkFromFirstColony(map, c.request.nbTeams, "the lake country",
							   "without cutting or swimming")
		.error;
}
} // namespace
PortageLakesOptions::PortageLakesOptions(const GenerationRequest &r)
	: elongation(r.option("lake-elongation")), depth(r.option("portage-depth")),
	  trails(r.option("extra-trails")), wheat(r.option("wheat-amount")),
	  wood(r.option("wood-amount")), stone(r.option("stone-amount")),
	  algae(r.option("algae-amount")), fruit(r.option("fruit-amount"))
{
}
GeneratorDefinition portageLakesDefinition()
{
	return {"portage-lakes",
			58,
			"Portage Lakes",
			1,
			false,
			{{"lake-elongation", "Lake elongation", 125, 300, 25, 200, ControlGroup::Terrain},
			 {"portage-depth", "Portage depth", 2, 8, 1, 4, ControlGroup::Layout},
			 {"extra-trails", "Extra trails", 0, 100, 25, 25, ControlGroup::Layout},
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
