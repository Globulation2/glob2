// SPDX-License-Identifier: GPL-3.0-or-later
#include "SpiderWebGenerator.h"
#include "Drawing.h"
#include "Game.h"
#include "GenerationContext.h"
#include "Geometry.h"
#include "Grid.h"
#include "LatticeNoise.h"
#include "Pipeline.h"
#include "Planting.h"
#include "Resources.h"
#include "Roads.h"
#include "Settlements.h"
#include "Sketch.h"
#include "Wedge.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>
using namespace MapGeneration;

// Spider web: an orb web of land spun over open water. Spokes run out from a hub at the centre of
// the map to a frame thread, and capture threads cross between the spokes, sagging towards the hub
// the way silk does; every colony starts on a pad where a spoke meets the frame. Threads are wide
// enough to build on but every ground route follows them, so the knots where threads cross are the
// places to hold, the hub with its orchard is the prize, and the water between is only crossed once
// colonies can swim. Dew drops, small islets in the web's cells, carry a prize each.
//
// With the spiral on, the capture threads are a spiral with one arm per colony, climbing one ring
// spacing per wedge, so it maps onto itself under a turn of one wedge. Every other choice (which
// strands are torn, how far each sags, how each spoke bows, where the dew drops hang) is made once
// per wedge, keyed by a thread's place in the wedge, so every colony gets the same web turned round
// the centre and the layout is fair for any colony count. The design is a pure function of the
// request, so validateWorld rebuilds it and checks the finished world.
namespace
{

// A colony's pad and how small it may shrink on a crowded ring before the request is refused; its
// outline wobbles by up to this share of its radius.
constexpr int kPadRadius = 13;
constexpr int kMinimumPad = 7;
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
// The threads' standing wheat and wood, as shares of their grass at 100.
constexpr int kWheatShare = 8;
constexpr int kWoodShare = 5;
// Dew drops and the water kept round them.
constexpr double kDewRadius = 3;
constexpr double kDewMoat = 3;

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
	g.threadHalf = o.threadWidth / 2.0;
	g.spokeHalf = g.threadHalf + 1;
	g.hubRadius = std::max(6.0, o.hubSize / 100.0 * g.half);
	g.pondRadius = g.hubRadius >= 9 ? std::max(2.0, 0.3 * g.hubRadius) : 0.0;
	g.spacing = o.ringSpacing;
	g.sag = o.sag / 100.0 * kMaximumSag;
	g.spiral = o.spiral;
	// The pads sit as far out as the wrap allows, and shrink on a crowded ring until they fit.
	for (int pad = kPadRadius; pad >= kMinimumPad; --pad)
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
	const int room = int(2 * kPi * middle / (kSpokeRoom * o.threadWidth));
	g.perColony = std::max({1, std::min(o.spokes, room / g.teams),
							std::min((kMinimumSpokes + g.teams - 1) / g.teams, room / g.teams)});
	g.spokes = g.perColony * g.teams;
	g.spokeAngle = 2 * kPi / g.spokes;
	// The middle spoke of every wedge ends at its colony's pad.
	g.offset = g.perColony % 2 ? 0.5 : 0.0;
	// A small map squeezes its capture threads closer, down to two thread widths apart, so the web
	// keeps at least one whole turn between the hub and the frame.
	const double span = g.frameRadius - 0.8 * g.padReach - g.hubRadius;
	g.spacing = std::min(g.spacing,
						 std::max(2.0 * o.threadWidth, span / (1 + kFreeZone + kOuterZone)));
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
	std::vector<double> bow;                 // per spoke of a wedge, in radians
	std::vector<unsigned char> land, water;  // per tile
	std::vector<unsigned char> hub, thread;  // per tile
	std::vector<int> padOf;                  // pad tiles: the colony, else -1
	std::vector<int> dewOf;                  // dew drop tiles: the drop, else -1
	std::vector<ShapePoint> pads, knots;     // pad centres by colony; stone knots
	std::vector<double> padAngle;            // each pad's heading out from the hub
	std::vector<Drop> drops;
	std::vector<std::vector<ShapePoint>> spokeLines; // every spoke's centre line
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
		return g.firstRing +
			   g.spacing * (double(k) + (g.spiral ? double(j) / g.perColony : 0.0));
	}
	/// The key every image of capture thread (k, j) shares: a turn of one wedge takes (k, j) to
	/// (k - 1, j + perColony) on the spiral and to (k, j + perColony) on rings, and both give the
	/// same key.
	long long key(long long k, long long j) const
	{
		return g.spiral ? k * g.perColony + j
						: k * g.perColony + wrapSpoke(j, g.perColony);
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
		const double pull = g.sag * std::hypot(to.x - from.x, to.y - from.y) *
							(0.6 + 0.8 * roll(r, "web-sag/", id) / 1000.0);
		out = {from,
			   {mx + (cx - mx) / inward * 2 * pull, my + (cy - my) / inward * 2 * pull},
			   to,
			   false};
		return true;
	}
};

