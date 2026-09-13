// SPDX-License-Identifier: GPL-3.0-or-later
#include "CityStatesGenerator.h"
#include "FertilityField.h"
#include "Game.h"
#include "GenerationContext.h"
#include "Grid.h"
#include "Geometry.h"
#include "HeightMap.h"
#include "Pipeline.h"
#include "Planting.h"
#include "Resources.h"
#include "Roads.h"
#include "Settlements.h"
#include "Sketch.h"
#include "Unit.h"
#include "Wedge.h"
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
//
// GAME RULES BEHIND IT (docs/map-generators/GAME_RULES_FOR_MAP_DESIGN.md): stone can never be
// cleared, so a stone wall is permanent, which is what makes the causeway the only door; water
// stops ground units until they swim, which makes the strait a timer rather than a wall forever;
// wheat and wood regrow only near water, which is why every home has its lake and its fields
// beside it; and fruit lets an inn pull hungry enemy units across, which makes the commons'
// orchard the prize worth leaving home for. The commons' frontier richness (60% by default)
// puts more of its resources toward the centre, so the farther a colony pushes, the more it gains.
//
// RECTANGULAR MAPS. Unlike the other ring-shaped generators, City states stays a circle on the
// shorter side and does not stretch to fill a rectangle. Stretching was tried: the homes then
// have different shapes at different angles, and the fairness score fell from 0.97 to between
// 0.65 and 0.8. Equal homes matter more here than filling the map.
//
// THE SIZES AT THE DEFAULTS (256x256, 4 colonies): a commons 70 tiles in radius, a strait 10 wide,
// homes 42 tiles deep from the strait out to a 6-tile rim of sea at the wrap, and a home lake about
// 4 tiles in radius.
namespace
{

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
			v += amplitude[h] * std::cos(2 * kPi * (first + h) * u + phase[h]);
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
		// Each harmonic weighted 40% to 100% at random and divided by its number to the falloff, so
		// higher harmonics are smaller; the peak is then found by sampling at 720 points and the
		// curve scaled so its peak is exactly one, so the amplitude constants mean what they say.
		p.amplitude[h] = (0.4 + 0.6 * context.bounded(stream, 1000) / 1000.0) /
						 std::pow(double(first + h), falloff);
		p.phase[h] = 2 * kPi * context.bounded(stream, 3600) / 3600.0;
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
		return teams < 2 ? 2 * kPi * innerRadius() : 2 * kPi * innerRadius() / teams - strait;
	}
	// Half the arc a wedge spans at radius d, less half the channel.
	double arcHalf(double d) const { return teams < 2 ? kPi * d : kPi * d / teams - strait / 2.0; }
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
	// Six tenths of the way out: nearer the strait than the sea, so the swarm, between causeway and
	// lake, is a short walk from both, and the land behind the lake is the home's own back country.
	return std::clamp(0.6 * g.depth(), low, std::max(low, high));
}

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

