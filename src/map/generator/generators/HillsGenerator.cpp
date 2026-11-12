// SPDX-License-Identifier: GPL-3.0-or-later
#include "HillsGenerator.h"
#include "Climates.h"
#include "Drawing.h"
#include "Farmland.h"
#include "GenerationWorld.h"
#include "GenerationContext.h"
#include "Geometry.h"
#include "Grid.h"
#include "HeightFieldLooks.h"
#include "LatticeNoise.h"
#include "Morphology.h"
#include "Orbits.h"
#include "Pipeline.h"
#include "Planting.h"
#include "Points.h"
#include "Rock.h"
#include "Roads.h"
#include "Room.h"
#include "Settlements.h"
#include "Sketch.h"
#include "Walls.h"
#include <algorithm>
#include <cmath>
#include <string>
#include <utility>
#include <vector>
using namespace MapGeneration;

// Hills: each colony farms the concentric contours around its own hill, lobed the way a real
// hill's contours are rather than drawn with compasses (a maintainer's first look at the circles
// saw a centre-pivot farm). Stone stairs climb straight through the fields and irrigation ditches
// and carry on as roads across the valley to the edge of the hill's ground; the summit is a town,
// the valleys are shared ground with a few small wild crops just outside every hill.
// Named Rice terraces until 2026-09-16, when a maintainer found closed rings round point summits
// did not read as rice terraces at all, however lobed; that name went to a map of long terraced
// hillsides (RiceTerracesGenerator.cpp), and this one kept its geometry under the name it looks
// like. The seed streams keep their rice- names so the layouts they draw are unchanged.
//
// HILLS 2.0 (REVISION 9, 2026-11-11). Revision 8 was drawn in grass, sand and water only: sand caps,
// sand stairs and roads, a Voronoi river cut every 24 tiles by sand fords. In the game view it read
// as targets on graph paper, and the stairs were no gates: a swimmer crossed the water bands and
// the sand caps stopped nobody. The 2.0 design keeps the contour rings (maintainer's choice) and
// draws them with the terrain catalogue, in one fixed look:
//  - the summit's cap is a rock crown and the outer cap a rock foot (Ridges, drawn on vertices and
//    thickened to three, so towers shoot over them), open only where a stair passes. Every hill is
//    then sealed against walkers and swimmers alike, and its stairs are its only gates;
//  - stairs, the summit's paved square and the valley roads are one stone paving (Paths), the
//    roads meandering a little; fords over the streams are boardwalk;
//  - scree lies at the foot of every hill (Rough: slow, holds nothing, holds no field back);
//  - the valleys hold streams along the frayed hollows between hills, dry at the saddle between
//    every two neighbours so no stream parts the valley, with meadows of fertile ground along them;
//  - every hill's valley has two rock outcrops with quarry faces, where the commons' stone was.
// These are terrain contours, not engine elevation: towers shoot across them without a height
// bonus. A summit tower covers a stair's final approach, not the whole climb from the valley.
// Harvesting can open crop bands, but nothing grows across the rock or the paving separating the
// town, the stairs and the commons from the farms.
namespace
{
// Radius 16 leaves room for a swarm, upgrade footprints and several ordinary AI buildings.
// Keep it fixed rather than squeezing the town to accommodate an overfull request.
constexpr double kSummit = 16;
constexpr double kCap = 2;
constexpr double kValley = 14;
// A stair is five corners across even on its narrow axis. At arbitrary angles its centre
// retains multiple walkable tiles after four-corner terrain conversion. Towers sit inside
// the summit, to one side of the mouth, never on the stair itself. Where a stair passes
// through a wall, one more vertex either side is paved too, so the wall's leaning corners
// never narrow the gate.
constexpr double kStairHalf = 2.5;
constexpr int kDoorMargin = 1;
// How far inside the summit's edge the crown may thicken.
constexpr double kCrownInset = 2;
// How far the contours may lobe in or out, in tiles. Up to half the valley floor between two
// hills' circular outer caps may go to their lobes; the rest stays open ground.
constexpr double kWobble = 4;
// The valley roads: paving this many corners either side of a stair's line, and this many tiles past
// the edge of the hill's ground, across the stream that runs along that edge. Beyond the foot a road
// meanders up to kRoadMeander tiles either side of its line, coming in over kRoadSettle tiles.
constexpr double kRoadHalf = 1.5, kRoadOverrun = 4, kRoadMeander = 3, kRoadSettle = 12;
// Wild crops just outside every hill (maintainer review 2026-09-16: "a couple small seeds of wheat
// and wood on the outside of each of the rings"): a patch of this many tiles between every two
// stairs, this far beyond the outer cap's furthest lobe (past the scree), alternating wheat and wood.
constexpr int kWildPatch = 4;
constexpr double kWildOut = 7;
// The streams: along the boundary between two hills' ground, sampled through a warp of up to kWarp
// tiles (noise cells kWarpCell across) so they wind; each half kStreamHalf to kStreamHalf +
// kStreamHalfSpan tiles wide; never within kStreamClear tiles of a hill's foot, and dry within
// kFordRadius of the saddle between every two neighbouring hills, so walkers always cross dry-shod
// there. The warp fades to nothing over kWarpFade tiles towards a saddle, so the stream meets its
// dry crossing where the design put it.
constexpr double kWarp = 12, kWarpCell = 48, kStreamHalf = 1.0, kStreamHalfSpan = 1.5,
				 kStreamClear = 6, kFordRadius = 8, kWarpFade = 10;
// The fewest vertices a stream keeps; anything less is a puddle and stays dry.
constexpr int kStreamLeast = 120;
// Meadows of fertile ground kMeadow to kMeadow + kMeadowSpan tiles either side of a stream, kept
// kMeadowFoot tiles off a hill's rock. Fertile ground pays only in bands this wide (terrain palette).
constexpr double kMeadow = 4, kMeadowSpan = 4, kMeadowFoot = 4;
// The meadows' own crops (maintainer review 2026-11-11: "place resources along all of those fertile
// terrain sections on the boundary rivers, like wheat and wood"): at the default amounts this share
// of every meadow's tiles holds wheat and this share wood, in patches (noise cells kMeadowPatch
// tiles across) and in stretches of one crop or the other (cells kMeadowStretch across).
constexpr int kMeadowWheat = 35, kMeadowWood = 15;
constexpr int kMeadowPatch = 8, kMeadowStretch = 20;
// Scree at a hill's foot: kTalus to kTalus + kTalusSpan tiles out from the rock.
constexpr double kTalus = 1, kTalusSpan = 2;
// kOutcrops rock outcrops in every hill's valley, at its most open ground and at least
// kOutcropApart tiles from each other: knots kOutcropRadius tiles round (lobed by kOutcropLobe),
// only where open grass reaches kOutcropRoom tiles round, each with a quarry face kOutcropFace
// tiles across on the side facing its hill. Two, because one face per hill held only half the stone
// revision 8 scattered over the commons.
constexpr int kOutcrops = 2;
constexpr double kOutcropRadius = 3.6, kOutcropLobe = 1.0, kOutcropRoom = 7, kOutcropApart = 24;
constexpr int kOutcropFace = 3;
// The summit's paved square round the hill's middle, and its quarry face at the crown's inner foot.
constexpr double kPave = 4.5, kSummitQuarryOut = kSummit - 2.5;
constexpr int kSummitQuarryTiles = 2;

// The hill country's one look (FEEDBACK 2026-11-11: a fixed look rather than a climate): plain
// grass everywhere so every grass rule reads one way, ridge-rock crowns and feet, slate stairs,
// squares and roads, boardwalk fords, scree, and spring meadow along the streams.
constexpr ClimateLook kHillsLook = {"hill-country", CLIFFS,       RIDGE_ROCK,   GRASS,     GRASS,
								   PALE_SAND,      SLATE_ASHLAR, SLATE_ASHLAR, SLATE_ASHLAR,
								   SLATE_ASHLAR,   BOARDWALK,    CLAY,         DEEP_WATER, SCREE,
								   SPRING_MEADOW,  RIDGE_ROCK,   GRASS};
static_assert(terrainGroup(kHillsLook.rock) == TerrainGroup::Ridges &&
				  terrainGroup(kHillsLook.homeGrass) == TerrainGroup::Grass &&
				  terrainGroup(kHillsLook.openGrass) == TerrainGroup::Grass &&
				  terrainGroup(kHillsLook.farmGrass) == TerrainGroup::Grass &&
				  terrainGroup(kHillsLook.town) == TerrainGroup::Paths &&
				  terrainGroup(kHillsLook.bridge) == TerrainGroup::Paths &&
				  terrainGroup(kHillsLook.rough) == TerrainGroup::Rough &&
				  terrainGroup(kHillsLook.fertile) == TerrainGroup::Fertile,
			  "The hill country's look must keep every terrain's rules");

struct Layout
{
	Torus t{1, 1};
	// The mechanics (one canonical terrain per rule) and the finished look painted over them.
	TerrainSketch terrain, finished;
	std::vector<ShapePoint> hills;
	// Per vertex/tile: its nearest hill, the hill whose ground it is (-1 in the valley), the colony
	// whose summit it is, and its distance from its nearest hill and the band arithmetic's radius.
	std::vector<int> nearestOf, homeOf, hillOf;
	std::vector<double> distance, nominal;
	std::vector<unsigned char> stairs, valley, roads, fords, stream, crown, foot, talus, meadow;
	// Each hill's outcrop quarry faces (tiles), fewer where an outcrop did not fit.
	std::vector<std::vector<std::vector<int>>> outcropFaces;
	Farm farm;
	double radius = 0, phase = 0, wobble = 0;
	int bands = 0;
	std::string failure;
};

Layout design(const GenerationRequest &r, GenerationContext &context)
{
	const HillsOptions o(r);
	Layout L;
	L.t = {1 << r.wDec, 1 << r.hDec};
	const Torus &t = L.t;
	const int n = t.size(), hills = r.nbTeams + o.extraHills;
	// Sequence draws explicitly: C++ does not order function arguments, so two
	// bounded() calls inside latticeSites(...) can swap x/y between compilers.
	const int originX = int(context.bounded("rice-layout", t.w));
	const int originY = int(context.bounded("rice-layout", t.h));
	L.hills = latticeSites(t.w, t.h, hills, originX, originY).sites;
	// Deal before assigning owners: the unoccupied hills and odd-count lattice positions must
	// not always favour the same team index. This is comparable spacing, not exact tile symmetry.
	dealStarts(context, L.hills);
	const double separation = std::min<double>(nearestSiteDistance(t, L.hills), std::min(t.w, t.h));
	const double budget = std::min<double>(o.hillRadius, (separation - kValley) / 2);
	// A circle contains every row angle. Use the axial yield fit as a single radial
	// period, rather than changing width by angle and breaking the concentric silhouette.
	const FarmRows fit = bestFarmRows(0);
	const FarmRows rows{fit.crops * o.bandWidth / 100, fit.water * o.bandWidth / 100};
	L.bands = int(std::floor((budget - kSummit - 2 * kCap) / rows.period()));
	if (L.bands < 1)
	{
		L.failure = "Hills need room for a summit, a full crop/water band and valleys; "
					"use a larger map, fewer hills, narrower bands or a larger hill radius.";
		return L;
	}
	// Finish at a complete water band and an outer cap. Never leave a truncated crop
	// band connected to valley grass: it would spread into the commons during a long game.
	L.radius = kSummit + 2 * kCap + L.bands * rows.period();
	L.phase = context.bounded("rice-stairs", 3600) * (2 * kPi / 3600);
	// The lobes: as much of kWobble as leaves half the valley floor open at the closest pair of
	// hills, so a fitted band count never changes for them; each hill draws its own phases.
	L.wobble = std::clamp((separation - 2 * L.radius - kValley / 2) / 2, 0.0, kWobble);
	std::vector<ContourWobble> wobbles;
	for (size_t k = 0; k < L.hills.size(); ++k)
	{
		ContourWobble w;
		w.amplitude = L.wobble;
		for (double &phase : w.phase)
			phase = context.bounded("rice-contours", 3600) * (2 * kPi / 3600);
		wobbles.push_back(w);
	}
	L.terrain.assign(n, GRASS);
	L.nearestOf.assign(n, -1);
	L.homeOf.assign(n, -1);
	L.hillOf.assign(n, -1);
	L.distance.assign(n, 0);
	L.nominal.assign(n, 0);
	L.valley.assign(n, 1);
	// The reusable farm operation owns row/cap/crossing geometry. This generator
	// supplies the fitted budget and later assigns summit and valley ownership.
	ContourFarmStyle style{rows, kSummit, kCap, L.bands, o.stairs, kStairHalf, L.phase};
	style.wobbles = wobbles;
	ContourFarm contours = layContourFarm(L.terrain, t, L.hills, style);
	L.farm = std::move(contours.farm);
	L.stairs = std::move(contours.crossings);

	// Every layer below draws from its own stream, so toggling one never moves another's draws.
	std::mt19937 &streamRandom = context.stream("hills-streams");
	const PeriodicNoise warpX(t.w, t.h, kWarpCell, streamRandom), warpY(t.w, t.h, kWarpCell, streamRandom),
		streamWidth(t.w, t.h, 12, streamRandom);
	std::vector<Site> sites;
	for (const ShapePoint &hill : L.hills)
		sites.push_back({int(hill.x), int(hill.y)});
	// Who owns each vertex, and where the streams run. A stream follows the boundary between two
	// hills' ground (points almost equally close to both), sampled through a smooth warp so it
	// winds, and stays dry near the saddle between those two hills, where the boundary passes
	// closest to both: whatever the warp does, every pair of neighbours keeps a dry crossing, so
	// the streams never part the valley. One hill has no streams.
	std::vector<unsigned char> streamLine(n, 0);
	for (int i = 0; i < n; ++i)
	{
		const int x = t.remainderX(i), y = i / t.w;
		const NearestSites nearest = nearestTwoSites(t, sites, x, y);
		const int h = nearest.first;
		const double first = std::sqrt(double(nearest.firstDistanceSquared));
		// The hill's ground ends where its lobed outer cap does, not at the circle.
		const int dx = t.offsetX(int(L.hills[h].x), x), dy = t.offsetY(int(L.hills[h].y), y);
		const double nominal = contourNominal(style, size_t(h), first, std::atan2(double(dy), double(dx)));
		L.nearestOf[i] = h;
		L.distance[i] = first;
		L.nominal[i] = nominal;
		if (nominal > L.radius)
		{
			if (!o.river || hills < 2 || nominal < L.radius + kStreamClear)
				continue;
			// The saddle of a vertex's two nearest hills: the midpoint of its own images of them.
			const auto saddle = [&](const NearestSites &pair)
			{
				const ShapePoint &a = L.hills[pair.first], &b = L.hills[pair.second];
				const double mx = (t.offsetX(x, int(a.x)) + t.offsetX(x, int(b.x))) / 2.0;
				const double my = (t.offsetY(y, int(a.y)) + t.offsetY(y, int(b.y))) / 2.0;
				return std::sqrt(mx * mx + my * my);
			};
			if (nearest.second < 0)
				continue;
			const double fade = std::clamp((saddle(nearest) - kFordRadius) / kWarpFade, 0.0, 1.0);
			const int sx = t.x(int(std::lround(x + (warpX.at(x, y) - 0.5) * 2 * kWarp * fade)));
			const int sy = t.y(int(std::lround(y + (warpY.at(x, y) - 0.5) * 2 * kWarp * fade)));
			const NearestSites warped = nearestTwoSites(t, sites, sx, sy);
			if (warped.second < 0)
				continue;
			const double gap = std::sqrt(double(warped.secondDistanceSquared)) -
							   std::sqrt(double(warped.firstDistanceSquared));
			if (gap >= 2 * (kStreamHalf + kStreamHalfSpan * streamWidth.at(x, y)))
				continue;
			if (saddle(warped) < kFordRadius)
				continue;
			streamLine[i] = 1;
			continue;
		}
		L.valley[i] = 0;
		L.hillOf[i] = h;
		if (first < kSummit && h < r.nbTeams)
			L.homeOf[i] = h;
	}
	// A stream cut short by a saddle or a hill's clearance leaves puddles: keep only real streams.
	streamLine = dropSmallRegions(t, streamLine, kStreamLeast);
	// Every stair carries on as a road across the valley to the edge of its hill's ground, the
	// line where another hill becomes the nearest, and a few tiles past it so it fords the stream
	// there (maintainer review 2026-09-16: "at least one sand road connecting from each of the
	// spokes outwards to the edges of your square"). The road starts inside the lobed outer cap,
	// over the stair's own paving, so the two always join, and meanders once clear of the foot.
	std::mt19937 &roadRandom = context.stream("hills-roads");
	const PeriodicNoise meander(t.w, t.h, 24, roadRandom);
	L.roads.assign(n, 0);
	for (size_t h = 0; h < L.hills.size(); ++h)
		for (int s = 0; s < o.stairs; ++s)
		{
			const double angle = L.phase + s * 2 * kPi / o.stairs;
			const double c = std::cos(angle), sn = std::sin(angle);
			double reach = L.radius;
			for (double r = L.radius; r < std::max(t.w, t.h); r += 0.5)
			{
				const int x = t.x(int(std::lround(L.hills[h].x + r * c)));
				const int y = t.y(int(std::lround(L.hills[h].y + r * sn)));
				if (nearestTwoSites(t, sites, x, y).first != int(h))
					break;
				reach = r;
			}
			reach += kRoadOverrun;
			const double from = L.radius - L.wobble - 2 * kCap;
			std::vector<StrokePoint> line;
			for (double along = from;; along += 3)
			{
				const double at = std::min(along, reach);
				const double px = L.hills[h].x + at * c, py = L.hills[h].y + at * sn;
				const double settle = std::clamp((at - L.radius - L.wobble) / kRoadSettle, 0.0, 1.0);
				const double side =
					(meander.at(t.x(int(std::lround(px))), t.y(int(std::lround(py)))) - 0.5) * 2 *
					kRoadMeander * settle;
				line.push_back({px - side * sn, py + side * c, kRoadHalf});
				if (at >= reach)
					break;
			}
			strokePath(L.roads, t, line);
		}
	for (int i = 0; i < n; ++i)
		L.roads[i] = L.roads[i] && L.valley[i];

	// The walls. The farm laid its caps and crossings as sand: every crossing becomes a stair of
	// paving, each cap a rock wall (the crown round the summit, the foot round the outer band),
	// paved instead where it lies within kDoorMargin of a stair so the gate keeps the stair's width.
	const std::vector<unsigned char> door = dilate(t, L.stairs, kDoorMargin);
	L.crown.assign(n, 0);
	L.foot.assign(n, 0);
	for (int i = 0; i < n; ++i)
	{
		if (!L.farm.sand[i])
			continue;
		if (L.stairs[i] || L.farm.row[i] >= 0 || door[i])
		{
			L.terrain[i] = TRAIL;
			continue;
		}
		L.terrain[i] = RIDGE_ROCK;
		(L.distance[i] < kSummit + 2 * kCap ? L.crown : L.foot)[i] = 1;
	}
	// The streams, with a boardwalk ford wherever a road crosses one, then the roads.
	L.stream.assign(n, 0);
	L.fords.assign(n, 0);
	int streamCorners = 0, fordCorners = 0, roadCorners = 0;
	for (int i = 0; i < n; ++i)
	{
		if (L.roads[i])
		{
			L.terrain[i] = TRAIL;
			L.fords[i] = streamLine[i];
			fordCorners += streamLine[i];
			++roadCorners;
		}
		else if (streamLine[i])
		{
			L.terrain[i] = WATER;
			L.stream[i] = 1;
			++streamCorners;
		}
	}
	// Thicken the walls to three vertices so they draw as rock (a two-vertex ring draws almost no
	// pure rock tile). The crown grows inwards over the town's outermost ring, never into the first
	// crop row: that row is the colony's nearest farmland, and a crown grown over it cost a fifth of
	// the food within 24 steps of the swarm. The foot grows outwards into the valley. Neither grows
	// within a vertex of paving or water.
	const auto canGrow = [&L, &t](int i)
	{
		const bool townEdge = L.hillOf[i] >= 0 && L.farm.row[i] < 0 && L.distance[i] < kSummit &&
							  L.distance[i] >= kSummit - kCrownInset;
		if (L.terrain[i] != GRASS || !(L.valley[i] || townEdge))
			return false;
		const int x = t.remainderX(i), y = i / t.w;
		for (int dy = -1; dy <= 1; ++dy)
			for (int dx = -1; dx <= 1; ++dx)
			{
				const int s = L.terrain[t.at(x + dx, y + dy)];
				if (s != GRASS && s != RIDGE_ROCK)
					return false;
			}
		return true;
	};
	int unthickened = 0;
	thickenRidges(L.terrain, t, canGrow, &unthickened, RIDGE_ROCK);
	for (int i = 0; i < n; ++i)
		if (L.terrain[i] == RIDGE_ROCK && !L.crown[i] && !L.foot[i])
			(L.valley[i] ? L.foot : L.crown)[i] = 1;
	// The town is what lies inside its crown.
	for (int i = 0; i < n; ++i)
		if (L.terrain[i] == RIDGE_ROCK)
			L.homeOf[i] = -1;

	// The outcrops: kOutcrops rock knots in every hill's valley, each in the most open ground of it
	// left (the valley grass farthest from water, paving, rock and the hill, kOutcropApart from the
	// other), so none crowds a stream or a road. Its quarry face looks back at the hill and is left out of the rock; the face and the
	// ring before it stay plain grass (quarryApproach).
	std::mt19937 &outcropRandom = context.stream("hills-outcrops");
	std::vector<unsigned char> outcropTiles(n, 0), keepGrass(n, 0);
	L.outcropFaces.assign(L.hills.size(), {});
	int outcrops = 0;
	const std::vector<std::int64_t> clearance = [&]
	{
		std::vector<unsigned char> blocked(n, 0);
		for (int i = 0; i < n; ++i)
			blocked[i] = !L.valley[i] || L.terrain[i] != GRASS;
		return distanceSquaredTo(t, blocked);
	}();
	for (size_t h = 0; h < L.hills.size(); ++h)
		for (int pick = 0; pick < kOutcrops; ++pick)
		{
			const double lobe = outcropRandom() % 3600 * (2 * kPi / 3600);
			int centre = -1;
			const auto apart = [&](int i)
			{
				for (const std::vector<int> &face : L.outcropFaces[h])
				{
					const int dx = t.offsetX(t.remainderX(face.front()), t.remainderX(i));
					const int dy = t.offsetY(face.front() / t.w, i / t.w);
					if (dx * dx + dy * dy < kOutcropApart * kOutcropApart)
						return false;
				}
				return true;
			};
			for (int i = 0; i < n; ++i)
				if (L.nearestOf[i] == int(h) && (centre < 0 || clearance[i] > clearance[centre]) &&
					apart(i))
					centre = i;
			if (centre < 0 || clearance[centre] < std::int64_t(kOutcropRoom * kOutcropRoom))
			{
				context.telemetry.fallback("hills.outcrops.omitted",
										   "No open valley grass for an outcrop.", int(h));
				continue;
			}
			const int cx = t.remainderX(centre), cy = centre / t.w;
			const double toHill = std::atan2(double(t.offsetY(cy, int(L.hills[h].y))),
											 double(t.offsetX(cx, int(L.hills[h].x))));
			std::vector<unsigned char> knot(n, 0);
			const int reachTiles = int(std::ceil(kOutcropRadius + kOutcropLobe));
			for (int dy = -reachTiles; dy <= reachTiles; ++dy)
				for (int dx = -reachTiles; dx <= reachTiles; ++dx)
				{
					const double theta = std::atan2(double(dy), double(dx));
					const double radius = kOutcropRadius + kOutcropLobe * std::cos(2 * (theta - lobe));
					if (dx * dx + dy * dy <= radius * radius)
						knot[t.at(cx + dx, cy + dy)] = 1;
				}
			std::vector<unsigned char> open(n, 0);
			for (int i = 0; i < n; ++i)
				open[i] = !knot[i];
			const int facing = t.at(int(std::lround(cx + kOutcropRadius * std::cos(toHill))),
									int(std::lround(cy + kOutcropRadius * std::sin(toHill))));
			const std::vector<int> face = quarryNear(t, knot, open, facing, kOutcropFace / 2 + 1, 1);
			for (int i : face)
				knot[i] = 0;
			const std::vector<unsigned char> rock = rockVertices(t, knot);
			for (int i = 0; i < n; ++i)
				if (rock[i])
					L.terrain[i] = RIDGE_ROCK;
			std::vector<unsigned char> faceMask = tileMask(t, face);
			const std::vector<unsigned char> approach = quarryApproach(t, faceMask);
			for (int i = 0; i < n; ++i)
				faceMask[i] = faceMask[i] || approach[i];
			const std::vector<unsigned char> corners = tileCorners(t, faceMask);
			for (int i = 0; i < n; ++i)
			{
				keepGrass[i] = keepGrass[i] || corners[i];
				outcropTiles[i] = outcropTiles[i] || rock[i];
			}
			if (face.empty())
			{
				context.telemetry.fallback("hills.outcrops.faceless", "An outcrop found no quarry face.",
										   int(h));
				continue;
			}
			L.outcropFaces[h].push_back(face);
			++outcrops;
		}

	// Scree at every hill's foot, frayed; meadows along the streams, kept off every rock. The
	// outcrops keep a grass foot all round: it is where stone above the default amount grows.
	std::mt19937 &screeRandom = context.stream("hills-talus");
	const PeriodicNoise fray(t.w, t.h, 10, screeRandom);
	std::mt19937 &meadowRandom = context.stream("hills-meadow");
	const PeriodicNoise meadowWidth(t.w, t.h, 16, meadowRandom);
	std::vector<unsigned char> footRock(n, 0), anyRock(n, 0), paved(n, 0);
	for (int i = 0; i < n; ++i)
	{
		footRock[i] = L.foot[i];
		anyRock[i] = L.foot[i] || outcropTiles[i];
		paved[i] = L.terrain[i] == TRAIL;
	}
	const std::vector<std::int64_t> toFoot = distanceSquaredTo(t, footRock);
	const std::vector<std::int64_t> toRock = distanceSquaredTo(t, anyRock);
	const std::vector<unsigned char> nearPaving = dilate(t, paved, 1);
	const std::vector<unsigned char> nearWater = [&]
	{
		std::vector<unsigned char> wet(n, 0);
		for (int i = 0; i < n; ++i)
			wet[i] = L.terrain[i] == WATER;
		return dilate(t, wet, 1);
	}();
	std::vector<unsigned char> streams(n, 0);
	for (int i = 0; i < n; ++i)
		streams[i] = L.stream[i];
	const std::vector<std::int64_t> toStream = distanceSquaredTo(t, streams);
	L.talus.assign(n, 0);
	L.meadow.assign(n, 0);
	int talusCorners = 0, meadowCorners = 0;
	for (int i = 0; i < n; ++i)
	{
		if (!L.valley[i] || L.terrain[i] != GRASS || keepGrass[i] || nearPaving[i] || nearWater[i])
			continue;
		const int x = t.remainderX(i), y = i / t.w;
		const double screeOut = kTalus + kTalusSpan * fray.at(x, y);
		if (toFoot[i] >= 0 && double(toFoot[i]) <= screeOut * screeOut)
		{
			L.terrain[i] = MUD;
			L.talus[i] = 1;
			++talusCorners;
			continue;
		}
		const double width = kMeadow + kMeadowSpan * meadowWidth.at(x, y);
		if (toStream[i] >= 0 && double(toStream[i]) <= width * width &&
			(toRock[i] < 0 || double(toRock[i]) > kMeadowFoot * kMeadowFoot))
		{
			L.terrain[i] = LOAM;
			L.meadow[i] = 1;
			++meadowCorners;
		}
	}
	// Every summit's paved square round its middle (vacant hills too: a town to take).
	for (size_t h = 0; h < L.hills.size(); ++h)
		paveCore(L.terrain, t, L.hills[h].x, L.hills[h].y, kPave,
				 [&L, h](int i) { return L.hillOf[i] == int(h) && L.distance[i] < kSummit; });

	// THE LOOK, painted last over the mechanics: boardwalk fords, slate for every other paving.
	L.finished = L.terrain;
	LookRoles roles;
	roles.tended = [&L](int i) { return L.homeOf[i] >= 0; };
	roles.path = [&L](int i) { return L.fords[i] ? PathLook::Bridge : PathLook::Town; };
	roles.farm = [&L](int i) { return L.farm.row[i] >= 0 && L.farm.row[i] % 2 == 0; };
	paintClimate(L.finished, t, kHillsLook, roles);

	context.telemetry.measure("hills.roads.corners", roadCorners);
	context.telemetry.measure("hills.hills.actual", hills);
	context.telemetry.measure("hills.hills.radius-budget", budget);
	context.telemetry.measure("hills.hills.radius-actual", L.radius);
	context.telemetry.measure("hills.contours.wobble", L.wobble);
	context.telemetry.measure("hills.bands.per-hill", L.bands);
	context.telemetry.measure("hills.bands.crop-width", rows.crops);
	context.telemetry.measure("hills.bands.water-width", rows.water);
	context.telemetry.measure("hills.stairs.per-hill", o.stairs);
	context.telemetry.measure("hills.stairs.phase-radians", L.phase);
	context.telemetry.measure("hills.walls.unthickened-vertices", unthickened);
	context.telemetry.measure("hills.streams.water-corners", streamCorners);
	context.telemetry.measure("hills.streams.ford-corners", fordCorners);
	context.telemetry.measure("hills.meadows.corners", meadowCorners);
	context.telemetry.measure("hills.talus.corners", talusCorners);
	context.telemetry.measure("hills.outcrops.placed", outcrops);
	return L;
}

bool generate(GenerationWorld &game, GenerationContext &context)
{
	context.stage = "hills layout";
	const HillsOptions o(context.request);
	const Layout L = design(context.request, context);
	if (!L.failure.empty())
	{
		context.detail = L.failure;
		return false;
	}
	const Torus &t = L.t;
	GenerationMap &map = game.map;
	const int n = t.size(), teams = context.request.nbTeams;
	for (int k = 0; k < teams; ++k)
		game.addTeam();
	writeVertices(map, L.finished);
	context.stage = "hills summits";
	const auto home = [&](int k)
	{
		std::vector<unsigned char> mask(n, 0);
		for (int i = 0; i < n; ++i)
			mask[i] = L.homeOf[i] == k && map.terrainPropertiesAt(t.remainderX(i), i / t.w).buildable;
		return mask;
	};
	const auto anchor = [&](int k)
	{ return MapGeneratorPoint(int(L.hills[k].x) - 2, int(L.hills[k].y) - 2); };
	if (!settleColonies(game, context, "rice-starts", home, anchor))
		return false;
	context.stage = "hills first meal";
	for (int k = 0; k < teams; ++k)
	{
		// The first feeding deadline arrives before some AI openings finish
		// building AND filling an inn across the summit cap. A small completed
		// inn supplies that first meal, not an ongoing food subsidy. It sits
		// halfway toward the first stair, leaving both its mouth and the swarm
		// margins free. Fruit remains exclusively a valley reward.
		if (placeStartingBuilding(game, k, "inn", 0, L.hills[k].x + 7 * std::cos(L.phase),
								  L.hills[k].y + 7 * std::sin(L.phase), 3, home(k), {WHEAT}) < 0)
		{
			context.detail = "A summit has no room for its starter inn.";
			return false;
		}
	}
	context.telemetry.measure("hills.inns.per-colony", 1);
	context.stage = "hills defenses";
	// Place one tower beside EACH mouth rather than scoring all stairs together (which can
	// concentrate every tower at one entrance). Search only three tiles around its designed
	// position. Failure is explicit: silently dropping one would leave one colony exposed.
	for (int k = 0; k < teams && o.towers > 0; ++k)
		for (int s = 0; s < o.stairs; ++s)
		{
			const double angle = L.phase + s * 2 * kPi / o.stairs;
			const double radial = kSummit - 3;
			const double x = L.hills[k].x + radial * std::cos(angle) - 2 * std::sin(angle);
			const double y = L.hills[k].y + radial * std::sin(angle) + 2 * std::cos(angle);
			// The whole upper stair width must be covered, not just its centre.
			// The shared helper filters candidates by the actual building's range.
			std::vector<MapGeneratorPoint> mouth;
			for (int side = -2; side <= 2; ++side)
				mouth.emplace_back(int(std::lround(L.hills[k].x + (kSummit + 1) * std::cos(angle) -
												   side * std::sin(angle))),
								   int(std::lround(L.hills[k].y + (kSummit + 1) * std::sin(angle) +
												   side * std::cos(angle))));
			// Three unsupplied towers recruit three of the default four workers
			// for stone before the first inn has food. Playtest telemetry exposed
			// early Cortex losses during this opening. Give each defensive tower
			// its finite reserve as well as its magazine; no ongoing income or
			// combat rules change, and later replenishment remains the town's job.
			if (placeTower(game, k, o.towers - 1, x, y, 3, home(k), true, mouth, true) < 0)
			{
				context.detail = "A summit stair has no room for a tower covering its mouth.";
				return false;
			}
		}
	context.telemetry.measure("hills.towers.per-colony", o.towers > 0 ? o.stairs : 0);
	context.telemetry.choice("hills.towers.supplies", "magazine-and-stone-reserve");
	context.stage = "hills crops";
	const auto free = [&](int i) { return clearGround(map, t.remainderX(i), i / t.w); };
	// Seed food beside EVERY stair, on the inner edge of the first crop band.
	// A single starter patch made a summit inn's opening haul depend on which
	// side of town the AI chose. Telemetry showed an empty first inn and early
	// losses even after the tower delivery jobs were removed. Three small near
	// patches give alternative gathering edges without filling the summit or
	// changing the permanent stairs. Wood needs only one opening patch; its
	// faster spread must not compete with wheat at every entrance.
	for (int k = 0; k < teams; ++k)
	{
		// A patch's seed must be able to hold its crop: with no beaches, the tile on a grass and
		// water seam is clear ground but holds no crop, and a seed there grows nothing.
		const auto eligibleFor = [&](int type)
		{
			return [&, type](int i)
			{
				return L.hillOf[i] == k && L.farm.row[i] == 0 && free(i) &&
					   map.isResourceAllowed(t.remainderX(i), i / t.w, type);
			};
		};
		int starterWheat = 0;
		for (int stair = 0; stair < o.stairs; ++stair)
		{
			const KitFrame frame{int(L.hills[k].x), int(L.hills[k].y),
								 L.phase + stair * 2 * kPi / o.stairs};
			// Two corners into the crop band is close to home but leaves the
			// containment cap intact. The five-corner side offset clears the
			// sand stair even after corner-to-tile conversion; search stays in
			// row zero, and a shortfall is explicit rather than planting in town.
			const int wheat =
				plantPatchNear(map, t, frame.at(kSummit + kCap + 2, -5, 5), WHEAT, 12, eligibleFor(WHEAT));
			if (wheat < 8)
			{
				context.detail = "A stair's starter wheat plot does not fit.";
				return false;
			}
			starterWheat += wheat;
		}
		const KitFrame woodFrame{int(L.hills[k].x), int(L.hills[k].y), L.phase};
		const KitSeed woodSeed = woodFrame.at(kSummit + kCap + 5, 6, 5);
		if (const int wood = plantPatchNear(map, t, woodSeed, WOOD, 12, eligibleFor(WOOD)); wood < 8)
		{
			context.detail = "A stair's starter wood plot does not fit: " + std::to_string(wood) +
							 " of 12 tiles near (" + std::to_string(t.x(woodSeed.x)) + ", " +
							 std::to_string(t.y(woodSeed.y)) + ") for colony " + std::to_string(k) + ".";
			return false;
		}
		context.telemetry.measure("hills.starter.wheat-tiles", starterWheat, k);
		// A permanent quarry face at the crown's inner foot, midway between the first two stairs,
		// guarantees early upgrades: the same place in every summit's frame. Unlike wheat and
		// wood it never spreads, so it cannot engulf summit building space.
		const KitFrame quarryFrame{int(L.hills[k].x), int(L.hills[k].y), L.phase + kPi / o.stairs};
		const int quarry = plantQuarryFaces(map, t, {quarryFrame.at(kSummitQuarryOut, 0, 4)},
											kSummitQuarryTiles,
											[&](int i) { return L.homeOf[i] == k && free(i); });
		if (quarry < 1)
		{
			context.detail = "A summit quarry does not fit.";
			return false;
		}
		context.telemetry.measure("hills.summit-quarry.tiles", quarry, k);
	}
	// Separate each hill's budget so raster ordering cannot give one hill all the wood.
	// A small starter guarantee survives 0%; all additional terrace crops scale normally.
	// The guarantee is ON the terrace, outside the summit containment ring.
	for (int k = 0; k < int(L.hills.size()); ++k)
	{
		int area = 0;
		for (int i = 0; i < n; ++i)
			area += L.hillOf[i] == k && L.farm.row[i] >= 0 && L.farm.row[i] % 2 == 0 &&
					(map.terrainSupportsResourceAtByIndex(t.remainderX(i), i / t.w, WHEAT) &&
						map.terrainSupportsResourceAtByIndex(t.remainderX(i), i / t.w, WOOD));
		const int wheat = int(scaledCount(area * 45 / 100, o.wheat));
		const int wood = int(scaledCount(area * 8 / 100, o.wood));
		const int actual = plantFarm(map, t, L.farm, wheat, wood,
									 [&](int i) { return L.hillOf[i] == k && free(i); });
		context.telemetry.measure("hills.crops.requested", wheat + wood, k);
		context.telemetry.measure("hills.crops.placed", actual, k);
		if (actual < wheat + wood)
			context.telemetry.fallback("hills.crops.saturated", "Available crop row grass filled.",
									   k);
	}
	// Small wild crops in the valley just outside every hill, between its stairs, alternating wheat
	// and wood so every hill has both. They lie within the growth probe of the hill's outer water
	// band, so they spread into the commons over a long game: that is the point, a reason to walk
	// out of the terraces, but it is also valley room a colony may have to clear.
	int wild = 0;
	for (int h = 0; h < int(L.hills.size()); ++h)
		for (int s = 0; s < o.stairs; ++s)
		{
			const bool wheat = s % 2 == 0;
			const int wanted = int(scaledCount(kWildPatch, wheat ? o.wheat : o.wood));
			if (wanted <= 0)
				continue;
			const KitFrame frame{int(L.hills[h].x), int(L.hills[h].y),
								 L.phase + (s + 0.5) * 2 * kPi / o.stairs};
			wild += plantPatchNear(map, t, frame.at(L.radius + L.wobble + kWildOut, 0, 4),
								   wheat ? WHEAT : WOOD, wanted,
								   [&](int i)
								   {
									   return L.valley[i] && !L.roads[i] && free(i) &&
											  map.isResourceAllowed(t.remainderX(i), i / t.w,
																	wheat ? WHEAT : WOOD);
								   });
		}
	context.telemetry.measure("hills.wild-crops.tiles", wild);
	// The meadows' crops: the fertile banks are the valleys' farmland, so they start stocked. Each
	// stretch of meadow is wheat or wood by a broad noise, and within its share the patchiest
	// ground (a finer noise's highest) is planted, so the fields lie in clumps along the banks.
	{
		std::mt19937 &cropRandom = context.stream("hills-meadow-crops");
		const std::vector<int> cover = periodicNoise(t.w, t.h, kMeadowPatch, cropRandom);
		const std::vector<int> stretch = periodicNoise(t.w, t.h, kMeadowStretch, cropRandom);
		const std::vector<unsigned char> loam = pureTiles(L.terrain, t, LOAM);
		std::vector<int> wheatGround, woodGround;
		for (int i = 0; i < n; ++i)
			if (loam[i] && L.valley[i] && free(i))
				(stretch[i] < 65536 * kMeadowWheat / (kMeadowWheat + kMeadowWood) ? wheatGround
																				 : woodGround)
					.push_back(i);
		const int meadowTiles = int(wheatGround.size() + woodGround.size());
		const auto plant = [&](std::vector<int> &ground, int type, int share, int amount)
		{
			std::stable_sort(ground.begin(), ground.end(),
							 [&](int a, int b) { return cover[a] > cover[b]; });
			const int wanted = int(scaledCount(meadowTiles * share / 100, amount));
			int placed = 0;
			for (int i : ground)
			{
				if (placed >= wanted)
					break;
				if (!map.isResourceAllowed(t.remainderX(i), i / t.w, type))
					continue;
				map.setResourceByIndex(t.remainderX(i), i / t.w, type, 1);
				++placed;
			}
			return placed;
		};
		const int meadowWheat = plant(wheatGround, WHEAT, kMeadowWheat, o.wheat);
		const int meadowWood = plant(woodGround, WOOD, kMeadowWood, o.wood);
		context.telemetry.measure("hills.meadows.tiles", meadowTiles);
		context.telemetry.measure("hills.meadows.wheat-tiles", meadowWheat);
		context.telemetry.measure("hills.meadows.wood-tiles", meadowWood);
	}
	// The outcrops' quarry faces, where the commons' stone used to lie scattered: the rock is
	// structural and always drawn, its face stocked in proportion to the stone amount. Above the
	// default the face is full, and the rest grows along the outcrop's foot beside it.
	int outcropStone = 0;
	for (const auto &faces : L.outcropFaces)
		for (const std::vector<int> &face : faces)
		{
			const int wanted = int(scaledCount(int(face.size()), o.stone));
			int stocked = 0;
			for (int f = 0; f < int(face.size()) && stocked < wanted; ++f)
				if (free(face[f]))
				{
					map.setResourceByIndex(t.remainderX(face[f]), face[f] / t.w, STONE, 1);
					++stocked;
				}
			if (stocked < wanted)
				stocked += plantQuarryFaces(
					map, t, {KitSeed{t.remainderX(face.front()), face.front() / t.w, 6}},
					wanted - stocked, [&](int i) { return L.valley[i] && !L.roads[i] && free(i); });
			outcropStone += stocked;
		}
	context.telemetry.measure("hills.outcrops.stone-tiles", outcropStone);
	// Otherwise only persistent fruit in the commons, most of it along the meadows: the terraces'
	// wheat and faster wood stay within their walls, so valley towns keep their expansion room.
	std::vector<int> commons;
	for (int i = 0; i < n; ++i)
		if (L.valley[i] && !L.roads[i] && free(i))
			commons.push_back(i);
	{
		const int wanted = int(scaledCount(int(commons.size()) / 600 + 3, o.fruit));
		const char *stream = "rice-fruit";
		const int actual = scatterClumps(
			context, t, commons, wanted, stream, free,
			[&](MapGeneratorPoint p)
			{ placeResourceClump(map, context, p, CHERRY + int(context.bounded(stream, 3)), 1); });
		context.telemetry.measure("hills.groves.requested", wanted);
		context.telemetry.measure("hills.groves.placed", actual);
		if (actual < wanted)
			context.telemetry.fallback("hills.commons.saturated",
									   "Valley grove placement exhausted its bounded search.", CHERRY);
	}
	seedAlgae(map, context, t, "rice-algae", o.algae, AlgaeBand::anyWater(100));
	// The shared guarantee can supply an awkwardly rasterized start. It does not alter
	// terrain or create another stair. The finished summit room and every sand stair are
	// checked below, so a repair cannot silently erase the map's defining geometry.
	// The generic first-crop rescue may plant a radius-two clump on ANY reachable
	// grass. In a contour map that includes the dry summit town. On sparse-wood,
	// dense-wheat combinations the wood rescue really runs: bulk seeds 34101
	// showed its clump spilling into protected building ground. Supply the
	// shared topup mask rather than repairing the spill after the fact. Every
	// tile of an emergency clump must stay on a crop row of some hill; sand
	// stairs, caps, towns, water rows and valley commons remain crop-free.
	std::vector<unsigned char> cropTopupGround(n, 0);
	for (int i = 0; i < n; ++i)
		cropTopupGround[i] = L.hillOf[i] >= 0 && L.farm.row[i] >= 0 &&
								L.farm.row[i] % 2 == 0 && !L.stairs[i];
	secureStartingCrops(game, context, t, 24, 32, 0, nullptr, &cropTopupGround);
	return true;
}

std::string atTile(const Torus &t, int i)
{
	return "(" + std::to_string(t.remainderX(i)) + ", " + std::to_string(i / t.w) + ")";
}

std::string validateWorld(const GenerationWorld &game, const GenerationContext &context)
{
	GenerationContext replay(context.request);
	const Layout L = design(context.request, replay);
	if (const std::string mismatch = designMismatch(L, game.map, "hills");
		!mismatch.empty())
		return mismatch;
	const GenerationMap &map = game.map;
	const Torus &t = L.t;
	const int n = t.size(), teams = context.request.nbTeams;
	// The terrain is the design's, vertex for vertex: walls, stairs, roads, fords, streams,
	// meadows, scree, outcrops and looks. No later stage may repaint it.
	for (int i = 0; i < n; ++i)
		if (map.vertexTerrainAt(t.remainderX(i), i / t.w) != L.finished[i])
			return "The hills' terrain differs from their design at " + atTile(t, i) + ".";
	std::vector<unsigned char> rock(n, 0);
	for (int i = 0; i < n; ++i)
		rock[i] = L.terrain[i] == RIDGE_ROCK;
	if (const int bad = wallTerrainMismatch(map, t, rock, TerrainGroup::Ridges); bad >= 0)
		return "A crown, foot or outcrop is not Ridges at " + atTile(t, bad) + ".";
	const auto open = walkableTiles(map);
	const auto buildable = buildableTiles(map);
	const auto workers = unitTilesByTeam(map, teams);
	// Check final tiles, not merely the corner sketch: the complete paved cores of stairs
	// must survive planting, colony placement and all later stages.
	const auto paving = pureTiles(L.terrain, t, TRAIL);
	for (int i = 0; i < n; ++i)
		if (L.stairs[i] && paving[i] && !open[i])
			return "A hill stair is blocked at " + atTile(t, i) + ".";
	const auto grass = pureTiles(L.terrain, t, GRASS);
	// A tile with a paved corner, within `from` to `to` tiles of hill k's middle: a slice through
	// every stair and gate of that hill, which a validator shuts to prove the walls hold.
	const auto gateSlice = [&](int k, double from, double to)
	{
		std::vector<unsigned char> shut(n, 0);
		for (int i = 0; i < n; ++i)
		{
			if (L.nearestOf[i] != k || L.nominal[i] < from || L.nominal[i] > to)
				continue;
			const int x = t.remainderX(i), y = i / t.w;
			for (int c = 0; c < 4 && !shut[i]; ++c)
				shut[i] = L.terrain[t.at(x + c % 2, y + c / 2)] == TRAIL;
		}
		return shut;
	};
	for (int k = 0; k < teams; ++k)
	{
		std::vector<unsigned char> home(n, 0);
		for (int i = 0; i < n; ++i)
			home[i] = L.homeOf[i] == k;
		// Test crop containment as a future footprint, not a lucky initial planting.
		// Any eight-connected PURE grass component reachable from the summit must be
		// disjoint from terrace crop rows. Rock, paving and water remain barriers to
		// growth even after every crop and building is removed.
		const auto fromHome = stepsFrom(t, home, grass);
		const auto fromWorkers = stepsFrom(t, tileMask(t, workers[k]), open);
		for (int i = 0; i < n; ++i)
		{
			if (fromHome[i] >= 0 && L.farm.row[i] >= 0)
				return "Terrace crops could grow into a summit.";
			if (home[i] && map.isResource(t.remainderX(i), i / t.w) &&
				(map.getResource(t.remainderX(i), i / t.w).type == WHEAT ||
				 map.getResource(t.remainderX(i), i / t.w).type == WOOD))
				return "A crop repair entered the protected summit.";
			if (L.hillOf[i] == k && L.stairs[i] && paving[i] && fromWorkers[i] < 0)
				return "A colony cannot walk the full length of every stair.";
		}
		// These overlap: 100 anchors are a generous contiguous budget, not 100 buildings.
		if (buildSites(t, buildable, home) < 100)
			return "A hill summit lacks building room.";
		// The walls hold, against walkers and swimmers: with the gates through its crown shut, a
		// colony never leaves its summit; with the gates through its foot shut, never its hill.
		const auto summitLeak = leakWithShut(
			map, t, workers[k], gateSlice(k, kSummit - 1, kSummit + 2 * kCap + 1), true,
			[&](int i) { return L.nearestOf[i] == k && L.nominal[i] < kSummit + 2 * kCap; });
		if (summitLeak >= 0)
			return "A summit's crown leaks at " + atTile(t, summitLeak) + ".";
		const auto hillLeak = leakWithShut(
			map, t, workers[k], gateSlice(k, L.radius - 2 * kCap - 1, L.radius + 1), true,
			[&](int i) { return L.hillOf[i] == k; });
		if (hillLeak >= 0)
			return "A hill's foot leaks at " + atTile(t, hillLeak) + ".";
	}
	if (const int parted = partingBarrier(map); parted >= 0)
		return "Rock parts the walkable ground at " + atTile(t, parted) + ".";
	return walkFromFirstColony(map, teams, "the hills", "along the stairs and valley fords").error;
}
} // namespace
HillsOptions::HillsOptions(const GenerationRequest &r)
	: extraHills(r.option("extra-hills")), hillRadius(r.option("hill-radius")),
	  bandWidth(r.option("band-width")), stairs(r.option("stairs")),
	  towers(r.option("starting-towers")), river(r.option("valley-river") != 0),
	  wheat(r.option("wheat-amount")), wood(r.option("wood-amount")),
	  stone(r.option("stone-amount")), algae(r.option("algae-amount")),
	  fruit(r.option("fruit-amount"))
{
}
GeneratorDefinition hillsDefinition()
{
	return {"hills",
			46,
			"Hills",
			9,
			false,
			{GeneratorControl{"extra-hills", "Unoccupied hills", 0, 4, 1, 0, ControlGroup::Layout}
				 .withSearchRange(0, 2),
			 GeneratorControl{"hill-radius", "Hill radius", 44, 100, 4, 60, ControlGroup::Layout}
				 .withSearchRange(52, 76),
			 GeneratorControl{"band-width", "Contour band width", 80, 120, 10, 100,
							  ControlGroup::Terrain}
				 .withSearchRange(90, 110),
			 GeneratorControl{"stairs", "Stairs per hill", 2, 4, 1, 3, ControlGroup::Layout}
				 .withSearchRange(2, 4),
			 GeneratorControl{"starting-towers", "Starting tower level", 0, 3, 1, 1,
							  ControlGroup::Layout}
				 .withSearchRange(0, 1),
			 GeneratorControl::toggle("valley-river", "Valley river", true, ControlGroup::Terrain)
				 .withSearchValues({0, 1}),
			 GeneratorControl::percentage("wheat-amount", "Wheat amount"),
			 GeneratorControl::percentage("wood-amount", "Wood amount"),
			 GeneratorControl::percentage("stone-amount", "Stone amount"),
			 GeneratorControl::percentage("algae-amount", "Algae amount"),
			 GeneratorControl::percentage("fruit-amount", "Fruit amount")},
			generate,
			true,
			designFailure<design>,
			validateWorld,
			{"terrain:natural", "feature:terraces", "feature:river", "feature:stone-walls",
			 "feature:farmland", "style:tight-building", "fairness:stamped-lattice"}};
}