void strokeThread(std::vector<unsigned char> &mask, const Torus &t, const Thread &thread,
				  double halfWidth)
{
	const double length = std::hypot(thread.to.x - thread.from.x, thread.to.y - thread.from.y);
	const int segments = std::max(4, int(length / 3));
	const std::vector<StrokePoint> path =
		bezierPath(thread.from, thread.control, thread.to, halfWidth, halfWidth, segments);
	if (!thread.torn)
	{
		strokePath(mask, t, path);
		return;
	}
	const int stub = std::max(1, int(std::lround(kTornStub * segments)));
	strokePath(mask, t, std::vector<StrokePoint>(path.begin(), path.begin() + stub + 1));
	strokePath(mask, t, std::vector<StrokePoint>(path.end() - stub - 1, path.end()));
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
			line.push_back(p);
			if (r >= g.frameRadius)
				break;
		}
		strokePath(L.land, t, path);
		L.spokeLines.push_back(std::move(line));
	}
	// The frame: a thread between every two neighbouring spoke ends, sagging inwards.
	for (int j = 0; j < g.spokes; ++j)
	{
		const ShapePoint from = L.onSpoke(j, g.frameRadius), to = L.onSpoke(j + 1, g.frameRadius);
		const double mx = (from.x + to.x) / 2, my = (from.y + to.y) / 2;
		const double inward = std::max(1e-9, std::hypot(L.cx - mx, L.cy - my));
		const double pull = 0.5 * g.sag * std::hypot(to.x - from.x, to.y - from.y);
		strokeThread(L.land, t,
					 {from,
					  {mx + (L.cx - mx) / inward * 2 * pull, my + (L.cy - my) / inward * 2 * pull},
					  to,
					  false},
					 g.spokeHalf);
	}
	// The capture threads: every turn of the spiral, or every ring, from spoke to spoke. A torn
	// strand keeps its two ends hanging from the spokes. Where a thread leaves a spoke is a knot.
	// The torn share is exact: of the distinct strands of a wedge, those with the lowest rolls tear.
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
			strokeThread(L.land, t, thread, g.threadHalf);
			if (roll(request, "web-knot/", id) <
				std::min<std::int64_t>(1000, scaledCount(kKnotStone, o.stone)))
				L.knots.push_back(thread.from);
		}

	// The hub, with a pond at its heart, and a pad for every colony where its spoke meets the frame.
	const RadialShape hubShape(g.hubRadius, 0.2, context, "web-hub");
	fillShape(L.land, t, L.cx, L.cy, hubShape);
	fillShape(L.hub, t, L.cx, L.cy, hubShape);
	const RadialShape padShape(g.padRadius, kPadRoughness, context, "web-pads");
	for (int k = 0; k < teams; ++k)
	{
		const long long spoke = (long long)k * m + m / 2;
		const ShapePoint centre = L.onSpoke(spoke, g.frameRadius);
		const double heading = L.phase + (double(spoke) + g.offset) * g.spokeAngle;
		L.pads.push_back(centre);
		L.padAngle.push_back(heading);
		forEachTileInShape(t, centre.x, centre.y, padShape, heading,
						   [&](int i, double, double)
						   {
							   L.land[i] = 1;
							   L.padOf[i] = k;
						   });
	}

	// Dew drops: islets hanging in the web's cells, chosen in one wedge where the water is widest
	// and hung in every wedge by turning them round the centre, each well clear of every thread.
	if (o.dewDrops > 0)
	{
		const std::vector<int> fromLand = stepsFrom(t, L.land);
		const RadialShape dewShape(kDewRadius, 0.25, context, "web-dew");
		const int need = int(std::ceil(dewShape.maximumRadius() + kDewMoat));
		const WedgeFrame wedges(t, L.phase, teams);
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
				apart = apart && t.dist2(i % t.w, i / t.w, c % t.w, c / t.w) >= 4 * need * need;
			if (!apart || int(chosen.size()) >= o.dewDrops)
				continue;
			const int prize = int(chosen.size()) % 4;
			chosen.push_back(i);
			const double dx = t.offsetX(L.cx, i % t.w), dy = t.offsetY(L.cy, i / t.w);
			for (int w = 0; w < teams; ++w)
			{
				const double turn = w * 2 * kPi / teams;
				const double x = L.cx + dx * std::cos(turn) - dy * std::sin(turn);
				const double y = L.cy + dx * std::sin(turn) + dy * std::cos(turn);
				const int drop = int(L.drops.size());
				L.drops.push_back({x, y, turn, prize});
				forEachTileInShape(t, x, y, dewShape, turn,
								   [&](int tile, double, double)
								   {
									   L.land[tile] = 1;
									   L.dewOf[tile] = drop;
								   });
			}
		}
	}

	L.water.assign(n, 0);
	L.thread.assign(n, 0);
	if (g.pondRadius > 0)
	{
		const RadialShape pond(g.pondRadius, 0.25, context, "web-hub");
		fillShape(L.water, t, L.cx, L.cy, pond);
	}
	for (int i = 0; i < n; ++i)
	{
		L.water[i] = L.water[i] || !L.land[i];
		L.thread[i] = L.land[i] && !L.hub[i] && L.padOf[i] < 0 && L.dewOf[i] < 0;
	}
	return L;
}

