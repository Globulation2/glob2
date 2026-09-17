// SPDX-License-Identifier: GPL-3.0-or-later
#include "AmphitheatreGenerator.h"
#include "Drawing.h"
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
#include "Topology.h"
#include "Walls.h"
#include "Wedge.h"
#include <algorithm>
#include <climits>
#include <cmath>
#include <cstdlib>
#include <string>
#include <vector>
using namespace MapGeneration;

// Amphitheatre: a sunken arena in the middle of the map, ringed by terraces, and all round it one
// walled territory for every colony. Concentric walls of stone step down from the territories to
// the pit, and each wall is broken only by ramps. The ramps through the outermost wall face the
// colonies, one each; the ramps through the next wall stand between neighbours, so two colonies
// share each; the next face the colonies again, and so on down. Every step towards the pit is a
// meeting at a known place: first alone, then with one neighbour, then with the other. The pit holds
// the orchard, the innermost terrace more fruit, and the territories everything a colony needs to
// live on.
//
// A territory is walled off from its neighbours along its whole border and has no other way out
// than its ramp, so a colony that wants anything beyond its own ground has to come down into the
// amphitheatre. There is no sea: each territory's water is two inland seas of its own, one on either
// side of its home (or, where a territory has no room beside its home, one at its far end), enclosed by
// its own land, so its algae and its regrowing fields are its own and no swimmer can use them to cross
// into another territory. Every colony's towers stand against the arena's outer wall beside its own
// ramp, covering the terrace inside and the ramp itself over the stone, so the way in is held from the
// first minute (FEEDBACK 2026-09-14: towers spread along the border walls "make no sense").
//
// The arena is designed round the centre and turned for every colony, like a wedge design. The
// territories cannot be: a square map's corners and the ground across its wrap belong to no wedge,
// and with an odd number of colonies the corners fall unevenly. So the territories are shared out by
// distance instead (balancedTerritories): every tile outside belongs to the colony whose ramp mouth is
// nearest by squared distance less a weight per colony, and the weights are tuned until the areas are
// equal, so every border is a straight line, perpendicular to the line between two ramps and shifted
// towards the colony with the more ground to spare. FEEDBACK 2026-09-13 (third play): "we need to
// find a more reliable mechanism to divide the territory between players for amphitheatre. because
// whatever our current mechanism is, it is causing wildly unsmooth borders. lets come up with something
// fair but also simple and reliable." Until then the territories were grown a tile at a time by the
// smallest first (growTerritories) with noise in the cost for a "border roughness" control, then
// smoothed by a majority filter: the turn-taking grows fingers along every border, the noise more, and
// four passes of smoothing left them ragged. Every colony's seas are then grown to the same number of
// tiles, and every swarm stands the same number of steps from its ramp, so what the division cannot
// make identical is measured and held equal instead. The design is a pure function of the request, so
// validateWorld rebuilds it and checks the finished world.
//
// GAME RULES BEHIND IT (docs/map-generators/GAME_RULES_FOR_MAP_DESIGN.md): stone can never be
// cleared, so the walls and the ramps are permanent; a unit may step diagonally, which is why every
// border is walled where any neighbour has another label (labelBorders); water blocks walking until a
// colony swims, which is why a sea must lie wholly inside its territory; a defence tower shoots over
// stone with no line of sight, which is why the towers stand against the arena wall; wheat and wood regrow
// only near water, which is why every territory's fields grow round its seas and the terraces are dry; and
// fruit lets an inn pull hungry enemy units across, which makes the pit's orchard the prize at the
// bottom of every ramp.
//
// THE SIZES AT THE DEFAULTS (256x256, 4 colonies): a pit 15 tiles in radius, three walls 13 tiles
// apart with ramps 7 wide, so the outer wall stands 41 tiles out; four territories of about 11,000
// tiles each, each with 1,500 tiles of inland sea; three level-1 towers per colony by the ramp.
namespace
{

// Half the thickness of a ring wall, in tiles: stone wherever a tile is this close to the ring.
constexpr double kRingHalf = 1.2;
// A terrace is never narrower than this, whatever the control.
constexpr double kMinimumTerrace = 8;
// Ground kept outside the outer wall before the map's edge, per colony at least this many tiles.
constexpr int kMinimumTerritory = 2000;
// The territories' balance (balancedTerritories): rounds of weight tuning, and how near an equal share
// every area must come, a fifth of what the validator allows (kAreaTolerance).
constexpr int kBalanceRounds = 80;
constexpr double kBalanceTolerance = 0.01;
// The ground every colony keeps round its ramp mouth whatever the balance says: on a 2:1 map the
// colonies on the short sides start with half the ground and their borders shift far, and without this
// a border crossed a neighbour's mouth (colony 0 could not walk to the pit on a third of the seeds).
// As deep as a swarm stands from its mouth (kHomeRampDepth), so the way in is always the colony's own.
constexpr double kSiteKeep = 14;
// An inland sea keeps this many steps from any wall, so its beach never reaches stone and its
// neighbours' seas are always parted by land.
constexpr int kBayWallGap = 7;
// A swarm needs this much room round its site, and its site may miss the common depth by this much.
constexpr int kSwarmRoom = 5;
constexpr int kSiteSpread = 3;
// A swarm stands this share of the way from its ramp to the far end of the shallowest territory. An
// inland sea beside it keeps this far from its site, and seeds no farther than this.
constexpr double kHomeDepthShare = 0.4;
constexpr int kLakeSiteGap = 10;
constexpr int kLakeReach = 60;
// A swarm stands at least this many steps from its ramp's mouth, and this many short of the nearest
// bay, measured from the ramp.
constexpr int kHomeRampDepth = 14;
// Every home's starter kit: this much wheat and wood facing its nearest sea, unscaled; no stone, since
// the territory is walled in it.
constexpr int kHomeWheat = 30;
constexpr int kHomeWood = 30;
// Ambient farmland on a territory, as percentages of its tiles at 100.
constexpr int kHomeWheatShare = 3;
constexpr int kHomeWoodShare = 2;
// Nothing is planted within this many steps of a ramp.
constexpr int kRampClearance = 5;
// Open 2x2 pads beside every colony's `tower-count` towers, and the spacing between sites.
constexpr int kTowerPads = 3;
constexpr int kTowerSpacing = 5;
// Towers stand within this many walking steps of the colony's own ramp mouth, so they cluster at the
// door rather than along the borders.
constexpr int kEntranceReach = 18;
// A tower keeps this many steps from water on every side, so it never closes a strip of shore.
constexpr int kTowerRoom = 4;
// The most the territories' areas may differ, in percent of the smallest: the balance stops within
// kBalanceTolerance, and the walls and the seas take a little more.
constexpr int kAreaTolerance = 5;
// The most the colonies' walks to their ramps and to the pit may differ.
constexpr int kWalkSpread = 12;

struct Geometry
{
	int teams = 0, half = 0, rings = 0, width = 0, height = 0;
	double wedge = 0, rampHalf = 0, pitR = 0, terrace = 0, outer = 0;
	std::vector<double> ringR;
	std::string failure;
};

Geometry geometryFor(const GenerationRequest &r)
{
	const AmphitheatreOptions o(r);
	Geometry g;
	g.teams = std::max(1, r.nbTeams);
	g.width = 1 << r.wDec;
	g.height = 1 << r.hDec;
	g.half = std::min(g.width, g.height) / 2;
	g.wedge = 2 * kPi / g.teams;
	g.rings = std::clamp(o.rings, 1, 6);
	g.rampHalf = o.rampWidth / 2.0;
	g.pitR = g.half * o.pitSize / 100.0;
	g.terrace = std::max(kMinimumTerrace, g.half * o.terraceWidth / 100.0);
	for (int j = 0; j < g.rings; ++j)
		g.ringR.push_back(g.pitR + j * g.terrace);
	g.outer = g.ringR.back();
	const double inside = kPi * (g.outer + kRingHalf + 1) * (g.outer + kRingHalf + 1);
	if (g.outer + kRingHalf + 8 > g.half)
		g.failure =
			"The amphitheatre does not fit on this map: use fewer rings or narrower terraces.";
	else if ((double(g.width) * g.height - inside) / g.teams < kMinimumTerritory)
		g.failure = "Too many colonies for this map: their territories would be too small.";
	else if (g.ringR[0] * g.wedge < 2 * g.rampHalf + 6)
		g.failure = "Too many colonies for the pit: its ramps would run into each other.";
	return g;
}

struct Layout
{
	Torus t{1, 1};
	Geometry g;
	double phase = 0, cx = 0, cy = 0;
	// zone: -1 wall, 0 the pit, j the terrace outside ring j - 1, rings the ground outside.
	std::vector<int> zone, territory, depth;
	// rampOf: the colony whose outer ramp a tile is (or -1); innerRamp marks every other ramp tile.
	std::vector<int> rampOf;
	// ring: the ring walls' stone; border: the stone between territories; unclaimed: ground no territory
	// took, also stone; bay: every territory's inland seas (the water the `bay-size` control sizes);
	// pocket: land a sea closed off from its ramp, also stone.
	std::vector<unsigned char> ring, innerRamp, border, unclaimed, bay, pocket;
	std::vector<std::vector<int>> mouths; // each colony's tiles just outside its outer ramp
	std::vector<int> anchor, rampMiddle;  // each colony's swarm centre and middle of its outer ramp
	std::vector<int> bayTiles, areas;
	int pitMiddle = 0;
	std::string failure;
};

Layout design(const GenerationRequest &request, GenerationContext &context)
{
	const AmphitheatreOptions o(request);
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
	L.phase = context.bounded("amphitheatre-phase", 3600) / 3600.0 * 2 * kPi;
	L.zone.assign(n, g.rings);
	L.rampOf.assign(n, -1);
	L.ring.assign(n, 0);
	L.innerRamp.assign(n, 0);
	L.mouths.assign(teams, {});

	// The arena: rings of stone with their ramps (ringWithGates). The outermost ring's ramps face the
	// colonies, the next ring's stand between them, and so on inwards.
	for (int i = 0; i < n; ++i)
	{
		const double r = std::hypot(t.offsetX(int(L.cx), i % t.w), t.offsetY(int(L.cy), i / t.w));
		for (int j = 0; j < g.rings && L.zone[i] == g.rings; ++j)
			if (r < g.ringR[j] - kRingHalf)
				L.zone[i] = j;
	}
	for (int j = 0; j < g.rings; ++j)
	{
		const bool outermost = j == g.rings - 1;
		const double offset = (g.rings - 1 - j) % 2 == 0 ? 0 : g.wedge / 2;
		std::vector<double> ramps;
		for (int k = 0; k < teams; ++k)
			ramps.push_back(L.phase + g.wedge * k + offset);
		ringWithGates(t, L.cx, L.cy, g.ringR[j], kRingHalf, ramps, g.rampHalf,
					  [&](int i, int ramp)
					  {
						  L.zone[i] = -1;
						  if (ramp < 0)
							  L.ring[i] = 1;
						  else if (outermost)
							  L.rampOf[i] = ramp;
						  else
							  L.innerRamp[i] = 1;
					  });
	}
	// The outer ramps' mouths: the outside tiles touching them.
	for (int i = 0; i < n; ++i)
	{
		if (L.zone[i] != g.rings)
			continue;
		for (int dy = -1; dy <= 1; ++dy)
			for (int dx = -1; dx <= 1; ++dx)
			{
				const int k = L.rampOf[t.at(i % t.w + dx, i / t.w + dy)];
				if (k >= 0 && (L.mouths[k].empty() || L.mouths[k].back() != i))
					L.mouths[k].push_back(i);
			}
	}

	// The territories (balancedTerritories): the ground outside the outer wall is shared out by weighted
	// squared distance from every colony's ramp mouth, the weights tuned until the areas are equal, so
	// every border is a straight line of stone (third play; see the header).
	std::vector<unsigned char> outside(n, 0);
	for (int i = 0; i < n; ++i)
		outside[i] = L.zone[i] == g.rings;
	// Every colony's site is the middle of its outer ramp's mouth, just outside the wall, so the border
	// between two neighbours starts on the wall half way between their ramps and runs straight out.
	std::vector<int> sites;
	for (int k = 0; k < teams; ++k)
	{
		const ShapePoint p = polarPoint(L.cx, L.cy, g.outer + kRingHalf + 2, L.phase + g.wedge * k);
		sites.push_back(t.at(int(std::lround(p.x)), int(std::lround(p.y))));
	}
	L.territory =
		balancedTerritories(t, outside, sites, kSiteKeep, kBalanceRounds, kBalanceTolerance).labels;
	L.areas.assign(teams, 0);
	for (int i = 0; i < n; ++i)
		if (L.territory[i] >= 0)
			++L.areas[L.territory[i]];

	// The borders: a wall where any neighbour belongs to another territory, as thick as the control
	// says, and every tile no territory claimed. A ramp's mouth is never walled.
	L.border = labelBorders(t, L.territory, o.borderWall);
	L.unclaimed.assign(n, 0);
	for (int i = 0; i < n; ++i)
		L.unclaimed[i] = outside[i] && L.territory[i] < 0;
	for (int k = 0; k < teams; ++k)
		for (int i : L.mouths[k])
			L.border[i] = 0;

	// The homes (siteAtDepth): every swarm the same number of steps from its ramp's mouth, a share of
	// the way to the shallowest territory's far end, as far as possible from any wall.
	std::vector<unsigned char> walls(n, 0);
	for (int i = 0; i < n; ++i)
		walls[i] = L.ring[i] || L.border[i] || L.unclaimed[i];
	const std::vector<int> fromWalls = stepsFrom(t, walls);
	L.depth.assign(n, -1);
	int shallowest = INT_MAX;
	for (int k = 0; k < teams; ++k)
	{
		std::vector<unsigned char> ground(n, 0);
		for (int i = 0; i < n; ++i)
			ground[i] = L.territory[i] == k && !L.border[i];
		const std::vector<int> depth = stepsFrom(t, tileMask(t, L.mouths[k]), ground);
		int deepest = 0;
		for (int i = 0; i < n; ++i)
			if (ground[i])
			{
				L.depth[i] = depth[i];
				deepest = std::max(deepest, depth[i]);
			}
		shallowest = std::min(shallowest, deepest);
	}
	const int home = std::max(kHomeRampDepth, int(kHomeDepthShare * shallowest));
	for (int k = 0; k < teams; ++k)
	{
		std::vector<int> depth(n, -1);
		for (int i = 0; i < n; ++i)
			if (L.territory[i] == k && !walls[i])
				depth[i] = L.depth[i];
		const int site = siteAtDepth(depth, fromWalls, home, kSwarmRoom, kSiteSpread);
		if (site < 0)
		{
			L.failure = "A territory has no room for its swarm.";
			return L;
		}
		L.anchor.push_back(site);
		// The middle of the colony's outer ramp.
		const ShapePoint p = polarPoint(L.cx, L.cy, g.outer, L.phase + g.wedge * k);
		L.rampMiddle.push_back(t.at(int(std::lround(p.x)), int(std::lround(p.y))));
	}

	// The inland seas: where every territory has the room, one either side of its home (growLakeBeside),
	// each half the bay size; otherwise one at the far end of every territory (growFarLake). Every
	// colony's seas are the same size, clear of every wall and of the home, and any land a sea closes off
	// becomes stone (strandedGround).
	const int total =
		int(std::lround(*std::min_element(L.areas.begin(), L.areas.end()) * o.baySize / 100.0));
	const PeriodicNoise shoreline(t.w, t.h, 8, context.stream("amphitheatre-bays"));
	const auto noise = [&](int i) { return shoreline.at(i % t.w, i / t.w); };
	std::vector<int> queued(n, 0);
	for (const bool beside : {true, false})
	{
		L.bay.assign(n, 0);
		L.bayTiles.assign(teams, 0);
		bool fits = true;
		for (int k = 0; k < teams && fits; ++k)
		{
			std::vector<int> depth(n, -1);
			for (int i = 0; i < n; ++i)
				if (L.territory[i] == k && !L.border[i])
					depth[i] = L.depth[i];
			if (beside)
			{
				const int site = L.anchor[k];
				const int mouth = L.rampMiddle[k];
				const double heading = std::atan2(t.offsetY(mouth / t.w, site / t.w),
												  t.offsetX(mouth % t.w, site % t.w));
				for (const int side : {1, -1})
				{
					const int grown = growLakeBeside(
						t, L.bay, depth, fromWalls, kBayWallGap, total / 2, site, heading, side,
						kLakeSiteGap, kLakeReach, noise, queued, 2 * k + (side > 0) + 1);
					fits = fits && grown == total / 2;
					L.bayTiles[k] += grown;
				}
			}
			else
			{
				L.bayTiles[k] = growFarLake(t, L.bay, depth, fromWalls, kBayWallGap, total, noise,
											queued, 100 + k);
				fits = L.bayTiles[k] == total;
			}
		}
		if (fits)
		{
			context.telemetry.choice("amphitheatre.bays.layout", beside ? "paired" : "far");
			break;
		}
		context.telemetry.fallback(
			"amphitheatre.bays.fit",
			beside ? "Paired bays did not fit; trying far bays for every colony."
				   : "Far bays did not fit.");
		if (!beside)
		{
			L.failure = "A territory's bay does not fit; use a smaller bay or fewer colonies.";
			return L;
		}
	}
	L.pocket.assign(n, 0);
	for (int k = 0; k < teams; ++k)
	{
		std::vector<unsigned char> ground(n, 0);
		for (int i = 0; i < n; ++i)
			ground[i] = L.territory[i] == k && !L.border[i] && !L.bay[i];
		const std::vector<unsigned char> stranded = strandedGround(t, L.mouths[k], ground);
		for (int i = 0; i < n; ++i)
			if (stranded[i])
				L.pocket[i] = 1;
	}
	context.telemetry.measure("amphitheatre.phase.radians", L.phase);
	context.telemetry.measure("amphitheatre.arena.outer-radius", g.outer);
	context.telemetry.measure("amphitheatre.home.depth-target", home);
	for (int k = 0; k < teams; ++k)
	{
		context.telemetry.measure("amphitheatre.territory.area", L.areas[k], k);
		context.telemetry.measure("amphitheatre.bay.corners", L.bayTiles[k], k);
	}
	L.pitMiddle = t.at(int(L.cx), int(L.cy));
	return L;
}

// The design's stone: every ring outside its ramps, every border and every unclaimed tile, and any
// land a bay closed off. Rings, borders and unclaimed ground all keep clear of the bays, so all of
// it lies on grass; a closed-off pocket may touch a beach, where its sand simply stays sand.
std::vector<unsigned char> stoneTiles(const Map &map, const Layout &L)
{
	const int n = L.t.size();
	std::vector<unsigned char> wall(n, 0), stone(n, 0);
	for (int i = 0; i < n; ++i)
		wall[i] = L.ring[i] || L.border[i] || L.unclaimed[i] || L.pocket[i];
	const DesignedStone designed = designedStone(map, L.t, wall);
	return designed.stone;
}

// The towers every colony starts with and the open pads beside them (chooseTowerSites): all in the
// colony's own territory, directly against the arena's outer wall and within kEntranceReach steps of
// the colony's ramp mouth, chosen for how much of the arena - the terraces, the pit and the ramps,
// where an attacker comes from - each covers over the stone. Nothing scores for covering the colony's
// own ground or its neighbours': the starting towers hold the door, and a colony that wants towers on
// its borders builds them (FEEDBACK 2026-09-14: towers scattered along the border walls "make no
// sense"; they should be "concentrated around the inner stone wall ... nearby the entrance").
TowerPlan planTowers(const Map &map, const Layout &L, const GenerationContext &context,
					 const AmphitheatreOptions &o, const std::vector<unsigned char> &stone)
{
	const Torus &t = L.t;
	const int n = t.size(), teams = L.g.teams;
	std::vector<unsigned char> ramps(n, 0), ringStone(n, 0);
	for (int i = 0; i < n; ++i)
	{
		ramps[i] = L.rampOf[i] >= 0 || L.innerRamp[i];
		ringStone[i] = L.ring[i] && stone[i];
	}
	const std::vector<int> fromRamps = stepsFrom(t, ramps);
	const std::vector<unsigned char> swarms = swarmSurroundings(t, context, 0);
	const std::vector<unsigned char> reserved = swarmSurroundings(t, context);
	// How far every tile of a territory is from its own ramp mouth, walking the territory.
	std::vector<int> fromMouth(n, -1);
	for (int k = 0; k < teams; ++k)
	{
		std::vector<unsigned char> ground(n, 0);
		for (int i = 0; i < n; ++i)
			ground[i] = L.territory[i] == k;
		const std::vector<int> steps = stepsFrom(t, tileMask(t, L.mouths[k]), ground);
		for (int i = 0; i < n; ++i)
			if (ground[i])
				fromMouth[i] = steps[i];
	}
	std::vector<int> owner(n, -1);
	std::vector<unsigned char> buildable(n, 0), target(n, 0);
	for (int i = 0; i < n; ++i)
	{
		const int x = i % t.w, y = i / t.w;
		// The arena belongs to "everyone else", so every colony's towers score for covering it.
		const bool arena = ramps[i] || (L.zone[i] >= 0 && L.zone[i] < L.g.rings);
		owner[i] = arena ? teams : L.territory[i];
		target[i] = arena && !map.isWater(x, y) && !stone[i];
		buildable[i] = L.territory[i] >= 0 && map.isGrass(x, y) && !stone[i] && !reserved[i] &&
					   fromRamps[i] > kRampClearance && fromMouth[i] >= 0 &&
					   fromMouth[i] <= kEntranceReach && !map.isResource(x, y);
	}
	// No tower on a shore strip narrow enough for it to close.
	std::vector<unsigned char> land(n, 0);
	for (int i = 0; i < n; ++i)
		land[i] = !map.isWater(i % t.w, i / t.w);
	const std::vector<unsigned char> roomy = roomyGround(t, land, kTowerRoom);
	for (int i = 0; i < n; ++i)
		buildable[i] = buildable[i] && roomy[i];
	TowerRequest request = startingTowerRequest(o.towers, o.towerCount, kTowerPads, kTowerSpacing);
	request.otherWeight = 1;
	request.ownWeight = 0;
	request.against = &ringStone;
	return chooseTowerSites(t, owner, buildable, target, swarms, teams, request);
}

bool generate(Game &game, GenerationContext &context)
{
	context.stage = "amphitheatre layout";
	const AmphitheatreOptions o(context.request);
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
	const int n = t.size();
	const Geometry &g = L.g;

	context.stage = "amphitheatre terrain";
	TerrainSketch terrain(n, GRASS);
	for (int i = 0; i < n; ++i)
		if (L.bay[i])
			terrain[i] = WATER;
	layBeaches(terrain, t);
	writeUndermap(map, terrain);
	const std::vector<unsigned char> stone = stoneTiles(map, L);
	for (int i = 0; i < n; ++i)
		if (stone[i])
			map.setResource(i % t.w, i / t.w, STONE, 1);

	context.stage = "amphitheatre colonies";
	const auto home = [&](int team)
	{
		std::vector<unsigned char> ground(n, 0);
		for (int i = 0; i < n; ++i)
			ground[i] = L.territory[i] == team && !stone[i] && map.isGrass(i % t.w, i / t.w);
		return ground;
	};
	const auto anchor = [&](int team)
	{ return MapGeneratorPoint(L.anchor[team] % t.w - 2, L.anchor[team] / t.w - 2); };
	if (!settleColonies(game, context, "amphitheatre-starts", home, anchor))
		return false;

	context.stage = "amphitheatre towers";
	TowerPlan towers = planTowers(map, L, context, o, stone);
	// A crowded map may not seat every tower: every colony then keeps as many as the fewest got, as
	// long as every colony has one.
	if (!settleStartingTowers(game, context, towers, o.towers, o.towers > 0 && o.towerCount > 0,
							  nullptr))
		return false;
	const std::vector<unsigned char> pads = towerFootprints(t, towers);

	context.stage = "amphitheatre resources";
	const std::vector<unsigned char> reserved = swarmSurroundings(t, context);
	std::vector<unsigned char> ramps(n, 0);
	for (int i = 0; i < n; ++i)
		ramps[i] = L.rampOf[i] >= 0 || L.innerRamp[i];
	const std::vector<int> fromRamps = stepsFrom(t, ramps);
	const auto free = [&](int i)
	{
		return !reserved[i] && !pads[i] && fromRamps[i] > kRampClearance &&
			   clearGround(map, i % t.w, i / t.w);
	};
	const Fertility::Field fertility = Fertility::forMap(map, false);
	const PeriodicNoise patch(t.w, t.h, 12, context.stream("amphitheatre-patch"));
	const PeriodicNoise split(t.w, t.h, 6, context.stream("amphitheatre-split"));
	for (int k = 0; k < teams; ++k)
	{
		const auto eligible = [&](int i) { return L.territory[i] == k && free(i); };
		// The kit faces the nearest sea: wheat and wood between the swarm and its shore.
		const int ax = L.anchor[k] % t.w, ay = L.anchor[k] / t.w;
		int bayTile = -1;
		for (int i = 0; i < n; ++i)
			if (L.bay[i] && L.territory[i] == k &&
				(bayTile < 0 ||
				 t.dist2(ax, ay, i % t.w, i / t.w) < t.dist2(ax, ay, bayTile % t.w, bayTile / t.w)))
				bayTile = i;
		const double facing =
			bayTile < 0 ? 0
						: std::atan2(t.offsetY(ay, bayTile / t.w), t.offsetX(ax, bayTile % t.w));
		const KitFrame frame{ax, ay, facing};
		plantKit(map, t, context,
				 Kit{frame.at(9, -6, 12), frame.at(9, 6, 12), frame.at(-8, 0, 10), kHomeWheat,
					 kHomeWood, -1},
				 eligible);
		furnishGround(
			map, t, context, fertility, eligible, [&](int i) { return patch.at(i % t.w, i / t.w); },
			[&](int i) { return split.at(i % t.w, i / t.w); },
			[&](int area)
			{
				return GroundAmounts{int(scaledCount(area * kHomeWheatShare / 100, o.wheat)),
									 int(scaledCount(area * kHomeWoodShare / 100, o.wood)), 0,
									 int(scaledCount(1, o.fruit))};
			},
			"amphitheatre-home-stone", "amphitheatre-home-fruit");
	}
	// The arena's prizes, the same in every wedge (plantOrchard, plantRound): groves of the three fruits
	// on the innermost terrace and in the pit, a quarter-wedge round from the ramps, and a stone outcrop
	// on every terrace.
	const auto arena = [&](int zone)
	{ return [&, zone](int i) { return L.zone[i] == zone && free(i); }; };
	std::vector<double> quarter, back;
	for (int k = 0; k < teams; ++k)
	{
		quarter.push_back(L.phase + g.wedge * k + g.wedge / 4);
		back.push_back(L.phase + g.wedge * k - g.wedge / 4);
	}
	if (scaledCount(1, o.fruit) > 0)
	{
		plantOrchard(map, t, context, L.cx, L.cy, 0.55 * g.pitR, quarter, 5, 4, 1, arena(0));
		if (g.rings > 1)
			plantOrchard(map, t, context, L.cx, L.cy, (g.ringR[0] + g.ringR[1]) / 2, quarter, 5, 4,
						 1, arena(1));
	}
	for (int j = 1; j < g.rings && scaledCount(1, o.stone) > 0; ++j)
		plantRound(map, t, context, L.cx, L.cy, (g.ringR[j - 1] + g.ringR[j]) / 2, back, STONE, 1,
				   4, arena(j));
	std::vector<int> bayOf(n, -1);
	for (int i = 0; i < n; ++i)
		if (L.bay[i])
			bayOf[i] = L.territory[i];
	seedAlgae(map, context, t, "amphitheatre-algae", o.algae, AlgaeBand::shallows(1, 4), bayOf,
			  teams);
	secureStartingCrops(game, context, t, 24, 32, 0, &stone);
	reopenCrampedStarts(game, context, {o.wheat, o.wood, o.stone, o.algae, o.fruit}, 24, 32, 0,
						&stone);

	// Keep every colony's walk open, clearing only deposits on it: home to its ramp, ramp to the pit.
	context.stage = "amphitheatre roads";
	const std::vector<std::vector<int>> workers = unitTilesByTeam(map, teams);
	std::vector<unsigned char> pit(n, 0);
	for (int i = 0; i < n; ++i)
		pit[i] =
			L.zone[i] == 0 && !stone[i] &&
			std::hypot(t.offsetX(int(L.cx), i % t.w), t.offsetY(int(L.cy), i / t.w)) < 0.4 * g.pitR;
	for (int team = 0; team < teams; ++team)
	{
		if (workers[team].empty())
			continue;
		std::vector<unsigned char> ramp(n, 0);
		std::vector<int> rampTiles;
		for (int i = 0; i < n; ++i)
			if (L.rampOf[i] == team)
			{
				ramp[i] = 1;
				rampTiles.push_back(i);
			}
		if (!openRoad(map, t, workers[team], ramp, &stone) ||
			!openRoad(map, t, rampTiles, pit, &stone))
		{
			context.detail =
				"colony " + std::to_string(team) + " has no walk to its ramp and the pit";
			return false;
		}
	}
	return true;
}

std::string validateRequest(const GenerationRequest &r)
{
	if (r.nbTeams < 2)
		return "An amphitheatre needs at least two colonies.";
	// The geometry's own reason stays in the candidate's diagnostics; the player sees one message
	// that says what to change.
	return geometryFor(r).failure.empty()
			   ? ""
			   : "The amphitheatre does not fit this map; use a bigger map, fewer rings, narrower "
				 "terraces or fewer colonies.";
}

// Checked on the finished world against the rebuilt design: every designed stone stands and every
// ramp is open; every colony walks to colony 0; the territories are equal to within a few percent and
// every colony's seas have the same number of tiles; with the outer ramps shut, no territory reaches another or
// the arena; and the colonies' walks to their ramps and to the pit are even.
std::string validateWorld(const Game &game, const GenerationContext &context)
{
	GenerationContext replay(context.request);
	const Layout L = design(context.request, replay);
	const Map &map = game.map;
	if (const std::string mismatch = designMismatch(L, map, "amphitheatre"); !mismatch.empty())
		return mismatch;
	const Torus &t = L.t;
	const Geometry &g = L.g;
	const int n = t.size(), teams = context.request.nbTeams;
	const auto where = [&](int i)
	{ return "(" + std::to_string(i % t.w) + ", " + std::to_string(i / t.w) + ")"; };
	const std::vector<unsigned char> stone = stoneTiles(map, L);
	const std::vector<unsigned char> open = walkableTiles(map);
	std::vector<int> area(teams, 0), water(teams, 0);
	for (int i = 0; i < n; ++i)
	{
		if (stone[i] && map.getResource(i % t.w, i / t.w).type != STONE)
			return "The stone at " + where(i) + " is missing.";
		if ((L.rampOf[i] >= 0 || L.innerRamp[i]) && !open[i])
			return "The ramp at " + where(i) + " is blocked.";
		if (L.territory[i] >= 0)
		{
			++area[L.territory[i]];
			water[L.territory[i]] += map.isWater(i % t.w, i / t.w);
		}
	}
	const int smallest = *std::min_element(area.begin(), area.end());
	const int largest = *std::max_element(area.begin(), area.end());
	if (largest - smallest > std::max(4, smallest * kAreaTolerance / 100))
		return "The territories differ in size by " + std::to_string(largest - smallest) +
			   " tiles.";
	// Each bay was grown to the same number of undermap corners; a tile is water only when all four of
	// its corners are, so the tiles that read as water can differ a little with the outline, but every
	// bay must be there.
	for (int k = 0; k < teams; ++k)
		if (L.bayTiles[k] != L.bayTiles[0] || water[k] == 0)
			return "The bays differ in size.";

	const ColonyWalk walk = walkFromFirstColony(map, teams, "the amphitheatre", "");
	if (!walk.error.empty())
		return walk.error;
	std::vector<int> piece(n, -1);
	std::vector<unsigned char> outerRamps(n, 0);
	for (int i = 0; i < n; ++i)
	{
		piece[i] = L.territory[i] >= 0                     ? L.territory[i]
				   : L.zone[i] >= 0 && L.zone[i] < g.rings ? teams
														   : -1;
		outerRamps[i] = L.rampOf[i] >= 0;
	}
	if (const int leak = pieceLeak(map, t, piece, outerRamps); leak >= 0)
		return "With the outer ramps shut, " + where(leak) +
			   " can still be reached from elsewhere.";

	const auto nearestOpen = [&](int tile)
	{
		const int s = seedNear(t, tile % t.w, tile / t.w, 4, [&](int i) { return open[i] != 0; });
		return s >= 0 ? s : tile;
	};
	// Every colony's target in the pit is the open tile of the pit's middle disc (the disc the roads
	// stage clears a way to, 0.4 of the pit's radius) that its ramp reaches in the fewest steps. The
	// very middle will not do: the orchard's fruit can ring it until eaten, and targeting it failed
	// every colony's walk on some seeds (128x128 with four colonies, seed 1) whatever the territories.
	std::vector<unsigned char> pitDisc(n, 0);
	for (int i = 0; i < n; ++i)
		pitDisc[i] =
			L.zone[i] == 0 && open[i] &&
			std::hypot(t.offsetX(int(L.cx), i % t.w), t.offsetY(int(L.cy), i / t.w)) < 0.4 * g.pitR;
	std::vector<int> ramps, pits;
	for (int k = 0; k < teams; ++k)
	{
		ramps.push_back(nearestOpen(L.rampMiddle[k]));
		const std::vector<int> fromRamp = stepsFrom(t, tileMask(t, {ramps.back()}), open);
		int best = -1;
		for (int i = 0; i < n; ++i)
			if (pitDisc[i] && fromRamp[i] >= 0 && (best < 0 || fromRamp[i] < fromRamp[best]))
				best = i;
		pits.push_back(best >= 0 ? best : nearestOpen(L.pitMiddle));
	}
	for (const auto &[targets, what] : {std::pair{ramps, "ramps"}, std::pair{pits, "the pit"}})
	{
		const WalkSpread spread = walkSpread(map, t, walk.workers, targets);
		if (spread.unreached >= 0)
			return "Colony " + std::to_string(spread.unreached) + " cannot walk to " + what + ".";
		if (spread.tooUneven(kWalkSpread))
			return std::string("The colonies' walks to ") + what + " differ by " +
				   std::to_string(spread.longest - spread.shortest) + " steps.";
	}
	return "";
}
} // namespace

