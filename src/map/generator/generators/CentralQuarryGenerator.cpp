// SPDX-License-Identifier: GPL-3.0-or-later
#include "CentralQuarryGenerator.h"
#include "Biomes.h"
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
#include "Territories.h"
#include "WalkBandStarts.h"
#include "Walls.h"
#include <algorithm>
#include <climits>
#include <cmath>
#include <cstdint>
#include <functional>
#include <map>
#include <random>
#include <unordered_map>
#include <string>
#include <vector>
using namespace MapGeneration;

// Central Quarry: open country of woods, meadows, lakes and winding streams, all draining to one lake
// in the middle of the map. In the lake stands an island, and on the island is the only stone in the
// world: a small grey outcrop that every colony wants and only one can hold.
//
// WHY IT PLAYS (docs/map-generators/GAME_RULES_FOR_MAP_DESIGN.md). Stone never runs out and is never
// cleared, so a quarry of a handful of tiles is a permanent site whose output is limited only by how
// many workers can stand beside it. Everything a colony needs to start and grow is stone-free (level-0
// inns, hospitals, schools, pools, barracks and swarms, and the towers themselves), but a tower fires
// stone, and walls, racetracks, markets and every upgrade cost it. So the colonies start as equals and
// the game turns on the isle: whoever holds the quarry arms its towers and upgrades its buildings, and
// everyone else has to take it from them. The isle joins the shore only by sand bars, and every bar
// lands at one place on it, the landing, where the lake is narrowest: a holder's few towers there
// cover every way in, and towers on the shore opposite still reach it. A colony that learns to swim (a
// level-0 pool costs no stone) can cross anywhere. A small sealed garden of wheat and wood on the far
// shore of the isle lets a holder build and feed a garrison there (maintainer request, 2026-09-17).
//
// NATURE, NOT PLOTS (maintainer, 2026-09-17: "avoid too much symmetry ... don't do a home plot ...
// ideally this is kind of a natural landscape"). No home is stamped. The country is woodland and open
// farmland (Biomes.h kits without their stone outcrops), zoned by distance from the lake, with rough
// lakes and curving streams whose fords are sand; an open common rings the lake's shore. Colonies are
// spread by walking distance (farthestSites) over roomy, watered grass, as Continents spreads them, but
// only within a narrow band of walking distance from the isle, so who lives next to whom is irregular
// while the race to the stone is even. A start that landed dry is dug a pond off its road to the isle,
// and every colony's trail to the isle is cleared.
//
// THE PROMISES validateWorld KEEPS. No stone off the isle, and a quarry of exactly the size asked; no
// crop on the isle outside its garden; with the bars shut, nobody walks onto the isle; every colony
// walks to every other and to the isle, those walks within twice the band plus slack of each other;
// no colony a short swim from the isle; no two colonies closer than the spacing floor; at least
// kLeastWheatNearby wheat near every swarm.
//
// REVIEW HISTORY (2026-09-17, eight rounds with one independent reviewer on frozen builds; look and
// playability out of 10: 5/5, 6.5/5, 6/5, 6.5/6, 6.5/6.5, 7/6.5, 7.5/7, 8/7.5). The comments on the
// constants below say which finding each answers. In short: sizes and walks capped in tiles so 512
// plays like 256 (round 1); the kit protected from trails and starts picked on watered ground (2);
// field-edge trims kept away from homes after they starved one seed, and the shared Biomes dry reserve
// fixed to deal its fields patchiest first rather than from the top rows (3); site yield scored by a
// 48-step flood (4); the single landing, streams, zoning, shoal bars and the garden (5, 6); a bigger
// island, restored water, spline streams and a compact quarry (7); wider spacing, a shore-sealed oval
// garden, jittered fords and a cheap request check that cut generation time threefold (8). The AIs mine
// the quarry but never defend the landing, so whether a person can hold it is for a human playtest.
namespace
{
// The lake's radius is the control's, in tiles, whatever the map's size, so a 512 map plays like a 256
// one with more country behind the homes (review round 1: as a share of the map, on 512 the isle had
// 1,588 tiles and the colonies started 172 steps out, beyond a unit's walk on one meal). A map too
// small for it shrinks it to fit.
// The island is never smaller than this radius in tiles (room round the quarry for a few towers),
// and the lake is at least this much wider than the island all round before its own rough outline.
constexpr double kLeastIsland = 7;
constexpr double kSmallMapLakeShare = 0.22;
constexpr double kLeastMoat = 5;
// Outline roughness of the lake and the island (RadialShape).
constexpr double kLakeRoughness = 0.35, kIslandRoughness = 0.3;
// The lake is stretched along a random axis by up to this much more than its width, so it is a lake
// and not a ring; the island's middle sits up to this share of the spare room (along the long axis:
// lake radius times stretch, less island and moat) off the lake's middle.
constexpr int kLakeStretchPercent = 70;
constexpr double kIslandDrift = 0.75;
// The country's own lakes: one per this many tiles, each a rough, stretched outline between the
// least and greatest radius, kept this far from any other water.
// Review round 6: halving the lakes to one per 9000 tiles cost up to a third of the yield on three
// seeds; one per 6000 with the wider streams restores it.
constexpr int kTilesPerLake = 6000, kDenseLakeArea = 256 * 256;
constexpr int kLeastLakeRadius = 3, kGreatestLakeRadius = 12;
// No country lake is more than this share of the isle's lake's radius, nor nearer to it than
// kPrizeLakeGap tiles, so the isle's lake is always the one that stands out.
constexpr double kCountryLakeShare = 0.5;
constexpr int kPrizeLakeGap = 20;
constexpr double kCountryLakeRoughness = 0.5;
constexpr int kLakeGap = 10, kLakeAttempts = 30;
// A sand bar's half width in corners and how far it wanders sideways. The angle of every bar is
// evenly spaced plus up to this share of the spacing either way.
constexpr double kBarWander = 6;
// Review round 5: with bars at even angles every colony walked in and mined, and no holder could
// close them all. Every bar now lands at one place on the island, the landing, where the lake is
// narrowest (so towers on the shore there still reach it, and a holder's few towers cover every way
// in), and fans out to shore ends spread either side of it. A bar is kBarWideHalf corners wide at
// both shores and kBarNarrowHalf in its middle, like a shoal, with kIsletsPerBar sand islets along it.
constexpr double kBarWideHalf = 2.4, kBarNarrowHalf = 1.3;
constexpr double kBarFanDegrees[4][4] = {{0}, {-50, 50}, {-65, 0, 65}, {-80, -27, 27, 80}};
constexpr double kBarFanJitterDegrees = 8, kLandingSpreadTiles = 2.5;
constexpr int kIsletsPerBar = 2;
// A bar hugs the shore when more than kBarHugLimit of its points lie within kBarShoreReach tiles of
// the mainland's grass farther than kBarEndReach from its shore end; it is drawn again with its fan
// angle kBarFanShrink times closer to the landing's, up to kBarAttempts times.
constexpr int kBarHugLimit = 8, kBarShoreReach = 3, kBarAttempts = 5;
constexpr double kBarEndReach = 8, kBarFanShrink = 0.7;
constexpr double kLeastIsletRadius = 1.8, kIsletRadiusSpread = 1.2;
// Streams (review round 5: "leopard print, not a landscape"): kLeastStreams to kLeastStreams +
// kStreamSpread - 1 winding streams run from the country into the lake, so it reads as the basin the
// land drains to. Each is kStreamShare of the map's shorter side long. A sand ford crosses it every kFordSpacing
// tiles, the first kFordSpacing / 2 from its source, none within kFordLakeGap of the lake.
constexpr int kLeastStreams = 2, kStreamSpread = 2;
// A stream bends through kStreamBends waypoints pushed sideways by up to kStreamSway of its length,
// its source kStreamSkewDegrees at most off the lake's radial, and it widens from kStreamSourceHalf
// to kStreamMouthHalf (the first version ran straight out from the lake like spokes).
constexpr double kStreamShare = 0.3, kStreamSway = 0.22;
constexpr double kStreamSourceHalf = 1.6, kStreamMouthHalf = 2.5, kStreamSkewDegrees = 40;
// Streams follow a Catmull-Rom curve through their waypoints (straight legs between waypoints read as
// drawn canals, review round 6), sampled every kStreamStep tiles, their width varying by up to
// kStreamWidthNoise of itself over kStreamWidthCell samples, and keep kStreamGap tiles from another
// stream except near the lake, so two never join in a "Y".
constexpr double kStreamStep = 0.7, kStreamWidthNoise = 0.3;
constexpr int kStreamWidthCell = 12, kStreamGap = 12;
constexpr int kStreamBends = 3;
constexpr double kFordSpacing = 24, kFordHalf = 2.8, kFordLakeGap = 10;
// Each ford's spacing from the last is kFordSpacing times kFordSpacingLow to kFordSpacingHigh, and its
// half width varies by a fifth either way (review round 7: evenly spaced fords read as beads).
constexpr double kFordSpacingLow = 0.75, kFordSpacingHigh = 1.35;
// A stream keeps kStreamBarGapDegrees from any bar's shore end, so it never cuts a way in.
constexpr double kStreamBarGapDegrees = 25;
constexpr int kStreamBarGap = 5;
// Zoning: woodland grows towards the country farthest from the lake (kEdgeWoods of the noise's range
// at the farthest ground), never within kStreamMeadow of a stream, and nothing is planted within
// kCommons tiles of the lake's shore: an open common round the water, the battleground.
constexpr double kEdgeWoods = 0.6;
constexpr int kStreamMeadow = 6, kCommons = 10;
// On maps bigger than 256 the centrepiece grows: the lake by kLakeGrowth and the walk cap by
// kWalkGrowth of the map's scale beyond 1 (512: lake x1.5, walk x1.3).
constexpr double kLakeGrowth = 0.5, kWalkGrowth = 0.3;
// The island's garden (maintainer, 2026-09-17: "a bit of small isolated food or wood so players can
// build there in order to hold it"): a plot against the island's shore on the side away from the
// landing, kGardenShare of the island's radius (between kLeastGarden and kGreatestGarden tiles),
// with up to kGardenWheat wheat and kGardenWood wood (half and two fifths of its tiles on a small
// plot), sealed by a one-corner sand line so its crops never spread over the island's building
// ground. Against the shore, its keep-out overlaps the water rather than the quarry's room (in the
// island's middle it left 128x128 islands no room for the quarry).
constexpr double kGardenShare = 0.45, kLeastGarden = 3.5, kGreatestGarden = 5,
				 kGardenSealWidth = 1.2;
// Review round 7: a round plot with its own sand ring inside the island's beach read as a bullseye.
// The plot is now an oval kGardenStretch times as long along the shore as across, centred on the
// island's rim so the shore's own beach seals half of it, its seal's radius swaying by up to
// kGardenSealSway tiles on a noise of period kGardenSealPeriod, and its crops dealt over the plot by
// noise rather than grown as two blobs.
constexpr double kGardenStretch = 1.6, kGardenSealSway = 0.6;
constexpr int kGardenSealPeriod = 4;
constexpr int kGardenWheat = 10, kGardenWood = 8;
// The island is this much bigger for its garden (review round 6: the garden's plot and seal took the
// island from about 100 building sites to 35 to 53).
constexpr double kGardenIslandGrowth = 1.15;
// On maps no bigger than 128 country lakes keep to kSmallMapCountryLakeShare of the prize lake.
constexpr double kSmallMapCountryLakeShare = 0.35;
// The country keeps its ponds this far from the lake.
constexpr int kLakeKeepOut = 8;
// A site needs a square of pure grass this many tiles out on every side (Continents' kSiteRoom),
// and keeps this far from the lake.
constexpr int kSiteRoom = 4;
constexpr int kSiteLakeClearance = 10;
// The walking distance from the isle every colony starts at: a percentile of the roomy ground's
// distances, tried from the first until the colonies spread at least kSiteSpacing apart. The band
// either side is a tenth of the distance, at least kLeastBand.
// The walk is capped at kGreatestWalk steps, inside the working range of the AIs (review round 1:
// colonies 80 to 100 steps out barely mined the quarry, and Maxima counts stone only within 20 tiles
// of its own buildings). Two colonies closer than kLeastSpacing steps are refused, not shipped (it
// accepted 5 steps on 128x128 with 12 colonies).
constexpr int kDistancePercentiles[] = {40, 50, 60, 70, 30, 80};
constexpr int kGreatestWalk = 70;
// ... plus this many steps for every colony beyond six, up to kGreatestWalkCeiling, so a crowd of
// colonies gets a wider ring rather than a packed one (review round 2: twelve colonies on 512 maps
// started 23 to 35 steps apart round a 70-step ring, three quarters of the map empty).
constexpr int kWalkPerExtraColony = 6, kGreatestWalkCeiling = 110;
constexpr int kLeastBand = 5;
// Round 4 found two colonies 48 steps apart on seed 3 with the target at 36; the spread now keeps
// looking until 48.
// Review round 7: with the bigger island, streams and lakes, pairs started 46 to 53 steps apart; the
// spread keeps looking until 60, first at the capped walk, then at kWalkStretch times it.
constexpr int kSiteSpacing = 60, kLeastSpacing = 28;
constexpr double kWalkStretch = 1.08, kRingSpacingShare = 0.45;
// A spread with a preference that reaches this share of the wanted spacing is kept without trying
// looser preferences (each try floods from every site in six trials).
constexpr int kGoodEnoughSpacingPercent = 85;
// The band either side of the target walk is this fraction of it (review round 4: walks to the stone
// spread 24 steps with a tenth).
constexpr int kBandDivisor = 14;
// A colony across the water from the isle is a short swim from it: every site's straight-line steps to
// the isle are at least this share of the walk, so nobody starts on the shore facing the quarry.
constexpr int kLeastSwimPercent = 80;
// Picks prefer sites whose yield (crop growth summed over the ground within kYieldSteps) lies between
// these percentiles of the band's candidates (sampled): watered ground, and alike. Review round 2: a band of +-30% round the
// median put starts on the dry open ground most of the band is, where the kit was the only food.
// Review round 8: 60th to 90th left 2:1 yield splits on two seeds; 70th to 90th.
constexpr int kYieldSteps = 48, kYieldSamples = 300, kYieldLowPercentile = 70,
			  kYieldHighPercentile = 90;
// A site's reachable room, as Continents checks it.
constexpr int kCatchmentSteps = 24;
constexpr int kCatchmentFloor = 900;
// Dry starts get ponds (Continents' constants: see there for the evidence).
constexpr int kRoomRadius = 10;
constexpr std::uint32_t kFertilityFloor = 2500;
// One rough pond of kPondCorners, a second only if that was not enough; the ripple is scaled by
// kPondRipple tiles so it is a pond and not a disc (review round 2: two small round ponds side by
// side read as binoculars and barely watered the start).
constexpr int kPondsPerColony = 2;
constexpr int kPondCorners = 180;
constexpr int kPondNearest = 7, kPondFarthest = 13, kSecondPondStep = 3;
constexpr double kPondRipple = 3;
// Ground within kRouteKeepMargin tiles of any walk from a site to the isle at most kRouteKeepSlack
// steps longer than the shortest takes no pond.
constexpr int kRouteKeepSlack = 2, kRouteKeepMargin = 3;
// No ambient deposit within kFieldClearing of a site, no ambient wood within kHomeClearing.
constexpr int kFieldClearing = 5, kHomeClearing = 8;
// Every colony's starter kit, unscaled.
// The kit's wheat, with the ambient fields, gives every swarm kLeastWheatNearby wheat tiles within
// kNearbyReach tiles, which validateWorld checks (review round 2: a start with 4 wheat tiles near its
// swarm failed with every AI).
constexpr int kHomeRadius = 14, kKitWheat = 36, kKitWood = 18;
constexpr int kLeastWheatNearby = 30, kNearbyReach = 12, kWheatMargin = 12;
// Kits keep this many tiles off each colony's shortest way to the isle.
constexpr int kTrailCorridor = 2;
// A kit looks for the water it faces this many tiles round its site.
constexpr int kKitWaterReach = 20;
// The quarry grows as an irregular outcrop from a tile up to kQuarryDrift from the island's middle,
// each next tile a random neighbour of the rock so far, never within kQuarryInset of the shore (review
// round 1: the patch was the same "+" stamp on every seed).
// On a small island the inset shrinks until the outcrop fits with kQuarryRoomFactor times its size
// of ground to grow in; if even an inset of one does not, the request is refused (review round 2:
// quarry 16 on the smallest island always failed at generation).
constexpr int kQuarryDrift = 2, kQuarryInset = 3, kQuarryRoomFactor = 2;
// Crops regrow only within the engine's square growth probe of water, and the kits plant the most
// fertile ground first, so fields end in ruler-straight lines along the probe's square contours
// (maintainer's first look, 2026-09-17). Crops up to this many tiles in from any field's edge are
// taken off where a noise field of this period says so, and every edge wanders instead. Trimming
// only the fertile ground's own edge changed nothing: the straight line lies inside it. The fields
// still regrow towards their straight contours in play.
constexpr int kFrayDepth = 2, kFrayPeriod = 8;
// The fray alone left the long straight edges (review round 2), which are where the kits stop at
// the square probe's reach. So ambient crops are also kept within a round distance of water, from
// kLeastFieldReach to kLeastFieldReach + kFieldReachSpread tiles by the fray's noise, inside that
// square: fields round a pond are round.
constexpr int kLeastFieldReach = 9, kFieldReachSpread = 4;
// Neither the trim nor the fray touches ground within kHomeFields tiles of a site: review round 3
// found the trim had stripped the fields round some homes, which then food-capped with every AI.
constexpr int kHomeFields = 20;
// Nothing is planted within this many tiles of a sand bar, nor on the island.
// Round 5 (P2): wide enough to leave an inn plot at each bar's shore end.
constexpr int kBarClearance = 5;
// Routes between colonies: open ground 1, a clearable deposit 3; stone and water are never crossed,
// so no ford is ever laid across the lake. A lane three wide.
constexpr StepCosts kRouteCosts{1, 3, -1, -1, -1};
constexpr int kRouteRadius = 1;
// The colonies' walks to the isle may differ by at most this many steps beyond the band's width on
// both sides (the swarm stands a few tiles off its site, and deposits add detours the sketch lacks).
// Full-range study, 2026-09-17: at 8, 2 of the first 240 maps failed by one or two steps (a spread of
// 19 and 20 steps with a band of 5), the swarm standing a few tiles off its designed site.
constexpr int kWalkSlack = 12;
// Checks on the finished map allow for the swarm standing a few tiles off its design site.
constexpr int kSiteSlack = 8;
// A trail steps over open ground for 10 to 20 (a noise field of this period, so it bends with the
// land) and through a deposit for 30 more, so it follows the gaps in the woods instead of cutting a
// ruler-straight lane (review round 1).
constexpr int kTrailNoisePeriod = 16;

struct Layout
{
	Torus t{1, 1};
	TerrainSketch terrain;               // with beaches laid
	std::vector<unsigned char> island;   // tiles of the island's ground
	std::vector<unsigned char> bars;     // tiles touching a sand bar's corners
	std::vector<unsigned char> woodland; // country tiles furnished as woodland
	std::vector<unsigned char> open;     // country tiles furnished as farmland
	BiomeTerrain woodPonds, farmPonds;
	std::vector<unsigned char> noCover, noFields, commons, streams;
	std::vector<unsigned char> garden; // the island garden's tiles, inside its sand seal
	double gardenX = 0, gardenY = 0, gardenAngle = 0, gardenRadius = 0;
	std::vector<int> sites, territory;
	int quarryX = 0, quarryY = 0, quarryInset = kQuarryInset;
	std::vector<unsigned char> quarryGround; // where the outcrop may grow
	int distance = 0, band = 0;
	std::string failure;
};

BiomeKit stoneless(BiomeKit kit)
{
	kit.outcropsPer1000 = 0;
	kit.wallThickness = 0;
	kit.coverResource = WOOD;
	return kit;
}

BiomeKit woodlandKit()
{
	return stoneless(woodland());
}

BiomeKit farmlandKit()
{
	return stoneless(farmland());
}

// The constants above as the shared search's request (WalkBandStarts.h).
WalkBandRequest walkBand()
{
	WalkBandRequest r;
	r.percentiles.assign(std::begin(kDistancePercentiles), std::end(kDistancePercentiles));
	r.greatestWalk = kGreatestWalk;
	r.walkPerExtraColony = kWalkPerExtraColony;
	r.greatestWalkCeiling = kGreatestWalkCeiling;
	r.walkGrowth = kWalkGrowth;
	r.walkStretch = kWalkStretch;
	r.leastBand = kLeastBand;
	r.bandDivisor = kBandDivisor;
	r.siteSpacing = kSiteSpacing;
	r.leastSpacing = kLeastSpacing;
	r.goodEnoughSpacingPercent = kGoodEnoughSpacingPercent;
	r.ringSpacingShare = kRingSpacingShare;
	r.leastSwimPercent = kLeastSwimPercent;
	r.yieldSteps = kYieldSteps;
	r.yieldSamples = kYieldSamples;
	r.yieldLowPercentile = kYieldLowPercentile;
	r.yieldHighPercentile = kYieldHighPercentile;
	r.catchmentSteps = kCatchmentSteps;
	r.catchmentFloor = kCatchmentFloor;
	r.roomRadius = kRoomRadius;
	r.fertilityFloor = kFertilityFloor;
	return r;
}


// A rough outline stretched `stretch` times along `axis` (radians) round (cx, cy): whether corner
// (x, y) lies inside it.
bool insideOutline(const Torus &t, const RadialShape &shape, double cx, double cy, double stretch,
				   double axis, int x, int y)
{
	const double dx = t.offsetX(int(std::lround(cx)), x) - (cx - std::lround(cx));
	const double dy = t.offsetY(int(std::lround(cy)), y) - (cy - std::lround(cy));
	const double along = (dx * std::cos(axis) + dy * std::sin(axis)) / stretch;
	const double across = -dx * std::sin(axis) + dy * std::cos(axis);
	return std::hypot(along, across) < shape.radiusAt(std::atan2(across, along));
}

// The lake's and the island's radii before their shapes are drawn, from the request alone, and
// whether the lake fits the map: shared by the design and the cheap request check.
struct LakeSize
{
	double lake = 0, island = 0, mapScale = 1;
	bool fits = true;
};
LakeSize lakeSizeFor(const GenerationRequest &request)
{
	const CentralQuarryOptions o(request);
	const int w = 1 << request.wDec, h = 1 << request.hDec;
	LakeSize size;
	const double half = std::min(w, h) / 2.0;
	// On a small map the lake keeps to kSmallMapLakeShare of the half side (128x128: 14 tiles), or its
	// long axis filled the map and pushed every colony to the edges.
	const double fits = std::min(kSmallMapLakeShare * half,
								 0.9 * half / ((1 + kLakeRoughness) * (1 + kLakeStretchPercent / 100.0)));
	size.mapScale = std::max(1.0, std::min(w, h) / 256.0);
	const double asked = std::min(fits, o.lakeSize * (1 + kLakeGrowth * (size.mapScale - 1)));
	size.island = std::max(kLeastIsland, asked * o.islandSize / 100.0) * kGardenIslandGrowth;
	size.lake = std::max(asked, size.island + kLeastMoat + 3);
	size.fits = size.lake * (1 + kLakeRoughness) * (1 + kLakeStretchPercent / 100.0) <= 0.9 * half;
	return size;
}

Layout designAfresh(const GenerationRequest &request, GenerationContext &context)
{
	const CentralQuarryOptions o(request);
	Layout L;
	L.t = Torus(1 << request.wDec, 1 << request.hDec);
	const Torus &t = L.t;
	const int n = t.size(), teams = request.nbTeams;
	L.terrain.assign(n, GRASS);
	if (teams < 2)
	{
		L.failure = "Central Quarry needs at least two colonies.";
		return L;
	}

	// The lake and its island, in the middle of the map.
	const LakeSize size = lakeSizeFor(request);
	const double mapScale = size.mapScale, islandR = size.island, lakeR = size.lake;
	if (!size.fits)
	{
		L.failure = "The lake does not fit this map; use a smaller lake or a bigger map.";
		return L;
	}
	// The middle of the map: nothing on a torus is more central than anything else, but the preview
	// and the minimap show the isle where a player looks first.
	const double lx = t.w / 2, ly = t.h / 2;
	const RadialShape lakeShape(lakeR, kLakeRoughness, context, "central-quarry-lake");
	const RadialShape islandShape(islandR, kIslandRoughness, context, "central-quarry-island");
	const double lakeStretch =
		1 + context.bounded("central-quarry-lake", kLakeStretchPercent + 1) / 100.0;
	const double lakeAxis = context.bounded("central-quarry-lake", 1800) / 1800.0 * kPi;
	const double drift = kIslandDrift * std::max(0.0, lakeR * lakeStretch - islandR - kLeastMoat) *
						 (context.bounded("central-quarry-island", 1000) / 1000.0 * 2 - 1);
	const double ix = lx + drift * std::cos(lakeAxis), iy = ly + drift * std::sin(lakeAxis);
	std::vector<unsigned char> islandCorners(n, 0), lakeCorners(n, 0);
	for (int i = 0; i < n; ++i)
	{
		const int x = i % t.w, y = i / t.w;
		islandCorners[i] = insideOutline(t, islandShape, ix, iy, 1, 0, x, y);
		lakeCorners[i] = insideOutline(t, lakeShape, lx, ly, lakeStretch, lakeAxis, x, y);
	}
	// The moat is at least kLeastMoat corners wide wherever the rough outlines came close.
	const std::vector<unsigned char> moat = dilateRound(t, islandCorners, kLeastMoat);
	for (int i = 0; i < n; ++i)
		if ((lakeCorners[i] || moat[i]) && !islandCorners[i])
			L.terrain[i] = WATER;
	L.quarryX = int(std::lround(ix)) % t.w;
	L.quarryY = int(std::lround(iy)) % t.h;
	if (L.quarryX < 0)
		L.quarryX += t.w;
	if (L.quarryY < 0)
		L.quarryY += t.h;

	// Along a ray from the island's middle: where the island ends and where the water beyond it ends.
	const double limit = lakeR * lakeStretch * (1 + kLakeRoughness) + std::abs(drift) + kLeastMoat + 4;
	const auto ray = [&](double angle)
	{
		const double cx = std::cos(angle), cy = std::sin(angle);
		const auto at = [&](double r)
		{ return t.at(int(std::lround(ix + cx * r)), int(std::lround(iy + cy * r))); };
		double rim = 0;
		while (rim < limit && islandCorners[at(rim)])
			rim += 0.5;
		// The shore is the first grass that is not the island's own: on a diagonal the rounded ray can
		// step back onto an island corner (a half-tile "crossing" put a bar's shore end on the island).
		double shore = rim;
		while (shore < limit && (L.terrain[at(shore)] != GRASS || islandCorners[at(shore)]))
			shore += 0.5;
		return std::pair<double, double>{rim, shore};
	};
	// The landing: where the crossing is shortest, ties to the first of 72 directions from a random
	// start, so the landing is not always on the same side.
	const double firstDirection = context.bounded("central-quarry-bars", 72) * 2 * kPi / 72;
	double landing = firstDirection, shortest = 1e9;
	for (int d = 0; d < 72; ++d)
	{
		const double angle = firstDirection + d * 2 * kPi / 72;
		const auto [rim, shore] = ray(angle);
		if (shore - rim < shortest)
		{
			shortest = shore - rim;
			landing = angle;
		}
	}
	const int bars = std::clamp(o.sandBars, 1, 4);
	std::vector<unsigned char> barCorners(n, 0);
	std::vector<unsigned char> mainlandGrass(n, 0);
	for (int i = 0; i < n; ++i)
		mainlandGrass[i] = L.terrain[i] == GRASS && !islandCorners[i];
	const std::vector<unsigned char> nearShore = dilate(t, mainlandGrass, kBarShoreReach);
	std::vector<double> barAngles;
	for (int b = 0; b < bars; ++b)
	{
		// A bar that would hug the shore (on a stretched lake a wide fan angle ran along it, review round
		// 6) is drawn again with its fan angle closer to the landing's.
		std::vector<StrokePoint> path;
		double angle = landing;
		ShapePoint to{0, 0};
		for (int attempt = 0; attempt < kBarAttempts; ++attempt)
		{
		const double jitter = (context.bounded("central-quarry-bars", 1000) / 1000.0 * 2 - 1) *
							  kBarFanJitterDegrees * kPi / 180;
		angle = landing + (kBarFanDegrees[bars - 1][b] * kPi / 180 + (bars > 1 ? jitter : 0)) *
							  std::pow(kBarFanShrink, attempt);
		// Its island end: on the landing, a little either side along the rim.
		const auto [landRim, landShore] = ray(landing);
		(void)landShore;
		const double along = (context.bounded("central-quarry-bars", 1000) / 1000.0 * 2 - 1) *
							 kLandingSpreadTiles;
		const ShapePoint from{ix + std::cos(landing) * (landRim - 2) - std::sin(landing) * along,
							  iy + std::sin(landing) * (landRim - 2) + std::cos(landing) * along};
		const auto [rim, shore] = ray(angle);
		(void)rim;
		to = ShapePoint{ix + std::cos(angle) * (shore + 3), iy + std::sin(angle) * (shore + 3)};
		path = wanderingPath(t, from, to, kBarWideHalf, kBarWander, 0, context.stream("central-quarry-bars"));
		int hugging = 0;
		for (const StrokePoint &point : path)
			hugging += nearShore[t.at(int(std::lround(point.x)), int(std::lround(point.y)))] &&
					   std::hypot(point.x - to.x, point.y - to.y) > kBarEndReach;
		if (hugging <= kBarHugLimit)
			break;
		context.telemetry.fallback("central-quarry.bars.hugged-shore", "A bar ran along the shore; fan narrowed", b);
		}
		barAngles.push_back(angle);
		// A shoal: wide at both shores, narrow in the middle.
		for (size_t k = 0; k < path.size(); ++k)
		{
			const double f = path.size() > 1 ? double(k) / (path.size() - 1) : 0;
			path[k].halfWidth =
				kBarNarrowHalf + (kBarWideHalf - kBarNarrowHalf) * std::pow(std::abs(2 * f - 1), 1.5);
		}
		strokePath(barCorners, t, path);
		// Islets along the bar.
		for (int k = 0; k < kIsletsPerBar && path.size() > 4; ++k)
		{
			const size_t at =
				path.size() / 4 + context.bounded("central-quarry-bars", std::uint32_t(path.size() / 2));
			const double radius =
				kLeastIsletRadius +
				context.bounded("central-quarry-bars", 1000) / 1000.0 * kIsletRadiusSpread;
			strokePath(barCorners, t, {StrokePoint{path[at].x, path[at].y, radius}});
		}
	}
	for (int i = 0; i < n; ++i)
		if (barCorners[i] && L.terrain[i] == WATER)
			L.terrain[i] = SAND;
	context.telemetry.measure("central-quarry.bars.landing-crossing", shortest);

	// The garden, on the far side of the island from the landing, sealed by a line of sand corners.
	L.gardenAngle = landing + kPi;
	{
		const auto [rim, shore] = ray(L.gardenAngle);
		(void)shore;
		L.gardenRadius = std::clamp(islandR * kGardenShare, kLeastGarden, kGreatestGarden);
		const double reach = std::max(0.0, rim - 1);
		L.gardenX = ix + std::cos(L.gardenAngle) * reach;
		L.gardenY = iy + std::sin(L.gardenAngle) * reach;
	}
	L.garden = stampSealedOval(
		L.terrain, t, islandCorners,
		SealedOval{L.gardenX, L.gardenY, L.gardenAngle, L.gardenRadius, kGardenStretch, kGardenSealWidth,
				   kGardenSealSway},
		periodicNoise(t.w, t.h, kGardenSealPeriod, context.stream("central-quarry-garden")));

	// Streams from the country into the lake, keeping clear of the bars' shore ends, each crossed by
	// sand fords.
	L.streams.assign(n, 0);
	const std::vector<unsigned char> barKeepOut = dilate(t, barCorners, kStreamBarGap);
	// Other streams, except where they meet the lake: every stream ends there.
	std::vector<unsigned char> streamKeepOut(n, 0);
	// One stream on maps no bigger than 128 (review round 7: two dominated the map).
	const int streams = std::min(t.w, t.h) <= 128
							? 1
							: kLeastStreams + int(context.bounded("central-quarry-streams", kStreamSpread));
	const double side = std::min(t.w, t.h);
	int streamsDrawn = 0;
	for (int k = 0; k < streams; ++k)
	{
		// A stream may not come near a bar or its shore end: one that wandered across a bar's end cut
		// the bar off from the country (256x256, one bar, seed 8).
		double angle = 0;
		bool clear = false;
		std::vector<StrokePoint> path;
		for (int attempt = 0; attempt < 12 && !clear; ++attempt)
		{
			angle = context.bounded("central-quarry-streams", 3600) / 3600.0 * 2 * kPi;
			clear = true;
			for (double bar : barAngles)
				clear = clear && std::abs(std::remainder(angle - bar, 2 * kPi)) >=
									 kStreamBarGapDegrees * kPi / 180;
			if (!clear)
				continue;
			const auto [rim, shore] = ray(angle);
			const double length = kStreamShare * side;
			const ShapePoint mouth{ix + std::cos(angle) * (rim + shore) / 2,
								   iy + std::sin(angle) * (rim + shore) / 2};
			const double skew = (context.bounded("central-quarry-streams", 1000) / 1000.0 * 2 - 1) *
								kStreamSkewDegrees * kPi / 180;
			const ShapePoint source{ix + std::cos(angle + skew) * (shore + length),
									iy + std::sin(angle + skew) * (shore + length)};
			// Waypoints between source and mouth, each pushed sideways; a wandering path between each
			// pair, joined, and tapered from source to mouth.
			const double ux = mouth.x - source.x, uy = mouth.y - source.y;
			const double span = std::max(1.0, std::hypot(ux, uy));
			std::vector<ShapePoint> waypoints{source};
			for (int b = 1; b <= kStreamBends; ++b)
			{
				const double f = double(b) / (kStreamBends + 1);
				const double sway = (context.bounded("central-quarry-streams", 1000) / 1000.0 * 2 - 1) *
									kStreamSway * length;
				waypoints.push_back({source.x + ux * f - uy / span * sway,
									 source.y + uy * f + ux / span * sway});
			}
			waypoints.push_back(mouth);
			// A Catmull-Rom curve through the waypoints (splinePath, Drawing.h).
			path = splinePath(waypoints, kStreamStep);
			// Width: tapering from source to mouth, varying along the stream.
			std::vector<double> swell;
			for (size_t q = 0; q <= path.size() / kStreamWidthCell + 1; ++q)
				swell.push_back(context.bounded("central-quarry-streams", 1000) / 1000.0 * 2 - 1);
			for (size_t q = 0; q < path.size(); ++q)
			{
				const double f = path.size() > 1 ? double(q) / (path.size() - 1) : 0;
				const double cell = double(q) / kStreamWidthCell;
				const size_t c = size_t(cell);
				const double noise = swell[c] + (swell[c + 1] - swell[c]) * (cell - c);
				path[q].halfWidth = (kStreamSourceHalf + (kStreamMouthHalf - kStreamSourceHalf) * f) *
									(1 + kStreamWidthNoise * noise);
			}
			clear = !strokeIntersectsMask(t, path, barKeepOut) &&
					!strokeIntersectsMask(t, path, streamKeepOut);
		}
		if (!clear)
			continue;
		std::vector<unsigned char> line(n, 0);
		strokePath(line, t, path);
		for (int i = 0; i < n; ++i)
			if (line[i] && L.terrain[i] == GRASS && !islandCorners[i] && !barCorners[i])
			{
				L.terrain[i] = WATER;
				L.streams[i] = 1;
			}
		// Fords, measured along the stream from its source.
		double travelled = 0, next = kFordSpacing / 2;
		for (size_t p = 1; p < path.size(); ++p)
		{
			travelled += std::hypot(path[p].x - path[p - 1].x, path[p].y - path[p - 1].y);
			if (travelled < next)
				continue;
			next += kFordSpacing * (kFordSpacingLow +
									(kFordSpacingHigh - kFordSpacingLow) *
										context.bounded("central-quarry-fords", 1000) / 1000.0);
			if (std::hypot(path[p].x - lx, path[p].y - ly) < lakeR * lakeStretch + kFordLakeGap)
				continue;
			std::vector<unsigned char> ford(n, 0);
			strokePath(ford, t,
					   {StrokePoint{path[p].x, path[p].y,
									kFordHalf * (0.8 + 0.4 * context.bounded("central-quarry-fords", 1000) / 1000.0)}});
			for (int i = 0; i < n; ++i)
				if (ford[i] && L.streams[i])
					L.terrain[i] = SAND;
		}
		++streamsDrawn;
		streamKeepOut = dilate(t, L.streams, kStreamGap);
		for (int i = 0; i < n; ++i)
			if (std::hypot(t.offsetX(int(lx), i % t.w), t.offsetY(int(ly), i / t.w)) <
				lakeR * lakeStretch * (1 + kLakeRoughness) + kStreamGap)
				streamKeepOut[i] = 0;
	}
	context.telemetry.measure("central-quarry.streams", streamsDrawn);
	L.bars.assign(n, 0);
	for (int i = 0; i < n; ++i)
		if (barCorners[i])
			for (int dy = -1; dy <= 0; ++dy)
				for (int dx = -1; dx <= 0; ++dx)
					L.bars[t.at(i % t.w + dx, i / t.w + dy)] = 1;

	// The country round the lake: woodland where a noise field is highest, farmland elsewhere, each
	// with its ponds, kept off the lake.
	std::vector<unsigned char> lakeZone(n, 0);
	for (int i = 0; i < n; ++i)
		lakeZone[i] = L.terrain[i] != GRASS || islandCorners[i];
	lakeZone = dilate(t, lakeZone, kLakeKeepOut);
	// Furnished ground reaches the lake's shore; only new lakes keep lakeZone's distance from it.
	std::vector<unsigned char> country(n, 0);
	for (int i = 0; i < n; ++i)
		country[i] = L.terrain[i] == GRASS && !islandCorners[i] && !barCorners[i];
	std::vector<int> woods = fractalNoise(t.w, t.h, 64, 3, context.stream("central-quarry-woods"));
	{
		const std::vector<int> fromStreams = stepsFrom(t, L.streams);
		const double farthest = std::hypot(t.w / 2.0, t.h / 2.0);
		for (int i = 0; i < n; ++i)
		{
			const double away =
				std::hypot(t.offsetX(int(lx), i % t.w), t.offsetY(int(ly), i / t.w)) / farthest;
			woods[i] = int(woods[i] + kEdgeWoods * 65535 * away);
			if (fromStreams[i] >= 0 && fromStreams[i] < kStreamMeadow)
				woods[i] = 0;
		}
	}
	L.woodland = noisyShare(country, woods, std::clamp(o.woodland, 0, 100));
	L.open.assign(n, 0);
	for (int i = 0; i < n; ++i)
		L.open[i] = country[i] && !L.woodland[i];
	// The country's lakes: rough outlines of every size, dropped at random and kept apart from the
	// isle's lake and from one another.
	// Beyond a 256x256 map's area they come at half the density, so the isle's lake still stands out
	// on a 512 map (review round 2).
	const int wanted = std::max(1, std::min(n, kDenseLakeArea) / kTilesPerLake +
									   std::max(0, n - kDenseLakeArea) / (2 * kTilesPerLake));
	std::vector<unsigned char> prize(n, 0);
	for (int i = 0; i < n; ++i)
		prize[i] = L.terrain[i] != GRASS || islandCorners[i];
	std::vector<unsigned char> taken = dilate(t, prize, kPrizeLakeGap);
	{
		const std::vector<unsigned char> nearStreams = dilate(t, L.streams, kLakeGap);
		for (int i = 0; i < n; ++i)
			taken[i] = taken[i] || nearStreams[i];
	}
	const double countryShare =
		std::min(t.w, t.h) <= 128 ? kSmallMapCountryLakeShare : kCountryLakeShare;
	const int greatestCountryLake =
		std::max(kLeastLakeRadius, std::min(kGreatestLakeRadius, int(lakeR * countryShare)));
	int placed = 0;
	for (int lake = 0; lake < wanted; ++lake)
		for (int attempt = 0; attempt < kLakeAttempts; ++attempt)
		{
			const int cx = int(context.bounded("central-quarry-lakes", std::uint32_t(t.w)));
			const int cy = int(context.bounded("central-quarry-lakes", std::uint32_t(t.h)));
			// Small lakes are common and big ones rare: the radius is the lower of two draws.
			const int span = greatestCountryLake - kLeastLakeRadius + 1;
			const double r =
				kLeastLakeRadius + std::min(context.bounded("central-quarry-lakes", span),
											context.bounded("central-quarry-lakes", span));
			const double stretch = 1 + context.bounded("central-quarry-lakes", 81) / 100.0;
			const double axis = context.bounded("central-quarry-lakes", 1800) / 1800.0 * kPi;
			const RadialShape shape(r, kCountryLakeRoughness, context, "central-quarry-lakes");
			std::vector<int> corners;
			bool clear = true;
			const int reach = int(std::ceil(r * stretch * (1 + kCountryLakeRoughness))) + 1;
			for (int dy = -reach; dy <= reach && clear; ++dy)
				for (int dx = -reach; dx <= reach && clear; ++dx)
					if (insideOutline(t, shape, cx, cy, stretch, axis, cx + dx, cy + dy))
					{
						const int i = t.at(cx + dx, cy + dy);
						clear = !taken[i];
						corners.push_back(i);
					}
			if (!clear || corners.empty())
				continue;
			// The lake and the square of kLakeGap round each of its corners are taken: a local stamp,
			// not a dilation of the whole map for every lake.
			for (int i : corners)
			{
				L.terrain[i] = WATER;
				for (int dy = -kLakeGap; dy <= kLakeGap; ++dy)
					for (int dx = -kLakeGap; dx <= kLakeGap; ++dx)
						taken[t.at(i % t.w + dx, i / t.w + dy)] = 1;
			}
			++placed;
			break;
		}
	context.telemetry.measure("central-quarry.lakes.wanted", wanted);
	context.telemetry.measure("central-quarry.lakes.placed", placed);
	const std::vector<unsigned char> none(n, 0);
	L.woodPonds = BiomeTerrain{none, none, none};
	L.farmPonds = BiomeTerrain{none, none, none};

	// The ground as the game will draw it, and every tile's walk to the isle.
	TerrainSketch beached = L.terrain;
	layBeaches(beached, t);
	const std::vector<unsigned char> pureWater = pureTiles(beached, t, WATER);
	const std::vector<unsigned char> pureGrass = pureTiles(beached, t, GRASS);
	std::vector<unsigned char> walkable(n, 0);
	for (int i = 0; i < n; ++i)
		walkable[i] = !pureWater[i];
	TerrainSketch islandSketch(n, WATER);
	for (int i = 0; i < n; ++i)
		if (islandCorners[i])
			islandSketch[i] = GRASS;
	L.island = pureTiles(islandSketch, t, GRASS);
	const std::vector<unsigned char> mainland = largestRegion(t, walkable, GridNeighbors::Eight);
	int islandTiles = 0;
	for (int i = 0; i < n; ++i)
		if (L.island[i])
		{
			++islandTiles;
			if (!mainland[i])
			{
				L.failure = "A sand bar did not reach the island.";
				return L;
			}
		}
	// The quarry's ground: the largest connected piece of the island at least quarryInset from its
	// shore and clear of the garden, which must hold the outcrop kQuarryRoomFactor times over. The
	// island's inner ground can come apart in pieces (128x128, seed 8: the outcrop stopped at 6 tiles in
	// a small piece while the design counted room across all of them).
	const std::vector<unsigned char> nearGarden = dilate(t, L.garden, 2);
	for (L.quarryInset = kQuarryInset; L.quarryInset >= 1; --L.quarryInset)
	{
		std::vector<unsigned char> inland = erode(t, L.island, L.quarryInset);
		for (int i = 0; i < n; ++i)
			inland[i] = inland[i] && !nearGarden[i];
		L.quarryGround = largestRegion(t, inland, GridNeighbors::Eight);
		if (std::count(L.quarryGround.begin(), L.quarryGround.end(), 1) >= kQuarryRoomFactor * o.quarrySize)
			break;
	}
	if (L.quarryInset < 1)
	{
		L.failure = "The island has no room for its quarry; use a bigger island.";
		return L;
	}
	// The outcrop starts from the tile of its ground nearest the island's middle.
	{
		const int cx = int(std::lround(ix)), cy = int(std::lround(iy));
		int nearest = -1, best = INT_MAX;
		for (int i = 0; i < n; ++i)
			if (L.quarryGround[i] && t.dist2(cx, cy, i % t.w, i / t.w) < best)
			{
				best = t.dist2(cx, cy, i % t.w, i / t.w);
				nearest = i;
			}
		L.quarryX = nearest % t.w;
		L.quarryY = nearest / t.w;
	}
	const std::vector<int> toIsle = stepsFrom(t, L.island, walkable);
	const std::vector<int> swimToIsle = stepsFrom(t, L.island);
	const std::vector<int> fromWater = stepsFrom(t, pureWater);

	// The sites: roomy grass on the mainland, clear of the lake, all at about the same walk from the
	// isle, spread by walking distance within that band.
	std::vector<unsigned char> roomy = erode(t, pureGrass, kSiteRoom);
	std::vector<int> distances;
	// The lake clearance is by steps from the lake's own water, not any pond's.
	std::vector<unsigned char> lakeWater(n, 0);
	for (int i = 0; i < n; ++i)
		lakeWater[i] = pureWater[i] && (lakeCorners[i] || moat[i]);
	const std::vector<int> fromLake = stepsFrom(t, lakeWater);
	L.commons.assign(n, 0);
	for (int i = 0; i < n; ++i)
		L.commons[i] = fromLake[i] >= 0 && fromLake[i] <= kCommons;
	for (int i = 0; i < n; ++i)
	{
		roomy[i] = roomy[i] && mainland[i] && !L.island[i] && toIsle[i] > 0 &&
				   fromLake[i] >= kSiteLakeClearance;
		if (roomy[i])
			distances.push_back(toIsle[i]);
	}
	if (int(distances.size()) < teams)
	{
		L.failure = "This map has no room for that many colonies; use a bigger map or fewer colonies.";
		return L;
	}
	// A pick prefers a site whose crops already regrow and whose reachable room is enough, so a
	// pond is dug only where the band holds no watered ground (spreadInWalkBand, WalkBandStarts.h).
	const Fertility::Field startField = cropGrowthField(beached, t);
	const WalkBandStarts starts =
		spreadInWalkBand(t, roomy, walkable, toIsle, swimToIsle, startField, teams, mapScale, lakeR,
						 context, "central-quarry-sites", "central-quarry.sites.candidates", walkBand());
	L.sites = starts.sites;
	L.distance = starts.distance;
	L.band = starts.band;
	const int bestSpacing = starts.spacing;
	const char *preferenceUsed = starts.preference;
	if (L.sites.empty() || bestSpacing < kLeastSpacing)
	{
		L.failure = "This map has no room for that many colonies; use a bigger map or fewer colonies.";
		return L;
	}
	if (bestSpacing < kSiteSpacing)
		context.telemetry.fallback("central-quarry.sites.crowded",
								   "No spread reaches the intended spacing; the widest is kept");
	dealStarts(context, L.sites, "central-quarry-deal");

	// Each colony's ground, grown from its square with wandering borders: where its ponds and kit go.
	std::vector<unsigned char> land(n, 0);
	for (int i = 0; i < n; ++i)
		land[i] = mainland[i] && !L.island[i];
	// Each tile belongs to the colony whose square it walks to first (firstWalkTerritories): the
	// territories only decide where a colony's ponds, kit and swarm may go, and growing balanced, noisy
	// territories cost more than the whole trail stage.
	L.territory = firstWalkTerritories(t, land, L.sites, kSiteRoom);

	// A pond for every start that landed dry (waterDrySite, as Continents waters its colonies).
	const std::vector<int> ripple = periodicNoise(t.w, t.h, 5, context.stream("central-quarry-ponds"));
	std::vector<int> queued(n, 0);
	const DryStartPonds plan{kPondsPerColony, kPondCorners, kPondNearest, kPondFarthest + 1,
							 kSecondPondStep};
	for (int k = 0; k < teams; ++k)
	{
		// A pond keeps off every shortest walk from the site to the isle, with a margin, so digging it
		// never lengthens the walk the sites were balanced on (review round 2: a 512 map's walks
		// spread from 93 to 126 steps after its ponds were dug).
		// A tile on such a walk is at most the site's walk plus the slack from the site, so the flood
		// stops there, and the margin is stamped round the walk's tiles alone.
		const int longest = toIsle[L.sites[k]] + kRouteKeepSlack;
		const Flood fromSite = floodFrom(t, tileMask(t, {L.sites[k]}), walkable, longest);
		std::vector<unsigned char> keepDry(n, 0);
		for (int i : fromSite.visited)
			if (toIsle[i] >= 0 && fromSite.steps[i] + toIsle[i] <= longest)
				for (int dy = -kRouteKeepMargin; dy <= kRouteKeepMargin; ++dy)
					for (int dx = -kRouteKeepMargin; dx <= kRouteKeepMargin; ++dx)
						keepDry[t.at(i % t.w + dx, i / t.w + dy)] = 1;
		const DryStartWatering watered = waterDrySite(
			L.terrain, t, L.sites[k], kRoomRadius, kFertilityFloor, plan,
			[&](int i)
			{
				return L.territory[i] == k && L.terrain[i] == GRASS && !lakeZone[i] && !L.bars[i] &&
					   !keepDry[i];
			},
			[&](int i) { return kPondRipple * ripple[i] / 65536.0; }, queued, k * kPondsPerColony + 1);
		const int dug = int(std::count_if(watered.dug.begin(), watered.dug.end(),
										  [](int corners) { return corners > 0; }));
		if (!watered.dug.empty() && watered.dug.back() == 0)
			context.telemetry.fallback("central-quarry.ponds.none",
									   "No room for a pond beside a dry site", k);
		context.telemetry.measure("central-quarry.ponds.fertility-before", watered.before, k);
		context.telemetry.measure("central-quarry.ponds.dug", dug, k);
		context.telemetry.measure("central-quarry.ponds.fertility-after", watered.after, k);
	}

	std::vector<unsigned char> homes(n, 0);
	for (int site : L.sites)
		homes[site] = 1;
	L.noCover = dilate(t, homes, kHomeClearing);
	L.noFields = dilate(t, homes, kFieldClearing);
	layBeaches(L.terrain, t);

	context.telemetry.measure("central-quarry.lake.radius", lakeR);
	context.telemetry.measure("central-quarry.island.radius", islandR);
	context.telemetry.measure("central-quarry.island.tiles", islandTiles);
	context.telemetry.measure("central-quarry.island.drift", drift);
	context.telemetry.measure("central-quarry.bars", bars);
	context.telemetry.measure("central-quarry.sites.distance", L.distance);
	context.telemetry.measure("central-quarry.sites.band", L.band);
	context.telemetry.measure("central-quarry.sites.spacing", bestSpacing);
	context.telemetry.choice("central-quarry.sites.preference", preferenceUsed);
	return L;
}

// A generation asks for the design twice (generate and validateWorld), and building it was most of a
// generation's time (78% on 512x512 with twelve colonies): cachedDesign (DesignCache.h) keeps the last.
Layout design(const GenerationRequest &request, GenerationContext &context)
{
	return cachedDesign<Layout>(request, context, designAfresh);
}

bool generate(Game &game, GenerationContext &context)
{
	context.stage = "central-quarry layout";
	const CentralQuarryOptions o(context.request);
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

	context.stage = "central-quarry terrain";
	writeUndermap(map, L.terrain);

	context.stage = "central-quarry colonies";
	const auto homeMask = [&](int team)
	{
		std::vector<unsigned char> ground(size_t(n), 0);
		for (int i = 0; i < n; ++i)
			ground[i] = L.territory[i] == team && map.isGrass(i % t.w, i / t.w);
		return ground;
	};
	const auto anchor = [&](int team)
	{ return MapGeneratorPoint(L.sites[team] % t.w - 2, L.sites[team] / t.w - 2); };
	if (!settleColonies(game, context, "central-quarry-starts", homeMask, anchor))
		return false;

	// The country: woodland and farmland kits, without stone, off the isle, the bars and the swarms'
	// clearings.
	context.stage = "central-quarry resources";
	const ResourceAmounts amounts{o.wheat, o.wood, 100, o.algae, o.fruit};
	const std::vector<unsigned char> reserved = swarmSurroundings(t, context);
	const std::vector<unsigned char> nearBars = dilate(t, L.bars, kBarClearance);
	const std::vector<unsigned char> nearIsland = dilate(t, L.island, 1);
	std::vector<unsigned char> ambientClear(n, 0);
	for (int i = 0; i < n; ++i)
		ambientClear[i] = reserved[i] || L.noFields[i] || nearBars[i] || nearIsland[i] || L.commons[i];
	// Woodland's cover only where no crop regrows: on watered ground it grew from 24% of the map to
	// 31% in 50,000 ticks and took a fifth of the building sites (review round 1). Watered woodland
	// still carries the kit's fields and woodlots.
	const Fertility::Field watered = Fertility::forMap(map, false);
	std::vector<unsigned char> noCover = L.noCover;
	for (int i = 0; i < n; ++i)
		noCover[i] = noCover[i] || watered.at(i % t.w, i / t.w) > 0;
	furnishBiome(map, t, context, L.woodland, L.woodPonds, scaledBiome(woodlandKit(), amounts),
				 ambientClear, "central-quarry-woodland", &noCover);
	furnishBiome(map, t, context, L.open, L.farmPonds, scaledBiome(farmlandKit(), amounts),
				 ambientClear, "central-quarry-farmland", &noCover);
	std::vector<unsigned char> homeSites(n, 0);
	for (int site : L.sites)
		homeSites[site] = 1;
	const std::vector<unsigned char> homeFields = dilate(t, homeSites, kHomeFields);
	const std::vector<int> reachNoise =
		periodicNoise(t.w, t.h, kFrayPeriod * 2, context.stream("central-quarry-field-reach"));
	context.telemetry.measure("central-quarry.fields.trimmed",
							  trimFieldsBeyondWater(map, t, homeFields, watered, kLeastFieldReach,
													kFieldReachSpread, reachNoise));
	const std::vector<int> fray =
		periodicNoise(t.w, t.h, kFrayPeriod, context.stream("central-quarry-fray"));
	context.telemetry.measure("central-quarry.fields.frayed",
							  frayFieldEdges(map, t, homeFields, kFrayDepth, fray));
	seedAlgae(map, context, t, "central-quarry-algae", o.algae, AlgaeBand::shallows(1, 8).thriving(0.5));
	std::vector<unsigned char> kit(n, 0);
	for (int i = 0; i < n; ++i)
		kit[i] = map.isResource(i % t.w, i / t.w);
	// Every colony's shortest way to the isle over land, deposits and all, widened by kTrailCorridor: no
	// kit is planted on it, so the trail cut later never has to go round a kit it may not clear (a
	// 512x256 roll's colony walked 86 steps to the isle over terrain 73 steps long).
	std::vector<unsigned char> corridor(n, 0);
	{
		const std::vector<std::vector<int>> workers = unitTilesByTeam(map, teams);
		for (int k = 0; k < teams; ++k)
		{
			if (workers[k].empty())
				continue;
			const std::vector<int> route = cheapestWalk(
				t, GridNeighbors::Eight, workers[k], L.island,
				[&](int, int to, int, int)
				{
					const int x = to % t.w, y = to / t.w;
					if (map.isWater(x, y) || map.getBuilding(x, y) != NOGBID)
						return -1;
					return map.isResource(x, y) ? 11 : 10;
				});
			for (int i : route)
				for (int dy = -kTrailCorridor; dy <= kTrailCorridor; ++dy)
					for (int dx = -kTrailCorridor; dx <= kTrailCorridor; ++dx)
						corridor[t.at(i % t.w + dx, i / t.w + dy)] = 1;
		}
	}
	// Every kit's wheat faces the water nearest its site, so it regrows and no two homes are the same
	// stamp; a site with no water in reach takes the map's own facing (review round 3: every kit was
	// the same wheat triangle north of its swarm).
	const std::vector<unsigned char> pureWater = pureTiles(map, WATER);
	const double mapFacing = context.bounded("central-quarry-kit", 3600) / 3600.0 * 2 * kPi;
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
		const double facing =
			nearest < 0 ? mapFacing
						: std::atan2(t.offsetY(sy, nearest / t.w), t.offsetX(sx, nearest % t.w));
		// plantOpenHomeKit lays the wheat a quarter turn clockwise of its axis.
		plantOpenHomeKit(map, t, context, site, facing + kPi / 2, kHomeRadius, kKitWheat, kKitWood, -1,
						 [&](int i)
						 {
							 return L.territory[i] == k && !reserved[i] && !nearBars[i] && !corridor[i] &&
									clearGround(map, i % t.w, i / t.w);
						 });
	}

