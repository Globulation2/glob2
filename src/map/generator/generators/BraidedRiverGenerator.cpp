// SPDX-License-Identifier: GPL-3.0-or-later
#include "BraidedRiverGenerator.h"
#include "Channels.h"
#include "Drawing.h"
#include "FertilityField.h"
#include "Game.h"
#include "GenerationContext.h"
#include "Geometry.h"
#include "Grid.h"
#include "Homes.h"
#include "LatticeNoise.h"
#include "Morphology.h"
#include "Patterns.h"
#include "Pipeline.h"
#include "Planting.h"
#include "Resources.h"
#include "Roads.h"
#include "Settlements.h"
#include "Sketch.h"
#include "Topology.h"
#include "Walls.h"
#include <algorithm>
#include <climits>
#include <cmath>
#include <cstdint>
#include <numeric>
#include <string>
#include <utility>
#include <vector>
using namespace MapGeneration;

// Braided river: a glacial outwash plain. A wide belt of interlaced channels and gravel bars runs
// the long way round the map, with a dry terrace along either bank and a wall of bedrock bluffs
// where the two terraces meet across the far seam of the torus. Homes stand on the terraces;
// every bar in the braid is fertile ground, and on foot the only way from one bank to the other
// is to hop from bar to bar over the riffles (sand shoals) that happen to join them this map.
//
// The braid is built from channel threads: every thread is a sinusoid that wraps the map a whole
// number of times, laid in its own lane across the belt, and neighbouring threads swing in
// antiphase, so every adjacent pair crosses twice a period and the land between two crossings is
// a lens-shaped bar. Slow drift of each thread's phase, swell of its amplitude and swelling and
// narrowing of its width, all whole cycles of the lap, keep the weave from reading as a pattern.
// The bars are then read back off the rasterized water as its land components; the channel
// stretches that part two bars are the edges of a graph (channelCrossings, Channels.h), and a
// random spanning tree of that graph, plus a share of the leftover stretches as loops and one
// forced crossing per colony to the bar nearest its home, says which stretches get a riffle
// (fordAlong, stampFord). Everything the design decides is a pure function of the request and the
// context's named streams, so validateWorld rebuilds it and checks the finished world against
// it: the bluffs stand complete, every channel keeps a core of open water except at its riffles
// (channelCoreFault), every riffle is open from bank to bank (fordFault), and every colony can
// walk to colony 0 and onto the bar it was promised.
//
// WHY IT PLAYS WELL (docs/map-generators/GAME_RULES_FOR_MAP_DESIGN.md). Wheat and wood regrow
// only within fifteen tiles of water, so a bar with channels on every side is the richest ground
// on the map, the terrace's own bank strip is middling, and the terrace behind a home, where the
// town is built, never grows anything: the town cannot be overgrown, and every colony's food comes
// from the bank strip and from the bars. The bar nearest each home is joined to its bank by a
// forced riffle and stocked with wheat and wood, so no start starves across water; every further
// bar is a decision, richer and more exposed the deeper into the braid it lies, and a bar with
// room for an inn is a forward base. Raiders cross the same riffles the farmers use, and which
// bars join which is redrawn every map, so scouting the braid is worth the trip. Channels are four
// to seven corners wide, so towers on one bar cover the next (Channels.h: banks eight to eleven
// tiles apart, a level-one tower reaches five), and the moraine, a broken line of stone hummocks
// four tiles up the bank, gives a terrace cover with gaps to hold rather than a wall to hide
// behind. The bedrock wall at the seam keeps the terraces from being one plain: without it the
// walk round the back of the torus would be shorter than the braid, and the braid would be
// scenery.
//
// THE SIZES AT THE DEFAULTS (256x256, braid 40%, 6 channels, bars 40, riffles 25%), measured over
// the seeds of the pull request's matrix: a belt 102 tiles wide with a 77-tile terrace either
// side; six threads in lanes 14.2 tiles apart, swinging 9 to 12 tiles, three periods of 85 tiles
// round the lap; 13 to 18 bars of 300 to 800 tiles of grass (the biggest lenses about 25 tiles of
// grass wide and 40 long); about 50 crossable stretches of 4 tiles or more, of which the forced
// crossings and the spanning tree open 17 to 19 and the loops three or four more; homes 10 steps
// up the bank. A 128 map gets 3 threads and 4 to 6 bars, a 512 map 7 threads and about 55 bars.
// Every map of the pull request's 2,072-run matrix (all controls at their ends, 128 to 512 tiles,
// rectangles, 1 to 12 colonies) generated, in under a second at 256 and under 10 at 512.
namespace
{

constexpr const char *kThreadStream = "braided-river-threads";
constexpr const char *kRiffleStream = "braided-river-riffles";
constexpr const char *kHomeStream = "braided-river-homes";
constexpr const char *kMoraineStream = "braided-river-moraine";
constexpr const char *kResourceStream = "braided-river-resources";

// --- The belt and its lanes ---------------------------------------------------------------------
//
// A thread's half width in undermap corners swells and narrows along the lap between its base and
// base plus swell: 2.2 to 3.5, so channels are 4.4 to 7 corners wide, 3 to 6 tiles of open water
// across. The floor matters for the game: a channel's pure-water tiles must stay 4-connected so no
// unit can step over it diagonally (channelCoreFault), and at a half width under about 1.8 a
// diagonal stretch loses that; 2.2 leaves a margin for the rounding of the disc stamp. The
// ceiling sets the lane arithmetic: the outermost threads' swing plus this radius must stay
// inside the belt.
constexpr double kWidthBaseLow = 2.2, kWidthBaseHigh = 2.5;
constexpr double kWidthSwellLow = 0.5, kWidthSwellHigh = 1.0;
constexpr double kMaxRadius = kWidthBaseHigh + kWidthSwellHigh;
// A thread's swing, as a share of the lane pitch. Two antiphase neighbours a pitch apart cross when
// the sum of their swings times the sine of half their phase difference exceeds the pitch, so
// with swings of 0.65 to 0.85 and a phase difference of 180 +/- 80 degrees the worst case is
// 2 * 0.65 * sin(50 deg) = 1.0: they still (just) meet, and most pairs cross by a comfortable
// margin. Larger swings make fatter lenses and bulge the terrace edge further; 0.85 is where the
// terrace bulges start to read as bars of their own.
constexpr double kSwingLow = 0.65, kSwingHigh = 0.85;
// The phase jitter, in degrees, either side of antiphase between neighbouring threads. At +/-50
// every pair crossed in the same few columns and the braid read as a chain of links; +/-80
// spreads the crossings by up to a quarter period between rows. The crossing guarantee above
// bounds it.
constexpr double kPhaseJitterDegrees = 80;
// A second harmonic at this share of the swing, and a slow drift of the phase (0.3 to 0.7 radians
// over one or two cycles of the lap) and swell of the amplitude (10% to 25% over one to three
// cycles): together they move each crossing a different way, so no two periods of the braid are
// alike. All whole cycles of the lap, so the seam cannot be seen.
constexpr double kSecondHarmonic = 0.15;
constexpr double kDriftLow = 0.3, kDriftHigh = 0.7;
constexpr double kSwellLow = 0.10, kSwellHigh = 0.25;
// The lane pitch is clamped to this range by adding or dropping threads: a lens between two
// antiphase threads is up to about two pitches wide before the channels and their beaches take
// their 8 tiles (kChannelSpoiledTiles plus the water), so under 11 a bar is a strip of beach with
// a thread of grass, and over 26 a bar is a field the size of a home and the braid stops reading
// as a braid. The floor is 11 rather than 12 so that a 128 map at the default width (a 51-tile
// belt, whose three-thread pitch is 11.9) keeps three threads and two rows of bars: with two it
// was a single chain of lenses, not a braid. Two threads at least kNarrowPitch apart is the least
// braid there is; a belt too narrow for that (every 64-tile map at any width) is refused.
constexpr double kPitchFloor = 11, kPitchCeiling = 26, kNarrowPitch = 8;
// Samples along a thread every half tile, as Watershed's channels: dense enough that the discs
// stamped at each sample overlap into a smooth tube, and that the core check walks the centre line
// one tile at a time.
constexpr double kSpacing = 0.5;

// --- Riffles --------------------------------------------------------------------------------------
//
// A riffle is a SandFord (Channels.h) across a channel: sand within kRiffleHalfWidth of its line
// along the river (three rows of pure sand, five walkable) and within the channel's radius plus
// kRiffleReach across, so it meets the beach on both banks. Watershed's fords use the same
// numbers; anything narrower rasterizes with a wet corner on a diagonal channel.
constexpr double kRiffleHalfWidth = 2.0, kRiffleReach = 1.5;
// A channel stretch qualifies as a crossing when the same two bars face each other across it for at
// least this many tiles (so the riffle has straight channel either side and lands on real bar,
// not a lens tip). When the two banks are not joined through the long stretches, stretches down
// to the short length are used as well, and the map says so in its telemetry. Whether a riffle
// at a stretch's middle really lands on land at both ends and leaves open water either side of
// it is not guessed from the threads' geometry but tried on the sketch (stampFord, then
// fordFault, Channels.h): a first version kept every point within a fixed distance of another
// thread's centre line off the list, and in a crowded braid the inner threads, always within
// that distance of one neighbour or the other, got no crossing at all (one 128 map in eight
// failed at the defaults, two in three at 60% width).
constexpr double kRiffleRun = 8, kRiffleShortRun = 4;
// Two riffles on one thread keep this far apart along it: a riffle is 4 tiles of sand along the
// channel and its check probes 2.5 tiles past either end for open water, so any nearer and the
// two merge into one long shoal that the check refuses (it did, twice in 1,560 maps).
constexpr double kRiffleGap = 9;
// A bar counts as one, for the crossing graph and the resource layers, from this many land tiles;
// smaller scraps at lens tips are scenery, except that a scrap of at least kScrapTiles may still
// be hopped across when nothing else joins the banks (a crowded braid of short bars leaves a
// terrace facing only scraps; before this, two seeds in three failed at 128 with a 60% braid).
// The bar a colony is promised must hold at least kGuaranteedBarGrass tiles of pure grass for its
// wheat and wood, or, failing that, the fallback floor; a crossing to a scrap smaller than that
// would be a promise of a beach.
constexpr int kBarTiles = 40, kScrapTiles = 12;
constexpr int kGuaranteedBarGrass = 50, kGuaranteedBarGrassFloor = 20;
// Deposits keep off a riffle's tiles and this far beyond them, so its landings stay walkable
// however the fields grow at generation time.
constexpr int kRiffleClear = 2;

// --- Terraces, homes and the wall ---------------------------------------------------------------
//
// A home's swarm stands this many steps (8-connected, from the nearest pure water) up the bank:
// near enough that its kit, planted beside it, still sees the channel (crops regrow within 15
// tiles of water, ever less often further off), far enough that the town behind it is on dry
// ground and never overgrown. The far edge shrinks on a narrow terrace, down to kHomeFarFloor.
constexpr int kHomeNear = 8, kHomeFar = 13, kHomeFarFloor = 11;
// A terrace narrower than this cannot hold a home behind a bank strip in front of the wall's
// wave: the swarm's 8x8 of grass at up to kHomeFar + 5 steps, plus the wall's 4 tiles and a
// couple to spare. Colonies on one bank need this many tiles of river between them, for two 8x8
// squares of grass, their kits and a stretch of bank of their own.
constexpr int kMinimumTerrace = 22, kBankPerColony = 36;
// The kit: wheat and wood either side of the swarm, along the bank, so both lie the same distance
// from the channel as the swarm and neither stands between the swarm and its bar; stone behind,
// on the dry side. 1:1 as every guaranteed placement is.
constexpr int kKitWheat = 14, kKitWood = 14, kKitStoneRadius = 1;
constexpr double kKitBeside = 6, kKitAhead = 3, kKitStoneBehind = 6;
// The town's room: no ambient deposit within this many tiles of a swarm's footprint, so a colony
// has its first buildings' worth of ground before it needs to clear anything.
constexpr int kTownRoom = 7;
// The wall at the seam: a sealed line of stone (traceSealedLap, Drawing.h, so it holds at any
// slant) that waves up to kWallWave tiles either side of the seam in two harmonics, with a vertex
// every kWallStep tiles. Stone only stands on pure grass, and the wall lies at least
// kMinimumTerrace minus kWallWave tiles from any water, so the beach pass never reaches it.
constexpr double kWallWave = 4;
constexpr int kWallStep = 4;
// Home sites keep this many tiles from the wall's furthest wave, so there is always dry ground to
// build on behind a home and a lane along the wall.
constexpr int kWallMargin = 7;

// --- The moraine ----------------------------------------------------------------------------------
//
// Hummocks stand on the contour this many steps up from the water: step 3 is the first pure-grass
// tile past a beach, so 4 leaves a walkable lane of one to two tiles between beach and stone.
// Along the bank they come in runs of 4 to 7 tiles with gaps of 3 to 5 (runsAndGaps, Patterns.h:
// about a third of the line open), so the line is cover with doors, not a wall, and every riffle
// landing on a terrace gets a door of its own: no hummock within kMoraineDoor tiles of one.
constexpr int kMoraineSteps = 4;
constexpr int kMoraineRunLow = 4, kMoraineRunHigh = 7, kMoraineGapLow = 3, kMoraineGapHigh = 5;
constexpr int kMoraineDoor = 5;

// --- Resources ------------------------------------------------------------------------------------
//
// Every bar's farmland covers this share of its eligible grass (furnishGround: plantFields in
// patches, most fertile first, so it lies along the shores and leaves the interior to build on),
// wheat to wood 2:1 as every ambient layer is. A third of a bar under crops keeps a 4x4 site on
// every bar bigger than a scrap; at the 300% ceiling the bars are solid crops and
// reopenCrampedStarts does its work. The terrace's bank strip, the fertile ground behind the
// moraine, gets a quarter of that: it is the poor ground of the map by design, and a home's
// renewal on its own bank is meant to be thin.
constexpr int kBarWheatPercent = 22, kBarWoodPercent = 11;
constexpr int kBankWheatPercent = 5, kBankWoodPercent = 3;
// The dry terrace: small finite woodlots (a radius-1 clump, five tiles) and stone outcrops, one
// per this many dry tiles, so building material is near the town without any of it regrowing.
constexpr int kDryTilesPerWoodlot = 1200, kDryTilesPerOutcrop = 1800;
// Fruit: one small grove per this many map tiles, at least three so every kind is somewhere, on
// bars whose middle lies within this share of the belt's half width of its centre line first (the
// deep bars, so fruit is the prize for crossing the most channels), then on any other bar when
// the deep ones run out (a three-thread braid has only one row of them).
constexpr int kTilesPerGrove = 8000, kFewestGroves = 3;
constexpr double kDeepBarShare = 0.5;
// The promised bar's wheat and wood, radius-2 clumps (about 13 tiles), and how far apart.
constexpr int kBarKitRadius = 2, kBarKitSpacing = 7;
// Algae in every channel, one clump per this many water tiles at 100%, on the half of the water
// where it regrows best (near sand, which the beaches and riffles supply everywhere).
constexpr int kWaterTilesPerAlgae = 50;
constexpr double kAlgaeBestShare = 0.5;

double sq(double v)
{
	return v * v;
}
double unit(GenerationContext &context, const char *stream)
{
	return context.bounded(stream, 1u << 20) / double(1u << 20);
}
double between(GenerationContext &context, const char *stream, double lo, double hi)
{
	return lo + (hi - lo) * unit(context, stream);
}
int wrapIndex(int value, int period)
{
	return ((value % period) + period) % period;
}
double centred(double delta, double period)
{
	return delta - period * std::round(delta / period);
}

// One channel thread: a sinusoid in its lane, with the modulations that make it a river. u runs
// along the lap (the map's longer side, Axes), v across it.
struct Thread
{
	double rest, swing, phase, phase2;
	double driftAmplitude, driftPhase;
	int driftCycles;
	double swellAmplitude, swellPhase;
	int swellCycles;
	double widthBase, widthSwell, widthPhase;
	int widthCycles;
	double vAt(int length, double period, double u) const
	{
		const double lap = 2 * kPi * u / length, theta = 2 * kPi * u / period;
		const double ph = phase + driftAmplitude * std::sin(driftCycles * lap + driftPhase);
		const double sw = swing * (1 + swellAmplitude * std::sin(swellCycles * lap + swellPhase));
		return rest + sw * (std::sin(theta + ph) + kSecondHarmonic * std::sin(2 * theta + phase2));
	}
	double rAt(int length, double u) const
	{
		const double lap = 2 * kPi * u / length;
		return widthBase + widthSwell * (0.5 + 0.5 * std::sin(widthCycles * lap + widthPhase));
	}
};

// A thread as Channels.h sees it: its centre line in map coordinates, a radius per point, closed
// round the lap.
struct ThreadLine
{
	std::vector<ShapePoint> points;
	std::vector<double> radius;
};

// A riffle laid across a crossing.
struct Riffle
{
	int thread, index; // the thread and the point along it
	SandFord ford;
	int a, b; // the bars it joins
	int kind; // 0 forced (a colony's promised bar), 1 spanning tree, 2 extra loop
};

struct Home
{
	int u, v;     // the swarm footprint's top-left, frame coordinates
	int bank;     // 0 the north terrace (v below the belt), 1 the south
	int terrace;  // its terrace's component
	int bar = -1; // the component of the bar it is promised
	int crossing = -1;
	int steps = 0;             // the swarm's middle's steps from water
	std::vector<int> promised; // crossings to bars beside its bank, nearest first
};

// A crossable stretch on one thread.
struct Crossing
{
	int thread;
	ChannelCrossing at;
};

struct Layout
{
	Torus t{1, 1};
	Axes axes{1, 1, true};
	double belt = 0, pitch = 0, period = 1;
	int channels = 0;
	std::vector<Thread> threads;
	std::vector<ThreadLine> lines;
	TerrainSketch terrain;                 // corners: water, beaches and riffles laid
	std::vector<unsigned char> wall;       // the sealed line of bluffs at the seam
	std::vector<unsigned char> hummocks;   // the moraine's stone
	std::vector<int> comp;                 // land component per tile before riffles; -1 water
	std::vector<int> compTiles, compGrass; // per component
	std::vector<double> compV;             // per component: mean v, the bar's depth in the belt
	int terraceNorth = -1, terraceSouth = -1;
	std::vector<Crossing> crossings;
	std::vector<Riffle> riffles;
	std::vector<unsigned char> riffleTiles; // riffles and their landings: kept clear of deposits
	std::vector<Home> homes;                // per colony, dealt
	std::string failure;
	bool isTerrace(int c) const { return c == terraceNorth || c == terraceSouth; }
	bool isBar(int c) const { return c >= 0 && !isTerrace(c) && compTiles[size_t(c)] >= kBarTiles; }
};

// How wide a lane is when the belt holds `channels` threads: the outermost threads swing out to
// the belt's edge less their radius, so the pitch is what is left divided among the gaps between
// threads plus the two outer swings.
double pitchFor(double belt, int channels)
{
	return (belt - 2 * kMaxRadius) / (channels - 1 + 2 * kSwingHigh);
}

// The channel threads: lanes across the belt, antiphase neighbours, every modulation a whole
// number of cycles of the lap. All from the thread stream, so the resource amounts never move a
// channel.
void planThreads(Layout &L, GenerationContext &context, int requested, int barSize)
{
	const Axes &axes = L.axes;
	const double belt = L.belt;
	// Bars are half a period long (antiphase neighbours cross twice a period), and the period must
	// divide the lap: the nearest whole number of periods to the asked-for bar length.
	const int periods = std::max(1, int(std::lround(axes.length() / (2.0 * barSize))));
	L.period = double(axes.length()) / periods;
	int channels = requested;
	while (channels > 2 && pitchFor(belt, channels) < kPitchFloor)
		--channels;
	while (pitchFor(belt, channels) > kPitchCeiling)
		++channels;
	L.channels = channels;
	L.pitch = pitchFor(belt, channels);
	context.telemetry.measure("braided-river.channels.requested", requested);
	context.telemetry.measure("braided-river.channels.actual", channels);
	context.telemetry.measure("braided-river.lanes.pitch", L.pitch);
	context.telemetry.measure("braided-river.bars.period", L.period);
	if (channels != requested)
		context.telemetry.fallback("braided-river.channels.clamped",
								   channels < requested ? "Belt too narrow for the channels asked"
														: "Belt too wide for the channels asked");
	const double centre = axes.breadth() / 2.0;
	const double rest0 = centre - belt / 2 + kMaxRadius + kSwingHigh * L.pitch;
	const double phase0 = between(context, kThreadStream, 0, 2 * kPi);
	for (int k = 0; k < channels; ++k)
	{
		Thread s;
		s.rest = rest0 + k * L.pitch;
		s.swing = L.pitch * between(context, kThreadStream, kSwingLow, kSwingHigh);
		s.phase =
			phase0 + k * kPi +
			between(context, kThreadStream, -kPhaseJitterDegrees, kPhaseJitterDegrees) * kPi / 180;
		s.phase2 = between(context, kThreadStream, 0, 2 * kPi);
		s.driftAmplitude = between(context, kThreadStream, kDriftLow, kDriftHigh);
		s.driftCycles = 1 + int(context.bounded(kThreadStream, 2));
		s.driftPhase = between(context, kThreadStream, 0, 2 * kPi);
		s.swellAmplitude = between(context, kThreadStream, kSwellLow, kSwellHigh);
		s.swellCycles = 1 + int(context.bounded(kThreadStream, 3));
		s.swellPhase = between(context, kThreadStream, 0, 2 * kPi);
		s.widthBase = between(context, kThreadStream, kWidthBaseLow, kWidthBaseHigh);
		s.widthSwell = between(context, kThreadStream, kWidthSwellLow, kWidthSwellHigh);
		// Two, three or five swells a lap: not a multiple of the period count, so the wide
		// stretches fall differently in every period.
		static const int swells[3] = {2, 3, 5};
		s.widthCycles = swells[context.bounded(kThreadStream, 3)];
		s.widthPhase = between(context, kThreadStream, 0, 2 * kPi);
		L.threads.push_back(s);
	}
	L.lines.assign(size_t(channels), {});
	const int count = int(axes.length() / kSpacing);
	for (int k = 0; k < channels; ++k)
	{
		L.lines[k].points.reserve(count);
		L.lines[k].radius.reserve(count);
		for (int i = 0; i < count; ++i)
		{
			const double u = i * kSpacing, v = L.threads[k].vAt(axes.length(), L.period, u);
			L.lines[k].points.push_back({axes.mapX(u, v), axes.mapY(u, v)});
			L.lines[k].radius.push_back(L.threads[k].rAt(axes.length(), u));
		}
	}
}

// Every corner within a thread's radius of its centre line is water: discs at every sample.
void stampThreads(const Layout &L, std::vector<unsigned char> &water)
{
	const int reach = int(std::ceil(kMaxRadius));
	for (const ThreadLine &line : L.lines)
		for (size_t i = 0; i < line.points.size(); ++i)
		{
			const ShapePoint p = line.points[i];
			const int cx = int(std::floor(p.x)), cy = int(std::floor(p.y));
			for (int dy = -reach; dy <= reach; ++dy)
				for (int dx = -reach; dx <= reach; ++dx)
					if (sq(cx + dx - p.x) + sq(cy + dy - p.y) <= sq(line.radius[i]))
						water[L.t.at(cx + dx, cy + dy)] = 1;
		}
}

// The bluffs: a sealed line of tiles round the lap at the seam, waving in two harmonics (whole
// cycles of the lap, from the thread stream).
void planWall(Layout &L, GenerationContext &context)
{
	const double a1 = between(context, kThreadStream, 0.5, 0.65) * kWallWave;
	const double a2 = kWallWave - a1;
	const double p1 = between(context, kThreadStream, 0, 2 * kPi);
	const double p2 = between(context, kThreadStream, 0, 2 * kPi);
	const int c1 = 2 + int(context.bounded(kThreadStream, 2));
	const int c2 = 5 + int(context.bounded(kThreadStream, 3));
	const int length = L.axes.length();
	L.wall.assign(size_t(L.t.size()), 0);
	traceSealedLap(L.wall, L.t, L.axes.alongX, kWallStep,
				   [&](int u)
				   {
					   const double lap = 2 * kPi * u / length;
					   return a1 * std::sin(c1 * lap + p1) + a2 * std::sin(c2 * lap + p2);
				   });
}

// The land parted by the channels: every tile that is not pure water and not wall, in
// 8-connected components (units step diagonally), so a channel whose water core is 4-connected
// parts the ground on its two sides. The two terraces are the components six tiles outside the
// belt on either side, which no thread reaches.
void labelLand(Layout &L)
{
	const Axes &axes = L.axes;
	const int n = L.t.size();
	const std::vector<unsigned char> pureWater = pureTiles(L.terrain, L.t, WATER);
	const std::vector<unsigned char> pureGrass = pureTiles(L.terrain, L.t, GRASS);
	std::vector<unsigned char> land(size_t(n), 0);
	for (int i = 0; i < n; ++i)
		land[i] = !pureWater[i] && !L.wall[i];
	L.comp = connectedRegions(land, L.t.w, L.t.h, true, GridNeighbors::Eight);
	int count = 0;
	for (int c : L.comp)
		count = std::max(count, c + 1);
	L.compTiles.assign(size_t(count), 0);
	L.compGrass.assign(size_t(count), 0);
	L.compV.assign(size_t(count), 0.0);
	for (int i = 0; i < n; ++i)
		if (L.comp[i] >= 0)
		{
			++L.compTiles[L.comp[i]];
			L.compGrass[L.comp[i]] += pureGrass[i] != 0;
			L.compV[L.comp[i]] += axes.vOf(i);
		}
	for (int c = 0; c < count; ++c)
		L.compV[c] /= std::max(1, L.compTiles[c]);
	const int centre = axes.breadth() / 2, half = int(std::ceil(L.belt / 2));
	L.terraceNorth = L.comp[axes.at(0, centre - half - 6)];
	L.terraceSouth = L.comp[axes.at(0, centre + half + 6)];
}

// Every stretch of channel that two bars face each other across, on every thread
// (channelCrossings). Nothing else is asked of a stretch here: whether a riffle at its middle
// really works is tried on the sketch when it is laid.
void findCrossings(Layout &L)
{
	for (int k = 0; k < L.channels; ++k)
	{
		const ThreadLine &line = L.lines[k];
		for (const ChannelCrossing &c :
			 channelCrossings(L.t, line.points, line.radius, L.comp, 2.0,
							  int(kRiffleShortRun / kSpacing), true, [](int) { return true; }))
			L.crossings.push_back({k, c});
	}
}

Riffle riffleAt(const Layout &L, const Crossing &c, int kind)
{
	const ThreadLine &line = L.lines[c.thread];
	return {
		c.thread,
		c.at.index,
		fordAlong(L.t, line.points, line.radius, c.at.index, true, kRiffleHalfWidth, kRiffleReach),
		c.at.a,
		c.at.b,
		kind};
}

// The tiles every riffle and its landings touch, recorded so nothing is planted on the way onto
// it. The sand itself went down when the riffle was laid (chooseRiffles).
void markRiffles(Layout &L)
{
	L.riffleTiles.assign(size_t(L.t.size()), 0);
	for (const Riffle &r : L.riffles)
	{
		const int reach = int(std::ceil(r.ford.span + kRiffleHalfWidth)) + 3;
		const int cx = int(std::floor(r.ford.x)), cy = int(std::floor(r.ford.y));
		for (int dy = -reach; dy <= reach; ++dy)
			for (int dx = -reach; dx <= reach; ++dx)
				if (r.ford.covers(L.t, cx + dx, cy + dy, 1, kRiffleClear))
					L.riffleTiles[L.t.at(cx + dx, cy + dy)] = 1;
	}
}

// Which crossings get a riffle. Every riffle is tried on the sketch before it counts: its sand is
// stamped (stampFord) and the ford checked on the sketch's water (fordFault: open water either
// side of it along the channel, dry across, land at both ends); one that fails is unstamped and
// its crossing passed over. The colonies' promised crossings first, each home taking the nearest
// bar whose crossing works; then a spanning tree over the bars and terraces (Kruskal over the
// crossings in a random order, long stretches between real bars first, and only if the banks are
// still apart a second pass over the short stretches that may hop across scraps as well); then
// the asked-for share of the long bar-to-bar crossings left over, as loops. No two riffles come
// within kRiffleGap of each other along one thread. False, with the failure set, when a home
// finds no crossing that works or the banks stay apart.
bool chooseRiffles(Layout &L, GenerationContext &context, int extraPercent)
{
	const Torus &t = L.t;
	DisjointSets sets(int(L.compTiles.size()));
	std::vector<unsigned char> taken(L.crossings.size(), 0);
	int forced = 0, tree = 0, shortTree = 0, loops = 0, refused = 0;
	const auto tooClose = [&](int c)
	{
		const Crossing &x = L.crossings[c];
		const int n = int(L.lines[x.thread].points.size());
		for (const Riffle &r : L.riffles)
			if (r.thread == x.thread &&
				std::abs(centred(x.at.index - r.index, n)) * kSpacing < kRiffleGap)
				return true;
		return false;
	};
	const auto pureWater = [&](int x, int y)
	{
		return L.terrain[t.at(x, y)] == WATER && L.terrain[t.at(x + 1, y)] == WATER &&
			   L.terrain[t.at(x, y + 1)] == WATER && L.terrain[t.at(x + 1, y + 1)] == WATER;
	};
	// Lays the riffle if it works: stamped, checked, and unstamped again when the check fails.
	const auto lay = [&](int c, int kind)
	{
		if (taken[c] || tooClose(c))
			return false;
		const Riffle riffle = riffleAt(L, L.crossings[c], kind);
		// Not across another thread's centre line: at a junction the two channels' water is one,
		// so the ford's check would pass while its sand silted the other channel.
		for (int k = 0; k < L.channels; ++k)
		{
			if (k == riffle.thread)
				continue;
			for (const ShapePoint &q : L.lines[k].points)
				if (riffle.ford.covers(t, q.x, q.y, 1.0, 1.0))
				{
					++refused;
					return false;
				}
		}
		const int reach = int(std::ceil(riffle.ford.span + riffle.ford.halfWidth)) + 2;
		const int cx = int(std::floor(riffle.ford.x)), cy = int(std::floor(riffle.ford.y));
		std::vector<std::pair<int, unsigned char>> before;
		for (int dy = -reach; dy <= reach; ++dy)
			for (int dx = -reach; dx <= reach; ++dx)
				before.push_back({t.at(cx + dx, cy + dy), L.terrain[t.at(cx + dx, cy + dy)]});
		stampFord(L.terrain, t, riffle.ford);
		if (!fordFault(t, riffle.ford, pureWater).empty())
		{
			for (const auto &corner : before)
				L.terrain[corner.first] = corner.second;
			++refused;
			return false;
		}
		taken[c] = 1;
		sets.unite(L.crossings[c].at.a, L.crossings[c].at.b);
		L.riffles.push_back(riffle);
		return true;
	};
	for (size_t k = 0; k < L.homes.size(); ++k)
	{
		Home &home = L.homes[k];
		for (int c : home.promised)
		{
			const ChannelCrossing &x = L.crossings[c].at;
			const bool already = taken[c];
			if (already || lay(c, 0))
			{
				home.crossing = c;
				home.bar = x.a == home.terrace ? x.b : x.a;
				forced += !already;
				break;
			}
		}
		if (home.crossing < 0)
		{
			L.failure = "A bank has no bar beside it that a riffle can reach; try another seed.";
			return false;
		}
		context.telemetry.measure("braided-river.homes.bar-grass", L.compGrass[home.bar], int(k));
	}
	std::vector<int> order(L.crossings.size());
	std::iota(order.begin(), order.end(), 0);
	context.shuffle(order.begin(), order.end(), kRiffleStream);
	const auto longEnough = [&](int c) { return L.crossings[c].at.run * kSpacing >= kRiffleRun; };
	const auto ground = [&](int comp, int floor)
	{ return L.isTerrace(comp) || L.isBar(comp) || L.compTiles[size_t(comp)] >= floor; };
	for (int pass = 0; pass < 2; ++pass)
	{
		// The second pass, over the short stretches and scraps, only when the banks are still
		// apart.
		if (pass == 1)
		{
			if (sets.joined(L.terraceNorth, L.terraceSouth))
				break;
			context.telemetry.fallback("braided-river.riffles.short-stretches",
									   "Banks not joined through long stretches; using short ones");
		}
		const int floor = pass == 0 ? kBarTiles : kScrapTiles;
		for (int c : order)
		{
			const ChannelCrossing &x = L.crossings[c].at;
			if (taken[c] || (pass == 0 && !longEnough(c)) || !ground(x.a, floor) ||
				!ground(x.b, floor) || sets.joined(x.a, x.b))
				continue;
			if (lay(c, 1))
				++(pass == 0 ? tree : shortTree);
		}
	}
	std::vector<int> spare;
	for (int c : order)
		if (!taken[c] && longEnough(c) && L.isBar(L.crossings[c].at.a) &&
			L.isBar(L.crossings[c].at.b))
			spare.push_back(c);
	const int extra = int(std::lround(spare.size() * extraPercent / 100.0));
	for (int k = 0, laid = 0; k < int(spare.size()) && laid < extra; ++k)
		if (lay(spare[k], 2))
		{
			++laid;
			++loops;
		}
	context.telemetry.measure("braided-river.crossings.found", L.crossings.size());
	context.telemetry.measure("braided-river.riffles.forced", forced);
	context.telemetry.measure("braided-river.riffles.tree", tree);
	context.telemetry.measure("braided-river.riffles.tree-short", shortTree);
	context.telemetry.measure("braided-river.riffles.loops", loops);
	context.telemetry.measure("braided-river.riffles.refused", refused);
	context.telemetry.measure("braided-river.riffles.spare", int(spare.size()) - extra);
	if (!sets.joined(L.terraceNorth, L.terraceSouth))
	{
		L.failure = "The bars do not join the two banks; try another seed.";
		return false;
	}
	return true;
}

// Home sites: one slot per colony along the lap, alternating banks, each home the site on its
// terrace nearest its slot at the right distance up the bank with an 8x8 of pure grass round its
// swarm. Then every home's promised bar: the crossing from its terrace to a bar of real size
// nearest the home.
bool planHomes(Layout &L, GenerationContext &context, int teams)
{
	const Axes &axes = L.axes;
	const int length = axes.length(), breadth = axes.breadth();
	const int terrace = (breadth - int(std::lround(L.belt))) / 2;
	const int farthest = std::max(kHomeFarFloor, std::min(kHomeFar, terrace - 9));
	const std::vector<unsigned char> pureWater = pureTiles(L.terrain, L.t, WATER);
	const std::vector<unsigned char> pureGrass = pureTiles(L.terrain, L.t, GRASS);
	const std::vector<int> waterSteps = stepsFrom(L.t, pureWater);
	const int firstBank = int(context.bounded(kHomeStream, 2));
	const double slot = double(length) / teams;
	for (int k = 0; k < teams; ++k)
	{
		Home home;
		home.bank = (firstBank + k) % 2;
		home.terrace = home.bank == 0 ? L.terraceNorth : L.terraceSouth;
		// The slot's middle, jittered by up to 15% of a slot, so same-bank neighbours are never
		// exactly a stride apart; the search window keeps the 8x8 squares of neighbours apart.
		const double middle = (k + 0.5 + between(context, kHomeStream, -0.15, 0.15)) * slot;
		const int window = std::max(0, int(slot / 2) - 4);
		int bestU = 0, bestV = 0, bestScore = INT_MAX, bestSteps = 0;
		// Two passes: at the designed distance up the bank, then two steps nearer and four further
		// out, should the bank there be all bulge or all beach.
		for (int pass = 0; pass < 2 && bestScore == INT_MAX; ++pass)
		{
			const int nearest = pass == 0 ? kHomeNear : kHomeNear - 2;
			const int reach = pass == 0 ? farthest : farthest + 4;
			const int target = (nearest + reach) / 2;
			if (pass == 1)
				context.telemetry.fallback("braided-river.homes.relaxed-distance",
										   "No site at the designed distance up the bank", k);
			for (int du = -window; du <= window; ++du)
			{
				const int u0 = wrapIndex(int(std::lround(middle)) + du, length);
				const int vFrom = home.bank == 0 ? kWallMargin + 2 : breadth / 2;
				const int vTo = home.bank == 0 ? breadth / 2 : breadth - kWallMargin - 6;
				for (int v0 = vFrom; v0 <= vTo; ++v0)
				{
					const int centre = axes.at(u0 + 2, v0 + 2);
					const int steps = waterSteps[centre];
					if (steps < nearest || steps > reach || L.comp[centre] != home.terrace)
						continue;
					bool open = true;
					for (int dv = -2; dv <= 5 && open; ++dv)
						for (int du2 = -2; du2 <= 5 && open; ++du2)
						{
							const int i = axes.at(u0 + du2, v0 + dv);
							open = pureGrass[i] && L.comp[i] == home.terrace && !L.wall[i];
						}
					if (!open)
						continue;
					// Nearest the slot first, then nearest the designed distance: a home a tile
					// off its slot is nothing, a home at the wrong distance is a different game.
					const int score = std::abs(du) + 3 * std::abs(steps - target);
					if (score < bestScore)
					{
						bestScore = score;
						bestU = u0;
						bestV = v0;
						bestSteps = steps;
					}
				}
			}
		}
		if (bestScore == INT_MAX)
		{
			L.failure = "The terraces have no room for a home; use a bigger map, a narrower braid "
						"or fewer colonies.";
			return false;
		}
		home.u = bestU;
		home.v = bestV;
		home.steps = bestSteps;
		context.telemetry.measure("braided-river.homes.water-steps", bestSteps, k);
		L.homes.push_back(home);
	}
	// Which colony gets which slot is a draw (Pipeline.h): a slot's bank and place are the design's,
	// the team number is not.
	dealStarts(context, L.homes, kHomeStream);
	// Every home's promised bars: the crossings from its terrace to a bar with room for wheat and
	// wood, nearest the home first, those with grass to spare before the smaller ones; the first
	// whose riffle works on the sketch is the one it gets (chooseRiffles).
	for (size_t k = 0; k < L.homes.size(); ++k)
	{
		Home &home = L.homes[k];
		const int centre = axes.at(home.u + 2, home.v + 2);
		const int hx = centre % L.t.w, hy = centre / L.t.w;
		std::vector<std::pair<std::int64_t, int>> ranked;
		for (size_t c = 0; c < L.crossings.size(); ++c)
		{
			const ChannelCrossing &x = L.crossings[c].at;
			const int bar = x.a == home.terrace ? x.b : x.b == home.terrace ? x.a : -1;
			if (bar < 0 || !L.isBar(bar) || L.compGrass[bar] < kGuaranteedBarGrassFloor ||
				x.run * kSpacing < kRiffleRun)
				continue;
			const ShapePoint p = L.lines[L.crossings[c].thread].points[x.index];
			const int i = L.t.at(int(std::floor(p.x)), int(std::floor(p.y)));
			// Bars below the full grass floor rank after every bar above it, however near.
			const std::int64_t key = std::int64_t(L.t.dist2(hx, hy, i % L.t.w, i / L.t.w)) +
									 (L.compGrass[bar] < kGuaranteedBarGrass ? 1LL << 40 : 0);
			ranked.push_back({key, int(c)});
		}
		std::sort(ranked.begin(), ranked.end());
		for (const auto &entry : ranked)
			home.promised.push_back(entry.second);
		if (home.promised.empty())
		{
			L.failure = "A bank has no bar beside it for a colony's food; try another seed.";
			return false;
		}
		const ChannelCrossing &first = L.crossings[home.promised.front()].at;
		if (L.compGrass[first.a == home.terrace ? first.b : first.a] < kGuaranteedBarGrass)
			context.telemetry.fallback("braided-river.homes.small-bar",
									   "No bar of full size beside this bank; a smaller one",
									   int(k));
	}
	return true;
}

// The moraine: hummocks on the contour kMoraineSteps up from the water on each terrace, in runs
// and gaps along the lap, with a door at every riffle landing and none against a home.
void planMoraine(Layout &L, GenerationContext &context)
{
	const Axes &axes = L.axes;
	const int n = L.t.size();
	L.hummocks.assign(size_t(n), 0);
	const std::vector<unsigned char> pureWater = pureTiles(L.terrain, L.t, WATER);
	const std::vector<int> waterSteps = stepsFrom(L.t, pureWater);
	const std::vector<unsigned char> doors = dilate(L.t, L.riffleTiles, kMoraineDoor);
	std::vector<unsigned char> homeSquares(size_t(n), 0);
	for (const Home &home : L.homes)
		for (int dv = -2; dv <= 5; ++dv)
			for (int du = -2; du <= 5; ++du)
				homeSquares[axes.at(home.u + du, home.v + dv)] = 1;
	homeSquares = dilate(L.t, homeSquares, 2);
	for (int bank = 0; bank < 2; ++bank)
	{
		const std::vector<unsigned char> on =
			runsAndGaps(axes.length(), kMoraineRunLow, kMoraineRunHigh, kMoraineGapLow,
						kMoraineGapHigh, context, kMoraineStream);
		const int terrace = bank == 0 ? L.terraceNorth : L.terraceSouth;
		for (int i = 0; i < n; ++i)
			if (L.comp[i] == terrace && waterSteps[i] == kMoraineSteps && on[axes.uOf(i)] &&
				!doors[i] && !homeSquares[i])
				L.hummocks[i] = 1;
	}
}

// Every channel keeps its core except at its own riffles (channelCoreFault), and every riffle is
// open from bank to bank (fordFault). `water(x, y)` answers for a tile.
template <typename Water> std::string checkBraid(const Layout &L, Water water)
{
	for (int k = 0; k < L.channels; ++k)
	{
		const ThreadLine &line = L.lines[k];
		const auto skip = [&](int i)
		{
			for (const Riffle &r : L.riffles)
				if (r.thread == k && r.ford.covers(L.t, line.points[i].x, line.points[i].y, 2, 1))
					return true;
			return false;
		};
		if (const std::string fault = channelCoreFault(L.t, line.points, skip, water);
			!fault.empty())
			return fault;
	}
	for (const Riffle &r : L.riffles)
		if (const std::string fault = fordFault(L.t, r.ford, water); !fault.empty())
			return fault;
	return "";
}

// What the request alone decides, before any draw: the belt's width in tiles, and whether the
// terraces, the belt and the lap can hold the homes and a braid at all. This is the registry's
// validateRequest: the rest of the design can fail on a seed (no bar beside a bank, banks the
// riffles do not join), and such a seed is the service's to roll again, not a bad request.
std::string requestFault(const GenerationRequest &request)
{
	const BraidedRiverOptions o(request);
	const Axes axes = axesFor(1 << request.wDec, 1 << request.hDec);
	const double belt = std::lround(axes.breadth() * o.braidWidth / 100.0);
	const int terrace = (axes.breadth() - int(belt)) / 2;
	const int teams = std::max(1, request.nbTeams);
	if (terrace < kMinimumTerrace)
		return "The braid leaves too little terrace for the homes; use a bigger map or a "
			   "narrower braid.";
	if (pitchFor(belt, 2) < kNarrowPitch)
		return "The map is too narrow across for a braid; use a bigger map.";
	if (teams > 1 && 2.0 * axes.length() / teams < kBankPerColony)
		return "Too many colonies for this map; use a bigger map or fewer colonies.";
	return "";
}

// The whole layout as a pure function of the request and the context's streams.
Layout design(const GenerationRequest &request, GenerationContext &context)
{
	const BraidedRiverOptions o(request);
	Layout L;
	L.t = {1 << request.wDec, 1 << request.hDec};
	const Torus &t = L.t;
	const int teams = std::max(1, request.nbTeams);
	L.failure = requestFault(request);
	if (!L.failure.empty())
		return L;
	// The river runs the long way; on a square map either way, drawn first so the choice is the
	// same however the rest of the design goes.
	L.axes = {t.w, t.h, t.w > t.h || (t.w == t.h && context.bounded(kThreadStream, 2) == 0)};
	const Axes &axes = L.axes;
	L.belt = std::lround(axes.breadth() * o.braidWidth / 100.0);
	const int terrace = (axes.breadth() - int(L.belt)) / 2;
	context.telemetry.measure("braided-river.frame.along-x", axes.alongX);
	context.telemetry.measure("braided-river.belt.width", L.belt);
	context.telemetry.measure("braided-river.terrace.width", terrace);

	planThreads(L, context, o.channels, o.barSize);
	std::vector<unsigned char> water(size_t(t.size()), 0);
	stampThreads(L, water);
	L.terrain.assign(size_t(t.size()), GRASS);
	for (int i = 0; i < t.size(); ++i)
		if (water[i])
			L.terrain[i] = WATER;
	layBeaches(L.terrain, t);
	planWall(L, context);
	labelLand(L);
	if (L.terraceNorth < 0 || L.terraceSouth < 0 || L.terraceNorth == L.terraceSouth)
	{
		L.failure = "The bluffs do not part the terraces; try another seed.";
		return L;
	}
	int bars = 0, barGrassLeast = INT_MAX;
	long long barGrass = 0;
	for (size_t c = 0; c < L.compTiles.size(); ++c)
		if (L.isBar(int(c)))
		{
			++bars;
			barGrass += L.compGrass[c];
			barGrassLeast = std::min(barGrassLeast, L.compGrass[c]);
		}
	context.telemetry.measure("braided-river.bars.count", bars);
	context.telemetry.measure("braided-river.bars.grass-mean",
							  bars ? double(barGrass) / bars : 0.0);
	context.telemetry.measure("braided-river.bars.grass-least", bars ? barGrassLeast : 0);
	findCrossings(L);
	if (!planHomes(L, context, teams))
		return L;
	if (!chooseRiffles(L, context, o.extraRiffles))
		return L;
	markRiffles(L);
	// The channels' cores and the riffles are checked on the sketch as well as on the finished
	// world, so a map that would fail is refused before a colony is placed.
	const std::vector<unsigned char> pureWater = pureTiles(L.terrain, t, WATER);
	L.failure = checkBraid(L, [&](int x, int y) { return pureWater[size_t(y) * t.w + x] != 0; });
	if (!L.failure.empty())
		return L;
	if (o.moraine)
		planMoraine(L, context);
	else
		L.hummocks.assign(size_t(t.size()), 0);
	int hummocks = 0;
	for (unsigned char h : L.hummocks)
		hummocks += h;
	context.telemetry.measure("braided-river.moraine.designed", hummocks);
	context.telemetry.measure("braided-river.homes.actual", L.homes.size());
	return L;
}

bool generate(Game &game, GenerationContext &context)
{
	context.stage = "braided river layout";
	const BraidedRiverOptions o(context.request);
	const Layout L = design(context.request, context);
	if (!L.failure.empty())
	{
		context.telemetry.fallback("braided-river.layout.failure", L.failure);
		context.detail = L.failure;
		return false;
	}
	Map &map = game.map;
	const Torus &t = L.t;
	const Axes &axes = L.axes;
	const int n = t.size(), teams = context.request.nbTeams;
	for (int k = 0; k < teams; ++k)
		game.addTeam();

	context.stage = "braided river terrain";
	writeUndermap(map, L.terrain);

	// The wall and the moraine go down before the colonies, so a settlement never lands on
	// stone; both are the map's structure and stay whatever the stone amount says.
	context.stage = "braided river bluffs";
	const DesignedStone bluffs = designedStone(map, t, L.wall);
	if (bluffs.gaps > 0)
	{
		context.detail = "The bluffs have a gap at (" + std::to_string(bluffs.firstGap % t.w) +
						 ", " + std::to_string(bluffs.firstGap / t.w) + ").";
		return false;
	}
	std::vector<unsigned char> protectedStone(size_t(n), 0);
	int wallTiles = 0, hummocks = 0;
	for (int i = 0; i < n; ++i)
		if (bluffs.stone[i])
		{
			map.setResource(i % t.w, i / t.w, STONE, 1);
			protectedStone[i] = 1;
			++wallTiles;
		}
	for (int i = 0; i < n; ++i)
		if (L.hummocks[i] && map.isGrass(i % t.w, i / t.w) && !map.isResource(i % t.w, i / t.w))
		{
			map.setResource(i % t.w, i / t.w, STONE, 1);
			protectedStone[i] = 1;
			++hummocks;
		}
	context.telemetry.measure("braided-river.wall.tiles", wallTiles);
	context.telemetry.measure("braided-river.moraine.placed", hummocks);

	context.stage = "braided river colonies";
	const auto homeMask = [&](int team)
	{
		// The terrace's grass in a 16x16 box round the site, the swarm's footprint nearest the
		// site within it (placeSettlement).
		const Home &home = L.homes[team];
		std::vector<unsigned char> mask(size_t(n), 0);
		for (int dv = -6; dv <= 9; ++dv)
			for (int du = -6; du <= 9; ++du)
			{
				const int i = axes.at(home.u + du, home.v + dv);
				mask[i] = L.comp[i] == home.terrace && map.isGrass(i % t.w, i / t.w) &&
						  !protectedStone[i];
			}
		return mask;
	};
	const auto anchor = [&](int team)
	{
		const int i = axes.at(L.homes[team].u, L.homes[team].v);
		return MapGeneratorPoint(i % t.w, i / t.w);
	};
	if (!settleColonies(game, context, "braided-river-starts", homeMask, anchor))
		return false;

	context.stage = "braided river resources";
	const Fertility::Field fertility = Fertility::forMap(map, false);
	const std::vector<unsigned char> reserved = swarmSurroundings(t, context);
	std::vector<unsigned char> grass(size_t(n), 0), notGrass(size_t(n), 0), swarms(size_t(n), 0);
	for (int i = 0; i < n; ++i)
	{
		grass[i] = map.isGrass(i % t.w, i / t.w);
		notGrass[i] = !grass[i];
	}
	for (int team = 0; team < teams; ++team)
		for (int dy = 0; dy < 4; ++dy)
			for (int dx = 0; dx < 4; ++dx)
				swarms[t.at(context.bootX[team] + dx, context.bootY[team] + dy)] = 1;
	const std::vector<int> grassDepth = stepsFrom(t, notGrass);
	const std::vector<unsigned char> townRoom = dilate(t, swarms, kTownRoom);
	const auto fertile = [&](int i) { return fertility.at(i % t.w, i / t.w); };
	// Ground nothing is planted on: the swarms' surroundings, the riffles' landings, the wall and
	// the moraine.
	const auto open = [&](int i)
	{
		return grass[i] && !reserved[i] && !L.riffleTiles[i] && !protectedStone[i] &&
			   clearGround(map, i % t.w, i / t.w);
	};

	// Every home's kit, beside its swarm along the bank, with stone behind.
	for (int team = 0; team < teams; ++team)
	{
		const Home &home = L.homes[team];
		const double axis = axes.heading(0, home.bank == 0 ? 1 : -1); // towards the river
		const KitFrame frame{context.bootX[team] + 2, context.bootY[team] + 2, axis};
		const Kit kit{frame.at(kKitAhead, -kKitBeside, 6),
					  frame.at(kKitAhead, kKitBeside, 6),
					  frame.at(-kKitStoneBehind, 0, 5),
					  kKitWheat,
					  kKitWood,
					  kKitStoneRadius};
		plantKit(map, t, context, kit, [&](int i) { return L.comp[i] == home.terrace && open(i); });
	}

	// The promised bar's wheat and wood: on its most fertile grass with room round it, the wood
	// a few tiles from the wheat so neither walls the other. Unscaled: this is the start.
	for (int team = 0; team < teams; ++team)
	{
		const Home &home = L.homes[team];
		int wheatAt = -1, woodAt = -1;
		for (int i = 0; i < n; ++i)
			if (L.comp[i] == home.bar && open(i) && grassDepth[i] >= 2 &&
				(wheatAt < 0 || fertile(i) > fertile(wheatAt)))
				wheatAt = i;
		if (wheatAt >= 0)
			placeResourceClump(map, context, MapGeneratorPoint(wheatAt % t.w, wheatAt / t.w), WHEAT,
							   kBarKitRadius);
		for (int i = 0; i < n; ++i)
			if (L.comp[i] == home.bar && open(i) && grassDepth[i] >= 2 &&
				(wheatAt < 0 || t.dist2(i % t.w, i / t.w, wheatAt % t.w, wheatAt / t.w) >=
									kBarKitSpacing * kBarKitSpacing) &&
				(woodAt < 0 || fertile(i) > fertile(woodAt)))
				woodAt = i;
		if (woodAt >= 0)
			placeResourceClump(map, context, MapGeneratorPoint(woodAt % t.w, woodAt / t.w), WOOD,
							   kBarKitRadius);
		context.telemetry.measure("braided-river.promised-bar.wheat", wheatAt >= 0, team);
		context.telemetry.measure("braided-river.promised-bar.wood", woodAt >= 0, team);
	}

	// Farmland on every bar, in patches along its shores; the terrace's bank strip gets a thin
	// scatter of the same. Patch noise of 8 tiles makes fields a few tiles across with gaps
	// between; split noise of 6 deals the two crops into separate patches rather than rings.
	const std::vector<int> patch =
		periodicNoise(t.w, t.h, 8, context.stream("braided-river-patch"));
	const std::vector<int> split =
		periodicNoise(t.w, t.h, 6, context.stream("braided-river-split"));
	const auto patchAt = [&](int i) { return float(patch[i]); };
	const auto splitAt = [&](int i) { return split[i]; };
	const auto furnish = [&](auto eligible, int wheatPercent, int woodPercent)
	{
		furnishGround(
			map, t, context, fertility, eligible, patchAt, splitAt,
			[&](int area)
			{
				return GroundAmounts{int(scaledCount(area * wheatPercent / 100, o.wheat)),
									 int(scaledCount(area * woodPercent / 100, o.wood)), 0, 0};
			},
			kResourceStream, kResourceStream);
	};
	int barsFarmed = 0, barsWithoutSite = 0;
	for (size_t c = 0; c < L.compTiles.size(); ++c)
	{
		if (!L.isBar(int(c)))
			continue;
		furnish([&](int i) { return L.comp[i] == int(c) && open(i); }, kBarWheatPercent,
				kBarWoodPercent);
		++barsFarmed;
		if (context.telemetry.enabled())
		{
			// Whether a 4x4 of clear grass is left on the bar, the room a forward inn needs.
			bool site = false;
			for (int i = 0; i < n && !site; ++i)
			{
				if (L.comp[i] != int(c))
					continue;
				site = true;
				for (int dy = 0; dy < 4 && site; ++dy)
					for (int dx = 0; dx < 4 && site; ++dx)
					{
						const int j = t.at(i % t.w + dx, i / t.w + dy);
						site = L.comp[j] == int(c) && grass[j] && !map.isResource(j % t.w, j / t.w);
					}
			}
			barsWithoutSite += !site;
		}
	}
	context.telemetry.measure("braided-river.bars.farmed", barsFarmed);
	context.telemetry.measure("braided-river.bars.without-site", barsWithoutSite);
	for (int terrace : {L.terraceNorth, L.terraceSouth})
		furnish([&](int i)
				{ return L.comp[i] == terrace && open(i) && !townRoom[i] && fertile(i) > 0; },
				kBankWheatPercent, kBankWoodPercent);

	// The dry terrace: finite woodlots and stone outcrops, off the town's room.
	std::vector<int> dry;
	for (int i = 0; i < n; ++i)
		if (L.isTerrace(L.comp[i]) && open(i) && !townRoom[i] && grassDepth[i] >= 2 &&
			fertile(i) == 0)
			dry.push_back(i);
	const int woodlots = int(scaledCount(int(dry.size()) / kDryTilesPerWoodlot, o.wood));
	const int outcrops = int(scaledCount(int(dry.size()) / kDryTilesPerOutcrop, o.stone));
	const int woodlotsPlaced =
		scatterClumps(context, t, dry, woodlots, kResourceStream, open,
					  [&](MapGeneratorPoint p) { placeResourceClump(map, context, p, WOOD, 1); });
	const int outcropsPlaced =
		scatterClumps(context, t, dry, outcrops, kResourceStream, open,
					  [&](MapGeneratorPoint p)
					  {
						  placeResourceClump(map, context, p, STONE,
											 1 + int(context.bounded(kResourceStream, 2)));
					  });
	context.telemetry.measure("braided-river.dry.tiles", dry.size());
	context.telemetry.measure("braided-river.dry.woodlots", woodlotsPlaced);
	context.telemetry.measure("braided-river.dry.outcrops", outcropsPlaced);

	// Fruit on the deep bars first, then the rest, one small grove each, the kinds dealt in turn.
	std::vector<int> deep, shallow;
	const double centre = axes.breadth() / 2.0;
	for (size_t c = 0; c < L.compTiles.size(); ++c)
		if (L.isBar(int(c)))
			(std::abs(L.compV[c] - centre) <= kDeepBarShare * L.belt / 2 ? deep : shallow)
				.push_back(int(c));
	context.shuffle(deep.begin(), deep.end(), kResourceStream);
	context.shuffle(shallow.begin(), shallow.end(), kResourceStream);
	const size_t deepBars = deep.size();
	deep.insert(deep.end(), shallow.begin(), shallow.end());
	const int groves = int(scaledCount(std::max(kFewestGroves, n / kTilesPerGrove), o.fruit));
	int grovesPlaced = 0, kind = int(context.bounded(kResourceStream, 3));
	for (size_t d = 0; d < deep.size() && grovesPlaced < groves; ++d)
	{
		int best = -1;
		for (int i = 0; i < n; ++i)
			if (L.comp[i] == deep[d] && open(i) && grassDepth[i] >= 2 &&
				(best < 0 || fertile(i) > fertile(best)))
				best = i;
		if (best < 0)
			continue;
		placeResourceClump(map, context, MapGeneratorPoint(best % t.w, best / t.w), CHERRY + kind,
						   1);
		kind = (kind + 1) % 3;
		++grovesPlaced;
	}
	context.telemetry.measure("braided-river.fruit.deep-bars", deepBars);
	context.telemetry.measure("braided-river.fruit.requested", groves);
	context.telemetry.measure("braided-river.fruit.placed", grovesPlaced);
	if (grovesPlaced < groves)
		context.telemetry.fallback("braided-river.fruit.short", "Fewer bars than groves asked");

	seedAlgae(map, context, t, "braided-river-algae", o.algae,
			  AlgaeBand::anyWater(kWaterTilesPerAlgae).thriving(kAlgaeBestShare));

	// Nothing on a riffle or its landings, whatever a later layer dropped there.
	clearDeposits(map, t, L.riffleTiles, &protectedStone);

	context.stage = "braided river routes";
	secureStartingCrops(game, context, t, 24, 32, 0, &protectedStone);
	int opened = connectColonies(map, teams, &protectedStone, context.detail);
	if (opened < 0)
		return false;
	if (reopenCrampedStarts(game, context, {o.wheat, o.wood, o.stone, o.algae, o.fruit}, 24, 32, 0,
							&protectedStone))
	{
		const int more = connectColonies(map, teams, &protectedStone, context.detail);
		if (more < 0)
			return false;
		opened += more;
	}
	context.telemetry.measure("braided-river.routes.opened", opened);
	return true;
}

// Checked on the finished world rather than trusted: the layout is designed again from the
// request, and the bluffs must stand complete, every channel keep its core outside its riffles,
// every riffle be open with land at both ends, every colony walk to colony 0, and every colony
// walk onto its promised bar and stand beside wheat and wood.
std::string validateWorld(const Game &game, const GenerationContext &context)
{
	GenerationContext replay(context.request);
	const Layout L = design(context.request, replay);
	const Map &map = game.map;
	if (const std::string mismatch = designMismatch(L, map, "braided river"); !mismatch.empty())
		return mismatch;
	const Torus &t = L.t;
	for (int i = 0; i < t.size(); ++i)
		if (L.wall[i] && map.getResource(i % t.w, i / t.w).type != STONE)
			return "The bluffs have a gap at (" + std::to_string(i % t.w) + ", " +
				   std::to_string(i / t.w) + ").";
	if (const std::string fault = checkBraid(L, [&](int x, int y) { return map.isWater(x, y); });
		!fault.empty())
		return fault;
	for (const Riffle &r : L.riffles)
		for (int side : {-1, 1})
			if (!fordLandingWalkable(map, t, r.ford, side))
				return "A riffle at (" + std::to_string(t.x(int(std::floor(r.ford.x)))) + ", " +
					   std::to_string(t.y(int(std::floor(r.ford.y)))) +
					   ") has no walkable land on one bank.";
	const int teams = context.request.nbTeams;
	const ColonyWalk walk = walkFromFirstColony(map, teams, "the terraces", "over the bars");
	if (!walk.error.empty())
		return walk.error;
	const std::vector<unsigned char> walkable = walkableTiles(map);
	for (int team = 0; team < teams; ++team)
	{
		const std::vector<int> reach = stepsFrom(t, tileMask(t, walk.workers[team]), walkable);
		bool onBar = false;
		for (int i = 0; i < t.size() && !onBar; ++i)
			onBar = reach[i] >= 0 && L.comp[i] == L.homes[team].bar;
		if (!onBar)
			return "Colony " + std::to_string(team) + " cannot walk onto its promised bar.";
		if (const std::string missing = cropsBesideReach(map, reach).missing(); !missing.empty())
			return "Colony " + std::to_string(team) + " " + missing;
	}
	return "";
}
} // namespace

