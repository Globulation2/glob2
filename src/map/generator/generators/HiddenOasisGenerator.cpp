// SPDX-License-Identifier: GPL-3.0-or-later
#include "HiddenOasisGenerator.h"
#include "Biomes.h"
#include "Building.h"
#include "Contact.h"
#include "DesignCache.h"
#include "Drawing.h"
#include "Farmland.h"
#include "Game.h"
#include "GenerationContext.h"
#include "Geometry.h"
#include "Grid.h"
#include "Growth.h"
#include "Homes.h"
#include "Landmass.h"
#include "LatticeNoise.h"
#include "Morphology.h"
#include "Pipeline.h"
#include "Planting.h"
#include "Points.h"
#include "Resources.h"
#include "Roads.h"
#include "Settlements.h"
#include "Sketch.h"
#include "WalkBandStarts.h" // firstWalkTerritories
#include "Walls.h"
#include <algorithm>
#include <climits>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>
using namespace MapGeneration;

// Hidden Oasis: dry canyon country of steppe, sand seas, washes, mesas and springs, round one great
// sandstone plateau. Inside the plateau lies a hidden basin with a green pond, the only algae in the
// world, and one winding slot canyon, the gorge, is the only way in.
//
// WHY IT PLAYS (docs/map-generators/GAME_RULES_FOR_MAP_DESIGN.md). Algae is the gate to everything past
// the opening: even a level-0 school costs 2 of it (and its upgrades 12 and 10, a level-2 pool 8, the
// top tower 2), and a worker may only work on a building of its own build level or below
// (Building::canUnitWorkHere), which it raises only at a school. So without algae a colony upgrades
// nothing. Unlike stone, algae is harvested away and regrows only beside sand, so the pond's size is
// a sustained yield. Central Quarry's lock was an isle anyone could walk to; this one is sealed by
// the colonies themselves. Every colony starts with one defence tower covering a pinch of the gorge,
// the towers on alternate sides of it and out of range of one another. A tower shoots over stone, so
// each stands outside the gorge, on a ledge at the head of its own box canyon, behind two tiles of
// rock: its owner walks in the back way to resupply it (a level-1 tower holds 12 shots and the stone
// for 12 more, and its canyon's walls are stone), rebuild it and defend it, while everyone else's
// workers die in the gorge under it. To reach the algae a colony must knock out the other colonies'
// towers, up their canyons; whoever breaks the seal first builds in the basin (a sealed garden of
// wheat and wood feeds a garrison there) and holds the pond. Water cannot be the barrier (a level-0
// pool swims it) and nor can woods (workers clear them): only stone is permanent, which is why the
// oasis hides in rock. The gorge's floor is sand, so nobody walls it or builds in it.
//
// THE TOWERS ARE LEVEL 1, AND ONLY LEVEL 1 (review round 2, 2026-09-17). The first two builds started
// level-2 towers. Under the algae lock no worker on the map has a build level, so nobody could carry
// a stone to one or repair it: in every game every starting tower fired its magazine and its reserve
// into workers and stood empty, and the seal fell open unfought. A level-1 tower is one its owner can
// serve, and one a level-0 warrior hurts (5 a hit against its armour of 8, where a level-2 tower's 12
// leaves 1): clearing the enemy towers is something a colony without a school can do.
//
// WHAT THE AIs DO WITH IT. They do not besiege towers on purpose and never garrison the basin; they
// send workers for the 2 algae a school site asks for, and the towers kill some. The seal is a toll
// against an AI and a siege only between people. With no algae anywhere else, a game without schools
// is played at level 0 until somebody is through, so every colony is granted one finished level-0
// school (`starting-school`, on by default: maintainer, 2026-09-17): builders train and first upgrades
// happen everywhere, while the pond still gates more schools, every school upgrade, the level-2 pool
// and the top tower. Off is the total lock. `starting-towers` off leaves the ledges as open pads.
//
// EVERY DOOR IS BESIDE THE MOUTH. Every box canyon runs forwards inside the plateau, in a lane of its
// own, and opens on the mouth's cliff. So one search makes the map fair (Central Quarry's: a band of
// equal walk to the mouth, spread by walking distance, on watered ground of a similar yield): a colony
// as far as its rivals from the prize is as far as they are from every tower's door. A deep tower's
// canyon is longer, for its attackers as much as for its owner. No home is stamped; a start that
// landed dry is dug a pond off its route, and the desert keeps off every home. The country is dry:
// crops grow only round the springs, washes of sand run out from every canyon towards the way the
// country drains, and lone scrub trees stand where nothing regrows.
//
// THE PROMISES validateWorld KEEPS. No algae off the pond, and the amount asked on it; the basin's
// crops are its garden's and it has room to build; the plateau's walls stand; with any one colony's
// pinch shut nobody walks into the basin (so every pinch is a whole cross-section of the gorge, and no
// box canyon leaks into it); every colony has its tower, its pinch within the tower's range, and
// walks to its ledge without entering the gorge, no farther than the mouth plus the gorge's length
// and slack; no tower within a level-3 tower's range of another colony's, nor within its own range of
// another colony's ledge or of the basin's ground; every colony walks to every other and to the mouth, those walks within twice
// the band plus slack of each other; no two colonies closer than the spacing floor; at least
// kLeastWheatNearby wheat near every swarm.
namespace
{
// Sizes are in tiles whatever the map's size (Central Quarry, review round 1), grown a little on maps
// bigger than 256 and capped on 128. The basin holds the pond and the ground round it (room for a
// forward base) inside a back wall of rock; the gorge runs out from it to the mouth with kFlank of
// rock either side for the ledges and their box canyons.
constexpr double kPondGrowth = 0.4;
constexpr double kSmallMapPond = 5, kSmallMapBasinRing = 6, kBasinRing = 8;
constexpr double kBackWall = 10, kSmallMapBackWall = 7, kFlankWall = 7;
constexpr double kSmallMapBasinKeep = 9;
// The massif is this share of the shorter side long at most.
constexpr double kGreatestMassifShare = 0.68;
// Every colony adds a pinch to the one gorge; beyond eight the deep ledges lie too far round the
// plateau from the mouth for the walks to be fair (review round 2), so the request is refused.
constexpr int kGreatestColonies = 8, kDesignAttempts = 4;
// Its outline swells and shrinks by this much round the basin's middle, and is ragged by a fine noise
// field of up to kMassifRag tiles (first look, 2026-09-17: a smooth grey ellipse, drawn with compasses).
constexpr double kMassifRoughness = 0.3, kMassifRag = 5;
constexpr int kMassifRagPeriod = 10;
constexpr double kBasinRoughness = 0.12, kPondRoughness = 0.3;
// The pond sits this far off the basin's middle, away from the gorge's entrance, so the entrance side
// has the room; its water keeps this far inside the basin's rim.
constexpr double kPondDrift = 2, kPondInset = 3;
// THE GORGE IS SHORT (review round 1, 2026-09-17). The first design swept a spiral round the basin with
// a post every 15 tiles: the pond lay about 170 steps from home, every tower 77 to 133 from its owner.
// In ten games no AI completed more than one school even with no towers at all, every starting tower
// fired its 32 shots and was never resupplied, and the seal fell open unfought. So there is one pinch
// for every colony, its tower on the other side of the gorge from the last, the pinches
// kLeastPinchSpacing to kPinchSpacing apart along the axis (unevenly, so the canyons are not a comb's
// teeth): neighbours stand across the gorge and 10 or more along it from each other, and towers on
// one side 20 or more apart, beyond a level-3 tower's reach of 9. The first pinch lies kMouthKeep
// inside the mouth's cliff, deep enough that no tower outside it reaches the ledge, and the last
// kBasinKeep from the basin: about 58 tiles of axis for four colonies. The gorge winds in a bend
// between every two pinches, swaying either side of its axis.
constexpr double kPinchSpacing = 12, kLeastPinchSpacing = 10, kBasinKeep = 10, kMouthKeep = 12;
constexpr double kGorgeStep = 0.7, kGorgeLeastSway = 1.5, kGorgeGreatestSway = 3;
constexpr double kGorgeBeyondMouth = 7;
// The gorge's half width in corners (about four tiles to walk), narrowing at a pinch to kPinchHalf (two
// to three tiles) for kPinchHalfLength tiles either side and easing back over kPinchEase more.
constexpr double kGorgeHalf = 1.0, kPinchHalf = 0.7, kPinchHalfLength = 1.5, kPinchEase = 2.5;
// Facing towers stand behind kPostWall tiles of rock each: with the two-tile pinch between them that
// is ten tiles from footprint to footprint, beyond a level-3 tower's reach with a tile to spare for
// the raster, and a level-2 tower (range 7, the least the map starts with) still covers the whole
// pinch over its own wall. With three-tile walls one facing pair in three came out in range.
constexpr int kPostWall = 2, kLeastPostWall = 2, kLeastTowerRange = 5, kCoverTowerRange = 5, kGreatestTowerRange = 9;
// A pinch's cross-section: the gorge's floor within kCoverReach tiles of the pinch's middle and
// kCoverHalfSlab either side of it along the gorge. A step is at most a diagonal, 1.41 tiles, so a slab
// 1.5 thick cannot be stepped over; the design floods to prove it is a whole cross-section.
constexpr double kCoverReach = 4.5, kCoverHalfSlab = 0.75;
// A ledge is carved, not searched for: the band of ground from kPostWall tiles back from the gorge's
// floor to kLedgeDepth more, kLedgeHalfLength either way along the axis (up to kLedgeStretchSpread
// longer), both swayed by a fine noise so no two are one stamp. One that fits no tower, pad and canyon
// is carved again a little deeper and longer, kLedgeAttempts times; kLedgeGap of rock parts two posts.
constexpr double kLedgeStretchSpread = 0.25;
constexpr int kLedgeGap = 1, kLedgeDepth = 3, kLedgeAttempts = 3;
constexpr double kLedgeHalfLength = 3;
// A box canyon heads sideways from a deep ledge and forwards, towards the mouth's side, from one near
// the mouth, so the canyons of one side fan out.
constexpr double kCanyonFirstLane = 9, kCanyonLaneGap = 9, kCanyonLaneJitter = 2.5, kCanyonBendSpacing = 11;
constexpr double kCanyonWobble = 2.2, kCanyonFan = 1.2;
// A box canyon widens from its ledge to its mouth and bows a little.
constexpr double kCanyonHeadHalf = 0.9, kCanyonMouthHalf = 1.5, kCanyonBend = 0.1, kCanyonBeyondRim = 6;
// Blind canyons notch the rest of the rim, this far in angle from any other canyon and this many
// corners from the gorge, the ledges and the basin.
constexpr int kBlindCanyons = 3, kBlindCanyonSpread = 3, kBlindAttempts = 12, kBlindKeepOut = 4;
constexpr double kBlindGapDegrees = 22, kBlindDeepest = 14, kBlindShallowest = 4;
// A wash runs out from every canyon and from the gorge's mouth, swaying, thinning to nothing. They all
// turn towards the way the country drains, one direction a map (first look: straight out from the
// massif on every side they read as a spider's legs): a wash's end lies kWashDrain of its length down
// the drainage and the rest straight out, or kWashDrainBehind where the drainage runs back into the rock.
constexpr double kWashShare = 0.3, kWashSway = 0.2, kWashHalf = 1.3, kWashEndHalf = 0.35;
constexpr double kWashDrain = 0.7, kWashDrainBehind = 0.25;
constexpr int kWashBends = 3;
// Springs: rough ponds of kLeastSpring to kGreatestSpring tiles' radius, the control's number per
// 256x256 of country plus one per colony, kept off the massif, the washes and one another.
constexpr int kLeastSpring = 3, kGreatestSpring = 7, kSpringAttempts = 30;
constexpr int kSpringMassifGap = 14, kSpringWashGap = 5, kSpringGap = 12;
constexpr double kSpringRoughness = 0.5;
// Buttes: rough stone outcrops of kLeastButte to kGreatestButte tiles' radius, twice the control's
// number per 256x256, kept off water, washes, the massif and one another; no site within kButteRoom.
// Review round 1: at radius 1.6 to 4.2 they rasterised as five-tile plus signs and read as noise, and
// the massif was the only rock with any size. They are mesas now, 3 to 9 tiles in radius, some close
// to the massif, so it reads as the biggest of a family.
constexpr double kLeastButte = 3, kGreatestButte = 9, kButteRoughness = 0.4;
constexpr int kButteAttempts = 20, kButteGap = 14, kButteWaterGap = 5, kButteMassifGap = 6, kButteRoom = 8;
// Sites (Central Quarry's constants unless said): a square of pure grass kSiteRoom out on every side,
// kSiteMassifClearance from the massif. A colony behind the massif walks round it, so its straight
// line to the mouth is far shorter than its walk: the floor on that is lower than round a lake.
constexpr int kSiteRoom = 4, kSiteMassifClearance = 12;
// A colony behind the plateau walks round it, so its straight line to the mouth is far shorter than
// its walk: the floor on that is lower than round Central Quarry's lake. The walk to the mouth is
// capped at 55 steps, not 70: the gorge and the basin add 60 more to the pond.
constexpr int kLeastStraightPercent = 45, kGreatestWalk = 55, kGreatestWalkCeiling = 95;
// The spacing worth searching for. The shared search keeps a preferred (evenly fed) spread at 85% of
// it, which is the spacing floor: at 60 the evenly fed spread was passed over on four maps in six for
// a wider one on any ground, and all three preferences were searched every time (profiling).
constexpr int kSiteSpacing = 54;
// A colony's walk to its own ledge may be this much longer than the longest walk to the mouth: its
// canyon's length, the deepest of which runs the gorge's length.
constexpr int kLedgeWalkBeyondGorge = 35;
// No buildable ground outside the plateau within a level-2 tower's reach of a ledge.
constexpr int kFrontReach = 7;
// Every colony beyond four widens the fan by kWalkPerExtraColony steps (eight colonies on 256x256
// found no room at 50).
constexpr int kExtraColoniesFrom = 4, kWalkPerExtraColony = 9;
// Two colonies nearer than this by walking are refused: 45 steps, or 36 with six colonies or more
// (review round 2: pairs 23 to 30 steps apart, and the near pair were the two worst colonies).
constexpr int kLeastSpacing = 45, kCrowdedLeastSpacing = 36, kCrowdedFrom = 6;
constexpr int kRoomRadius = 10;
constexpr std::uint32_t kFertilityFloor = 3500;
constexpr int kPondsPerColony = 2, kPondCorners = 180, kPondNearest = 7, kPondFarthest = 13,
			  kSecondPondStep = 3;
constexpr double kPondRipple = 3;
constexpr int kRouteKeepSlack = 2, kRouteKeepMargin = 3;
constexpr int kFieldClearing = 5, kHomeGreen = 22;
// A granted school stands this far from its site at least and at most.
constexpr int kSchoolClearance = 5, kSchoolReach = 10;
constexpr int kHomeRadius = 14, kKitWheat = 36, kKitWood = 18;
constexpr int kLeastWheatNearby = 30, kNearbyReach = 12, kWheatMargin = 12;
constexpr int kTrailCorridor = 2, kKitWaterReach = 20;
constexpr int kFrayDepth = 2, kFrayPeriod = 8, kLeastFieldReach = 9, kFieldReachSpread = 4;
constexpr int kHomeFields = 20;
// Nothing is planted within this many tiles of the massif or of the gorge's mouth: the approaches
// stay open ground for armies and inns.
constexpr int kMassifClearing = 3, kMouthClearing = 8;
// The desert: the control's share of the country is bare sand, in patches where a noise field is
// driest, never within kOasisReach tiles of a spring (the ground that feeds a colony) and less likely
// towards it, nor within kMouthGreen of the gorge's mouth, where inns and towers want grass.
constexpr int kDesertPeriod = 56, kDesertGrain = 9, kOasisReach = 10, kOasisFade = 26, kMouthGreen = 12;
constexpr double kOasisGrainTiles = 6;
// Scrub: a lone tree per this many tiles of ground where no crop regrows, so it never spreads.
constexpr int kTilesPerScrub = 140;
// The basin's garden (Central Quarry's, against the basin's rock instead of a shore).
constexpr double kGardenShare = 0.3, kLeastGarden = 3.5, kGreatestGarden = 5, kGardenTurnDegrees = 110;
constexpr int kGardenSealPeriod = 4, kGardenWheat = 10, kGardenWood = 8;
// Algae goes on the pond's water within this many tiles of its beach, where it regrows.
constexpr int kAlgaeShallows = 4;
// The least 3x3 footprints (overlapping anchors) the basin offers: 128x128 maps have 36 to 40.
constexpr int kLeastBasinRoom = 24;
constexpr StepCosts kRouteCosts{1, 3, -1, -1, -1};
constexpr int kRouteRadius = 1, kTrailNoisePeriod = 16;
constexpr int kWalkSlack = 12, kSiteSlack = 8;

// One colony's place on the gorge: its pinch, the ledge behind the wall there, the tower and the
// open pad on the ledge (top-left tiles), and the box canyon out to the country.
struct Post
{
	double x = 0, y = 0;       // the pinch's middle
	double angle = 0;          // the box canyon's heading, out from the middle of the map
	std::vector<int> cover;    // the gorge's floor tiles at the pinch: a whole cross-section
	std::vector<int> ledge;    // the ledge's tiles
	std::vector<StrokePoint> canyon; // the box canyon, ledge to country
	double side = 1;                 // which flank of the gorge it stands on
	int tower = -1, pad = -1;
};

struct Layout
{
	Torus t{1, 1};
	TerrainSketch terrain;             // with beaches laid
	std::vector<unsigned char> massif; // tiles inside the massif's outline
	std::vector<unsigned char> rock;   // tiles that take stone: the massif's walls
	std::vector<unsigned char> gorge;  // the gorge's floor, mouth to basin
	std::vector<unsigned char> mouth;  // the gorge's floor outside the massif: what the colonies walk to
	std::vector<unsigned char> basin;  // the basin's ground and pond
	std::vector<unsigned char> pond;   // the pond's pure water
	std::vector<unsigned char> garden; // the basin garden's tiles, inside its seal
	std::vector<unsigned char> washes;  // tiles touching a wash
	std::vector<unsigned char> buttes;  // tiles that take a butte's stone
	std::vector<unsigned char> noFields;
	std::vector<Post> posts;            // by colony
	std::vector<int> sites, territory;
	int distance = 0, band = 0;
	double gorgeLength = 0;
	std::string failure;
	bool redraw = false; // whether another draw of the design could mend the failure
};

// The massif's radii from the request alone, and whether it fits the map: shared by the design and
// the cheap request check.
struct Sizes
{
	double mapScale = 1, pond = 0, basin = 0, gorge = 0, flank = 0, back = 0;
	bool fits = true;
};
Sizes sizesFor(const GenerationRequest &request)
{
	const HiddenOasisOptions o(request);
	const int side = std::min(1 << request.wDec, 1 << request.hDec);
	const bool smallMap = side <= 128;
	Sizes s;
	s.mapScale = std::max(1.0, side / 256.0);
	s.pond = smallMap ? std::min(double(o.pondSize), kSmallMapPond) : o.pondSize * (1 + kPondGrowth * (s.mapScale - 1));
	s.basin = s.pond + (smallMap ? kSmallMapBasinRing : kBasinRing);
	// Rock enough either side of the gorge for the outermost lane of box canyons and a wall beyond it.
	s.flank = kCanyonFirstLane + kCanyonLaneGap * std::max(0, (request.nbTeams + 1) / 2 - 1) + kCanyonLaneJitter + kFlankWall;
	// The gorge from the basin's rim to the mouth's cliff, along the axis; its bends make the walk longer.
	const int pinches = request.nbTeams;
	s.gorge = kMouthKeep + std::max(0, pinches - 1) * kPinchSpacing + (smallMap ? kSmallMapBasinKeep : kBasinKeep);
	s.back = smallMap ? kSmallMapBackWall : kBackWall;
	s.fits = (2 * s.basin + s.back + s.gorge) * (1 + kMassifRoughness / 2) <= kGreatestMassifShare * side;
	return s;
}

// The savanna kit with its outcrops and groves scaled here, in hundred-thousandths, so that every step
// of the stone and fruit amounts changes the map (control study, 2026-09-17: scaled as whole numbers
// per 1000 tiles, stone 50 and 75 were one map, and fruit moved only at 50, 150 and 250).
BiomeKit steppeKit(const ResourceAmounts &amounts)
{
	const BiomeKit plain = savanna();
	BiomeKit kit = scaledBiome(plain, amounts);
	kit.wallThickness = 0;
	kit.outcropsPer1000 = 0;
	kit.grovesPer1000 = 0;
	kit.outcropsPer100000 = int(scaledCount(plain.outcropsPer1000 * 100, amounts.stone));
	kit.grovesPer100000 = int(scaledCount(plain.grovesPer1000 * 100, amounts.fruit));
	return kit;
}

// The tiles touching any corner of `corners`: the ground a stroke of sand or water spoils.
std::vector<unsigned char> tilesTouching(const Torus &t, const std::vector<unsigned char> &corners)
{
	std::vector<unsigned char> tiles(t.size(), 0);
	for (int i = 0; i < t.size(); ++i)
		if (corners[i])
			for (int dy = -1; dy <= 0; ++dy)
				for (int dx = -1; dx <= 0; ++dx)
					tiles[t.at(i % t.w + dx, i / t.w + dy)] = 1;
	return tiles;
}

// The tiles all four of whose corners lie in `corners`.
std::vector<unsigned char> tilesWithin(const Torus &t, const std::vector<unsigned char> &corners)
{
	std::vector<unsigned char> tiles(t.size(), 0);
	for (int i = 0; i < t.size(); ++i)
	{
		const int x = i % t.w, y = i / t.w;
		tiles[i] = corners[i] && corners[t.at(x + 1, y)] && corners[t.at(x, y + 1)] && corners[t.at(x + 1, y + 1)];
	}
	return tiles;
}

// How far beyond a 2x2 footprint at (fx, fy) the tile (x, y) lies, in the square rings a tower scans.
int beyondFootprint(const Torus &t, int fx, int fy, int x, int y)
{
	const int dx = t.offsetX(fx, x), dy = t.offsetY(fy, y);
	return std::max({-dx, dx - 1, -dy, dy - 1, 0});
}

Layout designAttempt(const GenerationRequest &request, GenerationContext &context)
{
	const HiddenOasisOptions o(request);
	Layout L;
	L.t = Torus(1 << request.wDec, 1 << request.hDec);
	const Torus &t = L.t;
	const int n = t.size(), teams = request.nbTeams;
	L.terrain.assign(n, GRASS);
	if (teams < 2)
	{
		L.failure = "Hidden Oasis needs at least two colonies.";
		return L;
	}
	if (teams > kGreatestColonies)
	{
		L.failure = "Hidden Oasis holds at most eight colonies.";
		return L;
	}
	const Sizes size = sizesFor(request);
	if (!size.fits)
	{
		L.failure = "The massif does not fit this map; use a bigger map or fewer colonies.";
		return L;
	}
	const double cx = t.w / 2, cy = t.h / 2;
	// The plateau's frame: the gorge runs from the basin out along `axis` to the mouth, and the whole
	// massif is centred on the map (nothing on a torus is more central than anything else, but the
	// preview and the minimap show it where a player looks first).
	const double axis = context.bounded("hidden-oasis-gorge", 3600) / 3600.0 * 2 * kPi;
	const double ax = std::cos(axis), ay = std::sin(axis);
	const double back = size.basin + size.back, front = size.basin + size.gorge;
	const double bx = cx - ax * (front - back) / 2, by = cy - ay * (front - back) / 2;
	const auto offsetFrom = [&](double ox, double oy, int i)
	{
		return std::pair<double, double>{t.offsetX(int(std::lround(ox)), i % t.w) - (ox - std::lround(ox)),
										 t.offsetY(int(std::lround(oy)), i / t.w) - (oy - std::lround(oy))};
	};

	// The gorge: a winding slot from the mouth in to the basin, its bends alternating either side of
	// the axis (review round 1: a radius falling with angle drew a compass spiral).
	// The pinches lie on the axis, where the slot crosses from one bend to the next and runs straight
	// (a pinch on a bend had the gorge curling round the ledge inside it); the bends lie between them.
	const int pinches = teams;
	std::vector<ShapePoint> waypoints, pinchPoints;
	{
		double side = context.bounded("hidden-oasis-gorge", 2) ? 1 : -1;
		const auto onAxis = [&](double along, double sway)
		{ return ShapePoint{bx + ax * along - ay * sway, by + ay * along + ax * sway}; };
		const auto sway = [&]()
		{
			const double tiles = side * (kGorgeLeastSway + context.bounded("hidden-oasis-gorge", 1000) / 1000.0 *
																(kGorgeGreatestSway - kGorgeLeastSway));
			side = -side;
			return tiles;
		};
		waypoints.push_back(onAxis(front + kGorgeBeyondMouth, 0));
		waypoints.push_back(onAxis(front - 1, 0));
		double last = front;
		for (int q = 0; q < pinches; ++q)
		{
			// Uneven spacing, so the box canyons are not a comb's teeth (review round 2: a fishbone).
			const double at = q == 0 ? front - kMouthKeep
									 : last - kLeastPinchSpacing -
										   context.bounded("hidden-oasis-gorge", 1000) / 1000.0 * (kPinchSpacing - kLeastPinchSpacing);
			if (q > 0)
				waypoints.push_back(onAxis((last + at) / 2, sway()));
			pinchPoints.push_back(onAxis(at, 0));
			waypoints.push_back(pinchPoints.back());
			last = at;
		}
		waypoints.push_back(onAxis((last + size.basin) / 2, sway()));
		waypoints.push_back(onAxis(size.basin - 3, 0));
	}
	std::vector<StrokePoint> gorge = splinePath(waypoints, kGorgeStep);
	std::vector<double> along(gorge.size(), 0);
	for (size_t p = 1; p < gorge.size(); ++p)
		along[p] = along[p - 1] + std::hypot(gorge[p].x - gorge[p - 1].x, gorge[p].y - gorge[p - 1].y);
	// Where the gorge crosses the mouth's cliff and where it meets the basin.
	size_t rimPoint = 0, basinPoint = gorge.size() - 1;
	while (rimPoint + 1 < gorge.size() &&
		   (gorge[rimPoint].x - bx) * ax + (gorge[rimPoint].y - by) * ay > front)
		++rimPoint;
	while (basinPoint > 0 && std::hypot(gorge[basinPoint].x - bx, gorge[basinPoint].y - by) < size.basin + 1)
		--basinPoint;

	// The massif, the basin and the pond, as corners. The massif is the rock the design needs (a back
	// wall round the basin, and kFlank of rock either side of the gorge for the ledges and their box
	// canyons, ending in a cliff at the mouth) swollen by a rough lobed outline and ragged by a fine
	// noise, so it is an elongated plateau and never a disc.
	const RadialShape massifShape(1.0, kMassifRoughness, context, "hidden-oasis-massif");
	const RadialShape basinShape(size.basin, kBasinRoughness, context, "hidden-oasis-basin");
	const RadialShape pondShape(size.pond, kPondRoughness, context, "hidden-oasis-pond");
	const std::vector<int> rag = fractalNoise(t.w, t.h, kMassifRagPeriod, 2, context.stream("hidden-oasis-massif"));
	const double pondX = bx - kPondDrift * ax, pondY = by - kPondDrift * ay;
	std::vector<unsigned char> massifCorners(n, 0), basinCorners(n, 0);
	const double plateauReach =
		(std::max(back, front + kGorgeBeyondMouth) + size.flank) * (1 + kMassifRoughness) + kMassifRag + 2;
	for (int i = 0; i < n; ++i)
	{
		const auto [dx, dy] = offsetFrom(bx, by, i);
		// Nothing of the plateau lies beyond its longest reach from the basin's middle (profiling: the
		// outline's trigonometry for every corner of a 512 map).
		if (std::abs(dx) > plateauReach || std::abs(dy) > plateauReach)
			continue;
		const double d = std::hypot(dx, dy), a = std::atan2(dy, dx);
		const double swell = massifShape.radiusAt(a) + kMassifRag * rag[i] / 65536.0 / back;
		bool rock = d < back * swell;
		if (!rock && dx * ax + dy * ay <= front + kMassifRag * rag[i] / 65536.0)
			for (size_t p = rimPoint; p <= basinPoint && !rock; p += 3)
				rock = std::hypot(bx + dx - gorge[p].x, by + dy - gorge[p].y) < size.flank * std::max(1.0, swell);
		massifCorners[i] = rock;
		basinCorners[i] = d < std::min(basinShape.radiusAt(a), size.basin + 1);
		const auto [px, py] = offsetFrom(pondX, pondY, i);
		if (std::hypot(px, py) < pondShape.radiusAt(std::atan2(py, px)) && d < size.basin - kPondInset)
			L.terrain[i] = WATER;
	}
	// How far the rock runs from the basin's middle along a heading.
	const auto rimAt = [&](double angle)
	{
		double r = size.basin;
		for (int gap = 0; gap < 4 && r < t.w; r += 1)
			gap = massifCorners[t.at(int(std::lround(bx + std::cos(angle) * r)), int(std::lround(by + std::sin(angle) * r)))]
					  ? 0
					  : gap + 1;
		return r - 4;
	};

	// The garden, against the basin's rock a good turn round from the entrance.
	{
		const double angle = axis + (context.bounded("hidden-oasis-garden", 2) ? 1 : -1) * kGardenTurnDegrees * kPi / 180;
		std::vector<unsigned char> ground(n, 0);
		for (int i = 0; i < n; ++i)
			ground[i] = basinCorners[i] && L.terrain[i] == GRASS;
		SealedOval oval;
		oval.x = bx + (size.basin - 1) * std::cos(angle);
		oval.y = by + (size.basin - 1) * std::sin(angle);
		oval.angle = angle;
		oval.radius = std::clamp(size.basin * kGardenShare, kLeastGarden, kGreatestGarden);
		L.garden = stampSealedOval(L.terrain, t, ground, oval,
								   periodicNoise(t.w, t.h, kGardenSealPeriod, context.stream("hidden-oasis-garden")));
	}
	L.gorgeLength = along[basinPoint] - along[rimPoint];
	context.telemetry.measure("hidden-oasis.gorge.length", L.gorgeLength);

	// The pinches, mouth inwards, and the posts: a colony's tower either side of each pinch, facing
	// each other across the gorge over kPostWall tiles of rock apiece, which with the pinch between
	// them is more than a level-3 tower reaches. An odd colony out has the innermost pinch to itself.
	std::vector<double> pinchAt;
	for (const ShapePoint &pinch : pinchPoints)
	{
		size_t nearest = 0;
		for (size_t p = 1; p < gorge.size(); ++p)
			if (std::hypot(gorge[p].x - pinch.x, gorge[p].y - pinch.y) <
				std::hypot(gorge[nearest].x - pinch.x, gorge[nearest].y - pinch.y))
				nearest = p;
		pinchAt.push_back(along[nearest]);
	}
	for (size_t p = 0; p < gorge.size(); ++p)
	{
		double half = kGorgeHalf;
		for (double s : pinchAt)
		{
			const double away = std::abs(along[p] - s);
			if (away < kPinchHalfLength)
				half = std::min(half, kPinchHalf);
			else if (away < kPinchHalfLength + kPinchEase)
				half = std::min(half, kPinchHalf + (kGorgeHalf - kPinchHalf) * (away - kPinchHalfLength) / kPinchEase);
		}
		gorge[p].halfWidth = half;
	}
	std::vector<unsigned char> gorgeCorners(n, 0);
	strokePath(gorgeCorners, t, gorge);
	const std::vector<unsigned char> gorgeFloor = tilesTouching(t, gorgeCorners);
	std::vector<Post> posts;
	{
		const std::vector<unsigned char> nearGorge = dilate(t, gorgeFloor, kPostWall);
		std::vector<unsigned char> canyonKeepOut(n, 0);
		for (int i = 0; i < n; ++i)
			canyonKeepOut[i] = gorgeCorners[i] || basinCorners[i];
		canyonKeepOut = dilate(t, canyonKeepOut, kPostWall + 1);
		// No starting tower reaches the basin's ground at its own level's range (a level-2 tower's
		// when the colonies start with pads alone).
		const int startRange = kLeastTowerRange;
		const std::vector<unsigned char> nearBasin = dilate(t, tilesTouching(t, basinCorners), startRange);
		// The other posts' ledges and canyons, with a wall's margin: no ledge or canyon may meet them.
		std::vector<unsigned char> occupied(n, 0);
		const std::vector<int> fromFloor = stepsFrom(t, gorgeFloor);
		const std::vector<int> ledgeNoise = periodicNoise(t.w, t.h, 3, context.stream("hidden-oasis-posts"));
		const double oddSide = context.bounded("hidden-oasis-posts", 2) ? 1 : -1;
		for (int k = 0; k < teams; ++k)
		{
			const int q = k;
			const double side = (k % 2 == 0 ? 1 : -1) * oddSide;
			Post post;
			size_t p = 0;
			while (p + 1 < gorge.size() && along[p] < pinchAt[q])
				++p;
			const size_t before = p >= 4 ? p - 4 : 0, after = std::min(gorge.size() - 1, p + 4);
			const double tx = gorge[after].x - gorge[before].x, ty = gorge[after].y - gorge[before].y;
			const double length = std::max(1e-9, std::hypot(tx, ty));
			// The ledge's frame is the axis', not the slot's own: the slot crosses the axis at a slant
			// that alternates from pinch to pinch, which leaned the ledges of one side into each other.
			const double nx = -ay * side, ny = ax * side;
			post.x = gorge[p].x;
			post.y = gorge[p].y;
			for (int dy = -5; dy <= 5; ++dy)
				for (int dx = -5; dx <= 5; ++dx)
				{
					const int i = t.at(int(std::lround(post.x)) + dx, int(std::lround(post.y)) + dy);
					const auto [ox, oy] = offsetFrom(post.x, post.y, i);
					if (gorgeFloor[i] && std::hypot(ox + 0.5, oy + 0.5) <= kCoverReach &&
						std::abs((ox + 0.5) * tx / length + (oy + 0.5) * ty / length) <= kCoverHalfSlab)
						post.cover.push_back(i);
				}
			// EVERY BOX CANYON RUNS FORWARDS AND OPENS ON THE MOUTH'S CLIFF (review round 2). Canyons that
			// left the plateau sideways put a deep ledge's door far round the rock from the mouth, so a
			// map could be fair on the walk to one's tower or on the walk to the prize but not both
			// (mouth walks 2.6 times apart on one map). Running forwards, every door is beside the
			// mouth: every colony is about as far from the prize and from every canyon, and a deep
			// tower's longer canyon is as much farther for its attackers as for its owner. The r-th
			// ledge of a flank slides out to its own lane, kCanyonLaneGap beyond the last, and follows
			// it to the cliff, outside the shallower ledges and their canyons.
			const int lane = int(std::count_if(posts.begin(), posts.end(), [&](const Post &other) { return other.side == side; }));
			const double laneAt = kCanyonFirstLane + lane * kCanyonLaneGap + context.bounded("hidden-oasis-canyons", 1000) / 1000.0 * kCanyonLaneJitter;
			const double heading = axis;
			// Ledges come in a few sizes and an oval's stretch, so they are not a row of one stamp.
			const double ledgeStretch = 1 + context.bounded("hidden-oasis-posts", 1000) / 1000.0 * kLedgeStretchSpread;
			// The ledge is carved, not searched for: the ground on this side of the pinch from kPostWall
			// tiles back from the gorge's floor to kLedgeDepth more, kLedgeHalfLength either way along the
			// gorge, both swayed by a noise, so the wall is exact by construction and the tower hugs it.
			// A ledge that fits no tower, pad and canyon is carved again a little deeper and longer.
			for (int attempt = 0; attempt < kLedgeAttempts && post.tower < 0; ++attempt)
			{
				std::vector<int> ledge;
				std::vector<unsigned char> onLedge(n, 0);
				double sumX = 0, sumY = 0;
				bool walled = true;
				const int reach = int(kPostWall + kLedgeDepth + kLedgeHalfLength) + 4;
				for (int dy = -reach; dy <= reach; ++dy)
					for (int dx = -reach; dx <= reach; ++dx)
					{
						const int i = t.at(int(std::lround(post.x)) + dx, int(std::lround(post.y)) + dy);
						const auto [ox, oy] = offsetFrom(post.x, post.y, i);
						const double u = (ox + 0.5) * ax + (oy + 0.5) * ay, v = (ox + 0.5) * nx + (oy + 0.5) * ny;
						const double grain = ledgeNoise[i] / 65536.0;
						if (v <= 0 || std::abs(u) > kLedgeHalfLength * ledgeStretch + grain - 0.5 + 0.75 * attempt ||
							fromFloor[i] <= kLeastPostWall || fromFloor[i] > kPostWall + kLedgeDepth + attempt + int(grain * 2))
							continue;
						walled = walled && !basinCorners[i] && !occupied[i] && massifCorners[i];
						ledge.push_back(i);
						onLedge[i] = 1;
						sumX += ox + 0.5;
						sumY += oy + 0.5;
					}
				if (ledge.empty())
					walled = false;
				const double lx = post.x + (ledge.empty() ? 0 : sumX / ledge.size()),
							 ly = post.y + (ledge.empty() ? 0 : sumY / ledge.size());
				if (!walled)
				{
					continue;
				}
				const auto footprint = [&](int i)
				{
					const int x = i % t.w, y = i / t.w;
					return onLedge[i] && onLedge[t.at(x + 1, y)] && onLedge[t.at(x, y + 1)] && onLedge[t.at(x + 1, y + 1)];
				};
				// The tower sites that cover the pinch at the least tower's range, out of
				// a level-3 tower's range of every tower placed so far.
				std::vector<std::pair<int, int>> towerSites;
				for (int i : ledge)
				{
					if (!footprint(i) || nearBasin[i] || nearBasin[t.at(i % t.w + 1, i / t.w)] ||
						nearBasin[t.at(i % t.w, i / t.w + 1)] || nearBasin[t.at(i % t.w + 1, i / t.w + 1)])
						continue;
					int worst = 0;
					for (int c : post.cover)
						worst = std::max(worst, beyondFootprint(t, i % t.w, i / t.w, c % t.w, c / t.w));
					for (const Post &other : posts)
						for (int dy = 0; dy <= 1; ++dy)
							for (int dx = 0; dx <= 1; ++dx)
								if (beyondFootprint(t, i % t.w, i / t.w, other.tower % t.w + dx, other.tower / t.w + dy) <=
									kGreatestTowerRange)
									worst = INT_MAX;
					if (worst <= kCoverTowerRange)
						towerSites.push_back({worst, i});
				}
				// Farthest first: a tower that only just covers its pinch leaves the one facing it the
				// room to stand out of its range.
				std::sort(towerSites.rbegin(), towerSites.rend());
				if (towerSites.empty())
				{
					continue;
				}
				// The box canyon: from the ledge's outer side out to its lane, then along it to the cliff.
				const double ledgeAlong = (lx - bx) * ax + (ly - by) * ay, ledgeAcross = ((lx - bx) * -ay + (ly - by) * ax) * side;
				const auto inFrame = [&](double a, double l)
				{ return ShapePoint{bx + ax * a - ay * l * side, by + ay * a + ax * l * side}; };
				const double lanes = std::max(laneAt, ledgeAcross + 2);
				// Every lane sways to its own rhythm (review round 3: in lockstep they read as one template).
				const double bendSpacing = kCanyonBendSpacing * (0.7 + 0.6 * context.bounded("hidden-oasis-canyons", 1000) / 1000.0);
				std::vector<ShapePoint> bends{inFrame(ledgeAlong + 1, ledgeAcross + 1.5), inFrame(ledgeAlong + 5, lanes)};
				for (double a = ledgeAlong + 5 + bendSpacing; a < front - 2; a += bendSpacing)
					bends.push_back(inFrame(a, lanes + (context.bounded("hidden-oasis-canyons", 1000) / 1000.0 * 2 - 1) * kCanyonWobble));
				bends.push_back(inFrame(front + kCanyonBeyondRim, lanes * kCanyonFan));
				std::vector<StrokePoint> canyon = splinePath(bends, kGorgeStep);
				for (size_t c = 0; c < canyon.size(); ++c)
					canyon[c].halfWidth = kCanyonHeadHalf + (kCanyonMouthHalf - kCanyonHeadHalf) * c / std::max<size_t>(1, canyon.size() - 1);
				const ShapePoint from = bends.front();
				if (strokeIntersectsMask(t, canyon, canyonKeepOut) || strokeIntersectsMask(t, canyon, occupied))
					canyon.clear();
				if (canyon.empty())
				{
					continue;
				}
				// The tower may not stand between the canyon and the rest of the ledge (a tower at the
				// canyon's head shut its owner out of the pad behind it): with the tower down, every
				// other tile of the ledge is still walked to from the tile where the canyon comes in.
				int door = -1;
				for (int i : ledge)
				{
					const auto [ox, oy] = offsetFrom(from.x, from.y, i);
					if (door < 0 || std::hypot(ox + 0.5, oy + 0.5) < std::hypot(offsetFrom(from.x, from.y, door).first + 0.5,
																				 offsetFrom(from.x, from.y, door).second + 0.5))
						door = i;
				}
				int best = -1, pad = -1;
				for (const auto &[reached, site] : towerSites)
				{
					(void)reached;
					std::vector<unsigned char> open = onLedge;
					for (int dy = 0; dy <= 1; ++dy)
						for (int dx = 0; dx <= 1; ++dx)
							open[t.at(site % t.w + dx, site / t.w + dy)] = 0;
					if (!open[door])
						continue;
					const Reach flood = reachFrom(t, {door}, open, INT_MAX);
					std::vector<unsigned char> walked(n, 0);
					for (int i : flood.tiles)
						walked[i] = 1;
					const auto walkedFootprint = [&](int i)
					{
						const int x = i % t.w, y = i / t.w;
						return walked[i] && walked[t.at(x + 1, y)] && walked[t.at(x, y + 1)] && walked[t.at(x + 1, y + 1)];
					};
					int farthest = -1;
					pad = -1;
					for (int i : ledge)
						if (walkedFootprint(i) && t.chebyshev(site % t.w, site / t.w, i % t.w, i / t.w) >= 2 &&
							t.dist2(site % t.w, site / t.w, i % t.w, i / t.w) > farthest)
						{
							farthest = t.dist2(site % t.w, site / t.w, i % t.w, i / t.w);
							pad = i;
						}
					if (pad < 0)
						continue;
					best = site;
					break;
				}
				if (best < 0)
				{
					continue;
				}
				post.tower = best;
				post.pad = pad;
				post.ledge = ledge;
				post.canyon = canyon;
				post.angle = heading;
				post.side = side;
			}
			if (post.tower < 0)
			{
				L.failure = "The gorge has no ledge for colony " + std::to_string(k) + "'s tower.";
				L.redraw = true;
				context.telemetry.fallback("hidden-oasis.posts.no-ledge", "No ledge fitted a post", k);
				return L;
			}
			std::vector<unsigned char> taken(n, 0);
			for (int i : post.ledge)
				for (int dy = 0; dy <= 1; ++dy)
					for (int dx = 0; dx <= 1; ++dx)
						taken[t.at(i % t.w + dx, i / t.w + dy)] = 1;
			strokePath(taken, t, post.canyon);
			// Two ledges need no seal between them, only rock enough to stay two places.
			taken = dilate(t, taken, kLedgeGap);
			for (int i = 0; i < n; ++i)
				if (!occupied[i] && taken[i])
					occupied[i] = (unsigned char)(k + 1);
			posts.push_back(post);
		}
	}
	// The plateau is padded round every ledge, so no ground outside it lies within a tower's reach of
	// one (kFrontReach, which validateWorld checks): a buttress where a ledge came near the cliff.
	{
		std::vector<unsigned char> ledgeGround(n, 0);
		for (const Post &post : posts)
			for (int i : post.ledge)
				ledgeGround[i] = 1;
		// A tower's reach is a square, so the pad is one too (a round pad left its corners in reach).
		const std::vector<unsigned char> padded = dilate(t, ledgeGround, kFrontReach + 2);
		for (int i = 0; i < n; ++i)
			massifCorners[i] = massifCorners[i] || padded[i];
	}
	for (int i = 0; i < n; ++i)
		if (gorgeCorners[i] && L.terrain[i] == GRASS)
			L.terrain[i] = SAND;

	// The box canyons, and the blind ones that notch the rest of the rim.
	const double mouthAngle = axis;
	std::vector<unsigned char> canyonCorners(n, 0), ledgeTiles(n, 0), ledgeCorners(n, 0);
	std::vector<double> canyonAngles{mouthAngle};
	std::vector<ShapePoint> washStarts;
	std::vector<double> washHeadings;
	washStarts.push_back(waypoints.front());
	washHeadings.push_back(mouthAngle);
	std::vector<unsigned char> keepOut(n, 0);
	for (int i = 0; i < n; ++i)
		keepOut[i] = gorgeCorners[i] || basinCorners[i];
	for (const Post &post : posts)
	{
		for (int i : post.ledge)
		{
			ledgeTiles[i] = 1;
			for (int dy = 0; dy <= 1; ++dy)
				for (int dx = 0; dx <= 1; ++dx)
					keepOut[t.at(i % t.w + dx, i / t.w + dy)] = ledgeCorners[t.at(i % t.w + dx, i / t.w + dy)] = 1;
		}
		strokePath(canyonCorners, t, post.canyon);
		strokePath(keepOut, t, post.canyon);
		canyonAngles.push_back(std::atan2(post.canyon.back().y - by, post.canyon.back().x - bx));
		washStarts.push_back({post.canyon.back().x, post.canyon.back().y});
		washHeadings.push_back(post.angle);
	}
	{
		const std::vector<unsigned char> blindKeepOut = dilate(t, keepOut, kBlindKeepOut);
		const int wantedBlind = kBlindCanyons + int(context.bounded("hidden-oasis-canyons", kBlindCanyonSpread));
		int cut = 0;
		for (int b = 0; b < wantedBlind; ++b)
			for (int attempt = 0; attempt < kBlindAttempts; ++attempt)
			{
				const double angle = context.bounded("hidden-oasis-canyons", 3600) / 3600.0 * 2 * kPi;
				const double depth =
					kBlindShallowest + context.bounded("hidden-oasis-canyons", 1000) / 1000.0 * (kBlindDeepest - kBlindShallowest);
				const double bend = (context.bounded("hidden-oasis-canyons", 1000) / 1000.0 * 2 - 1) * 2 * kCanyonBend;
				bool apart = true;
				for (double other : canyonAngles)
					apart = apart && std::abs(std::remainder(angle - other, 2 * kPi)) >= kBlindGapDegrees * kPi / 180;
				if (!apart)
					continue;
				const ShapePoint from = polarPoint(bx, by, rimAt(angle) + kCanyonBeyondRim, angle);
				const std::vector<StrokePoint> path =
					bentPath(from, angle + kPi, depth + kCanyonBeyondRim, bend, kCanyonMouthHalf, kCanyonHeadHalf,
							 std::max(4, int(depth + kCanyonBeyondRim)));
				if (strokeIntersectsMask(t, path, blindKeepOut))
					continue;
				strokePath(canyonCorners, t, path);
				canyonAngles.push_back(angle);
				washStarts.push_back(from);
				washHeadings.push_back(angle);
				++cut;
				break;
			}
		context.telemetry.measure("hidden-oasis.canyons.blind", cut);
	}
	for (int i = 0; i < n; ++i)
		if (canyonCorners[i] && L.terrain[i] == GRASS && !gorgeCorners[i] && !basinCorners[i] && !ledgeCorners[i])
			L.terrain[i] = SAND;

	// The washes: from every canyon's mouth and the gorge's out across the country.
	std::vector<unsigned char> washCorners(n, 0);
	{
		const double side = std::min(t.w, t.h);
		const double drainage = context.bounded("hidden-oasis-washes", 3600) / 3600.0 * 2 * kPi;
		for (size_t w = 0; w < washStarts.size(); ++w)
		{
			const double length = kWashShare * side * (0.5 + 1.0 * context.bounded("hidden-oasis-washes", 1000) / 1000.0);
			const double skew = (context.bounded("hidden-oasis-washes", 1000) / 1000.0 * 2 - 1) * 0.4;
			const ShapePoint from = washStarts[w];
			const double out = washHeadings[w] + skew;
			const double drain = std::cos(washHeadings[w] - drainage) < 0 ? kWashDrainBehind : kWashDrain;
			const ShapePoint to{from.x + (std::cos(out) * (1 - drain) + std::cos(drainage) * drain) * length,
								from.y + (std::sin(out) * (1 - drain) + std::sin(drainage) * drain) * length};
			const double ux = to.x - from.x, uy = to.y - from.y;
			const double span = std::max(1.0, std::hypot(ux, uy));
			std::vector<ShapePoint> bends{from};
			for (int b = 1; b <= kWashBends; ++b)
			{
				const double f = double(b) / (kWashBends + 1);
				const double sway = (context.bounded("hidden-oasis-washes", 1000) / 1000.0 * 2 - 1) * kWashSway * length * f;
				bends.push_back({from.x + ux * f - uy / span * sway, from.y + uy * f + ux / span * sway});
			}
			bends.push_back(to);
			std::vector<StrokePoint> path = splinePath(bends, kGorgeStep);
			for (size_t p = 0; p < path.size(); ++p)
			{
				const double f = path.size() > 1 ? double(p) / (path.size() - 1) : 0;
				path[p].halfWidth = kWashHalf + (kWashEndHalf - kWashHalf) * f;
			}
			strokePath(washCorners, t, path);
		}
		for (int i = 0; i < n; ++i)
		{
			washCorners[i] = washCorners[i] && !massifCorners[i];
			if (washCorners[i] && L.terrain[i] == GRASS)
				L.terrain[i] = SAND;
		}
	}
	L.washes = tilesTouching(t, washCorners);

	// Springs and buttes, dropped at random and kept apart.
	const int countryArea = std::max(1, n - int(std::count(massifCorners.begin(), massifCorners.end(), 1)));
	std::vector<unsigned char> taken = dilate(t, massifCorners, kSpringMassifGap);
	{
		const std::vector<unsigned char> nearWash = dilate(t, washCorners, kSpringWashGap);
		for (int i = 0; i < n; ++i)
			taken[i] = taken[i] || nearWash[i];
		// Linear in the control, with more for more colonies (control study: as the colony count plus a
		// rounded share, 5 and 6 were one map and 1 gave five springs).
		const int wanted = int(std::lround(o.springs * (1 + teams / 8.0) * countryArea / 65536.0));
		int placed = 0;
		for (int spring = 0; spring < wanted; ++spring)
			for (int attempt = 0; attempt < kSpringAttempts; ++attempt)
			{
				const int sx = int(context.bounded("hidden-oasis-springs", std::uint32_t(t.w)));
				const int sy = int(context.bounded("hidden-oasis-springs", std::uint32_t(t.h)));
				const int span = kGreatestSpring - kLeastSpring + 1;
				const double r = kLeastSpring + std::min(context.bounded("hidden-oasis-springs", span),
														 context.bounded("hidden-oasis-springs", span));
				const double stretch = 1 + context.bounded("hidden-oasis-springs", 61) / 100.0;
				const double axis = context.bounded("hidden-oasis-springs", 1800) / 1800.0 * kPi;
				const RadialShape shape(r, kSpringRoughness, context, "hidden-oasis-springs");
				const int reach = int(std::ceil(r * stretch * (1 + kSpringRoughness))) + 1;
				std::vector<int> corners;
				bool clear = true;
				for (int dy = -reach; dy <= reach && clear; ++dy)
					for (int dx = -reach; dx <= reach && clear; ++dx)
					{
						const double a = (dx * std::cos(axis) + dy * std::sin(axis)) / stretch;
						const double c = -dx * std::sin(axis) + dy * std::cos(axis);
						if (std::hypot(a, c) >= shape.radiusAt(std::atan2(c, a)))
							continue;
						const int i = t.at(sx + dx, sy + dy);
						clear = !taken[i];
						corners.push_back(i);
					}
				if (!clear || corners.empty())
					continue;
				for (int i : corners)
				{
					L.terrain[i] = WATER;
					for (int dy = -kSpringGap; dy <= kSpringGap; ++dy)
						for (int dx = -kSpringGap; dx <= kSpringGap; ++dx)
							taken[t.at(i % t.w + dx, i / t.w + dy)] = 1;
				}
				++placed;
				break;
			}
		context.telemetry.measure("hidden-oasis.springs.wanted", wanted);
		context.telemetry.measure("hidden-oasis.springs.placed", placed);
	}
	// The desert: bare sand where the country is driest, away from the springs.
	std::vector<unsigned char> desertCorners(n, 0);
	{
		std::vector<unsigned char> springs(n, 0), open(n, 0);
		for (int i = 0; i < n; ++i)
			springs[i] = L.terrain[i] == WATER && !massifCorners[i];
		const std::vector<std::int64_t> fromSpring = distanceSquaredTo(t, springs);
		const std::vector<int> grain = periodicNoise(t.w, t.h, kDesertGrain, context.stream("hidden-oasis-desert-grain"));
		std::vector<unsigned char> mouthCorners(n, 0);
		for (int i = 0; i < n; ++i)
			mouthCorners[i] = gorgeCorners[i] && !massifCorners[i];
		const std::vector<std::int64_t> fromMouth = distanceSquaredTo(t, mouthCorners);
		std::vector<int> dryness = fractalNoise(t.w, t.h, kDesertPeriod, 3, context.stream("hidden-oasis-desert"));
		for (int i = 0; i < n; ++i)
		{
			// Round distances, and a fine grain on the reach, so an oasis's edge is neither square nor ruled.
			const double spring = std::min(double(kOasisFade), std::sqrt(double(fromSpring[i]))) +
								  (grain[i] / 65536.0 - 0.5) * kOasisGrainTiles;
			open[i] = L.terrain[i] == GRASS && !massifCorners[i] && spring > kOasisReach &&
					  std::sqrt(double(fromMouth[i])) + (grain[i] / 65536.0 - 0.5) * kOasisGrainTiles > kMouthGreen;
			dryness[i] = dryness[i] * 3 / 8 + grain[i] / 8 +
						 int(32767 * std::clamp(spring, 0.0, double(kOasisFade)) / kOasisFade);
		}
		const std::vector<unsigned char> desert = noisyShare(open, dryness, std::clamp(o.desert, 0, 100));
		desertCorners = desert;
		int sand = 0;
		for (int i = 0; i < n; ++i)
			if (desert[i])
			{
				L.terrain[i] = SAND;
				++sand;
			}
		context.telemetry.measure("hidden-oasis.desert.corners", sand);
	}
	L.buttes.assign(n, 0);
	{
		std::vector<unsigned char> noButte(n, 0);
		for (int i = 0; i < n; ++i)
			noButte[i] = L.terrain[i] == WATER || washCorners[i];
		noButte = dilate(t, noButte, kButteWaterGap);
		// A butte stands on grass (stone takes only pure grass), a tile clear of any sand.
		{
			std::vector<unsigned char> sandy(n, 0);
			for (int i = 0; i < n; ++i)
				sandy[i] = L.terrain[i] == SAND;
			sandy = dilate(t, sandy, 1);
			for (int i = 0; i < n; ++i)
				noButte[i] = noButte[i] || sandy[i];
		}
		const std::vector<unsigned char> nearMassif = dilate(t, massifCorners, kButteMassifGap);
		for (int i = 0; i < n; ++i)
			noButte[i] = noButte[i] || nearMassif[i];
		// Mesas are the country's stone as much as its look, so the stone amount scales them too; the
		// plateau is the map's structure and is never scaled.
		const int wanted = int(scaledCount(std::lround(1.4 * o.buttes * countryArea / 65536.0), o.stone));
		int placed = 0;
		for (int butte = 0; butte < wanted; ++butte)
			for (int attempt = 0; attempt < kButteAttempts; ++attempt)
			{
				const int bx = int(context.bounded("hidden-oasis-buttes", std::uint32_t(t.w)));
				const int by = int(context.bounded("hidden-oasis-buttes", std::uint32_t(t.h)));
				const double r = kLeastButte + std::min(context.bounded("hidden-oasis-buttes", 1000),
														context.bounded("hidden-oasis-buttes", 1000)) /
												   1000.0 * (kGreatestButte - kLeastButte);
				const double stretch = 1 + context.bounded("hidden-oasis-buttes", 81) / 100.0;
				const double axis = context.bounded("hidden-oasis-buttes", 1800) / 1800.0 * kPi;
				const RadialShape shape(r, kButteRoughness, context, "hidden-oasis-buttes");
				const int reach = int(std::ceil(r * stretch * (1 + kButteRoughness))) + 1;
				std::vector<int> tiles;
				bool clear = true;
				for (int dy = -reach; dy <= reach && clear; ++dy)
					for (int dx = -reach; dx <= reach && clear; ++dx)
					{
						const double a = (dx * std::cos(axis) + dy * std::sin(axis)) / stretch;
						const double c = -dx * std::sin(axis) + dy * std::cos(axis);
						if (std::hypot(a, c) >= shape.radiusAt(std::atan2(c, a)))
							continue;
						const int i = t.at(bx + dx, by + dy);
						clear = !noButte[i];
						tiles.push_back(i);
					}
				if (!clear || tiles.empty())
					continue;
				for (int i : tiles)
				{
					L.buttes[i] = 1;
					for (int dy = -kButteGap; dy <= kButteGap; ++dy)
						for (int dx = -kButteGap; dx <= kButteGap; ++dx)
							noButte[t.at(i % t.w + dx, i / t.w + dy)] = 1;
				}
				++placed;
				break;
			}
		context.telemetry.measure("hidden-oasis.buttes.wanted", wanted);
		context.telemetry.measure("hidden-oasis.buttes.placed", placed);
	}

	// The ground as the game will draw it; the rock is every tile of the massif left pure grass that is
	// neither a ledge nor the basin.
	TerrainSketch beached = L.terrain;
	layBeaches(beached, t);
	const std::vector<unsigned char> pureGrass = pureTiles(beached, t, GRASS);
	const std::vector<unsigned char> pureWater = pureTiles(beached, t, WATER);
	L.massif = tilesWithin(t, massifCorners);
	L.basin = tilesWithin(t, basinCorners);
	L.gorge = gorgeFloor;
	L.rock.assign(n, 0);
	L.pond.assign(n, 0);
	L.mouth.assign(n, 0);
	for (int i = 0; i < n; ++i)
	{
		L.rock[i] = L.massif[i] && pureGrass[i] && !ledgeTiles[i] && !L.basin[i];
		L.pond[i] = L.basin[i] && pureWater[i];
		L.buttes[i] = L.buttes[i] && pureGrass[i] && !L.massif[i];
		L.mouth[i] = L.gorge[i] && !massifCorners[i];
		L.garden[i] = L.garden[i] && L.basin[i];
	}
	if (std::count(L.mouth.begin(), L.mouth.end(), 1) == 0)
	{
		L.failure = "The gorge does not open onto the country.";
		return L;
	}
	std::vector<unsigned char> walkable(n, 0);
	for (int i = 0; i < n; ++i)
		walkable[i] = !pureWater[i] && !L.rock[i] && !L.buttes[i];

	// Every tile's walk to the mouth: the one whole-map flood the design needs (profiling, 2026-09-17:
	// whole-map floods were half of generation, most of them proving facts about the plateau alone).
	const std::vector<int> toMouth = stepsFrom(t, L.mouth, walkable);
	// The gorge is open: the basin is walked to from the mouth.
	{
		bool reached = false;
		for (int i = 0; i < n && !reached; ++i)
			reached = L.basin[i] && toMouth[i] >= 0;
		if (!reached)
		{
			L.failure = "The gorge does not reach the basin.";
			return L;
		}
	}
	// The seal, on the design: with any one pinch shut, a walk from the basin never leaves the plateau.
	// Flooded from the basin outwards, so a sound seal costs only the basin and the gorge's inner end.
	std::vector<int> basinGround;
	for (int i = 0; i < n; ++i)
		if (L.basin[i] && walkable[i])
			basinGround.push_back(i);
	for (size_t k = 0; k < posts.size(); ++k)
	{
		std::vector<unsigned char> open = walkable;
		for (int i : posts[k].cover)
			open[i] = 0;
		const Reach sealed = reachFrom(t, basinGround, open, INT_MAX);
		for (int i : sealed.tiles)
			if (!L.massif[i] && !L.gorge[i])
			{
				L.failure = "The gorge leaks past colony " + std::to_string(k) + "'s pinch.";
				context.telemetry.fallback("hidden-oasis.seal.leak", "A pinch was not a whole cross-section", int(k));
				return L;
			}
	}
	// Every ledge is walked to from the country without the gorge: flooded from the ledge, over the
	// plateau and a tile or two beyond it, until it comes out of the rock.
	{
		std::vector<unsigned char> open = dilate(t, L.massif, 2);
		for (int i = 0; i < n; ++i)
			open[i] = open[i] && walkable[i] && !(L.gorge[i] && L.massif[i]);
		for (size_t k = 0; k < posts.size(); ++k)
		{
			const Reach out = reachFrom(t, posts[k].ledge, open, INT_MAX);
			bool reached = false;
			for (int i : out.tiles)
				reached = reached || !L.massif[i];
			if (!reached)
			{
				L.failure = "Colony " + std::to_string(k) + "'s ledge cannot be walked to from the country.";
				L.redraw = true;
				return L;
			}
		}
	}
	// No tower within a level-3 tower's range of another's, or of the basin.
	for (size_t a = 0; a < posts.size(); ++a)
	{
		const int ax = posts[a].tower % t.w, ay = posts[a].tower / t.w;
		for (size_t b = a + 1; b < posts.size(); ++b)
			for (int dy = 0; dy <= 1; ++dy)
				for (int dx = 0; dx <= 1; ++dx)
					if (beyondFootprint(t, ax, ay, posts[b].tower % t.w + dx, posts[b].tower / t.w + dy) <= kGreatestTowerRange)
					{
						L.failure = "Two towers stand within range of each other.";
						return L;
					}
	}

	// The sites: roomy grass in the country, clear of the massif and the buttes, all at about the same
	// walk from the gorge's mouth.
	// The desert keeps off every home (kHomeGreen), so a site's room is counted as if the desert were
	// grass: at desert 60 and more there was otherwise no roomy grass left in the band (review round 3).
	std::vector<unsigned char> roomy;
	{
		TerrainSketch greened = L.terrain;
		for (int i = 0; i < n; ++i)
			if (desertCorners[i])
				greened[i] = GRASS;
		layBeaches(greened, t);
		roomy = erode(t, pureTiles(greened, t, GRASS), kSiteRoom);
	}
	{
		const std::vector<unsigned char> nearMassif = dilate(t, massifCorners, kSiteMassifClearance);
		const std::vector<unsigned char> nearButte = dilate(t, L.buttes, kButteRoom);
		std::vector<unsigned char> backWay = walkable;
		for (int i = 0; i < n; ++i)
			if (L.gorge[i] && L.massif[i])
				backWay[i] = 0;
		for (int i = 0; i < n; ++i)
			roomy[i] = roomy[i] && !nearMassif[i] && !nearButte[i] && toMouth[i] > 0;
		// Central Quarry's search: a band of equal walk to the mouth, spread by walking distance, on
		// watered ground of a similar yield. Every box canyon opens beside the mouth, so a colony that
		// is fairly placed for the prize is fairly placed for every tower's door as well.
		const Fertility::Field startField = cropGrowthField(beached, t);
		const std::vector<int> straight = stepsFrom(t, L.mouth);
		WalkBandRequest band;
		band.greatestWalk = kGreatestWalk;
		band.greatestWalkCeiling = kGreatestWalkCeiling;
		band.extraColoniesFrom = kExtraColoniesFrom;
		band.walkPerExtraColony = kWalkPerExtraColony;
		band.leastSwimPercent = kLeastStraightPercent;
		band.leastSpacing = teams >= kCrowdedFrom ? kCrowdedLeastSpacing : kLeastSpacing;
		band.siteSpacing = kSiteSpacing;
		band.roomRadius = kRoomRadius;
		band.fertilityFloor = kFertilityFloor;
		const WalkBandStarts starts = spreadInWalkBand(t, roomy, walkable, toMouth, straight, startField, teams,
													   size.mapScale, size.flank, context, "hidden-oasis-sites",
													   "hidden-oasis.sites.candidates", band);
		if (starts.sites.empty() || starts.spacing < band.leastSpacing)
		{
			L.failure = "This map has no room for that many colonies; use a bigger map or fewer colonies.";
			L.redraw = true;
			return L;
		}
		L.sites = starts.sites;
		L.distance = starts.distance;
		L.band = starts.band;
		context.telemetry.measure("hidden-oasis.sites.distance", L.distance);
		context.telemetry.measure("hidden-oasis.sites.band", L.band);
		context.telemetry.measure("hidden-oasis.sites.spacing", starts.spacing);
		context.telemetry.choice("hidden-oasis.sites.preference", starts.preference);
		dealStarts(context, L.sites, "hidden-oasis-deal");
		// Posts go to colonies so that the walks from home to ledge, the back way, are as short and as
		// alike as they can be: in order to start with, then any two colonies swap posts while that
		// lowers the sum of the squared walks.
		{
			std::vector<std::vector<std::int64_t>> walk(teams, std::vector<std::int64_t>(teams, 0));
			for (int p = 0; p < teams; ++p)
			{
				// No farther than any site can be: the band's far edge, the gorge's length and slack.
				const Flood reach = floodFrom(t, tileMask(t, posts[p].ledge), backWay,
											  L.distance + L.band + int(L.gorgeLength + size.flank) + kLedgeWalkBeyondGorge + kSiteSlack);
				for (int k = 0; k < teams; ++k)
					walk[k][p] = reach.steps[L.sites[k]] < 0 ? 100000 : reach.steps[L.sites[k]];
			}
			std::vector<int> postOf(teams);
			for (int k = 0; k < teams; ++k)
				postOf[k] = k;
			for (bool swapped = true; swapped;)
			{
				swapped = false;
				for (int a = 0; a < teams; ++a)
					for (int b = a + 1; b < teams; ++b)
					{
						const std::int64_t now = walk[a][postOf[a]] * walk[a][postOf[a]] + walk[b][postOf[b]] * walk[b][postOf[b]];
						const std::int64_t then = walk[a][postOf[b]] * walk[a][postOf[b]] + walk[b][postOf[a]] * walk[b][postOf[a]];
						if (then < now)
						{
							std::swap(postOf[a], postOf[b]);
							swapped = true;
						}
					}
			}
			L.posts.resize(teams);
			for (int k = 0; k < teams; ++k)
			{
				L.posts[k] = posts[postOf[k]];
				context.telemetry.measure("hidden-oasis.posts.design-walk", walk[k][postOf[k]], k);
				// A post's place along the gorge, 0 nearest the mouth: a tournament is read by it.
				context.telemetry.measure("hidden-oasis.posts.depth-order", postOf[k], k);
			}
		}

		// A home is an oasis: the desert keeps kHomeGreen tiles off every site (a site on the desert's
		// edge had no grass for its kit's wheat, and shipped with 16 to 25 wheat tiles near its swarm).
		for (int site : L.sites)
			for (int dy = -kHomeGreen; dy <= kHomeGreen; ++dy)
				for (int dx = -kHomeGreen; dx <= kHomeGreen; ++dx)
				{
					const int i = t.at(site % t.w + dx, site / t.w + dy);
					if (dx * dx + dy * dy <= kHomeGreen * kHomeGreen && L.terrain[i] == SAND && desertCorners[i] &&
						!washCorners[i])
						L.terrain[i] = GRASS;
				}
		std::vector<unsigned char> land(n, 0);
		for (int i = 0; i < n; ++i)
			land[i] = walkable[i] && !massifCorners[i];
		L.territory = firstWalkTerritories(t, land, L.sites, kSiteRoom);

		// A pond for every start that landed dry, off its walks to the mouth.
		const std::vector<int> ripple = periodicNoise(t.w, t.h, 5, context.stream("hidden-oasis-ponds"));
		std::vector<int> queued(n, 0);
		const DryStartPonds plan{kPondsPerColony, kPondCorners, kPondNearest, kPondFarthest + 1, kSecondPondStep};
		const std::vector<unsigned char> dryZone = dilate(t, massifCorners, kSpringMassifGap);
		for (int k = 0; k < teams; ++k)
		{
			const int longest = toMouth[L.sites[k]] + kRouteKeepSlack;
			const Flood fromSite = floodFrom(t, tileMask(t, {L.sites[k]}), walkable, longest);
			std::vector<unsigned char> keepDry(n, 0);
			for (int i : fromSite.visited)
				if (toMouth[i] >= 0 && fromSite.steps[i] + toMouth[i] <= longest)
					for (int dy = -kRouteKeepMargin; dy <= kRouteKeepMargin; ++dy)
						for (int dx = -kRouteKeepMargin; dx <= kRouteKeepMargin; ++dx)
							keepDry[t.at(i % t.w + dx, i / t.w + dy)] = 1;
			const DryStartWatering watered = waterDrySite(
				L.terrain, t, L.sites[k], kRoomRadius, kFertilityFloor, plan,
				[&](int i)
				{
					return L.territory[i] == k && L.terrain[i] == GRASS && !dryZone[i] && !keepDry[i] &&
						   !L.buttes[i];
				},
				[&](int i) { return kPondRipple * ripple[i] / 65536.0; }, queued, k * kPondsPerColony + 1);
			const int dug = int(std::count_if(watered.dug.begin(), watered.dug.end(), [](int c) { return c > 0; }));
			if (!watered.dug.empty() && watered.dug.back() == 0)
				context.telemetry.fallback("hidden-oasis.ponds.none", "No room for a pond beside a dry site", k);
			context.telemetry.measure("hidden-oasis.ponds.fertility-before", watered.before, k);
			context.telemetry.measure("hidden-oasis.ponds.dug", dug, k);
			context.telemetry.measure("hidden-oasis.ponds.fertility-after", watered.after, k);
		}
	}
	std::vector<unsigned char> homes(n, 0);
	for (int site : L.sites)
		homes[site] = 1;
	L.noFields = dilate(t, homes, kFieldClearing);
	layBeaches(L.terrain, t);

	context.telemetry.measure("hidden-oasis.gorge.axis-length", size.gorge);
	context.telemetry.measure("hidden-oasis.basin.radius", size.basin);
	context.telemetry.measure("hidden-oasis.pond.radius", size.pond);
	context.telemetry.measure("hidden-oasis.rock.tiles", std::count(L.rock.begin(), L.rock.end(), 1));
	return L;
}

// A gorge whose bends left no room for some ledge, or a country whose springs left the band no room for
// the colonies, is drawn again: the streams have moved on, so the next attempt is another plateau and
// another country. Review round 3: about 13% of default requests were refused for one or the other.
// Refusals that no redraw can mend (too many colonies, a map too small) come back at once.
Layout designAfresh(const GenerationRequest &request, GenerationContext &context)
{
	Layout L;
	for (int attempt = 0; attempt < kDesignAttempts; ++attempt)
	{
		L = designAttempt(request, context);
		if (L.failure.empty() || !L.redraw)
			break;
		context.telemetry.fallback("hidden-oasis.design.redrawn", L.failure, attempt);
	}
	return L;
}

Layout design(const GenerationRequest &request, GenerationContext &context)
{
	return cachedDesign<Layout>(request, context, designAfresh);
}

bool generate(Game &game, GenerationContext &context)
{
	context.stage = "hidden-oasis layout";
	const HiddenOasisOptions o(context.request);
	const Layout L = design(context.request, context);
	if (!L.failure.empty())
	{
		context.detail = L.failure;
		return false;
	}
	Map &map = game.map;
	const Torus &t = L.t;
	const int n = t.size(), teams = context.request.nbTeams;
	for (int k = 0; k < teams; ++k)
		game.addTeam();

	context.stage = "hidden-oasis terrain";
	writeUndermap(map, L.terrain);

	context.stage = "hidden-oasis colonies";
	const auto homeMask = [&](int team)
	{
		std::vector<unsigned char> ground(size_t(n), 0);
		for (int i = 0; i < n; ++i)
			ground[i] = L.territory[i] == team && !L.buttes[i] && map.isGrass(i % t.w, i / t.w);
		return ground;
	};
	const auto anchor = [&](int team)
	{ return MapGeneratorPoint(L.sites[team] % t.w - 2, L.sites[team] / t.w - 2); };
	if (!settleColonies(game, context, "hidden-oasis-starts", homeMask, anchor))
		return false;

	// The granted school (on by default; off is the total lock). A finished level-0 school beside the
	// swarm lets a colony train builders without the pond, which still gates every school upgrade, the
	// level-2 pool and the top tower. Review rounds 1 to 3: with the seal holding, no AI completed a
	// school in any towers-on game, so without it an AI game is played at level 0 throughout.
	if (o.school)
		for (int k = 0; k < teams; ++k)
		{
			const int sx = L.sites[k] % t.w, sy = L.sites[k] / t.w;
			std::vector<unsigned char> allowed(n, 0);
			for (int dy = -kSchoolReach; dy <= kSchoolReach; ++dy)
				for (int dx = -kSchoolReach; dx <= kSchoolReach; ++dx)
				{
					const int i = t.at(sx + dx, sy + dy);
					allowed[i] = L.territory[i] == k && std::max(std::abs(dx), std::abs(dy)) >= kSchoolClearance &&
								 clearGround(map, i % t.w, i / t.w);
				}
			if (placeStartingBuilding(game, k, "school", 0, sx, sy, kSchoolReach, allowed) < 0)
			{
				context.detail = "Colony " + std::to_string(k) + " has no room for its school.";
				return false;
			}
		}

	// The rock: the massif's walls and the buttes.
	context.stage = "hidden-oasis rock";
	std::vector<unsigned char> stone(n, 0);
	for (int i = 0; i < n; ++i)
		if ((L.rock[i] || L.buttes[i]) && clearGround(map, i % t.w, i / t.w))
		{
			map.setResource(i % t.w, i / t.w, STONE, 1);
			stone[i] = 1;
		}
	for (int i = 0; i < n; ++i)
		if (L.rock[i] && !stone[i])
		{
			context.detail = "The massif's wall has a gap at (" + std::to_string(i % t.w) + ", " +
							 std::to_string(i / t.w) + ").";
			return false;
		}

	// The country: the steppe kit off the massif's approaches, the swarms' clearings and the washes.
	context.stage = "hidden-oasis resources";
	const ResourceAmounts amounts{o.wheat, o.wood, o.stone, 100, o.fruit};
	const std::vector<unsigned char> reserved = swarmSurroundings(t, context);
	std::vector<unsigned char> ledges(n, 0), sitesMask(n, 0);
	for (const Post &post : L.posts)
		for (int i : post.ledge)
			ledges[i] = 1;
	const std::vector<unsigned char> nearMassif = dilate(t, L.massif, kMassifClearing);
	const std::vector<unsigned char> nearMouth = dilate(t, L.mouth, kMouthClearing);
	std::vector<unsigned char> country(n, 0), ambientClear(n, 0);
	for (int i = 0; i < n; ++i)
	{
		country[i] = !nearMassif[i] && !L.buttes[i] && map.isGrass(i % t.w, i / t.w);
		ambientClear[i] = reserved[i] || L.noFields[i] || nearMouth[i] || L.washes[i];
	}
	const std::vector<unsigned char> none(n, 0);
	furnishBiome(map, t, context, country, BiomeTerrain{none, none, none}, steppeKit(amounts),
				 ambientClear, "hidden-oasis-steppe");
	for (int site : L.sites)
		sitesMask[site] = 1;
	const std::vector<unsigned char> homeFields = dilate(t, sitesMask, kHomeFields);
	const Fertility::Field watered = Fertility::forMap(map, false);
	context.telemetry.measure(
		"hidden-oasis.fields.trimmed",
		trimFieldsBeyondWater(map, t, homeFields, watered, kLeastFieldReach, kFieldReachSpread,
							  periodicNoise(t.w, t.h, kFrayPeriod * 2, context.stream("hidden-oasis-field-reach"))));
	context.telemetry.measure(
		"hidden-oasis.fields.frayed",
		frayFieldEdges(map, t, homeFields, kFrayDepth,
					   periodicNoise(t.w, t.h, kFrayPeriod, context.stream("hidden-oasis-fray"))));
	// Scrub: lone trees where nothing regrows, so the steppe is not bare and never grows shut.
	{
		int dry = 0;
		for (int i = 0; i < n; ++i)
			dry += country[i] && watered.at(i % t.w, i / t.w) == 0;
		const int wanted = scaledCount(dry / kTilesPerScrub, o.wood);
		int planted = 0;
		for (int attempt = 0; attempt < wanted * 4 && planted < wanted; ++attempt)
		{
			const int i = int(context.bounded("hidden-oasis-scrub", std::uint32_t(n)));
			const int x = i % t.w, y = i / t.w;
			if (!country[i] || ambientClear[i] || watered.at(x, y) != 0 || !clearGround(map, x, y) ||
				!map.isResourceAllowed(x, y, WOOD))
				continue;
			map.setResource(x, y, WOOD, 1);
			++planted;
		}
		context.telemetry.measure("hidden-oasis.scrub.planted", planted);
	}
	std::vector<unsigned char> ambient(n, 0);
	for (int i = 0; i < n; ++i)
		ambient[i] = map.isResource(i % t.w, i / t.w);
	// Every colony's shortest way to the mouth, widened: no kit is planted on it.
	std::vector<unsigned char> corridor(n, 0);
	{
		const std::vector<std::vector<int>> workers = unitTilesByTeam(map, teams);
		for (int k = 0; k < teams; ++k)
		{
			if (workers[k].empty())
				continue;
			const std::vector<int> route =
				cheapestWalk(t, GridNeighbors::Eight, workers[k], L.mouth,
							 [&](int, int to, int, int)
							 {
								 const int x = to % t.w, y = to / t.w;
								 if (map.isWater(x, y) || map.getBuilding(x, y) != NOGBID || stone[to])
									 return -1;
								 return map.isResource(x, y) ? 11 : 10;
							 });
			for (int i : route)
				for (int dy = -kTrailCorridor; dy <= kTrailCorridor; ++dy)
					for (int dx = -kTrailCorridor; dx <= kTrailCorridor; ++dx)
						corridor[t.at(i % t.w + dx, i / t.w + dy)] = 1;
		}
	}
	// Every kit's wheat faces the water nearest its site, so it regrows and no two homes are one stamp.
	const std::vector<unsigned char> pureWater = pureTiles(map, WATER);
	const double mapFacing = context.bounded("hidden-oasis-kit", 3600) / 3600.0 * 2 * kPi;
	for (int k = 0; k < teams; ++k)
	{
		const ShapePoint site{double(L.sites[k] % t.w), double(L.sites[k] / t.w)};
		const int sx = L.sites[k] % t.w, sy = L.sites[k] / t.w;
		int nearest = -1, nearestDistance = INT_MAX;
		for (int dy = -kKitWaterReach; dy <= kKitWaterReach; ++dy)
			for (int dx = -kKitWaterReach; dx <= kKitWaterReach; ++dx)
				if (pureWater[t.at(sx + dx, sy + dy)] && dx * dx + dy * dy < nearestDistance)
				{
					nearest = t.at(sx + dx, sy + dy);
					nearestDistance = dx * dx + dy * dy;
				}
		const double facing = nearest < 0 ? mapFacing
										  : std::atan2(double(t.offsetY(sy, nearest / t.w)), double(t.offsetX(sx, nearest % t.w)));
		// plantOpenHomeKit lays the wheat a quarter turn clockwise of its axis.
		plantOpenHomeKit(map, t, context, site, facing + kPi / 2, kHomeRadius, kKitWheat, kKitWood, -1,
						 [&](int i)
						 {
							 return L.territory[i] == k && !reserved[i] && !corridor[i] && !L.washes[i] &&
									clearGround(map, i % t.w, i / t.w);
						 });
	}
	for (int k = 0; k < teams; ++k)
	{
		const WheatTopUp topped =
			topUpWheatNearby(map, t, L.sites[k], kNearbyReach - kSiteRoom, kLeastWheatNearby + kWheatMargin, ambient,
							 [&](int i) { return L.territory[i] == k && !reserved[i] && !corridor[i]; });
		if (!topped.needed)
			continue;
		context.telemetry.fallback("hidden-oasis.kit.topped-up", "A crowded kit was topped up", k);
		context.telemetry.measure("hidden-oasis.kit.top-up-tiles", topped.tiles, k);
	}
	std::vector<unsigned char> kit(n, 0);
	for (int i = 0; i < n; ++i)
		kit[i] = !ambient[i] && map.isResource(i % t.w, i / t.w);

	// The basin: its garden, unscaled, and the pond's algae, the only algae on the map.
	context.stage = "hidden-oasis basin";
	{
		const auto [wheat, wood] = plantSealedGarden(map, t, context, L.garden, kGardenWheat, kGardenWood,
													 "hidden-oasis-garden-crops", "hidden-oasis-garden-split");
		context.telemetry.measure("hidden-oasis.garden.wheat", wheat);
		context.telemetry.measure("hidden-oasis.garden.wood", wood);
		std::vector<unsigned char> notPond(n, 0);
		for (int i = 0; i < n; ++i)
			notPond[i] = !L.pond[i];
		const std::vector<int> fromBeach = stepsFrom(t, notPond);
		// From the shore inwards, in clumps: a shuffle, then a coarse noise plus the distance from the
		// beach (review round 1: sorted by a period-3 noise alone it lay in ruled bars).
		const std::vector<int> order = fractalNoise(t.w, t.h, 6, 2, context.stream("hidden-oasis-algae"));
		std::vector<int> water;
		for (int i = 0; i < n; ++i)
			if (L.pond[i] && fromBeach[i] <= kAlgaeShallows && map.isResourceAllowed(i % t.w, i / t.w, ALGA))
				water.push_back(i);
		context.shuffle(water.begin(), water.end(), "hidden-oasis-algae");
		std::stable_sort(water.begin(), water.end(), [&](int a, int b)
						 { return order[a] - fromBeach[a] * 9000 > order[b] - fromBeach[b] * 9000; });
		if (int(water.size()) < o.algae)
		{
			context.detail = "The pond has no room for that much algae; use a bigger pond.";
			return false;
		}
		for (int k = 0; k < o.algae; ++k)
			map.setResource(water[k] % t.w, water[k] / t.w, ALGA, 1);
		context.telemetry.measure("hidden-oasis.algae.tiles", o.algae);
		// The forward base's room: the basin's free 3x3 footprints (an inn, a tower and a school fit one).
		int room = 0;
		for (int i = 0; i < n; ++i)
		{
			bool free = true;
			for (int dy = 0; dy < 3 && free; ++dy)
				for (int dx = 0; dx < 3 && free; ++dx)
				{
					const int j = t.at(i % t.w + dx, i / t.w + dy);
					free = L.basin[j] && !L.garden[j] && clearGround(map, j % t.w, j / t.w);
				}
			room += free;
		}
		context.telemetry.measure("hidden-oasis.basin.room-3x3", room);
	}

	// The towers, each on its ledge covering its pinch.
	context.stage = "hidden-oasis towers";
	if (o.towers > 0)
		for (int k = 0; k < teams; ++k)
		{
			const Post &post = L.posts[k];
			std::vector<unsigned char> allowed(n, 0);
			for (int dy = 0; dy <= 1; ++dy)
				for (int dx = 0; dx <= 1; ++dx)
					allowed[t.at(post.tower % t.w + dx, post.tower / t.w + dy)] = 1;
			std::vector<MapGeneratorPoint> cover;
			for (int i : post.cover)
				cover.push_back(MapGeneratorPoint(i % t.w, i / t.w));
			if (placeTower(game, k, 0, post.tower % t.w + 1, post.tower / t.w + 1, 2, allowed, true, cover,
						   true) < 0)
			{
				context.detail = "Colony " + std::to_string(k) + "'s ledge has no room for its tower.";
				return false;
			}
		}

	// Trails from every colony to the mouth and to its own ledge, routes between colonies, the crop
	// guarantee, and the trails again in case a top-up landed on one.
	context.stage = "hidden-oasis routes";
	const std::vector<int> lie = periodicNoise(t.w, t.h, kTrailNoisePeriod, context.stream("hidden-oasis-trails"));
	std::vector<unsigned char> protect(n, 0);
	for (int i = 0; i < n; ++i)
		protect[i] = stone[i] || kit[i] || L.garden[i];
	// The ledge is reached the back way: the gorge is shut to its trail.
	std::vector<unsigned char> shut = stone;
	for (int i = 0; i < n; ++i)
		shut[i] = shut[i] || (L.gorge[i] && L.massif[i]);
	const int mouthLimit = L.distance + L.band + kSiteSlack / 2;
	const int ledgeLimit = L.distance + L.band + int(L.gorgeLength) + kLedgeWalkBeyondGorge - kSiteSlack;
	// `again` is the pass after the crop guarantee: a colony whose walks still hold is left alone
	// (profiling: cutting every trail twice was an eighth of generation, and a top-up rarely lands on one).
	const auto trails = [&](bool again)
	{
		const std::vector<std::vector<int>> workers = unitTilesByTeam(map, teams);
		for (int k = 0; k < teams; ++k)
		{
			if (workers[k].empty())
				continue;
			std::vector<unsigned char> ledge(n, 0);
			for (int i : L.posts[k].ledge)
				ledge[i] = map.getBuilding(i % t.w, i / t.w) == NOGBID;
			if (again)
			{
				std::vector<unsigned char> open = walkableTiles(map);
				const Reach toMouth = reachFrom(t, workers[k], open, mouthLimit);
				bool mouth = false, door = false;
				for (int i : toMouth.tiles)
					mouth = mouth || L.mouth[i];
				for (int i = 0; i < n; ++i)
					open[i] = open[i] && !shut[i];
				const Reach toLedge = reachFrom(t, workers[k], open, ledgeLimit);
				for (int i : toLedge.tiles)
					door = door || ledge[i];
				if (mouth && door)
					continue;
				context.telemetry.fallback("hidden-oasis.trails.recut", "A trail was closed after the crop guarantee", k);
			}
			if (!openTrail(map, t, workers[k], L.mouth, protect, protect, &lie, 10, kRouteRadius) &&
				!openTrail(map, t, workers[k], L.mouth, stone, protect, &lie, 10, kRouteRadius))
			{
				context.detail = "Colony " + std::to_string(k) + " has no walk to the gorge.";
				return false;
			}
			if (!openTrail(map, t, workers[k], ledge, shut, protect, &lie, 10, kRouteRadius))
			{
				context.detail = "Colony " + std::to_string(k) + " has no walk to its ledge.";
				return false;
			}
			// A natural trail that wandered too far is cut again straight, so every colony's walk stays
			// within the band the design balanced.
			const auto reaches = [&]()
			{
				const Reach flood = reachFrom(t, workers[k], walkableTiles(map), mouthLimit);
				for (int i : flood.tiles)
					if (L.mouth[i])
						return true;
				return false;
			};
			if (!reaches())
			{
				context.telemetry.fallback("hidden-oasis.trails.straightened", "A trail wandered; cut straight", k);
				if (!openTrail(map, t, workers[k], L.mouth, protect, protect, nullptr, 0, kRouteRadius))
					openTrail(map, t, workers[k], L.mouth, stone, protect, nullptr, 0, kRouteRadius);
			}
		}
		return true;
	};
	if (!trails(false))
		return false;
	openColonyRoutes(map, context, t, kRouteCosts, kRouteRadius, &protect);
	secureStartingCrops(game, context, t);
	reopenCrampedStarts(game, context, amounts, 24, 32, 0, &stone);
	openColonyRoutes(map, context, t, kRouteCosts, kRouteRadius, &protect);
	if (!trails(true))
		return false;
	context.telemetry.measure("hidden-oasis.fields.slivers", removeCropSlivers(map, t, protect));
	if (context.telemetry.enabled())
	{
		const std::vector<std::vector<int>> workers = unitTilesByTeam(map, teams);
		const std::vector<unsigned char> open = walkableTiles(map);
		for (int k = 0; k < teams; ++k)
		{
			// No farther than any of these walks can be on a map that validates.
			const std::vector<int> walked =
				floodFrom(t, tileMask(t, workers[k]), open,
						  2 * (L.distance + L.band) + int(L.gorgeLength) + kLedgeWalkBeyondGorge).steps;
			int toMouth = INT_MAX, toLedge = INT_MAX;
			for (int i = 0; i < n; ++i)
				if (L.mouth[i] && walked[i] >= 0)
					toMouth = std::min(toMouth, walked[i]);
			for (int i : L.posts[k].ledge)
				if (walked[i] >= 0)
					toLedge = std::min(toLedge, walked[i]);
			int toPond = INT_MAX, toEnemyLedge = INT_MAX;
			for (int i = 0; i < n; ++i)
				if (L.basin[i] && !L.pond[i] && walked[i] >= 0)
					toPond = std::min(toPond, walked[i]);
			for (int other = 0; other < teams; ++other)
				if (other != k)
					for (int i : L.posts[other].ledge)
						if (walked[i] >= 0)
							toEnemyLedge = std::min(toEnemyLedge, walked[i]);
			context.telemetry.measure("hidden-oasis.mouth.walk", toMouth, k);
			context.telemetry.measure("hidden-oasis.ledge.walk", toLedge, k);
			context.telemetry.measure("hidden-oasis.basin.walk", toPond, k);
			context.telemetry.measure("hidden-oasis.enemy-ledge.walk", toEnemyLedge, k);
		}
	}
	return true;
}

// The lobby's check, from the request alone (Central Quarry, review round 8: running the whole design
// here tripled its cost). Refusals that depend on the seed surface when the map is generated.
std::string validateRequest(const GenerationRequest &request)
{
	if (request.nbTeams < 2)
		return "Hidden Oasis needs at least two colonies.";
	if (request.nbTeams > kGreatestColonies)
		return "Hidden Oasis holds at most eight colonies.";
	if (!sizesFor(request).fits)
		return "The massif does not fit this map; use a bigger map or fewer colonies.";
	return "";
}

std::string validateWorld(const Game &game, const GenerationContext &context)
{
	GenerationContext replay(context.request);
	const Layout L = design(context.request, replay);
	const Map &map = game.map;
	if (const std::string mismatch = designMismatch(L, map, "hidden oasis"); !mismatch.empty())
		return mismatch;
	const HiddenOasisOptions o(context.request);
	const Torus &t = L.t;
	const int n = t.size(), teams = context.request.nbTeams;
	const auto at = [&](int i) { return "(" + std::to_string(i % t.w) + ", " + std::to_string(i / t.w) + ")"; };

	// The only algae is the pond's, of the amount asked; the basin's crops are its garden's.
	int algae = 0, gardenWheat = 0, gardenWood = 0;
	for (int i = 0; i < n; ++i)
	{
		const int type = map.getResource(i % t.w, i / t.w).type;
		if (type == ALGA)
		{
			if (!L.pond[i])
				return "Algae at " + at(i) + " is off the oasis pond.";
			++algae;
		}
		if ((type == WHEAT || type == WOOD) && L.massif[i] && !L.garden[i])
			return "A crop at " + at(i) + " lies in the massif outside the basin's garden.";
		gardenWheat += L.garden[i] && type == WHEAT;
		gardenWood += L.garden[i] && type == WOOD;
		if (L.rock[i] && type != STONE)
			return "The massif's wall has a gap at " + at(i) + ".";
	}
	if (algae != o.algae)
		return "The pond has " + std::to_string(algae) + " algae tiles, not " + std::to_string(o.algae) + ".";
	if (gardenWheat == 0 || gardenWood == 0)
		return "The basin's garden has no wheat or no wood.";
	// The basin is a forward base: room for kLeastBasinRoom 3x3 footprints on its open grass.
	{
		int room = 0;
		for (int i = 0; i < n; ++i)
		{
			bool free = true;
			for (int dy = 0; dy < 3 && free; ++dy)
				for (int dx = 0; dx < 3 && free; ++dx)
				{
					const int j = t.at(i % t.w + dx, i / t.w + dy);
					free = L.basin[j] && !L.garden[j] && map.isGrass(j % t.w, j / t.w) && !map.isResource(j % t.w, j / t.w);
				}
			room += free;
		}
		if (room < kLeastBasinRoom)
			return "The basin has room for only " + std::to_string(room) + " buildings.";
	}

	const ColonyWalk walk = walkFromFirstColony(map, teams, "the country", "");
	if (!walk.error.empty())
		return walk.error;

	// With any one pinch shut, nobody walks into the basin (terrain and stone alone: crops come and go):
	// a walk from the basin never leaves the plateau. Flooded from the basin, so a sound seal costs the
	// basin and the gorge's inner end rather than the map.
	std::vector<unsigned char> ground(n, 0);
	std::vector<int> basinGround;
	for (int i = 0; i < n; ++i)
	{
		ground[i] = !map.isWater(i % t.w, i / t.w) && map.getResource(i % t.w, i / t.w).type != STONE;
		if (L.basin[i] && ground[i])
			basinGround.push_back(i);
	}
	for (int k = 0; k < teams; ++k)
	{
		std::vector<unsigned char> open = ground;
		for (int i : L.posts[k].cover)
			open[i] = 0;
		const Reach sealed = reachFrom(t, basinGround, open, INT_MAX);
		for (int i : sealed.tiles)
			if (!L.massif[i] && !L.gorge[i])
				return "The basin can be walked to round colony " + std::to_string(k) + "'s pinch, by " + at(i) + ".";
	}

	// Every colony's tower stands on its ledge with its pinch in range, reached without the gorge, and
	// out of a level-3 tower's range of every other colony's.
	if (o.towers > 0)
	{
		const int range = kLeastTowerRange;
		for (int k = 0; k < teams; ++k)
		{
			const Post &post = L.posts[k];
			const int tx = post.tower % t.w, ty = post.tower / t.w;
			const Uint16 gbid = map.getBuilding(tx, ty);
			if (gbid == NOGBID || Building::GIDtoTeam(gbid) != k || countBuildings(game, k, "defencetower") != 1)
				return "Colony " + std::to_string(k) + " has no tower on its ledge.";
			for (int c : post.cover)
				if (beyondFootprint(t, tx, ty, c % t.w, c / t.w) > range)
					return "Colony " + std::to_string(k) + "'s tower does not reach its pinch at " + at(c) + ".";
			// Nor does it reach another colony's ledge: an owner at work on its own tower is not shot at.
			for (int other = 0; other < teams; ++other)
				if (other != k)
					for (int i : L.posts[other].ledge)
						if (beyondFootprint(t, tx, ty, i % t.w, i / t.w) <= range)
							return "Colony " + std::to_string(k) + "'s tower reaches colony " + std::to_string(other) +
								   "'s ledge at " + at(i) + ".";
			for (int other = k + 1; other < teams; ++other)
				for (int dy = 0; dy <= 1; ++dy)
					for (int dx = 0; dx <= 1; ++dx)
						if (beyondFootprint(t, tx, ty, L.posts[other].tower % t.w + dx, L.posts[other].tower / t.w + dy) <=
							kGreatestTowerRange)
							return "Colonies " + std::to_string(k) + " and " + std::to_string(other) +
								   " start with towers in range of each other.";
			for (int i = 0; i < n; ++i)
				if (L.basin[i] && !L.pond[i] && beyondFootprint(t, tx, ty, i % t.w, i / t.w) <= range)
					return "Colony " + std::to_string(k) + "'s tower reaches the basin at " + at(i) + ".";
		}
	}

	// Nobody shoots a ledge from the country: no ground a tower could stand on outside the plateau lies
	// within kFrontReach of one (review round 3: a rival's tower outside the cliff reached the
	// shallowest ledge on two maps in eight).
	{
		std::vector<unsigned char> ledges(n, 0);
		for (const Post &post : L.posts)
			for (int i : post.ledge)
				ledges[i] = 1;
		const std::vector<unsigned char> nearLedge = dilate(t, ledges, kFrontReach);
		for (int i = 0; i < n; ++i)
			if (nearLedge[i] && !ledges[i] && !L.massif[i] && map.isGrass(i % t.w, i / t.w) &&
				map.getResource(i % t.w, i / t.w).type != STONE)
				return "Ground outside the plateau at " + at(i) + " is within a tower's reach of a ledge.";
	}

	// Every colony's walk to its own ledge, the back way, is about as long as every other's.
	{
		std::vector<unsigned char> backWay = walkableTiles(map);
		for (int i = 0; i < n; ++i)
			if (L.gorge[i] && L.massif[i])
				backWay[i] = 0;
		int shortest = INT_MAX, longest = 0;
		const int ledgeLimit = L.distance + L.band + int(L.gorgeLength) + kLedgeWalkBeyondGorge;
		for (int k = 0; k < teams; ++k)
		{
			// Flooded no farther than the promise allows: beyond it the walk fails either way.
			const Reach reach = reachFrom(t, walk.workers[k], backWay, ledgeLimit + 1);
			std::vector<int> ledge = L.posts[k].ledge;
			std::sort(ledge.begin(), ledge.end());
			int best = INT_MAX;
			for (size_t j = 0; j < reach.tiles.size(); ++j)
				if (std::binary_search(ledge.begin(), ledge.end(), reach.tiles[j]))
					best = std::min(best, reach.steps[j]);
			if (best == INT_MAX)
				return "Colony " + std::to_string(k) + " cannot walk to its ledge without the gorge, or not within " +
					   std::to_string(ledgeLimit) + " steps.";
			shortest = std::min(shortest, best);
			longest = std::max(longest, best);
		}
		if (longest > ledgeLimit)
			return "A colony walks " + std::to_string(longest) + " steps to its own ledge.";
		(void)shortest;
	}

	// Every colony's walk to the mouth, and the spread of those walks: flooded no farther than the band's
	// far edge and its slack, beyond which the spread fails either way.
	const std::vector<unsigned char> open = walkableTiles(map);
	std::vector<int> firstWorkers;
	{
		const int mouthLimit = L.distance + L.band + kWalkSlack + kSiteSlack;
		int shortest = INT_MAX, longest = 0;
		for (int k = 0; k < teams; ++k)
		{
			const Reach reach = reachFrom(t, walk.workers[k], open, mouthLimit);
			int best = INT_MAX;
			for (size_t j = 0; j < reach.tiles.size(); ++j)
				if (L.mouth[reach.tiles[j]])
					best = std::min(best, reach.steps[j]);
			if (best == INT_MAX)
				return "Colony " + std::to_string(k) + " cannot walk to the gorge within " + std::to_string(mouthLimit) +
					   " steps.";
			shortest = std::min(shortest, best);
			longest = std::max(longest, best);
			firstWorkers.push_back(walk.workers[k].front());
		}
		if (longest - shortest > 2 * L.band + kWalkSlack)
			return "The colonies' walks to the gorge differ by " + std::to_string(longest - shortest) + " steps.";
	}
	for (int k = 0; k < teams; ++k)
	{
		const int wx = firstWorkers[k] % t.w, wy = firstWorkers[k] / t.w;
		int wheat = 0;
		for (int dy = -kNearbyReach; dy <= kNearbyReach; ++dy)
			for (int dx = -kNearbyReach; dx <= kNearbyReach; ++dx)
				wheat += map.getResource(t.at(wx + dx, wy + dy) % t.w, t.at(wx + dx, wy + dy) / t.w).type == WHEAT;
		if (wheat < kLeastWheatNearby)
			return "Colony " + std::to_string(k) + " has only " + std::to_string(wheat) + " wheat tiles near its swarm.";
	}
	std::vector<unsigned char> land(n, 0);
	for (int i = 0; i < n; ++i)
		land[i] = ground[i];
	if (const int closest = closestWalk(t, firstWorkers, land);
		closest < (teams >= kCrowdedFrom ? kCrowdedLeastSpacing : kLeastSpacing) - kSiteSlack)
		return "Two colonies start " + std::to_string(closest) + " steps apart.";
	return "";
}
} // namespace

