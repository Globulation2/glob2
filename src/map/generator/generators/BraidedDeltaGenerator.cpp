// SPDX-License-Identifier: GPL-3.0-or-later
#include "BraidedDeltaGenerator.h"
#include "Channels.h"
#include "DesignCache.h"
#include "Drawing.h"
#include "Game.h"
#include "GenerationContext.h"
#include "GenerationNumeric.h"
#include "Geometry.h"
#include "Grid.h"
#include "LatticeNoise.h"
#include "Morphology.h"
#include "Pipeline.h"
#include "Planting.h"
#include "Points.h"
#include "Resources.h"
#include "Roads.h"
#include "Room.h"
#include "Settlements.h"
#include "Sketch.h"
#include "Topology.h"
#include "TerrainTypeTable.h"
#include <algorithm>
#include <cmath>
#include <functional>
#include <string>
#include <vector>
using namespace MapGeneration;
namespace N = ::MapGeneration::Numeric;

// Braided Delta is an inland delta, after the Okavango: a river that never reaches the sea. It
// leaves a rock range through a gorge, runs down a marsh-lined panhandle, and at the apex spreads
// into one great swamp in the middle of a desert basin. Out from the apex:
//
// - the permanent swamp: papyrus marsh threaded by channels that split and rejoin, deep lagoons,
//   and islands of grass ringed with trees round bare salt-pan hearts, one of them a great island;
// - the seasonal floodplain: loam levees and lush grass along shrinking channels;
// - the fringe, where the channels die out in dry sandy beds among sand ridges;
// - the desert: dry grass, linear dunes, clay pans and rocky kopjes, round the wrap to the range.
//
// PLAY INTENT: water and food are in the middle, building room and stone at the edge. Colonies
// start round the swamp's rim on dry ground and push inward for floodplain farms and islands. The
// panhandle's river is deep, a wall even to swimmers, crossed only at its boardwalk bridges; the
// swamp's channels are shallow, forded or swum; marsh is slow ground nobody can build on. Starts
// are found, not stamped: fairness is measured.
//
// Everything is drawn in a frame along the flow (u downstream, v across), on a torus of its own,
// and transposed onto the map at the end, so every rule below reads the same on either axis.
namespace
{
constexpr const char *kLayout = "braided-delta-layout";

// A tile mixing shallow and deep corners is neither walkable nor swimmable, so a deep core whose
// banks are three vertices of shallow water is a wall to every unit: the panhandle's river and the
// lagoons have one.
constexpr int kDeep = 3;
// Levees: a loam crest two to three vertices from a floodplain channel's water.
constexpr int kLeveeCrest = 3, kLeveeWidth = 7;
// A home: a square of this many pure grass tiles with clear ground round it.
constexpr int kHome = 14;
// The plain grass clearing round a home, radius in tiles.
constexpr int kHomeClearing = 20;
// A starter kit: tiles of wheat and wood, this far from the home's middle.
constexpr int kKitWheat = 24, kKitWood = 18, kKitDistance = 11;
// Ambient resources at 100%: the share of levee loam under wheat and of lush ground under wood,
// trees per fruit on the great island, and stone at each kopje.
constexpr double kWheatShare = 0.45, kWoodShare = 0.22;
constexpr int kGroveTrees = 4, kQuarryStone = 6;
// The repair joins pieces of land of this many corners or more. The validator counts tiles, and
// every tile with a land corner is walkable, so a thin strip of a few corners can make a piece of
// tens of tiles: keep this far below the validator's 48.
constexpr int kRepairPiece = 8;
// Where the zones end, as a share of the fan's radius (the swamp's edge depends on the flood).
constexpr double kFloodplainEdge = 0.9, kWetEdge = 0.95, kFringeEdge = 1.02;

double unit(std::mt19937 &random)
{
	return (random() >> 5) * (1.0 / 134217728.0);
}

// A river's meanders: the path resampled a tile apart and pushed sideways along its normal by two
// sine waves (one of `wavelength`, one a little under half of it) whose phases and the second's
// weight come from `random`. The push fades to nothing over the first and last stretch, so an arm
// still leaves its fork and a river still meets its gorge where it was drawn. Half widths follow
// the points they belong to. Four draws.
std::vector<StrokePoint> meander(const std::vector<StrokePoint> &path, double amplitude, double wavelength,
								 std::mt19937 &random)
{
	const double phase = 2 * kPi * unit(random), phase2 = 2 * kPi * unit(random);
	const double ratio = 0.38 + 0.12 * unit(random), weight = 0.12 + 0.15 * unit(random);
	if (path.size() < 2)
		return path;
	std::vector<StrokePoint> even{path.front()};
	double walked = 0, next = 1;
	for (size_t k = 1; k < path.size(); ++k)
	{
		const StrokePoint a = path[k - 1], b = path[k];
		const double length = N::hypot(b.x - a.x, b.y - a.y);
		for (; length > 0 && next <= walked + length; next += 1)
		{
			const double t = (next - walked) / length;
			even.push_back({a.x + t * (b.x - a.x), a.y + t * (b.y - a.y), a.halfWidth + t * (b.halfWidth - a.halfWidth)});
		}
		walked += length;
	}
	if (N::hypot(even.back().x - path.back().x, even.back().y - path.back().y) > 0.01)
		even.push_back(path.back());
	const double total = double(even.size() - 1);
	const double ease = std::min(0.25 * wavelength, total / 5);
	std::vector<StrokePoint> out = even;
	for (size_t k = 0; k < even.size(); ++k)
	{
		const size_t before = k ? k - 1 : 0, after = std::min(even.size() - 1, k + 1);
		const double tx = even[after].x - even[before].x, ty = even[after].y - even[before].y;
		const double norm = N::hypot(tx, ty);
		if (norm <= 0)
			continue;
		const double s = double(k);
		const double edge = std::min(s, total - s) / std::max(1.0, ease);
		const double fade = edge >= 1 ? 1 : edge * edge * (3 - 2 * edge);
		const double push = amplitude * fade *
							(N::sin(2 * kPi * s / wavelength + phase) +
							 weight * N::sin(2 * kPi * s / (wavelength * ratio) + phase2)) /
							(1 + weight);
		out[k].x = even[k].x - push * ty / norm;
		out[k].y = even[k].y + push * tx / norm;
	}
	return out;
}

// The panhandle's river as the validator needs it: where along the flow it is a wall (from the
// gorge to the apex) and where its bridges cut that wall.
struct RiverPlan
{
	double start = 0, end = 0, halfWidth = 0;
	// The river's middle across the flow at each whole u from 0: its meanders push it only
	// sideways, so it is a function of u.
	std::vector<double> middle;
	std::vector<double> bridges;
};

struct Layout
{
	Torus t{1, 1}; // the map
	Torus f{1, 1}; // the flow frame: u across its width, v down its height
	bool transpose = false;
	int along = 0, across = 0;
	TerrainSketch terrain, paint;  // in the map's vertices: mechanics, then looks
	std::vector<ShapePoint> homes; // map tiles
	std::vector<int> homeOf;       // map tiles
	std::string failure;
	int bridges = 0, fords = 0, arms = 0;
	RiverPlan river;
	std::vector<double> frameU, frameV;   // each map tile's place in the flow frame
	std::vector<unsigned char> isles;     // map tiles of swamp islands: 2 on the great island
	int greatIsland = -1;                 // the map tile at the great island's middle

