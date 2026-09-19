// SPDX-License-Identifier: GPL-3.0-or-later
#include "CityStatesGenerator.h"
#include "FertilityField.h"
#include "Game.h"
#include "GenerationContext.h"
#include "Grid.h"
#include "Drawing.h"
#include "Farmland.h"
#include "Geometry.h"
#include "HeightMap.h"
#include "Homes.h"
#include "Pipeline.h"
#include "Planting.h"
#include "Resources.h"
#include "Roads.h"
#include "Settlements.h"
#include "Sketch.h"
#include "Unit.h"
#include "Walls.h"
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
// several layouts (a lake, a creek with a ford, stone ridges with passes, a marsh) and the heart of
// the commons one of several (a lake, a stone crag, an island, a river delta, a forest), and sand
// lies in patches over the ground. Everything a home is made of is designed in the wedge's own
// frame - arc across it and radius out from the centre - and stamped into every home alike, so
// however a roll comes out, every colony gets the same home turned round the centre, and the layout
// is fair for any colony count.
//
// THE ARCHIPELAGO (FEEDBACK 2026-09-14: "WAY more islands out filling the no mans land where there
// is currently only ocean. it should be like a mini archipelago dotting the landscape. Additionally
// I want those islands to all contain the 10x4 little sand building plots with no resources in
// them so that users have a spot to place buildings"; and later that day: they belong "in the area
// outside the circle ... in like the empty space where the torus wraps", and bigger, since the
// plot "is taking up too much of the space"). The design circle sits on the map's shorter side, so
// the sea outside it - a square's four corners, which the torus joins into one ocean round the
// point across the map from the centre, and a rectangle's bands - held nothing. It now holds round
// islets, kIsletRadius in radius, each with an axis-aligned 10x4 clearing of grass in a ring of
// sand at its middle - the same building plot the farms use (Farmland.h) - and a small prize on its
// grass beyond the ring. Their middles lie on rings round each wrap point (rasterize): the point
// itself, then `islands` rings of eight, sixteen and so on at equal angles, so the set has the
// map's own four-fold symmetry with mirrors; an islet whose disc and kIsletMoat of water round it
// do not lie wholly in the sea - too near a home's outer coast, or another islet - is left out
// together with its mirror images, which keeps the symmetry. An islet is only ever reached by
// swimming: a forward post for whoever swims first, not a stepping stone. The archipelago cannot
// be turned round the centre for every colony like the rest of the design, so with three, five or
// six colonies it lies nearer some homes than others; the lobby's choice of the fairest of several
// rolls covers that, as on Canals. A 128 map's corners hold only the islet on the wrap point. The
// former random "resource islands" went with this.
//
// The Barrens home layout - both flanks of the home under sand - went the same day ("that one
// just feels lame when you receive it"), so a home is one of four kinds.
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
// homes 42 tiles deep from the strait out to a 6-tile rim of sea at the wrap, a home lake about 4
// tiles in radius, and an archipelago of thirteen islets of radius 11 round the wrap point: one on
// it, a ring of eight round that, and four more out along the axes.
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
// Sand roads (see planSandRoads). The ring road runs at this share of the commons' radius, halfway
// from the landings to the centre, and keeps this much ground from the heart's outer edge and
// from the landings' clear approaches. In a home, roads keep this far from any water, so they never
// reach a wall on its shore, and in the commons this far; the branches reach
// at most this share of the home's depth in from the strait, leaving the rest to build on.
constexpr double kRingShare = 0.5;
constexpr double kRingHeartGap = 5.0;
constexpr int kRoadSeaGap = 5;
constexpr int kRoadWaterGap = 2;
constexpr int kRoadSandGap = 3;
constexpr double kRoadDepthShare = 0.5;
constexpr double kStreetInset = 7.0;
// Sand vertices across a road.
constexpr int kRoadWidth = 2;
// The islets (see the header): radius (11 since the plot "is taking up too much of the space" at 9)
// and roughness (nearly round, so the axis-aligned 10x4 plot of grass, whose far corner is 5.4 tiles
// out, sits well inside the beach with room to build round it: the grass reaches 10.45 - 1 beach =
// 9.5 at the roughest), the water kept between an islet and any coast or other islet, and how far
// from the middle the prize stands, clear of the ring.
constexpr double kIsletRadius = 11.0, kIsletRoughness = 0.05;
constexpr int kIsletMoat = 3, kIsletPrizeOut = 7;