// Every colony's kit, identical and unscaled: wheat and wood on the hub side of the swarm, either
// side of the spoke, and a quarry out beyond the swarm.
void furnishPads(Map &map, const Layout &L, GenerationContext &context)
{
	const Torus &t = L.t;
	const std::vector<unsigned char> reserved = swarmSurroundings(t, context);
	for (int team = 0; team < L.g.teams; ++team)
	{
		const auto eligible = [&](int i)
		{ return L.padOf[i] == team && !reserved[i] && clearGround(map, i % t.w, i / t.w); };
		const KitFrame frame{int(std::lround(L.pads[team].x)), int(std::lround(L.pads[team].y)),
							 L.padAngle[team]};
		plantKit(map, t, context,
				 {frame.at(-5, -6, 8), frame.at(-5, 6, 8), frame.at(8, 0, 6), kHomeWheat,
				  kHomeWood, 2},
				 eligible);
	}
}

// Stone at the knots, a prize on every dew drop, the orchard of all three fruits round the hub's
// pond with a quarry, then the threads' own wheat and wood in patches. The patches are sampled in
// the wedge's frame, so every colony's stretch of web is farmed alike.
void stockWeb(Map &map, const Layout &L, GenerationContext &context, const SpiderWebOptions &o)
{
	const Torus &t = L.t;
	const int n = t.size();
	const auto onThread = [&](int i) { return L.thread[i] && clearGround(map, i % t.w, i / t.w); };
	for (const ShapePoint &knot : L.knots)
		if (const int seed = seedNear(t, int(std::lround(knot.x)), int(std::lround(knot.y)), 3,
									  onThread);
			seed >= 0)
			growPatch(map, t, seed, STONE, kKnotStoneTiles, onThread);

	for (size_t d = 0; d < L.drops.size(); ++d)
	{
		const Drop &drop = L.drops[d];
		const auto onDrop = [&](int i)
		{ return L.dewOf[i] == int(d) && clearGround(map, i % t.w, i / t.w); };
		const int seed = seedNear(t, int(std::lround(drop.x)), int(std::lround(drop.y)), 3, onDrop);
		if (seed < 0)
			continue;
		const bool stone = drop.prize == 3;
		if (scaledCount(1, stone ? o.stone : o.fruit) > 0)
			growPatch(map, t, seed, stone ? STONE : CHERRY + drop.prize, 9, onDrop);
	}

	const auto onHub = [&](int i) { return L.hub[i] && clearGround(map, i % t.w, i / t.w); };
	const double rho = L.g.pondRadius > 0 ? L.g.pondRadius * 1.25 + 4 : 0.5 * L.g.hubRadius;
	const double spin = context.bounded("web-hub", 3600) / 3600.0 * 2 * kPi;
	if (scaledCount(1, o.fruit) > 0)
		for (int f = 0; f < 3; ++f)
		{
			const ShapePoint p = polarPoint(L.cx, L.cy, rho, spin + 2 * kPi * f / 3);
			if (const int seed = seedNear(t, int(std::lround(p.x)), int(std::lround(p.y)), 6, onHub);
				seed >= 0)
				placeResourceClump(map, context, {seed % t.w, seed / t.w}, CHERRY + f, 2);
		}
	if (scaledCount(1, o.stone) > 0)
	{
		const ShapePoint p = polarPoint(L.cx, L.cy, rho + 2, spin + kPi / 3);
		if (const int seed = seedNear(t, int(std::lround(p.x)), int(std::lround(p.y)), 6, onHub);
			seed >= 0)
			placeResourceClump(map, context, {seed % t.w, seed / t.w}, STONE, 2);
	}

	const WedgeFrame wedges(t, L.phase, L.g.teams);
	const PeriodicNoise patch(t.w, t.h, 12, context.stream("web-patch"));
	const PeriodicNoise split(t.w, t.h, 7, context.stream("web-split"));
	std::vector<std::pair<double, int>> ranked;
	std::vector<double> splitKey(n, 0.0);
	for (int i = 0; i < n; ++i)
		if (onThread(i))
		{
			const WedgeFrame::Cell cell = wedges.cell(i % t.w, i / t.w);
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

bool generate(Game &game, GenerationContext &context)
{
	context.stage = "web layout";
	const SpiderWebOptions o(context.request);
	Map &map = game.map;
	const int teams = context.request.nbTeams;
	map.makeHomogenMap(WATER);
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
	TerrainSketch terrain(n, GRASS);
	for (int i = 0; i < n; ++i)
		if (L.water[i])
			terrain[i] = WATER;
	layBeaches(terrain, t);
	writeUndermap(map, terrain);

	context.stage = "web colonies";
	const auto pad = [&](int team)
	{
		std::vector<unsigned char> home(n, 0);
		for (int i = 0; i < n; ++i)
			home[i] = L.padOf[i] == team && map.isGrass(i % t.w, i / t.w);
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
	seedAlgae(map, context, t, "web-algae", o.algae, AlgaeBand::shallows(2, 6, 45));
	secureStartingCrops(game, context, t);

	// Beaches keep the threads walkable, but a small hub can be stocked shut: keep a way open from
	// colony 0 onto the hub and to every other colony, clearing only the deposits in the way.
	context.stage = "web roads";
	const std::vector<std::vector<int>> workers = unitTilesByTeam(map, teams);
	if (teams < 1 || workers[0].empty())
		return true;
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
std::string validateWorld(const Game &game, const GenerationContext &context)
{
	GenerationContext replay(context.request);
	const Layout L = design(context.request, replay);
	const Map &map = game.map;
	if (const std::string mismatch = designMismatch(L, map, "web"); !mismatch.empty())
		return mismatch;
	const Torus &t = L.t;
	for (size_t j = 0; j < L.spokeLines.size(); ++j)
		for (const ShapePoint &p : L.spokeLines[j])
			if (map.isWater(t.x(int(std::lround(p.x))), t.y(int(std::lround(p.y)))))
				return "Spoke " + std::to_string(j) + " is broken by water.";
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
	  dewDrops(r.option("dew-drops")), spiral(r.option("spiral") != 0),
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
		1,
		false,
		// Spokes per colony (more on a web with few colonies); the spacing between capture threads
		// and every thread's width in tiles; how far the capture threads sag and the share of them
		// torn, in percent; the hub's radius as a share of the half side; dew drops per colony.
		{{"spokes", "Spokes per colony", 1, 3, 1, 2, ControlGroup::Layout},
		 {"ring-spacing", "Ring spacing", 20, 40, 2, 28, ControlGroup::Terrain},
		 {"thread-width", "Thread width", 7, 15, 2, 11, ControlGroup::Terrain},
		 {"sag", "Sag", 0, 100, 10, 70, ControlGroup::Terrain},
		 {"torn-strands", "Torn strands", 0, 60, 5, 15, ControlGroup::Terrain},
		 {"hub-size", "Hub size", 8, 24, 1, 14, ControlGroup::Terrain},
		 {"dew-drops", "Dew drops", 0, 4, 1, 2, ControlGroup::Layout},
		 // Off, the capture threads are closed rings rather than a spiral.
		 GeneratorControl::toggle("spiral", "Spiral", true, ControlGroup::Layout),
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
		validateWorld};
}
