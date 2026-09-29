// SPDX-License-Identifier: GPL-3.0-or-later
#include "BajadaGenerator.h"
#include "Drawing.h"
#include "FertilityField.h"
#include "Game.h"
#include "GenerationContext.h"
#include "Geometry.h"
#include "Grid.h"
#include "Growth.h"
#include "Homes.h"
#include "LatticeNoise.h"
#include "Morphology.h"
#include "Orbits.h"
#include "Patterns.h"
#include "Pipeline.h"
#include "Planting.h"
#include "Resources.h"
#include "Roads.h"
#include "Settlements.h"
#include "Sketch.h"
#include "Walls.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>
using namespace MapGeneration;

// Bajada: the desert's foot of a mountain range. Where a dry range meets its basin, every canyon
// that drains it drops its gravel in a fan, and the streams that leave the canyon mouth do not join
// as they run downhill, they split: a fan is a river tree turned inside out, one spring at the apex
// and a spray of ever thinner distributaries that die out in the sand. Neighbouring fans grow into
// one another along the range (the bajada), and what water reaches the bottom collects in the
// playa, a chain of shallow lakes in the middle of the basin with wide white rims of salt.
//
// The map is bands that wrap the torus: range, bajada, playa, bajada, range. Long stone ranges
// cross the map from side to side, each cut by a few passes (a sand floor between stone shoulders).
// Below both faces of every range hangs a row of fans; each is a spring at the range's foot, a tree
// of streams grown from it, and green ground along the streams that is wide near the apex and
// narrows downhill into fingers, edged with a fraying line of sand and ending in dry sand washes.
// Between and below the fans lies the desert: a belt of gravel along the foot of the ranges (grass,
// buildable, farmable only where a fan's water reaches), giving way downhill to dune sand in bands
// along the contour, with rock outcrops and lone scrub on the dry gravel. Some fans are dry, only
// washes of sand across the gravel. The playa at the bottom is a chain of lakes of different
// lengths, each ringed by a salt flat of sand and a meadow beyond it, with salt-flat crossings
// between the lakes.
//
// WHY IT PLAYS WELL (docs/map-generators/GAME_RULES_FOR_MAP_DESIGN.md). Every colony starts beside a
// fan of its own: a town on the dry gravel of the fan's shoulder, sealed from crops by a ring of sand
// corners, a garden with its kit and a pond between the ring and the streams, and the range's stone a
// short walk away. Crops grow only where the
// growth probe finds water, so a fan's streams are its farm; the gravel beside the fans is building
// room, which crops reach only near water. A colony expands along its range onto neutral fans
// (fertile, contested with its neighbours) and down the slope to the playa, whose meadows are the
// only wet ground away from the ranges and face the fans of the other side of the basin. The ranges
// are permanent walls (stone is never cleared), so a colony's back is safe except through a pass:
// fewer passes give a quiet rear, more give raids from the basin behind. Swimming turns the playa's
// lakes from obstacles into short cuts.
//
// Fairness is by construction for the home. One home fan (springs, streams, town, ring, fringe,
// and its kit and share of farmland) is designed once in its own frame and copied tile for
// tile to every colony; whatever a neighbouring fan, a lake or the desert would have drawn inside a
// home's footprint is erased first. With more than one range, all homes hang evenly from the same
// face of their ranges (every home a translation of the others); a single range's one basin takes
// homes on both faces, half a turn apart, as do maps whose faces cannot hold them. The range may
// bend away from a home but never into it. What lies beside a home (which neutral fans, how near a
// pass, how far to a lake) is where it fell; validateWorld checks the copies are identical and still
// sown alike, and refuses homes crowded into one another's footprints.
//
// REVIEW 2026-09-17 (round 1, independent reviewer): "almost no buildable ground; the buildable
// desert is sand", "homes are not identical" (lakes and neighbouring halos reached into homes, the
// swarm settled beside a track, facings mixed), "a 48-gon ring reads as a bullseye", "home fans have
// no downhill length", "playa lozenges with no white rim", "dry fans are ghost outlines", "lollipop
// fans", "ranges are a flat grey bar", "range spacing has two outcomes". So: the desert became
// gravel; the home stencil is stamped last over an erased footprint; homes take one face; towns are
// rough outlines; fans are budgeted against the lake's widest lobe; salt rims are lobed; dry fans
// have no halo; small fans have no pond; passes have stone shoulders.
//
// REVIEW 2026-09-17 (round 2): "it no longer reads as a desert, ponds in a green park", "every
// fan is outlined like a sticker", "ruler-straight ranges across the home footprints", "the home fan
// is a comma along the foot of the range", "one range puts every home in a row in an empty basin",
// "playa above 60 is always refused", rare ridge breaches with thin ridges. So: sand gathers in
// bands towards the playa (gravel only along the ranges); the fringe frays from one to three corners;
// ranges sway away from homes instead of running straight; towns sit higher with their tracks
// running down to the toe and the streams running down the fan; one range takes homes on both faces;
// the playa narrows to fit instead of refusing; springs sit below the range's edge; washes only run
// downhill; the kit is part of the stencil; crowded homes are refused.
//
// REVIEW 2026-09-17 (round 3): "the Shoulder home is a glyph, a letter b repeated in a row", "towns
// hold about one building", "playa above the default is dead", "lakes are smooth lozenges", "washes
// are straight fence posts", "tracks are stubs", "no neutral fans between close homes", "a neutral
// fan's water within a short walk of one town makes the fairness tail". So: every home is now a
// real fan (Broad fan, Long fan or Twin springs) with a town of radius 11 on its dry shoulder and no
// track; fans are sized for the widest playa, so the control only
// moves the lakes; lakes swell into two or three basins and their meadows break; washes bend and
// fork; neutral fans stand wherever their apex clears a home's footprint, and their streams keep 26
// tiles from every town; cramped-start relief never takes a home's own crops. The partial ring left a
// town's open side exposed to a neighbour's water, which the validator caught, so the ring is whole.
//
// REVIEW 2026-09-17 (rounds 4 and 5): "block the merge: the kit and water lie 12 to 16 tiles from the
// swarm, and every AI is food-capped on seed 202", then "Nicowar stalls on seed 404: the garden seven
// tiles from water regrows too slowly". So: the swarm sits on the fan's side of the town, the kit's
// wheat in a garden between the ring and the streams (kept kTownWater off the town), and a garden pond
// below it; close homes get smaller towns so their fans keep room to spread (a study of 516 maps
// found home trees of one branch at 64 tiles apart).
namespace
{

// Starting kit on every home fan, unscaled whatever the amounts say: wheat on the bank between the
// spring and the town, wood on a bank beside the town. The range behind every home is its quarry.
constexpr int kHomeWheat = 24, kHomeWood = 14;
// The smallest fan a home fits: the town, its ring and the streams round it.
constexpr double kSmallestHomeFan = 30;
// Home towns at least this far apart, or the request is refused: two towns with their farm streams
// between them.
constexpr double kHomeSeparation = 42;
// A fan's apex lies this many tiles out from the ridge's nominal edge.
constexpr double kFootGap = 1;
// The ridge sways across the map by up to this many tiles, in noise cells this long, except round
// the homes, where it runs straight for kFlatReach tiles past either side of the home stencil and eases back to
// its sway over kFlatRamp more, so the stretch of range copied with every home is the same.
constexpr double kRidgeSway = 9, kRidgeSwayCell = 56, kFlatReach = 3, kFlatRamp = 16, kAwaySway = 0.35;
// The ridge thickens into massifs and thins between them by up to this share of its width, in cells
// this long, and its edge is rough by up to kRidgeRough tiles in cells kRidgeRoughCell long (neither
// round a home). The thinnest stretch is still a wall; validateWorld proves it.
constexpr double kMassif = 0.4, kMassifCell = 36, kRidgeRough = 2.5, kRidgeRoughCell = 5;
// However thin a stretch of range, and whatever notch a spring cuts, stone stands within this many
// tiles of its centre line except in a pass.
constexpr double kRidgeCore = 3.5;
// Spurs off the ridges' faces: one every kSpurGapLow to kSpurGapHigh tiles, kSpurLow to kSpurHigh
// long and kSpurBaseLow to kSpurBaseHigh tiles half wide at the root.
constexpr double kSpurGapLow = 14, kSpurGapHigh = 40, kSpurLow = 5, kSpurHigh = 16;
constexpr double kSpurBaseLow = 3, kSpurBaseHigh = 5.5;
// Passes: a sand floor this half width (in tiles) through the stone, the stone kept this much wider,
// running this many tiles past the ridge's edge on both sides, skewed along the ridge by up to
// kPassSkew. The ridge swells by kPassShoulder tiles either side of a pass over kPassShoulderReach
// tiles along it, so a pass reads as a gorge between shoulders. No neutral fan's apex lies within
// kPassFanClear of a pass mouth.
constexpr double kPassHalfWidth = 1.6, kPassStoneClear = 3.0, kPassOverrun = 1, kPassSkew = 8;
constexpr double kPassShoulder = 4, kPassShoulderReach = 12, kPassFanClear = 12;
// The playa: its lakes are at most this share of the basin wide at a `playa` of 100 (the smaller of
// the requested spacing and the one the map holds), their shores wobbling out to kLakeLobe times
// that; laid along the basin with lengths of kLakeLengthLow to kLakeLengthHigh times kLakePitch and
// salt-flat crossings kCrossingLow to kCrossingHigh wide between them, shifted across the basin by
// up to kLakeShift; lakes shorter than kShortestLake are left out.
constexpr double kLakeShare = 0.18, kLakeLobe = 1.35, kLakePitch = 72;
constexpr double kLakeLengthLow = 0.3, kLakeLengthHigh = 1.1, kCrossingLow = 6, kCrossingHigh = 24;
constexpr double kShortestLake = 12, kLakeShift = 3, kLakeLobeCell = 10;
// Lakes swell and pinch by this share of their width into two or three basins, and the meadow round
// them breaks where a noise field falls below kMeadowGap.
constexpr double kLakeBasins = 0.3, kMeadowGap = 0.3;
// A salt flat kSaltRim tiles wide, give or take kSaltLobe, rings every lake, and a meadow kMeadow
// tiles wide lies beyond it; the playa's line sways by kPlayaSway in cells kPlayaSwayCell long.
constexpr double kSaltRim = 4, kSaltLobe = 1.5, kMeadow = 5, kPlayaSway = 2, kPlayaSwayCell = 64;
// A fan's toes stop this far short of the playa's reach.
constexpr double kToeGap = 1;
// A playa narrowed to fit leaves the home fans this many tiles over their least length.
constexpr double kLakeSlack = 4;
// Fans never run longer than this; a wider basin gets a wider floor of desert below them.
constexpr double kLongestFan = 60, kFanShare = 0.6;
// The green ground along a fan's streams: this many tiles either side of the water at the apex,
// narrowing to kGreenToe of that at the fan's toe, lined with a fringe of sand corners this many
// tiles wide.
constexpr double kGreenHead = 11, kGreenToe = 0.35, kHaloWobble = 0.3;
// Homes: a stencil's footprint is everything it draws, widened by kFootprintMargin tiles; the town's
// outline is rough by kTownRoughness, its green reaches kTownGreen tiles past the ring.
constexpr int kFootprintMargin = 4;
// Neighbouring homes may share at most this share of a home's footprint (their fans merge there);
// more and the map is too crowded for the homes to be homes.
constexpr int kSharedPercent = 20;
// A home may lose this many of its stencil's crops to routes and swarm clearance and still count as
// sown like the others.
constexpr int kCropsLost = 12;
constexpr double kTownRoughness = 0.2, kTownGreen = 2;
// A home's streams keep this far from its town's edge: the garden between them.
constexpr double kTownWater = 7;
// The garden pond: this radius, this many tiles out from the town's ring, its centre this far below the
// town's middle for every tile across.
constexpr double kGardenPond = 3.2, kGardenPondGap = 4, kGardenPondDrop = 1.3;
// Homes kRoomyGap or more apart get full-sized towns; closer homes' towns shrink, to kCrowdedTown.
constexpr double kRoomyGap = 96, kCrowdedTown = 0.72;
// Home stream trees grown to choose the fullest from.
constexpr int kHomeTreeAttempts = 4;
// Neutral fans: this share of slots is left as open desert, this share of the rest is dry (washes of
// sand, no spring), and their apexes jitter along the ridge by this share of the slot spacing. A fan
// whose streams took fewer than kPondBranches branches has no spring pond.
constexpr int kEmptyFanPercent = 12, kDryFanPercent = 25, kPondBranches = 4;
constexpr double kFanJitter = 0.18;
constexpr int kNeutralTownClear = 26;
// A dry wash continues past every leaf of a fan's streams for this many tiles.
constexpr double kWashLow = 6, kWashHigh = 14;
// Desert: dune sand covers this share of the open desert, in bands kDuneWavelength apart along the
// contour (a Turing pattern stretched kDuneStretch percent along the ranges) broken by patches of
// fractal noise kDuneCell tiles across, gathering towards the playa: gravel lies at the foot of the
// ranges and sand in the basin, where the finest grains settle. kDuneSlope weighs the slope.
constexpr int kDuneCell = 24, kDuneSlope = 64000, kDuneWavelength = 16, kDuneStretch = 320;
constexpr int kDunePercent = 48;
// Ambient farmland on the fans and meadows (share of fertile tiles) and desert features (one per
// this many desert tiles), before the amounts scale them.
constexpr int kFarmWheatPercent = 26, kFarmWoodPercent = 9;
constexpr int kDesertPerOutcrop = 400, kDesertPerScrub = 110, kMeadowPerGrove = 350;
constexpr int kWaterPerAlgae = 400;

// A stencil's local frame: vertex (dx, dy) from the apex, dy running downhill.
constexpr int kStencilHalf = 128;

double unit(std::mt19937 &random)
{
	return (random() >> 5) * (1.0 / 134217728.0);
}

// A home fan design, in the stencil's frame (x across, y downhill from the apex): its springs, its
// town and its swarm, the streams, and where the kit's crops seed.
struct Root
{
	double x, y, turn, length, halfWidth;
	int forks;
};
struct HomeDesign
{
	const char *name;
	std::vector<std::array<double, 3>> springs; // x, y, radius
	ShapePoint town;
	double townRadius;
	ShapePoint swarm; // the swarm's middle tile, on the fan's side of the town
	std::vector<Root> roots;
	ShapePoint wheat, wood;
};

// Every home is a fan with its town beside it, on the dry gravel of the fan's shoulder, in the frame
// of the home's slot (x across, y downhill from the range's foot). Broad fan: the spring's streams
// split early and spread wide. Long fan: one trunk runs straight down the fan and forks as it goes.
// Twin springs: two springs a little apart, each with its own fan, the two merging downhill. Towns
// lie left of the fan unless the map flips them. Between the town's ring and the fan's streams lies a
// garden, kTownWater tiles of grass watered by the streams beside it, where the kit's wheat grows a
// short walk from the swarm (REVIEW round 4: the kit and the water lay 12 to 16 tiles out, beyond
// the streams' beaches, and every AI was food-capped).
HomeDesign homeDesign(int design, bool flip)
{
	HomeDesign d;
	if (design == 0)
		d = {"broad fan",  {{7, 5.5, 4.5}}, {-15, 16}, 11, {-9, 16},
			 {{6, 9.5, 0.05, 9, 1.6, 4}, {10, 9.5, 0.7, 9, 1.5, 3}}, {0, 17}, {-1, 29}};
	else if (design == 1)
		d = {"long fan", {{7, 5.5, 4.5}}, {-15, 15}, 11, {-9, 15}, {{7, 10, 0.1, 11, 1.8, 5}}, {0, 16}, {0, 28}};
	else
		d = {"twin springs",
			 {{1, 5, 3.5}, {13, 5, 3.5}},
			 {-17, 16},
			 11,
			 {-11, 16},
			 {{1, 8.5, 0.05, 9, 1.5, 4}, {13, 8.5, 0.4, 9, 1.5, 4}},
			 {-2, 17},
			 {-3, 29}};
	if (flip)
	{
		for (ShapePoint *p : {&d.town, &d.swarm, &d.wheat, &d.wood})
			p->x = -p->x;
		for (auto &s : d.springs)
			s[0] = -s[0];
		for (Root &r : d.roots)
		{
			r.x = -r.x;
			r.turn = -r.turn;
		}
	}
	return d;
}
constexpr int kHomeDesigns = 3;

struct Fan
{
	int x, y;   // apex vertex
	int side;   // +1 downhill runs +y, -1 downhill runs -y
	int ridge;  // the range it hangs from
	int colony; // -1 for a neutral fan
	bool dry = false;
};

// A tile of the home stencil, as an offset from the apex in the stencil's frame, with what it holds.
struct StencilTile
{
	int dx, dy;
	unsigned char water, sand, green, fringe, town, farm; // vertex masks, then tile masks
	unsigned char crop;                                    // 0 none, 1 wheat, 2 wood
	unsigned char inner; // far enough inside the footprint that no neighbour's corner reaches it
};

struct Layout
{
	Torus t{1, 1};
	int ranges = 0;
	double spacing = 0, halfRidge = 0, lakeHalf = 0, fanLength = 0, homeGap = 0, homeHalfWidth = 0;
	int homeShift = 0; // tiles the stencil's frame moves across so the home is centred on its slot
	int base = 0;
	HomeDesign home;
	std::vector<std::vector<double>> ridgeCentre; // per range, per column
	// Per face (0 the face above a range, 1 below), per range, per column: 0 round a home on that
	// face, rising to 1 away from every home.
	std::array<std::vector<std::vector<double>>, 2> ridgeWild;
	std::vector<Fan> fans;                        // the homes first, in colony order
	std::vector<StencilTile> stencil;             // every tile of a home's footprint
	std::vector<ShapePoint> kits;                 // per colony: its first spring, in map coordinates
	std::vector<MapGeneratorPoint> swarms;                        // per colony
	std::vector<int> facing;                                      // per colony
	std::vector<int> townOf, farmOf, homeOf; // per tile: town, home farm, home footprint, or -1
	std::vector<unsigned char> water, sand, green, meadow, stone, ridge, fringe, pass;
	int passes = 0, lakes = 0, dryFans = 0, neutralFans = 0, emptyTrees = 0;
	std::string failure;
};

// Signed offset from a centre line in rows, wrapped into [-h/2, h/2).
double wrappedOffset(double y, double centre, int h)
{
	double d = std::fmod(y - centre, double(h));
	if (d < -h / 2.0)
		d += h;
	if (d >= h / 2.0)
		d -= h;
	return d;
}

double smoothstep(double v)
{
	v = std::clamp(v, 0.0, 1.0);
	return v * v * (3 - 2 * v);
}

// Strokes a stream tree: its water and its green halo, or for a dry fan a wash of sand, and a dry
// wash past every leaf. `u(p)` is how far downhill a point lies from the apex, for the halo's taper.
template <typename Downhill>
void strokeTree(const Torus &t, const std::vector<Branch> &tree, double fanLength, double reach,
				bool dry, std::mt19937 &random, Downhill u, std::vector<unsigned char> &water,
				std::vector<unsigned char> &sand, std::vector<unsigned char> &green)
{
	for (const Branch &b : tree)
	{
		if (dry)
		{
			std::vector<StrokePoint> wash = b.path;
			for (StrokePoint &p : wash)
				p.halfWidth = std::min(p.halfWidth, 1.1);
			strokePath(sand, t, wash);
		}
		else
		{
			// The halo's edge swells and narrows along the branch, so a fan's outline wanders.
			std::vector<StrokePoint> halo = b.path;
			const double phase = 2 * kPi * unit(random), wavelength = 7 + 6 * unit(random);
			for (size_t k = 0; k < halo.size(); ++k)
			{
				StrokePoint &p = halo[k];
				const double along = std::clamp(u(p) / fanLength, 0.0, 1.0);
				p.halfWidth += kGreenHead * reach * (1 - (1 - kGreenToe) * along) *
							   (1 + kHaloWobble * std::sin(phase + 2 * kPi * k / wavelength));
			}
			strokePath(green, t, halo);
			strokePath(water, t, b.path);
		}
		if (b.leaf && b.path.size() >= 2)
		{
			const StrokePoint &tip = b.path.back();
			const double length = kWashLow + (kWashHigh - kWashLow) * unit(random);
			const double bend = (2 * unit(random) - 1) * 0.2;
			const std::vector<StrokePoint> wash = bentPath({tip.x, tip.y}, b.heading, length, bend * 2.5, 1, 1, 8);
			// A wash only ever runs downhill, away from the range, and forks once near its end.
			if (u(wash.back()) > u(tip))
			{
				tracePath(sand, t, wash);
				const StrokePoint &fork = wash[wash.size() * 2 / 3];
				const double turn = (unit(random) < 0.5 ? -1 : 1) * (0.4 + 0.3 * unit(random));
				const std::vector<StrokePoint> twig =
					bentPath({fork.x, fork.y}, b.heading + turn, length * 0.45, -bend * 2, 1, 1, 5);
				if (u(twig.back()) > u(fork))
					tracePath(sand, t, twig);
			}
		}
	}
}

// The sand edge round green ground: one corner everywhere, a second and a third where `noise` (0 to
// 65535) is high, so the edge frays rather than drawing an outline.
std::vector<unsigned char> noisyFringe(const Torus &t, const std::vector<unsigned char> &green,
									   const std::vector<int> &noise)
{
	const std::vector<unsigned char> one = dilate(t, green, 1), two = dilate(t, green, 2),
									 three = dilate(t, green, 3);
	std::vector<unsigned char> fringe(green.size(), 0);
	for (size_t i = 0; i < green.size(); ++i)
		fringe[i] = !green[i] && (one[i] || (two[i] && noise[i] > 26000) || (three[i] && noise[i] > 46000));
	return fringe;
}

struct Slot
{
	int x, y, side, ridge;
};

// Stage 1: bands, budget, design and home slots. Everything that can refuse a request.
bool planHomes(const BajadaOptions &o, int teams, GenerationContext &context, Layout &L,
			   std::vector<Slot> &slots, std::vector<Slot> &homes, double &slot)
{
	const Torus &t = L.t;
	const double reach = o.streamReach / 100.0;
	// Bands: as many whole ranges as fit one `range-spacing` apart. The lake and the fans are sized
	// from the smaller of the requested spacing and the spacing the map holds, so a wider spacing that
	// adds no range still lengthens the fans and widens the basin floor below them.
	L.ranges = std::max(1, t.h / o.rangeSpacing);
	L.spacing = double(t.h) / L.ranges;
	const double basin = std::min<double>(L.spacing, o.rangeSpacing);
	L.halfRidge = o.ridgeWidth / 2.0;
	// The slope from a range to its playa is shared: the fans take kFanShare of it (at least
	// kSmallestHomeFan and a little over, at most kLongestFan), and the widest playa the control allows
	// takes what is left, up to kLakeShare of the basin; a playa that would need more is narrowed, and
	// said so. The fans are sized for that widest playa, so every playa setting changes only the lakes
	// (and the floor of desert round them), never the fans; a wider basin lengthens both.
	const double rimReach = kSaltRim + kSaltLobe + kMeadow + kPlayaSway + kToeGap;
	const double slope = std::min(L.spacing, basin) / 2 - L.halfRidge - kFootGap;
	const double fanTarget = std::clamp(slope * kFanShare, kSmallestHomeFan + kLakeSlack, kLongestFan);
	const double lakeRoom = std::max(0.0, (slope - rimReach - fanTarget) / kLakeLobe);
	const double widest = std::min(basin * kLakeShare, lakeRoom);
	if (widest < basin * kLakeShare)
		context.telemetry.fallback("bajada.playa.narrowed", "The playa narrowed to leave the fans room");
	L.lakeHalf = widest * o.playa / 100.0;
	L.fanLength = std::min(kLongestFan, slope - widest * kLakeLobe - rimReach);
	context.telemetry.measure("bajada.ranges", L.ranges);
	context.telemetry.measure("bajada.fan-length", L.fanLength);
	if (L.fanLength < kSmallestHomeFan)
	{
		L.failure = "The bajadas are too narrow for the homes; use a bigger map, a wider range "
					"spacing, narrower ridges or a smaller playa.";
		return false;
	}
	(void)reach;

	// The home design, one for the whole map, and its mirror image.
	const bool flip = context.bounded("bajada-flip", 2) == 1;
	const int design = o.homeDesign > 0 ? o.homeDesign - 1
										: int(context.bounded("bajada-design", kHomeDesigns));
	L.home = homeDesign(design, flip);
	context.telemetry.choice("bajada.home.design", L.home.name);

	// Fan slots: a row below each face of every range, `fans` for every 256 tiles of it; the rows on the
	// two faces of a range are staggered by half a slot.
	const int fansPerRow = std::max(2, int(std::lround(o.fans * t.w / 256.0)));
	slot = double(t.w) / fansPerRow;
	L.base = int(context.bounded("bajada-layout", std::uint32_t(t.h)));
	const double phase = context.bounded("bajada-layout", std::uint32_t(t.w));
	const int homeSide = context.bounded("bajada-layout", 2) ? 1 : -1;
	for (int r = 0; r < L.ranges; ++r)
		for (int side : {-1, 1})
		{
			const double stagger = side > 0 ? slot / 2 : 0;
			const double footY = L.base + r * L.spacing + side * (L.halfRidge + kFootGap);
			for (int j = 0; j < fansPerRow; ++j)
				slots.push_back({t.x(int(std::lround(phase + stagger + j * slot))),
								 t.y(int(std::lround(footY))), side, r});
		}

	// Homes: evenly along one row per range, below the map's home face; below both faces (rows on the
	// two faces of a range staggered by half a gap) only when one face cannot hold every home
	// kHomeSeparation apart. Rows share the colonies out as evenly as they go.
	const double rowPhase = context.bounded("bajada-homes", std::uint32_t(t.w));
	// A map with a single range has one basin between its two faces, so its homes take both faces
	// from the start; with more ranges, one face per range keeps every home a translation of the others.
	for (bool oneFace : {L.ranges > 1, false})
	{
		const int rows = oneFace ? L.ranges : 2 * L.ranges;
		const int perRow = (teams + rows - 1) / rows;
		L.homeGap = double(t.w) / perRow;
		homes.clear();
		if (L.homeGap < kHomeSeparation)
			continue;
		for (int q = 0, placed = 0; q < rows; ++q)
		{
			const int r = oneFace ? q : q / 2;
			const int side = oneFace ? homeSide : (q % 2 ? 1 : -1);
			const int count = teams / rows + (q < teams % rows ? 1 : 0);
			const double gap = double(t.w) / std::max(1, count);
			const double stagger = oneFace ? r * gap / 2 : (q % 2) * gap / 2 + (q / 2) * gap / 4;
			const double footY = L.base + r * L.spacing + side * (L.halfRidge + kFootGap);
			for (int j = 0; j < count && placed < teams; ++j, ++placed)
				homes.push_back({t.x(int(std::lround(rowPhase + stagger + j * gap))),
								 t.y(int(std::lround(footY))), side, r});
		}
		if (!oneFace)
			context.telemetry.fallback("bajada.homes.both-faces",
									   "One face of the ranges could not hold every home");
		break;
	}
	context.telemetry.measure("bajada.homes.gap", L.homeGap);
	if (int(homes.size()) < teams)
	{
		L.failure = "Too many colonies for this map; use a bigger map or fewer colonies.";
		return false;
	}
	dealStarts(context, homes);
	return true;
}

// Stage 2: the ranges' centre lines: a sway along the map. Round a home the range may bend away from
// its fan but never towards it (bending away by at most kAwaySway of its sway, so the spring stays at
// the foot), and the home's apex is laid on the unswayed line, so the range never enters a home's
// footprint however it sways; `ridgeWild` holds each face's window.
void swayRanges(const std::vector<Slot> &homes, GenerationContext &context, Layout &L)
{
	const Torus &t = L.t;
	PeriodicNoise sway(t.w, t.h, kRidgeSwayCell, context.stream("bajada-ridges"));
	L.ridgeCentre.assign(L.ranges, std::vector<double>(t.w, 0));
	for (int face = 0; face < 2; ++face)
		L.ridgeWild[face].assign(L.ranges, std::vector<double>(t.w, 1));
	for (int r = 0; r < L.ranges; ++r)
		for (int x = 0; x < t.w; ++x)
		{
			for (const Slot &s : homes)
				if (s.ridge == r)
				{
					double &w = L.ridgeWild[s.side > 0][r][x];
					w = std::min(w, smoothstep((std::abs(t.offsetX(s.x, x)) - L.homeHalfWidth - kFlatReach) /
											   kFlatRamp));
				}
			// Towards a face, the offset is held by that face's window; away from it, by its window
			// but never below kAwaySway.
			double offset = kRidgeSway * (2 * sway.at(x, r * L.spacing) - 1);
			const double towards = L.ridgeWild[offset > 0][r][x], away = L.ridgeWild[offset <= 0][r][x];
			offset *= std::min(towards, std::max(away, kAwaySway));
			L.ridgeCentre[r][x] = L.base + r * L.spacing + offset;
		}
}

// Stage 3: the home stencil, grown once in its own frame (a torus big enough that nothing wraps) with
// the apex at vertex (kStencilHalf, kStencilHalf): springs, town, ring, garden pond, streams, fringe and the
// crops the fan starts with, laid on the stencil's own growth field so every copy is sown alike.
void designHome(const BajadaOptions &o, GenerationContext &context, Layout &L)
{
	const Torus local{2 * kStencilHalf, 2 * kStencilHalf};
	const int ln = local.size();
	const double ox = kStencilHalf, oy = kStencilHalf;
	const double reach = o.streamReach / 100.0;
	std::vector<unsigned char> water(ln, 0), sand(ln, 0), green(ln, 0), town(ln, 0), ring(ln, 0),
							   farmable(ln, 1);
	// On a map whose homes stand close along their face the town shrinks towards the fan, so the fan
	// keeps room to spread: its size, its distance from the springs and the garden all scale with the gap
	// between homes, down to kCrowdedTown of their full size.
	const double scale = std::clamp(L.homeGap / kRoomyGap, kCrowdedTown, 1.0);
	{
		HomeDesign &h = L.home;
		const ShapePoint town = h.town;
		h.town.x *= scale;
		h.townRadius *= scale;
		h.swarm.x = h.town.x + (h.swarm.x - town.x) * scale;
		h.swarm.y = h.town.y + (h.swarm.y - town.y) * scale;
	}
	context.telemetry.measure("bajada.home.town-scale", scale);
	const double townWater = kTownWater * scale;
	const HomeDesign &d = L.home;
	for (const auto &spring : d.springs)
	{
		const RadialShape pond(spring[2], 0.0, context, "bajada-home-pond");
		fillShape(water, local, ox + spring[0], oy + spring[1], pond);
		const RadialShape halo(spring[2] + kGreenHead * reach, 0.0, context, "bajada-home-pond");
		fillShape(green, local, ox + spring[0], oy + spring[1], halo);
	}
	// The town: a rough outline of open ground on the fan's shoulder, grass to kTownGreen beyond it,
	// with no fringe on the gravel side.
	const RadialShape outline(d.townRadius, kTownRoughness, context, "bajada-home-town");
	const double tx = ox + d.town.x + 0.5, ty = oy + d.town.y + 0.5;
	std::vector<unsigned char> townGround(ln, 0);
	const double reachOut = outline.maximumRadius() + kTownGreen + 1;
	for (int y = int(ty - reachOut); y <= int(ty + reachOut); ++y)
		for (int x = int(tx - reachOut); x <= int(tx + reachOut); ++x)
		{
			const double dx = x + 0.5 - tx, dy = y + 0.5 - ty;
			const double r = std::hypot(dx, dy), edge = outline.radiusAt(std::atan2(dy, dx));
			const int i = local.at(x, y);
			if (r < edge + kTownGreen)
				townGround[i] = 1;
			// The town is everything inside the ring's line, so no grass lies between the two.
			if (r < edge)
				town[i] = 1;
		}

	// The garden pond: a small pool in the garden between the town and the fan, a little below the
	// swarm's row, kGardenPondGap tiles out from the ring, so the kit's wheat beside it regrows (REVIEW
	// round 5: a garden seven tiles from any water held the kit but regrew too slowly, and Nicowar
	// stalled on some maps).
	{
		const double side = d.town.x < 0 ? 1 : -1;
		const double angle = std::atan2(kGardenPondDrop, side);
		const double out = outline.radiusAt(angle) + kGardenPondGap + kGardenPond;
		const double px = tx + out * std::cos(angle), py = ty + out * std::sin(angle);
		const RadialShape pool(kGardenPond, 0.0, context, "bajada-home-pond");
		fillShape(water, local, px, py, pool);
		const RadialShape halo(kGardenPond + kGreenHead * reach * 0.6, 0.0, context, "bajada-home-pond");
		fillShape(green, local, px, py, halo);
	}

	// The streams: every root grown in turn, each branch accepted only downhill of the springs, on the
	// fan, and clear of the town.
	const double length = L.fanLength;
	// Streams keep this far out on the fan's side of the frame, so that with their green and fringe,
	// and the town on the other side, they leave a few tiles between neighbouring homes.
	const double townSide = d.town.x < 0 ? 1 : -1;
	const double townExtent = std::abs(d.town.x) + d.townRadius * (1 + kTownRoughness) + kTownGreen;
	const double sideways = L.homeGap - 4 - townExtent - kGreenHead * reach - 3;
	std::mt19937 &random = context.stream("bajada-home-streams");
	const auto roll = [&] { return unit(random); };
	std::vector<Branch> tree;
	const auto accept = [&](const std::vector<StrokePoint> &path, int)
	{
		for (const StrokePoint &p : path)
		{
			const double u = p.y - oy, v = p.x - ox;
			if (u > length || u < 3 || v * townSide > sideways || std::abs(v - d.springs[0][0]) > u * 1.3 + 12)
				return false;
			const double angle = std::atan2(p.y - ty, p.x - tx);
			if (std::hypot(p.x - tx, p.y - ty) < outline.radiusAt(angle) + townWater + p.halfWidth)
				return false;
		}
		return true;
	};
	ForkStyle style;
	style.spread = 0.5;
	style.lengthRatio = 0.82;
	style.widthRatio = 0.85;
	style.minimumHalfWidth = 1.25;
	style.minimumLength = 4;
	style.bend = 0.15;
	const int extraFork = reach > 1.2 ? 1 : 0;
	// A tree that meets the town or the fan's edge early can come out a single stream; the home keeps
	// the fullest of a few grown in turn from the same stream.
	for (int attempt = 0; attempt < kHomeTreeAttempts; ++attempt)
	{
		std::vector<Branch> grown;
		for (const Root &root : d.roots)
			growBranches(grown, -1, {ox + root.x, oy + root.y}, kPi / 2 - root.turn, root.length * reach,
						 root.halfWidth * std::sqrt(reach),
						 int(std::lround(root.forks * std::min(1.0, reach))) + extraFork, style, roll, accept);
		if (grown.size() > tree.size())
			tree.swap(grown);
	}
	context.telemetry.measure("bajada.home.branches", tree.size());
	strokeTree(local, tree, length, reach, false, random, [&](const StrokePoint &p) { return p.y - oy; },
			   water, sand, green);
	// The ring: the town's outline, two corners of sand, all the way round. Crops could reach the open
	// side from a neighbour's water too, and a ring drawn only where the home's own water reaches would
	// leave that side open on some maps and not others.
	{
		std::vector<StrokePoint> circle;
		for (int k = 0; k < 64; ++k)
		{
			const double angle = 2 * kPi * k / 64;
			const ShapePoint p = polarPoint(tx, ty, outline.radiusAt(angle), angle);
			circle.push_back({p.x, p.y, 1.0});
		}
		strokePath(ring, local, circle, 1, true);
		for (int i = 0; i < ln; ++i)
			if (ring[i])
				sand[i] = 1;
	}
	// The range's stone stands on the rows above the apex, so nothing is sown there.
	for (int i = 0; i < ln; ++i)
		if (i / local.w < oy + 1)
			farmable[i] = 0;
	// The fringe: sand corners just outside the green, fraying from one to three wide.
	std::vector<unsigned char> fringe =
		noisyFringe(local, green, periodicNoise(local.w, local.h, 6, context.stream("bajada-home-fringe")));
	// The town's ground is grass with no fringe of its own: it opens onto the gravel.
	for (int i = 0; i < ln; ++i)
		if (townGround[i])
		{
			green[i] = 1;
			fringe[i] = 0;
		}
	// Nothing but the springs reaches into the range above the apex, which must stay a wall.
	for (int i = 0; i < ln; ++i)
		if (i / local.w < oy - 2)
			green[i] = sand[i] = fringe[i] = 0;
	// The footprint: everything drawn, widened; the range above the apex is part of it too, since the
	// ridge runs straight round every home.
	std::vector<unsigned char> drawn(ln, 0);
	for (int i = 0; i < ln; ++i)
		drawn[i] = water[i] || sand[i] || green[i] || fringe[i];
	const std::vector<unsigned char> footprint = dilate(local, drawn, kFootprintMargin);
	const std::vector<unsigned char> inner = dilate(local, drawn, kFootprintMargin - 2);

	// The crops the home fan starts with, beyond its kit: the share of the fertile farm the ambient
	// layer gives every fan, in patches, on the stencil's own growth field (its sketch with gravel
	// outside the footprint).
	TerrainSketch sketch(ln, GRASS);
	for (int i = 0; i < ln; ++i)
		sketch[i] = water[i] ? WATER : (sand[i] || fringe[i]) ? SAND : GRASS;
	layBeaches(sketch, local);
	const Fertility::Field fertility = cropGrowthField(sketch, local);
	const std::vector<unsigned char> pureGrass = pureTiles(sketch, local, GRASS);
	const std::vector<int> patch = periodicNoise(local.w, local.h, 9, context.stream("bajada-home-crops"));
	const std::vector<int> split = periodicNoise(local.w, local.h, 5, context.stream("bajada-home-crops"));
	std::vector<unsigned char> crop(ln, 0);
	// The kit first: the kHomeWheat farm tiles nearest the wheat seed and the kHomeWood nearest the
	// wood seed, never within the swarm's clearance.
	{
		const int sx = ox + int(d.swarm.x) - 2, sy = oy + int(d.swarm.y) - 2;
		const auto sow = [&](ShapePoint seed, int count, unsigned char kind)
		{
			std::vector<std::pair<double, int>> closest;
			for (int i = 0; i < ln; ++i)
			{
				const int x = i % local.w, y = i / local.w;
				if (!green[i] || town[i] || ring[i] || !pureGrass[i] || crop[i] || !farmable[i] ||
					(x >= sx - kSwarmClearance && x < sx + 4 + kSwarmClearance && y >= sy - kSwarmClearance &&
					 y < sy + 4 + kSwarmClearance))
					continue;
				closest.push_back({std::hypot(x + 0.5 - ox - seed.x, y + 0.5 - oy - seed.y), i});
			}
			std::stable_sort(closest.begin(), closest.end());
			for (int k = 0; k < std::min<int>(count, int(closest.size())); ++k)
				crop[closest[k].second] = kind;
		};
		sow(d.wheat, kHomeWheat, 1);
		sow(d.wood, kHomeWood, 2);
	}
	{
		std::vector<int> farm, levels;
		for (int i = 0; i < ln; ++i)
			if (green[i] && !town[i] && !ring[i] && pureGrass[i] && !crop[i] && farmable[i] &&
				fertility.at(i % local.w, i / local.w) > 0)
			{
				farm.push_back(i);
				levels.push_back(patch[i]);
			}
		if (!farm.empty())
		{
			const int cut = percentile(levels, 45);
			std::vector<std::pair<long long, int>> ranked;
			for (int i : farm)
				if (patch[i] >= cut)
					ranked.push_back({-(long long)fertility.at(i % local.w, i / local.w), i});
			std::stable_sort(ranked.begin(), ranked.end());
			const int wheat = int(scaledCount(int(farm.size()) * kFarmWheatPercent / 100, o.wheat));
			const int wood = int(scaledCount(int(farm.size()) * kFarmWoodPercent / 100, o.wood));
			const int total = std::min<int>(int(ranked.size()), wheat + wood);
			std::vector<int> chosen;
			for (int k = 0; k < total; ++k)
				chosen.push_back(ranked[k].second);
			std::stable_sort(chosen.begin(), chosen.end(), [&](int a, int b) { return split[a] < split[b]; });
			const int wheatShare = int(std::int64_t(total) * wheat / std::max(1, wheat + wood));
			for (int k = 0; k < total; ++k)
				crop[chosen[k]] = k < wheatShare ? 1 : 2;
		}
	}

	// The stencil is centred across its slot: the fan and its town together, not the springs.
	{
		int low = ln, high = -ln;
		for (int i = 0; i < ln; ++i)
			if (drawn[i])
			{
				low = std::min(low, i % local.w - kStencilHalf);
				high = std::max(high, i % local.w - kStencilHalf);
			}
		L.homeShift = low <= high ? -(low + high) / 2 : 0;
	}
	L.stencil.clear();
	for (int ly = 0; ly < local.h; ++ly)
		for (int lx = 0; lx < local.w; ++lx)
		{
			const int li = ly * local.w + lx;
			if (footprint[li])
				L.stencil.push_back({lx - kStencilHalf + L.homeShift, ly - kStencilHalf, water[li], sand[li], green[li],
									 fringe[li], town[li], (unsigned char)(green[li] && !town[li] && !ring[li]),
									 crop[li], inner[li]});
		}
	L.homeHalfWidth = 0;
	for (const StencilTile &st : L.stencil)
		if (st.water || st.green || st.sand)
			L.homeHalfWidth = std::max(L.homeHalfWidth, std::abs(st.dx + 0.5));
	context.telemetry.measure("bajada.home.footprint", L.stencil.size());
	context.telemetry.measure("bajada.home.half-width", L.homeHalfWidth);
}

// Stage 4: stamps the stencil at every home, over whatever the design drew there before.
void stampHomes(int teams, Layout &L,
				GenerationContext &context)
{
	const Torus &t = L.t;
	L.kits.assign(teams, {0, 0});
	L.swarms.assign(teams, {0, 0});
	L.facing.assign(teams, 0);
	const HomeDesign &d = L.home;
	for (int k = 0; k < teams; ++k)
	{
		const Fan &fan = L.fans[k];
		const int facing = fan.side > 0 ? 0 : 2;
		L.facing[k] = facing;
		context.telemetry.measure("bajada.home.facing", facing, k);
		const auto placeTile = [&](int dx, int dy)
		{
			const auto [x, y] = turnStencilTile(facing, dx, dy);
			return MapGeneratorPoint(t.x(fan.x + x), t.y(fan.y + y));
		};
		const ShapePoint spring = turnStencilPoint(facing, {d.springs[0][0] + L.homeShift, d.springs[0][1]});
		L.kits[k] = {fan.x + spring.x, fan.y + spring.y};
		// The swarm's 4x4 footprint centred on the town's middle tile, and the kit's seeds, turned as
		// tiles so every copy lands on the same stencil tiles.
		const MapGeneratorPoint townTile = placeTile(int(d.swarm.x) + L.homeShift, int(d.swarm.y));
		L.swarms[k] = MapGeneratorPoint(townTile.x - (facing ? 1 : 2), townTile.y - (facing ? 1 : 2));
		for (const StencilTile &s : L.stencil)
		{
			const auto [vx, vy] = turnStencilVertex(facing, s.dx, s.dy);
			const int v = t.at(fan.x + vx, fan.y + vy);
			L.water[v] = L.sand[v] = L.green[v] = L.fringe[v] = L.meadow[v] = 0;
			const MapGeneratorPoint p = placeTile(s.dx, s.dy);
			L.homeOf[t.at(p.x, p.y)] = k;
		}
	}
	{
		std::vector<unsigned char> covers(t.size(), 0);
		int shared = 0;
		for (int k = 0; k < teams; ++k)
			for (const StencilTile &s : L.stencil)
				if (s.inner)
				{
					const auto [x, y] = turnStencilTile(L.facing[k], s.dx, s.dy);
					shared += covers[t.at(L.fans[k].x + x, L.fans[k].y + y)]++ == 1;
				}
		if (shared > 0)
			context.telemetry.measure("bajada.homes.shared-tiles", shared);
		int inner = 0;
		for (const StencilTile &s : L.stencil)
			inner += s.inner;
		if (shared * 100 > inner * kSharedPercent)
			L.failure = "Too many colonies for this map; use a bigger map or fewer colonies.";
	}
	// Drawn only once every footprint is clear, so a neighbour's margin never erases a home's town.
	for (int k = 0; k < teams; ++k)
		for (const StencilTile &s : L.stencil)
		{
			const auto [vx, vy] = turnStencilVertex(L.facing[k], s.dx, s.dy);
			const int v = t.at(L.fans[k].x + vx, L.fans[k].y + vy);
			L.water[v] |= s.water;
			L.sand[v] |= s.sand;
			L.green[v] |= s.green;
			L.fringe[v] |= s.fringe;
			const auto [tx, ty] = turnStencilTile(L.facing[k], s.dx, s.dy);
			const int i = t.at(L.fans[k].x + tx, L.fans[k].y + ty);
			if (s.inner)
				L.homeOf[i] = k;
			if (s.town)
				L.townOf[i] = k;
			if (s.farm)
				L.farmOf[i] = k;
		}
}

Layout design(const GenerationRequest &request, GenerationContext &context)
{
	const BajadaOptions o(request);
	Layout L;
	L.t = {1 << request.wDec, 1 << request.hDec};
	const Torus &t = L.t;
	const int n = t.size(), teams = std::max(1, request.nbTeams);
	const double reach = o.streamReach / 100.0;
	std::vector<Slot> slots;
	std::vector<Slot> homes;
	double slot = 0;
	if (!planHomes(o, teams, context, L, slots, homes, slot))
		return L;
	PeriodicNoise rough(t.w, t.h, kRidgeRoughCell, context.stream("bajada-ridges"));
	PeriodicNoise massif(t.w, t.h, kMassifCell, context.stream("bajada-ridges"));

	L.water.assign(n, 0);
	L.sand.assign(n, 0);
	L.green.assign(n, 0);
	L.meadow.assign(n, 0);
	L.fringe.assign(n, 0);
	L.townOf.assign(n, -1);
	L.farmOf.assign(n, -1);
	L.homeOf.assign(n, -1);
	L.pass.assign(n, 0);
	std::vector<unsigned char> canyon(n, 0);

	designHome(o, context, L);
	swayRanges(homes, context, L);
	// The homes' fans, and a claim neutral fans keep out of: every home's footprint.
	std::vector<unsigned char> homeClaim(n, 0);
	std::vector<ShapePoint> towns;
	for (int k = 0; k < teams; ++k)
	{
		const Slot &s = homes[k];
		const int ay = t.y(int(std::lround(L.base + s.ridge * L.spacing + s.side * (L.halfRidge + kFootGap))));
		L.fans.push_back({s.x, ay, s.side, s.ridge, k});
		const int facing = s.side > 0 ? 0 : 2;
		const ShapePoint town = turnStencilPoint(facing, {L.home.town.x + L.homeShift, L.home.town.y});
		towns.push_back({s.x + town.x, ay + town.y});
		for (const StencilTile &st : L.stencil)
		{
			const auto [x, y] = turnStencilTile(facing, st.dx, st.dy);
			homeClaim[t.at(s.x + x, ay + y)] = 1;
		}
		for (const auto &spring : L.home.springs)
		{
			const ShapePoint p = turnStencilPoint(facing, {spring[0] + L.homeShift, spring[1]});
			const RadialShape notch(spring[2] + 3, 0.0, context, "bajada-home-pond");
			fillShape(canyon, t, s.x + p.x, ay + p.y, notch);
		}
	}

	// Passes: `passes` per range, evenly along it, at the phase that keeps their mouths farthest from
	// every home on the range. Each is a wandering sand floor through the stone, the stone cleared a
	// little wider, and the range swells into shoulders either side of it.
	std::vector<std::vector<int>> passColumns(L.ranges);
	std::vector<std::vector<double>> shoulder(L.ranges, std::vector<double>(t.w, 0));
	{
		std::mt19937 &random = context.stream("bajada-passes");
		for (int r = 0; r < L.ranges; ++r)
		{
			const double pitch = double(t.w) / o.passes;
			int bestPhase = 0;
			double bestScore = -1;
			for (int p = 0; p < int(pitch); ++p)
			{
				double score = 1e18;
				for (int k = 0; k < o.passes; ++k)
				{
					const int x = t.x(int(p + k * pitch));
					for (const Fan &f : L.fans)
						if (f.ridge == r)
							score = std::min(score, double(std::abs(t.offsetX(f.x, x))));
				}
				if (score > bestScore)
				{
					bestScore = score;
					bestPhase = p;
				}
			}
			for (int k = 0; k < o.passes; ++k)
			{
				const int x = t.x(int(bestPhase + k * pitch));
				const double skew = (2 * unit(random) - 1) * kPassSkew;
				const int x2 = t.x(int(std::lround(x + skew)));
				const double top = L.ridgeCentre[r][x] - L.halfRidge * (1 + kMassif) - kPassShoulder - kPassOverrun;
				const double bottom =
					L.ridgeCentre[r][x2] + L.halfRidge * (1 + kMassif) + kPassShoulder + kPassOverrun;
				const std::vector<StrokePoint> gorge = wanderingPath(
					t, {x + 0.5, top}, {x + skew + 0.5, bottom}, kPassHalfWidth, 2.0, 0.15, random);
				strokePath(L.sand, t, gorge);
				std::vector<StrokePoint> wide = gorge;
				for (StrokePoint &p : wide)
					p.halfWidth += kPassStoneClear;
				strokePath(L.pass, t, wide);
				passColumns[r].push_back(x);
				passColumns[r].push_back(x2);
				for (int dx = -int(kPassShoulderReach + kPassSkew); dx <= int(kPassShoulderReach + kPassSkew); ++dx)
				{
					const int column = t.x(x + dx);
					const double off = std::abs(dx - skew / 2) / (kPassShoulderReach + std::abs(skew) / 2);
					shoulder[r][column] = std::max(shoulder[r][column], kPassShoulder * (1 - smoothstep(off)));
				}
				++L.passes;
			}
		}
	}

	// Neutral fans at every other slot: some left as desert, some dry washes, the rest springs with
	// their own stream trees, each drawn from its own rolls. A branch may not reach into a home's
	// footprint; neighbouring neutral fans may merge, which is what makes a bajada.
	{
		std::mt19937 &random = context.stream("bajada-fans");
		for (size_t s = 0; s < slots.size(); ++s)
		{
			// Every slot draws the same rolls whatever becomes of it, so one fan's fate never moves the
			// shapes of the fans after it.
			const double empty = unit(random), dry = unit(random), jitter = unit(random),
						 lengthShare = unit(random), turn = unit(random), roots = unit(random),
						 pondSize = unit(random);
			std::mt19937 fanRandom(random());
			const Slot &at = slots[s];
			const int ax = t.x(int(std::lround(at.x + (2 * jitter - 1) * kFanJitter * slot)));
			// A neutral fan may stand between homes wherever its apex is clear of every home's footprint;
			// its streams keep kNeutralTownClear tiles from every town, so the water within a short walk
			// of a town is its own.
			const int apexY = t.y(int(std::lround(L.ridgeCentre[at.ridge][ax] + at.side * (L.halfRidge + kFootGap))));
			const bool nearHome = homeClaim[t.at(ax, apexY + at.side * 3)] != 0;
			bool nearPass = false;
			for (int x : passColumns[at.ridge])
				nearPass |= std::abs(t.offsetX(x, ax)) < kPassFanClear;
			if (nearHome || nearPass || empty * 100 < kEmptyFanPercent)
				continue;
			const int side = at.side;
			const int ay = t.y(int(std::lround(L.ridgeCentre[at.ridge][ax] + side * (L.halfRidge + kFootGap))));
			Fan fan{ax, ay, side, at.ridge, -1, dry * 100 < kDryFanPercent};
			const double length = L.fanLength * (0.65 + 0.35 * lengthShare);
			const auto downhill = [&](const StrokePoint &p) { return side * (p.y - ay); };
			const auto roll = [&] { return unit(fanRandom); };
			const auto accept = [&](const std::vector<StrokePoint> &path, int)
			{
				for (const StrokePoint &p : path)
				{
					const double u = downhill(p), v = p.x - ax;
					if (u > length || u < 2 || std::abs(v) > u * 1.3 + 6)
						return false;
					if (homeClaim[t.at(int(std::floor(p.x)), int(std::floor(p.y)))])
						return false;
					for (const ShapePoint &town : towns)
						if (t.dist2(int(p.x), int(p.y), int(town.x), int(town.y)) <
							kNeutralTownClear * kNeutralTownClear)
							return false;
				}
				return true;
			};
			ForkStyle style;
			style.spread = 0.38 + 0.12 * roll();
			style.lengthRatio = 0.78;
			style.widthRatio = 0.85;
			style.minimumHalfWidth = fan.dry ? 0.8 : 1.25;
			style.minimumLength = 5;
			style.bend = 0.2;
			std::vector<Branch> tree;
			const int rootCount = roots < 0.35 ? 2 : 1;
			const double heading = side > 0 ? kPi / 2 : -kPi / 2;
			const int forks = std::max(2, int(std::lround(3 * std::min(1.0, reach))) + (reach > 1.2));
			for (int k = 0; k < rootCount; ++k)
			{
				const double offset = rootCount == 1 ? (2 * turn - 1) * 0.3 : (k ? 0.7 : -0.7);
				growBranches(tree, -1, {ax + 0.5, ay + side * 4.0 + 0.5}, heading + offset,
							 (10 + 5 * roll()) * reach, fan.dry ? 1.0 : 1.35 + 0.3 * roll(), forks, style,
							 roll, accept);
			}
			if (tree.empty())
			{
				++L.emptyTrees;
				continue;
			}
			// A spring pond only where the fan is big enough to carry one; a small fan's stream simply
			// leaves the foot of the range.
			if (!fan.dry && int(tree.size()) >= kPondBranches)
			{
				const double radius = 3 + 1.5 * pondSize, below = side * (radius * 1.25 + 1.5);
				const RadialShape pond(radius, 0.25, context, "bajada-fan-ponds");
				fillShape(L.water, t, ax + 0.5, ay + below + 0.5, pond);
				const RadialShape halo(radius + kGreenHead * reach * 0.8, 0.25, context, "bajada-fan-ponds");
				fillShape(L.green, t, ax + 0.5, ay + below + 0.5, halo);
				const RadialShape notch(radius + 3, 0.0, context, "bajada-fan-ponds");
				fillShape(canyon, t, ax + 0.5, ay + below + 0.5, notch);
			}
			strokeTree(t, tree, length, reach, fan.dry, fanRandom, downhill, L.water, L.sand, L.green);
			L.dryFans += fan.dry;
			++L.neutralFans;
			L.fans.push_back(fan);
		}
	}

	// The playa: a chain of lakes along the middle of every basin, each with its lobed salt flat and
	// its meadow, and salt-flat crossings between them. Walking along the basin from a random start,
	// every lake takes a length and a crossing a width of its own, and the last lake stops short of
	// the first crossing, so no two basins or two maps have the same chain.
	{
		std::mt19937 &random = context.stream("bajada-playa");
		PeriodicNoise sway(t.w, t.h, kPlayaSwayCell, random);
		PeriodicNoise lobes(t.w, t.h, kLakeLobeCell, random);
		PeriodicNoise salt(t.w, t.h, kLakeLobeCell / 2, random);
		PeriodicNoise meadowNoise(t.w, t.h, kLakeLobeCell, random);
		const double rim = kSaltRim + kSaltLobe;
		for (int r = 0; r < L.ranges && L.lakeHalf > 0; ++r)
		{
			const double start = unit(random) * t.w;
			double cursor = start;
			while (true)
			{
				const double crossing = kCrossingLow + (kCrossingHigh - kCrossingLow) * unit(random);
				const double length =
					kLakePitch * (kLakeLengthLow + (kLakeLengthHigh - kLakeLengthLow) * unit(random));
				const double widthShare = 0.55 + 0.45 * unit(random);
				const double shift = (2 * unit(random) - 1) * kLakeShift;
				const double from = cursor + crossing + 2 * rim;
				double to = std::min(from + length, start + t.w - kCrossingLow - 2 * rim);
				if (to - from < kShortestLake)
					break;
				cursor = to;
				const double halfLength = (to - from) / 2, cx = (from + to) / 2;
				// Two or three basins along the lake, where the shore swells and pinches.
				const double basins = 1 + unit(random) * 2, basinPhase = 2 * kPi * unit(random);
				++L.lakes;
				const int alongReach = int(halfLength + rim + kMeadow) + 1;
				for (int dx = -alongReach; dx <= alongReach; ++dx)
				{
					const int x = t.x(int(std::lround(cx)) + dx);
					const double centre = L.ridgeCentre[r][x] + L.spacing / 2 + shift +
										  kPlayaSway * (2 * sway.at(x, r * L.spacing) - 1);
					const double along = dx / halfLength;
					const double shape =
						std::abs(along) < 1
							? std::pow(1 - along * along, 0.35) *
								  (1 + kLakeBasins * std::cos(basinPhase + kPi * basins * (along + 1)))
							: 0;
					const int acrossReach = int(L.lakeHalf * kLakeLobe + rim + kMeadow) + 2;
					for (int dy = -acrossReach; dy <= acrossReach; ++dy)
					{
						const int y = t.y(int(std::floor(centre)) + dy);
						const int i = t.at(x, y);
						const double off = std::abs(wrappedOffset(y + 0.5, centre, t.h));
						const double half = L.lakeHalf * widthShare * shape *
											(1 + (kLakeLobe - 1) * (2 * lobes.at(x, y) - 1));
						// Distance outside the lake's edge, across the basin and, past its ends, along
						// it too.
						const double outside =
							std::abs(along) < 1 ? off - half : std::hypot(off, std::abs(dx) - halfLength);
						const double saltEdge = kSaltRim + kSaltLobe * (2 * salt.at(x, y) - 1);
						if (half > 1 && off < half)
							L.water[i] = 1;
						else if (outside < saltEdge && !L.water[i])
							L.sand[i] = 1;
						else if (outside < saltEdge + kMeadow && meadowNoise.at(x, y) > kMeadowGap)
							L.meadow[i] = 1;
					}
				}
			}
		}
	}

	// Ridges: stone where a tile lies within the ridge's half width of its centre (with its massifs,
	// roughness and pass shoulders), except in a spring's notch and a pass.
	L.stone.assign(n, 0);
	L.ridge.assign(n, 0);
	for (int y = 0; y < t.h; ++y)
		for (int x = 0; x < t.w; ++x)
		{
			const int i = y * t.w + x;
			for (int r = 0; r < L.ranges; ++r)
			{
				const double d = wrappedOffset(y + 0.5, L.ridgeCentre[r][x], t.h);
				// The face away from a home keeps all its massifs and roughness.
				const double wild = L.ridgeWild[d > 0][r][x];
				const double edge = L.halfRidge * (1 + kMassif * wild * (2 * massif.at(x, r * L.spacing) - 1)) +
									kRidgeRough * wild * (2 * rough.at(x, d < 0 ? 0 : t.h / 2) - 1) +
									shoulder[r][x] * wild;
				if (std::abs(d) <= std::max(edge, kRidgeCore))
				{
					L.ridge[i] = 1;
					L.stone[i] = (!canyon[i] || std::abs(d) <= kRidgeCore) && !L.pass[i];
				}
			}
		}

	// Spurs: tapering arms of stone off both faces of every range, where they reach no fan, no pass
	// and no playa, so a range reads as mountains rather than a wall.
	{
		std::mt19937 &random = context.stream("bajada-spurs");
		std::vector<unsigned char> spur(n, 0);
		for (int r = 0; r < L.ranges; ++r)
			for (int side : {-1, 1})
				for (double x = unit(random) * kSpurGapHigh; x < t.w;
					 x += kSpurGapLow + (kSpurGapHigh - kSpurGapLow) * unit(random))
				{
					const double length = kSpurLow + (kSpurHigh - kSpurLow) * unit(random);
					const double heading = side * kPi / 2 + (2 * unit(random) - 1) * 0.8;
					const double base = kSpurBaseLow + (kSpurBaseHigh - kSpurBaseLow) * unit(random);
					const double bend = (2 * unit(random) - 1) * 0.4;
					const int column = t.x(int(x));
					const double y0 = L.ridgeCentre[r][column] + side * (L.halfRidge - 1);
					const std::vector<StrokePoint> arm = bentPath({x, y0}, heading, length, bend, base, 0.8, 10);
					strokePath(spur, t, arm);
					const PathBounds box = pathBounds(arm);
					std::vector<int> tiles;
					bool clear = true;
					for (int y = int(box.y - box.radius) - 1; y <= int(box.y + box.radius) + 1; ++y)
						for (int xx = int(box.x - box.radius) - 1; xx <= int(box.x + box.radius) + 1; ++xx)
						{
							const int i = t.at(xx, y);
							if (!spur[i])
								continue;
							spur[i] = 0;
							if (homeClaim[i] || L.green[i] || L.water[i] || L.sand[i] || L.meadow[i] ||
								canyon[i] || L.pass[i])
								clear = false;
							tiles.push_back(i);
						}
					if (clear)
						for (int i : tiles)
							L.ridge[i] = L.stone[i] = 1;
				}
	}

	// The fringe: sand corners just outside every neutral fan's green, so the fans read against the
	// gravel. Then the homes, stamped last over whatever was drawn in their footprints.
	{
		const std::vector<unsigned char> out =
			noisyFringe(t, L.green, periodicNoise(t.w, t.h, 6, context.stream("bajada-fringe")));
		for (int i = 0; i < n; ++i)
			L.fringe[i] = out[i] && !L.ridge[i] && !L.meadow[i];
	}
	stampHomes(teams, L, context);
	if (!L.failure.empty())
		return L;
	context.telemetry.measure("bajada.passes", L.passes);
	context.telemetry.measure("bajada.lakes", L.lakes);
	context.telemetry.measure("bajada.fans.neutral", L.neutralFans);
	context.telemetry.measure("bajada.fans.dry", L.dryFans);
	if (L.emptyTrees > 0)
		context.telemetry.measure("bajada.fans.no-room", L.emptyTrees);
	return L;
}

bool generate(Game &game, GenerationContext &context)
{
	context.stage = "bajada layout";
	const BajadaOptions o(context.request);
	const Layout L = design(context.request, context);
	if (!L.failure.empty())
	{
		context.telemetry.fallback("bajada.layout.failure", L.failure);
		context.detail = L.failure;
		return false;
	}
	Map &map = game.map;
	const Torus &t = L.t;
	const int n = t.size(), teams = context.request.nbTeams;
	for (int k = 0; k < teams; ++k)
		game.addTeam();

	// Terrain: water, then the sand the design drew (passes, washes, rings, fringes, salt
	// flats), then grass on the fans, meadows, ridges and homes, and the desert elsewhere: gravel
	// (grass) crossed by bands of dune sand along the ranges. A home's footprint takes no dunes, so
	// every copy is the same.
	context.stage = "bajada terrain";
	// Dunes: long bands along the contour (a Turing pattern stretched along the ranges), broken by
	// patches of fractal noise, gathering towards the playa.
	TuringStyle bands;
	bands.wavelength = kDuneWavelength;
	bands.stretchY = kDuneStretch;
	bands.iterations = 14;
	std::vector<int> dunes = turingPattern(t, bands, context.stream("bajada-dunes"));
	{
		const std::vector<int> patches = fractalNoise(t.w, t.h, kDuneCell, 2, context.stream("bajada-dunes"));
		for (int i = 0; i < n; ++i)
			dunes[i] = dunes[i] / 2 + (patches[i] - 32768) * 3 / 4;
	}
	for (int y = 0; y < t.h; ++y)
		for (int x = 0; x < t.w; ++x)
		{
			double nearestRange = 1e9;
			for (int r = 0; r < L.ranges; ++r)
				nearestRange = std::min(nearestRange, std::abs(wrappedOffset(y + 0.5, L.ridgeCentre[r][x], t.h)));
			dunes[y * t.w + x] += int(kDuneSlope * std::min(1.0, nearestRange / (L.spacing / 2)));
		}
	std::vector<int> desertValues;
	const auto openDesert = [&](int i)
	{ return !L.green[i] && !L.meadow[i] && !L.ridge[i] && !L.fringe[i] && L.homeOf[i] < 0; };
	for (int i = 0; i < n; ++i)
		if (openDesert(i))
			desertValues.push_back(dunes[i]);
	const int duneCut = percentile(desertValues, 100 - kDunePercent);
	TerrainSketch terrain(n, GRASS);
	for (int i = 0; i < n; ++i)
	{
		if (L.water[i])
			terrain[i] = WATER;
		else if (L.sand[i] || L.fringe[i] || (openDesert(i) && dunes[i] >= duneCut))
			terrain[i] = SAND;
	}
	layBeaches(terrain, t);
	writeUndermap(map, terrain);
	for (int i = 0; i < n; ++i)
		if (L.stone[i] && map.isResourceAllowed(i % t.w, i / t.w, STONE))
			map.setResource(i % t.w, i / t.w, STONE, 1);

	context.stage = "bajada colonies";
	if (!settleColonies(
			game, context, "bajada-starts",
			[&](int team)
			{
				std::vector<unsigned char> home(n, 0);
				for (int i = 0; i < n; ++i)
					home[i] = L.townOf[i] == team && map.isGrass(i % t.w, i / t.w);
				return home;
			},
			[&](int team) { return L.swarms[team]; }))
		return false;

	context.stage = "bajada resources";
	const std::vector<unsigned char> reserved = swarmSurroundings(t, context);
	const auto clear = [&](int i) { return !reserved[i] && clearGround(map, i % t.w, i / t.w); };
	// Every home: the crops of its stencil (its kit and its share of farmland), turned as its tiles are.
	for (int k = 0; k < teams; ++k)
	{
		const Fan &fan = L.fans[k];
		for (const StencilTile &s : L.stencil)
			if (s.crop)
			{
				const auto [x, y] = turnStencilTile(L.facing[k], s.dx, s.dy);
				const int i = t.at(fan.x + x, fan.y + y);
				if (clear(i) && map.isResourceAllowed(i % t.w, i / t.w, s.crop == 1 ? WHEAT : WOOD))
					map.setResource(i % t.w, i / t.w, s.crop == 1 ? WHEAT : WOOD, 1);
			}
	}

	// Farmland on the neutral fans' green ground and the meadows, in patches.
	const Fertility::Field fertility = Fertility::forMap(map, false);
	const std::vector<int> patch = periodicNoise(t.w, t.h, 9, context.stream("bajada-patch"));
	const std::vector<int> split = periodicNoise(t.w, t.h, 5, context.stream("bajada-split"));
	const auto farmland = [&](int i)
	{ return (L.green[i] || L.meadow[i]) && L.homeOf[i] < 0 && !L.stone[i] && clear(i); };
	int fertile = 0;
	for (int i = 0; i < n; ++i)
		fertile += farmland(i) && fertility.at(i % t.w, i / t.w) > 0;
	furnishGround(
		map, t, context, fertility, farmland, [&](int i) { return float(patch[i]); },
		[&](int i) { return split[i]; },
		[&](int)
		{
			return GroundAmounts{int(scaledCount(fertile * kFarmWheatPercent / 100, o.wheat)),
								 int(scaledCount(fertile * kFarmWoodPercent / 100, o.wood)), 0, 0};
		},
		"bajada-stone", "bajada-fruit");

	// The desert: rock outcrops and lone scrub on the dry gravel (grass no probe reaches, so scrub
	// never spreads), never in a home's footprint, and fruit groves on the playa meadows.
	std::vector<int> desert, meadow;
	const auto dryClear = [&](int i)
	{
		return clear(i) && fertility.at(i % t.w, i / t.w) == 0 && !L.ridge[i] && L.homeOf[i] < 0 &&
			   map.isGrass(i % t.w, i / t.w);
	};
	for (int i = 0; i < n; ++i)
	{
		if (!L.green[i] && !L.meadow[i] && dryClear(i))
			desert.push_back(i);
		if (L.meadow[i] && L.homeOf[i] < 0 && clear(i))
			meadow.push_back(i);
	}
	scatterClumps(context, t, desert, int(scaledCount(desert.size() / kDesertPerOutcrop, o.stone)),
				  "bajada-outcrops", dryClear,
				  [&](MapGeneratorPoint p)
				  { placeResourceClump(map, context, p, STONE, 1 + int(context.bounded("bajada-outcrops", 2))); });
	scatterClumps(context, t, desert, int(scaledCount(desert.size() / kDesertPerScrub, o.wood)),
				  "bajada-scrub", dryClear,
				  [&](MapGeneratorPoint p)
				  {
					  if (map.isResourceAllowed(p.x, p.y, WOOD))
						  map.setResource(p.x, p.y, WOOD, 1);
				  });
	scatterClumps(context, t, meadow, int(scaledCount(meadow.size() / kMeadowPerGrove, o.fruit)),
				  "bajada-groves", [&](int i) { return clear(i); },
				  [&](MapGeneratorPoint p)
				  { placeResourceClump(map, context, p, CHERRY + int(context.bounded("bajada-groves", 3)), 1); });
	seedAlgae(map, context, t, "bajada-algae", o.algae, AlgaeBand::anyWater(kWaterPerAlgae));

	std::vector<unsigned char> topup(n, 0);
	for (int i = 0; i < n; ++i)
		topup[i] = L.farmOf[i] >= 0;
	secureStartingCrops(game, context, t, 24, 32, 0, &L.stone, &topup);
	// The relief for cramped starts may not take a home's own crops, which every home shares.
	std::vector<unsigned char> kept = L.stone;
	for (int k = 0; k < teams; ++k)
		for (const StencilTile &s : L.stencil)
			if (s.crop)
			{
				const auto [x, y] = turnStencilTile(L.facing[k], s.dx, s.dy);
				kept[t.at(L.fans[k].x + x, L.fans[k].y + y)] = 1;
			}
	reopenCrampedStarts(game, context, {o.wheat, o.wood, o.stone, o.algae, o.fruit}, 24, 32, 0, &kept);
	context.stage = "bajada routes";
	openColonyRoutes(map, context, t, StepCosts{1, 3, -1, -1, -1}, 0, &L.stone);
	return true;
}

std::string validateWorld(const Game &game, const GenerationContext &context)
{
	GenerationContext replay(context.request);
	const Layout L = design(context.request, replay);
	const Map &map = game.map;
	if (const std::string mismatch = designMismatch(L, map, "bajada"); !mismatch.empty())
		return mismatch;
	const Torus &t = L.t;
	const int teams = context.request.nbTeams;
	const auto at = [&](int i) { return std::to_string(i % t.w) + ", " + std::to_string(i / t.w); };
	if (const std::string lost = homePondMissing(map, t, L.kits, teams, "spring", "pond"); !lost.empty())
		return lost;
	for (int i = 0; i < t.size(); ++i)
		if (L.stone[i] && map.isGrass(i % t.w, i / t.w) && map.getResource(i % t.w, i / t.w).type != STONE)
			return "A range has lost its stone at (" + at(i) + ").";
	// Every home's terrain is the first home's, tile for tile, turned as the stencil is, except where
	// two homes' footprints overlap on a crowded map (their fans merge there).
	std::vector<unsigned char> covers(t.size(), 0);
	for (int k = 0; k < teams; ++k)
		for (const StencilTile &s : L.stencil)
		{
			const auto [x, y] = turnStencilTile(L.facing[k], s.dx, s.dy);
			unsigned char &c = covers[t.at(L.fans[k].x + x, L.fans[k].y + y)];
			c = std::min(2, c + 1);
		}
	for (int k = 1; k < teams; ++k)
		for (const StencilTile &s : L.stencil)
		{
			if (!s.inner)
				continue;
			const auto coverAt = [&](int colony)
			{
				const auto [x, y] = turnStencilTile(L.facing[colony], s.dx, s.dy);
				return covers[t.at(L.fans[colony].x + x, L.fans[colony].y + y)];
			};
			if (coverAt(k) > 1 || coverAt(0) > 1)
				continue;
			const auto kind = [&](int colony)
			{
				const auto [x, y] = turnStencilTile(L.facing[colony], s.dx, s.dy);
				const int px = t.x(L.fans[colony].x + x), py = t.y(L.fans[colony].y + y);
				return map.isWater(px, py) ? 2 : map.isGrass(px, py) ? 0 : 1;
			};
			if (kind(k) != kind(0))
			{
				const auto [x, y] = turnStencilTile(L.facing[k], s.dx, s.dy);
				return "Colony " + std::to_string(k) + "'s home differs from colony 0's at (" +
					   at(t.at(L.fans[k].x + x, L.fans[k].y + y)) + ").";
			}
		}
	// And every home was sown alike: each colony still holds its stencil's crops (the kit and its share
	// of farmland), give or take the few a route or a swarm's clearance took.
	for (int k = 0; k < teams; ++k)
	{
		int missing = 0;
		for (const StencilTile &s : L.stencil)
			if (s.crop)
			{
				const auto [x, y] = turnStencilTile(L.facing[k], s.dx, s.dy);
				const int px = t.x(L.fans[k].x + x), py = t.y(L.fans[k].y + y);
				if (covers[t.at(px, py)] > 1)
					continue;
				missing += !map.isResource(px, py) ||
						   map.getResource(px, py).type != (s.crop == 1 ? WHEAT : WOOD);
				}
		if (missing > kCropsLost)
			return "Colony " + std::to_string(k) + "'s home has lost " + std::to_string(missing) +
				   " of its starting crops.";
	}
	// No town starts with a crop.
	for (int i = 0; i < t.size(); ++i)
		if (L.townOf[i] >= 0 && map.isResource(i % t.w, i / t.w))
		{
			const int type = map.getResource(i % t.w, i / t.w).type;
			if (type == WHEAT || type == WOOD)
				return "Colony " + std::to_string(L.townOf[i]) + "'s town has a crop at (" + at(i) + ").";
		}
	// The ranges are walls: with every pass shut, nothing walks from one face of a range to the other
	// inside the band the range, its massifs, roughness and shoulders can occupy (spurs only reach
	// outwards).
	{
		const double band = L.halfRidge * (1 + kMassif) + kRidgeRough + kPassShoulder + 2;
		for (int r = 0; r < L.ranges; ++r)
		{
			std::vector<unsigned char> open(t.size(), 0), north(t.size(), 0);
			std::vector<double> offset(t.size(), -1e9);
			for (int y = 0; y < t.h; ++y)
				for (int x = 0; x < t.w; ++x)
				{
					const int i = y * t.w + x;
					const double d = wrappedOffset(y + 0.5, L.ridgeCentre[r][x], t.h);
					if (std::abs(d) >= band)
						continue;
					offset[i] = d;
					const bool stone = map.isResource(x, y) && map.getResource(x, y).type == STONE;
					open[i] = !L.pass[i] && !map.isWater(x, y) && !stone;
					north[i] = d < -band + 1 && open[i];
				}
			const Flood flood = floodFrom(t, north, open);
			for (int i : flood.visited)
				if (offset[i] > band - 1)
					return "Range " + std::to_string(r) + " can be crossed outside its passes at (" + at(i) + ").";
		}
	}
	// Every town's ring seals it from crops: no pure grass of a town touches grass outside it where a
	// crop could grow. (On its gravel side a town opens onto ground no crop reaches.)
	{
		const Fertility::Field fertility = Fertility::forMap(map, false);
		std::vector<int> ground(t.size(), -1);
		std::vector<unsigned char> grass(t.size(), 0);
		for (int i = 0; i < t.size(); ++i)
		{
			const bool inTown = L.townOf[i] >= 0;
			ground[i] = inTown ? L.townOf[i] : teams;
			grass[i] = map.isGrass(i % t.w, i / t.w) && (inTown || fertility.at(i % t.w, i / t.w) > 0);
		}
		if (const RegionLeak leak = firstRegionLeak(t, grass, ground, [](int, int) { return false; });
			leak.tile >= 0)
			return "A town's ring has a gap at (" + at(leak.tile) + ").";
	}
	return walkFromFirstColony(map, teams, "the bajadas", "through the passes").error;
}
} // namespace

