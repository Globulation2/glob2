// SPDX-License-Identifier: GPL-3.0-or-later
#include "FjordContinentGenerator.h"
#include "Distances.h"
#include "Game.h"
#include "GenerationContext.h"
#include "Geometry.h"
#include "GlobalContainer.h"
#include "Pipeline.h"
#include "Regions.h"
#include "Resources.h"
#include "Settlements.h"
#include "Topology.h"
#include "Unit.h"
#include <algorithm>
#include <cmath>
#include <limits>
using namespace MapGeneration;
// Fjord continent (id 12): one big continent in the middle of an ocean, cut by a fjord between
// every pair of neighbouring colonies, so each colony gets a peninsula of its own that hangs off a
// shared core. Written on this branch in September 2026 with the modular generator work; later
// passes added the ambient scatter and bank deposits, after playtesters found the map had lost
// personality with only counted deposits, and then the central lake and the lake-connected switch.
//
// THE DESIGN, IN GAME TERMS (docs/map-generators/GAME_RULES_FOR_MAP_DESIGN.md):
// - Home is the peninsula's tip, as far from the core as the peninsula allows. Neighbours are a
//   fjord apart: close as the crow flies, but ground units must walk the long way round through
//   the core until they can swim. A map where everyone is near everyone becomes one where
//   contact is a choice of route.
// - The fjords are the economy. Wheat and wood regrow only near water, and every fjord bank gets
//   both, so each peninsula is fed from the two fjords that flank it: the same count per colony,
//   whatever the jitter did to the shapes.
// - The core is the prize: several stone clumps and groves of all three fruits (an inn holding all
//   three pulls hungry enemy units across), round a lake with algae. In the default mode it is the
//   one place every colony can reach by land, so it is where fights happen.
// - Resource islands out at sea are a bonus for the first colony to swim.
//
// STAGES: stamp the continent, carve the fjords, carve the lake, raise the outlier islands, lay
// beaches, anchor the colonies, check the land links, then place resources (core, sea, starter
// kits, ambient scatter, fjord banks) and run the start guarantees last.
namespace
{
double randomAngle(GenerationContext &context)
{
	return context.bounded("layout", 3600) / 3600.0 * 2 * kPi;
}
void createJaggedIsland(Map &map, GenerationContext &context, std::vector<int> &grid, int area,
						int x, int y, int radius, double roughness)
{
	stampRoughDisc(grid, map.getW(), map.getH(), area, x, y, radius, roughness, context,
				   "outliers");
}

bool placeBankClump(Map &map, GenerationContext &context,
					const std::vector<MapGeneratorPoint> &centerline, double progress, int side,
					int resourceType)
{
	if (centerline.size() < 3)
		return false;
	const int target = int(progress * (centerline.size() - 1));
	for (int offset = 0; offset < int(centerline.size()); ++offset)
	{
		const int signedOffset = offset % 2 ? -(offset + 1) / 2 : offset / 2;
		const int i = std::max(1, std::min(int(centerline.size()) - 2, target + signedOffset));
		int tx = centerline[i + 1].x - centerline[i - 1].x;
		int ty = centerline[i + 1].y - centerline[i - 1].y;
		if (tx > map.getW() / 2)
			tx -= map.getW();
		else if (tx < -map.getW() / 2)
			tx += map.getW();
		if (ty > map.getH() / 2)
			ty -= map.getH();
		else if (ty < -map.getH() / 2)
			ty += map.getH();
		const double length = std::sqrt(double(tx * tx + ty * ty));
		if (length < 0.5)
			continue;
		const double nx = -ty / length * side, ny = tx / length * side;
		// Walk out from the fjord's centre line: the first steps are still water, and 16 is past
		// the widest fjord's bank (radius up to 12) and its beach.
		for (int distance = 2; distance <= 16; ++distance)
		{
			MapGeneratorPoint anchor(
				map.normalizeX(centerline[i].x + int(std::lround(nx * distance))),
				map.normalizeY(centerline[i].y + int(std::lround(ny * distance))));
			if (map.isResourceAllowed(anchor.x, anchor.y, resourceType) &&
				placeResourceClump(map, context, anchor, resourceType, 2) >= 3)
				return true;
		}
	}
	return false;
}

// Shared read-only geometry computed once at the start of generation and threaded through
// every step below: the continent's shape, its lake and fjord targets, and where along the
// rim each team is aimed.
struct FjordLayout
{
	int W, H, nbTeams;
	ShapeTransform xf;
	RadialShape coast;
	double coreR, lakeR, fjordInnerR, maxStretch;
	bool hasLake, lakeConnected;
	std::vector<double> teamTheta;
	// The continent is sized on the map's shorter side; on a rectangular map it is stretched along
	// the longer side as well, so it fills the map as an oval rather than a disc in the middle.
	Stretch fill;

