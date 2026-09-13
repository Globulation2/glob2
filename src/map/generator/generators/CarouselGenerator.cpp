// SPDX-License-Identifier: GPL-3.0-or-later
#include "CarouselGenerator.h"
#include "FertilityField.h"
#include "Game.h"
#include "GenerationContext.h"
#include "Drawing.h"
#include "Farmland.h"
#include "Geometry.h"
#include "Grid.h"
#include "Homes.h"
#include "LatticeNoise.h"
#include "Pipeline.h"
#include "Planting.h"
#include "Resources.h"
#include "Roads.h"
#include "Settlements.h"
#include "Sketch.h"
#include "Territories.h"
#include "Towers.h"
#include "Walls.h"
#include "Wedge.h"
#include <algorithm>
#include <climits>
#include <cmath>
#include <string>
#include <vector>
using namespace MapGeneration;

// Carousel: a ring of walled homes in the sea, each with a single door that leads clockwise. A
// colony's door opens onto a corridor that sweeps round to a small court pressed against the next
// colony's home, and from every court a spoke runs in to the plaza in the middle of the map. The
// court and the home beside it are parted by nothing but a thin wall of stone: no unit can walk
// through it, but a tower shoots over it. Nothing but stone parts any two pieces: the sea between them
// fills in, and a single line of stone stands where they meet. So every
// colony besieges one neighbour from its court while the other neighbour besieges it, and the
// only way to walk into anyone's home is the long way round, through the plaza, the enemy's court
// and the corridor behind it.
//
// Every coast is sealed, as City states' homes are: stone stands on every solid-grass tile that
// touches the sea's margin, so a swimmer can land on a beach and walk its sand lane but never get
// inside. Swimming therefore opens nothing; the corridors, spokes and courts are the only routes
// for the whole game. Each home has no pond: its two farms, inland and clear of the sea, are its water
// algae and for fields that regrow.
//
// Everything is designed once in the wedge's frame (a home, its corridor, its court, the wall and
// the spoke, measured by angle round the centre and radius out from it) and turned round the
// centre for every colony, so every colony's ground is the same and the layout is fair for any
// colony count. Like City states it stays round on a rectangular map, in a circle on the shorter
// side, with sea filling the rest. The whole design is a pure function of the request, so
// validateWorld rebuilds it and checks the finished world.
//
// GAME RULES BEHIND IT (docs/map-generators/GAME_RULES_FOR_MAP_DESIGN.md): stone can never be
// cleared, so the walls are permanent and a sealed coast stays sealed; a defence tower scans square
// rings round its footprint with no line of sight (Building::findBestTarget), so a wall two tiles
// thick stops walking but not shooting; grass may never touch water, so every coast wall stands one
// tile in from a sand lane, and the ponds keep well away from the sea so their beaches never join
// its margin; wheat and wood regrow only near water, which is why every home has its ponds; and
// fruit lets an inn pull hungry enemy units across, which makes the plaza's orchard worth the walk.
//
// THE SIZES AT THE DEFAULTS (256x256, 4 colonies): homes 26 tiles in radius 94 tiles out from the
// centre, corridors 3 wide sweeping about 50 degrees, courts 3 in radius, a wall 2 thick, spokes 3
// wide and a plaza 31 in radius with a pond and an orchard.
namespace
{

// The homes' outer coast, as a share of the half side on a 512-tile map (a little more on smaller
// ones): the sea beyond it is where the outer farms grow.
constexpr double kRingShare = 0.7;
// ...but never less than this many tiles of the ring's outer coast per colony, nor more than this
// share of the half side.
constexpr double kArcPerColony = 110;
constexpr double kMaximumRingShare = 0.9;
// The farms: water kept between a farm and any other land, the neck joining it to its home, the land
// a farm keeps between its water rows and its coast, and the smallest farm worth having, as a share
// of the home's area.
constexpr int kFarmGap = 3;
// How far the ground between the pieces fills in from the homes and farms, and the margin of land any
// sea left beyond that keeps beside every piece.
constexpr int kFillReach = 24;
constexpr int kSeaMargin = 3;
constexpr double kNeckHalf = 8;
// The least share of a farm's crop land its colony must be able to walk to.
constexpr double kFarmReach = 0.95;
// How far apart, along a farm's rows, the sand bridges across its water rows stand.
constexpr int kFarmBridgeSpacing = 16;
// Land kept round a farm's water rows inside its walls, and the further margin kept from open sea.
constexpr int kFarmRim = 3;
constexpr int kFarmSeaRim = 3;
constexpr double kMinimumFarmShare = 0.5;
// A farm is a wheat farm: wheat on this share of its crop rows at 100, and a small woodlot of this
// share along one row.
constexpr int kFarmWheatShare = 45;
constexpr int kFarmWoodShare = 3;
// Towers: every colony starts with `tower-count` of them, and this many open 2x2 pads beside them for
// more, all sited against the walls (chooseTowerSites).
constexpr int kTowerPads = 3;
constexpr int kTowerSpacing = 2;
// A colony's elbow, which its neighbour's towers aim at: its court and its lanes this many tiles beyond
// the court's edge. The siege line: towers stand in a home no more than this many steps from the wall.
constexpr double kElbowReach = 12;
constexpr int kSiegeLine = 6;
// A home's radius at 100% of home size, as a share of the half side, but never across more than
// this share of the ring's arc per colony (as a half share: the diameter takes twice it), so crowded
// rings get smaller homes; its outline wobbles by up to this share. A home needs room for its ponds,
// their gap to the sea and this much more for the swarm.
constexpr double kHomeShare = 0.2;
constexpr double kHomeArcShare = 0.2;
constexpr double kMinimumHome = 14;
constexpr double kSwarmRoom = 4;
constexpr double kHomeRoughness = 0.12;
// A court's radius at 100% of court size, in tiles at 256 and never below the smallest court that
// seats a tower with room round it.
constexpr double kCourtRadius = 9;
constexpr double kMinimumCourt = 3;
// The wall between a court and the next home is a band this many court radii wide, so it reaches
// past the court's sides and the court's own coast wall never cuts in between its grass and the wall.
constexpr double kWallBand = 1.3;
// Ground kept between pieces that must not join: two colonies' lanes (a wall on one of them must not
// close it), and the plaza's clearance from the homes' ring.
constexpr double kPieceGap = 4;
// A lane passing another colony's home needs only a tile between them for the wall.
constexpr int kLaneHomeGap = 2;
// The corridor must run at least this far between its home and its court, or the court is simply
// part of the home.
constexpr double kMinimumCorridor = 10;
// A pond keeps this much land between its water and the home's coast, so its beach never meets the
// sea's margin (where it did, a swimmer could step from one beach to the other, round the wall).
constexpr double kLakeSeaGap = 5;
// A pond's radius as a share of its home's, clamped.
constexpr double kPondShare = 0.18;
constexpr double kMinimumPond = 2.5;
constexpr double kMaximumPond = 7;
// The plaza's pond as a share of the plaza.
constexpr double kPlazaPondShare = 0.28;
// Every home starts identical: fixed wheat and wood beside its first pond and a stone clump behind
// it, all unscaled, wheat and wood 1:1.
constexpr int kHomeWheat = 30;
constexpr int kHomeWood = 30;
// Ambient farmland on a home's ground, as percentages of their tiles at 100.
constexpr int kHomeWheatShare = 4;
constexpr int kHomeWoodShare = 2;
// Deposits keep this many steps from every corridor and spoke, so a door never grows shut.
constexpr int kDoorClearance = 6;
// A sand road vertex keeps this many steps from any water.
constexpr int kRoadSeaGap = 5;
// The most the colonies' walks to the plaza may differ.
constexpr int kWalkSpread = 12;
// A tower in a court must reach the next home at this level (0, 1 or 2): level 1 shoots 7 tiles.
constexpr int kSiegeTower = 1;

struct Geometry
{
	int teams = 0, half = 0;
	double wedge = 0, outer = 0;
	double homeR = 0, homeReach = 0, homeRadius = 0; // radius, wobbled reach, centre's distance out
	double corridorHalf = 0, spokeHalf = 0, courtR = 0, wall = 0, plazaR = 0, plazaPondR = 0, pondR = 0;
	double sweep = 0;       // from a home's axis to its court's
	double courtRadius = 0; // the court centre's distance out: the homes' ring
	int ponds = 1;
	bool roads = true;
	std::string failure;
};

// The court sits on the homes' ring, clockwise of its home, where its edge is `wall` tiles from the
// next home's edge; `toward` is how far the next home's outline reaches towards it.
double courtSweep(const Geometry &g, double toward)
{
	const double chord = toward + g.wall + g.courtR;
	if (chord >= 2 * g.homeRadius)
		return -1;
	return g.wedge - 2 * std::asin(chord / (2 * g.homeRadius));
}

Geometry geometryFor(const GenerationRequest &r)
{
	const CarouselOptions o(r);
	Geometry g;
	g.teams = std::max(1, r.nbTeams);
	g.half = std::min(1 << r.wDec, 1 << r.hDec) / 2;
	const double scale = g.half / 128.0;
	g.wedge = 2 * kPi / g.teams;
	// The ring of homes keeps well inside the map, leaving a band of sea round it for the outer farms;
	// smaller maps give the ring more of their room, since homes cannot shrink with them.
	const double ringShare = g.half >= 256 ? kRingShare : g.half >= 128 ? kRingShare + 0.06 : kRingShare + 0.18;
	// A crowded ring grows until every colony has its arc, up to most of the map.
	g.outer = std::min(kMaximumRingShare * g.half,
					   std::max(ringShare * g.half, g.teams * kArcPerColony / (2 * kPi)));
	// A home is a share of the half side, but never wider than its share of the ring: with its
	// centre at outer - reach, a reach of s * wedge * (outer - reach) solves to the bound below.
	const double arcShare = kHomeArcShare * g.wedge;
	const double reach = std::min(std::max(kMinimumHome, kHomeShare * g.half) * (1 + kHomeRoughness),
								  arcShare * g.outer / (1 + arcShare));
	g.homeReach = reach * o.homeSize / 100.0;
	g.homeR = g.homeReach / (1 + kHomeRoughness);
	g.homeRadius = g.outer - g.homeReach;
	g.corridorHalf = o.corridorWidth / 2.0;
	g.spokeHalf = o.spokeWidth / 2.0;
	g.courtR = std::max(kMinimumCourt, kCourtRadius * std::sqrt(scale) * o.courtSize / 100.0);
	g.courtR = std::max(g.courtR, g.corridorHalf + 0.5);
	g.wall = o.courtWall;
	// The plaza is a share of the half side, held clear of the homes' ring.
	g.plazaR = std::min(g.half * o.plazaSize / 100.0,
						g.homeRadius - g.homeReach - g.corridorHalf - 2 * kPieceGap);
	g.plazaPondR = std::max(kMinimumPond, kPlazaPondShare * g.plazaR);
	g.pondR = std::clamp(kPondShare * g.homeR, kMinimumPond, kMaximumPond);
	// No ponds: every home's farms are its water.
	g.ponds = 0;
	g.roads = o.sandRoads;
	g.sweep = courtSweep(g, g.homeReach);
	// A home must still hold its swarm, its kit and its towers: at least what a home with a pond once needed.
	if (g.homeR < g.pondR + kLakeSeaGap + kSwarmRoom)
		g.failure = "Too many colonies for this map: the homes are too small.";
	else if (g.sweep <= 0)
		g.failure = "Too many colonies for this map: the homes do not fit round the ring.";
	else if (g.homeRadius * g.sweep - g.homeReach - g.courtR < kMinimumCorridor)
		g.failure = "Too many colonies for this map: no room for a corridor between a home and its court.";
	else if (g.plazaR - g.plazaPondR < kLakeSeaGap + 3)
		g.failure = "The plaza is too small for its pond on this map.";
	return g;
}

struct Layout
{
	Torus t{1, 1};
	Geometry g;
	double phase = 0, cx = 0, cy = 0, sweep = 0;
	// Per tile: which colony's home, court or path (corridor and spoke) it belongs to, -1 for none.
	std::vector<int> homeOf, courtOf, pathOf;
	// farmOf: the colony a farm tile belongs to; farms: every farm laid out, two per colony.
	std::vector<int> farmOf, farmSet; // farmSet: 0 an inner farm, 1 an outer one
	std::vector<Farm> farms;
	std::vector<unsigned char> farmSand, farmPlot; // the farms' building plots: sand rings, grass
	std::vector<int> farmColony;
	// Each tile's side for the walls between them: the plaza (0), a colony's court and lanes (1 + colony),
	// its home and farms (1 + teams + colony); -1 for the court walls and the sea. homeSteps: steps
	// from the homes as designed, before the gaps filled in, which is where a home's door stands.
	std::vector<int> side, homeSteps;
	std::vector<unsigned char> plaza, land, pond, wall, road, roadTile;
	std::vector<double> axis;               // every colony's home axis angle
	std::vector<ShapePoint> homes, courts;  // their centres
	std::vector<ShapePoint> heartOf;        // where each colony's spoke comes into the plaza
	std::vector<ShapePoint> firstPond;      // each home's first pond's centre
	std::string failure;
};

// Whether the border between tile i and its lower-sided neighbour j stays open: a lane into the plaza,
// or a home's door onto its own corridor where the corridor leaves the home.
bool borderOpen(const Layout &L, int i, int j)
{
	const int teams = L.g.teams, a = L.side[i], b = L.side[j];
	if (b == 0 && a >= 1 && a <= teams)
		return true;
	return a == b + teams && b >= 1 && L.pathOf[j] >= 0 && L.homeSteps[j] >= 0 && L.homeSteps[j] <= 2;
}

Layout design(const GenerationRequest &request, GenerationContext &context)
{
	const CarouselOptions o(request);
	Layout L;
	L.t = Torus(1 << request.wDec, 1 << request.hDec);
	const Torus &t = L.t;
	const int n = t.size();
	L.g = geometryFor(request);
	const Geometry &g = L.g;
	if (!g.failure.empty())
	{
		L.failure = g.failure;
		return L;
	}
	const int teams = g.teams;
	L.cx = t.w / 2;
	L.cy = t.h / 2;
	L.phase = context.bounded("carousel-phase", 3600) / 3600.0 * 2 * kPi;
	L.homeOf.assign(n, -1);
	L.courtOf.assign(n, -1);
	L.pathOf.assign(n, -1);
	L.plaza.assign(n, 0);
	L.land.assign(n, 0);
	L.pond.assign(n, 0);
	L.wall.assign(n, 0);
	L.road.assign(n, 0);
	L.roadTile.assign(n, 0);

	// One home outline for every colony, turned to each home's axis.
	const RadialShape homeShape(g.homeR, kHomeRoughness, context, "carousel-home");
	const RadialShape pondShape(g.pondR, 0.2, context, "carousel-pond");
	const RadialShape courtShape(g.courtR, 0.0001, context, "carousel-court");
	const RadialShape plazaShape(g.plazaR, 0.0001, context, "carousel-plaza");
	const RadialShape plazaPondShape(g.plazaPondR, 0.0001, context, "carousel-plaza-pond");

	// The court's exact place: its edge `wall` tiles from the next home's outline where that
	// outline faces it. The outline is the same shape turned, so this is the same for every colony.
	double sweep = g.sweep;
	for (int pass = 0; pass < 3; ++pass)
	{
		const ShapePoint home = polarPoint(0, 0, g.homeRadius, g.wedge);
		const ShapePoint court = polarPoint(0, 0, g.homeRadius, sweep);
		const double toward =
			homeShape.radiusAt(std::atan2(court.y - home.y, court.x - home.x) - g.wedge);
		const double next = courtSweep(g, toward);
		if (next <= 0)
			break;
		sweep = next;
	}

	L.sweep = sweep;
	std::vector<std::vector<StrokePoint>> paths;
	for (int k = 0; k < teams; ++k)
	{
		const double a = L.phase + g.wedge * k, c = a + sweep;
		L.axis.push_back(a);
		L.homes.push_back(polarPoint(L.cx, L.cy, g.homeRadius, a));
		L.courts.push_back(polarPoint(L.cx, L.cy, g.homeRadius, c));
		// The corridor follows the homes' ring from the home's axis to its court, and the spoke runs
		// in from the court to inside the plaza's edge at the middle of the wedge, leaning back
		// towards its own home and away from the next, which the court is pressed against.
		const double entry = std::min(c, a + g.wedge / 2);
		L.heartOf.push_back(polarPoint(L.cx, L.cy, g.plazaR - 4, entry));
		std::vector<StrokePoint> corridor = arcPath(L.cx, L.cy, g.homeRadius, a, c, g.corridorHalf);
		const ShapePoint in = polarPoint(L.cx, L.cy, g.plazaR - g.spokeHalf, entry);
		std::vector<StrokePoint> spoke = {{L.courts[k].x, L.courts[k].y, g.spokeHalf},
										  {in.x, in.y, g.spokeHalf}};
		std::vector<unsigned char> mask(n, 0);
		strokePath(mask, t, corridor);
		strokePath(mask, t, spoke);
		for (int i = 0; i < n; ++i)
			if (mask[i])
				L.pathOf[i] = k;
		paths.push_back(corridor);
		paths.push_back(spoke);
		if (g.roads)
		{
			// The road runs down the middle of the corridor, through the court and down the spoke, then on
			// across the plaza to its pond, so the way in to the middle never overgrows; the home's interior
			// is left to build on.
			const double inset = (g.homeReach + 1) / g.homeRadius;
			std::vector<StrokePoint> line = arcPath(L.cx, L.cy, g.homeRadius, a + inset, c, 0);
			tracePath(L.road, t, line);
			const ShapePoint pond = polarPoint(L.cx, L.cy, g.plazaPondR, entry);
			tracePath(L.road, t, {{L.courts[k].x, L.courts[k].y, 0}, {in.x, in.y, 0}, {pond.x, pond.y, 0}});
		}
	}

	// The shapes: homes with their ponds, courts, the plaza with its pond, and the wall between each
	// court and the next home.
	for (int k = 0; k < teams; ++k)
	{
		L.firstPond.push_back(stampPondHome(t, L.homes[k], L.axis[k], homeShape, pondShape,
											{g.homeR, g.pondR, kLakeSeaGap, g.ponds}, L.pond,
											[&](int i)
											{
												L.homeOf[i] = k;
												L.pathOf[i] = -1;
											}));
		forEachTileInShape(t, L.courts[k].x, L.courts[k].y, courtShape, 0,
						   [&](int i, double, double)
						   {
							   L.courtOf[i] = k;
							   L.pathOf[i] = -1;
						   });
	}
	// The walls, once every home and court is down: a thin line of stone between each court and the next
	// home, just the tiles no more than `court-wall` steps from both - the land where they face each
	// other. The band only bounds where to look, on the home's side of the court's
	// middle and never over the court's own corridor or spoke.
	for (int k = 0; k < teams; ++k)
	{
		const ShapePoint next = L.homes[(k + 1) % teams];
		std::vector<unsigned char> band(n, 0), court(n, 0), home(n, 0);
		strokePath(band, t, {{L.courts[k].x, L.courts[k].y, g.courtR * kWallBand}, {next.x, next.y, g.courtR * kWallBand}});
		for (int i = 0; i < n; ++i)
		{
			court[i] = L.courtOf[i] == k;
			home[i] = L.homeOf[i] == (k + 1) % teams;
		}
		const std::vector<int> fromCourt = stepsFrom(t, court), fromHome = stepsFrom(t, home);
		const double ax = next.x - L.courts[k].x, ay = next.y - L.courts[k].y;
		for (int i = 0; i < n; ++i)
			if (band[i] && L.courtOf[i] < 0 && L.homeOf[i] < 0 && L.pathOf[i] != k &&
				fromCourt[i] <= g.wall && fromHome[i] <= g.wall)
			{
				const double along =
					t.offsetX(int(std::lround(L.courts[k].x)), i % t.w) * ax +
					t.offsetY(int(std::lround(L.courts[k].y)), i / t.w) * ay;
				if (along <= 0)
					continue;
				L.wall[i] = 1;
				L.pathOf[i] = -1;
			}
	}
	fillShape(L.plaza, t, L.cx, L.cy, plazaShape);
	fillShape(L.pond, t, L.cx, L.cy, plazaPondShape);
	for (int i = 0; i < n; ++i)
	{
		if (L.plaza[i])
			L.pathOf[i] = -1;
		L.land[i] = L.homeOf[i] >= 0 || L.courtOf[i] >= 0 || L.pathOf[i] >= 0 || L.plaza[i] || L.wall[i];
		if (L.pond[i])
			L.road[i] = 0;
		// Roads only on the lanes, courts and plaza, never inside a home.
		if (L.road[i] && L.pathOf[i] < 0 && L.courtOf[i] < 0 && !L.plaza[i])
			L.road[i] = 0;
	}

	// The pieces that must not join keep water between them: every colony's corridor and spoke stay
	// clear of every other home, and of the next colony's corridor and spoke outside the plaza.
	{
		std::vector<int> nearestHome(n, -1), homeSteps(n, -1), pathSteps(n, -1);
		for (int k = 0; k < teams; ++k)
		{
			std::vector<unsigned char> home(n, 0), path(n, 0);
			for (int i = 0; i < n; ++i)
			{
				home[i] = L.homeOf[i] == k;
				path[i] = L.pathOf[i] == (k + 1) % teams;
			}
			const std::vector<int> fromHome = stepsFrom(t, home);
			const std::vector<int> fromPath = stepsFrom(t, path);
			for (int i = 0; i < n; ++i)
			{
				if (L.pathOf[i] < 0)
					continue;
				// Beside its own court a path is the court's mouth, which the wall parts from the home.
				const int own = L.pathOf[i];
				const bool mouth = std::hypot(t.offsetX(int(std::lround(L.courts[own].x)), i % t.w),
											  t.offsetY(int(std::lround(L.courts[own].y)), i / t.w)) <
								   g.courtR + g.corridorHalf;
				if (own != k && !mouth && fromHome[i] < kLaneHomeGap)
				{
					L.failure = "Too many colonies for this map: a spoke or corridor passes too close to a home.";
					return L;
				}
				const double d = std::hypot(i % t.w - L.cx, i / t.w - L.cy);
				if (teams > 1 && L.pathOf[i] == k && L.pathOf[i] != (k + 1) % teams &&
					fromPath[i] < kPieceGap && d > g.plazaR + kPieceGap)
				{
					L.failure = "Too many colonies for this map: the spokes crowd each other.";
					return L;
				}
			}
		}
	}
	// The wall must part the court from the home: at least one tile of it between them everywhere.
	for (int k = 0; k < teams; ++k)
	{
		std::vector<unsigned char> next(n, 0);
		for (int i = 0; i < n; ++i)
			next[i] = L.homeOf[i] == (k + 1) % teams;
		const std::vector<int> steps = stepsFrom(t, next);
		for (int i = 0; i < n; ++i)
			if (L.courtOf[i] == k && steps[i] < 2)
			{
				L.failure = "A court touches the next home; make the court wall thicker.";
				return L;
			}
	}
	std::vector<int> farmSets;             // the sets of farms laid: 0 inner, 1 outer
	std::vector<ShapePoint> farmOrigins;   // per laid set, every colony's row origin
	// The farms (growFarmFields): every colony's home reaches one farm in towards the plaza, filling the
	// sea between its corridor and the spokes either side, and one out beyond the ring, sharing the
	// sea round the ring equally with the others. Rows run along the colony's axis at the widths that
	// yield most (bestFarmRows); every farm's coast is sealed like the rest.
	{
		std::vector<unsigned char> inner(n, 0), outer(n, 0);
		for (int i = 0; i < n; ++i)
		{
			const double d = std::hypot(t.offsetX(int(L.cx), i % t.w), t.offsetY(int(L.cy), i / t.w));
			inner[i] = d < g.homeRadius;
			outer[i] = d >= g.homeRadius;
		}
		std::vector<ShapePoint> innerSeeds, outerSeeds, anchors;
		for (int k = 0; k < teams; ++k)
		{
			innerSeeds.push_back(polarPoint(L.cx, L.cy, g.homeRadius - g.homeReach - 2, L.axis[k]));
			outerSeeds.push_back(polarPoint(L.cx, L.cy, g.homeRadius + g.homeReach + 2, L.axis[k]));
			anchors.push_back(L.homes[k]);
		}
		std::vector<int> owners;
		for (int k = 0; k < teams; ++k)
			owners.push_back(k);
		// Rows run along every colony's axis; a colony whose axis falls on the diagonal gets more
		// ground, so every colony's farms yield alike (farmYield).
		const std::vector<int> innerFields =
			growFarmFields(t, L.land, L.homeOf, inner, innerSeeds, owners, anchors, kFarmGap, kNeckHalf, L.axis);
		const std::vector<int> outerFields =
			growFarmFields(t, L.land, L.homeOf, outer, outerSeeds, owners, anchors, kFarmGap, kNeckHalf, L.axis);
		L.farmOf.assign(n, -1);
		L.farmSet.assign(n, -1);
		L.farmSand.assign(n, 0);
		L.farmPlot.assign(n, 0);
		// Each set of farms, inner and outer, is laid only where every colony's field has room, so a
		// small or crowded map loses a set for everyone rather than favouring anyone.
		const double homeArea = kPi * g.homeR * g.homeR;
		for (int set = 0; set < 2; ++set)
		{
			const std::vector<int> &fields = set == 0 ? innerFields : outerFields;
			std::vector<int> size(teams, 0);
			for (int i = 0; i < n; ++i)
				if (fields[i] >= 0)
					++size[fields[i]];
			if (*std::min_element(size.begin(), size.end()) < kMinimumFarmShare * homeArea)
				continue;
			// The rows are laid once the gaps round the farm have filled in, over the whole walled field.
			farmSets.push_back(set);
			for (int i = 0; i < n; ++i)
				if (fields[i] >= 0 && L.homeOf[i] < 0)
				{
					L.farmOf[i] = fields[i];
					L.farmSet[i] = set;
					L.land[i] = 1;
				}
			for (int k = 0; k < teams; ++k)
				farmOrigins.push_back(set == 0 ? innerSeeds[k] : outerSeeds[k]);
		}
	}
	// No water between the pieces: the sea between a home, its farms, the lanes, the courts and the plaza
	// fills in from the homes and farms (fillToNearest), so the lanes, courts and plaza keep exactly the
	// width they were drawn and every gap beside them becomes its colony's ground. A single line of stone
	// then stands where two sides meet (labelBorders), on the home's or farm's side, so no wall narrows a
	// lane and a tower behind it stands as close to what it shoots at as the wall allows. Sea too far
	// from any home or farm keeps a thin margin of the nearest piece and its coast is sealed. Two borders
	// stay open: a home's door onto its own corridor and every spoke's way into the plaza.
	{
		// Farm labels: one per colony per set of farms (inner, outer).
		const int homeLabel = 0, farmLabel = teams, courtLabel = 3 * teams, pathLabel = 4 * teams, plazaLabel = 5 * teams;
		std::vector<int> piece(n, -1), grown(n, -1);
		std::vector<unsigned char> sea(n, 0), home(n, 0);
		for (int i = 0; i < n; ++i)
		{
			piece[i] = L.wall[i]		   ? -1
					   : L.homeOf[i] >= 0  ? homeLabel + L.homeOf[i]
					   : L.farmOf[i] >= 0  ? farmLabel + L.farmSet[i] * teams + L.farmOf[i]
					   : L.courtOf[i] >= 0 ? courtLabel + L.courtOf[i]
					   : L.pathOf[i] >= 0  ? pathLabel + L.pathOf[i]
					   : L.plaza[i]		   ? plazaLabel
										   : -1;
			grown[i] = piece[i] < courtLabel ? piece[i] : -1;
			sea[i] = !L.land[i];
			home[i] = L.homeOf[i] >= 0;
		}
		std::vector<unsigned char> filled = fillToNearest(t, grown, sea, kFillReach);
		for (int i = 0; i < n; ++i)
		{
			if (filled[i])
				piece[i] = grown[i];
			sea[i] = sea[i] && !filled[i];
		}
		// What sea is left keeps a margin of land beside every piece, so its beach and coast wall never
		// eat into a lane.
		const std::vector<unsigned char> margin = fillToNearest(t, piece, sea, kSeaMargin);
		for (int i = 0; i < n; ++i)
			filled[i] = filled[i] || margin[i];
		L.homeSteps = stepsFrom(t, home);
		for (int i = 0; i < n; ++i)
		{
			if (!filled[i])
				continue;
			const int p = piece[i];
			L.land[i] = 1;
			if (p >= plazaLabel)
				L.plaza[i] = 1;
			else if (p >= pathLabel)
				L.pathOf[i] = p - pathLabel;
			else if (p >= courtLabel)
				L.courtOf[i] = p - courtLabel;
			else if (p >= farmLabel)
			{
				L.farmOf[i] = (p - farmLabel) % teams;
				L.farmSet[i] = (p - farmLabel) / teams;
			}
			else
				L.homeOf[i] = p;
		}
		// The sides the walls go between, numbered so each wall stands on the higher: the plaza (0), a
		// colony's court and lanes (1 + colony), and its home and farms (1 + teams + colony).
		std::vector<int> &side = L.side;
		side.assign(n, -1);
		for (int i = 0; i < n; ++i)
			side[i] = piece[i] < 0			  ? -1
					  : piece[i] >= plazaLabel ? 0
					  : piece[i] >= courtLabel ? 1 + (piece[i] - courtLabel) % teams
											   : 1 + teams + piece[i] % teams;
		const std::vector<unsigned char> borders =
			labelBorders(t, side, [&](int i, int j) { return borderOpen(L, i, j); });
		for (int i = 0; i < n; ++i)
			if (borders[i])
				L.wall[i] = 1;
	}
	// The farms' rows (layFarm), over each whole field inside its walls, along the colony's axis at the
	// widths that yield most (bestFarmRows), with just enough land round the water for a beach and a wall.
	{
		const FarmPlot plot;
		// Against the open sea a farm keeps a wider margin, for the beach, the sealed coast and the land
		// between them and the water rows.
		std::vector<unsigned char> openSea(n, 0);
		for (int i = 0; i < n; ++i)
			openSea[i] = !L.land[i];
		const std::vector<int> fromSea = stepsFrom(t, openSea);
		for (size_t s = 0; s < farmSets.size(); ++s)
			for (int k = 0; k < teams; ++k)
			{
				std::vector<unsigned char> region(n, 0);
				for (int i = 0; i < n; ++i)
					region[i] = L.farmOf[i] == k && L.farmSet[i] == farmSets[s] && !L.wall[i] &&
								(fromSea[i] < 0 || fromSea[i] > kFarmSeaRim);
				TerrainSketch rows(n, GRASS);
				L.farms.push_back(layFarm(rows, t, region, L.axis[k], farmOrigins[s * teams + k], kFarmRim,
										  bestFarmRows(L.axis[k]), o.farmPlots ? &plot : nullptr, kFarmBridgeSpacing));
				L.farmColony.push_back(k);
				const Farm &farm = L.farms.back();
				for (int i = 0; i < n; ++i)
				{
					if (farm.water[i])
						L.pond[i] = 1;
					if (farm.sand[i])
						L.farmSand[i] = 1;
					if (farm.plot[i])
						L.farmPlot[i] = 1;
				}
			}
	}
	// A road keeps well inside the land (keepRoadInland), so its sand never meets a beach, and no road
	// vertex touches a wall tile, since stone only stands on grass.
	{
		std::vector<unsigned char> water(n, 0);
		for (int i = 0; i < n; ++i)
			water[i] = !L.land[i] || L.pond[i];
		keepRoadInland(L.road, t, water, kRoadSeaGap);
		for (int i = 0; i < n; ++i)
			for (int dy = -1; dy <= 0 && L.road[i]; ++dy)
				for (int dx = -1; dx <= 0; ++dx)
					if (L.wall[t.at(i % t.w + dx, i / t.w + dy)])
						L.road[i] = 0;
	}
	L.roadTile = roadTiles(t, L.road);
	return L;
}

// The sea: every water vertex but the ponds'.
std::vector<unsigned char> seaMargin(const Map &map, const Layout &L)
{
	// Neither a sand road nor a farm plot's sand ring is beach: neither may carry the sea's margin inland.
	std::vector<unsigned char> notBeach = L.roadTile;
	const std::vector<unsigned char> plotSand = roadTiles(L.t, L.farmSand);
	for (int i = 0; i < L.t.size(); ++i)
		notBeach[i] = notBeach[i] || plotSand[i];
	return MapGeneration::seaMargin(map, L.t, seaVertices(map, L.t, L.pond), notBeach);
}

// The design's stone, once the terrain is laid: every coast sealed, and the wall between every court
// and the next home.
std::vector<unsigned char> stoneTiles(const Map &map, const Layout &L)
{
	std::vector<unsigned char> stone = sealCoasts(map, L.t, seaMargin(map, L), L.land);
	const DesignedStone wall = designedStone(map, L.t, L.wall);
	for (int i = 0; i < L.t.size(); ++i)
		if (wall.stone[i])
			stone[i] = 1;
	return stone;
}

void furnish(Map &map, const Layout &L, GenerationContext &context, const CarouselOptions &o,
			 const std::vector<unsigned char> &pads)
{
	const Torus &t = L.t;
	const Geometry &g = L.g;
	const int n = t.size();
	const std::vector<unsigned char> reserved = swarmSurroundings(t, context);
	const Fertility::Field fertility = Fertility::forMap(map, false);
	// The wedge frame starts half a wedge before colony 0's axis, so a wedge's middle is its home.
	const WedgeFrame wedges(t, L.phase - g.wedge / 2, g.teams);
	const WedgeField patch{wedges, PeriodicNoise(t.w, t.h, 12, context.stream("carousel-patch"))};
	const WedgeField split{wedges, PeriodicNoise(t.w, t.h, 6, context.stream("carousel-split"))};
	// Nothing is planted within a few steps of a corridor or spoke, so no field grows across a door.
	std::vector<unsigned char> paths(n, 0);
	for (int i = 0; i < n; ++i)
		paths[i] = L.pathOf[i] >= 0;
	const std::vector<int> fromPaths = stepsFrom(t, paths);
	const auto free = [&](int i)
	{
		return !L.road[i] && !L.roadTile[i] && !reserved[i] && !pads[i] && fromPaths[i] > kDoorClearance &&
			   clearGround(map, i % t.w, i / t.w);
	};
	for (int k = 0; k < g.teams; ++k)
	{
		const auto eligible = [&](int i) { return L.homeOf[i] == k && free(i); };
		plantPondHomeKit(map, t, context, L.firstPond[k], L.axis[k], g.pondR, kHomeWheat, kHomeWood,
						 eligible);
		furnishGround(
			map, t, context, fertility, eligible, patch, split,
			[&](int area)
			{
				return GroundAmounts{int(scaledCount(area * kHomeWheatShare / 100, o.wheat)),
										 int(scaledCount(area * kHomeWoodShare / 100, o.wood)), 0, 0};
			},
			"carousel-home-stone", "carousel-home-fruit");
	}
	// The courts: one grove of the same fruit each, out on the court's far side from the plaza, clear of
	// where its tower stands against the wall.
	const int courtFruit = CHERRY + int(context.bounded("carousel-court", 3));
	if (scaledCount(1, o.fruit) > 0)
		for (int k = 0; k < g.teams; ++k)
			plantRound(map, t, context, L.courts[k].x, L.courts[k].y, g.courtR * 0.45, {L.axis[k]}, courtFruit,
					   1, 4, [&](int i) { return L.courtOf[i] == k && free(i); });
	// The plaza: only fruit, so nothing overgrows its groves - in every wedge the orchard's three groves
	// on the pond's shore, halfway between one spoke's arrival and the next.
	const auto plazaFree = [&](int i) { return L.plaza[i] && free(i); };
	std::vector<double> between;
	for (int k = 0; k < g.teams; ++k)
		between.push_back(std::min(L.axis[k] + g.wedge / 2, L.axis[k] + L.sweep) + g.wedge / 2);
	if (scaledCount(1, o.fruit) > 0)
		plantOrchard(map, t, context, L.cx, L.cy, g.plazaPondR + 4, between, 5, 5, 1, plazaFree);
	// The farms: wheat along the water on every crop row and a small woodlot on one (plantFarm).
	for (size_t f = 0; f < L.farms.size(); ++f)
	{
		const Farm &farm = L.farms[f];
		int crops = 0;
		for (int i = 0; i < n; ++i)
			crops += farm.row[i] >= 0 && farm.row[i] % 2 == 0;
		const int k = L.farmColony[f];
		plantFarm(map, t, farm, int(scaledCount(crops * kFarmWheatShare / 100, o.wheat)),
				  int(scaledCount(crops * kFarmWoodShare / 100, o.wood)),
				  [&](int i) { return (L.farmOf[i] == k || L.homeOf[i] == k) && free(i); });
	}
}

// The towers every colony starts with and the open pads beside them (chooseTowerSites): the carousel's
// siege line. Every colony's sites stand in its own home, packed against the thin wall its home shares
// with the previous colony's court, and are chosen for how much of that colony's elbow - its court and
// the lanes within kElbowReach of it - they cover across the wall. So every colony shells one
// neighbour's way out while the other neighbour shells its own. With the control at 0 every site is an
// open pad.
TowerPlan planTowers(const Map &map, const Layout &L, const GenerationContext &context,
					 const CarouselOptions &o, const std::vector<unsigned char> &stone)
{
	const Torus &t = L.t;
	const Geometry &g = L.g;
	const int n = t.size();
	const std::vector<int> fromWall = stepsFrom(t, L.wall);
	const std::vector<unsigned char> reserved = swarmSurroundings(t, context);
	std::vector<int> owner(n, -1);
	std::vector<unsigned char> buildable(n, 0), target(n, 0);
	for (int i = 0; i < n; ++i)
	{
		const int x = i % t.w, y = i / t.w;
		owner[i] = L.homeOf[i] >= 0	   ? L.homeOf[i]
				   : L.courtOf[i] >= 0 ? L.courtOf[i]
				   : L.farmOf[i] >= 0  ? L.farmOf[i]
									   : L.pathOf[i];
		buildable[i] = L.homeOf[i] >= 0 && fromWall[i] >= 0 && fromWall[i] <= kSiegeLine && map.isGrass(x, y) &&
					   !stone[i] && !L.roadTile[i] && !reserved[i] && !map.isResource(x, y);
		// The elbows: every court and the lanes near it.
		const int k = L.courtOf[i] >= 0 ? L.courtOf[i] : L.pathOf[i];
		target[i] = k >= 0 && !map.isWater(x, y) && !stone[i] &&
					std::hypot(t.offsetX(int(std::lround(L.courts[k].x)), x),
							   t.offsetY(int(std::lround(L.courts[k].y)), y)) <= g.courtR + kElbowReach;
	}
	TowerRequest request;
	request.range = kTowerRange[std::clamp(o.towers - 1, 0, 2)];
	request.towers = o.towers > 0 ? o.towerCount : 0;
	request.pads = o.towers > 0 ? kTowerPads : o.towerCount + kTowerPads;
	request.spacing = kTowerSpacing;
	request.otherWeight = 1;
	request.ownWeight = 0;
	request.against = &stone;
	return chooseTowerSites(t, owner, buildable, target, swarmSurroundings(t, context, 0), g.teams, request);
}

bool generate(Game &game, GenerationContext &context)
{
	context.stage = "carousel layout";
	const CarouselOptions o(context.request);
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

	context.stage = "carousel terrain";
	TerrainSketch terrain(n, WATER);
	for (int i = 0; i < n; ++i)
	{
		if (L.land[i])
			terrain[i] = L.road[i] || L.farmSand[i] ? SAND : GRASS;
		if (L.pond[i])
			terrain[i] = WATER;
	}
	layBeaches(terrain, t);
	writeUndermap(map, terrain);
	const std::vector<unsigned char> stone = stoneTiles(map, L);
	for (int i = 0; i < n; ++i)
		if (stone[i])
			map.setResource(i % t.w, i / t.w, STONE, 1);

	context.stage = "carousel colonies";
	const auto home = [&](int team)
	{
		std::vector<unsigned char> ground(n, 0);
		for (int i = 0; i < n; ++i)
			ground[i] = L.homeOf[i] == team && !stone[i] && !L.roadTile[i] &&
						map.isGrass(i % t.w, i / t.w);
		return ground;
	};
	// The swarm stands in from the home's middle towards the plaza, with the ponds behind it.
	const auto anchor = [&](int team) { return pondHomeAnchor(L.homes[team], L.axis[team], L.g.homeR); };
	if (!settleColonies(game, context, "carousel-starts", home, anchor))
		return false;

	context.stage = "carousel towers";
	TowerPlan towers = planTowers(map, L, context, o, stone);
	// No site may close a colony's way through its court to the plaza (dropBlockingSites); a crowded map
	// may not seat every tower either, so every colony keeps as many as the fewest got, as long as every
	// colony has one.
	{
		std::vector<unsigned char> open(n, 0), plazaHeart(n, 0);
		for (int i = 0; i < n; ++i)
		{
			const int x = i % t.w, y = i / t.w;
			open[i] = !map.isWater(x, y) && !stone[i] && map.getBuilding(x, y) == NOGBID;
			plazaHeart[i] = L.plaza[i] && open[i] && std::hypot(i % t.w - L.cx, i / t.w - L.cy) < L.g.plazaPondR + 3;
		}
		std::vector<std::vector<int>> sources(teams);
		const std::vector<std::vector<unsigned char>> goals(teams, plazaHeart);
		for (int k = 0; k < teams; ++k)
			for (int dy = -1; dy <= 4; ++dy)
				for (int dx = -1; dx <= 4; ++dx)
					if (dx < 0 || dy < 0 || dx > 3 || dy > 3)
						sources[k].push_back(t.at(context.bootX[k] + dx, context.bootY[k] + dy));
		dropBlockingSites(t, towers, open, sources, goals);
	}
	if (evenTowerPlan(towers) < (o.towers > 0 && o.towerCount > 0 ? 1 : 0))
	{
		context.detail = "a colony has no room for its towers";
		return false;
	}
	if (!raiseTowers(game, towers, std::clamp(o.towers - 1, 0, 2)))
	{
		context.detail = "a tower site no longer fits";
		return false;
	}
	const std::vector<unsigned char> pads = towerFootprints(t, towers);

	context.stage = "carousel resources";
	furnish(map, L, context, o, pads);
	const WedgeFrame algaeWedges(t, L.phase - L.g.wedge / 2, L.g.teams);
	seedAlgae(map, context, t, "carousel-algae", o.algae, AlgaeBand::shallows(1, 4), &algaeWedges);
	secureStartingCrops(game, context, t, 24, 32, 0, &stone);
	reopenCrampedStarts(game, context, {o.wheat, o.wood, o.stone, o.algae, o.fruit}, 24, 32, 0, &stone);
	clearFarmPlots(map, t, L.farms);

	// Keep every colony's walk open, clearing only deposits on it: home to court, court to plaza.
	context.stage = "carousel roads";
	const std::vector<std::vector<int>> workers = unitTilesByTeam(map, teams);
	// The walk from a court ends at the orchard round the plaza's pond, not at the plaza's edge.
	std::vector<unsigned char> plaza(n, 0);
	for (int i = 0; i < n; ++i)
		plaza[i] = L.plaza[i] && !map.isWater(i % t.w, i / t.w) && !stone[i] &&
				   std::hypot(i % t.w - L.cx, i / t.w - L.cy) < L.g.plazaPondR + 3;
	for (int team = 0; team < teams; ++team)
	{
		if (workers[team].empty())
			continue;
		std::vector<unsigned char> court(n, 0);
		std::vector<int> courtTiles;
		for (int i = 0; i < n; ++i)
			// The court's own coast wall is neither where a walk starts nor where it ends, or the
			// walk would clear it.
			if (L.courtOf[i] == team && !stone[i] && !map.isWater(i % t.w, i / t.w))
			{
				court[i] = 1;
				courtTiles.push_back(i);
			}
		if (!openRoad(map, t, workers[team], court, &stone) ||
			!openRoad(map, t, courtTiles, plaza, &stone))
		{
			context.detail = "colony " + std::to_string(team) + " has no walk to its court and the plaza";
			return false;
		}
		}
	return true;
}

std::string validateRequest(const GenerationRequest &r)
{
	if (r.nbTeams < 2)
		return "Carousels need at least two colonies.";
	// The geometry's own reason stays in the candidate's diagnostics; the player sees one message
	// that says what to change.
	return geometryFor(r).failure.empty() ? "" : "The carousel does not fit this map; use a bigger map, smaller homes or fewer colonies.";
}

// Checked on the finished world against the rebuilt design: every designed stone stands; every
// colony walks to colony 0; nothing landing from the sea gets into any home, court, path or the
// plaza; with every corridor and spoke shut, no home, court or the plaza reaches any other (the
// court walls hold and every home has a single door); a tower in every court reaches the next home;
// and the colonies' walks to the plaza are even.
std::string validateWorld(const Game &game, const GenerationContext &context)
{
	GenerationContext replay(context.request);
	const Layout L = design(context.request, replay);
	const Map &map = game.map;
	if (const std::string mismatch = designMismatch(L, map, "carousel"); !mismatch.empty())
		return mismatch;
	const Torus &t = L.t;
	const int n = t.size(), teams = context.request.nbTeams;
	const auto where = [&](int i)
	{ return "(" + std::to_string(i % t.w) + ", " + std::to_string(i / t.w) + ")"; };
	const std::vector<unsigned char> stone = stoneTiles(map, L);
	for (int i = 0; i < n; ++i)
		if (stone[i] && map.getResource(i % t.w, i / t.w).type != STONE)
			return "The stone at " + where(i) + " is missing.";
	const ColonyWalk walk = walkFromFirstColony(map, teams, "the carousel", "");
	if (!walk.error.empty())
		return walk.error;

	const std::vector<unsigned char> open = walkableTiles(map);
	const std::vector<unsigned char> margin = seaMargin(map, L);
	if (const int entry = seaEntry(map, t, margin, L.land); entry >= 0)
		return "Land at " + where(entry) + " can be reached from the sea.";

	// Every piece on its own with the corridors and spokes shut. A piece is its ground inside the
	// coast wall: the sand lane outside it is the sea's, and already checked above.
	std::vector<int> piece(n, -1);
	for (int i = 0; i < n; ++i)
		if (!margin[i])
			piece[i] = L.homeOf[i] >= 0	   ? L.homeOf[i]
					   : L.farmOf[i] >= 0  ? L.farmOf[i]
					   : L.courtOf[i] >= 0 ? teams + L.courtOf[i]
					   : L.plaza[i]		   ? 2 * teams
										   : -1;
	std::vector<unsigned char> paths(n, 0);
	for (int i = 0; i < n; ++i)
		paths[i] = L.pathOf[i] >= 0;
	// Every farm is joined to its home: nearly all of its crop land, and all of its plot, is a walk from
	// its colony's workers.
	for (size_t f = 0; f < L.farms.size(); ++f)
		if (farmReachable(map, t, L.farms[f], walk.workers[L.farmColony[f]]) < kFarmReach)
			return "Colony " + std::to_string(L.farmColony[f]) + " cannot walk into one of its farms.";
	// Every border the design walls is stone in the finished world: no two sides meet on open ground but
	// at a door or a lane's way into the plaza.
	for (int y = 0; y < t.h; ++y)
		for (int x = 0; x < t.w; ++x)
		{
			const int i = y * t.w + x;
			if (L.side[i] < 0 || stone[i] || map.isWater(x, y))
				continue;
			for (int dy = -1; dy <= 1; ++dy)
				for (int dx = -1; dx <= 1; ++dx)
				{
					const int j = t.at(x + dx, y + dy);
					if (L.side[j] >= 0 && L.side[j] < L.side[i] && !stone[j] && !map.isWater(j % t.w, j / t.w) &&
						!margin[i] && !margin[j] && !borderOpen(L, i, j))
						return "The wall between two parts of the carousel has a gap at " + where(i) + ".";
				}
		}
	if (const int leak = pieceLeak(map, t, piece, paths); leak >= 0)
		return "With the corridors shut, " + where(leak) + " can still be reached from another part of the carousel.";

	// The siege line: from the ground of every home next to the wall, a tower reaches the previous
	// colony's court across it.
	for (int k = 0; k < teams; ++k)
	{
		std::vector<unsigned char> court(n, 0), next(n, 0);
		for (int i = 0; i < n; ++i)
		{
			const int x = i % t.w, y = i / t.w;
			court[i] = L.courtOf[i] == k && !map.isWater(x, y) && !stone[i];
			next[i] = L.homeOf[i] == (k + 1) % teams && map.isGrass(x, y) && !stone[i] && !map.isResource(x, y);
		}
		const int reach = towerReach(t, next, court);
		if (reach > kTowerRange[kSiegeTower])
			return "No tower in home " + std::to_string((k + 1) % teams) + " reaches court " + std::to_string(k) +
				   " across the wall; it is " + std::to_string(reach) + " tiles away.";
	}

	std::vector<int> targets;
	for (int k = 0; k < teams; ++k)
	{
		// The walkable tile nearest where the colony's spoke comes into the plaza.
		const int hx = int(std::lround(L.heartOf[k].x)), hy = int(std::lround(L.heartOf[k].y));
		const int seed = seedNear(t, hx, hy, 3, [&](int i) { return open[i] && !margin[i]; });
		targets.push_back(seed >= 0 ? seed : t.at(hx, hy));
	}
	const WalkSpread spread = walkSpread(map, t, walk.workers, targets);
	if (spread.unreached >= 0)
		return "Colony " + std::to_string(spread.unreached) + " cannot walk to the plaza.";
	if (spread.tooUneven(kWalkSpread))
		return "The colonies' walks to the plaza differ by " +
			   std::to_string(spread.longest - spread.shortest) + " steps.";
	return "";
}
} // namespace