	// A kit crowded by the country's fields and woods can come up short: it is topped up on clear
	// ground near the site until kLeastWheatNearby + kWheatMargin wheat stands within kNearbyReach
	// (topUpWheatNearby, Homes.h).
	for (int k = 0; k < teams; ++k)
	{
		const WheatTopUp topped = topUpWheatNearby(
			map, t, L.sites[k], kNearbyReach - kSiteRoom, kLeastWheatNearby + kWheatMargin, kit,
			[&](int i) { return L.territory[i] == k && !reserved[i] && !corridor[i]; });
		if (!topped.needed)
			continue;
		context.telemetry.fallback("central-quarry.kit.topped-up", "A crowded kit was topped up", k);
		if (topped.clearedAmbient)
			context.telemetry.fallback("central-quarry.kit.cleared-for-top-up",
									   "A crowded kit cleared the country's wood to be topped up", k);
		context.telemetry.measure("central-quarry.kit.top-up-tiles", topped.tiles, k);
	}
	for (int i = 0; i < n; ++i)
		kit[i] = !kit[i] && map.isResource(i % t.w, i / t.w);

	// The island's garden, unscaled: a holder's foothold whatever the amounts say.
	context.stage = "central-quarry garden";
	{
		const auto [wheat, wood] =
			plantSealedGarden(map, t, context, L.garden, kGardenWheat, kGardenWood,
							  "central-quarry-garden-crops", "central-quarry-garden-split");
		context.telemetry.measure("central-quarry.garden.wheat", wheat);
		context.telemetry.measure("central-quarry.garden.wood", wood);
	}

