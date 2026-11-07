// SPDX-License-Identifier: GPL-3.0-or-later
#include "RainShadowGenerator.h"
#include "Drawing.h"
#include "FertilityField.h"
#include "Game.h"
#include "GenerationContext.h"
#include "Geometry.h"
#include "Grid.h"
#include "Homes.h"
#include "LatticeNoise.h"
#include "Morphology.h"
#include "Orbits.h"
#include "Patterns.h"
#include "Pipeline.h"
#include "Points.h"
#include "Planting.h"
#include "Resources.h"
#include "ResourceSemantics.h"
#include "Roads.h"
#include "Rock.h"
#include "Settlements.h"
#include "Sketch.h"
#include "TerrainTypeTable.h"
#include "Topology.h"
#include <algorithm>
#include <array>
#include <climits>
#include <cstddef>
#include <cmath>
#include <cstdint>
#include <numeric>
#include <string>
#include <vector>
using namespace MapGeneration;

// Rain shadow: a map with a grain. Long rock ridges run right across the torus, parallel and a valley
// apart, and the wind blows across them from one side. The windward foot of every ridge catches the
// rain: a chain of streams runs along it, spring seeps lie between the streams and the rock, and the
// farmland grows there. The lee side is dry: a skirt of scree at the foot of the rock, then a band of
// sand drifting into dunes downwind, where nothing grows and nothing can be built. Every ridge is cut
// by passes at regular intervals, staggered from ridge to ridge, so moving along a valley is easy and
// crossing the ridges is slow.
//
// A colony's home sits in the middle of a valley: its lush side faces the windward foot of the next
// ridge, and behind it lies the dry back of the ridge it shelters under, with the colony's own quarry
// face cut into that rock straight upwind of the town. An attacker crossing a pass picks through the
// scree of the pass mouth and arrives on barren sand facing a defender standing on fields; a defender
// who sallies out crosses the same sand. Nothing here is turned round a centre: the ridges are stripes
// that wrap the torus exactly at their slant (Patterns.h), the homes sit on a lattice (Orbits.h)
// snapped to the middle of their valleys, and fairness comes from every valley being the same valley.
//
// WHY IT PLAYS WELL (docs/map-generators/game-rules-for-map-design.md). Ridge rock is a wall no unit
// crosses and nobody clears, and towers shoot over it, so the passes are doors worth holding, and being
// staggered no straight road runs across the map. Crops regrow only near water and never beside sand,
// so the windward streams decide where the fields are and the lee sand is a desert even though it is
// walkable: an army on it is far from any inn. Sand also carries no building, so a forward base has to
// wait for the next valley. Stone is no longer the ridge itself but the quarry faces cut into it: one
// behind every home, the same for every colony, and more along the ridges as `stone-amount` says.
//
// DESIGN 2.0 (2026-11-07, after Bajada 2.0). The ridges are Ridges-group rock drawn as tiles and
// converted with rockVertices (a vertex is rock only where all four of its tiles are), so the blocked
// region is the drawn band; the band is drawn one tile thicker than `ridge-thickness` (a band drawn
// at the nominal thickness loses about a tile to its convex corners and leaks diagonally on slanted
// stretches), and design() thickens it half a tile at a time until a flood across every ridge with
// the passes shut finds no way over. Two sketches: the mechanics (WATER, SAND, GRASS, LOAM, SCREE,
// RIDGE_ROCK) that every shared helper and check reads, and looks painted over them with composeLooks
// inside each vertex's rule group: a look-only `climate` (temperate, steppe, desert) grades the valley
// grass lush by the water and dry away from it and the lee sand from plain at the scree to dunes and
// wind-rippled crests downwind, and a look-only `lithology` picks granite, sandstone hoodoos or volcanic
// outcrop with pumice talus. Home clearings, the ground round them and quarry approaches stay plain
// GRASS, which the starting-crop guarantee and the stone rule need.
//
// The quarry behind every home is found once in colony 0's frame and copied tile for tile: the warp
// that sways the ridges fades to nothing across every home's valley (so the ridges run straight behind
// and before every town and every home sits the same distance from both), the homes snap to tiles of
// equal phase, and the stretch of ridge round the quarry is stamped from colony 0's. Passes are shifted
// along the ridges (and the quarry slid along the valley wall, the same way for every home) to keep
// clear of the quarries; a pass that still cannot is closed, never a quarry moved. The windward gap
// between the rock and the streams (kept at four tiles so the streams' growth reaches as far into the
// valley as before) is spring seeps, fertile ground that waters at 48/256, with patches of scree
// against the rock. The lee sand starts at the rock now (sand no longer spoils stone), with a frayed
// scree skirt one to three tiles wide in front of it. Pass floors are sand, so a pass never grows shut,
// and its mouths are scree, slow to rush. Hardpan pans were considered for the far lee and left out:
// hardpan does not inhibit growth, so it would change what the lee band is for.
//
// FEEDBACK 2026-09-13 (first play): "default amount of water is not enough. the ponds need to be
// deeper and more connected to each other. also, for the passes that connect between the ridges, we
// need sand roadways leading inland to prevent those chasms from being overgrown because they get
// clogged up too easily. also in the valleys between the mountains it just feels too empty; toss in
// a few rivers connected to some inland lakes connecting from the rain shadows, and some inland
// desert or sporadic sand patches to break up the texture." So: the windward foot is a chain of
// streams 7 deep and 20 long every 24 (was 3 deep, 12 long: nearly continuous water now, with a
// four-tile beach gap between streams); a sand road runs through every pass and kPassRoad tiles into
// the valley on both sides; inland lakes lie scattered along the middle of every valley, each joined
// by a wandering river to the nearest windward stream; and sand patches are sprinkled over the
// valleys' grass (`sand-patches` percent).
//
// FEEDBACK 2026-09-14 (second play): "make the default lakes/ponds on the rain side of the mountain a
// couple tiles larger. still too small right now. Also the sand roads that go between the ridges need
// to extend further into either side of the base and should connect more smoothly to the beaches
// around the water instead of being distinct from them." So the streams are 7 deep (was 5), the pass
// roads run 18 tiles into the valley on both sides (was 12), and on the windward side each road ends
// in a lane of sand along the middle of the foot, out to the streams either side of the pass, so the
// road runs into their beaches instead of stopping short of them in the grass.
namespace
{

// Every home's starting kit, unscaled whatever the amounts say: wheat and wood beside the home's own
// pond. Its stone is the quarry face behind it.
constexpr int kHomeWheat = 14, kHomeWood = 12;
// The ground a home clearing keeps from the ridges, the sand and the pools on either side: its own
// pond's beach (2 tiles) and a lane to walk round the home (2 more).
constexpr int kHomeMargin = 4;
// The ridge's own wobble, as a share of the valley spacing: enough that the ridges read as
// mountains rather than ruled lines, little enough that a valley never pinches shut (the pools and
// homes are placed by the warped phase, so they follow the wobble).
constexpr int kRidgeWarpPercent = 12;
// Ridge wobble noise cells, in tiles: features a few times longer than a pass, so a ridge sways
// rather than shivers.
constexpr int kRidgeWarpPeriod = 40;
// The wobble fades out across every home's valley: none within this many tiles either side of the
// line straight across the valley through the home (beyond the home clearing and the quarry's
// stretch of ridge), rising to its full sway over kStraightRamp more.
constexpr double kStraightMargin = 2, kStraightRamp = 24;
// The windward foot: streams start this many tiles out from the ridge's nominal edge and run this many
// deep into the valley. Revision 6 kept four tiles of grass between stream and stone so the stream's
// beach left the stone its pure grass; with rock the gap holds spring seeps instead, and staying at
// four keeps the streams' growth reaching as far into the valley.
constexpr int kFootGap = 4, kFootDepth = 7;
// Streams along the foot: each this long along the ridge, every this many tiles (first play: 20 every
// 24, from 12 every 24; second play: 7 deep, from 5). A stream 20 long and 7 deep is about 140 corners
// of water, which the engine's growth probe finds from most of the valley; the four-tile gaps between
// streams, once their beaches meet, are where the valley's beach lets units walk along the foot.
constexpr int kPoolLength = 20, kPoolSpacing = 24;
// A sand road through every pass, running this far into the valley on either side of the rock
// (first play: the passes "get clogged up too easily"; second play: 18, from 12, to reach well into
// the bases): a line of single sand corners, which nothing grows onto and nothing is built across. No
// stream lies within kPassClear tiles along the ridge of a pass, so the road never runs into water; on
// the windward side it ends in a lane along the foot that joins the streams' beaches (second play).
constexpr int kPassRoad = 18, kPassClear = 4;
// Inland lakes (first play: "a few rivers connected to some inland lakes"): sites at least this far
// apart along the middle of each valley (the band within kLakeBand of the valley's middle phase), each
// a rough disc of this radius, joined to the nearest windward stream of its own valley by a river this
// many corners wide (one tile of water, with the fords a wandering line leaves) wandering by up to
// kRiverWander.
constexpr int kLakeSpacing = 56;
constexpr double kLakeBand = 0.18, kLakeRadius = 5.0, kRiverHalfWidth = 1.0, kRiverWander = 4.0;
// The rock band is drawn this many tiles thicker than `ridge-thickness` (1.5 for the thinnest, whose
// slanted stretches otherwise leak), and thickened by kRockCoreStep until no flood crosses it.
constexpr double kRockCoreExtra = 1.0, kThinRockCoreExtra = 1.5, kRockCoreStep = 0.5, kRockCoreMost = 3.0;
// Quarry faces: tiles of the rock within kQuarryHalf (Chebyshev) of the face point and kQuarryDepth
// steps of the open side. A home's quarry must hold at least kHomeQuarryStone; ambient faces, half
// as wide, stand one every kQuarryPitch tiles of ridge face at the default `stone-amount`. Passes
// keep kQuarryPassClear tiles along the ridge from any quarry, and water three tiles (a beach is laid
// a vertex out from water, and every corner of a quarry tile must stay grass).
constexpr int kQuarryHalf = 3, kQuarryDepth = 2, kHomeQuarryStone = 8, kAmbientQuarryHalf = 2,
			  kQuarryPassClear = 3, kQuarryWaterClear = 3;
constexpr double kQuarryPitch = 48;
// The quarry may slide along the valley wall by one of these offsets (the same for every home) to
// keep the passes clear of it.
constexpr std::array<int, 7> kQuarrySlides{0, 8, -8, 16, -16, 24, -24};
// Talus: scree in a skirt kLeeTalus tiles at the lee foot of the rock, frayed out to kLeeTalusFray,
// and frayed round the mouths of the passes. None on the windward foot: scree there took farmland
// from the streams (a 1-tile frayed skirt cost 6% of the map's growth potential).
constexpr int kLeeTalus = 1, kLeeTalusFray = 3, kMouthTalus = 3;
constexpr int kTalusCut = 30000, kMouthTalusCut = 22000;

// The looks of a climate, inside each vertex's rule group so every climate plays alike: how far from
// water the valley grass stays lush and green, the share of the grass beyond that drying, and the
// lee sand's grade from the scree to the dune crests.
struct Climate
{
	const char *name;
	int lushReach, greenReach, dryPercent;
	TerrainType nearSand, dune, crest, patch, beach, seepWet, seepDry;
};
constexpr std::array<Climate, 3> kClimates{{
	{"temperate", 3, 16, 35, SAND, DUNE_SAND, WINDY_SAND_DUNE, SAND, SAND, SPRING_MEADOW, MOSS},
	{"steppe", 2, 8, 75, SAND, DUNE_SAND, WINDY_SAND_DUNE, DUNE_SAND, SAND, SPRING_MEADOW, LOAM},
	{"desert", 2, 5, 100, PALE_SAND, DUNE_SAND, WINDY_SAND_DUNE, PALE_SAND, PALE_SAND, SPRING_MEADOW, LOAM},
}};
// What the ridges are made of: rock and its talus, looks only.
struct Lithology
{
	const char *name;
	TerrainType rock, talus;
};
constexpr std::array<Lithology, 3> kLithologies{{
	{"granite", RIDGE_ROCK, SCREE},
	{"sandstone", HOODOOS, SCREE},
	{"volcanic", OUTCROP, PUMICE},
}};
// The garden round every home that keeps plain grass (the starting-crop guarantee looks for it), as
// tiles beyond the home clearing, frayed by up to kGardenFray so it never reads as a circle.
constexpr double kGarden = 6, kGardenFray = 5;

struct Layout
{
	Torus t{1, 1};
	StripeStyle across, along; // phase across the ridges (0 on a ridge) and along them
	int ridges = 0;            // crossings of the map, after any taken away to fit
	double spacing = 0;        // between ridges, in tiles
	double homeRadius = 0, coreExtra = 0;
	int windX = 0, windY = 0;  // the wind's direction in whole steps
	int passShift = 0, quarrySlide = 0, droppedPasses = 0, ambientQuarries = 0;
	Climate climate = kClimates[0];
	Lithology lithology = kLithologies[0];
	std::vector<int> phase, alongPhase;
	std::vector<int> crest; // distance from the nearest crest, in sixteenths of a tile
	int rockHalf = 0;       // the drawn band's half width, in sixteenths of a tile
	std::vector<ShapePoint> homes, kits;
	std::vector<int> homeOf, quarryOf;
	// Tile masks: the rock band (rock and quarry faces, not passes), pass floors, quarry faces; vertex
	// masks: rock, water, lee sand, roads, scree, seeps; the home clearings.
	std::vector<unsigned char> band, pass, quarry, rock, water, sand, road, scree, seep, clearing;
	std::vector<int> shadow; // steps downwind from the rock, -1 beyond the lee band
	std::vector<Site> lakes;
	std::string failure;
};

int circularGap(int a, int b)
{
	const int d = ((a - b) % 65536 + 65536) % 65536;
	return std::min(d, 65536 - d);
}

// Distance from the nearest crest, in sixteenths of a tile: the phase's distance from the crest over the
// local phase gradient, so a stretch the sway squeezes is drawn as thick as a straight one (measured on
// phase alone, a steep stretch of a 512 map came out under half as thick and leaked).
std::vector<int> crestDistance(const Torus &t, const std::vector<int> &phase)
{
	const auto difference = [](int a, int b)
	{
		const int d = ((a - b) % 65536 + 65536) % 65536;
		return d >= 32768 ? d - 65536 : d;
	};
	std::vector<int> distance(phase.size(), 0);
	for (int y = 0; y < t.h; ++y)
		for (int x = 0; x < t.w; ++x)
		{
			const double gx = difference(phase[t.at(x + 1, y)], phase[t.at(x - 1, y)]) / 2.0,
						 gy = difference(phase[t.at(x, y + 1)], phase[t.at(x, y - 1)]) / 2.0;
			const double gradient = std::max(1.0, ::MapGeneration::Numeric::hypot(gx, gy));
			distance[size_t(y) * t.w + x] = int(stripeDistance(phase[size_t(y) * t.w + x]) * 16 / gradient);
		}
	return distance;
}

// Whether a flood across any ridge, with the passes shut, gets from one face to the other: the region
// within `reach` (sixteenths of a tile) of the rock band, open where `open` says, from its windward edge
// to its lee edge. A pass is shut with the two tiles either side of it: the rock's convex corner tiles
// along a slanted pass walk, and slipping along them is going through the pass. Returns a tile the
// flood reached on the far side, or -1.
template <typename Open>
int ridgeLeak(const Torus &t, const std::vector<int> &phase, const std::vector<int> &crest,
			  const std::vector<unsigned char> &pass, int half, int reach, int edge, Open open)
{
	const int n = t.size();
	const std::vector<unsigned char> shut = dilate(t, pass, 2);
	std::vector<unsigned char> region(n, 0), windward(n, 0);
	for (int i = 0; i < n; ++i)
	{
		if (crest[i] >= half + reach || shut[i])
			continue;
		region[i] = open(i);
		windward[i] = region[i] && phase[i] > 32768 && crest[i] >= half + reach - edge;
	}
	const Flood flood = floodFrom(t, windward, region);
	for (int i : flood.visited)
		if (phase[i] < 32768 && crest[i] >= half + reach - edge)
			return i;
	return -1;
}

Layout design(const GenerationRequest &request, GenerationContext &context)
{
	const RainShadowOptions o(request);
	Layout L;
	L.t = {1 << request.wDec, 1 << request.hDec};
	const Torus &t = L.t;
	const int n = t.size(), teams = std::max(1, request.nbTeams);

	// The ridges: `ridges` crossings of the map from top to bottom, tilted so that following one
	// across the width climbs `slant` ridges. With a slant every ridge is one long spiral round the
	// torus, which is what keeps the passes staggered from one crossing to the next. The valley
	// between two ridges must hold a home with its margins, the windward pools and the lee sand: on a
	// small map the ridges are taken away one at a time until the valleys are wide enough (four
	// ridges suit a 256 map; a 128 map gets two), then the homes shrink to what is left, and only then
	// is the map refused.
	L.across.acrossX = o.slant;
	L.across.warpPeriod = kRidgeWarpPeriod;
	L.across.warpPercent = 0; // the warp is added below, faded round the homes
	const double taken = kFootGap + kFootDepth + o.leeWidth;
	const double smallest = homePondRadius(o.homeSize) + kHomePondGap + kHomeSwarmRoom;
	for (L.ridges = o.ridges; L.ridges > 2; --L.ridges)
	{
		L.across.acrossY = L.ridges;
		if ((stripeSpacing(t, L.across) - o.ridgeThickness - taken) / 2 - kHomeMargin >= smallest)
			break;
	}
	L.across.acrossY = L.ridges;
	L.along = alongStripes(t, L.across);
	L.spacing = stripeSpacing(t, L.across);
	const double alongLength = stripeSpacing(t, L.along);
	// Phase units: 65536 is one spacing across, or one turn along.
	const auto tiles16 = [](double tiles) { return int(tiles * 16); }; // tile distances in sixteenths
	const auto alongUnits = [&](double tiles) { return int(tiles / alongLength * 65536); };
	const double valley = L.spacing - o.ridgeThickness;
	L.homeRadius = std::min<double>(o.homeSize, std::floor((valley - taken) / 2 - kHomeMargin));
	context.telemetry.measure("rain-shadow.ridges.actual", L.ridges);
	context.telemetry.measure("rain-shadow.valleys.spacing", L.spacing);
	context.telemetry.measure("rain-shadow.homes.actual-radius", L.homeRadius);
	if (L.ridges < o.ridges)
		context.telemetry.fallback("rain-shadow.ridges.reduced",
								   "Fewer ridges needed for viable valleys");
	if (L.homeRadius < o.homeSize)
		context.telemetry.fallback("rain-shadow.homes.shrunk", "Homes shrank to the valley budget");
	if (!homeHasRoom(L.homeRadius))
	{
		L.failure =
			"The valleys are too narrow for the homes; use fewer ridges, thinner ridges or a "
			"bigger map.";
		return L;
	}
	// The looks: drawn from their own streams, so a pinned look changes nothing else.
	L.climate = kClimates[o.climate > 0 ? std::min<int>(o.climate, kClimates.size()) - 1
										: int(context.bounded("rain-climate", kClimates.size()))];
	L.lithology = kLithologies[o.lithology > 0 ? std::min<int>(o.lithology, kLithologies.size()) - 1
											   : int(context.bounded("rain-lithology", kLithologies.size()))];
	context.telemetry.choice("rain-shadow.climate", L.climate.name);
	context.telemetry.choice("rain-shadow.lithology", L.lithology.name);

	// The straight phase (exact whole turns, so it is the same at every tile it is the same at) and the
	// wobble the ridges sway by, drawn from the same stream revision 6 drew it from; and the phase
	// along the ridges, which carries no warp.
	std::mt19937 &ridgeStream = context.stream("rain-ridges");
	const std::vector<int> linear = stripePhase(t, L.across, ridgeStream);
	const std::vector<int> warp = fractalNoise(t.w, t.h, std::max(2, kRidgeWarpPeriod), 3, ridgeStream);
	L.alongPhase = stripePhase(t, L.along, ridgeStream);
	// The wind blows towards rising phase: its direction is the across field's normal, as whole steps.
	{
		const int g = std::gcd(std::max(1, o.slant * t.h), L.ridges * t.w);
		L.windX = o.slant * t.h / g;
		L.windY = L.ridges * t.w / g;
	}
	const double windLength = ::MapGeneration::Numeric::hypot(double(L.windX), double(L.windY));
	const double nx = L.windX / windLength, ny = L.windY / windLength; // downwind, across the ridges
	const double ax = -ny, ay = nx;                                      // along the ridges

	// Homes: on a lattice, each slid along the wind to the middle of its valley by the straight phase
	// and snapped to the nearby tile whose phase is nearest the middle, so every home has the same
	// ridge behind it and the same foot before it (the warp fades out round them, below).
	{
		const LatticeSites lattice =
			latticeSites(t.w, t.h, teams, context.bounded("rain-layout", std::uint32_t(t.w)),
						 context.bounded("rain-layout", std::uint32_t(t.h)));
		for (const ShapePoint &site : lattice.sites)
		{
			const int at = t.at(int(site.x), int(site.y));
			const double shortfall = (32768 - linear[at]) / 65536.0 * L.spacing;
			const double sx = site.x + shortfall * nx, sy = site.y + shortfall * ny;
			const int cx = int(std::floor(sx)), cy = int(std::floor(sy));
			int best = -1, bestGap = 0;
			double bestDistance = 0;
			for (int dy = -4; dy <= 4; ++dy)
				for (int dx = -4; dx <= 4; ++dx)
				{
					const int i = t.at(cx + dx, cy + dy);
					const int gap = circularGap(linear[i], 32768);
					const double distance = (cx + dx + 0.5 - sx) * (cx + dx + 0.5 - sx) +
											(cy + dy + 0.5 - sy) * (cy + dy + 0.5 - sy);
					if (best < 0 || gap < bestGap || (gap == bestGap && distance < bestDistance))
					{
						best = i;
						bestGap = gap;
						bestDistance = distance;
					}
				}
			L.homes.push_back({t.remainderX(best) + 0.5, best / t.w + 0.5});
		}
	}
	dealStarts(context, L.homes); // which colony gets which valley site is a draw, not the order
	const double nearest = nearestSiteDistance(t, L.homes);
	if (nearest < 2 * (L.homeRadius + kHomeMargin))
	{
		L.failure = "Too many colonies for this map; use a bigger map or fewer colonies.";
		return L;
	}
	std::vector<int> homeTile;
	for (const ShapePoint &home : L.homes)
		homeTile.push_back(t.at(int(home.x), int(home.y)));
	// The clearing round a home has no pools, no sand and no rock.
	L.clearing.assign(n, 0);
	{
		const double reach = L.homeRadius + kHomeMargin;
		const int r = int(std::ceil(reach));
		for (int home : homeTile)
			for (int dy = -r; dy <= r; ++dy)
				for (int dx = -r; dx <= r; ++dx)
					if (::MapGeneration::Numeric::hypot(double(dx), double(dy)) <= reach)
						L.clearing[t.at(t.remainderX(home) + dx, home / t.w + dy)] = 1;
	}

	// Passes are cut every `passSpacing` tiles along the ridge (as many whole passes as fit in one turn
	// along it, never fewer than one), and on straight ridges every other ridge's passes are shifted by
	// half a spacing, so no pass lines up with the one on the next ridge; a slanted ridge staggers its
	// own passes, since each crossing of the map shifts the along phase by a fraction of a turn. The
	// whole pattern may shift along the ridges to keep clear of the homes' quarries.
	const int ridgeHalf = tiles16(o.ridgeThickness / 2.0);
	const int passes = std::max(1, int(std::lround(alongLength / o.passSpacing)));
	const int passPeriod = 65536 / passes, passHalf = alongUnits(o.passWidth / 2.0);
	const auto passOffsetAt = [&](int i, int shift)
	{
		const int y = i / t.w;
		const int stagger = o.slant == 0 ? ((L.ridges * y / t.h) % 2) * passPeriod / 2 : 0;
		const int offset = ((L.alongPhase[i] + stagger + shift) % passPeriod + passPeriod) % passPeriod;
		return std::min(offset, passPeriod - offset);
	};
	// The face of the ridge a home shelters under: straight upwind from the home (slid along the valley
	// by `slide` tiles) to the first tile of the band.
	const auto faceFrom = [&](const std::vector<int> &field, int half, int home, int slide)
	{
		const double hx = t.remainderX(home) + 0.5 + slide * ax, hy = home / t.w + 0.5 + slide * ay;
		for (double s = 0; s < L.spacing; s += 0.5)
		{
			const int i = t.at(int(std::floor(hx - s * nx)), int(std::floor(hy - s * ny)));
			if (field[i] < half)
				return i;
		}
		return -1;
	};
	const int windowHalf = kQuarryHalf + 3;
	const double firstExtra = o.ridgeThickness <= 2 ? kThinRockCoreExtra : kRockCoreExtra;
	{
		// The slide and shift that keep every home's quarry, and the stretch of ridge round it, clear
		// of the passes: the first slide (nearest the home's own line first) with a shift that clears
		// them all, or failing that the one that leaves the fewest homes with a pass to close.
		const int half = tiles16((o.ridgeThickness + firstExtra) / 2.0);
		const std::vector<int> straight = crestDistance(t, linear);
		int bestConflicts = teams + 1;
		for (int slide : kQuarrySlides)
		{
			const int face = faceFrom(straight, half, homeTile[0], slide);
			if (face < 0)
				continue;
			const int fdx = t.offsetX(t.remainderX(homeTile[0]), t.remainderX(face)),
					  fdy = t.offsetY(homeTile[0] / t.w, face / t.w);
			for (int step = 0; step < 16 && bestConflicts > 0; ++step)
			{
				const int shift = step * passPeriod / 16;
				int conflicts = 0;
				for (int home : homeTile)
				{
					bool cut = false;
					for (int dy = -windowHalf; dy <= windowHalf && !cut; ++dy)
						for (int dx = -windowHalf; dx <= windowHalf && !cut; ++dx)
						{
							const int i = t.at(t.remainderX(home) + fdx + dx, home / t.w + fdy + dy);
							cut = straight[i] < half + tiles16(2) &&
								  passOffsetAt(i, shift) < passHalf + alongUnits(kQuarryPassClear);
						}
					conflicts += cut;
				}
				if (conflicts < bestConflicts)
				{
					bestConflicts = conflicts;
					L.quarrySlide = slide;
					L.passShift = shift;
				}
			}
			if (bestConflicts == 0)
				break;
		}
		if (bestConflicts > teams)
		{
			L.failure = "No ridge stands behind the homes; use fewer ridges or a bigger map.";
			return L;
		}
		context.telemetry.measure("rain-shadow.quarry.slide", L.quarrySlide);
		context.telemetry.measure("rain-shadow.passes.shift", L.passShift);
	}

	// The wobble fades out across every home's valley: a straight stretch of ridge behind and before
	// every home, wide enough for the home and its quarry, easing back into the sway beyond.
	{
		const double reach = std::max(L.homeRadius + kHomeMargin, std::abs(L.quarrySlide) + kQuarryHalf + 4.0) +
							 kStraightMargin;
		std::vector<int> weight(n, 65536);
		for (int home : homeTile)
			for (int i = 0; i < n; ++i)
			{
				const double dx = t.offsetX(t.remainderX(home), t.remainderX(i)),
							 dy = t.offsetY(home / t.w, i / t.w);
				const double acrossOver = std::max(0.0, std::abs(dx * nx + dy * ny) - L.spacing / 2),
							 alongOver = std::max(0.0, std::abs(dx * ax + dy * ay) - reach);
				const double share =
					std::min(1.0, ::MapGeneration::Numeric::hypot(acrossOver, alongOver) / kStraightRamp);
				weight[i] = std::min(weight[i], int(65536 * share * share * (3 - 2 * share)));
			}
		L.phase.resize(n);
		for (int i = 0; i < n; ++i)
		{
			const std::int64_t sway = std::int64_t(warp[i] - 32768) * kRidgeWarpPercent / 100;
			const std::int64_t turns = linear[i] + sway * weight[i] / 65536;
			L.phase[i] = int((turns % 65536 + 65536) % 65536);
		}
	}
	L.crest = crestDistance(t, L.phase);

	// The rock: the band of phase round every crest, drawn kRockCoreExtra thicker than asked, less the
	// passes; a pass that would still cut a home's quarry is closed whole; colony 0's stretch of ridge
	// round its quarry is stamped onto every home's, and the quarry face copied with it. Thickened half
	// a tile at a time while a flood can cross any ridge with the passes shut.
	int face0 = -1, fdx = 0, fdy = 0;
	for (L.coreExtra = firstExtra;; L.coreExtra += kRockCoreStep)
	{
		const int half = L.rockHalf = tiles16((o.ridgeThickness + L.coreExtra) / 2.0);
		L.band.assign(n, 0);
		L.pass.assign(n, 0);
		for (int i = 0; i < n; ++i)
			if (L.crest[i] < half && !L.clearing[i])
				(passOffsetAt(i, L.passShift) < passHalf ? L.pass : L.band)[i] = 1;
		face0 = faceFrom(L.crest, half, homeTile[0], L.quarrySlide);
		if (face0 < 0)
		{
			L.failure = "No ridge stands behind the homes; use fewer ridges or a bigger map.";
			return L;
		}
		fdx = t.offsetX(t.remainderX(homeTile[0]), t.remainderX(face0));
		fdy = t.offsetY(homeTile[0] / t.w, face0 / t.w);
		const auto faceOf = [&](int k) { return t.at(t.remainderX(homeTile[k]) + fdx, homeTile[k] / t.w + fdy); };
		{
			std::vector<unsigned char> guard(n, 0);
			const int g = windowHalf + kQuarryPassClear;
			for (int k = 0; k < teams; ++k)
				for (int dy = -g; dy <= g; ++dy)
					for (int dx = -g; dx <= g; ++dx)
						guard[t.at(t.remainderX(faceOf(k)) + dx, faceOf(k) / t.w + dy)] = 1;
			const std::vector<int> floors = connectedRegions(L.pass, t.w, t.h, true, GridNeighbors::Eight);
			std::vector<unsigned char> closed(n, 0);
			for (int i = 0; i < n; ++i)
				if (L.pass[i] && guard[i] && floors[i] >= 0)
					closed[floors[i]] = 1;
			L.droppedPasses = int(std::count(closed.begin(), closed.end(), 1));
			for (int i = 0; i < n; ++i)
				if (L.pass[i] && floors[i] >= 0 && closed[floors[i]])
				{
					L.pass[i] = 0;
					L.band[i] = 1;
				}
		}
		struct StencilTile
		{
			int dx, dy;
			unsigned char rock;
		};
		std::vector<StencilTile> stencil;
		for (int dy = -windowHalf; dy <= windowHalf; ++dy)
			for (int dx = -windowHalf; dx <= windowHalf; ++dx)
			{
				const int i = t.at(t.remainderX(face0) + dx, face0 / t.w + dy);
				if (L.crest[i] < half + tiles16(2))
					stencil.push_back({dx, dy, L.band[i]});
			}
		for (int k = 1; k < teams; ++k)
			for (const StencilTile &s : stencil)
			{
				const int i = t.at(t.remainderX(faceOf(k)) + s.dx, faceOf(k) / t.w + s.dy);
				L.band[i] = s.rock;
				L.pass[i] = 0;
			}
		// The quarry: colony 0's face, found on its lee side, copied to every home.
		std::vector<unsigned char> lee(n, 0);
		for (int i = 0; i < n; ++i)
			lee[i] = !L.band[i] && !L.pass[i] && L.phase[i] < 32768;
		const std::vector<int> face = quarryNear(t, L.band, lee, face0, kQuarryHalf, kQuarryDepth);
		L.quarry.assign(n, 0);
		L.quarryOf.assign(n, -1);
		for (int k = 0; k < teams; ++k)
			for (int q : face)
			{
				const int i = t.at(t.remainderX(faceOf(k)) + t.offsetX(t.remainderX(face0), t.remainderX(q)),
								   faceOf(k) / t.w + t.offsetY(face0 / t.w, q / t.w));
				if (L.band[i])
				{
					L.quarry[i] = 1;
					L.quarryOf[i] = k;
				}
			}
		if (int(face.size()) < kHomeQuarryStone)
		{
			L.failure = "The ridges leave no room for the homes' quarries; use thicker ridges or a bigger map.";
			return L;
		}
		std::vector<unsigned char> rockTiles(n, 0);
		for (int i = 0; i < n; ++i)
			rockTiles[i] = L.band[i] && !L.quarry[i];
		L.rock = rockVertices(t, rockTiles);
		const auto blocked = [&](int i)
		{
			const int x = t.remainderX(i), y = i / t.w;
			return L.quarry[i] || L.rock[i] + L.rock[t.at(x + 1, y)] + L.rock[t.at(x, y + 1)] +
										  L.rock[t.at(x + 1, y + 1)] >=
									  2;
		};
		const int leak = ridgeLeak(t, L.phase, L.crest, L.pass, half, tiles16(3), tiles16(1),
								   [&](int i) { return !L.pass[i] && !blocked(i); });
		if (leak < 0)
			break;
		if (L.coreExtra + kRockCoreStep > kRockCoreMost)
		{
			L.failure = "The ridges could not be sealed; use thicker ridges.";
			return L;
		}
	}
	context.telemetry.measure("rain-shadow.rock.core-extra-tenths", int(std::lround(L.coreExtra * 10)));
	if (L.droppedPasses > 0)
		context.telemetry.fallback("rain-shadow.passes.closed", "A pass was closed to keep clear of the homes' quarries");
	const auto faceOf = [&](int k) { return t.at(t.remainderX(homeTile[k]) + fdx, homeTile[k] / t.w + fdy); };

	// Windward pools: a chain along the foot of every ridge on the side the wind comes from, which
	// is the side of falling phase (the wind blows towards rising phase, so it arrives at the ridge
	// from below its phase). Each pool runs kPoolLength along the ridge every kPoolSpacing, and
	// kFootGap to kFootGap + kFootDepth out from the ridge's nominal edge.
	L.water.assign(n, 0);
	L.sand.assign(n, 0);
	L.road.assign(n, 0);
	const int footNear = ridgeHalf + tiles16(kFootGap), footFar = footNear + tiles16(kFootDepth);
	const int poolPeriod = alongUnits(kPoolSpacing), poolHalf = alongUnits(kPoolLength / 2.0);
	const int passClear = passHalf + alongUnits(kPassClear);
	const int footMiddle = (footNear + footFar) / 2;
	// The road leaves only open passes: a closed one keeps its pools and has no road.
	const std::vector<int> fromPass = stepsFrom(t, L.pass);
	for (int i = 0; i < n; ++i)
	{
		if (L.band[i] || L.pass[i])
			continue;
		// How far below the next ridge's crest, on the windward side of it.
		const int below = L.phase[i] >= 32768 ? L.crest[i] : INT_MAX;
		const int offset = passOffsetAt(i, L.passShift);
		if (below >= footNear && below < footFar && offset > passClear)
		{
			const int pool = L.alongPhase[i] % std::max(1, poolPeriod);
			if (pool < poolHalf || poolPeriod - pool < poolHalf)
				L.water[i] = 1;
		}
		const bool open = fromPass[i] >= 0 && fromPass[i] <= kPassRoad + 2;
		// The pass road: the pass's centre line, from kPassRoad tiles before the ridge to as far past.
		if (open && offset <= alongUnits(0.5) && L.crest[i] < ridgeHalf + tiles16(kPassRoad))
			L.road[i] = 1;
		// The road's junction with the foot (second play): a lane of sand two tiles across along the
		// middle of the foot band, from the pass out to the streams on either side of it, so the road
		// meets their beaches rather than stopping in the grass a few tiles short. It runs one tile
		// past kPassClear so it touches the first tile of each stream's beach.
		if (open && offset <= passClear + alongUnits(1) && below >= footMiddle - tiles16(1) &&
			below < footMiddle + tiles16(1) && !L.water[i])
			L.road[i] = 1;
	}
	// Lee sand: downwind of the rock as far as leeWidth, measured from the rock itself (upwindSteps
	// against the wind) so the sand stops where a pass lets the rain through. It starts at the rock:
	// sand no longer spoils a ridge, which is rock now, not stone on grass.
	L.shadow = upwindSteps(t, L.band, L.windX, L.windY, o.leeWidth + 1);
	for (int i = 0; i < n; ++i)
		L.sand[i] = L.shadow[i] >= 1;
	// Inland lakes along the middle of every valley (first play: the valleys "feel too empty"): sites
	// thrown at least kLakeSpacing apart over the map and kept where the across phase is within
	// kLakeBand of the valley's middle, each a rough disc, joined by a wandering river to the nearest
	// stream tile it can see without crossing rock (its own valley's). Rivers leave the lee sand alone
	// but cross the valley's grass.
	if (o.inlandLakes)
	{
		const RadialShape lake(kLakeRadius, 0.35, context, "rain-lakes");
		std::mt19937 &rivers = context.stream("rain-rivers");
		std::vector<int> streams;
		for (int i = 0; i < n; ++i)
			if (L.water[i])
				streams.push_back(i);
		for (const Site &site : spreadPoints(t, kLakeSpacing, context, "rain-lakes"))
		{
			const int at = t.at(site.x, site.y);
			if (std::abs(L.phase[at] - 32768) > kLakeBand * 65536)
				continue;
			L.lakes.push_back(site);
			fillShape(L.water, t, site.x + 0.5, site.y + 0.5, lake, 0.0);
			std::vector<std::pair<int, int>> byDistance;
			for (int i : streams)
				byDistance.push_back({t.dist2(site.x, site.y, t.remainderX(i), i / t.w), i});
			std::sort(byDistance.begin(), byDistance.end());
			int mouthTile = -1;
			for (size_t c = 0; c < byDistance.size() && c < 400 && mouthTile < 0; ++c)
			{
				const int i = byDistance[c].second;
				const double ox = t.offsetX(site.x, t.remainderX(i)), oy = t.offsetY(site.y, i / t.w);
				const double length = ::MapGeneration::Numeric::hypot(ox, oy);
				bool clear = true;
				for (double s = 0; s <= length && clear; s += 0.5)
					clear = !L.band[t.at(int(std::floor(site.x + 0.5 + ox * s / std::max(1.0, length))),
										 int(std::floor(site.y + 0.5 + oy * s / std::max(1.0, length))))];
				if (clear)
					mouthTile = i;
			}
			if (mouthTile < 0)
				continue;
			const ShapePoint mouth{site.x + 0.5 + t.offsetX(site.x, t.remainderX(mouthTile)),
								   site.y + 0.5 + t.offsetY(site.y, mouthTile / t.w)};
			strokePath(L.water, t,
					   wanderingPath(t, {site.x + 0.5, site.y + 0.5}, mouth, kRiverHalfWidth,
									 kRiverWander, 0.0, rivers));
		}
	}
	// No water on the rock, the passes or the clearings, none within kQuarryWaterClear of a quarry
	// (its beach would spoil the face); no sand or road on water.
	{
		const std::vector<unsigned char> dry = dilate(t, L.quarry, kQuarryWaterClear);
		for (int i = 0; i < n; ++i)
		{
			if (L.band[i] || L.pass[i] || dry[i] || L.clearing[i])
				L.water[i] = 0;
			if (L.water[i] || L.clearing[i])
				L.road[i] = L.sand[i] = 0;
		}
	}
	L.homeOf.assign(n, -1);
	const RadialShape home(L.homeRadius, 0.15, context, "rain-home");
	const RadialShape pond(homePondRadius(L.homeRadius), 0.3, context, "rain-pond");
	L.kits = stampRoundHomes(t, L.homes, 0.0, home, L.homeRadius, 1, &pond, L.water, L.homeOf);

	// Ambient quarry faces along both faces of every ridge, one every kQuarryPitch tiles of face at
	// the default `stone-amount`: spaced along the faces, clear of the passes, the homes, roads and
	// water.
	{
		std::vector<unsigned char> leeOpen(n, 0), windOpen(n, 0);
		for (int i = 0; i < n; ++i)
		{
			const bool open = !L.band[i] && !L.pass[i];
			leeOpen[i] = open && L.phase[i] < 32768;
			windOpen[i] = open && L.phase[i] >= 32768;
		}
		const std::vector<int> fromLee = stepsFrom(t, leeOpen), fromWind = stepsFrom(t, windOpen),
							   fromWater = stepsFrom(t, L.water);
		const std::vector<unsigned char> busy = dilate(t, [&]
		{
			std::vector<unsigned char> m(n, 0);
			for (int i = 0; i < n; ++i)
				m[i] = L.road[i] || L.pass[i] || L.clearing[i] || L.homeOf[i] >= 0;
			return m;
		}(), 2);
		std::vector<int> faces;
		for (int i = 0; i < n; ++i)
			if (L.band[i] && !L.quarry[i] && (fromLee[i] == 1 || fromWind[i] == 1))
				faces.push_back(i);
		const int wanted = int(scaledCount(std::int64_t(std::lround(faces.size() / kQuarryPitch)), o.stone));
		context.shuffle(faces.begin(), faces.end(), "rain-quarries");
		context.telemetry.measure("rain-shadow.quarries.wanted", wanted);
		std::vector<int> centres;
		std::vector<std::vector<int>> ambient;
		for (int k = 0; k < teams; ++k)
			centres.push_back(faceOf(k));
		// Faces keep this far apart (Chebyshev, so the two faces of one ridge count too), closer as
		// stone-amount rises: kQuarryPitch / 3 at the default, never less than a face's own width and a gap.
		const int spacing = std::max(2 * kAmbientQuarryHalf + 3, int(kQuarryPitch / 3 * 100 / std::max(50, o.stone)));
		for (int c : faces)
		{
			if (L.ambientQuarries >= wanted)
				break;
			bool clear = passOffsetAt(c, L.passShift) >= passHalf + alongUnits(kAmbientQuarryHalf + kQuarryPassClear);
			for (int other : centres)
				clear = clear && std::max(std::abs(t.offsetX(t.remainderX(c), t.remainderX(other))),
										  std::abs(t.offsetY(c / t.w, other / t.w))) >= spacing;
			if (!clear)
				continue;
			const std::vector<int> &from = fromLee[c] == 1 ? fromLee : fromWind;
			std::vector<int> tiles;
			for (int dy = -kAmbientQuarryHalf; dy <= kAmbientQuarryHalf && clear; ++dy)
				for (int dx = -kAmbientQuarryHalf; dx <= kAmbientQuarryHalf && clear; ++dx)
				{
					const int i = t.at(t.remainderX(c) + dx, c / t.w + dy);
					if (L.band[i] && !L.quarry[i] && from[i] >= 1 && from[i] <= kQuarryDepth)
					{
						tiles.push_back(i);
						clear = (fromWater[i] < 0 || fromWater[i] > kQuarryWaterClear) && !busy[i];
					}
				}
			if (!clear || tiles.size() < 3)
				continue;
			for (int i : tiles)
				L.quarry[i] = 1;
			centres.push_back(c);
			ambient.push_back(tiles);
			++L.ambientQuarries;
		}
		// A face cut into a thin or steep stretch takes the rock's corners round it with it; one that
		// opens a way across the ridge is given back to the rock, nearest the breach first.
		for (;;)
		{
			std::vector<unsigned char> rockTiles(n, 0);
			for (int i = 0; i < n; ++i)
				rockTiles[i] = L.band[i] && !L.quarry[i];
			L.rock = rockVertices(t, rockTiles);
			const int leak = ridgeLeak(t, L.phase, L.crest, L.pass, L.rockHalf, tiles16(3), tiles16(1),
									   [&](int i)
									   {
										   const int x = t.remainderX(i), y = i / t.w;
										   return !L.pass[i] && !L.quarry[i] &&
												  L.rock[i] + L.rock[t.at(x + 1, y)] + L.rock[t.at(x, y + 1)] +
														  L.rock[t.at(x + 1, y + 1)] <
													  2;
									   });
			if (leak < 0 || ambient.empty())
				break;
			size_t worst = 0;
			int nearestGap = INT_MAX;
			for (size_t a = 0; a < ambient.size(); ++a)
				for (int i : ambient[a])
				{
					const int gap = std::max(std::abs(t.offsetX(t.remainderX(leak), t.remainderX(i))),
											 std::abs(t.offsetY(leak / t.w, i / t.w)));
					if (gap < nearestGap)
					{
						nearestGap = gap;
						worst = a;
					}
				}
			for (int i : ambient[worst])
				L.quarry[i] = 0;
			ambient.erase(ambient.begin() + std::ptrdiff_t(worst));
			--L.ambientQuarries;
		}
		context.telemetry.measure("rain-shadow.quarries.ambient", L.ambientQuarries);
		int homeStone = 0;
		for (int i = 0; i < n; ++i)
			homeStone += L.quarryOf[i] == 0;
		context.telemetry.measure("rain-shadow.quarries.home-stone", homeStone);
	}

	// Talus and seeps. Scree skirts the lee foot of the rock (one tile, frayed to three) and frays
	// round the mouths of the passes: slow ground where nothing grows and nothing is built, and it does
	// not inhibit growth (so in front of the lee sand it costs nothing). The windward gap between rock
	// and streams is spring seeps, crop land that waters its surroundings a little. Never on a quarry
	// or its approach, a road, a pass floor, water or a home's clearing.
	{
		const std::vector<int> fray = periodicNoise(t.w, t.h, 5, context.stream("rain-talus"));
		const std::vector<unsigned char> nearQuarry = dilate(t, L.quarry, 1);
		std::vector<unsigned char> lee(n, 0), wind(n, 0), mouth(n, 0);
		for (int i = 0; i < n; ++i)
		{
			const bool base = !L.water[i] && !L.road[i] && !L.pass[i] && !L.band[i] && !L.clearing[i] &&
							  L.homeOf[i] < 0 && !nearQuarry[i];
			lee[i] = base && L.phase[i] < 32768;
			wind[i] = base && L.phase[i] >= 32768;
			mouth[i] = base;
		}
		L.scree = talusApron(t, L.band, lee, fray, kLeeTalus, kLeeTalusFray, kTalusCut);
		const std::vector<unsigned char> mouths = talusApron(t, L.pass, mouth, fray, 2, kMouthTalus, kMouthTalusCut);
		L.seep.assign(n, 0);
		for (int i = 0; i < n; ++i)
		{
			L.scree[i] = L.scree[i] || mouths[i];
			L.seep[i] = wind[i] && !L.scree[i] && L.crest[i] < footNear;
		}
	}
	context.telemetry.measure("rain-shadow.passes.per-turn", passes);
	context.telemetry.measure("rain-shadow.passes.closed", L.droppedPasses);
	context.telemetry.measure("rain-shadow.lakes.actual", L.lakes.size());
	return L;
}

bool generate(Game &game, GenerationContext &context)
{
	context.stage = "rain shadow layout";
	const RainShadowOptions o(context.request);
	const Layout L = design(context.request, context);
	if (!L.failure.empty())
	{
		context.telemetry.fallback("rain-shadow.layout.failure", L.failure);
		context.detail = L.failure;
		return false;
	}
	Map &map = game.map;
	const Torus &t = L.t;
	const int n = t.size(), teams = context.request.nbTeams;
	for (int k = 0; k < teams; ++k)
		game.addTeam();

	// The mechanics: one canonical type per rule group, which every shared helper reads.
	context.stage = "rain shadow terrain";
	const std::vector<unsigned char> approach = dilate(t, L.quarry, 1), passFloor = tileCorners(t, L.pass);
	TerrainSketch terrain(n, GRASS);
	for (int i = 0; i < n; ++i)
	{
		if (L.water[i])
			terrain[i] = WATER;
		else if (L.rock[i])
			terrain[i] = RIDGE_ROCK;
		else if (approach[i])
			continue; // grass, so every quarry tile has four grass corners and holds its stone
		else if (passFloor[i] || L.road[i])
			terrain[i] = SAND;
		else if (L.scree[i])
			terrain[i] = SCREE;
		else if (L.sand[i])
			terrain[i] = SAND;
		else if (L.seep[i])
			terrain[i] = LOAM;
	}
	// Sand patches over the valleys' grass (first play: "some inland desert or sporadic sand patches
	// to break up the texture"): `sand-patches` percent of the grass at least three tiles from any
	// water, in patches from a noise field, kept off the homes, the rock, the roads and the quarries.
	std::vector<unsigned char> patched(n, 0);
	{
		std::vector<unsigned char> valley(n, 0);
		const std::vector<unsigned char> nearQuarry = dilate(t, L.quarry, 2);
		for (int i = 0; i < n; ++i)
			valley[i] = !L.band[i] && !L.road[i] && !L.sand[i] && L.homeOf[i] < 0 && !L.pass[i] &&
						!L.scree[i] && !L.seep[i] && !nearQuarry[i];
		const std::vector<int> patches = periodicNoise(t.w, t.h, 14, context.stream("rain-patches"));
		const TerrainSketch before = terrain;
		sprinkleSand(terrain, t, valley, o.sandPatches / 100.0, 3, [&](int i) { return patches[i]; });
		for (int i = 0; i < n; ++i)
			patched[i] = terrain[i] == SAND && before[i] != SAND;
	}
	layBeaches(terrain, t);
	// A beach never eats the rock: where the sway squeezes a stream or a river against a ridge, the
	// rock runs into the water.
	for (int i = 0; i < n; ++i)
		if (L.rock[i])
			terrain[i] = RIDGE_ROCK;
	// The looks, inside each vertex's rule group so nothing plays differently: the climate's grass lush
	// by the water and drying away from it, the lee sand plain at the scree, then dunes and wind-rippled
	// crests downwind, seeps wet by the streams, and the lithology's rock and talus. The home clearings
	// and a frayed garden round them stay plain grass, where the starting-crop guarantee looks for it,
	// and so do the quarries' approaches.
	{
		std::vector<unsigned char> wet(n, 0);
		for (int i = 0; i < n; ++i)
			wet[i] = terrain[i] == WATER;
		const std::vector<int> nearWater = stepsFrom(t, wet);
		const std::vector<int> dry = fractalNoise(t.w, t.h, 32, 3, context.stream("rain-dry"));
		const int dryCut = percentile(dry, L.climate.dryPercent);
		const std::vector<int> crests = periodicNoise(t.w, t.h, 4, context.stream("rain-dunes"));
		const std::vector<int> fray = periodicNoise(t.w, t.h, 8, context.stream("rain-garden"));
		const auto garden = [&](int i)
		{
			const double reach = L.homeRadius + kHomeMargin + kGarden + kGardenFray * fray[i] / 65536.0;
			for (const ShapePoint &home : L.homes)
				if (t.dist2(int(home.x), int(home.y), t.remainderX(i), i / t.w) <= reach * reach)
					return true;
			return false;
		};
		const int o2 = std::max(1, RainShadowOptions(context.request).leeWidth);
		TerrainSketch looks(n, 0);
		for (int i = 0; i < n; ++i)
		{
			switch (terrain[i])
			{
			case GRASS:
				if (approach[i] || garden(i))
					break;
				if (nearWater[i] >= 0 && nearWater[i] <= L.climate.lushReach)
					looks[i] = LUSH_GRASS;
				else if (nearWater[i] >= 0 && nearWater[i] <= L.climate.greenReach + 2 * fray[i] / 65536)
					break;
				else if (dry[i] <= dryCut)
					looks[i] = DRY_GRASS;
				break;
			case SAND:
				if (nearWater[i] >= 0 && nearWater[i] <= 1)
					looks[i] = L.climate.beach;
				else if (patched[i])
					looks[i] = L.climate.patch;
				else if (L.sand[i] && !L.road[i] && !passFloor[i] && L.shadow[i] >= 1)
				{
					// Downwind from the scree: plain sand, then dunes, then wind-rippled crests.
					const int k = L.shadow[i];
					looks[i] = k <= 2 ? L.climate.nearSand
							   : (k > (o2 + 1) / 2 && crests[i] > 36000) ? L.climate.crest
																		 : L.climate.dune;
				}
				break;
			case LOAM:
				looks[i] = nearWater[i] >= 0 && nearWater[i] <= 2 ? L.climate.seepWet : L.climate.seepDry;
				break;
			case RIDGE_ROCK:
				looks[i] = L.lithology.rock;
				break;
			case SCREE:
				looks[i] = L.lithology.talus;
				break;
			default:
				break;
			}
		}
		writeVertices(map, composeLooks(terrain, looks));
	}
	for (int i = 0; i < n; ++i)
		if (L.quarry[i] && map.isResourceAllowed(t.remainderX(i), i / t.w, STONE))
			map.setResourceByIndex(t.remainderX(i), i / t.w, STONE, 1);

	context.stage = "rain shadow colonies";
	if (!settleRoundColonies(game, context, "rain-starts", L.homeOf, L.homes, L.homeRadius))
		return false;

	context.stage = "rain shadow resources";
	const std::vector<unsigned char> reserved = swarmSurroundings(t, context);
	for (int k = 0; k < teams; ++k)
		plantHomeKit(
			map, t, context, L.kits[k], 0.0, L.homeRadius, kHomeWheat, kHomeWood,
			[&](int i)
			{ return L.homeOf[i] == k && !reserved[i] && clearGround(map, t.remainderX(i), i / t.w); });
	// The fields: crops on the fertile ground, which the streams make the windward side of every valley
	// (the growth probe reaches 15 tiles from water and refuses beside sand, so the lee band grows
	// nothing). A third of the fertile ground under wheat and a fifth under wood is a working
	// countryside, in patches so it can be walked and built on; outcrops are few, the quarry faces
	// being stone enough.
	const Fertility::Field fertility = Fertility::forMap(map, false);
	const std::vector<int> patch = periodicNoise(t.w, t.h, 10, context.stream("rain-patch"));
	const std::vector<int> split = periodicNoise(t.w, t.h, 5, context.stream("rain-split"));
	const std::vector<unsigned char> nearQuarry = dilate(t, L.quarry, 1);
	const auto eligible = [&](int i)
	{
		return L.homeOf[i] < 0 && !reserved[i] && !L.band[i] && !nearQuarry[i] &&
			   clearGround(map, t.remainderX(i), i / t.w);
	};
	int fertile = 0;
	for (int i = 0; i < n; ++i)
		fertile += eligible(i) && fertility.at(t.remainderX(i), i / t.w) > 0;
	furnishGround(
		map, t, context, fertility, eligible, [&](int i) { return float(patch[i]); },
		[&](int i) { return split[i]; },
		[&](int area)
		{
			return GroundAmounts{int(scaledCount(fertile * 30 / 100, o.wheat)),
								 int(scaledCount(fertile * 20 / 100, o.wood)),
								 int(scaledCount(area / 3000, o.stone)),
								 int(scaledCount(area / 2000, o.fruit))};
		},
		"rain-stone", "rain-fruit");
	seedAlgae(map, context, t, "rain-algae", o.algae, AlgaeBand::anyWater(30));
	secureStartingCrops(game, context, t, 24, 32, 0, &L.band);
	reopenCrampedStarts(game, context, {o.wheat, o.wood, o.stone, o.algae, o.fruit}, 24, 32, 0, &L.band);
	// The passes join every valley, but the fields can close one; the cheapest cut through crops is
	// opened, never through rock, a quarry or water.
	context.stage = "rain shadow routes";
	openColonyRoutes(map, context, t, StepCosts{1, 3, -1, -1, -1}, 0, &L.band);
	return true;
}

std::string validateWorld(const Game &game, const GenerationContext &context)
{
	GenerationContext replay(context.request);
	const Layout L = design(context.request, replay);
	const Map &map = game.map;
	if (const std::string mismatch = designMismatch(L, map, "rain shadow"); !mismatch.empty())
		return mismatch;
	const Torus &t = L.t;
	const int teams = context.request.nbTeams;
	const auto at = [&](int i) { return std::to_string(t.remainderX(i)) + ", " + std::to_string(i / t.w); };
	if (const std::string lost = homePondMissing(map, t, L.kits, teams, "home", "pond"); !lost.empty())
		return lost;
	// Rock where the design put rock: the ridges are the map's walls.
	for (int i = 0; i < t.size(); ++i)
		if (L.rock[i] && terrainGroup(map.vertexTerrainAt(t.remainderX(i), i / t.w)) != TerrainGroup::Ridges)
			return "A ridge has lost its rock at (" + at(i) + ").";
	// Every quarry face holds its stone, and every home's quarry holds the same stone.
	if (const std::string fault = quarryFault(map, t, L.quarry, L.quarryOf, teams, kHomeQuarryStone);
		!fault.empty())
		return fault;
	// The ridges are walls: with every pass shut, nothing walks from one face of a ridge to the other.
	const int leak = ridgeLeak(t, L.phase, L.crest, L.pass, L.rockHalf, 3 * 16, 16,
							   [&](int i)
							   {
								   return !L.pass[i] && map.terrainPropertiesAt(t.remainderX(i), i / t.w).walkable &&
										  !permanentResourceBarrier(map, size_t(i));
							   });
	if (leak >= 0)
		return "A ridge can be crossed outside its passes at (" + at(leak) + ").";
	return walkFromFirstColony(map, teams, "the valleys", "through the passes").error;
}
} // namespace