	/// A map point in the continent's own round frame, and back: xf's rotation and elongation, with
	/// the fill stretch undone first and applied last.
	ShapePoint toShape(ShapePoint p) const
	{
		if (fill.sx == 1 && fill.sy == 1)
			return xf.toShape(p);
		const ShapePoint round = fill.undo(p.x - W / 2.0, p.y - H / 2.0);
		return xf.toShape({W / 2.0 + round.x, H / 2.0 + round.y});
	}
	ShapePoint toMap(ShapePoint q) const { return fill.apply(W / 2.0, H / 2.0, xf.toMap(q)); }
};

FjordLayout computeLayout(Game &game, GenerationContext &context,
						  const FjordContinentOptions &options, int nbTeams)
{
	const int W = game.map.getW();
	const int H = game.map.getH();

	// An oval looks the same turned half a circle, so half a turn covers every orientation. The
	// stretch, 1 to 1.3, keeps the continent from looking stamped from a circle without letting
	// peninsulas on the long axis grow much longer than those on the short one.
	const double rotation = randomAngle(context) * 0.5;
	const double elongation = 1.0 + context.bounded("layout", 1000) / 1000.0 * 0.3;
	ShapeTransform xf({W / 2.0, H / 2.0}, rotation, elongation);

	// Continent size is the radius as a percentage of the shorter side: the default 34 gives a
	// continent 68% of the map across (174 tiles on a 256 map). The coast wobbles by four harmonics
	// (2, 3, 5 and 7 lobes) whose amplitudes add up to at most roughness x 1.4, so at the default
	// roughness 22 the coast can bulge 31% past the base radius in its worst direction. That is why
	// the size range stops at 40 and why the outlier islands below test the coast point by point.
	const double baseR = options.continentSize / 100.0 * std::min(W, H);
	RadialShape coast(baseR, options.roughness / 100.0, context, "coast", 1.4);
	// The core: the disc every fjord stops short of, so it always stays one piece of land. 0.23 of
	// the radius (20 tiles on the default 256 map) leaves the fjords three quarters of the radius
	// to run, which is the length of each peninsula, while making room for the lake, its sand ring
	// and the core's deposits (it was 0.19 before the lake existed).
	const double coreR = baseR * 0.23;
	// lake-size is a percentage of coreR; 0 disables the lake entirely. lake-connected decides
	// whether the fjords actually cut through to it (every peninsula then water-isolated from its
	// neighbors, boats required) or stop short of it behind a solid land ring (every peninsula
	// stays mutually land-connected, verified explicitly in verifyConnectivity rather than
	// assumed) - both are legitimate map styles, so both stay available rather than picking one
	// permanently.
	const double lakeR = coreR * (options.lakeSize / 100.0);
	const bool hasLake = lakeR > 0.5;
	const bool lakeConnected = hasLake && options.lakeConnected != 0;
	// Where every fjord actually stops: coreR in disconnected mode. In connected mode, well inside
	// the lake (0.4x its radius) rather than exactly on its edge, so the centerline crosses the
	// lake's real boundary with margin to spare - the extra length beyond the boundary just carves
	// more water inside ground that's already the lake, which is harmless.
	const double fjordInnerR = lakeConnected ? lakeR * 0.4 : coreR;
	const double maxStretch = std::max(elongation, 1.0 / elongation);

	// One angular sector per team, evenly spaced with a little jitter so it
	// doesn't look mechanical. Deciding sectors up front (rather than deriving
	// neighbor order from wherever seed points happen to land) is what guarantees
	// every team gets pushed out to its own tip instead of sometimes landing near
	// the shared middle.
	std::vector<double> teamTheta(nbTeams);
	{
		double baseRotation = randomAngle(context);
		double slot = 2 * kPi / nbTeams;
		for (int i = 0; i < nbTeams; ++i)
		{
			double jitter = ((context.bounded("layout", 2001)) / 1000.0 - 1.0) * 0.2 * slot;
			teamTheta[i] = baseRotation + i * slot + jitter;
		}
	}

	return FjordLayout{W,           H,          nbTeams, xf,
					   coast,       coreR,      lakeR,   fjordInnerR,
					   maxStretch,  hasLake,    lakeConnected, teamTheta,
					   Stretch::toFill(W, H)};
}

// 1) Stamp the continent.
void stampContinent(Game &game, const FjordLayout &layout)
{
	for (int y = 0; y < layout.H; ++y)
	{
		for (int x = 0; x < layout.W; ++x)
		{
			const auto shaped = layout.toShape({double(x), double(y)});
			double u = shaped.x, v = shaped.y;
			double theta = atan2(v, u);
			double r = sqrt(u * u + v * v);
			if (r < layout.coast.radiusAt(theta))
				game.map.setUMatPos(x, y, GRASS, 1);
		}
	}
}

// 2) Carve a fjord between every pair of angularly-neighboring teams: a
// smooth S-curve from just outside the coast at the sector boundary, in to a
// fixed inner radius (coreR) that every fjord stops short of -- so the disc
// r<coreR is never touched and always stays connected land, the "palm" every
// peninsula "finger" hangs off. Lateral wiggle is enveloped to zero at both
// ends so the mouth stays aligned with the sector boundary and the tip stays
// on the radial line.
std::vector<std::vector<MapGeneratorPoint>> carveFjords(Game &game, GenerationContext &context,
														const FjordLayout &layout,
														const FjordContinentOptions &options)
{
	std::vector<std::vector<MapGeneratorPoint>> fjordCenterlines(layout.nbTeams);
	if (layout.nbTeams < 2)
		return fjordCenterlines;
	for (int k = 0; k < layout.nbTeams; ++k)
	{
		int i = k;
		int j = (k + 1) % layout.nbTeams;
		double ti = layout.teamTheta[i];
		double tj = layout.teamTheta[j];
		double d = fmod(tj - ti, 2 * kPi);
		if (d < 0)
			d += 2 * kPi;
		double midTheta = ti + d / 2.0;

		double mouthR = layout.coast.radiusAt(midTheta) + 3.0;
		double mouthU = mouthR * cos(midTheta), mouthV = mouthR * sin(midTheta);
		double tipU = layout.fjordInnerR * cos(midTheta), tipV = layout.fjordInnerR * sin(midTheta);
		double perpU = -sin(midTheta), perpV = cos(midTheta);

		// The S-curve's sideways swing: 17% of the arc between the two colonies' tips at the coast.
		// It peaks at mid-length, where the arc is only about 60% as long, so two fjords swinging
		// toward each other can narrow the peninsula between them to under half its width, but not
		// cut it; much more and a peninsula could be pinched to a strip too thin to build on.
		double gap = std::min(d, 2 * kPi - d);
		double amplitude = gap * mouthR * 0.17;
		double phase = randomAngle(context);
		// The mouth's radius: fjord width plus 0.6 to 2.0, so fjords differ a little from each
		// other (the default 4 gives mouths 9 to 12 tiles across). Water that wide survives
		// controlSand, and ground units cannot cross it until the colony can swim.
		double mouthWidth = options.fjordWidth + 0.6 + context.bounded("layout", 1400) / 1000.0;
		// Map::controlSand (MapTerrain.cpp) converts any water tile with a grass neighbor in its
		// own 3x3 neighborhood to sand - not just a coastal decoration, it can erase a channel
		// outright. The disconnected-mode tip (0.65, ~1.3 tiles across) is entirely coastal by that
		// rule and gets sanded over - an intentional dead end there, but it would silently close
		// the one connection lake-connected mode needs open. A channel needs a surviving center row
		// not touching grass on either side to stay water at all, which takes a radius of at least
		// ~1.5; this targets comfortably above that floor.
		double tipWidth = layout.lakeConnected ? std::max(2.5, mouthWidth * 0.6) : 0.65;

		// About one disc per tile of the fjord's straight length (at least 24), so consecutive
		// discs overlap into one channel even where the tip is narrow and the curve swings.
		int steps = std::max(24, (int)(mouthR - layout.fjordInnerR));
		for (int s = 0; s <= steps; ++s)
		{
			double t = double(s) / steps;
			double baseU = mouthU + t * (tipU - mouthU);
			double baseV = mouthV + t * (tipV - mouthV);
			double envelope = sin(kPi * t);
			double lateral = amplitude * sin(2 * kPi * t + phase) * envelope;
			double u = baseU + lateral * perpU;
			double v = baseV + lateral * perpV;
			double width = mouthWidth * (1 - t) + tipWidth * t;

			const auto mapped = layout.toMap({u, v});
			double mx = mapped.x, my = mapped.y;
			// The square scanned round each disc: the disc's radius in shape space, widened by the
			// most any stretch could enlarge it on the map, plus a tile of margin.
			int rad = (int)ceil(width * layout.maxStretch * layout.fill.longest()) + 1;
			int ix = (int)lround(mx), iy = (int)lround(my);
			fjordCenterlines[k].push_back(
				MapGeneratorPoint(game.map.normalizeX(ix), game.map.normalizeY(iy)));
			for (int dy = -rad; dy <= rad; ++dy)
			{
				for (int dx = -rad; dx <= rad; ++dx)
				{
					int nx = game.map.normalizeX(ix + dx);
					int ny = game.map.normalizeY(iy + dy);
					const auto shaped = layout.toShape({double(nx), double(ny)});
					double u2 = shaped.x, v2 = shaped.y;
					double dd = (u2 - u) * (u2 - u) + (v2 - v) * (v2 - v);
					if (dd <= width * width)
					{
						game.map.setUMatPos(nx, ny, WATER, 1);
					}
				}
			}
		}
	}
	return fjordCenterlines;
}

// 3) A lake at the very center every fjord points toward (lake-size 0 skips this entirely).
// In disconnected mode the fjords stop at coreR, well outside lakeR, leaving the coreR-lakeR
// ring solid - every peninsula stays mutually land-connected around the lake's edge
// (verifyConnectivity checks this explicitly). In connected mode the fjords already reach
// well inside lakeR (fjordInnerR, computed in computeLayout), so this carve is what actually
// opens each fjord into the lake - the fjords' own tapered tips would otherwise be too narrow
// to reliably merge with it.
void carveLake(Game &game, const FjordLayout &layout, bool sandyShore)
{
	if (!layout.hasLake)
		return;
	for (int y = 0; y < layout.H; ++y)
	{
		for (int x = 0; x < layout.W; ++x)
		{
			const auto shaped = layout.toShape({double(x), double(y)});
			double u = shaped.x, v = shaped.y;
			if (u * u + v * v <= layout.lakeR * layout.lakeR)
				game.map.setUMatPos(x, y, WATER, 1);
		}
	}

	// A sandy no-man's-land ring just outside the lake - wider than controlSand's own thin
	// coastal fringe would give it, so the open ground around the lake reads as a deliberate
	// contested space rather than an ordinary beach. Only touches tiles the lake/fjord carving
	// above left as land, so it never overwrites water. Without it, the lake has an ordinary
	// beach and grass to build and farm on right up to it.
	if (!sandyShore)
		return;
	// Out to one and a half lake radii: at the defaults a lake about 9 tiles in radius gets sand
	// out to about 14, and the core's grass beyond it (to coreR + 4, about 24) holds its deposits.
	const double sandOuterR = layout.lakeR + layout.lakeR * 0.5;
	for (int y = 0; y < layout.H; ++y)
	{
		for (int x = 0; x < layout.W; ++x)
		{
			if (game.map.isWater(x, y))
				continue;
			const auto shaped = layout.toShape({double(x), double(y)});
			double u = shaped.x, v = shaped.y;
			if (u * u + v * v <= sandOuterR * sandOuterR)
				game.map.setUMatPos(x, y, SAND, 1);
		}
	}
}

// 4) A couple of small, unconnected resource islands out in the open sea --
// purely a bonus for whoever explores, never touching the mainland or each
// other.
void placeOutlierIslands(Game &game, GenerationContext &context, const FjordLayout &layout,
						 const FjordContinentOptions &options, std::vector<int> &grid,
						 int &areaNumber)
{
	// A single global "mainland reach" bound has to stay safe in whichever single direction
	// roughness and elongation happen to push the coastline furthest, and that alone can exceed
	// half the map (continent-size up to 40%, roughness up to 35%, elongation up to 1.3x) on a
	// majority of rolls regardless of map size - a global bound leaves islands unplaced almost
	// always, not just at small sizes. Each island's own center is independently randomized,
	// though, so it only needs to be safe in the coastline's direction at its own location, not
	// everywhere. Checking each candidate directly against coast.radiusAt() there (the same
	// per-point technique placeOpenSeaAlgae's placement already uses for its own shoreline check,
	// via the same xf.toShape transform) replaces the pessimistic global bound with an exact
	// local one, so an island can land close to a narrow stretch of coast even while the
	// coastline bulges out far away in some other direction.
	// Island centres are drawn out to half the shorter side, less 6 tiles (stretched on a
	// rectangle): the ocean between the continent and its own wrapped image across the map edge.
	double halfMapMargin = std::min(layout.W, layout.H) / 2.0 - 6.0;
	// No cap beyond the control's own range here - resource-islands now goes up to 20 for
	// players who want an island-heavy map, and each one is still an independent best-effort
	// placement (a request that can't all fit in the margin just places as many as do).
	int outlierCount = options.resourceIslands + int(context.bounded("layout", 2));
	std::vector<MapGeneratorPoint> outlierCenters;
	std::vector<int> outlierRadii;
	for (int oi = 0; oi < outlierCount; ++oi)
	{
		// Radius 5 to 8: room for one themed deposit and a small outpost. Each island keeps 8 tiles
		// of open water from the coast and from other islands, more than beaches could bridge, so
		// it is only reachable by swimming.
		int islandRadius = 5 + context.bounded("layout", 4);
		bool placed = false;
		for (int attempt = 0; attempt < 60 && !placed; ++attempt)
		{
			double theta = randomAngle(context);
			double r = (context.bounded("layout", 1000)) / 1000.0 * halfMapMargin;
			int cx =
				game.map.normalizeX((int)lround(layout.W / 2.0 + r * cos(theta) * layout.fill.sx));
			int cy =
				game.map.normalizeY((int)lround(layout.H / 2.0 + r * sin(theta) * layout.fill.sy));

			const auto shaped = layout.toShape({double(cx), double(cy)});
			const double shapeR = std::hypot(shaped.x, shaped.y);
			const double shapeTheta = atan2(shaped.y, shaped.x);
			if (shapeR < layout.coast.radiusAt(shapeTheta) + islandRadius + 8.0)
				continue; // too close to (or inside) the mainland at this specific point

			bool clear = true;
			for (unsigned int oc = 0; oc < outlierCenters.size() && clear; ++oc)
			{
				int minSep = islandRadius + outlierRadii[oc] + 8;
				if (game.map.warpDistSquare(cx, cy, outlierCenters[oc].x, outlierCenters[oc].y) <
					minSep * minSep)
					clear = false;
			}
			if (!clear)
				continue;

			int outlierArea = areaNumber++;
			createJaggedIsland(game.map, context, grid, outlierArea, cx, cy, islandRadius, 0.3);
			std::vector<MapGeneratorPoint> islandPts;
			getAllPoints(game.map, grid, outlierArea, islandPts);
			if (islandPts.empty())
				continue;
			for (unsigned int p = 0; p < islandPts.size(); ++p)
				game.map.setUMatPos(islandPts[p].x, islandPts[p].y, GRASS, 1);

			// Each island leans on one resource theme, so finding one feels like a
			// distinct little prize rather than an interchangeable resource dump.
			switch (context.bounded("layout", 3))
			{
			case 0:
				placeResourceClump(game.map, context,
								   islandPts[context.bounded("resources", islandPts.size())], STONE,
								   2);
				break;
			case 1:
				placeResourceClump(game.map, context,
								   islandPts[context.bounded("resources", islandPts.size())],
								   CHERRY + context.bounded("resources", 3), 2);
				break;
			default:
				placeResourceClump(game.map, context,
								   islandPts[context.bounded("resources", islandPts.size())], CORN,
								   3);
				break;
			}

			outlierCenters.push_back(MapGeneratorPoint(cx, cy));
			outlierRadii.push_back(islandRadius);
			placed = true;
		}
	}
}

// 5) Anchor each team out at its own tip: start just inland of the coast at
// the team's angle and only back off toward the core if that exact spot turns
// out to be water (a sharp jaggedness dip, or a fjord belly that swung wider
// than expected) -- the same "compute where we want to be, then confirm the
// grid agrees" shape as chooseFreeForBuildingSquares.
std::vector<MapGeneratorPoint> anchorTeams(Game &game, const FjordLayout &layout)
{
	std::vector<MapGeneratorPoint> teamPts;
	teamPts.reserve(layout.nbTeams);
	for (int i = 0; i < layout.nbTeams; ++i)
	{
		double theta = layout.teamTheta[i];
		double maxR = layout.coast.radiusAt(theta);
		// 8 tiles in from the coast: room for the swarm and its clear ring with the sea at its
		// back. Stepping inward (at most 30 steps, stopping 2 tiles outside the core) is only for
		// when that tile turned out to be water.
		double r = std::max(layout.coreR + 6.0, maxR - 8.0);
		double stepIn = (maxR - (layout.coreR + 2.0)) / 30.0;
		if (stepIn <= 0.0)
			stepIn = 1.0;

		int fx = -1, fy = -1;
		for (int tries = 0; tries < 30; ++tries)
		{
			const auto mapped = layout.toMap({r * cos(theta), r * sin(theta)});
			double mx = mapped.x, my = mapped.y;
			int ix = game.map.normalizeX((int)lround(mx));
			int iy = game.map.normalizeY((int)lround(my));
			if (!game.map.isWater(ix, iy))
			{
				fx = ix;
				fy = iy;
				break;
			}
			r -= stepIn;
		}
		if (fx < 0)
		{
			const auto mapped = layout.toMap(
				{(layout.coreR + 6.0) * cos(theta), (layout.coreR + 6.0) * sin(theta)});
			double mx = mapped.x, my = mapped.y;
			fx = game.map.normalizeX((int)lround(mx));
			fy = game.map.normalizeY((int)lround(my));
		}
		teamPts.push_back(MapGeneratorPoint(fx, fy));
	}
	return teamPts;
}

// 6) Connectivity: verify, don't assume. The untouched core should make this
// unreachable in practice, but every other generator checks its own
// invariants explicitly instead of trusting the construction, so this does
// too. In lake-connected mode every peninsula is water-isolated from its neighbors by
// design - failing here would just reject every map that mode ever produces - so this check
// only runs in the default, disconnected mode. Each peninsula's own viability is still
// verified locally further down (placeStarterKits fails outright if a team's home area comes
// up empty).
bool verifyConnectivity(Game &game, const FjordLayout &layout,
						const std::vector<MapGeneratorPoint> &teamPts)
{
	if (layout.lakeConnected)
		return true;
	const int W = layout.W, H = layout.H;
	std::vector<bool> visited(W * H, false);
	std::vector<MapGeneratorPoint> stack;
	stack.push_back(teamPts[0]);
	visited[teamPts[0].y * W + teamPts[0].x] = true;
	while (!stack.empty())
	{
		MapGeneratorPoint p = stack.back();
		stack.pop_back();
		for (int dy = -1; dy <= 1; ++dy)
		{
			for (int dx = -1; dx <= 1; ++dx)
			{
				if (dx == 0 && dy == 0)
					continue;
				int nx = game.map.normalizeX(p.x + dx);
				int ny = game.map.normalizeY(p.y + dy);
				if (!visited[ny * W + nx] && !game.map.isWater(nx, ny))
				{
					visited[ny * W + nx] = true;
					stack.push_back(MapGeneratorPoint(nx, ny));
				}
			}
		}
	}
	for (int i = 0; i < layout.nbTeams; ++i)
		if (!visited[teamPts[i].y * W + teamPts[i].x])
			return false;
	return true;
}

// 7) Stone and fruit in the ring around the new central lake -- the reward for pushing to the
// middle of the map instead of staying home. Several stone clumps rather than one, and every
// fruit type instead of a single random pick, so finding this area feels like a genuinely rich
// destination and not a single repeated deposit.
void placeCoreResources(Game &game, GenerationContext &context, const FjordLayout &layout,
						const FjordContinentOptions &options, std::vector<int> &grid,
						int &areaNumber)
{
	int coreArea = areaNumber++;
	std::vector<MapGeneratorPoint> corePts;
	for (int y = 0; y < layout.H; ++y)
	{
		for (int x = 0; x < layout.W; ++x)
		{
			// Corn/stone/fruit all require grass, so the sand ring the lake just grew (when there
			// is one) is deliberately excluded here rather than merely non-water - a clump center
			// landing on sand could miss every grass tile within its own radius and place nothing.
			if (!game.map.isGrass(x, y) || grid[y * layout.W + x] != 0)
				continue;
			const auto shaped = layout.toShape({double(x), double(y)});
			double u = shaped.x, v = shaped.y;
			// The core plus 4 tiles, so deposits also land where the peninsulas join it.
			if (u * u + v * v <= (layout.coreR + 4.0) * (layout.coreR + 4.0))
			{
				grid[y * layout.W + x] = coreArea;
				corePts.push_back(MapGeneratorPoint(x, y));
			}
		}
	}
	if (!corePts.empty())
	{
		for (int stoneClump = 0; stoneClump < scaledCount(3, options.stone); ++stoneClump)
			placeResourceClump(game.map, context,
							   corePts[context.bounded("resources", corePts.size())], STONE, 3);
		const int fruitClumpsPerType = int(scaledCount(3, options.fruit));
		for (int fruitType = 0; fruitType < 3; ++fruitType)
			for (int clump = 0; clump < fruitClumpsPerType; ++clump)
				placeResourceClump(game.map, context,
								   corePts[context.bounded("resources", corePts.size())],
								   CHERRY + fruitType, 2);
	}

	// The lake gets the same algae treatment the open sea gets in placeOpenSeaAlgae below, but
	// candidates are drawn from well inside the shoreline (innerLakeR, not lakeR) rather than
	// anywhere in the lake - a clump anchored right up against the shore is still entirely valid
	// water, but reads as "stuck to one side" rather than "in the lake". The very first clump is
	// placed dead center, at the shape transform's own origin - the lake's exact geometric middle
	// - so there's always at least one unambiguously centered deposit regardless of how the
	// interior sampling below happens to land.
	if (layout.hasLake && options.algae > 0)
	{
		const int centerX = game.map.normalizeX((int)std::lround(layout.W / 2.0));
		const int centerY = game.map.normalizeY((int)std::lround(layout.H / 2.0));
		placeResourceClump(game.map, context, MapGeneratorPoint(centerX, centerY), ALGA, 2);

		const double innerLakeR = std::max(0.0, layout.lakeR - 4.0);
		std::vector<MapGeneratorPoint> lakeWater;
		for (int y = 0; y < layout.H; ++y)
			for (int x = 0; x < layout.W; ++x)
			{
				if (!game.map.isWater(x, y))
					continue;
				const auto shaped = layout.toShape({double(x), double(y)});
				double u = shaped.x, v = shaped.y;
				if (u * u + v * v <= innerLakeR * innerLakeR)
					lakeWater.push_back(MapGeneratorPoint(x, y));
			}
		// The center clump above already guarantees at least one, so this is purely bonus
		// coverage for a lake big enough to have real interior room left over - no forced minimum.
		// One more clump per 40 tiles of the lake's interior: algae spreads round a clump where it
		// can grow, so a sparse seeding is enough.
		if (!lakeWater.empty())
			for (int i = 0; i < scaledCount(int(lakeWater.size()) / 40, options.algae); ++i)
				placeResourceClump(game.map, context,
								   lakeWater[context.bounded("resources", lakeWater.size())], ALGA,
								   2);
	}
}

// 8) Algae out in the open sea: any water tile clearly beyond the coastline
// (not a fjord, not the moat-ish water right against the shore) gets an
// occasional patch.
void placeOpenSeaAlgae(Game &game, GenerationContext &context, const FjordLayout &layout,
					   int algaePercent)
{
	std::vector<MapGeneratorPoint> algaeWater;
	for (int y = 0; y < layout.H; ++y)
	{
		for (int x = 0; x < layout.W; ++x)
		{
			if (!game.map.isWater(x, y))
				continue;
			const auto shaped = layout.toShape({double(x), double(y)});
			double u = shaped.x, v = shaped.y;
			double theta = atan2(v, u);
			double r = sqrt(u * u + v * v);
			double shoreR = layout.coast.radiusAt(theta);
			// 2 to 14 tiles off the coast: past the beach, yet near enough that algae's growth
			// probe (water within 15 tiles, sand at the doubled offset within 30) can find the
			// beach, so patches regrow rather than being mined out for good.
			if (r > shoreR + 2.0 && r < shoreR + 14.0)
				algaeWater.emplace_back(x, y);
		}
	}
	if (!algaeWater.empty())
		// One clump per 180 tiles of the band, at least one: a few patches along each stretch of
		// coast, since algae is for upgrades, not everyday food.
		for (int i = 0; i < scaledCount(std::max(1, int(algaeWater.size()) / 180), algaePercent);
			 ++i)
			placeResourceClump(game.map, context,
							   algaeWater[context.bounded("resources", algaeWater.size())], ALGA,
							   2);
}

// 9) A light per-team starter kit so nobody is stuck waiting to reach the
// fjord banks before they can build anything; the banks and the core are the
// map's real economy. The kit is a small clump of wheat, a small clump of wood and one stone, on
// land within 10 steps of the swarm (walking round water, not across it).
bool placeStarterKits(Game &game, GenerationContext &context, const FjordLayout &layout,
					  const std::vector<MapGeneratorPoint> &teamPts)
{
	std::vector<MapGeneratorPoint> allWater;
	for (int y = 0; y < layout.H; ++y)
		for (int x = 0; x < layout.W; ++x)
			if (game.map.isWater(x, y))
				allWater.push_back(MapGeneratorPoint(x, y));

	for (int i = 0; i < layout.nbTeams; ++i)
	{
		std::vector<MapGeneratorPoint> sources;
		sources.push_back(teamPts[i]);
		std::vector<int> distances;
		computeDistances(game.map, sources, allWater, distances);

		std::vector<MapGeneratorPoint> homePoints;
		for (int y = 0; y < layout.H; ++y)
			for (int x = 0; x < layout.W; ++x)
				if (distances[y * layout.W + x] >= 1 && distances[y * layout.W + x] <= 10)
					homePoints.push_back(MapGeneratorPoint(x, y));
		if (homePoints.empty())
			return false;

		placeResourceClump(game.map, context,
						   homePoints[context.bounded("resources", homePoints.size())], CORN, 2);
		placeResourceClump(game.map, context,
						   homePoints[context.bounded("resources", homePoints.size())], WOOD, 2);

		std::vector<MapGeneratorPoint> stonePts = homePoints;
		chooseRandomPoints(game.map, context, stonePts, 1);
		for (unsigned int j = 0; j < stonePts.size(); ++j)
			game.map.setResource(stonePts[j].x, stonePts[j].y, STONE, 1);

		std::vector<unsigned char> home(size_t(layout.W) * layout.H, 0);
		for (const auto &point : homePoints)
			home[point.y * layout.W + point.x] = 1;
		if (!placeSettlement(game, context, i, home, teamPts[i], "starts"))
			return false;
	}
	return true;
}

// 11) Place bank resources last so settlement and regional deposits cannot
// overwrite the guarantee. Every side of every fjord receives both resources
// at distinct points along its length. Progress runs from the mouth (0) to the inner tip (1):
// wheat a third of the way in, nearer the colony at its peninsula's outer end, and wood two thirds
// in, toward the core. Each peninsula is flanked by two fjords, so every colony gets two wheat and
// two wood deposits on its own banks.
bool placeBankResources(Game &game, GenerationContext &context, const FjordLayout &layout,
						const std::vector<std::vector<MapGeneratorPoint>> &fjordCenterlines,
						const FjordContinentOptions &options)
{
	if (layout.nbTeams < 2)
		return true;
	for (int k = 0; k < layout.nbTeams; ++k)
	{
		for (int side : {-1, 1})
			if (!placeBankClump(game.map, context, fjordCenterlines[k], 0.35, side, CORN) ||
				!placeBankClump(game.map, context, fjordCenterlines[k], 0.68, side, WOOD))
				return false;
		if (!options.bankDeposits)
			continue;

		// A handful more, lighter clumps at other points along the same banks besides
		// the two guaranteed spots above, so walking a fjord's edge feels like following
		// a shoreline with its own economy rather than passing exactly two fixed
		// deposits. Best-effort: unlike the guarantee above, nothing downstream depends
		// on any single one of these existing, so a spot that doesn't pan out is simply
		// skipped rather than failing generation. A small jitter on each progress value
		// keeps the spacing from reading as mechanically regular.
		// Two points in each stretch between the mouth, the wheat at 0.35, the wood at 0.68 and the
		// tip, so the extras never land on the guaranteed deposits.
		static const double bankScatterProgress[] = {0.10, 0.22, 0.48, 0.58, 0.80, 0.92};
		for (double progress : bankScatterProgress)
		{
			const double jitter = (context.bounded("resources", 41) - 20) / 1000.0; // +/-0.02
			for (int side : {-1, 1})
			{
				// wheat:wood at 2:1 (4 and 2 of 8) with stone 2 of 8, the same ratio as the ambient
				// scatter in generate: in playtesting an even split read as too much wood.
				const int roll = context.bounded("resources", 8);
				const int resourceType = roll < 4 ? CORN : roll < 6 ? WOOD : STONE;
				// The amount controls place each rolled clump that many hundredths of a time: whole
				// copies, and one more by chance from a stream of its own, so the rolls stay the
				// same.
				const int percent = resourceType == CORN   ? options.wheat
									: resourceType == WOOD ? options.wood
														   : options.stone;
				int copies = percent / 100;
				if (percent % 100 != 0 &&
					int(context.bounded("fjord-bank-amounts", 100)) < percent % 100)
					++copies;
				for (int copy = 0; copy < copies; ++copy)
					placeBankClump(game.map, context, fjordCenterlines[k], progress + jitter, side,
								   resourceType);
			}
		}
	}
	return true;
}

static bool generate(Game &game, GenerationContext &context)
{
	context.stage = "continent";
	const FjordContinentOptions options(context.request);
	game.map.makeHomogenMap(WATER);
	for (int i = 0; i < context.request.nbTeams; ++i)
		game.addTeam();

	const int nbTeams = context.request.nbTeams;
	if (nbTeams < 1)
		return false;

	const FjordLayout layout = computeLayout(game, context, options, nbTeams);

	// grid holds area numbers for resource zoning, kept alongside the real
	// terrain (which is written straight onto game.map as we go, the same way
	// computeIsles and computeContestedCommons do it).
	std::vector<int> grid(layout.W * layout.H, 0);
	int areaNumber = 1;

	stampContinent(game, layout);
	std::vector<std::vector<MapGeneratorPoint>> fjordCenterlines =
		carveFjords(game, context, layout, options);
	carveLake(game, layout, options.sandyLakeShore);
	placeOutlierIslands(game, context, layout, options, grid, areaNumber);

	game.map.controlSand();

	std::vector<MapGeneratorPoint> teamPts = anchorTeams(game, layout);
	if (!verifyConnectivity(game, layout, teamPts))
		return false;

	placeCoreResources(game, context, layout, options, grid, areaNumber);
	placeOpenSeaAlgae(game, context, layout, options.algae);

	if (!placeStarterKits(game, context, layout, teamPts))
		return false;

	// 10) Every resource so far is a deliberate, counted placement tied to a specific
	// purpose: a home starter kit, the core, an outlier island. That leaves the whole
	// continent interior in between them bare grass, which reads as empty rather than as a
	// place with its own history the way a noise-painted map does. A light map-wide scatter
	// fills that gap with
	// ordinary, unclaimed deposits. This runs after every swarm and worker is already placed,
	// not before: isResourceAllowed refuses any tile with a building or unit on it, so a
	// scatter placed earlier can claim the one remaining tile a colony's swarm footprint
	// needed, failing generation outright. Running last costs nothing here -- setResource
	// always overwrites, so a scatter clump landing on an earlier deposit just loses that one
	// tile to it, the same low-stakes trade every other generator's own layering already
	// makes. Algae is left at zero: the shoreline band in placeOpenSeaAlgae already places it
	// with a shape tuned to the coastline, and scattering more over open water would just
	// fight that.
	// wheat:wood at 2:1: in playtesting an even split (18 and 18) read as too much wood, so the
	// same total of 36 was reshared rather than more wheat added on top. The numbers are densities
	// that scatterResources scales by map area (wheat and wood per 1600 tiles, stone per 3000).
	scatterResources(game, context,
					 {/*corn=*/int(scaledCount(24, options.wheat)),
					  /*wood=*/int(scaledCount(12, options.wood)),
					  /*stone=*/int(scaledCount(10, options.stone)), /*algae=*/0,
					  /*fruit=*/int(scaledCount(3, options.fruit))});

	context.stage = "fjord bank resources";
	if (!placeBankResources(game, context, layout, fjordCenterlines, options))
		return false;

	// The starter kit guarantees wheat and wood exist somewhere near each boot tile, but the
	// ambient scatter and bank clumps painted afterward don't know that - a wide enough patch of
	// one crop can still wall the other off from a swarm that was placed before either existed.
	// guaranteeStartingResources re-checks reachability through the actual resource layout (not
	// just distance) and, if a wall is responsible for a cramped pocket, clears exactly the tiles
	// sealing it before topping up whichever resource is still out of range - the same backstop
	// RuggedArchipelago and ShatteredCoast already rely on for the same class of problem.
	guaranteeStartingResources(game, context, 24, 32);
	// The scatter and bank clumps above are sized by the resource amounts, and the ambient scatter
	// covers up to two thirds of the continent's grass at the top of their range.
	reopenCrampedStarts(game, context, {options.wheat, options.wood, options.stone, options.algae,
										options.fruit});
	return true;
}

} // namespace