BajadaOptions::BajadaOptions(const GenerationRequest &r)
	: rangeSpacing(r.option("range-spacing")), ridgeWidth(r.option("ridge-width")),
	  passes(r.option("passes")), fans(r.option("fans")),
	  streamReach(r.option("stream-reach")), playa(r.option("playa")),
	  homeDesign(r.option("home-design")), wheat(r.option("wheat-amount")),
	  wood(r.option("wood-amount")), stone(r.option("stone-amount")),
	  algae(r.option("algae-amount")), fruit(r.option("fruit-amount"))
{
}

GeneratorDefinition bajadaDefinition()
{
	return {
			"bajada",
			55,
			"Bajada",
			1,
			false,
			{{"range-spacing", "Range spacing", 128, 256, 16, 128, ControlGroup::Terrain},
			 {"ridge-width", "Ridge width", 8, 16, 1, 12, ControlGroup::Terrain},
			 {"passes", "Passes", 1, 6, 1, 2, ControlGroup::Layout},
			 {"fans", "Fans per range", 3, 8, 1, 6, ControlGroup::Terrain},
			 {"stream-reach", "Stream reach", 70, 150, 10, 100, ControlGroup::Terrain},
			 {"playa", "Playa", 0, 100, 10, 80, ControlGroup::Terrain},
			 GeneratorControl::choice("home-design", "Home design",
									  {"Random", "Broad fan", "Long fan", "Twin springs"}, 0,
									  ControlGroup::Layout),
			 // Above 200% every fan and meadow is already sown (a study of 510 maps: 3,360 wheat tiles at
			 // 200%, 3,530 at 300%).
			 GeneratorControl::percentage("wheat-amount", "Wheat amount", 200),
			 GeneratorControl::percentage("wood-amount", "Wood amount"),
			 GeneratorControl::percentage("stone-amount", "Stone amount"),
			 GeneratorControl::percentage("algae-amount", "Algae amount"),
			 GeneratorControl::percentage("fruit-amount", "Fruit amount")},
			generate,
			true,
			designFailure<design>,
			validateWorld,
			{"terrain:natural", "feature:mountains", "feature:desert", "feature:lakes",
			 "style:sprawling", "fairness:stamped-lattice"}};
}
