// SPDX-License-Identifier: GPL-3.0-or-later
#include "RiverGenerator.h"
#include "GenerationWorld.h"
#include "GenerationContext.h"
#include "HeightFieldLooks.h"
#include "LatticeNoise.h"
#include "Morphology.h"
#include "Contact.h"
#include "Pipeline.h"
#include "Roads.h"
#include "Rock.h"
#include "Sketch.h"
#include "StartingPositions.h"
#include "Team.h"
#include "Terrain.h"
#include "TerrainCatalog.h"
#include "Topology.h"
#include "TerrainGroupTable.h"
#include <algorithm>
#include <climits>
#include <cmath>
#include <random>
#include <string>
#include <utility>
#include <vector>
using namespace MapGeneration;
// River (id 2): Leo Wandersleb's river map from January 2006 (nct fixed it on merge, giszmo patched
// gaps in the river bed a week later), on the shared height-field pipeline (see Terrain.cpp). The
// shape is one river bed that loops across the whole torus (makeRiver in HeightMap.cpp).
//
// River width is the bed's diameter as a percentage of the mean side: 35% is about 90 tiles on a
// 256x256 map. The water weight, not the width, sets how much of the map floods (35% at the
// defaults), so the two trade off. Renders at 256x256, seed 3: at width 20 the bed holds a fraction
// of the water and the rest floods the noise's hollows as ponds all over both banks; at 35 it is
// one river with a few ponds; at 65 the whole share fits in the bed, whose gentle slopes turn the
// resource bands into long parallel stripes of stone, wheat and wood along each bank, with dry,
// resource-less grass beyond. The default sits where the river reads as a river yet ponds still
// bring water, and so regrowing farmland, inland. The winding switch only removes the meander;
// everything else stays the same.
//
// Game rules: the river splits the map into banks that meet only across water, so colonies on
// opposite banks cannot fight until they swim, and the banks are the best farmland on the map
// (wheat and wood regrow only near water).
//
// RIVER 2.0 (revision 6): the same river, drawn with the engine's full terrain palette. The design
// brief: an aerial photograph of a big lowland river shows (1) dark deep water down the middle of
// the wide reaches with paler shallows along each bank; (2) a ribbon of dark, wet floodplain soil
// right behind the banks, the greenest ground in view, fading through meadow to dry grass as the
// ground rises away; (3) pale mud point bars on the inside of every bend and low rock bluffs where
// the outside of a bend cuts into the land; (4) far from the water, worn bare ground. A desert river
// (the Nile) shows the same with the contrast turned up: one green ribbon between pale baked desert.
// The height field, the river's course, the resource bands, the starts and their kits are exactly
// revision 5's for the same seed: generateHeightField runs unchanged and chooses the starts, then a
// terrain post-pass (shared/HeightFieldLooks.h) redraws the finished terrain before the swarms are
// placed:
// - Two sketches: the mechanic sketch holds each vertex's canonical rule-group type (GRASS, SAND,
//   WATER, DEEP_WATER, LOAM, SHALE, MUDFLAT, HARDPAN, SCREE, RIDGE_ROCK) and every rule below reads
//   it; looks are painted over it with composeLooks, which can never change a rule. Every home's
//   garden (kGardenRadius tiles round the swarm's middle, frayed) keeps GRASS mechanics. The banks
//   draw one look per rule group (revision 9): the desert's one grass look, dry grass, goes on only
//   after placeStarts, openStartsBuriedByResources and the home gates, gardens included, so the starts
//   and the starting-crop guarantee meet plain GRASS everywhere and no home reads as a round lawn.
// - Deep channel (`deep-channel`): plain water far enough from land is deep water (deepenWater):
//   at least kDeepReach tiles in a straight line, so the core's outline is round, more until the core
//   holds at most kDeepShare of the water (deepReachForShare), so at least three whole tiles of
//   shallows line every bank and the shallows widen with the river; only bodies of kDeepLeast vertices
//   or more. At the defaults the 90-tile bed keeps a core about half its width: a fixed five steps
//   swallowed 80% of the water and two thirds of the algae (the lowest sixth of the field's water,
//   right where the core goes). Deep water swims at 192/256 of the speed and holds no algae, so
//   crossing the river by swimming is slower than in revision 5 (a feel change). The algae it displaces
//   are seated again on the nearest free shallows (straight-line distance with a noise wobble), so the
//   map keeps its algae.
// - Floodplain levee: grass kLeveeNear to kLeveeFar vertices from water (one more where a noise
//   peaks) is loam (Fertile): buildable crop land that waters the ground round it at 48/256; never in a
//   garden or before a quarry face. Five deep (behind the sand band, really two or three) lost 4% of
//   the growth potential to the grass/loam edge row, nine gained 40%; seven gains about 5% and pays
//   for the farmland the bluffs take (see the docs' food table).
// - Point bars: on the inside of the river's bends (readBends: wetShare over a window a quarter of the
//   bed's diameter, the river alone, not the ponds, in the banks' top kBarPercentile percent and above
//   kBarShare; a fixed 17-tile window found no bends on a 90-tile river), the beach and the sand
//   behind it, and grass one row further up on the sharpest bends, are mudflat (Rough: slow,
//   unbuildable, holds nothing, and unlike sand it does not slow the crops behind it), never on a
//   vertex touching a deposit.
// - Cutbank bluffs (`bluffs`): on the outside of the bends (the bottom kBluffPercentile percent, at
//   most kBluffShare), knots of
//   rock (Ridges: impassable, towers shoot over) stand kBluffNear to kBluffFar tiles back from the
//   water, a crescent following the bank, kBluffSpacing apart and kBluffHomeClear from every home, one
//   per kSidePerBluff tiles of side at most (each takes bank farmland: one per 56 cost 8% of the food). Stone-amount sets how many carry a quarry face of stone on a
//   flank (tiles left out of the rock, a ring of plain grass before them), with scree skirting the
//   rest. A knot whose ring of ground falls apart (a pond behind it) is dropped, so the strip between
//   the rock and the water always walks through, and validateWorld proves no rock parts any ground.
//   Bluffs are planned from the height field rebuilt from the request (heightFieldSketch: its terrain
//   before the starting-crop top-up) and the starts alone, in generate and in
//   validateWorld alike, so the validator plans exactly the bluffs generate drew and checks every
//   quarry holds its stone.
// - Far ground: beyond kPaveReach steps of
//   any water (where the growth probe never finds water, and a mirrored probe almost never reaches a
//   crop) old desert sand and bare grass are pavement (Barren: buildable, holds nothing, inhibits
//   growth like sand), never on a vertex touching a deposit.
// - Banks (`banks`): Temperate as above (plain grass, sand and loam, heath pavement, outcrop bluffs) or
//   Desert, a Nile: the same mechanics, plus baked hardpan pans (Rough, which does not inhibit) on the
//   bare grass from kPanReach steps out, the grass dry, the pavement weathered sandstone and the bluffs
//   hoodoos; the loam levee is the green ribbon.
//
// REVISION 9 (one look per role): each rule group takes one look per banks variant. The lush and dry
// grass bands, the second levee, sand and pavement looks dealt out by noise, and the dune sand behind the
// beach are gone (looks only: every rule, deposit and start is revision 8's).
//
// REVISION 7 (home quarries): no stone from the height field. Its stone band lies just above the beach,
// which on a river is the whole shoreline, so on gentle banks it walled the shore in stone and the
// algae off (FEEDBACK 2026-11-08: "it doesn't really make sense, either from gameplay or from a just
// logical perspective"). The band is painted empty at its default width (riverField), so the crops lie
// where they did, and every stone is a quarry face in rock: each colony's home quarry
// (planHomeQuarries) and the bluffs' faces.
//
// REVISION 8 (home gates): the empty stone band opened the floor of dry hollows, a contour ring of
// grass inside the wheat and wood bands, and chooseBalancedStarts likes that open ground: on default
// rolls (seeds 1, 5 and 8 of ten, 26 of seeds 1-60) a colony started inside a closed ring of crops and
// could walk nowhere. Where a home's ground does not reach colony 0's, the cheapest walk out through the crops is
// cleared three tiles wide (openHomeGates), a gate in the field; every other map is
// unchanged. validateWorld now asks every colony to walk to colony 0 (walkFromFirstColony).
namespace
{
constexpr const char *kBanksStream = "river-banks";
constexpr const char *kLookStream = "river-looks";
constexpr const char *kGardenStream = "river-gardens";
constexpr const char *kBluffStream = "river-bluffs";
constexpr const char *kMudStream = "river-mud";
constexpr const char *kAlgaeStream = "river-algae";

// Deep water: plain water kDeepReach tiles from land in a straight line (so the core's outline is round,
// not a staircase of squares), in bodies of kDeepLeast vertices or more. The reach rises above kDeepReach
// until the core holds at most kDeepShare thousandths of the water, so a wide river keeps shallows in
// proportion and its displaced algae find room there. Five tiles in a straight line is never under four
// eight-connected steps, so three whole tiles of shallows line every bank.
constexpr int kDeepReach = 5, kDeepLeast = 16, kDeepShare = 500;
// The algae the deep core displaces are seated again on the free shallows nearest them, by straight-line
// distance plus up to kAlgaeWobble tiles of noise, so the reseated beds have ragged edges rather than the
// wedges a flood's visiting order drew.
constexpr double kAlgaeWobble = 4;
// Home gardens: grass (no levee, mud, scree or pavement) within kGardenRadius (frayed by kGardenFray)
// of every swarm's middle.
constexpr double kGardenRadius = 12, kGardenFray = 5;
// Levee: grass kLeveeNear to kLeveeFar vertices from water, one more where a noise passes kLeveeWide.
constexpr int kLeveeNear = 2, kLeveeFar = 7, kLeveeWide = 40000;
// Bends are read over a square of the river's own water a quarter of the bed's diameter in radius
// (kBendRadiusLeast to kBendRadiusMost): a meander's curve is as broad as its bed. Point bars stand
// where the share is in the river banks' top kBarPercentile percent and at least kBarShare thousandths,
// mud climbs one row further up the bank in the top kSharpPercentile percent; bluffs go where it is in
// the bottom kBluffPercentile percent and at most kBluffShare. So a straight river (500 all along)
// has neither. Water bodies with kMajorShare thousandths of the largest count as river.
constexpr int kBendRadiusLeast = 8, kBendRadiusMost = 24, kBarPercentile = 20, kSharpPercentile = 8;
constexpr int kBarShare = 530, kBluffPercentile = 20, kBluffShare = 470, kMajorShare = 250;
// Bluffs: knot tiles kBluffNear to kBluffFar steps from water within kBluffReach tiles of a cutbank
// vertex; centres kBluffSpacing apart, kBluffHomeClear from a swarm's middle, at least kBluffLeast
// tiles each, one per kSidePerBluff tiles of the map's width and height together (the river's length
// grows with the side, not the area); scree kTalusOuter deep where a noise peaks.
constexpr int kBluffNear = 4, kBluffFar = 9, kBluffSpacing = 30, kBluffHomeClear = 22, kBluffLeast = 24;
constexpr int kSidePerBluff = 80, kTalusOuter = 1;
constexpr double kBluffReach = 10.0;
// A bluff on a bank smaller than this many walkable tiles (an island in the river) is not drawn.
constexpr int kBluffLandLeast = 1500;
// Home quarries (revision 7): every colony's own outcrop, a disc of rock kHomeQuarryRadius tiles round
// with a face of kHomeQuarryStone stone on the side towards its swarm, about kHomeQuarryWalk walking
// steps from the swarm's middle (never nearer than kHomeQuarryClear in a straight line, past the home's
// garden, nor further than kHomeQuarryWalkMost), on dry ground kHomeQuarryDry or more steps from water so
// it takes no farmland. The same stone for every colony: quarryFault checks it.
constexpr double kHomeQuarryRadius = 4.0;
constexpr int kHomeQuarryStone = 10, kHomeQuarryWalk = 28, kHomeQuarryWalkMost = 48, kHomeQuarryDry = 6;
constexpr double kHomeQuarryClear = 19;
// Home gates (revision 8): a colony whose walkable ground does not reach colony 0's has the cheapest
// walk out cleared (a step onto a deposit costs kHomeGateCosts.clearable, open ground one; never across
// water), and every deposit within kHomeGateRadius of it, so the gate is three tiles wide.
constexpr StepCosts kHomeGateCosts{1, 3, -1, -1, -1};
constexpr int kHomeGateRadius = 1;
// Far ground: pavement from kPaveReach steps (frayed up to kPaveFray further) of any water, hardpan pans
// on the desert's bare grass on kPanShare percent of it. A crop up to 15 steps from water can have its
// mirrored probe land 30 steps out, so pavement from 22 steps still cost a low-water map (water weight
// 30) 8% of its food; from 28 it costs about 2%.
constexpr int kPaveReach = 28, kPaveFray = 3, kPanShare = 55;
// Pans only where the growth probe (15 tiles each way) never finds water, so they take no farmland that
// grows: from 16 steps.
constexpr int kPanReach = 16;
// Both edges are measured as straight lines from water, floored in eight-connected steps: pavement
// never within kPaveFloor steps, pans never within kPanFloor.
constexpr int kPaveFloor = 22, kPanFloor = 15;
// No hardpan within kPanHomeClear tiles of a swarm's middle: it is unbuildable, and a colony needs its
// 16 building sites within 24 steps, which reach 34 tiles out along a diagonal.
constexpr double kPanHomeClear = 34;

// The banks' variants: one look per rule group (every other group keeps its plain type: grass, sand,
// loam, deep water, mudflat, hardpan). paveShare is the percentage of the ground beyond kPaveReach that
// is paved: heath patches in the grass on the temperate banks, all of it in the desert.
struct Banks
{
	const char *name;
	TerrainType grass, pave, rock;
	int paveShare;
	bool desert;
};
constexpr Banks kBanks[2] = {{"temperate", GRASS, HEATH, OUTCROP, 40, false},
							 {"desert", DRY_GRASS, WEATHERED_SANDSTONE, HOODOOS, 100, true}};

// The river's bends: the share of its own water round every vertex, and the cuts that make a bank
// vertex a point bar, a sharp one, or a cutbank, from the banks' own distribution.
struct Bends
{
	std::vector<unsigned char> river;
	std::vector<int> share, fromRiver;
	int bar = INT_MAX, sharp = INT_MAX, bluff = INT_MIN;
};
Bends readBends(const Torus &t, const std::vector<unsigned char> &wet, const RiverOptions &o)
{
	Bends b;
	b.river = majorWater(t, wet, kMajorShare);
	const HeightFieldTiling tiling = heightFieldTiling(t.w, t.h, o.repeat);
	const int diameter = o.river_width * int(tiling.w + tiling.h) / 2 / 100;
	b.share = wetShare(t, b.river, std::clamp(diameter / 4, kBendRadiusLeast, kBendRadiusMost));
	b.fromRiver = stepsFrom(t, b.river);
	std::vector<int> bank;
	for (int i = 0; i < t.size(); ++i)
		if (b.fromRiver[i] == 1)
			bank.push_back(b.share[i]);
	if (bank.empty())
		return b;
	b.bar = std::max(kBarShare, percentile(bank, 100 - kBarPercentile));
	b.sharp = std::max(kBarShare, percentile(bank, 100 - kSharpPercentile));
	b.bluff = std::min(kBluffShare, percentile(bank, kBluffPercentile));
	return b;
}

HeightFieldBuilder riverBuilder(const RiverOptions &options, GenerationContext &context)
{
	return [&options, &context](HeightMap &hm, unsigned w, unsigned h, float smoothing)
	{
		context.telemetry.measure("river.bed.diameter", options.river_width * (w + h) / 2 / 100);
		context.telemetry.choice("river.bed.shape", options.winding ? "winding" : "straight");
		hm.makeRiver(options.river_width * (w + h) / 2 / 100, smoothing, options.winding);
	};
}

// The bluffs on the outer bends, planned from the height field's own terrain and the starts alone, so
// validateWorld can plan them again. Tile masks.
struct Bluffs
{
	std::vector<unsigned char> body;   // every knot tile: rock and quarry
	std::vector<unsigned char> rock;   // the tiles drawn as rock (rockVertices)
	std::vector<unsigned char> quarry; // quarry faces: stone on grass, left out of the rock
	std::vector<int> owner;            // the colony whose home quarry a face tile is, or -1 (a bluff's)
	int wanted = 0, placed = 0, faces = 0, dropped = 0;
	int homeQuarries = 0, homeWalkLeast = INT_MAX, homeWalkMost = 0;
	std::string failure;               // a colony left without a home quarry
};

// Whether the walkable tiles of the ring round a knot (within `width` of it) still form one piece once
// the knot is solid: a knot that does not would close the strip between the rock and the water.
// `inKnot` marks the knot; `scratch` is all zero on entry and is left so.
bool ringHolds(const Torus &t, const std::vector<int> &knot, const std::vector<unsigned char> &inKnot,
			   const std::vector<unsigned char> &walkable, int width, std::vector<unsigned char> &scratch)
{
	// scratch: 1 in the ring, 2 in the ring and reached.
	std::vector<int> ring;
	for (int i : knot)
		for (int dy = -width; dy <= width; ++dy)
			for (int dx = -width; dx <= width; ++dx)
			{
				const int j = t.at(t.remainderX(i) + dx, i / t.w + dy);
				if (!inKnot[j] && !scratch[j] && walkable[j])
				{
					scratch[j] = 1;
					ring.push_back(j);
				}
			}
	size_t reached = 0;
	if (!ring.empty())
	{
		std::vector<int> stack{ring.front()};
		scratch[ring.front()] = 2;
		while (!stack.empty())
		{
			const int i = stack.back();
			stack.pop_back();
			++reached;
			for (int dy = -1; dy <= 1; ++dy)
				for (int dx = -1; dx <= 1; ++dx)
				{
					const int j = t.at(t.remainderX(i) + dx, i / t.w + dy);
					if (scratch[j] == 1)
					{
						scratch[j] = 2;
						stack.push_back(j);
					}
				}
		}
	}
	for (int j : ring)
		scratch[j] = 0;
	return !ring.empty() && reached == ring.size();
}

// Every colony's home quarry (see kHomeQuarryRadius), planned into `b` before any bluff so the bluffs
// keep clear of them. Sets b.failure when a colony's ground leaves no room for one.
void planHomeQuarries(Bluffs &b, const Torus &t, const std::vector<unsigned char> &walkable,
					  const std::vector<int> &land, const std::vector<int> &fromWet, const std::vector<int> &bootX,
					  const std::vector<int> &bootY, int teams)
{
	const int n = t.size();
	const int r = int(std::ceil(kHomeQuarryRadius));
	std::vector<std::pair<int, int>> disc;
	for (int dy = -r; dy <= r; ++dy)
		for (int dx = -r; dx <= r; ++dx)
			if (dx * dx + dy * dy <= kHomeQuarryRadius * kHomeQuarryRadius)
				disc.push_back({dx, dy});
	std::vector<unsigned char> inKnot(size_t(n), 0), ring(size_t(n), 0);
	for (int k = 0; k < teams; ++k)
	{
		const int sx = t.x(bootX[size_t(k)] + 2), sy = t.y(bootY[size_t(k)] + 2);
		const int home = t.at(sx, sy);
		const std::vector<int> walk = stepsFrom(t, tileMask(t, {home}), walkable);
		// Nearer its own swarm than any other colony's, by a margin, so it is plainly this home's.
		const auto ownNearest = [&](int x, int y)
		{
			const int own = t.dist2(x, y, sx, sy);
			for (int j = 0; j < teams; ++j)
				if (j != k && t.dist2(x, y, t.x(bootX[size_t(j)] + 2), t.y(bootY[size_t(j)] + 2)) <
								  own + 16 * int(std::sqrt(double(own))) + 64)
					return false;
			return true;
		};
		std::vector<std::pair<int, int>> candidates;
		for (int i = 0; i < n; ++i)
			if (walk[i] >= int(kHomeQuarryClear) && walk[i] <= kHomeQuarryWalkMost && fromWet[i] >= kHomeQuarryDry + r)
				candidates.push_back({std::abs(walk[i] - kHomeQuarryWalk), i});
		std::sort(candidates.begin(), candidates.end());
		const std::vector<unsigned char> nearBody = dilate(t, b.body, 3);
		bool seated = false;
		for (const auto &candidate : candidates)
		{
			const int c = candidate.second;
			const int cx = t.remainderX(c), cy = c / t.w;
			if (!ownNearest(cx, cy))
				continue;
			std::vector<int> knot;
			bool fits = true;
			for (const auto &d : disc)
			{
				const int i = t.at(cx + d.first, cy + d.second);
				if (!walkable[i] || land[i] != land[home] || fromWet[i] < kHomeQuarryDry || nearBody[i] ||
					t.dist2(t.remainderX(i), i / t.w, sx, sy) < kHomeQuarryClear * kHomeQuarryClear)
				{
					fits = false;
					break;
				}
				knot.push_back(i);
			}
			if (!fits)
				continue;
			for (int i : knot)
				inKnot[i] = 1;
			const bool holds = ringHolds(t, knot, inKnot, walkable, 3, ring);
			if (!holds)
			{
				for (int i : knot)
					inKnot[i] = 0;
				continue;
			}
			// The face: the knot's edge tiles nearest the swarm.
			std::vector<std::pair<int, int>> edge;
			for (int i : knot)
			{
				bool exposed = false;
				for (int dy = -1; dy <= 1 && !exposed; ++dy)
					for (int dx = -1; dx <= 1 && !exposed; ++dx)
						exposed = !inKnot[t.at(t.remainderX(i) + dx, i / t.w + dy)];
				if (exposed)
					edge.push_back({t.dist2(t.remainderX(i), i / t.w, sx, sy), i});
			}
			std::sort(edge.begin(), edge.end());
			int faceWalk = INT_MAX;
			for (size_t e = 0; e < edge.size() && e < size_t(kHomeQuarryStone); ++e)
			{
				b.quarry[edge[e].second] = 1;
				b.owner[edge[e].second] = k;
				faceWalk = std::min(faceWalk, walk[edge[e].second]);
			}
			for (int i : knot)
			{
				b.body[i] = 1;
				inKnot[i] = 0;
			}
			++b.homeQuarries;
			b.homeWalkLeast = std::min(b.homeWalkLeast, faceWalk);
			b.homeWalkMost = std::max(b.homeWalkMost, faceWalk);
			seated = true;
			break;
		}
		if (!seated && b.failure.empty())
			b.failure = "Colony " + std::to_string(k) + " has no room for its home quarry.";
	}
}

// The rock: every colony's home quarry, then the bluffs on the outer bends.
Bluffs planBluffs(const TerrainSketch &stamp, const Torus &t, const std::vector<int> &bootX,
				  const std::vector<int> &bootY, int teams, const RiverOptions &o, GenerationContext &context)
{
	const int n = t.size();
	Bluffs b;
	b.body.assign(size_t(n), 0);
	b.rock.assign(size_t(n), 0);
	b.quarry.assign(size_t(n), 0);
	b.owner.assign(size_t(n), -1);
	const std::vector<unsigned char> wet = wetVertices(stamp);
	const std::vector<int> fromWet = stepsFromWet(t, stamp);
	if (fromWet.empty() || fromWet[0] < 0)
		return b;
	const std::vector<unsigned char> walkable = walkableTiles(stamp, t);
	// The walkable ground's pieces, so no bluff goes on an island in the river.
	const std::vector<int> land = connectedRegions(walkable, t.w, t.h, true, GridNeighbors::Eight);
	planHomeQuarries(b, t, walkable, land, fromWet, bootX, bootY, teams);
	const auto finish = [&]()
	{
		for (int i = 0; i < n; ++i)
			b.rock[i] = b.body[i] && !b.quarry[i];
		return b;
	};
	if (!o.bluffs)
		return finish();
	const Bends bends = readBends(t, wet, o);
	const std::vector<unsigned char> &river = bends.river;
	const std::vector<int> &share = bends.share;
	std::vector<int> landSize;
	for (int l : land)
		if (l >= 0)
		{
			if (int(landSize.size()) <= l)
				landSize.resize(size_t(l) + 1, 0);
			++landSize[size_t(l)];
		}
	// Cutbank vertices: beach vertices beside the river where its water bites into the land.
	std::vector<std::pair<int, int>> candidates;
	for (int i = 0; i < n; ++i)
	{
		if (fromWet[i] != 1 || share[i] > bends.bluff)
			continue;
		bool besideRiver = false;
		for (int dy = -1; dy <= 1 && !besideRiver; ++dy)
			for (int dx = -1; dx <= 1 && !besideRiver; ++dx)
				besideRiver = river[t.at(t.remainderX(i) + dx, i / t.w + dy)];
		if (besideRiver)
			candidates.push_back({share[i], i});
	}
	std::sort(candidates.begin(), candidates.end());
	b.wanted = std::max(1, (t.w + t.h) / kSidePerBluff);
	// How many faces: about one per bluff at the default stone amount, a second flank from 150%.
	const int flanks = o.stone <= 0 ? 0 : o.stone >= 150 ? 2 : 1;
	// The chance a bluff carries a face: 60% at the default stone amount, every bluff from about 170%.
	const int faceChance = std::min(100, 60 * std::max(0, o.stone) / 100);
	std::vector<int> centres;
	// Scratch masks, all zero between knots.
	std::vector<unsigned char> lens(size_t(n), 0), inKnot(size_t(n), 0), ring(size_t(n), 0);
	for (const auto &candidate : candidates)
	{
		if (b.placed >= b.wanted)
			break;
		const int c = candidate.second;
		const int cx = t.remainderX(c), cy = c / t.w;
		bool clear = true;
		for (int k : centres)
			clear = clear && t.dist2(cx, cy, t.remainderX(k), k / t.w) >= kBluffSpacing * kBluffSpacing;
		for (int k = 0; k < teams && clear; ++k)
			clear = t.dist2(cx, cy, t.x(bootX[size_t(k)] + 2), t.y(bootY[size_t(k)] + 2)) >=
					kBluffHomeClear * kBluffHomeClear;
		if (!clear)
			continue;
		// Each knot draws its reach and depth from its own stream, in the order knots are tried.
		const double reach = kBluffReach - 1 + context.bounded(kBluffStream, 3);
		const int deepest = kBluffFar - 1 + int(context.bounded(kBluffStream, 2));
		const int r = int(std::ceil(reach));
		// The lens: walkable tiles kBluffNear to `deepest` steps from water within `reach` of the cutbank
		// vertex, kept only where they lie in a 2x2 block of it, so every tile has a rock vertex.
		std::vector<int> lensTiles;
		for (int dy = -r; dy <= r; ++dy)
			for (int dx = -r; dx <= r; ++dx)
			{
				const int i = t.at(cx + dx, cy + dy);
				if (dx * dx + dy * dy <= reach * reach && fromWet[i] >= kBluffNear && fromWet[i] <= deepest && walkable[i])
				{
					lens[i] = 1;
					lensTiles.push_back(i);
				}
			}
		std::vector<int> knot;
		for (int i : lensTiles)
		{
			const int x = t.remainderX(i), y = i / t.w;
			bool kept = false;
			for (int oy = -1; oy <= 0 && !kept; ++oy)
				for (int ox = -1; ox <= 0 && !kept; ++ox)
					kept = lens[t.at(x + ox, y + oy)] && lens[t.at(x + ox + 1, y + oy)] &&
						   lens[t.at(x + ox, y + oy + 1)] && lens[t.at(x + ox + 1, y + oy + 1)];
			if (kept)
				knot.push_back(i);
		}
		for (int i : lensTiles)
			lens[i] = 0;
		int landPiece = -1;
		bool oneLand = true;
		for (int i : knot)
		{
			if (landPiece < 0)
				landPiece = land[i];
			oneLand = oneLand && land[i] == landPiece;
		}
		if (int(knot.size()) < kBluffLeast || !oneLand || landPiece < 0 ||
			landSize[size_t(landPiece)] < kBluffLandLeast)
		{
			++b.dropped;
			continue;
		}
		bool touchesBody = false;
		for (int i : knot)
			for (int dy = -3; dy <= 3 && !touchesBody; ++dy)
				for (int dx = -3; dx <= 3 && !touchesBody; ++dx)
					touchesBody = b.body[t.at(t.remainderX(i) + dx, i / t.w + dy)];
		for (int i : knot)
			inKnot[i] = 1;
		if (touchesBody || !ringHolds(t, knot, inKnot, walkable, 3, ring))
		{
			for (int i : knot)
				inKnot[i] = 0;
			++b.dropped;
			continue;
		}
		centres.push_back(c);
		++b.placed;
		for (int i : knot)
			b.body[i] = 1;
		// The flanks: the knot's two ends along the bank (across the gradient of the distance from
		// water at the cutbank vertex).
		const double gx = fromWet[t.at(cx + 2, cy)] - fromWet[t.at(cx - 2, cy)];
		const double gy = fromWet[t.at(cx, cy + 2)] - fromWet[t.at(cx, cy - 2)];
		const double norm = std::sqrt(gx * gx + gy * gy);
		const double tx = norm > 0 ? -gy / norm : 1, ty = norm > 0 ? gx / norm : 0;
		int lo = -1, hi = -1;
		double loAlong = 1e9, hiAlong = -1e9;
		for (int i : knot)
		{
			const double along = t.offsetX(cx, t.remainderX(i)) * tx + t.offsetY(cy, i / t.w) * ty;
			if (along < loAlong || (along == loAlong && i < lo))
				loAlong = along, lo = i;
			if (along > hiAlong || (along == hiAlong && i < hi))
				hiAlong = along, hi = i;
		}
		const bool faced = flanks > 0 && int(context.bounded(kBluffStream, 100)) < faceChance;
		if (faced)
		{
			// quarryNear's face, worked out in the knot's own neighbourhood: knot tiles within two tiles
			// (Chebyshev) of the end and within two steps of a tile outside the knot.
			int laid = 0;
			for (int end : {lo, hi})
			{
				if (laid >= flanks)
					break;
				const int ex = t.remainderX(end), ey = end / t.w;
				for (int dy = -2; dy <= 2; ++dy)
					for (int dx = -2; dx <= 2; ++dx)
					{
						const int i = t.at(ex + dx, ey + dy);
						if (!inKnot[i])
							continue;
						bool exposed = false;
						for (int qy = -2; qy <= 2 && !exposed; ++qy)
							for (int qx = -2; qx <= 2 && !exposed; ++qx)
								exposed = !inKnot[t.at(ex + dx + qx, ey + dy + qy)];
						if (exposed)
							b.quarry[i] = 1;
					}
				++laid;
				++b.faces;
			}
		}
		for (int i : knot)
			inKnot[i] = 0;
	}
	return finish();
}

// The rock from the request and the colonies' starts alone, as generate and validateWorld both plan
// it: the height field's own terrain rebuilt in a fresh context (heightFieldSketch), before the
// starting-crop top-up and before any start. Both read the same input, so the validator replans
// exactly the rock generate drew.
// River's height field: the shared one with its stone band left empty. The band lies just above the
// beach, so on gentle banks it walled the whole shore in stone, between the land and the algae; River's
// stone is all quarry faces in rock instead (planBluffs). The band keeps its default width, whatever
// the stone amount, so the wheat and wood lie where revision 6's did and the strip above the beach is
// open floodplain the crops regrow into.
HeightFieldOptions riverField(const GenerationRequest &request)
{
	HeightFieldOptions terrain = HeightFieldOptions::fromRequest(request, false);
	terrain.stone = 100;
	terrain.hilltopStone = false;
	terrain.paintStone = false;
	return terrain;
}

Bluffs replanBluffs(const GenerationRequest &request, const std::vector<int> &bootX, const std::vector<int> &bootY,
					int teams)
{
	const RiverOptions options(request);
	GenerationContext replay(request);
	const HeightFieldOptions terrain = riverField(request);
	const TerrainSketch stamp = heightFieldSketch(request, replay, terrain, riverBuilder(options, replay));
	const Torus t(1 << request.wDec, 1 << request.hDec);
	// The banks' draw comes before the bluffs' in generate; the streams are separate, so it need not
	// be repeated here.
	return planBluffs(stamp, t, bootX, bootY, teams, options, replay);
}

struct LayerCounts
{
	int deep = 0, levee = 0, mud = 0, pave = 0, pan = 0, scree = 0, rock = 0, garden = 0, barCut = 0, bluffCut = 0, deepReach = 0;
};

// The post-pass: the mechanic layers over the height field's terrain, then the looks. The grass look
// is laid after the starts (layGrassLook).
void redraw(GenerationWorld &game, GenerationContext &context, const RiverOptions &o, const Banks &banks,
			const Bluffs &bluffs, LayerCounts &counts)
{
	GenerationMap &map = game.map;
	const Torus t(map);
	const int n = t.size(), teams = context.request.nbTeams;
	TerrainSketch mech = mapSketch(map);
	std::vector<int> bootX(context.bootX.begin(), context.bootX.begin() + teams),
		bootY(context.bootY.begin(), context.bootY.begin() + teams);
	const std::vector<unsigned char> garden =
		homeGardens(t, bootX, bootY, teams, kGardenRadius, periodicNoise(t.w, t.h, 9, context.stream(kGardenStream)),
					kGardenFray);
	for (unsigned char g : garden)
		counts.garden += g;
	// Vertices touching a tile with a deposit: pavement and pans never go there.
	std::vector<unsigned char> holding(size_t(n), 0);
	for (int i = 0; i < n; ++i)
		holding[i] = map.isResource(t.remainderX(i), i / t.w);
	holding = tileCorners(t, holding);
	const std::vector<int> fromStamp = stepsFromWet(t, mech);
	// The far ground's edges follow the straight-line distance from water, so they round off rather than
	// draw squares round the ponds, but never come nearer than the step floors the growth rules need.
	const std::vector<int> round = distanceFromWet(t, mech);
	std::vector<int> farField(static_cast<size_t>(n), 0), panField(static_cast<size_t>(n), 0);
	for (int i = 0; i < n; ++i)
	{
		farField[i] = fromStamp[i] >= kPaveFloor ? round[i] : fromStamp[i];
		panField[i] = fromStamp[i] >= kPanFloor ? round[i] : fromStamp[i];
	}

	if (o.deepChannel)
	{
		counts.deepReach = deepReachForShare(mech, t, kDeepReach, kDeepShare);
		counts.deep = deepenWater(mech, t, counts.deepReach, kDeepLeast, DEEP_WATER);
	}

	// Bluffs: plain grass before every quarry face (never the beach), rock, then scree.
	const std::vector<unsigned char> halo = tileCorners(t, dilateRound(t, bluffs.quarry, 2.5));
	for (int i = 0; i < n; ++i)
		if (halo[i] && fromStamp[i] >= 2 && terrainGroup(TerrainType(mech[i])) != TerrainGroup::Water)
			mech[i] = GRASS;
	std::mt19937 &looks = context.stream(kLookStream);
	const std::vector<int> fray = periodicNoise(t.w, t.h, 7, looks);
	if (std::find(bluffs.body.begin(), bluffs.body.end(), (unsigned char)1) != bluffs.body.end())
	{
		std::vector<unsigned char> eligible(size_t(n), 0);
		const std::vector<unsigned char> nearFace = dilate(t, bluffs.quarry, 2);
		for (int i = 0; i < n; ++i)
			eligible[i] = !bluffs.body[i] && !nearFace[i] && !garden[i];
		const std::vector<unsigned char> talus = talusApron(t, bluffs.rock, eligible, fray, 0, kTalusOuter, 30000);
		for (int i = 0; i < n; ++i)
			if (talus[i] && fromStamp[i] >= 2 && !halo[i] && !holding[i] && (mech[i] == GRASS || mech[i] == SAND))
			{
				mech[i] = SCREE;
				++counts.scree;
			}
		const std::vector<unsigned char> rock = rockVertices(t, bluffs.rock);
		for (int i = 0; i < n; ++i)
			if (rock[i])
			{
				mech[i] = RIDGE_ROCK;
				++counts.rock;
			}
	}

	// Point bars on the inside of the river's bends.
	{
		const Bends bends = readBends(t, wetVertices(mech), o);
		const std::vector<int> &share = bends.share, &fromRiver = bends.fromRiver;
		std::vector<unsigned char> bar(size_t(n), 0), sharp(size_t(n), 0);
		for (int i = 0; i < n; ++i)
			if (fromRiver[i] == 1)
			{
				bar[i] = share[i] >= bends.bar;
				sharp[i] = share[i] >= bends.sharp;
			}
		counts.barCut = bends.bar;
		counts.bluffCut = bends.bluff;
		bar = dilateRound(t, bar, 2.5);
		sharp = dilateRound(t, sharp, 2.0);
		const std::vector<int> mudFray = periodicNoise(t.w, t.h, 6, context.stream(kMudStream));
		for (int i = 0; i < n; ++i)
		{
			// Mud holds nothing, so it never goes on a vertex touching a deposit (unguarded, the bars took
			// dozens of stone off the banks).
			if (garden[i] || halo[i] || holding[i] || !bar[i] || fromRiver[i] < 1 || fromRiver[i] > 3)
				continue;
			const bool sandBar = mech[i] == SAND;
			const bool upBank = sharp[i] && mech[i] == GRASS && fromRiver[i] == 2 && fromStamp[i] == 2 &&
								mudFray[i] > 26000;
			if (sandBar || upBank)
			{
				mech[i] = MUDFLAT;
				++counts.mud;
			}
		}
	}

	// The levee, everywhere along the water but the gardens and the quarries' approaches.
	{
		std::vector<unsigned char> eligible(size_t(n), 0);
		for (int i = 0; i < n; ++i)
			eligible[i] = !garden[i] && !halo[i];
		const std::vector<int> widen = periodicNoise(t.w, t.h, 9, looks);
		counts.levee = layLevee(mech, fromStamp, eligible, kLeveeNear, kLeveeFar, widen, kLeveeWide);
	}

	// Far ground: pavement beyond the water's reach, and on the desert banks hardpan pans on the bare
	// grass between the ribbon and the pavement.
	{
		std::vector<unsigned char> eligible(size_t(n), 0);
		for (int i = 0; i < n; ++i)
			eligible[i] = !garden[i] && !halo[i] && !holding[i];
		if (banks.paveShare < 100)
		{
			// Patches of the far ground, not all of it: heath in the grass.
			std::vector<unsigned char> farGround(size_t(n), 0);
			for (int i = 0; i < n; ++i)
				farGround[i] = eligible[i] && farField[i] >= kPaveReach;
			const std::vector<unsigned char> patches =
				noisyShare(farGround,fractalNoise(t.w, t.h, 20, 3, looks), banks.paveShare);
			for (int i = 0; i < n; ++i)
				eligible[i] = eligible[i] && patches[i];
		}
		counts.pave = paveBeyondWater(mech, farField, eligible, fray, kPaveReach, kPaveFray, SAND, SHALE) +
					  paveBeyondWater(mech, farField, eligible, fray, kPaveReach, kPaveFray, GRASS, SHALE);
		if (banks.desert)
		{
			std::vector<unsigned char> bare(size_t(n), 0);
			const std::vector<unsigned char> homes = homeGardens(t, bootX, bootY, teams, kPanHomeClear, {}, 0);
			for (int i = 0; i < n; ++i)
				bare[i] = eligible[i] && !homes[i] && mech[i] == GRASS && panField[i] >= kPanReach;
			const std::vector<int> panNoise = fractalNoise(t.w, t.h, 24, 3, looks);
			const std::vector<unsigned char> pan = noisyShare(bare, panNoise, kPanShare);
			for (int i = 0; i < n; ++i)
				if (pan[i])
				{
					mech[i] = HARDPAN;
					++counts.pan;
				}
		}
	}

	// The looks: one per rule group; the grass look waits for the starts (layGrassLook).
	TerrainSketch paint(size_t(n), 0);
	for (int i = 0; i < n; ++i)
		if (mech[i] == SHALE)
			paint[i] = banks.pave;
		else if (mech[i] == RIDGE_ROCK)
			paint[i] = banks.rock;
	const DisplacedResources lost = writeLayeredTerrain(map, mech, paint);

	// The algae deep water displaced, seated again on the nearest free shallows (the river's own first,
	// then the nearest ponds'), so the map keeps its algae: nearest by straight-line distance with a
	// wobble of noise, so a reseated bed's edge is ragged (a flood's visiting order drew wedges).
	int reseated = 0;
	if (!lost.algaeTiles.empty())
	{
		std::vector<unsigned char> source(size_t(n), 0);
		for (int i : lost.algaeTiles)
			source[i] = 1;
		const std::vector<std::int64_t> d2 = distanceSquaredTo(t, source);
		const std::vector<int> wobble = periodicNoise(t.w, t.h, 7, context.stream(kAlgaeStream));
		std::vector<std::pair<double, int>> shallows;
		for (int i = 0; i < n; ++i)
		{
			const int x = t.remainderX(i), y = i / t.w;
			if (d2[i] >= 0 && !map.isResource(x, y) && map.terrainSupportsResourceAtByIndex(x, y, ALGA))
				shallows.push_back({std::sqrt(double(d2[i])) + kAlgaeWobble * wobble[i] / 65536.0, i});
		}
		std::sort(shallows.begin(), shallows.end());
		for (const auto &shallow : shallows)
		{
			if (reseated >= int(lost.algaeTiles.size()))
				break;
			map.setResourceByIndex(t.remainderX(shallow.second), shallow.second / t.w, ALGA, 1);
			++reseated;
		}
	}

	// The quarry faces' stone goes down before the swarms, so nothing lands on it; the ground before
	// each face is cleared.
	int quarryStone = 0;
	const std::vector<unsigned char> approach = quarryApproach(t, bluffs.quarry);
	for (int i = 0; i < n; ++i)
	{
		const int x = t.remainderX(i), y = i / t.w;
		if ((approach[i] || bluffs.body[i]) && map.isResource(x, y))
			map.replaceResource(x, y, Resource{});
		if (bluffs.quarry[i] && map.terrainSupportsResourceAtByIndex(x, y, STONE))
		{
			map.setResourceByIndex(x, y, STONE, 1);
			++quarryStone;
		}
	}

	context.telemetry.choice("river.banks", banks.name);
	context.telemetry.choice("river.deep-channel", o.deepChannel ? "on" : "off");
	context.telemetry.choice("river.bluffs", o.bluffs ? "on" : "off");
	context.telemetry.measure("river.deep.vertices", counts.deep);
	context.telemetry.measure("river.deep.reach", counts.deepReach);
	context.telemetry.measure("river.levee.vertices", counts.levee);
	context.telemetry.measure("river.mudflat.vertices", counts.mud);
	context.telemetry.measure("river.pavement.vertices", counts.pave);
	context.telemetry.measure("river.hardpan.vertices", counts.pan);
	context.telemetry.measure("river.scree.vertices", counts.scree);
	context.telemetry.measure("river.rock.vertices", counts.rock);
	context.telemetry.measure("river.garden.vertices", counts.garden);
	context.telemetry.measure("river.bends.bar-share", counts.barCut);
	context.telemetry.measure("river.bends.bluff-share", counts.bluffCut);
	context.telemetry.measure("river.bluffs.wanted", bluffs.wanted);
	context.telemetry.measure("river.bluffs.placed", bluffs.placed);
	context.telemetry.measure("river.bluffs.dropped", bluffs.dropped);
	context.telemetry.measure("river.bluffs.faces", bluffs.faces);
	context.telemetry.measure("river.quarry.stone", quarryStone);
	context.telemetry.measure("river.quarry.home", bluffs.homeQuarries);
	context.telemetry.measure("river.quarry.home-stone", bluffs.homeQuarries * kHomeQuarryStone);
	context.telemetry.measure("river.quarry.home-walk-least", bluffs.homeQuarries ? bluffs.homeWalkLeast : 0);
	context.telemetry.measure("river.quarry.home-walk-most", bluffs.homeWalkMost);
	context.telemetry.measure("river.quarry.tiles",
							  int(std::count(bluffs.quarry.begin(), bluffs.quarry.end(), (unsigned char)1)));
	context.telemetry.measure("river.displaced.wheat", lost.wheat);
	context.telemetry.measure("river.displaced.wood", lost.wood);
	context.telemetry.measure("river.displaced.stone", lost.stone);
	context.telemetry.measure("river.displaced.algae", lost.algae);
	context.telemetry.measure("river.displaced.fruit", lost.fruit);
	context.telemetry.measure("river.algae.reseated", reseated);
}

// Gates out of the fields that ring a home: while some colony cannot walk to colony 0 (the validator's
// walkFromFirstColony), the cheapest four-connected walk from colony 0's ground to that colony's workers
// under kHomeGateCosts (a deposit costs three steps; water, rock, stone and `keep` cannot be crossed)
// has every deposit on dry ground within kHomeGateRadius of it cleared (algae beside it stay). Returns how
// many gates were opened; stops when every colony walks out or one has no walk at all, which the
// validator then refuses. It draws from no stream, and on a map where every colony already walks out it
// changes nothing.
int openHomeGates(GenerationMap &map, const Torus &t, int teams, const std::vector<unsigned char> &keep)
{
	std::vector<unsigned char> spare = keep;
	for (int i = 0; i < t.size(); ++i)
		spare.at(size_t(i)) = spare.at(size_t(i)) || !map.terrainPropertiesAt(t.remainderX(i), i / t.w).walkable;
	int opened = 0;
	while (opened < teams)
	{
		const ColonyWalk walk = walkFromFirstColony(map, teams, "the banks", "");
		if (walk.workers.empty() || walk.workers.at(0).empty())
			break;
		const int cut = firstColonyCutOff(walk.steps, walk.workers);
		if (cut < 0)
			break;
		std::vector<int> reached;
		for (int i = 0; i < t.size(); ++i)
			if (walk.steps.at(size_t(i)) >= 0)
				reached.push_back(i);
		const std::vector<int> route =
			cheapestWalk(t, GridNeighbors::Cardinal, reached, tileMask(t, walk.workers.at(size_t(cut))),
						 [&](int, int to, int, int)
						 {
							 return keep.at(size_t(to)) ? -1
														: stepCost(map, t.remainderX(to), to / t.w, kHomeGateCosts);
						 });
		if (route.empty())
			break;
		clearRoute(map, t, route, kHomeGateRadius, &spare);
		++opened;
	}
	return opened;
}

// The banks' one grass look over every plain grass vertex, the homes' gardens included.
void layGrassLook(GenerationMap &map, TerrainType look)
{
	if (look == GRASS || terrainGroup(look) != TerrainGroup::Grass)
		return;
	TerrainSketch sketch = mapSketch(map);
	for (size_t i = 0; i < sketch.size(); ++i)
		if (sketch[i] == GRASS)
			sketch[i] = look;
	writeVertices(map, sketch);
}

const Banks &chooseBanks(const RiverOptions &o, GenerationContext &context)
{
	const int index = o.banks > 0 ? std::clamp(o.banks, 1, 2) - 1 : int(context.bounded(kBanksStream, 2));
	return kBanks[index];
}

bool generate(GenerationWorld &game, GenerationContext &context)
{
	const RiverOptions options(context.request);
	const HeightFieldOptions terrain = riverField(context.request);
	if (!generateHeightField(game, context, terrain, riverBuilder(options, context)))
		return false;
	// 2.0: the post-pass, on the finished field with its resources and chosen starts.
	context.stage = "river 2.0 terrain";
	const Torus t(game.map);
	const int teams = context.request.nbTeams;
	std::vector<int> bootX(static_cast<size_t>(teams), 0), bootY(static_cast<size_t>(teams), 0);
	for (int k = 0; k < teams; ++k)
	{
		bootX[size_t(k)] = t.x(context.bootX[k]);
		bootY[size_t(k)] = t.y(context.bootY[k]);
	}
	const Banks &banks = chooseBanks(options, context);
	const Bluffs bluffs = replanBluffs(context.request, bootX, bootY, teams);
	if (!bluffs.failure.empty())
	{
		context.detail = bluffs.failure;
		return false;
	}
	LayerCounts counts;
	redraw(game, context, options, banks, bluffs, counts);
	context.stage = "starts";
	if (!placeStarts(game, context))
		return false;
	// The resource bands are painted from map-wide noise with no awareness of where any colony
	// starts, so they can wall one into a pocket with nowhere to build — a raised amount widens
	// them, but the defaults manage it too. placeStarts has already carved out the swarm's own
	// rectangle by now, so nothing needs to be kept clear for it. The quarry faces are walls the
	// relief never clears.
	openStartsBuriedByResources(game, context, 24, 32, 0, &bluffs.quarry);
	// A gate out of every field that rings a home (revision 8). The empty stone band leaves the grass
	// of a dry hollow open, and the wheat and wood bands round it close into a ring: the best ground on
	// the bank to start on, and sealed (seed 1's colony 2). The cheapest walk out through the crops is
	// cleared three tiles wide, so a column passes and the first regrowth does not shut it; the quarry
	// faces are never touched (openHomeGates). A gate takes a few tiles of a ring whose rest stays at
	// the home's edge, so the starting crops are not topped up again; validateWorld still holds every
	// colony to its wheat and wood. Where every colony already walks out, nothing changes.
	context.telemetry.measure("river.home_gates.opened", openHomeGates(game.map, t, teams, bluffs.quarry));
	// Last, the grass look: until now every grass vertex was plain GRASS, which is all the
	// starting-crop guarantee will plant on (a top-up that found plain grass only beside the swarm
	// filled a desert home's building room with wheat). A look inside the grass group changes no rule
	// and no deposit, so it goes on now, over all the plain grass the starts left, gardens included.
	layGrassLook(game.map, banks.grass);
	return true;
}

// Nothing here designs an economy: the height field decides where land is and placeStarts picks
// the best spots it left. So the only promise this landscape can make is the floor every colony
// needs — wheat and wood it can walk to, and room for a first base — and the service rolls
// another seed when a field does not leave one. 2.0 adds the bluffs' promises, planned again from
// the request and the colonies' starts: every quarry face holds its stone, and no rock parts any
// walkable ground.
// The colonies' swarm corners on a finished map, wrapped; empty when a colony is missing.
bool finishedStarts(const GenerationWorld &game, int teams, std::vector<int> &bootX, std::vector<int> &bootY)
{
	const Torus t(game.map);
	bootX.assign(static_cast<size_t>(teams), 0);
	bootY.assign(static_cast<size_t>(teams), 0);
	for (int k = 0; k < teams; ++k)
	{
		if (!game.hasTeam(k))
			return false;
		bootX[size_t(k)] = t.x(game.teamStart(k).x);
		bootY[size_t(k)] = t.y(game.teamStart(k).y);
	}
	return true;
}

std::string validateWorld(const GenerationWorld &game, const GenerationContext &context)
{
	if (std::string floor = startingFloorFailure(game.map, context.request.nbTeams); !floor.empty())
		return floor;
	const GenerationMap &map = game.map;
	const Torus t(map);
	const int teams = context.request.nbTeams;
	std::vector<int> bootX, bootY;
	for (int k = 0; k < teams; ++k)
		if (!game.hasTeam(k))
			return "Colony " + std::to_string(k) + " is missing.";
	finishedStarts(game, teams, bootX, bootY);
	const Bluffs bluffs = replanBluffs(context.request, bootX, bootY, teams);
	if (!bluffs.failure.empty())
		return bluffs.failure;
	if (const std::string fault = quarryFault(map, t, bluffs.quarry, bluffs.owner, teams, kHomeQuarryStone);
		!fault.empty())
		return fault;
	// Stone stands only in the quarries: no band of it along the shore.
	for (int i = 0; i < t.size(); ++i)
		if (!bluffs.quarry[i] && map.isResource(t.remainderX(i), i / t.w) &&
			map.getResource(t.remainderX(i), i / t.w).type == STONE)
			return "Stone at (" + std::to_string(t.remainderX(i)) + ", " + std::to_string(i / t.w) +
				   ") stands outside every quarry.";
	if (const int parting = partingBarrier(map); parting >= 0)
		return "A knot of rock at (" + std::to_string(t.remainderX(parting)) + ", " + std::to_string(parting / t.w) +
			   ") cuts ground off.";
	// The river loops once round the torus, so its banks join beyond its ends and every colony walks
	// to every other: no field or rock may seal a home.
	return walkFromFirstColony(map, teams, "the banks", "round the river").error;
}
} // namespace