FjordContinentOptions::FjordContinentOptions(const GenerationRequest &r)
	: continentSize(r.option("continent-size")), roughness(r.option("coast-roughness")),
	  fjordWidth(r.option("fjord-width")), resourceIslands(r.option("resource-islands")),
	  lakeSize(r.option("lake-size")), lakeConnected(r.option("lake-connected")),
	  sandyLakeShore(r.option("sandy-lake-shore") != 0),
	  bankDeposits(r.option("bank-deposits") != 0), wheat(r.option("wheat-amount")),
	  wood(r.option("wood-amount")), stone(r.option("stone-amount")),
	  algae(r.option("algae-amount")), fruit(r.option("fruit-amount"))
{
}

GeneratorDefinition fjordContinentDefinition()
{
	return {"fjord-continent",
			12,
			"Fjord continent",
			11,
			false,
			{{"continent-size", "Continent size", 28, 40, 2, 34, ControlGroup::Terrain},
			 {"coast-roughness", "Coast roughness", 10, 35, 1, 22, ControlGroup::Terrain},
			 {"fjord-width", "Fjord width", 2, 10, 1, 4, ControlGroup::Terrain},
			 {"lake-size", "Lake size", 0, 90, 5, 45, ControlGroup::Terrain},
			 GeneratorControl::toggle("lake-connected", "Lake connects to fjords", false,
									  ControlGroup::Terrain),
			 {"resource-islands", "Resource islands", 0, 20, 1, 2, ControlGroup::Resources},
			 // Off, the lake has an ordinary beach instead of a wide ring of sand.
			 GeneratorControl::toggle("sandy-lake-shore", "Sandy lake shore", true,
									  ControlGroup::Terrain),
			 // Off, each fjord bank keeps only its guaranteed wheat and wood.
			 GeneratorControl::toggle("bank-deposits", "Fjord bank deposits", true,
									  ControlGroup::Resources),
			 // The ambient scatter, the core's stone and fruit, the lake's and open sea's algae and
			 // the fjord banks' extra deposits. Starter kits and each bank's guaranteed wheat and
			 // wood stay as they are.
			 GeneratorControl::percentage("wheat-amount", "Wheat amount"),
			 GeneratorControl::percentage("wood-amount", "Wood amount"),
			 GeneratorControl::percentage("stone-amount", "Stone amount"),
			 GeneratorControl::percentage("algae-amount", "Algae amount"),
			 GeneratorControl::percentage("fruit-amount", "Fruit amount")},
			generate};
}