// A roll's coasts: the profiles and amplitudes drawn for it, and where the commons' shore, the
// strait's far side and the homes' outer edge lie at any point of a wedge. All periodic in the
// wedge, so every home gets the same.
struct Coasts
{
	const Geometry &g;
	double wedge, inner, depth;
	Profile coast, ripple, innerBays, outerBays;
	double coastAmp, rippleAmp, innerBayAmp, outerBayAmp, bend;
	// The coasts hold flat for a stretch either side of every causeway, measured in tiles of
	// arc so the stretch is the same however many homes share the ring, and the flanks' bays
	// begin only beyond the lake and its clearance.
	double lakeRadius, lakeReach, flatArc, bayArc;
	// The commons' coast: harmonics 1 to 4 per wedge for bays and headlands, and 6 to 11 at a
	// smaller amplitude for a finer ripple. Profiles are periodic in the wedge, so every wedge gets
	// the same coast. A home's lake is a tenth of its depth, 2 to 6 tiles: water for its fields,
	// not a sea that eats its building room. The coasts hold flat for 4 tiles beyond the causeway
	// and its shoulders and lanes, so the causeway lands square on a straight shore, and bays start
	// only 6 tiles beyond the lake's clearance from the sea.
	Coasts(const Geometry &g, GenerationContext &context)
		: g(g), wedge(2 * kPi / g.teams), inner(g.innerRadius()), depth(g.depth()),
		  coast(rollProfile(context, "city-coast", 1, 4, 1.2)),
		  ripple(rollProfile(context, "city-coast", 6, 6, 0.6)),
		  innerBays(rollProfile(context, "city-coast", 1, 3, 1.0)),
		  outerBays(rollProfile(context, "city-coast", 1, 3, 1.0)),
		  coastAmp(kCoastAmplitude * g.amplitude * g.commonsRadius),
		  rippleAmp(kRippleAmplitude * g.amplitude * g.commonsRadius),
		  innerBayAmp(kInnerBayShare * g.amplitude * depth),
		  outerBayAmp(kOuterBayShare * g.amplitude * depth),
		  bend((context.bounded("city-coast", 2001) / 1000.0 - 1) * kChannelBend),
		  lakeRadius(std::clamp(0.1 * depth, 2.0, 6.0)), lakeReach(1.3 * lakeRadius),
		  flatArc(g.causeway / 2.0 + kShoulder + kLane + 4), bayArc(lakeReach + kLakeSeaGap + 6)
	{
	}
	double coastAt(double u, double d) const
	{
		// The bays fade in over 12 tiles of arc past the flat stretch (10 for the home coasts), so
		// the shore curves into them rather than stepping.
		const double away = awayFromMiddle(d * std::abs(u - 0.5) * wedge, flatArc, 12);
		return g.commonsRadius + (coastAmp * coast.at(u) + rippleAmp * ripple.at(u)) * away;
	}
	double innerAt(double u, double d) const
	{
		return coastAt(u, d) + g.strait +
			   innerBayAmp * std::max(0.0, innerBays.at(u)) *
				   awayFromMiddle(d * std::abs(u - 0.5) * wedge, bayArc, 10);
	}
	double outerAt(double u, double d) const
	{
		return std::min(double(g.half - 3),
						g.outerRadius -
							outerBayAmp * std::max(0.0, outerBays.at(u)) *
								awayFromMiddle(d * std::abs(u - 0.5) * wedge, bayArc - 2, 10));
	}
	double bendAt(double d) const
	{
		const double x = (d - inner) / std::max(1.0, depth);
		return x <= 0 || x >= 1 ? 0.0 : bend * std::sin(kPi * x);
	}
};

struct Patch
{
	int x, y;
	RadialShape shape;
};

// What a roll puts in its homes and on its commons, in the wedge frame: the lakes (the main one
// first), a creek and its ford, the ridges' arcs and the pass radius, how much of a flank the
// barrens take, the sand patches, and the commons' heart with its own sand.
struct Features
{
	double mainLakeR = 0;
	std::vector<Blob> lakes, sandBlobs;
	std::vector<std::pair<double, double>> creek;
	int fordSegment = -1;
	// Highland ridges run at 28% and 72% across the wedge, splitting the home into a middle valley
	// with the lake and two side valleys; Barrens leave the middle 45% of the half arc as grass and
	// put sand beyond it.
	double ridgeU[2] = {0.28, 0.72};
	double passR = 0;
	double barrenShare = 0.45;
	double deltaFordR = 0;
	RadialShape heart;
	std::vector<Patch> commonsSand;
	Features(RadialShape heart, double deltaFordR) : deltaFordR(deltaFordR), heart(heart) {}
};

