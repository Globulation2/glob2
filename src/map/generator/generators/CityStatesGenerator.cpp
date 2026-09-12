// SPDX-License-Identifier: GPL-3.0-or-later
#include "CityStatesGenerator.h"
#include "FertilityField.h"
#include "Game.h"
#include "GenerationContext.h"
#include "Geometry.h"
#include "HeightMap.h"
#include "Resources.h"
#include "Settlements.h"
#include "Unit.h"
#include <algorithm>
#include <array>
#include <climits>
#include <cmath>
#include <cstdint>
#include <deque>
#include <limits>
#include <string>
#include <utility>
#include <vector>
using namespace MapGeneration;

// City states: a large shared commons in the middle of the map, ringed by a strait, and round it
// one big home for every colony - a wedge of the outer land with its own lake, fields and quarry,
// cut off from its neighbours by water channels and from the commons by the strait. The only way
// off a home on foot is its causeway: a road across the strait, lined with stone, that lands on
// the commons at the home's own angle - and a wall of stone runs round every home's coast, one
// tile in from the beach, so a swimmer can land on the beach but never leave it. The commons is
// where the game is fought - richer towards its centre, where an orchard of all three fruits
// stands - and once swimming pools let armies cross water anywhere, the causeways stop being the
// only way in.
//
// Every roll differs: the coasts have bays and headlands, the channels bend, the homes take one of
// several layouts (a lake, a creek with a ford, stone ridges with passes, a marsh, a band of sand)
// and the heart of the commons one of several (a lake, a stone crag, an island, a river delta, a
// forest), and sand lies in patches over the ground. Everything a home is made of is designed in
// the wedge's own frame - arc across it and radius out from the centre - and stamped into every
// home alike, so however a roll comes out, every colony gets the same home turned round the
// centre, and the layout is fair for any colony count.
//
// The whole design is a pure function of the request, so validateWorld rebuilds it and checks the
// finished world. Terrain is written straight to the undermap with an order-independent beach
// pass, as Ring world does.
namespace
{

constexpr double pi = 3.14159265358979323846;

// Sea kept between the homes' outer coast and the map's wrap, as a share of the half side.
constexpr double kRimShare = 0.05;
constexpr int kRimMinimum = 4;
// Coast roughness at 100: bays and headlands on the commons' coast as a share of its radius, a
// finer ripple on top, and bays into the homes' inner and outer coasts as shares of their depth.
constexpr double kCoastAmplitude = 0.28;
constexpr double kRippleAmplitude = 0.04;
constexpr double kInnerBayShare = 0.36;
constexpr double kOuterBayShare = 0.5;
// The channels between homes bow sideways by up to this many tiles, all the same way.
constexpr double kChannelBend = 9.0;
// Land outside a causeway's road, on each side: stone stands on every solid-grass tile of it, and
// the beach pass turns the outermost vertex to sand, which leaves a sand lane a unit can walk along
// beside the stone (grass may never touch water, so no wall can stand right at the water's edge).
constexpr double kShoulder = 3.5;
// The causeway as a barrier includes those sand lanes, for keeping deposits off and for checking.
constexpr double kLane = 1.5;
// How far each causeway's approaches are kept clear of deposits, on both shores.
constexpr int kLanding = 10;
// A home needs this much land between the strait and the outer sea, and this much arc at its
// inner coast beyond the channel and the causeway.
constexpr int kMinimumDepth = 20;
constexpr int kMinimumArc = 10;
// A home's lake keeps its water this far from the sea's, so the two beaches never touch: where
// they did, a unit could step from the sea's beach to the lake's and round the wall.
constexpr int kLakeSeaGap = 7;
// Every home starts identical: fixed wheat and wood beside its lake, a stone deposit, all
// unscaled, outside a clear ring round the swarm.
constexpr int kHomeWheat = 40;
constexpr int kHomeWood = 30;
constexpr int kSwarmClearance = 2;
// Land kept between a lake and any coast, landing or other lake.
constexpr int kLakeShore = 6;
// The commons' heart: its central lake's radius as a share of the commons' radius, and how far
// beyond that shore the orchard stands.
constexpr double kHeartShare = 0.22;
constexpr int kOrchardReach = 5;
// The most the colonies' walks to their causeway landings may differ: this many steps, or a
// third of the longest walk on maps where the walks are long.
constexpr int kLandingSpread = 12;
// A home needs this much depth for a creek, ridges or a band of sand to fit round its lake.
constexpr int kFeatureDepth = 30;

// What a home is made of, and what the heart of the commons is. One of each per map.
enum HomeKind
{
	Lakeland = 0,
	Riverside,
	Highland,
	Marsh,
	Barrens,
	kHomeKinds
};
enum HeartKind
{
	LakeHeart = 0,
	Crag,
	IslandHeart,
	Delta,
	ForestHeart,
	kHeartKinds
};

struct Torus
{
	int w, h;
	int x(int v) const { return ((v % w) + w) % w; }
	int y(int v) const { return ((v % h) + h) % h; }
	int at(int px, int py) const { return y(py) * w + x(px); }
	int offsetX(int from, int to) const
	{
		const int d = x(to - from);
		return d > w / 2 ? d - w : d;
	}
	int offsetY(int from, int to) const
	{
		const int d = y(to - from);
		return d > h / 2 ? d - h : d;
	}
};

// Breadth-first 8-neighbour steps on the torus from every source tile through open tiles; -1
// where the flood never arrives.
std::vector<int> stepsFrom(const Torus &t, const std::vector<unsigned char> &source,
						   const std::vector<unsigned char> &open)
{
	std::vector<int> dist(size_t(t.w) * t.h, -1);
	std::vector<int> queue;
	queue.reserve(dist.size());
	for (size_t i = 0; i < dist.size(); ++i)
		if (source[i])
		{
			dist[i] = 0;
			queue.push_back(int(i));
		}
	for (size_t head = 0; head < queue.size(); ++head)
	{
		const int x = queue[head] % t.w, y = queue[head] / t.w;
		for (int dy = -1; dy <= 1; ++dy)
			for (int dx = -1; dx <= 1; ++dx)
			{
				const int n = t.at(x + dx, y + dy);
				if (dist[n] < 0 && open[n])
				{
					dist[n] = dist[queue[head]] + 1;
					queue.push_back(n);
				}
			}
	}
	return dist;
}

// A smooth random curve round a ring, periodic in u over [0, 1): a few harmonics with random
// phases, scaled so its peak is one.
struct Profile
{
	std::array<double, 12> amplitude{}, phase{};
	int first = 1, count = 0;
	double at(double u) const
	{
		double v = 0;
		for (int h = 0; h < count; ++h)
			v += amplitude[h] * std::cos(2 * pi * (first + h) * u + phase[h]);
		return v;
	}
};

Profile rollProfile(GenerationContext &context, const char *stream, int first, int count,
					double falloff)
{
	Profile p;
	p.first = first;
	p.count = count;
	for (int h = 0; h < count; ++h)
	{
		p.amplitude[h] = (0.4 + 0.6 * context.bounded(stream, 1000) / 1000.0) /
						 std::pow(double(first + h), falloff);
		p.phase[h] = 2 * pi * context.bounded(stream, 3600) / 3600.0;
	}
	double peak = 0;
	for (int i = 0; i < 720; ++i)
		peak = std::max(peak, std::abs(p.at(i / 720.0)));
	if (peak > 0)
		for (int h = 0; h < count; ++h)
			p.amplitude[h] /= peak;
	return p;
}

// 0 within `flat` tiles of arc from the wedge's middle, 1 beyond `flat + fade`, smooth between.
double awayFromMiddle(double arc, double flat, double fade)
{
	const double x = std::clamp((arc - flat) / fade, 0.0, 1.0);
	return x * x * (3 - 2 * x);
}

// The numbers every part of the layout is measured from; a pure function of the request, shared
// by validateRequest and the design.
struct Geometry
{
	int teams, half, rim, strait, causeway;
	bool walls;
	double commonsRadius, outerRadius, amplitude;
	double innerRadius() const { return commonsRadius + strait; }
	double depth() const { return outerRadius - innerRadius(); }
	// Arc of a home's inner coast beyond its channel, at the nominal radius.
	double innerArc() const
	{
		return teams < 2 ? 2 * pi * innerRadius() : 2 * pi * innerRadius() / teams - strait;
	}
	// Half the arc a wedge spans at radius d, less half the channel.
	double arcHalf(double d) const { return teams < 2 ? pi * d : pi * d / teams - strait / 2.0; }
};

Geometry geometryFor(const GenerationRequest &r)
{
	const CityStatesOptions o(r);
	Geometry g;
	g.teams = std::max(1, r.nbTeams);
	const int side = std::min(1 << r.wDec, 1 << r.hDec);
	g.half = side / 2;
	g.rim = std::max(kRimMinimum, int(std::lround(kRimShare * g.half)));
	g.strait = std::max(4, int(std::lround(o.straitWidth / 100.0 * side)));
	g.causeway = o.causewayWidth;
	g.walls = o.stoneWalls;
	g.commonsRadius = std::lround(o.commonsSize / 100.0 * g.half);
	g.outerRadius = g.half - g.rim;
	g.amplitude = o.coastRoughness / 100.0;
	return g;
}

// How far in from the strait a home's lake lies: six tenths of the depth, held clear of both
// coasts however far their roughness lets them wander. RadialShape reaches at most 1.3 times its
// radius at the roughness used here.
double lakeOffset(const Geometry &g, double lakeRadius)
{
	const double reach = 1.3 * lakeRadius + kLakeSeaGap;
	const double low = reach + 2.0;
	const double high = g.depth() - reach - 2.0;
	return std::clamp(0.6 * g.depth(), low, std::max(low, high));
}

// A round feature in a wedge's frame: arc offset from the middle, radius from the centre.
struct Blob
{
	double s, r;
	RadialShape shape;
	bool holds(double ds, double dr) const
	{
		return std::hypot(ds, dr) < shape.radiusAt(std::atan2(dr, ds));
	}
};

struct Home
{
	double angle;       // the wedge's middle, where its causeway and lake lie
	double coast;       // the commons' coast radius at that angle
	int lakeX, lakeY;   // its main lake's centre
	int landing, shore; // one tile at each end of its causeway: on the commons, on the home
};

enum Region : signed char
{
	Sea = 0,
	Commons = 1,
	HomeLand = 2
};

// The terrain design: a pure function of the request, rebuilt by validateWorld.
struct Layout
{
	Torus t{1, 1};
	Geometry g{};
	int cx = 0, cy = 0;
	double phase = 0;
	int homeKind = Lakeland, heartKind = LakeHeart;
	double creekSide = 0; // Riverside: which flank the creek runs to, -1 or 1
	double lakeR = 0, islandR = 0, ringR = 0, forestR = 0; // the heart's radii
	std::vector<Home> homes;
	// Per tile.
	std::vector<signed char> region;
	std::vector<int> homeOf; // HomeLand tiles: which home, else -1
	std::vector<unsigned char> lake, river, ford, sand, ridge, strip, causeway, road, clear;
	std::vector<double> radius, angle;
	std::string failure;
};

Layout design(const GenerationRequest &request, GenerationContext &context)
{
	Layout L;
	L.t = {1 << request.wDec, 1 << request.hDec};
	L.g = geometryFor(request);
	const Torus &t = L.t;
	const Geometry &g = L.g;
	const int n = t.w * t.h, teams = g.teams;
	const CityStatesOptions o(request);
	L.cx = t.w / 2;
	L.cy = t.h / 2;
	L.phase = context.bounded("city-layout", 3600) / 3600.0 * 2 * pi;
	const double wedge = 2 * pi / teams;
	const double inner = g.innerRadius(), depth = g.depth();

	// What this map is made of.
	L.homeKind = int(context.bounded("city-kind", kHomeKinds));
	L.heartKind = int(context.bounded("city-kind", kHeartKinds));
	if (L.heartKind == Delta && teams < 2)
		L.heartKind = LakeHeart;
	if (depth < kFeatureDepth &&
		(L.homeKind == Riverside || L.homeKind == Highland || L.homeKind == Barrens))
		L.homeKind = Lakeland;

	// The coasts: bays and headlands round the commons, a ripple on top, bays into the homes'
	// flanks, and a bow in every channel. All periodic in the wedge, so every home gets the same.
	const Profile coast = rollProfile(context, "city-coast", 1, 4, 1.2);
	const Profile ripple = rollProfile(context, "city-coast", 6, 6, 0.6);
	const Profile innerBays = rollProfile(context, "city-coast", 1, 3, 1.0);
	const Profile outerBays = rollProfile(context, "city-coast", 1, 3, 1.0);
	const double coastAmp = kCoastAmplitude * g.amplitude * g.commonsRadius;
	const double rippleAmp = kRippleAmplitude * g.amplitude * g.commonsRadius;
	const double innerBayAmp = kInnerBayShare * g.amplitude * depth;
	const double outerBayAmp = kOuterBayShare * g.amplitude * depth;
	const double bend = (context.bounded("city-coast", 2001) / 1000.0 - 1) * kChannelBend;
	// The coasts hold flat for a stretch either side of every causeway, measured in tiles of arc so
	// the stretch is the same however many homes share the ring, and the flanks' bays begin only
	// beyond the lake and its clearance.
	const double lakeRadius = std::clamp(0.1 * depth, 2.0, 6.0);
	const double lakeReach = 1.3 * lakeRadius;
	const double flatArc = g.causeway / 2.0 + kShoulder + kLane + 4;
	const double bayArc = lakeReach + kLakeSeaGap + 6;
	const auto coastAt = [&](double u, double d)
	{
		const double away = awayFromMiddle(d * std::abs(u - 0.5) * wedge, flatArc, 12);
		return g.commonsRadius + (coastAmp * coast.at(u) + rippleAmp * ripple.at(u)) * away;
	};
	const auto innerAt = [&](double u, double d)
	{
		return coastAt(u, d) + g.strait +
			   innerBayAmp * std::max(0.0, innerBays.at(u)) *
				   awayFromMiddle(d * std::abs(u - 0.5) * wedge, bayArc, 10);
	};
	const auto outerAt = [&](double u, double d)
	{
		return std::min(double(g.half - 3),
						g.outerRadius -
							outerBayAmp * std::max(0.0, outerBays.at(u)) *
								awayFromMiddle(d * std::abs(u - 0.5) * wedge, bayArc - 2, 10));
	};
	const auto bendAt = [&](double d)
	{
		const double x = (d - inner) / std::max(1.0, depth);
		return x <= 0 || x >= 1 ? 0.0 : bend * std::sin(pi * x);
	};

	for (int k = 0; k < teams; ++k)
	{
		const double a = L.phase + wedge * (k + 0.5);
		L.homes.push_back({a, g.commonsRadius, 0, 0, -1, -1});
	}

	// The home's features in the wedge's frame: lakes first (the main one first), then whatever
	// the kind adds. A feature keeps clear of the coasts as bays can bring them, of the channels,
	// of the causeway's approach and of other features.
	const double mainLakeR = inner + lakeOffset(g, lakeRadius);
	std::vector<Blob> lakes, sandBlobs;
	lakes.push_back({0.0, mainLakeR, RadialShape(lakeRadius, 0.3, context, "city-home-lakes")});
	const auto fits =
		[&](double s0, double r0, double reach, double gap, const std::vector<Blob> &others)
	{
		if (std::abs(s0) + reach + gap > g.arcHalf(r0))
			return false;
		const double u = 0.5 + s0 / (wedge * r0);
		if (r0 - reach - gap < innerAt(u, r0) || r0 + reach + gap > outerAt(u, r0))
			return false;
		if (r0 - reach < inner + kLanding + 2 && std::abs(s0) < g.causeway + reach + 4)
			return false;
		for (const Blob &b : others)
			if (std::hypot(s0 - b.s, r0 - b.r) < reach + b.shape.maximumRadius() + gap)
				return false;
		return true;
	};
	if (L.homeKind == Marsh)
		for (int attempt = 0; attempt < 400 && lakes.size() < 4; ++attempt)
		{
			const double r0 =
				inner + 8 + context.bounded("city-home-lakes", 1000) / 1000.0 * (depth - 16);
			const double s0 =
				(context.bounded("city-home-lakes", 2001) / 1000.0 - 1) * 0.6 * g.arcHalf(r0);
			RadialShape shape(2.0 + context.bounded("city-home-lakes", 1000) / 1000.0 * 2.0, 0.3,
							  context, "city-home-lakes");
			if (fits(s0, r0, shape.maximumRadius(), kLakeSeaGap, lakes))
				lakes.push_back({s0, r0, shape});
		}
	// Riverside: a creek from the main lake towards one flank of the inner coast, with a ford.
	std::vector<std::pair<double, double>> creek;
	int fordSegment = -1;
	if (L.homeKind == Riverside)
	{
		const double side = context.bounded("city-features", 2) ? 1.0 : -1.0;
		L.creekSide = side;
		const double reachArc = 0.32 * g.arcHalf(mainLakeR);
		const double endR = inner + kLakeSeaGap + 6.0;
		creek = {{0.0, mainLakeR},
				 {side * 0.35 * reachArc, mainLakeR - 0.3 * (mainLakeR - endR)},
				 {side * 0.75 * reachArc, mainLakeR - 0.65 * (mainLakeR - endR)},
				 {side * reachArc, endR}};
		fordSegment = 1;
	}
	// Highland: two ridges out from the strait to the sea, a pass through each.
	const double ridgeU[2] = {0.28, 0.72};
	const double passR = inner + 0.5 * depth;
	// Barrens: both flanks of the home are sand, leaving a strip of grass down the middle from the
	// causeway past the lake; the sand keeps clear of every coast so the wall stays on the shore.
	const double barrenShare = 0.45;
	// Sand patches, in every kind of home.
	const double homeArea = wedge * (g.outerRadius * g.outerRadius - inner * inner) / 2;
	const int homeSand = int(std::lround(o.sand * homeArea / 8192.0));
	for (int attempt = 0; attempt < 400 && int(sandBlobs.size()) < homeSand; ++attempt)
	{
		const double r0 = inner + 8 + context.bounded("city-sand", 1000) / 1000.0 * (depth - 16);
		const double s0 = (context.bounded("city-sand", 2001) / 1000.0 - 1) * 0.75 * g.arcHalf(r0);
		RadialShape shape(3.0 + context.bounded("city-sand", 1000) / 1000.0 * 4.0, 0.5, context,
						  "city-sand");
		std::vector<Blob> avoid = lakes;
		avoid.insert(avoid.end(), sandBlobs.begin(), sandBlobs.end());
		if (fits(s0, r0, shape.maximumRadius(), 5, avoid))
			sandBlobs.push_back({s0, r0, shape});
	}

	// The heart of the commons.
	L.lakeR = std::max(3.0, kHeartShare * g.commonsRadius);
	L.ringR = std::max(8.0, kHeartShare * g.commonsRadius);
	L.islandR = 0;
	if (L.heartKind == Crag)
		L.lakeR = std::max(2.5, 0.06 * g.commonsRadius);
	if (L.heartKind == IslandHeart)
	{
		L.lakeR = std::max(6.0, 0.3 * g.commonsRadius);
		L.islandR = std::max(5.0, 0.12 * g.commonsRadius);
	}
	L.forestR = L.lakeR + 4 + 0.09 * g.commonsRadius;
	const double deltaFordR = 0.6 * g.commonsRadius;
	const RadialShape heart(L.lakeR, L.heartKind == IslandHeart ? 0.2 : 0.3, context, "city-heart");
	// Sand on the commons: anywhere between the heart and the shore, off the landings.
	struct Patch
	{
		int x, y;
		RadialShape shape;
	};
	std::vector<Patch> commonsSand;
	const double commonsArea = pi * g.commonsRadius * g.commonsRadius;
	const int wantedSand = int(std::lround(o.sand * commonsArea / 8192.0));
	for (int attempt = 0; attempt < 400 && int(commonsSand.size()) < wantedSand; ++attempt)
	{
		const double a = context.bounded("city-sand", 3600) / 3600.0 * 2 * pi;
		const double r0 = 0.45 * g.commonsRadius + context.bounded("city-sand", 1000) / 1000.0 *
													   (0.55 * g.commonsRadius - 12);
		RadialShape shape(3.0 + context.bounded("city-sand", 1000) / 1000.0 * 5.0, 0.5, context,
						  "city-sand");
		const double reach = shape.maximumRadius();
		const double turn = std::fmod(a - L.phase + 4 * pi, 2 * pi);
		const double u = turn / wedge - std::floor(turn / wedge);
		if (r0 * std::abs(u - 0.5) * wedge < reach + g.causeway + 4 &&
			r0 > g.commonsRadius - kLanding - reach - 4)
			continue;
		if (r0 + reach > coastAt(u, r0) - 8)
			continue;
		bool open = true;
		for (const Patch &p : commonsSand)
		{
			const int px = L.cx + int(std::lround(r0 * std::cos(a))),
					  py = L.cy + int(std::lround(r0 * std::sin(a)));
			const double dx = t.offsetX(px, p.x), dy = t.offsetY(py, p.y);
			open = open && std::hypot(dx, dy) >= reach + p.shape.maximumRadius() + 4;
		}
		if (open)
			commonsSand.push_back({t.x(L.cx + int(std::lround(r0 * std::cos(a)))),
								   t.y(L.cy + int(std::lround(r0 * std::sin(a)))), shape});
	}

	L.region.assign(n, Sea);
	L.homeOf.assign(n, -1);
	for (auto *mask :
		 {&L.lake, &L.river, &L.ford, &L.sand, &L.ridge, &L.strip, &L.causeway, &L.road, &L.clear})
		mask->assign(n, 0);
	L.radius.assign(n, 0.0);
	L.angle.assign(n, 0.0);
	const double roadHalf = g.causeway / 2.0;
	const double stripHalf = roadHalf + kShoulder;
	std::vector<double> landingAlong(teams, -1.0), shoreAlong(teams, 1e9);
	for (int y = 0; y < t.h; ++y)
		for (int x = 0; x < t.w; ++x)
		{
			const int i = y * t.w + x;
			const int dx = t.offsetX(L.cx, x), dy = t.offsetY(L.cy, y);
			const double d = std::hypot(double(dx), double(dy));
			const double theta = std::atan2(double(dy), double(dx));
			L.radius[i] = d;
			L.angle[i] = theta;
			// The wedge frame: which home, how far across it and how far along the arc from its middle.
			double turn = std::fmod(theta - L.phase + 4 * pi, 2 * pi);
			if (teams >= 2)
				turn = std::fmod(turn - bendAt(d) / std::max(1.0, d) + 4 * pi, 2 * pi);
			const int k = std::min(teams - 1, int(turn / wedge));
			const double u = turn / wedge - k;
			const double s = d * (u - 0.5) * wedge;
			const double toBoundary = teams < 2 ? 1e9 : d * std::min(u, 1 - u) * wedge;
			const double toLanding = d * std::abs(u - 0.5) * wedge;
			const double c = coastAt(u, d);
			const double in = innerAt(u, d), out = outerAt(u, d);
			if (d < c)
			{
				L.region[i] = Commons;
			}
			else if (d < in)
			{
				L.region[i] = Sea;
			}
			else if (d < out && toBoundary >= g.strait / 2.0)
			{
				L.region[i] = HomeLand;
				L.homeOf[i] = k;
			}
			// The home's causeway: straight across the strait at the wedge's middle.
			const Home &h = L.homes[k];
			const double along = d * std::cos(theta - h.angle),
						 across = std::abs(d * std::sin(theta - h.angle));
			if (across <= stripHalf + kLane && along >= h.coast - 3 &&
				along <= h.coast + g.strait + 3)
				L.causeway[i] = 1;
			if (across <= stripHalf && along >= h.coast - 3 && along <= h.coast + g.strait + 3)
			{
				L.strip[i] = 1;
				if (L.region[i] == Sea)
				{
					L.region[i] = HomeLand;
					L.homeOf[i] = k;
				}
				if (across <= roadHalf)
					L.road[i] = 1;
				if (along > landingAlong[k] && L.road[i])
				{
					landingAlong[k] = along;
					L.homes[k].shore = i;
				}
				if (along < shoreAlong[k] && L.road[i])
				{
					shoreAlong[k] = along;
					L.homes[k].landing = i;
				}
			}
			// Both approaches to the causeway are kept clear of deposits.
			if (across <= stripHalf + kLane &&
				((along >= h.coast - kLanding && along < h.coast) ||
				 (along > h.coast + g.strait && along <= h.coast + g.strait + kLanding)))
				L.clear[i] = 1;

			// The home's features, the same in every wedge.
			if (L.region[i] == HomeLand && !L.strip[i])
			{
				for (const Blob &b : lakes)
					if (b.holds(s - b.s, d - b.r))
						L.lake[i] = 1;
				for (size_t seg = 0; seg + 1 < creek.size(); ++seg)
				{
					const double ax = creek[seg].first, ay = creek[seg].second;
					const double bx = creek[seg + 1].first, by = creek[seg + 1].second;
					const double len2 = (bx - ax) * (bx - ax) + (by - ay) * (by - ay);
					const double tt = std::clamp(((s - ax) * (bx - ax) + (d - ay) * (by - ay)) /
													 std::max(1e-6, len2),
												 0.0, 1.0);
					const double dist =
						std::hypot(s - (ax + tt * (bx - ax)), d - (ay + tt * (by - ay)));
					if (dist < 1.6)
					{
						if (int(seg) == fordSegment && std::abs(tt - 0.5) * std::sqrt(len2) < 2.5)
						{
							L.ford[i] = 1;
							L.clear[i] = 1;
						}
						else
						{
							L.lake[i] = 1;
						}
					}
					else if (int(seg) == fordSegment && dist < 3.5 &&
							 std::abs(tt - 0.5) * std::sqrt(len2) < 3.5)
					{
						L.clear[i] = 1;
					}
				}
				if (L.homeKind == Highland && d >= in + 1 && d <= out - 1)
					for (double ur : ridgeU)
					{
						const double sr = (ur - 0.5) * wedge * d;
						if (std::abs(s - sr) < 1.25)
						{
							if (std::abs(d - passR) < 2.5)
								L.clear[i] = 1;
							else
								L.ridge[i] = 1;
						}
						else if (std::abs(s - sr) < 3.5 && std::abs(d - passR) < 4.5)
						{
							L.clear[i] = 1;
						}
					}
				if (L.homeKind == Barrens && d >= in + 8 && d <= out - 8 &&
					std::abs(s) > barrenShare * g.arcHalf(d) && std::abs(s) < g.arcHalf(d) - 8)
					L.sand[i] = 1;
				for (const Blob &b : sandBlobs)
					if (b.holds(s - b.s, d - b.r))
						L.sand[i] = 1;
			}
			// The heart of the commons.
			if (L.region[i] == Commons)
			{
				const bool inHeart = d < heart.radiusAt(theta);
				if (L.heartKind == IslandHeart)
				{
					if (inHeart && d >= L.islandR)
						L.lake[i] = 1;
					if (inHeart && d >= L.islandR - 1 && d <= L.lakeR + 2.5 && toLanding < 2)
					{
						L.ford[i] = 1;
						L.lake[i] = 0;
					}
					if (inHeart && d >= L.islandR - 2 && d <= L.lakeR + 3.5 && toLanding < 3.5)
						L.clear[i] = 1;
				}
				else if (inHeart)
				{
					L.lake[i] = 1;
				}
				if (L.heartKind == Crag)
				{
					if (d >= L.ringR && d < L.ringR + 1.5 && toLanding >= 2.5)
						L.ridge[i] = 1;
					if (d >= L.ringR - 2 && d < L.ringR + 3.5 && toLanding < 4)
						L.clear[i] = 1;
				}
				if (L.heartKind == Delta && teams >= 2 && d >= L.lakeR - 1 && d <= c + 1)
				{
					if (toBoundary < 2)
					{
						if (std::abs(d - deltaFordR) < 2.5)
							L.ford[i] = 1;
						else
							L.river[i] = 1;
					}
					if (toBoundary < 3.5 && std::abs(d - deltaFordR) < 4)
						L.clear[i] = 1;
				}
				if (!L.lake[i] && !L.river[i] && !L.ford[i] && !L.clear[i] && !L.causeway[i])
					for (const Patch &p : commonsSand)
					{
						const double px = t.offsetX(p.x, x), py = t.offsetY(p.y, y);
						if (std::hypot(px, py) < p.shape.radiusAt(std::atan2(py, px)))
							L.sand[i] = 1;
					}
			}
		}
	// The commons-side end is the end nearest the centre; the home-side end the farthest.
	for (int k = 0; k < teams; ++k)
	{
		std::swap(L.homes[k].landing, L.homes[k].shore);
		if (L.homes[k].landing < 0 || L.homes[k].shore < 0)
		{
			L.failure = "home " + std::to_string(k) + " has no causeway";
			return L;
		}
		Home &h = L.homes[k];
		h.lakeX = t.x(L.cx + int(std::lround(mainLakeR * std::cos(h.angle))));
		h.lakeY = t.y(L.cy + int(std::lround(mainLakeR * std::sin(h.angle))));
	}
	return L;
}

// The ground every causeway leads to, by the kind of heart: the island, the inside of the crag,
// or the central lake's shore.
std::vector<unsigned char> heartTiles(const Map &map, const Layout &L)
{
	const Torus &t = L.t;
	const int n = t.w * t.h;
	std::vector<unsigned char> heart(n, 0);
	const double reach = L.heartKind == IslandHeart ? L.islandR
						 : L.heartKind == Crag      ? L.ringR - 1
													: L.lakeR + kOrchardReach + 3;
	for (int i = 0; i < n; ++i)
		heart[i] = L.region[i] == Commons && L.radius[i] < reach && !map.isWater(i % t.w, i / t.w);
	return heart;
}

// Grass may never touch water (Map::regenerateMap reads each tile from its four undermap corners),
// so every land corner beside water becomes sand. Unlike Map::controlSand() this reads only the
// original terrain, so the result doesn't depend on scan order and water is never eaten away.
void layBeaches(std::vector<unsigned char> &terrain, const Torus &t)
{
	const std::vector<unsigned char> original(terrain);
	for (int y = 0; y < t.h; ++y)
		for (int x = 0; x < t.w; ++x)
		{
			const int i = y * t.w + x;
			if (original[i] == WATER)
				continue;
			bool shore = false;
			for (int dy = -1; dy <= 1 && !shore; ++dy)
				for (int dx = -1; dx <= 1 && !shore; ++dx)
					shore = original[t.at(x + dx, y + dy)] == WATER;
			if (shore)
				terrain[i] = SAND;
		}
}

// The ground a unit landing from the sea can reach without crossing solid grass: every land tile
// with a sea vertex in the box its four corners touch, and every beach joined to those - a lake's
// beach too, where it meets the sea's. Stone cannot stand on any of it.
std::vector<unsigned char> seaMargin(const Map &map, const Layout &L)
{
	const Torus &t = L.t;
	const int n = t.w * t.h;
	std::vector<unsigned char> sea(n, 0), margin(n, 0), beach(n, 0);
	for (int i = 0; i < n; ++i)
		sea[i] =
			map.getUMTerrain(i % t.w, i / t.w) == WATER && !L.lake[i] && L.region[i] != Commons;
	for (int i = 0; i < n; ++i)
	{
		const int x = i % t.w, y = i / t.w;
		if (map.isWater(x, y))
			continue;
		beach[i] = map.getTerrainType(x, y) != GRASS;
		for (int dy = -1; dy <= 2 && !margin[i]; ++dy)
			for (int dx = -1; dx <= 2; ++dx)
				if (sea[t.at(x + dx, y + dy)])
				{
					margin[i] = 1;
					break;
				}
	}
	const std::vector<int> joined = stepsFrom(t, margin, beach);
	for (int i = 0; i < n; ++i)
		if (joined[i] >= 0)
			margin[i] = 1;
	return margin;
}

// The design's stone, once the terrain is laid: every solid-grass tile of a causeway's shoulders
// outside its road, the ridges and the crag, and round every home a wall on the solid-grass tiles
// that touch the sea's margin. Every step off the beach lands on the wall, so a home is sealed but
// for its road. Rebuilt the same way by validateWorld.
std::vector<unsigned char> stoneTiles(const Map &map, const Layout &L)
{
	const Torus &t = L.t;
	const int n = t.w * t.h;
	std::vector<unsigned char> stone(n, 0);
	if (!L.g.walls)
		return stone;
	const std::vector<unsigned char> margin = seaMargin(map, L);
	for (int i = 0; i < n; ++i)
	{
		if (L.road[i] || map.getTerrainType(i % t.w, i / t.w) != GRASS)
			continue;
		if (L.strip[i] || L.ridge[i])
		{
			stone[i] = 1;
			continue;
		}
		if (L.homeOf[i] < 0)
			continue;
		for (int dy = -1; dy <= 1 && !stone[i]; ++dy)
			for (int dx = -1; dx <= 1; ++dx)
				if (margin[t.at(i % t.w + dx, i / t.w + dy)])
				{
					stone[i] = 1;
					break;
				}
	}
	return stone;
}

// Extra lakes in the commons for fertility, likelier towards the centre; each keeps kLakeShore
// of land to any coast, landing, the central lake and other lakes. Counted per 128x128 of commons.
void carveValleys(std::vector<unsigned char> &terrain, const Layout &L, GenerationContext &context,
				  int perStandardMap)
{
	const Torus &t = L.t;
	const int n = t.w * t.h;
	std::vector<unsigned char> water(n, 0), land(n, 0), keep(n, 0);
	std::vector<int> ground;
	for (int i = 0; i < n; ++i)
	{
		water[i] = terrain[i] == WATER;
		land[i] = !water[i];
		keep[i] = L.clear[i] || L.causeway[i];
		if (L.region[i] == Commons && land[i])
			ground.push_back(i);
	}
	if (perStandardMap <= 0 || ground.empty())
		return;
	std::vector<unsigned char> all(n, 1);
	const std::vector<int> shore = stepsFrom(t, water, land);
	const std::vector<int> fromKeep = stepsFrom(t, keep, all);
	const int wanted = int(std::lround(perStandardMap * double(ground.size()) / 16384.0));
	const double scale = std::clamp(std::sqrt(std::min(t.w, t.h) / 128.0), 0.8, 1.6);
	struct Lake
	{
		int x, y;
		double reach;
	};
	std::vector<Lake> lakes;
	for (int attempt = 0; int(lakes.size()) < wanted && attempt < wanted * 80; ++attempt)
	{
		const int at = ground[context.bounded("city-valleys", ground.size())];
		const double inward = 1 - L.radius[at] / std::max(1.0, L.g.commonsRadius);
		if (int(context.bounded("city-valleys", 100)) >= 25 + int(75 * inward))
			continue;
		const int x = at % t.w, y = at / t.w;
		const double stretch = 1.2 + context.bounded("city-valleys", 81) / 100.0;
		const double turn = context.bounded("city-valleys", 3600) / 3600.0 * pi;
		const RadialShape shape((3 + context.bounded("city-valleys", 4)) * scale, 0.35, context,
								"city-valleys");
		const double reach = shape.maximumRadius() * stretch;
		if (shore[at] < reach + kLakeShore || fromKeep[at] < reach + kLakeShore)
			continue;
		bool open = true;
		for (const Lake &other : lakes)
		{
			const double gap = reach + other.reach + 2 * kLakeShore;
			const double dx = t.offsetX(x, other.x), dy = t.offsetY(y, other.y);
			open = open && dx * dx + dy * dy >= gap * gap;
		}
		if (!open)
			continue;
		const ShapeTransform transform({double(x), double(y)}, turn, stretch);
		const int r = int(std::ceil(reach));
		for (int dy = -r; dy <= r; ++dy)
			for (int dx = -r; dx <= r; ++dx)
			{
				const ShapePoint p = transform.toShape({double(x + dx), double(y + dy)});
				if (std::hypot(p.x, p.y) < shape.radiusAt(std::atan2(p.y, p.x)))
					terrain[t.at(x + dx, y + dy)] = WATER;
			}
		lakes.push_back({x, y, reach});
	}
}

struct Island
{
	int x, y;
	std::vector<int> tiles;
};

// Small islands out in the open sea, each well clear of every coast and of each other, so they are
// only ever reached by swimming. Counted per 128x128 of sea.
std::vector<Island> raiseIslands(std::vector<unsigned char> &terrain, const Layout &L,
								 GenerationContext &context, int perStandardMap)
{
	const Torus &t = L.t;
	const int n = t.w * t.h;
	std::vector<Island> islands;
	if (perStandardMap <= 0)
		return islands;
	std::vector<unsigned char> water(n), land(n);
	std::vector<int> sea;
	for (int i = 0; i < n; ++i)
	{
		water[i] = terrain[i] == WATER;
		land[i] = !water[i];
		if (water[i])
			sea.push_back(i);
	}
	if (sea.empty())
		return islands;
	const std::vector<int> offshore = stepsFrom(t, land, water);
	const int wanted = int(std::lround(perStandardMap * double(sea.size()) / 16384.0));
	const double scale = std::clamp(std::sqrt(std::min(t.w, t.h) / 128.0), 1.0, 1.6);
	for (int attempt = 0; int(islands.size()) < wanted && attempt < wanted * 60; ++attempt)
	{
		const int at = sea[context.bounded("city-islands", sea.size())];
		const int x = at % t.w, y = at / t.w;
		const RadialShape shape((4 + context.bounded("city-islands", 3)) * scale, 0.3, context,
								"city-islands");
		const double reach = shape.maximumRadius();
		if (offshore[at] < reach + kLakeShore + 1)
			continue;
		bool open = true;
		for (const Island &other : islands)
		{
			const double gap = 2 * reach + kLakeShore + 1;
			const double dx = t.offsetX(x, other.x), dy = t.offsetY(y, other.y);
			open = open && dx * dx + dy * dy >= gap * gap;
		}
		if (!open)
			continue;
		Island island{x, y, {}};
		const int r = int(std::ceil(reach));
		for (int dy = -r; dy <= r; ++dy)
			for (int dx = -r; dx <= r; ++dx)
				if (std::hypot(double(dx), double(dy)) <
					shape.radiusAt(std::atan2(double(dy), double(dx))))
				{
					const int i = t.at(x + dx, y + dy);
					terrain[i] = GRASS;
					island.tiles.push_back(i);
				}
		islands.push_back(std::move(island));
	}
	return islands;
}

// Grows a compact patch of one resource outward from a seed tile, breadth-first over the four
// cardinal neighbours, onto tiles the predicate allows. Returns how many tiles it placed.
template <typename Eligible>
int growPatch(Map &map, const Torus &t, int seed, int type, int count, Eligible eligible)
{
	std::vector<unsigned char> queued(size_t(t.w) * t.h, 0);
	std::vector<int> frontier{seed};
	queued[seed] = 1;
	int placed = 0;
	static const int steps[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
	for (size_t head = 0; head < frontier.size() && placed < count; ++head)
	{
		const int i = frontier[head], x = i % t.w, y = i / t.w;
		if (!eligible(i) || !map.isResourceAllowed(x, y, type))
			continue;
		map.setResource(x, y, type, 1);
		++placed;
		for (const auto &step : steps)
		{
			const int n = t.at(x + step[0], y + step[1]);
			if (!queued[n])
			{
				queued[n] = 1;
				frontier.push_back(n);
			}
		}
	}
	return placed;
}

bool clearGround(const Map &map, int x, int y)
{
	return map.isGrass(x, y) && !map.isResource(x, y) && map.getBuilding(x, y) == NOGBID &&
		   map.getGroundUnit(x, y) == NOGUID;
}

// Splits a set of tiles into wheat and wood by an unrelated noise field, so the two crops form
// separate patches rather than rings sorted by fertility.
void plantFields(Map &map, const Torus &t, std::vector<int> tiles, int wheat, int wood,
				 HeightMap &split)
{
	const int total = std::min(int(tiles.size()), wheat + wood);
	if (total <= 0)
		return;
	tiles.resize(total);
	std::stable_sort(
		tiles.begin(), tiles.end(), [&](int a, int b)
		{ return split.uiLevel(a % t.w, a / t.w, 2048) < split.uiLevel(b % t.w, b / t.w, 2048); });
	const int wheatShare = int(std::int64_t(total) * wheat / std::max(1, wheat + wood));
	for (int k = 0; k < total; ++k)
		map.setResource(tiles[k] % t.w, tiles[k] / t.w, k < wheatShare ? CORN : WOOD, 1);
}

// Every home's kit, identical and unscaled: wheat and wood patches beside its lake, where the lake
// keeps them growing, and a stone deposit further off; then its own scaled ambient farmland,
// outcrops and a grove, as a self-sufficient base needs.
void furnishHomes(Map &map, const Layout &L, GenerationContext &context, const CityStatesOptions &o)
{
	const Torus &t = L.t;
	const int n = t.w * t.h;
	std::vector<unsigned char> reserved(n, 0), water(n), dry(n);
	for (int team = 0; team < L.g.teams; ++team)
		for (int dy = -kSwarmClearance; dy < 4 + kSwarmClearance; ++dy)
			for (int dx = -kSwarmClearance; dx < 4 + kSwarmClearance; ++dx)
				reserved[t.at(context.bootX[team] + dx, context.bootY[team] + dy)] = 1;
	for (int i = 0; i < n; ++i)
	{
		water[i] = map.isWater(i % t.w, i / t.w);
		dry[i] = !water[i];
	}
	const std::vector<int> shore = stepsFrom(t, water, dry);
	const Fertility::Field fertility = Fertility::forMap(map, false);
	HeightMap split(t.w, t.h, context.stream("city-home-split"));
	split.makePlain(6);
	HeightMap patch(t.w, t.h, context.stream("city-home-patch"));
	patch.makePlain(12);
	for (int team = 0; team < L.g.teams; ++team)
	{
		const Home &h = L.homes[team];
		const auto eligible = [&](int i)
		{
			return L.homeOf[i] == team && !L.causeway[i] && !L.clear[i] && !reserved[i] &&
				   clearGround(map, i % t.w, i / t.w);
		};
		// The kit: wheat and wood beside the lake on the swarm's side, on the flank away from any
		// creek, and the quarry behind the lake.
		const double reach = std::clamp(0.1 * L.g.depth(), 2.0, 6.0) * 1.3 + 3;
		const double flank = L.creekSide != 0 ? -L.creekSide : -1.0;
		const auto seedNear = [&](double along, double across, int within)
		{
			const int ax =
				h.lakeX + int(std::lround(along * std::cos(h.angle) - across * std::sin(h.angle)));
			const int ay =
				h.lakeY + int(std::lround(along * std::sin(h.angle) + across * std::cos(h.angle)));
			int seed = -1, nearest = INT_MAX;
			for (int dy = -within; dy <= within; ++dy)
				for (int dx = -within; dx <= within; ++dx)
				{
					const int i = t.at(ax + dx, ay + dy);
					if (eligible(i) && dx * dx + dy * dy < nearest)
					{
						nearest = dx * dx + dy * dy;
						seed = i;
					}
				}
			return seed;
		};
		if (L.creekSide == 0)
		{
			if (const int seed = seedNear(-0.3 * reach, -reach, 14); seed >= 0)
				growPatch(map, t, seed, CORN, kHomeWheat, eligible);
			if (const int seed = seedNear(-0.3 * reach, reach, 14); seed >= 0)
				growPatch(map, t, seed, WOOD, kHomeWood, eligible);
		}
		else
		{
			if (const int seed = seedNear(-0.5 * reach, flank * reach, 14); seed >= 0)
				growPatch(map, t, seed, CORN, kHomeWheat, eligible);
			if (const int seed = seedNear(0.6 * reach, flank * (reach + 1), 14); seed >= 0)
				growPatch(map, t, seed, WOOD, kHomeWood, eligible);
		}
		if (const int seed = seedNear(reach + 8, 0, 12); seed >= 0)
			placeResourceClump(map, context, {seed % t.w, seed / t.w}, STONE, 2);
		// Ambient farmland on the home's fertile ground, in patches, then outcrops and a grove.
		std::vector<int> ground;
		std::vector<std::pair<double, int>> farm;
		std::vector<float> levels;
		for (int i = 0; i < n; ++i)
			if (eligible(i))
			{
				ground.push_back(i);
				if (fertility.at(i % t.w, i / t.w) > 0)
					levels.push_back(patch(i % t.w, i / t.w));
			}
		float cut = 0;
		if (!levels.empty())
		{
			std::nth_element(levels.begin(), levels.begin() + levels.size() * 45 / 100,
							 levels.end());
			cut = levels[levels.size() * 45 / 100];
		}
		for (int i : ground)
		{
			const std::uint32_t f = fertility.at(i % t.w, i / t.w);
			if (f > 0 && patch(i % t.w, i / t.w) >= cut)
				farm.push_back({-double(f), i});
		}
		std::stable_sort(farm.begin(), farm.end());
		std::vector<int> chosen;
		for (const auto &entry : farm)
			chosen.push_back(entry.second);
		const int area = int(ground.size());
		plantFields(map, t, chosen, int(scaledCount(area * 4 / 100, o.wheat)),
					int(scaledCount(area * 2 / 100, o.wood)), split);
		const int outcrops = int(scaledCount(std::max(1, area / 2500), o.stone));
		for (int k = 0; k < outcrops; ++k)
			for (int attempt = 0; attempt < 100; ++attempt)
			{
				const int at = ground[context.bounded("city-home-stone", ground.size())];
				if (eligible(at))
				{
					placeResourceClump(map, context, {at % t.w, at / t.w}, STONE, 1);
					break;
				}
			}
		const int groves = int(scaledCount(1, o.fruit));
		for (int k = 0; k < groves; ++k)
			for (int attempt = 0; attempt < 100; ++attempt)
			{
				const int at = ground[context.bounded("city-home-fruit", ground.size())];
				if (eligible(at))
				{
					placeResourceClump(map, context, {at % t.w, at / t.w},
									   CHERRY + int(context.bounded("city-home-fruit", 3)), 1);
					break;
				}
			}
	}
}

// The commons: farmland on its fertile ground, richer towards the centre as the frontier control
// says; stone outcrops and fruit groves sampled by the same weight; and the orchard of all three
// fruits round the central lake, the heart every causeway leads to.
void stockCommons(Map &map, const Layout &L, GenerationContext &context, const CityStatesOptions &o)
{
	const Torus &t = L.t;
	const int n = t.w * t.h;
	const double frontier = o.frontier / 100.0;
	const auto weight = [&](int i)
	{
		const double inward =
			std::clamp(1 - L.radius[i] / std::max(1.0, L.g.commonsRadius), 0.0, 1.0);
		return 1 - frontier + frontier * std::min(1.0, inward / (1 - kHeartShare));
	};
	const auto eligible = [&](int i)
	{
		return L.region[i] == Commons && !L.clear[i] && !L.causeway[i] &&
			   clearGround(map, i % t.w, i / t.w);
	};
	const Fertility::Field fertility = Fertility::forMap(map, false);
	HeightMap patch(t.w, t.h, context.stream("city-patch"));
	patch.makePlain(12);
	HeightMap split(t.w, t.h, context.stream("city-split"));
	split.makePlain(6);
	std::vector<int> ground;
	std::vector<float> levels;
	for (int i = 0; i < n; ++i)
		if (eligible(i))
		{
			ground.push_back(i);
			if (fertility.at(i % t.w, i / t.w) > 0)
				levels.push_back(patch(i % t.w, i / t.w));
		}
	if (ground.empty())
		return;
	float cut = 0;
	if (!levels.empty())
	{
		std::nth_element(levels.begin(), levels.begin() + levels.size() * 45 / 100, levels.end());
		cut = levels[levels.size() * 45 / 100];
	}
	std::vector<std::pair<double, int>> farm;
	for (int i : ground)
	{
		const std::uint32_t f = fertility.at(i % t.w, i / t.w);
		if (f > 0 && patch(i % t.w, i / t.w) >= cut)
			farm.push_back({-double(f) * weight(i), i});
	}
	std::stable_sort(farm.begin(), farm.end());
	std::vector<int> chosen;
	for (const auto &entry : farm)
		chosen.push_back(entry.second);
	const int area = int(ground.size());
	plantFields(map, t, chosen, int(scaledCount(area * 7 / 100, o.wheat)),
				int(scaledCount(area * 4 / 100, o.wood)), split);

	const auto pick = [&](const char *stream)
	{
		for (int attempt = 0; attempt < 200; ++attempt)
		{
			const int at = ground[context.bounded(stream, ground.size())];
			if (int(context.bounded(stream, 1000)) < int(weight(at) * 1000) && eligible(at))
				return at;
		}
		return -1;
	};
	const int outcrops = int(scaledCount(std::max(2, area / 2000), o.stone));
	for (int k = 0; k < outcrops; ++k)
		if (const int at = pick("city-stone"); at >= 0)
			placeResourceClump(map, context, {at % t.w, at / t.w}, STONE,
							   1 + int(context.bounded("city-stone", 2)));
	const int groves = int(scaledCount(std::max(3, area / 2500), o.fruit));
	for (int k = 0; k < groves; ++k)
		if (const int at = pick("city-fruit"); at >= 0)
			placeResourceClump(map, context, {at % t.w, at / t.w},
							   CHERRY + int(context.bounded("city-fruit", 3)),
							   1 + int(context.bounded("city-fruit", 2)));
	// The orchard: three groves of the three fruits round the central lake's shore, on the island,
	// or inside the crag.
	if (scaledCount(1, o.fruit) > 0)
	{
		const double rho = L.heartKind == IslandHeart   ? std::max(1.5, 0.45 * L.islandR)
						   : L.heartKind == Crag        ? 0.6 * L.ringR
						   : L.heartKind == ForestHeart ? L.lakeR + 3
														: L.lakeR + kOrchardReach;
		const double spin = context.bounded("city-fruit", 3600) / 3600.0 * 2 * pi;
		for (int f = 0; f < 3; ++f)
		{
			const double a = spin + 2 * pi * f / 3;
			const int ax = L.cx + int(std::lround(rho * std::cos(a))),
					  ay = L.cy + int(std::lround(rho * std::sin(a)));
			int seed = -1, nearest = INT_MAX;
			for (int dy = -8; dy <= 8; ++dy)
				for (int dx = -8; dx <= 8; ++dx)
				{
					const int i = t.at(ax + dx, ay + dy);
					if (eligible(i) && dx * dx + dy * dy < nearest)
					{
						nearest = dx * dx + dy * dy;
						seed = i;
					}
				}
			if (seed >= 0)
				placeResourceClump(map, context, {seed % t.w, seed / t.w}, CHERRY + f, 2);
		}
	}
	// The forest heart: a belt of wood round the orchard that has to be cut through, and that the
	// lake keeps growing back.
	if (L.heartKind == ForestHeart)
		for (int i = 0; i < n; ++i)
			if (eligible(i) && L.radius[i] >= L.lakeR + 2 && L.radius[i] < L.forestR)
				map.setResource(i % t.w, i / t.w, WOOD, 1);
}

// Each island carries one themed prize, as on Ring world.
void stockIslands(Map &map, GenerationContext &context, const std::vector<Island> &islands)
{
	const int width = map.getW();
	for (const Island &island : islands)
	{
		std::vector<MapGeneratorPoint> grass;
		for (int i : island.tiles)
			if (map.isGrass(i % width, i / width))
				grass.emplace_back(i % width, i / width);
		if (grass.empty())
			continue;
		MapGeneratorPoint centre(island.x, island.y);
		if (!map.isGrass(centre.x, centre.y))
			centre = grass[context.bounded("city-islands", grass.size())];
		switch (context.bounded("city-islands", 3))
		{
		case 0:
			placeResourceClump(map, context, centre, STONE, 2);
			break;
		case 1:
			placeResourceClump(map, context, centre,
							   CHERRY + int(context.bounded("city-islands", 3)), 2);
			break;
		default:
			placeResourceClump(map, context, centre, CORN, 2);
			break;
		}
	}
}

// Algae in the shallows along every coast, the strait's, the sea's and the lakes' alike.
void seedAlgae(Map &map, GenerationContext &context, const Torus &t, int algaePercent)
{
	const int n = t.w * t.h;
	std::vector<unsigned char> water(n), dry(n);
	for (int y = 0; y < t.h; ++y)
		for (int x = 0; x < t.w; ++x)
		{
			water[y * t.w + x] = map.isWater(x, y);
			dry[y * t.w + x] = !map.isWater(x, y);
		}
	const std::vector<int> offshore = stepsFrom(t, dry, water);
	std::vector<MapGeneratorPoint> shallows;
	for (int i = 0; i < n; ++i)
		if (offshore[i] >= 2 && offshore[i] <= 4)
			shallows.emplace_back(i % t.w, i / t.w);
	if (shallows.empty())
		return;
	for (int clump = 0; clump < scaledCount(int(shallows.size()) / 90, algaePercent); ++clump)
		placeResourceClump(map, context, shallows[context.bounded("city-algae", shallows.size())],
						   ALGA, 1);
}

// Clears the ring the swarm's workers walk out through; a causeway's stone beside it stays.
void clearAroundSwarms(Map &map, const GenerationContext &context, const Torus &t,
					   const std::vector<unsigned char> &line)
{
	for (int team = 0; team < context.request.nbTeams; ++team)
		for (int dy = -kSwarmClearance; dy < 4 + kSwarmClearance; ++dy)
			for (int dx = -kSwarmClearance; dx < 4 + kSwarmClearance; ++dx)
			{
				const int x = t.x(context.bootX[team] + dx), y = t.y(context.bootY[team] + dy);
				if (map.isResource(x, y) && !line[y * t.w + x])
					map.setNoResource(x, y, 1);
			}
}

// Causeways and their approaches hold nothing but the causeways' own stone lines.
void clearRoads(Map &map, const Layout &L, const std::vector<unsigned char> &line)
{
	const Torus &t = L.t;
	for (int i = 0; i < t.w * t.h; ++i)
		if ((L.causeway[i] || L.clear[i]) && !line[i] && map.isResource(i % t.w, i / t.w))
			map.setNoResource(i % t.w, i / t.w, 1);
}

// The cheapest walk from any source tile to any goal tile (deposits cost one, open ground nothing;
// water, buildings and the stone lines are impassable), with only the deposits on it cleared.
bool openRoad(Map &map, const Layout &L, const std::vector<unsigned char> &line,
			  const std::vector<int> &sources, const std::vector<unsigned char> &goal)
{
	const Torus &t = L.t;
	const int n = t.w * t.h;
	std::vector<int> cost(n, INT_MAX), parent(n, -1);
	std::vector<unsigned char> done(n, 0);
	std::deque<int> queue;
	for (int i : sources)
	{
		cost[i] = 0;
		queue.push_back(i);
	}
	int reached = -1;
	while (!queue.empty() && reached < 0)
	{
		const int i = queue.front();
		queue.pop_front();
		if (done[i])
			continue;
		done[i] = 1;
		if (goal[i])
		{
			reached = i;
			break;
		}
		const int x = i % t.w, y = i / t.w;
		for (int dy = -1; dy <= 1; ++dy)
			for (int dx = -1; dx <= 1; ++dx)
			{
				if (!dx && !dy)
					continue;
				const int nx = t.x(x + dx), ny = t.y(y + dy), next = ny * t.w + nx;
				if (map.isWater(nx, ny) || map.getBuilding(nx, ny) != NOGBID || line[next])
					continue;
				const int nextCost = cost[i] + (map.isResource(nx, ny) ? 1 : 0);
				if (nextCost < cost[next])
				{
					cost[next] = nextCost;
					parent[next] = i;
					if (nextCost == cost[i])
						queue.push_front(next);
					else
						queue.push_back(next);
				}
			}
	}
	if (reached < 0)
		return false;
	for (int i = reached; i >= 0; i = parent[i])
		if (map.isResource(i % t.w, i / t.w))
			map.setNoResource(i % t.w, i / t.w, 1);
	return true;
}

// Deposits may land anywhere, and a band of them could close a home's swarm off from its causeway
// or a landing off from the heart. Keep both walks open, clearing only what stands on the cheapest
// one. Almost always nothing is in the way.
bool openRoads(Map &map, const Layout &L, const std::vector<unsigned char> &line,
			   GenerationContext &context)
{
	const Torus &t = L.t;
	const int n = t.w * t.h;
	const std::vector<unsigned char> heart = heartTiles(map, L);
	for (int k = 0; k < L.g.teams; ++k)
	{
		std::vector<int> workers;
		for (int i = 0; i < n; ++i)
		{
			const Uint16 gid = map.getGroundUnit(i % t.w, i / t.w);
			if (gid != NOGUID && Unit::GIDtoTeam(gid) == k)
				workers.push_back(i);
		}
		std::vector<unsigned char> shore(n, 0);
		shore[L.homes[k].shore] = 1;
		if (!openRoad(map, L, line, workers, shore))
		{
			context.detail = "colony " + std::to_string(k) + " has no way to its causeway";
			return false;
		}
		if (!openRoad(map, L, line, {L.homes[k].landing}, heart))
		{
			context.detail = "causeway " + std::to_string(k) + " has no way into the commons";
			return false;
		}
	}
	return true;
}

bool generate(Game &game, GenerationContext &context)
{
	context.stage = "city layout";
	const CityStatesOptions o(context.request);
	Map &map = game.map;
	const int teams = context.request.nbTeams;
	map.makeHomogenMap(GRASS);
	for (int i = 0; i < teams; ++i)
		game.addTeam();
	const Layout L = design(context.request, context);
	if (!L.failure.empty())
	{
		context.detail = L.failure;
		return false;
	}
	const Torus &t = L.t;
	const int n = t.w * t.h;

	context.stage = "city terrain";
	std::vector<unsigned char> terrain(n, GRASS);
	for (int i = 0; i < n; ++i)
	{
		if (L.region[i] == Sea || L.lake[i] || L.river[i])
			terrain[i] = WATER;
		if (L.ford[i])
			terrain[i] = SAND;
		if (L.sand[i] && terrain[i] == GRASS)
			terrain[i] = SAND;
	}
	carveValleys(terrain, L, context, o.valleys);
	const std::vector<Island> islands = raiseIslands(terrain, L, context, o.resourceIslands);
	layBeaches(terrain, t);
	for (int y = 0; y < t.h; ++y)
		for (int x = 0; x < t.w; ++x)
			map.setUMTerrain(x, y, TerrainType(terrain[y * t.w + x]));
	map.rebuildTerrain();
	const std::vector<unsigned char> line = stoneTiles(map, L);
	for (int i = 0; i < n; ++i)
		if (line[i])
			map.setResource(i % t.w, i / t.w, STONE, 1);

	context.stage = "city colonies";
	for (int team = 0; team < teams; ++team)
	{
		const Home &h = L.homes[team];
		std::vector<unsigned char> home(n, 0);
		for (int i = 0; i < n; ++i)
			home[i] =
				L.homeOf[i] == team && !L.strip[i] && !L.clear[i] && map.isGrass(i % t.w, i / t.w);
		// The swarm stands between the causeway's home end and the lake, a short walk from the lake's
		// fields whatever the home's depth; placeSettlement measures from the footprint's top-left
		// tile, so the anchor is offset to centre the 4x4 there.
		const double lakeRadius = std::clamp(0.1 * L.g.depth(), 2.0, 6.0);
		const double rho =
			std::max(h.coast + L.g.strait + 8.0,
					 h.coast + L.g.strait + lakeOffset(L.g, lakeRadius) - lakeRadius - 9.0);
		const MapGeneratorPoint anchor(L.cx + int(std::lround(rho * std::cos(h.angle))) - 2,
									   L.cy + int(std::lround(rho * std::sin(h.angle))) - 2);
		if (!placeSettlement(game, context, team, home, anchor, "city-starts"))
			return false;
	}

	context.stage = "city resources";
	furnishHomes(map, L, context, o);
	stockCommons(map, L, context, o);
	stockIslands(map, context, islands);
	seedAlgae(map, context, t, o.algae);
	clearAroundSwarms(map, context, t, line);
	// The kits already put wheat and wood a short walk from every swarm; this is only the backstop,
	// and the walls and the causeways' stone are designed and must never be cleared.
	guaranteeStartingResources(game, context, 24, 32, 0, &line);
	clearAroundSwarms(map, context, t, line);
	clearRoads(map, L, line);
	context.stage = "city roads";
	return openRoads(map, L, line, context);
}

std::string validateRequest(const GenerationRequest &r)
{
	if (r.nbTeams < 1)
		return "City states need at least one colony.";
	const Geometry g = geometryFor(r);
	if (g.depth() < kMinimumDepth || g.innerArc() < g.causeway + kMinimumArc)
		return "The commons and the strait leave too little room for the home bases; use a bigger "
			   "map, "
			   "fewer colonies, a smaller commons or a narrower strait.";
	return "";
}

// Checked on the finished world against the rebuilt design: every causeway is open and carries its
// stone lines; every colony can walk to every other and to the heart of the commons; with the
// causeways shut no home reaches another home or the commons at all; and no colony's walk to its
// landing is much longer than another's. Water, buildings and every resource tile block the
// walks; units don't, since they move.
std::string validateWorld(const Game &game, const GenerationContext &context)
{
	GenerationContext replay(context.request);
	const Layout L = design(context.request, replay);
	if (!L.failure.empty())
		return "The city design could not be rebuilt: " + L.failure;
	const Map &map = game.map;
	const Torus &t = L.t;
	const int n = t.w * t.h, teams = context.request.nbTeams;
	if (map.getW() != t.w || map.getH() != t.h)
		return "The city design does not match the map size.";
	const auto where = [&](int i)
	{ return "(" + std::to_string(i % t.w) + ", " + std::to_string(i / t.w) + ")"; };
	const auto walkable = [&](int i)
	{
		const int x = i % t.w, y = i / t.w;
		return !map.isWater(x, y) && !map.isResource(x, y) && map.getBuilding(x, y) == NOGBID;
	};
	const std::vector<unsigned char> line = stoneTiles(map, L);
	for (int i = 0; i < n; ++i)
	{
		if (line[i] && map.getResource(i % t.w, i / t.w).type != STONE)
			return "The stone at " + where(i) + " is missing.";
		if (L.road[i] && !walkable(i))
			return "The causeway at " + where(i) + " is blocked.";
		if (L.ford[i] && !walkable(i))
			return "The ford at " + where(i) + " is blocked.";
	}
	std::vector<std::vector<int>> workers(std::max(teams, 1));
	for (int i = 0; i < n; ++i)
	{
		const Uint16 gid = map.getGroundUnit(i % t.w, i / t.w);
		if (gid != NOGUID && Unit::GIDtoTeam(gid) < teams)
			workers[Unit::GIDtoTeam(gid)].push_back(i);
	}
	if (teams < 1 || workers[0].empty())
		return "Colony 0 has no workers to walk the map.";
	std::vector<unsigned char> open(n, 0), source(n, 0);
	for (int i = 0; i < n; ++i)
		open[i] = walkable(i);
	for (int i : workers[0])
		source[i] = 1;
	const std::vector<int> fromFirst = stepsFrom(t, source, open);
	for (int team = 1; team < teams; ++team)
	{
		bool arrived = false;
		for (int i : workers[team])
			arrived = arrived || fromFirst[i] >= 0;
		if (!arrived)
			return "Colony " + std::to_string(team) + " cannot walk to colony 0.";
	}
	const std::vector<unsigned char> heart = heartTiles(map, L);
	bool heartReached = false;
	for (int i = 0; i < n && !heartReached; ++i)
		heartReached = heart[i] && fromFirst[i] >= 0;
	if (!heartReached)
		return "The heart of the commons cannot be reached on foot.";

	// Shut every causeway road: with the walls up, nothing landing from the sea may get in.
	if (L.g.walls)
	{
		const std::vector<unsigned char> margin = seaMargin(map, L);
		std::vector<unsigned char> beach(n, 0), inland(n, 0);
		for (int i = 0; i < n; ++i)
		{
			inland[i] = walkable(i) && !L.road[i];
			beach[i] = inland[i] && margin[i];
		}
		const std::vector<int> landed = stepsFrom(t, beach, inland);
		for (int team = 0; team < teams; ++team)
			for (int i : workers[team])
				if (landed[i] >= 0)
					return "Colony " + std::to_string(team) +
						   "'s home can be entered from the sea.";
	}
	// Shut every causeway: no home may reach the commons or another home any other way.
	std::vector<unsigned char> shut(n, 0);
	for (int i = 0; i < n; ++i)
		shut[i] = walkable(i) && !L.causeway[i];
	int shortest = INT_MAX, longest = 0;
	for (int team = 0; team < teams; ++team)
	{
		std::vector<unsigned char> from(n, 0);
		for (int i : workers[team])
			from[i] = 1;
		const std::vector<int> inside = stepsFrom(t, from, shut);
		for (int i = 0; i < n; ++i)
			if (inside[i] >= 0 &&
				(L.region[i] == Commons || (L.homeOf[i] >= 0 && L.homeOf[i] != team)))
				return "Colony " + std::to_string(team) +
					   " can leave its home without its causeway, at " + where(i) + ".";
		// And every colony's walk to its landing on the commons, over its own causeway.
		const std::vector<int> steps = stepsFrom(t, from, open);
		const int landing = steps[L.homes[team].landing];
		if (landing < 0)
			return "Colony " + std::to_string(team) + " cannot reach its causeway landing.";
		shortest = std::min(shortest, landing);
		longest = std::max(longest, landing);
	}
	if (longest - shortest > std::max(kLandingSpread, longest / 3))
		return "The colonies' walks to their landings differ by " +
			   std::to_string(longest - shortest) + " steps.";
	return "";
}
} // namespace

CityStatesOptions::CityStatesOptions(const GenerationRequest &r)
	: commonsSize(r.option("commons-size")), straitWidth(r.option("strait-width")),
	  causewayWidth(r.option("causeway-width")), coastRoughness(r.option("coast-roughness")),
	  valleys(r.option("valleys")), resourceIslands(r.option("resource-islands")),
	  sand(r.option("sand")), frontier(r.option("frontier-richness")),
	  stoneWalls(r.option("stone-walls") != 0), wheat(r.option("wheat-amount")),
	  wood(r.option("wood-amount")), stone(r.option("stone-amount")),
	  algae(r.option("algae-amount")), fruit(r.option("fruit-amount"))
{
}

GeneratorDefinition cityStatesDefinition()
{
	return {
		"city-states",
		17,
		"City states",
		4,
		false,
		// The commons' radius as a share of half the shorter side, the strait's width as a
		// share of the shorter side, the causeway's road in tiles; valleys per 128x128 of
		// commons; resource islands per 128x128 of sea.
		{{"commons-size", "Commons size", 30, 65, 5, 55, ControlGroup::Terrain},
		 {"strait-width", "Strait width", 3, 8, 1, 4, ControlGroup::Terrain},
		 {"causeway-width", "Causeway width", 5, 11, 2, 7, ControlGroup::Layout},
		 // Bays and headlands on every coast and the bow in the channels.
		 {"coast-roughness", "Coast roughness", 0, 100, 5, 50, ControlGroup::Terrain},
		 {"valleys", "Valleys", 0, 8, 1, 3, ControlGroup::Terrain},
		 // Patches of sand over the homes and the commons, per 64x128 tiles of land.
		 {"sand", "Sand patches", 0, 8, 1, 3, ControlGroup::Terrain},
		 {"resource-islands", "Resource islands", 0, 6, 1, 2, ControlGroup::Resources},
		 // How much richer the commons' heart is than its shores.
		 {"frontier-richness", "Frontier richness", 0, 100, 10, 60, ControlGroup::Resources},
		 // Off, no stone: the causeways are plain roads and the homes' coasts are open.
		 GeneratorControl::toggle("stone-walls", "Stone walls", true, ControlGroup::Layout),
		 // Every home's ambient fields, outcrops and grove, the commons and the islands' prizes;
		 // every home's kit and the causeways' stone stay as they are.
		 GeneratorControl::percentage("wheat-amount", "Wheat amount"),
		 GeneratorControl::percentage("wood-amount", "Wood amount"),
		 GeneratorControl::percentage("stone-amount", "Stone amount"),
		 GeneratorControl::percentage("algae-amount", "Algae amount"),
		 GeneratorControl::percentage("fruit-amount", "Fruit amount")},
		generate,
		true,
		validateRequest,
		validateWorld};
}
