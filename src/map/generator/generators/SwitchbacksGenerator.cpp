// SPDX-License-Identifier: GPL-3.0-or-later
#include "SwitchbacksGenerator.h"
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
#include <climits>
#include <cmath>
#include <cstdlib>
#include <string>
#include <vector>
using namespace MapGeneration;

// Switchbacks: a plateau in the middle of the sea, and round it a ring of homes, each joined to the
// plateau by a mountain of solid stone with one trail cut through it.
//
// THE SHAPE. The trail zigzags: out of the home it runs along one side of the mountain, across to the
// other, back, and across again, one leg after another, before it climbs out onto the plateau. Walking
// from a home to the plateau takes several times the distance as the crow flies, but the walls between
// the legs are only `leg-wall` tiles thick (2 by default), and a defence tower shoots over stone. So the
// trail is a fortification: every colony's towers stand on its own trail, against the inner side of each
// wall (the side towards the middle of the map), and shoot across it at the next leg up, where
// attackers coming down from the plateau must pass. Every attack walks the whole zigzag under fire from
// towers that ignore it.
//
// THE WATER. Every coast is sealed: stone stands on every grass tile touching the sea's margin, so a
// swimmer can land on a beach but never get inside, and the trails are the only way anywhere for the
// whole game. Each home reaches two walled wheat farms, one on either flank, laid in rows that regrow
// best and shared out by equal yield (Farmland); they are the homes' water, on every size of map (128
// maps went without, and had home ponds, until 2026-09-13). The plateau has a pond and only fruit, the prize
// every trail climbs to, with no wheat or wood to smother it.
//
// HOW IT IS BUILT. Everything is designed once in the wedge's frame, along the colony's axis and across
// it, and turned round the centre for every colony, so every colony's ground and trail are the same and
// the layout is fair for any colony count. The map stays round, in a circle on the shorter side, with sea
// filling a rectangle's ends. The design is a pure function of the request, so validateWorld rebuilds it
// and checks the finished world.
//
// GAME RULES BEHIND IT (docs/map-generators/GAME_RULES_FOR_MAP_DESIGN.md): stone can never be cleared,
// so a mountain's walls are permanent and its trail is the only door; a tower scans round its footprint
// with no line of sight (BuildingUtils::turretScanTile), which is what makes a thin wall between two legs
// a firing line; grass may never touch water, so a coast wall always stands a tile in from its sand lane;
// wheat and wood regrow only near water, which is why every home has farms; and fruit lets an
// inn pull hungry enemy units across, which makes the plateau's orchard worth the climb.
//
// THE SIZES AT THE DEFAULTS (256x256, 4 colonies): a plateau 28 tiles in radius; mountains about 40
// tiles wide, each with a trail of several legs about 26 tiles long and 2 tiles of stone between them;
// homes 24 tiles in radius on the rim, each with a farm on either flank; three towers per colony.
namespace
{

// Sea kept between the homes' outer coast and the map's wrap, as a share of the half side.
constexpr double kRimShare = 0.04;
constexpr int kRimMinimum = 4;
// A home's radius at 100% of home size: a share of the half side, never below this many tiles, never
// across more than this share of its ring's arc (half share, as in Carousel), and never more than
// the room left outside the mountains; its outline wobbles by up to this share.
constexpr double kHomeShare = 0.2;
constexpr double kMinimumHome = 14;
constexpr double kHomeArcShare = 0.2;
constexpr double kHomeRoughness = 0.12;
// The plateau's pond as a share of the plateau's radius, and the land kept round it.
constexpr double kPlateauPondShare = 0.25;
constexpr double kPlateauPondShore = 5;
// How far a small map narrows its trail and walls (see geometryFor), and the narrowest of each.
constexpr double kMinimumScale = 0.72;
constexpr int kMinimumTrail = 5;
constexpr int kMinimumWall = 2;
// A trail needs at least this many legs to zigzag at all.
constexpr int kMinimumLegs = 2;
// Stone kept between a trail's outermost legs and the mountain's sides.
constexpr double kSideWall = 3;
// A leg's centre line runs this far either side of the mountain's axis: a share of the half side,
// but at least two trail widths.
constexpr double kSpanShare = 0.1;
// Water kept between two mountains: enough for a beach and a coast wall on each.
constexpr double kPieceGap = 4;
// Every home's starter kit: this much wheat and wood beside the swarm, unscaled; no stone, since the
// mountains are stone.
constexpr int kHomeWheat = 30;
constexpr int kHomeWood = 30;
// A home's scattered farmland, as percentages of its tiles at 100% wheat and wood.
constexpr int kHomeWheatShare = 4;
constexpr int kHomeWoodShare = 2;
// Deposits keep this many steps from every trail, so no field grows across a trail's mouth.
constexpr int kTrailClearance = 6;
// A sand road vertex keeps this many steps from any water.
constexpr int kRoadSeaGap = 5;
// The most the colonies' walks to the plateau may differ.
constexpr int kWalkSpread = 12;
// The farms: water kept between a farm and any other land, and the neck joining it to its home.
constexpr int kFarmGap = 3;
constexpr double kNeckHalf = 8;
// The least share of a farm's crop land its colony must be able to walk to.
constexpr double kFarmReach = 0.95;
// How far apart, along a farm's rows, the sand bridges across its water rows stand; the land a farm keeps
// between its water rows and its coast (a beach, a coast wall and the sand cap); and the smallest farm
// worth having, as a share of the home's area.
constexpr int kFarmBridgeSpacing = 16;
constexpr int kFarmRim = 6;
constexpr double kMinimumFarmShare = 0.5;
// A farm is a wheat farm: wheat on this share of its crop rows at 100, and a small woodlot of this
// share along one row.
constexpr int kFarmWheatShare = 45;
constexpr int kFarmWoodShare = 3;
// Open 2x2 pads beside every colony's `tower-count` towers, and the spacing between sites.
constexpr int kTowerPads = 4;
constexpr int kTowerSpacing = 4;
// Tiles either side of a trail's centre line kept free of towers, the walkway attackers and workers use.
constexpr int kWalkway = 1;
// A tower hugs a wall: rock within this many tiles of its footprint towards the middle of the map.
constexpr int kWallHug = 3;
// The level of tower (0, 1 or 2) that must reach a trail's first leg from its home and its last leg
// from the plateau: level 1 shoots 7 tiles.
constexpr int kCoverTower = 1;

struct Geometry
{
	int teams = 0, half = 0, legs = 0;
	double wedge = 0, outer = 0;
	double trailHalf = 0, narrowestHalf = 0, legWall = 0, pitch = 0, span = 0, blockHalf = 0;
	double plateauR = 0, plateauPondR = 0;
	double innerLeg = 0, outerLeg = 0, blockOut = 0; // along the axis from the centre
	double homeR = 0, homeReach = 0, homeRadius = 0;
	bool roads = true;
	std::string failure;
};

// The legs share the ground between the plateau's edge and the home's (`homeEdge` along the axis): every
// wall between two legs, and between the end legs and the plateau and the home, is exactly the control's
// thickness, as many legs fit as leave each trail at least its narrowest width, and the trails widen to
// use up the rest. Walls stay thin, so a tower on one leg reaches across to the next.
void layLegs(Geometry &g, double homeEdge)
{
	const double wall = g.legWall, narrowest = g.narrowestHalf;
	const double depth = homeEdge - g.plateauR, end = wall + 1;
	// With L legs of half width h: depth = 2 (h + end) + (L - 1) (2 h + wall), so h = (depth - 2 end -
	// (L - 1) wall) / (2 L).
	g.legs = std::max(0, int(std::floor((depth - 2 * end + wall) / (2 * narrowest + wall))));
	g.trailHalf = g.legs > 0 ? (depth - 2 * end - (g.legs - 1) * wall) / (2 * g.legs) : narrowest;
	g.pitch = 2 * g.trailHalf + wall;
	g.innerLeg = g.plateauR + end + g.trailHalf;
	g.outerLeg = homeEdge - end - g.trailHalf;
	g.blockHalf = g.span + g.trailHalf + kSideWall;
}

Geometry geometryFor(const GenerationRequest &r)
{
	const SwitchbacksOptions o(r);
	Geometry g;
	g.teams = std::max(1, r.nbTeams);
	g.half = std::min(1 << r.wDec, 1 << r.hDec) / 2;
	g.wedge = 2 * kPi / g.teams;
	g.outer = g.half - std::max<double>(kRimMinimum, kRimShare * g.half);
	// The trail and wall controls are widths on a 256-tile map; a smaller map narrows both with the
	// square root of its size, down to a trail still wide enough for its road and a wall two tiles
	// thick, so a 128-tile map still fits a zigzag.
	const double narrow = std::clamp(std::sqrt(g.half / 128.0), kMinimumScale, 1.0);
	const int trailWidth =
		std::max(kMinimumTrail, 2 * int(std::lround((o.trailWidth * narrow - 1) / 2)) + 1);
	g.trailHalf = trailWidth / 2.0;
	g.narrowestHalf = g.trailHalf;
	g.legWall = std::max(kMinimumWall, int(std::lround(o.legWall * narrow)));
	g.span = std::max(4.0 * g.trailHalf, kSpanShare * g.half);
	g.blockHalf = g.span + g.trailHalf + kSideWall;
	// The plateau is a share of the half side, but on a crowded ring it grows until the mountains
	// fit round it with water between them at their innermost legs.
	const double halfAngle = std::min(g.wedge / 2, 1.5);
	g.plateauR =
		std::max(g.half * o.plateauSize / 100.0,
				 (g.blockHalf + kPieceGap) / std::tan(halfAngle) - (g.trailHalf + g.legWall + 1));
	g.plateauPondR = std::max(kHomeSmallestPond, kPlateauPondShare * g.plateauR);
	// Homes stand on the rim, as big as their share of the ring allows. The mountain fills the ground
	// between the plateau and the homes with as many legs as fit a pitch apart, the innermost a wall's
	// thickness of stone from the plateau and the outermost the same from the home, so a bigger map
	// gets a longer trail rather than a sea between mountain and home.
	const double ringShare = kHomeArcShare * g.wedge;
	// ...and never so big that the mountain between it and the plateau has no room for two legs.
	const double end = g.trailHalf + g.legWall + 1;
	// However its rough outline falls, a home reaches in no further than its wobbled reach.
	const double mountain = (g.outer - g.plateauR - 2 * end - (2 * g.trailHalf + g.legWall)) / 2;
	const double reach =
		std::min({std::max(kMinimumHome, kHomeShare * g.half) * (1 + kHomeRoughness),
				  ringShare * g.outer / (1 + ringShare), mountain});
	g.homeReach = reach * o.homeSize / 100.0;
	g.homeR = g.homeReach / (1 + kHomeRoughness);
	g.homeRadius = g.outer - g.homeReach;
	layLegs(g, g.homeRadius - g.homeR);
	g.blockOut = g.homeRadius;
	g.roads = o.sandRoads;
	if (!homeHasRoom(g.homeR))
		g.failure = "Too many colonies for this map: the homes are too small.";
	else if (g.legs < kMinimumLegs)
		g.failure = "No room for a trail on this map: use a bigger map, a smaller plateau or a "
					"narrower trail.";
	else if (g.plateauR - g.plateauPondR < kPlateauPondShore + 3)
		g.failure = "The plateau is too small for its pond on this map.";
	// Measured at the trail's narrowest width, as the plateau was sized: trails widened to use up a
	// mountain's depth are trimmed to their wedges near the plateau, and the validator checks the rest.
	else if (g.innerLeg * std::tan(std::min(g.wedge / 2, 1.5)) <
			 g.span + g.narrowestHalf + kSideWall + kPieceGap)
		g.failure =
			"Too many colonies for this map: the mountains crowd each other at the plateau.";
	return g;
}

struct Layout
{
	Torus t{1, 1};
	Geometry g;
	double phase = 0, cx = 0, cy = 0;
	std::vector<int> homeOf, blockOf, trailOf,
		legOf; // legOf: which leg (0 first) a trail tile is on
	std::vector<int> farmOf, farmColony;
	std::vector<Farm> farms;
	std::vector<unsigned char> farmSand; // all the farms' sand: plot rings, bridges and caps
	std::vector<unsigned char> plateau, land, pond, stone, road, roadTile;
	std::vector<double> axis;
	std::vector<AxisFrame>
		frames; // every colony's frame: along its axis from the centre, and across
	std::vector<std::vector<StrokePoint>> trails; // every colony's trail as drawn
	std::vector<ShapePoint> homes, kitCentre,
		summit; // kitCentre: stampRoundHome's; summit: where each trail reaches the plateau
	std::string failure;
};

Layout design(const GenerationRequest &request, GenerationContext &context)
{
	const SwitchbacksOptions o(request);
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
	L.phase = context.bounded("switchbacks-phase", 3600) / 3600.0 * 2 * kPi;
	L.homeOf.assign(n, -1);
	L.blockOf.assign(n, -1);
	L.trailOf.assign(n, -1);
	L.legOf.assign(n, -1);
	L.plateau.assign(n, 0);
	L.land.assign(n, 0);
	L.pond.assign(n, 0);
	L.stone.assign(n, 0);
	L.road.assign(n, 0);
	L.roadTile.assign(n, 0);
	for (int k = 0; k < teams; ++k)
	{
		L.axis.push_back(L.phase + g.wedge * k);
		L.frames.push_back({L.cx, L.cy, L.axis.back()});
	}

	const RadialShape homeShape(g.homeR, kHomeRoughness, context, "switchbacks-home");
	// The home's outline is rough, so its inner edge is where the outline facing the plateau says:
	// the legs are laid out from there, the same for every colony.
	layLegs(L.g, g.homeRadius - homeShape.radiusAt(kPi));
	if (L.g.legs < kMinimumLegs)
	{
		L.failure = "No room for a trail on this map: use a bigger map, a smaller plateau or a "
					"narrower trail.";
		return L;
	}
	const RadialShape pondShape(homePondRadius(g.homeR), 0.2, context, "switchbacks-pond");
	const RadialShape plateauShape(g.plateauR, 0.0001, context, "switchbacks-plateau");
	const RadialShape plateauPondShape(g.plateauPondR, 0.0001, context, "switchbacks-plateau-pond");

	// The mountains: a straight block along each axis from inside the plateau to inside the home. The
	// plateau is round and the block square, so the block starts where the plateau's edge meets the
	// block's sides, and no strip of sea is left at its corners.
	const double blockIn =
		std::sqrt(std::max(0.0, g.plateauR * g.plateauR - g.blockHalf * g.blockHalf)) - 2;
	const WedgeFrame wedges(t, L.phase - g.wedge / 2, teams);
	const double halfAngle = std::min(g.wedge / 2, 1.5);
	for (int i = 0; i < n; ++i)
	{
		const WedgeFrame::Cell cell = wedges.cell(i % t.w, i / t.w);
		const ShapePoint p = L.frames[cell.k].project(cell.dx, cell.dy);
		// Near the plateau, where the wedges narrow, a mountain keeps half the gap between mountains
		// from its wedge's edge, so neighbours never touch.
		const double room = p.x * std::tan(halfAngle) - (kPieceGap + 1) / (2 * std::cos(halfAngle));
		if (p.x >= blockIn && p.x <= g.homeRadius && std::abs(p.y) <= std::min(g.blockHalf, room))
			L.blockOf[i] = cell.k;
	}

	for (int k = 0; k < teams; ++k)
	{
		// The trail (zigzagPath): out of the home down one side of the mountain, then leg after leg
		// across it from the outermost in, turning down the sides, and up onto the plateau.
		const double top = g.plateauR - g.trailHalf - 2;
		Zigzag trail = zigzagPath(L.frames[k], g.homeRadius - 0.3 * g.homeR, g.outerLeg, g.pitch,
								  g.legs, g.span, top, g.trailHalf);
		// The trail leaves from the home's middle, so it reaches the home however small the home is
		// beside the mountain's side.
		const ShapePoint middle = L.frames[k].at(g.homeRadius, 0);
		trail.path.insert(trail.path.begin(), {middle.x, middle.y, g.trailHalf});
		L.summit.push_back(L.frames[k].at(top, trail.finishAcross));
		std::vector<unsigned char> mask(n, 0);
		strokePath(mask, t, trail.path);
		for (int i = 0; i < n; ++i)
			if (mask[i])
				L.trailOf[i] = k;
		for (size_t j = 0; j < trail.legs.size(); ++j)
		{
			std::vector<unsigned char> leg(n, 0);
			strokePath(leg, t, trail.legs[j]);
			for (int i = 0; i < n; ++i)
				if (leg[i])
					L.legOf[i] = int(j);
		}
		if (g.roads)
			tracePath(L.road, t, trail.path);
		L.trails.push_back(trail.path);
		L.homes.push_back(polarPoint(L.cx, L.cy, g.homeRadius, L.axis[k]));
	}
	for (int k = 0; k < teams; ++k)
		// No pond in a home: its farms are its water, on every size of map.
		L.kitCentre.push_back(stampRoundHome(t, L.homes[k], L.axis[k], homeShape, g.homeR, 0,
											 &pondShape, L.pond, [&](int i) { L.homeOf[i] = k; }));
	fillShape(L.plateau, t, L.cx, L.cy, plateauShape);
	fillShape(L.pond, t, L.cx, L.cy, plateauPondShape);
	for (int i = 0; i < n; ++i)
	{
		const bool open = L.homeOf[i] >= 0 || L.plateau[i];
		if (open)
		{
			L.trailOf[i] = -1;
			L.legOf[i] = -1;
		}
		L.land[i] = open || L.blockOf[i] >= 0 || L.trailOf[i] >= 0;
		L.stone[i] = !open && L.blockOf[i] >= 0 && L.trailOf[i] < 0;
		if (L.trailOf[i] < 0)
			L.road[i] = 0;
	}

	// The mountains must keep water between each other and between each and every home but its own,
	// beyond their innermost legs; inside those the climbs come together at the plateau, and the
	// validator checks that no climb meets another's trail.
	for (int k = 0; k < teams && teams > 1; ++k)
	{
		std::vector<unsigned char> mine(n, 0);
		for (int i = 0; i < n; ++i)
			mine[i] = L.blockOf[i] == k || L.homeOf[i] == k || L.trailOf[i] == k;
		const std::vector<int> steps = stepsFrom(t, mine);
		for (int i = 0; i < n; ++i)
		{
			const int other = L.blockOf[i] >= 0   ? L.blockOf[i]
							  : L.trailOf[i] >= 0 ? L.trailOf[i]
												  : L.homeOf[i];
			// Chebyshev steps: two tiles of water between is enough for a beach on each side.
			if (other >= 0 && other != k && !L.plateau[i] && steps[i] < kPieceGap - 1 &&
				std::hypot(t.offsetX(int(L.cx), i % t.w), t.offsetY(int(L.cy), i / t.w)) >
					g.innerLeg + g.trailHalf)
			{
				L.failure = "Too many colonies for this map: the mountains crowd each other.";
				return L;
			}
		}
	}
	// The farms (growFarmFields): every home reaches out sideways, across its axis, to a farm on either
	// flank, and the farms share the open sea between the mountains and round the rim by equal yield. Their
	// rows run across the axis at the widths that yield most (bestFarmRows), with sand caps, bridges and a
	// building plot (layFarm), and every farm's coast is sealed like the rest.
	L.farmOf.assign(n, -1);
	L.farmSand.assign(n, 0);
	// Farms on every size of map. They were off below a half side of 100 (128 maps got two home ponds
	// instead) because a farm squeezed between the mountains kept losing a strip to its own coast walls;
	// FEEDBACK 2026-09-13: "on 128x128 on switchback, for some reason the farms lands are not
	// generating" - the fix is in growFarmFields (see the call below), not in going without.
	std::vector<ShapePoint> seeds, anchors;
	for (int k = 0; k < teams; ++k)
		for (const double side : {-1.0, 1.0})
		{
			seeds.push_back(L.frames[k].at(g.homeRadius, side * (g.homeReach + 2)));
			anchors.push_back(L.homes[k]);
		}
	const std::vector<unsigned char> everywhere(n, 1);
	const FarmPlot plot;
	// Rows run across every colony's axis; fields whose rows fall on the diagonal get more ground,
	// so every farm yields alike (farmYield).
	std::vector<int> owners;
	std::vector<double> angles;
	for (size_t f = 0; f < seeds.size(); ++f)
	{
		owners.push_back(int(f) / 2);
		angles.push_back(L.axis[f / 2] + kPi / 2);
	}
	// A field's strips narrower than a beach and a wall each side go, and only ground whose core joins
	// the home's core stays (growFarmFields): at 128x256 a strip joined to its field through an isthmus
	// three tiles wide was sealed off when the walls of the coasts either side met across it, a fifth
	// of the farm, so the farm-reach check failed and the lobby retried a quarter of its seeds. Opening
	// by the whole rim (kFarmRim) instead would drop every strip with no crop row in it, but on a 128 map
	// that is most of a farm squeezed between the mountains, and the size check below then fails.
	const std::vector<int> fields = growFarmFields(t, L.land, L.homeOf, everywhere, seeds, owners,
												   anchors, kFarmGap, kNeckHalf, angles);
	const double homeArea = kPi * g.homeR * g.homeR;
	for (int f = 0; f < 2 * teams; ++f)
	{
		const int k = f / 2;
		std::vector<unsigned char> region(n, 0);
		int size = 0;
		for (int i = 0; i < n; ++i)
			if (fields[i] == f)
			{
				region[i] = 1;
				++size;
			}
		if (size < kMinimumFarmShare * homeArea)
		{
			L.failure =
				"Too many colonies for this map: there is no room for every colony's farms.";
			return L;
		}
		const double across = L.axis[k] + kPi / 2;
		TerrainSketch rows(n, GRASS);
		L.farms.push_back(layFarm(rows, t, region, across, seeds[f], kFarmRim, bestFarmRows(across),
								  o.farmPlots ? &plot : nullptr, kFarmBridgeSpacing));
		L.farmColony.push_back(k);
		for (int i = 0; i < n; ++i)
			if (region[i] && L.homeOf[i] < 0)
			{
				L.farmOf[i] = k;
				L.land[i] = 1;
				if (L.farms.back().water[i])
					L.pond[i] = 1;
				if (L.farms.back().sand[i])
					L.farmSand[i] = 1;
			}
	}
	// A road keeps well inside the land, so its sand never meets a beach.
	std::vector<unsigned char> water(n, 0);
	for (int i = 0; i < n; ++i)
		water[i] = !L.land[i] || L.pond[i];
	keepRoadInland(L.road, t, water, kRoadSeaGap);
	L.roadTile = roadTiles(t, L.road);
	return L;
}

// The sea: every water vertex but the ponds'.
std::vector<unsigned char> seaMargin(const Map &map, const Layout &L)
{
	return islandSeaMargin(map, L.t, L.pond, L.roadTile, L.farmSand);
}

// The design's stone once the terrain is laid: every coast sealed, and every mountain's rock.
std::vector<unsigned char> stoneTiles(const Map &map, const Layout &L)
{
	return sealedIslandStone(map, L.t, seaMargin(map, L), L.land, L.stone);
}

void furnish(Map &map, const Layout &L, GenerationContext &context, const SwitchbacksOptions &o,
			 const std::vector<unsigned char> &pads)
{
	const Torus &t = L.t;
	const Geometry &g = L.g;
	const int n = t.size();
	const std::vector<unsigned char> reserved = swarmSurroundings(t, context);
	const Fertility::Field fertility = Fertility::forMap(map, false);
	const WedgeFrame wedges(t, L.phase - g.wedge / 2, g.teams);
	const WedgeField patch{wedges,
						   PeriodicNoise(t.w, t.h, 12, context.stream("switchbacks-patch"))};
	const WedgeField split{wedges, PeriodicNoise(t.w, t.h, 6, context.stream("switchbacks-split"))};
	std::vector<unsigned char> trails(n, 0);
	for (int i = 0; i < n; ++i)
		trails[i] = L.trailOf[i] >= 0;
	const std::vector<int> fromTrails = stepsFrom(t, trails);
	const auto free = [&](int i)
	{
		return !L.roadTile[i] && !reserved[i] && !pads[i] && fromTrails[i] > kTrailClearance &&
			   clearGround(map, i % t.w, i / t.w);
	};
	for (int k = 0; k < g.teams; ++k)
	{
		const auto eligible = [&](int i) { return L.homeOf[i] == k && free(i); };
		plantHomeKit(map, t, context, L.kitCentre[k], L.axis[k], g.homeR, kHomeWheat, kHomeWood,
					 eligible);
		furnishGround(
			map, t, context, fertility, eligible, patch, split,
			[&](int area)
			{
				return GroundAmounts{int(scaledCount(area * kHomeWheatShare / 100, o.wheat)),
									 int(scaledCount(area * kHomeWoodShare / 100, o.wood)), 0,
									 int(scaledCount(1, o.fruit))};
			},
			"switchbacks-home-stone", "switchbacks-home-fruit");
	}
	// The plateau: only fruit, so nothing overgrows its groves - in every wedge the orchard's three groves
	// on the pond's shore, halfway between one trail's summit and the next.
	const auto plateauFree = [&](int i) { return L.plateau[i] && free(i); };
	if (scaledCount(1, o.fruit) > 0)
	{
		std::vector<double> between;
		for (int k = 0; k < g.teams; ++k)
			between.push_back(L.axis[k] + g.wedge / 2);
		plantOrchard(map, t, context, L.cx, L.cy, g.plateauPondR + 4, between, 5, 5, 1,
					 plateauFree);
	}
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
				  [&](int i) { return L.farmOf[i] == k && free(i); });
	}
}