CarouselOptions::CarouselOptions(const GenerationRequest &r)
	: homeSize(r.option("home-size")), corridorWidth(r.option("corridor-width")),
	  spokeWidth(r.option("spoke-width")),
	  courtSize(r.option("court-size")), courtWall(r.option("court-wall")),
	  plazaSize(r.option("plaza-size")), 
	  towers(r.option("starting-towers")), towerCount(r.option("tower-count")),
	  sandRoads(r.option("sand-roads") != 0), farmPlots(r.option("farm-plots") != 0), wheat(r.option("wheat-amount")),
	  wood(r.option("wood-amount")), stone(r.option("stone-amount")),
	  algae(r.option("algae-amount")), fruit(r.option("fruit-amount"))
{
}

GeneratorDefinition carouselDefinition()
{
	return {"carousel",
			22,
			"Carousel",
			1,
			false,
			// Each home's radius and each court's as percentages of the standard; the corridors' width and
			// the spokes' in tiles; the wall between a court and the next home in tiles; the plaza's
			// radius as a share of the half side.
			{{"home-size", "Home size", 60, 160, 10, 100, ControlGroup::Layout},
			 {"corridor-width", "Corridor width", 3, 17, 2, 3, ControlGroup::Terrain},
			 {"spoke-width", "Spoke width", 3, 17, 2, 3, ControlGroup::Terrain},
			 {"court-size", "Court size", 20, 160, 10, 30, ControlGroup::Layout},
			 {"court-wall", "Court wall", 1, 5, 1, 2, ControlGroup::Terrain},
			 {"plaza-size", "Plaza size", 14, 34, 2, 24, ControlGroup::Layout},
			 // The towers every colony starts with, all against its walls: their level (0 for none, just
			 // open pads) and how many.
			 {"starting-towers", "Starting tower level", 0, 3, 1, 2, ControlGroup::Layout},
			 {"tower-count", "Towers per colony", 0, 12, 1, 3, ControlGroup::Layout},
			 // Off, the corridors and spokes are grass from coast wall to coast wall.
			 GeneratorControl::toggle("sand-roads", "Sand roads", true, ControlGroup::Layout),
			 // On, every farm has a 10x4 clearing of grass ringed with sand in its middle, for a
			 // swarm or an inn.
			 GeneratorControl::toggle("farm-plots", "Farm building plots", true, ControlGroup::Layout),
			 // Every home's ambient fields, the plaza's farmland and outcrops, the courts' and the
			 // orchard's fruit, and the algae; every home's kit and the walls' stone are unscaled.
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
