// SPDX-License-Identifier: GPL-3.0-or-later
#include "CarouselGenerator.h"
#include "Drawing.h"
#include "Farmland.h"
#include "FertilityField.h"
#include "Game.h"
#include "GenerationContext.h"
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
#include <cmath>
#include <string>
#include <vector>
using namespace MapGeneration;

// Carousel: a ring of walled homes round a plaza, where every colony besieges one neighbour and is
// besieged by the other.
//
// THE SHAPE. Each home has a single door, on its clockwise side. The door opens onto a narrow corridor
// that follows the ring round to a small court - the elbow - pressed against the next colony's home,
// and from the court a narrow spoke runs in to the plaza in the middle of the map. The court and the
// home beside it are parted by a thin wall of stone. No unit can walk through it, but a defence tower
// shoots straight over it. So every colony's towers, standing in its own home against that wall, cover
// its neighbour's elbow: the one way that neighbour has out of its home. Every colony shells one
// neighbour's exit while the other neighbour shells its own, and the only way to walk into anyone's
// home is the long way round: through the plaza, down the enemy's spoke, across its court under fire,
// and back along its corridor.
//
// WHY THE LANES ARE TIGHT. The concept only works if the laneways are narrow: a wide corridor or court
// lets an attacker walk out of range of the siege towers, and lets a defender build its own towers
// in the court. So the corridors and spokes default to 3 tiles and the court to a circle about 3 tiles
// in radius, and every wall is a single line of stone with no water beside it, so a tower behind a
// wall stands as close as possible to the lane it covers.
//
// WHY THE FARMS. Each home reaches two walled wheat farms, one in towards the plaza and one out beyond
// the ring, laid in rows at the widths that regrow best (Farmland). The farms are the homes' water:
// there are no ponds, which would only take building room. Every set of farms is shared out by equal
// yield for its rows' angle, so no colony's farms grow less for facing the wrong way. The plaza has
// only fruit - an orchard for inns to pull hungry enemies across - with no wheat or wood to smother it.
//
// HOW IT IS BUILT. Everything is designed once, in the wedge's frame (angle round the centre and
// distance out from it), and turned round the centre for every colony, so every colony's ground is the
// same and the layout is fair for any colony count. The pieces - homes, lanes, courts and the plaza -
// are drawn first, then the farms grown into the sea between them. Then all the sea near a home or farm
// fills in with that colony's ground, and a line of stone is drawn wherever two colonies' ground meets
// (see fillAndWall). What sea is left beyond the outermost pieces is sealed off: stone stands on every
// grass tile touching its beaches, so a swimmer can land but never walk in. The design is a pure
// function of the request, so validateWorld rebuilds it and checks the finished world against it.
//
// GAME RULES BEHIND IT (docs/map-generators/GAME_RULES_FOR_MAP_DESIGN.md): stone can never be cleared, so
// the walls are permanent; a defence tower scans square rings round its footprint with no line of sight
// (BuildingUtils::turretScanTile), so a wall stops walking but not shooting; stone stands only on pure
// grass and grass may never touch water, so a wall never runs along water; wheat and wood regrow only
// near water and spread onto the grass round them, which is why the farms are rows of crops between
// water, capped with sand; and fruit lets an inn pull hungry enemy units across, which makes the
// plaza's orchard worth the walk.
//
// THE SIZES AT THE DEFAULTS (256x256, 4 colonies): homes 26 tiles in radius 94 tiles out from the
// centre, corridors 3 wide sweeping about 50 degrees, courts 3 in radius, a court wall 2 thick, spokes 3
// wide and a plaza 31 in radius with a pond and an orchard; three towers per colony.
namespace
{

// THE RING. The homes' outer edge, as a share of the half side on a 512-tile map (smaller maps give the
// ring more of their room, since homes cannot shrink with them); a crowded ring grows until every colony
// has at least this many tiles of arc, up to this share of the half side.
constexpr double kRingShare = 0.7;
constexpr double kArcPerColony = 110;
constexpr double kMaximumRingShare = 0.9;

// THE HOMES. A home's radius at 100% of home size, as a share of the half side and never below this many
// tiles, but never across more than this share of the ring's arc per colony, so a crowded ring gets
// smaller homes; its outline wobbles by up to this share.
constexpr double kHomeShare = 0.2;
constexpr double kHomeArcShare = 0.2;
constexpr double kMinimumHome = 14;
constexpr double kHomeRoughness = 0.12;
// Every home's starter kit: this much wheat and wood beside the swarm, unscaled, 1:1.
constexpr int kHomeWheat = 30;
constexpr int kHomeWood = 30;
// A home's scattered farmland, as percentages of its tiles at 100% wheat and wood.
constexpr int kHomeWheatShare = 4;
constexpr int kHomeWoodShare = 2;

// THE LANES AND COURTS. A court's radius at 100% of court size, in tiles on a 256-tile map (it scales
// with the square root of the map), and never below this, nor narrower than its corridor.
constexpr double kCourtRadius = 9;
constexpr double kMinimumCourt = 3;
// The corridor must run at least this far between its home and its court, or the court is simply part
// of the home.
constexpr double kMinimumCorridor = 10;
// The court wall is drawn inside a band this many court radii wide from the court to the next home, so
// it covers where they face each other and nowhere else.
constexpr double kWallBand = 1.3;
// Two colonies' lanes keep at least this many steps apart outside the plaza, so a wall between them
// never closes either; the plaza keeps twice this from the homes' ring.
constexpr double kLaneGap = 4;
// A lane passing another colony's home needs only a tile between them, for the wall.
constexpr int kLaneHomeGap = 2;
// The plaza's pond as a share of the plaza's radius, and the land kept round it.
constexpr double kPlazaPondShare = 0.28;
constexpr double kPlazaPondShore = 5;

// THE FARMS. While they grow, farms keep this many steps from other colonies' ground (the gap fills in
// afterwards), join their home through a neck this wide, and are laid in a set (every inner farm, or
// every outer one) only if every colony's field in it is at least this share of a home's area.
constexpr int kFarmGap = 3;
constexpr double kNeckHalf = 8;
constexpr double kMinimumFarmShare = 0.5;
// Land kept round a farm's water rows inside its walls (enough for a beach and the sand cap), and the
// further margin kept from any open sea, whose beach and sealed coast need more.
constexpr int kFarmRim = 3;
constexpr int kFarmSeaRim = 3;
// A sand bridge crosses a farm's water rows every this many tiles along them.
constexpr int kFarmBridgeSpacing = 16;
// A farm is a wheat farm: wheat on this share of its crop rows at 100%, and a woodlot of this share.
constexpr int kFarmWheatShare = 45;
constexpr int kFarmWoodShare = 3;
// The least share of a farm's crop land its colony must be able to walk to.
constexpr double kFarmReach = 0.95;

// FILLING IN. The sea within this many steps of a home or farm becomes that colony's ground; sea left
// beyond it keeps this many tiles of the nearest piece's ground before its coast.
constexpr int kFillReach = 24;
constexpr int kSeaMargin = 3;
// A road's corridor sand keeps this many steps from any water, so it never joins a beach.
constexpr int kRoadSeaGap = 5;
// Deposits keep this many steps from every corridor and spoke, so no field ever grows a door shut.
constexpr int kDoorClearance = 6;

// THE TOWERS. Open 2x2 pads per colony beside its `tower-count` towers, and the least distance between
// sites, kept small so the siege line packs against its wall.
constexpr int kTowerPads = 3;
constexpr int kTowerSpacing = 2;
// A colony's elbow, which the next colony's towers aim at: its court and its lanes up to this many tiles
// beyond the court's edge. Towers stand in a home no more than this many steps from a wall.
constexpr double kElbowReach = 12;
constexpr int kSiegeLine = 6;

// CHECKS. The level (0, 1 or 2) of tower that must reach the neighbour's court from a home: level 1
// shoots 7 tiles. The most the colonies' walks to the plaza may differ.
constexpr int kSiegeTower = 1;
constexpr int kWalkSpread = 12;

struct Geometry
{
	int teams = 0, half = 0;
	double wedge = 0, outer = 0;
	double homeR = 0, homeReach = 0, homeRadius = 0; // radius, wobbled reach, centre's distance out
	double corridorHalf = 0, spokeHalf = 0, courtR = 0, wall = 0, plazaR = 0, plazaPondR = 0;
	double sweep =
		0; // from a home's axis to its court's, before the court is fitted to the outline
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
	const double ringShare = g.half >= 256   ? kRingShare
							 : g.half >= 128 ? kRingShare + 0.06
											 : kRingShare + 0.18;
	g.outer = std::min(kMaximumRingShare * g.half,
					   std::max(ringShare * g.half, g.teams * kArcPerColony / (2 * kPi)));
	// A home is a share of the half side, but never wider than its share of the ring: with its centre at
	// outer - reach, a reach of s * wedge * (outer - reach) solves to the bound below.
	const double arcShare = kHomeArcShare * g.wedge;
	const double reach =
		std::min(std::max(kMinimumHome, kHomeShare * g.half) * (1 + kHomeRoughness),
				 arcShare * g.outer / (1 + arcShare));
	g.homeReach = reach * o.homeSize / 100.0;
	g.homeR = g.homeReach / (1 + kHomeRoughness);
	g.homeRadius = g.outer - g.homeReach;
	g.corridorHalf = o.corridorWidth / 2.0;
	g.spokeHalf = o.spokeWidth / 2.0;
	g.courtR = std::max(kMinimumCourt, kCourtRadius * std::sqrt(scale) * o.courtSize / 100.0);
	g.courtR = std::max(g.courtR, g.corridorHalf + 0.5);
	g.wall = o.courtWall;
	g.plazaR = std::min(g.half * o.plazaSize / 100.0,
						g.homeRadius - g.homeReach - g.corridorHalf - 2 * kLaneGap);
	g.plazaPondR = std::max(kHomeSmallestPond, kPlazaPondShare * g.plazaR);
	g.roads = o.sandRoads;
	g.sweep = courtSweep(g, g.homeReach);
	if (!homeHasRoom(g.homeR))
		g.failure = "Too many colonies for this map: the homes are too small.";
	else if (g.sweep <= 0)
		g.failure = "Too many colonies for this map: the homes do not fit round the ring.";
	else if (g.homeRadius * g.sweep - g.homeReach - g.courtR < kMinimumCorridor)
		g.failure =
			"Too many colonies for this map: no room for a corridor between a home and its court.";
	else if (g.plazaR - g.plazaPondR < kPlazaPondShore + 3)
		g.failure = "The plaza is too small for its pond on this map.";
	return g;
}

struct Layout
{
	Torus t{1, 1};
	Geometry g;
	double phase = 0, cx = 0, cy = 0, sweep = 0;
	std::vector<double> axis;              // every colony's home axis angle
	std::vector<ShapePoint> homes, courts; // their centres
	std::vector<ShapePoint> heartOf;       // where each colony's spoke comes into the plaza
	// Per tile, the colony whose home, court, lane (corridor or spoke) or farm it is, -1 for none; a farm
	// tile's set is 0 for an inner farm and 1 for an outer one.
	std::vector<int> homeOf, courtOf, pathOf, farmOf, farmSet;
	// Per tile, the side it lies on for the walls between sides: the plaza (0), a colony's court and
	// lanes (1 + colony), its home and farms (1 + teams + colony); -1 for the court walls and open sea.
	std::vector<int> side;
	// Steps from the homes as first drawn, before the sea filled in: where a home's door stands.
	std::vector<int> homeSteps;
	std::vector<unsigned char> plaza, land, pond, wall, road, roadTile;
	// Every farm laid, the colony it belongs to, and all the farms' sand (plot rings, bridges and caps).
	std::vector<Farm> farms;
	std::vector<int> farmColony;
	std::vector<unsigned char> farmSand;
	std::string failure;
};

// Whether the border between tile i and its lower-sided neighbour j stays open rather than walled: a lane
// into the plaza, or a home's door onto its own corridor, where the corridor leaves the home as drawn.
bool borderOpen(const Layout &L, int i, int j)
{
	const int teams = L.g.teams, a = L.side[i], b = L.side[j];
	if (b == 0 && a >= 1 && a <= teams)
		return true;
	return a == b + teams && b >= 1 && L.pathOf[j] >= 0 && L.homeSteps[j] >= 0 &&
		   L.homeSteps[j] <= 2;
}

// THE LANES: every colony's home axis, home, court, corridor and spoke, and the sand road down them. The
// corridor follows the homes' ring from the home's axis to its court; the spoke runs in from the court to
// just inside the plaza, at the middle of the wedge, leaning back towards its own home and away from the
// next, which its court is pressed against.
void drawLanes(Layout &L, const RadialShape &homeShape)
{
	const Torus &t = L.t;
	const Geometry &g = L.g;
	const int n = t.size();
	// The court's exact place: its edge `wall` tiles from the next home's outline where that outline faces
	// it. The outline is the same shape turned for every home, so this holds for every colony.
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
	for (int k = 0; k < g.teams; ++k)
	{
		const double a = L.phase + g.wedge * k, c = a + sweep;
		L.axis.push_back(a);
		L.homes.push_back(polarPoint(L.cx, L.cy, g.homeRadius, a));
		L.courts.push_back(polarPoint(L.cx, L.cy, g.homeRadius, c));
		const double entry = std::min(c, a + g.wedge / 2);
		L.heartOf.push_back(polarPoint(L.cx, L.cy, g.plazaR - 4, entry));
		const ShapePoint in = polarPoint(L.cx, L.cy, g.plazaR - g.spokeHalf, entry);
		std::vector<unsigned char> mask(n, 0);
		strokePath(mask, t, arcPath(L.cx, L.cy, g.homeRadius, a, c, g.corridorHalf));
		strokePath(mask, t,
				   {{L.courts[k].x, L.courts[k].y, g.spokeHalf}, {in.x, in.y, g.spokeHalf}});
		for (int i = 0; i < n; ++i)
			if (mask[i])
				L.pathOf[i] = k;
		if (g.roads)
		{
			// The road runs down the corridor from just outside the home, through the court, down the spoke
			// and across the plaza to its pond, so the way to the middle can never be fully overgrown.
			const double inset = (g.homeReach + 1) / g.homeRadius;
			tracePath(L.road, t, arcPath(L.cx, L.cy, g.homeRadius, a + inset, c, 0));
			const ShapePoint pond = polarPoint(L.cx, L.cy, g.plazaPondR, entry);
			tracePath(L.road, t,
					  {{L.courts[k].x, L.courts[k].y, 0}, {in.x, in.y, 0}, {pond.x, pond.y, 0}});
		}
	}
}

// THE PIECES: homes and courts over the lanes, the wall between every court and the next home, and the
// plaza with its pond. The court wall is just the land no more than `court-wall` steps from both the
// court and the next home, on the home's side of the court's middle and never over the court's own lanes:
// the thinnest wall that parts them.
void stampPieces(Layout &L, const RadialShape &homeShape, const RadialShape &courtShape,
				 const RadialShape &plazaShape, const RadialShape &plazaPondShape)
{
	const Torus &t = L.t;
	const Geometry &g = L.g;
	const int n = t.size(), teams = g.teams;
	for (int k = 0; k < teams; ++k)
	{
		stampRoundHome(t, L.homes[k], L.axis[k], homeShape, g.homeR, 0, nullptr, L.pond,
					   [&](int i)
					   {
						   L.homeOf[i] = k;
						   L.pathOf[i] = -1;
					   });
		forEachTileInShape(t, L.courts[k].x, L.courts[k].y, courtShape, 0,
						   [&](int i, double, double)
						   {
							   L.courtOf[i] = k;
							   L.pathOf[i] = -1;
						   });
	}
	for (int k = 0; k < teams; ++k)
	{
		const ShapePoint next = L.homes[(k + 1) % teams];
		std::vector<unsigned char> band(n, 0), court(n, 0), home(n, 0);
		strokePath(band, t,
				   {{L.courts[k].x, L.courts[k].y, g.courtR * kWallBand},
					{next.x, next.y, g.courtR * kWallBand}});
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
				const double along = t.offsetX(int(std::lround(L.courts[k].x)), i % t.w) * ax +
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
		L.land[i] =
			L.homeOf[i] >= 0 || L.courtOf[i] >= 0 || L.pathOf[i] >= 0 || L.plaza[i] || L.wall[i];
		// Roads only on the lanes, courts and plaza: never in a home, and never in water.
		if (L.pond[i] || (L.road[i] && L.pathOf[i] < 0 && L.courtOf[i] < 0 && !L.plaza[i]))
			L.road[i] = 0;
	}
}

// A crowded ring the design refuses: a lane running past another colony's home with no room for a wall,
// two colonies' lanes too close outside the plaza, or a court touching the next home.
std::string crowding(const Layout &L)
{
	const Torus &t = L.t;
	const Geometry &g = L.g;
	const int n = t.size(), teams = g.teams;
	for (int k = 0; k < teams; ++k)
	{
		std::vector<unsigned char> home(n, 0), nextPath(n, 0);
		for (int i = 0; i < n; ++i)
		{
			home[i] = L.homeOf[i] == k;
			nextPath[i] = L.pathOf[i] == (k + 1) % teams;
		}
		const std::vector<int> fromHome = stepsFrom(t, home), fromNextPath = stepsFrom(t, nextPath);
		for (int i = 0; i < n; ++i)
		{
			const int own = L.pathOf[i];
			if (own < 0)
				continue;
			// Beside its own court a lane is the court's mouth, which the court wall parts from the home.
			const bool mouth = std::hypot(t.offsetX(int(std::lround(L.courts[own].x)), i % t.w),
										  t.offsetY(int(std::lround(L.courts[own].y)), i / t.w)) <
							   g.courtR + g.corridorHalf;
			if (own != k && !mouth && fromHome[i] < kLaneHomeGap)
				return "Too many colonies for this map: a spoke or corridor passes too close to a "
					   "home.";
			const double d = std::hypot(i % t.w - L.cx, i / t.w - L.cy);
			if (teams > 1 && own == k && fromNextPath[i] < kLaneGap && d > g.plazaR + kLaneGap)
				return "Too many colonies for this map: the spokes crowd each other.";
		}
	}
	for (int k = 0; k < teams; ++k)
	{
		std::vector<unsigned char> next(n, 0);
		for (int i = 0; i < n; ++i)
			next[i] = L.homeOf[i] == (k + 1) % teams;
		const std::vector<int> steps = stepsFrom(t, next);
		for (int i = 0; i < n; ++i)
			if (L.courtOf[i] == k && steps[i] < 2)
				return "A court touches the next home; make the court wall thicker.";
	}
	return "";
}

// A set of farms laid: which set (0 inner, 1 outer), and every colony's row origin.
struct FarmSet
{
	int set;
	std::vector<ShapePoint> origins;
};

// THE FARM FIELDS (growFarmFields): every colony's home reaches one field in towards the plaza, in the
// sea between its corridor and the spokes, and one out beyond the ring. Each set is shared out by equal
// yield for every colony's row angle (its axis), so a colony whose rows fall on the diagonal gets more
// ground. A set is kept only if every colony's field in it has room, so a small or crowded map loses a
// set for everyone rather than favouring anyone. The rows are laid later, once the fields' walls stand.
std::vector<FarmSet> claimFarmFields(Layout &L)
{
	const Torus &t = L.t;
	const Geometry &g = L.g;
	const int n = t.size(), teams = g.teams;
	std::vector<unsigned char> inner(n, 0), outer(n, 0);
	for (int i = 0; i < n; ++i)
	{
		const double d = std::hypot(t.offsetX(int(L.cx), i % t.w), t.offsetY(int(L.cy), i / t.w));
		inner[i] = d < g.homeRadius;
		outer[i] = d >= g.homeRadius;
	}
	std::vector<ShapePoint> innerSeeds, outerSeeds;
	std::vector<int> owners;
	for (int k = 0; k < teams; ++k)
	{
		innerSeeds.push_back(polarPoint(L.cx, L.cy, g.homeRadius - g.homeReach - 2, L.axis[k]));
		outerSeeds.push_back(polarPoint(L.cx, L.cy, g.homeRadius + g.homeReach + 2, L.axis[k]));
		owners.push_back(k);
	}
	const std::vector<int> fields[2] = {
		growFarmFields(t, L.land, L.homeOf, inner, innerSeeds, owners, L.homes, kFarmGap, kNeckHalf,
					   L.axis),
		growFarmFields(t, L.land, L.homeOf, outer, outerSeeds, owners, L.homes, kFarmGap, kNeckHalf,
					   L.axis)};
	const double homeArea = kPi * g.homeR * g.homeR;
	std::vector<FarmSet> sets;
	for (int set = 0; set < 2; ++set)
	{
		std::vector<int> size(teams, 0);
		for (int i = 0; i < n; ++i)
			if (fields[set][i] >= 0)
				++size[fields[set][i]];
		if (*std::min_element(size.begin(), size.end()) < kMinimumFarmShare * homeArea)
			continue;
		sets.push_back({set, set == 0 ? innerSeeds : outerSeeds});
		for (int i = 0; i < n; ++i)
			if (fields[set][i] >= 0 && L.homeOf[i] < 0)
			{
				L.farmOf[i] = fields[set][i];
				L.farmSet[i] = set;
				L.land[i] = 1;
			}
	}
	return sets;
}

// FILLING IN AND WALLING. Nothing but stone parts two colonies' ground, so every wall stands right beside
// what it guards:
//  - The sea within kFillReach of a home or farm becomes that home's or farm's ground, whichever is
//    nearest (fillToNearest). Lanes, courts and the plaza never grow, so they keep exactly the width they
//    were drawn. Sea left beyond that keeps a kSeaMargin strip of the nearest piece before its beach, so
//    a beach and its sealed coast never eat into a lane.
//  - Every tile gets a side: the plaza, a colony's lanes and court, or a colony's home and farms. A
//    single line of stone stands wherever two sides meet (labelBorders), on the higher side - always the
//    home's or farm's, so no wall narrows a lane. Two borders stay open (borderOpen): each home's door
//    onto its own corridor, and each spoke's way into the plaza.
void fillAndWall(Layout &L)
{
	const Torus &t = L.t;
	const int n = t.size(), teams = L.g.teams;
	// Fill labels: a home per colony, a farm per colony per set, then courts, lanes and the plaza.
	const int farmLabel = teams, courtLabel = 3 * teams, pathLabel = 4 * teams,
			  plazaLabel = 5 * teams;
	std::vector<int> piece(n, -1), grown(n, -1);
	std::vector<unsigned char> sea(n, 0), home(n, 0);
	for (int i = 0; i < n; ++i)
	{
		piece[i] = L.wall[i]           ? -1
				   : L.homeOf[i] >= 0  ? L.homeOf[i]
				   : L.farmOf[i] >= 0  ? farmLabel + L.farmSet[i] * teams + L.farmOf[i]
				   : L.courtOf[i] >= 0 ? courtLabel + L.courtOf[i]
				   : L.pathOf[i] >= 0  ? pathLabel + L.pathOf[i]
				   : L.plaza[i]        ? plazaLabel
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
	const std::vector<unsigned char> margin = fillToNearest(t, piece, sea, kSeaMargin);
	L.homeSteps = stepsFrom(t, home);
	for (int i = 0; i < n; ++i)
	{
		if (!filled[i] && !margin[i])
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
	L.side.assign(n, -1);
	for (int i = 0; i < n; ++i)
		L.side[i] = piece[i] < 0             ? -1
					: piece[i] >= plazaLabel ? 0
					: piece[i] >= courtLabel ? 1 + (piece[i] - courtLabel) % teams
											 : 1 + teams + piece[i] % teams;
	const std::vector<unsigned char> borders =
		labelBorders(t, L.side, [&](int i, int j) { return borderOpen(L, i, j); });
	for (int i = 0; i < n; ++i)
		if (borders[i])
			L.wall[i] = 1;
}

// THE FARM ROWS (layFarm): over each whole field inside its walls, along the colony's axis at the widths
// that yield most (bestFarmRows), with a sand cap closing the crop rows, a sand bridge across the water
// every kFarmBridgeSpacing tiles and, with `farm-plots`, a building plot in the middle.
void layFarmRows(Layout &L, const std::vector<FarmSet> &sets, bool plots)
{
	const Torus &t = L.t;
	const int n = t.size(), teams = L.g.teams;
	const FarmPlot plot;
	std::vector<unsigned char> openSea(n, 0);
	for (int i = 0; i < n; ++i)
		openSea[i] = !L.land[i];
	const std::vector<int> fromSea = stepsFrom(t, openSea);
	for (const FarmSet &set : sets)
		for (int k = 0; k < teams; ++k)
		{
			std::vector<unsigned char> region(n, 0);
			for (int i = 0; i < n; ++i)
				region[i] = L.farmOf[i] == k && L.farmSet[i] == set.set && !L.wall[i] &&
							(fromSea[i] < 0 || fromSea[i] > kFarmSeaRim);
			TerrainSketch rows(n, GRASS);
			L.farms.push_back(layFarm(rows, t, region, L.axis[k], set.origins[k], kFarmRim,
									  bestFarmRows(L.axis[k]), plots ? &plot : nullptr,
									  kFarmBridgeSpacing));
			L.farmColony.push_back(k);
			const Farm &farm = L.farms.back();
			for (int i = 0; i < n; ++i)
			{
				if (farm.water[i])
					L.pond[i] = 1;
				if (farm.sand[i])
					L.farmSand[i] = 1;
			}
		}
}

// THE ROADS, finished: no road vertex within kRoadSeaGap of water (keepRoadInland), so its sand never
// carries a beach inland, and none touching a wall tile, since stone stands only on grass.
void finishRoads(Layout &L)
{
	const Torus &t = L.t;
	const int n = t.size();
	std::vector<unsigned char> water(n, 0);
	for (int i = 0; i < n; ++i)
		water[i] = !L.land[i] || L.pond[i];
	keepRoadInland(L.road, t, water, kRoadSeaGap);
	for (int i = 0; i < n; ++i)
		for (int dy = -1; dy <= 0 && L.road[i]; ++dy)
			for (int dx = -1; dx <= 0; ++dx)
				if (L.wall[t.at(i % t.w + dx, i / t.w + dy)])
					L.road[i] = 0;
	L.roadTile = roadTiles(t, L.road);
}

Layout design(const GenerationRequest &request, GenerationContext &context)
{
	const CarouselOptions o(request);
	Layout L;
	L.t = Torus(1 << request.wDec, 1 << request.hDec);
	const int n = L.t.size();
	L.g = geometryFor(request);
	const Geometry &g = L.g;
	if (!g.failure.empty())
	{
		L.failure = g.failure;
		return L;
	}
	L.cx = L.t.w / 2;
	L.cy = L.t.h / 2;
	L.phase = context.bounded("carousel-phase", 3600) / 3600.0 * 2 * kPi;
	L.homeOf.assign(n, -1);
	L.courtOf.assign(n, -1);
	L.pathOf.assign(n, -1);
	L.farmOf.assign(n, -1);
	L.farmSet.assign(n, -1);
	L.plaza.assign(n, 0);
	L.land.assign(n, 0);
	L.pond.assign(n, 0);
	L.wall.assign(n, 0);
	L.road.assign(n, 0);
	L.farmSand.assign(n, 0);
	// One outline for every home, turned to each home's axis; the court and plaza are true circles.
	const RadialShape homeShape(g.homeR, kHomeRoughness, context, "carousel-home");
	const RadialShape courtShape(g.courtR, 0.0001, context, "carousel-court");
	const RadialShape plazaShape(g.plazaR, 0.0001, context, "carousel-plaza");
	const RadialShape plazaPondShape(g.plazaPondR, 0.0001, context, "carousel-plaza-pond");

	drawLanes(L, homeShape);
	stampPieces(L, homeShape, courtShape, plazaShape, plazaPondShape);
	if (const std::string crowded = crowding(L); !crowded.empty())
	{
		L.failure = crowded;
		return L;
	}
	const std::vector<FarmSet> farmSets = claimFarmFields(L);
	fillAndWall(L);
	layFarmRows(L, farmSets, o.farmPlots);
	finishRoads(L);
	return L;
}

// The sea's margin: the land a swimmer can stand on. Neither a road's sand nor a farm's is beach, so
// neither carries the margin inland.
std::vector<unsigned char> seaMargin(const Map &map, const Layout &L)
{
	std::vector<unsigned char> notBeach = L.roadTile;
	const std::vector<unsigned char> farmSand = roadTiles(L.t, L.farmSand);
	for (int i = 0; i < L.t.size(); ++i)
		notBeach[i] = notBeach[i] || farmSand[i];
	return MapGeneration::seaMargin(map, L.t, seaVertices(map, L.t, L.pond), notBeach);
}

// The design's stone, once the terrain is laid: every coast of the remaining sea sealed, and every wall.
std::vector<unsigned char> stoneTiles(const Map &map, const Layout &L)
{
	std::vector<unsigned char> stone = sealCoasts(map, L.t, seaMargin(map, L), L.land);
	const DesignedStone wall = designedStone(map, L.t, L.wall);
	for (int i = 0; i < L.t.size(); ++i)
		if (wall.stone[i])
			stone[i] = 1;
	return stone;
}

// The resources: every home's kit and scattered farmland, a fruit grove in every court, the plaza's
// orchard (fruit only, so nothing smothers it), and the farms' wheat and woodlots.
void furnish(Map &map, const Layout &L, GenerationContext &context, const CarouselOptions &o,
			 const std::vector<unsigned char> &pads)
{
	const Torus &t = L.t;
	const Geometry &g = L.g;
	const int n = t.size();
	const std::vector<unsigned char> reserved = swarmSurroundings(t, context);
	const Fertility::Field fertility = Fertility::forMap(map, false);
	// The wedge frame starts half a wedge before colony 0's axis, so a wedge's middle is its home and
	// every home samples the same patches.
	const WedgeFrame wedges(t, L.phase - g.wedge / 2, g.teams);
	const WedgeField patch{wedges, PeriodicNoise(t.w, t.h, 12, context.stream("carousel-patch"))};
	const WedgeField split{wedges, PeriodicNoise(t.w, t.h, 6, context.stream("carousel-split"))};
	std::vector<unsigned char> paths(n, 0);
	for (int i = 0; i < n; ++i)
		paths[i] = L.pathOf[i] >= 0;
	const std::vector<int> fromPaths = stepsFrom(t, paths);
	const auto free = [&](int i)
	{
		return !L.road[i] && !L.roadTile[i] && !reserved[i] && !pads[i] &&
			   fromPaths[i] > kDoorClearance && clearGround(map, i % t.w, i / t.w);
	};
	for (int k = 0; k < g.teams; ++k)
	{
		const auto eligible = [&](int i) { return L.homeOf[i] == k && free(i); };
		plantHomeKit(map, t, context, L.homes[k], L.axis[k], g.homeR, kHomeWheat, kHomeWood,
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
	const int courtFruit = CHERRY + int(context.bounded("carousel-court", 3));
	if (scaledCount(1, o.fruit) > 0)
		for (int k = 0; k < g.teams; ++k)
			plantRound(map, t, context, L.courts[k].x, L.courts[k].y, g.courtR * 0.45, {L.axis[k]},
					   courtFruit, 1, 4, [&](int i) { return L.courtOf[i] == k && free(i); });
	// The orchard's three groves in every wedge, on the pond's shore halfway between two spokes' arrivals.
	const auto plazaFree = [&](int i) { return L.plaza[i] && free(i); };
	std::vector<double> between;
	for (int k = 0; k < g.teams; ++k)
		between.push_back(std::min(L.axis[k] + g.wedge / 2, L.axis[k] + L.sweep) + g.wedge / 2);
	if (scaledCount(1, o.fruit) > 0)
		plantOrchard(map, t, context, L.cx, L.cy, g.plazaPondR + 4, between, 5, 5, 1, plazaFree);
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

// THE SIEGE LINE (chooseTowerSites): every colony's towers and pads stand in its own home, directly
// against stone and within kSiegeLine steps of a wall, chosen for how much of the previous colony's elbow
// - its court and lanes within kElbowReach - they cover across the wall. Nothing scores for covering the
// colony's own lanes: the design is attack, not defence.
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
		owner[i] = L.homeOf[i] >= 0    ? L.homeOf[i]
				   : L.courtOf[i] >= 0 ? L.courtOf[i]
				   : L.farmOf[i] >= 0  ? L.farmOf[i]
									   : L.pathOf[i];
		buildable[i] = L.homeOf[i] >= 0 && fromWall[i] >= 0 && fromWall[i] <= kSiegeLine &&
					   map.isGrass(x, y) && !stone[i] && !L.roadTile[i] && !reserved[i] &&
					   !map.isResource(x, y);
		const int k = L.courtOf[i] >= 0 ? L.courtOf[i] : L.pathOf[i];
		target[i] =
			k >= 0 && !map.isWater(x, y) && !stone[i] &&
			std::hypot(t.offsetX(int(std::lround(L.courts[k].x)), x),
					   t.offsetY(int(std::lround(L.courts[k].y)), y)) <= g.courtR + kElbowReach;
	}
	TowerRequest request = startingTowerRequest(o.towers, o.towerCount, kTowerPads, kTowerSpacing);
	request.otherWeight = 1;
	request.ownWeight = 0;
	request.against = &stone;
	return chooseTowerSites(t, owner, buildable, target, swarmSurroundings(t, context, 0), g.teams,
							request);
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
			ground[i] =
				L.homeOf[i] == team && !stone[i] && !L.roadTile[i] && map.isGrass(i % t.w, i / t.w);
		return ground;
	};
	const auto anchor = [&](int team)
	{ return homeSwarmSite(L.homes[team], L.axis[team], L.g.homeR); };
	if (!settleColonies(game, context, "carousel-starts", home, anchor))
		return false;

	// No tower or pad may close a colony's way to the plaza's heart; every colony keeps as many as the
	// fewest got, and must have at least one when towers were asked for.
	context.stage = "carousel towers";
	TowerPlan towers = planTowers(map, L, context, o, stone);
	std::vector<unsigned char> plazaHeart(n, 0);
	for (int i = 0; i < n; ++i)
		plazaHeart[i] =
			L.plaza[i] && std::hypot(i % t.w - L.cx, i / t.w - L.cy) < L.g.plazaPondR + 3;
	if (!settleStartingTowers(game, context, towers, o.towers, o.towers > 0 && o.towerCount > 0,
							  &plazaHeart))
		return false;
	const std::vector<unsigned char> pads = towerFootprints(t, towers);

	context.stage = "carousel resources";
	furnish(map, L, context, o, pads);
	const WedgeFrame algaeWedges(t, L.phase - L.g.wedge / 2, L.g.teams);
	seedAlgae(map, context, t, "carousel-algae", o.algae, AlgaeBand::shallows(1, 4), &algaeWedges);
	secureStartingCrops(game, context, t, 24, 32, 0, &stone);
	reopenCrampedStarts(game, context, {o.wheat, o.wood, o.stone, o.algae, o.fruit}, 24, 32, 0,
						&stone);
	clearFarmPlots(map, t, L.farms);

	// Keep every colony's walk open, clearing only deposits on it: from home to its court, and from the
	// court to the orchard round the plaza's pond.
	context.stage = "carousel roads";
	const std::vector<std::vector<int>> workers = unitTilesByTeam(map, teams);
	std::vector<unsigned char> heart(n, 0);
	for (int i = 0; i < n; ++i)
		heart[i] = L.plaza[i] && !map.isWater(i % t.w, i / t.w) && !stone[i] &&
				   std::hypot(i % t.w - L.cx, i / t.w - L.cy) < L.g.plazaPondR + 3;
	for (int team = 0; team < teams; ++team)
	{
		if (workers[team].empty())
			continue;
		std::vector<unsigned char> court(n, 0);
		std::vector<int> courtTiles;
		for (int i = 0; i < n; ++i)
			if (L.courtOf[i] == team && !stone[i] && !map.isWater(i % t.w, i / t.w))
			{
				court[i] = 1;
				courtTiles.push_back(i);
			}
		if (!openRoad(map, t, workers[team], court, &stone) ||
			!openRoad(map, t, courtTiles, heart, &stone))
		{
			context.detail =
				"colony " + std::to_string(team) + " has no walk to its court and the plaza";
			return false;
		}
	}
	return true;
}

std::string validateRequest(const GenerationRequest &r)
{
	if (r.nbTeams < 2)
		return "Carousels need at least two colonies.";
	// The geometry's own reason stays in the candidate's diagnostics; the player sees one message that
	// says what to change.
	return geometryFor(r).failure.empty() ? ""
										  : "The carousel does not fit this map; use a bigger map, "
											"smaller homes or fewer colonies.";
}

// Checked on the finished world against the rebuilt design:
//  - every designed stone stands, and every colony walks to colony 0;
//  - nothing landing from the sea gets onto any land (seaEntry);
//  - 95% of every farm's crop land, and all of its plot, is a walk from its colony (farmReachable);
//  - every border the design walls is stone: no two sides meet on open ground except at a door or a
//    spoke's way into the plaza;
//  - with every corridor and spoke shut, no home, farm, court or the plaza reaches another (pieceLeak,
//    counting crops and buildings as passable, since they go in time);
//  - from every home, a level-1 tower reaches the previous colony's court across the wall (the siege);
//  - and the colonies' walks to the plaza are even.
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

	for (size_t f = 0; f < L.farms.size(); ++f)
		if (farmReachable(map, t, L.farms[f], walk.workers[L.farmColony[f]]) < kFarmReach)
			return "Colony " + std::to_string(L.farmColony[f]) +
				   " cannot walk into one of its farms.";

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
					if (L.side[j] >= 0 && L.side[j] < L.side[i] && !stone[j] &&
						!map.isWater(j % t.w, j / t.w) && !margin[i] && !margin[j] &&
						!borderOpen(L, i, j))
						return "The wall between two parts of the carousel has a gap at " +
							   where(i) + ".";
				}
		}

