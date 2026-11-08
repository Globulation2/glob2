// SPDX-License-Identifier: GPL-3.0-or-later
#include "TidalFlatsGenerator.h"
#include "FertilityField.h"
#include "GenerationWorld.h"
#include "GenerationContext.h"
#include "Grid.h"
#include "Geometry.h"
#include "HeightMap.h"
#include "LatticeNoise.h"
#include "Morphology.h"
#include "Pipeline.h"
#include "Planting.h"
#include "Resources.h"
#include "Rock.h"
#include "Settlements.h"
#include "Sketch.h"
#include "TerrainCatalog.h"
#include "Topology.h"
#include "Unit.h"
#include "Wedge.h"
#include <algorithm>
#include <array>
#include <climits>
#include <cmath>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>
using namespace MapGeneration;

// Tidal flats: grass islands standing on a wide expanse of walkable tidal mud, threaded by sand
// channels and shell banks, with tide pools and lagoons of water over the flats and the odd grassy
// sandbar. The flats carry units but hold no building and no deposit, so there are open-field
// battles from the first minute and no forward bases: an army on the flats fights far from any inn
// or tower, while a defender holds a rim of towers on the island's edge. Expansion means taking
// another island whole. Every colony's island has a pond inland, fields beside it and a quarry;
// smaller islands round the flats carry a prize each, and an island at the centre of the map
// carries the orchard of all three fruits round a rocky tor with a quarry face turned to every
// colony.
//
// DESIGN BRIEF, REVISION 9 (TIDAL FLATS 2.0). Revision 8 drew the flats as sand; 2.0 draws them
// with the engine's full terrain palette, keeping the concept and the islands:
// - The flats are mudflat (Rough: walkable at 160/256, unbuildable, holds nothing). Unlike sand,
//   mud does not inhibit growth, so an island's fields regrow faster than on revision 8's sand,
//   and nothing on the flats can be built on or farmed, which is the concept's invariant. No
//   Barren and no Paths ground is laid on the flats.
// - Sand tidal channels thread the mud so the flats have routes: a wavy ring through every home
//   island (the way to the neighbours), a spoke through every home island from the centre to the
//   design's rim (the way to the middle), an inner ring between the spokes when there is room, and
//   short creeks off the ring. Sand walks at full speed, so the mud is slow ground with fast ways
//   through it. Shell banks (sand, pale look) ring every pool and lagoon on the flats; they are
//   also what lets algae regrow there, since algae needs solid sand within reach of its probe.
// - Lagoons are long tidal channels of water with a deep core (deepCore, DEEP_WATER: swimmable
//   at 192/256, no algae, still waters land); their shallow margins keep algae and beaches.
//   Revision 8's lagoons were too large to fit any wedge at the defaults; they are smaller now.
// - The central island's pond becomes a rocky tor (Ridges, OUTCROP look), rock vertices only where
//   all four tiles are rock (rockVertices), with a quarry face turned to every colony: plain grass
//   quarry tiles left out of the rock and stocked with stone (quarryNear, quarryApproach,
//   quarryFault). Telemetry measures every colony's walk to the nearest face.
// - The tide (`tide`): neap, mean or spring, random by default. It scales the water of every
//   pool and lagoon (0.8, 1, 1.25 of its radius; spring water keeps kSpringClear tiles from any
//   island), widens the shell banks at neap, and moves the looks: wet mud by the water, dried
//   hardpan far from it. The islands and channels are the same at every tide for one seed.
// - Looks are painted over a mechanic sketch (canonical GRASS, SAND, WATER, DEEP_WATER, MUDFLAT,
//   RIDGE_ROCK) with composeLooks, so looks never change a rule: sandbars and oases lush, shell
//   banks pale, wet mud and hardpan on the flats, the tor outcrop. Home islands and the central
//   island stay plain grass, which the starting-crop guarantee needs.
//
// Every island, pool, sandbar and channel is designed in the frame of a colony's wedge - arc across
// it and radius out from the centre - and stamped into every wedge alike, so whatever the roll,
// every colony gets the same neighbourhood turned round the centre, and the layout is fair for any
// colony count. The whole design is a pure function of the request, so validateWorld rebuilds it
// and checks the finished world. The home and neutral islands are placed exactly as revision 8
// placed them for the same seed; revision 9's lagoons fit a wedge (revision 8's never did) and are
// placed before the sandbars and tide pools, so those land elsewhere when a lagoon takes their spot.
//
// GAME RULES BEHIND IT (docs/map-generators/game-rules-for-map-design.md): buildings need pure
// grass, so the islands are the only building ground; deposits stand only on grass, so the flats
// stay bare whatever grows; and farmland needs water, so every island's pond is what keeps its
// fields alive. Neutral islands arrive already under wheat, which blocks building until cleared,
// so taking one is a visible commitment. The central orchard of all three fruits is the prize:
// an inn stocked with them pulls hungry enemy units across.
//
// THE SIZES AT THE DEFAULTS (256x256, 4 colonies, island size 20): home islands about 25 tiles in
// radius on a ring 74 tiles from the centre, a central island about 17 in radius with a tor of
// radius 4.7, and a pond of radius 5 on each home island; six oasis islands and eight sandbars per
// wedge.
//
// FEEDBACK 2026-09-14: "it needs to have more of those pond + wheat or pond + wood oases, especially on
// smaller maps with more players ... also want to see a few more of the random 'green patches' within
// the sandy desert which can provide small building spaces." So the oases default to six per wedge
// (was three), are drawn smaller and rounder, and keep less sand from each other (kOasisGap) and
// from the home islands (kOasisHomeGap, where every feature used to keep kIslandGap), so more of
// them fit a narrow wedge; and the sandbars, which are those green patches, default to eight per
// wedge (was three), a little bigger, and keep the same smaller gaps. The scatter also tries three
// times as hard to seat each one. A 128 map with six colonies is the limit: its home islands nearly
// touch, and only an oasis or two of the smallest size fits between them and the central island.
namespace
{

// The home islands' centres lie this far out, as a share of the half side, and keep this much flat
// from each other, from the map's wrap and from every other island.
constexpr double kHomeRing = 0.58;
constexpr int kIslandGap = 8;
// Every home starts identical: wheat and wood beside its pond, a quarry, all unscaled.
constexpr int kHomeWheat = 40;
constexpr int kHomeWood = 30;
// Neutral islands' radii as shares of the home islands' (never below a size that holds a pond and
// a ring of wheat round it; smaller since 2026-09-14 so more fit a crowded wedge), how rough their
// coasts are (rounder than the home islands, so a small one is not all beach), and the central
// island's radius as a share of the half side.
constexpr double kNeutralLow = 0.30, kNeutralHigh = 0.45;
constexpr double kNeutralMinimum = 3.5, kNeutralRoughness = 0.25;
// Oases and sandbars keep this much flat from every other feature, and from a home island
// kOasisHomeGap (water features keep kIslandGap from a home, so its beach stays dry): the less they
// keep, the more of them a narrow wedge seats, which is what a small map with many colonies needs
// (2026-09-14).
constexpr int kOasisGap = 3, kSandbarGap = 3, kOasisHomeGap = 4;
constexpr double kCentralShare = 0.13;
// Sandbars are just big enough to hold an inn and a tower: a green patch a forward post can stand on.
constexpr double kSandbarLow = 3.5, kSandbarHigh = 5.0;
// Lagoons: long tidal channels of water, 3.5 to 5.5 tiles in radius before their 1.2 to 2 times
// stretch, keeping 3 tiles of flat from other features and kIslandGap from a home island. Revision 8 drew them 8 to 14 tiles with a
// gap of 6, which fitted no wedge at the defaults, so no map had one.
constexpr double kLagoonLow = 3.5, kLagoonHigh = 5.5;
constexpr int kLagoonGap = 3, kLagoonHomeGap = 6;
constexpr double kLagoonRoughness = 0.2;
// A lagoon's water at least kDeepSteps vertex steps from any other ground is deep; cores smaller
// than kDeepLeast vertices are left shallow so a deep patch reads as a channel's bed, not a speck.
constexpr int kDeepSteps = 2, kDeepLeast = 4;

// The tides. `water` scales every pool and lagoon on the flats; `bank` is how many vertex steps of
// shell bank ring their water; `wet` how far wet mud reaches from any water (looks); `hardpan`
// the share of the flats far from water that has dried to hardpan (looks).
struct TideSpec
{
	const char *name;
	double water;
	int bank, wet;
	double hardpan;
};
constexpr std::array<TideSpec, 3> kTides{{{"neap", 0.8, 3, 2, 0.34},
										  {"mean", 1.0, 2, 3, 0.16},
										  {"spring", 1.25, 2, 5, 0.05}}};
// A bank's width sways kBankFray tiles either way on noise kBankCell tiles across, its contrast
// stretched by kBankContrast (smooth noise stays near its middle), never below kBankLeast: a pool
// a few tiles across sees a wide drift of shells on one side and a thin rim on another, not a ring
// (a 7-tile cell and 1.2 tiles of sway drew nearly even rings round every pool, bull's-eyes with
// the wet mud beyond them).
constexpr double kBankFray = 2.2, kBankLeast = 1.0, kBankCell = 5, kBankContrast = 2.2;
// Wet mud (looks): see lookSketch.
constexpr int kSeepCell = 6;
constexpr double kSeepContrast = 2.2;
// Spring water keeps this many tiles from every island, so a swollen pool never eats a coast.
constexpr int kSpringClear = 4;

// The channels' half widths in tiles (a channel three vertices across leaves two tiles of pure
// sand down its middle) and the creeks' taper to their tips.
constexpr double kRingHalf = 1.7, kSpokeHalf = 1.5, kInnerHalf = 1.4, kCreekHalf = 1.4;
constexpr double kCreekTaper = 0.55;
// Creeks per wedge: one for every kCreekArc tiles of the ring's arc across it, at most
// kCreeksPerWedge, so a small map crowded with colonies is not all channel (three creeks in a 39-tile
// wedge at 128x128 with six colonies left almost no mud between the sand).
constexpr int kCreeksPerWedge = 3;
constexpr double kCreekArc = 24, kInnerArc = 24;

// The tor: a knot of rock at the central island's middle, this share of the island's radius,
// between 3.5 and 7 tiles, a little rough; each colony's quarry face is the rock within one tile of
// the rim point facing its home, or that rim tile alone where the rim gives each colony fewer than
// kTorFaceRim tiles: on a 128 map with six colonies three-tile faces took the whole rim and left a
// tor of 2 rock vertices, a speck. Fewer than kTorRockLeast rock vertices refuses the map.
constexpr double kTorShare = 0.35, kTorLeast = 3.5, kTorMost = 7.0, kTorRoughness = 0.15;
constexpr double kTorFaceRim = 6;
constexpr int kTorRockLeast = 9;

struct Geometry
{
	int teams, half;
	double homeRing, homeRadius, centralRadius, amplitude;
};

Geometry geometryFor(const GenerationRequest &r)
{
	const TidalFlatsOptions o(r);
	Geometry g;
	g.teams = std::max(1, r.nbTeams);
	g.half = std::min(1 << r.wDec, 1 << r.hDec) / 2;
	g.homeRing = kHomeRing * g.half;
	// The central island is 13% of the half side (at least 8 tiles, enough for its tor and the
	// orchard round it). Roughness 100 lets a coast wobble up to 35% of its radius: rough enough to
	// read as a natural island, while two neighbouring islands, spaced for their fullest extent,
	// still keep kIslandGap of flat between them.
	g.centralRadius = o.centralIsland ? std::max(8.0, kCentralShare * g.half) : 0.0;
	g.amplitude = o.coastRoughness / 100.0 * 0.35;
	// The control is the islands' radius as a share of the half side; with many colonies on the
	// ring, or a small map, the islands shrink to what the ring, the wrap and the central island
	// leave room for, and validateRequest refuses a map where that is too small for a base.
	double radius = o.homeIslandSize / 100.0 * g.half;
	const double reach = 1 + g.amplitude;
	if (g.teams >= 2)
		radius =
			std::min(radius, (2 * g.homeRing * std::sin(kPi / g.teams) - kIslandGap) / (2 * reach));
	radius = std::min(radius, (g.half - kIslandGap / 2.0 - g.homeRing) / reach);
	if (g.centralRadius > 0)
		// 1.3 covers the central island's own wobble (its roughness is 60% of the others', at most
		// 21%), so a home island never runs into it.
		radius = std::min(radius, (g.homeRing - g.centralRadius * 1.3 - kIslandGap) / reach);
	g.homeRadius = std::floor(radius);
	return g;
}

// A feature in a wedge's frame (Blob) and what it is. Shapes are evaluated in the wedge's frame
// too, so every wedge gets the same.
enum Kind
{
	HomeIsland = 0,
	Neutral,
	Central,
	Sandbar,
	Pool,
	Lagoon
};
struct Feature : Blob
{
	Kind kind;
	double reach; // the blob's fullest extent, stretched
	int prize;    // neutral islands: 0 fruit, 1 stone (every other island in turn)
};

// A creek off the ring channel, in the wedge's frame: from (s0, d0) along `heading` (radians from
// straight out, towards positive arc) for `length` tiles, meandering `amplitude` tiles either side.
struct Creek
{
	double s0, d0, heading, length, amplitude, wave, phase;
};
// The tidal channels, every one in the wedge's frame so every wedge has the same. The ring and the
// spoke each sway on two waves, a long one and a short one, so they meander rather than run as
// chords and spokes drawn with a ruler; whole numbers of waves per wedge keep the ring continuous
// across the wedge boundaries.
struct Channels
{
	double ringRadius = 0, ringAmplitude = 0, ringPhase = 0, ringRipple = 0, ripplePhase = 0;
	int ringWaves = 1, rippleWaves = 3;
	double spokeAmplitude = 0, spokeWave = 1, spokePhase = 0;
	double spokeRipple = 0, spokeRippleWave = 1, spokeRipplePhase = 0;
	double innerRadius = 0, innerAmplitude = 0, innerPhase = 0;
	int innerWaves = 1;
	bool inner = false;
	std::vector<Creek> creeks;
	double ring(double u) const
	{
		return ringRadius + ringAmplitude * std::sin(2 * kPi * ringWaves * u + ringPhase) +
			   ringRipple * std::sin(2 * kPi * rippleWaves * u + ripplePhase);
	}
	double spoke(double d) const
	{
		return (spokeAmplitude * std::sin(2 * kPi * d / spokeWave + spokePhase) +
				spokeRipple * std::sin(2 * kPi * d / spokeRippleWave + spokeRipplePhase)) *
			   std::min(1.0, d / 24.0);
	}
};

struct Layout
{
	Torus t{1, 1};
	Geometry g{};
	int cx = 0, cy = 0;
	double phase = 0;
	int tide = 1; // index into kTides
	// The flats are designed round the centre in a circle on the map's shorter side; this places
	// them on the map, stretched along the longer side so the islands fill a rectangular map.
	Stretch stretch;
	std::vector<Feature> blobs;    // in the wedge's frame
	Channels channels;             // in the wedge's frame
	std::vector<int> homeX, homeY; // every home island's centre
	std::vector<int> pondX, pondY; // and its pond's
	std::vector<unsigned char> grass, water;
	std::vector<unsigned char> lagoon;  // water of a lagoon
	std::vector<unsigned char> channel; // a sand channel vertex on the flats
	std::vector<unsigned char> bank;    // a shell bank vertex
	std::vector<unsigned char> tor;     // a tile of the central tor, quarry faces included
	std::vector<unsigned char> quarry;  // the tor's quarry tiles
	std::vector<int> quarryOf;          // the colony a quarry tile faces, else -1
	std::vector<int> islandOf; // -1 flats, -2 central, -3 neutral or sandbar, k a home
	std::vector<int> prizeOf;  // neutral islands: the prize, else -1
	std::vector<unsigned char> sandbar; // a sandbar's tile
	std::vector<double> radius, angle;
	double torRadius = 0;
	// The mechanic sketch: the canonical type of every vertex's rule group.
	TerrainSketch terrain;
	std::string failure;
};

double torRadiusFor(const Geometry &g)
{
	return g.centralRadius > 0 ? std::clamp(kTorShare * g.centralRadius, kTorLeast, kTorMost) : 0.0;
}

// The channels' shapes, drawn from their own stream so they move nothing revision 8 placed.
Channels drawChannels(const Geometry &g, double arcHalfAt, GenerationContext &context)
{
	Channels c;
	const char *stream = "flats-channels";
	const auto unit = [&](int bound) { return context.bounded(stream, bound) / double(bound); };
	// The ring's long wave: one or two per wedge; its ripple: one about every 30 tiles of arc.
	const double arc = 2 * arcHalfAt * g.homeRing;
	c.ringRadius = g.homeRing;
	c.ringWaves = 1 + int(context.bounded(stream, 2));
	c.ringAmplitude = 2.0 + unit(1000) * std::max(1.0, 0.05 * g.half);
	c.ringPhase = unit(3600) * 2 * kPi;
	c.rippleWaves = std::max(2, int(std::lround(arc / 30.0)));
	c.ringRipple = 1.0 + unit(1000) * 1.5;
	c.ripplePhase = unit(3600) * 2 * kPi;
	c.spokeAmplitude = 1.5 + unit(1000) * std::max(1.0, 0.04 * g.half);
	c.spokeWave = std::max(16.0, (0.25 + unit(1000) * 0.2) * g.half);
	c.spokePhase = unit(3600) * 2 * kPi;
	c.spokeRipple = 0.8 + unit(1000) * 1.2;
	c.spokeRippleWave = 11 + unit(1000) * 8;
	c.spokeRipplePhase = unit(3600) * 2 * kPi;
	// The inner ring runs midway between the central island and the home islands' inner coasts,
	// when that leaves room for it, and only where each wedge's stretch of it is kInnerArc tiles or
	// more: with six colonies on a 128 map its short arcs and the spokes drew a web of sand round
	// the central island.
	const double inside = g.centralRadius > 0 ? g.centralRadius * 1.3 : 6.0;
	const double outside = g.homeRing - g.homeRadius * (1 + g.amplitude);
	c.innerRadius = (inside + outside) / 2;
	c.inner = outside - inside >= 12 && 2 * arcHalfAt * c.innerRadius >= kInnerArc;
	c.innerAmplitude = std::min(0.25 * (outside - inside) - 1, 1.5 + unit(1000) * 3);
	c.innerWaves = 1 + int(context.bounded(stream, 3));
	c.innerPhase = unit(3600) * 2 * kPi;
	// Creeks leave the ring away from the home island, out or in, and stay inside their wedge.
	const int creeks = std::clamp(int(arc / kCreekArc), 0, kCreeksPerWedge);
	for (int k = 0, attempts = 0; k < creeks && attempts < 60; ++attempts)
	{
		const double side = context.bounded(stream, 2) ? 1 : -1;
		const double u = 0.5 + side * (0.15 + unit(1000) * 0.3);
		const bool out = context.bounded(stream, 2) == 0;
		const double heading = (out ? 0 : kPi) + side * (0.35 + unit(1000) * 0.7) * (out ? 1 : -1);
		const double length = (0.08 + unit(1000) * 0.10) * g.half + 6;
		Creek creek;
		creek.d0 = c.ring(u);
		creek.s0 = (u - 0.5) * 2 * arcHalfAt * creek.d0;
		creek.heading = heading;
		creek.length = length;
		creek.amplitude = 1.0 + unit(1000) * 1.5;
		creek.wave = 10 + unit(1000) * 8;
		creek.phase = unit(3600) * 2 * kPi;
		const double s1 = creek.s0 + length * std::sin(heading),
					 d1 = creek.d0 + length * std::cos(heading);
		// Both ends three tiles inside the wedge and the tip inside the design circle and clear of
		// the centre.
		if (std::abs(creek.s0) + 3 > arcHalfAt * creek.d0 || std::abs(s1) + 3 > arcHalfAt * d1 ||
			d1 > g.half - 4 || d1 < 8 + g.centralRadius * 1.3)
			continue;
		c.creeks.push_back(creek);
		++k;
	}
	return c;
}

// Whether the cell at arc offset `s`, radius `d` and share `u` of the way round its wedge lies in a
// tidal channel.
bool inChannel(const Channels &c, const Geometry &g, double arcHalfAt, double s, double d, double u)
{
	if (std::abs(d - c.ring(u)) < kRingHalf)
		return true;
	if (c.inner)
	{
		const double inner =
			c.innerRadius + c.innerAmplitude * std::sin(2 * kPi * c.innerWaves * u + c.innerPhase);
		if (std::abs(d - inner) < kInnerHalf)
			return true;
	}
	// The spoke runs straight through the home island from the centre (or the central island) to
	// four tiles short of the design circle, swaying less near the centre where the wedge narrows.
	if (d < g.half - 4)
	{
		const double sway = c.spoke(d);
		const double limit = std::max(0.0, arcHalfAt * d - kSpokeHalf - 1);
		if (std::abs(s - std::clamp(sway, -limit, limit)) < kSpokeHalf)
			return true;
	}
	for (const Creek &k : c.creeks)
	{
		const double ds = s - k.s0, dd = d - k.d0;
		const double along = ds * std::sin(k.heading) + dd * std::cos(k.heading);
		if (along < 0 || along > k.length)
			continue;
		const double across = ds * std::cos(k.heading) - dd * std::sin(k.heading);
		const double share = along / k.length;
		const double offset = across - k.amplitude * share * std::sin(2 * kPi * along / k.wave + k.phase);
		if (std::abs(offset) < kCreekHalf * (1 - kCreekTaper * share))
			return true;
	}
	return false;
}

int tideOf(const TidalFlatsOptions &o)
{
	return std::clamp(o.tide, 1, 3) - 1;
}

Layout design(const GenerationRequest &request, GenerationContext &context)
{
	Layout L;
	L.t = {1 << request.wDec, 1 << request.hDec};
	L.g = geometryFor(request);
	const Torus &t = L.t;
	const Geometry &g = L.g;
	const int n = t.w * t.h, teams = g.teams;
	const TidalFlatsOptions o(request);
	L.tide = tideOf(o);
	const TideSpec &tide = kTides[L.tide];
	L.cx = t.w / 2;
	L.cy = t.h / 2;
	L.stretch = Stretch::toFill(t.w, t.h);
	context.telemetry.measure("tidal-flats.homes.actual-radius", g.homeRadius);
	context.telemetry.measure("tidal-flats.homes.ring", g.homeRing);
	context.telemetry.measure("tidal-flats.central.radius", g.centralRadius);
	context.telemetry.choice("tidal-flats.tide", tide.name);
	L.phase = context.bounded("flats-layout", 3600) / 3600.0 * 2 * kPi;
	const double wedge = 2 * kPi / teams;
	const double arcHalfAt = [&](double d) { return teams < 2 ? kPi * d : kPi * d / teams; }(1.0);

	// Everything in one wedge, then stamped into every wedge: the home island in the middle of the
	// ring, then neutral islands, sandbars, lagoons and pools that keep clear of each other, of the
	// central island and of the map's wrap.
	std::vector<Feature> &blobs = L.blobs;
	blobs.push_back({{0.0, g.homeRing, 1.0, 0.0,
					  RadialShape(g.homeRadius, g.amplitude, context, "flats-coast")},
					 HomeIsland,
					 0,
					 -1});
	blobs.back().reach = blobs.back().shape.maximumRadius();
	const auto fits = [&](double s0, double r0, double reach, double gap, double homeGap)
	{
		// A feature keeps 6 tiles from the very centre (plus the central island and a gap when
		// there is one) and 4 from the edge of the design circle, stays inside its wedge by half a
		// gap, and keeps its gap from every feature already placed (homeGap from a home island).
		if (r0 - reach < 6 + g.centralRadius * 1.3 + (g.centralRadius > 0 ? gap : 0))
			return false;
		if (r0 + reach > g.half - 4)
			return false;
		if (teams >= 2 && std::abs(s0) + reach + gap / 2 > arcHalfAt * r0)
			return false;
		for (const Feature &b : blobs)
		{
			const double need =
				reach + b.reach + std::max(gap, b.kind == HomeIsland ? homeGap : gap);
			if (std::hypot(s0 - b.s, r0 - b.r) < need)
				return false;
		}
		return true;
	};
	const auto scatter = [&](Kind kind, int wanted, double low, double high, double roughness,
							 double gap, double homeGap, const char *stream, bool stretched)
	{
		const size_t beforeCount = blobs.size();
		context.telemetry.measure("tidal-flats.features.requested", wanted, int(kind));
		// Features are drawn at random across the wedge, from 8 tiles out from the centre to 8
		// tiles short of the design circle, with 240 tries each (80 until 2026-09-14: a crowded
		// wedge needs the extra draws to find its last few spots); one that fits nowhere is left
		// out, the same for every wedge. Lagoons are stretched 1.2 to 2 times and turned, so they
		// read as long tidal channels rather than round pools. Water is placed at the mean tide's
		// size; a spring tide's extra water is kept off the islands below.
		for (int attempt = 0, placed = 0; attempt < wanted * 240 && placed < wanted; ++attempt)
		{
			const double r0 = 8 + context.bounded(stream, 1000) / 1000.0 * (g.half - 16);
			const double s0 = (context.bounded(stream, 2001) / 1000.0 - 1) * arcHalfAt * r0;
			const double radius = low + context.bounded(stream, 1000) / 1000.0 * (high - low);
			const double stretch = stretched ? 1.2 + context.bounded(stream, 81) / 100.0 : 1.0;
			const double turn = stretched ? context.bounded(stream, 3600) / 3600.0 * kPi : 0.0;
			RadialShape shape(radius, roughness, context, stream);
			const double reach = shape.maximumRadius() * stretch;
			if (!fits(s0, r0, reach, gap, homeGap))
				continue;
			blobs.push_back({{s0, r0, stretch, turn, shape}, kind, reach, placed % 2});
			++placed;
		}
		context.telemetry.measure("tidal-flats.features.actual", blobs.size() - beforeCount,
								  int(kind));
		if (blobs.size() - beforeCount < size_t(wanted))
			context.telemetry.fallback("tidal-flats.features.omitted",
									   "Placement budget exhausted for crowded wedge", int(kind));
	};
	scatter(Neutral, o.extraIslands, std::max(kNeutralMinimum, kNeutralLow * g.homeRadius),
			std::max(kNeutralMinimum + 1.5, kNeutralHigh * g.homeRadius), kNeutralRoughness,
			kOasisGap, kOasisHomeGap, "flats-islands", false);
	// Sandbars keep kSandbarGap tiles of flat round them and lagoons kLagoonGap, tide pools 4: small
	// water keeps the flats walkable round it. Tide pools are 1.5 to 4 tiles in radius, counted per
	// 16384 tiles of wedge so their density is the same on every map size.
	scatter(Lagoon, o.lagoons, kLagoonLow, kLagoonHigh, kLagoonRoughness, kLagoonGap, kLagoonHomeGap,
			"flats-lagoons", true);
	scatter(Sandbar, o.sandbars, kSandbarLow, kSandbarHigh, 0.3, kSandbarGap, kOasisHomeGap,
			"flats-sandbars", false);
	const double wedgeArea = wedge * g.half * g.half / 2;
	scatter(Pool, int(std::lround(o.tidePools * wedgeArea / 16384.0)), 1.5, 4.0, 0.4, 4, kIslandGap,
			"flats-pools", false);
	const RadialShape central(std::max(1.0, g.centralRadius), g.amplitude * 0.6, context,
							  "flats-central");
	L.channels = drawChannels(g, arcHalfAt, context);
	context.telemetry.measure("tidal-flats.channels.inner-ring", L.channels.inner);
	context.telemetry.measure("tidal-flats.channels.creeks", L.channels.creeks.size());
	L.torRadius = torRadiusFor(g);
	const RadialShape torShape(std::max(1.0, L.torRadius), kTorRoughness, context, "flats-tor");
	// The shell banks' width sways by up to kBankFray tiles either way on noise sampled in the
	// wedge's frame, so every wedge has the same banks and none is a ring drawn with compasses.
	const PeriodicNoise bankNoise(512, 512, kBankCell, context.stream("flats-banks"));
	std::vector<float> bankFray(n, 0.5f);

	L.grass.assign(n, 0);
	L.water.assign(n, 0);
	L.lagoon.assign(n, 0);
	L.channel.assign(n, 0);
	L.tor.assign(n, 0);
	L.sandbar.assign(n, 0);
	L.islandOf.assign(n, -1);
	L.prizeOf.assign(n, -1);
	L.radius.assign(n, 0.0);
	L.angle.assign(n, 0.0);
	L.homeX.assign(teams, 0);
	L.homeY.assign(teams, 0);
	L.pondX.assign(teams, 0);
	L.pondY.assign(teams, 0);
	for (int k = 0; k < teams; ++k)
	{
		const double a = L.phase + wedge * (k + 0.5);
		L.homeX[k] = t.x(L.cx + int(std::lround(g.homeRing * std::cos(a) * L.stretch.sx)));
		L.homeY[k] = t.y(L.cy + int(std::lround(g.homeRing * std::sin(a) * L.stretch.sy)));
		L.pondX[k] = L.homeX[k];
		L.pondY[k] = L.homeY[k];
	}
	// A home's pond is a fifth of its island's radius, 3 to 6 tiles: wide enough to stay water
	// inside its own beach, and small enough to leave the island its building room. Oasis ponds are
	// 2.5, shrunk in step with the island below kOasisPondReach tiles of reach (an island of
	// radius 5.5 or so) so the smallest oasis, a crowded wedge's, is still a pond in a ring of
	// wheat rather than a pond with a beach.
	const double pondRadius = std::clamp(0.2 * g.homeRadius, 3.0, 6.0);
	const RadialShape pond(pondRadius, 0.3, context, "flats-ponds");
	const RadialShape oasis(2.5, 0.3, context, "flats-ponds");
	constexpr double kOasisPondReach = 6.5;
	const WedgeFrame frame(t, L.phase, teams, L.stretch);
	// The tide scales the water on the flats; a spring tide's water beyond the mean's is only a
	// candidate until it is known to keep clear of every island.
	const double low = std::min(1.0, tide.water), high = tide.water;
	std::vector<unsigned char> swell(n, 0), swellLagoon(n, 0);
	for (int y = 0; y < t.h; ++y)
		for (int x = 0; x < t.w; ++x)
		{
			const int i = y * t.w + x;
			const WedgeFrame::Cell cell = frame.cell(x, y);
			const double d = cell.d, theta = cell.theta;
			L.radius[i] = d;
			L.angle[i] = theta;
			if (g.centralRadius > 0 && d < central.radiusAt(theta))
			{
				L.grass[i] = 1;
				L.islandOf[i] = -2;
				if (d < torShape.radiusAt(theta))
					L.tor[i] = 1;
				continue;
			}
			const int k = cell.k;
			const double s = cell.s;
			L.channel[i] = inChannel(L.channels, g, arcHalfAt, s, d, cell.u);
			bankFray[i] = float(bankNoise.at(s + 256, d));
			for (const Feature &b : blobs)
			{
				if (b.kind == Pool || b.kind == Lagoon)
				{
					if (b.holds((s - b.s) / low, (d - b.r) / low))
					{
						L.water[i] = 1;
						L.grass[i] = 0;
						L.islandOf[i] = -1;
						L.lagoon[i] = L.lagoon[i] || b.kind == Lagoon;
					}
					else if (high > 1 && b.holds((s - b.s) / high, (d - b.r) / high))
					{
						swell[i] = 1;
						swellLagoon[i] = swellLagoon[i] || b.kind == Lagoon;
					}
					continue;
				}
				if (!b.holds(s - b.s, d - b.r))
					continue;
				L.grass[i] = 1;
				L.islandOf[i] = b.kind == HomeIsland ? k : -3;
				if (b.kind == Neutral)
					L.prizeOf[i] = b.prize;
				if (b.kind == Sandbar)
					L.sandbar[i] = 1;
				if (b.kind == HomeIsland &&
					std::hypot(s - b.s, d - b.r) < pond.radiusAt(std::atan2(d - b.r, s - b.s)))
					L.water[i] = 1;
				if (b.kind == Neutral && std::hypot(s - b.s, d - b.r) <
											 oasis.radiusAt(std::atan2(d - b.r, s - b.s)) *
												 std::min(1.0, b.reach / kOasisPondReach))
					L.water[i] = 1;
			}
		}
	int swelled = 0;
	if (high > 1)
	{
		const std::vector<int> fromIsland = stepsFrom(t, L.grass);
		for (int i = 0; i < n; ++i)
			if (swell[i] && !L.grass[i] && !L.water[i] && fromIsland[i] >= kSpringClear)
			{
				L.water[i] = 1;
				L.lagoon[i] = swellLagoon[i];
				++swelled;
			}
	}
	context.telemetry.measure("tidal-flats.tide.swelled-tiles", swelled);

	// The tor's quarry faces: the rock within a tile of the rim point facing each colony's home,
	// on the rim (next to open ground). A face another colony's already claimed stays that colony's;
	// with many colonies on a small tor neighbouring faces can share tiles, so fairness is measured
	// on the finished map (tidal-flats.tor.quarry-walk) rather than promised by ownership.
	L.quarry.assign(n, 0);
	L.quarryOf.assign(n, -1);
	if (L.torRadius > 0)
	{
		std::vector<unsigned char> open(n, 0);
		for (int i = 0; i < n; ++i)
			open[i] = !L.tor[i];
		const int faceHalf = 2 * kPi * L.torRadius / teams >= kTorFaceRim ? 1 : 0;
		context.telemetry.measure("tidal-flats.tor.face-half", faceHalf);
		for (int k = 0; k < teams; ++k)
		{
			const double a = L.phase + wedge * (k + 0.5);
			const double r = torShape.radiusAt(a) - 0.5;
			const int x = t.x(L.cx + int(std::lround(r * std::cos(a) * L.stretch.sx)));
			const int y = t.y(L.cy + int(std::lround(r * std::sin(a) * L.stretch.sy)));
			for (int q : quarryNear(t, L.tor, open, t.at(x, y), faceHalf, 1))
				if (!L.quarry[q])
				{
					L.quarry[q] = 1;
					L.quarryOf[q] = k;
				}
		}
	}

	// The mechanic sketch: mud, then the sand channels, the islands and the water over it; shell
	// banks round the water on the flats; beaches; the tor's rock; deep cores in the lagoons.
	TerrainSketch &T = L.terrain;
	T.assign(n, MUDFLAT);
	int channelVertices = 0;
	for (int i = 0; i < n; ++i)
	{
		if (L.channel[i] && !L.grass[i])
		{
			T[i] = SAND;
			++channelVertices;
		}
		if (L.grass[i])
			T[i] = GRASS;
		if (L.water[i])
			T[i] = WATER;
	}
	std::vector<unsigned char> flatsWater(n, 0);
	for (int i = 0; i < n; ++i)
		flatsWater[i] = L.water[i] && L.islandOf[i] == -1;
	{
		// Round banks (Euclidean distance), not the squares a step count draws.
		const std::vector<std::int64_t> fromWater = distanceSquaredTo(t, flatsWater);
		L.bank.assign(n, 0);
		for (int i = 0; i < n; ++i)
		{
			const double fray = std::clamp(0.5 + kBankContrast * (bankFray[i] - 0.5), 0.0, 1.0);
			const double reach = std::max(kBankLeast, tide.bank + 0.5 + kBankFray * (2 * fray - 1));
			if (T[i] == MUDFLAT && fromWater[i] > 0 && double(fromWater[i]) <= reach * reach)
			{
				T[i] = SAND;
				L.bank[i] = 1;
			}
		}
	}
	layBeaches(T, t);
	std::vector<unsigned char> rockTiles(n, 0);
	for (int i = 0; i < n; ++i)
		rockTiles[i] = L.tor[i] && !L.quarry[i];
	const std::vector<unsigned char> rock = rockVertices(t, rockTiles);
	int rockLaid = 0;
	for (int i = 0; i < n; ++i)
		if (rock[i] && T[i] == GRASS)
		{
			T[i] = RIDGE_ROCK;
			++rockLaid;
		}
	std::vector<unsigned char> lagoonWater(n, 0);
	for (int i = 0; i < n; ++i)
		lagoonWater[i] = L.lagoon[i] && T[i] == WATER;
	const std::vector<unsigned char> core =
		dropSmallRegions(t, deepCore(t, lagoonWater, kDeepSteps), kDeepLeast);
	int deepLaid = 0;
	for (int i = 0; i < n; ++i)
		if (core[i])
		{
			T[i] = DEEP_WATER;
			++deepLaid;
		}
	int quarryTiles = 0;
	for (int i = 0; i < n; ++i)
		quarryTiles += L.quarry[i];
	context.telemetry.measure("tidal-flats.channels.vertices", channelVertices);
	context.telemetry.measure("tidal-flats.banks.vertices",
							  std::count(L.bank.begin(), L.bank.end(), 1));
	context.telemetry.measure("tidal-flats.deep.vertices", deepLaid);
	context.telemetry.measure("tidal-flats.tor.radius", L.torRadius);
	context.telemetry.measure("tidal-flats.tor.rock-vertices", rockLaid);
	if (L.torRadius > 0 && rockLaid < kTorRockLeast)
		L.failure = "The central tor has no room for its rock beside the quarry faces; use a bigger map or "
					"fewer colonies.";
	context.telemetry.measure("tidal-flats.tor.quarry-tiles", quarryTiles);
	return L;
}

// The looks over the mechanic sketch (composeLooks keeps each inside its rule group): wet mud by the
// water and dried hardpan far from it, by the tide; shell banks pale and the channels pale in
// patches; sandbars and oases lush; deep lagoon beds dark at neap; the tor outcrop. Home islands and
// the central island stay plain grass.
TerrainSketch lookSketch(const Layout &L, GenerationContext &context)
{
	const Torus &t = L.t;
	const int n = t.size();
	const TideSpec &tide = kTides[L.tide];
	std::vector<unsigned char> wet(size_t(n), 0);
	for (int i = 0; i < n; ++i)
		wet[i] = L.terrain[i] == WATER || L.terrain[i] == DEEP_WATER;
	const std::vector<int> nearWater = stepsFrom(t, wet);
	const std::vector<std::int64_t> wetDistance = distanceSquaredTo(t, wet);
	// Seeps: noise kSeepCell tiles across, a few swells round the rim of even a small pool, its
	// contrast stretched so the wet mud runs out in tongues on some sides and stops at the bank on
	// others (a 12-tile cell, about a pool's size, drew an even ring round each: a bull's-eye).
	const std::vector<int> fray = fractalNoise(t.w, t.h, kSeepCell, 2, context.stream("flats-looks"));
	const std::vector<int> split = fractalNoise(t.w, t.h, 32, 3, context.stream("flats-looks"));
	const std::vector<int> shells = fractalNoise(t.w, t.h, 8, 2, context.stream("flats-looks"));
	TerrainSketch looks(size_t(n), 0);
	int mud = 0, hardpan = 0;
	for (int i = 0; i < n; ++i)
	{
		switch (L.terrain[i])
		{
		case MUDFLAT:
			// Wet mud beyond the shell banks in blotches, so it reads as seeps, not a ring.
			if (const double seep = std::clamp(0.5 + kSeepContrast * (fray[i] / 65536.0 - 0.5), 0.0, 1.0),
				reach = tide.bank + tide.wet * (3.0 * seep - 1.0);
				reach > 0 && double(wetDistance[i]) <= reach * reach)
			{
				looks[i] = MUD;
				++mud;
			}
			else if (nearWater[i] > 8 && split[i] < tide.hardpan * 65536)
			{
				looks[i] = HARDPAN;
				++hardpan;
			}
			break;
		case SAND:
			if (L.bank[i] || (L.channel[i] && shells[i] > 30000))
				looks[i] = PALE_SAND;
			break;
		case GRASS:
			if (L.islandOf[i] == -3)
				looks[i] = LUSH_GRASS;
			break;
		case DEEP_WATER:
			if (L.tide == 0)
				looks[i] = DARK_WATER;
			break;
		case RIDGE_ROCK:
			looks[i] = OUTCROP;
			break;
		default:
			break;
		}
	}
	context.telemetry.measure("tidal-flats.looks.wet-mud", mud);
	context.telemetry.measure("tidal-flats.looks.hardpan", hardpan);
	return looks;
}

// Every home's kit, identical and unscaled: wheat and wood beside the pond on the side away from
// the swarm, a quarry on the island's rim towards the centre of the map; then its own scaled
// ambient farmland and outcrops.
void furnishHomes(GenerationMap &map, const Layout &L, GenerationContext &context, const TidalFlatsOptions &o)
{
	const Torus &t = L.t;
	const int n = t.w * t.h;
	const std::vector<unsigned char> reserved = swarmSurroundings(t, context);
	const Fertility::Field fertility = Fertility::forMap(map, false);
	HeightMap split(t.w, t.h, context.stream("flats-home-split"));
	split.makePlain(6);
	// The kit's distance from the pond's centre: its fullest radius (1.3 for the wobble) plus a
	// 3-tile beach, so wheat and wood start on the first grass past the sand, as near the water as
	// they can grow.
	const double pondRadius = std::clamp(0.2 * L.g.homeRadius, 3.0, 6.0) * 1.3 + 3;
	for (int team = 0; team < L.g.teams; ++team)
	{
		const auto eligible = [&](int i)
		{ return L.islandOf[i] == team && !reserved[i] && clearGround(map, t.remainderX(i), i / t.w); };
		const double a = std::atan2(double(t.offsetY(L.cy, L.pondY[team])),
									double(t.offsetX(L.cx, L.pondX[team])));
		const KitFrame frame{L.pondX[team], L.pondY[team], a};
		// Wheat and wood on the two sides of the pond, the quarry towards the map's centre.
		plantKit(map, t, context,
				 {frame.at(0, -pondRadius, 12), frame.at(0, pondRadius, 12),
				  frame.at(-(pondRadius + 4), 0, 10), kHomeWheat, kHomeWood, 2},
				 eligible);
		// Ambient farmland on the island's fertile ground, split two to one.
		std::vector<int> ground;
		std::vector<std::pair<double, int>> farm;
		for (int i = 0; i < n; ++i)
			if (eligible(i))
			{
				ground.push_back(i);
				if (const std::uint32_t f = fertility.at(t.remainderX(i), i / t.w); f > 0)
					farm.push_back({-double(f), i});
			}
		std::stable_sort(farm.begin(), farm.end());
		std::vector<int> byFertility;
		for (const auto &entry : farm)
			byFertility.push_back(entry.second);
		const int area = int(ground.size());
		// 4% of the island under ambient wheat and 2% under wood, on its most fertile ground
		// (nearest the pond), on top of the kit: an island is a home, so most of it stays free to
		// build on.
		plantFields(map, t, byFertility, int(scaledCount(area * 4 / 100, o.wheat)),
					int(scaledCount(area * 2 / 100, o.wood)),
					[&](int i) { return split.uiLevel(t.remainderX(i), i / t.w, 2048); });
		scatterClumps(context, t, ground, int(scaledCount(1, o.stone)), "flats-home-stone",
					  eligible,
					  [&](MapGeneratorPoint p) { placeResourceClump(map, context, p, STONE, 1); });
	}
}

// Every neutral island is an oasis, its pond ringed by wheat to its shores, with one prize inside,
// a fruit grove or a stone deposit in turn; the central island carries the orchard of all three
// fruits round its tor, whose quarry faces are stocked before the colonies settle.
void stockIslands(GenerationMap &map, const Layout &L, GenerationContext &context, const TidalFlatsOptions &o,
				  const std::vector<unsigned char> &approach)
{
	const Torus &t = L.t;
	const int n = t.w * t.h;
	// Every neutral island is its own component of grass; each gets its prize at its middle.
	std::vector<unsigned char> neutral(n, 0);
	for (int i = 0; i < n; ++i)
		neutral[i] = L.islandOf[i] == -3 && L.prizeOf[i] >= 0;
	const std::vector<int> label = connectedRegions(neutral, t.w, t.h, true, GridNeighbors::Eight);
	std::vector<std::vector<int>> islands;
	for (int i = 0; i < n; ++i)
		if (label[i] >= 0)
		{
			if (label[i] >= int(islands.size()))
				islands.resize(label[i] + 1);
			islands[label[i]].push_back(i);
		}
	for (const std::vector<int> &tiles : islands)
	{
		const int start = tiles.front();
		double sx = 0, sy = 0;
		for (int i : tiles)
		{
			sx += t.offsetX(start, t.remainderX(i));
			sy += t.offsetY(start / t.w, i / t.w);
		}
		const int mx = t.x(t.remainderX(start) + int(std::lround(sx / tiles.size())));
		const int my = t.y(start / t.w + int(std::lround(sy / tiles.size())));
		const auto eligible = [&](int i)
		{ return L.islandOf[i] == -3 && clearGround(map, t.remainderX(i), i / t.w); };
		// The prize, on the far side of the pond from the map's centre.
		const double a = std::atan2(double(t.offsetY(L.cy, my)), double(t.offsetX(L.cx, mx)));
		if (const int seed = seedNear(t, mx + int(std::lround(6 * std::cos(a))),
									  my + int(std::lround(6 * std::sin(a))), 8, eligible);
			seed >= 0)
		{
			const MapGeneratorPoint centre(t.remainderX(seed), seed / t.w);
			if (L.prizeOf[start] == 0)
			{
				if (scaledCount(1, o.fruit) > 0)
					placeResourceClump(map, context, centre,
									   CHERRY + int(context.bounded("flats-prizes", 3)), 2);
			}
			else if (scaledCount(1, o.stone) > 0)
			{
				placeResourceClump(map, context, centre, STONE, 2);
			}
		}
		// The oasis: every free tile of the island under wheat, unscaled, so it has to be cleared.
		for (int i : tiles)
			if (eligible(i) && map.isResourceAllowed(t.remainderX(i), i / t.w, WHEAT))
				map.setResourceByIndex(t.remainderX(i), i / t.w, WHEAT, 1);
	}
	if (L.g.centralRadius > 0)
	{
		const auto eligible = [&](int i) {
			return L.islandOf[i] == -2 && !L.tor[i] && !approach[i] &&
				   clearGround(map, t.remainderX(i), i / t.w);
		};
		// The orchard's three groves sit a third of a turn apart, 4 tiles out from the tor's
		// fullest edge (revision 8: from the central pond's), clear of every quarry's approach.
		const double rho = L.torRadius * (1 + kTorRoughness) + 4;
		const double spin = context.bounded("flats-prizes", 3600) / 3600.0 * 2 * kPi;
		if (scaledCount(1, o.fruit) > 0)
			for (int f = 0; f < 3; ++f)
			{
				const double a = spin + 2 * kPi * f / 3;
				const int seed = seedNear(
					t, L.cx + int(std::lround(rho * std::cos(a) * L.stretch.sx)),
					L.cy + int(std::lround(rho * std::sin(a) * L.stretch.sy)), 8, eligible);
				if (seed >= 0)
					placeResourceClump(map, context, {t.remainderX(seed), seed / t.w}, CHERRY + f, 2);
			}
		// A grove's clump can spill onto a quarry's approach: the faces must stay open.
		for (int i = 0; i < n; ++i)
			if (approach[i] && !L.quarry[i] && map.isResource(t.remainderX(i), i / t.w))
				map.replaceResource(t.remainderX(i), i / t.w, Resource{});
	}
}

bool generate(GenerationWorld &game, GenerationContext &context)
{
	context.stage = "flats layout";
	const TidalFlatsOptions o(context.request);
	GenerationMap &map = game.map;
	const int teams = context.request.nbTeams;
	for (int i = 0; i < teams; ++i)
		game.addTeam();
	const Layout L = design(context.request, context);
	if (!L.failure.empty())
	{
		context.detail = L.failure;
		return false;
	}
	const Torus &t = L.t;
	const int n = t.w * t.h;

	context.stage = "flats terrain";
	writeVertices(map, composeLooks(L.terrain, lookSketch(L, context)));

	// The tor's quarry faces go down before the colonies, so nothing is planted over them.
	context.stage = "flats tor";
	std::vector<unsigned char> protectedStone(size_t(n), 0);
	int torStone = 0;
	for (int i = 0; i < n; ++i)
		if (L.quarry[i] && map.isResourceAllowed(t.remainderX(i), i / t.w, STONE))
		{
			map.setResourceByIndex(t.remainderX(i), i / t.w, STONE, 1);
			protectedStone[i] = 1;
			++torStone;
		}
	context.telemetry.measure("tidal-flats.tor.stone", torStone);
	const std::vector<unsigned char> approach = quarryApproach(t, L.quarry);

	context.stage = "flats colonies";
	const auto island = [&](int team)
	{
		std::vector<unsigned char> home(n, 0);
		for (int i = 0; i < n; ++i)
			home[i] = L.islandOf[i] == team && map.terrainPropertiesAt(t.remainderX(i), i / t.w).buildable;
		return home;
	};
	// The swarm stands on the far side of the pond from the map's centre, a short walk from the
	// fields; placeSettlement measures from the footprint's top-left tile.
	const auto anchor = [&](int team)
	{
		const double a = std::atan2(double(t.offsetY(L.cy, L.homeY[team])),
									double(t.offsetX(L.cx, L.homeX[team])));
		// The swarm stands 6 tiles past the pond's fullest edge, on the side away from the centre:
		// its fields between it and the pond, and the rest of the island between it and the flats.
		const double back = std::clamp(0.2 * L.g.homeRadius, 3.0, 6.0) * 1.3 + 6;
		return MapGeneratorPoint(L.pondX[team] + int(std::lround(back * std::cos(a))) - 2,
								 L.pondY[team] + int(std::lround(back * std::sin(a))) - 2);
	};
	if (!settleColonies(game, context, "flats-starts", island, anchor))
		return false;

	context.stage = "flats resources";
	furnishHomes(map, L, context, o);
	stockIslands(map, L, context, o, approach);
	seedAlgae(map, context, t, "flats-algae", o.algae, AlgaeBand::anyWater());
	secureStartingCrops(game, context, t, 24, 32, 0, &protectedStone);

	if (context.telemetry.enabled() && L.torRadius > 0)
	{
		// How far each colony walks to the nearest of the tor's quarry faces, and to the one
		// facing it: the fairness of the central stone, measured on the finished map.
		const std::vector<unsigned char> walkable = walkableTiles(map);
		int least = INT_MAX, most = 0;
		bool unreachable = false;
		for (int team = 0; team < teams; ++team)
		{
			std::vector<unsigned char> swarm(size_t(n), 0);
			for (int dy = 0; dy < 4; ++dy)
				for (int dx = 0; dx < 4; ++dx)
					swarm[t.at(context.bootX[team] + dx, context.bootY[team] + dy)] = 1;
			const std::vector<int> steps = stepsFrom(t, swarm, walkable);
			int own = INT_MAX, any = INT_MAX;
			for (int i = 0; i < n; ++i)
			{
				if (!L.quarry[i])
					continue;
				const int x = t.remainderX(i), y = i / t.w;
				for (int dy = -1; dy <= 1; ++dy)
					for (int dx = -1; dx <= 1; ++dx)
					{
						const int s = steps[t.at(x + dx, y + dy)];
						if (s < 0)
							continue;
						any = std::min(any, s + 1);
						if (L.quarryOf[i] == team)
							own = std::min(own, s + 1);
					}
			}
			context.telemetry.measure("tidal-flats.tor.quarry-walk", any == INT_MAX ? -1 : any, team);
			context.telemetry.measure("tidal-flats.tor.own-face-walk", own == INT_MAX ? -1 : own,
									  team);
			unreachable = unreachable || any == INT_MAX;
			least = std::min(least, any);
			most = std::max(most, any == INT_MAX ? 0 : any);
		}
		// -1 when some colony reaches no face at all: no spread is fair then.
		context.telemetry.measure("tidal-flats.tor.quarry-walk-spread", unreachable ? -1 : most - least);
	}
	return true;
}

std::string validateRequest(const GenerationRequest &r)
{
	if (r.nbTeams < 1)
		return "Tidal flats need at least one colony.";
	const Geometry g = geometryFor(r);
	// Below radius 9 an island cannot hold a pond, its kit and room for a base.
	if (g.homeRadius < 9)
		return "The home islands do not fit on this map; use a bigger map, fewer colonies or "
			   "smaller islands.";
	return "";
}

bool waterGroup(TerrainType v)
{
	const TerrainGroup group = terrainTypeDefinition(v).group;
	return group == TerrainGroup::Water || group == TerrainGroup::DeepWater;
}

// Checked on the finished world: every home island still carries its pond; nothing on the flats
// can be built on; every deep vertex lies inside a shallow margin; the tor's quarry faces hold
// their stone; colony 0 can walk to every other colony, onto the central island and to the shore
// of every island, with water, buildings and every resource blocking.
std::string validateWorld(const GenerationWorld &game, const GenerationContext &context)
{
	GenerationContext replay(context.request);
	const Layout L = design(context.request, replay);
	const GenerationMap &map = game.map;
	if (const std::string mismatch = designMismatch(L, map, "flats"); !mismatch.empty())
		return mismatch;
	const Torus &t = L.t;
	const int n = t.w * t.h, teams = context.request.nbTeams;
	for (int k = 0; k < teams; ++k)
	{
		bool pond = false;
		for (int dy = -2; dy <= 2 && !pond; ++dy)
			for (int dx = -2; dx <= 2 && !pond; ++dx)
				pond = map.isWater(t.x(L.pondX[k] + dx), t.y(L.pondY[k] + dy));
		if (!pond)
			return "Colony " + std::to_string(k) + "'s island has lost its pond.";
	}
	for (int i = 0; i < n; ++i)
	{
		const int x = t.remainderX(i), y = i / t.w;
		if (L.islandOf[i] == -1 && map.terrainPropertiesAt(x, y).buildable)
			return "The flats can be built on at (" + std::to_string(x) + ", " + std::to_string(y) +
				   ").";
		if (terrainTypeDefinition(map.vertexTerrainAt(x, y)).group == TerrainGroup::DeepWater)
			for (int dy = -1; dy <= 1; ++dy)
				for (int dx = -1; dx <= 1; ++dx)
					if (!waterGroup(map.vertexTerrainAt(t.x(x + dx), t.y(y + dy))))
						return "Deep water touches the shore at (" + std::to_string(x) + ", " +
							   std::to_string(y) + ").";
	}
	if (L.torRadius > 0)
		if (const std::string fault =
				quarryFault(map, t, L.quarry, std::vector<int>(size_t(n), -1), 0, 0);
			!fault.empty())
			return fault;
	const ColonyWalk walk = walkFromFirstColony(map, teams, "the flats", "over the flats");
	if (!walk.error.empty())
		return walk.error;
	// Every island, the central one included, has a reachable tile on it or on its shore.
	std::vector<unsigned char> reached(n, 0), isle(n, 0);
	for (int i = 0; i < n; ++i)
	{
		reached[i] = walk.steps[i] >= 0;
		isle[i] = L.grass[i] && !L.water[i] && !L.tor[i];
	}
	const std::vector<unsigned char> nearby = dilate(t, reached, 1);
	const std::vector<int> label = connectedRegions(isle, t.w, t.h, true, GridNeighbors::Eight);
	std::vector<char> arrived;
	for (int i = 0; i < n; ++i)
		if (label[i] >= 0)
		{
			if (label[i] >= int(arrived.size()))
				arrived.resize(label[i] + 1, 0);
			arrived[label[i]] |= nearby[i];
		}
	for (int i = 0; i < n; ++i)
		if (label[i] >= 0 && !arrived[label[i]])
			return std::string(L.islandOf[i] == -2 ? "The central island" : "An island") + " at (" +
				   std::to_string(t.remainderX(i)) + ", " + std::to_string(i / t.w) +
				   ") cannot be reached over the flats.";
	return "";
}
} // namespace