// Roll the features. Each kind of feature draws from a stream of its own, so the heart, rolled
// first here for its shape, draws the same as it did after the sand.
static Features rollFeatures(const GenerationRequest &request, GenerationContext &context,
							 const Geometry &g, const Coasts &c, Layout &L)
{
	const CityStatesOptions o(request);
	const Torus &t = L.t;
	const double wedge = c.wedge, inner = c.inner, depth = c.depth;
	// The heart of the commons.
	// The heart's shapes as shares of the commons' radius: a lake 22% (15 tiles at the defaults); a
	// crag a small 6% tarn inside a ring of stone at 22%; an island heart a 30% lake round a 12%
	// island; a forest reaching 4 tiles plus 9% beyond the lake. Each is small enough to leave the
	// commons most of its land.
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
	// A delta heart's rivers are forded 60% of the way out from the centre, between the heart and
	// the landings.
	Features f(RadialShape(L.lakeR, L.heartKind == IslandHeart ? 0.2 : 0.3, context, "city-heart"),
			   0.6 * g.commonsRadius);
	// The home's features in the wedge's frame: f.lakes first (the main one first), then whatever
	// the kind adds. A feature keeps clear of the coasts as bays can bring them, of the channels,
	// of the causeway's approach and of other features.
	f.mainLakeR = inner + lakeOffset(g, c.lakeRadius);
	f.lakes.push_back(
		{0.0, f.mainLakeR, 1.0, 0.0, RadialShape(c.lakeRadius, 0.3, context, "city-home-lakes")});
	const auto fits =
		[&](double s0, double r0, double reach, double gap, const std::vector<Blob> &others)
	{
		if (std::abs(s0) + reach + gap > g.arcHalf(r0))
			return false;
		const double u = 0.5 + s0 / (wedge * r0);
		if (r0 - reach - gap < c.innerAt(u, r0) || r0 + reach + gap > c.outerAt(u, r0))
			return false;
		if (r0 - reach < inner + kLanding + 2 && std::abs(s0) < g.causeway + reach + 4)
			return false;
		for (const Blob &b : others)
			if (std::hypot(s0 - b.s, r0 - b.r) < reach + b.shape.maximumRadius() + gap)
				return false;
		return true;
	};
	if (L.homeKind == Marsh)
		// A marsh adds up to 3 small lakes (radius 2 to 4) within 60% of the home's half arc and 8
		// tiles in from its inner and outer edges: more shore to farm, more ground to walk round.
		for (int attempt = 0; attempt < 400 && f.lakes.size() < 4; ++attempt)
		{
			const double r0 =
				inner + 8 + context.bounded("city-home-lakes", 1000) / 1000.0 * (depth - 16);
			const double s0 =
				(context.bounded("city-home-lakes", 2001) / 1000.0 - 1) * 0.6 * g.arcHalf(r0);
			RadialShape shape(2.0 + context.bounded("city-home-lakes", 1000) / 1000.0 * 2.0, 0.3,
							  context, "city-home-lakes");
			if (fits(s0, r0, shape.maximumRadius(), kLakeSeaGap, f.lakes))
				f.lakes.push_back({s0, r0, 1.0, 0.0, shape});
		}
	// Riverside: a f.creek from the main lake towards one flank of the inner coast, with a ford.
	if (L.homeKind == Riverside)
	{
		const double side = context.bounded("city-features", 2) ? 1.0 : -1.0;
		L.creekSide = side;
		// The creek bends out a third of the half arc to one flank, through two control points, and
		// ends 6 tiles past the lake's sea gap from the strait; its ford is the middle segment, so
		// the flank beyond the creek is a second field reached through one crossing.
		const double reachArc = 0.32 * g.arcHalf(f.mainLakeR);
		const double endR = inner + kLakeSeaGap + 6.0;
		f.creek = {{0.0, f.mainLakeR},
				   {side * 0.35 * reachArc, f.mainLakeR - 0.3 * (f.mainLakeR - endR)},
				   {side * 0.75 * reachArc, f.mainLakeR - 0.65 * (f.mainLakeR - endR)},
				   {side * reachArc, endR}};
		f.fordSegment = 1;
	}
	// Highland: two ridges out from the strait to the sea, a pass through each.
	f.passR = inner + 0.5 * depth;
	// Barrens: both flanks of the home are sand, leaving a strip of grass down the middle from the
	// causeway past the lake; the sand keeps clear of every coast so the wall stays on the shore.
	// Sand patches, in every kind of home.
	const double homeArea = wedge * (g.outerRadius * g.outerRadius - inner * inner) / 2;
	// Sand patches per 8192 tiles (the default 3 gives about 2 per home and 6 on the commons at
	// 256x256), radius 3 to 7 with rough outlines, 5 tiles from every lake and patch in a home:
	// they decorate, and slow growth near them, without walling anything off.
	const int homeSand = int(std::lround(o.sand * homeArea / 8192.0));
	for (int attempt = 0; attempt < 400 && int(f.sandBlobs.size()) < homeSand; ++attempt)
	{
		const double r0 = inner + 8 + context.bounded("city-sand", 1000) / 1000.0 * (depth - 16);
		const double s0 = (context.bounded("city-sand", 2001) / 1000.0 - 1) * 0.75 * g.arcHalf(r0);
		RadialShape shape(3.0 + context.bounded("city-sand", 1000) / 1000.0 * 4.0, 0.5, context,
						  "city-sand");
		std::vector<Blob> avoid = f.lakes;
		avoid.insert(avoid.end(), f.sandBlobs.begin(), f.sandBlobs.end());
		if (fits(s0, r0, shape.maximumRadius(), 5, avoid))
			f.sandBlobs.push_back({s0, r0, 1.0, 0.0, shape});
	}

	// Sand on the commons: anywhere between the f.heart and the shore, off the landings.
	const double commonsArea = kPi * g.commonsRadius * g.commonsRadius;
	const int wantedSand = int(std::lround(o.sand * commonsArea / 8192.0));
	for (int attempt = 0; attempt < 400 && int(f.commonsSand.size()) < wantedSand; ++attempt)
	{
		const double a = context.bounded("city-sand", 3600) / 3600.0 * 2 * kPi;
		// Commons sand goes from 45% of the radius out to 12 tiles short of its edge, off the heart
		// and the landings, patches 3 to 8 in radius and 4 tiles apart.
		const double r0 = 0.45 * g.commonsRadius + context.bounded("city-sand", 1000) / 1000.0 *
													   (0.55 * g.commonsRadius - 12);
		RadialShape shape(3.0 + context.bounded("city-sand", 1000) / 1000.0 * 5.0, 0.5, context,
						  "city-sand");
		const double reach = shape.maximumRadius();
		const double turn = std::fmod(a - L.phase + 4 * kPi, 2 * kPi);
		const double u = turn / wedge - std::floor(turn / wedge);
		if (r0 * std::abs(u - 0.5) * wedge < reach + g.causeway + 4 &&
			r0 > g.commonsRadius - kLanding - reach - 4)
			continue;
		if (r0 + reach > c.coastAt(u, r0) - 8)
			continue;
		bool open = true;
		for (const Patch &p : f.commonsSand)
		{
			const int px = L.cx + int(std::lround(r0 * std::cos(a))),
					  py = L.cy + int(std::lround(r0 * std::sin(a)));
			const double dx = t.offsetX(px, p.x), dy = t.offsetY(py, p.y);
			open = open && std::hypot(dx, dy) >= reach + p.shape.maximumRadius() + 4;
		}
		if (open)
			f.commonsSand.push_back({t.x(L.cx + int(std::lround(r0 * std::cos(a)))),
									 t.y(L.cy + int(std::lround(r0 * std::sin(a)))), shape});
	}

	return f;
}