HiddenOasisOptions::HiddenOasisOptions(const GenerationRequest &r)
	: pondSize(r.option("pond-size")), algae(r.option("pond-algae")), towers(r.option("starting-towers")),
	  school(r.option("starting-school")), desert(r.option("desert")), buttes(r.option("buttes")), springs(r.option("springs")),
	  wheat(r.option("wheat-amount")), wood(r.option("wood-amount")), stone(r.option("stone-amount")),
	  fruit(r.option("fruit-amount"))
{
}

GeneratorDefinition hiddenOasisDefinition()
{
	// The pond's radius and its algae in tiles (the only algae on the map, so there is no algae amount);
	// the level of the tower every colony starts with over the gorge (0 for open pads only); the gorge's
	// width away from its pinches; how many buttes and springs the country has.
	return {"hidden-oasis",
			57,
			"Hidden Oasis",
			1,
			false,
			{{"pond-size", "Pond size", 5, 12, 1, 8, ControlGroup::Terrain},
			 {"pond-algae", "Pond algae", 8, 48, 4, 24, ControlGroup::Layout},
			 // Off leaves the ledges as open pads. The towers are level 1, the only level a colony
			 // without a school can resupply or repair (Building::canUnitWorkHere).
			 GeneratorControl::toggle("starting-towers", "Starting towers", true, ControlGroup::Layout),
			 // On by default (maintainer, 2026-09-17). Off is the total lock: no school anywhere until
			 // somebody reaches the pond.
			 GeneratorControl::toggle("starting-school", "Starting school", true, ControlGroup::Layout),
			 {"desert", "Desert", 0, 80, 10, 40, ControlGroup::Terrain},
			 {"buttes", "Mesas", 0, 10, 1, 5, ControlGroup::Terrain},
			 {"springs", "Springs", 0, 10, 1, 6, ControlGroup::Terrain},
			 GeneratorControl::percentage("wheat-amount", "Wheat amount"),
			 GeneratorControl::percentage("wood-amount", "Wood amount"),
			 GeneratorControl::percentage("stone-amount", "Stone amount"),
			 GeneratorControl::percentage("fruit-amount", "Fruit amount")},
			generate,
			true,
			validateRequest,
			validateWorld,
			// The sealed basin is the contested centre and the towers over the gorge make it a siege;
			// the massif and buttes are its stone walls. Colonies are found by a walk-band search, so
			// no fairness tag.
			{"terrain:natural", "feature:mountains", "feature:stone-walls", "feature:desert", "feature:oases",
			 "style:contested-center", "style:siege"}};
}