	int toMap(int i) const
	{
		const int u = f.remainderX(i), v = i / f.w;
		return transpose ? t.at(v, u) : t.at(u, v);
	}
};

Layout designAfresh(const GenerationRequest &request, GenerationContext &context)
{
	const BraidedDeltaOptions o(request);
	Layout L;
	L.t = {1 << request.wDec, 1 << request.hDec};
	const Torus &t = L.t;
	// The flow runs along the longer dimension; on squares the seed picks.
	L.transpose = t.h > t.w || (t.h == t.w && context.bounded(kLayout, 2));
	L.along = L.transpose ? t.h : t.w;
	L.across = L.transpose ? t.w : t.h;
	L.f = {L.along, L.across};
	const Torus &f = L.f;
	const int A = L.along, C = L.across, n = f.size();
	if (A < 128 || C < 128)
	{
		L.failure = "An inland delta needs at least 128 tiles each way.";
		return L;
	}
	const double c = context.bounded(kLayout, C);
	const auto across = [&](double dv) { return ChannelDetail::centred(dv, C); };

	// The range along the wrap, the panhandle below it, the apex where the river spreads, and the
	// fan: a lobe round the apex whose radius wanders with its bearing.
	const PeriodicNoise rangeNoise(C, 1, 12, context.stream("braided-delta-range"));
	const double riverHalf = std::clamp(A / 60.0, 3.6, 5.5);
	// The range thickens round the gorge, so the river rises inside the rock: nothing behind the
	// range reaches its water.
	const auto rangeEdge = [&](double v)
	{
		const double gorge = (2 * riverHalf + 6) * std::max(0.0, 1 - std::abs(across(v - c)) / 24);
		return std::max(4 + 5 * rangeNoise.at(v, 0), gorge);
	};
	// The panhandle runs at least 24 tiles below the thickest part of the range, so its bridges have
	// a stretch of river between the gorge and the apex on the smallest maps.
	const double apex = std::max(9 + 0.13 * A, 2 * riverHalf + 6 + 24);
	const double floodScale = o.flood == 0 ? 0.85 : o.flood == 2 ? 1.1 : 1.0;
	// The fan is an ellipse round the apex: down the flow it reaches most of the way to the wrap,
	// across it its sides nearly meet round the other wrap. `radius` is their geometric mean.
	const double reachAlong = floodScale * 0.92 * (A - apex), reachAcross = floodScale * 0.62 * C;
	const double radius = N::sqrt(reachAlong * reachAcross);
	const double swampEdge = o.flood == 0 ? 0.38 : o.flood == 2 ? 0.58 : 0.48;
	constexpr double kFanAngle = 1.65;
	const PeriodicNoise lobe(64, 1, 5, context.stream("braided-delta-lobe"));
	const std::vector<int> warp = fractalNoise(A, C, std::max(16, int(radius / 4)), 2, context.stream("braided-delta-warp"));
	// The fan's normalized radius at a frame point: under 1 inside the fan, growing outwards.
	const auto fanRadius = [&](double u, double v, int warpAt)
	{
		const double du = (u - apex) / reachAlong, dv = across(v - c) / reachAcross;
		const double rho = N::hypot(du, dv);
		const double bearing = N::atan2(dv, du);
		if (std::abs(bearing) >= kFanAngle)
			return 9.0;
		const double profile = N::pow(N::cos(bearing * kPi / (2 * kFanAngle)), 0.35);
		const double edge = profile * (1 + 0.18 * (2 * lobe.at(32 * (bearing / kFanAngle + 1), 0) - 1));
		double r = rho / std::max(1 / radius, edge);
		if (warpAt >= 0)
			r += 0.14 * (2 * warp[warpAt] / 65536.0 - 1) * std::min(1.0, rho * radius / 12);
		return r;
	};
	// How far the fan reaches along a heading from the apex, in tiles.
	const auto reachToward = [&](double heading)
	{ return N::hypot(reachAlong * N::cos(heading), reachAcross * N::sin(heading)); };
	std::vector<double> r(n);
	for (int i = 0; i < n; ++i)
		r[i] = fanRadius(f.remainderX(i) + 0.5, i / f.w + 0.5, i);

	std::vector<unsigned char> water(n, 0), core(n, 0), arms(n, 0), dryBed(n, 0), lagoon(n, 0);
	std::mt19937 &random = context.stream("braided-delta-river");
	const auto roll = [&] { return unit(random); };
	// The panhandle's river: from inside the range to the apex.
	const double source = riverHalf + 2;
	const std::vector<StrokePoint> river = meander({{source, c, riverHalf}, {apex, c, riverHalf}},
												   std::min(7.0, 0.04 * A) * (0.8 + 0.4 * roll()), 30 + 12 * roll(), random);
	strokePath(water, f, river);
	strokePath(core, f, river);
	L.river.start = source - riverHalf;
	L.river.halfWidth = riverHalf;
	L.river.middle.assign(size_t(apex) + 2, c);
	for (const StrokePoint &p : river)
		if (p.x >= 0 && p.x < L.river.middle.size())
			L.river.middle[size_t(p.x)] = p.y;
	L.river.end = apex;

	// The fan's channels: primary arms leave the apex across the fan, fork, and thin as they go
	// out; where the water gives out they carry on a little as dry sandy beds. Every arm keeps a gap
	// from every other beyond the stretch where it leaves its fork.
	std::vector<std::vector<StrokePoint>> channels;
	{
		ForkStyle style;
		style.spread = 0.36;
		style.spreadJitter = 0.35;
		style.lengthRatio = 0.78;
		style.lengthJitter = 0.25;
		style.widthRatio = 0.85;
		style.bend = 0.15;
		style.minimumLength = 8;
		style.minimumHalfWidth = 1.2;
		const int roots = 4 + 2 * o.distributaries;
		const double gap = 10, armHalf = 2.4;
		const double fork = std::min(60.0, gap + (gap + 2 * armHalf) / (2 * N::sin(style.spread * 0.65)));
		std::vector<Branch> tree;
		const auto accept = [&](const std::vector<StrokePoint> &path, int)
		{
			for (const StrokePoint &p : path)
				if (fanRadius(p.x, p.y, -1) > 0.95)
					return false;
			const StrokePoint start = path.front();
			const PathBounds bounds = pathBounds(path);
			for (const StrokePoint &p : path)
			{
				if (N::hypot(p.x - start.x, p.y - start.y) < fork)
					continue;
				for (const Branch &b : tree)
					if (N::hypot(b.bounds.x - bounds.x, b.bounds.y - bounds.y) < b.bounds.radius + bounds.radius + gap)
						for (const StrokePoint &q : b.path)
							if (N::hypot(p.x - q.x, p.y - q.y) - p.halfWidth - q.halfWidth < gap)
								return false;
			}
			return true;
		};
		std::vector<int> order;
		for (int k = 0; k < roots; ++k)
			order.push_back(k);
		std::stable_sort(order.begin(), order.end(), [&](int a, int b)
						 { return std::abs(2 * a + 1 - roots) < std::abs(2 * b + 1 - roots); });
		for (int k : order)
		{
			const double heading = 0.85 * kFanAngle * ((2.0 * k + 1) / roots - 1) + 0.1 * (2 * roll() - 1);
			growBranches(tree, -1, {apex, c}, heading, 0.32 * reachToward(heading), armHalf, 2, style, roll, accept);
		}
		std::mt19937 &sway = context.stream("braided-delta-sway");
		for (Branch &b : tree)
		{
			if (b.leaf)
			{
				// A leaf runs on to the fringe along its heading.
				const StrokePoint tip = b.path.back();
				const double run = 0.3 * reachToward(b.heading);
				const auto more = bentPath({tip.x, tip.y}, b.heading, run, 0.1 * (2 * unit(sway) - 1), tip.halfWidth,
										   tip.halfWidth, std::max(2, int(run / 3)));
				for (size_t k = 1; k < more.size(); ++k)
				{
					b.path.push_back(more[k]);
					if (fanRadius(more[k].x, more[k].y, -1) > 1.1)
						break;
				}
			}
			auto path = meander(b.path, 3.5 + 2 * unit(sway), 22 + 12 * unit(sway), sway);
			// Channels thin outwards; past the wet edge they are beds of sand, not water.
			for (StrokePoint &p : path)
				p.halfWidth = std::max(1.1, armHalf * (1 - 0.75 * std::min(1.0, fanRadius(p.x, p.y, -1))));
			channels.push_back(path);
		}
	}
	// Braiding: in the permanent swamp, side channels leave one arm and rejoin a neighbour.
	{
		std::mt19937 &braid = context.stream("braided-delta-braids");
		const size_t primary = channels.size();
		for (size_t a = 0; a < primary; ++a)
			for (int k = 0; k < 2; ++k)
			{
				const auto &from = channels[a][size_t(unit(braid) * channels[a].size())];
				if (fanRadius(from.x, from.y, -1) > swampEdge * 0.95 || fanRadius(from.x, from.y, -1) < 0.12)
					continue;
				double best = 1e9;
				ShapePoint to{};
				for (size_t b = 0; b < primary; ++b)
					if (b != a)
						for (const auto &q : channels[b])
						{
							const double d = N::hypot(q.x - from.x, across(q.y - from.y));
							if (d >= 14 && d <= 34 && d < best && fanRadius(q.x, q.y, -1) < swampEdge)
							{
								best = d;
								to = {q.x, from.y + across(q.y - from.y)};
							}
						}
				if (best < 1e9)
					channels.push_back(meander(wanderingPath(f, {from.x, from.y}, to, 1.4, 2, 0.2, braid),
											   2.5, 14 + 6 * unit(braid), braid));
			}
	}
	L.arms = int(channels.size());
	for (const auto &path : channels)
	{
		std::vector<StrokePoint> wet, dry;
		for (const StrokePoint &p : path)
		{
			const double pr = fanRadius(p.x, p.y, -1);
			(pr < kWetEdge ? wet : dry).push_back(p);
			if (pr >= kWetEdge - 0.03 && pr < kWetEdge)
				dry.push_back(p);
		}
		if (wet.size() >= 2)
		{
			strokePath(water, f, wet);
			strokePath(arms, f, wet);
		}
		if (dry.size() >= 2)
		{
			for (StrokePoint &p : dry)
				p.halfWidth = 1.2;
			strokePath(dryBed, f, dry);
		}
	}
	const std::vector<int> fromArm = stepsFrom(f, arms);
	// Lagoons: deep pools in the permanent swamp, off the channels.
	int lagoons = 0;
	{
		std::mt19937 &pool = context.stream("braided-delta-lagoons");
		const int wanted = std::max(2, int(radius * radius * swampEdge * swampEdge / 900));
		for (int attempt = 0; attempt < 12 * wanted && lagoons < wanted; ++attempt)
		{
			const int i = int(unit(pool) * n);
			const double size = 4 + 4 * unit(pool), stretch = 1.2 + 0.6 * unit(pool), turn = kPi * unit(pool);
			if (r[i] > swampEdge * 0.9 || r[i] < 0.1 || fromArm[i] < size * stretch + 4 || water[i])
				continue;
			const double u0 = f.remainderX(i), v0 = i / f.w;
			const int reach = int(size * stretch) + 2;
			for (int dv = -reach; dv <= reach; ++dv)
				for (int du = -reach; du <= reach; ++du)
				{
					const double a = (du * N::cos(turn) + dv * N::sin(turn)) / stretch;
					const double b = -du * N::sin(turn) + dv * N::cos(turn);
					const double bearing = N::atan2(b, a);
					const double edge = size * (1 + 0.22 * N::sin(3 * bearing + 7 * turn) + 0.12 * N::sin(5 * bearing));
					if (N::hypot(a, b) < edge)
					{
						const int j = f.at(int(u0) + du, int(v0) + dv);
						water[j] = lagoon[j] = 1;
					}
				}
			++lagoons;
		}
	}
	// Seasonal pools on the floodplain: small, shallow, between the channels.
	const std::vector<int> poolNoise = periodicNoise(A, C, 6, context.stream("braided-delta-pools"));
	for (int i = 0; i < n; ++i)
		if (r[i] > swampEdge && r[i] < kFloodplainEdge && fromArm[i] > kLeveeWidth + 4 && poolNoise[i] > 62000)
			water[i] = 1;
	// Desert pans: a few far out hold water after rain.
	const std::vector<int> panNoise = periodicNoise(A, C, 18, context.stream("braided-delta-pans"));
	std::vector<unsigned char> pan(n, 0);
	for (int i = 0; i < n; ++i)
	{
		const int u = f.remainderX(i);
		if (r[i] > 1.25 && u > rangeEdge(i / f.w) + 10 && panNoise[i] > 59000)
		{
			pan[i] = 1;
			if (panNoise[i] > 63500)
				water[i] = 1;
		}
	}

	// The sketch, in the flow frame.
	TerrainSketch sketch(n, GRASS), look(n, 0);
	for (int i = 0; i < n; ++i)
		if (water[i])
			sketch[i] = WATER;
	layBeaches(sketch, f);
	// Deep cores: the panhandle's river and the lagoons.
	{
		std::vector<unsigned char> wet(n, 0);
		for (int i = 0; i < n; ++i)
			wet[i] = sketch[i] == WATER;
		const std::vector<int> depth = clearance(f, wet);
		for (int i = 0; i < n; ++i)
		{
			// Inside the range all the river's water is deep, so the deep core runs into the rock: a
			// shallow tip there would let a swimmer round the core from one bank to the other.
			const bool gorge = core[i] && f.remainderX(i) < rangeEdge(i / f.w) + 1;
			if (sketch[i] == WATER && (core[i] || lagoon[i]) && (depth[i] >= kDeep || gorge))
			{
				sketch[i] = DEEP_WATER;
				look[i] = depth[i] >= kDeep + 2 ? DARK_WATER : DEEP_WATER;
			}
		}
		// Deep water is swimmable: it is a wall only behind the ring of tiles mixing it with shallow
		// water, which nobody can enter. Where it would touch ground a unit can stand on (the gorge's
		// mouth, where the rock gives way to the banks), a swimmer could step straight in and across,
		// so there it stays shallow. Rock (the range, still to be laid) may touch it.
		const auto rock = [&](int j) { return f.remainderX(j) < rangeEdge(j / f.w) && sketch[j] != WATER; };
		std::vector<int> shoal;
		for (int i = 0; i < n; ++i)
			if (sketch[i] == DEEP_WATER)
				for (int dv = -1; dv <= 1; ++dv)
					for (int du = -1; du <= 1; ++du)
					{
						const int j = f.at(f.remainderX(i) + du, i / f.w + dv);
						if (sketch[j] != WATER && sketch[j] != DEEP_WATER && !rock(j))
							shoal.push_back(i);
					}
		for (int i : shoal)
		{
			sketch[i] = WATER;
			look[i] = 0;
		}
	}
	std::vector<unsigned char> wetAll(n, 0);
	for (int i = 0; i < n; ++i)
		wetAll[i] = sketch[i] == WATER || sketch[i] == DEEP_WATER;
	const std::vector<int> fromWater = stepsFrom(f, wetAll), fromRiver = stepsFrom(f, core);
	// The zones reach out along the channels in fingers, as a flood does: a tile's zone radius is
	// its fan radius less a little for being near a channel.
	const std::vector<int> fromChannel = stepsFrom(f, arms);
	for (int i = 0; i < n; ++i)
		r[i] -= 0.16 * N::exp(-fromChannel[i] / 8.0);

	// Islands in the permanent swamp: the swamp is marsh except where the island noise rises, and
	// one great island lies along the flow in the swamp's middle.
	const std::vector<int> isleNoise = fractalNoise(A, C, 14, 2, context.stream("braided-delta-isles"));
	std::vector<int> swampNoise;
	for (int i = 0; i < n; ++i)
		if (sketch[i] == GRASS && r[i] < swampEdge)
			swampNoise.push_back(isleNoise[i]);
	const int isleLevel =
		swampNoise.empty() ? 65536 : percentile(swampNoise, int(100 - (14 + 0.32 * o.islands)));
	std::vector<unsigned char> isle(n, 0);
	{
		std::mt19937 &great = context.stream("braided-delta-great-island");
		const double bearing = 0.5 * (2 * unit(great) - 1);
		const double distance = radius * swampEdge * (0.5 + 0.15 * unit(great));
		const double gu = apex + distance * N::cos(bearing), gv = c + distance * N::sin(bearing);
		L.greatIsland = L.toMap(f.at(int(gu), int(gv)));
		const double half = radius * swampEdge * 0.24, wide = half * (0.34 + 0.1 * unit(great));
		const int reach = int(half) + 2;
		for (int dv = -reach; dv <= reach; ++dv)
			for (int du = -reach; du <= reach; ++du)
			{
				const double a = du * N::cos(bearing) + dv * N::sin(bearing);
				const double b = -du * N::sin(bearing) + dv * N::cos(bearing);
				const int j = f.at(int(gu) + du, int(gv) + dv);
				const double wobble = 1 + 0.15 * (2 * isleNoise[j] / 65536.0 - 1);
				if (N::hypot(a / half, b / wide) < wobble && sketch[j] == GRASS)
					isle[j] = 2;
			}
	}
	for (int i = 0; i < n; ++i)
		if (sketch[i] == GRASS && r[i] < swampEdge && isleNoise[i] >= isleLevel && !isle[i])
			isle[i] = 1;
	const std::vector<int> grain = periodicNoise(A, C, 5, context.stream("braided-delta-grain"));
	const std::vector<int> kopje = periodicNoise(A, C, 9, context.stream("braided-delta-kopjes"));
	// Linear dunes: parallel ridges across the wind, a whole number of them round each axis so they
	// close across the seams, broken by noise into separate dunes.
	std::mt19937 &wind = context.stream("braided-delta-wind");
	const int waveU = 1 + int(3 * unit(wind)), waveV = std::max(4, int(C / (11 + 4 * unit(wind))));
	const double windPhase = 2 * kPi * unit(wind);
	const std::vector<int> duneBreak = fractalNoise(A, C, 20, 2, wind);
	const double duneCut = 1 - 0.9 * o.dunes / 100.0;
	int marsh = 0, loam = 0, isleTiles = 0, duneTiles = 0;
	// Desert features, the fringe's sand ridges and the dry beds are laid only once the homes are
	// chosen, and never in a home's clearing: they would break up the room a home needs.
	std::vector<unsigned char> decoMech(n, 0), decoLook(n, 0);
	const auto later = [&](int i, TerrainType mechanic, TerrainType painted)
	{
		decoMech[i] = mechanic;
		decoLook[i] = painted;
	};
	for (int i = 0; i < n; ++i)
	{
		if (sketch[i] != GRASS)
			continue;
		const int u = f.remainderX(i), v = i / f.w;
		if (r[i] < swampEdge)
		{
			if (isle[i])
			{
				// Islands: grass ringed by trees later, round a bare salt pan where they are wide.
				look[i] = LUSH_GRASS;
				++isleTiles;
			}
			else
			{
				sketch[i] = MUDFLAT;
				look[i] = grain[i] > 47000 ? MUD : MUDFLAT;
				++marsh;
			}
		}
		else if (r[i] < kFloodplainEdge)
		{
			if (fromArm[i] >= 2 && fromArm[i] <= kLeveeCrest)
			{
				sketch[i] = LOAM;
				look[i] = grain[i] > 45000 ? SPRING_MEADOW : LOAM;
				++loam;
			}
			else
				look[i] = fromWater[i] <= 10 ? LUSH_GRASS : GRASS;
		}
		else if (r[i] < kFringeEdge + 0.08)
		{
			// Sand ridges in broken arcs at the edge of the flood.
			look[i] = fromWater[i] <= 4 ? GRASS : DRY_GRASS;
			if (std::abs(r[i] - kFringeEdge) < 0.02 && duneBreak[i] > 42000)
				later(i, SAND, DUNE_SAND);
		}
		else if (u >= apex || fromRiver[i] > 9)
		{
			// The desert.
			const double stripe = N::sin(2 * kPi * (waveU * double(u) / A + waveV * double(v) / C) + windPhase +
										 4 * duneBreak[i] / 65536.0 + 2 * grain[i] / 65536.0);
			look[i] = DRY_GRASS;
			if (pan[i])
				later(i, CLAY, grain[i] > 30000 ? CLAY : DIRT);
			else if (kopje[i] > 63000)
				later(i, OUTCROP, OUTCROP);
			else if (kopje[i] > 61500)
				later(i, SCREE, SCREE);
			else if (stripe > 0.7 && duneBreak[i] / 65536.0 > duneCut && r[i] > 1.15)
				later(i, SAND, DUNE_SAND);
			else if (grain[i] > 52000)
				later(i, SHALE, SHALE);
		}
		else
		{
			// The panhandle: papyrus along the river, dry ground beyond.
			if (fromRiver[i] <= 7 && fromWater[i] >= 1)
			{
				sketch[i] = MUDFLAT;
				look[i] = MUDFLAT;
			}
			else
				look[i] = DRY_GRASS;
		}
	}
	// Bare salt pans at the hearts of the wider islands.
	{
		std::vector<unsigned char> ground(n, 0);
		for (int i = 0; i < n; ++i)
			ground[i] = isle[i] && sketch[i] == GRASS;
		const std::vector<int> room = clearance(f, ground);
		for (int i = 0; i < n; ++i)
			if (ground[i] && room[i] >= 5 && grain[i] > 20000)
			{
				sketch[i] = SAND;
				look[i] = PALE_SAND;
			}
	}
	// Papyrus to the water's edge: in the swamp and down the panhandle, the banks the beach rule
	// laid in sand are marsh. Marsh is walkable like the sand it replaces, but slow.
	for (int i = 0; i < n; ++i)
		if (sketch[i] == SAND && !look[i] && !isle[i] &&
			(r[i] < swampEdge || (f.remainderX(i) < apex && fromRiver[i] <= 7)))
		{
			sketch[i] = MUDFLAT;
			look[i] = MUDFLAT;
		}
	// Dry channel beds past the water's end.
	for (int i = 0; i < n; ++i)
		if (dryBed[i] && !wetAll[i] && sketch[i] == GRASS)
			later(i, SAND, PALE_SAND);
	// The range: ridge rock along the wrap, cut only by the gorge's water, with scree at its foot.
	for (int i = 0; i < n; ++i)
	{
		const int u = f.remainderX(i), v = i / f.w;
		const double edge = rangeEdge(v);
		if (u < edge && !wetAll[i])
			sketch[i] = look[i] = RIDGE_ROCK;
		else if (u < edge + 1.5 + 2 * grain[i] / 65536.0 && !wetAll[i])
		{
			sketch[i] = SCREE;
			look[i] = grain[i] > 40000 ? BOULDERS : SCREE;
			if (look[i] == BOULDERS)
				sketch[i] = BOULDERS;
		}
	}
	context.telemetry.measure("braided-delta.fan.radius", radius);
	context.telemetry.measure("braided-delta.swamp.marsh", marsh);
	context.telemetry.measure("braided-delta.swamp.island-tiles", isleTiles);
	context.telemetry.measure("braided-delta.swamp.lagoons", lagoons);
	context.telemetry.measure("braided-delta.floodplain.loam", loam);

	// Crossings. Label the land before any is laid, so a ford is offered only where it joins two
	// pieces not yet joined, then once more wherever it is far from every other.
	std::vector<unsigned char> walk(n, 0);
	for (int i = 0; i < n; ++i)
	{
		const auto group = terrainGroup(TerrainType(sketch[i]));
		walk[i] = !wetAll[i] && group != TerrainGroup::Ridges && group != TerrainGroup::Obstacles;
	}
	const std::vector<int> pieces = connectedRegions(walk, f.w, f.h, true, GridNeighbors::Cardinal);
	DisjointSets joined(std::max(1, *std::max_element(pieces.begin(), pieces.end()) + 1));
	std::vector<ShapePoint> laid;
	const auto labelAt = [&](double u, double v) { return pieces[f.at(int(N::floor(u)), int(N::floor(v)))]; };
	// Bridges first: boardwalk decks over the panhandle's deep river, spread down it.
	for (int b = 0; b < o.crossings; ++b)
	{
		const double u = rangeEdge(c) + 10 + (apex - rangeEdge(c) - 16) * (b + 0.5) / o.crossings;
		int best = -1 << 20;
		for (int dv = -20; dv <= 20; ++dv)
			if (core[f.at(int(u), int(c) + dv)] && std::abs(dv) < std::abs(best))
				best = dv;
		if (best == -1 << 20)
			continue;
		int lo = best, hi = best;
		while (core[f.at(int(u), int(c) + lo - 1)])
			--lo;
		while (core[f.at(int(u), int(c) + hi + 1)])
			++hi;
		for (int dv = lo - 4; dv <= hi + 4; ++dv)
			for (int du = -1; du <= 1; ++du)
			{
				const int i = f.at(int(u) + du, int(c) + dv);
				const bool deck = dv >= lo - 1 && dv <= hi + 1;
				sketch[i] = TRAIL;
				look[i] = deck ? BOARDWALK : BRICK_PATH;
			}
		const int a = labelAt(u, c + lo - 6), z = labelAt(u, c + hi + 6);
		if (a >= 0 && z >= 0)
			joined.unite(a, z);
		laid.push_back({u, c + (lo + hi) / 2.0});
		L.river.bridges.push_back(u);
		++L.bridges;
	}
	// Fords: sand across the shallow channels, on a spanning set of the land pieces plus loops.
	for (int pass = 0; pass < 2; ++pass)
		for (const auto &path : channels)
			for (size_t k = 4; k + 4 < path.size(); k += 3)
			{
				const StrokePoint p = path[k];
				if (fanRadius(p.x, p.y, -1) > kWetEdge - 0.04)
					continue;
				const double tu = path[k + 1].x - path[k - 1].x, tv = path[k + 1].y - path[k - 1].y;
				const double len = N::hypot(tu, tv);
				if (len <= 0)
					continue;
				const double nu = -tv / len, nv = tu / len, probe = p.halfWidth + 3;
				const int left = labelAt(p.x + nu * probe, p.y + nv * probe);
				const int right = labelAt(p.x - nu * probe, p.y - nv * probe);
				if (left < 0 || right < 0 || left == right)
					continue;
				bool far = true;
				for (const ShapePoint &q : laid)
					far = far && N::hypot(q.x - p.x, across(q.y - p.y)) >= (pass ? 44 : 10);
				if (!far || (pass == 0 && joined.find(left) == joined.find(right)))
					continue;
				SandFord ford{p.x, p.y, tu / len, tv / len, nu, nv, p.halfWidth + 2.5, 2};
				stampFord(sketch, f, ford);
				joined.unite(left, right);
				laid.push_back({p.x, p.y});
				++L.fords;
			}
	// Repair: a piece of ground the fords above still leave cut off (a pocket between a junction and
	// a lagoon, say) gets a ford along the cheapest crossing of shallow water to any other piece.
	// Deep water is never filled.
	int repairs = 0;
	std::vector<unsigned char> hopeless(n, 0); // tiles of pieces no allowed crossing reaches
	for (int round = 0; round < 24; ++round)
	{
		for (int i = 0; i < n; ++i)
		{
			const auto group = terrainGroup(TerrainType(sketch[i]));
			walk[i] = group != TerrainGroup::Water && group != TerrainGroup::DeepWater &&
					  group != TerrainGroup::Ridges && group != TerrainGroup::Obstacles;
		}
		const std::vector<int> now = connectedRegions(walk, f.w, f.h, true, GridNeighbors::Cardinal);
		const int count = *std::max_element(now.begin(), now.end()) + 1;
		std::vector<int> size(std::max(1, count), 0);
		for (int i = 0; i < n; ++i)
			if (now[i] >= 0)
				++size[now[i]];
		const int main = int(std::max_element(size.begin(), size.end()) - size.begin());
		std::vector<unsigned char> given(std::max(1, count), 0);
		for (int i = 0; i < n; ++i)
			if (now[i] >= 0 && hopeless[i])
				given[now[i]] = 1;
		int stranded = -1;
		for (int k = 0; k < count && stranded < 0; ++k)
			if (k != main && size[k] >= kRepairPiece && !given[k])
				stranded = k;
		if (stranded < 0)
			break;
		std::vector<int> sources;
		std::vector<unsigned char> goal(n, 0);
		for (int i = 0; i < n; ++i)
		{
			if (now[i] == stranded)
				sources.push_back(i);
			goal[i] = now[i] >= 0 && now[i] != stranded;
		}
		// Shallow water costs a ford; a lagoon's deep water costs more; the panhandle's river is never
		// crossed this way.
		const auto route = cheapestWalk(f, GridNeighbors::Cardinal, sources, goal, [&](int, int to, int, int)
										{
											if (walk[to])
												return 1;
											if (core[to])
												return -1;
											return sketch[to] == WATER ? 20 : sketch[to] == DEEP_WATER ? 60 : -1;
										});
		if (route.empty())
		{
			for (int i : sources)
				hopeless[i] = 1;
			continue;
		}
		for (int i : route)
			if (sketch[i] == WATER || sketch[i] == DEEP_WATER)
				for (int dv = -1; dv <= 1; ++dv)
					for (int du = -1; du <= 1; ++du)
					{
						const int j = f.at(f.remainderX(i) + du, i / f.w + dv);
						if ((sketch[j] == WATER || sketch[j] == DEEP_WATER) && !core[j])
						{
							sketch[j] = SAND;
							look[j] = PALE_SAND;
						}
					}
		++repairs;
	}
	{
		// What the repair could not reach, for the record.
		for (int i = 0; i < n; ++i)
		{
			const auto group = terrainGroup(TerrainType(sketch[i]));
			walk[i] = group != TerrainGroup::Water && group != TerrainGroup::DeepWater &&
					  group != TerrainGroup::Ridges && group != TerrainGroup::Obstacles;
		}
		const std::vector<int> now = connectedRegions(walk, f.w, f.h, true, GridNeighbors::Cardinal);
		const int count = *std::max_element(now.begin(), now.end()) + 1;
		std::vector<int> size(std::max(1, count), 0);
		for (int i = 0; i < n; ++i)
			if (now[i] >= 0)
				++size[now[i]];
		int apart = 0;
		for (int k = 0; k < count; ++k)
			apart += size[k] >= kRepairPiece;
		context.telemetry.measure("braided-delta.fords.pieces-left", apart - 1);
	}
	if (repairs)
		context.telemetry.fallback("braided-delta.fords.repair", "a cut-off piece got a ford by the cheapest crossing");
	context.telemetry.measure("braided-delta.fords.repairs", repairs);
	for (int i = 0; i < n; ++i)
		if (sketch[i] == SAND && !look[i])
			look[i] = PALE_SAND;
	context.telemetry.measure("braided-delta.bridges", L.bridges);
	context.telemetry.measure("braided-delta.fords", L.fords);
	context.telemetry.measure("braided-delta.channels", L.arms);
	context.telemetry.choice("braided-delta.orientation", L.transpose ? "vertical" : "horizontal");

	// Homes, in the frame: squares of pure grass with clear ground round them, spread by walking
	// distance, on the swamp's rim where they can.
	std::vector<int> sites;
	{
		const auto grass = pureTiles(sketch, f, GRASS);
		std::vector<unsigned char> blocked(n, 0), walkable(n, 0);
		for (int i = 0; i < n; ++i)
		{
			const int u = f.remainderX(i), v = i / f.w;
			bool dry = false;
			for (int k = 0; k < 4; ++k)
			{
				const auto group = terrainGroup(TerrainType(sketch[f.at(u + (k & 1), v + (k >> 1))]));
				dry = dry || (group != TerrainGroup::Water && group != TerrainGroup::DeepWater &&
							  group != TerrainGroup::Ridges && group != TerrainGroup::Obstacles);
			}
			walkable[i] = dry;
			blocked[i] = !grass[i];
		}
		const std::vector<int> room = stepsFrom(f, blocked);
		std::vector<unsigned char> candidates(n, 0);
		for (int i = 0; i < n; ++i)
			candidates[i] = room[i] >= kHome / 2 + 1 && r[i] > swampEdge;
		// The rim first; anywhere roomy only when the rim cannot hold every colony.
		std::vector<unsigned char> rim = candidates;
		for (int i = 0; i < n; ++i)
			rim[i] = rim[i] && r[i] > 0.6 && r[i] < 1.5;
		sites = farthestSites(f, rim, walkable, request.nbTeams, context, "braided-delta-starts");
		if (int(sites.size()) < request.nbTeams)
		{
			context.telemetry.fallback("braided-delta.starts.off-rim", "The rim has too little room for every colony");
			sites = farthestSites(f, candidates, walkable, request.nbTeams, context, "braided-delta-starts");
		}
		if (int(sites.size()) < request.nbTeams)
		{
			L.failure = "The delta has too little open ground for its colonies; enlarge the map.";
			return L;
		}
	}
	// The deferred features, outside every home's clearing, which is plain grass: the starting-crop
	// guarantee plants only on plain grass, not on its lush or dry looks.
	{
		std::vector<unsigned char> clearing(n, 0);
		for (int site : sites)
			for (int dv = -kHomeClearing - 2; dv <= kHomeClearing + 2; ++dv)
				for (int du = -kHomeClearing - 2; du <= kHomeClearing + 2; ++du)
					if (du * du + dv * dv <= (kHomeClearing + 2) * (kHomeClearing + 2))
					{
						const int i = f.at(f.remainderX(site) + du, site / f.w + dv);
						clearing[i] = du * du + dv * dv <= kHomeClearing * kHomeClearing ? 2 : 1;
					}
		for (int i = 0; i < n; ++i)
		{
			if (clearing[i] == 2 && sketch[i] == GRASS)
				look[i] = GRASS;
			if (decoMech[i] && !clearing[i] && sketch[i] == GRASS)
			{
				sketch[i] = decoMech[i];
				look[i] = decoLook[i];
				duneTiles += decoLook[i] == DUNE_SAND;
			}
		}
		context.telemetry.measure("braided-delta.desert.dunes", duneTiles);
	}

	// Onto the map: mechanics, and looks where they share the mechanic.
	L.terrain.assign(t.size(), GRASS);
	L.paint.assign(t.size(), GRASS);
	L.frameU.assign(t.size(), 0);
	L.isles.assign(t.size(), 0);
	L.frameV.assign(t.size(), 0);
	for (int i = 0; i < n; ++i)
	{
		const int m = L.toMap(i);
		L.terrain[m] = sketch[i];
		const TerrainType mechanic = TerrainType(sketch[i]), chosen = TerrainType(look[i]);
		L.paint[m] = look[i] && terrainGroup(chosen) == terrainGroup(mechanic) ? chosen
					 : mechanic == SAND										 ? PALE_SAND
																			 : mechanic;
		L.frameU[m] = f.remainderX(i);
		L.isles[m] = isle[i];
		L.frameV[m] = i / f.w;
	}

	for (int site : sites)
	{
		const int m = L.toMap(site);
		L.homes.push_back({double(t.remainderX(m)), double(m / t.w)});
	}
	dealStarts(context, L.homes, "braided-delta-deal");
	L.homeOf.assign(t.size(), -1);
	for (int team = 0; team < request.nbTeams; ++team)
		for (int dy = -kHome / 2; dy < kHome / 2; ++dy)
			for (int dx = -kHome / 2; dx < kHome / 2; ++dx)
				L.homeOf[t.at(int(L.homes[team].x) + dx, int(L.homes[team].y) + dy)] = team;
	return L;
}

// The request check, generation and validation each ask for the design; it is built once.
Layout design(const GenerationRequest &request, GenerationContext &context)
{
	return cachedDesign(request, context, designAfresh);
}

bool generate(Game &game, GenerationContext &context)
{
	context.stage = "braided delta layout";
	const BraidedDeltaOptions o(context.request);
	const Layout L = design(context.request, context);
	if (!L.failure.empty())
	{
		context.detail = L.failure;
		return false;
	}
	const Torus &t = L.t;
	Map &map = game.map;
	writeVertices(map, L.paint);
	for (int k = 0; k < context.request.nbTeams; ++k)
		game.addTeam();
	context.stage = "braided delta colonies";
	if (!settleColonies(
			game, context, "braided-delta-colonies",
			[&](int team) { return homeGrassMask(map, t, L.homeOf, team); },
			[&](int team) { return MapGeneratorPoint(t.x(int(L.homes[team].x) - 2), t.y(int(L.homes[team].y) - 2)); }))
		return false;
	context.stage = "braided delta resources";
	// Every colony's starter kit, before anything ambient: a wheat patch and a copse on opposite
	// sides of its clearing, outside the home square, on plain grass. Starter supplies stay at
	// these sizes whatever the resource amounts.
	{
		std::mt19937 &kits = context.stream("braided-delta-kits");
		for (int team = 0; team < context.request.nbTeams; ++team)
		{
			const ShapePoint home = L.homes[team];
			const double turn = 2 * kPi * unit(kits);
			for (int crop = 0; crop < 2; ++crop)
			{
				const double heading = turn + crop * kPi;
				const int ax = int(home.x + kKitDistance * N::cos(heading));
				const int ay = int(home.y + kKitDistance * N::sin(heading));
				const int wanted = crop ? kKitWood : kKitWheat;
				const auto placed = growPatchesNear(map, t, ax, ay, 8, crop ? WOOD : WHEAT, wanted, [&](int i)
													{
														return L.homeOf[i] < 0 && map.isGrass(t.remainderX(i), i / t.w) &&
															   t.chebyshev(t.remainderX(i), i / t.w, int(home.x), int(home.y)) <=
																   kHomeClearing;
													});
				context.telemetry.measure(crop ? "braided-delta.kit.wood" : "braided-delta.kit.wheat", placed.tiles, team);
				if (placed.tiles < wanted)
					context.telemetry.fallback("braided-delta.kit.short", "The clearing could not hold the whole kit", team);
			}
		}
	}
	// The great island's orchard, the swamp's prize: a grove of each fruit near its middle.
	if (L.greatIsland >= 0 && o.fruit > 0)
	{
		const int gx = t.remainderX(L.greatIsland), gy = L.greatIsland / t.w;
		const int trees = int(std::max<std::int64_t>(1, scaledCount(kGroveTrees, o.fruit)));
		for (int fruit = 0; fruit < 3; ++fruit)
		{
			const double heading = 2 * kPi * fruit / 3;
			const auto placed = growPatchesNear(map, t, gx + int(4 * N::cos(heading)), gy + int(4 * N::sin(heading)), 10,
												CHERRY + fruit, trees, [&](int i) { return L.isles[i] == 2; });
			context.telemetry.measure("braided-delta.orchard.trees", placed.tiles, fruit);
			if (placed.tiles < trees)
				context.telemetry.fallback("braided-delta.orchard.short", "The great island could not hold a whole grove", fruit);
		}
	}
	// Stone at the kopjes' feet: a small quarry beside each outcrop.
	if (o.stone > 0)
	{
		const auto outcrop = pureTiles(L.terrain, t, OUTCROP);
		const std::vector<int> kopjes = connectedRegions(outcrop, t.w, t.h, true, GridNeighbors::Eight);
		const int count = *std::max_element(kopjes.begin(), kopjes.end()) + 1;
		std::vector<int> anchor(std::max(1, count), -1);
		for (int i = 0; i < t.size(); ++i)
			if (kopjes[i] >= 0 && anchor[kopjes[i]] < 0)
				anchor[kopjes[i]] = i;
		int quarries = 0;
		for (int k = 0; k < count; ++k)
		{
			const auto placed = growPatchesNear(map, t, t.remainderX(anchor[k]), anchor[k] / t.w, 6, STONE,
												int(scaledCount(kQuarryStone, o.stone)), [&](int i) { return L.homeOf[i] < 0; });
			quarries += placed.tiles > 0;
		}
		context.telemetry.measure("braided-delta.quarries", quarries);
	}
	// Wheat on the levee loam and woods on the lush ground beside the water and on the islands, as
	// much of each as the amounts ask, in stands rather than speckle.
	const std::vector<int> stands = periodicNoise(t.w, t.h, 6, context.stream("braided-delta-stands"));
	const auto loam = pureTiles(L.terrain, t, LOAM), grass = pureTiles(L.terrain, t, GRASS);
	const double wheatShare = std::min(0.9, kWheatShare * o.wheat / 100), woodShare = std::min(0.7, kWoodShare * o.wood / 100);
	for (int i = 0; i < t.size(); ++i)
	{
		const int x = t.remainderX(i), y = i / t.w;
		if (L.homeOf[i] >= 0 || !map.isFreeForGroundUnit(x, y, false, 0))
			continue;
		if (loam[i] && stands[i] >= 65536 * (1 - wheatShare))
			map.setResourceByIndex(x, y, WHEAT, 1);
		else if (grass[i] && TerrainType(L.paint[i]) == LUSH_GRASS && stands[i] < 65536 * woodShare)
			map.setResourceByIndex(x, y, WOOD, 1);
	}
	seedAlgae(map, context, t, "braided-delta-algae", o.algae, AlgaeBand::anyWater(90));
	// The guarantee measures from the swarm's boot tile and the validator from the workers, a few
	// steps apart: aim a few steps short of the validator's ranges.
	secureStartingCrops(game, context, t, 20, 28);
	return true;
}

// The panhandle's river is a wall to every unit between its gorge, its bridges and the apex: in
// each stretch between two cuts, nothing that walks or swims can get from one bank to the other
// inside a band along the river. A tile's bank is the side of the river's middle it lies on.
std::string riverWallFault(const Layout &L, const Map &map)
{
	const Torus &t = L.t;
	const RiverPlan &river = L.river;
	std::vector<double> cuts{river.start};
	for (double b : river.bridges)
		cuts.push_back(b);
	cuts.push_back(river.end - 6);
	std::sort(cuts.begin(), cuts.end());
	const double band = river.halfWidth + 8, bank = river.halfWidth + 3;
	for (size_t s = 0; s + 1 < cuts.size(); ++s)
	{
		const double from = s ? cuts[s] + 4 : cuts[s], to = s + 2 < cuts.size() ? cuts[s + 1] - 4 : cuts[s + 1];
		if (to - from < 2)
			continue;
		// Each tile of the stretch's band: its side, and whether it is out on a bank.
		std::vector<signed char> side(t.size(), 0);
		std::vector<int> queue;
		for (int i = 0; i < t.size(); ++i)
		{
			const double u = L.frameU[i];
			if (u < from || u > to || size_t(u) >= river.middle.size())
				continue;
			const double offset = ChannelDetail::centred(L.frameV[i] + 0.5 - river.middle[size_t(u)], L.across);
			if (std::abs(offset) > band)
				continue;
			side[i] = offset > 0 ? 1 : -1;
			const TerrainProperties &rules = map.terrainPropertiesAt(i);
			if (side[i] > 0 && offset > bank && (rules.walkable || rules.swimmable))
				queue.push_back(i);
		}
		std::vector<unsigned char> seen(t.size(), 0);
		for (int i : queue)
			seen[i] = 1;
		for (size_t head = 0; head < queue.size(); ++head)
		{
			const int i = queue[head];
			const int x = t.remainderX(i), y = i / t.w;
			if (side[i] < 0)
			{
				const double offset =
					ChannelDetail::centred(L.frameV[i] + 0.5 - river.middle[size_t(L.frameU[i])], L.across);
				if (-offset > bank)
					return "Units can cross the panhandle's river near (" + std::to_string(x) + ", " + std::to_string(y) +
						   "), between flow " + std::to_string(int(from)) + " and " + std::to_string(int(to)) +
						   ", away from its bridges.";
			}
			for (int dy = -1; dy <= 1; ++dy)
				for (int dx = -1; dx <= 1; ++dx)
				{
					const int j = t.at(x + dx, y + dy);
					if (seen[j] || !side[j])
						continue;
					const TerrainProperties &rules = map.terrainPropertiesAt(j);
					if (rules.walkable || rules.swimmable)
					{
						seen[j] = 1;
						queue.push_back(j);
					}
				}
		}
	}
	return "";
}

// Every sizeable stretch of ground can be walked to from the first colony: a channel no ford
// crosses would strand it.
std::string strandedGroundFault(const Layout &L, const Map &map, const std::vector<int> &steps)
{
	const Torus &t = L.t;
	std::vector<unsigned char> ground(t.size(), 0);
	for (int i = 0; i < t.size(); ++i)
		ground[i] = map.terrainPropertiesAt(i).walkable;
	const std::vector<int> pieces = connectedRegions(ground, t.w, t.h, true, GridNeighbors::Eight);
	const int count = *std::max_element(pieces.begin(), pieces.end()) + 1;
	std::vector<int> size(std::max(1, count), 0);
	std::vector<unsigned char> reached(std::max(1, count), 0);
	for (int i = 0; i < t.size(); ++i)
		if (pieces[i] >= 0)
		{
			++size[pieces[i]];
			reached[pieces[i]] = reached[pieces[i]] || steps[i] >= 0;
		}
	for (int i = 0; i < t.size(); ++i)
		if (pieces[i] >= 0 && !reached[pieces[i]] && size[pieces[i]] >= 48)
			return "A stretch of the delta (" + std::to_string(size[pieces[i]]) + " tiles, at " +
				   std::to_string(t.remainderX(i)) + ", " + std::to_string(i / t.w) + ") has no ford to it.";
	return "";
}

std::string validateWorld(const Game &game, const GenerationContext &context)
{
	GenerationContext replay(context.request);
	const Layout L = design(context.request, replay);
	if (const auto error = designMismatch(L, game.map, "braided delta"); !error.empty())
		return error;
	// The finished terrain is the design's, corner for corner: nothing after it filled a channel
	// or laid a crossing the checks below did not plan for.
	for (int i = 0; i < L.t.size(); ++i)
		if (game.map.vertexTerrainAt(L.t.remainderX(i), i / L.t.w) != TerrainType(L.paint[i]))
			return "The delta's terrain has changed since it was laid out.";
	const auto walk = walkFromFirstColony(game.map, context.request.nbTeams, "the delta", "over its fords and bridges");
	if (!walk.error.empty())
		return walk.error;
	if (const auto error = riverWallFault(L, game.map); !error.empty())
		return error;
	{
		// Terrain alone: crops and woods can be cut, so they don't strand ground.
		const Torus t(game.map);
		std::vector<unsigned char> open(t.size(), 0);
		for (int i = 0; i < t.size(); ++i)
			open[i] = game.map.terrainPropertiesAt(i).walkable;
		const auto steps = stepsFrom(t, tileMask(t, walk.workers.at(0)), open);
		if (const auto error = strandedGroundFault(L, game.map, steps); !error.empty())
			return error;
	}
	return startingAccessFailure(game.map, context.request.nbTeams,
								 {{MaterialId::Food, 24, "food"}, {MaterialId::Wood, 32, "wood"}}, 16, 24);
}
} // namespace