RainShadowOptions::RainShadowOptions(const GenerationRequest &r)
	: ridges(r.option("ridges")), slant(r.option("slant")),
	  ridgeThickness(r.option("ridge-thickness")), passSpacing(r.option("pass-spacing")),
	  passWidth(r.option("pass-width")), leeWidth(r.option("lee-width")),
	  homeSize(r.option("home-size")), sandPatches(r.option("sand-patches")),
	  inlandLakes(r.option("inland-lakes") != 0), climate(r.option("climate")),
	  lithology(r.option("lithology")), wheat(r.option("wheat-amount")),
	  wood(r.option("wood-amount")), stone(r.option("stone-amount")),
	  algae(r.option("algae-amount")), fruit(r.option("fruit-amount"))
{
}

GeneratorDefinition rainShadowDefinition()
{
	return {
		"rain-shadow",
		27,
		"Rain shadow",
		7,
		false,
		// Four ridges on a 256 map give 64-tile valleys: a 12-tile home, a pass every 48 tiles
		// and a lee band of 6 leave a valley wide enough to farm and to fight in. Ridges three
		// thick are thin enough for a level-1 tower to shoot across (Walls.h's towerReach; the rock
		// is drawn a tile thicker and blocks about a third of a tile more than the stone did);
		// passes five wide take a column of units and can be walled shut by whoever holds them.
		{GeneratorControl{"ridges", "Ridges", 2, 8, 1, 4, ControlGroup::Terrain}.withSearchRange(3,
																								 6),
		 GeneratorControl{"slant", "Slant", 0, 3, 1, 1, ControlGroup::Terrain}.withSearchRange(0,
																							   3),
		 GeneratorControl{"ridge-thickness", "Ridge thickness", 2, 6, 1, 3, ControlGroup::Terrain}
			 .withSearchRange(2, 4),
		 GeneratorControl{"pass-spacing", "Pass spacing", 24, 96, 8, 48, ControlGroup::Terrain}
			 .withSearchRange(32, 64),
		 GeneratorControl{"pass-width", "Pass width", 3, 9, 1, 5, ControlGroup::Terrain}
			 .withSearchRange(5, 8),
		 GeneratorControl{"lee-width", "Lee width", 2, 12, 1, 6, ControlGroup::Terrain}
			 .withSearchRange(4, 8),
		 GeneratorControl{"home-size", "Home size", 10, 20, 1, 12, ControlGroup::Layout}
			 .withSearchRange(12, 18),
		 // FEEDBACK 2026-09-13: lakes with rivers in every valley, and 6% of the valleys' grass in
		 // sand patches.
		 GeneratorControl::toggle("inland-lakes", "Inland lakes", true, ControlGroup::Terrain)
			 .withSearchValues({0, 1}),
		 GeneratorControl{"sand-patches", "Sand patches", 0, 20, 2, 6, ControlGroup::Terrain}
			 .withSearchRange(2, 10),
		 // Looks only: every climate and every rock plays exactly alike.
		 GeneratorControl::choice("climate", "Climate", {"Random", "Temperate", "Steppe", "Desert"}, 0,
								  ControlGroup::Terrain)
			 .withSearchValues({1, 2, 3}),
		 GeneratorControl::choice("lithology", "Rock", {"Random", "Granite", "Sandstone", "Volcanic"}, 0,
								  ControlGroup::Terrain)
			 .withSearchValues({1, 2, 3}),
		 GeneratorControl::percentage("wheat-amount", "Wheat amount"),
		 GeneratorControl::percentage("wood-amount", "Wood amount"),
		 GeneratorControl::percentage("stone-amount", "Stone amount"),
		 GeneratorControl::percentage("algae-amount", "Algae amount"),
		 GeneratorControl::percentage("fruit-amount", "Fruit amount")},
		generate,
		true,
		designFailure<design>,
		validateWorld,
		// Identical ridge-and-valley bands repeat in parallel across the map, same mechanism
		// as Bajada's fans.
		{"terrain:natural", "feature:mountains", "feature:river", "feature:desert",
		 "style:wide-open", "fairness:stamped-lattice"}};
}
