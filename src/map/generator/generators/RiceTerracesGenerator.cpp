// SPDX-License-Identifier: GPL-3.0-or-later
#include "RiceTerracesGenerator.h"
#include "Climates.h"
#include "Drawing.h"
#include "Farmland.h"
#include "FertilityField.h"
#include "GenerationWorld.h"
#include "GenerationContext.h"
#include "Geometry.h"
#include "Grid.h"
#include "HeightFieldLooks.h"
#include "Homes.h"
#include "LatticeNoise.h"
#include "Morphology.h"
#include "Orbits.h"
#include "Patterns.h"
#include "Pipeline.h"
#include "Planting.h"
#include "Resources.h"
#include "Rock.h"
#include "Room.h"
#include "Settlements.h"
#include "Sketch.h"
#include "Walls.h"
#include <algorithm>
#include <array>
#include <climits>
#include <cmath>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>
using namespace MapGeneration;

// Rice terraces: long terraced hillsides that run diagonally across the whole torus, the way a
// real terraced valley side does. Every hillside is one continuous stripe (Patterns.h's stripe
// field, at Rain shadow's slant, so with a slant every crest is one long spiral round the torus):
// a forested crest where the towns stand in clearings, then narrow contour paddies of rice and
// irrigation water stepping down each slope, each held up by an earthen bund, then a valley floor
// of shared ground with a river winding along its middle between fertile banks. The contours sway
// together along the hillside, in a few long waves and a fine grain, so the terraces meander
// rather than run ruled; stone stairs wind down through them every `stair-spacing` tiles and carry
// on across the valley as roads that ford the river on boardwalks.
//
// WHY THIS MAP EXISTS (maintainer review 2026-09-16). The first Rice terraces drew concentric
// rings round point summits; lobing the rings answered "a centre-pivot farm" but never made them
// read as rice terraces, because terraces are not rings round a point at all: they are many narrow
// strips following the contour of a long slope, stacked, winding and joined into one landscape.
// That map is Hills now (HillsGenerator.cpp); this one starts from the landscape instead of from
// the contour-farm primitive.
//
// 2.0 (revision 6, 2026-11-11) redrew it on the terrain catalogue: the sand caps, rings, stairs,
// roads and beaches read as graph paper, crests and valleys were the same plain grass, and nothing
// showed a terrace stepping down to the next. Four signatures now: paddies stacked behind dark
// earthen bunds; a forested ridge crowning each hillside with towns in paved clearings; stone
// stairs snaking down the slopes; a broad river at the valley floor with loam banks.
//
// WHY IT PLAYS WELL (docs/map-generators/game-rules-for-map-design.md). Water blocks walking and
// crops block walking and building, so the terraces are a wall between the crest and the valley
// that only the stairs cross. Each bund is a walkable footpath along its contour (Rough ground:
// nothing grows on it, nothing is built on it), sealed from the levels above and below by water,
// so workers reach a paddy's middle without opening a way down the slope. Every crop strip lies
// within a few tiles of water, so the terraces regrow and a colony's food is the slope below its
// town. Barren caps above and below the terraces, and a barren ring round every town, keep the
// crops off the towns and the commons however long the game runs. The ridge forest stands beyond
// the reach of any water, so it never spreads onto a town. The valley is where colonies on facing
// slopes meet, over its banks' extra farmland and its outcrops' stone.
namespace
{
// The crest's half width beyond a town's radius: a lane of grass round every town along the crest.
constexpr double kCrestMargin = 3;
// Barren caps, in tiles, above and below every slope's terraces and round every town that cuts
// into them: two make a full tile no crop crosses on eight neighbours.
constexpr double kCap = 2;
// The narrowest valley floor: a river and its banks with walking ground either side.
constexpr double kValleyMinimum = 16;
// One terrace at 100% band width: ten tiles of rice, six of water and a two-tile earthen bund on
// the downhill side of the water. Revision 5 had twelve and six with no bund; the bund comes out of
// the rice so a 256 map keeps its three terraces a slope, and its footpath lets workers reach the
// paddy from its downhill side as well as its ends. Every rice tile still lies within the engine's
// 15-tile growth probe of several channels (rice.crop-width.* and rice.water-width.* telemetry).
constexpr double kCropCorners = 10, kWaterCorners = 6, kBundCorners = 2;
// Stairs are five tiles across, as Hills' are; valley roads three.
constexpr double kStairHalf = 2.5, kRoadHalf = 1.5;
// Each stair snakes down its slope: two sideways jogs added together, a long one of kJogLeast to
// kJogLeast + kJogSpan tiles in a wave kJogWave to kJogWave + kJogWaveSpan tiles long and a short
// one of kJogKink of those in a wave kJogKinkWave of that length, its own draws per stair, so no two
// stairs wind alike. The stair keeps its width across its own line however steeply it turns.
constexpr double kJogLeast = 2, kJogSpan = 4, kJogWave = 44, kJogWaveSpan = 36;
constexpr double kJogKink = 0.35, kJogKinkWave = 0.42;
// The river: kRiverHalf to kRiverHalf + kRiverHalfSpan tiles either side of its line, which winds
// across the valley in long waves of up to kRiverMeander tiles (less where the valley is narrow),
// with kBank to kBank + kBankSpan tiles of loam on either bank, and sand bars kBar tiles wide on the
// inside of the bends the river swings at least kBarSwing of its meander into.
constexpr double kRiverHalf = 3, kRiverHalfSpan = 2, kRiverMeander = 7;
constexpr std::array<double, 2> kRiverWaveLengths{150, 64}, kRiverWaveShares{0.7, 0.3};
constexpr double kBank = 3, kBankSpan = 4, kBankFoot = 4, kBar = 2.5, kBarSwing = 0.6;
// A ford reaches this far past the river on the far bank, so the road lands on dry ground.
constexpr double kFordLanding = 2.5;
// The contours' sway: three long waves along the hillside of about these lengths (whole numbers
// of waves per turn along, so the stripes still meet themselves across the seams), sharing the
// waviness control in these proportions, plus a fine fractal grain of kGrainPercent of a spacing.
// Every contour of a hillside moves by the same amount at a point along it, so the terraces keep
// their widths along the slope's normal and never fold; only steeper sway narrows them where the
// hillside turns. Lengthened 2026-09-17 (from 110, 60, 36) so a given waviness amplitude turns more
// gently: bigger, broader sweeps down the hillside instead of sharp doglegs, and less narrowing at
// every turn for the same visual sway.
constexpr std::array<double, 3> kWaveLengths{220, 120, 72}, kWaveShares{0.5, 0.3, 0.2};
constexpr int kGrainPeriod = 24, kGrainPercent = 3;
// The towns' starter supplies: two wheat patches on the first terrace below the town, one on either
// slope's side, and one of wood; a quarry face in a small crag at the town's edge; and a completed
// inn for the first meal (Hills' first AI games showed the first feeding deadline arriving before
// an inn is built and filled across a cap).
constexpr int kStarterWheat = 14, kStarterWood = 12;
constexpr int kMinimumRoom = 48;
// The paved square round every town's middle.
constexpr double kPave = 4.5;
// Every town's quarry crag: a knot kCragRadius tiles round, kCragOut tiles from the town's middle
// along the crest, with a face of kCragFace stone tiles looking back at the town.
constexpr double kCragRadius = 2.6, kCragOut = 12.5;
constexpr int kCragFace = 2;
// Two valley outcrops per colony, at the valley's most open ground and at least kOutcropApart tiles
// from each other: knots kOutcropRadius tiles round (lobed by kOutcropLobe), only where open grass
// reaches kOutcropRoom tiles round, each with a quarry face kOutcropFace tiles across.
constexpr int kOutcropsPerColony = 2;
constexpr double kOutcropRadius = 3.4, kOutcropLobe = 1.0, kOutcropRoom = 6, kOutcropApart = 24;
constexpr int kOutcropFace = 3;
// The ridge forest: crest grass farther than kForestDry tiles (on both axes, the engine's growth
// probe) from any water, so it never grows or spreads, and kForestClear tiles beyond a town's edge.
// kForestShare of that ground is wooded at the default wood amount, in clumps kForestPatch across.
constexpr int kForestDry = 16, kForestPatch = 7, kForestShare = 45;
constexpr double kForestClear = 4;
// The banks' crops, as Hills' meadows (FEEDBACK 2026-11-11, "place resources along all of those
// fertile terrain sections"): this share of the loam holds wheat and this share wood, in patches.
constexpr int kBankWheat = 35, kBankWood = 15, kBankPatch = 8, kBankStretch = 20;

// The terraced valley's one look: plain grass, lush paddies, slate stairs and town squares,
// boardwalk fords, clay caps, mud bunds and spring-meadow banks (green, so the banks read as the
// river's meadows and not as more bare earth beside the bunds).
constexpr ClimateLook kRiceLook = {"rice-terraces", CLIFFS,       RIDGE_ROCK,   GRASS,     GRASS,
								  PALE_SAND,       SLATE_ASHLAR, SLATE_ASHLAR, SLATE_ASHLAR,
								  SLATE_ASHLAR,    BOARDWALK,    CLAY,         DEEP_WATER, MUD,
								  SPRING_MEADOW,   RIDGE_ROCK,   LUSH_GRASS};
static_assert(terrainGroup(kRiceLook.rock) == TerrainGroup::Ridges &&
				  terrainGroup(kRiceLook.homeGrass) == TerrainGroup::Grass &&
				  terrainGroup(kRiceLook.openGrass) == TerrainGroup::Grass &&
				  terrainGroup(kRiceLook.farmGrass) == TerrainGroup::Grass &&
				  terrainGroup(kRiceLook.town) == TerrainGroup::Paths &&
				  terrainGroup(kRiceLook.bridge) == TerrainGroup::Paths &&
				  terrainGroup(kRiceLook.scrub) == TerrainGroup::Barren &&
				  terrainGroup(kRiceLook.rough) == TerrainGroup::Rough &&
				  terrainGroup(kRiceLook.fertile) == TerrainGroup::Fertile,
			  "The terraced valley's look must keep every terrain's rules");

struct Layout
{
	Torus t{1, 1};
	StripeStyle across, along;
	int hillsides = 0, terraces = 0, stairCount = 0;
	double spacing = 0, crestHalf = 0, homeRadius = 0, period = 0, valleyStart = 0;
	FarmRows rows{kCropCorners, kWaterCorners};
	std::vector<ShapePoint> homes;
	std::vector<int> homeOf, row; // row: 2k for the k-th rice strip down a slope, 2k+1 its channel
	std::vector<unsigned char> crest, stairs, roads, fords, valley, bund, bank, forest;
	std::vector<unsigned char> upper; // on the slope of rising phase from its crest
	// Every town's crag face and the valley outcrops' faces (tiles).
	std::vector<std::vector<int>> cragFaces, outcropFaces;
	// The mechanics (one canonical terrain per rule) and the finished look painted over them.
	TerrainSketch terrain, finished;
	std::string failure;
};

// A rock knot round (cx, cy), lobed, with a quarry face of `faceTiles` tiles looking towards
// `heading`, drawn into the sketch as RIDGE_ROCK. The face tiles and the ring before them stay
// grass (`keepGrass`). Returns the face, empty when none fits.
std::vector<int> drawKnot(Layout &L, int cx, int cy, double radius, double lobe, double lobeAngle,
						  double heading, int faceTiles, std::vector<unsigned char> &rockTiles,
						  std::vector<unsigned char> &keepGrass)
{
	const Torus &t = L.t;
	const int n = t.size();
	std::vector<unsigned char> knot(n, 0);
	const int reach = int(std::ceil(radius + lobe));
	for (int dy = -reach; dy <= reach; ++dy)
		for (int dx = -reach; dx <= reach; ++dx)
		{
			const double theta = std::atan2(double(dy), double(dx));
			const double r = radius + lobe * std::cos(2 * (theta - lobeAngle));
			if (dx * dx + dy * dy <= r * r)
				knot[t.at(cx + dx, cy + dy)] = 1;
		}
	std::vector<unsigned char> open(n, 0);
	for (int i = 0; i < n; ++i)
		open[i] = !knot[i];
	const int facing = t.at(int(std::lround(cx + radius * std::cos(heading))),
							int(std::lround(cy + radius * std::sin(heading))));
	const std::vector<int> face = quarryNear(t, knot, open, facing, faceTiles / 2 + 1, 1);
	for (int i : face)
		knot[i] = 0;
	const std::vector<unsigned char> rock = rockVertices(t, knot);
	for (int i = 0; i < n; ++i)
		if (rock[i])
		{
			L.terrain[i] = RIDGE_ROCK;
			rockTiles[i] = 1;
		}
	std::vector<unsigned char> faceMask = tileMask(t, face);
	const std::vector<unsigned char> approach = quarryApproach(t, faceMask);
	for (int i = 0; i < n; ++i)
		faceMask[i] = faceMask[i] || approach[i];
	const std::vector<unsigned char> corners = tileCorners(t, faceMask);
	for (int i = 0; i < n; ++i)
		keepGrass[i] = keepGrass[i] || corners[i];
	return face;
}

Layout design(const GenerationRequest &request, GenerationContext &context)
{
	const RiceTerracesOptions o(request);
	Layout L;
	L.t = {1 << request.wDec, 1 << request.hDec};
	const Torus &t = L.t;
	const int n = t.size(), teams = std::max(1, request.nbTeams);
	// Band width scales the rice freely but never narrows a channel below its default: the channels
	// are what seal a crest from its valley, and a 90% channel on a steeply turning hillside let
	// walkers through (one roll in 2,000 of the 2.0 control study).
	L.rows = {kCropCorners * o.bandWidth / 100.0,
			  kWaterCorners * std::max(o.bandWidth, 100) / 100.0};
	L.period = L.rows.period() + kBundCorners;
	L.homeRadius = o.homeSize;
	L.crestHalf = L.homeRadius + kCrestMargin;

	// FITTING. Hillsides are taken away one at a time until a slope holds the terraces asked for
	// beside a crest and the narrowest valley, as Rain shadow takes away ridges; then the terraces
	// are cut to what fits; and only a slope that holds no terrace refuses the map. The sway
	// narrows the terraces where the hillside turns, so the valley is budgeted a wave's worth wider.
	L.across.acrossX = o.slant;
	L.across.warpPeriod = kGrainPeriod;
	L.across.warpPercent = kGrainPercent;
	const auto slopeRoom = [&](int hillsides)
	{
		L.across.acrossY = hillsides;
		const double spacing = stripeSpacing(t, L.across);
		return (spacing - 2 * L.crestHalf - kValleyMinimum - o.waviness) / 2 - 2 * kCap;
	};
	// Every colony needs its own stretch of crest: the crests' total length (the map's area over the
	// spacing) must hold every town with a town's width of crest between it and the next, so a
	// crowded map keeps more hillsides than it asked for.
	const double townSpacing = 2 * (L.homeRadius + kCap + kCrestMargin) + 4;
	int fewest = 1;
	while (fewest < 12)
	{
		L.across.acrossY = fewest;
		if (t.size() / stripeSpacing(t, L.across) >= teams * townSpacing)
			break;
		++fewest;
	}
	// The control counts hillsides per 256 tiles of the map's longer side, so a 512 map keeps the
	// same slopes rather than one hillside with valleys twice as wide.
	const int asked = std::max(1, int(std::lround(o.hillsides * std::max(t.w, t.h) / 256.0)));
	for (L.hillsides = std::max(asked, fewest); L.hillsides > fewest; --L.hillsides)
		if (slopeRoom(L.hillsides) >= o.terraces * L.period)
			break;
	const double room = slopeRoom(L.hillsides);
	L.terraces = std::min(o.terraces, int(std::floor(room / L.period)));
	L.across.acrossY = L.hillsides;
	L.spacing = stripeSpacing(t, L.across);
	context.telemetry.measure("rice.hillsides.actual", L.hillsides);
	context.telemetry.measure("rice.terraces.per-slope", L.terraces);
	context.telemetry.measure("rice.hillsides.spacing", L.spacing);
	if (L.hillsides < asked)
		context.telemetry.fallback("rice.hillsides.reduced", "Fewer hillsides so the terraces fit");
	if (L.hillsides > asked)
		context.telemetry.fallback("rice.hillsides.added", "More hillsides so every town has crest");
	if (L.terraces < o.terraces)
		context.telemetry.fallback("rice.terraces.reduced", "Fewer terraces so a slope fits");
	if (L.terraces < 1)
	{
		L.failure = "A hillside has no room for a terrace; use a bigger map, fewer hillsides, "
					"smaller towns or narrower terraces.";
		return L;
	}

	// THE HILLSIDES. Phase 0 is a crest, half a turn a valley's middle. The long waves shift the
	// whole across phase by the same number of tiles at a point along the hillside.
	L.along = alongStripes(t, L.across);
	const double alongLength = stripeSpacing(t, L.along);
	std::vector<int> phase = stripePhase(t, L.across, context.stream("rice-hillsides"));
	const std::vector<int> along = stripePhase(t, L.along, context.stream("rice-hillsides"));
	std::array<int, 3> waves{};
	std::array<double, 3> wavePhase{};
	for (size_t j = 0; j < waves.size(); ++j)
	{
		waves[j] = std::max(1, int(std::lround(alongLength / kWaveLengths[j])));
		wavePhase[j] = context.bounded("rice-waves", 3600) * (2 * kPi / 3600);
	}
	for (int i = 0; i < n; ++i)
	{
		const double u = along[i] / 65536.0 * 2 * kPi;
		double shift = 0;
		for (size_t j = 0; j < waves.size(); ++j)
			shift += o.waviness * kWaveShares[j] * std::sin(waves[j] * u + wavePhase[j]);
		phase[i] = ((phase[i] + int(std::lround(shift / L.spacing * 65536))) % 65536 + 65536) % 65536;
	}
	const auto tilesFromCrest = [&](int i) { return stripeDistance(phase[i]) / 65536.0 * L.spacing; };

	const double terracesStart = L.crestHalf + kCap;
	const double terracesEnd = terracesStart + L.terraces * L.period;
	L.valleyStart = terracesEnd + kCap;
	const double valleyHalf = L.spacing / 2 - L.valleyStart;

	// THE RIVER winds across the valley's middle in long waves of its own, its width breathing
	// along it; the meander is cut so the river and its banks stay off the caps however the valley
	// narrows. Positions across the valley are signed: zero at its middle, negative towards the
	// crest of rising phase.
	std::array<int, 2> riverWaves{};
	std::array<double, 2> riverPhase{};
	for (size_t j = 0; j < riverWaves.size(); ++j)
	{
		riverWaves[j] = std::max(1, int(std::lround(alongLength / kRiverWaveLengths[j])));
		riverPhase[j] = context.bounded("rice-river", 3600) * (2 * kPi / 3600);
	}
	const int widthWaves = std::max(1, int(std::lround(alongLength / 90)));
	const double widthPhase = context.bounded("rice-river", 3600) * (2 * kPi / 3600);
	const double meander = std::clamp(
		valleyHalf - kRiverHalf - kRiverHalfSpan - kBank - kBankSpan - 2, 0.0, kRiverMeander);
	const auto riverLine = [&](int i)
	{
		const double u = along[i] / 65536.0 * 2 * kPi;
		double m = 0;
		for (size_t j = 0; j < riverWaves.size(); ++j)
			m += meander * kRiverWaveShares[j] * std::sin(riverWaves[j] * u + riverPhase[j]);
		return m;
	};
	const auto riverHalf = [&](int i)
	{
		const double u = along[i] / 65536.0 * 2 * kPi;
		return kRiverHalf + kRiverHalfSpan * (0.5 + 0.5 * std::sin(widthWaves * u + widthPhase));
	};
	const auto acrossValley = [&](int i) { return phase[i] / 65536.0 * L.spacing - L.spacing / 2; };

	// THE STAIRS, every stair-spacing tiles along each turn of the hillside, the two slopes of a
	// valley staggered by half a spacing so no stair lines up with the one facing it. Each snakes
	// down its slope in a jog of its own, and carries on across the valley as a road that fords the
	// river and lands on the far bank.
	L.stairCount = std::max(1, int(std::lround(alongLength / o.stairSpacing)));
	const int stairPeriod = 65536 / L.stairCount;
	struct Jog
	{
		double amplitude, wave, phase, kinkPhase;
	};
	std::vector<Jog> jogs(size_t(2 * L.stairCount));
	for (Jog &jog : jogs)
	{
		jog.amplitude = kJogLeast + kJogSpan * context.bounded("rice-stairs", 1000) / 1000.0;
		jog.wave = kJogWave + kJogWaveSpan * context.bounded("rice-stairs", 1000) / 1000.0;
		jog.phase = context.bounded("rice-stairs", 3600) * (2 * kPi / 3600);
		jog.kinkPhase = context.bounded("rice-stairs", 3600) * (2 * kPi / 3600);
	}
	const auto toAlongTiles = [&](double units) { return units / 65536.0 * alongLength; };
	// How far tile i lies across the nearest stair or road of the slope of rising (`rising`) or
	// falling phase, `d` tiles from that slope's crest, in tiles across the path's own line.
	const auto pathOffset = [&](int i, bool rising, double d)
	{
		const int shift = rising ? stairPeriod / 2 : 0;
		const int a = along[i] + shift;
		int offset = (a % stairPeriod + stairPeriod) % stairPeriod;
		int index = a / stairPeriod;
		if (offset > stairPeriod / 2)
		{
			offset -= stairPeriod;
			++index;
		}
		const Jog &jog = jogs[size_t(2 * (index % L.stairCount) + rising)];
		const double angle = 2 * kPi * d / jog.wave + jog.phase;
		const double kinkWave = jog.wave * kJogKinkWave, kink = jog.amplitude * kJogKink;
		const double kinkAngle = 2 * kPi * d / kinkWave + jog.kinkPhase;
		const double sideways = jog.amplitude * std::sin(angle) + kink * std::sin(kinkAngle);
		const double slope = jog.amplitude * 2 * kPi / jog.wave * std::cos(angle) +
							 kink * 2 * kPi / kinkWave * std::cos(kinkAngle);
		return std::abs(toAlongTiles(offset) - sideways) / std::sqrt(1 + slope * slope);
	};

	L.terrain.assign(n, GRASS);
	L.row.assign(n, -1);
	for (auto *mask : {&L.crest, &L.stairs, &L.roads, &L.fords, &L.valley, &L.bund, &L.bank,
					   &L.forest, &L.upper})
		mask->assign(n, 0);
	std::vector<unsigned char> river(n, 0);
	int riverCorners = 0, barCorners = 0, bundCorners = 0;
	for (int i = 0; i < n; ++i)
	{
		const double d = tilesFromCrest(i);
		L.upper[i] = phase[i] < 32768;
		if (d < L.crestHalf)
		{
			L.crest[i] = 1;
			continue;
		}
		if (d < L.valleyStart)
		{
			if (pathOffset(i, L.upper[i], d) < kStairHalf)
			{
				L.stairs[i] = 1;
				L.terrain[i] = TRAIL;
				continue;
			}
			if (d < terracesStart || d >= terracesEnd)
			{
				L.terrain[i] = DIRT;
				continue;
			}
			const double e = d - terracesStart;
			const int k = int(std::floor(e / L.period));
			const double in = e - k * L.period;
			if (in >= L.rows.period())
			{
				L.bund[i] = 1;
				L.terrain[i] = MUD;
				++bundCorners;
				continue;
			}
			const bool wet = in >= L.rows.crops;
			L.row[i] = 2 * k + wet;
			L.terrain[i] = wet ? WATER : GRASS;
			continue;
		}
		L.valley[i] = 1;
		const double s = acrossValley(i);
		const double m = o.river ? riverLine(i) : 0, h = riverHalf(i);
		// A road from either slope, as far as the river's far bank: from the slope of rising phase
		// (where s is negative) a road runs until it is past the river's far edge, from the other
		// likewise in the other direction.
		bool road = false;
		for (bool rising : {true, false})
		{
			const double reach = rising ? m + h + kFordLanding : -(m - h - kFordLanding);
			const double fromCrest = L.spacing / 2 + (rising ? s : -s);
			if (fromCrest <= L.spacing / 2 + std::max(reach, 0.0) &&
				pathOffset(i, rising, fromCrest) < kRoadHalf)
				road = true;
		}
		const bool wet = o.river && std::abs(s - m) < h;
		if (road)
		{
			L.roads[i] = 1;
			L.fords[i] = wet;
			L.terrain[i] = TRAIL;
			continue;
		}
		if (wet)
		{
			L.terrain[i] = WATER;
			river[i] = 1;
			++riverCorners;
			continue;
		}
		// A sand bar on the inside of every strong bend: the bank the river swings away from.
		if (o.river && meander > 0 && std::abs(m) >= kBarSwing * meander &&
			(m < 0 ? s > m + h && s < m + h + kBar : s < m - h && s > m - h - kBar))
		{
			L.terrain[i] = SAND;
			++barCorners;
		}
	}

	// THE TOWNS, on the middle line of the crests, spread as far apart along them as they go: the
	// first at a seeded crest tile, every next one the crest tile farthest from the towns before it.
	// Each is a clearing that the terraces bend round inside a barren ring.
	std::vector<int> ridgeline;
	for (int i = 0; i < n; ++i)
		if (stripeDistance(phase[i]) / 65536.0 * L.spacing < 0.5)
			ridgeline.push_back(i);
	if (ridgeline.empty())
	{
		L.failure = "The crests have no middle line.";
		return L;
	}
	std::vector<std::int64_t> nearestTown(ridgeline.size(), INT64_MAX);
	int next = int(context.bounded("rice-layout", std::uint32_t(ridgeline.size())));
	for (int k = 0; k < teams; ++k)
	{
		const int at = ridgeline[size_t(next)];
		L.homes.push_back({t.remainderX(at) + 0.5, at / t.w + 0.5});
		int farthest = 0;
		for (size_t c = 0; c < ridgeline.size(); ++c)
		{
			const int i = ridgeline[c];
			nearestTown[c] = std::min<std::int64_t>(nearestTown[c],
													t.dist2(t.remainderX(at), at / t.w, t.remainderX(i), i / t.w));
			if (nearestTown[c] > nearestTown[size_t(farthest)])
				farthest = int(c);
		}
		next = farthest;
	}
	dealStarts(context, L.homes);
	if (teams > 1 && nearestSiteDistance(t, L.homes) < 2 * (L.homeRadius + kCap + kCrestMargin))
	{
		L.failure = "Too many colonies for these crests; use a bigger map or fewer colonies.";
		return L;
	}
	L.homeOf.assign(n, -1);
	std::vector<unsigned char> anyTown(n, 0);
	for (int k = 0; k < int(L.homes.size()); ++k)
	{
		const int reach = int(std::ceil(L.homeRadius + kCap)) + 1;
		const int hx = int(std::lround(L.homes[k].x)), hy = int(std::lround(L.homes[k].y));
		for (int dy = -reach; dy <= reach; ++dy)
			for (int dx = -reach; dx <= reach; ++dx)
			{
				const int i = t.at(hx + dx, hy + dy);
				const double r = std::hypot(dx, dy);
				if (r < L.homeRadius)
				{
					L.homeOf[i] = k;
					anyTown[i] = 1;
					L.terrain[i] = GRASS;
					L.row[i] = -1;
					L.stairs[i] = L.roads[i] = L.fords[i] = L.valley[i] = L.bund[i] = 0;
					L.crest[i] = 1;
				}
				else if (r < L.homeRadius + kCap && !L.crest[i] && L.terrain[i] != DIRT)
				{
					L.terrain[i] = DIRT;
					L.row[i] = -1;
					L.stairs[i] = L.bund[i] = 0;
				}
			}
	}

	// THE CRAGS: a small knot of rock at every town's edge, the same distance along the crest from
	// every town's middle, its quarry face looking back at the town. The town's stone.
	std::vector<unsigned char> rockTiles(n, 0), keepGrass(n, 0);
	const double crestHeading = stripeNormal(t, L.across) + kPi / 2;
	L.cragFaces.assign(L.homes.size(), {});
	for (size_t k = 0; k < L.homes.size(); ++k)
	{
		const int cx = int(std::lround(L.homes[k].x + kCragOut * std::cos(crestHeading)));
		const int cy = int(std::lround(L.homes[k].y + kCragOut * std::sin(crestHeading)));
		L.cragFaces[k] = drawKnot(L, cx, cy, kCragRadius, 0, 0, crestHeading + kPi, kCragFace,
								  rockTiles, keepGrass);
		if (int(L.cragFaces[k].size()) < 1)
		{
			L.failure = "A town's quarry crag has no face.";
			return L;
		}
	}
	// A town is what lies inside its ring, less its crag.
	for (int i = 0; i < n; ++i)
		if (L.terrain[i] == RIDGE_ROCK)
			L.homeOf[i] = -1;

	// THE OUTCROPS: two rock knots a colony in the valleys' most open ground (the valley grass
	// farthest from water, paving, sand and the caps), kOutcropApart from each other, with a quarry
	// face on the side towards the nearer crest.
	std::mt19937 &outcropRandom = context.stream("rice-outcrops");
	const std::vector<std::int64_t> clearance = [&]
	{
		std::vector<unsigned char> blocked(n, 0);
		for (int i = 0; i < n; ++i)
			blocked[i] = !L.valley[i] || L.terrain[i] != GRASS;
		return distanceSquaredTo(t, blocked);
	}();
	int outcrops = 0;
	// Tiles within kOutcropApart of an outcrop's face, where no other outcrop may stand.
	std::vector<unsigned char> crowded(n, 0);
	for (int pick = 0; pick < kOutcropsPerColony * teams; ++pick)
	{
		const double lobe = outcropRandom() % 3600 * (2 * kPi / 3600);
		int centre = -1;
		for (int i = 0; i < n; ++i)
			if (!crowded[i] && (centre < 0 || clearance[i] > clearance[centre]))
				centre = i;
		if (centre < 0 || clearance[centre] < std::int64_t(kOutcropRoom * kOutcropRoom))
		{
			context.telemetry.fallback("rice.outcrops.omitted", "No open valley grass for an outcrop.");
			break;
		}
		const int cx = t.remainderX(centre), cy = centre / t.w;
		const double toCrest = stripeNormal(t, L.across) + (acrossValley(centre) < 0 ? kPi : 0);
		const std::vector<int> face = drawKnot(L, cx, cy, kOutcropRadius, kOutcropLobe, lobe, toCrest,
											   kOutcropFace, rockTiles, keepGrass);
		if (face.empty())
		{
			context.telemetry.fallback("rice.outcrops.faceless", "An outcrop found no quarry face.");
			break;
		}
		const int fx = t.remainderX(face.front()), fy = face.front() / t.w;
		const int apart = int(std::ceil(kOutcropApart));
		for (int dy = -apart; dy <= apart; ++dy)
			for (int dx = -apart; dx <= apart; ++dx)
				if (dx * dx + dy * dy < kOutcropApart * kOutcropApart)
					crowded[t.at(fx + dx, fy + dy)] = 1;
		L.outcropFaces.push_back(face);
		++outcrops;
	}

	// THE BANKS: loam along the river, frayed by a broad noise, off the roads, the bars, and the foot
	// of every outcrop (where stone above the default amount grows).
	int bankCorners = 0;
	if (o.river)
	{
		const PeriodicNoise bankWidth(t.w, t.h, 16, context.stream("rice-banks"));
		const std::vector<std::int64_t> toRiver = distanceSquaredTo(t, river);
		const std::vector<std::int64_t> toRock = distanceSquaredTo(t, rockTiles);
		for (int i = 0; i < n; ++i)
		{
			if (!L.valley[i] || L.terrain[i] != GRASS || keepGrass[i])
				continue;
			const double width = kBank + kBankSpan * bankWidth.at(t.remainderX(i), i / t.w);
			if (toRiver[i] >= 0 && double(toRiver[i]) <= width * width &&
				(toRock[i] < 0 || double(toRock[i]) > kBankFoot * kBankFoot))
			{
				L.terrain[i] = LOAM;
				L.bank[i] = 1;
				++bankCorners;
			}
		}
	}

	// THE RIDGE FOREST'S GROUND: crest grass beyond the growth probe of any water, clear of the
	// towns, the crags and the paving.
	{
		std::vector<unsigned char> wet(n, 0), clear(n, 0);
		for (int i = 0; i < n; ++i)
		{
			wet[i] = L.terrain[i] == WATER;
			clear[i] = anyTown[i] || rockTiles[i] || keepGrass[i];
		}
		const std::vector<unsigned char> watered = dilate(t, wet, kForestDry);
		const std::vector<unsigned char> nearTown = dilateRound(t, clear, kCap + kForestClear);
		for (int i = 0; i < n; ++i)
			L.forest[i] = L.crest[i] && L.terrain[i] == GRASS && !watered[i] && !nearTown[i];
	}

	// Every town's paved square round its middle.
	for (size_t k = 0; k < L.homes.size(); ++k)
		paveCore(L.terrain, t, L.homes[k].x, L.homes[k].y, kPave,
				 [&L, k](int i) { return L.homeOf[i] == int(k); });

	// THE LOOK, painted last over the mechanics: boardwalk fords, slate for every other paving,
	// lush paddies.
	L.finished = L.terrain;
	LookRoles roles;
	roles.tended = [&L](int i) { return L.homeOf[i] >= 0; };
	roles.path = [&L](int i) { return L.fords[i] ? PathLook::Bridge : PathLook::Town; };
	roles.farm = [&L](int i) { return L.row[i] >= 0 && L.row[i] % 2 == 0; };
	paintClimate(L.finished, t, kRiceLook, roles);

	context.telemetry.measure("rice.stairs.per-turn", L.stairCount);
	context.telemetry.measure("rice.river.water-corners", riverCorners);
	context.telemetry.measure("rice.river.meander", meander);
	context.telemetry.measure("rice.river.bar-corners", barCorners);
	context.telemetry.measure("rice.banks.corners", bankCorners);
	context.telemetry.measure("rice.bunds.corners", bundCorners);
	context.telemetry.measure("rice.outcrops.placed", outcrops);
	context.telemetry.measure("rice.waves.first", waves[0]);
	{
		const auto grassPure = pureTiles(L.terrain, t, GRASS);
		const auto waterPure = pureTiles(L.terrain, t, WATER);
		const double heading = stripeNormal(t, L.across);
		const int sx = int(std::lround(std::cos(heading))), sy = int(std::lround(std::sin(heading)));
		const auto measure = [&](const std::vector<unsigned char> &pure, bool wet, const char *prefix)
		{
			std::vector<int> widths;
			for (int i = 0; i < n; ++i)
			{
				if (!(L.row[i] >= 0 && bool(L.row[i] % 2) == wet && pure[i]))
					continue;
				int width = 1;
				for (int dir : {-1, 1})
				{
					int x = t.remainderX(i), y = i / t.w;
					for (;;)
					{
						x += dir * sx;
						y += dir * sy;
						const int j = t.at(x, y);
						if (pure[j] && L.row[j] == L.row[i])
							++width;
						else
							break;
					}
				}
				widths.push_back(width);
			}
			if (!widths.empty())
			{
				std::sort(widths.begin(), widths.end());
				context.telemetry.measure((std::string(prefix) + ".min").c_str(), widths.front());
				context.telemetry.measure((std::string(prefix) + ".p10").c_str(),
										  widths[widths.size() / 10]);
				context.telemetry.measure((std::string(prefix) + ".median").c_str(),
										  widths[widths.size() / 2]);
			}
		};
		measure(grassPure, false, "rice.crop-width");
		measure(waterPure, true, "rice.water-width");
	}
	return L;
}

bool generate(GenerationWorld &game, GenerationContext &context)
{
	context.stage = "rice terraces layout";
	const RiceTerracesOptions o(context.request);
	const Layout L = design(context.request, context);
	if (!L.failure.empty())
	{
		context.telemetry.fallback("rice.layout.failure", L.failure);
		context.detail = L.failure;
		return false;
	}
	const Torus &t = L.t;
	GenerationMap &map = game.map;
	const int n = t.size(), teams = context.request.nbTeams;
	for (int k = 0; k < teams; ++k)
		game.addTeam();
	writeVertices(map, L.finished);

	context.stage = "rice terraces towns";
	if (!settleRoundColonies(game, context, "rice-starts", L.homeOf, L.homes, L.homeRadius))
		return false;
	const auto free = [&](int i) { return clearGround(map, t.remainderX(i), i / t.w); };
	const auto allowed = [&](int i, int type)
	{ return free(i) && map.isResourceAllowed(t.remainderX(i), i / t.w, type); };
	const auto home = [&](int k)
	{
		std::vector<unsigned char> mask(n, 0);
		for (int i = 0; i < n; ++i)
			mask[i] = L.homeOf[i] == k && map.terrainPropertiesAt(t.remainderX(i), i / t.w).buildable;
		return mask;
	};
	const double normal = stripeNormal(t, L.across);
	for (int k = 0; k < teams; ++k)
	{
		const ShapePoint h = L.homes[k];
		if (placeStartingBuilding(game, k, "inn", 0, h.x + 6 * std::cos(normal),
								  h.y + 6 * std::sin(normal), 4, home(k), {WHEAT}) < 0)
		{
			context.detail = "A town has no room for its starter inn.";
			return false;
		}
		// The town's crag face: a starter supply, the same at every stone amount.
		int quarry = 0;
		for (int i : L.cragFaces[k])
			if (allowed(i, STONE))
			{
				map.setResourceByIndex(t.remainderX(i), i / t.w, STONE, 1);
				++quarry;
			}
		context.telemetry.measure("rice.starter.quarry-tiles", quarry, k);
		if (quarry < 1)
		{
			context.detail = "A town's quarry crag holds no stone.";
			return false;
		}
	}

	context.stage = "rice terraces crops";
	// Starter crops on the first rice strip below each town, down both slopes of its crest: wheat
	// from the strip tile nearest the town on each slope, wood from the upper slope's nearest tile
	// at least a patch's width from that slope's wheat. With no beaches, a tile on a grass and
	// water seam is clear ground but holds no crop, so every seed must be able to hold its crop.
	const auto firstStrip = [&](int type)
	{ return [&, type](int i) { return L.row[i] == 0 && allowed(i, type); }; };
	for (int k = 0; k < teams; ++k)
	{
		const int hx = int(L.homes[k].x), hy = int(L.homes[k].y);
		const int reach = int(L.homeRadius + kCap) + 14;
		// Whether a patch of `count` fits in the strip pocket round a tile: a stair or the town's
		// ring can cut a strip into pieces too short for one.
		const auto roomFor = [&](int seed, int count, int type)
		{
			const auto fits = firstStrip(type);
			std::vector<int> queue{seed};
			std::vector<int> seen{seed};
			for (size_t q = 0; q < queue.size() && int(queue.size()) < count; ++q)
			{
				const int x = t.remainderX(queue[q]), y = queue[q] / t.w;
				for (const auto &[ox, oy] : {std::pair{1, 0}, {-1, 0}, {0, 1}, {0, -1}})
				{
					const int j = t.at(x + ox, y + oy);
					if (fits(j) && std::find(seen.begin(), seen.end(), j) == seen.end())
					{
						seen.push_back(j);
						queue.push_back(j);
					}
				}
			}
			return int(queue.size()) >= count;
		};
		const auto nearest = [&](bool upper, int awayFrom, int count, int type)
		{
			const auto fits = firstStrip(type);
			int best = -1;
			std::int64_t bestDistance = 0;
			for (int dy = -reach; dy <= reach; ++dy)
				for (int dx = -reach; dx <= reach; ++dx)
				{
					const int i = t.at(hx + dx, hy + dy);
					if (!fits(i) || bool(L.upper[i]) != upper ||
						(awayFrom >= 0 &&
						 t.dist2(t.remainderX(i), i / t.w, t.remainderX(awayFrom), awayFrom / t.w) < 100))
						continue;
					const std::int64_t distance = std::int64_t(dx) * dx + std::int64_t(dy) * dy;
					if ((best < 0 || distance < bestDistance) && roomFor(i, count, type))
					{
						best = i;
						bestDistance = distance;
					}
				}
			return best;
		};
		int wheat = 0, upperWheat = -1;
		for (bool upper : {false, true})
			if (const int seed = nearest(upper, -1, kStarterWheat, WHEAT); seed >= 0)
			{
				wheat += growPatch(map, t, seed, WHEAT, kStarterWheat, firstStrip(WHEAT));
				if (upper)
					upperWheat = seed;
			}
		const int woodSeed = nearest(true, upperWheat, kStarterWood, WOOD);
		const int wood =
			woodSeed >= 0 ? growPatch(map, t, woodSeed, WOOD, kStarterWood, firstStrip(WOOD)) : 0;
		context.telemetry.measure("rice.starter.wheat-tiles", wheat, k);
		context.telemetry.measure("rice.starter.wood-tiles", wood, k);
		if (wheat < kStarterWheat || wood < kStarterWood / 2)
		{
			context.detail = "A town's starter terraces do not fit (" + std::to_string(wheat) +
							 " wheat, " + std::to_string(wood) + " wood).";
			return false;
		}
	}
	// The terraces: rice (wheat) over most of the strips and a little timber, in patches so the
	// strips can be harvested from their ends and their bunds.
	const Fertility::Field fertility = Fertility::forMap(map, false);
	const std::vector<int> patch = periodicNoise(t.w, t.h, 6, context.stream("rice-patch"));
	const std::vector<int> split = periodicNoise(t.w, t.h, 5, context.stream("rice-split"));
	const auto strip = [&](int i) { return L.row[i] >= 0 && L.row[i] % 2 == 0 && free(i); };
	int stripArea = 0;
	for (int i = 0; i < n; ++i)
		stripArea += strip(i) && fertility.at(t.remainderX(i), i / t.w) > 0;
	furnishGround(
		map, t, context, fertility, strip, [&](int i) { return float(patch[i]); },
		[&](int i) { return split[i]; },
		[&](int)
		{
			return GroundAmounts{int(scaledCount(stripArea * 50 / 100, o.wheat)),
								 int(scaledCount(stripArea * 6 / 100, o.wood)), 0, 0};
		},
		"rice-stone", "rice-fruit");

	// The ridge forest: clumps of timber along the crests between the towns, beyond the reach of
	// any water, so it never grows back or spreads; scaled by the wood amount.
	{
		const std::vector<int> cover =
			periodicNoise(t.w, t.h, kForestPatch, context.stream("rice-forest"));
		std::vector<int> ground;
		for (int i = 0; i < n; ++i)
			if (L.forest[i] && allowed(i, WOOD))
				ground.push_back(i);
		std::stable_sort(ground.begin(), ground.end(), [&](int a, int b) { return cover[a] > cover[b]; });
		const int wanted = int(scaledCount(int(ground.size()) * kForestShare / 100, o.wood));
		int placed = 0;
		for (int i : ground)
		{
			if (placed >= wanted)
				break;
			map.setResourceByIndex(t.remainderX(i), i / t.w, WOOD, 1);
			++placed;
		}
		context.telemetry.measure("rice.forest.ground", int(ground.size()));
		context.telemetry.measure("rice.forest.wood-tiles", placed);
	}

	// The banks' crops: each stretch of loam is wheat or wood by a broad noise, and within its share
	// the patchiest ground (a finer noise's highest) is planted, so the fields lie in clumps.
	{
		std::mt19937 &cropRandom = context.stream("rice-bank-crops");
		const std::vector<int> cover = periodicNoise(t.w, t.h, kBankPatch, cropRandom);
		const std::vector<int> stretch = periodicNoise(t.w, t.h, kBankStretch, cropRandom);
		const std::vector<unsigned char> loam = pureTiles(L.terrain, t, LOAM);
		std::vector<int> wheatGround, woodGround;
		for (int i = 0; i < n; ++i)
			if (loam[i] && L.valley[i] && free(i))
				(stretch[i] < 65536 * kBankWheat / (kBankWheat + kBankWood) ? wheatGround : woodGround)
					.push_back(i);
		const int bankTiles = int(wheatGround.size() + woodGround.size());
		const auto plant = [&](std::vector<int> &ground, int type, int share, int amount)
		{
			std::stable_sort(ground.begin(), ground.end(),
							 [&](int a, int b) { return cover[a] > cover[b]; });
			const int wanted = int(scaledCount(bankTiles * share / 100, amount));
			int placed = 0;
			for (int i : ground)
			{
				if (placed >= wanted)
					break;
				if (!map.isResourceAllowed(t.remainderX(i), i / t.w, type))
					continue;
				map.setResourceByIndex(t.remainderX(i), i / t.w, type, 1);
				++placed;
			}
			return placed;
		};
		const int bankWheat = plant(wheatGround, WHEAT, kBankWheat, o.wheat);
		const int bankWood = plant(woodGround, WOOD, kBankWood, o.wood);
		context.telemetry.measure("rice.banks.tiles", bankTiles);
		context.telemetry.measure("rice.banks.wheat-tiles", bankWheat);
		context.telemetry.measure("rice.banks.wood-tiles", bankWood);
	}

	// The outcrops' quarry faces, the valleys' stone: the rock is structural and always drawn, its
	// face stocked in proportion to the stone amount. Above the default the face is full, and the
	// rest grows along the outcrop's foot beside it.
	const auto commonsGround = [&](int i) { return L.valley[i] && !L.roads[i] && free(i); };
	int outcropStone = 0;
	for (const std::vector<int> &face : L.outcropFaces)
	{
		const int wanted = int(scaledCount(int(face.size()), o.stone));
		int stocked = 0;
		for (int f = 0; f < int(face.size()) && stocked < wanted; ++f)
			if (allowed(face[f], STONE))
			{
				map.setResourceByIndex(t.remainderX(face[f]), face[f] / t.w, STONE, 1);
				++stocked;
			}
		if (stocked < wanted)
			stocked += plantQuarryFaces(map, t,
										{KitSeed{t.remainderX(face.front()), face.front() / t.w, 6}},
										wanted - stocked, commonsGround);
		outcropStone += stocked;
	}
	context.telemetry.measure("rice.outcrops.stone-tiles", outcropStone);

	// Fruit groves in the valleys.
	std::vector<int> commons;
	for (int i = 0; i < n; ++i)
		if (commonsGround(i))
			commons.push_back(i);
	{
		const int wanted = int(scaledCount(int(commons.size()) / 700 + 3, o.fruit));
		const char *stream = "rice-fruit";
		const int actual = scatterClumps(
			context, t, commons, wanted, stream, free,
			[&](MapGeneratorPoint p)
			{ placeResourceClump(map, context, p, CHERRY + int(context.bounded(stream, 3)), 1); });
		context.telemetry.measure("rice.groves.placed", actual);
	}
	seedAlgae(map, context, t, "rice-algae", o.algae, AlgaeBand::anyWater(60));
	// The generic first-crop rescue may only plant on a rice strip, never in a town or the valley.
	std::vector<unsigned char> topup(n, 0);
	for (int i = 0; i < n; ++i)
		topup[i] = L.row[i] >= 0 && L.row[i] % 2 == 0;
	secureStartingCrops(game, context, t, 24, 32, 0, nullptr, &topup);
	return true;
}

std::string atTile(const Torus &t, int i)
{
	return "(" + std::to_string(t.remainderX(i)) + ", " + std::to_string(i / t.w) + ")";
}

std::string validateWorld(const GenerationWorld &game, const GenerationContext &context)
{
	GenerationContext replay(context.request);
	const Layout L = design(context.request, replay);
	if (const std::string mismatch = designMismatch(L, game.map, "rice terraces"); !mismatch.empty())
		return mismatch;
	const GenerationMap &map = game.map;
	const Torus &t = L.t;
	const int n = t.size(), teams = context.request.nbTeams;
	// The terrain is the design's, vertex for vertex: no later stage may repaint it.
	for (int i = 0; i < n; ++i)
		if (map.vertexTerrainAt(t.remainderX(i), i / t.w) != L.finished[i])
			return "The terraces' terrain differs from their design at " + atTile(t, i) + ".";
	std::vector<unsigned char> rock(n, 0);
	for (int i = 0; i < n; ++i)
		rock[i] = L.terrain[i] == RIDGE_ROCK;
	if (const int bad = wallTerrainMismatch(map, t, rock, TerrainGroup::Ridges); bad >= 0)
		return "A crag or outcrop is not Ridges at " + atTile(t, bad) + ".";
	const auto open = walkableTiles(map);
	const auto paving = pureTiles(L.terrain, t, TRAIL);
	for (int i = 0; i < n; ++i)
		if ((L.stairs[i] || L.roads[i]) && paving[i] && !open[i])
			return "A terrace stair or valley road is blocked at " + atTile(t, i) + ".";
	// Crop containment as a future footprint: no pure grass reachable from a town, on eight
	// neighbours, may be a rice strip, whatever grows or is harvested later.
	const auto grass = pureTiles(L.terrain, t, GRASS);
	const auto buildable = buildableTiles(map);
	for (int k = 0; k < teams; ++k)
	{
		std::vector<unsigned char> home(n, 0);
		for (int i = 0; i < n; ++i)
			home[i] = L.homeOf[i] == k;
		const auto fromHome = stepsFrom(t, home, grass);
		for (int i = 0; i < n; ++i)
			if (fromHome[i] >= 0 && L.row[i] >= 0)
				return "Terrace crops could grow into a town.";
		if (buildSites(t, buildable, home) < kMinimumRoom)
			return "A town lacks building room.";
	}
	// The terraces hold: with every stair shut, nobody on a crest walks down to a valley, along the
	// bunds or across harvested paddies. Every tile with a stair corner is shut.
	{
		std::vector<unsigned char> shut(n, 0);
		for (int i = 0; i < n; ++i)
		{
			const int x = t.remainderX(i), y = i / t.w;
			for (int c = 0; c < 4 && !shut[i]; ++c)
				shut[i] = L.stairs[t.at(x + c % 2, y + c / 2)] != 0;
		}
		std::vector<int> units;
		for (const std::vector<int> &team : unitTilesByTeam(map, teams))
			units.insert(units.end(), team.begin(), team.end());
		const int leak = leakWithShut(map, t, units, shut, false, [&](int i) { return !L.valley[i]; });
		if (leak >= 0)
			return "The terraces leak from a crest to a valley at " + atTile(t, leak) + ".";
	}
	if (const int parted = partingBarrier(map); parted >= 0)
		return "Rock parts the walkable ground at " + atTile(t, parted) + ".";
	return walkFromFirstColony(map, teams, "the terraces",
							   "along the crests, stairs and valley roads")
		.error;
}
} // namespace

