// SPDX-License-Identifier: GPL-3.0-or-later
#include "SpiderWebGenerator.h"
#include "Drawing.h"
#include "GenerationWorld.h"
#include "GenerationContext.h"
#include "Geometry.h"
#include "Grid.h"
#include "HeightFieldLooks.h"
#include "Morphology.h"
#include "LatticeNoise.h"
#include "Pipeline.h"
#include "Planting.h"
#include "Resources.h"
#include "Roads.h"
#include "Settlements.h"
#include "Sketch.h"
#include "Walls.h"
#include "Wedge.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>
using namespace MapGeneration;

// Spider web: a frost web spun over a frozen sea. Spokes run out from a glacier dome at the centre of
// the map to a frame thread, and capture threads cross between the spokes, sagging towards the hub
// the way silk does; every colony starts on a green pad, a hot-spring valley where a spoke meets the
// frame. The threads are snow-drifted tundra with a white road down every one: fast to walk and
// buildable, but nothing grows on them. The cells between the threads are frozen: walkable without
// swimming, but at half speed and in a cold that slowly wears units down, and nothing can be built
// on them, so they are shortcuts with a price. A share of the cells stays open water (the leads),
// where algae grows and the threads beside them turn green with fields; the open sea beyond the
// frame does the same for the pads. The dew drops are hot-spring islets in the ice, and the hub is a
// glacier dome walled against feet and arrows, climbed by a ramp at every spoke to the orchard round
// its spring: the prize every road leads to.
//
// REVISION 7 (FEEDBACK 2026-11-11: "a full remake of this map in terms of appearance and gameplay"
// around ice and snow: tundra, arctic tundra, white snow and the white roads, still with grassy
// ground for the resources). Before it, the web was grass over open water, crossed only by swimmers.
//
// With the spiral on, the capture threads are a spiral with one arm per colony, climbing one ring
// spacing per wedge, so it maps onto itself under a turn of one wedge. Every other choice (which
// strands are torn, how far each sags, how each spoke bows, where the dew drops hang) is made once
// per wedge, keyed by a thread's place in the wedge, so every colony gets the same web turned round
// the centre and the layout is fair for any colony count. The design is a pure function of the
// request, so validateWorld rebuilds it and checks the finished world.
//
// GAME RULES BEHIND IT (docs/map-generators/game-rules-for-map-design.md, TerrainGroup.h): ice
// (Ice) is walkable at half speed, wears ground units down a little every step (8/256 of a hit
// point) and holds nothing and no building, so a frozen cell is a crossing, never a home; tundra
// (Barren) is building room where nothing grows and which holds crops back like sand; wheat and wood
// regrow only within reach of liquid water, which is why the green ground lies along the open water
// and round the springs; stone stands only on grass, so every quarry has a clearing; glacier
// (Obstacles) stops feet and arrows; and fruit lets an inn pull hungry enemy units across, which
// makes the dome's orchard the prize at the one place every spoke leads.
namespace
{

// A colony's pad and how small it may shrink on a crowded ring before the request is refused; its
// outline wobbles by up to this share of its radius.
constexpr int kPadRadius = 13;
constexpr int kMinimumPad = 7;
// ...and never more than this share of the half side (times the home size control), so a small
// map's pads leave room for the web.
constexpr double kPadShare = 0.16;
constexpr double kPadRoughness = 0.2;
// Sea kept between the pads and their images across the wrap, and between neighbouring pads.
constexpr double kWrapGap = 6;
constexpr double kPadGap = 6;
// A web with fewer spokes than this reads as a star, so small colony counts get more per colony.
constexpr int kMinimumSpokes = 6;
// The free zone between the hub and the first capture thread, and the gap between the last and the
// frame, as shares of the ring spacing.
constexpr double kFreeZone = 0.6;
constexpr double kOuterZone = 0.45;
// Halfway out, neighbouring spokes are at least this many thread widths apart.
constexpr double kSpokeRoom = 2.5;
// A spoke bows sideways by up to this share of the angle between spokes, most at mid-length.
constexpr double kSpokeBow = 0.12;
// At a sag of 100 a capture thread's middle hangs this share of its length towards the hub.
constexpr double kMaximumSag = 0.22;
// A torn strand keeps this share of its length at each end.
constexpr double kTornStub = 0.36;
// Every home starts identical: wheat and wood beside the swarm, a quarry behind it, all unscaled.
constexpr int kHomeWheat = 40;
constexpr int kHomeWood = 30;
// Knots carrying stone, in tenths of a percent at 100, and each one's size in tiles.
constexpr int kKnotStone = 280;
constexpr int kKnotStoneTiles = 5;
// The green threads' standing wheat and wood, as shares of their grass at 100: the open ground is
// the web's breadbasket (three times revision 6's wheat, FEEDBACK 2026-11-11).
constexpr int kWheatShare = 60;
constexpr int kWoodShare = 13;
// One algae clump per this many tiles of shallows at 100, all on the share of each colony's
// shallows where algae regrows most readily.
constexpr int kAlgaeTilesPerClump = 30;
constexpr double kAlgaeBestShare = 0.3;
// The thread width control is the width on a 256-tile map. A smaller map thins its threads with
// the square root of its size, down to this share, so a 128-tile web still reads as threads rather
// than one blob of land, while its threads keep grass either side of their road; bigger maps
// keep the control's width, which already reads as a web.
constexpr double kMinimumThreadScale = 0.75;
// Dew drops: hot-spring islets in the ice, with a spring of this share of their radius at the
// middle, and the ice kept round them.
constexpr double kDewRadius = 6;
constexpr double kDewMoat = 3;
constexpr double kDewSpringShare = 0.3;
// THE FROST WEB (revision 7). Of the cells' distinct keys (a cell's sector in its wedge and its
// radius, shared by its image in every wedge), the share in tenths of a percent left open water as
// leads, so every wedge gets the same leads. Thread ground within kShoreGrass steps of open water (a lead or the sea) is grass, the rest
// tundra. Every pad has a hot spring kPadSpringShare of its radius across, kPadSpringOut of its radius
// to one side of its axis; a stone knot is a grass clearing kKnotClearing tiles across. The dome's
// glacier wall is kGlacierWall tiles thick, broken by a ramp kRampHalf either side of each colony's
// home spoke.
constexpr int kLeadShare = 250;
constexpr int kShoreGrass = 7;
constexpr double kPadSpringShare = 0.18;
constexpr double kPadSpringOut = 0.55;
constexpr double kKnotClearing = 3;
constexpr int kGlacierWall = 4;
constexpr double kRampHalf = 3.5;
constexpr int kSpringSpokeGap = 3;

struct Geometry
{
	int teams, half, perColony, spokes;
	double spokeAngle, offset; // spoke j points at phase + (j + offset) * spokeAngle
	double threadHalf, spokeHalf, hubRadius, pondRadius, padRadius, padReach, frameRadius;
	double spacing, firstRing, lastRing, sag;
	bool spiral, fits;
};

Geometry geometryFor(const GenerationRequest &r)
{
	const SpiderWebOptions o(r);
	Geometry g;
	g.teams = std::max(1, r.nbTeams);
	g.half = std::min(1 << r.wDec, 1 << r.hDec) / 2;
	const double threadWidth =
		o.threadWidth * std::clamp(std::sqrt(g.half / 128.0), kMinimumThreadScale, 1.0);
	g.threadHalf = threadWidth / 2.0;
	g.spokeHalf = g.threadHalf + 1;
	// The hub is a share of the half side, at least 6 tiles; from 9 tiles up it holds a pond 30% of
	// its radius (at least 2), so the orchard round it stands beside water with room for its
	// groves.
	g.hubRadius = std::max(6.0, o.hubSize / 100.0 * g.half);
	g.pondRadius = g.hubRadius >= 9 ? std::max(2.0, 0.3 * g.hubRadius) : 0.0;
	g.spacing = o.ringSpacing;
	g.sag = o.sag / 100.0 * kMaximumSag;
	g.spiral = o.spiral;
	// The pads sit as far out as the wrap allows, and shrink on a crowded ring until they fit.
	// The home size control scales the pad, 100 being kPadRadius: a bigger pad is more room to
	// build the first economy before the colony has to push out along the threads. It is a size to
	// aim for; a crowded ring shrinks it as ever.
	const double homeScale = o.homeSize / 100.0;
	const int largestPad = std::max(
		kMinimumPad,
		int(std::lround(std::min(kPadRadius * homeScale, kPadShare * homeScale * g.half))));
	for (int pad = largestPad; pad >= kMinimumPad; --pad)
	{
		g.padRadius = pad;
		g.padReach = pad * (1 + kPadRoughness);
		g.frameRadius = g.half - g.padReach - kWrapGap / 2;
		g.fits = g.frameRadius > g.hubRadius + g.padReach &&
				 (g.teams < 2 ||
				  2 * g.frameRadius * std::sin(kPi / g.teams) >= 2 * g.padReach + kPadGap);
		if (g.fits)
			break;
	}
	// A few colonies get more spokes each, so the web never reads as a star; many get fewer, so
	// the spokes never crowd into one mass round the hub: halfway out, neighbouring spokes keep
	// well over a thread's width of water between them.
	const double middle = (g.hubRadius + g.frameRadius) / 2;
	const int room = int(2 * kPi * middle / (kSpokeRoom * threadWidth));
	g.perColony = std::max({1, std::min(o.spokes, room / g.teams),
							std::min((kMinimumSpokes + g.teams - 1) / g.teams, room / g.teams)});
	g.spokes = g.perColony * g.teams;
	g.spokeAngle = 2 * kPi / g.spokes;
	// The middle spoke of every wedge ends at its colony's pad.
	g.offset = g.perColony % 2 ? 0.5 : 0.0;
	// A small map squeezes its capture threads closer, down to two thread widths apart, so the web
	// keeps at least one whole turn between the hub and the frame.
	const double span = g.frameRadius - 0.8 * g.padReach - g.hubRadius;
	g.spacing =
		std::min(g.spacing, std::max(2.0 * threadWidth, span / (1 + kFreeZone + kOuterZone)));
	g.firstRing = g.hubRadius + kFreeZone * g.spacing;
	g.lastRing = g.frameRadius - std::max(kOuterZone * g.spacing, 0.8 * g.padReach);
	return g;
}

// A thread's place in its wedge, the same for its image in every wedge: a stateless roll from the
// request's seed, so a strand and its images are torn, sagged and bowed alike.
int roll(const GenerationRequest &r, const char *what, long long key)
{
	return int(GenerationContext::deriveSeed(r.seed, std::string(what) + std::to_string(key)) %
			   1000);
}

int wrapSpoke(long long j, int spokes)
{
	return int(((j % spokes) + spokes) % spokes);
}

struct Thread
{
	ShapePoint from, control, to;
	bool torn;
};

struct Drop
{
	double x, y, turn;
	int prize; // 0, 1, 2 a fruit, 3 stone
};

struct Layout
{
	Torus t{1, 1};
	Geometry g{};
	int cx = 0, cy = 0;
	double phase = 0;
	// The web is designed round the centre in a circle on the map's shorter side; this places it on
	// the map, stretched along the longer side so the web fills a rectangular map.
	Stretch stretch;
	std::vector<double> bow;                // per spoke of a wedge, in radians
	std::vector<unsigned char> land, water; // per tile
	std::vector<unsigned char> hub, thread; // per tile
	// The frost: liquid water (the sea, the leads and the springs) of `water`, the rest of it ice;
	// the innermost cells' ice drawn as wind-swept snow; the dome's glacier wall and its ramps;
	// grass on the threads by open water and round the stone knots.
	std::vector<unsigned char> open, snow, wall, ramp, green;
	int leads = 0, frozen = 0;
	std::vector<int> padOf;                 // pad tiles: the colony, else -1
	std::vector<int> dewOf;                 // dew drop tiles: the drop, else -1
	std::vector<ShapePoint> pads, knots;    // pad centres by colony; stone knots
	std::vector<double> padAngle;           // each pad's heading out from the hub
	std::vector<Drop> drops;
	std::vector<std::vector<ShapePoint>> spokeLines; // every spoke's centre line
	std::vector<std::vector<StrokePoint>> roads;     // every thread's centre line, on the map
	std::vector<int> roadWidth;                      // each road's width in vertices: 2 or 1
	std::string failure;