	// The quarry: the only stone, on the island's middle.
	context.stage = "central-quarry quarry";
	const auto rock = [&](int i) { return L.quarryGround[i] && clearGround(map, i % t.w, i / t.w); };
	const int span = 2 * kQuarryDrift + 1;
	int first = seedNear(
		t,
		L.quarryX - kQuarryDrift + int(context.bounded("central-quarry-quarry", std::uint32_t(span))),
		L.quarryY - kQuarryDrift + int(context.bounded("central-quarry-quarry", std::uint32_t(span))),
		kQuarryDrift * 2, rock);
	std::vector<unsigned char> outcrop(n, 0);
	int quarryTilesPlaced = 0;
	if (first >= 0)
	{
		outcrop[first] = 1;
		quarryTilesPlaced = 1;
	}
	while (first >= 0 && quarryTilesPlaced < o.quarrySize)
	{
		// The frontier tiles touching the most rock so far, so the outcrop grows as a compact knot
		// rather than a twig (review round 6), still rough from the random pick among them.
		std::vector<int> frontier;
		int mostTouching = 0;
		for (int i = 0; i < n; ++i)
		{
			if (outcrop[i] || !rock(i))
				continue;
			int touching = 0;
			for (int dy = -1; dy <= 1; ++dy)
				for (int dx = -1; dx <= 1; ++dx)
					touching += outcrop[t.at(i % t.w + dx, i / t.w + dy)] != 0;
			if (touching == 0 || touching < mostTouching)
				continue;
			if (touching > mostTouching)
			{
				frontier.clear();
				mostTouching = touching;
			}
			frontier.push_back(i);
		}
		if (frontier.empty())
			break;
		outcrop[frontier[context.bounded("central-quarry-quarry",
										 std::uint32_t(frontier.size()))]] = 1;
		++quarryTilesPlaced;
	}
	for (int i = 0; i < n; ++i)
		if (outcrop[i])
			map.setResource(i % t.w, i / t.w, STONE, 1);
	context.telemetry.measure("central-quarry.quarry.tiles", quarryTilesPlaced);
	if (quarryTilesPlaced != o.quarrySize)
	{
		context.detail = "The island has no room for its quarry; use a bigger island.";
		return false;
	}