BraidedDeltaOptions::BraidedDeltaOptions(const GenerationRequest &r)
	: flood(r.option("flood")), distributaries(r.option("distributaries")), islands(r.option("islands")),
	  crossings(r.option("river-crossings")), dunes(r.option("dunes")), wheat(r.option("wheat-amount")),
	  wood(r.option("wood-amount")), stone(r.option("stone-amount")), algae(r.option("algae-amount")),
	  fruit(r.option("fruit-amount"))
{
}

GeneratorDefinition braidedDeltaDefinition()
{
	return {"braided-delta",
			36,
			"Braided Delta",
			6,
			false,
			{GeneratorControl::choice("flood", "Flood", {"Dry", "Normal", "Wet"}, 1, ControlGroup::Terrain)
				 .withSearchValues({0, 1, 2}),
			 GeneratorControl::choice("distributaries", "Distributaries", {"Few", "Normal", "Many"}, 1,
									  ControlGroup::Terrain)
				 .withSearchValues({0, 1, 2}),
			 GeneratorControl{"islands", "Islands", 0, 100, 10, 50, ControlGroup::Terrain}.withSearchRange(20, 80),
			 GeneratorControl{"river-crossings", "River crossings", 1, 3, 1, 2, ControlGroup::Layout}
				 .withSearchRange(1, 3),
			 GeneratorControl{"dunes", "Dunes", 0, 100, 10, 50, ControlGroup::Terrain}.withSearchRange(20, 80),
			 GeneratorControl::percentage("wheat-amount", "Wheat amount"),
			 GeneratorControl::percentage("wood-amount", "Wood amount"),
			 GeneratorControl::percentage("stone-amount", "Stone amount"),
			 GeneratorControl::percentage("algae-amount", "Algae amount"),
			 GeneratorControl::percentage("fruit-amount", "Fruit amount")},
			generate,
			true,
			designFailure<design>,
			validateWorld,
			{"terrain:natural", "feature:river", "feature:swamp", "feature:desert", "feature:islands",
			 "style:contested-center"}};
}
