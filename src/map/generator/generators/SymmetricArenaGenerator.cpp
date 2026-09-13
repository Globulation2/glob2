// SPDX-License-Identifier: GPL-3.0-or-later
#include "SymmetricArenaGenerator.h"
#include "Building.h"
#include "BuildingType.h"
#include "Game.h"
#include "GenerationContext.h"
#include "Geometry.h"
#include "Grid.h"
#include "GlobalContainer.h"
#include "HeightMap.h"
#include "Resources.h"
#include "Roads.h"
#include "Topology.h"
#include "Unit.h"
#include <algorithm>
#include <array>
#include <climits>
#include <cmath>
#include <cstdint>
#include <functional>
#include <map>
#include <queue>
#include <string>
#include <utility>
#include <vector>
using MapGeneration::kPi;
using MapGeneration::scaledCount;
using MapGeneration::scaledShare;

// A symmetric arena: an orchard of all three fruits and some stone on an island behind a moat,
// crossed by causeways, with the colonies spaced round it so that every colony's ground, route
// and resources are exactly the same as every other's.
//
// Symmetry is enforced by construction. Every decision is either a function of a tile's whole
// orbit (integer noise summed over the orbit, the integer squared distance from the centre) or
// is made once for colony 0 and stamped onto every image of it. Map::controlSand()'s in-place,
// row-order pass is replaced by the same rule applied to every corner at once, per-tile random
// clumps by deterministic growth, and the amounts the engine RNG gives each resource tile are
// equalised over its orbit. validateWorld then checks the invariance on the finished world.
//
// WHY IT PLAYS WELL (docs/map-generators/GAME_RULES_FOR_MAP_DESIGN.md). It is the tournament map:
// no colony can blame its start, because every colony's ground is an exact image of every other's.
// The prize in the middle is the strongest a map can offer, an orchard of all three fruits (an inn
// stocked with all three pulls hungry enemy units across) with stone among it, and the moat, which
// ground units cannot cross until they swim, funnels every early attack onto the causeways, the
// chokepoints worth holding. Each home has its own pond, because wheat and wood regrow only near
// water, so no colony has to leave home to keep its farmland alive. Only 2, 4 and 8 colonies are
// offered, because those are the counts the square's symmetries can serve exactly.
namespace
{

// Clear grass round every swarm; nothing is planted or dug inside it.
constexpr double kHomeRadius = 6.5;
// Each home's own pond, which keeps its starting farmland growing back.
constexpr double kPondDistance = 11.5, kPondRadius = 3.5;
// Open land kept between the moat and the nearest lake.
constexpr double kApron = 4.0;
// The smallest orchard island radius that always holds three orbits of groves (one lattice
// phase has them within five tiles of the centre), wherever the causeways come ashore.
constexpr double kMinimumCentre = 8.0;
// The nearest two homes may be.
constexpr double kMinimumSpacing = 28.0;
// Every home's starting kit, the same whatever the richness control says.
constexpr int kKitFarmland = 16, kKitStone = 4;

int wrap(int v, int n)
{
	v %= n;
	return v < 0 ? v + n : v;
}

// A symmetry of the map about its centre, as a signed permutation matrix acting on doubled
// centred coordinates: tile (x, y) is (2x + 1 - W, 2y + 1 - H) and undermap corner (u, v) is
// (2u - W, 2v - H). A tile's terrain comes from its four corners (Map::regenerateMap), and the
// same matrix maps a tile's corners onto its image's corners, so the tile rotation
// (x, y) -> (W-1-y, x) is the corner rotation (u, v) -> (W-v, u).
struct Isometry
{
	int a, b, c, d;
};

struct Symmetry
{
	int width = 0, height = 0;
	// elements[0] is the identity; colony k starts on elements[k]'s image of colony 0's home.
	std::vector<Isometry> elements;