	// A piece is its ground off the sea's margin (the sand lane outside a sealed coast, checked above).
	std::vector<int> piece(n, -1);
	std::vector<unsigned char> paths(n, 0);
	for (int i = 0; i < n; ++i)
	{
		if (!margin[i])
			piece[i] = L.homeOf[i] >= 0    ? L.homeOf[i]
					   : L.farmOf[i] >= 0  ? L.farmOf[i]
					   : L.courtOf[i] >= 0 ? teams + L.courtOf[i]
					   : L.plaza[i]        ? 2 * teams
										   : -1;
		paths[i] = L.pathOf[i] >= 0;
	}
	if (const int leak = pieceLeak(map, t, piece, paths); leak >= 0)
		return "With the corridors shut, " + where(leak) +
			   " can still be reached from another part of the carousel.";

	for (int k = 0; k < teams; ++k)
	{
		std::vector<unsigned char> court(n, 0), next(n, 0);
		for (int i = 0; i < n; ++i)
		{
			const int x = i % t.w, y = i / t.w;
			court[i] = L.courtOf[i] == k && !map.isWater(x, y) && !stone[i];
			next[i] = L.homeOf[i] == (k + 1) % teams && map.isGrass(x, y) && !stone[i] &&
					  !map.isResource(x, y);
		}
		const int reach = towerReach(t, next, court);
		if (reach > kTowerRange[kSiegeTower])
			return "No tower in home " + std::to_string((k + 1) % teams) + " reaches court " +
				   std::to_string(k) + " across the wall; it is " + std::to_string(reach) +
				   " tiles away.";
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
	  spokeWidth(r.option("spoke-width")), courtSize(r.option("court-size")),
	  courtWall(r.option("court-wall")), plazaSize(r.option("plaza-size")),
	  towers(r.option("starting-towers")), towerCount(r.option("tower-count")),
	  sandRoads(r.option("sand-roads") != 0), farmPlots(r.option("farm-plots") != 0),
	  wheat(r.option("wheat-amount")), wood(r.option("wood-amount")),
	  stone(r.option("stone-amount")), algae(r.option("algae-amount")),
	  fruit(r.option("fruit-amount"))
{
}

GeneratorDefinition carouselDefinition()
{
	return {
		"carousel",
		22,
		"Carousel",
		1,
		false,
		// Each home's radius and each court's as percentages of the standard; the corridors' and the
		// spokes' widths and the wall between a court and the next home in tiles; the plaza's radius as
		// a share of the half side. The lanes and courts default tight: the siege only works in them.
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
		 // Off, the lanes and the plaza are grass from wall to wall.
		 GeneratorControl::toggle("sand-roads", "Sand roads", true, ControlGroup::Layout),
		 // On, every farm has a 10x4 clearing of grass ringed with sand in its middle, for a swarm or
		 // an inn.
		 GeneratorControl::toggle("farm-plots", "Farm building plots", true, ControlGroup::Layout),
		 // Every home's scattered fields, the farms' wheat and woodlots, the courts' and the orchard's
		 // fruit, and the algae; every home's kit, the walls' stone and the towers are unscaled.
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