RiceTerracesOptions::RiceTerracesOptions(const GenerationRequest &r)
	: hillsides(r.option("hillsides")), slant(r.option("slant")), terraces(r.option("terraces")),
	  bandWidth(r.option("band-width")), waviness(r.option("waviness")),
	  stairSpacing(r.option("stair-spacing")), homeSize(r.option("home-size")),
	  river(r.option("valley-river") != 0), wheat(r.option("wheat-amount")),
	  wood(r.option("wood-amount")), stone(r.option("stone-amount")),
	  algae(r.option("algae-amount")), fruit(r.option("fruit-amount"))
{
}

GeneratorDefinition riceTerracesDefinition()
{
	return {
		"rice-terraces",
		52,
		"Rice terraces",
		6,
		false,
		// One hillside at a slant of one is a single terraced slope spiralling round the torus: on
		// a 256 map it crosses twice. Terraces should be most of the map (a first render with two
		// hillsides of four terraces was mostly grass); the `terraces` control asks for a count per
		// slope but the room a slope actually holds, at a workable band width, wins (see
		// rice.terraces.reduced telemetry) — a 256 map with one hillside fits about three at the
		// default band width. Waviness is the long sway's amplitude in tiles.
		{GeneratorControl{"hillsides", "Hillsides", 1, 6, 1, 1, ControlGroup::Terrain}
			 .withSearchRange(1, 4),
		 GeneratorControl{"slant", "Slant", 0, 3, 1, 1, ControlGroup::Terrain}.withSearchRange(0,
																							   3),
		 GeneratorControl{"terraces", "Terraces per slope", 2, 12, 1, 8, ControlGroup::Terrain}
			 .withSearchRange(5, 10),
		 GeneratorControl{"band-width", "Contour band width", 90, 140, 10, 100,
						  ControlGroup::Terrain}
			 .withSearchRange(90, 120),
		 GeneratorControl{"waviness", "Waviness", 0, 20, 2, 16, ControlGroup::Terrain}
			 .withSearchRange(8, 20),
		 GeneratorControl{"stair-spacing", "Stair spacing", 24, 96, 8, 48, ControlGroup::Layout}
			 .withSearchRange(32, 64),
		 GeneratorControl{"home-size", "Home size", 10, 18, 1, 12, ControlGroup::Layout}
			 .withSearchRange(12, 18),
		 GeneratorControl::toggle("valley-river", "Valley river", true, ControlGroup::Terrain)
			 .withSearchValues({0, 1}),
		 GeneratorControl::percentage("wheat-amount", "Wheat amount"),
		 GeneratorControl::percentage("wood-amount", "Wood amount"),
		 GeneratorControl::percentage("stone-amount", "Stone amount"),
		 GeneratorControl::percentage("algae-amount", "Algae amount"),
		 GeneratorControl::percentage("fruit-amount", "Fruit amount")},
		generate,
		true,
		designFailure<design>,
		validateWorld,
		{"terrain:natural", "feature:terraces", "feature:river", "feature:farmland",
		 "style:tight-building", "fairness:stamped-lattice"}};
}