	/// Where spoke j crosses radius r: it bows sideways most at mid-length and not at all at the
	/// hub or the frame.
	ShapePoint onSpoke(long long j, double r) const
	{
		const double span = std::max(1.0, g.frameRadius - g.hubRadius);
		const double along = std::clamp((r - g.hubRadius) / span, 0.0, 1.0);
		const double angle = phase + (double(j) + g.offset) * g.spokeAngle +
							 bow[wrapSpoke(j, g.perColony)] * std::sin(kPi * along);
		return polarPoint(cx, cy, r, angle);
	}
	/// A capture thread's radius where it meets spoke j on its k-th turn. The spiral climbs one
	/// spacing per wedge, so it has one arm per colony and meets itself under a turn of a wedge.
	double level(long long k, long long j) const
	{
		return g.firstRing + g.spacing * (double(k) + (g.spiral ? double(j) / g.perColony : 0.0));
	}
	/// The key every image of capture thread (k, j) shares: a turn of one wedge takes (k, j) to
	/// (k - 1, j + perColony) on the spiral and to (k, j + perColony) on rings, and both give the
	/// same key.
	long long key(long long k, long long j) const
	{
		return g.spiral ? k * g.perColony + j : k * g.perColony + wrapSpoke(j, g.perColony);
	}
	/// The capture thread from spoke j to spoke j + 1 on turn k, sagging towards the hub; false
	/// when it lies outside the rings.
	bool capture(const GenerationRequest &r, long long k, long long j, Thread &out) const
	{
		const double a = level(k, j), b = level(k, j + 1);
		if (a < g.firstRing - 1e-9 || b > g.lastRing + 1e-9)
			return false;
		const long long id = key(k, j);
		const ShapePoint from = onSpoke(j, a), to = onSpoke(j + 1, b);
		const double mx = (from.x + to.x) / 2, my = (from.y + to.y) / 2;
		const double inward = std::max(1e-9, std::hypot(cx - mx, cy - my));
		// Each strand sags by 60% to 140% of the sag share of its length. The curve's middle
		// control point is set at twice the pull, since a quadratic curve passes only halfway to
		// its control point.
		const double pull = g.sag * std::hypot(to.x - from.x, to.y - from.y) *
							(0.6 + 0.8 * roll(r, "web-sag/", id) / 1000.0);
		out = {from,
			   {mx + (cx - mx) / inward * 2 * pull, my + (cy - my) / inward * 2 * pull},
			   to,
			   false};
		return true;
	}
};

// Strokes a thread onto the mask, and records its centre line as road unless it is torn.
void strokeThread(std::vector<unsigned char> &mask, const Torus &t, Layout &L, const Thread &thread,
				  double halfWidth, int roadWidth)
{
	// A torn strand's stubs are dead ends and carry no traffic, so only whole threads get a road.
	const auto stroke = [&](std::vector<StrokePoint> path, bool road)
	{
		strokePath(mask, t, path);
		if (road)
		{
			L.roads.push_back(std::move(path));
			L.roadWidth.push_back(roadWidth);
		}
	};
	const double length = std::hypot(thread.to.x - thread.from.x, thread.to.y - thread.from.y);
	const int segments = std::max(4, int(length / 3));
	const std::vector<StrokePoint> path = stretchPath(
		bezierPath(thread.from, thread.control, thread.to, halfWidth, halfWidth, segments), L.cx,
		L.cy, L.stretch);
	if (!thread.torn)
	{
		stroke(path, true);
		return;
	}
	const int stub = std::max(1, int(std::lround(kTornStub * segments)));
	stroke(std::vector<StrokePoint>(path.begin(), path.begin() + stub + 1), false);
	stroke(std::vector<StrokePoint>(path.end() - stub - 1, path.end()), false);
}

// Where the round design frame puts tile i, relative to the centre.
ShapePoint roundOf(const Layout &L, int i)
{
	const Torus &t = L.t;
	return L.stretch.undo(t.offsetX(L.cx, t.remainderX(i)), t.offsetY(L.cy, i / t.w));
}

void planFrost(Layout &L, const GenerationRequest &request, GenerationContext &context)
{
	const Torus &t = L.t;
	const Geometry &g = L.g;
	const int n = t.size();
	L.open.assign(n, 0);
	L.snow.assign(n, 0);
	L.wall.assign(n, 0);
	L.ramp.assign(n, 0);
	L.green.assign(n, 0);
	// The springs: the hub's pond, one on every pad and one in every dew drop.
	for (int i = 0; i < n; ++i)
		L.open[i] = L.water[i] && L.hub[i];
	// A spring keeps kSpringSpokeGap tiles off every spoke's centre line, so it never cuts the way in
	// (a squeezed pad on a stretched map can bring its spoke past the spring).
	std::vector<unsigned char> spokeLine(n, 0);
	for (const std::vector<ShapePoint> &line : L.spokeLines)
		for (const ShapePoint &p : line)
			spokeLine[t.at(int(std::lround(p.x)), int(std::lround(p.y)))] = 1;
	const std::vector<int> fromSpoke = stepsFrom(t, spokeLine);
	const auto spring = [&](double x, double y, double radius, const char *stream)
	{
		const RadialShape shape(radius, 0.2, context, stream);
		std::vector<unsigned char> pool(n, 0);
		fillShape(pool, t, int(std::lround(x)), int(std::lround(y)), shape, 0, 1);
		for (int i = 0; i < n; ++i)
			if (pool[i] && (fromSpoke[i] < 0 || fromSpoke[i] > kSpringSpokeGap))
				L.water[i] = L.open[i] = 1;
	};
	for (int k = 0; k < g.teams; ++k)
	{
		const double a = L.padAngle[k], across = kPadSpringOut * g.padRadius;
		spring(L.pads[k].x - across * std::sin(a), L.pads[k].y + across * std::cos(a),
			   std::max(1.5, kPadSpringShare * g.padRadius), "web-springs");
	}
	for (const Drop &drop : L.drops)
		spring(drop.x, drop.y, std::max(1.5, kDewSpringShare * kDewRadius), "web-springs");
	// The cells: every body of water off the land but the springs. One reaching past the pads is
	// the open sea; every other is a cell, frozen unless its sector of the wedge and its radius roll
	// a lead.
	struct Cell
	{
		std::vector<int> members;
		bool sea = false, dew = false;
		double radius = 0;
		long long key = 0;
	};
	std::vector<Cell> cells;
	std::vector<int> body(n, -1), stack;
	for (int start = 0; start < n; ++start)
	{
		if (!L.water[start] || L.open[start] || body[start] >= 0)
			continue;
		Cell cell;
		body[start] = start;
		stack.assign(1, start);
		double sx = 0, sy = 0;
		while (!stack.empty())
		{
			const int i = stack.back();
			stack.pop_back();
			cell.members.push_back(i);
			const ShapePoint p = roundOf(L, i);
			sx += p.x;
			sy += p.y;
			cell.sea = cell.sea || std::hypot(p.x, p.y) > g.frameRadius + g.padReach;
			const int x = t.remainderX(i), y = i / t.w;
			for (const auto &[dx, dy] : {std::pair{1, 0}, {-1, 0}, {0, 1}, {0, -1}})
			{
				const int j = t.at(x + dx, y + dy);
				cell.dew = cell.dew || L.dewOf[j] >= 0;
				if (L.water[j] && !L.open[j] && body[j] < 0)
				{
					body[j] = start;
					stack.push_back(j);
				}
			}
		}
		// A cell's key is its sector in the wedge and its radius, in half spacings, the same for its
		// image in every wedge.
		const double cx = sx / cell.members.size(), cy = sy / cell.members.size();
		cell.radius = std::hypot(cx, cy);
		const double turn = std::fmod(std::atan2(cy, cx) - L.phase + 8 * kPi, 2 * kPi);
		const int sector =
			int(std::floor(turn / g.spokeAngle - g.offset + 1e-9 + 2 * g.spokes)) % g.perColony;
		cell.key = std::lround(2 * (cell.radius - g.firstRing) / g.spacing) * 16 + sector;
		cells.push_back(std::move(cell));
	}
	// The leads: of the distinct keys of the cells that may open (not the sea, nor a cell holding a
	// dew drop, whose islet is reached on foot, nor the innermost round the dome), the kLeadShare
	// with the lowest rolls, at least one; every cell with one of those keys opens.
	std::vector<std::pair<int, long long>> keys;
	for (const Cell &cell : cells)
		if (!cell.sea && !cell.dew && cell.radius >= g.firstRing)
			keys.push_back({roll(request, "web-lead/", cell.key), cell.key});
	std::sort(keys.begin(), keys.end());
	keys.erase(std::unique(keys.begin(), keys.end()), keys.end());
	const size_t leadKeys =
		keys.empty() ? 0 : std::max<size_t>(1, size_t(std::lround(keys.size() * kLeadShare / 1000.0)));
	std::vector<long long> leadKey;
	for (size_t k = 0; k < leadKeys; ++k)
		leadKey.push_back(keys[k].second);
	for (const Cell &cell : cells)
	{
		const bool lead =
			cell.sea || (!cell.dew && cell.radius >= g.firstRing &&
						 std::find(leadKey.begin(), leadKey.end(), cell.key) != leadKey.end());
		if (!cell.sea)
			++(lead ? L.leads : L.frozen);
		for (const int i : cell.members)
		{
			L.open[i] = lead;
			L.snow[i] = !lead && cell.radius < g.firstRing;
		}
	}
	// The dome: its glacier wall round the hub's edge, broken by a ramp along every spoke.
	std::vector<unsigned char> outside(n, 0);
	for (int i = 0; i < n; ++i)
		outside[i] = !L.hub[i];
	const std::vector<int> fromOutside = stepsFrom(t, outside);
	for (int i = 0; i < n; ++i)
	{
		if (!L.hub[i] || L.water[i] || fromOutside[i] > kGlacierWall)
			continue;
		const ShapePoint p = roundOf(L, i);
		bool ramp = false;
		// One ramp per colony, up its home spoke (the middle spoke of its wedge); the other spokes end
		// at the wall.
		for (int j = g.perColony / 2; j < g.spokes && !ramp; j += g.perColony)
		{
			const double a = L.phase + (j + g.offset) * g.spokeAngle;
			const double along = p.x * std::cos(a) + p.y * std::sin(a),
						 across = -p.x * std::sin(a) + p.y * std::cos(a);
			ramp = along > 0 && std::abs(across) <= kRampHalf;
		}
		(ramp ? L.ramp : L.wall)[i] = 1;
	}
	// Green ground on the threads: within reach of open water (a lead or the sea), and a clearing
	// round every stone knot.
	std::vector<unsigned char> liquid(n, 0);
	for (int i = 0; i < n; ++i)
		liquid[i] = L.open[i] && !L.hub[i] && L.padOf[i] < 0 && L.dewOf[i] < 0;
	const std::vector<int> fromLiquid = stepsFrom(t, liquid);
	for (int i = 0; i < n; ++i)
		L.green[i] = L.thread[i] && fromLiquid[i] >= 0 && fromLiquid[i] <= kShoreGrass;
	for (const ShapePoint &knot : L.knots)
		for (int i = 0; i < n; ++i)
			if (L.thread[i] && t.dist2(t.remainderX(i), i / t.w, int(std::lround(knot.x)), int(std::lround(knot.y))) <=
								   kKnotClearing * kKnotClearing)
				L.green[i] = 1;
}

Layout design(const GenerationRequest &request, GenerationContext &context)
{
	Layout L;
	L.t = {1 << request.wDec, 1 << request.hDec};
	L.g = geometryFor(request);
	const Torus &t = L.t;
	const Geometry &g = L.g;
	const SpiderWebOptions o(request);
	const int n = t.size(), teams = g.teams, m = g.perColony;
	if (!g.fits)
	{
		L.failure = "the colonies' pads do not fit round the web";
		return L;
	}
	L.cx = t.w / 2;
	L.cy = t.h / 2;
	L.stretch = Stretch::toFill(t.w, t.h);
	L.phase = context.bounded("web-layout", 3600) / 3600.0 * 2 * kPi;
	for (int j = 0; j < m; ++j)
		L.bow.push_back((roll(request, "web-bow/", j) / 500.0 - 1) * kSpokeBow * g.spokeAngle);
	L.land.assign(n, 0);
	L.hub.assign(n, 0);
	L.padOf.assign(n, -1);
	L.dewOf.assign(n, -1);

	// The spokes, tapering from the hub to the frame.
	for (int j = 0; j < g.spokes; ++j)
	{
		std::vector<StrokePoint> path;
		std::vector<ShapePoint> line;
		for (double r = g.hubRadius / 2;; r = std::min(g.frameRadius, r + 3))
		{
			const ShapePoint p = L.onSpoke(j, r);
			const double along = (r - g.hubRadius / 2) / (g.frameRadius - g.hubRadius / 2);
			path.push_back({p.x, p.y, g.spokeHalf - along * (g.spokeHalf - g.threadHalf - 0.5)});
			line.push_back(L.stretch.apply(L.cx, L.cy, p));
			if (r >= g.frameRadius)
				break;
		}
		std::vector<StrokePoint> placed = stretchPath(path, L.cx, L.cy, L.stretch);
		strokePath(L.land, t, placed);
		L.roads.push_back(std::move(placed));
		L.roadWidth.push_back(2);
		L.spokeLines.push_back(std::move(line));
	}
	// The frame: a thread between every two neighbouring spoke ends, sagging inwards.
	for (int j = 0; j < g.spokes; ++j)
	{
		const ShapePoint from = L.onSpoke(j, g.frameRadius), to = L.onSpoke(j + 1, g.frameRadius);
		const double mx = (from.x + to.x) / 2, my = (from.y + to.y) / 2;
		const double inward = std::max(1e-9, std::hypot(L.cx - mx, L.cy - my));
		const double pull = 0.5 * g.sag * std::hypot(to.x - from.x, to.y - from.y);
		strokeThread(L.land, t, L,
					 {from,
					  {mx + (L.cx - mx) / inward * 2 * pull, my + (L.cy - my) / inward * 2 * pull},
					  to,
					  false},
					 g.spokeHalf, 2);
	}
	// The capture threads: every turn of the spiral, or every ring, from spoke to spoke. A torn
	// strand keeps its two ends hanging from the spokes. Where a thread leaves a spoke is a knot.
	// The torn share is exact: of the distinct strands of a wedge, those with the lowest rolls
	// tear.
	const long long turns = (long long)std::ceil((g.lastRing - g.firstRing) / g.spacing) + 1;
	std::vector<std::pair<int, long long>> strands;
	for (long long k = -teams - 1; k <= turns; ++k)
		for (long long j = 0; j < g.spokes; ++j)
			if (Thread thread; L.capture(request, k, j, thread))
				strands.push_back({roll(request, "web-tear/", L.key(k, j)), L.key(k, j)});
	std::sort(strands.begin(), strands.end());
	strands.erase(std::unique(strands.begin(), strands.end()), strands.end());
	std::vector<long long> torn;
	for (size_t s = 0; s < size_t(std::lround(strands.size() * o.tornStrands / 100.0)); ++s)
		torn.push_back(strands[s].second);
	for (long long k = -teams - 1; k <= turns; ++k)
		for (long long j = 0; j < g.spokes; ++j)
		{
			Thread thread;
			if (!L.capture(request, k, j, thread))
				continue;
			const long long id = L.key(k, j);
			thread.torn = std::find(torn.begin(), torn.end(), id) != torn.end();
			strokeThread(L.land, t, L, thread, g.threadHalf, 1);
			if (roll(request, "web-knot/", id) <
				std::min<std::int64_t>(1000, scaledCount(kKnotStone, o.stone)))
				L.knots.push_back(L.stretch.apply(L.cx, L.cy, thread.from));
		}

	// The hub, with a pond at its heart, and a pad for every colony where its spoke meets the
	// frame.
	const RadialShape hubShape(g.hubRadius, 0.2, context, "web-hub");
	fillShape(L.land, t, L.cx, L.cy, hubShape, 0, 1, L.stretch);
	fillShape(L.hub, t, L.cx, L.cy, hubShape, 0, 1, L.stretch);
	const RadialShape padShape(g.padRadius, kPadRoughness, context, "web-pads");
	for (int k = 0; k < teams; ++k)
	{
		const long long spoke = (long long)k * m + m / 2;
		const ShapePoint centre = L.stretch.apply(L.cx, L.cy, L.onSpoke(spoke, g.frameRadius));
		const double heading = L.phase + (double(spoke) + g.offset) * g.spokeAngle;
		L.pads.push_back(centre);
		L.padAngle.push_back(L.stretch.heading(heading));
		forEachTileInShape(
			t, centre.x, centre.y, padShape, heading,
			[&](int i, double, double)
			{
				L.land[i] = 1;
				L.padOf[i] = k;
			},
			L.stretch);
	}

	// Dew drops: islets hanging in the web's cells, chosen in one wedge where the water is widest
	// and hung in every wedge by turning them round the centre, each well clear of every thread.
	if (o.dewDrops > 0)
	{
		const std::vector<int> fromLand = stepsFrom(t, L.land);
		const RadialShape dewShape(kDewRadius, 0.25, context, "web-dew");
		const int need = int(std::ceil(dewShape.maximumRadius() + kDewMoat));
		const WedgeFrame wedges(t, L.phase, teams, L.stretch);
		// Inside the frame's sagging chords, not out at sea between them.
		const double enclosed =
			g.frameRadius * std::cos(g.spokeAngle / 2) * (1 - g.sag) - g.spokeHalf - need;
		std::vector<int> open;
		for (int y = 0; y < t.h; ++y)
			for (int x = 0; x < t.w; ++x)
			{
				const int i = y * t.w + x;
				const WedgeFrame::Cell cell = wedges.cell(x, y);
				if (cell.k != 0 || cell.d > enclosed || fromLand[i] < need)
					continue;
				bool peak = true;
				for (int dy = -1; dy <= 1 && peak; ++dy)
					for (int dx = -1; dx <= 1 && peak; ++dx)
						peak = fromLand[t.at(x + dx, y + dy)] <= fromLand[i];
				if (peak)
					open.push_back(i);
			}
		context.shuffle(open.begin(), open.end(), "web-dew");
		std::vector<int> chosen;
		for (int i : open)
		{
			bool apart = true;
			for (int c : chosen)
				apart = apart && t.dist2(t.remainderX(i), i / t.w, t.remainderX(c), c / t.w) >= 4 * need * need;
			if (!apart || int(chosen.size()) >= o.dewDrops)
				continue;
			const int prize = int(chosen.size()) % 4;
			chosen.push_back(i);
			// Turned round the centre in the round design frame, then stretched back onto the map.
			const ShapePoint round =
				L.stretch.undo(t.offsetX(L.cx, t.remainderX(i)), t.offsetY(L.cy, i / t.w));
			for (int w = 0; w < teams; ++w)
			{
				const double turn = w * 2 * kPi / teams;
				const ShapePoint at =
					L.stretch.apply(L.cx, L.cy,
									{L.cx + round.x * std::cos(turn) - round.y * std::sin(turn),
									 L.cy + round.x * std::sin(turn) + round.y * std::cos(turn)});
				const int drop = int(L.drops.size());
				L.drops.push_back({at.x, at.y, turn, prize});
				forEachTileInShape(
					t, at.x, at.y, dewShape, turn,
					[&](int tile, double, double)
					{
						L.land[tile] = 1;
						L.dewOf[tile] = drop;
					},
					L.stretch);
			}
		}
	}

	L.water.assign(n, 0);
	L.thread.assign(n, 0);
	if (g.pondRadius > 0)
	{
		const RadialShape pond(g.pondRadius, 0.25, context, "web-hub");
		fillShape(L.water, t, L.cx, L.cy, pond, 0, 1, L.stretch);
	}
	for (int i = 0; i < n; ++i)
	{
		L.water[i] = L.water[i] || !L.land[i];
		L.thread[i] = L.land[i] && !L.hub[i] && L.padOf[i] < 0 && L.dewOf[i] < 0;
	}
	planFrost(L, request, context);
	context.telemetry.measure("spider-web.spokes.actual", g.spokes);
	context.telemetry.measure("spider-web.home.radius-fitted", g.padRadius);
	context.telemetry.measure("spider-web.thread.half-width", g.threadHalf);
	context.telemetry.measure("spider-web.rings.spacing-fitted", g.spacing);
	context.telemetry.measure("spider-web.strands.distinct", strands.size());
	context.telemetry.measure("spider-web.strands.torn", torn.size());
	context.telemetry.measure("spider-web.dew-drops.target", o.dewDrops * teams);
	context.telemetry.measure("spider-web.dew-drops.actual", L.drops.size());
	if (int(L.drops.size()) < o.dewDrops * teams)
		context.telemetry.fallback("spider-web.dew-drops.omitted",
								   "Too few separated water pockets fit the requested dew drops.");
	return L;
}

// Every colony's kit, identical and unscaled: wheat and wood on the hub side of the swarm, either
// side of the spoke, and a quarry out beyond the swarm.
void furnishPads(GenerationMap &map, const Layout &L, GenerationContext &context)
{
	const Torus &t = L.t;
	const std::vector<unsigned char> reserved = swarmSurroundings(t, context);
	for (int team = 0; team < L.g.teams; ++team)
	{
		const auto eligible = [&](int i)
		{ return L.padOf[i] == team && !reserved[i] && clearGround(map, t.remainderX(i), i / t.w); };
		const KitFrame frame{int(std::lround(L.pads[team].x)), int(std::lround(L.pads[team].y)),
							 L.padAngle[team]};
		// Wheat and wood 6 tiles to either side of the pad's centre and 5 back from it, stone 8
		// tiles the other way along the pad's axis: the three far enough apart that no patch grows
		// over another, all inside the pad.
		plantKit(
			map, t, context,
			{frame.at(-5, -6, 8), frame.at(-5, 6, 8), frame.at(8, 0, 6), kHomeWheat, kHomeWood, 2},
			eligible);
	}
}

// Stone at the knots, a prize on every dew drop, the orchard of all three fruits round the hub's
// pond with a quarry, then the threads' own wheat and wood in patches. The patches are sampled in
// the wedge's frame, so every colony's stretch of web is farmed alike.
void stockWeb(GenerationMap &map, const Layout &L, GenerationContext &context,
			  const SpiderWebOptions &o)
{
	const Torus &t = L.t;
	const int n = t.size();
	// Fields and stone stand only on the threads' green ground: tundra holds nothing.
	const auto onThread = [&](int i)
	{ return L.thread[i] && L.green[i] && clearGround(map, t.remainderX(i), i / t.w); };
	for (const ShapePoint &knot : L.knots)
		if (const int seed =
				seedNear(t, int(std::lround(knot.x)), int(std::lround(knot.y)), 3, onThread);
			seed >= 0)
			growPatch(map, t, seed, STONE, kKnotStoneTiles, onThread);

	for (size_t d = 0; d < L.drops.size(); ++d)
	{
		const Drop &drop = L.drops[d];
		const auto onDrop = [&](int i)
		{ return L.dewOf[i] == int(d) && clearGround(map, t.remainderX(i), i / t.w); };
		const int seed = seedNear(t, int(std::lround(drop.x)), int(std::lround(drop.y)), 3, onDrop);
		if (seed < 0)
			continue;
		const bool stone = drop.prize == 3;
		if (scaledCount(1, stone ? o.stone : o.fruit) > 0)
			// A dew drop's prize is a patch of 9 tiles of fruit or stone: a reason to swim out, not an
			// economy.
			growPatch(map, t, seed, stone ? STONE : CHERRY + drop.prize, 9, onDrop);
	}

	// The orchard and the quarry stand on the dome's plateau, inside its wall and off its ramps.
	const auto onHub = [&](int i)
	{ return L.hub[i] && !L.wall[i] && !L.ramp[i] && clearGround(map, t.remainderX(i), i / t.w); };
	// The orchard's groves stand 4 tiles past the pond's fullest outline (1.25), clear of its
	// beach, or halfway out on a hub without a pond.
	const double rho = L.g.pondRadius > 0 ? L.g.pondRadius * 1.25 + 4 : 0.5 * L.g.hubRadius;
	const double spin = context.bounded("web-hub", 3600) / 3600.0 * 2 * kPi;
	if (scaledCount(1, o.fruit) > 0)
		for (int f = 0; f < 3; ++f)
		{
			const ShapePoint p =
				L.stretch.apply(L.cx, L.cy, polarPoint(L.cx, L.cy, rho, spin + 2 * kPi * f / 3));
			if (const int seed =
					seedNear(t, int(std::lround(p.x)), int(std::lround(p.y)), 6, onHub);
				seed >= 0)
				placeResourceClump(map, context, {t.remainderX(seed), seed / t.w}, CHERRY + f, 2);
		}
	if (scaledCount(1, o.stone) > 0)
	{
		const ShapePoint p =
			L.stretch.apply(L.cx, L.cy, polarPoint(L.cx, L.cy, rho + 2, spin + kPi / 3));
		if (const int seed = seedNear(t, int(std::lround(p.x)), int(std::lround(p.y)), 6, onHub);
			seed >= 0)
			placeResourceClump(map, context, {t.remainderX(seed), seed / t.w}, STONE, 2);
	}

	const WedgeFrame wedges(t, L.phase, L.g.teams, L.stretch);
	// patch (12-tile cells) ranks where on the threads the fields go, so they come in runs with
	// bare thread between; split (7-tile cells) picks wheat or wood, so a run of fields changes
	// crop now and then. Both are read in the wedge's frame, so every colony's threads are farmed
	// alike.
	const PeriodicNoise patch(t.w, t.h, 12, context.stream("web-patch"));
	const PeriodicNoise split(t.w, t.h, 7, context.stream("web-split"));
	std::vector<std::pair<double, int>> ranked;
	std::vector<double> splitKey(n, 0.0);
	for (int i = 0; i < n; ++i)
		if (onThread(i))
		{
			const WedgeFrame::Cell cell = wedges.cell(t.remainderX(i), i / t.w);
			ranked.push_back({-patch.at(cell.s, cell.d), i});
			splitKey[i] = split.at(cell.s, cell.d);
		}
	std::stable_sort(ranked.begin(), ranked.end());
	std::vector<int> tiles;
	for (const auto &entry : ranked)
		tiles.push_back(entry.second);
	const std::int64_t area = std::int64_t(tiles.size());
	plantFields(map, t, tiles, int(scaledCount(area * kWheatShare / 100, o.wheat)),
				int(scaledCount(area * kWoodShare / 100, o.wood)),
				[&](int i) { return splitKey[i]; });
}

// The web's terrain, a pure function of the layout: `mechanics` holds one canonical type per rule
// (GRASS, SAND, DIRT, WATER, ICE, CLIFFS, TRAIL), which every placement and check below reads, and `looks`
// the artwork painted over it last (composeLooks), which can never change a rule.
struct WebTerrain
{
	TerrainSketch mechanics, looks;
	int spokeRoad = 0, threadRoad = 0, ice = 0, tundra = 0, glacier = 0, beach = 0;
};

WebTerrain sketchTerrain(const Layout &L, GenerationContext &context)
{
	const Torus &t = L.t;
	const int n = t.size();
	WebTerrain T;
	TerrainSketch &terrain = T.mechanics;
	terrain.assign(n, GRASS);
	for (int i = 0; i < n; ++i)
	{
		if (L.wall[i])
			terrain[i] = CLIFFS;
		else if (L.water[i])
			terrain[i] = L.open[i] ? WATER : ICE;
		else if (L.thread[i] && !L.green[i])
			terrain[i] = DIRT;
	}
	// Pale beaches where grass meets open water (the sea, a lead or a spring), as layBeaches lays
	// them but on grass alone: the tundra and the ice run straight into the water.
	{
		const TerrainSketch original(terrain);
		for (int i = 0; i < n; ++i)
		{
			if (original[i] != GRASS)
				continue;
			const int x = t.remainderX(i), y = i / t.w;
			bool shore = false;
			for (int dy = -1; dy <= 1 && !shore; ++dy)
				for (int dx = -1; dx <= 1 && !shore; ++dx)
					shore = original[t.at(x + dx, y + dy)] == WATER;
			if (shore)
			{
				terrain[i] = SAND;
				++T.beach;
			}
		}
	}
	// The white roads, off the pads and the dew drops so the homes keep their building room, and
	// stopping at the dome, which they climb by its ramps. Nothing grows on or across a road.
	std::vector<unsigned char> spoke(n, 0);
	if (SpiderWebOptions(context.request).sandRoads)
	{
		std::vector<unsigned char> wide(n, 0), narrow(n, 0);
		for (size_t r = 0; r < L.roads.size(); ++r)
			tracePath(L.roadWidth[r] >= 2 ? wide : narrow, t, L.roads[r]);
		const auto onSilk = [&](int i)
		{ return (terrain[i] == GRASS || terrain[i] == DIRT) && !L.hub[i] && L.padOf[i] < 0 && L.dewOf[i] < 0; };
		T.spokeRoad = paveRoute(terrain, t, wide, 2, TRAIL, onSilk);
		T.threadRoad = paveRoute(terrain, t, narrow, 1, TRAIL, onSilk);
	}
	for (int i = 0; i < n; ++i)
		if (L.ramp[i])
			terrain[i] = TRAIL;

	// The looks, one per rule group: lush grass, pale sand, snow-drifted arctic tundra, frozen lake (wind-swept
	// snow on the innermost cells round the dome, a named feature), the glacier and the white
	// moonstone road.
	T.looks.assign(n, 0);
	for (int i = 0; i < n; ++i)
	{
		switch (terrain[i])
		{
		case GRASS:
			T.looks[i] = LUSH_GRASS;
			break;
		case SAND:
			T.looks[i] = PALE_SAND;
			break;
		case DIRT:
			T.looks[i] = ARCTIC_TUNDRA;
			++T.tundra;
			break;
		case ICE:
			T.looks[i] = L.snow[i] ? WIND_SWEPT_SNOW : FROZEN_LAKE;
			++T.ice;
			break;
		case CLIFFS:
			T.looks[i] = GLACIER;
			++T.glacier;
			break;
		case TRAIL:
			T.looks[i] = MOONSTONE_LOZENGE;
			break;
		default:
			break;
		}
	}
	return T;
}

bool generate(GenerationWorld &game, GenerationContext &context)
{
	context.stage = "web layout";
	const SpiderWebOptions o(context.request);
	GenerationMap &map = game.map;
	const int teams = context.request.nbTeams;
	map.fillTerrain(WATER);
	for (int i = 0; i < teams; ++i)
		game.addTeam();
	const Layout L = design(context.request, context);
	if (!L.failure.empty())
	{
		context.detail = L.failure;
		return false;
	}
	const Torus &t = L.t;
	const int n = t.size();

	context.stage = "web terrain";
	const WebTerrain terrain = sketchTerrain(L, context);
	writeVertices(map, terrain.mechanics);
	context.telemetry.measure("spider-web.silk.spoke-vertices", terrain.spokeRoad);
	context.telemetry.measure("spider-web.silk.thread-vertices", terrain.threadRoad);
	context.telemetry.measure("spider-web.frost.ice-vertices", terrain.ice);
	context.telemetry.measure("spider-web.frost.tundra-vertices", terrain.tundra);
	context.telemetry.measure("spider-web.frost.glacier-vertices", terrain.glacier);
	context.telemetry.measure("spider-web.frost.beach-vertices", terrain.beach);
	context.telemetry.measure("spider-web.frost.leads", L.leads);
	context.telemetry.measure("spider-web.frost.frozen-cells", L.frozen);

	context.stage = "web colonies";
	const auto pad = [&](int team)
	{
		std::vector<unsigned char> home(n, 0);
		for (int i = 0; i < n; ++i)
			home[i] = L.padOf[i] == team && map.terrainPropertiesAt(t.remainderX(i), i / t.w).buildable;
		return home;
	};
	// The swarm stands a little out from the pad's middle, away from the hub, so the kit and the
	// spoke's landing lie in front of it; placeSettlement measures from the footprint's top-left.
	const auto anchor = [&](int team)
	{
		const ShapePoint p = polarPoint(L.pads[team].x, L.pads[team].y, 3, L.padAngle[team]);
		return MapGeneratorPoint(int(std::lround(p.x)) - 2, int(std::lround(p.y)) - 2);
	};
	if (!settleColonies(game, context, "web-starts", pad, anchor))
		return false;

	context.stage = "web resources";
	furnishPads(map, L, context);
	stockWeb(map, L, context, o);
	// Algae only grows in liquid water with a coast within reach (see algaeGrowthChance): the sea,
	// the leads and the springs. It is counted over their shallows but seeded on each colony's
	// best-growing water, the same number of clumps in every wedge.
	const WedgeFrame algaeWedges(t, L.phase, L.g.teams, L.stretch);
	seedAlgae(map, context, t, "web-algae", o.algae,
			  AlgaeBand::shallows(1, 10, kAlgaeTilesPerClump).thriving(kAlgaeBestShare), &algaeWedges);
	secureStartingCrops(game, context, t);

	// Beaches keep the threads walkable, but a small hub can be stocked shut: keep a way open from
	// colony 0 onto the hub and to every other colony, clearing only the deposits in the way.
	context.stage = "web roads";
	const std::vector<std::vector<int>> workers = unitTilesByTeam(map, teams);
	if (teams < 1 || workers[0].empty())
	{
		writeLayeredTerrain(map, terrain.mechanics, terrain.looks);
		return true;
	}
	if (!openRoad(map, t, workers[0], L.hub))
	{
		context.detail = "no walk from colony 0 reaches the hub";
		return false;
	}
	for (int team = 1; team < teams; ++team)
		if (!openRoad(map, t, workers[0], tileMask(t, workers[team])))
		{
			context.detail = "no walk from colony 0 reaches colony " + std::to_string(team);
			return false;
		}
	// The looks last: every placement and repair above read the canonical terrain.
	context.stage = "web looks";
	writeLayeredTerrain(map, terrain.mechanics, terrain.looks);
	return true;
}

std::string validateRequest(const GenerationRequest &r)
{
	if (r.nbTeams < 1)
		return "Spider webs need at least one colony.";
	if (!geometryFor(r).fits)
		return "Too many colonies for this map; use a bigger map or fewer colonies.";
	return "";
}

// Checked on the finished world: every spoke is still land from the hub to the frame, and colony 0
// can walk to every other colony and onto the hub, with water, buildings and every resource
// blocking.
std::string validateWorld(const GenerationWorld &game, const GenerationContext &context)
{
	GenerationContext replay(context.request);
	const Layout L = design(context.request, replay);
	const GenerationMap &map = game.map;
	if (const std::string mismatch = designMismatch(L, map, "web"); !mismatch.empty())
		return mismatch;
	const Torus &t = L.t;
	// Every road, wall, ice field and green patch stands as designed, vertex for vertex, looks
	// included.
	{
		const WebTerrain designed = sketchTerrain(L, replay);
		const TerrainSketch terrain = composeLooks(designed.mechanics, designed.looks);
		for (int i = 0; i < t.size(); ++i)
			if (map.vertexTerrainAt(t.remainderX(i), i / t.w) != terrain[i])
				return "The web at (" + std::to_string(t.remainderX(i)) + ", " + std::to_string(i / t.w) +
					   ") is not as designed.";
	}
	// Every spoke is land from the dome to the frame (the dome is checked below: a spoke other than
	// a colony's home spoke ends at its glacier wall).
	for (size_t j = 0; j < L.spokeLines.size(); ++j)
		for (const ShapePoint &p : L.spokeLines[j])
		{
			const int x = t.x(int(std::lround(p.x))), y = t.y(int(std::lround(p.y)));
			const bool dome = L.hub[size_t(y) * t.w + x] || cornerOf(t, L.wall, t.at(x, y)) ||
							  L.wall[t.at(x, y)] || L.wall[t.at(x + 1, y)] || L.wall[t.at(x, y + 1)] ||
							  L.wall[t.at(x + 1, y + 1)];
			if (!dome && !map.terrainPropertiesAt(x, y).walkable)
				return "Spoke " + std::to_string(j) + " is broken by water at (" + std::to_string(x) + ", " +
					   std::to_string(y) + ").";
		}
	const ColonyWalk walk =
		walkFromFirstColony(map, context.request.nbTeams, "the web", "along the web");
	if (!walk.error.empty())
		return walk.error;
	for (int i = 0; i < t.size(); ++i)
		if (L.hub[i] && walk.steps[i] >= 0)
			return "";
	return "The hub cannot be reached along the web.";
}
} // namespace