// Classify every tile: which region and home it lies in, the causeway and its clearances, and
// each feature's mask, the same in every wedge; then each home's causeway ends.
static void rasterize(Layout &L, const Geometry &g, const Coasts &coasts, const Features &f)
{
	const Torus &t = L.t;
	const int n = t.w * t.h, teams = g.teams;
	const double wedge = coasts.wedge;
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
	const WedgeFrame frame(t, L.phase, teams);
	for (int y = 0; y < t.h; ++y)
		for (int x = 0; x < t.w; ++x)
		{
			const int i = y * t.w + x;
			// The wedge frame: which home, how far across it and how far along the arc from its
			// middle, with the channels between homes bowed by this roll's bend.
			WedgeFrame::Cell cell = frame.cell(x, y);
			if (teams >= 2)
				frame.bend(cell, coasts.bendAt(cell.d));
			const double d = cell.d, theta = cell.theta, u = cell.u, s = cell.s;
			const int k = cell.k;
			L.radius[i] = d;
			L.angle[i] = theta;
			const double toBoundary = teams < 2 ? 1e9 : d * std::min(u, 1 - u) * wedge;
			const double toLanding = d * std::abs(u - 0.5) * wedge;
			const double c = coasts.coastAt(u, d);
			const double in = coasts.innerAt(u, d), out = coasts.outerAt(u, d);
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
				for (const Blob &b : f.lakes)
					if (b.holds(s - b.s, d - b.r))
						L.lake[i] = 1;
				for (size_t seg = 0; seg + 1 < f.creek.size(); ++seg)
				{
					const double ax = f.creek[seg].first, ay = f.creek[seg].second;
					const double bx = f.creek[seg + 1].first, by = f.creek[seg + 1].second;
					const double len2 = (bx - ax) * (bx - ax) + (by - ay) * (by - ay);
					const double tt = std::clamp(((s - ax) * (bx - ax) + (d - ay) * (by - ay)) /
													 std::max(1e-6, len2),
												 0.0, 1.0);
					const double dist =
						std::hypot(s - (ax + tt * (bx - ax)), d - (ay + tt * (by - ay)));
					// The creek is water within 1.6 tiles of its line, a channel just wide enough
					// to stay water inside its own beaches; its ford is 5 tiles of that channel
					// (2.5 each way), with a clear margin to 3.5.
					if (dist < 1.6)
					{
						if (int(seg) == f.fordSegment && std::abs(tt - 0.5) * std::sqrt(len2) < 2.5)
						{
							L.ford[i] = 1;
							L.clear[i] = 1;
						}
						else
						{
							L.lake[i] = 1;
						}
					}
					else if (int(seg) == f.fordSegment && dist < 3.5 &&
							 std::abs(tt - 0.5) * std::sqrt(len2) < 3.5)
					{
						L.clear[i] = 1;
					}
				}
				if (L.homeKind == Highland && d >= in + 1 && d <= out - 1)
					for (double ur : f.ridgeU)
					{
						const double sr = (ur - 0.5) * wedge * d;
						// A ridge is stone 2.5 tiles across; its pass is 5 tiles along the ridge,
						// with 3.5 and 4.5 tiles of clear ground round it so the pass stays
						// walkable after deposits.
						if (std::abs(s - sr) < 1.25)
						{
							if (std::abs(d - f.passR) < 2.5)
								L.clear[i] = 1;
							else
								L.ridge[i] = 1;
						}
						else if (std::abs(s - sr) < 3.5 && std::abs(d - f.passR) < 4.5)
						{
							L.clear[i] = 1;
						}
					}
				// Barrens' sand keeps 8 tiles from both coasts and the channels, so the home's wall
				// still stands on grass.
				if (L.homeKind == Barrens && d >= in + 8 && d <= out - 8 &&
					std::abs(s) > f.barrenShare * g.arcHalf(d) && std::abs(s) < g.arcHalf(d) - 8)
					L.sand[i] = 1;
				for (const Blob &b : f.sandBlobs)
					if (b.holds(s - b.s, d - b.r))
						L.sand[i] = 1;
			}
			// The f.heart of the commons.
			if (L.region[i] == Commons)
			{
				const bool inHeart = d < f.heart.radiusAt(theta);
				if (L.heartKind == IslandHeart)
				{
					if (inHeart && d >= L.islandR)
						L.lake[i] = 1;
					// An island heart's fords run 4 tiles wide (2 each way) along each causeway's
					// line from the lake shore to the island, so each colony's road leads onto it;
					// 3.5 each way stays clear.
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
					// The crag is a ring of stone 1.5 tiles thick, broken by a 5-tile gap on each
					// causeway's line.
					if (d >= L.ringR && d < L.ringR + 1.5 && toLanding >= 2.5)
						L.ridge[i] = 1;
					if (d >= L.ringR - 2 && d < L.ringR + 3.5 && toLanding < 4)
						L.clear[i] = 1;
				}
				if (L.heartKind == Delta && teams >= 2 && d >= L.lakeR - 1 && d <= c + 1)
				{
					if (toBoundary < 2)
					{
						// A delta's rivers run out along the wedge boundaries 4 tiles wide, cutting
						// the commons into one sector per colony, each river forded 5 tiles long,
						// so neighbours meet at a shared crossing.
						if (std::abs(d - f.deltaFordR) < 2.5)
							L.ford[i] = 1;
						else
							L.river[i] = 1;
					}
					if (toBoundary < 3.5 && std::abs(d - f.deltaFordR) < 4)
						L.clear[i] = 1;
				}
				if (!L.lake[i] && !L.river[i] && !L.ford[i] && !L.clear[i] && !L.causeway[i])
					for (const Patch &p : f.commonsSand)
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
			return;
		}
		Home &h = L.homes[k];
		h.lakeX = t.x(L.cx + int(std::lround(f.mainLakeR * std::cos(h.angle))));
		h.lakeY = t.y(L.cy + int(std::lround(f.mainLakeR * std::sin(h.angle))));
	}
}