// The towers every colony starts with and the open pads beside them (chooseTowerSites), all built into
// its switchback: on the trail, against the inner side of each wall - the wall between the tower and the
// middle of the map - so each tower shoots across the stone at the next leg up, where attackers coming
// down from the plateau must pass. Sites keep clear of the walkway down the trail's middle and are chosen
// for how much of the colony's own trail they cover. With the control at 0 every site is an open pad.
TowerPlan planTowers(const Map &map, const Layout &L, const GenerationContext &context,
					 const SwitchbacksOptions &o, const std::vector<unsigned char> &stone)
{
	const Torus &t = L.t;
	const int n = t.size();
	// The walkway: the trail's centre line and a tile either side, which no tower may take.
	std::vector<unsigned char> centre(n, 0);
	for (int i = 0; i < n; ++i)
		centre[i] = L.road[i];
	if (!L.g.roads)
		for (int k = 0; k < L.g.teams; ++k)
			tracePath(centre, t, L.trails[k]);
	const std::vector<int> fromCentre = stepsFrom(t, centre);
	const std::vector<unsigned char> reserved = swarmSurroundings(t, context);
	// Against a wall on the side towards the middle of the map: rock within a couple of tiles that way.
	const auto facesInwardWall = [&](int i)
	{
		const double dx = t.offsetX(i % t.w, int(L.cx)), dy = t.offsetY(i / t.w, int(L.cy));
		const double d = std::max(1.0, std::hypot(dx, dy));
		for (int step = 1; step <= kWallHug; ++step)
			if (L.stone[t.at(int(std::lround(i % t.w + dx / d * step)),
							 int(std::lround(i / t.w + dy / d * step)))])
				return true;
		return false;
	};
	std::vector<int> owner(n, -1);
	std::vector<unsigned char> buildable(n, 0), target(n, 0);
	for (int i = 0; i < n; ++i)
	{
		const int x = i % t.w, y = i / t.w;
		owner[i] = L.homeOf[i] >= 0 ? L.homeOf[i] : L.farmOf[i] >= 0 ? L.farmOf[i] : L.trailOf[i];
		buildable[i] = L.trailOf[i] >= 0 && map.isGrass(x, y) && !stone[i] && !L.roadTile[i] &&
					   !reserved[i] && (fromCentre[i] < 0 || fromCentre[i] > kWalkway) &&
					   !map.isResource(x, y) && facesInwardWall(i);
		target[i] = L.trailOf[i] >= 0 && !map.isWater(x, y) && !stone[i];
	}
	TowerRequest request = startingTowerRequest(o.towers, o.towerCount, kTowerPads, kTowerSpacing);
	request.otherWeight = 0;
	request.ownWeight = 1;
	request.against = &stone;
	return chooseTowerSites(t, owner, buildable, target, swarmSurroundings(t, context, 0),
							L.g.teams, request);
}