TidalFlatsOptions::TidalFlatsOptions(const GenerationRequest &r)
	: homeIslandSize(r.option("home-island-size")), extraIslands(r.option("extra-islands")),
	  sandbars(r.option("sandbars")), tidePools(r.option("tide-pools")),
	  lagoons(r.option("lagoons")), coastRoughness(r.option("coast-roughness")),
	  centralIsland(r.option("central-island") != 0), tide(r.option("tide")),
	  wheat(r.option("wheat-amount")), wood(r.option("wood-amount")),
	  stone(r.option("stone-amount")), algae(r.option("algae-amount")),
	  fruit(r.option("fruit-amount"))
{
	if (tide < 1 || tide > 3)
		tide = GenerationContext::choiceFromSeed(r.seed, "tidal-flats/tide", {1, 2, 3});
}

GeneratorDefinition tidalFlatsDefinition()
{
	return {
		"tidal-flats",
		18,
		"Tidal flats",
		9,
		false,
		// The home islands' radius as a share of the half side; extra islands (the oases) and
		// sandbars (the green patches) per colony, six and eight since 2026-09-14; lagoons per
		// colony; tide pools per 128x128 of flats.
		{GeneratorControl{"home-island-size", "Home island size", 14, 26, 1, 20,
						  ControlGroup::Terrain}
			 .withSearchRange(20, 23),
		 GeneratorControl{"extra-islands", "Extra islands", 0, 10, 1, 6, ControlGroup::Terrain}
			 .withSearchRange(4, 8),
		 GeneratorControl{"sandbars", "Sandbars", 0, 16, 1, 8, ControlGroup::Terrain}
			 .withSearchRange(6, 12),
		 GeneratorControl{"tide-pools", "Tide pools", 0, 12, 1, 8, ControlGroup::Terrain}
			 .withSearchRange(5, 10),
		 // Lagoons per colony: long tidal channels of water with a deep bed, 3.5 to 5.5 tiles in
		 // radius before their stretch since revision 9 (revision 8's never fitted a wedge).
		 GeneratorControl{"lagoons", "Lagoons", 0, 4, 1, 3, ControlGroup::Terrain}.withSearchRange(
			 2, 4),
		 GeneratorControl{"coast-roughness", "Coast roughness", 0, 100, 5, 50,
						  ControlGroup::Terrain}
			 .withSearchRange(35, 65),
		 // Off, the middle of the map is flats like the rest, and there is no orchard or tor.
		 GeneratorControl::toggle("central-island", "Central island", true, ControlGroup::Layout)
			 .withSearchValues({0, 1}),
		 // The water on the flats: a neap tide shrinks every pool and lagoon to 0.8 of its radius
		 // and bares wider shell banks and more dried hardpan; a spring tide swells them to 1.25
		 // (kept four tiles off every island) with wet mud further from the water. The islands
		 // and channels are the same at every tide.
		 GeneratorControl::choice("tide", "Tide", {"Random", "Neap tide", "Mean tide", "Spring tide"},
								  0, ControlGroup::Terrain)
			 .withSearchValues({1, 2, 3}),
		 // Every island's ambient fields and outcrops and every prize; each home's kit and the
		 // tor's quarry faces are unscaled.
		 GeneratorControl::percentage("wheat-amount", "Wheat amount"),
		 GeneratorControl::percentage("wood-amount", "Wood amount"),
		 GeneratorControl::percentage("stone-amount", "Stone amount"),
		 GeneratorControl::percentage("algae-amount", "Algae amount"),
		 GeneratorControl::percentage("fruit-amount", "Fruit amount")},
		generate,
		true,
		validateRequest,
		validateWorld,
		// The background is exposed mud and sand, not open ocean water - "tidal" reads more
		// accurately than "ocean" for a render, and the two share no water color in common.
		{"terrain:natural", "feature:islands", "feature:tidal", "style:contested-center",
		 "fairness:repeated-wedge"}};
}