RiverBluffCount riverReplannedBluffs(const GenerationWorld &game, const GenerationRequest &request)
{
	RiverBluffCount count;
	std::vector<int> bootX, bootY;
	if (!finishedStarts(game, request.nbTeams, bootX, bootY))
		return count;
	const Bluffs bluffs = replanBluffs(request, bootX, bootY, request.nbTeams);
	count.placed = bluffs.placed;
	count.faces = bluffs.faces;
	return count;
}

GeneratorDefinition riverDefinition()
{
	std::vector<GeneratorControl> controls{
		GeneratorControl{"water", "Water weight", 0, 100, 1, 45, ControlGroup::Terrain, false, true}
			.withSearchRange(30, 55),
		GeneratorControl{"sand", "Sand weight", 0, 100, 1, 3, ControlGroup::Terrain, false, true}
			.withSearchRange(1, 6),
		GeneratorControl{"grass", "Grass weight", 0, 100, 1, 75, ControlGroup::Terrain, false, true}
			.withSearchRange(60, 90),
		GeneratorControl{"desert", "Desert weight", 0, 100, 1, 0, ControlGroup::Terrain, false,
						 true}
			.withSearchRange(0, 5),
		GeneratorControl{"smoothing", "Smoothing", 1, 8, 1, 4, ControlGroup::Terrain, false}
			.withSearchRange(3, 6),
		GeneratorControl{"river-width", "River width", 20, 65, 5, 35, ControlGroup::Terrain, false}
			.withSearchRange(25, 45),
		// Off, the river runs straight across the map instead of meandering.
		GeneratorControl::toggle("winding-river", "Winding river", true, ControlGroup::Terrain)
			.withSearchValues({0, 1}),
		// Temperate (green grass, loam levees, heath, outcrop bluffs) or Desert, a Nile: a loam ribbon
		// along the water in dry grass, baked hardpan pans (slow, unbuildable) and weathered sandstone,
		// with hoodoo bluffs.
		GeneratorControl::choice("banks", "Banks", {"Random", "Temperate", "Desert"}, 0, ControlGroup::Terrain)
			.withSearchValues({1, 2}),
		// On, the wide water has a core of deep water: swimming across is slower and no algae grows
		// there. At least three tiles of shallows stay along every bank.
		GeneratorControl::toggle("deep-channel", "Deep channels", true, ControlGroup::Terrain)
			.withSearchValues({0, 1}),
		// On, rock bluffs stand on the outside of the river's bends, some with a quarry face of stone
		// (how many follows the stone amount) and scree round them.
		GeneratorControl::toggle("bluffs", "River bluffs", true, ControlGroup::Layout).withSearchValues({0, 1}),
		GeneratorControl{"fruit", "Fruit", 0, 64, 1, 4, ControlGroup::Resources, false}
			.withSearchRange(2, 8)};
	// River's stone is all quarries in rock, so the field's stone band (and where it goes) is not used.
	for (auto &c : heightFieldResourceControls())
		if (c.id != "hilltop-stone")
			controls.push_back(std::move(c));
	controls.push_back(
		GeneratorControl{"repeat", "Repeat landscape", 0, 5, 1, 0, ControlGroup::Layout, true}
			.withSearchRange(0, 1));
	return {
			"river",
			2,
			"River",
			9, // 9: one look per role
			false,
			std::move(controls),
			generate,
			true,
			nullptr,
			validateWorld,
			{"terrain:natural", "feature:river", "style:wide-open"}};
}