Layout design(const GenerationRequest &request, GenerationContext &context)
{
	Layout L;
	L.t = {1 << request.wDec, 1 << request.hDec};
	L.g = geometryFor(request);
	const Torus &t = L.t;
	const Geometry &g = L.g;
	const int teams = g.teams;
	const CityStatesOptions o(request);
	L.cx = t.w / 2;
	L.cy = t.h / 2;
	L.phase = context.bounded("city-layout", 3600) / 3600.0 * 2 * kPi;
	const double wedge = 2 * kPi / teams;
	const double depth = g.depth();

	// What this map is made of.
	L.homeKind = int(context.bounded("city-kind", kHomeKinds));
	L.heartKind = int(context.bounded("city-kind", kHeartKinds));
	if (L.heartKind == Delta && teams < 2)
		L.heartKind = LakeHeart;
	if (depth < kFeatureDepth &&
		(L.homeKind == Riverside || L.homeKind == Highland || L.homeKind == Barrens))
		L.homeKind = Lakeland;

	const Coasts c(g, context);
	for (int k = 0; k < teams; ++k)
	{
		const double a = L.phase + wedge * (k + 0.5);
		L.homes.push_back({a, g.commonsRadius, 0, 0, -1, -1});
	}

	const Features f = rollFeatures(request, context, g, c, L);
	rasterize(L, g, c, f);
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
		// Valley lakes are kept with a chance from 25% at the commons' edge to 100% at its centre:
		// the frontier gets more water, and with it more farmland, the farther in it lies. Valleys
		// per 16384 tiles of commons, radius 3 to 6 (grown with the map), stretched 1.2 to 2 times
		// into long valleys.
		if (int(context.bounded("city-valleys", 100)) >= 25 + int(75 * inward))
			continue;
		const int x = at % t.w, y = at / t.w;
		const double stretch = 1.2 + context.bounded("city-valleys", 81) / 100.0;
		const double turn = context.bounded("city-valleys", 3600) / 3600.0 * kPi;
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

// Every home's kit, identical and unscaled: wheat and wood patches beside its lake, where the lake
// keeps them growing, and a stone deposit further off; then its own scaled ambient farmland,
// outcrops and a grove, as a self-sufficient base needs.
void furnishHomes(Map &map, const Layout &L, GenerationContext &context, const CityStatesOptions &o)
{
	const Torus &t = L.t;
	const int n = t.w * t.h;
	const std::vector<unsigned char> reserved = swarmSurroundings(t, context);
	std::vector<unsigned char> water(n), dry(n);
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
		// The kit sits just past the lake's fullest outline (1.3) and its 3-tile beach, where wheat
		// and wood regrow; the quarry 8 tiles further out behind the lake. With a creek, both crops
		// go to the flank away from it, so a colony never has to cross the ford for its own food.
		const double reach = std::clamp(0.1 * L.g.depth(), 2.0, 6.0) * 1.3 + 3;
		const double flank = L.creekSide != 0 ? -L.creekSide : -1.0;
		const KitFrame frame{h.lakeX, h.lakeY, h.angle};
		const Kit kit = L.creekSide == 0
							? Kit{frame.at(-0.3 * reach, -reach, 14),
								  frame.at(-0.3 * reach, reach, 14), frame.at(reach + 8, 0, 12),
								  kHomeWheat, kHomeWood, 2}
							: Kit{frame.at(-0.5 * reach, flank * reach, 14),
								  frame.at(0.6 * reach, flank * (reach + 1), 14),
								  frame.at(reach + 8, 0, 12), kHomeWheat, kHomeWood, 2};
		plantKit(map, t, context, kit, eligible);
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
			// The patch field (12-tile cells) cut at its 45th percentile keeps 55% of the fertile
			// ground as candidate farmland, in patches with gaps to walk and build in.
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
		const auto bySplit = [&](int i) { return split.uiLevel(i % t.w, i / t.w, 2048); };
		// A home: 4% of its ground ambient wheat and 2% wood on top of the kit, one outcrop per
		// 2500 tiles and a single grove: self-sufficient but not rich, so the commons is worth the
		// trip.
		plantFields(map, t, chosen, int(scaledCount(area * 4 / 100, o.wheat)),
					int(scaledCount(area * 2 / 100, o.wood)), bySplit);
		scatterClumps(context, t, ground, int(scaledCount(std::max(1, area / 2500), o.stone)),
					  "city-home-stone", eligible,
					  [&](MapGeneratorPoint p) { placeResourceClump(map, context, p, STONE, 1); });
		scatterClumps(context, t, ground, int(scaledCount(1, o.fruit)), "city-home-fruit",
					  eligible,
					  [&](MapGeneratorPoint p) {
						  placeResourceClump(map, context, p,
											 CHERRY + int(context.bounded("city-home-fruit", 3)),
											 1);
					  });
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
	// The commons: 7% wheat and 4% wood, nearly twice a home's density, weighted toward the centre;
	// one outcrop per 2000 tiles and one grove per 2500, sampled by the same weight.
	plantFields(map, t, chosen, int(scaledCount(area * 7 / 100, o.wheat)),
				int(scaledCount(area * 4 / 100, o.wood)),
				[&](int i) { return split.uiLevel(i % t.w, i / t.w, 2048); });

	const auto pick = [&](const char *stream)
	{
		// Rejection sampling by the frontier weight: 200 draws finds a spot unless almost nothing
		// is eligible.
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
		const double spin = context.bounded("city-fruit", 3600) / 3600.0 * 2 * kPi;
		for (int f = 0; f < 3; ++f)
		{
			const double a = spin + 2 * kPi * f / 3;
			const int ax = L.cx + int(std::lround(rho * std::cos(a))),
					  ay = L.cy + int(std::lround(rho * std::sin(a)));
			int seed = -1, nearest = INT_MAX;
			// The orchard's three groves sit a third of a turn apart round the heart; each grows
			// from the eligible tile nearest its point within 8 tiles.
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

// Causeways and their approaches hold nothing but the causeways' own stone lines.
void clearRoads(Map &map, const Layout &L, const std::vector<unsigned char> &line)
{
	const Torus &t = L.t;
	for (int i = 0; i < t.w * t.h; ++i)
		if ((L.causeway[i] || L.clear[i]) && !line[i] && map.isResource(i % t.w, i / t.w))
			map.setNoResource(i % t.w, i / t.w, 1);
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
	const auto workers = unitTilesByTeam(map, L.g.teams);
	for (int k = 0; k < L.g.teams; ++k)
	{
		std::vector<unsigned char> shore(n, 0);
		shore[L.homes[k].shore] = 1;
		if (!openRoad(map, t, workers[k], shore, &line))
		{
			context.detail = "colony " + std::to_string(k) + " has no way to its causeway";
			return false;
		}
		if (!openRoad(map, t, {L.homes[k].landing}, heart, &line))
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
	TerrainSketch terrain(n, GRASS);
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
	// Resource islands are counted per 128x128 of sea.
	const std::vector<Island> islands = raiseIslands(
		terrain, t, context,
		{"city-islands",
		 int(std::lround(o.resourceIslands * double(countTiles(terrain, WATER)) / 16384.0)), 60,
		 kLakeShore + 1});
	layBeaches(terrain, t);
	writeUndermap(map, terrain);
	const std::vector<unsigned char> line = stoneTiles(map, L);
	for (int i = 0; i < n; ++i)
		if (line[i])
			map.setResource(i % t.w, i / t.w, STONE, 1);

	context.stage = "city colonies";
	const auto home = [&](int team)
	{
		std::vector<unsigned char> ground(n, 0);
		for (int i = 0; i < n; ++i)
			ground[i] =
				L.homeOf[i] == team && !L.strip[i] && !L.clear[i] && map.isGrass(i % t.w, i / t.w);
		return ground;
	};
	// The swarm stands between the causeway's home end and the lake, a short walk from the lake's
	// fields whatever the home's depth; placeSettlement measures from the footprint's top-left
	// tile, so the anchor is offset to centre the 4x4 there.
	const auto anchor = [&](int team)
	{
		const Home &h = L.homes[team];
		const double lakeRadius = std::clamp(0.1 * L.g.depth(), 2.0, 6.0);
		// The swarm stands 9 tiles short of the lake's near shore, and never within 8 tiles of the
		// strait: between causeway and lake, a short walk from both.
		const double rho =
			std::max(h.coast + L.g.strait + 8.0,
					 h.coast + L.g.strait + lakeOffset(L.g, lakeRadius) - lakeRadius - 9.0);
		return MapGeneratorPoint(L.cx + int(std::lround(rho * std::cos(h.angle))) - 2,
								 L.cy + int(std::lround(rho * std::sin(h.angle))) - 2);
	};
	if (!settleColonies(game, context, "city-starts", home, anchor))
		return false;

	context.stage = "city resources";
	furnishHomes(map, L, context, o);
	stockCommons(map, L, context, o);
	stockIslands(map, context, islands, "city-islands");
	seedAlgae(map, context, t, "city-algae", o.algae, AlgaeBand::shallows(2, 4));
	// The kits already put wheat and wood a short walk from every swarm; this is only the backstop,
	// and the walls and the causeways' stone are designed and must never be cleared.
	secureStartingCrops(game, context, t, 24, 32, 0, &line);
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
	const Map &map = game.map;
	if (const std::string mismatch = designMismatch(L, map, "city"); !mismatch.empty())
		return mismatch;
	const Torus &t = L.t;
	const int n = t.w * t.h, teams = context.request.nbTeams;
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
	const ColonyWalk walk = walkFromFirstColony(map, teams, "the map", "");
	if (!walk.error.empty())
		return walk.error;
	const std::vector<std::vector<int>> &workers = walk.workers;
	const std::vector<int> &fromFirst = walk.steps;
	const std::vector<unsigned char> open = walkableTiles(map);
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
		5,
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
