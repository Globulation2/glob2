// SPDX-License-Identifier: GPL-3.0-or-later
#include "FjordContinentGenerator.h"
#include "Climates.h"
#include "DesignCache.h"
#include "GenerationContext.h"
#include "GenerationWorld.h"
#include "Geometry.h"
#include "Grid.h"
#include "HeightFieldLooks.h"
#include "LatticeNoise.h"
#include "Pipeline.h"
#include "Planting.h"
#include "Resources.h"
#include "Roads.h"
#include "Rock.h"
#include "Sketch.h"
#include "Walls.h"
#include <algorithm>
#include <array>
#include <climits>
#include <cmath>
#include <limits>
#include <string>
#include <vector>
using namespace MapGeneration;
namespace N = MapGeneration::Numeric;
// Fjord continent (id 12), 2.0: a frozen continent. One peninsula per colony hangs off an ice
// sheet at the heart of the continent, and a fjord runs between every pair of neighbours. The
// fjords are open sea at the mouth and frozen from the ice line inward; their banks are glacier
// walls, broken by a few landings where a colony can step down onto the ice.
//
// THE DESIGN, IN GAME TERMS (docs/map-generators/fjord-continent.md,
// docs/map-generators/game-rules-for-map-design.md):
// - Ice is the front line. Ice is walkable at half speed, wears ground units down a little at
//   every step and holds no building and no crop, so it is ground everybody can cross and nobody
//   can own. There is no dry route between two colonies: every contact is over a frozen fjord
//   (through a landing) or across the sheet (through the colony's gap in the penitente rim), until
//   colonies swim and can cross the open fjord mouths too.
// - The landings are the chokepoints. Every bank of every fjord has the same number of them, at
//   the same places along the fjord, so every colony owns the same number of doors onto the ice;
//   the glacier walls between them block arrows, so a tower guards its landing and no more.
// - Home is the peninsula's outer half, on the sea and the open fjord mouths: water is what feeds
//   wheat and wood, and ice feeds nothing. Every bank carries a guaranteed wheat and wood patch near
//   the mouth, so every colony is fed from the two fjords that flank it.
// - The walls are the quarries: every colony has a stone face at the foot of each flanking fjord's
//   wall and one at the rim beside its gap.
// - Nunataks, rock islets standing out of the sheet, hold the prizes: fruit and stone between every
//   pair of neighbours and a richer one in the middle, reached only over the ice.
// - Islands out at sea are a bonus for the first colony to swim.
//
// STAGES: design (a pure function of the request, cached): the continent, the sheet and its rim,
// the fjords with their ice and walls, landings, tundra, nunataks, islands, beaches and deep water,
// both as rule terrain and as looks; write the rules; settle the colonies; plant the kits, the bank
// crops, the quarry faces, the prizes, the algae and the ambient scatter; keep the landings and
// gaps open; guarantee every colony its crops; write the looks. validateWorld rebuilds the design
// and checks the finished world against it.
namespace
{
// A glacier wall is every land vertex within this many vertices (Chebyshev) of a fjord.
constexpr int kWallReach = 3;
// The penitente rim round the sheet, in the continent's frame (about tiles).
constexpr double kRimWidth = 3.0;
// Half the width of a colony's gap in the rim, and of a landing in a fjord wall.
constexpr double kGapHalf = 4.0;
// Half a landing's length along its fjord, in tiles, and how many centre-line samples (half
// tiles) a landing may slide to find its bank's own ground.
constexpr double kLandingHalf = 3.5;
constexpr int kLandingSlide = 30;
// Share of a fjord's length over which it may swing sideways; the rest runs straight to the sheet.
constexpr double kStraightHead = 0.75;
// How far past the rim a colony's gap is kept clear of fjord walls.
constexpr double kGapApproach = 4.0;
// Where landings and crops may go along a fjord, as progress from the mouth (0) to the head (1):
// past the mouth's own stretch of open water and short of the rim.
constexpr double kLandingFloor = 0.12;
// The meadows: grass within kMeadowDepth to kMeadowDepth + kMeadowFray steps of open water, and
// within kHomeMeadow of every home. Drifts of deep snow where the drift noise passes kDriftCut.
constexpr double kMeadowDepth = 6.0, kMeadowFray = 6.0, kHomeMeadow = 7.0;
constexpr int kDriftCut = 50000, kDriftCutInland = 38000, kDriftCutPerStep = 300;
// The warmer tundra's band beyond the meadows, in steps (plus up to 7 more from noise).
constexpr int kTundraBand = 5;
// Half the width of the drift-free spine from each home to its gap.
constexpr double kSpineHalf = 6.0;
// Where along each fjord its quarry faces are tried, in order: by the mouth, inside the meadows.
constexpr double kQuarryVerge = 0.12;
constexpr double kQuarryTries[] = {0.08, 0.12, 0.04, 0.16, 0.20, 0.24, 0.0, 0.28, 0.32, 0.36};
// Starts: this far in from the coast along the colony's axis.
constexpr double kHomeInset = 11.0;
// Sea kept between the continent's widest point and the map's edge (the torus wraps).
constexpr double kEdgeClear = 6.0;
// Each quarry face, and the least stone a colony's walls must carry on the finished map.
constexpr int kQuarryTiles = 6;
constexpr int kQuarryLeast = 6;
// The share of a map a grass landscape's ambient scatter was sized for.
constexpr double kGrassShare = 0.45;
// How far from home the fallback face may stand.
constexpr int kQuarrySearch = 40;
// Algae: one clump per this many shallow tiles at an amount of 100, on the best-growing share.
constexpr int kAlgaeTilesPerClump = 40;
// Fruit clumps on each nunatak between neighbours, and of each fruit on the middle one.
constexpr int kNunatakGroves = 3, kCentreGroves = 2;
// The ring of nunataks between neighbours, as a share of the sheet's radius.
constexpr double kNunatakRing = 0.58;

struct Landing
{
	int owner, fjord;
	std::vector<int> tiles; // the tiles of the gap in the wall
};

struct Nunatak
{
	ShapePoint at; // map frame
	double radius;
	bool centre;
	int fruit;
};

// The finished design: everything a generation and its validator need, as plain data.
struct Layout
{
	std::string failure;
	Torus t{1, 1};
	int teams = 0;
	TerrainSketch mechanics, looks;
	std::vector<int> homeOf;              // per tile: the colony whose peninsula it is, or -1
	std::vector<ShapePoint> home;         // per colony, map frame
	std::vector<double> facing;           // per colony: outward, a map angle
	std::vector<Landing> landings;        // every landing, both banks of every fjord
	std::vector<std::vector<int>> gaps;   // per colony: the vertices (as tiles) of its gap in the rim
	std::vector<unsigned char> keepOpen;  // tiles no deposit may block: landings, gaps, their approach
	std::vector<std::vector<KitSeed>> quarries; // per colony
	// Per fjord, in the map frame: the centre line, its normal (towards the east bank) and radius.
	std::vector<std::vector<ShapePoint>> line, normal;
	std::vector<std::vector<double>> width;
	std::vector<int> west, east; // per fjord: the colony on each bank
	double landHigh = 0;         // the highest progress a landing or crop may take
	std::vector<Nunatak> nunataks;
	std::vector<Island> islands;
	int ice = 0, glacier = 0, penitentes = 0, tundra = 0;
	double stretch = 1; // the most the continent's frame stretches a length on the map
	Stretch fill;       // how a rectangular map stretches the round frame to fill it
};

// A door's region: the colony's own ground (off the ice) touching the door's tiles, diagonals
// included. The cut tiles themselves mostly touch the fjord's ice; a unit reaches the door when it
// reaches this.
std::vector<int> doorRegion(const Torus &t, const std::vector<int> &homeOf, const std::vector<int> &door,
							int owner)
{
	std::vector<unsigned char> seen(t.size(), 0);
	std::vector<int> region;
	for (int v : door)
		for (int dy = -1; dy <= 1; ++dy)
			for (int dx = -1; dx <= 1; ++dx)
			{
				const int i = t.at(t.remainderX(v) + dx, v / t.w + dy);
				if (homeOf[i] == owner && !seen[i])
				{
					seen[i] = 1;
					region.push_back(i);
				}
			}
	return region;
}

double wrapAngle(double a)
{
	while (a > kPi)
		a -= 2 * kPi;
	while (a < -kPi)
		a += 2 * kPi;
	return a;
}

// How many map tiles one length of the continent's round frame covers along map direction `nm` (a
// unit vector): a fjord's width is drawn in the round frame, and on a rectangular map the frame is
// stretched to fill it, so a fjord running down a long map's short axis is several times wider than
// drawn. A square map's fill is the identity and the factor exactly 1.
double acrossScale(const Stretch &fill, ShapePoint nm)
{
	if (fill.sx == fill.sy)
		return 1.0;
	const ShapePoint round = fill.undo(nm.x, nm.y);
	return 1.0 / std::max(1e-9, N::hypot(round.x, round.y));
}

Layout designAfresh(const GenerationRequest &request, GenerationContext &context)
{
	Layout L;
	const FjordContinentOptions o(request);
	L.t = {1 << request.wDec, 1 << request.hDec};
	L.teams = request.nbTeams;
	const Torus &t = L.t;
	const int W = t.w, H = t.h, n = t.size();
	if (L.teams < 1)
	{
		L.failure = "Fjord continents need at least one colony.";
		return L;
	}

	// The continent's frame: half a turn of rotation covers every orientation of an oval, a stretch
	// of 1 to 1.3 keeps it from looking cut from a circle, and on a rectangular map the whole frame
	// is stretched to fill it.
	const double rotation = context.bounded("layout", 3600) / 3600.0 * kPi;
	const double elongation = 1.0 + context.bounded("layout", 1000) / 1000.0 * 0.3;
	const ShapeTransform xf({W / 2.0, H / 2.0}, rotation, elongation);
	const Stretch fill = Stretch::toFill(W, H);
	L.fill = fill;
	const double maxStretch = std::max(elongation, 1.0 / elongation) * fill.longest();
	L.stretch = maxStretch;
	const auto toShape = [&](ShapePoint p)
	{
		const ShapePoint round = fill.undo(p.x - W / 2.0, p.y - H / 2.0);
		return xf.toShape({W / 2.0 + round.x, H / 2.0 + round.y});
	};
	const auto toMap = [&](ShapePoint q) { return fill.apply(W / 2.0, H / 2.0, xf.toMap(q)); };
	const auto polar = [](double r, double a) { return ShapePoint{r * N::cos(a), r * N::sin(a)}; };

	// The coast wobbles by four harmonics whose amplitudes add up to at most roughness x 1.4.
	const double baseR = o.continentSize / 100.0 * std::min(W, H);
	const RadialShape coastShape(baseR, o.roughness / 100.0, context, "coast", 1.4);
	// The continent must not touch its own copy across the map's edge: its widest wobble, stretched,
	// keeps kEdgeClear tiles of sea from the edge, or the whole continent is scaled down to fit.
	const double fit = std::min(1.0, (std::min(W, H) / 2.0 - kEdgeClear) /
										  (coastShape.maximumRadius() * std::max(elongation, 1.0 / elongation)));
	const auto coastAt = [&](double a) { return fit * coastShape.radiusAt(a); };
	context.telemetry.measure("fjord-continent.coast.fit", fit);
	// The sheet: a tenth of the continent's radius at size 0, under three tenths at 90, its edge
	// wobbling by up to 15%. Its rim runs round it kRimWidth thick.
	const double sheetR = fit * baseR * (0.10 + 0.002 * o.sheetSize);
	const RadialShape sheet(sheetR, 0.12, context, "sheet", 1.0);

	// One sector per colony, evenly spaced with a little jitter.
	std::vector<double> theta(L.teams);
	{
		const double base = context.bounded("layout", 3600) / 3600.0 * 2 * kPi;
		const double slot = 2 * kPi / L.teams;
		for (int i = 0; i < L.teams; ++i)
			theta[i] = base + i * slot + (context.bounded("layout", 2001) / 1000.0 - 1.0) * 0.2 * slot;
	}
	context.telemetry.measure("fjord-continent.coast.rotation-radians", rotation);
	context.telemetry.measure("fjord-continent.coast.elongation", elongation);
	context.telemetry.measure("fjord-continent.sheet.radius", sheetR);

	// Every vertex in the continent's frame.
	std::vector<double> rad(n), ang(n);
	for (int i = 0; i < n; ++i)
	{
		generationCheckpoint();
		const ShapePoint s = toShape({double(t.remainderX(i)), double(i / t.w)});
		rad[i] = N::hypot(s.x, s.y);
		ang[i] = N::atan2(s.y, s.x);
	}

	// The fjords: an S-curve from just outside the coast on the line between two neighbours, in
	// towards the middle, ending inside the sheet so the frozen reach runs into it. Each vertex
	// within a fjord's radius of its centre line belongs to the nearest sample of it.
	const int fjords = L.teams >= 2 ? L.teams : 0;
	std::vector<int> fjordOf(n, -1), sampleOf(n, -1);
	std::vector<double> score(n, std::numeric_limits<double>::max());
	std::vector<std::vector<ShapePoint>> shapeLine(fjords);
	std::vector<double> iceFrom(fjords, 1.0);
	const int landings = o.landings;
	L.line.resize(fjords);
	L.normal.resize(fjords);
	L.width.resize(fjords);
	L.west.resize(fjords);
	L.east.resize(fjords);
	double landHigh = 0.85;
	for (int k = 0; k < fjords; ++k)
	{
		const int a = k, b = (k + 1) % L.teams;
		double gap = N::fmod(theta[b] - theta[a], 2 * kPi);
		if (gap < 0)
			gap += 2 * kPi;
		const double mid = theta[a] + gap / 2;
		L.west[k] = a;
		L.east[k] = b;
		const double mouthR = coastAt(mid) + 3.0;
		const double headR = sheetR * 0.7;
		const ShapePoint mouth = polar(mouthR, mid), head = polar(headR, mid);
		const ShapePoint perp{-N::sin(mid), N::cos(mid)};
		// The sideways swing: 17% of the arc between the neighbours at the coast, faded to nothing at
		// the mouth and well before the head.
		// Capped at a four-colony map's arc: with two or three colonies the full arc would fold a
		// fjord back on itself.
		// On a rectangular map the frame is stretched to fill it, and a swing across the long axis
		// would grow with the stretch (four times on a 512x128 map) until the fjord ran along the
		// coast and left one bank no ground at its mouth; it is shrunk back to its round length. A
		// square map's fill is the identity and keeps the swing as drawn.
		double swingStretch = 1.0;
		if (fill.sx != fill.sy)
		{
			const ShapePoint from = xf.toMap({0, 0}), to = xf.toMap(perp);
			const ShapePoint drawn = toMap(perp), centre = toMap({0, 0});
			swingStretch = N::hypot(drawn.x - centre.x, drawn.y - centre.y) /
						   std::max(1e-9, N::hypot(to.x - from.x, to.y - from.y));
		}
		const double amplitude = std::min({gap, 2 * kPi - gap, kPi / 2}) * mouthR * 0.17 / swingStretch;
		const double phase = context.bounded("layout", 3600) / 3600.0 * 2 * kPi;
		const double mouthWidth = o.fjordWidth + 0.6 + context.bounded("layout", 1400) / 1000.0;
		const double headWidth = std::max(1.8, mouthWidth * 0.5);
		context.telemetry.measure("fjord-continent.fjords.mouth-width", mouthWidth, k);
		context.telemetry.measure("fjord-continent.fjords.wiggle-amplitude", amplitude, k);
		// Two samples per tile of straight length, so a landing's length in samples is in half tiles.
		const int steps = std::max(48, int(2 * (mouthR - headR)));
		for (int s = 0; s <= steps; ++s)
		{
			generationCheckpoint();
			const double p = double(s) / steps;
			// The swing fades to nothing by kStraightHead, so every fjord's last stretch runs straight
			// in on its axis, halfway between the neighbours' gaps.
			const double lateral =
				p < kStraightHead ? amplitude * N::sin(2 * kPi * p + phase) * N::sin(kPi * p / kStraightHead) : 0.0;
			const ShapePoint c{mouth.x + p * (head.x - mouth.x) + lateral * perp.x,
							   mouth.y + p * (head.y - mouth.y) + lateral * perp.y};
			const double w = mouthWidth * (1 - p) + headWidth * p;
			shapeLine[k].push_back(c);
			const ShapePoint m = toMap(c);
			L.line[k].push_back(m);
			L.width[k].push_back(w);
			const int reach = int(std::ceil(w * maxStretch)) + 1;
			const int ix = int(N::lround(m.x)), iy = int(N::lround(m.y));
			for (int dy = -reach; dy <= reach; ++dy)
				for (int dx = -reach; dx <= reach; ++dx)
				{
					const int j = t.at(ix + dx, iy + dy);
					const ShapePoint q = toShape({double(ix + dx), double(iy + dy)});
					const double dd = ((q.x - c.x) * (q.x - c.x) + (q.y - c.y) * (q.y - c.y)) / (w * w);
					if (dd <= 1.0 && dd < score[j])
					{
						score[j] = dd;
						fjordOf[j] = k;
						sampleOf[j] = s;
					}
				}
		}
		// Each sample's normal towards the east bank, in the map frame.
		for (int s = 0; s <= steps; ++s)
		{
			const ShapePoint prev = shapeLine[k][std::max(0, s - 1)];
			const ShapePoint next = shapeLine[k][std::min(steps, s + 1)];
			const double tx = next.x - prev.x, ty = next.y - prev.y;
			const double len = std::max(1e-9, N::hypot(tx, ty));
			// Perpendicular in the map's own frame (a stretched frame skews the continent's right
			// angles), turned towards the east bank as the continent's frame has it.
			const ShapePoint c = shapeLine[k][s];
			const ShapePoint side = toMap({c.x + ty / len, c.y - tx / len});
			const ShapePoint m = L.line[k][s];
			const ShapePoint mp = L.line[k][std::max(0, s - 1)], mn = L.line[k][std::min(steps, s + 1)];
			const double mx = mn.x - mp.x, my = mn.y - mp.y;
			const double mlen = std::max(1e-9, N::hypot(mx, my));
			ShapePoint nm{-my / mlen, mx / mlen};
			if (nm.x * (side.x - m.x) + nm.y * (side.y - m.y) < 0)
				nm = {-nm.x, -nm.y};
			L.normal[k].push_back(nm);
		}
		// Landings stop short of the rim: progress where the centre line is still kRimWidth + 5 past
		// the sheet's widest edge.
		const double rimClear = sheetR * 1.15 + kRimWidth + kGapApproach + 8.0;
		landHigh = std::min(landHigh, (mouthR - rimClear) / std::max(1.0, mouthR - headR));
	}
	if (fjords > 0 && landHigh < kLandingFloor + 0.15)
	{
		L.failure = "The fjords are too short for their landings; use a bigger continent or a smaller ice "
					"sheet.";
		return L;
	}
	L.landHigh = landHigh;
	// The ice line: ice reach 0 freezes nothing short of the landings' range, 100 freezes the fjord
	// to the end of the mouth's open water.
	const double frozenFrom = landHigh + 0.03 - o.iceReach / 100.0 * (landHigh + 0.03 - kLandingFloor);
	PeriodicNoise floe(W, H, 6, context.stream("ice"));
	for (int k = 0; k < fjords; ++k)
	{
		iceFrom[k] = frozenFrom;
		context.telemetry.measure("fjord-continent.fjords.ice-from", frozenFrom, k);
	}

	// The rule terrain, one canonical type per rule group.
	TerrainSketch &terrain = L.mechanics;
	terrain.assign(n, WATER);
	std::vector<unsigned char> fjordVertex(n, 0), sheetVertex(n, 0), rimVertex(n, 0), gapVertex(n, 0),
		nunatakVertex(n, 0), driftVertex(n, 0);
	for (int i = 0; i < n; ++i)
	{
		generationCheckpoint();
		const double edge = sheet.radiusAt(ang[i]);
		const bool land = rad[i] < coastAt(ang[i]);
		if (rad[i] < edge)
		{
			terrain[i] = ICE;
			sheetVertex[i] = 1;
			continue;
		}
		if (fjordOf[i] >= 0)
		{
			const int k = fjordOf[i];
			const double p = double(sampleOf[i]) / (L.line[k].size() - 1);
			const double ragged = (floe.at(t.remainderX(i), i / t.w) - 0.5) * 0.08;
			terrain[i] = p + ragged >= iceFrom[k] ? ICE : WATER;
			fjordVertex[i] = 1;
			continue;
		}
		if (!land)
			continue;
		terrain[i] = GRASS;
		if (rad[i] < edge + kRimWidth)
		{
			bool gap = false;
			for (int c = 0; c < L.teams && !gap; ++c)
				gap = std::abs(wrapAngle(ang[i] - theta[c])) * rad[i] < kGapHalf;
			if (gap)
				gapVertex[i] = 1;
			else
			{
				terrain[i] = RIDGE_ROCK;
				rimVertex[i] = 1;
			}
		}
	}

	// Each colony's spine, the line from its home in to its gap, is kept clear of fjord walls: where
	// many colonies crowd the sheet, the walls of the two fjords flanking a colony would otherwise
	// meet across its root and seal its gap off from its home.
	std::vector<unsigned char> spineVertex(n, 0);
	for (int i = 0; i < n; ++i)
		if (terrain[i] == GRASS)
			for (int c = 0; c < L.teams; ++c)
				if (std::abs(wrapAngle(ang[i] - theta[c])) * rad[i] < kGapHalf &&
					rad[i] < coastAt(theta[c]) - kHomeInset + 2)
					spineVertex[i] = 1;
	// Each gap runs on past the rim, so no fjord wall closes its outer end.
	L.gaps.assign(L.teams, {});
	for (int i = 0; i < n; ++i)
		if (terrain[i] == GRASS && rad[i] < sheet.radiusAt(ang[i]) + kRimWidth + kGapApproach)
			for (int c = 0; c < L.teams; ++c)
				if (std::abs(wrapAngle(ang[i] - theta[c])) * rad[i] < kGapHalf)
				{
					gapVertex[i] = 1;
					L.gaps[c].push_back(i);
				}
	// The glacier walls: every grass vertex within kWallReach of a fjord. Each remembers the fjord
	// sample it stands beside (the nearest fjord vertex's) and which bank it is on, so a landing is
	// cut from exactly the wall it opens.
	std::vector<double> wallProgress(n, -1);
	std::vector<int> wallFjord(n, -1), wallSample(n, -1), wallReach(n, kWallReach + 1);
	for (int i = 0; i < n; ++i)
	{
		if (!fjordVertex[i])
			continue;
		const int x = t.remainderX(i), y = i / t.w;
		for (int dy = -kWallReach; dy <= kWallReach; ++dy)
			for (int dx = -kWallReach; dx <= kWallReach; ++dx)
			{
				const int j = t.at(x + dx, y + dy);
				const int d = std::max(std::abs(dx), std::abs(dy));
				if (terrain[j] != GRASS || d >= wallReach[j])
					continue;
				wallReach[j] = d;
				wallFjord[j] = fjordOf[i];
				wallSample[j] = sampleOf[i];
			}
	}
	for (int i = 0; i < n; ++i)
		if (wallFjord[i] >= 0 && !gapVertex[i] && !spineVertex[i])
		{
			terrain[i] = CLIFFS;
			wallProgress[i] = double(wallSample[i]) / (L.line[wallFjord[i]].size() - 1);
		}

	// Walls whose look draws its rock need tiles with four rock corners: thicken both kinds onto
	// grass, never into a gap or within two steps of one (on the stretched axis of an oval a gap is
	// only a few tiles wide). The landings are cut afterwards, through the finished walls, so no
	// thickening can close one.
	{
		std::vector<unsigned char> door(n, 0);
		for (int i = 0; i < n; ++i)
			door[i] = gapVertex[i] || spineVertex[i];
		const std::vector<int> fromGap = stepsFrom(t, door);
		const auto canGrow = [&](int i) { return terrain[i] == GRASS && (fromGap[i] < 0 || fromGap[i] > 2); };
		thickenRidges(terrain, t, canGrow, nullptr, CLIFFS);
		thickenRidges(terrain, t, canGrow, nullptr, RIDGE_ROCK);
	}

	// Nunataks: rock islets standing out of the sheet, grass for a prize.
	if (o.nunataks && sheetR >= 7.0)
	{
		// Between neighbours at kNunatakRing of the sheet's radius, each no wider than 40% of the gap
		// to the next (many colonies crowd the ring) and clear of the middle one.
		const double chord = fjords > 0 ? 2 * kNunatakRing * sheetR * N::sin(kPi / fjords) : sheetR;
		const double islet = std::min(std::clamp(sheetR * 0.2, 3.0, 4.5), 0.4 * chord);
		for (int k = 0; k < fjords; ++k)
		{
			double gap = N::fmod(theta[L.east[k]] - theta[L.west[k]], 2 * kPi);
			if (gap < 0)
				gap += 2 * kPi;
			// Between the two neighbours, on the line their fjord points along, short of its head.
			L.nunataks.push_back({toMap(polar(sheetR * kNunatakRing, theta[L.west[k]] + gap / 2)), islet, false, k % 3});
		}
		if (sheetR >= 12.0 || fjords == 0)
			L.nunataks.push_back({toMap({0, 0}), std::clamp(sheetR * 0.22, 3.0, 5.0), true, 0});
		for (const Nunatak &nk : L.nunataks)
		{
			const ShapePoint c = toShape(nk.at);
			const int reach = int(std::ceil(nk.radius * maxStretch)) + 1;
			const int ax = int(N::lround(nk.at.x)), ay = int(N::lround(nk.at.y));
			for (int dy = -reach; dy <= reach; ++dy)
				for (int dx = -reach; dx <= reach; ++dx)
				{
					const ShapePoint q = toShape({double(ax + dx), double(ay + dy)});
					if (N::hypot(q.x - c.x, q.y - c.y) <= nk.radius)
					{
						terrain[t.at(ax + dx, ay + dy)] = GRASS;
						nunatakVertex[t.at(ax + dx, ay + dy)] = 1;
					}
				}
		}
	}
	context.telemetry.measure("fjord-continent.nunataks.placed", L.nunataks.size());

	// Homes: kHomeInset in from the coast on each colony's axis, stepping inward past water.
	L.home.resize(L.teams);
	L.facing.resize(L.teams);
	for (int c = 0; c < L.teams; ++c)
	{
		const double coastR = coastAt(theta[c]);
		double r = std::max(sheetR * 1.15 + kRimWidth + 8.0, coastR - kHomeInset);
		int retreat = 0;
		for (; retreat < 30; ++retreat, r -= 1.0)
		{
			const ShapePoint m = toMap(polar(r, theta[c]));
			const int x = int(N::lround(m.x)), y = int(N::lround(m.y));
			if (terrain[t.at(x, y)] == GRASS && terrain[t.at(x + 1, y)] == GRASS && terrain[t.at(x, y + 1)] == GRASS &&
				terrain[t.at(x + 1, y + 1)] == GRASS)
				break;
		}
		const ShapePoint m = toMap(polar(r, theta[c]));
		const ShapePoint out = toMap(polar(r + 1, theta[c]));
		L.home[c] = m;
		L.facing[c] = N::atan2(out.y - m.y, out.x - m.x);
		context.telemetry.measure("fjord-continent.homes.retreat", retreat, c);
	}

	// Landings: the same number on both banks of every fjord, at the same progress down it. Each is
	// the shortest way through the wall from the bank's own ground to the fjord near its anchor: the
	// wall vertices with the least steps from that ground plus steps to the fjord, within
	// kLandingHalf tiles of the anchor along the fjord. A bank with no ground of its own at the
	// anchor (a bend can leave the other bank both sides) slides its landing along the fjord to the
	// nearest place it has. On a frozen reach a landing steps down onto the ice; with nothing
	// frozen it is a beach.
	std::vector<unsigned char> landingVertex(n, 0);
	{
		// Each colony's own ground: the tiles a unit walks from its home without touching ice (the
		// rule the peninsulas are checked by), and every corner of them, as vertices.
		std::vector<int> groundOf(n, -1), groundTileOf(n, -1);
		{
			const std::vector<unsigned char> walk = walkableTiles(terrain, t);
			std::vector<unsigned char> openTile(n, 0);
			for (int i = 0; i < n; ++i)
			{
				const int x = t.remainderX(i), y = i / t.w;
				openTile[i] = walk[i] && terrain[i] != ICE && terrain[t.at(x + 1, y)] != ICE &&
							  terrain[t.at(x, y + 1)] != ICE && terrain[t.at(x + 1, y + 1)] != ICE;
			}
			for (int c = 0; c < L.teams; ++c)
			{
				std::vector<unsigned char> source(n, 0);
				source[t.at(int(N::lround(L.home[c].x)), int(N::lround(L.home[c].y)))] = 1;
				const std::vector<int> steps = stepsFrom(t, source, openTile);
				for (int i = 0; i < n; ++i)
					if (steps[i] >= 0)
					{
						if (groundTileOf[i] < 0)
							groundTileOf[i] = c;
						const int x = t.remainderX(i), y = i / t.w;
						for (int v : {i, t.at(x + 1, y), t.at(x, y + 1), t.at(x + 1, y + 1)})
							if (groundOf[v] < 0 && terrain[v] == GRASS)
								groundOf[v] = c;
					}
			}
		}
		// Whether colony c's units walk from its home, off the ice, to a tile beside fjord k.
		const auto walksOnto = [&](int c, int k, const std::vector<int> &through)
		{
			std::vector<unsigned char> mine(n, 0);
			for (int v : through)
				mine[v] = 1;
			const std::vector<unsigned char> walk = walkableTiles(terrain, t);
			std::vector<unsigned char> openTile(n, 0), fjordTile(n, 0);
			for (int i = 0; i < n; ++i)
			{
				const int x = t.remainderX(i), y = i / t.w;
				const int corners[4] = {i, t.at(x + 1, y), t.at(x, y + 1), t.at(x + 1, y + 1)};
				bool ice = false, fjord = false;
				for (int v : corners)
				{
					ice = ice || terrain[v] == ICE;
					fjord = fjord || (fjordVertex[v] && fjordOf[v] == k);
				}
				openTile[i] = walk[i] && !ice;
				fjordTile[i] = fjord;
			}
			std::vector<unsigned char> source(n, 0);
			source[t.at(int(N::lround(L.home[c].x)), int(N::lround(L.home[c].y)))] = 1;
			const std::vector<int> steps = stepsFrom(t, source, openTile);
			for (int i = 0; i < n; ++i)
			{
				if (steps[i] < 0)
					continue;
				const int x = t.remainderX(i), y = i / t.w;
				bool fjord = false, near = mine[i] != 0;
				for (int dy = -1; dy <= 1; ++dy)
					for (int dx = -1; dx <= 1; ++dx)
					{
						fjord = fjord || fjordTile[t.at(x + dx, y + dy)];
						near = near || mine[t.at(x + dx, y + dy)];
					}
				if (fjord && near)
					return true;
			}
			return false;
		};
		std::vector<unsigned char> wallOfFjord(n, 0), own(n, 0), water(n, 0), taken(n, 0);
		for (int k = 0; k < fjords; ++k)
		{
			for (int i = 0; i < n; ++i)
				wallOfFjord[i] = terrain[i] == CLIFFS;
			const double from = o.iceReach > 0 ? std::max(iceFrom[k] + 0.04, kLandingFloor) : kLandingFloor + 0.1;
			const int samples = int(L.line[k].size());
			for (int j = 0; j < landings; ++j)
			{
				const double p = from + (landHigh - from) * (j + 0.5) / landings;
				const int s0 = std::clamp(int(N::lround(p * (samples - 1))), 0, samples - 1);
				for (int owner : {L.west[k], L.east[k]})
				{
					Landing landing{owner, k, {}};
					for (int shift = 0; shift <= kLandingSlide && landing.tiles.empty(); ++shift)
						for (int sign : {1, -1})
						{
							const int s = s0 + sign * shift;
							if (s < 0 || s >= samples || !landing.tiles.empty() || (shift == 0 && sign < 0))
								continue;
							const ShapePoint &a = L.line[k][s];
							const ShapePoint &nm = L.normal[k][s];
							const double reach = L.width[k][s] * maxStretch + kWallReach + 4;
							const int box = int(std::ceil(reach)) + 1;
							const int ax = int(N::lround(a.x)), ay = int(N::lround(a.y));
							std::vector<int> window;
							for (int dy = -box; dy <= box; ++dy)
								for (int dx = -box; dx <= box; ++dx)
									window.push_back(t.at(ax + dx, ay + dy));
							bool anyOwn = false;
							for (int v : window)
							{
								own[v] = groundOf[v] == owner;
								water[v] = fjordOf[v] == k;
								anyOwn = anyOwn || own[v];
							}
							if (anyOwn)
							{
								// The shortest 4-connected path of tiles, within kLandingHalf (plus a
								// tile) of the anchor along the fjord, from a tile of the bank's own
								// ground to a tile beside the fjord's shore; then every blocked corner of
								// the path, the shore tile it reaches and the tiles round the path is
								// opened, so the passage walks by construction.
								const int side = 2 * box + 1;
								const auto tileAt = [&](int bx, int by) { return t.at(ax - box + bx, ay - box + by); };
								std::vector<signed char> kind(size_t(side) * side, 0); // 1 source, 2 passable, 3 shore
								for (int by = 0; by < side; ++by)
									for (int bx = 0; bx < side; ++bx)
									{
										const int i = tileAt(bx, by), x = t.remainderX(i), y = i / t.w;
										const int corners[4] = {i, t.at(x + 1, y), t.at(x, y + 1), t.at(x + 1, y + 1)};
										bool shore = false, solid = true;
										for (int v : corners)
										{
											shore = shore || (fjordVertex[v] && fjordOf[v] == k);
											const int ty = terrain[v];
											solid = solid && (ty == GRASS || ty == CLIFFS || ty == DIRT || ty == SAND);
										}
										const double along = -(bx - box) * nm.y + (by - box) * nm.x;
										signed char &c = kind[size_t(by) * side + bx];
										if (groundTileOf[i] == owner)
											c = 1;
										else if (shore)
											c = 3;
										else if (solid && std::abs(along) <= kLandingHalf + 1)
											c = 2;
									}
								std::vector<int> parent(size_t(side) * side, -2), queue;
								for (int q = 0; q < side * side; ++q)
									if (kind[q] == 1)
									{
										parent[q] = -1;
										queue.push_back(q);
									}
								int end = -1, shoreTile = -1;
								for (size_t head = 0; head < queue.size() && end < 0; ++head)
								{
									const int q = queue[head], bx = q % side, by = q / side;
									for (const auto &st : kCardinalSteps)
									{
										const int nx = bx + st[0], ny = by + st[1];
										if (nx < 0 || ny < 0 || nx >= side || ny >= side)
											continue;
										const int r = ny * side + nx;
										if (kind[r] == 3 && kind[q] != 1)
										{
											end = q;
											shoreTile = r;
											break;
										}
										if (kind[r] == 2 && parent[r] == -2)
										{
											parent[r] = q;
											queue.push_back(r);
										}
									}
								}
								if (end >= 0)
								{
									std::vector<int> path{shoreTile};
									for (int q = end; q >= 0 && kind[q] != 1; q = parent[q])
										path.push_back(q);
									std::vector<int> open = path;
									for (int q : path)
										for (int dy = -1; dy <= 1; ++dy)
											for (int dx = -1; dx <= 1; ++dx)
											{
												const int nx = q % side + dx, ny = q / side + dy;
												if (nx >= 0 && ny >= 0 && nx < side && ny < side &&
													kind[size_t(ny) * side + nx] == 2)
													open.push_back(ny * side + nx);
											}
									for (int q : open)
									{
										const int i = tileAt(q % side, q / side), x = t.remainderX(i), y = i / t.w;
										for (int v : {i, t.at(x + 1, y), t.at(x, y + 1), t.at(x + 1, y + 1)})
											if (terrain[v] == CLIFFS && !taken[v])
											{
												taken[v] = 1;
												landing.tiles.push_back(v);
											}
									}
									for (int v : landing.tiles)
										taken[v] = 0;
								}
							}
							for (int v : window)
								own[v] = water[v] = 0;
							// Keep the cut only if a unit really walks through it: from the home, over
							// tiles without ice, to a tile beside the fjord.
							if (!landing.tiles.empty())
							{
								for (int v : landing.tiles)
									terrain[v] = GRASS;
								if (!walksOnto(owner, k, landing.tiles))
								{
									for (int v : landing.tiles)
										terrain[v] = CLIFFS;
									landing.tiles.clear();
								}
							}
							if (!landing.tiles.empty())
								context.telemetry.measure("fjord-continent.landings.slide", sign * shift,
														  int(L.landings.size()));
						}
					if (landing.tiles.empty())
					{
						L.failure = "Colony " + std::to_string(owner) + " has no ground of its own on fjord " +
									std::to_string(k) + " for a landing.";
						return L;
					}
					for (int v : landing.tiles)
					{
						terrain[v] = GRASS;
						landingVertex[v] = 1;
					}
					L.landings.push_back(std::move(landing));
				}
			}
		}
	}


	// Where the quarries need grass at a wall's foot: every fjord bank near its mouth (generate plants
	// the faces there, trying along the wall), and a lone colony, with no fjords, either side of its
	// gap. The ground keeps a small verge of grass round each.
	L.quarries.assign(L.teams, {});
	for (int k = 0; k < fjords; ++k)
	{
		const int samples = int(L.line[k].size());
		const int s = std::clamp(int(N::lround(kQuarryVerge * (samples - 1))), 0, samples - 1);
		for (int side : {-1, 1})
		{
			// Square maps keep the verge as drawn; a stretched map's verge stands just past the wall
			// across the fjord as it is drawn there, not the longest stretch out (far off the bank).
			const double w = fill.sx == fill.sy ? (L.width[k][s] + kWallReach + 1.0) * fill.longest()
												: L.width[k][s] * acrossScale(fill, L.normal[k][s]) + kWallReach + 1.0;
			L.quarries[side < 0 ? L.west[k] : L.east[k]].push_back(
				{int(N::lround(L.line[k][s].x + L.normal[k][s].x * side * w)),
				 int(N::lround(L.line[k][s].y + L.normal[k][s].y * side * w)), 4});
		}
	}
	for (int c = 0; c < L.teams && fjords == 0; ++c)
		for (int side : {-1, 1})
		{
			const double r = sheetR * 1.15 + kRimWidth + 1.5;
			const ShapePoint at = toMap(polar(r, theta[c] + side * (kGapHalf + 4.0) / r));
			L.quarries[c].push_back({int(N::lround(at.x)), int(N::lround(at.y)), 10});
		}

	// The ground: grass only on the outer ring, a ragged meadow band along the sea where wheat and
	// wood regrow; everything inland is tundra (barren: it builds, but nothing grows), colder the
	// further from the sea, with drifts of deep snow (ice: slow, cold, unbuildable) growing more
	// common towards the sheet, away from every home and door. Each home keeps a meadow for its kit,
	// a lone colony's rim quarries a verge of grass, and the nunataks stay grass for their prizes.
	std::vector<int> fromSea;
	{
		std::vector<unsigned char> water(n, 0), wall(n, 0), door(n, 0);
		for (int i = 0; i < n; ++i)
		{
			water[i] = terrain[i] == WATER && !fjordVertex[i];
			wall[i] = terrain[i] == CLIFFS || terrain[i] == RIDGE_ROCK;
			door[i] = landingVertex[i] || gapVertex[i];
		}
		fromSea = stepsFrom(t, water);
		const std::vector<int> &fromWater = fromSea;
		const std::vector<int> fromWall = stepsFrom(t, wall), fromDoor = stepsFrom(t, door);
		const std::vector<int> fray = periodicNoise(W, H, 12, context.stream("tundra"));
		const std::vector<int> drift = fractalNoise(W, H, 24, 2, context.stream("drifts"));
		const auto near = [&](int i, int x, int y, double reach)
		{
			return N::hypot(double(t.offsetX(x, t.remainderX(i))), double(t.offsetY(y, i / t.w))) <= reach;
		};
		for (int i = 0; i < n; ++i)
		{
			if (terrain[i] != GRASS || nunatakVertex[i])
				continue;
			const double meadow = kMeadowDepth + kMeadowFray * fray[i] / 65535.0;
			if (fromWater[i] >= 0 && fromWater[i] <= meadow)
				continue;
			bool kept = false;
			for (int c = 0; c < L.teams && !kept; ++c)
				kept = near(i, int(N::lround(L.home[c].x)), int(N::lround(L.home[c].y)), kHomeMeadow);
			for (int c = 0; c < L.teams && !kept; ++c)
				for (const KitSeed &q : L.quarries[c])
					kept = kept || (near(i, q.x, q.y, q.within + 2.0) && fromWall[i] >= 0 && fromWall[i] <= 3);
			if (kept)
				continue;
			bool homeNear = false;
			for (int c = 0; c < L.teams && !homeNear; ++c)
				homeNear = near(i, int(N::lround(L.home[c].x)), int(N::lround(L.home[c].y)), kHomeMeadow + 6);
			// The drift threshold falls the further inland, so the snow thickens towards the sheet.
			const int inland = fromWater[i] < 0 ? 1000 : fromWater[i] - int(kMeadowDepth + kMeadowFray) - 4;
			const int cut = std::max(kDriftCutInland, kDriftCut - kDriftCutPerStep * inland);
			// Never on a colony's spine, the line from its home to its gap: drifts must not part a
			// peninsula's dry ground from its own door onto the sheet.
			bool spine = false;
			for (int c = 0; c < L.teams && !spine; ++c)
				spine = std::abs(wrapAngle(ang[i] - theta[c])) * rad[i] < kSpineHalf;
			const bool drifts = drift[i] > cut && inland > 0 && !spine &&
								(fromWall[i] < 0 || fromWall[i] > 3) && (fromDoor[i] < 0 || fromDoor[i] > 5) &&
								!homeNear;
			terrain[i] = drifts ? ICE : DIRT;
			if (drifts)
				driftVertex[i] = 1;
		}
	}

	// Islands out at sea, before the beaches, each kept 8 tiles of water from any coast.
	{
		const int wanted = o.resourceIslands + int(context.bounded("layout", 2));
		L.islands = raiseIslands(terrain, t, context, {"outliers", wanted, 60, 8.0});
		context.telemetry.measure("fjord-continent.outliers.requested", wanted);
		context.telemetry.measure("fjord-continent.outliers.actual", L.islands.size());
	}

	// Beaches where grass or tundra meets open water; the walls and the ice run straight into it.
	{
		const TerrainSketch original(terrain);
		for (int i = 0; i < n; ++i)
		{
			if (original[i] != GRASS && original[i] != DIRT)
				continue;
			const int x = t.remainderX(i), y = i / t.w;
			bool shore = false;
			for (int dy = -1; dy <= 1 && !shore; ++dy)
				for (int dx = -1; dx <= 1 && !shore; ++dx)
					shore = original[t.at(x + dx, y + dy)] == WATER;
			if (shore)
				terrain[i] = SAND;
		}
	}
	// The open sea's deep core: slower swimming, no algae, shallows kept along every shore.
	{
		std::vector<unsigned char> water(n, 0);
		for (int i = 0; i < n; ++i)
			water[i] = terrain[i] == WATER;
		const std::vector<unsigned char> deep = deepCore(t, water, 4);
		for (int i = 0; i < n; ++i)
			if (deep[i] && !fjordVertex[i])
				terrain[i] = DEEP_WATER;
	}

	// The peninsulas: each home's ground, flooded over the tiles a unit walks without touching ice.
	// Two homes on one piece of ground would put a dry route between them, which the design never
	// does.
	{
		const std::vector<unsigned char> walk = walkableTiles(terrain, t);
		std::vector<unsigned char> open(n, 0);
		for (int i = 0; i < n; ++i)
		{
			const int x = t.remainderX(i), y = i / t.w;
			// Fjord and sheet ice part peninsulas; a drift lies on one, so its ice counts as its ground.
			const auto parts = [&](int v) { return terrain[v] == ICE && !driftVertex[v]; };
			open[i] = walk[i] && !parts(i) && !parts(t.at(x + 1, y)) && !parts(t.at(x, y + 1)) &&
					  !parts(t.at(x + 1, y + 1));
		}
		L.homeOf.assign(n, -1);
		for (int c = 0; c < L.teams; ++c)
		{
			const int start = t.at(int(N::lround(L.home[c].x)), int(N::lround(L.home[c].y)));
			if (!open[start])
			{
				L.failure = "Colony " + std::to_string(c) + "'s home is not on open ground.";
				return L;
			}
			if (L.homeOf[start] >= 0)
			{
				L.failure = "Colonies " + std::to_string(L.homeOf[start]) + " and " + std::to_string(c) +
							" share a peninsula.";
				return L;
			}
			std::vector<unsigned char> source(n, 0);
			source[start] = 1;
			const std::vector<int> steps = stepsFrom(t, source, open);
			for (int i = 0; i < n; ++i)
				if (steps[i] >= 0)
				{
					if (L.homeOf[i] >= 0 && L.homeOf[i] != c)
					{
						L.failure = "Colonies " + std::to_string(L.homeOf[i]) + " and " + std::to_string(c) +
									" share a peninsula.";
						return L;
					}
					L.homeOf[i] = c;
				}
		}
	}

	// What must stay open: every landing and gap tile, and two tiles round them.
	{
		std::vector<unsigned char> door(n, 0);
		for (int i = 0; i < n; ++i)
			door[i] = landingVertex[i] || gapVertex[i];
		const std::vector<int> fromDoor = stepsFrom(t, door);
		L.keepOpen.assign(n, 0);
		for (int i = 0; i < n; ++i)
			L.keepOpen[i] = fromDoor[i] >= 0 && fromDoor[i] <= 2;
	}

	// The looks, last: the polar climate over the rules, then the ice.
	L.looks = terrain;
	LookRoles roles;
	paintClimate(L.looks, t, kClimateLooks[kPolarClimate], roles);
	{
		const std::vector<int> snow = fractalNoise(W, H, 32, 3, context.stream("snow"));
		for (int i = 0; i < n; ++i)
		{
			switch (terrain[i])
			{
			case ICE:
				++L.ice;
				if (driftVertex[i])
					L.looks[i] = DEEP_SNOW;
				else if (!sheetVertex[i])
					L.looks[i] = FROZEN_LAKE;
				else
					L.looks[i] = snow[i] < 20000 ? GLACIER_ICE : snow[i] < 44000 ? WIND_SWEPT_SNOW : DEEP_SNOW;
				break;
			case CLIFFS:
				++L.glacier;
				if (wallProgress[i] > 0.7)
					L.looks[i] = ICEFALL;
				break;
			case RIDGE_ROCK:
				++L.penitentes;
				break;
			case DIRT:
				// Warmer tundra just inland of the meadows, arctic tundra beyond, with a ragged edge.
				++L.tundra;
				if (fromSea[i] >= 0 && fromSea[i] < kMeadowDepth + kMeadowFray + kTundraBand + (snow[i] >> 13))
					L.looks[i] = TUNDRA;
				break;
			case DEEP_WATER:
				L.looks[i] = kClimateLooks[kPolarClimate].deep;
				break;
			default:
				break;
			}
		}
	}
	return L;
}

Layout design(const GenerationRequest &request, GenerationContext &context)
{
	return cachedDesign(request, context, designAfresh);
}

// The scaled extras along every bank, as progress within the open and landing stretches; the
// guaranteed wheat and wood near the mouth.
constexpr double kBankScatter[] = {0.10, 0.22, 0.48, 0.58, 0.80, 0.92};
constexpr double kBankWheat = 0.04, kBankWood = 0.1, kBankExtrasReach = 0.22;

// A bank seed: `along` progress down fjord k, `out` tiles past its wall on `side`.
KitSeed bankSeed(const Layout &L, int k, double progress, int side, double out, int within)
{
	const int samples = int(L.line[k].size());
	const int s = std::clamp(int(N::lround(progress * (samples - 1))), 0, samples - 1);
	const double w = L.width[k][s] * acrossScale(L.fill, L.normal[k][s]) + kWallReach + out;
	return {int(N::lround(L.line[k][s].x + L.normal[k][s].x * side * w)),
			int(N::lround(L.line[k][s].y + L.normal[k][s].y * side * w)), within};
}

bool generate(GenerationWorld &game, GenerationContext &context)
{
	context.stage = "fjord design";
	const FjordContinentOptions o(context.request);
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
	writeVertices(map, L.mechanics);
	context.telemetry.measure("fjord-continent.terrain.ice-vertices", L.ice);
	context.telemetry.measure("fjord-continent.terrain.glacier-vertices", L.glacier);
	context.telemetry.measure("fjord-continent.terrain.penitente-vertices", L.penitentes);
	context.telemetry.measure("fjord-continent.terrain.tundra-vertices", L.tundra);
	context.telemetry.measure("fjord-continent.landings.per-bank", o.landings);

	context.stage = "fjord colonies";
	const auto homeMask = [&](int team) { return homeGrassMask(map, t, L.homeOf, team); };
	const auto anchor = [&](int team)
	{ return MapGeneratorPoint(t.x(int(N::lround(L.home[team].x)) - 2), t.y(int(N::lround(L.home[team].y)) - 2)); };
	if (!settleColonies(game, context, "starts", homeMask, anchor))
		return false;

	context.stage = "fjord resources";
	// Ground a deposit of `type` may take: the engine's own test (pure grass for crops and stone),
	// off the landings and gaps.
	const auto open = [&](int i) { return !L.keepOpen[i]; };
	const auto takes = [&](int i, int type)
	{ return open(i) && map.isResourceAllowed(t.remainderX(i), i / t.w, type); };
	// The walls' stone, first, so the kits and bank crops cannot take its grass. Each fjord bank's
	// face is tried by the mouth first, then a little further in or out along the wall, until one
	// holds most of a face; a lone colony quarries the rim either side of its gap.
	std::vector<int> quarryStone(teams, 0);
	for (int k = 0; k < int(L.line.size()); ++k)
		for (int side : {-1, 1})
		{
			const int owner = side < 0 ? L.west[k] : L.east[k];
			int placed = 0;
			for (double progress : kQuarryTries)
			{
				placed += plantQuarryFaces(map, t, {bankSeed(L, k, progress, side, 1, 10)}, kQuarryTiles - placed,
										   [&](int i) { return L.homeOf[i] == owner && takes(i, STONE); });
				if (placed >= kQuarryTiles - 1)
					break;
			}
			quarryStone[owner] += placed;
		}
	for (int c = 0; c < teams; ++c)
	{
		if (L.line.empty())
			quarryStone[c] = plantQuarryFaces(map, t, L.quarries[c], kQuarryTiles,
											  [&](int i) { return L.homeOf[i] == c && takes(i, STONE); });
		// A small map's mouths can leave a colony short: one more face at the wall foot nearest its
		// home, anywhere on its own meadows. A stretched map stretches the peninsula too, so the search
		// reaches as much further as the map is stretched.
		if (quarryStone[c] < kQuarryTiles)
			quarryStone[c] += plantQuarryFaces(
				map, t,
				{KitSeed{int(N::lround(L.home[c].x)), int(N::lround(L.home[c].y)),
						 int(N::lround(kQuarrySearch * L.fill.longest()))}},
				kQuarryTiles, [&](int i) { return L.homeOf[i] == c && takes(i, STONE); });
		context.telemetry.measure("fjord-continent.quarry.stone", quarryStone[c], c);
	}
	// The faces as laid: the ambient scatter and the start repairs below overwrite deposits, so the
	// faces are kept from the repairs and laid again at the end wherever a later layer took them.
	std::vector<unsigned char> quarry(n, 0);
	for (int i = 0; i < n; ++i)
		quarry[i] = map.getResource(i).type == STONE && L.homeOf[i] >= 0;
	// Starter kits, laid out the same way round every home: wheat and wood towards the sea, a stone
	// clump behind.
	for (int c = 0; c < teams; ++c)
	{
		const KitFrame frame{int(N::lround(L.home[c].x)), int(N::lround(L.home[c].y)), L.facing[c]};
		const Kit kit{frame.at(5, 4, 8), frame.at(5, -4, 8), frame.at(-5, 0, 8), 8, 6, 1};
		plantKit(map, t, context, kit, [&](int i) { return L.homeOf[i] == c && takes(i, WHEAT); });
	}
	// Every bank's guaranteed wheat and wood near the mouth, on the bank's own peninsula.
	for (int k = 0; k < int(L.line.size()); ++k)
		for (int side : {-1, 1})
		{
			const int owner = side < 0 ? L.west[k] : L.east[k];
			// Each patch tries its own place first, then a little further in or out along the bank.
			const auto bankPatch = [&](double progress, int type)
			{
				for (double shift : {0.0, -0.06, 0.06, -0.12, 0.12, 0.18, 0.24, 0.30})
					if (const int placed = plantPatchNear(
							map, t, bankSeed(L, k, std::max(0.0, progress + shift), side, 3, 16), type, 6,
							[&](int i) { return L.homeOf[i] == owner && takes(i, type); });
						placed > 0)
						return placed;
				// A crowded mouth (a small map): the bank's own meadow nearest the mouth, further out. On
				// a stretched map a fjord meets the coast at a slant and one bank is a thin wedge of wall
				// at its mouth, so the search reaches as much further as the map is stretched.
				return plantPatchNear(map, t,
									  bankSeed(L, k, 0.0, side, 3, int(N::lround(28 * L.fill.longest()))), type, 6,
									  [&](int i) { return L.homeOf[i] == owner && takes(i, type); });
			};
			const int wheat = bankPatch(kBankWheat, WHEAT);
			const int wood = bankPatch(kBankWood, WOOD);
			if (wheat == 0 || wood == 0)
			{
				context.detail = "fjord " + std::to_string(k) + " has no room for its bank crops";
				return false;
			}
		}
	// The nunataks' prizes, and the map's only fruit: every orchard stands out on the ice, so a
	// colony that wants fruit sends its workers across ground that slows and chills them. Between
	// every pair of neighbours a grove of one fruit (the fruits taken in turn) and a little stone;
	// in the middle, groves of all three. The fruit amount scales the groves.
	for (const Nunatak &nk : L.nunataks)
	{
		const MapGeneratorPoint at(t.x(int(N::lround(nk.at.x))), t.y(int(N::lround(nk.at.y))));
		std::vector<int> ground;
		const int reach = int(std::ceil(nk.radius * L.stretch)) + 2;
		for (int dy = -reach; dy <= reach; ++dy)
			for (int dx = -reach; dx <= reach; ++dx)
			{
				const int i = t.at(at.x + dx, at.y + dy);
				if (map.isResourceAllowed(t.remainderX(i), i / t.w, CHERRY))
					ground.push_back(i);
			}
		if (ground.empty())
			continue;
		placeResourceClump(map, context, at, STONE, 1);
		const auto grove = [&](int fruit, int clumps)
		{
			scatterClumps(context, t, ground, int(scaledCount(clumps, o.fruit)), "nunatak-fruit",
						  [&](int i) { return map.isResourceAllowed(t.remainderX(i), i / t.w, fruit); },
						  [&](MapGeneratorPoint p) { placeResourceClump(map, context, p, fruit, 1); });
		};
		if (nk.centre)
			for (int f = 0; f < 3; ++f)
				grove(CHERRY + f, kCentreGroves);
		else
			grove(CHERRY + nk.fruit, kNunatakGroves);
	}
	stockIslands(map, context, L.islands, "outliers");
	// Algae along the open shallows of the sea and the fjord mouths, where it can regrow.
	seedAlgae(map, context, t, "algae", o.algae, AlgaeBand::shallows(1, 14, kAlgaeTilesPerClump).thriving(0.6));
	// The ambient scatter, wheat:wood 2:1 (densities per 1600 tiles; stone per 3000). No fruit: it
	// grows only on the nunataks.
	// The scatter's densities are per tile of the whole map, sized for a map of grass; here crops
	// stand only on the coastal meadows (and nunataks and islands), so they are scaled by the share
	// of the map that is crop ground over kGrassShare, or the meadows fill solid and leave a colony
	// no free ground beside its fields for an inn.
	{
		int cropGround = 0;
		for (int i = 0; i < n; ++i)
			cropGround += map.isResourceAllowed(t.remainderX(i), i / t.w, WHEAT);
		const double share = std::min(1.0, double(cropGround) / n / kGrassShare);
		context.telemetry.measure("fjord-continent.scatter.meadow-scale", share);
		const auto density = [&](int base, int percent)
		{ return int(N::lround(scaledCount(base, percent) * share)); };
		scatterResources(game, context,
						 {/*wheat=*/density(24, o.wheat), /*wood=*/density(12, o.wood),
						  /*stone=*/density(10, o.stone), /*algae=*/0, /*fruit=*/0});
	}
	// The scaled extras along every bank: wheat:wood:stone 4:2:2.
	if (o.bankDeposits)
		for (int k = 0; k < int(L.line.size()); ++k)
			for (double progress : kBankScatter)
			{
				const double jitter = (int(context.bounded("resources", 41)) - 20) / 1000.0;
				// Extras stay by the mouth too: the banks further in are tundra.
				const double along = progress * kBankExtrasReach + jitter;
				for (int side : {-1, 1})
				{
					const int roll = int(context.bounded("resources", 8));
					const int type = roll < 4 ? WHEAT : roll < 6 ? WOOD : STONE;
					const int percent = type == WHEAT ? o.wheat : type == WOOD ? o.wood : o.stone;
					int copies = percent / 100;
					if (percent % 100 != 0 && int(context.bounded("fjord-bank-amounts", 100)) < percent % 100)
						++copies;
					const int owner = side < 0 ? L.west[k] : L.east[k];
					for (int copy = 0; copy < copies; ++copy)
						plantPatchNear(map, t, bankSeed(L, k, along, side, 3 + 3 * copy, 6), type, 4,
									   [&](int i) { return L.homeOf[i] == owner && takes(i, type); });
				}
			}

	// Nothing may block a landing or a gap: they are the only doors onto the ice.
	clearDeposits(map, t, L.keepOpen);
	std::vector<unsigned char> topup(n, 0);
	for (int i = 0; i < n; ++i)
		topup[i] = !L.keepOpen[i];
	secureStartingCrops(game, context, t, 24, 32, 0, &quarry, &topup);
	openStartsBuriedByResources(game, context, 24, 32, 0, &quarry);
	clearDeposits(map, t, L.keepOpen);
	for (int i = 0; i < n; ++i)
		if (quarry[i] && !L.keepOpen[i] && map.getResource(i).type != STONE)
			map.setResourceByIndex(t.remainderX(i), i / t.w, STONE, 1);
	// Every colony keeps a walk from its workers to each of its own doors, every landing and its
	// gap, clearing only the deposits in the way and going round the quarry faces.
	{
		const std::vector<std::vector<int>> workers = unitTilesByTeam(map, teams);
		// The walk stays off the ice: round over the sheet is no way to the door.
		std::vector<unsigned char> blocked = quarry;
		for (int i = 0; i < n; ++i)
			blocked[i] = blocked[i] || map.terrainPropertiesAt(t.remainderX(i), i / t.w).groundHealthQ8 < 0;
		const auto keepWalk = [&](int c, const std::vector<int> &door)
		{
			std::vector<unsigned char> goal(n, 0);
			for (int i : doorRegion(t, L.homeOf, door, c))
				goal[i] = !blocked[i];
			return openRoad(map, t, workers[c], goal, &blocked);
		};
		for (const Landing &landing : L.landings)
			if (!keepWalk(landing.owner, landing.tiles))
			{
				context.detail = "colony " + std::to_string(landing.owner) + " cannot walk to its landing on fjord " +
								 std::to_string(landing.fjord);
				return false;
			}
		for (int c = 0; c < teams; ++c)
			if (!keepWalk(c, L.gaps[c]))
			{
				context.detail = "colony " + std::to_string(c) + " cannot walk to its gap in the rim";
				return false;
			}
	}

	context.stage = "fjord looks";
	writeLayeredTerrain(map, L.mechanics, L.looks);
	return true;
}

// The supported envelope: a peninsula needs room for a home, its meadows, two glacier walls and their
// landings, so the map's shorter side must be at least kShortestSide and give every colony
// kSidePerColony tiles of it.
constexpr int kShortestSide = 128, kSidePerColony = 32;
std::string validateRequest(const GenerationRequest &r)
{
	// Every message is a translation key, so none carries a number.
	const int shorter = std::min(1 << r.wDec, 1 << r.hDec);
	if (r.nbTeams < 1)
		return "Fjord continents need at least one colony.";
	static_assert(kShortestSide == 128, "the refusal names 128 tiles");
	if (shorter < kShortestSide)
		return "A fjord continent needs a map at least 128 tiles on its shorter side.";
	if (r.nbTeams > shorter / kSidePerColony)
		return "Too many colonies for this map; use a bigger map or fewer colonies.";
	return "";
}

// Ground a unit stands on that wears it down: a tile with an ice corner.
std::vector<unsigned char> coldTiles(const GenerationMap &map, const Torus &t)
{
	std::vector<unsigned char> cold(t.size(), 0);
	for (int i = 0; i < t.size(); ++i)
		cold[i] = map.terrainPropertiesAt(t.remainderX(i), i / t.w).groundHealthQ8 < 0;
	return cold;
}

// Checked on the finished world: the terrain is the design's, vertex for vertex; every colony has
// its floor of crops and room; colony 0 walks to every other colony over the ice; no colony walks
// to another without crossing ice; every landing opens onto the fjord from its own peninsula; and
// every colony's walls carry its quarry stone.
std::string validateWorld(const GenerationWorld &game, const GenerationContext &context)
{
	GenerationContext replay(context.request);
	const Layout L = design(context.request, replay);
	const GenerationMap &map = game.map;
	if (const std::string mismatch = designMismatch(L, map, "fjord continent"); !mismatch.empty())
		return mismatch;
	const Torus &t = L.t;
	const int n = t.size(), teams = context.request.nbTeams;
	const auto at = [&](int i)
	{ return "(" + std::to_string(t.remainderX(i)) + ", " + std::to_string(i / t.w) + ")"; };
	{
		const TerrainSketch terrain = composeLooks(L.mechanics, L.looks);
		for (int i = 0; i < n; ++i)
			if (map.vertexTerrainAt(t.remainderX(i), i / t.w) != terrain[i])
				return "The continent at " + at(i) + " is not as designed.";
	}
	if (std::string floor = startingFloorFailure(map, teams); !floor.empty())
		return floor;
	const ColonyWalk walk = walkFromFirstColony(map, teams, "the ice", "over the ice");
	const std::vector<unsigned char> cold = coldTiles(map, t);
	for (int c = 0; c < teams; ++c)
	{
		const int leak = leakWithShut(map, t, walk.workers[c], cold, false,
									  [&](int i) { return L.homeOf[i] == c; });
		if (leak >= 0)
			return "Colony " + std::to_string(c) + " walks off its peninsula at " + at(leak) +
				   " without crossing ice.";
	}
	for (const Landing &landing : L.landings)
	{
		// The landing opens when the owner's own ground reaches the fjord (ice or water) through it:
		// a tile of its door region beside the fjord, or a walkable cut tile beside both.
		bool opens = false;
		const auto fjordBeside = [&](int i)
		{
			const int x = t.remainderX(i), y = i / t.w;
			for (int dy = -1; dy <= 1; ++dy)
				for (int dx = -1; dx <= 1; ++dx)
				{
					const int j = t.at(x + dx, y + dy);
					if ((cold[j] && L.homeOf[j] < 0) || map.terrainPropertiesAt(t.remainderX(j), j / t.w).swimmable)
						return true;
				}
			return false;
		};
		for (int i : doorRegion(t, L.homeOf, landing.tiles, landing.owner))
			if ((opens = fjordBeside(i)))
				break;
		for (int i : landing.tiles)
		{
			if (opens)
				break;
			if (!map.terrainPropertiesAt(t.remainderX(i), i / t.w).walkable || !fjordBeside(i))
				continue;
			opens = !doorRegion(t, L.homeOf, {i}, landing.owner).empty();
		}
		if (!opens)
			return "Colony " + std::to_string(landing.owner) + "'s landing on fjord " +
				   std::to_string(landing.fjord) + " does not open onto the fjord.";
	}
	// Every landing is reached from its own colony's workers with deposits blocking, off the ice.
	{
		const std::vector<unsigned char> ground = groundUnitTiles(map);
		std::vector<unsigned char> open(n, 0);
		for (int i = 0; i < n; ++i)
			open[i] = ground[i] && !cold[i];
		std::vector<std::vector<int>> reach(teams);
		for (int c = 0; c < teams; ++c)
			reach[c] = stepsFrom(t, tileMask(t, walk.workers[c]), open);
		for (const Landing &landing : L.landings)
		{
			bool reached = false;
			for (int i : doorRegion(t, L.homeOf, landing.tiles, landing.owner))
				reached = reached || reach[landing.owner][i] >= 0;
			if (!reached)
				return "Colony " + std::to_string(landing.owner) + " cannot walk to its landing on fjord " +
					   std::to_string(landing.fjord) + " past the deposits.";
		}
	}
	std::vector<int> stone(teams, 0);
	for (int i = 0; i < n; ++i)
	{
		const int c = L.homeOf[i];
		const int x = t.remainderX(i), y = i / t.w;
		if (c < 0 || map.getResource(x, y).type != STONE)
			continue;
		bool foot = false;
		for (int dy = -1; dy <= 1 && !foot; ++dy)
			for (int dx = -1; dx <= 1 && !foot; ++dx)
			{
				const TerrainProperties &p = map.terrainPropertiesAt(t.x(x + dx), t.y(y + dy));
				foot = !p.walkable && !p.swimmable;
			}
		stone[c] += foot;
	}
	for (int c = 0; c < teams; ++c)
		if (stone[c] < kQuarryLeast)
			return "Colony " + std::to_string(c) + "'s walls carry " + std::to_string(stone[c]) +
				   " stone, fewer than " + std::to_string(kQuarryLeast) + ".";
	return "";
}
} // namespace

