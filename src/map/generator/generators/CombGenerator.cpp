// SPDX-License-Identifier: GPL-3.0-or-later
#include "CombGenerator.h"
#include "Channels.h"
#include "Drawing.h"
#include "Game.h"
#include "GenerationContext.h"
#include "Geometry.h"
#include "Grid.h"
#include "Growth.h"
#include "Morphology.h"
#include "Noise.h"
#include "Pipeline.h"
#include "Planting.h"
#include "Resources.h"
#include "Room.h"
#include "ScoredSettlements.h"
#include "Settlements.h"
#include "Sketch.h"
#include "Topology.h"
#include "Walls.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <string>
#include <vector>
using namespace MapGeneration;

// Two productive mainland shores, joined around both ends of a winding inlet. Its bends
// form broad interlocking peninsulas: near by water, far by foot. Crops stay on the outer
// coast, leaving the inlet's banks for forward inns, towers and swimming approaches.
// Mid-tier towers threaten the opposing waterfront; units have priority over buildings.
namespace
{
constexpr double kChannelRadius = 1.75;
constexpr int kFoodFloor = 24, kWoodFloor = 12;
struct Layout
{
	Torus t{1, 1};
	bool horizontal = true;
	int length = 0, breadth = 0, first = 0, last = 0;
	TerrainSketch terrain;
	std::vector<int> side, plot, kind, scrubKind;
	std::vector<unsigned char> scrub;
	std::vector<std::vector<int>> fields;
	std::vector<unsigned char> town, roads, ends[2];
	std::vector<ShapePoint> tips;
	std::vector<double> line, coast[2], edges;
	std::string failure;
	ShapePoint point(double u, double v) const
	{
		return horizontal ? ShapePoint{u, v} : ShapePoint{v, u};
	}
	int index(int u, int v) const { return horizontal ? t.at(u, v) : t.at(v, u); }
	int u(int i) const { return horizontal ? i % t.w : i / t.w; }
	int v(int i) const { return horizontal ? i / t.w : i % t.w; }
};
std::string validateRequest(const GenerationRequest &r)
{
	if (r.wDec < 8 || r.hDec < 8 || r.wDec > 9 || r.hDec > 9)
		return "The Comb needs sides of 256 or 512 tiles.";
	if (r.nbTeams < 2 || r.nbTeams > ((r.wDec == 9 && r.hDec == 9) ? 8 : 4))
		return "The Comb supports 2–4 colonies, or up to 8 on 512 by 512 maps.";
	return {};
}
Layout design(const GenerationRequest &r, GenerationContext &c)
{
	Layout L;
	L.failure = validateRequest(r);
	if (!L.failure.empty())
		return L;
	const CombOptions o(r);
	L.t = {1 << r.wDec, 1 << r.hDec};
	const auto &t = L.t;
	L.horizontal = t.w == t.h ? c.bounded("comb-orientation", 2) == 0 : t.w > t.h;
	L.length = std::max(t.w, t.h);
	L.breadth = std::min(t.w, t.h);
	const int n = t.size(), U = L.length, V = L.breadth;
	L.first = 30;
	L.last = U - 31;
	L.terrain.assign(n, WATER);
	L.side.assign(n, -1);
	L.plot.assign(n, -1);
	L.town.assign(n, 0);
	L.roads.assign(n, 0);
	for (auto &mask : L.ends)
		mask.assign(n, 0);
	L.line.resize(U, V * 0.5);
	for (auto &coast : L.coast)
		coast.resize(U);
	const double phase = c.bounded("comb-outline", 6283) * 0.001;
	const int segments = 2 * o.peninsulas;
	std::vector<double> edges{double(L.first)}, amplitudes(segments), bays(segments),
		noses(segments), contacts(segments);
	double total = 0;
	for (int k = 0; k < segments; ++k)
	{
		total += 85 + c.bounded("comb-bends", 31);
		edges.push_back(total);
		amplitudes[k] = 0.76 + c.bounded("comb-bends", 25) * 0.01;
		bays[k] = 4.0 + c.bounded("comb-bays", 31) * 0.1;
		noses[k] = 0.40 + c.bounded("comb-noses", 21) * 0.01;
		contacts[k] = 0.17 + c.bounded("comb-contacts", 13) * 0.01;
	}
	for (int k = 1; k <= segments; ++k)
		edges[k] = L.first + edges[k] / total * (L.last - L.first);
	L.edges = edges;
	std::vector<double> channelWidth(U, kChannelRadius), offset(U, 0);
	const int bayA = c.bounded("comb-bays", segments / 2),
			  bayB = segments / 2 + c.bounded("comb-bays", segments - segments / 2);
	for (int u = 0; u < U; ++u)
	{
		const double nx = std::abs((u - U * 0.5) / (U * 0.5 - 10));
		const double extent =
			nx < 1 ? std::min(115.0, V * 0.5 - 13) * std::pow(1 - std::pow(nx, 4), 0.25) : 0;
		L.coast[0][u] =
			V * 0.5 - extent + 3.5 * std::sin(u * 0.041 + phase) + 2 * std::sin(u * 0.089 + phase);
		L.coast[1][u] = V * 0.5 + extent + 3.5 * std::sin(u * 0.035 + phase + 2) +
						2 * std::sin(u * 0.082 + phase);
		const int k =
			std::clamp(int(std::upper_bound(edges.begin(), edges.end(), u) - edges.begin()) - 1, 0,
					   segments - 1);
		const double f = std::clamp((u - edges[k]) / (edges[k + 1] - edges[k]), 0.0, 1.0);
		const double progress =
			f < noses[k] ? 0.5 * f / noses[k] : 0.5 + 0.5 * (f - noses[k]) / (1 - noses[k]);
		const double wave = std::pow(std::sin(kPi * progress), 0.7);
		const double headroom = std::min(
			82.0, std::max(20.0, std::min(V * 0.5 - L.coast[0][u], L.coast[1][u] - V * 0.5) - 58));
		L.line[u] = V * 0.5 + (k % 2 == 0 ? 1 : -1) * headroom * amplitudes[k] * wave;
		// Long facing reaches alternate with broader water; only two asymmetrical bays
		// widen substantially, rather than repeating a round pool at every bend.
		const double distance = std::abs(f - contacts[k]);
		const double narrow = 1 - std::clamp((distance - 0.065) / 0.12, 0.0, 1.0);
		channelWidth[u] = 3.1 - (3.1 - kChannelRadius) * narrow;
		if (k == bayA || k == bayB)
		{
			const double bay = std::exp(-std::pow((f - 0.72) / 0.20, 2)) * bays[k];
			channelWidth[u] += bay;
			offset[u] = (k % 2 == 0 ? 1 : -1) * bay * 0.8;
		}
		for (int v = 0; v < V; ++v)
		{
			const int i = L.index(u, v);
			L.ends[0][i] = u <= L.first + 2;
			L.ends[1][i] = u >= L.last - 2;
			if (u < 10 || u > U - 11 || v < L.coast[0][u] || v > L.coast[1][u])
				continue;
			L.terrain[i] = GRASS;
			L.side[i] = v < L.line[u] ? 0 : 1;
			L.ends[0][i] = u <= L.first + 2;
			L.ends[1][i] = u >= L.last - 2;
		}
	}
	std::vector<StrokePoint> channel;
	for (int u = L.first; u <= L.last; ++u)
	{
		const double slope = (L.line[std::min(U - 1, u + 1)] - L.line[std::max(0, u - 1)]) / 2;
		const double norm = std::sqrt(1 + slope * slope);
		auto p = L.point(u - offset[u] * slope / norm, L.line[u] + offset[u] / norm);
		channel.push_back({p.x, p.y, channelWidth[u]});
	}
	std::vector<unsigned char> water(n, 0);
	strokePath(water, t, channel);
	for (int i = 0; i < n; ++i)
		if (water[i])
			L.terrain[i] = WATER;
	// Each half-wave is one peninsula. The road ends inland of its tip, leaving both banks
	// buildable; its approach meets the continuous mainland route.
	for (int k = 0; k < segments; ++k)
	{
		const int lo = int(std::ceil(edges[k]));
		const int hi = int(std::floor(edges[k + 1]));
		int extremum = lo;
		for (int u = lo; u <= hi; ++u)
			if ((k % 2 == 0 && L.line[u] > L.line[extremum]) ||
				(k % 2 == 1 && L.line[u] < L.line[extremum]))
				extremum = u;
		const int bank = k % 2;
		const double tipV = L.line[extremum] + (bank == 0 ? -25 : 25);
		L.tips.push_back(L.point(extremum, tipV));
		const double rootV = bank == 0 ? L.coast[0][extremum] + 43 : L.coast[1][extremum] - 43;
		const auto a = L.point(extremum, rootV),
				   b = L.point(extremum, L.line[extremum] + (bank == 0 ? -42 : 42));
		const auto control = L.point(extremum + 10 * std::sin(k + phase), (rootV + tipV) * 0.5);
		strokePath(L.roads, t, bezierPath(a, control, b, 0.65, 0.65, 24));
	}
	// Contained, coast-following farm ribbons. Cross aisles and the wood/wheat divider
	// partition crops without invisible growth flags; no fertile decorative timber escapes.
	const int fieldStart = 38, fieldEnd = U - 38, plots = std::max(1, (fieldEnd - fieldStart) / 32);
	L.fields.resize(4 * plots);
	L.kind.resize(4 * plots);
	for (int bank = 0; bank < 2; ++bank)
		for (int p = 0; p < plots; ++p)
		{
			L.kind[bank * 2 * plots + 2 * p] = WHEAT;
			L.kind[bank * 2 * plots + 2 * p + 1] = WOOD;
		}
	for (int u = 18; u < U - 18; ++u)
		for (int v = 0; v < V; ++v)
		{
			int i = L.index(u, v);
			if (L.terrain[i] != GRASS)
				continue;
			const int bank = v < V / 2 ? 0 : 1;
			const double d = bank == 0 ? v - L.coast[0][u] : L.coast[1][u] - v;
			if (d >= 17 && d <= 35 && u >= 34 && u < U - 34)
				L.town[i] = 1;
			if (d >= 40 && d <= 41.3)
				L.roads[i] = 1;
			if (u < fieldStart || u >= fieldEnd)
				continue;
			const double z = double(u - fieldStart) * plots / (fieldEnd - fieldStart);
			const int p = std::min(plots - 1, int(z));
			const double f = z - p;
			if (d <= 15)
			{
				if (d >= 14 || f < 0.045 || f > 0.95 || (f > 0.66 && f < 0.71))
					L.terrain[i] = SAND;
				else if (d >= 2)
					L.plot[i] = bank * 2 * plots + 2 * p + (f >= 0.71);
			}
		}
	for (int i = 0; i < n; ++i)
		if (L.roads[i] && L.terrain[i] == GRASS)
			L.terrain[i] = SAND;
	// Wind-scoured clearings break up the open lawns. Some retain little grassy
	// centres for isolated crop clumps; a real sand rim contains their growth.
	std::vector<unsigned char> protectedGround(n, 0), eligible(n, 0), patches(n, 0);
	for (int i = 0; i < n; ++i)
		protectedGround[i] = L.town[i] || L.roads[i] || L.plot[i] >= 0;
	protectedGround = dilate(t, protectedGround, 3);
	for (int i = 0; i < n; ++i)
	{
		eligible[i] = !protectedGround[i] && L.u(i) > L.first && L.u(i) < L.last;
		for (const auto &tip : L.tips)
			if (t.dist2(i % t.w, i / t.w, int(tip.x), int(tip.y)) < 100)
				eligible[i] = 0;
	}
	GenerationNoise scatter(GenerationContext::deriveSeed(r.seed, "comb-scattered-ground"));
	const auto beforeScatter = L.terrain;
	sprinkleSand(L.terrain, t, eligible, 0.065, 5,
				 [&](int i)
				 {
					 return scatter.Noise((i % t.w) * 0.075f, (i / t.w) * 0.075f) +
							0.3 * scatter.Noise((i % t.w) * 0.17f, (i / t.w) * 0.17f);
				 });
	for (int i = 0; i < n; ++i)
		patches[i] = L.terrain[i] == SAND && beforeScatter[i] == GRASS;
	const auto depth = clearance(t, patches);
	L.scrub.assign(n, 0);
	L.scrubKind.assign(n, WHEAT);
	int sandCorners = 0;
	for (int i = 0; i < n; ++i)
	{
		if (depth[i] >= 2 && scatter.Noise((i % t.w) * 0.04f + 37, (i / t.w) * 0.04f) > 0)
		{
			L.terrain[i] = GRASS;
			L.scrub[i] = 1;
			L.scrubKind[i] =
				scatter.Noise((i % t.w) * 0.035f, (i / t.w) * 0.035f + 71) > 0 ? WOOD : WHEAT;
		}
		sandCorners += patches[i] && !L.scrub[i];
	}
	c.telemetry.measure("comb.scatter.sand-corners", sandCorners);
	layBeaches(L.terrain, t);
	const auto grass = pureTiles(L.terrain, t, GRASS);
	for (int i = 0; i < n; ++i)
	{
		L.town[i] &= grass[i];
		const int p = L.plot[i];
		if (p >= 0)
		{
			const int x = i % t.w, y = i / t.w;
			if (grass[i] && L.plot[t.at(x + 1, y)] == p && L.plot[t.at(x, y + 1)] == p &&
				L.plot[t.at(x + 1, y + 1)] == p)
			{
				// Leave an initial farm-side clearing for an inn and its upgrades. It
				// belongs to the renewable plot, so normal crop growth may reclaim it;
				// permanent mainland and forward room remain behind the sand cap.
				const double z = double(L.u(i) - fieldStart) * plots / (fieldEnd - fieldStart);
				const double f = z - int(z);
				const int bank = L.v(i) < V / 2 ? 0 : 1;
				const double d =
					bank == 0 ? L.v(i) - L.coast[0][L.u(i)] : L.coast[1][L.u(i)] - L.v(i);
				if (!(L.kind[p] == WHEAT && f >= 0.22 && f <= 0.46 && d >= 5))
					L.fields[p].push_back(i);
			}
			else
				L.plot[i] = -1;
		}
	}
	c.telemetry.measure("comb.peninsulas.actual", L.tips.size());
	c.telemetry.measure("comb.channel.radius", kChannelRadius);
	c.telemetry.choice("comb.orientation", L.horizontal ? "horizontal" : "vertical");
	return L;
}

bool populate(Game &game, GenerationContext &c, const Layout &L, const std::vector<int> &sites)
{
	const auto &t = L.t;
	const CombOptions o(c.request);
	writeUndermap(game.map, L.terrain);
	for (size_t k = 0; k < sites.size(); ++k)
		game.addTeam();
	if (!sites.empty() && !settleColonies(
							  game, c, "comb-starts", [&](int) { return L.town; }, [&](int k)
							  { return MapGeneratorPoint(sites[k] % t.w, sites[k] / t.w); }))
		return false;
	for (size_t p = 0; p < L.fields.size(); ++p)
	{
		auto tiles = L.fields[p];
		const bool food = L.kind[p] == WHEAT;
		if (food)
		{
			// Keep the guaranteed food in a compact patch beside the initial inn
			// clearing. Scattered singles can leave a reachable farm without a
			// usable feeding edge, especially with the abundance slider at zero.
			const int plots = int(L.fields.size()) / 4;
			const int section = int(p) % (2 * plots) / 2;
			const double u = 38 + (section + 0.22) * (L.length - 76) / plots - 2;
			const int bank = int(p) / (2 * plots);
			const double v = L.coast[bank][int(u)] + (bank == 0 ? 8 : -8);
			auto distance = [&](int i)
			{ return std::pow(L.u(i) - u, 2) + std::pow(L.v(i) - v, 2); };
			std::stable_sort(tiles.begin(), tiles.end(),
							 [&](int a, int b) { return distance(a) < distance(b); });
		}
		c.shuffle(tiles.begin() + std::min(int(tiles.size()), food ? kFoodFloor : 0), tiles.end(),
				  "comb-crops");
		const int base = int(tiles.size()) * (food ? 32 : 25) / 100;
		int count =
			std::min(int(tiles.size()), (food ? kFoodFloor : kWoodFloor) +
											int(scaledCount(base, food ? o.wheat : o.wood)));
		// Preserve the default density, then use the remaining plot capacity all
		// the way to 300%, rather than saturating the last two slider steps.
		if (food && o.wheat > 100)
		{
			const int normal = std::min(int(tiles.size()), kFoodFloor + base);
			count = normal + (int(tiles.size()) - normal) * (o.wheat - 100) / 200;
		}
		for (int j = 0; j < count; ++j)
			game.map.setResource(tiles[j] % t.w, tiles[j] / t.w, L.kind[p], 1);
		c.telemetry.measure(food ? "comb.wheat.planted" : "comb.wood.planted", count, int(p));
	}
	for (int type : {WHEAT, WOOD})
	{
		std::vector<int> seeds;
		for (int i = 0; i < t.size(); ++i)
			if (L.scrub[i] && L.scrubKind[i] == type && clearGround(game.map, i % t.w, i / t.w))
				seeds.push_back(i);
		c.shuffle(seeds.begin(), seeds.end(), "comb-scrub");
		const int amount = type == WHEAT ? o.wheat : o.wood;
		const int count = int(seeds.size()) * amount / 300;
		for (int k = 0; k < count; ++k)
			game.map.setResource(seeds[k] % t.w, seeds[k] / t.w, type, 1);
		c.telemetry.measure("comb.scatter.crop-seeds", count, type);
	}
	// All quarries and fruit remain on the mainland, away from town and supply roads.
	std::vector<int> stones, fruits, algae;
	for (int i = 0; i < t.size(); ++i)
	{
		const int u = L.u(i), v = L.v(i), bank = v < L.breadth / 2 ? 0 : 1;
		const double d = bank == 0 ? v - L.coast[0][u] : L.coast[1][u] - v;
		if (u >= 35 && u < L.length - 35 && d >= 45 && d <= 49 &&
			clearGround(game.map, i % t.w, i / t.w))
		{
			if (u % 9 < 3)
				stones.push_back(i);
			else if (u % 9 >= 6)
				fruits.push_back(i);
		}
		if (game.map.isWater(i % t.w, i / t.w) && (d < -2 && d > -9) && u >= 20 &&
			u < L.length - 20)
			algae.push_back(i);
	}
	c.shuffle(stones.begin(), stones.end(), "comb-stone");
	c.shuffle(fruits.begin(), fruits.end(), "comb-fruit");
	c.shuffle(algae.begin(), algae.end(), "comb-algae");
	auto plant = [&](const std::vector<int> &cells, int count, int type)
	{
		count = std::min(count, int(cells.size()));
		for (int j = 0; j < count; ++j)
			game.map.setResource(cells[j] % t.w, cells[j] / t.w,
								 type == CHERRY ? CHERRY + j % 3 : type, 1);
		c.telemetry.measure("comb.resource.planted", count, type);
	};
	plant(stones, int(stones.size()) / 8 + scaledCount(int(stones.size()) / 6, o.stone), STONE);
	plant(fruits, scaledCount(int(fruits.size()) / 6, o.fruit), CHERRY);
	plant(algae, scaledCount(int(algae.size()) / 12, o.algae), ALGA);
	return true;
}

// The nearest usable gathering face, with resources excluded from the walk itself.
std::vector<int> supplyWalk(const Map &map, const Torus &t, int type)
{
	const auto open = groundUnitTiles(map);
	std::vector<unsigned char> deposits(t.size(), 0);
	for (int i = 0; i < t.size(); ++i)
		deposits[i] = map.getResource(i % t.w, i / t.w).type == type;
	auto gathering = dilate(t, deposits, 1);
	for (int i = 0; i < t.size(); ++i)
		gathering[i] &= open[i];
	return stepsFrom(t, gathering, open);
}
std::string checkWorld(const Game &game, const Layout &L, GenerationContext *trace)
{
	const auto &t = L.t;
	const auto &map = game.map;
	for (int i = 0; i < t.size(); ++i)
		if (map.getUMTerrain(i % t.w, i / t.w) != L.terrain[i])
			return "The Comb terrain no longer matches its coast and channel design.";
	auto open = groundUnitTiles(map);
	const auto units = unitTilesByTeam(map, game.teamsCount());
	if (units.empty() || units.front().empty())
		return "The Comb has no starting workers.";
	const auto walk = stepsFrom(t, tileMask(t, units.front()), open);
	for (const auto &u : units)
		if (u.empty() || walk[u.front()] < 0)
			return "The Comb's shores are disconnected.";
	const auto building = potentialBuildingTiles(map);
	const auto anchors = buildAnchors(t, building, 4);

	const auto spread = cropSpreadEnvelope(map);
	const auto foodWalk = supplyWalk(map, t, WHEAT), stoneWalk = supplyWalk(map, t, STONE),
			   woodWalk = supplyWalk(map, t, WOOD);
	for (int i = 0; i < t.size(); ++i)
		if (spread.steps[i] >= 0 && !L.scrub[i] &&
			(L.town[i] || std::min(L.v(i) - L.coast[0][L.u(i)], L.coast[1][L.u(i)] - L.v(i)) > 15))
			return "The Comb's crops can spread into construction ground.";
	// Close the complete end regions, not just a nominal road. Eight-neighbour torus
	// floods catch both diagonal channel leaks and accidental routes over a map seam.
	for (int closure = 1; closure <= 3; ++closure)
	{
		auto blocked = open;
		for (int i = 0; i < t.size(); ++i)
			if (((closure & 1) && L.ends[0][i]) || ((closure & 2) && L.ends[1][i]))
				blocked[i] = 0;
		const auto components = connectedRegions(blocked, t.w, t.h, true, GridNeighbors::Eight);
		const int first = units.front().front();
		for (const auto &u : units)
		{
			const bool connected =
				components[first] >= 0 && components[first] == components[u.front()];
			if (closure != 3 && !connected)
				return "An end connection cannot carry the land circuit.";
			if (closure == 3 && L.side[first] != L.side[u.front()] && connected)
				return "The Comb has an unintended crossing between shores.";
		}
	}
	// Footprints depend on the shore, not the peninsula. Cache each shore once;
	// the interval test below keeps the entire footprint inside its own half-wave.
	std::vector<unsigned char> shore[2], shoreAnchors[2], shoreTowers[2];
	for (int bank = 0; bank < 2; ++bank)
	{
		shore[bank] = building;
		for (int i = 0; i < t.size(); ++i)
			shore[bank][i] &= L.side[i] == bank;
		shoreAnchors[bank] = buildAnchors(t, shore[bank], 4);
		shoreTowers[bank] = buildAnchors(t, shore[bank], 2);
	}
	for (size_t p = 0; p < L.tips.size(); ++p)
	{
		const auto tip = L.tips[p];
		int room = 0, fire = 0, landing = 0;
		const auto &own = shore[p % 2];
		const auto &ownAnchors = shoreAnchors[p % 2];
		const auto &ownTowers = shoreTowers[p % 2];
		const auto inPeninsula = [&](int i, int size)
		{ return L.u(i) >= L.edges[p] && L.u(i) + size - 1 < L.edges[p + 1]; };
		BuildingArrangement buildings;
		// Find a pair, not a maximal grid: a maximal fill can block a coastal sliver
		// which a player would simply leave empty. Both 4x4 buildings have service aisles.
		for (int dy = -28; dy <= 24 && buildings.footprints.empty(); dy += 4)
			for (int dx = -28; dx <= 24 && buildings.footprints.empty(); dx += 4)
				for (bool across : {false, true})
				{
					const int x = int(tip.x) + dx, y = int(tip.y) + dy;
					const int j = t.at(x + 1, y + 1),
							  other = t.at(x + 1 + (across ? 6 : 0), y + 1 + (across ? 0 : 6));
					const auto projects = [&](int i)
					{
						return p % 2 == 0 ? L.v(i) + 2 >= L.breadth / 2
										  : L.v(i) + 2 <= L.breadth / 2;
					};
					if (!ownAnchors[j] || !ownAnchors[other] || !inPeninsula(j, 4) ||
						!inPeninsula(other, 4) || !projects(j) || !projects(other))
						continue;
					const BuildingGrid grid{
						{x, y, x + (across ? 12 : 6), y + (across ? 6 : 12)}, 4, 4, 2, 1};
					auto pair = arrangeBuildingGrid(t, own, open, grid, units.front());
					if (pair.failure.empty() && pair.footprints.size() == 2)
					{
						buildings = std::move(pair);
						break;
					}
				}
		if (buildings.footprints.size() != 2)
			return "A Comb tip cannot fit two accessible forward buildings: " + std::to_string(p);
		// On long, low-count combs, the facing reach is farther along the
		// peninsula than its nose. Inspect that reach as well as the tip.
		const int along = std::max(20, (L.last - L.first) / int(L.tips.size()) / 2);
		const int rx = L.horizontal ? along : 20, ry = L.horizontal ? 20 : along;
		for (int dy = -ry; dy <= ry; ++dy)
			for (int dx = -rx; dx <= rx; ++dx)
			{
				const int i = t.at(int(tip.x) + dx, int(tip.y) + dy);
				if (L.u(i) < L.edges[p] || L.u(i) >= L.edges[p + 1])
					continue;
				room += ownAnchors[i] && inPeninsula(i, 4) && walk[i] >= 0;
				if (open[i] && L.side[i] == int(p % 2))
					for (const auto &step : kCardinalSteps)
						if (map.isWater(t.at(i % t.w + step[0], i / t.w + step[1]) % t.w,
										t.at(i % t.w + step[0], i / t.w + step[1]) / t.w))
						{
							++landing;
							break;
						}
				if (!ownTowers[i] || !inPeninsula(i, 2))
					continue;
				bool hits = false;
				for (int vy = -7; vy <= 8 && !hits; ++vy)
					for (int ux = -7; ux <= 8; ++ux)
					{
						const int j = t.at(i % t.w + ux, i / t.w + vy);
						if (open[j] && L.side[j] == 1 - int(p % 2))
						{
							hits = true;
							break;
						}
					}
				fire += hits;
			}
		const auto court = buildings.footprints.front();
		int foodDistance = 999, stoneDistance = 999;
		for (int y = court.y0 - 1; y <= court.y1; ++y)
			for (int x = court.x0 - 1; x <= court.x1; ++x)
			{
				if (x >= court.x0 && x < court.x1 && y >= court.y0 && y < court.y1)
					continue;
				const int i = t.at(x, y);
				if (foodWalk[i] >= 0)
					foodDistance = std::min(foodDistance, foodWalk[i]);
				if (stoneWalk[i] >= 0)
					stoneDistance = std::min(stoneDistance, stoneWalk[i]);
			}
		if (foodDistance > 160 || stoneDistance > 160)
			return "A Comb forward base is too far from food or ammunition stone.";
		if (!landing)
			return "A Comb peninsula has no accessible landing beach.";
		if (trace)
		{
			trace->telemetry.measure("comb.peninsula.food-walk", foodDistance, p);
			trace->telemetry.measure("comb.peninsula.stone-walk", stoneDistance, p);
			trace->telemetry.measure("comb.peninsula.landing-frontage", landing, p);
		}
		if (room < 8)
			return "A Comb peninsula lacks forward building space: " + std::to_string(p);
		if (fire < 6)
			return "A Comb peninsula lacks a mid-tier firing position: " + std::to_string(p);
		if (trace)
		{
			trace->telemetry.measure("comb.peninsula.build-sites", room, p);
			trace->telemetry.measure("comb.peninsula.separate-buildings",
									 buildings.footprints.size(), p);
			trace->telemetry.measure("comb.peninsula.firing-sites", fire, p);
		}
	}
	// Final gathering distances are measured from workers, never straight-line kit distance.
	for (size_t k = 0; k < units.size(); ++k)
	{
		const auto nearby = floodFrom(t, tileMask(t, units[k]), open, 24);
		int food = 999, wood = 999, room = 0;
		for (int i : units[k])
		{
			if (foodWalk[i] >= 0)
				food = std::min(food, foodWalk[i] + 1);
			if (woodWalk[i] >= 0)
				wood = std::min(wood, woodWalk[i] + 1);
		}
		for (int i = 0; i < t.size(); ++i)
			if (nearby.steps[i] >= 0)
				room += anchors[i];
		if (food > 24 || wood > 32 || room < 48)
			return "A Comb opening lacks reachable supplies or building room.";
		if (trace)
		{
			trace->telemetry.measure("comb.start.wheat-walk", food, k);
			trace->telemetry.measure("comb.start.wood-walk", wood, k);
			trace->telemetry.measure("comb.start.build-sites", room, k);
		}
	}
	return {};
}
bool generate(Game &game, GenerationContext &c)
{
	c.stage = "comb layout";
	const auto L = design(c.request, c);
	if (!L.failure.empty())
	{
		c.detail = L.failure;
		return false;
	}
	Game preview(nullptr);
	preview.map.setSize(c.request.wDec, c.request.hDec);
	if (!populate(preview, c, L, {}))
		return false;
	const auto anchors = buildAnchors(L.t, potentialBuildingTiles(preview.map), 6);
	const std::vector<std::vector<int>> supply{supplyWalk(preview.map, L.t, WHEAT),
											   supplyWalk(preview.map, L.t, WOOD)};
	const bool openingAtEnd = c.bounded("comb-two-player-end", 2) == 0;

	std::vector<std::vector<int>> proposals;
	// A small deterministic set of longitudinal offsets; actual legal sites are found
	// within each shore's landscape rather than stamping home pads into it.
	for (int trial = 0; trial < 4; ++trial)
	{
		std::vector<int> sites;
		for (int bank = 0; bank < 2; ++bank)
		{
			const int count = (c.request.nbTeams + (bank == 0)) / 2;
			for (int k = 0; k < count; ++k)
			{
				const double target =
					(c.request.nbTeams == 2 ? (openingAtEnd ? 66 : L.length - 67)
											: 44 + (L.length - 88) * (k + 0.5) / count) +
					(trial - 1.5) * 4;
				int best = -1;
				double cost = 1e30;
				for (int i = 0; i < L.t.size(); ++i)
				{
					if (!anchors[i] || !L.town[i] || L.side[i] != bank)
						continue;
					const int u = L.u(i), v = L.v(i);
					const double d = bank == 0 ? v - L.coast[0][u] : L.coast[1][u] - v;
					if (supply[0][i] < 0 || supply[1][i] < 0)
						continue;
					const double score = std::abs(u - target) * 0.8 + std::abs(d - 18) +
										 3 * std::max(supply[0][i], supply[1][i]);
					if (score < cost)
					{
						cost = score;
						best = i;
					}
				}
				if (best >= 0)
					sites.push_back(best);
			}
		}
		if (int(sites.size()) == c.request.nbTeams)
		{
			dealStarts(c, sites, "comb-deal");
			proposals.push_back(sites);
		}
	}
	const auto build = [&](Game &world, GenerationContext &probe, const std::vector<int> &sites)
	{ return populate(world, probe, L, sites); };
	c.stage = "comb settlements";
	auto choice = chooseScoredSettlements(
		c, proposals, build,
		[](const StartQualityReport &q)
		{
			return q.fairness >= 0.55 ? std::string()
									  : std::string("Comb opening economies are too unequal.");
		});
	if (choice.selected < 0)
	{
		c.detail = choice.failure;
		return false;
	}
	if (!build(game, c, choice.sites))
		return false;
	c.stage = "comb validation";
	c.detail = checkWorld(game, L, &c);
	return c.detail.empty();
}
std::string validateWorld(const Game &game, const GenerationContext &c)
{
	GenerationContext replay(c.request);
	const auto L = design(c.request, replay);
	if (!L.failure.empty())
		return L.failure;
	return checkWorld(game, L, nullptr);
}
} // namespace
CombOptions::CombOptions(const GenerationRequest &r)
	: peninsulas(r.option("peninsulas")), wheat(r.option("wheat-amount")),
	  wood(r.option("wood-amount")), stone(r.option("stone-amount")),
	  algae(r.option("algae-amount")), fruit(r.option("fruit-amount"))
{
}
GeneratorDefinition combDefinition()
{
	return {
		"comb",
		62,
		"The Comb",
		2,
		false,
		{{"peninsulas", "Peninsulas per shore", 2, 4, 1, 3, ControlGroup::Layout},
		 GeneratorControl::percentage("wheat-amount", "Wheat amount"),
		 GeneratorControl::percentage("wood-amount", "Wood amount"),
		 GeneratorControl::percentage("stone-amount", "Stone amount"),
		 GeneratorControl::percentage("algae-amount", "Algae amount"),
		 GeneratorControl::percentage("fruit-amount", "Fruit amount")},
		generate,
		true,
		validateRequest,
		validateWorld,
		{"terrain:natural", "feature:river", "feature:islands", "style:siege", "style:sprawling"}};
}