	// The routes between colonies and from every colony to the isle, then the crop guarantee, then
	// the routes again in case a top-up landed on a lane.
	context.stage = "central-quarry routes";
	const std::vector<int> lie =
		periodicNoise(t.w, t.h, kTrailNoisePeriod, context.stream("central-quarry-trails"));
	std::vector<unsigned char> quarryTiles(n, 0), protect(n, 0);
	for (int i = 0; i < n; ++i)
	{
		quarryTiles[i] = map.getResource(i % t.w, i / t.w).type == STONE;
		protect[i] = quarryTiles[i] || kit[i] || L.garden[i];
	}
	// Whether a flood from `from` over `open` reaches the island within `limit` steps.
	const auto reachesWithin = [&](const std::vector<unsigned char> &from,
								   const std::vector<unsigned char> &open, int limit)
	{
		const Flood flood = floodFrom(t, from, open, limit);
		for (int i : flood.visited)
			if (L.island[i])
				return true;
		return false;
	};
	const auto trails = [&]()
	{
		const std::vector<std::vector<int>> workers = unitTilesByTeam(map, teams);
		for (int k = 0; k < teams; ++k)
			if (!workers[k].empty() && !openTrail(map, t, workers[k], L.island, protect, protect, &lie, 10, kRouteRadius) &&
				!openTrail(map, t, workers[k], L.island, quarryTiles, protect, &lie, 10, kRouteRadius))
			{
				context.detail = "Colony " + std::to_string(k) + " has no walk to the isle.";
				return false;
			}
		// A natural trail that wandered too far round the woods is cut again straight, so every
		// colony's walk stays within the band the design balanced (512x512 with eight colonies: walks
		// differed by up to 37 steps with natural trails alone).
		const std::vector<unsigned char> open = walkableTiles(map);
		for (int k = 0; k < teams; ++k)
		{
			if (workers[k].empty())
				continue;
			// Only whether the walk is within the limit matters, so the flood stops at the limit.
			const int limit = L.distance + L.band + kSiteSlack / 2;
			if (!reachesWithin(tileMask(t, workers[k]), open, limit))
			{
				// First a trail with half the bend, then, if that still wanders, a straight one.
				context.telemetry.fallback("central-quarry.trails.recut",
										   "A natural trail wandered; cut again with less bend", k);
				if (!openTrail(map, t, workers[k], L.island, protect, protect, &lie, 5, kRouteRadius))
					openTrail(map, t, workers[k], L.island, quarryTiles, protect, &lie, 5, kRouteRadius);
				if (reachesWithin(tileMask(t, workers[k]), walkableTiles(map), limit))
					continue;
				context.telemetry.fallback("central-quarry.trails.straightened",
										   "A trail still wandered; cut straight", k);
				// Round the kit, which it may not clear, and through it only if nothing else reaches.
				if (!openTrail(map, t, workers[k], L.island, protect, protect, nullptr, 0, kRouteRadius) &&
					!openTrail(map, t, workers[k], L.island, quarryTiles, protect, nullptr, 0, kRouteRadius))
				{
					context.detail = "Colony " + std::to_string(k) + " has no walk to the isle.";
					return false;
				}
			}
		}
		return true;
	};
	if (!trails())
		return false;
	openColonyRoutes(map, context, t, kRouteCosts, kRouteRadius, &protect);
	secureStartingCrops(game, context, t);
	reopenCrampedStarts(game, context, amounts);
	openColonyRoutes(map, context, t, kRouteCosts, kRouteRadius, &protect);
	if (!trails())
		return false;
	// Where trails and routes cut through fields they leave one-tile strips of crop between two lanes
	// (review round 4, 128 maps); a crop tile open on two opposite sides goes, outside the kits.
	const int slivers = removeCropSlivers(map, t, protect);
	context.telemetry.measure("central-quarry.fields.slivers", slivers);
	if (context.telemetry.enabled())
	{
		const std::vector<std::vector<int>> workers = unitTilesByTeam(map, teams);
		const std::vector<unsigned char> open = walkableTiles(map);
		std::vector<unsigned char> ground(n, 0);
		for (int i = 0; i < n; ++i)
			ground[i] = !map.isWater(i % t.w, i / t.w);
		for (int k = 0; k < teams; ++k)
		{
			const std::vector<int> walked = stepsFrom(t, tileMask(t, workers[k]), open);
			const std::vector<int> terrain = stepsFrom(t, tileMask(t, workers[k]), ground);
			int best = INT_MAX, bestTerrain = INT_MAX;
			for (int i = 0; i < n; ++i)
				if (L.island[i])
				{
					if (walked[i] >= 0)
						best = std::min(best, walked[i]);
					if (terrain[i] >= 0)
						bestTerrain = std::min(bestTerrain, terrain[i]);
				}
			context.telemetry.measure("central-quarry.isle.walk", best, k);
			context.telemetry.measure("central-quarry.isle.terrain-walk", bestTerrain, k);
		}
	}
	return true;
}