BraidedRiverOptions::BraidedRiverOptions(const GenerationRequest &r)
	: braidWidth(r.option("braid-width")), channels(r.option("channels")),
	  barSize(r.option("bar-size")), extraRiffles(r.option("extra-riffles")),
	  moraine(r.option("moraine") != 0), wheat(r.option("wheat-amount")),
	  wood(r.option("wood-amount")), stone(r.option("stone-amount")),
	  algae(r.option("algae-amount")), fruit(r.option("fruit-amount"))
{
}

GeneratorDefinition braidedRiverDefinition()
{
	return {"braided-river",
			42,
			"Braided river",
			1,
			false,
			{// The belt of channels and bars as a share of the map's breadth; what is left either
			 // side is terrace. Below 30 the braid is two threads; above 60 a 128 map has no
			 // terrace left for a home.
			 {"braid-width", "Braid width", 30, 60, 5, 40, ControlGroup::Terrain},
			 // Channel threads across the belt, as asked when the lanes come out 12 to 26 tiles
			 // apart; a narrower belt drops threads and a wider one adds them, so the bars stay
			 // bar-sized (3 on a 128 map, 6 on 256, 7 on 512 at the default width).
			 {"channels", "Channels", 3, 9, 1, 6, ControlGroup::Terrain},
			 // A bar's length along the river, in tiles: half the threads' period, rounded to a
			 // whole number of periods round the map.
			 {"bar-size", "Bar size", 24, 64, 4, 40, ControlGroup::Terrain},
			 // How many of the crossable stretches left over after the spanning tree get a riffle
			 // too: at 0 the way across is a tree, at 100 every stretch long enough is open.
			 {"extra-riffles", "Extra riffles", 0, 100, 5, 25, ControlGroup::Layout},
			 // Off, the terrace edges are open bank; on, a broken line of stone hummocks four
			 // tiles up each bank, with a gap at every riffle landing.
			 GeneratorControl::toggle("moraine", "Moraine hummocks", true, ControlGroup::Layout),
			 // The bars' and bank strips' farmland, the dry terrace's woodlots and outcrops, the
			 // deep bars' fruit and the channels' algae. Every home's kit, every promised bar's
			 // wheat and wood, the bluffs and the moraine stay as they are.
			 GeneratorControl::percentage("wheat-amount", "Wheat amount"),
			 GeneratorControl::percentage("wood-amount", "Wood amount"),
			 GeneratorControl::percentage("stone-amount", "Stone amount"),
			 GeneratorControl::percentage("algae-amount", "Algae amount"),
			 GeneratorControl::percentage("fruit-amount", "Fruit amount")},
			generate,
			true,
			requestFault,
			validateWorld};
}