SpiderWebOptions::SpiderWebOptions(const GenerationRequest &r)
	: spokes(r.option("spokes")), ringSpacing(r.option("ring-spacing")),
	  threadWidth(r.option("thread-width")), sag(r.option("sag")),
	  tornStrands(r.option("torn-strands")), hubSize(r.option("hub-size")),
	  dewDrops(r.option("dew-drops")), homeSize(r.option("home-size")),
	  spiral(r.option("spiral") != 0), sandRoads(r.option("sand-roads") != 0),
	  wheat(r.option("wheat-amount")), wood(r.option("wood-amount")),
	  stone(r.option("stone-amount")), algae(r.option("algae-amount")),
	  fruit(r.option("fruit-amount"))
{
}

GeneratorDefinition spiderWebDefinition()
{
	return {
		"spider-web",
		20,
		"Spider web",
		// Revision 5: the frosted web, drawn from the terrain catalogue (paved silk, dark deep cells,
		// moss dew drops and a hub meadow, pale beaches, no sand patches). Revision 6: algae regrows
		// near any coast now, so its best-growing water moved. Revision 7: the frost web, frozen
		// cells with open leads, tundra threads, hot-spring pads and islets, a glacier dome.
		7,
		false,
		// Spokes per colony (more on a web with few colonies); the spacing between capture threads
		// and every thread's width in tiles; how far the capture threads sag and the share of them
		// torn, in percent; the hub's radius as a share of the half side; dew drops per colony.
		{GeneratorControl{"spokes", "Spokes per colony", 1, 3, 1, 2, ControlGroup::Layout}
			 .withSearchRange(2, 3),
		 GeneratorControl{"ring-spacing", "Ring spacing", 20, 40, 2, 28, ControlGroup::Terrain}
			 .withSearchRange(24, 34),
		 GeneratorControl{"thread-width", "Thread width", 7, 15, 2, 11, ControlGroup::Terrain}
			 .withSearchRange(11, 15),
		 GeneratorControl{"sag", "Sag", 0, 100, 10, 70, ControlGroup::Terrain}.withSearchRange(30,
																							   80),
		 GeneratorControl{"torn-strands", "Torn strands", 0, 60, 5, 15, ControlGroup::Terrain}
			 .withSearchRange(5, 30),
		 GeneratorControl{"hub-size", "Hub size", 8, 24, 1, 14, ControlGroup::Terrain}
			 .withSearchRange(12, 20),
		 GeneratorControl{"dew-drops", "Dew drops", 0, 4, 1, 2, ControlGroup::Layout}
			 .withSearchRange(1, 3),
		 // Each home pad's radius as a percentage of the standard pad.
		 GeneratorControl{"home-size", "Home size", 60, 200, 10, 130, ControlGroup::Layout}
			 .withSearchRange(120, 170),
		 // Off, the capture threads are closed rings rather than a spiral.
		 GeneratorControl::toggle("spiral", "Spiral", true, ControlGroup::Layout)
			 .withSearchValues({0, 1}),
		 // Off, the threads are grass from shore to shore, with no paved road down the middle.
		 // Search keeps roads open as adjacent crops and trees regrow. The ID predates the paving.
		 GeneratorControl::toggle("sand-roads", "Paved roads", true, ControlGroup::Layout)
			 .withSearchValues({1}),
		 // The threads' standing wheat and wood, the knots' stone and the drops' and hub's prizes;
		 // every pad's kit is unscaled.
		 GeneratorControl::percentage("wheat-amount", "Wheat amount"),
		 GeneratorControl::percentage("wood-amount", "Wood amount"),
		 GeneratorControl::percentage("stone-amount", "Stone amount"),
		 GeneratorControl::percentage("algae-amount", "Algae amount"),
		 GeneratorControl::percentage("fruit-amount", "Fruit amount")},
		generate,
		true,
		validateRequest,
		validateWorld,
		{"terrain:arena", "feature:snow", "feature:oases", "feature:islands", "style:contested-center",
		 "fairness:repeated-wedge"}};
}