	int order() const { return int(elements.size()); }
	int tile(int e, int x, int y) const
	{
		const Isometry &g = elements[e];
		const int X = 2 * x + 1 - width, Y = 2 * y + 1 - height;
		return wrap((g.c * X + g.d * Y + height - 1) / 2, height) * width +
			   wrap((g.a * X + g.b * Y + width - 1) / 2, width);
	}
	int corner(int e, int u, int v) const
	{
		const Isometry &g = elements[e];
		const int U = 2 * u - width, V = 2 * v - height;
		return wrap((g.c * U + g.d * V + height) / 2, height) * width +
			   wrap((g.a * U + g.b * V + width) / 2, width);
	}
	// The top-left tile of the image of a w x h footprint anchored at (x, y): the image of a
	// rectangle is a rectangle, so it is the image's lowest corner on each axis.
	std::pair<int, int> anchor(int e, int x, int y, int w, int h) const
	{
		const Isometry &g = elements[e];
		const int U0 = 2 * x - width, V0 = 2 * y - height;
		const int U1 = 2 * (x + w) - width, V1 = 2 * (y + h) - height;
		const int X = std::min(g.a * U0 + g.b * V0, g.a * U1 + g.b * V1);
		const int Y = std::min(g.c * U0 + g.d * V0, g.c * U1 + g.d * V1);
		return {wrap((X + width) / 2, width), wrap((Y + height) / 2, height)};
	}
};

// The symmetry that gives every colony identical ground: a half turn for two colonies, a
// quarter turn for four on a square map, the two mirrors for four on a rectangular map (where a
// quarter turn doesn't map the map onto itself), and all eight symmetries of a square for
// eight. Elements are listed in order round the centre, so neighbouring colonies are
// neighbouring team numbers. Empty for colony counts no symmetry serves.
Symmetry symmetryFor(int width, int height, int teams)
{
	Symmetry s;
	s.width = width;
	s.height = height;
	const bool square = width == height;
	if (teams == 2)
		s.elements = {{1, 0, 0, 1}, {-1, 0, 0, -1}};
	else if (teams == 4 && square)
		s.elements = {{1, 0, 0, 1}, {0, -1, 1, 0}, {-1, 0, 0, -1}, {0, 1, -1, 0}};
	else if (teams == 4)
		s.elements = {{1, 0, 0, 1}, {-1, 0, 0, 1}, {-1, 0, 0, -1}, {1, 0, 0, -1}};
	else if (teams == 8 && square)
		s.elements = {{1, 0, 0, 1},   {0, 1, 1, 0},   {0, -1, 1, 0}, {-1, 0, 0, 1},
					  {-1, 0, 0, -1}, {0, -1, -1, 0}, {0, 1, -1, 0}, {1, 0, 0, -1}};
	return s;
}

// Continuous positions relative to the map centre, in tiles. The centre is an undermap corner,
// so corners sit on integers and tile centres on half-integers.
struct Point
{
	double x, y;
};

Point cornerPoint(int width, int height, int u, int v)
{
	return {u - width / 2.0, v - height / 2.0};
}
Point tilePoint(int width, int height, int x, int y)
{
	return {x + 0.5 - width / 2.0, y + 0.5 - height / 2.0};
}
double wrapDelta(double d, double period)
{
	d = std::fmod(d, period);
	if (d >= period / 2)
		d -= period;
	else if (d < -period / 2)
		d += period;
	return d;
}
double torusDistance(int width, int height, Point p, Point q)
{
	return std::hypot(wrapDelta(p.x - q.x, width), wrapDelta(p.y - q.y, height));
}

// Squared distance from the centre in doubled units: an integer, so exactly equal across an
// orbit, and comparing it against a radius gives every member of the orbit the same answer.
std::int64_t cornerRadius2(int width, int height, int u, int v)
{
	const std::int64_t U = 2 * u - width, V = 2 * v - height;
	return U * U + V * V;
}
std::int64_t tileRadius2(int width, int height, int x, int y)
{
	const std::int64_t X = 2 * x + 1 - width, Y = 2 * y + 1 - height;
	return X * X + Y * Y;
}
bool within(std::int64_t radius2, double radius)
{
	// radius2 is in doubled coordinates, so the radius is doubled (squared, times 4) to compare:
	// all integer arithmetic on the squared side, so every tile in an orbit gets the same answer.
	return double(radius2) < 4.0 * radius * radius;
}

// The request's geometry.
struct Arena
{
	int width, height, teams, causeways;
	double centre, moat, causewayWidth;
	Symmetry symmetry;
};

Arena arenaFor(const GenerationRequest &r)
{
	const SymmetricArenaOptions o(r);
	Arena a;
	a.width = 1 << r.wDec;
	a.height = 1 << r.hDec;
	a.teams = r.nbTeams;
	a.causeways = o.causeways;
	// centre-size is a share of the shorter side, over a floor that keeps room for all three
	// fruits on the smallest maps.
	a.centre = 4.0 + o.centreSize / 100.0 * 0.75 * std::min(a.width, a.height);
	a.moat = o.moatWidth;
	a.causewayWidth = o.causewayWidth;
	a.symmetry = symmetryFor(a.width, a.height, a.teams);
	return a;
}

double imageAngle(const Isometry &g, double angle)
{
	const double x = std::cos(angle), y = std::sin(angle);
	return std::atan2(g.c * x + g.d * y, g.a * x + g.b * y);
}

// Colony 0's causeways, as directions from the centre: one straight towards its home, or two
// flanking it, each a quarter of the way to the nearest neighbour's direction.
std::vector<double> gateAngles(const Arena &a, Point home)
{
	const double angle = std::atan2(home.y, home.x);
	if (a.causeways < 2)
		return {angle};
	double gap = 2 * kPi;
	for (int e = 1; e < a.symmetry.order(); ++e)
	{
		double d = std::fmod(std::fabs(imageAngle(a.symmetry.elements[e], angle) - angle), 2 * kPi);
		gap = std::min(gap, std::min(d, 2 * kPi - d));
	}
	return {angle - gap / 4, angle + gap / 4};
}

// The smallest angle between two causeways once every image of colony 0's is drawn.
double gateSeparation(const Arena &a, const std::vector<double> &gates)
{
	std::vector<double> all;
	for (const Isometry &g : a.symmetry.elements)
		for (double angle : gates)
		{
			double image = std::fmod(imageAngle(g, angle), 2 * kPi);
			all.push_back(image < 0 ? image + 2 * kPi : image);
		}
	std::sort(all.begin(), all.end());
	double gap = all.front() + 2 * kPi - all.back();
	for (size_t i = 1; i < all.size(); ++i)
		gap = std::min(gap, all[i] - all[i - 1]);
	return gap;
}

struct Home
{
	int u, v;
	double spacing, radius;
};

enum class Fit
{
	Ok,
	Crowded,
	Causeways
};

// Every corner that could be colony 0's home. This depends only on the request, so validation
// and generation agree: the home's clear ground misses the moat, it is well inside half the
// shorter side (so the direction to the centre is unambiguous on the torus), it is far enough
// from every rival, and its causeways leave shore between them.
std::vector<Home> homeCandidates(const Arena &a, Fit &fit)
{
	const int w = a.width, h = a.height, shortSide = std::min(w, h);
	const int step = std::max(1, shortSide / 128);
	// Homes lie at least 4 tiles outside the moat plus their own clear disc, so a swarm never
	// stands on the moat's shore, and within 45% of the shorter side from the centre, so the way to
	// the centre is unambiguous on the torus (beyond half the side it would be shorter the other
	// way round).
	const double inner = a.centre + a.moat + kHomeRadius + 4, outer = 0.45 * shortSide;
	std::vector<Home> homes;
	bool spaced = false;
	for (int v = 0; v < h && a.symmetry.order() > 1; v += step)
		for (int u = 0; u < w; u += step)
		{
			const Point p = cornerPoint(w, h, u, v);
			const double radius = std::hypot(p.x, p.y);
			if (radius < inner || radius > outer)
				continue;
			double spacing = 1e9;
			for (int e = 1; e < a.symmetry.order(); ++e)
			{
				const int c = a.symmetry.corner(e, u, v);
				spacing =
					std::min(spacing, torusDistance(w, h, p, cornerPoint(w, h, c % w, c / w)));
			}
			if (spacing < kMinimumSpacing)
				continue;
			spaced = true;
			// Neighbouring causeways must leave at least 5 tiles of shore between them where they
			// land, or two colonies' causeways would merge into one wide bridge and the chokepoints
			// would be lost.
			if (gateSeparation(a, gateAngles(a, p)) * a.centre < a.causewayWidth + 5)
				continue;
			homes.push_back({u, v, spacing, radius});
		}
	fit = !homes.empty() ? Fit::Ok : spaced ? Fit::Causeways : Fit::Crowded;
	return homes;
}

// The best-spaced homes, and of those the ones nearest the centre, picked from at random.
Home chooseHome(const std::vector<Home> &homes, GenerationContext &context, int shortSide)
{
	double best = 0;
	for (const Home &home : homes)
		best = std::max(best, home.spacing);
	// Keep only homes within 10% (at least 2 tiles) of the widest spacing, then of those the ones
	// within 15% of the shorter side of the nearest to the centre: colonies as far apart as the map
	// allows, but not pushed out to the far corners, so the orchard stays a reachable prize. The
	// random pick among them varies maps without giving up either.
	const double floor = best - std::max(2.0, 0.1 * best);
	double nearest = 1e9;
	for (const Home &home : homes)
		if (home.spacing >= floor)
			nearest = std::min(nearest, home.radius);
	std::vector<Home> pool;
	for (const Home &home : homes)
		if (home.spacing >= floor && home.radius <= nearest + 0.15 * shortSide)
			pool.push_back(home);
	return pool[context.bounded("layout", pool.size())];
}

std::string validate(const GenerationRequest &r)
{
	if (r.nbTeams != 2 && r.nbTeams != 4 && r.nbTeams != 8)
		return "Symmetric arena needs 2, 4 or 8 colonies.";
	if (r.nbTeams == 8 && r.wDec != r.hDec)
		return "Eight colonies need a square map in the symmetric arena.";
	const Arena a = arenaFor(r);
	if (a.centre < kMinimumCentre)
		return "The centre is too small for an orchard on this map; use a bigger centre or a "
			   "bigger "
			   "map.";
	Fit fit;
	homeCandidates(a, fit);
	if (fit == Fit::Crowded)
		return "The arena leaves too little room for every colony; use a bigger map, a smaller "
			   "centre or a narrower moat.";
	if (fit == Fit::Causeways)
		return "The causeways are too close together; use fewer or narrower causeways, or a bigger "
			   "centre.";
	return "";
}

// Colony 0's choices; everything else is its images.
struct Layout
{
	int homeU, homeV;
	Point home, pond;
	double pondAngle;
	std::vector<double> gates;
	int orchardPhase, orchardPattern;
};

// Colony 0's feature stamped onto every image of it: an entry is set when any image of its
// tile (or corner) lies in the feature. A union doesn't depend on the order an orbit is
// visited in, so the result is exactly symmetric.
std::vector<unsigned char> stamp(const Symmetry &s, const std::vector<unsigned char> &feature,
								 bool corners)
{
	std::vector<unsigned char> result(feature.size(), 0);
	for (int y = 0; y < s.height; ++y)
		for (int x = 0; x < s.width; ++x)
			for (int e = 0; e < s.order(); ++e)
				if (feature[size_t(corners ? s.corner(e, x, y) : s.tile(e, x, y))])
				{
					result[size_t(y) * s.width + x] = 1;
					break;
				}
	return result;
}

// Plain noise summed over every orbit, as integers so the sum is exact in any order.
std::vector<int> orbitNoise(GenerationContext &context, const Symmetry &s,
							const std::string &stream, float smoothing, bool corners)
{
	const int w = s.width, h = s.height;
	HeightMap noise(w, h, context.stream(stream));
	noise.makePlain(smoothing);
	std::vector<int> raw(size_t(w) * h), summed(size_t(w) * h, 0);
	for (int y = 0; y < h; ++y)
		for (int x = 0; x < w; ++x)
			// Noise as integers (12 bits) so the sum over an orbit is exact: floating-point sums in
			// different orders could differ in the last bit and break the symmetry.
			raw[size_t(y) * w + x] = int(noise(x, y) * 4096);
	for (int y = 0; y < h; ++y)
		for (int x = 0; x < w; ++x)
			for (int e = 0; e < s.order(); ++e)
				summed[size_t(y) * w + x] +=
					raw[size_t(corners ? s.corner(e, x, y) : s.tile(e, x, y))];
	return summed;
}

// The share (0 to 1) of eligible entries with the highest (or lowest) values, as a mask. Ties
// at the cut-off are all kept, so the members of an orbit, which have equal values, are never
// split.
std::vector<unsigned char> topShare(const std::vector<int> &value,
									const std::vector<unsigned char> &eligible, double share,
									bool highest)
{
	std::vector<int> pool;
	for (size_t i = 0; i < value.size(); ++i)
		if (eligible[i])
			pool.push_back(value[i]);
	std::vector<unsigned char> mask(value.size(), 0);
	const size_t count =
		std::min(pool.size(), size_t(std::lround(std::max(0.0, share) * pool.size())));
	if (!count)
		return mask;
	std::sort(pool.begin(), pool.end());
	const int threshold = highest ? pool[pool.size() - count] : pool[count - 1];
	for (size_t i = 0; i < value.size(); ++i)
		mask[i] = eligible[i] && (highest ? value[i] >= threshold : value[i] <= threshold);
	return mask;
}

// Whether a point lies on one of colony 0's causeways, extended past the island's shore by
// `inner` and past the outer shore by `outer`.
bool onCauseway(const Arena &a, const Layout &l, Point p, double inner, double outer,
				double halfWidth)
{
	for (double angle : l.gates)
	{
		const double dx = std::cos(angle), dy = std::sin(angle);
		const double along = p.x * dx + p.y * dy, across = std::fabs(p.x * dy - p.y * dx);
		if (along >= a.centre - inner && along <= a.centre + a.moat + outer && across <= halfWidth)
			return true;
	}
	return false;
}

// The finished undermap, and the corner masks later steps need.
struct Terrain
{
	std::vector<unsigned char> undermap, moat, ponds, causeways, paths;
};

// A walking route for colony 0 from its home to the outer end of each of its causeways: the
// shortest one, where every lake corner crossed costs as much as a long detour, so it keeps to
// land wherever land will do; the moat and the ponds are never crossed. The route's images give
// every colony the same one. Lake water on and beside a route becomes land.
bool carvePaths(const Arena &a, const Layout &l, Terrain &t)
{
	constexpr int kStraight = 2, kDiagonal = 3, kFord = 60;
	const int w = a.width, h = a.height;
	const size_t n = size_t(w) * h;
	const MapGeneration::Torus torus(w, h);
	std::vector<unsigned char> route(n, 0);
	for (double angle : l.gates)
	{
		// The route to each causeway aims 3.5 tiles past the moat's outer edge, on the causeway's
		// landing, so the path ends on dry ground at the bridge rather than in the water.
		const double reach = a.centre + a.moat + 3.5;
		const int target = wrap(int(std::lround(h / 2.0 + reach * std::sin(angle))), h) * w +
						   wrap(int(std::lround(w / 2.0 + reach * std::cos(angle))), w);
		std::vector<unsigned char> goal(n, 0);
		goal[size_t(target)] = 1;
		const std::vector<int> walk = MapGeneration::cheapestWalk(
			torus, MapGeneration::GridNeighbors::Eight, {l.homeV * w + l.homeU}, goal,
			[&](int, int j, int dx, int dy)
			{
				const bool water = t.undermap[size_t(j)] == WATER;
				if (water && (t.moat[size_t(j)] || t.ponds[size_t(j)]))
					return -1;
				return (dx && dy ? kDiagonal : kStraight) + (water ? kFord : 0);
			});
		if (walk.empty())
			return false;
		for (int i : walk)
			route[size_t(i)] = 1;
	}
	t.paths = stamp(a.symmetry, route, true);
	for (int v = 0; v < h; ++v)
		for (int u = 0; u < w; ++u)
		{
			if (!t.paths[size_t(v) * w + u])
				continue;
			for (int dy = -1; dy <= 1; ++dy)
				for (int dx = -1; dx <= 1; ++dx)
				{
					const size_t j = size_t(wrap(v + dy, h)) * w + wrap(u + dx, w);
					if (t.undermap[j] == WATER && !t.moat[j] && !t.ponds[j])
						t.undermap[j] = GRASS;
				}
		}
	return true;
}

// The 8-connected groups of set entries in a mask, on the torus. Which entries share a group is
// a matter of geometry alone, so every symmetry maps groups onto groups of the same size.
std::vector<std::vector<int>> groupsOf(const std::vector<unsigned char> &mask, int w, int h)
{
	const std::vector<int> label =
		MapGeneration::connectedRegions(mask, w, h, true, MapGeneration::GridNeighbors::Eight);
	std::vector<std::vector<int>> groups;
	for (size_t i = 0; i < label.size(); ++i)
		if (label[i] >= 0)
		{
			if (label[i] >= int(groups.size()))
				groups.resize(label[i] + 1);
			groups[label[i]].push_back(int(i));
		}
	return groups;
}

bool buildTerrain(Map &map, GenerationContext &context, const Arena &a, const Layout &l,
				  const SymmetricArenaOptions &o, Terrain &t)
{
	// A lake of fewer corners than this is a patch of shoreline with no open water in it.
	constexpr size_t kSmallestLake = 16;
	context.stage = "arena terrain";
	const int w = a.width, h = a.height;
	const size_t n = size_t(w) * h;
	const Symmetry &s = a.symmetry;
	// Lakes keep clear of every home and of its pond's banks, where the starting kit grows.
	std::vector<unsigned char> homeDisc(n, 0), homeZone(n, 0), pond(n, 0), gate(n, 0);
	for (int v = 0; v < h; ++v)
		for (int u = 0; u < w; ++u)
		{
			const size_t i = size_t(v) * w + u;
			const Point p = cornerPoint(w, h, u, v);
			const double toHome = torusDistance(w, h, p, l.home);
			const double toPond = torusDistance(w, h, p, l.pond);
			homeDisc[i] = toHome <= kHomeRadius;
			// Lakes keep out of a zone 4 tiles round the home disc and 7 round its pond, so the
			// pond's farmland and the swarm's workers have land to use.
			homeZone[i] = toHome <= kHomeRadius + 4 || toPond <= kPondRadius + 7;
			pond[i] = toPond <= kPondRadius;
			gate[i] = onCauseway(a, l, p, 1.5, 1.5, a.causewayWidth / 2);
		}
	homeDisc = stamp(s, homeDisc, true);
	homeZone = stamp(s, homeZone, true);
	t.ponds = stamp(s, pond, true);
	// Without a moat the orchard island joins the land round it, and no causeway is needed; the
	// moat's width still spaces the homes.
	t.causeways = o.moat ? stamp(s, gate, true) : std::vector<unsigned char>(n, 0);

	t.moat.assign(n, 0);
	std::vector<unsigned char> lakeEligible(n, 0);
	for (int v = 0; v < h; ++v)
		for (int u = 0; u < w; ++u)
		{
			const size_t i = size_t(v) * w + u;
			const std::int64_t r2 = cornerRadius2(w, h, u, v);
			t.moat[i] = o.moat && !within(r2, a.centre) && within(r2, a.centre + a.moat);
			lakeEligible[i] = !within(r2, a.centre + a.moat + kApron) && !homeZone[i];
		}
	// Capped so big maps get more lakes rather than bigger ones: more shore, more farmland.
	const float smoothing = std::min(32.0f, std::max(12.0f, std::min(w, h) / 10.0f));
	std::vector<unsigned char> lakes =
		topShare(orbitNoise(context, s, "arena-lakes", smoothing, true), lakeEligible,
				 o.lakes / 100.0, false);
	for (const auto &group : groupsOf(lakes, w, h))
		if (group.size() < kSmallestLake)
			for (int i : group)
				lakes[size_t(i)] = 0;

	// Later layers win: lakes and ponds, then clear home ground, the moat, and causeways of sand.
	t.undermap.assign(n, GRASS);
	for (size_t i = 0; i < n; ++i)
	{
		if (lakes[i] || t.ponds[i])
			t.undermap[i] = WATER;
		if (homeDisc[i])
			t.undermap[i] = GRASS;
		if (t.moat[i])
			t.undermap[i] = WATER;
		if (t.causeways[i])
			t.undermap[i] = SAND;
	}
	if (!carvePaths(a, l, t))
	{
		context.detail = "no route from colony 0's home to its causeway";
		return false;
	}

	// Map::controlSand()'s rule - grass touching water becomes sand - applied to every corner at
	// once rather than in place in row order, which is what keeps it symmetric. Water is left
	// alone, so no moat, pond or channel silts up.
	std::vector<unsigned char> sanded(t.undermap);
	for (int v = 0; v < h; ++v)
		for (int u = 0; u < w; ++u)
		{
			const size_t i = size_t(v) * w + u;
			if (t.undermap[i] != GRASS)
				continue;
			for (int dy = -1; dy <= 1 && sanded[i] == GRASS; ++dy)
				for (int dx = -1; dx <= 1; ++dx)
					if (t.undermap[size_t(wrap(v + dy, h)) * w + wrap(u + dx, w)] == WATER)
					{
						sanded[i] = SAND;
						break;
					}
		}
	t.undermap.swap(sanded);
	for (int v = 0; v < h; ++v)
		for (int u = 0; u < w; ++u)
			map.setUMTerrain(u, v, TerrainType(t.undermap[size_t(v) * w + u]));
	map.rebuildTerrain();
	return true;
}

// Every swarm, then every worker, from colony 0's choices mapped by each colony's symmetry:
// its footprint centred on its home corner (the image of a footprint needs its own top-left
// anchor, since a turn or mirror moves which corner is top-left), and a random choice of the
// free tiles touching it.
bool placeColonies(Game &game, GenerationContext &context, const Arena &a, const Layout &l)
{
	context.stage = "settlement";
	Map &map = game.map;
	const Symmetry &s = a.symmetry;
	const int w = a.width, h = a.height;
	const int type = globalContainer->buildingsTypes.getTypeNum("swarm", 0, false);
	const BuildingType *swarm = globalContainer->buildingsTypes.get(type);
	if (!swarm)
	{
		context.detail = "missing swarm type";
		return false;
	}
	const int x0 = l.homeU - swarm->width / 2, y0 = l.homeV - swarm->height / 2;
	std::vector<Building *> buildings;
	for (int team = 0; team < a.teams; ++team)
	{
		const auto [x, y] = s.anchor(team, x0, y0, swarm->width, swarm->height);
		Building *building = game.checkRoomForBuilding(x, y, swarm, team, false)
								 ? game.addBuilding(x, y, type, team, 1, 0)
								 : nullptr;
		if (!building)
		{
			context.detail = "Colony " + std::to_string(team) + ": the swarm does not fit its home";
			return false;
		}
		buildings.push_back(building);
	}
	std::vector<std::pair<int, int>> ring;
	for (int dy = -1; dy <= swarm->height; ++dy)
		for (int dx = -1; dx <= swarm->width; ++dx)
		{
			if (dx >= 0 && dx < swarm->width && dy >= 0 && dy < swarm->height)
				continue;
			const int x = wrap(x0 + dx, w), y = wrap(y0 + dy, h);
			if (map.isFreeForGroundUnit(x, y, false, Team::teamNumberToMask(0)))
				ring.emplace_back(x, y);
		}
	const int workers = context.request.nbWorkers;
	if (int(ring.size()) < workers)
	{
		context.detail = "need " + std::to_string(workers) + " worker tiles; found " +
						 std::to_string(ring.size());
		return false;
	}
	for (int i = 0; i < workers; ++i)
		std::swap(ring[size_t(i)], ring[size_t(i) + context.bounded("starts", ring.size() - i)]);
	for (int team = 0; team < a.teams; ++team)
	{
		for (int i = 0; i < workers; ++i)
		{
			const int tile = s.tile(team, ring[size_t(i)].first, ring[size_t(i)].second);
			if (!game.addUnit(tile % w, tile / w, team, WORKER, 0, 0, 0, 0))
			{
				context.detail = "Colony " + std::to_string(team) + ": worker placement failed";
				return false;
			}
		}
		Team *colony = game.teams[team];
		colony->startPosX = buildings[size_t(team)]->posX;
		colony->startPosY = buildings[size_t(team)]->posY;
		colony->startPosSet = Team::START_POS_FROM_SWARM;
		context.bootX[team] = colony->startPosX;
		context.bootY[team] = colony->startPosY;
		colony->createLists();
	}
	return true;
}

bool furnish(Game &game, GenerationContext &context, const Arena &a, const Layout &l,
			 const SymmetricArenaOptions &o, const Terrain &t)
{
	context.stage = "arena resources";
	Map &map = game.map;
	const Symmetry &s = a.symmetry;
	const int w = a.width, h = a.height;
	const size_t n = size_t(w) * h;
	const double richness = o.richness / 100.0;

	// Ground kept free of deposits: every home's clear ground, both approaches to every causeway,
	// and a lane along every walking route.
	std::vector<unsigned char> grass(n), water(n), land(n), homeClear(n), landing(n), blocked(n);
	for (int y = 0; y < h; ++y)
		for (int x = 0; x < w; ++x)
		{
			const size_t i = size_t(y) * w + x;
			grass[i] = map.isGrass(x, y) && map.getBuilding(x, y) == NOGBID &&
					   map.getGroundUnit(x, y) == NOGUID;
			water[i] = map.isWater(x, y);
			land[i] = !water[i];
			const Point p = tilePoint(w, h, x, y);
			// Nothing is planted within 1.5 tiles past the home's clear disc, nor within 3 to 4
			// tiles of where a causeway lands (2 tiles to each side beyond its width), so neither
			// the swarm nor a bridge can be walled in by deposits.
			homeClear[i] = torusDistance(w, h, p, l.home) <= kHomeRadius + 1.5;
			landing[i] = onCauseway(a, l, p, 3.0, 4.0, a.causewayWidth / 2 + 2);
		}
	homeClear = stamp(s, homeClear, false);
	landing = stamp(s, landing, false);
	const auto onPath = [&](int u, int v)
	{ return t.paths[size_t(wrap(v, h)) * w + wrap(u, w)] != 0; };
	for (int y = 0; y < h; ++y)
		for (int x = 0; x < w; ++x)
		{
			const size_t i = size_t(y) * w + x;
			blocked[i] = homeClear[i] || landing[i] || onPath(x, y) || onPath(x + 1, y) ||
						 onPath(x, y + 1) || onPath(x + 1, y + 1);
		}
	const std::vector<int> waterSteps = MapGeneration::stepsFrom(MapGeneration::Torus{w, h}, water),
						   landSteps = MapGeneration::stepsFrom(MapGeneration::Torus{w, h}, land);
	const float coarse = std::min(24.0f, std::max(8.0f, std::min(w, h) / 10.0f));
	std::vector<int> plan(n, -1);

	// The ambient layer beyond the open apron round the moat: farmland on lake and pond shores
	// (wheat to wood 2:1), stone outcrops away from the water, and algae off every shore.
	std::vector<unsigned char> farm(n, 0), rock(n, 0), alga(n, 0);
	for (int y = 0; y < h; ++y)
		for (int x = 0; x < w; ++x)
		{
			const size_t i = size_t(y) * w + x;
			const bool open = grass[i] && !blocked[i] &&
							  !within(tileRadius2(w, h, x, y), a.centre + a.moat + kApron + 2);
			// Farmland 2 to 7 steps from water, where it regrows and off the beach; stone at least
			// 3 steps from water, where it takes no farmland; algae at least 2 steps out from land,
			// off the shore.
			farm[i] = open && waterSteps[i] >= 2 && waterSteps[i] <= 7;
			rock[i] = open && waterSteps[i] >= 3;
			alga[i] = water[i] && landSteps[i] >= 2;
		}
	// The amount controls scale each layer on top of richness: wheat and wood their shares of the
	// farmland (2:1 at the defaults, which use the shares exactly as they were).
	const bool farmDefault = o.wheat == 100 && o.wood == 100;
	// At richness 1 farmland takes 30% of its eligible ground, two thirds wheat and a third wood;
	// stone 1.2% of its ground (a few outcrops, since stone never runs out) and algae 10% of its
	// water. Farmland is capped at 90% so a very rich map still leaves room to walk.
	const double wheatShare = scaledShare(0.3 * richness * 2 / 3, o.wheat);
	const double woodShare = scaledShare(0.3 * richness / 3, o.wood);
	const double farmShare =
		farmDefault ? std::min(0.9, 0.3 * richness) : std::min(0.9, wheatShare + woodShare);
	const double woodFraction = farmDefault ? 1.0 / 3
								: wheatShare + woodShare > 0.0
									? woodShare / (wheatShare + woodShare)
									: 0.0;
	const auto farmland =
		topShare(orbitNoise(context, s, "arena-farmland", coarse, false), farm, farmShare, true);
	const auto wood =
		topShare(orbitNoise(context, s, "arena-split", 6.0f, false), farmland, woodFraction, true);
	const auto outcrops = topShare(orbitNoise(context, s, "arena-stone", coarse, false), rock,
								   scaledShare(0.012 * richness, o.stone), true);
	const auto algae = topShare(orbitNoise(context, s, "arena-algae", coarse, false), alga,
								scaledShare(0.1 * richness, o.algae), true);
	for (size_t i = 0; i < n; ++i)
	{
		if (farmland[i])
			plan[i] = wood[i] ? WOOD : CORN;
		if (outcrops[i])
			plan[i] = STONE;
		if (algae[i])
			plan[i] = ALGA;
	}

	// The orchard: two-by-two groves on a four-tile lattice squared up to the centre, so every
	// grove has walkway at least two tiles wide on all four sides and every tree can be picked.
	// Groves are dealt out an orbit at a time - the groves the symmetries carry onto each other -
	// from the centre outwards: cherries, oranges and prunes in turn from a seeded start, with
	// about a fifth of the orbits turned to stone but never fewer than three left for fruit. The
	// lattice phase comes from the seed; if it leaves a small centre fewer than three orbits, the
	// other phases are tried.
	struct Orbit
	{
		std::int64_t radius2 = INT64_MAX;
		int key = 0, value = INT_MIN;
		bool stone = false;
		std::vector<int> tiles;
	};
	const std::vector<int> orchardNoise = orbitNoise(context, s, "arena-orchard", 4.0f, false);
	bool planted = false;
	for (int attempt = 0; attempt < 4 && !planted; ++attempt)
	{
		// Phase 1 gives even walkways, 2 an avenue along each axis, 3 a grove on the centre; 0
		// would join the four groves round the centre into one block with unpickable middle trees.
		const int phase = attempt == 0 ? l.orchardPhase : attempt;
		// Whether a tile's doubled centred coordinate falls in a grove row. It depends only on the
		// distance from the axis, so turns and mirrors keep it.
		const auto inGrove = [phase](int doubled)
		{ return ((std::abs(doubled) - 1) / 2 + 4 - phase) % 4 < 2; };
		std::vector<unsigned char> trees(n, 0);
		for (int y = 0; y < h; ++y)
			for (int x = 0; x < w; ++x)
			{
				const size_t i = size_t(y) * w + x;
				trees[i] = grass[i] && !blocked[i] &&
						   within(tileRadius2(w, h, x, y), a.centre - 2) &&
						   inGrove(2 * x + 1 - w) && inGrove(2 * y + 1 - h);
			}
		// An orbit is named by the lowest tile index any symmetry carries one of its tiles to, and
		// ranked by its nearest tile to the centre and its highest noise value; all three are the
		// same whichever of its groves they are read from.
		std::map<int, Orbit> orbits;
		for (const auto &group : groupsOf(trees, w, h))
		{
			int key = INT_MAX, value = INT_MIN;
			std::int64_t radius2 = INT64_MAX;
			for (int i : group)
			{
				value = std::max(value, orchardNoise[size_t(i)]);
				radius2 = std::min(radius2, tileRadius2(w, h, i % w, i / w));
				for (int e = 0; e < s.order(); ++e)
					key = std::min(key, s.tile(e, i % w, i / w));
			}
			Orbit &orbit = orbits[key];
			orbit.key = key;
			orbit.radius2 = radius2;
			orbit.value = value;
			orbit.tiles.insert(orbit.tiles.end(), group.begin(), group.end());
		}
		if (orbits.size() < 3)
			continue;
		std::vector<Orbit *> ranked;
		for (auto &entry : orbits)
			ranked.push_back(&entry.second);
		std::sort(ranked.begin(), ranked.end(), [](const Orbit *p, const Orbit *q)
				  { return p->value != q->value ? p->value > q->value : p->key < q->key; });
		const size_t stones =
			o.orchardStone
				// A fifth of the orchard's orbits become stone, taking the highest noise values,
				// but never so many that fewer than three orbits are left for fruit: the orchard
				// must hold every kind.
				? std::min(ranked.size() - 3, size_t(std::lround(0.2 * double(ranked.size()))))
				: 0;
		for (size_t k = 0; k < stones; ++k)
			ranked[k]->stone = true;
		std::sort(ranked.begin(), ranked.end(), [](const Orbit *p, const Orbit *q)
				  { return p->radius2 != q->radius2 ? p->radius2 < q->radius2 : p->key < q->key; });
		// The fruit amount keeps that share of the fruit orbits nearest the centre, never fewer
		// than three so every fruit stays; the rest are left as open grass.
		const size_t fruitOrbits = ranked.size() - stones;
		const size_t keptFruit =
			std::clamp<size_t>(size_t(scaledCount(std::int64_t(fruitOrbits), o.fruit)),
							   std::min<size_t>(3, fruitOrbits), fruitOrbits);
		int fruit = l.orchardPattern;
		size_t fruitPlanted = 0;
		for (const Orbit *orbit : ranked)
		{
			if (!orbit->stone && fruitPlanted++ >= keptFruit)
				continue;
			const int type = orbit->stone ? STONE : CHERRY + fruit++ % 3;
			for (int i : orbit->tiles)
				plan[size_t(i)] = type;
		}
		planted = true;
	}
	if (!planted)
	{
		context.detail = "the centre is too small to hold all three fruits";
		return false;
	}

	// Colony 0's starting kit, grown tile by tile from seeds on its pond's banks - wheat on one
	// side, wood on the other - with a little stone round the side of its home. Stamped over the
	// ambient layer, with a fixed priority wherever two colonies' kits overlap.
	std::vector<int> kit(n, -1);
	std::vector<unsigned char> kitGround(n, 0);
	for (int y = 0; y < h; ++y)
		for (int x = 0; x < w; ++x)
		{
			const size_t i = size_t(y) * w + x;
			// The kit may use ground up to 12 tiles past the home's clear disc and at least 2 steps
			// from water.
			kitGround[i] = grass[i] && !blocked[i] && waterSteps[i] >= 2 &&
						   torusDistance(w, h, tilePoint(w, h, x, y), l.home) <= kHomeRadius + 12;
		}
	// A clump grows outward from the free kit ground nearest its target; if that pocket fills
	// first, it carries on from the next nearest free tile, so a cramped bank still gets its kit.
	const auto grow = [&](Point target, int type, int size)
	{
		std::vector<std::pair<double, int>> nearest;
		for (size_t i = 0; i < n; ++i)
			if (kitGround[i] && kit[i] < 0)
				nearest.emplace_back(
					torusDistance(w, h, tilePoint(w, h, int(i % w), int(i / w)), target), int(i));
		std::sort(nearest.begin(), nearest.end());
		static const int steps[4][2] = {{1, 0}, {0, 1}, {-1, 0}, {0, -1}};
		std::vector<unsigned char> queued(n, 0);
		int placed = 0;
		for (const auto &candidate : nearest)
		{
			if (placed >= size)
				break;
			if (queued[size_t(candidate.second)])
				continue;
			std::vector<int> queue{candidate.second};
			queued[size_t(candidate.second)] = 1;
			for (size_t head = 0; head < queue.size() && placed < size; ++head, ++placed)
			{
				const int i = queue[head], x = i % w, y = i / w;
				kit[size_t(i)] = type;
				for (const auto &step : steps)
				{
					const size_t j = size_t(wrap(y + step[1], h)) * w + wrap(x + step[0], w);
					if (!queued[j] && kitGround[j] && kit[j] < 0)
					{
						queued[j] = 1;
						queue.push_back(int(j));
					}
				}
			}
		}
		return placed;
	};
	const Point side{-std::sin(l.pondAngle), std::cos(l.pondAngle)};
	// Wheat and wood 4.5 tiles to either side of the pond, where they regrow and the way from swarm
	// to pond stays open; stone a third of a turn round from the pond, just outside the clear disc,
	// away from the farmland.
	const double stoneAngle = l.pondAngle + 2 * kPi / 3;
	const int wheatKit =
		grow({l.pond.x + 4.5 * side.x, l.pond.y + 4.5 * side.y}, CORN, kKitFarmland);
	const int woodKit =
		grow({l.pond.x - 4.5 * side.x, l.pond.y - 4.5 * side.y}, WOOD, kKitFarmland);
	const int stoneKit = grow({l.home.x + (kHomeRadius + 3) * std::cos(stoneAngle),
							   l.home.y + (kHomeRadius + 3) * std::sin(stoneAngle)},
							  STONE, kKitStone);
	if (wheatKit < kKitFarmland / 2 || woodKit < kKitFarmland / 2 || stoneKit < 1)
	{
		context.detail = "colony 0's starting kit has no room beside its pond";
		return false;
	}
	const auto rank = [](int type)
	{
		return type == STONE ? 3 : type == WOOD ? 2 : type == CORN ? 1 : 0;
	};
	for (int y = 0; y < h; ++y)
		for (int x = 0; x < w; ++x)
		{
			int best = -1;
			for (int e = 0; e < s.order(); ++e)
			{
				const int type = kit[size_t(s.tile(e, x, y))];
				if (rank(type) > rank(best))
					best = type;
			}
			if (best >= 0)
				plan[size_t(y) * w + x] = best;
		}

	for (int y = 0; y < h; ++y)
		for (int x = 0; x < w; ++x)
			if (plan[size_t(y) * w + x] >= 0)
				map.setResource(x, y, plan[size_t(y) * w + x], 1);
	// The engine draws each deposit's amount and look from the gameplay RNG. Every orbit takes
	// its lowest-indexed tile's, so deposits are symmetric in size as well as type.
	for (int y = 0; y < h; ++y)
		for (int x = 0; x < w; ++x)
		{
			const int i = y * w + x;
			int first = i;
			for (int e = 1; e < s.order(); ++e)
				first = std::min(first, s.tile(e, x, y));
			if (first == i)
				continue;
			const Resource source = map.getResource(size_t(first));
			Resource &target = map.getResource(size_t(i));
			if (source.type != target.type)
			{
				context.detail = "deposits differ across the orbit of (" + std::to_string(x) +
								 ", " + std::to_string(y) + ")";
				return false;
			}
			target = source;
		}
	return true;
}

bool generate(Game &game, GenerationContext &context)
{
	context.stage = "arena layout";
	const SymmetricArenaOptions o(context.request);
	const Arena a = arenaFor(context.request);
	Map &map = game.map;
	map.makeHomogenMap(WATER);
	for (int i = 0; i < a.teams; ++i)
		game.addTeam();
	if (a.teams < 2 || a.symmetry.order() != a.teams)
	{
		context.detail = "no symmetry gives this many colonies the same ground";
		return false;
	}
	Fit fit;
	const std::vector<Home> homes = homeCandidates(a, fit);
	if (homes.empty())
	{
		context.detail = "no home position fits these settings";
		return false;
	}
	const Home home = chooseHome(homes, context, std::min(a.width, a.height));
	Layout l;
	l.homeU = home.u;
	l.homeV = home.v;
	l.home = cornerPoint(a.width, a.height, home.u, home.v);
	l.gates = gateAngles(a, l.home);
	// The pond lies away from the centre, turned up to 60 degrees either way.
	l.pondAngle =
		std::atan2(l.home.y, l.home.x) + (int(context.bounded("layout", 121)) - 60) * kPi / 180;
	l.pond = {l.home.x + kPondDistance * std::cos(l.pondAngle),
			  l.home.y + kPondDistance * std::sin(l.pondAngle)};
	l.orchardPhase = 1 + int(context.bounded("layout", 3));
	l.orchardPattern = int(context.bounded("layout", 3));
	Terrain t;
	return buildTerrain(map, context, a, l, o, t) && placeColonies(game, context, a, l) &&
		   furnish(game, context, a, l, o, t);
}

// The class of a tile's graphic (Map::lookup): grass, grass and sand, sand, sand and water,
// water. It depends only on which terrains a tile's corners hold, not on which corner holds
// which, so it is unchanged by any symmetry.
int terrainClass(Uint16 terrain)
{
	return terrain < 16 ? 0 : terrain < 128 ? 1 : terrain < 144 ? 2 : terrain < 256 ? 3 : 4;
}

std::string at(int x, int y)
{
	return " at (" + std::to_string(x) + ", " + std::to_string(y) + ")";
}

// The arena's guarantees, checked on the finished world rather than trusted: every symmetry of
// the request maps undermap corners, tile terrain, deposits (type and amount), buildings and
// units onto themselves with the colonies permuted one to one; those permutations carry any
// colony onto any other; and every colony walks to wheat, wood, each fruit and open ground in
// the orchard, all in the same number of steps. Water, buildings and every resource block the
// walk; units don't, since they move.
std::string validateWorld(const Game &game, const GenerationContext &context)
{
	const Map &map = game.map;
	const int w = map.getW(), h = map.getH(), teams = context.request.nbTeams;
	const Arena a = arenaFor(context.request);
	const Symmetry &s = a.symmetry;
	if (s.order() != teams)
		return "No symmetry gives " + std::to_string(teams) + " colonies the same ground.";
	std::vector<std::vector<int>> permutations;
	for (int e = 1; e < s.order(); ++e)
	{
		const std::string under = " is not symmetric under symmetry " + std::to_string(e) + ".";
		std::vector<int> image(size_t(teams), -1);
		const auto relate = [&](int from, int to)
		{
			if (from < 0 || from >= teams || to < 0 || to >= teams)
				return false;
			if (image[size_t(from)] < 0)
				image[size_t(from)] = to;
			return image[size_t(from)] == to;
		};
		for (int v = 0; v < h; ++v)
			for (int u = 0; u < w; ++u)
			{
				const int c = s.corner(e, u, v);
				if (map.getUMTerrain(u, v) != map.getUMTerrain(c % w, c / w))
					return "Undermap corner" + at(u, v) + under;
			}
		for (int y = 0; y < h; ++y)
			for (int x = 0; x < w; ++x)
			{
				const int q = s.tile(e, x, y), qx = q % w, qy = q / w;
				if (terrainClass(map.getTerrain(x, y)) != terrainClass(map.getTerrain(qx, qy)))
					return "Terrain" + at(x, y) + under;
				const Resource &ra = map.getResource(x, y), &rb = map.getResource(qx, qy);
				if (ra.type != rb.type || ra.amount != rb.amount)
					return "Deposit" + at(x, y) + under;
				const Uint16 ba = map.getBuilding(x, y), bb = map.getBuilding(qx, qy);
				if ((ba == NOGBID) != (bb == NOGBID))
					return "Building" + at(x, y) + under;
				if (ba != NOGBID && (!relate(Building::GIDtoTeam(ba), Building::GIDtoTeam(bb)) ||
									 game.teams[Building::GIDtoTeam(ba)]
											 ->myBuildings[Building::GIDtoID(ba)]
											 ->type != game.teams[Building::GIDtoTeam(bb)]
														   ->myBuildings[Building::GIDtoID(bb)]
														   ->type))
					return "Building" + at(x, y) + under;
				const Uint16 ua = map.getGroundUnit(x, y), ub = map.getGroundUnit(qx, qy);
				if ((ua == NOGUID) != (ub == NOGUID))
					return "Unit" + at(x, y) + under;
				if (ua != NOGUID &&
					(!relate(Unit::GIDtoTeam(ua), Unit::GIDtoTeam(ub)) ||
					 game.teams[Unit::GIDtoTeam(ua)]->myUnits[Unit::GIDtoID(ua)]->typeNum !=
						 game.teams[Unit::GIDtoTeam(ub)]->myUnits[Unit::GIDtoID(ub)]->typeNum))
					return "Unit" + at(x, y) + under;
			}
		std::vector<unsigned char> hit(size_t(teams), 0);
		for (int team = 0; team < teams; ++team)
		{
			const int to = image[size_t(team)];
			if (to < 0 || hit[size_t(to)])
				return "Symmetry " + std::to_string(e) + " does not map colonies one to one.";
			hit[size_t(to)] = 1;
		}
		permutations.push_back(image);
	}
	std::vector<int> carried{0};
	std::vector<unsigned char> seen(size_t(teams), 0);
	seen[0] = 1;
	for (size_t head = 0; head < carried.size(); ++head)
		for (const auto &image : permutations)
			if (!seen[size_t(image[size_t(carried[head])])])
			{
				seen[size_t(image[size_t(carried[head])])] = 1;
				carried.push_back(image[size_t(carried[head])]);
			}
	if (int(carried.size()) != teams)
		return "The symmetries do not carry colony 0 onto every other colony.";

	static const char *const targets[] = {"wheat",   "wood",   "cherries",
										  "oranges", "prunes", "the orchard"};
	const std::vector<unsigned char> walkable = MapGeneration::walkableTiles(map);
	const MapGeneration::Torus torus(map);
	const auto workers = MapGeneration::unitTilesByTeam(map, teams);
	std::array<int, 6> first{};
	for (int team = 0; team < teams; ++team)
	{
		const std::vector<int> dist = MapGeneration::stepsFrom(
			torus, MapGeneration::tileMask(torus, workers[team]), walkable);
		std::array<int, 6> steps;
		steps.fill(-1);
		const auto reach = [&](int slot, int d)
		{
			if (steps[size_t(slot)] < 0 || d < steps[size_t(slot)])
				steps[size_t(slot)] = d;
		};
		for (int y = 0; y < h; ++y)
			for (int x = 0; x < w; ++x)
			{
				const int d = dist[size_t(y) * w + x];
				if (d < 0)
					continue;
				if (within(tileRadius2(w, h, x, y), a.centre - 2))
					reach(5, d);
				for (int dy = -1; dy <= 1; ++dy)
					for (int dx = -1; dx <= 1; ++dx)
					{
						const int type = map.getResource(wrap(x + dx, w), wrap(y + dy, h)).type;
						if (type == CORN)
							reach(0, d + 1);
						else if (type == WOOD)
							reach(1, d + 1);
						else if (type >= CHERRY && type <= CHERRY + 2)
							reach(2 + type - CHERRY, d + 1);
					}
			}
		for (size_t k = 0; k < steps.size(); ++k)
		{
			if (steps[k] < 0)
				return "Colony " + std::to_string(team) + " cannot walk to " + targets[k] + ".";
			if (team > 0 && steps[k] != first[k])
				return "Colony " + std::to_string(team) + " walks " + std::to_string(steps[k]) +
					   " steps to " + targets[k] + ", colony 0 walks " + std::to_string(first[k]) +
					   ".";
		}
		if (team == 0)
			first = steps;
	}
	return "";
}
} // namespace