bool generate(Game &game, GenerationContext &context)
{
	context.stage = "switchbacks layout";
	const SwitchbacksOptions o(context.request);
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

	context.stage = "switchbacks terrain";
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

	context.stage = "switchbacks colonies";
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
	if (!settleColonies(game, context, "switchbacks-starts", home, anchor))
		return false;

	context.stage = "switchbacks towers";
	TowerPlan towers = planTowers(map, L, context, o, stone);
	// No tower or pad may close a colony's trail to the plateau's heart; every colony keeps as many as the
	// fewest got, and a trail too narrow for any tower simply has none.
	std::vector<unsigned char> plateauHeart(n, 0);
	for (int i = 0; i < n; ++i)
		plateauHeart[i] = L.plateau[i] &&
						  std::hypot(t.offsetX(int(L.cx), i % t.w), t.offsetY(int(L.cy), i / t.w)) <
							  L.g.plateauPondR + 3;
	if (!settleStartingTowers(game, context, towers, o.towers, false, &plateauHeart))
		return false;
	const std::vector<unsigned char> pads = towerFootprints(t, towers);

	context.stage = "switchbacks resources";
	furnish(map, L, context, o, pads);
	const WedgeFrame algaeWedges(t, L.phase - L.g.wedge / 2, L.g.teams);
	seedAlgae(map, context, t, "switchbacks-algae", o.algae, AlgaeBand::shallows(1, 4),
			  &algaeWedges);
	secureStartingCrops(game, context, t, 24, 32, 0, &stone);
	reopenCrampedStarts(game, context, {o.wheat, o.wood, o.stone, o.algae, o.fruit}, 24, 32, 0,
						&stone);
	clearFarmPlots(map, t, L.farms);

	// Keep every trail open, clearing only deposits on it, from each home to the plateau's pond shore.
	context.stage = "switchbacks trails";
	const std::vector<std::vector<int>> workers = unitTilesByTeam(map, teams);
	std::vector<unsigned char> heart(n, 0);
	for (int i = 0; i < n; ++i)
		heart[i] = L.plateau[i] && !map.isWater(i % t.w, i / t.w) && !stone[i] &&
				   std::hypot(i % t.w - L.cx, i / t.w - L.cy) < L.g.plateauPondR + 3;
	for (int team = 0; team < teams; ++team)
		if (!workers[team].empty() && !openRoad(map, t, workers[team], heart, &stone))
		{
			context.detail = "colony " + std::to_string(team) + " has no trail to the plateau";
			return false;
		}
	return true;
}