AmphitheatreOptions::AmphitheatreOptions(const GenerationRequest &r)
	: rings(r.option("rings")), rampWidth(r.option("ramp-width")), pitSize(r.option("pit-size")),
	  terraceWidth(r.option("terrace-width")), baySize(r.option("bay-size")),
	  borderWall(r.option("border-wall")), towers(r.option("starting-towers")),
	  towerCount(r.option("tower-count")), wheat(r.option("wheat-amount")),
	  wood(r.option("wood-amount")), stone(r.option("stone-amount")),
	  algae(r.option("algae-amount")), fruit(r.option("fruit-amount"))
{
}

GeneratorDefinition amphitheatreDefinition()
{
	return {
			"amphitheatre",
			23,
			"Amphitheatre",
			3,
			false,
			// Rings of wall; each ramp's width in tiles; the pit's radius and each terrace's width as
		// shares of the half side; every colony's inland seas together as a percentage of the smallest
		// territory; the borders' thickness in tiles.
		{{"rings", "Rings", 2, 4, 1, 3, ControlGroup::Layout},
		 {"ramp-width", "Ramp width", 5, 11, 2, 7, ControlGroup::Terrain},
		 {"pit-size", "Pit size", 8, 30, 2, 16, ControlGroup::Layout},
		 {"terrace-width", "Terrace width", 6, 20, 1, 14, ControlGroup::Layout},
		 {"bay-size", "Bay size", 6, 24, 2, 14, ControlGroup::Terrain},
		 {"border-wall", "Border wall", 1, 3, 1, 2, ControlGroup::Terrain},
		 // The towers every colony starts with, all against the arena wall by its ramp: their level
		 // (0 for none, just open pads; level 1 by default since 2026-09-14, so players upgrade
		 // their own towers) and how many.
		 {"starting-towers", "Starting tower level", 0, 3, 1, 1, ControlGroup::Layout},
		 {"tower-count", "Towers per colony", 0, 12, 1, 3, ControlGroup::Layout},
		 // Every territory's ambient fields and grove, the arena's fruit and outcrops, and the
		 // bays' algae; every home's kit and the walls' stone are unscaled.
		 GeneratorControl::percentage("wheat-amount", "Wheat amount"),
		 GeneratorControl::percentage("wood-amount", "Wood amount"),
		 GeneratorControl::percentage("stone-amount", "Stone amount"),
		 GeneratorControl::percentage("algae-amount", "Algae amount"),
		 GeneratorControl::percentage("fruit-amount", "Fruit amount")},
			generate,
			true,
			validateRequest,
			validateWorld,
			{"terrain:arena", "feature:stone-walls", "feature:orchard", "style:siege",
			 "style:contested-center", "fairness:repeated-wedge"}};
}