// The lobby's check, from the request alone: running the whole design here tripled its cost (it runs
// again to generate and again to validate). Refusals that depend on the seed (no room for the
// colonies, a bar that misses) surface when the map is generated.
std::string validateRequest(const GenerationRequest &request)
{
	if (request.nbTeams < 2)
		return "Central Quarry needs at least two colonies.";
	if (!lakeSizeFor(request).fits)
		return "The lake does not fit this map; use a smaller lake or a bigger map.";
	return "";
}

// Checked on the finished world against the rebuilt design: the only stone is the quarry, on the
// island, of the size asked; with the sand bars shut nobody walks onto the island; every colony
// walks to colony 0 and to the isle, and their walks to it are even.
std::string validateWorld(const Game &game, const GenerationContext &context)
{
	GenerationContext replay(context.request);
	const Layout L = design(context.request, replay);
	const Map &map = game.map;
	if (const std::string mismatch = designMismatch(L, map, "central quarry"); !mismatch.empty())
		return mismatch;
	const CentralQuarryOptions o(context.request);
	const Torus &t = L.t;
	const int n = t.size(), teams = context.request.nbTeams;
	int stone = 0;
	for (int i = 0; i < n; ++i)
		if (map.getResource(i % t.w, i / t.w).type == STONE)
		{
			if (!L.island[i])
				return "Stone at (" + std::to_string(i % t.w) + ", " + std::to_string(i / t.w) +
					   ") is off the isle.";
			++stone;
		}
	if (stone != o.quarrySize)
		return "The quarry has " + std::to_string(stone) + " tiles, not " +
			   std::to_string(o.quarrySize) + ".";
	// The island's crops are its garden's, and the garden has some of each.
	int gardenWheat = 0, gardenWood = 0;
	for (int i = 0; i < n; ++i)
	{
		const int type = map.getResource(i % t.w, i / t.w).type;
		if ((type == WHEAT || type == WOOD) && L.island[i] && !L.garden[i])
			return "A crop at (" + std::to_string(i % t.w) + ", " + std::to_string(i / t.w) +
				   ") lies on the isle outside its garden.";
		gardenWheat += L.garden[i] && type == WHEAT;
		gardenWood += L.garden[i] && type == WOOD;
	}
	if (gardenWheat == 0 || gardenWood == 0)
		return "The isle's garden has no wheat or no wood.";

	const ColonyWalk walk = walkFromFirstColony(map, teams, "the country", "");
	if (!walk.error.empty())
		return walk.error;

	// With the bars shut, the island is out of reach on foot (terrain alone: deposits come and go).
	std::vector<unsigned char> ground(n, 0), sources(n, 0);
	for (int i = 0; i < n; ++i)
		ground[i] = !map.isWater(i % t.w, i / t.w) && !L.bars[i];
	for (const std::vector<int> &tiles : walk.workers)
		for (int i : tiles)
			sources[i] = 1;
	const Flood shut = floodFrom(t, sources, ground);
	for (int i : shut.visited)
		if (L.island[i])
			return "The isle can be walked to without a sand bar, at (" + std::to_string(i % t.w) +
				   ", " + std::to_string(i / t.w) + ").";

	// Every colony's walk to the isle: to the nearest open island tile its workers reach.
	const std::vector<unsigned char> open = walkableTiles(map);
	std::vector<int> targets;
	for (int k = 0; k < teams; ++k)
	{
		const std::vector<int> steps = stepsFrom(t, tileMask(t, walk.workers[k]), open);
		int best = -1;
		for (int i = 0; i < n; ++i)
			if (L.island[i] && steps[i] >= 0 && (best < 0 || steps[i] < steps[best]))
				best = i;
		if (best < 0)
			return "Colony " + std::to_string(k) + " cannot walk to the isle.";
		targets.push_back(best);
	}
	const WalkSpread spread = walkSpread(map, t, walk.workers, targets);
	if (spread.unreached >= 0)
		return "Colony " + std::to_string(spread.unreached) + " cannot walk to the isle.";
	if (spread.longest - spread.shortest > 2 * L.band + kWalkSlack)
		return "The colonies' walks to the isle differ by " +
			   std::to_string(spread.longest - spread.shortest) + " steps.";

	// No colony starts a short swim from the isle, and no two colonies start on top of each other,
	// measured over the terrain alone from each colony's workers.
	std::vector<unsigned char> land(n, 0);
	for (int i = 0; i < n; ++i)
		land[i] = !map.isWater(i % t.w, i / t.w);
	const std::vector<int> swim = stepsFrom(t, L.island);
	std::vector<int> firstWorkers;
	for (int k = 0; k < teams; ++k)
	{
		int nearest = INT_MAX;
		for (int i : walk.workers[k])
			nearest = std::min(nearest, swim[i]);
		if (nearest * 100 < L.distance * kLeastSwimPercent - kSiteSlack * 100)
			return "Colony " + std::to_string(k) + " starts " + std::to_string(nearest) +
				   " steps' swim from the isle.";
		firstWorkers.push_back(walk.workers[k].front());
	}
	for (int k = 0; k < teams; ++k)
	{
		const int wx = firstWorkers[k] % t.w, wy = firstWorkers[k] / t.w;
		int wheat = 0;
		for (int dy = -kNearbyReach; dy <= kNearbyReach; ++dy)
			for (int dx = -kNearbyReach; dx <= kNearbyReach; ++dx)
				wheat += map.getResource(t.at(wx + dx, wy + dy) % t.w, t.at(wx + dx, wy + dy) / t.w).type == WHEAT;
		if (wheat < kLeastWheatNearby)
			return "Colony " + std::to_string(k) + " has only " + std::to_string(wheat) +
				   " wheat tiles near its swarm.";
	}
	if (const int closest = closestWalk(t, firstWorkers, land); closest < kLeastSpacing - kSiteSlack)
		return "Two colonies start " + std::to_string(closest) + " steps apart.";
	return "";
}
} // namespace