FjordContinentOptions::FjordContinentOptions(const GenerationRequest &r)
	: continentSize(r.option("continent-size")), roughness(r.option("coast-roughness")),
	  fjordWidth(r.option("fjord-width")), sheetSize(r.option("lake-size")),
	  iceReach(r.option("ice-reach")), landings(r.option("fjord-landings")),
	  resourceIslands(r.option("resource-islands")), nunataks(r.option("nunataks") != 0),
	  bankDeposits(r.option("bank-deposits") != 0), wheat(r.option("wheat-amount")),
	  wood(r.option("wood-amount")), stone(r.option("stone-amount")),
	  algae(r.option("algae-amount")), fruit(r.option("fruit-amount"))
{
}

GeneratorDefinition fjordContinentDefinition()
{
	return {
		"fjord-continent",
		12,
		"Fjord continent",
		// Revision 16: the frozen continent (2.0). An ice sheet at the heart with a penitente rim,
		// fjords frozen from the sheet outward behind glacier walls with landings, nunataks, the
		// polar climate. Nothing of revision 15's draws survives.
		// Revision 17: on a rectangular map the fjords' sideways swing, their banks' crop and quarry
		// seeds and the fallback quarry search follow the map's stretch, so a fjord no longer runs
		// along the coast and starves one bank. Square maps are unchanged.
		17,
		false,
		{GeneratorControl{"continent-size", "Continent size", 28, 40, 2, 34, ControlGroup::Terrain}
			 .withSearchRange(34, 38),
		 GeneratorControl{"coast-roughness", "Coast roughness", 10, 35, 1, 22, ControlGroup::Terrain}
			 .withSearchRange(15, 28),
		 GeneratorControl{"fjord-width", "Fjord width", 2, 10, 1, 4, ControlGroup::Terrain}
			 .withSearchRange(3, 5),
		 // The ice sheet's radius: from a tenth of the continent's at 0 to under three tenths at 90.
		 // The id is the old lake's, so saved settings keep their meaning of "the middle's size".
		 GeneratorControl{"lake-size", "Ice sheet size", 0, 90, 5, 45, ControlGroup::Terrain}
			 .withSearchRange(30, 60),
		 // How much of every fjord is frozen, from the sheet outward: 0 leaves the fjords open water
		 // (the sheet is then the only way across), 100 freezes them to the mouth's open water.
		 GeneratorControl{"ice-reach", "Ice reach", 0, 100, 5, 60, ControlGroup::Terrain}
			 .withSearchRange(40, 80),
		 // Gaps in each glacier wall onto the fjord, the same on both banks of every fjord.
		 GeneratorControl{"fjord-landings", "Fjord landings", 1, 4, 1, 2, ControlGroup::Layout}
			 .withSearchRange(1, 3),
		 // On, rock islets stand out of the sheet holding stone and fruit: one between every pair
		 // of neighbours, and a richer one in the middle of a big enough sheet.
		 GeneratorControl::toggle("nunataks", "Nunatak orchards", true, ControlGroup::Layout)
			 .withSearchValues({0, 1}),
		 GeneratorControl{"resource-islands", "Resource islands", 0, 20, 1, 2, ControlGroup::Resources}
			 .withSearchRange(1, 5),
		 // Off, each fjord bank keeps only its guaranteed wheat and wood.
		 GeneratorControl::toggle("bank-deposits", "Fjord bank deposits", true, ControlGroup::Resources)
			 .withSearchValues({1}),
		 // The ambient scatter, the open sea's algae and the fjord banks' extra deposits. Starter
		 // kits, each bank's guaranteed wheat and wood, the quarry faces and the nunataks' prizes
		 // stay as they are.
		 GeneratorControl::percentage("wheat-amount", "Wheat amount"),
		 GeneratorControl::percentage("wood-amount", "Wood amount"),
		 GeneratorControl::percentage("stone-amount", "Stone amount"),
		 GeneratorControl::percentage("algae-amount", "Algae amount"),
		 GeneratorControl::percentage("fruit-amount", "Fruit amount")},
		generate,
		true,
		validateRequest,
		validateWorld,
		{"terrain:natural", "feature:ocean", "feature:islands", "feature:snow", "style:sprawling",
		 "fairness:repeated-wedge"}};
}