SymmetricArenaOptions::SymmetricArenaOptions(const GenerationRequest &r)
	: centreSize(r.option("centre-size")), moatWidth(r.option("moat-width")),
	  causewayWidth(r.option("causeway-width")), causeways(r.option("causeways")),
	  lakes(r.option("lakes")), richness(r.option("richness")), moat(r.option("moat") != 0),
	  orchardStone(r.option("orchard-stone") != 0), wheat(r.option("wheat-amount")),
	  wood(r.option("wood-amount")), stone(r.option("stone-amount")),
	  algae(r.option("algae-amount")), fruit(r.option("fruit-amount"))
{
}

GeneratorDefinition symmetricArenaDefinition()
{
	return {"symmetric-arena",
			15,
			"Symmetric arena",
			1,
			false,
			// centre-size: the orchard island's radius as a share of the shorter side (over a
			// small floor); moat and causeway widths are in tiles; causeways per colony, one
			// straight towards it or two flanking it; lakes as a share of the open land outside
			// the arena; richness scales the ambient deposits, never a home's own kit.
			{{"centre-size", "Centre size", 6, 16, 1, 10, ControlGroup::Terrain},
			 {"moat-width", "Moat width", 3, 10, 1, 5, ControlGroup::Terrain},
			 {"causeway-width", "Causeway width", 2, 8, 1, 4, ControlGroup::Layout},
			 {"causeways", "Causeways", 1, 2, 1, 1, ControlGroup::Layout},
			 {"lakes", "Lakes", 0, 40, 5, 20, ControlGroup::Terrain},
			 {"richness", "Resource richness", 0, 200, 25, 100, ControlGroup::Resources},
			 // Off, no water rings the orchard island; it joins the land around it.
			 GeneratorControl::toggle("moat", "Moat", true, ControlGroup::Terrain),
			 // Off, every grove in the orchard is fruit.
			 GeneratorControl::toggle("orchard-stone", "Stone in the orchard", true,
									  ControlGroup::Resources),
			 // Each ambient layer on top of richness: the farmland's wheat and wood, stone outcrops
			 // and algae. Fruit keeps that share of the orchard's groves nearest the centre, never
			 // fewer than three. Every home's kit stays as it is.
			 GeneratorControl::percentage("wheat-amount", "Wheat amount"),
			 GeneratorControl::percentage("wood-amount", "Wood amount"),
			 GeneratorControl::percentage("stone-amount", "Stone amount"),
			 GeneratorControl::percentage("algae-amount", "Algae amount"),
			 GeneratorControl::percentage("fruit-amount", "Fruit amount", 100)},
			generate,
			true,
			validate,
			validateWorld};
}