// What a home is made of, and what the heart of the commons is. One of each per map.
enum HomeKind
{
	Lakeland = 0,
	Riverside,
	Highland,
	Marsh,
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
	g.commonsRadius = std::lround(o.commonsSize / 100.0 * g.half);
	g.outerRadius = g.half - g.rim;
	// The strait is a share of the side, but never so wide that the homes lose the depth a home needs.
	g.strait = std::max(4, int(std::lround(o.straitWidth / 100.0 * side)));
	g.strait =
		std::max(4, std::min(g.strait, int(g.outerRadius - g.commonsRadius) - kMinimumDepth));
	g.causeway = o.causewayWidth;
	g.walls = o.stoneWalls;
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
	HomeLand = 2,
	Islet = 3
};

// Where the swarm stands along its home's axis, from the centre: 9 tiles short of the lake's near
// shore, and never within 8 tiles of the strait, between causeway and lake.
double swarmRadius(const Geometry &g, double coast)
{
	const double lakeRadius = std::clamp(0.1 * g.depth(), 2.0, 6.0);
	return std::max(coast + g.strait + 8.0,
					coast + g.strait + lakeOffset(g, lakeRadius) - lakeRadius - 9.0);
}

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
	// The sand roads: undermap vertices turned to sand, and the tiles with a corner on one.
	std::vector<unsigned char> sandRoad, roadTile;
	std::vector<double> radius, angle;
	// The islets' middles, where their 10x4 plots are stamped and their prizes stand, with each
	// islet's place among its wedge's (the prize kind goes round by it).
	struct IsletSite
	{
		int x, y, place;
	};
	std::vector<IsletSite> plots;
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
	// with the lake and two side valleys.
	double ridgeU[2] = {0.28, 0.72};
	double passR = 0;
	double deltaFordR = 0;
	RadialShape heart;
	std::vector<Patch> commonsSand;
	// The archipelago: every islet's outline, and how many rings of them round each wrap point.
	RadialShape islet;
	int isletRings = 0;
	Features(RadialShape heart, RadialShape islet, double deltaFordR)
		: deltaFordR(deltaFordR), heart(heart), islet(islet)
	{
	}
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
			   RadialShape(kIsletRadius, kIsletRoughness, context, "city-islets"),
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

	// The archipelago is laid on the map in rasterize; only how many rings is decided here.
	f.isletRings = o.islands;

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
	L.plots.clear();
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
	// The archipelago (see the header): islets on rings round every wrap point - the point across
	// the torus from the centre, which is a square map's four corners at once, and on a rectangle
	// also the point across the wrap on each axis, where the circle on the shorter side leaves a
	// band of sea (on a square those two lie on the circle and seat nothing). Ring k, for k from 1
	// to `islands`, carries 8k candidates at equal angles, a pitch of two radii and the moat out
	// from the ring before and far enough out for its neighbours to keep that pitch too. A
	// candidate's whole disc with kIsletMoat of water round it must be sea, which keeps it off
	// every home's outer coast and clear of the islets already raised, and its images under the
	// map's symmetries are raised or dropped with it. Three is the least moat that leaves a tile
	// of pure water: the beach pass sands the land vertices beside water, and a tile with a sand
	// corner is no longer water to walk on. The plots and prizes stand at every middle raised; the
	// prize kind goes round by the islet's place.
	{
		const double reach = f.islet.maximumRadius();
		const double pitch = 2 * reach + kIsletMoat + 1;
		const int moat = kIsletMoat, span = int(std::ceil(reach)) + moat;
		const auto fits = [&](int x, int y)
		{
			for (int dy = -span; dy <= span; ++dy)
				for (int dx = -span; dx <= span; ++dx)
					if (std::hypot(dx, dy) < f.islet.radiusAt(std::atan2(dy, dx)) + moat &&
						L.region[t.at(x + dx, y + dy)] != Sea)
						return false;
			return true;
		};
		const auto raise = [&](int x, int y)
		{
			for (int dy = -span; dy <= span; ++dy)
				for (int dx = -span; dx <= span; ++dx)
					if (std::hypot(dx, dy) < f.islet.radiusAt(std::atan2(dy, dx)))
						L.region[t.at(x + dx, y + dy)] = Islet;
			L.plots.push_back({x, y, int(L.plots.size())});
		};
		// A candidate offset from a focus and its images under the square's symmetries, each once.
		const auto orbit = [&](int fx, int fy, double u, double v)
		{
			const std::pair<double, double> images[] = {{u, v}, {-u, v}, {u, -v}, {-u, -v},
														{v, u}, {-v, u}, {v, -u}, {-v, -u}};
			std::vector<std::pair<int, int>> points;
			for (const auto &[a, b] : images)
			{
				const std::pair<int, int> point{t.x(fx + int(std::lround(a))),
												t.y(fy + int(std::lround(b)))};
				if (std::find(points.begin(), points.end(), point) == points.end())
					points.push_back(point);
			}
			for (const auto &[x, y] : points)
				if (!fits(x, y))
					return;
			for (const auto &[x, y] : points)
				raise(x, y);
		};
		L.plots.clear();
		const int cx = int(L.cx), cy = int(L.cy);
		std::vector<std::pair<int, int>> foci = {{t.x(cx + t.w / 2), t.y(cy + t.h / 2)}};
		if (t.w != t.h)
		{
			foci.push_back({t.x(cx + t.w / 2), t.y(cy)});
			foci.push_back({t.x(cx), t.y(cy + t.h / 2)});
		}
		for (const auto &[fx, fy] : foci)
		{
			orbit(fx, fy, 0, 0);
			double rho = 0;
			for (int k = 1; k <= f.isletRings; ++k)
			{
				rho = std::max(rho + pitch, pitch / (2 * std::sin(kPi / (8 * k))));
				for (int j = 0; j <= k; ++j)
				{
					const double a = j * (kPi / 4) / k;
					orbit(fx, fy, rho * std::cos(a), rho * std::sin(a));
				}
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

// The sand roads: a line of sand two vertices thick (tracePath, widened) that no deposit can grow over and
// nothing can be built on, like a real road that growth closes in on from both sides. It makes
// the causeways and the commons' main routes stay walkable however the fields spread, while the
// fields right beside a road turn it into a chokepoint.
//
// Every home gets the same road, drawn in its own frame (along its axis from the map's centre,
// and across it): from the heart of the commons out along the axis, over the causeway, to a fork
// a few tiles inside the gate; two arms swing out round either side of the swarm's square and
// each forks again, one twig turning in behind the swarm towards the lake and one out towards the
// flank. Nothing reaches more than half the home's depth in from the strait, so most of the home
// stays open for buildings. A ring road at half the commons' radius joins every home's road; with
// a river delta at the heart it runs at the delta's fords instead, so it crosses the rivers where
// they can be walked.
//
// A vertex is only turned to sand where that cannot break the design: on land, off every ridge
// and causeway shoulder (stone stands only on grass, so sand there would open a gap in a wall),
// at least kRoadSeaGap from any water inside a home (off the coast wall) and kRoadWaterGap from
// water in the commons, and clear of the swarm's square. Where a road meets water or a wall it simply
// breaks off, as a road stops at a lake shore.
void planSandRoads(Layout &L, const Features &f)
{
	const Torus &t = L.t;
	const Geometry &g = L.g;
	const int n = t.w * t.h, teams = g.teams;
	L.sandRoad.assign(n, 0);
	L.roadTile.assign(n, 0);
	std::vector<unsigned char> water(n, 0), forbidden(n, 0);
	for (int i = 0; i < n; ++i)
	{
		water[i] = L.region[i] == Sea || L.lake[i] || L.river[i];
		forbidden[i] = L.ridge[i] || (L.strip[i] && !L.road[i]);
	}
	const std::vector<int> fromWater = stepsFrom(t, water), fromSand = stepsFrom(t, L.sand);

	std::vector<unsigned char> line(n, 0);
	const auto trace = [&](const std::vector<StrokePoint> &path) { tracePath(line, t, path); };
	std::vector<std::pair<int, int>> swarms; // each home's swarm footprint, top-left
	const double arcHalf = g.arcHalf(g.innerRadius());
	for (int k = 0; k < teams; ++k)
	{
		const Home &h = L.homes[k];
		const double ca = std::cos(h.angle), sa = std::sin(h.angle);
		// A point `along` tiles out from the centre on the home's axis and `across` to one side.
		const auto at = [&](double along, double across) -> ShapePoint
		{ return {L.cx + along * ca - across * sa, L.cy + along * sa + across * ca}; };
		const double gate = h.coast + g.strait, swarm = swarmRadius(g, h.coast) - gate;
		// The main street crosses the axis kStreetInset tiles in from the gate, following the
		// strait at that distance out to either flank; side streets turn inland from it past both
		// sides of the swarm and at its ends, and stop a few tiles behind the swarm, short of the
		// lake and the kit's fields beside it.
		// Between the coast wall and the swarm's square: never inside the square, and never nearer
		// the strait than the wall allows.
		const double street = std::clamp(swarm - 5, double(kRoadSeaGap), kStreetInset);
		const double side = std::clamp(0.35 * arcHalf, 6.0, 11.0);
		const double reach = std::min(std::max(side + 8.0, 0.55 * arcHalf), 24.0);
		const double deep = std::min(swarm + 4, kRoadDepthShare * g.depth());
		trace({{at(0, 0).x, at(0, 0).y, 0}, {at(gate + street, 0).x, at(gate + street, 0).y, 0}});
		for (double sign : {-1.0, 1.0})
		{
			std::vector<StrokePoint> main;
			const double radius = gate + street;
			for (int step = 0; step <= 12; ++step)
			{
				const double across = sign * reach * step / 12;
				const double a = across / radius;
				const ShapePoint p = at(radius * std::cos(a), radius * std::sin(a));
				main.push_back({p.x, p.y, 0});
			}
			trace(main);
			const auto inland = [&](double across, double to)
			{
				const double a = across / radius;
				const ShapePoint from = at(radius * std::cos(a), radius * std::sin(a));
				const ShapePoint end = at((gate + to) * std::cos(a), (gate + to) * std::sin(a));
				trace({{from.x, from.y, 0}, {end.x, end.y, 0}});
			};
			inland(sign * side, deep);
			if (reach > side + 6)
				inland(sign * reach, std::max(street + 4, deep - 4));
		}
		swarms.push_back({int(std::lround(L.cx + (gate + swarm) * ca)) - 2,
						  int(std::lround(L.cy + (gate + swarm) * sa)) - 2});
	}
	if (teams >= 1)
	{
		const double heartEdge = L.heartKind == IslandHeart   ? L.lakeR * 1.3
								 : L.heartKind == Crag        ? L.ringR + 2
								 : L.heartKind == ForestHeart ? std::max(L.forestR, L.lakeR * 1.3)
															  : L.lakeR * 1.3 + kOrchardReach + 2;
		double ring = L.heartKind == Delta ? f.deltaFordR : kRingShare * g.commonsRadius;
		ring = std::max(ring, heartEdge + kRingHeartGap);
		if (ring <= g.commonsRadius - kLanding - 4)
		{
			const int segments = std::max(24, int(2 * kPi * ring / 3));
			std::vector<StrokePoint> circle;
			for (int s = 0; s <= segments; ++s)
			{
				const double a = L.phase + 2 * kPi * s / segments;
				circle.push_back({L.cx + ring * std::cos(a), L.cy + ring * std::sin(a), 0});
			}
			trace(circle);
		}
	}

	// Two vertices wide: every traced vertex also marks the vertices right, below and diagonally
	// below-right of it, a square brush that is two thick whatever way the road runs. A road then
	// spoils three tiles across, wide enough to read as a road and still a chokepoint between
	// fields.
	std::vector<unsigned char> wide(n, 0);
	for (int i = 0; i < n; ++i)
		if (line[i])
			for (int dy = 0; dy < kRoadWidth; ++dy)
				for (int dx = 0; dx < kRoadWidth; ++dx)
					wide[t.at(i % t.w + dx, i / t.w + dy)] = 1;
	for (int i = 0; i < n; ++i)
	{
		if (!wide[i] || water[i] || fromWater[i] < kRoadWaterGap)
			continue;
		const int x = i % t.w, y = i / t.w;
		// A home's lakes can be walled too, where their beach joins the sea's, so inside a home a
		// road keeps the wall's distance from every water.
		// A sand patch that touches a beach is walled as part of the shore, so a home's roads also
		// keep off sand patches by a wall's width.
		if (L.homeOf[i] >= 0 && !L.strip[i] &&
			(fromWater[i] < kRoadSeaGap || (fromSand[i] >= 0 && fromSand[i] < kRoadSandGap)))
			continue;
		// The four tiles sharing this corner: none may be stone-bearing design.
		bool clear = true;
		for (int dy = -1; dy <= 0 && clear; ++dy)
			for (int dx = -1; dx <= 0; ++dx)
				if (forbidden[t.at(x + dx, y + dy)])
				{
					clear = false;
					break;
				}
		// Nor inside the swarm's square with two tiles to spare round its 4x4 footprint.
		for (const auto &sw : swarms)
			if (clear && std::abs(t.offsetX(sw.first + 2, x)) <= 4 &&
				std::abs(t.offsetY(sw.second + 2, y)) <= 4)
				clear = false;
		if (!clear)
			continue;
		L.sandRoad[i] = 1;
	}
	L.roadTile = roadTiles(t, L.sandRoad);
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
	{
		context.telemetry.fallback("city-states.heart.delta-to-lake",
								   "A delta requires multiple colonies.");
		L.heartKind = LakeHeart;
	}
	if (depth < kFeatureDepth && (L.homeKind == Riverside || L.homeKind == Highland))
	{
		context.telemetry.fallback("city-states.home.to-lakeland",
								   "The home is too shallow for the chosen creek or ridges.");
		L.homeKind = Lakeland;
	}
	const char *homeKinds[] = {"lakeland", "riverside", "highland", "marsh"};
	const char *heartKinds[] = {"lake", "crag", "island", "delta", "forest"};
	context.telemetry.choice("city-states.home.kind", homeKinds[L.homeKind]);
	context.telemetry.choice("city-states.heart.kind", heartKinds[L.heartKind]);
	context.telemetry.measure("city-states.home.depth", depth);
	context.telemetry.measure("city-states.strait.width-fitted", g.strait);

	const Coasts c(g, context);
	for (int k = 0; k < teams; ++k)
	{
		const double a = L.phase + wedge * (k + 0.5);
		L.homes.push_back({a, g.commonsRadius, 0, 0, -1, -1});
	}

	const Features f = rollFeatures(request, context, g, c, L);
	rasterize(L, g, c, f);
	if (L.failure.empty() && o.sandRoads)
		planSandRoads(L, f);
	else
	{
		L.sandRoad.assign(size_t(t.w) * t.h, 0);
		L.roadTile.assign(size_t(t.w) * t.h, 0);
	}
	context.telemetry.measure("city-states.islets.actual", L.plots.size());
	context.telemetry.measure("city-states.home-lakes.actual", f.lakes.size());
	context.telemetry.measure("city-states.home-sand.actual", f.sandBlobs.size());
	context.telemetry.measure("city-states.commons-sand.actual", f.commonsSand.size());
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

// The ground a unit landing from the sea can reach without crossing solid grass (seaMargin): the
// sea is every water vertex outside the homes' lakes and the commons' own water, and a sand road is
// not a beach, so it must not carry the margin inland.
std::vector<unsigned char> seaMargin(const Map &map, const Layout &L)
{
	const Torus &t = L.t;
	const int n = t.w * t.h;
	std::vector<unsigned char> sea(n, 0);
	for (int i = 0; i < n; ++i)
		sea[i] =
			map.getUMTerrain(i % t.w, i / t.w) == WATER && !L.lake[i] && L.region[i] != Commons;
	return MapGeneration::seaMargin(map, t, sea, L.roadTile);
}

// The design's stone, once the terrain is laid: every solid-grass tile of a causeway's shoulders
// outside its road, the ridges and the crag, and round every home a wall on the solid-grass tiles
// that touch the sea's margin (sealCoasts). Every step off the beach lands on the wall, so a home
// is sealed but for its road. Rebuilt the same way by validateWorld.
std::vector<unsigned char> stoneTiles(const Map &map, const Layout &L)
{
	const Torus &t = L.t;
	const int n = t.w * t.h;
	std::vector<unsigned char> stone(n, 0);
	if (!L.g.walls)
		return stone;
	std::vector<unsigned char> wallable(n, 0);
	for (int i = 0; i < n; ++i)
		wallable[i] = L.homeOf[i] >= 0 && !L.road[i];
	stone = sealCoasts(map, t, seaMargin(map, L), wallable);
	for (int i = 0; i < n; ++i)
		if ((L.strip[i] || L.ridge[i]) && !L.road[i] &&
			map.getTerrainType(i % t.w, i / t.w) == GRASS)
			stone[i] = 1;
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
		keep[i] = L.clear[i] || L.causeway[i] || L.sandRoad[i];
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
	context.telemetry.measure("city-states.valleys.target", wanted);
	context.telemetry.measure("city-states.valleys.actual", lakes.size());
	if (int(lakes.size()) < wanted)
		context.telemetry.fallback("city-states.valleys.omitted",
								   "Candidate budget or clearance limited valley lakes.");
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
		const Kit kit = L.creekSide == 0 ? Kit{frame.at(-0.3 * reach, -reach, 14),
											   frame.at(-0.3 * reach, reach, 14),
											   frame.at(reach + 8, 0, 12),
											   kHomeWheat,
											   kHomeWood,
											   2}
										 : Kit{frame.at(-0.5 * reach, flank * reach, 14),
											   frame.at(0.6 * reach, flank * (reach + 1), 14),
											   frame.at(reach + 8, 0, 12),
											   kHomeWheat,
											   kHomeWood,
											   2};
		plantKit(map, t, context, kit, eligible);
		// Ambient farmland on the home's fertile ground, in patches, then outcrops and a grove: 4% of
		// its ground ambient wheat and 2% wood on top of the kit, one outcrop per 2500 tiles and a
		// single grove - self-sufficient but not rich, so the commons is worth the trip. The patch
		// field has 12-tile cells.
		furnishGround(
			map, t, context, fertility, eligible, [&](int i) { return patch(i % t.w, i / t.w); },
			[&](int i) { return split.uiLevel(i % t.w, i / t.w, 2048); },
			[&](int area)
			{
				return GroundAmounts{int(scaledCount(area * 4 / 100, o.wheat)),
									 int(scaledCount(area * 2 / 100, o.wood)),
									 int(scaledCount(std::max(1, area / 2500), o.stone)),
									 int(scaledCount(1, o.fruit))};
			},
			"city-home-stone", "city-home-fruit");
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

// Every islet's prize: one small clump on its grass beyond the plot's ring, kIsletPrizeOut tiles
// below the middle (the same side on every islet), of a kind that goes round by the islet's place
// - stone, then a fruit (the three fruits in turn), then wheat - each scaled by its amount. Small,
// since the islet is a place to build, not a mine.
void stockIslets(Map &map, const Layout &L, GenerationContext &context, const CityStatesOptions &o,
				 const Farm &plots)
{
	const Torus &t = L.t;
	for (const Layout::IsletSite &site : L.plots)
	{
		const int kind = site.place % 3;
		const int type = kind == 0 ? STONE : kind == 1 ? CHERRY + (site.place / 3) % 3 : WHEAT;
		const int amount = kind == 0 ? o.stone : kind == 1 ? o.fruit : o.wheat;
		if (scaledCount(1, amount) <= 0)
			continue;
		const int seed = seedNear(t, site.x, site.y + kIsletPrizeOut, 3,
								  [&](int i)
								  {
									  return L.region[i] == Islet && !plots.plot[i] &&
											 !plots.sand[i] && clearGround(map, i % t.w, i / t.w);
								  });
		if (seed >= 0)
			placeResourceClump(map, context, MapGeneratorPoint(seed % t.w, seed / t.w), type, 1);
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
		if (L.sandRoad[i] && o.cobblestoneRoads && terrain[i] == GRASS)
			terrain[i] = COBBLESTONE;
		else if ((L.sand[i] || L.sandRoad[i]) && terrain[i] == GRASS)
			terrain[i] = SAND;
	}
	carveValleys(terrain, L, context, o.valleys);
	// The islets' building plots (stampFarmPlot, as the farms lay theirs): a 10x4 clearing of grass
	// in a two-vertex ring of sand at every islet's middle, which nothing is planted on and the beach
	// pass leaves alone, so a colony that swims out finds a spot to build on at once.
	Farm isletPlots;
	isletPlots.water.assign(n, 0);
	isletPlots.sand.assign(n, 0);
	isletPlots.plot.assign(n, 0);
	isletPlots.row.assign(n, -1);
	const FarmPlot plot;
	for (const Layout::IsletSite &site : L.plots)
		stampFarmPlot(terrain, t, isletPlots, site.x - plot.width / 2, site.y - plot.height / 2,
					  plot);
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
		const double rho = swarmRadius(L.g, h.coast);
		return MapGeneratorPoint(L.cx + int(std::lround(rho * std::cos(h.angle))) - 2,
								 L.cy + int(std::lround(rho * std::sin(h.angle))) - 2);
	};
	if (!settleColonies(game, context, "city-starts", home, anchor))
		return false;

	context.stage = "city resources";
	furnishHomes(map, L, context, o);
	stockCommons(map, L, context, o);
	stockIslets(map, L, context, o, isletPlots);
	seedAlgae(map, context, t, "city-algae", o.algae, AlgaeBand::shallows(2, 4));
	// The kits already put wheat and wood a short walk from every swarm; this is only the backstop,
	// and the walls and the causeways' stone are designed and must never be cleared.
	secureStartingCrops(game, context, t, 24, 32, 0, &line);
	clearRoads(map, L, line);
	clearFarmPlots(map, t, {isletPlots});
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
		if (map.getUMTerrain(i % t.w, i / t.w) == COBBLESTONE &&
			map.touchesUMTerrain(i % t.w, i / t.w, WATER))
			return "The cobblestone at " + where(i) + " touches water.";
	}
	const ColonyWalk walk = walkFromFirstColony(map, teams, "the map", "");
	if (!walk.error.empty())
		return walk.error;
	const std::vector<std::vector<int>> &workers = walk.workers;
	const std::vector<int> &fromFirst = walk.steps;
	const std::vector<unsigned char> heart = heartTiles(map, L);
	bool heartReached = false;
	for (int i = 0; i < n && !heartReached; ++i)
		heartReached = heart[i] && fromFirst[i] >= 0;
	if (!heartReached)
		return "The heart of the commons cannot be reached on foot.";
	// Every islet keeps its plot clear and buildable, and no islet can be walked to: the moat holds.
	for (const Layout::IsletSite &site : L.plots)
	{
		for (int dy = 0; dy < 4; ++dy)
			for (int dx = 0; dx < 10; ++dx)
			{
				const int x = t.x(site.x - 5 + dx), y = t.y(site.y - 2 + dy);
				if (!map.isGrass(x, y) || map.isResource(x, y) || map.getBuilding(x, y) != NOGBID)
					return "The islet plot at " + where(t.at(site.x, site.y)) +
						   " is not buildable.";
			}
		if (fromFirst[t.at(site.x, site.y)] >= 0)
			return "The islet at " + where(t.at(site.x, site.y)) + " can be walked to.";
	}

	// Shut every causeway road: with the walls up, nothing landing from the sea may get in.
	if (L.g.walls)
	{
		const std::vector<unsigned char> margin = seaMargin(map, L);
		std::vector<unsigned char> beach(n, 0);
		for (int i = 0; i < n; ++i)
			beach[i] = walkable(i) && !L.road[i] && margin[i];
		const std::vector<int> landed = reachesWithShut(map, t, beach, L.road);
		for (int team = 0; team < teams; ++team)
			for (int i : workers[team])
				if (landed[i] >= 0)
					return "Colony " + std::to_string(team) +
						   "'s home can be entered from the sea.";
	}
	// Shut every causeway: no home may reach the commons or another home any other way.
	for (int team = 0; team < teams; ++team)
	{
		const std::vector<int> inside =
			reachesWithShut(map, t, tileMask(t, workers[team]), L.causeway);
		for (int i = 0; i < n; ++i)
			if (inside[i] >= 0 &&
				(L.region[i] == Commons || (L.homeOf[i] >= 0 && L.homeOf[i] != team)))
				return "Colony " + std::to_string(team) +
					   " can leave its home without its causeway, at " + where(i) + ".";
	}
	// And every colony's walk to its landing on the commons, over its own causeway.
	std::vector<int> landings;
	for (int team = 0; team < teams; ++team)
		landings.push_back(L.homes[team].landing);
	const WalkSpread spread = walkSpread(map, t, workers, landings);
	if (spread.unreached >= 0)
		return "Colony " + std::to_string(spread.unreached) + " cannot reach its causeway landing.";
	if (spread.tooUneven(kLandingSpread))
		return "The colonies' walks to their landings differ by " +
			   std::to_string(spread.longest - spread.shortest) + " steps.";
	return "";
}
} // namespace

CityStatesOptions::CityStatesOptions(const GenerationRequest &r)
	: commonsSize(r.option("commons-size")), straitWidth(r.option("strait-width")),
	  causewayWidth(r.option("causeway-width")), coastRoughness(r.option("coast-roughness")),
	  valleys(r.option("valleys")), islands(r.option("islands")), sand(r.option("sand")),
	  frontier(r.option("frontier-richness")), stoneWalls(r.option("stone-walls") != 0),
	  sandRoads(r.option("sand-roads") != 0), cobblestoneRoads(r.option("road-surface") == 1),
	  wheat(r.option("wheat-amount")),
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
			9,
			false,
			// The commons' radius as a share of half the shorter side, the strait's width as a share of
		// the shorter side, the causeway's road in tiles; valleys per 128x128 of commons; rings of
		// islets round the map's wrap point (see the header), 0 for the one on the point alone.
		{{"commons-size", "Commons size", 30, 65, 5, 55, ControlGroup::Terrain},
		 {"strait-width", "Strait width", 3, 14, 1, 4, ControlGroup::Terrain},
		 {"causeway-width", "Causeway width", 5, 11, 2, 7, ControlGroup::Layout},
		 // Bays and headlands on every coast and the bow in the channels.
		 {"coast-roughness", "Coast roughness", 0, 100, 5, 50, ControlGroup::Terrain},
		 {"valleys", "Valleys", 0, 8, 1, 3, ControlGroup::Terrain},
		 // Patches of sand over the homes and the commons, per 64x128 tiles of land.
		 {"sand", "Sand patches", 0, 8, 1, 3, ControlGroup::Terrain},
		 {"islands", "Islands", 0, 4, 1, 2, ControlGroup::Terrain},
		 // How much richer the commons' heart is than its shores.
		 {"frontier-richness", "Frontier richness", 0, 100, 10, 60, ControlGroup::Resources},
		 // Off, no stone: the causeways are plain roads and the homes' coasts are open.
		 GeneratorControl::toggle("stone-walls", "Stone walls", true, ControlGroup::Layout),
		 // Off, no sand roads: the causeways and the commons are grass from shore to shore.
		 GeneratorControl::toggle("sand-roads", "Sand roads", true, ControlGroup::Layout),
		 // What the roads are paved with. Cobblestone doubles the pace along them and takes
		 // buildings, so a city can grow along its road or keep it open for its army; no crop
		 // spreads onto either surface.
		 GeneratorControl::choice("road-surface", "Road surface", {"Sand", "Cobblestone"}, 0,
								  ControlGroup::Layout),
		 // Every home's ambient fields, outcrops and grove, the commons and the islets' prizes;
		 // every home's kit and the causeways' stone stay as they are.
		 GeneratorControl::percentage("wheat-amount", "Wheat amount"),
		 GeneratorControl::percentage("wood-amount", "Wood amount"),
		 GeneratorControl::percentage("stone-amount", "Stone amount"),
		 GeneratorControl::percentage("algae-amount", "Algae amount"),
		 GeneratorControl::percentage("fruit-amount", "Fruit amount")},
			generate,
			true,
			validateRequest,
			validateWorld,
			// The wedge dividers are stone roads/walls, not blue canals, and the city is one landmass
			// (small ring islands in the surrounding ocean are decoration, not a mapped feature).
			{"terrain:urban", "feature:stone-walls", "style:tight-building", "style:contested-center",
			 "fairness:repeated-wedge"}};
}
