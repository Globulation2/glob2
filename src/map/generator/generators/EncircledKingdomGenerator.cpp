// SPDX-License-Identifier: GPL-3.0-or-later
#include "EncircledKingdomGenerator.h"
#include "Drawing.h"
#include "DesignCache.h"
#include "Farmland.h"
#include "Game.h"
#include "GenerationContext.h"
#include "Growth.h"
#include "LatticeNoise.h"
#include "Morphology.h"
#include "Pipeline.h"
#include "Planting.h"
#include "Resources.h"
#include "Room.h"
#include "Sketch.h"
#include "Walls.h"
#include <algorithm>
#include <cmath>
#include <numeric>
using namespace MapGeneration;

// One agricultural kingdom, several invading towns. Colony zero is deliberately never dealt:
// its connected heartland is richer than any single outer town, not than the whole coalition.
// Alliances belong to the lobby. More opponents means a harder siege, not a larger starting army.
// Ramparts are eternal stone deposits; wide gates and two unwalled water fronts admit attacks.
// All renewable crops live in visible sand-contained gardens, leaving roads and building courts
// usable after growth. No simulation rules, team settings or growth flags are changed here.
namespace
{
constexpr int kTownRadius = 27;
struct Garden
{
	RegionBounds bounds;
	int resource, owner;
	bool starter;
	std::vector<int> tiles;
};
struct Layout
{
	Torus t{1, 1};
	TerrainSketch terrain;
	std::vector<unsigned char> wall, roads, gates, inside, reserved;
	std::vector<int> homeOf, plotOf;
	std::vector<std::vector<unsigned char>> portMasks, gateMasks;
	std::vector<ShapePoint> homes, polygon, fronts, ports;
	std::vector<Garden> gardens;
	std::vector<double> edgeStart;
	double perimeter = 0;
	int plan = 0, townPlan = 0;
	std::string failure;
};
std::string validateRequest(const GenerationRequest &r)
{
	const int w = 1 << r.wDec, h = 1 << r.hDec;
	if (r.nbTeams < 3 || r.nbTeams > 12)
		return "Encircled Kingdom supports 3 to 12 colonies; colony zero holds the heartland.";
	if (std::min(w, h) < 256 || std::max(w, h) > 2 * std::min(w, h))
		return "Encircled Kingdom needs sides of at least 256 tiles and an aspect ratio at most "
			   "2:1.";
	if (r.nbTeams >= 7 && w * h < 131072)
		return "Seven or more colonies need 256x512, 512x256 or 512x512 tiles.";
	return "";
}
// Point and outward normal at a distance along a clockwise polygon in screen coordinates.
std::pair<ShapePoint, ShapePoint> boundaryAt(const Layout &L, double along)
{
	along = std::fmod(along + L.perimeter, L.perimeter);
	for (size_t j = 0; j < L.polygon.size(); ++j)
	{
		const auto a = L.polygon[j], b = L.polygon[(j + 1) % L.polygon.size()];
		const double length = L.edgeStart[j + 1] - L.edgeStart[j];
		if (along <= L.edgeStart[j + 1])
		{
			const double f = (along - L.edgeStart[j]) / length;
			return {{a.x + f * (b.x - a.x), a.y + f * (b.y - a.y)},
					{(b.y - a.y) / length, -(b.x - a.x) / length}};
		}
	}
	return {};
}
double arcDistance(double a, double b, double length)
{
	const double d = std::abs(a - b);
	return std::min(d, length - d);
}
// Returns signed distance and arc position of the nearest polygon edge.
std::pair<double, double> boundaryDistance(const Layout &L, double x, double y)
{
	bool inside = false;
	double best = 1e30, arc = 0;
	for (size_t j = 0; j < L.polygon.size(); ++j)
	{
		const auto a = L.polygon[j], b = L.polygon[(j + 1) % L.polygon.size()];
		if ((a.y > y) != (b.y > y) && x < a.x + (b.x - a.x) * (y - a.y) / (b.y - a.y))
			inside = !inside;
		const double dx = b.x - a.x, dy = b.y - a.y;
		const double f =
			std::clamp(((x - a.x) * dx + (y - a.y) * dy) / (dx * dx + dy * dy), 0.0, 1.0);
		const double d = std::hypot(x - a.x - f * dx, y - a.y - f * dy);
		if (d < best)
		{
			best = d;
			arc = L.edgeStart[j] + f * (L.edgeStart[j + 1] - L.edgeStart[j]);
		}
	}
	return {inside ? -best : best, arc};
}
void garden(Layout &L, RegionBounds b, int resource, int owner, bool starter)
{
	Garden g{b, resource, owner, starter, {}};
	const Torus &t = L.t;
	// The ditch lies behind the crop when approached from the town; the crop-facing sand
	// boundary stays fixed, so widening farmland never pushes food into the building court.
	for (int y = b.y0 - 1; y <= b.y1 + 1; ++y)
		for (int x = b.x0 - 1; x <= b.x1 + 1; ++x)
		{
			const int i = t.at(x, y);
			L.reserved[i] = 1;
			L.terrain[i] = x <= b.x0 || x >= b.x1 || y <= b.y0 || y >= b.y1 ? SAND
						   : x >= b.x1 - 6                                  ? WATER
																			: GRASS;
		}
	L.gardens.push_back(std::move(g));
}
void town(Layout &L, ShapePoint p, int team)
{
	const int cx = int(p.x), cy = int(p.y), r = kTownRadius;
	for (int y = -r; y <= r; ++y)
		for (int x = -r; x <= r; ++x)
		{
			const int i = L.t.at(cx + x, cy + y);
			L.terrain[i] = GRASS;
			L.reserved[i] = 1;
			L.wall[i] = 0;
			L.roads[i] = std::abs(y) <= 1;
			if (L.roads[i])
				L.terrain[i] = SAND;
			L.homeOf[i] = team;
			// Two opposing, wide entrances, with extra side exits so an outer town does not
			// obstruct the circumferential road. The capital is a town within the great enceinte.
			if (team && (std::abs(x) >= r - 1 || std::abs(y) >= r - 1) && std::abs(x) > 6 &&
				std::abs(y) > 6)
				L.wall[i] = 1;
		}
	garden(L, {cx + 6, cy - 24, cx + 24, cy - 3}, WHEAT, team, true);
	if (L.townPlan == 0)
		garden(L, {cx + 6, cy + 3, cx + 24, cy + 24}, WOOD, team, true);
	else if (L.townPlan == 1)
		garden(L, {cx - 24, cy + 3, cx - 6, cy + 24}, WOOD, team, true);
	else
		garden(L, {cx - 24, cy - 24, cx - 10, cy - 3}, WOOD, team, true);
	if (team == 0)
	{
		if (L.townPlan == 0)
			garden(L, {cx - 24, cy - 24, cx - 6, cy - 3}, WHEAT, 0, true);
		else
			garden(L, {cx + 6, cy + 3, cx + 24, cy + 24}, WHEAT, 0, true);
	}
}
Layout buildDesign(const GenerationRequest &r, GenerationContext &c)
{
	Layout L;
	L.failure = validateRequest(r);
	if (!L.failure.empty())
		return L;
	const EncircledKingdomOptions o(r);
	L.t = {1 << r.wDec, 1 << r.hDec};
	const Torus &t = L.t;
	L.terrain.assign(t.size(), GRASS);
	L.wall.assign(t.size(), 0);
	L.roads.assign(t.size(), 0);
	L.gates.assign(t.size(), 0);
	L.inside.assign(t.size(), 0);
	L.reserved.assign(t.size(), 0);
	L.homeOf.assign(t.size(), -1);
	L.plotOf.assign(t.size(), -1);
	L.plan = o.plan ? o.plan - 1 : int(c.bounded("kingdom-plan", 3));
	L.townPlan = c.bounded("kingdom-towns", 3);
	const double cx = t.w / 2, cy = t.h / 2;
	const double rx = (L.plan == 1 ? 0.29 : 0.25) * t.w, ry = (L.plan == 0   ? 0.235
															   : L.plan == 1 ? 0.29
																			 : 0.25) *
															  t.h;
	std::vector<ShapePoint> outline;
	if (L.plan == 0)
		outline = {{-.8, -1}, {.8, -1}, {1, -.65}, {1, .65},
				   {.8, 1},   {-.8, 1}, {-1, .65}, {-1, -.65}};
	else if (L.plan == 1)
		outline = {{-.8, -1},  {-.55, -.85}, {.55, -.85},  {.8, -1},   {1, -.8},    {.85, -.55},
				   {.85, .55}, {1, .8},      {.8, 1},      {.55, .85}, {-.55, .85}, {-.8, 1},
				   {-1, .8},   {-.85, .55},  {-.85, -.55}, {-1, -.8}};
	else
		outline = {{-.8, -1}, {-.25, -1}, {0, -.8}, {.25, -1}, {.8, -1}, {1, -.65}, {1, .65},
				   {.8, 1},   {.25, 1},   {0, .8},  {-.25, 1}, {-.8, 1}, {-1, .65}, {-1, -.65}};
	for (auto p : outline)
		L.polygon.push_back({cx + p.x * rx, cy + p.y * ry});
	L.edgeStart.push_back(0);
	for (size_t j = 0; j < L.polygon.size(); ++j)
	{
		auto a = L.polygon[j], b = L.polygon[(j + 1) % L.polygon.size()];
		L.perimeter += std::hypot(a.x - b.x, a.y - b.y);
		L.edgeStart.push_back(L.perimeter);
	}
	const int fronts = 3 + (r.nbTeams - 3) / 3;
	const double phase = (0.15 + c.bounded("kingdom-fronts", 700) / 1000.0) * L.perimeter / fronts;
	std::vector<double> gates, ports;
	for (int k = 0; k < fronts; ++k)
	{
		const double desired = std::fmod(phase + k * L.perimeter / fronts, L.perimeter);
		double chosen = -1, cost = 1e30;
		for (size_t e = 0; e < L.polygon.size(); ++e)
			for (double a = L.edgeStart[e] + 15; a < L.edgeStart[e + 1] - 15; a += 1)
			{
				if (std::any_of(gates.begin(), gates.end(),
								[&](double g) { return arcDistance(a, g, L.perimeter) < 34; }))
					continue;
				const double d = arcDistance(a, desired, L.perimeter);
				if (d < cost)
				{
					cost = d;
					chosen = a;
				}
			}
		if (chosen < 0)
		{
			L.failure = "The fortress cannot fit its separated gates.";
			return L;
		}
		gates.push_back(chosen);
		const auto [p, n] = boundaryAt(L, chosen);
		L.fronts.push_back(p);
		L.gateMasks.emplace_back(t.size(), 0);
		strokePath(L.gateMasks.back(), t,
				   {{p.x - n.x * 4, p.y - n.y * 4, o.gateWidth / 2.0},
					{p.x + n.x * 14, p.y + n.y * 14, o.gateWidth / 2.0}});
	}
	for (const auto &gate : L.gateMasks)
		for (int i = 0; i < t.size(); ++i)
			L.gates[i] |= gate[i];
	// Put water landings on two well-separated stretches, away from every land gate.
	for (int k = 0; k < 2; ++k)
	{
		double chosen = 0, best = -1;
		for (double a = 0; a < L.perimeter; a += 1)
		{
			double separation = L.perimeter;
			for (double g : gates)
				separation = std::min(separation, arcDistance(a, g, L.perimeter));
			for (double p : ports)
				separation = std::min(separation, arcDistance(a, p, L.perimeter));
			if (separation > best)
			{
				chosen = a;
				best = separation;
			}
		}
		ports.push_back(chosen);
		L.ports.push_back(boundaryAt(L, chosen).first);
	}
	int gateArea = 0;
	L.portMasks.assign(2, std::vector<unsigned char>(t.size(), 0));
	for (int y = 0; y < t.h; ++y)
		for (int x = 0; x < t.w; ++x)
		{
			const int i = t.at(x, y);
			const auto [d, arc] = boundaryDistance(L, x, y);
			L.inside[i] = d < 0;
			for (int p = 0; p < 2; ++p)
				if (std::abs(d) < 18 && arcDistance(arc, ports[p], L.perimeter) < 16)
					L.portMasks[p][i] = 1;
			const bool port = std::any_of(ports.begin(), ports.end(), [&](double p)
										  { return arcDistance(arc, p, L.perimeter) < 12; });
			if (d > -5 && d < 16)
				L.reserved[i] = 1;
			if ((d >= 5 && d <= 11) || (port && d > -7 && d <= 11))
				L.terrain[i] = WATER;
			if (std::abs(d) < 2 && !port && !L.gates[i])
				L.wall[i] = 1;
			if (L.gates[i])
			{
				++gateArea;
				L.terrain[i] = SAND;
				L.roads[i] = 1;
			}
		}
	// Find complete outer-town footprints in the dry exterior. Opposite map edges touch:
	// testing toroidal separation prevents two towns colliding across the seam.
	std::vector<unsigned char> exterior(t.size());
	for (int i = 0; i < t.size(); ++i)
		exterior[i] = !L.inside[i] && !L.reserved[i];
	const auto room = clearance(t, exterior);
	std::vector<int> candidatesHomes;
	for (int y = 0; y < t.h; y += 4)
		for (int x = 0; x < t.w; x += 4)
			if (room[t.at(x, y)] >= kTownRadius + 3)
				candidatesHomes.push_back(t.at(x, y));
	c.shuffle(candidatesHomes.begin(), candidatesHomes.end(), "kingdom-site-ties");
	std::vector<int> frontOrder(fronts);
	std::iota(frontOrder.begin(), frontOrder.end(), 0);
	std::vector<int> sites;
	for (int attempt = 0; attempt < 16 && int(sites.size()) < r.nbTeams - 1; ++attempt)
	{
		sites.clear();
		if (candidatesHomes.empty())
			break;
		c.shuffle(frontOrder.begin(), frontOrder.end(), "kingdom-sites");
		while (int(sites.size()) < r.nbTeams - 1)
		{
			int best = -1, score = INT_MIN;
			const auto target = L.fronts[frontOrder[sites.size() % fronts]];
			for (int i : candidatesHomes)
			{
				int distance = 3600;
				bool fits = true;
				for (int p : sites)
				{
					if (t.chebyshev(i % t.w, i / t.w, p % t.w, p / t.w) < 60)
					{
						fits = false;
						break;
					}
					distance = std::min(distance, t.dist2(i % t.w, i / t.w, p % t.w, p / t.w));
				}
				// Deal balanced groups to nearby fronts before furnishing the country. This
				// keeps a remote toroidal corner from becoming a needlessly long opening.
				const int approach = t.dist2(i % t.w, i / t.w, int(target.x), int(target.y));
				const bool visible = std::min({i % t.w, t.w - 1 - i % t.w, i / t.w,
											   t.h - 1 - i / t.w}) >= kTownRadius + 2;
				const int preference = distance / 2 - approach + (visible ? 1600 : 0);
				if (fits && preference > score)
				{
					score = preference;
					best = i;
				}
			}
			if (best < 0)
				break;
			sites.push_back(best);
		}
	}
	if (int(sites.size()) < r.nbTeams - 1)
	{
		L.failure = "The exterior cannot fit every complete town.";
		return L;
	}
	L.homes.push_back({cx, cy});
	for (int i : sites)
		L.homes.push_back({double(i % t.w), double(i / t.w)});
	c.shuffle(L.homes.begin() + 1, L.homes.end(), "kingdom-deal");
	// Circumferential road and radial approaches. Gardens are placed afterwards, outside roads.
	strokePath(L.roads, t,
			   {{16, 16, 2}, {t.w - 16.0, 16, 2}, {t.w - 16.0, t.h - 16.0, 2}, {16, t.h - 16.0, 2}},
			   1, true);
	for (int k = 0; k < fronts; ++k)
	{
		const auto [p, n] = boundaryAt(L, gates[k]);
		const ShapePoint out{p.x + n.x * 19, p.y + n.y * 19};
		strokePath(L.roads, t, {{cx, cy, 2}, {p.x, p.y, 2}, {out.x, out.y, 2}}, 1);
		// Attach to the closest side of the exterior road without crossing the enceinte again.
		ShapePoint end = out;
		if (std::abs(n.x) >= std::abs(n.y))
			end.x = n.x > 0 ? t.w - 16 : 16;
		else
			end.y = n.y > 0 ? t.h - 16 : 16;
		strokePath(L.roads, t, {{out.x, out.y, 2}, {end.x, end.y, 2}}, 1);
	}
	// Short exterior lanes connect each town to its two closest fronts. They follow the
	// dry ground around the moat; forests may crowd the lanes but cannot close them.
	std::vector<unsigned char> approachGround(t.size(), 0), lanes(t.size(), 0);
	for (int i = 0; i < t.size(); ++i)
		approachGround[i] = !L.inside[i] && !L.reserved[i] && L.terrain[i] != WATER;
	std::vector<Flood> approaches;
	for (int f = 0; f < fronts; ++f)
	{
		const auto [p, n] = boundaryAt(L, gates[f]);
		const int seed = seedNear(t, int(p.x + n.x * 20), int(p.y + n.y * 20), 32,
								  [&](int i) { return approachGround[i]; });
		if (seed < 0)
		{
			L.failure = "A gate cannot reach the dry exterior.";
			return L;
		}
		approaches.push_back(floodFrom(t, tileMask(t, {seed}), approachGround));
	}
	for (size_t k = 1; k < L.homes.size(); ++k)
	{
		const int start = t.at(int(L.homes[k].x), int(L.homes[k].y));
		std::vector<int> order(fronts);
		std::iota(order.begin(), order.end(), 0);
		std::stable_sort(order.begin(), order.end(), [&](int a, int b)
						 { return approaches[a].steps[start] < approaches[b].steps[start]; });
		for (int j = 0; j < std::min(2, fronts); ++j)
		{
			const auto &steps = approaches[order[j]].steps;
			int i = start;
			while (steps[i] > 0)
			{
				lanes[i] = 1;
				int next = -1;
				for (int dy = -1; dy <= 1 && next < 0; ++dy)
					for (int dx = -1; dx <= 1; ++dx)
					{
						int candidate = t.at(i % t.w + dx, i / t.w + dy);
						if (steps[candidate] == steps[i] - 1)
						{
							next = candidate;
							break;
						}
					}
				if (next < 0)
					break;
				i = next;
			}
			lanes[i] = 1;
		}
	}
	lanes = dilate(t, lanes, 2);
	for (int i = 0; i < t.size(); ++i)
		if (lanes[i] && approachGround[i])
			L.roads[i] = 1;
	// The paired design divides its districts with a central boulevard; the bastioned design
	// uses a cross; the elongated design has a single long market street.
	std::vector<unsigned char> streets(t.size(), 0);
	strokePath(streets, t, {{cx - rx + 9, cy, 1}, {cx + rx - 9, cy, 1}});
	if (L.plan != 0)
		strokePath(streets, t, {{cx, cy - ry + 9, 1}, {cx, cy + ry - 9, 1}});
	for (int i = 0; i < t.size(); ++i)
		if (streets[i] && L.inside[i] && !L.reserved[i])
			L.roads[i] = 1;
	for (int i = 0; i < t.size(); ++i)
		if (L.roads[i])
		{
			if (L.wall[i] || (L.terrain[i] == WATER && !L.gates[i]))
				L.roads[i] = 0;
			else
				L.terrain[i] = SAND;
		}
	for (int k = 0; k < r.nbTeams; ++k)
		town(L, L.homes[k], k);
	// An encircling street connects every approach to the town's east-west street without
	// drawing roads through irrigated gardens or the opening building court.
	for (auto p : L.homes)
	{
		strokePath(L.roads, t,
				   {{p.x - 29, p.y - 29, 1},
					{p.x + 29, p.y - 29, 1},
					{p.x + 29, p.y + 29, 1},
					{p.x - 29, p.y + 29, 1}},
				   1, true);
		strokePath(L.roads, t, {{p.x - 29, p.y, 1}, {p.x + 29, p.y, 1}});
	}
	for (int i = 0; i < t.size(); ++i)
		if (L.roads[i])
		{
			if (L.wall[i] || (L.terrain[i] == WATER && !L.gates[i]))
				L.roads[i] = 0;
			else
				L.terrain[i] = SAND;
		}
	// The capital's starter quarry is within ordinary AI working range; its distant enceinte
	// supplies expansion frontage, not the opening's only stone.
	for (int y = (L.townPlan == 1 ? -20 : 5); y < (L.townPlan == 1 ? -17 : 8); ++y)
		for (int x = -22; x < -19; ++x)
			L.wall[t.at(int(cx) + x, int(cy) + y)] = 1;
	if (L.plan == 2)
	{
		for (int side : {-1, 1})
		{
			const int courtX = int(cx + side * rx * 0.66), courtY = int(cy);
			for (int y = courtY - 6; y <= courtY + 6; ++y)
				for (int x = courtX - 6; x <= courtX + 6; ++x)
					L.reserved[t.at(x, y)] = 1;
		}
	}
	// Pack complete farm districts into the spaces between the streets. Candidate spacing is
	// finer than a district: a road must not delete a whole quadrant merely by crossing a grid.
	struct Candidate
	{
		int x, y;
	};
	std::vector<Candidate> candidates;
	for (int y = 40; y < t.h - 40; y += 4)
		for (int x = 40; x < t.w - 40; x += 4)
		{
			bool fit = true;
			for (int dy = -13; dy <= 13 && fit; ++dy)
				for (int dx = -11; dx <= 11; ++dx)
				{
					int i = t.at(x + dx, y + dy);
					if (!L.inside[i] || L.reserved[i] || L.roads[i] || L.terrain[i] != GRASS)
					{
						fit = false;
						break;
					}
				}
			if (fit)
				candidates.push_back({x, y});
		}
	c.shuffle(candidates.begin(), candidates.end(), "kingdom-districts");
	std::vector<Candidate> districts;
	const int wanted = 6 * t.size() / 65536;
	while (int(districts.size()) < wanted)
	{
		int best = -1, score = -1;
		for (size_t k = 0; k < candidates.size(); ++k)
		{
			const auto p = candidates[k];
			int distance = INT_MAX;
			for (auto q : districts)
			{
				if (std::abs(p.x - q.x) < 26 && std::abs(p.y - q.y) < 30)
				{
					distance = -1;
					break;
				}
				distance = std::min(distance, t.dist2(p.x, p.y, q.x, q.y));
			}
			if (distance > score)
			{
				score = distance;
				best = int(k);
			}
		}
		if (best < 0)
			break;
		districts.push_back(candidates[best]);
	}
	const int length = 18 + 2 * ((o.farmland - 75) / 25);
	for (size_t k = 0; k < districts.size(); ++k)
	{
		const auto p = districts[k];
		garden(L, {p.x - 10, p.y - length / 2, p.x + 10, p.y + length / 2},
			   k % 4 == 3 ? WOOD : WHEAT, 0, false);
	}
	for (int i = 0; i < t.size(); ++i)
		if (L.wall[i])
			L.roads[i] = 0;
	layBeaches(L.terrain, t);
	const auto grass = pureTiles(L.terrain, t, GRASS);
	// A beach at the edge of a waterfront removes its last rampart tile intentionally; the
	// partition validator proves that this only opens onto water, never onto walkable ground.
	for (int i = 0; i < t.size(); ++i)
		if (!grass[i])
			L.wall[i] = 0;
	for (size_t g = 0; g < L.gardens.size(); ++g)
	{
		const auto b = L.gardens[g].bounds;
		for (int y = b.y0; y < b.y1; ++y)
			for (int x = b.x0; x < b.x1; ++x)
			{
				const int i = t.at(x, y);
				if (grass[i])
				{
					L.plotOf[i] = int(g);
					L.gardens[g].tiles.push_back(i);
				}
			}
	}
	c.telemetry.choice("kingdom.plan", L.plan == 0   ? "elongated"
									   : L.plan == 1 ? "bastioned"
													 : "paired-courtyard");
	if (c.telemetry.enabled())
		c.telemetry.choice("kingdom.town-plan", std::to_string(L.townPlan));
	c.telemetry.measure("kingdom.fronts", fronts);
	c.telemetry.measure("kingdom.gates.area", gateArea);
	c.telemetry.measure("kingdom.districts.available", candidates.size());
	c.telemetry.measure("kingdom.districts.placed", L.gardens.size() - 2 * r.nbTeams - 1);
	return L;
}
// Validation reconstructs the same request. Reuse its immutable layout and named-stream
// state, while still checking all resources, routes and colonies against the live world.
Layout design(const GenerationRequest &r, GenerationContext &c)
{
	return cachedDesign(r, c, buildDesign);
}
// Finished-map economics and approaches, shared by generation telemetry and final validation.
std::string assess(const Game &game, const Layout &L, GenerationContext &c)
{
	const Torus &t = L.t;
	const int teams = c.request.nbTeams, attackers = teams - 1, fronts = int(L.fronts.size());
	const auto walk = groundUnitTiles(game.map);
	// Economic potential includes crop ground that harvesting makes traversable. Counting
	// only currently empty crop tiles would perversely report less capacity at more wheat.
	auto economicWalk = walk;
	for (const auto &garden : L.gardens)
		for (int i : garden.tiles)
			if (game.map.isResource(i % t.w, i / t.w))
			{
				const int type = game.map.getResource(i).type;
				if (type == WHEAT || type == WOOD)
					economicWalk[i] = 1;
			}
	const auto units = unitTilesByTeam(game.map, teams);
	const auto growth = cropGrowthField(L.terrain, t);
	const auto anchors = buildAnchors(t, potentialBuildingTiles(game.map));
	std::vector<std::uint64_t> food(teams, 0);
	for (const auto &g : L.gardens)
		if (g.resource == WHEAT)
			for (int i : g.tiles)
				food[g.owner] += growth.at(i % t.w, i / t.w);
	std::vector<int> room(teams, 0);
	for (int k = 0; k < teams; ++k)
	{
		const auto reach = reachFrom(t, units[k], economicWalk, k == 0 ? 512 : 48);
		std::uint64_t local24 = 0, local48 = 0;
		for (size_t j = 0; j < reach.tiles.size(); ++j)
		{
			const int i = reach.tiles[j], d = reach.steps[j];
			if (anchors[i] && (k == 0 ? L.inside[i] : L.homeOf[i] == k))
				++room[k];
			if (d <= 48 && L.plotOf[i] >= 0 && L.gardens[L.plotOf[i]].resource == WHEAT)
			{
				auto f = growth.at(i % t.w, i / t.w);
				local48 += f;
				if (d <= 24)
					local24 += f;
			}
		}
		c.telemetry.measure("kingdom.economy.food-potential", food[k], k);
		c.telemetry.measure("kingdom.economy.connected-build-sites", room[k], k);
		c.telemetry.measure("kingdom.economy.reachable-growth-24", local24, k);
		c.telemetry.measure("kingdom.economy.reachable-growth-48", local48, k);
		if (room[k] < 120)
			return "A town lacks connected construction space.";
	}
	const auto maxFood = *std::max_element(food.begin() + 1, food.end());
	const int maxRoom = *std::max_element(room.begin() + 1, room.end());
	if (food[0] <= maxFood || room[0] <= maxRoom)
		return "The heartland lost its economic advantage.";
	c.telemetry.measure("kingdom.economy.food-ratio",
						double(food[0]) / std::max<std::uint64_t>(1, maxFood));
	c.telemetry.measure("kingdom.economy.room-ratio", double(room[0]) / std::max(1, maxRoom));
	// Capacity-constrained minimum-cost matching, with at most eleven attackers (2^11 states).
	// Distances come from final walking terrain, not angle or straight-line proximity.
	std::vector<std::vector<int>> distance(fronts, std::vector<int>(attackers, 100000));
	for (int f = 0; f < fronts; ++f)
	{
		int seed =
			seedNear(t, int(L.fronts[f].x), int(L.fronts[f].y), 6, [&](int i) { return walk[i]; });
		if (seed < 0)
			return "A front has no walkable gate.";
		const auto flood = floodFrom(t, tileMask(t, {seed}), walk);
		for (int k = 0; k < attackers; ++k)
			for (int i : units[k + 1])
				if (flood.steps[i] >= 0)
					distance[f][k] = std::min(distance[f][k], flood.steps[i]);
	}
	const int count = 1 << attackers, infinity = 1000000;
	std::vector<int> bits(count, 0);
	for (int m = 1; m < count; ++m)
		bits[m] = bits[m >> 1] + (m & 1);
	std::vector<int> cost(count, infinity);
	cost[0] = 0;
	std::vector<std::vector<int>> previous(fronts, std::vector<int>(count, -1));
	const int base = attackers / fronts, ceiling = (attackers + fronts - 1) / fronts;
	for (int f = 0; f < fronts; ++f)
	{
		std::vector<int> next(count, infinity);
		for (int assigned = 0; assigned < count; ++assigned)
			if (cost[assigned] < infinity)
			{
				const int available = (count - 1) ^ assigned;
				for (int sub = available;; sub = (sub - 1) & available)
				{
					if (bits[sub] >= base && bits[sub] <= ceiling)
					{
						int value = cost[assigned];
						for (int k = 0; k < attackers; ++k)
							if (sub & (1 << k))
								value += distance[f][k] <= 240 ? distance[f][k] : infinity;
						if (value < next[assigned | sub])
						{
							next[assigned | sub] = value;
							previous[f][assigned | sub] = assigned;
						}
					}
					if (!sub)
						break;
				}
			}
		cost = std::move(next);
	}
	int mask = count - 1;
	for (int f = fronts - 1; f >= 0; --f)
	{
		const int prior = previous[f][mask];
		if (prior < 0)
			return "A town cannot be assigned a reachable front.";
		const int sub = mask ^ prior;
		for (int k = 0; k < attackers; ++k)
			if (sub & (1 << k))
			{
				c.telemetry.measure("kingdom.front.assignment", f, k + 1);
				c.telemetry.measure("kingdom.front.walk", distance[f][k], k + 1);
				if (distance[f][k] > 240)
					return "An outer town's assigned front is too far away.";
			}
		mask = prior;
	}
	return "";
}

bool generate(Game &game, GenerationContext &c)
{
	c.stage = "kingdom design";
	const Layout L = design(c.request, c);
	if (!L.failure.empty())
	{
		c.detail = L.failure;
		return false;
	}
	const Torus &t = L.t;
	const EncircledKingdomOptions o(c.request);
	writeUndermap(game.map, L.terrain);
	for (int i = 0; i < t.size(); ++i)
		if (L.wall[i])
			game.map.setResource(i % t.w, i / t.w, STONE, 1);
	for (int k = 0; k < c.request.nbTeams; ++k)
		game.addTeam();
	c.stage = "kingdom colonies";
	if (!settleColonies(
			game, c, "kingdom-starts",
			[&](int k)
			{
				auto m = homeGrassMask(game.map, t, L.homeOf, k);
				for (int i = 0; i < t.size(); ++i)
					if (L.plotOf[i] >= 0 || L.wall[i])
						m[i] = 0;
				return m;
			},
			[&](int k) { return MapGeneratorPoint(int(L.homes[k].x) - 3, int(L.homes[k].y) - 7); }))
		return false;
	c.stage = "kingdom gardens";
	const auto fertility = cropGrowthField(L.terrain, t);
	for (size_t j = 0; j < L.gardens.size(); ++j)
	{
		const auto &g = L.gardens[j];
		const int amount = g.resource == WHEAT ? o.wheat : o.wood;
		const int guarantee =
			g.starter ? (g.resource == WHEAT ? 64 : 32) : (g.resource == WHEAT ? 4 : 2);
		const int wanted = guarantee + int(scaledCount(g.starter ? 16 : 48, amount));
		int planted = 0;
		if (g.starter && g.resource == WOOD)
		{
			// Keep a useful timber reserve in the town's immediate working territory.
			// Ranking only by fertility can hide all opening wood at the back of its plot.
			std::vector<int> nearby;
			for (int i : g.tiles)
				if (t.dist2(i % t.w, i / t.w, c.bootX[g.owner], c.bootY[g.owner]) <= 400)
					nearby.push_back(i);
			planted = plantContainedPlot(game.map, t, nearby, fertility, WOOD, 16);
			if (planted < 16)
			{
				c.detail = "A town lacks timber in its immediate working territory.";
				return false;
			}
		}
		planted +=
			plantContainedPlot(game.map, t, g.tiles, fertility, g.resource, wanted - planted);
		c.telemetry.measure("kingdom.garden.planted", planted, int(j));
		if (planted < guarantee)
		{
			c.detail = "A town garden lost its renewable starter supply.";
			return false;
		}
	}
	// Dry hinterland scenery cannot overgrow the approaches. Fertile timber stays in gardens.
	const auto noise = fractalNoise(t.w, t.h, 24, 3, c.stream("kingdom-country"));
	std::vector<int> scenery;
	for (int i = 0; i < t.size(); ++i)
		if (!L.reserved[i] && !L.inside[i] && !L.roads[i] &&
			clearGround(game.map, i % t.w, i / t.w) && fertility.at(i % t.w, i / t.w) == 0)
			scenery.push_back(i);
	std::stable_sort(scenery.begin(), scenery.end(),
					 [&](int a, int b) { return noise[a] > noise[b]; });
	const int woods = std::min(int(scenery.size()), int(scaledCount(scenery.size() / 6, o.wood)));
	const int stones =
		std::min(int(scenery.size()) - woods, int(scaledCount(scenery.size() / 100, o.stone)));
	for (int j = 0; j < woods; ++j)
	{
		int i = scenery[j];
		game.map.setResource(i % t.w, i / t.w, WOOD, 1);
	}
	for (int j = 0; j < stones; ++j)
	{
		int i = scenery[scenery.size() - 1 - j];
		game.map.setResource(i % t.w, i / t.w, STONE, 1);
	}
	c.telemetry.measure("kingdom.scenery.wood", woods);
	c.telemetry.measure("kingdom.scenery.stone", stones);
	// Fruit belongs to town courts, where its finite footprint cannot block a gate or food plot.
	for (int k = 0; k < c.request.nbTeams; ++k)
		for (int fruit = 0; fruit < 3; ++fruit)
			for (int n = 0; n < int(scaledCount(4, o.fruit)); ++n)
			{
				const int x = t.x(int(L.homes[k].x) - 20 + n % 3),
						  y = t.y(int(L.homes[k].y) + (L.townPlan == 1 ? -20 : 8) + fruit * 4 +
								  n / 3);
				if (L.plotOf[t.at(x, y)] < 0 && !L.wall[t.at(x, y)] && clearGround(game.map, x, y))
					game.map.setResource(x, y, CHERRY + fruit, 1);
			}
	seedAlgae(game.map, c, t, "kingdom-algae", o.algae, AlgaeBand::anyWater());
	c.detail = assess(game, L, c);
	return c.detail.empty();
}
std::string validateWorld(const Game &game, const GenerationContext &c)
{
	GenerationContext replay(c.request);
	const Layout L = design(c.request, replay);
	if (auto e = designMismatch(L, game.map, "kingdom"); !e.empty())
		return e;
	const Torus &t = L.t;
	const auto fertility = cropGrowthField(L.terrain, t);
	if (auto e = containedPlotsMismatch(game.map, t, L.plotOf, &fertility); !e.empty())
		return e;
	for (int i = 0; i < t.size(); ++i)
	{
		if (L.wall[i] &&
			(!game.map.isResource(i % t.w, i / t.w) || game.map.getResource(i).type != STONE))
			return "A kingdom rampart is missing.";
		if (L.roads[i] && game.map.isResource(i % t.w, i / t.w))
			return "A kingdom road is obstructed at " + std::to_string(i % t.w) + "," +
				   std::to_string(i / t.w);
	}
	const auto walk = groundUnitTiles(game.map);
	const auto units = unitTilesByTeam(game.map, c.request.nbTeams);
	const auto reach = floodFrom(t, tileMask(t, units[0]), walk);
	if (firstColonyCutOff(reach.steps, units) >= 0)
		return "An outer town cannot reach the heartland.";
	const auto gateTiles = dilate(t, L.gates, 2);
	auto sealed = walk;
	for (int i = 0; i < t.size(); ++i)
		if (gateTiles[i])
			sealed[i] = 0;
	const auto shut = floodFrom(t, tileMask(t, units[0]), sealed);
	for (int k = 1; k < c.request.nbTeams; ++k)
		for (int i : units[k])
			if (shut.steps[i] >= 0)
			{
				for (int a : shut.visited)
					if (L.inside[a])
						for (int dy = -1; dy <= 1; ++dy)
							for (int dx = -1; dx <= 1; ++dx)
							{
								const int b = t.at(a % t.w + dx, a / t.w + dy);
								if (!L.inside[b] && sealed[b])
									return "The kingdom leaks at " + std::to_string(a % t.w) + "," +
										   std::to_string(a / t.w);
							}
				return "The kingdom has an unintended land entrance.";
			}
	// Each drawn gate must work on its own, rather than merely decorate a connected map.
	for (const auto &gate : L.gateMasks)
	{
		auto open = sealed;
		const auto passage = dilate(t, gate, 2);
		for (int i = 0; i < t.size(); ++i)
			if (passage[i])
				open[i] = walk[i];
		const auto front = floodFrom(t, tileMask(t, units[0]), open);
		if (firstColonyCutOff(front.steps, units) >= 0)
			return "A land gate has no independent approach.";
	}
	if (auto e = startingAccessFailure(
			game.map, c.request.nbTeams,
			{{WHEAT, 24, "wheat"}, {WOOD, 24, "wood"}, {STONE, 36, "stone"}}, 32, 24);
		!e.empty())
		return e;
	const auto swim = groundUnitTiles(game.map, true);
	for (int port = 0; port < 2; ++port)
	{
		auto wet = swim;
		const auto other = dilate(t, L.portMasks[1 - port], 2);
		for (int i = 0; i < t.size(); ++i)
			if (gateTiles[i] || other[i])
				wet[i] = 0;
		const auto swimmers = floodFrom(t, tileMask(t, units[0]), wet);
		if (firstColonyCutOff(swimmers.steps, units) >= 0)
			return "A waterfront offers no independent swimming approach.";
	}
	const auto buildable = buildableTiles(game.map);
	for (int k = 0; k < c.request.nbTeams; ++k)
	{
		auto home = homeGrassMask(game.map, t, L.homeOf, k);
		if (buildSites(t, buildable, home) < 120)
			return "A kingdom town has insufficient building room.";
		int closeWood = 0;
		for (const auto &garden : L.gardens)
			if (garden.owner == k && garden.starter && garden.resource == WOOD)
				for (int i : garden.tiles)
					if (game.map.getResource(i).type == WOOD &&
						t.dist2(i % t.w, i / t.w, game.teams[k]->startPosX,
								game.teams[k]->startPosY) <= 400)
						++closeWood;
		if (closeWood < 16)
			return "A town lacks timber in its immediate working territory.";
		for (int type : {WHEAT, WOOD})
		{
			int count = 0;
			for (const auto &g : L.gardens)
				if (g.owner == k && g.starter && g.resource == type)
					for (int i : g.tiles)
						if (game.map.isResource(i % t.w, i / t.w) &&
							game.map.getResource(i).type == type)
							++count;
			if (count < (type == WHEAT ? 64 : 32))
				return "A kingdom town lost its starter crops.";
		}
	}
	if (L.gardens.size() < size_t(2 * c.request.nbTeams + 2))
		return "The heartland has insufficient expansion farmland.";
	return assess(game, L, replay);
}
} // namespace
EncircledKingdomOptions::EncircledKingdomOptions(const GenerationRequest &r)
	: plan(r.option("fortress-plan")), gateWidth(r.option("gate-width")),
	  farmland(r.option("heartland-farmland")), wheat(r.option("wheat-amount")),
	  wood(r.option("wood-amount")), stone(r.option("stone-amount")),
	  algae(r.option("algae-amount")), fruit(r.option("fruit-amount"))
{
}
GeneratorDefinition encircledKingdomDefinition()
{
	return {
		"encircled-kingdom",
		62,
		"Encircled Kingdom",
		1,
		false,
		{GeneratorControl::choice(
			 "fortress-plan", "Fortress plan",
			 {"Automatic", "Elongated enclosure", "Bastioned enclosure", "Paired courtyards"}, 0),
		 {"gate-width", "Gate width", 6, 14, 2, 10, ControlGroup::Layout},
		 {"heartland-farmland", "Heartland farmland", 75, 150, 25, 100, ControlGroup::Layout},
		 GeneratorControl::percentage("wheat-amount", "Wheat amount"),
		 GeneratorControl::percentage("wood-amount", "Wood amount"),
		 GeneratorControl::percentage("stone-amount", "Stone amount"),
		 GeneratorControl::percentage("algae-amount", "Algae amount"),
		 GeneratorControl::percentage("fruit-amount", "Fruit amount")},
		generate,
		true,
		validateRequest,
		validateWorld,
		{"terrain:stronghold", "feature:stone-walls", "feature:lakes", "style:siege",
		 "style:fortified"}};
}
