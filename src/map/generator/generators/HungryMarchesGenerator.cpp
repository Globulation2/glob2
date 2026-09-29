// SPDX-License-Identifier: GPL-3.0-or-later
#include "HungryMarchesGenerator.h"
#include "Game.h"
#include "GenerationContext.h"
#include "DesignCache.h"
#include "Growth.h"
#include "Pipeline.h"
#include "Planting.h"
#include "Sketch.h"
#include "Topology.h"
#include <algorithm>
#include <cmath>
#include <numeric>
using namespace MapGeneration;

// Dry towns live on a finite opening ration. All renewable food lies in shared
// floodplain districts: multiple outer fronts and a fragmented interior.
// Sand caps and crossings keep future crops off the approaches. No growth flags,
// free-form resource guarantees or emergency ponds may bypass that contract.
namespace
{
constexpr double pi = 3.14159265358979323846;
// Natural layouts allow unequal remote journeys while retaining two useful
// alternatives and a tighter near-home bound. This is a design allowance,
// not a guarantee of equal arrival times or win rates.
constexpr int maximumRivalGap = 40;
struct Site
{
	int x, y;
	double angle;
};
struct Pool
{
	int x, y;
	double rx, ry, phase, angle;
	bool central;
};
struct Layout
{
	Torus t{1, 1};
	TerrainSketch terrain;
	std::vector<Site> sites;
	std::vector<Pool> pools;
	std::vector<int> field, component, grainCapacity;
	std::vector<long long> grainPotential;
	std::vector<unsigned char> roads, timber;
	std::vector<int> scenery;
	Fertility::Field fertility;
	std::string failure;
};
std::string validateRequest(const GenerationRequest &r)
{
	if (r.wDec < 7 || r.hDec < 7 || r.wDec > 9 || r.hDec > 9 || r.nbTeams < 2 || r.nbTeams > 12)
		return "The Hungry Marches needs 128 to 512 tiles per side and two to twelve colonies.";
	if (r.nbTeams > ((1 << std::min(r.wDec, r.hDec)) == 128 ? 4 : 12))
		return "This narrow floodplain needs fewer colonies or a wider map.";
	return {};
}
Layout designAfresh(const GenerationRequest &r, GenerationContext &c)
{
	Layout L;
	L.t = Torus(1 << r.wDec, 1 << r.hDec);
	const auto &t = L.t;
	const int n = t.size();
	L.failure = validateRequest(r);
	if (!L.failure.empty())
		return L;
	L.terrain.assign(n, GRASS);
	L.field.assign(n, -1);
	L.roads.assign(n, 0);
	HungryMarchesOptions options(r);
	const bool compact = std::min(t.w, t.h) == 128;
	const double phase = c.bounded("hungry-marches.orientation", 10000) * 2 * pi / 10000;
	const double extra = std::max(0., (t.w + t.h - 512.) / 512.);
	// Leave room in the 96-step central harvest budget for shoreline detours.
	const double rx =
		std::min({t.w * .34, 116., 34. + r.nbTeams * 6. + extra * std::min(22., r.nbTeams * 2.)});
	const double ry =
		std::min({t.h * .34, 116., 34. + r.nbTeams * 6. + extra * std::min(22., r.nbTeams * 2.)});
	const int cx = t.w / 2, cy = t.h / 2;
	const auto bent = [&](double x, double y)
	{
		return Site{int(x + (compact ? 4 : 12) * std::sin((y - cy) / 31. + phase)),
					int(y + (compact ? 3 : 9) * std::sin((x - cx) / 37. - phase)), 0};
	};
	for (int k = 0; k < r.nbTeams; ++k)
	{
		const double a = phase + 2 * pi * k / r.nbTeams;
		const double jitter = (int(c.bounded("hungry-marches.sites", 7)) - 3) * .01;
		auto site = bent(cx + rx * std::cos(a + jitter), cy + ry * std::sin(a + jitter));
		site.angle = a;
		L.sites.push_back(site);
	}
	const int count = r.nbTeams == 2 ? 4 : r.nbTeams;
	const double outerScale = .85 + (80 - options.concentration) * .008;
	const double innerScale = .85 + (options.concentration - 50) * .008;
	for (int k = 0; k < count; ++k)
	{
		const double a = phase + 2 * pi * (k + .5) / count;
		const double variation = .85 + c.bounded("hungry-marches.pool-size", 31) * .01;
		const double reach = .64 + .035 * std::sin(3 * a + phase);
		const auto point = bent(cx + rx * reach * std::cos(a), cy + ry * reach * std::sin(a));
		L.pools.push_back(
			{point.x, point.y,
			 std::max(17., std::min(23., std::min(rx, ry) * .64 * std::sin(pi / count) * .75)) *
				 variation * outerScale,
			 (compact ? 17. : 14.) / variation * outerScale,
			 c.bounded("hungry-marches.pool-shape", 10000) * 2 * pi / 10000, a + pi / 2, false});
	}
	const int centralCount = compact || r.nbTeams <= 3
								 ? 1
								 : std::min(std::max(3, r.nbTeams / 2) + int(extra * 2),
											std::max(3, int(std::min(rx, ry) / 17.)));
	for (int k = 0; k < centralCount; ++k)
	{
		const double across = (k - (centralCount - 1) * .5) * 14.;
		const double along = centralCount == 1 ? 0 : 9 * std::sin(k * 2. + phase);
		// Aim each end into a gap between actual homes. A straight chain along
		// the home ring's phase points at one colony, especially with odd counts.
		const int pair = across < 0 ? r.nbTeams / 2 : 0;
		const auto &a = L.sites[pair], &b = L.sites[(pair + 1) % r.nbTeams];
		const double mx = (a.x + b.x) * .5, my = (a.y + b.y) * .5;
		const double axis = centralCount == 1 ? phase : std::atan2(my - cy, mx - cx);
		auto point = bent(cx + std::abs(across) * std::cos(axis) - along * std::sin(axis),
						  cy + std::abs(across) * std::sin(axis) + along * std::cos(axis));
		if (centralCount > 1)
		{
			const double dx = b.x - a.x, dy = b.y - a.y;
			const double correction =
				((point.x - mx) * dx + (point.y - my) * dy) / (dx * dx + dy * dy);
			const double taper = std::abs(across) / ((centralCount - 1) * .5 * 14.);
			point.x = int(point.x - correction * dx * taper);
			point.y = int(point.y - correction * dy * taper);
		}
		L.pools.push_back({point.x, point.y,
						   (r.nbTeams <= 3 ? 25.
							: compact      ? 24.
										   : 18.) *
							   innerScale,
						   (r.nbTeams <= 3 ? 20.
							: compact      ? 20.
										   : 14.) *
							   innerScale,
						   c.bounded("hungry-marches.pool-shape", 10000) * 2 * pi / 10000,
						   axis + pi / 2, true});
	}
	// Outer fields belong between actual neighbouring starts, including their
	// landscape bends. Sampling another independent ring can accidentally give
	// one colony a near-private field, particularly on rectangles and odd counts.
	if (r.nbTeams >= 3)
		for (int k = 0; k < count; ++k)
		{
			const auto &a = L.sites[k % r.nbTeams], &b = L.sites[(k + 1) % r.nbTeams];
			const double factor = r.nbTeams == 3   ? 1.0
								  : r.nbTeams == 4 ? .90
								  : r.nbTeams == 5 ? .80
												   : .70;
			const double mx = (a.x + b.x) * .5, my = (a.y + b.y) * .5;
			const double dx = b.x - a.x, dy = b.y - a.y;
			// Move inward along the pair's bisector, retaining equal geometric
			// access despite the landscape's bent and jittered home positions.
			const double projection = ((cx - mx) * dx + (cy - my) * dy) / (dx * dx + dy * dy);
			L.pools[k].x = int(mx + (1 - factor) * (cx - mx - projection * dx));
			L.pools[k].y = int(my + (1 - factor) * (cy - my - projection * dy));
			L.pools[k].angle = std::atan2(double(b.y - a.y), double(b.x - a.x)) + pi / 2;
		}
	// In a duel a ring gives each home its own side's farms. Put all pools
	// along the actual starts' perpendicular bisector instead; the finite
	// reserves remain at home and neither side gets a renewable back garden.
	if (r.nbTeams == 2)
	{
		const double mx = (L.sites[0].x + L.sites[1].x) * .5,
					 my = (L.sites[0].y + L.sites[1].y) * .5;
		const double angle =
			std::atan2(double(L.sites[1].y - L.sites[0].y), double(L.sites[1].x - L.sites[0].x));
		L.pools.clear();
		const int pairs = compact ? 1 : 1 + int(extra + .5);
		for (int pair = 0; pair < pairs; ++pair)
			for (int sign : {-1, 1})
			{
				const double across = sign * (35 + pair * 32);
				L.pools.push_back({int(mx - across * std::sin(angle)),
								   int(my + across * std::cos(angle)), 17 * outerScale,
								   18 * outerScale,
								   c.bounded("hungry-marches.duel-shores", 10000) * 2 * pi / 10000,
								   angle + pi / 2, false});
			}
		L.pools.push_back({int(mx), int(my), 14 * innerScale, 25 * innerScale,
						   c.bounded("hungry-marches.duel-shores", 10000) * 2 * pi / 10000,
						   angle + pi / 2, true});
	}
	// Each wetland finger bends in its own local frame. Separate channels,
	// asymmetric crop banks and cross-country tracks replace circular farm stamps.
	L.scenery = periodicNoise(t.w, t.h, 18, c.stream("hungry-marches.scrub"));
	const auto dunes = periodicNoise(t.w, t.h, 38, c.stream("hungry-marches.dry-ground"));
	L.timber.assign(n, 0);
	std::vector<double> cosine, sine, bounds;
	for (const auto &pool : L.pools)
	{
		cosine.push_back(std::cos(pool.angle));
		sine.push_back(std::sin(pool.angle));
		// A relevant point has normalized radius <= 1.10 * 1.19,
		// plus at most .20 bend: twice the largest axis is conservative.
		bounds.push_back(2 * std::max(pool.rx, pool.ry));
	}
	for (int y = 0; y < t.h; ++y)
		for (int x = 0; x < t.w; ++x)
		{
			const int i = t.at(x, y);
			double best = 2.;
			int owner = -1;
			double u = 0, v = 0;
			bool crossingAny = false;
			for (int p = 0; p < int(L.pools.size()); ++p)
			{
				const auto &pool = L.pools[p];
				const double dx = t.offsetX(pool.x, x), dy = t.offsetY(pool.y, y);
				if (std::abs(dx) > bounds[p] || std::abs(dy) > bounds[p])
					continue;
				const double along = (dx * cosine[p] + dy * sine[p]) / pool.rx;
				const double across = (-dx * sine[p] + dy * cosine[p]) / pool.ry -
									  .20 * std::sin(along * 3 + pool.phase);
				const double shape = 1. + .13 * std::sin(along * 6 + pool.phase) +
									 .06 * std::cos(across * 5 - pool.phase);
				const double d = std::sqrt(along * along + across * across) / shape;
				// Preserve every finger's crossing through overlapping shores.
				// Nearest-pool ownership must not erase a neighbour's route.
				if (d <= 1.10 &&
					std::min(std::abs(along - .38 - .045 * std::sin(across * 5)),
							 std::abs(along + .40 - .045 * std::sin(across * 5))) < .065)
					crossingAny = true;
				if (d < best)
				{
					best = d;
					owner = p;
					u = along;
					v = across;
				}
			}
			double homeDistance = 1000;
			for (const auto &site : L.sites)
				homeDistance =
					std::min(homeDistance, std::sqrt(double(t.dist2(site.x, site.y, x, y))));
			if (owner < 0 || best > 1.10)
			{
				if (dunes[i] > 47000 + std::max(0., 30 - homeDistance) * 1700 &&
					L.scenery[i] > 26000)
					L.terrain[i] = SAND;
				continue;
			}
			// Two transverse paths cut crops as well as water. A third bank, beyond
			// the paths, holds renewable timber without competing with the grain.
			const double crossing = std::min(std::abs(u - .38 - .045 * std::sin(v * 5)),
											 std::abs(u + .40 - .045 * std::sin(v * 5)));
			if (best > 1. || crossing < .065 || crossingAny)
			{
				L.terrain[i] = SAND;
				L.roads[i] = 1;
			}
			else if (u * u / .90 + v * v / .19 < 1.)
				L.terrain[i] = WATER;
			else if (homeDistance < 22)
			{
				L.terrain[i] = SAND;
				L.roads[i] = 1;
			}
			else
			{
				L.field[i] = owner;
				L.timber[i] = u < -.45 && !L.pools[owner].central;
			}
		}
	// Rasterize the cap in corner space too: a fractional-width outline can
	// skip a diagonal corner on a compact map. One closed corner row is sufficient
	// to stop eight-neighbour crop growth after tile conversion.
	const auto beforeCap = L.terrain;
	for (int i = 0; i < n; ++i)
		if (L.field[i] >= 0)
			for (int dy = -1; dy <= 1; ++dy)
				for (int dx = -1; dx <= 1; ++dx)
				{
					const int j = t.at(i % t.w + dx, i / t.w + dy);
					if (L.field[j] < 0 && !L.roads[j] && beforeCap[j] == GRASS)
					{
						L.terrain[i] = SAND;
						L.roads[i] = 1;
					}
				}
	layBeaches(L.terrain, t);
	const auto grass = pureTiles(L.terrain, t, GRASS);
	L.fertility = cropGrowthField(L.terrain, t);
	for (int i = 0; i < n; ++i)
		if (!grass[i])
			L.field[i] = -1;
	const auto components = connectedRegions(grass, t.w, t.h, true, GridNeighbors::Eight);
	L.component = components;
	std::vector<int> woodVotes(n, 0), totalVotes(n, 0);
	for (int i = 0; i < n; ++i)
		if (L.field[i] >= 0 && components[i] >= 0)
		{
			++totalVotes[components[i]];
			woodVotes[components[i]] += L.timber[i];
		}
	// A bank shared with an interior district belongs wholly to grain.
	for (int i = 0; i < n; ++i)
		if (L.field[i] >= 0 && L.pools[L.field[i]].central && components[i] >= 0)
			woodVotes[components[i]] = 0;
	for (int i = 0; i < n; ++i)
		if (L.field[i] >= 0 && components[i] >= 0)
			L.timber[i] = woodVotes[components[i]] * 2 > totalVotes[components[i]];
	// Clipped shores can leave a token grain sliver beside a substantial wood
	// bank. Restore its timber components so that district is a useful
	// harvest alternative; never mix competing trees and wheat on one bank.
	L.grainCapacity.assign(L.pools.size(), 0);
	L.grainPotential.assign(L.pools.size(), 0);
	for (int i = 0; i < n; ++i)
		if (L.field[i] >= 0 && !L.timber[i] && L.fertility.values()[i] > 0)
		{
			++L.grainCapacity[L.field[i]];
			L.grainPotential[L.field[i]] += L.fertility.values()[i];
		}
	std::vector<unsigned char> restore(n, 0), restoredPool(L.pools.size(), 0);
	for (int i = 0; i < n; ++i)
	{
		const int p = L.field[i];
		if (p >= 0 && L.timber[i] && L.fertility.values()[i] > 0 &&
			(L.grainCapacity[p] < 24 || L.grainPotential[p] < 3 * Fertility::kScale))
		{
			restore[L.component[i]] = 1;
			restoredPool[p] = 1;
		}
	}
	for (int i = 0; i < n; ++i)
		if (L.field[i] >= 0 && restore[L.component[i]])
			L.timber[i] = 0;
	c.telemetry.measure("hungry-marches.restored-grain-banks",
						std::accumulate(restoredPool.begin(), restoredPool.end(), 0));
	std::fill(L.grainCapacity.begin(), L.grainCapacity.end(), 0);
	std::fill(L.grainPotential.begin(), L.grainPotential.end(), 0);
	for (int i = 0; i < n; ++i)
		if (L.field[i] >= 0 && !L.timber[i] && L.fertility.values()[i] > 0)
		{
			++L.grainCapacity[L.field[i]];
			L.grainPotential[L.field[i]] += L.fertility.values()[i];
		}
	dealStarts(c, L.sites, "hungry-marches.deal");
	c.telemetry.measure("hungry-marches.outer-districts", int(L.pools.size()) - centralCount);
	c.telemetry.measure("hungry-marches.central-districts", centralCount);
	return L;
}

Layout design(const GenerationRequest &r, GenerationContext &c)
{
	return cachedDesign<Layout>(r, c, designAfresh);
}
std::string foodAccess(const Game &game, const Layout &L, int teams, bool future,
					   GenerationTelemetry *telemetry = nullptr)
{
	const auto &t = L.t;
	const auto workers = unitTilesByTeam(game.map, teams);
	auto open = walkableTiles(game.map);
	if (future)
		for (int i = 0; i < t.size(); ++i)
			if (L.field[i] >= 0)
				open[i] = 0;
	if (future)
	{
		const auto reach = reachFrom(t, workers[0], open, t.size());
		std::vector<unsigned char> reached(t.size(), 0);
		for (int i : reach.tiles)
			reached[i] = 1;
		for (int team = 1; team < teams; ++team)
			if (std::none_of(workers[team].begin(), workers[team].end(),
							 [&](int i) { return reached[i]; }))
				return "Mature crops disconnect the colonies.";
	}
	std::vector<int> visitors(L.pools.size(), 0), first(L.pools.size(), 1000),
		second(L.pools.size(), 1000);
	std::vector<std::vector<int>> approaches, harvests;
	for (int team = 0; team < teams; ++team)
	{
		const auto reach = reachFrom(t, workers[team], open, 96);
		std::vector<int> distances(L.pools.size(), -1), targets(L.pools.size(), -1);
		for (size_t j = 0; j < reach.tiles.size(); ++j)
		{
			const int at = reach.tiles[j];
			for (int dy = -1; dy <= 1; ++dy)
				for (int dx = -1; dx <= 1; ++dx)
				{
					const int i = t.at(at % t.w + dx, at / t.w + dy), p = L.field[i];
					if (p >= 0 && !L.timber[i] &&
						((future && L.fertility.values()[i] > 0) ||
						 (!future && game.map.getResource(i % t.w, i / t.w).type == WHEAT)) &&
						distances[p] < 0)
					{
						distances[p] = reach.steps[j] + 1;
						targets[p] = i;
					}
				}
		}
		approaches.push_back(distances);
		harvests.push_back(targets);
		int alternatives = 0;
		for (int p = 0; p < int(L.pools.size()); ++p)
			if (distances[p] >= 0)
			{
				++visitors[p];
				if (!L.pools[p].central && distances[p] <= 64 && L.grainCapacity[p] >= 24 &&
					L.grainPotential[p] >= 3 * Fertility::kScale)
					++alternatives;
				if (distances[p] < first[p])
				{
					second[p] = first[p];
					first[p] = distances[p];
				}
				else if (distances[p] < second[p])
					second[p] = distances[p];
			}
		if (alternatives < 2)
			return "A colony cannot reach two outer harvest alternatives within 64 steps.";
	}
	for (int p = 0; p < int(L.pools.size()); ++p)
	{
		if (!L.grainCapacity[p])
			continue;
		if (visitors[p] < 2)
			return "A farming district is not accessible to two rival colonies within 96 steps: "
				   "district " +
				   std::to_string(p) + " walks " + std::to_string(first[p]) + "/" +
				   std::to_string(second[p]) + " capacity " + std::to_string(L.grainCapacity[p]);
		if ((first[p] < 8 && second[p] - first[p] > 12) || second[p] - first[p] > maximumRivalGap)
		{
			std::vector<std::pair<int, int>> nearest;
			for (int team = 0; team < teams; ++team)
				if (approaches[team][p] >= 0)
					nearest.emplace_back(approaches[team][p], team);
			std::sort(nearest.begin(), nearest.end());
			std::string detail;
			for (int k = 0; k < 2; ++k)
			{
				const int team = nearest[k].second, at = harvests[team][p];
				const auto &home = L.sites[team];
				detail += " home(" + std::to_string(home.x) + "," + std::to_string(home.y) +
						  ") grain(" + std::to_string(at % t.w) + "," + std::to_string(at / t.w) +
						  ")";
			}
			return "A shared field is too private: district " + std::to_string(p) + " walks " +
				   std::to_string(first[p]) + "/" + std::to_string(second[p]) + detail;
		}
		if (telemetry)
		{
			telemetry->measure(future ? "hungry-marches.field.mature-closest-walk"
									  : "hungry-marches.field.closest-walk",
							   first[p], p);
			telemetry->measure(future ? "hungry-marches.field.mature-rival-walk"
									  : "hungry-marches.field.rival-walk",
							   second[p], p);
		}
	}
	return {};
}

bool generate(Game &game, GenerationContext &c)
{
	c.stage = "hungry marches landscape";
	const auto L = design(c.request, c);
	if (!L.failure.empty())
	{
		c.detail = L.failure;
		return false;
	}
	const auto &t = L.t;
	HungryMarchesOptions o(c.request);
	writeUndermap(game.map, L.terrain);
	for (int k = 0; k < c.request.nbTeams; ++k)
		game.addTeam();
	for (int k = 0; k < c.request.nbTeams; ++k)
	{
		const auto &s = L.sites[k];
		std::vector<unsigned char> home(t.size(), 0);
		for (int y = -9; y <= 9; ++y)
			for (int x = -9; x <= 9; ++x)
				home[t.at(s.x + x, s.y + y)] = 1;
		if (!placeSettlement(game, c, k, home, {s.x, s.y}, "hungry-marches.settlements"))
			return false;
	}
	c.stage = "hungry marches finite reserves";
	for (int k = 0; k < c.request.nbTeams; ++k)
	{
		const auto &s = L.sites[k];
		const int ax = s.x - int(7 * std::cos(s.angle)), ay = s.y - int(7 * std::sin(s.angle));
		const auto dry = [&](int i)
		{
			return L.field[i] < 0 && !L.fertility.values()[i] &&
				   t.chebyshev(s.x, s.y, i % t.w, i / t.w) >= 6 &&
				   t.chebyshev(s.x, s.y, i % t.w, i / t.w) <= 16;
		};
		const int ration = o.ration;
		const auto food = growPatchesNear(game.map, t, ax, ay, 24, WHEAT, ration, dry);
		const auto timberGround = [&](int i)
		{
			return L.field[i] < 0 && !L.fertility.values()[i] &&
				   t.chebyshev(s.x, s.y, i % t.w, i / t.w) >= 6 &&
				   t.chebyshev(s.x, s.y, i % t.w, i / t.w) <= 24;
		};
		const auto quarryGround = [&](int i)
		{
			return L.field[i] < 0 && t.chebyshev(s.x, s.y, i % t.w, i / t.w) >= 8 &&
				   t.chebyshev(s.x, s.y, i % t.w, i / t.w) <= 24;
		};
		const auto wood = growPatchesNear(game.map, t, s.x - int(9 * std::sin(s.angle)),
										  s.y + int(9 * std::cos(s.angle)), 24, WOOD,
										  18 + int(scaledCount(52, o.wood)), timberGround);
		const auto stone = growPatchesNear(game.map, t, s.x + int(11 * std::sin(s.angle)),
										   s.y - int(11 * std::cos(s.angle)), 24, STONE,
										   2 + int(scaledCount(4, o.stone)), quarryGround);
		if (food.tiles < ration || wood.tiles < 18 || stone.tiles < 2)
		{
			c.detail = "Colony " + std::to_string(k + 1) + " has dry supplies " +
					   std::to_string(food.tiles) + "/" + std::to_string(ration) + " wheat, " +
					   std::to_string(wood.tiles) + "/18 wood, " + std::to_string(stone.tiles) +
					   "/2 stone; use fewer colonies or a larger map.";
			return false;
		}
		c.telemetry.measure("hungry-marches.home.wheat", food.tiles, k);
		c.telemetry.measure("hungry-marches.home.wood", wood.tiles, k);
		c.telemetry.measure("hungry-marches.home.wood-requested", 18 + int(scaledCount(52, o.wood)),
							k);
		c.telemetry.measure("hungry-marches.home.stone", stone.tiles, k);
		c.telemetry.measure("hungry-marches.home.stone-requested", 2 + int(scaledCount(4, o.stone)),
							k);
	}
	c.stage = "hungry marches shared harvest";
	for (int p = 0; p < int(L.pools.size()); ++p)
	{
		std::vector<int> tiles;
		for (int i = 0; i < t.size(); ++i)
			if (L.field[i] == p && !L.timber[i] && L.fertility.values()[i] > 0)
				tiles.push_back(i);
		c.shuffle(tiles.begin(), tiles.end(), "hungry-marches.wheat");
		std::vector<unsigned char> seeded(t.size(), 0);
		int planted = 0;
		long long potential = 0;
		// Crossings isolate banks permanently. Every productive grass component
		// gets a seed, even at zero optional wheat; otherwise its advertised
		// growing capacity would never become available to a player.
		for (int i : tiles)
		{
			potential += L.fertility.values()[i];
			if (!seeded[L.component[i]] && game.map.isResourceAllowed(i % t.w, i / t.w, WHEAT))
			{
				game.map.setResource(i % t.w, i / t.w, WHEAT, 1);
				seeded[L.component[i]] = 1;
				++planted;
			}
		}
		const int wanted =
			std::min(int(tiles.size()), planted + int(tiles.size()) * o.wheat * 65 / 20000);
		for (int i : tiles)
			if (planted < wanted && !game.map.isResource(i % t.w, i / t.w) &&
				game.map.isResourceAllowed(i % t.w, i / t.w, WHEAT))
			{
				game.map.setResource(i % t.w, i / t.w, WHEAT, 1);
				++planted;
			}
		c.telemetry.measure("hungry-marches.field.wheat", planted, p);
		c.telemetry.measure("hungry-marches.field.x", L.pools[p].x, p);
		c.telemetry.measure("hungry-marches.field.y", L.pools[p].y, p);
		c.telemetry.measure("hungry-marches.field.crop-tiles", int(tiles.size()), p);
		c.telemetry.measure("hungry-marches.field.omitted", tiles.empty() ? 1 : 0, p);
		c.telemetry.measure("hungry-marches.field.growth-potential",
							double(potential) / Fertility::kScale, p);
		c.telemetry.measure("hungry-marches.field.central", L.pools[p].central ? 1 : 0, p);
	}
	int timber = 0, scrub = 0;
	long long timberPotential = 0;
	std::vector<unsigned char> timberSeeded(t.size(), 0);
	for (int i = 0; i < t.size(); ++i)
	{
		bool home = false;
		for (const auto &site : L.sites)
			if (t.chebyshev(site.x, site.y, i % t.w, i / t.w) < 20)
				home = true;
		const bool renewable = L.field[i] >= 0 && L.timber[i] && L.fertility.values()[i] > 0;
		if (renewable)
			timberPotential += L.fertility.values()[i];
		const bool seed = renewable && !timberSeeded[L.component[i]];
		const bool dry =
			!home && L.field[i] < 0 && !L.fertility.values()[i] && L.scenery[i] > 51000;
		if ((renewable || dry) &&
			(c.bounded("hungry-marches.timber", 100) < unsigned(10 + o.wood * 40 / 100) || seed) &&
			!game.map.isResource(i % t.w, i / t.w) &&
			game.map.isResourceAllowed(i % t.w, i / t.w, WOOD))
		{
			game.map.setResource(i % t.w, i / t.w, WOOD, 1);
			if (renewable)
			{
				++timber;
				timberSeeded[L.component[i]] = 1;
			}
			else
				++scrub;
		}
	}
	c.telemetry.measure("hungry-marches.renewable-timber", timber);
	c.telemetry.measure("hungry-marches.timber-growth-potential",
						double(timberPotential) / Fertility::kScale);
	c.telemetry.measure("hungry-marches.dry-scrub", scrub);
	// Other renewable resources live at the frontier too; no ambient wheat pass.
	seedAlgae(game.map, c, t, "hungry-marches.algae", o.algae, AlgaeBand::shallows(2, 4));
	c.detail = foodAccess(game, L, c.request.nbTeams, false, &c.telemetry);
	return c.detail.empty();
}

std::string validateWorld(const Game &game, const GenerationContext &c)
{
	GenerationContext replay(c.request);
	const auto L = design(c.request, replay);
	if (const auto mismatch = designMismatch(L, game.map, "hungry marches"); !mismatch.empty())
		return mismatch;
	const auto &t = L.t;
	const auto grass = pureTiles(game.map, GRASS);
	const auto components = connectedRegions(grass, t.w, t.h, true, GridNeighbors::Eight);
	std::vector<unsigned char> shared(t.size(), 0), kinds(t.size(), 0);
	int privateWheat = 0;
	for (int i = 0; i < t.size(); ++i)
		if (L.field[i] >= 0 && components[i] >= 0)
			shared[components[i]] = 1;
	for (int i = 0; i < t.size(); ++i)
	{
		const int resource = game.map.getResource(i % t.w, i / t.w).type;
		if (resource == WHEAT && L.field[i] < 0)
			++privateWheat;
		if (L.field[i] >= 0 && components[i] >= 0)
		{
			kinds[components[i]] |= resource == WHEAT ? 1 : resource == WOOD ? 2 : 0;
			if (kinds[components[i]] == 3)
				return "Timber and wheat share a growing bank.";
		}
		if (game.map.getUMTerrain(i % t.w, i / t.w) != L.terrain[i])
			return "The floodplain terrain changed after design.";
		if (L.roads[i] && game.map.isResource(i % t.w, i / t.w))
			return "A harvest crossing is blocked.";
		if (L.field[i] < 0 && components[i] >= 0 && shared[components[i]])
			return "A shared field can spread into the surrounding country.";
		if (game.map.getResource(i % t.w, i / t.w).type == WHEAT && L.field[i] < 0 &&
			L.fertility.values()[i])
			return "Private wheat must be a finite, dry opening ration.";
	}
	if (privateWheat != HungryMarchesOptions(c.request).ration * c.request.nbTeams)
		return "The finite opening wheat no longer matches the requested ration.";
	if (const auto access = startingAccessFailure(
			game.map, c.request.nbTeams, {{WHEAT, 24, "opening wheat"}, {WOOD, 32, "wood"}}, 32);
		!access.empty())
		return access;
	if (const auto error = foodAccess(game, L, c.request.nbTeams, false); !error.empty())
		return error;
	if (const auto error = foodAccess(game, L, c.request.nbTeams, true); !error.empty())
		return "Mature fields: " + error;
	return walkFromFirstColony(game.map, c.request.nbTeams, "the marches", "around the floodplain")
		.error;
}
} // namespace
HungryMarchesOptions::HungryMarchesOptions(const GenerationRequest &r)
	: ration(r.option("opening-ration")), concentration(r.option("central-concentration")),
	  wheat(r.option("wheat-amount")), wood(r.option("wood-amount")),
	  stone(r.option("stone-amount")), algae(r.option("algae-amount"))
{
}
GeneratorDefinition hungryMarchesDefinition()
{
	return {
		"hungry-marches",
		69,
		"The Hungry Marches",
		1,
		false,
		{{"opening-ration", "Opening ration", 20, 100, 10, 50, ControlGroup::Layout},
		 {"central-concentration", "Central concentration", 50, 80, 5, 65, ControlGroup::Layout},
		 GeneratorControl::percentage("wheat-amount", "Wheat amount", 200),
		 GeneratorControl::percentage("wood-amount", "Wood amount", 200),
		 GeneratorControl::percentage("stone-amount", "Stone amount", 200),
		 GeneratorControl::percentage("algae-amount", "Algae amount", 200)},
		generate,
		true,
		validateRequest,
		validateWorld,
		{"terrain:natural", "feature:lakes", "style:wide-open", "style:contested-center"}};
}