std::string validateRequest(const GenerationRequest &r)
{
	if (r.nbTeams < 2)
		return "Switchbacks need at least two colonies.";
	// The geometry's own reason stays in the candidate's diagnostics; the player sees one message
	// that says what to change.
	return geometryFor(r).failure.empty()
			   ? ""
			   : "The switchbacks do not fit this map; use a bigger map, a smaller plateau, "
				 "narrower trails or fewer colonies.";
}

// Checked on the finished world against the rebuilt design: every designed stone stands; every colony
// walks to colony 0; nothing landing from the sea gets inside; with the trails shut no home reaches the
// plateau or another home; with only each trail's middle leg shut, the same (the walls between the
// legs hold, so no leg can be skipped); a tower at home reaches the first leg and one on the plateau the
// last; and the colonies' walks to the plateau are even.
std::string validateWorld(const Game &game, const GenerationContext &context)
{
	GenerationContext replay(context.request);
	const Layout L = design(context.request, replay);
	const Map &map = game.map;
	if (const std::string mismatch = designMismatch(L, map, "switchbacks"); !mismatch.empty())
		return mismatch;
	const Torus &t = L.t;
	const Geometry &g = L.g;
	const int n = t.size(), teams = context.request.nbTeams;
	const auto where = [&](int i)
	{ return "(" + std::to_string(i % t.w) + ", " + std::to_string(i / t.w) + ")"; };
	const std::vector<unsigned char> stone = stoneTiles(map, L);
	for (int i = 0; i < n; ++i)
		if (stone[i] && map.getResource(i % t.w, i / t.w).type != STONE)
			return "The stone at " + where(i) + " is missing.";
	const ColonyWalk walk = walkFromFirstColony(map, teams, "the switchbacks", "");
	if (!walk.error.empty())
		return walk.error;

	const std::vector<unsigned char> open = walkableTiles(map);
	const std::vector<unsigned char> margin = seaMargin(map, L);
	if (const int entry = seaEntry(map, t, margin, L.land); entry >= 0)
		return "Land at " + where(entry) + " can be reached from the sea.";

	std::vector<int> piece(n, -1);
	std::vector<unsigned char> trails(n, 0), middle(n, 0);
	for (int i = 0; i < n; ++i)
	{
		if (!margin[i])
			piece[i] = L.homeOf[i] >= 0   ? L.homeOf[i]
					   : L.farmOf[i] >= 0 ? L.farmOf[i]
					   : L.plateau[i]     ? teams
										  : -1;
		trails[i] = L.trailOf[i] >= 0;
		middle[i] = L.legOf[i] == g.legs / 2;
	}
	// Every farm is joined to its home: nearly all of its crop land, and all of its plot, is a walk from
	// its colony's workers.
	for (size_t f = 0; f < L.farms.size(); ++f)
		if (farmReachable(map, t, L.farms[f], walk.workers[L.farmColony[f]]) < kFarmReach)
			return "Colony " + std::to_string(L.farmColony[f]) +
				   " cannot walk into one of its farms.";
	if (const int leak = pieceLeak(map, t, piece, trails); leak >= 0)
		return "With the trails shut, " + where(leak) +
			   " can still be reached from another part of the map.";
	if (const int leak = pieceLeak(map, t, piece, middle); leak >= 0)
		return "With the middle legs shut, " + where(leak) +
			   " can still be reached: a wall between legs has a gap.";

	for (int k = 0; k < teams; ++k)
	{
		std::vector<unsigned char> homeGround(n, 0), plateauGround(n, 0), first(n, 0), last(n, 0);
		for (int i = 0; i < n; ++i)
		{
			const int x = i % t.w, y = i / t.w;
			const bool buildable = map.isGrass(x, y) && !map.isResource(x, y) && !L.roadTile[i];
			homeGround[i] = buildable && L.homeOf[i] == k;
			plateauGround[i] = buildable && L.plateau[i];
			first[i] = L.trailOf[i] == k && L.legOf[i] == 0;
			last[i] = L.trailOf[i] == k && L.legOf[i] == g.legs - 1;
		}
		if (towerReach(t, homeGround, first) > kTowerRange[kCoverTower])
			return "No tower at home " + std::to_string(k) + " reaches its trail's first leg.";
		if (towerReach(t, plateauGround, last) > kTowerRange[kCoverTower])
			return "No tower on the plateau reaches trail " + std::to_string(k) + "'s last leg.";
	}

	std::vector<int> targets;
	for (int k = 0; k < teams; ++k)
	{
		const int hx = int(std::lround(L.summit[k].x)), hy = int(std::lround(L.summit[k].y));
		const int seed = seedNear(t, hx, hy, 3, [&](int i) { return open[i] && !margin[i]; });
		targets.push_back(seed >= 0 ? seed : t.at(hx, hy));
	}
	const WalkSpread spread = walkSpread(map, t, walk.workers, targets);
	if (spread.unreached >= 0)
		return "Colony " + std::to_string(spread.unreached) + " cannot climb to the plateau.";
	if (spread.tooUneven(kWalkSpread))
		return "The colonies' walks to the plateau differ by " +
			   std::to_string(spread.longest - spread.shortest) + " steps.";
	return "";
}
} // namespace

