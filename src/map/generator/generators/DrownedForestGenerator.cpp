// SPDX-License-Identifier: GPL-3.0-or-later
#include "DrownedForestGenerator.h"
#include "Contact.h"
#include "Building.h"
#include "game/entities/BuildingType.h"
#include "Team.h"
#include "Drawing.h"
#include "Farmland.h"
#include "FertilityField.h"
#include "Game.h"
#include "GenerationContext.h"
#include "GraphMaze.h"
#include "Growth.h"
#include "LatticeNoise.h"
#include "Morphology.h"
#include "Pipeline.h"
#include "Planting.h"
#include "Points.h"
#include "Resources.h"
#include "Room.h"
#include "ScoredSettlements.h"
#include "Settlements.h"
#include "Sketch.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <numeric>
#include <string>
#include <vector>
using namespace MapGeneration;

// Drowned Forest: wet woodland islands, branching sandbars and found meadow settlements.
// Sand routes always work; sandy inlets almost meet inside the woods, leaving a timber neck
// whose removal opens a shorter route. Ordinary resource growth may reclaim that route.
// Shore gardens and meadow margins are sealed by sand, never by invisible no-growth flags.
// Starts are selected on the landscape and measured, not stamped copies or symmetric wedges.
namespace
{
constexpr int kHomeRadius = 13, kFarmWheat = 60, kFarmWood = 20;
constexpr double kRoadWidth = 2.5;
struct Clearing
{
	int site = -1, island = -1, radius = 0, turn = 0, roomScore = 0;
	int wheatSite = -1, woodSite = -1;
	std::vector<int> town, wheat, wood, exits;
};
struct Shortcut
{
	int a = -1, b = -1, island = -1;
	std::vector<int> plug;
	int before = -1, after = -1;
};
struct Layout
{
	Torus t{1, 1};
	TerrainSketch terrain;
	std::vector<int> labels, homes;
	std::vector<unsigned char> forest, roads;
	std::vector<Clearing> clearings;
	std::vector<Shortcut> shortcuts;
	std::vector<std::array<int, 2>> edges;
	std::vector<std::vector<std::pair<int, int>>> landings;
	std::string failure;
};
int localTile(const Torus &t, const Clearing &g, int x, int y)
{
	for (int k = 0; k < g.turn; ++k)
	{
		const int old = x;
		x = -y;
		y = old;
	}
	return t.at(g.site % t.w + x, g.site / t.w + y);
}
int homeAnchor(const Torus &t, const Clearing &g)
{
	const int dx = t.offsetX(g.site % t.w, g.wheatSite % t.w);
	const int dy = t.offsetY(g.site / t.w, g.wheatSite / t.w);
	const double length = std::max(1.0, std::hypot(dx, dy));
	return t.at(g.site % t.w + int(std::lround(10 * dx / length)),
				g.site / t.w + int(std::lround(10 * dy / length)));
}
ShapePoint point(const Torus &t, int i)
{
	return {double(i % t.w), double(i / t.w)};
}
std::string requestFailure(const GenerationRequest &r)
{
	const int w = 1 << r.wDec, h = 1 << r.hDec;
	if (std::min(w, h) < 128 || std::max(w, h) > 512 || std::max(w, h) > 2 * std::min(w, h))
		return "Drowned Forest needs 128–512 tile sides, square or 2:1.";
	const int maximum = std::min(8, w * h / 8192);
	if (r.nbTeams < 1 || r.nbTeams > maximum)
		return "Drowned Forest needs at least 8192 tiles per colony, up to eight colonies.";
	return "";
}

std::vector<unsigned char> sketchWalk(const Layout &L)
{
	auto water = pureTiles(L.terrain, L.t, WATER);
	const auto grass = pureTiles(L.terrain, L.t, GRASS);
	std::vector<unsigned char> town(L.t.size(), 0);
	for (const auto &g : L.clearings)
		for (int i : g.town)
			town[i] = 1;
	for (int i = 0; i < L.t.size(); ++i)
		water[i] = !water[i] && (!grass[i] || town[i]) && !L.forest[i];
	for (const auto &g : L.clearings)
		for (const auto *field : {&g.wheat, &g.wood})
			for (int i : *field)
				water[i] = 0;
	return water;
}

// Mark a winding shape only on its existing island. The town's outline is different at
// every site; its sand margin is the clearing's boundary against the encroaching woodland.
std::vector<int> plot(Layout &L, int site, double rx, double ry, const std::vector<int> &noise,
					  int island, const std::vector<int> &avoid = {})
{
	std::vector<int> corners;
	const auto excluded = dilate(L.t, tileCorners(L.t, tileMask(L.t, avoid)), 2);
	const int cx = site % L.t.w, cy = site / L.t.w;
	for (int dy = -int(ry) - 3; dy <= int(ry) + 3; ++dy)
		for (int dx = -int(rx) - 3; dx <= int(rx) + 3; ++dx)
		{
			const int i = L.t.at(cx + dx, cy + dy);
			const double q = std::pow((dx - 3.0 * std::sin(dy / 7.0 + cx)), 2) / (rx * rx) +
							 std::pow(dy - 2.0 * std::sin(dx / 6.0 + cy), 2) / (ry * ry);
			if (q < 0.82 + 0.4 * noise[i] / 65535.0 && L.labels[i] == island &&
				L.terrain[i] == GRASS && !excluded[i])
				corners.push_back(i);
		}
	return stampContainedPlot(L.terrain, L.t, corners, 1);
}

bool independentExits(const Torus &, const std::vector<unsigned char> &, const Clearing &);

Layout design(const GenerationRequest &r, GenerationContext &c)
{
	Layout L;
	L.t = {1 << r.wDec, 1 << r.hDec};
	L.failure = requestFailure(r);
	if (!L.failure.empty())
		return L;
	const DrownedForestOptions o(r);
	const Torus &t = L.t;
	const int n = t.size();
	// Relaxed, jittered sites give broad islands even on 128 maps; warp erases the lattice
	// from their coastlines. The sites are terrain seeds, not predetermined colony positions.
	auto sites = spreadPoints(t, 64, c, "drowned-sites", 75);
	sites = relaxPoints(t, sites, 3);
	L.labels = nearestSiteLabels(t, sites, 64, c, "drowned-coasts", 150, 12);
	std::vector<unsigned char> boundary(n, 0);
	for (int i = 0; i < n; ++i)
		for (const auto &d : kCardinalSteps)
			if (L.labels[i] != L.labels[t.at(i % t.w + d[0], i / t.w + d[1])])
				boundary[i] = 1;
	const auto inland = stepsFrom(t, boundary);
	const auto noise = periodicNoise(t.w, t.h, 5, c.stream("drowned-grain"));
	L.terrain.assign(n, WATER);
	std::vector<RadialShape> coast;
	for (int j = 0; j < int(sites.size()); ++j)
		coast.emplace_back(46 + int(c.bounded("drowned-island-size", 9)), 0.28, c,
						   "drowned-island-lobes");
	for (int i = 0; i < n; ++i)
	{
		const int j = L.labels[i];
		const int dx = t.offsetX(sites[j].x, i % t.w), dy = t.offsetY(sites[j].y, i / t.w);
		const double radius = coast[j].radiusAt(std::atan2(dy, dx));
		if (inland[i] > 3 + noise[i] * 3 / 65536 && dx * dx + dy * dy < radius * radius)
			L.terrain[i] = GRASS;
	}
	layBeaches(L.terrain, t);
	const auto ground = pureTiles(L.terrain, t, GRASS);
	const auto room = clearance(t, ground);
	// Find each island's largest usable meadow. A farm needs ground on its eastern side,
	// so test the full town-and-garden envelope before placing anything.
	std::vector<Clearing> candidates;
	for (int island = 0; island < int(sites.size()); ++island)
	{
		int best = -1, bestTurn = 0, bestWheat = -1, bestWood = -1;
		long bestScore = -1000000000;
		constexpr int offsets[8][2] = {{22, 0},  {16, 16},   {0, 22},  {-16, 16},
									   {-22, 0}, {-16, -16}, {0, -22}, {16, -16}};
		for (int y = -20; y <= 20; y += 2)
			for (int x = -20; x <= 20; x += 2)
			{
				const int p = t.at(sites[island].x + x, sites[island].y + y);
				if (L.labels[p] != island || room[p] < kHomeRadius)
					continue;
				for (int f = 0; f < 8; ++f)
				{
					const int farm = t.at(p % t.w + offsets[f][0], p / t.w + offsets[f][1]);
					if (L.labels[farm] != island || room[farm] < 7)
						continue;
					for (int w = 0; w < 8; ++w)
					{
						const int timber = t.at(p % t.w + offsets[w][0], p / t.w + offsets[w][1]);
						if (L.labels[timber] != island || room[timber] < 5 ||
							t.dist2(farm % t.w, farm / t.w, timber % t.w, timber / t.w) < 18 * 18)
							continue;
						const long score = std::min(room[p], 17) * 100 +
										   std::min(room[farm], 10) * 80 +
										   std::min(room[timber], 8) * 70 - x * x - y * y;
						if (score > bestScore)
						{
							best = p;
							bestTurn = ((f + 1) / 2) % 4;
							bestWheat = farm;
							bestWood = timber;
							bestScore = score;
						}
					}
				}
			}
		if (best < 0)
			continue;
		Clearing clearing;
		clearing.site = best;
		clearing.island = island;
		clearing.radius = kHomeRadius;
		clearing.turn = bestTurn;
		clearing.roomScore = int(bestScore);
		clearing.wheatSite = bestWheat;
		clearing.woodSite = bestWood;
		candidates.push_back(clearing);
	}
	c.telemetry.measure("drowned-forest.home.landscape-candidates", candidates.size());
	std::stable_sort(candidates.begin(), candidates.end(), [](const Clearing &a, const Clearing &b)
					 { return a.roomScore > b.roomScore; });
	if (t.w == 128 && t.h == 128 && candidates.size() > 2)
	{
		size_t farthest = 1;
		int best = -1;
		for (size_t j = 1; j < candidates.size(); ++j)
		{
			const int a = candidates[0].site, b = candidates[j].site;
			const int distance = t.dist2(a % t.w, a / t.w, b % t.w, b / t.w);
			if (distance > best)
			{
				best = distance;
				farthest = j;
			}
		}
		std::swap(candidates[1], candidates[farthest]);
	}
	candidates.resize(
		std::min(int(candidates.size()),
				 std::max(r.nbTeams + (r.nbTeams >= 4 ? 2 : 0), int(sites.size() / 2))));
	std::vector<unsigned char> developed(sites.size(), 0);
	for (auto clearing : candidates)
	{
		const int best = clearing.site, island = clearing.island;
		developed[island] = 1;
		// A natural oval meadow and a crescent shore garden, separated by their sand margins.
		clearing.town = plot(L, best, 14 + (island % 3) * 0.5, 15, noise, island);
		clearing.wheat = plot(L, clearing.wheatSite, 8, 9, noise, island, clearing.town);
		auto avoid = clearing.town;
		avoid.insert(avoid.end(), clearing.wheat.begin(), clearing.wheat.end());
		clearing.wood = plot(L, clearing.woodSite, 6, 5, noise, island, avoid);
		// A shore-fed pool beyond the found grain patch keeps its interior renewable.
		auto keep = tileMask(t, clearing.town);
		for (int i : clearing.wheat)
			keep[i] = 1;
		for (int i : clearing.wood)
			keep[i] = 1;
		keep = dilate(t, tileCorners(t, keep), 1);
		const int fx = t.offsetX(best % t.w, clearing.wheatSite % t.w),
				  fy = t.offsetY(best / t.w, clearing.wheatSite / t.w);
		const double length = std::hypot(fx, fy);
		const int pond = t.at(clearing.wheatSite % t.w + int(std::lround(11 * fx / length)),
							  clearing.wheatSite / t.w + int(std::lround(11 * fy / length)));
		for (int dy = -7; dy <= 7; ++dy)
			for (int dx = -7; dx <= 7; ++dx)
			{
				const int i = t.at(pond % t.w + dx, pond / t.w + dy);
				if (dx * dx + dy * dy < 45 && L.labels[i] == island && ground[i] && !keep[i])
					L.terrain[i] = WATER;
			}
		L.clearings.push_back(std::move(clearing));
	}
	if (int(L.clearings.size()) < r.nbTeams)
	{
		L.failure = "Too few islands have room for sustainable meadow settlements.";
		return L;
	}
	// Near-neighbour sandbars, with a second edge at every island before optional loops.
	auto neighbours = siteNeighbours(t, L.labels, int(sites.size()));
	// Colony islands open into woodland approaches; they do not bypass the forest
	// with direct town-to-town causeways. Extra connections retain this structure.
	for (int a = 0; a < int(sites.size()); ++a)
	{
		auto &row = neighbours[a];
		row.erase(std::remove_if(row.begin(), row.end(),
								 [&](int b) { return developed[a] && developed[b]; }),
				  row.end());
	}
	for (int a = 0; a < int(sites.size()); ++a)
		if (developed[a])
		{
			std::vector<int> woods;
			for (int b = 0; b < int(sites.size()); ++b)
				if (!developed[b])
					woods.push_back(b);
			std::stable_sort(woods.begin(), woods.end(),
							 [&](int b, int d)
							 {
								 return t.dist2(sites[a].x, sites[a].y, sites[b].x, sites[b].y) <
										t.dist2(sites[a].x, sites[a].y, sites[d].x, sites[d].y);
							 });
			for (int j = 0; j < std::min(2, int(woods.size())); ++j)
			{
				const int b = woods[j];
				if (std::find(neighbours[a].begin(), neighbours[a].end(), b) == neighbours[a].end())
				{
					neighbours[a].push_back(b);
					neighbours[b].push_back(a);
				}
			}
		}
	for (auto &row : neighbours)
		std::sort(row.begin(), row.end());
	const auto graph = cellGraph(t, sites, neighbours);
	std::vector<unsigned char> blocked(sites.size(), 0), open(graph.edgeCells.size(), 0);
	if (!carveNearTree(graph, c, "drowned-bars", blocked, 35, open))
	{
		L.failure = "The sandbars could not connect the islands.";
		return L;
	}
	for (int s = 0; s < int(sites.size()); ++s)
	{
		int degree = 0;
		for (int e : graph.cellEdges[s])
			degree += open[e];
		for (int e : graph.cellEdges[s])
			if (degree < 2 && !open[e])
			{
				open[e] = 1;
				++degree;
			}
	}
	const int mandatory = std::count(open.begin(), open.end(), 1);
	auto spare = closedEdges(graph, open);
	c.shuffle(spare.begin(), spare.end(), "drowned-extra-bars");
	for (int k = 0; k < int(spare.size()) * o.connections / 100; ++k)
		open[spare[k]] = 1;
	L.roads.assign(n, 0);
	L.landings.resize(sites.size());
	std::vector<unsigned char> preserve(n, 0);
	for (const auto &g : L.clearings)
		for (const auto *v : {&g.town, &g.wheat, &g.wood})
			for (int i : *v)
				preserve[i] = 1;
	preserve = tileCorners(t, preserve);
	for (int e = 0; e < int(open.size()); ++e)
		if (open[e])
		{
			const auto pair = graph.edgeCells[e];
			const auto a = sites[pair[0]], b = sites[pair[1]];
			auto path = wanderingPath(t, {double(a.x), double(a.y)}, {double(b.x), double(b.y)},
									  kRoadWidth, 3, .15, c.stream("drowned-bar-shapes"));
			const auto tileAt = [&](int j)
			{ return t.at(int(std::lround(path[j].x)), int(std::lround(path[j].y))); };
			int first = 0, last = int(path.size()) - 1;
			while (first < last && ground[tileAt(first)] && L.labels[tileAt(first)] == pair[0])
				++first;
			while (last > first && ground[tileAt(last)] && L.labels[tileAt(last)] == pair[1])
				--last;
			first = std::max(0, first - 5);
			last = std::min(int(path.size()) - 1, last + 5);
			L.landings[pair[0]].push_back({pair[1], tileAt(first)});
			L.landings[pair[1]].push_back({pair[0], tileAt(last)});
			std::vector<StrokePoint> crossing(path.begin() + first, path.begin() + last + 1);
			std::vector<unsigned char> stroke(n, 0);
			strokePath(stroke, t, crossing);
			for (int i = 0; i < n; ++i)
				if (stroke[i] && !preserve[i])
					L.roads[i] = 1;
			L.edges.push_back(pair);
		}
	for (int i = 0; i < n; ++i)
		if (L.roads[i])
			L.terrain[i] = SAND;
	// Two meadow exits run out to opposite shores. These are the permanent paths; no
	// clearance repair is permitted later to create a third path through a timber neck.
	for (auto &glade : L.clearings)
	{
		std::vector<unsigned char> lane(n, 0);
		std::vector<int> neighbours;
		for (int e : graph.cellEdges[glade.island])
			if (open[e])
				neighbours.push_back(graph.other(e, glade.island));
		std::stable_sort(
			neighbours.begin(), neighbours.end(),
			[&](int a, int b)
			{
				return t.dist2(glade.site % t.w, glade.site / t.w, sites[a].x, sites[a].y) <
					   t.dist2(glade.site % t.w, glade.site / t.w, sites[b].x, sites[b].y);
			});
		if (neighbours.size() > 2)
		{
			const auto first = sites[neighbours.front()];
			const double ax = t.offsetX(glade.site % t.w, first.x),
						 ay = t.offsetY(glade.site / t.w, first.y);
			double least = 2;
			size_t opposite = 1;
			for (size_t j = 1; j < neighbours.size(); ++j)
			{
				const auto other = sites[neighbours[j]];
				const double bx = t.offsetX(glade.site % t.w, other.x),
							 by = t.offsetY(glade.site / t.w, other.y);
				const double dot =
					(ax * bx + ay * by) / std::sqrt((ax * ax + ay * ay) * (bx * bx + by * by));
				if (dot < least)
				{
					least = dot;
					opposite = j;
				}
			}
			std::swap(neighbours[1], neighbours[opposite]);
		}
		for (int index = 0; index < int(neighbours.size()) && glade.exits.size() < 2; ++index)
		{
			int shore = glade.site;
			for (const auto &[neighbour, landing] : L.landings[glade.island])
				if (neighbour == neighbours[index])
					shore = landing;
			// Route around the gardens with enough clearance for a three-wide lane.
			auto crops = tileMask(t, glade.wheat);
			for (int i : glade.wood)
				crops[i] = 1;
			crops = dilate(t, tileCorners(t, crops), 4);
			std::vector<unsigned char> allowed(n, 0);
			for (int i = 0; i < n; ++i)
				allowed[i] = L.labels[i] == glade.island && L.terrain[i] != WATER && !crops[i];
			if (!glade.exits.empty())
			{
				const auto used = dilate(t, lane, 2);
				const auto town = tileCorners(t, tileMask(t, glade.town));
				for (int i = 0; i < n; ++i)
					if (used[i] && !town[i])
						allowed[i] = 0;
			}
			auto roots = tileMask(t, glade.town);
			for (int i = 0; i < n; ++i)
				roots[i] = roots[i] && allowed[i];
			const auto distance = stepsFrom(t, roots, allowed);
			int landing = -1, best = INT_MAX;
			for (int dy = -6; dy <= 6; ++dy)
				for (int dx = -6; dx <= 6; ++dx)
				{
					const int i = t.at(shore % t.w + dx, shore / t.w + dy);
					if (allowed[i] && distance[i] > 0 && dx * dx + dy * dy < best)
					{
						landing = i;
						best = dx * dx + dy * dy;
					}
				}
			if (landing < 0)
				continue;
			glade.exits.push_back(landing);
			std::vector<StrokePoint> route;
			int current = landing;
			const double sx = glade.site % t.w, sy = glade.site / t.w;
			while (distance[current] > 0)
			{
				route.push_back({sx + t.offsetX(glade.site % t.w, current % t.w),
								 sy + t.offsetY(glade.site / t.w, current / t.w), kRoadWidth});
				int next = -1;
				for (int dy = -1; dy <= 1; ++dy)
					for (int dx = -1; dx <= 1; ++dx)
					{
						const int i = t.at(current % t.w + dx, current / t.w + dy);
						if (allowed[i] && distance[i] == distance[current] - 1 &&
							(next < 0 || i < next))
							next = i;
					}
				if (next < 0)
					break;
				current = next;
			}
			route.push_back({sx + t.offsetX(glade.site % t.w, current % t.w),
							 sy + t.offsetY(glade.site / t.w, current / t.w), kRoadWidth});
			strokePath(lane, t, route);
		}
		auto town = tileMask(t, glade.town);
		for (int i : glade.wheat)
			town[i] = 1;
		for (int i : glade.wood)
			town[i] = 1;
		const auto keep = tileCorners(t, town);
		for (int i = 0; i < n; ++i)
			if (lane[i] && L.labels[i] == glade.island && !keep[i])
				L.terrain[i] = SAND;
	}
	layBeaches(L.terrain, t);
	// Re-read all pure grass after every shoreline and margin has been drawn.
	const auto grass = pureTiles(L.terrain, t, GRASS);
	std::vector<unsigned char> reserved(n, 0);
	for (auto &glade : L.clearings)
	{
		for (auto *v : {&glade.town, &glade.wheat, &glade.wood})
		{
			v->erase(std::remove_if(v->begin(), v->end(), [&](int i) { return !grass[i]; }),
					 v->end());
			for (int i : *v)
				reserved[i] = 1;
		}
	}
	L.forest.assign(n, 0);
	const int forestThreshold = int(65536LL * o.wood / (o.wood + 25));
	for (int i = 0; i < n; ++i)
	{
		const int draw = c.bounded("drowned-ambient-wood", 65536);
		L.forest[i] = grass[i] && !reserved[i] && draw < forestThreshold;
	}
	c.telemetry.measure("drowned-forest.ambient-wood-probability", double(forestThreshold) / 65536);

	// Sandy flood inlets across a wooded shoulder almost meet. Their uncut central neck
	// and its lateral timber remain structural at zero ambient wood, so the shortcut
	// control never degenerates into an already-open route.
	const auto coastalWalk = sketchWalk(L);
	std::vector<Clearing> woodedIslands;
	for (int island = 0; island < int(sites.size()); ++island)
		if (!developed[island])
		{
			Clearing g;
			g.site = t.at(sites[island].x, sites[island].y);
			g.island = island;
			const auto &landings = L.landings[island];
			if (landings.size() < 2)
				continue;
			int first = 0, second = 1, best = -1000000;
			for (int j = 0; j < int(landings.size()); ++j)
			{
				const int a = landings[j].second;
				const auto distance = stepsFrom(t, tileMask(t, {a}), coastalWalk);
				for (int k = j + 1; k < int(landings.size()); ++k)
				{
					const int b = landings[k].second;
					if (distance[b] < 0)
						continue;
					const int straight = std::max(std::abs(t.offsetX(a % t.w, b % t.w)),
												  std::abs(t.offsetY(a / t.w, b / t.w)));
					const int saving = distance[b] - straight;
					if (saving > best)
					{
						best = saving;
						first = j;
						second = k;
					}
				}
			}
			const int firstLanding = landings[first].second, lastLanding = landings[second].second;
			g.site =
				t.at(firstLanding % t.w + t.offsetX(firstLanding % t.w, lastLanding % t.w) / 2,
					 firstLanding / t.w + t.offsetY(firstLanding / t.w, lastLanding / t.w) / 2);
			g.exits = {landings[first].second, landings[second].second};
			const int firstTile = g.exits[0];
			const double angle = std::atan2(t.offsetY(g.site / t.w, firstTile / t.w),
											t.offsetX(g.site % t.w, firstTile % t.w));
			g.turn = (int(std::lround(angle / (3.14159265358979323846 / 2))) + 6) % 4;
			woodedIslands.push_back(g);
		}
	for (const auto &glade : woodedIslands)
	{
		const int offset = 0;
		Shortcut shortcut;
		shortcut.island = glade.island;
		shortcut.a = localTile(t, glade, -o.neck / 2 - 4, offset);
		shortcut.b = localTile(t, glade, o.neck / 2 + 4, offset);
		bool fits = true;
		for (int dy = -2; dy <= 2; ++dy)
			for (int dx = -o.neck / 2 - 4; dx <= o.neck / 2 + 4; ++dx)
			{
				const int i = localTile(t, glade, dx, offset + dy);
				if (!grass[i] || reserved[i] || L.labels[i] != glade.island)
					fits = false;
			}
		if (!fits)
		{
			c.telemetry.fallback("drowned-forest.shortcut.fit", "No wooded shoulder", glade.island);
			continue;
		}
		// A continuous wooded shoulder makes the designated cut meaningful even when
		// ambient forest is zero. Its ends follow the island's natural coast.
		for (int dy = -80; dy <= 80; ++dy)
			for (int dx = -o.neck / 2 - 9; dx <= o.neck / 2 + 9; ++dx)
			{
				const int i = localTile(t, glade, dx, offset + dy);
				const int shift = int(std::lround(2 * std::sin(dy / 11.0)));
				const int half = o.neck / 2 + 4 + noise[i] * 3 / 65536;
				if (grass[i] && !reserved[i] && L.labels[i] == glade.island &&
					std::abs(dx - shift) <= half)
					L.forest[i] = 1;
			}
		for (int dx = -o.neck / 2; dx <= o.neck / 2; ++dx)
			for (int dy = -2; dy <= 2; ++dy)
				shortcut.plug.push_back(localTile(t, glade, dx, offset + dy));
		const auto protectedCorners = tileCorners(t, tileMask(t, shortcut.plug));
		std::vector<unsigned char> inlets(n, 0);
		for (int side : {-1, 1})
		{
			const int shore = glade.exits[side < 0 ? 0 : 1];
			strokePath(inlets, t,
					   wanderingPath(t, point(t, shore),
									 point(t, side < 0 ? shortcut.a : shortcut.b), 1.7, 3, 0.15,
									 c.stream("drowned-inlets")));
		}
		const auto mouthA = point(t, shortcut.a);
		strokePath(inlets, t,
				   {{mouthA.x, mouthA.y, 1.3},
					{mouthA.x + t.offsetX(shortcut.a % t.w, shortcut.b % t.w),
					 mouthA.y + t.offsetY(shortcut.a / t.w, shortcut.b / t.w), 1.3}});
		for (int i = 0; i < n; ++i)
			if (inlets[i] && L.labels[i] == glade.island && L.terrain[i] != WATER &&
				!protectedCorners[i] && !reserved[i])
				L.terrain[i] = SAND;
		L.shortcuts.push_back(std::move(shortcut));
	}
	// Most woodland remains unoccupied. A forward meadow sits beyond a timber cut
	// from the nearer colony approaches; some islands retain only their wild shore.
	const auto inletGrass = pureTiles(L.terrain, t, GRASS);
	for (int i = 0; i < n; ++i)
		L.forest[i] = L.forest[i] && inletGrass[i];
	std::vector<int> homeRoots;
	for (const auto &g : L.clearings)
		homeRoots.insert(homeRoots.end(), g.town.begin(), g.town.end());
	const auto fromHomes = stepsFrom(t, tileMask(t, homeRoots), sketchWalk(L));
	const auto finishMeadows = [&](Layout L, int sideMask)
	{
		int woodlandIndex = 0;
		for (const auto &woodland : woodedIslands)
		{
			if (woodedIslands.size() > 3 && woodland.island % 3 == 0)
				continue;
			const int a = fromHomes[woodland.exits[0]], b = fromHomes[woodland.exits[1]];
			const int preferred = a >= 0 && (b < 0 || a < b) ? 1 : -1;
			const int side =
				woodlandIndex < 4 && (sideMask & (1 << woodlandIndex)) ? -preferred : preferred;
			++woodlandIndex;
			Clearing g = woodland;
			g.site = localTile(t, woodland, side * (o.neck / 2 + 12), 0);
			g.radius = o.clearing / 2 + 4;
			std::vector<int> avoid;
			for (const auto &neck : L.shortcuts)
				avoid.insert(avoid.end(), neck.plug.begin(), neck.plug.end());
			for (const auto &old : L.clearings)
				for (const auto *v : {&old.town, &old.wheat, &old.wood})
					avoid.insert(avoid.end(), v->begin(), v->end());
			g.town = plot(L, g.site, g.radius, 12, noise, g.island, avoid);
			avoid.insert(avoid.end(), g.town.begin(), g.town.end());
			const int upper = localTile(t, woodland, side * (o.neck / 2 + 12), 17);
			const int lower = localTile(t, woodland, side * (o.neck / 2 + 12), -17);
			const int farm = room[upper] > room[lower] ? upper : lower;
			g.wheat = plot(L, farm, 6, 6, noise, g.island, avoid);
			for (int i : g.town)
				L.forest[i] = 0;
			for (int i : g.wheat)
				L.forest[i] = 0;
			if (!g.town.empty())
				L.clearings.push_back(std::move(g));
		}

		layBeaches(L.terrain, t);
		const auto finalGrass = pureTiles(L.terrain, t, GRASS);
		for (int i = 0; i < n; ++i)
			L.forest[i] = L.forest[i] && finalGrass[i];
		for (auto &g : L.clearings)
			for (auto *v : {&g.town, &g.wheat, &g.wood})
				v->erase(
					std::remove_if(v->begin(), v->end(), [&](int i) { return !finalGrass[i]; }),
					v->end());
		L.clearings.erase(std::remove_if(L.clearings.begin(), L.clearings.end(),
										 [](const Clearing &g) { return g.town.empty(); }),
						  L.clearings.end());
		auto walking = sketchWalk(L);
		for (auto &s : L.shortcuts)
		{
			s.before = stepsFrom(t, tileMask(t, {s.a}), walking)[s.b];
			auto cut = walking;
			for (int i : s.plug)
				cut[i] = 1;
			s.after = stepsFrom(t, tileMask(t, {s.a}), cut)[s.b];
			c.telemetry.measure("drowned-forest.shortcut.candidate-before", s.before, s.island);
			c.telemetry.measure("drowned-forest.shortcut.candidate-after", s.after, s.island);
		}
		L.shortcuts.erase(std::remove_if(L.shortcuts.begin(), L.shortcuts.end(),
										 [](const Shortcut &s)
										 {
											 return s.before < 0 || s.after < 0 ||
													s.before - s.after < 8 ||
													s.after * 4 > s.before * 3;
										 }),
						  L.shortcuts.end());
		// Site proposals are filtered by a real nearby shortcut, not just the island graph.
		std::vector<Clearing> viable;
		for (auto &glade : L.clearings)
		{
			bool shortcut = false;
			for (const auto &s : L.shortcuts)
				shortcut |=
					t.dist2(glade.site % t.w, glade.site / t.w, s.a % t.w, s.a / t.w) < 100 * 100;
			if (shortcut && glade.town.size() >= 320 && glade.wheat.size() >= 60 &&
				glade.wood.size() >= 20)
				viable.push_back(glade);
		}
		// All clearings remain on the map; home candidates are their indices.
		for (int s = 0; s < int(L.clearings.size()); ++s)
			for (const auto &v : viable)
				if (v.site == L.clearings[s].site)
					L.homes.push_back(s);
		c.telemetry.measure("drowned-forest.home.room-candidates", L.homes.size());
		const auto permanent = sketchWalk(L);
		const auto allGrass = pureTiles(L.terrain, t, GRASS);
		L.homes.erase(std::remove_if(L.homes.begin(), L.homes.end(),
									 [&](int h)
									 {
										 const auto &g = L.clearings[h];
										 const auto town = tileMask(t, g.town);
										 auto lane = permanent;
										 for (int i = 0; i < n; ++i)
											 if (allGrass[i] && !town[i])
												 lane[i] = 0;
										 return !independentExits(t, lane, g);
									 }),
					  L.homes.end());
		c.telemetry.measure("drowned-forest.home.exit-candidates", L.homes.size());
		// Reject unusable starts before constructing and furnishing eight whole candidate worlds.
		// These are only prefilters; the final validator repeats the checks from actual workers.
		std::vector<std::vector<int>> fromMouthA, fromMouthB;
		for (const auto &shortcut : L.shortcuts)
		{
			fromMouthA.push_back(stepsFrom(t, tileMask(t, {shortcut.a}), permanent));
			fromMouthB.push_back(stepsFrom(t, tileMask(t, {shortcut.b}), permanent));
		}
		const auto distanceTo = [](const std::vector<int> &d, const Clearing &g)
		{
			int best = INT_MAX;
			for (int i : g.town)
				if (d[i] >= 0)
					best = std::min(best, d[i]);
			return best;
		};
		L.homes.erase(
			std::remove_if(
				L.homes.begin(), L.homes.end(),
				[&](int h)
				{
					const auto &g = L.clearings[h];
					const auto distance = stepsFrom(t, tileMask(t, {homeAnchor(t, g)}), permanent);
					for (size_t k = 0; k < L.shortcuts.size(); ++k)
					{
						const auto &shortcut = L.shortcuts[k];
						const int a = distance[shortcut.a], b = distance[shortcut.b];
						if ((a < 0 || a > 100) && (b < 0 || b > 100))
							continue;
						for (size_t j = 0; j < L.clearings.size(); ++j)
						{
							const auto &target = L.clearings[j];
							if (int(j) == h || target.town.size() < 140 || target.wheat.size() < 30)
								continue;
							const int before = distanceTo(distance, target);
							const int da = distanceTo(fromMouthA[k], target),
									  db = distanceTo(fromMouthB[k], target);
							if (before == INT_MAX)
								continue;
							if ((a >= 0 && db != INT_MAX &&
								 a + shortcut.after + db <= before - 8) ||
								(b >= 0 && da != INT_MAX && b + shortcut.after + da <= before - 8))
								return false;
						}
					}
					return true;
				}),
			L.homes.end());
		c.telemetry.measure("drowned-forest.home.viable-candidates", L.homes.size());
		if (int(L.homes.size()) < r.nbTeams)
			L.failure = "Too few meadow settlements have a useful wooded shortcut.";
		c.telemetry.measure("drowned-forest.islands", sites.size());
		c.telemetry.measure("drowned-forest.clearings", L.clearings.size());
		c.telemetry.measure("drowned-forest.sandbars.mandatory", mandatory);
		c.telemetry.measure("drowned-forest.sandbars.actual", L.edges.size());
		c.telemetry.measure("drowned-forest.shortcuts", L.shortcuts.size());
		for (int k = 0; k < int(L.shortcuts.size()); ++k)
		{
			c.telemetry.measure("drowned-forest.shortcut.walk-before", L.shortcuts[k].before, k);
			c.telemetry.measure("drowned-forest.shortcut.walk-after", L.shortcuts[k].after, k);
			c.telemetry.measure("drowned-forest.shortcut.wood-tiles", L.shortcuts[k].plug.size(),
								k);
			c.telemetry.measure("drowned-forest.shortcut.mouth-a", L.shortcuts[k].a, k);
			c.telemetry.measure("drowned-forest.shortcut.mouth-b", L.shortcuts[k].b, k);
			c.telemetry.measure("drowned-forest.shortcut.centre",
								L.shortcuts[k].plug[L.shortcuts[k].plug.size() / 2], k);
		}
		return L;
	};
	const auto beforeMeadows = c.telemetry;
	auto selected = finishMeadows(L, 0);
	auto selectedTelemetry = c.telemetry;
	int selectedSides = 0, variants = 1;
	// On the smallest map, both colonies may approach opposite ends of the same
	// woods. A side chosen only for the nearest colony can strand the other one.
	// Try the bounded side combinations before discarding an otherwise good island
	// landscape. Larger maps have enough independent woodland destinations already.
	const int combinations =
		t.w == 128 && t.h == 128 ? 1 << std::min(4, int(woodedIslands.size())) : 1;
	for (int sides = 1; sides < combinations && int(selected.homes.size()) < r.nbTeams; ++sides)
	{
		c.telemetry = beforeMeadows;
		auto alternative = finishMeadows(L, sides);
		++variants;
		if (alternative.homes.size() > selected.homes.size())
		{
			selected = std::move(alternative);
			selectedTelemetry = c.telemetry;
			selectedSides = sides;
		}
	}
	c.telemetry = std::move(selectedTelemetry);
	c.telemetry.measure("drowned-forest.meadow.side-variants", variants);
	c.telemetry.measure("drowned-forest.meadow.selected-sides", selectedSides);
	return selected;
}

int meadowDistance(const std::vector<int> &distance, const Clearing &g)
{
	int best = INT_MAX;
	for (int i : g.town)
		if (distance[i] >= 0)
			best = std::min(best, distance[i]);
	return best == INT_MAX ? -1 : best;
}

// Prove circulation in a small window first. When the real sources are outside,
// the earliest reachable boundary tile has a path that cannot cross this window's
// proposed buildings. A local failure falls back to the full map, preserving the
// original acceptance test even for winding approaches or wraparound detours.
BuildingArrangement nearbyBuildingGrid(const Torus &t, const std::vector<unsigned char> &buildable,
									   const std::vector<unsigned char> &walking,
									   const BuildingGrid &grid, const std::vector<int> &entrances,
									   const std::vector<int> &arrival)
{
	// stepsFrom admits blocked sources, whereas building circulation does not.
	// Such a distance field cannot justify an external boundary entrance.
	for (int i : entrances)
		if (i < 0 || i >= t.size() || !walking[i])
			return arrangeBuildingGrid(t, buildable, walking, grid, entrances);
	const Torus local{64, 64};
	const int ox = (grid.bounds.x0 + grid.bounds.x1) / 2 - 32;
	const int oy = (grid.bounds.y0 + grid.bounds.y1) / 2 - 32;
	if (t.w < 128 || t.h < 128 || grid.bounds.x0 < ox + 1 || grid.bounds.y0 < oy + 1 ||
		grid.bounds.x1 > ox + 63 || grid.bounds.y1 > oy + 63)
		return arrangeBuildingGrid(t, buildable, walking, grid, entrances);
	std::vector<unsigned char> land(local.size(), 0), walk(local.size(), 0);
	std::vector<int> roots;
	for (int i : entrances)
	{
		const int x = t.offsetX(t.at(ox, oy) % t.w, i % t.w);
		const int y = t.offsetY(t.at(ox, oy) / t.w, i / t.w);
		if (x > 0 && x < 63 && y > 0 && y < 63)
			roots.push_back(local.at(x, y));
	}
	int first = -1, best = INT_MAX;
	for (int y = 1; y < 63; ++y)
		for (int x = 1; x < 63; ++x)
		{
			const int i = t.at(ox + x, oy + y), j = local.at(x, y);
			land[j] = buildable[i];
			walk[j] = walking[i];
			if ((x == 1 || y == 1 || x == 62 || y == 62) && walk[j] && arrival[i] >= 0 &&
				arrival[i] < best)
			{
				best = arrival[i];
				first = j;
			}
		}
	if (roots.empty() && first >= 0)
		roots.push_back(first);
	auto localGrid = grid;
	localGrid.bounds = {grid.bounds.x0 - ox, grid.bounds.y0 - oy, grid.bounds.x1 - ox,
						grid.bounds.y1 - oy};
	auto result = arrangeBuildingGrid(local, land, walk, localGrid, roots);
	if (!result.failure.empty())
		return arrangeBuildingGrid(t, buildable, walking, grid, entrances);
	for (auto &box : result.footprints)
	{
		box.x0 += ox;
		box.x1 += ox;
		box.y0 += oy;
		box.y1 += oy;
	}
	return result;
}

// Find shared footholds from actual sand walks, linking existing shore meadows first.
// Where needed, an elongated shoal beside a balanced sandbar supplies a forward base.
void addJunctions(Layout &L, const std::vector<int> &homes, int size)
{
	const auto &t = L.t;
	if (homes.size() < 2)
		return;
	auto walking = sketchWalk(L);
	const auto homeDistances = [&](const Layout &layout)
	{
		std::vector<std::vector<int>> result;
		const auto passable = sketchWalk(layout);
		for (int h : homes)
			result.push_back(
				stepsFrom(t, tileMask(t, {homeAnchor(t, layout.clearings[h])}), passable));
		return result;
	};
	auto distances = homeDistances(L);
	const auto isHome = [&](int j)
	{ return std::find(homes.begin(), homes.end(), j) != homes.end(); };
	std::vector<unsigned char> worthwhile(L.clearings.size(), 0);
	for (int j = 0; j < int(L.clearings.size()); ++j)
	{
		const auto &g = L.clearings[j];
		if (g.wheat.size() < 30 || g.town.size() < 140)
			continue;
		const int x = g.site % t.w, y = g.site / t.w, radius = std::max(g.radius, 13);
		const auto room = nearbyBuildingGrid(
			t, tileMask(t, g.town), walking,
			{{x - radius, y - radius, x + radius + 1, y + radius + 1}, 4, 4, 2, 1},
			{homeAnchor(t, L.clearings[homes[0]])}, distances[0]);
		worthwhile[j] = room.failure.empty() && room.footprints.size() >= 3;
	}
	const auto shared =
		[&](const Layout &layout, const std::vector<std::vector<int>> &d, int k, int j)
	{
		if (j < int(worthwhile.size()) && !worthwhile[j])
			return false;
		const int a = meadowDistance(d[k], layout.clearings[j]);
		if (a < 0 || a > 65)
			return false;
		for (int rival = 0; rival < int(homes.size()); ++rival)
			if (rival != k)
			{
				const int b = meadowDistance(d[rival], layout.clearings[j]);
				if (b >= 0 && b <= 65 && std::abs(a - b) <= 8)
					return true;
			}
		return false;
	};
	std::vector<unsigned char> necks(t.size(), 0);
	for (const auto &neck : L.shortcuts)
		for (int i : neck.plug)
			necks[i] = 1;
	necks = dilate(t, necks, 3);
	const auto connect = [&](Layout &layout, int from, int to)
	{
		const double x = from % t.w, y = from / t.w;
		const int dx = t.offsetX(from % t.w, to % t.w), dy = t.offsetY(from / t.w, to / t.w);
		const std::vector<StrokePoint> path{
			{x, y, 2}, {x + dx * .5 - dy * .08, y + dy * .5 + dx * .08, 2}, {x + dx, y + dy, 2}};
		if (strokeIntersectsMask(t, path, necks))
			return false;
		std::vector<unsigned char> keep(t.size(), 0), lane(t.size(), 0);
		for (const auto &g : layout.clearings)
			for (const auto *v : {&g.town, &g.wheat, &g.wood})
				for (int i : *v)
					keep[i] = 1;
		keep = tileCorners(t, keep);
		strokePath(lane, t, path);
		for (int i = 0; i < t.size(); ++i)
			if (lane[i] && !keep[i])
				layout.terrain[i] = SAND;
		const auto grass = pureTiles(layout.terrain, t, GRASS);
		for (int i = 0; i < t.size(); ++i)
			layout.forest[i] = layout.forest[i] && grass[i];
		return true;
	};
	for (int k = 0; k < int(homes.size()); ++k)
	{
		bool done = false;
		for (int j = 0; j < int(L.clearings.size()); ++j)
			if (!isHome(j) && shared(L, distances, k, j))
				done = true;
		if (done)
			continue;
		// Keep several balanced attachment points, rather than demanding that the road
		// midpoint itself also contain a large buildable field.
		std::vector<std::pair<int, int>> ranked;
		for (int i = 0; i < t.size(); ++i)
		{
			const int a = distances[k][i];
			if (!walking[i] || a < 15 || a > 75 || necks[i])
				continue;
			for (int rival = 0; rival < int(homes.size()); ++rival)
				if (rival != k)
				{
					const int b = distances[rival][i];
					if (b >= 15 && b <= 75 && std::abs(a - b) <= 4)
						ranked.push_back({std::max(a, b) + 4 * std::abs(a - b), i});
				}
		}
		std::sort(ranked.begin(), ranked.end());
		std::vector<int> attachments;
		for (const auto &[score, i] : ranked)
		{
			bool nearbyJunction = false;
			for (int q : attachments)
				if (t.dist2(i % t.w, i / t.w, q % t.w, q / t.w) < 64)
					nearbyJunction = true;
			if (!nearbyJunction)
				attachments.push_back(i);
			if (attachments.size() == 12)
				break;
		}
		struct Spur
		{
			int length, q, j, landing;
		};
		std::vector<Spur> spurs;
		for (int q : attachments)
			for (int j = 0; j < int(L.clearings.size()); ++j)
				if (!isHome(j))
				{
					int landing = -1, length = INT_MAX;
					for (int tile : L.clearings[j].town)
					{
						const int d = t.dist2(q % t.w, q / t.w, tile % t.w, tile / t.w);
						if (d < length)
						{
							length = d;
							landing = tile;
						}
					}
					if (landing >= 0 && length <= 32 * 32)
						spurs.push_back({length, q, j, landing});
				}
		std::stable_sort(spurs.begin(), spurs.end(),
						 [](const Spur &a, const Spur &b) { return a.length < b.length; });
		int attempts = 0;
		for (const auto &spur : spurs)
		{
			if (++attempts > 24)
				break;
			Layout trial = L;
			if (!connect(trial, spur.q, spur.landing))
				continue;
			auto d = homeDistances(trial);
			if (!shared(trial, d, k, spur.j))
				continue;
			L = std::move(trial);
			distances = std::move(d);
			done = true;
			break;
		}
		if (done)
		{
			walking = sketchWalk(L);
			continue;
		}
		// A meadow bank follows a balanced sandbar junction. Keep its old walking
		// spine open; the small grain shelf lies beside it, surrounded by wet sand.
		std::vector<unsigned char> protectedTiles = necks;
		for (const auto &g : L.clearings)
			for (const auto *v : {&g.town, &g.wheat, &g.wood})
				for (int i : *v)
					protectedTiles[i] = 1;
		const auto protectedCorners = dilate(t, tileCorners(t, protectedTiles), 2);
		for (int q : attachments)
		{
			if (done)
				break;
			for (int turn = 0; turn < 4; ++turn)
			{
				Layout trial = L;
				Clearing g;
				g.site = q;
				g.island = L.labels[q];
				g.radius = size / 2 + 4;
				g.turn = turn;
				std::vector<int> townCorners, cropCorners;
				bool fits = true;
				const int half = size / 2 + 3;
				for (int x = -half; x <= half; ++x)
				{
					const double width = 6.5 + 1.1 * std::sin(x * .37 + q) -
										 std::max(0, std::abs(x) - half + 3) * .7;
					for (int y = -int(width); y <= int(width); ++y)
					{
						const int i = localTile(t, g, x, y);
						if (protectedCorners[i])
							fits = false;
						townCorners.push_back(i);
					}
				}
				for (int x = -6; x <= 6; ++x)
					for (int y = 9; y <= 16; ++y)
					{
						const int i = localTile(t, g, x, y);
						if (protectedCorners[i])
							fits = false;
						if (!walking[i])
							cropCorners.push_back(i);
					}
				if (!fits)
					continue;
				g.town = stampContainedPlot(trial.terrain, t, townCorners, 1);
				g.wheat = stampContainedPlot(trial.terrain, t, cropCorners, 1);
				if (g.wheat.size() < 30)
					continue;
				const auto pure = pureTiles(trial.terrain, t, GRASS);
				g.town.erase(
					std::remove_if(g.town.begin(), g.town.end(), [&](int i) { return !pure[i]; }),
					g.town.end());
				for (int i : g.town)
					trial.forest[i] = 0;
				for (int i : g.wheat)
					trial.forest[i] = 0;
				trial.clearings.push_back(g);
				const auto grass = pureTiles(trial.terrain, t, GRASS);
				for (int i = 0; i < t.size(); ++i)
					trial.forest[i] = trial.forest[i] && grass[i];
				const auto passable = sketchWalk(trial);
				const int radius = std::max(g.radius, 13);
				const auto arrangement =
					arrangeBuildingGrid(t, tileMask(t, g.town), passable,
										{{q % t.w - radius, q / t.w - radius, q % t.w + radius + 1,
										  q / t.w + radius + 1},
										 4,
										 4,
										 2,
										 1},
										{L.clearings[homes[0]].site});
				if (!arrangement.failure.empty() || arrangement.footprints.size() < 3)
					continue;
				auto d = homeDistances(trial);
				if (!shared(trial, d, k, int(trial.clearings.size()) - 1))
					continue;
				L = std::move(trial);
				distances = std::move(d);
				walking = sketchWalk(L);
				done = true;
				break;
			}
		}
	}
}

// Neutral sizing affects woodland footholds; colony candidates retain their construction room.
Layout settlementLayout(const Layout &source, const std::vector<int> &homes, int size)
{
	Layout L = source;
	const auto &t = L.t;

	const auto grass = pureTiles(L.terrain, t, GRASS);
	for (int i = 0; i < t.size(); ++i)
		L.forest[i] = L.forest[i] && grass[i];
	addJunctions(L, homes, size);
	return L;
}
std::vector<int> occupiedClearings(const Game &game, const GenerationContext &c, const Layout &L)
{
	std::vector<int> homes;
	const auto units = unitTilesByTeam(game.map, c.request.nbTeams);
	for (const auto &team : units)
	{
		int best = -1, distance = INT_MAX;
		for (int j = 0; j < int(L.clearings.size()); ++j)
			for (int i : team)
			{
				const int p = L.clearings[j].site;
				const int d = L.t.dist2(p % L.t.w, p / L.t.w, i % L.t.w, i / L.t.w);
				if (d < distance)
				{
					best = j;
					distance = d;
				}
			}
		homes.push_back(best);
	}
	return homes;
}

bool buildWorld(Game &game, GenerationContext &c, const Layout &source,
				const std::vector<int> &homes)
{
	const Layout &L = source;
	const DrownedForestOptions o(c.request);
	Map &map = game.map;
	const Torus &t = L.t;
	writeUndermap(map, L.terrain);
	for (int i = 0; i < t.size(); ++i)
		if (L.forest[i])
			map.setResource(i % t.w, i / t.w, WOOD, 1);
	const auto fertility = Fertility::forMap(map, false);
	for (int j = 0; j < int(L.clearings.size()); ++j)
	{
		const auto &g = L.clearings[j];
		const bool home = std::find(homes.begin(), homes.end(), j) != homes.end();
		int wheat = plantContainedPlot(map, t, g.wheat, fertility, WHEAT,
									   (home ? kFarmWheat : 20) + int(scaledCount(40, o.wheat)));
		if (home)
		{
			auto nearby = g.wheat;
			const int from = homeAnchor(t, g);
			std::stable_sort(nearby.begin(), nearby.end(),
							 [&](int a, int b)
							 {
								 return t.dist2(from % t.w, from / t.w, a % t.w, a / t.w) <
										t.dist2(from % t.w, from / t.w, b % t.w, b / t.w);
							 });
			int kit = 0;
			for (int i : nearby)
				if (fertility.at(i % t.w, i / t.w) > 0)
				{
					if (!map.isResource(i % t.w, i / t.w))
					{
						map.setResource(i % t.w, i / t.w, WHEAT, 1);
						++wheat;
					}
					if (++kit == 8)
						break;
				}
		}
		const int wood = plantContainedPlot(map, t, g.wood, fertility, WOOD,
											(home ? kFarmWood : 0) + int(scaledCount(10, o.wood)));
		double growth = 0;
		for (int i : g.wheat)
			growth += double(fertility.at(i % t.w, i / t.w)) / Fertility::kScale;
		c.telemetry.measure(home ? "drowned-forest.home.wheat-growth-potential"
								 : "drowned-forest.meadow.wheat-growth-potential",
							growth, j);
		c.telemetry.measure("drowned-forest.meadow.building-tiles", g.town.size(), j);
		c.telemetry.measure("drowned-forest.meadow.wheat-tiles", wheat, j);
		c.telemetry.measure("drowned-forest.meadow.wood-tiles", wood, j);
		if (home && (wheat < kFarmWheat || wood < kFarmWood))
		{
			c.detail = "A meadow has insufficient renewable crops: wheat=" + std::to_string(wheat) +
					   ", wood=" + std::to_string(wood);
			return false;
		}
		// Preserve complete forward-base footprints and their immediate gathering
		// faces even at maximum mineral and orchard abundance.
		const int radius = std::max(g.radius, 13), gx = g.site % t.w, gy = g.site / t.w;
		// Reservation only needs the grid's complete rectangles; circulation is
		// checked after settlement. Avoid a discarded whole-world flood per meadow.
		const auto townMask = tileMask(t, g.town);
		BuildingArrangement room;
		for (int y = gy - radius + 1; y + 4 <= gy + radius; y += 6)
			for (int x = gx - radius + 1; x + 4 <= gx + radius; x += 6)
			{
				bool fits = true;
				for (int dy = 0; dy < 4 && fits; ++dy)
					for (int dx = 0; dx < 4; ++dx)
						if (!townMask[t.at(x + dx, y + dy)])
						{
							fits = false;
							break;
						}
				if (fits)
					room.footprints.push_back({x, y, x + 4, y + 4});
			}
		std::vector<unsigned char> construction(t.size(), 0);
		const int reserve =
			home ? int(room.footprints.size()) : std::min(3, int(room.footprints.size()));
		for (int k = 0; k < reserve; ++k)
		{
			const auto &box = room.footprints[k];
			for (int y = box.y0 - 1; y <= box.y1; ++y)
				for (int x = box.x0 - 1; x <= box.x1; ++x)
					construction[t.at(x, y)] = 1;
		}
		std::vector<int> edge;
		for (int i : g.town)
			if (!construction[i] && t.dist2(gx, gy, i % t.w, i / t.w) >
										std::max(3, g.radius - 4) * std::max(3, g.radius - 4))
				edge.push_back(i);
		// Fractional rounding across meadows lets every 25% step affect the
		// population of deposits without raising the default mineral supply.
		const int ambientStone =
			(2 * o.stone + int(c.bounded("drowned-stone-rounding", 100))) / 100;
		const int stone = plantContainedPlot(map, t, edge, fertility, STONE,
											 (home ? 2 : 0) + ambientStone, false);
		c.telemetry.measure("drowned-forest.meadow.stone-tiles", stone, j);
		if (!home)
			plantContainedPlot(map, t, edge, fertility, CHERRY + j % 3,
							   int(scaledCount(4, o.fruit)), false);
	}
	seedAlgae(map, c, t, "drowned-algae", o.algae, AlgaeBand::anyWater(40));
	for (int k = 0; k < c.request.nbTeams; ++k)
		game.addTeam();
	for (int k = 0; k < c.request.nbTeams; ++k)
	{
		const auto &g = L.clearings[homes[k]];
		if (!placeSettlement(game, c, k, tileMask(t, g.town),
							 MapGeneratorPoint(homeAnchor(t, g) % t.w, homeAnchor(t, g) / t.w),
							 "drowned-settlements"))
			return false;
	}
	auto walking = groundUnitTiles(map);
	const auto permanent = sketchWalk(L);
	for (int i = 0; i < t.size(); ++i)
		walking[i] = walking[i] && permanent[i];
	const auto workers = unitTilesByTeam(map, c.request.nbTeams);
	for (int k = 0; k < c.request.nbTeams; ++k)
	{
		const auto reached = reachFrom(t, workers[k], walking, 32);
		std::vector<int> shore;
		std::vector<unsigned char> seen(t.size(), 0);
		int placed = 0;
		for (int tile : reached.tiles)
			for (const auto &d : kCardinalSteps)
			{
				const int i = t.at(tile % t.w + d[0], tile / t.w + d[1]);
				if (!seen[i] && map.isWater(i % t.w, i / t.w))
				{
					seen[i] = 1;
					shore.push_back(i);
					placed += map.getResource(i % t.w, i / t.w).type == ALGA;
				}
			}
		for (int i : shore)
		{
			if (placed >= 3)
				break;
			if (!map.isResource(i % t.w, i / t.w))
			{
				map.setResource(i % t.w, i / t.w, ALGA, 1);
				++placed;
			}
		}
		if (placed < 3)
		{
			c.detail = "A meadow has no reachable algae shore.";
			return false;
		}
	}
	return true;
}

// A constructive certificate: two three-tile-wide routes may share the meadow,
// but not their footprints outside it. Try both greedy route orders.
bool independentExits(const Torus &t, const std::vector<unsigned char> &permanent,
					  const Clearing &g)
{
	if (g.exits.size() != 2)
		return false;
	const auto town = tileMask(t, g.town);
	for (int order = 0; order < 2; ++order)
	{
		auto available = permanent;
		bool success = true;
		for (int leg = 0; leg < 2; ++leg)
		{
			const auto wide = erode(t, available, 1);
			auto roots = town;
			for (int i = 0; i < t.size(); ++i)
				roots[i] = roots[i] && wide[i];
			// Only goals within sixty steps can qualify; their predecessor chains
			// are identical without flooding the rest of the island network.
			const auto d = floodFrom(t, roots, wide, 60).steps;
			const int shore = g.exits[(order + leg) % 2];
			int goal = -1;
			for (int dy = -2; dy <= 2; ++dy)
				for (int dx = -2; dx <= 2; ++dx)
				{
					const int i = t.at(shore % t.w + dx, shore / t.w + dy);
					if (wide[i] && d[i] >= 1 && d[i] <= 60 && (goal < 0 || d[i] < d[goal]))
						goal = i;
				}
			if (goal < 0)
			{
				success = false;
				break;
			}
			std::vector<unsigned char> path(t.size(), 0);
			while (d[goal] > 0)
			{
				path[goal] = 1;
				int next = -1;
				for (int dy = -1; dy <= 1; ++dy)
					for (int dx = -1; dx <= 1; ++dx)
					{
						const int i = t.at(goal % t.w + dx, goal / t.w + dy);
						if (wide[i] && d[i] == d[goal] - 1 && (next < 0 || i < next))
							next = i;
					}
				if (next < 0)
				{
					success = false;
					break;
				}
				goal = next;
			}
			if (!success)
				break;
			const auto occupied = dilate(t, path, 1);
			for (int i = 0; i < t.size(); ++i)
				if (occupied[i] && !town[i])
					available[i] = 0;
		}
		if (success)
			return true;
	}
	return false;
}

// A worker may leave a hypothetical building site before construction. The
// existing swarm's reachable gathering faces anchor future city circulation.
std::vector<int> swarmEntrances(const Game &game, int team, const Torus &t,
								const std::vector<unsigned char> &walk,
								const std::vector<int> &reached)
{
	std::vector<int> result;
	for (int slot = 0; slot < Building::MAX_COUNT; ++slot)
	{
		const auto *b = game.teams[team]->myBuildings[slot];
		if (!b || b->type->type != "swarm")
			continue;
		const auto add = [&](int x, int y)
		{
			const int i = t.at(x, y);
			if (walk[i] && reached[i] >= 0)
				result.push_back(i);
		};
		for (int x = b->posX; x < b->posX + b->type->width; ++x)
		{
			add(x, b->posY - 1);
			add(x, b->posY + b->type->height);
		}
		for (int y = b->posY; y < b->posY + b->type->height; ++y)
		{
			add(b->posX - 1, y);
			add(b->posX + b->type->width, y);
		}
		break;
	}
	return result;
}

std::string checkWorld(const Game &game, const GenerationContext &c, const Layout &L)
{
	const Map &map = game.map;
	const Torus &t = L.t;
	auto walk = groundUnitTiles(map);
	const auto permanent = sketchWalk(L);
	for (int i = 0; i < t.size(); ++i)
		walk[i] = walk[i] && permanent[i];
	for (const auto &g : L.clearings)
		for (const auto *field : {&g.wheat, &g.wood})
			for (int i : *field)
				walk[i] = 0;
	const auto units = unitTilesByTeam(map, c.request.nbTeams);
	const auto reached = stepsFrom(t, tileMask(t, units[0]), walk);
	if (firstColonyCutOff(reached, units) >= 0)
		return "A colony is disconnected from the permanent routes.";
	for (const auto &g : L.clearings)
	{
		if (std::none_of(g.town.begin(), g.town.end(), [&](int i) { return reached[i] >= 0; }))
			return "A neutral meadow is disconnected.";
		for (int i : g.town)
			if (map.getResource(i % t.w, i / t.w).type == WHEAT ||
				map.getResource(i % t.w, i / t.w).type == WOOD)
				return "A crop has invaded a building clearing at " + std::to_string(g.site) +
					   " tile " + std::to_string(i) + " type " +
					   std::to_string(map.getResource(i % t.w, i / t.w).type);
	}
	const auto homes = occupiedClearings(game, c, L);
	const auto building = potentialBuildingTiles(map);
	std::vector<unsigned char> forward(L.clearings.size(), 0);
	for (int j = 0; j < int(L.clearings.size()); ++j)
	{
		const auto &g = L.clearings[j];
		int food = 0;
		for (int i : g.wheat)
			food += map.getResource(i % t.w, i / t.w).type == WHEAT;
		if (food < 20 || g.town.size() < 100)
			continue;
		auto eligible = tileMask(t, g.town);
		for (int i : g.town)
			eligible[i] = building[i];
		const int x = g.site % t.w, y = g.site / t.w, radius = std::max(g.radius, 13);
		const BuildingGrid grid{
			{x - radius, y - radius, x + radius + 1, y + radius + 1}, 4, 4, 2, 1};
		auto room = nearbyBuildingGrid(t, eligible, walk, grid, units[0], reached);
		if (!room.failure.empty() && j == homes[0])
			room = arrangeBuildingGrid(t, eligible, walk, grid,
									   swarmEntrances(game, 0, t, walk, reached));
		forward[j] = room.failure.empty() && room.footprints.size() >= 3;
	}
	std::vector<std::vector<int>> distances;
	for (int k = 0; k < c.request.nbTeams; ++k)
	{
		distances.push_back(stepsFrom(t, tileMask(t, units[k]), walk));
		const auto &g = L.clearings[homes[k]];
		int grain = 0, timber = 0, algae = 0;
		for (int i : g.wheat)
			grain += map.getResource(i % t.w, i / t.w).type == WHEAT;
		for (int i : g.wood)
			timber += map.getResource(i % t.w, i / t.w).type == WOOD;
		if (grain < kFarmWheat || timber < kFarmWood)
			return "A colony lost its guaranteed renewable crops.";
		std::vector<unsigned char> seen(t.size(), 0);
		for (int i = 0; i < t.size(); ++i)
			if (distances.back()[i] >= 0 && distances.back()[i] <= 32)
				for (const auto &d : kCardinalSteps)
				{
					const int q = t.at(i % t.w + d[0], i / t.w + d[1]);
					if (!seen[q] && map.getResource(q % t.w, q / t.w).type == ALGA)
					{
						seen[q] = 1;
						++algae;
					}
				}
		if (algae < 3)
			return "A colony lost its reachable algae supply.";
		auto eligible = building;
		const auto town = tileMask(t, g.town);
		for (int i = 0; i < t.size(); ++i)
			eligible[i] = eligible[i] && town[i];
		const int x = g.site % t.w, y = g.site / t.w;
		const BuildingGrid grid{{x - 14, y - 15, x + 15, y + 16}, 4, 4, 2, 1};
		auto arrangement = nearbyBuildingGrid(t, eligible, walk, grid, units[k], distances.back());
		if (!arrangement.failure.empty())
			arrangement = arrangeBuildingGrid(t, eligible, walk, grid,
											  swarmEntrances(game, k, t, walk, distances.back()));
		if (!arrangement.failure.empty() || arrangement.footprints.size() < 6)
			return "A home cannot fit six buildings with access lanes.";
		for (int exit : g.exits)
			if (distances.back()[exit] < 0)
				return "A meadow exit is blocked.";
		auto permanent = walk;
		for (int i = 0; i < t.size(); ++i)
			if (map.isGrass(i % t.w, i / t.w) && !town[i])
				permanent[i] = 0;
		if (!independentExits(t, permanent, g))
			return "A meadow lacks two independent three-wide exits.";
	}
	if (c.request.nbTeams > 1)
		for (int k = 0; k < c.request.nbTeams; ++k)
		{
			bool shared = false;
			for (int j = 0; j < int(L.clearings.size()); ++j)
			{
				if (std::find(homes.begin(), homes.end(), j) != homes.end() || !forward[j])
					continue;
				const int a = meadowDistance(distances[k], L.clearings[j]);
				if (a < 0 || a > 70)
					continue;
				for (int rival = 0; rival < c.request.nbTeams; ++rival)
					if (rival != k)
					{
						const int b = meadowDistance(distances[rival], L.clearings[j]);
						if (b >= 0 && b <= 70 &&
							std::abs(a - b) <= std::max(12, std::max(a, b) / 5))
							shared = true;
					}
			}
			if (!shared)
			{
				return "A colony has no usable shared meadow within seventy walking steps.";
			}
		}
	// Label every intended pure-grass component, including the forest. A field cannot
	// join a town, its woodlot, or the surrounding woods even through a diagonal seam.
	std::vector<int> plots(t.size(), -1);
	for (int i = 0; i < t.size(); ++i)
		if (map.isGrass(i % t.w, i / t.w))
			plots[i] = 0;
	for (int k = 0; k < int(L.clearings.size()); ++k)
	{
		for (int i : L.clearings[k].town)
			plots[i] = 3 * k + 1;
		for (int i : L.clearings[k].wheat)
			plots[i] = 3 * k + 2;
		for (int i : L.clearings[k].wood)
			plots[i] = 3 * k + 3;
	}
	if (auto error = containedPlotsMismatch(map, t, plots); !error.empty())
		return error;
	// Empty forest ground is passable today. Only permanent-route guarantees may
	// conservatively assume that grass will eventually fill with growing wood.
	const auto shortcutWalk = groundUnitTiles(map);
	std::vector<std::vector<int>> shortcutDistances;
	for (const auto &team : units)
		shortcutDistances.push_back(stepsFrom(t, tileMask(t, team), shortcutWalk));
	std::vector<int> useful(c.request.nbTeams, 0);
	for (const auto &s : L.shortcuts)
	{
		for (int i : s.plug)
			if (map.getResource(i % t.w, i / t.w).type != WOOD)
				return "A timber neck is already open.";
		const auto fromA = stepsFrom(t, tileMask(t, {s.a}), shortcutWalk);
		const int before = fromA[s.b];
		auto cut = shortcutWalk;
		for (int i : s.plug)
			cut[i] = 1;
		const int bound = std::max(0, std::min(before - 8, before * 3 / 4));
		const int after = floodFrom(t, tileMask(t, {s.a}), cut, bound).steps[s.b];
		if (before < 0 || after < 0 || before - after < 8 || after * 4 > before * 3)
			return "A timber neck does not shorten the coastal walk.";
		bool relevant = false;
		for (int k = 0; k < c.request.nbTeams; ++k)
			if (!useful[k])
				for (int mouth : {s.a, s.b})
					relevant |=
						shortcutDistances[k][mouth] >= 0 && shortcutDistances[k][mouth] <= 100;
		if (!relevant)
			continue;
		const auto fromB = stepsFrom(t, tileMask(t, {s.b}), shortcutWalk);
		for (int k = 0; k < c.request.nbTeams; ++k)
		{
			if (useful[k])
				continue;
			const int a = shortcutDistances[k][s.a], b = shortcutDistances[k][s.b];
			if ((a < 0 || a > 100) && (b < 0 || b > 100))
				continue;
			for (int j = 0; j < int(L.clearings.size()); ++j)
				if (j != homes[k] && forward[j])
				{
					const auto &target = L.clearings[j];
					const int original = meadowDistance(shortcutDistances[k], target);
					if (original < 0)
						continue;
					const int da = meadowDistance(fromA, target),
							  db = meadowDistance(fromB, target);
					const int opened = std::min(a >= 0 && db >= 0 ? a + after + db : INT_MAX,
												b >= 0 && da >= 0 ? b + after + da : INT_MAX);
					if (opened <= original - 8)
					{
						++useful[k];
						break;
					}
				}
		}
	}
	for (int k = 0; k < c.request.nbTeams; ++k)
		if (!useful[k])
			return "A home has no reachable timber shortcut to another meadow.";
	return "";
}

struct SelectedLayout
{
	Layout landscape;
	std::vector<int> homes;
	std::string failure;
};
SelectedLayout selectLayoutAfresh(GenerationContext &c)
{
	std::map<std::string, int> rejected;
	const auto reportRejections = [&]()
	{
		int i = 0;
		for (const auto &[reason, count] : rejected)
		{
			c.telemetry.choice("drowned-forest.search.rejected-reason", reason, i);
			c.telemetry.measure("drowned-forest.search.rejected-count", count, i++);
		}
	};
	// Fully occupied small maps can need more draws when extra sandbars compete
	// with thick timber cuts. Extend only these dense requests' tails; every earlier
	// successful landscape and its random-stream state remain unchanged.
	const bool compact = c.request.wDec == 7 && c.request.hDec == 7;
	const int area = (1 << c.request.wDec) * (1 << c.request.hDec);
	const bool dense = area <= 65536 && c.request.nbTeams == area / 8192;
	const int landscapeLimit = compact ? 512 : dense ? 256 : 64;
	for (int attempt = 0; attempt < landscapeLimit; ++attempt)
	{
		c.telemetry = GenerationTelemetry(c.telemetry.enabled());
		c.telemetry.measure("drowned-forest.landscape.attempts", attempt + 1);

		c.stage = "drowned forest landscape";
		const Layout L = design(c.request, c);
		if (!L.failure.empty())
		{
			c.detail = L.failure;
			++rejected[c.detail];
			continue;
		}
		std::vector<std::vector<int>> proposals;
		const auto walk = sketchWalk(L);
		// Start with a nearby pair, then spread additional homes by walking distance. Multiple
		// proposals are evaluated after the exact same resource and settlement stages.
		for (int trial = 0; trial < 8; ++trial)
		{
			std::vector<int> candidates = L.homes;
			c.shuffle(candidates.begin(), candidates.end(), "drowned-home-proposals");
			std::vector<int> homes{candidates[0]};
			while (int(homes.size()) < c.request.nbTeams)
			{
				std::vector<int> sources;
				for (int h : homes)
					sources.push_back(L.clearings[h].site);
				const auto distance = stepsFrom(L.t, tileMask(L.t, sources), walk);
				int best = -1, score = -1;
				for (int h : candidates)
					if (std::find(homes.begin(), homes.end(), h) == homes.end())
					{
						const int d = distance[L.clearings[h].site];
						const int merit = d < 30 ? -10000 : 10000 - d;
						if (merit > score)
						{
							score = merit;
							best = h;
						}
					}
				if (best < 0)
					break;
				homes.push_back(best);
			}
			dealStarts(c, homes);
			proposals.push_back(homes);
		}
		c.stage = "drowned forest settlements";
		std::map<std::vector<int>, Layout> settled;
		const auto builder = [&](Game &g, GenerationContext &probe, const std::vector<int> &homes)
		{
			auto [where, inserted] = settled.try_emplace(homes);
			if (inserted)
				where->second =
					settlementLayout(L, homes, DrownedForestOptions(c.request).clearing);
			const auto &layout = where->second;
			if (!buildWorld(g, probe, layout, homes))
				return false;
			probe.detail = checkWorld(g, probe, layout);
			return probe.detail.empty();
		};
		const auto choice = chooseScoredSettlements(
			c, proposals, builder,
			[](const StartQualityReport &q)
			{
				for (const auto &s : q.colonies)
					if (s.wheatDistance < 0 || s.wheatDistance > 12 || s.woodDistance < 0 ||
						s.woodDistance > 32 || s.buildSites < 32)
						return std::string("A meadow lacks access: wheat=") +
							   std::to_string(s.wheatDistance) +
							   ", wood=" + std::to_string(s.woodDistance) +
							   ", room=" + std::to_string(s.buildSites);
				return std::string();
			});
		if (!choice.failure.empty())
		{
			c.detail = choice.failure;
			++rejected[c.detail];
			continue;
		}
		reportRejections();
		return {std::move(settled.at(choice.sites)), choice.sites, {}};
	}
	reportRejections();
	return {{}, {}, "No sustainable Drowned Forest landscape found: " + c.detail};
}

// Selection includes finished settlements, so workers belong in this request cache
// along with terrain controls. Replaying named streams preserves the final builder's
// random state; validation can reuse the deterministic search without repeating it.
SelectedLayout selectedLayout(const GenerationRequest &r, GenerationContext &c)
{
	struct Cache
	{
		bool valid = false;
		GenerationRequest request;
		SelectedLayout layout;
		GenerationTelemetry telemetry;
		std::map<std::string, std::mt19937> streams;
	};
	thread_local Cache cache;
	const auto &was = cache.request;
	if (!cache.valid || was.method != r.method || was.wDec != r.wDec || was.hDec != r.hDec ||
		was.nbTeams != r.nbTeams || was.nbWorkers != r.nbWorkers || was.seed != r.seed ||
		was.options != r.options || (c.telemetry.enabled() && !cache.telemetry.enabled()))
	{
		cache.valid = false;
		GenerationContext fresh(r, c.telemetry.enabled());
		cache.layout = selectLayoutAfresh(fresh);
		cache.telemetry = fresh.telemetry;
		cache.streams = fresh.namedStreams();
		cache.request = r;
		cache.valid = true;
	}
	for (const auto &[name, state] : cache.streams)
		c.stream(name) = state;
	c.telemetry.replay(cache.telemetry);
	return cache.layout;
}
bool generate(Game &game, GenerationContext &c)
{
	c.stage = "drowned forest landscape search";
	const auto selected = selectedLayout(c.request, c);
	if (!selected.failure.empty())
	{
		c.detail = selected.failure;
		return false;
	}
	c.stage = "drowned forest settlements";
	if (!buildWorld(game, c, selected.landscape, selected.homes))
		return false;
	// GenerationService invokes validateWorld on this final build.
	return true;
}

std::string validateWorld(const Game &game, const GenerationContext &c)
{
	GenerationContext replay(c.request);
	const auto selected = selectedLayout(c.request, replay);
	if (!selected.failure.empty())
		return selected.failure;
	const auto &source = selected.landscape;
	const auto &L = source;
	if (!L.failure.empty())
		return L.failure;
	for (int i = 0; i < L.t.size(); ++i)
		if (game.map.getUMTerrain(i % L.t.w, i / L.t.w) != L.terrain[i])
			return "The drowned landscape changed after design.";
	return checkWorld(game, c, L);
}
} // namespace
DrownedForestOptions::DrownedForestOptions(const GenerationRequest &r)
	: connections(r.option("sandbar-connections")), neck(r.option("wooded-neck-thickness")),
	  clearing(r.option("neutral-clearing-size")), wheat(r.option("wheat-amount")),
	  wood(r.option("wood-amount")), stone(r.option("stone-amount")),
	  algae(r.option("algae-amount")), fruit(r.option("fruit-amount"))
{
}
GeneratorDefinition drownedForestDefinition()
{
	return {
		"drowned-forest",
		67,
		"Drowned Forest",
		1,
		false,
		{{"sandbar-connections", "Sandbar connections", 0, 100, 10, 30, ControlGroup::Layout},
		 {"wooded-neck-thickness", "Wooded neck thickness", 3, 9, 2, 5, ControlGroup::Terrain},
		 {"neutral-clearing-size", "Neutral clearing size", 12, 24, 2, 18, ControlGroup::Layout},
		 GeneratorControl::percentage("wheat-amount", "Wheat amount"),
		 GeneratorControl::percentage("wood-amount", "Wood amount"),
		 GeneratorControl::percentage("stone-amount", "Stone amount"),
		 GeneratorControl::percentage("algae-amount", "Algae amount"),
		 GeneratorControl::percentage("fruit-amount", "Fruit amount")},
		generate,
		true,
		requestFailure,
		validateWorld,
		{"terrain:natural", "feature:islands", "feature:forest", "feature:lakes",
		 "style:expansion"}};
}