CentralQuarryOptions::CentralQuarryOptions(const GenerationRequest &r)
	: lakeSize(r.option("lake-size")), islandSize(r.option("island-size")),
	  quarrySize(r.option("quarry-size")), sandBars(r.option("sand-bars")),
	  woodland(r.option("woodland")), wheat(r.option("wheat-amount")), wood(r.option("wood-amount")),
	  algae(r.option("algae-amount")), fruit(r.option("fruit-amount"))
{
}

GeneratorDefinition centralQuarryDefinition()
{
	// The lake's radius as a percentage of half the shorter side, the island's as a percentage of
	// the lake's; the quarry in tiles (the only stone on the map, so there is no stone amount); the
	// sand bars to the island; the share of the country under woodland.
	return {"central-quarry",
			56,
			"Central Quarry",
			1,
			false,
			{{"lake-size", "Lake size", 14, 30, 2, 24, ControlGroup::Terrain},
			 {"island-size", "Island size", 30, 60, 5, 45, ControlGroup::Terrain},
			 {"quarry-size", "Quarry size", 4, 16, 1, 9, ControlGroup::Layout},
			 {"sand-bars", "Sand bars", 1, 4, 1, 2, ControlGroup::Terrain},
			 {"woodland", "Woodland", 0, 80, 10, 40, ControlGroup::Terrain},
			 GeneratorControl::percentage("wheat-amount", "Wheat amount"),
			 GeneratorControl::percentage("wood-amount", "Wood amount"),
			 GeneratorControl::percentage("algae-amount", "Algae amount"),
			 GeneratorControl::percentage("fruit-amount", "Fruit amount")},
			generate,
			true,
			validateRequest,
			validateWorld,
			// Tagged by the #336 vocabulary: the isle in its lake is the contested centre (as Contested
			// commons' commons island); the streams read as rivers; woods are scenery here, not a
			// forest map (Old growth, Locust). Colonies are found by a walk-band search, not a
			// repeated module, so no fairness tag.
			{"terrain:natural", "feature:lakes", "feature:islands", "feature:river",
			 "style:contested-center", "style:wide-open"}};
}