SwitchbacksOptions::SwitchbacksOptions(const GenerationRequest &r)
	: trailWidth(r.option("trail-width")), legWall(r.option("leg-wall")),
	  plateauSize(r.option("plateau-size")), homeSize(r.option("home-size")),
	  towers(r.option("starting-towers")), towerCount(r.option("tower-count")),
	  sandRoads(r.option("sand-roads") != 0), farmPlots(r.option("farm-plots") != 0),
	  wheat(r.option("wheat-amount")), wood(r.option("wood-amount")),
	  stone(r.option("stone-amount")), algae(r.option("algae-amount")),
	  fruit(r.option("fruit-amount"))
{
}

GeneratorDefinition switchbacksDefinition()
{
	return {
		"switchbacks",
		24,
		"Switchbacks",
		2,
		false,
		// The trail's width and the stone between its legs in tiles; the plateau's radius as a
		// share of the half side (the mountains fill the rest with as many legs as fit); each home's
		// radius as a percentage of the standard.
		{{"trail-width", "Trail width", 5, 9, 2, 7, ControlGroup::Terrain},
		 {"leg-wall", "Wall between legs", 2, 6, 1, 2, ControlGroup::Terrain},
		 {"plateau-size", "Plateau size", 14, 34, 2, 22, ControlGroup::Layout},
		 {"home-size", "Home size", 60, 160, 10, 100, ControlGroup::Layout},
		 // The towers every colony starts with, all against its walls: their level (0 for none, just
		 // open pads) and how many.
		 {"starting-towers", "Starting tower level", 0, 3, 1, 2, ControlGroup::Layout},
		 {"tower-count", "Towers per colony", 0, 12, 1, 3, ControlGroup::Layout},
		 // Off, the trails are grass from wall to wall.
		 GeneratorControl::toggle("sand-roads", "Sand roads", true, ControlGroup::Layout),
		 // On, every farm has a 10x4 clearing of grass ringed with sand in its middle, for a
		 // swarm or an inn.
		 GeneratorControl::toggle("farm-plots", "Farm building plots", true, ControlGroup::Layout),
		 // Every home's scattered fields and grove, the farms' wheat and woodlots, the plateau's
		 // orchard, and the algae; every home's kit, the mountains' stone and the towers are unscaled.
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
