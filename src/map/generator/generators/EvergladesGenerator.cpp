// SPDX-License-Identifier: GPL-3.0-or-later
#include "EvergladesGenerator.h"
#include "FertilityField.h"
#include "GenerationWorld.h"
#include "GenerationContext.h"
#include "Geometry.h"
#include "Grid.h"
#include "HeightMap.h"
#include "LatticeNoise.h"
#include "Morphology.h"
#include "Pipeline.h"
#include "Planting.h"
#include "ResourceSemantics.h"
#include "Resources.h"
#include "Roads.h"
#include "Rock.h"
#include "Settlements.h"
#include "Sketch.h"
#include "TerrainCatalog.h"
#include "Topology.h"
#include "Unit.h"
#include <algorithm>
#include <array>
#include <climits>
#include <cmath>
#include <cstdint>
#include <functional>
#include <string>
#include <utility>
#include <vector>
using namespace MapGeneration;

// Everglades: a dense wetland, the same everywhere and across the wrap. Pools and long sloughs lie
// on a jittered lattice that tiles the torus, bigger where the land is wet and smaller where it is
// dry, and the grass between them starts thick with wood and wheat. Every tile is a few steps from
// water, so wood and wheat grow back at the engine's top rate; resources block movement, so the
// swamp grows shut unless workers keep cutting it. Nothing grows on a pool's bank, so the banks
// between pools are the lanes that stay open, winding and narrow. Every colony starts in a clearing
// with a pond and a kit, ringed by a sand levee with gaps the swamp creeps through; what lies beyond
// is held only while it is kept cut.
//
// Nothing here is symmetric on purpose: the landscape is one random field over the whole map and
// the clearings are nudged off their ring, so it reads as country, not as an arena. Fairness is
// statistical, and the lobby keeps the best-scoring of several seeds. The design is a pure
// function of the request, so validateWorld rebuilds it and checks the finished world.
//
// DESIGN BRIEF, REVISION 4 (EVERGLADES 2.0). Revision 3 drew the river of grass with grass, sand and
// water alone; 2.0 draws it with the engine's full terrain palette, keeping the concept, the pool
// lattice, the clearings and the kits (the same pools, clearings and starts for the same seed):
// - The sloughs run dark: pool water at least kDeepSteps vertex steps from any bank is deep
//   (deepCore, DARK_WATER: swimmable at 192/256, no algae, still waters land), so every pool keeps a
//   shallow margin two tiles wide for algae, beaches and the fords, and only the big pools and sloughs
//   have a dark bed. The clearings' ponds stay shallow.
// - Mud banks (`mudflats`): that share of the swamp's pool banks is mudflat instead of sand (Rough:
//   walkable at 160/256, unbuildable, holds nothing). The lanes stay open, but mud does not inhibit
//   growth as sand does, so the grass beside them regrows harder: the swamp pushes back. Banks within
//   kMudHomeClear tiles of a clearing's levee stay sand, so the home keeps its room; the mud lies in
//   long stretches chosen by smooth noise, so sand stays by most reaches of water and algae (which
//   needs a sandy shore within reach) still regrows. Mud walks slower than sand, so travel along the
//   lanes is slower too.
// - Hammocks (`hammocks`): tree islands of thicket (Obstacles: impassable, block arrows), each a
//   rough knot at least two tiles thick drawn as tiles (rockVertices), on the swamp's grass at least
//   kHammockShore tiles from any bank and kHammockHomeClear from any clearing, so they never stand on
//   a lane. A hammock whose blocked tiles would part the ground round it is dropped, and the
//   validator checks the same on the finished map. The swamp's stone outcrops keep revision 3's
//   number and are scattered over the open ground the hammocks leave, so with hammocks on they lie in
//   different places than on revision 3 (the walk to the nearest stone is measured, docs).
// - Moss in the wet reaches: swamp grass where the wetness field passes kMossWet, beyond a tile past
//   each clearing's levee clearance, is moss (Fertile: crop land that waters its neighbours at
//   48/256), so the wet country farms better. It covers most of the swamp's grass at the defaults; the
//   rest, the dry reaches, is sawgrass. The growth tool scores a grass/moss boundary tile as nothing,
//   so a little moss measured worse than none; this much measured better than revision 3 (docs).
// - Fords are boardwalks: where openRoutes has to ford a slough to join a boxed-in colony, the ford
//   is boardwalk (Paths: fast, buildable, holds nothing) rather than revision 3's odd sand spit.
// - A season (`season`), looks only: wet (lush sawgrass, wet mud banks, moss) or dry (sawgrass gone
//   tawny, dried mud and pale sand on the banks, peat), random by default.
// - Looks are painted over a mechanic sketch (canonical GRASS, SAND, WATER, DEEP_WATER, MUDFLAT, MOSS,
//   THICKET) with composeLooks, so looks never change a rule. The clearings stay plain grass, which
//   the kit and the starting-crop guarantee need.
namespace
{

// The clearings sit near a ring at this share of the half side, evenly spaced from a random start,
// each nudged by up to these shares of a wedge and of the ring's radius.
constexpr double kHomeRing = 0.58;
// On a crowded map the ring widens up to this share of the half side before the request is refused.
constexpr double kHomeRingMax = 0.76;
constexpr double kHomeJitterTurn = 0.12;
constexpr double kHomeJitterRadius = 0.08;
// The clearings' outlines wobble by up to this share of their radius. A clearing that would not fit
// its ring shrinks down to this radius before the request is refused.
constexpr double kClearingRoughness = 0.3;
constexpr int kMinimumClearing = 6;
// A clearing smaller than this has no pond: the swamp's pools are a few steps away anyway.
constexpr int kPondClearing = 9;
// A pond up to this big sits in every clearing (smaller in a shrunken clearing); the levee is this
// wide, and no pool water lies within this distance beyond it, so the gaps open onto grass.
constexpr double kPondRadius = 3.0;
constexpr double kLeveeWidth = 2.0;
constexpr double kLeveeClearance = 2.0;
constexpr int kLeveeSectors = 36;
// Every home starts identical: wheat and wood beside its pond, a quarry, all unscaled.
constexpr int kHomeWheat = 40;
constexpr int kHomeWood = 30;
// The kit keeps this far from the swarm beyond the clearance, so there is room to build beside it.
constexpr int kBuildRoom = 4;
// The swamp's standing wood and wheat, as shares of the grass off the clearings, at 100.
constexpr int kWoodShare = 55;
constexpr int kWheatShare = 15;
// A slough is a pool this much bigger, stretched this much along a random line.
constexpr double kSloughGrowth = 1.4;
constexpr double kSloughStretch = 2.2;
// Pools scale from this at the driest ground to this at the wettest.
constexpr double kDryScale = 0.6;
constexpr double kWetScale = 1.5;
// The most of the map the pools may be expected to cover, before the beaches; pools that would
// cover more are scaled down to this, since beyond it the colonies can hardly reach each other.
constexpr double kMaximumCoverage = 0.6;
// Opening a route for a boxed-in colony: the cost of a step onto open ground, onto a deposit that
// will be cleared, onto stone or fruit, and onto water that becomes a ford.
constexpr int kStepOpen = 1, kStepDeposit = 3, kStepEternal = 8, kStepFord = 25;

// 2.0. Deep water: pool water at least kDeepSteps vertex steps from the bank, in cores of at least
// kDeepLeast vertices, so a deep patch reads as a slough's bed, not a speck. Two steps (the least
// deepCore allows) left no pure shallow tile in most pools: algae, which grows only on pure shallow
// water, fell from about 1,750 tiles to 7 at the defaults, and the AIs' wood harvest in the
// calibration game halved. Four steps leaves a shallow band two tiles wide (about 1,260 tiles of
// algae) and lays deep water only in the big pools' and sloughs' middles.
constexpr int kDeepSteps = 4, kDeepLeast = 6;
// Mud banks keep this many tiles beyond a clearing's levee clearance as sand.
constexpr int kMudHomeClear = 6;
// Hammocks: per 128x128 tiles at the control's value; a rough knot kHammockLow to kHammockHigh tiles
// in radius, kHammockShore tiles from any bank, kHammockHomeClear tiles beyond a clearing's levee
// clearance, kHammockSpacing tiles between centres; a knot left with fewer than kHammockLeast tiles
// after its two-thick trim is not laid.
// A knot is drawn no bigger than its centre's room allows (its steps from the bank less
// kHammockShore, over 1.2 for the rough outline), so the big ones stand in the broad dry reaches.
// kHammockShore 2: shore steps are counted from the tiles that are not pure grass (a bank's mixed
// tiles among them), so a hammock tile two steps out leaves one tile of pure grass between the knot and
// the bank's mixed tiles, which keeps the lane beside it.
constexpr double kHammockLow = 2.8, kHammockHigh = 5.0, kHammockRoughness = 0.3;
constexpr int kHammockShore = 2, kHammockHomeClear = 6, kHammockSpacing = 16, kHammockLeast = 10;
constexpr int kHammockTries = 12; // candidate draws per hammock wanted
// Moss: the swamp's grass where the wetness field passes this, beyond kMossHomeClear tiles past a
// clearing's levee clearance.
constexpr double kMossHomeClear = 1.0;
constexpr float kMossWet = 0.30f;

// The seasons, looks only.
struct Season
{
	const char *name;
	// Grass: lush where the wetness passes `lush` (or near the water), dry where it is under `dry`.
	float lush, dry;
	// The dry season's moss shows peat (loam) in patches away from the water. A moss/loam tile plays
	// as moss (one rule group), but the terrain dump records it as mixed and the growth tool scores it
	// as nothing, so food is measured with the season pinned wet (the rules are the same; contract).
	TerrainType deep, bank, mud, mudSecond, moss, mossSecond;
	int peatShare; // of 65536
};
constexpr std::array<Season, 2> kSeasons{{
	{"wet", 0.15f, 0.04f, DARK_WATER, SAND, MUD, MUDFLAT, MOSS, MOSS, 0},
	{"dry", 0.28f, 0.20f, DARK_WATER, PALE_SAND, MUDFLAT, HARDPAN, MOSS, LOAM, 22000},
}};

struct Geometry
{
	int teams, half, clearing, buildRoom;
	double spacing, poolRadius, sloughs, homeRing, levee, pond, clearance;
	double reach, jitterTurn, jitterRadius; // a clearing's fullest extent; how far homes are nudged
	double coverage;                        // the share of the map the pools are expected to cover
};

// The share of the map the pools cover, expected: the mean pool area (radius scaled by wetness,
// the random factor and the rough outline, stretched; sloughs grown and stretched further) over
// the cell area, with overlaps discounted.
double expectedCoverage(const Geometry &g)
{
	const double meanScale = (kDryScale + kWetScale) / 2;
	const double radius2 = g.poolRadius * g.poolRadius * meanScale * meanScale * 1.1;
	// The 1.1 above is the rough outline's extra area and the 1.2 here the mean stretch of an
	// ordinary pool (1 to 1.4); a slough is grown and stretched further instead, so its own 1.2
	// comes out. 1 - exp(-nominal) is the share covered by randomly overlapping shapes of that
	// total area, since two pools on one tile cover it only once.
	const double pool = kPi * radius2 * 1.2;
	const double slough = pool * kSloughGrowth * kSloughGrowth * kSloughStretch / 1.2;
	const double nominal = ((1 - g.sloughs) * pool + g.sloughs * slough) / (g.spacing * g.spacing);
	return 1 - std::exp(-nominal);
}

// Whether every clearing, at its fullest reach, fits between its neighbours and inside the wrap
// with the homes nudged this far.
bool clearingsFit(const Geometry &g)
{
	const double nearest =
		2 * g.homeRing * (1 - g.jitterRadius) * std::sin(kPi * (1 - 2 * g.jitterTurn) / g.teams);
	const bool fitsRing = g.teams < 2 || nearest >= 2 * g.reach;
	const bool fitsWrap = g.homeRing * (1 + g.jitterRadius) + g.reach <= g.half - 1;
	return fitsRing && fitsWrap;
}

Geometry geometryFor(const GenerationRequest &r)
{
	const EvergladesOptions o(r);
	Geometry g;
	g.teams = std::max(1, r.nbTeams);
	g.half = std::min(1 << r.wDec, 1 << r.hDec) / 2;
	g.clearing = o.clearingSize;
	g.spacing = o.poolSpacing;
	g.poolRadius = o.poolSize;
	g.sloughs = o.sloughs / 100.0;
	g.homeRing = kHomeRing * g.half;
	g.levee = o.levee / 100.0;
	// Nudge the homes as far as the ring allows, down to not at all on a crowded ring; when even
	// that does not fit, the clearings shrink until it does, then the grass beyond the levee and
	// the margin go, then the ring widens. Only then is the request refused.
	g.clearance = kLeveeClearance;
	double margin = 2;
	for (;;)
	{
		g.reach = g.clearing * (1 + kClearingRoughness) + kLeveeWidth + g.clearance + margin;
		for (int step = 4; step >= 0; --step)
		{
			g.jitterTurn = kHomeJitterTurn * step / 4;
			g.jitterRadius = kHomeJitterRadius * step / 4;
			if (clearingsFit(g))
				break;
		}
		if (clearingsFit(g))
			break;
		if (g.clearing > kMinimumClearing)
			--g.clearing;
		else if (g.clearance > 0 || margin > 0)
			g.clearance = margin = 0;
		// Widen the ring 2% of the half side at a time: the least change that makes room.
		else if (g.homeRing + 0.02 * g.half <= kHomeRingMax * g.half)
			g.homeRing += 0.02 * g.half;
		else
			break;
	}
	// The pond is about a quarter of the clearing's radius (2 to 3 tiles): enough water for the
	// kit's fields to regrow beside, and small enough to leave the clearing its building room.
	g.pond = g.clearing >= kPondClearing ? std::clamp(g.clearing * 0.23, 2.0, kPondRadius) : 0.0;
	g.buildRoom = std::clamp(g.clearing - 5, 0, kBuildRoom);
	// Pools that would drown the map are scaled down to the wettest playable coverage.
	g.coverage = expectedCoverage(g);
	if (g.coverage > kMaximumCoverage)
	{
		const double wanted = -std::log(1 - kMaximumCoverage), now = -std::log(1 - g.coverage);
		g.poolRadius *= std::sqrt(wanted / now);
		g.coverage = expectedCoverage(g);
	}
	return g;
}

// A pool: a rough disc stretched along a line through its centre.
struct Pool
{
	double x, y, aspect, spin, reach;
	RadialShape shape;
	bool holds(double dx, double dy) const
	{
		if (std::abs(dx) > reach || std::abs(dy) > reach)
			return false;
		const double u = (dx * std::cos(spin) + dy * std::sin(spin)) / aspect;
		const double v = -dx * std::sin(spin) + dy * std::cos(spin);
		return std::hypot(u, v) < shape.radiusAt(std::atan2(v, u));
	}
};

struct Home
{
	int x, y;
	RadialShape outline, pond;
	std::array<unsigned char, kLeveeSectors> standing;
};

struct Layout
{
	Torus t{1, 1};
	Geometry g{};
	int cx = 0, cy = 0;
	std::vector<Pool> pools;
	std::vector<Home> homes;
	std::vector<unsigned char> water, sand; // per vertex: pool and pond water, the levee
	std::vector<int> clearingOf;            // clearing tiles: the colony, else -1
	std::vector<float> wetness;             // the wetness field, 0 to 1, per tile
	// 2.0: the mechanic sketch (canonical rule-group types), and what each layer took.
	TerrainSketch terrain;
	std::vector<unsigned char> hammock; // hammock tiles (the knot as drawn, before rockVertices)
	std::vector<unsigned char> deep;    // deep vertices
	std::vector<unsigned char> nearHome; // tiles within kMudHomeClear of a clearing's levee clearance
	int hammocksWanted = 0, hammocksPlaced = 0, hammocksDropped = 0;
	int mudVertices = 0, mossVertices = 0, deepVertices = 0, thicketVertices = 0;
	std::string failure;
};

// Keeps only the tiles of `mask` that lie in some 2x2 block of it, so every kept tile has a blocked
// vertex once rockVertices draws it.
std::vector<unsigned char> blocksOfTwo(const Torus &t, const std::vector<unsigned char> &mask)
{
	std::vector<unsigned char> out(mask.size(), 0);
	for (int y = 0; y < t.h; ++y)
		for (int x = 0; x < t.w; ++x)
			if (mask[t.at(x, y)] && mask[t.at(x + 1, y)] && mask[t.at(x, y + 1)] &&
				mask[t.at(x + 1, y + 1)])
				for (int dy = 0; dy <= 1; ++dy)
					for (int dx = 0; dx <= 1; ++dx)
						out[t.at(x + dx, y + dy)] = 1;
	return out;
}

// The tiles a knot drawn as `tiles` blocks: two or more blocked corners, as the engine derives a tile.
std::vector<unsigned char> knotBlocked(const Torus &t, const std::vector<unsigned char> &tiles)
{
	const std::vector<unsigned char> v = rockVertices(t, tiles);
	std::vector<unsigned char> blocked(v.size(), 0);
	for (int y = 0; y < t.h; ++y)
		for (int x = 0; x < t.w; ++x)
			blocked[t.at(x, y)] =
				v[t.at(x, y)] + v[t.at(x + 1, y)] + v[t.at(x, y + 1)] + v[t.at(x + 1, y + 1)] >= 2;
	return blocked;
}

// Whether some group of `blocked` (8-connected) parts the ground round it: the open tiles beside it
// fall in more than one walkable component, so the group cut some ground off. Returns the first such
// group's label, or -1; `label` receives the groups.
int partingGroup(const Torus &t, const std::vector<unsigned char> &walkable,
				 const std::vector<unsigned char> &blocked, std::vector<int> &label)
{
	const int n = t.size();
	std::vector<unsigned char> open(size_t(n), 0);
	for (int i = 0; i < n; ++i)
		open[i] = walkable[i] && !blocked[i];
	const std::vector<int> component = connectedRegions(open, t.w, t.h, true, GridNeighbors::Eight);
	label = connectedRegions(blocked, t.w, t.h, true, GridNeighbors::Eight);
	int groups = 0;
	for (int l : label)
		groups = std::max(groups, l + 1);
	std::vector<int> seen(size_t(groups), -1);
	for (int i = 0; i < n; ++i)
	{
		if (label[i] < 0)
			continue;
		const int x = t.remainderX(i), y = i / t.w;
		for (int dy = -1; dy <= 1; ++dy)
			for (int dx = -1; dx <= 1; ++dx)
			{
				const int j = t.at(x + dx, y + dy);
				if (!open[j])
					continue;
				if (seen[label[i]] < 0)
					seen[label[i]] = component[j];
				else if (seen[label[i]] != component[j])
					return label[i];
			}
	}
	return -1;
}

int seasonOf(const EvergladesOptions &o)
{
	return std::clamp(o.season, 1, 2) - 1;
}

// The tiles within `extra` tiles beyond the fullest reach of any clearing's levee clearance.
std::vector<unsigned char> homeDiscs(const Layout &L, double extra)
{
	const Torus &t = L.t;
	std::vector<unsigned char> nearby(size_t(t.size()), 0);
	for (int k = 0; k < L.g.teams; ++k)
	{
		const Home &h = L.homes[k];
		const double r = h.outline.maximumRadius() + kLeveeWidth + L.g.clearance + extra;
		const int box = int(std::ceil(r)) + 1;
		for (int dy = -box; dy <= box; ++dy)
			for (int dx = -box; dx <= box; ++dx)
				if (dx * dx + dy * dy <= r * r)
					nearby[t.at(h.x + dx, h.y + dy)] = 1;
	}
	return nearby;
}

// The hammocks: rough knots of thicket on the swamp's grass, well off every bank and every
// clearing, apart from each other; any whose blocked tiles would part the ground round it is
// dropped. Planned from the stamped sketch and the request alone, so validateWorld plans them again.
void planHammocks(Layout &L, const EvergladesOptions &o, GenerationContext &context)
{
	const Torus &t = L.t;
	const int n = t.size();
	L.hammock.assign(size_t(n), 0);
	L.hammocksWanted = int(std::lround(double(o.hammocks) * n / (128.0 * 128.0)));
	if (L.hammocksWanted <= 0)
		return;
	// Steps from anything that is not swamp grass: banks, water, levees and the clearings.
	std::vector<unsigned char> notSwamp(size_t(n), 0);
	const std::vector<unsigned char> grass = pureTiles(L.terrain, t, GRASS);
	for (int i = 0; i < n; ++i)
		notSwamp[i] = !grass[i];
	const std::vector<int> shore = stepsFrom(t, notSwamp);
	std::vector<unsigned char> home(size_t(n), 0);
	for (int k = 0; k < L.g.teams; ++k)
	{
		const Home &h = L.homes[k];
		const double r = h.outline.maximumRadius() + kLeveeWidth + L.g.clearance + kHammockHomeClear;
		const int box = int(std::ceil(r)) + 1;
		for (int dy = -box; dy <= box; ++dy)
			for (int dx = -box; dx <= box; ++dx)
				if (dx * dx + dy * dy <= r * r)
					home[t.at(h.x + dx, h.y + dy)] = 1;
	}
	const char *stream = "glades-hammocks";
	std::vector<int> candidates;
	for (int i = 0; i < n; ++i)
		if (shore[i] >= kHammockShore + 3 && !home[i])
			candidates.push_back(i);
	context.shuffle(candidates.begin(), candidates.end(), stream);
	// The broadest dry ground first (in random order within each step), so the hammocks stand where
	// there is room for a big one.
	std::stable_sort(candidates.begin(), candidates.end(),
					 [&](int a, int b) { return shore[a] > shore[b]; });
	std::vector<unsigned char> taken(size_t(n), 0);
	std::vector<std::vector<int>> placed;
	const int tries = std::min<int>(int(candidates.size()), L.hammocksWanted * kHammockTries);
	for (int c = 0; c < tries && int(placed.size()) < L.hammocksWanted; ++c)
	{
		const int centre = candidates[size_t(c)];
		if (taken[centre])
			continue;
		const int cx = t.remainderX(centre), cy = centre / t.w;
		const double radius =
			std::min(kHammockLow + (kHammockHigh - kHammockLow) * context.bounded(stream, 1001) / 1000.0,
					 std::max(2.2, (shore[centre] - kHammockShore) / 1.2));
		const RadialShape shape(radius, kHammockRoughness, context, stream);
		const int box = int(std::ceil(shape.maximumRadius())) + 1;
		std::vector<int> tiles;
		std::vector<unsigned char> local(size_t(n), 0);
		bool fits = true;
		for (int dy = -box; dy <= box && fits; ++dy)
			for (int dx = -box; dx <= box && fits; ++dx)
			{
				const double d = std::hypot(double(dx), double(dy));
				if (d >= shape.radiusAt(std::atan2(double(dy), double(dx))))
					continue;
				const int i = t.at(cx + dx, cy + dy);
				if (shore[i] < kHammockShore || home[i])
					fits = false;
				local[i] = 1;
				tiles.push_back(i);
			}
		if (!fits)
			continue;
		const std::vector<unsigned char> blocks = blocksOfTwo(t, local);
		std::vector<int> kept;
		for (int i : tiles)
			if (blocks[i])
				kept.push_back(i);
		if (int(kept.size()) < kHammockLeast)
			continue;
		const int clear = kHammockSpacing;
		for (int dy = -clear; dy <= clear; ++dy)
			for (int dx = -clear; dx <= clear; ++dx)
				if (dx * dx + dy * dy <= clear * clear)
					taken[t.at(cx + dx, cy + dy)] = 1;
		placed.push_back(kept);
	}
	// Drop any hammock that parts the ground round it, then check again.
	const std::vector<unsigned char> walk = walkableTiles(L.terrain, t);
	std::vector<unsigned char> alive(placed.size(), 1);
	std::vector<int> owner(size_t(n), -1);
	for (size_t q = 0; q < placed.size(); ++q)
		for (int i : placed[q])
			owner[i] = int(q);
	for (size_t round = 0; round <= placed.size(); ++round)
	{
		std::fill(L.hammock.begin(), L.hammock.end(), 0);
		for (size_t q = 0; q < placed.size(); ++q)
			if (alive[q])
				for (int i : placed[q])
					L.hammock[i] = 1;
		std::vector<int> label;
		const int parting = partingGroup(t, walk, knotBlocked(t, L.hammock), label);
		if (parting < 0)
			break;
		for (int i = 0; i < n; ++i)
			if (label[i] == parting && owner[i] >= 0)
				alive[size_t(owner[i])] = 0;
		++L.hammocksDropped;
	}
	for (size_t q = 0; q < placed.size(); ++q)
		L.hammocksPlaced += alive[q];
}

// The mechanic sketch: grass, the pools and ponds, the levees, beaches; the hammocks' thicket; mud
// on the swamp's banks away from the homes; moss in the wettest reaches; the sloughs' deep beds.
void sketchTerrain(Layout &L, const EvergladesOptions &o, GenerationContext &context)
{
	const Torus &t = L.t;
	const int n = t.size();
	TerrainSketch &T = L.terrain;
	T.assign(size_t(n), GRASS);
	for (int i = 0; i < n; ++i)
	{
		if (L.water[i])
			T[i] = WATER;
		else if (L.sand[i])
			T[i] = SAND;
	}
	layBeaches(T, t);

	// Near the homes: tiles within kMudHomeClear beyond any clearing's levee clearance.
	L.nearHome = homeDiscs(L, kMudHomeClear);
	const std::vector<unsigned char> mossClear = homeDiscs(L, kMossHomeClear);

	planHammocks(L, o, context);

	// Mud banks: of the swamp's beach vertices (sand beside water, not a levee, not near a home), the
	// share `mudflats` ranked by smooth noise, so the mud lies in long stretches of bank.
	{
		std::vector<int> fray = fractalNoise(t.w, t.h, 16, 2, context.stream("glades-mud"));
		std::vector<std::pair<int, int>> banks;
		for (int i = 0; i < n; ++i)
		{
			if (T[i] != SAND || L.sand[i] || L.nearHome[i] || L.clearingOf[i] >= 0)
				continue;
			const int x = t.remainderX(i), y = i / t.w;
			bool shore = false;
			for (int dy = -1; dy <= 1 && !shore; ++dy)
				for (int dx = -1; dx <= 1 && !shore; ++dx)
					shore = T[t.at(x + dx, y + dy)] == WATER;
			if (shore)
				banks.push_back({fray[i], i});
		}
		std::stable_sort(banks.begin(), banks.end());
		const size_t mud =
			size_t(std::lround(banks.size() * std::clamp(o.mudflats, 0, 100) / 100.0));
		for (size_t k = 0; k < mud; ++k)
			T[banks[k].second] = MUDFLAT;
		L.mudVertices = int(mud);
	}
	// Moss in the wettest reaches of the swamp's grass, off the homes.
	for (int i = 0; i < n; ++i)
		if (T[i] == GRASS && !mossClear[i] && L.clearingOf[i] < 0 && L.wetness[i] >= kMossWet)
			T[i] = MOSS;
	// The hammocks' thicket, drawn as tiles.
	const std::vector<unsigned char> thicket = rockVertices(t, L.hammock);
	for (int i = 0; i < n; ++i)
		if (thicket[i])
		{
			T[i] = THICKET;
			++L.thicketVertices;
		}
	// The moss left once the thicket has taken its vertices (a hammock may stand in the wet reaches).
	L.mossVertices = int(std::count(T.begin(), T.end(), MOSS));
	// The sloughs' deep beds: pool water (not a clearing's pond) well inside its bank.
	std::vector<unsigned char> pool(size_t(n), 0);
	for (int i = 0; i < n; ++i)
		pool[i] = T[i] == WATER && L.clearingOf[i] < 0;
	L.deep = dropSmallRegions(t, deepCore(t, pool, kDeepSteps), kDeepLeast);
	for (int i = 0; i < n; ++i)
		if (L.deep[i])
		{
			T[i] = DEEP_WATER;
			++L.deepVertices;
		}
}

Layout design(const GenerationRequest &request, GenerationContext &context)
{
	Layout L;
	L.t = {1 << request.wDec, 1 << request.hDec};
	L.g = geometryFor(request);
	const EvergladesOptions o(request);
	const Torus &t = L.t;
	const Geometry &g = L.g;
	const int n = t.w * t.h, teams = g.teams;
	L.cx = t.w / 2;
	L.cy = t.h / 2;
	const auto unit = [&](const char *stream)
	{ return context.bounded(stream, 2001) / 1000.0 - 1; };

	if (context.telemetry.enabled())
	{
		context.telemetry.measure("everglades.clearing.requested-radius", o.clearingSize);
		context.telemetry.measure("everglades.clearing.actual-radius", g.clearing);
		context.telemetry.measure("everglades.pools.actual-radius", g.poolRadius);
		context.telemetry.measure("everglades.pools.expected-coverage", g.coverage);
		context.telemetry.measure("everglades.home.ring", g.homeRing);
		context.telemetry.measure("everglades.home.jitter-turn", g.jitterTurn);
		context.telemetry.measure("everglades.home.jitter-radius", g.jitterRadius);
		context.telemetry.measure("everglades.home.pond-radius", g.pond);
		if (g.clearing < o.clearingSize)
			context.telemetry.fallback("everglades.clearing.shrunk",
									   "Clearings shrank to fit their ring");
		if (g.poolRadius < o.poolSize)
			context.telemetry.fallback("everglades.pools.scaled",
									   "Pool coverage cap reduced radii");
	}
	// The homes: near a ring, evenly spaced from a random start, each nudged a little.
	const double phase = context.bounded("glades-layout", 3600) / 3600.0 * 2 * kPi;
	const double wedge = 2 * kPi / teams;
	for (int k = 0; k < teams; ++k)
	{
		const double a = phase + wedge * (k + 0.5 + g.jitterTurn * unit("glades-layout"));
		const double r = g.homeRing * (1 + g.jitterRadius * unit("glades-layout"));
		Home home{t.x(L.cx + int(std::lround(r * std::cos(a)))),
				  t.y(L.cy + int(std::lround(r * std::sin(a)))),
				  RadialShape(g.clearing, kClearingRoughness, context, "glades-clearings"),
				  RadialShape(std::max(g.pond, 1.0), 0.3, context, "glades-ponds"),
				  {}};
		// The levee round the clearing: a share of its sectors stand, the rest are gaps.
		home.standing.fill(1);
		std::array<int, kLeveeSectors> sectors{};
		for (int s = 0; s < kLeveeSectors; ++s)
			sectors[s] = s;
		context.shuffle(sectors.begin(), sectors.end(), "glades-levee");
		const int open = int(std::lround((1 - g.levee) * kLeveeSectors));
		for (int i = 0; i < open; ++i)
			home.standing[sectors[i]] = 0;
		L.homes.push_back(home);
	}

	// The pools: one per cell of a lattice that tiles the torus exactly, jittered, sized by a
	// smooth wetness field so the country has wet reaches and dry ones, some grown and stretched
	// into sloughs.
	HeightMap wet(t.w, t.h, context.stream("glades-wet"));
	// The wetness field's noise has cells 12 tiles across and its reaches span several of them, so
	// wet and dry country runs across several pools rather than changing from one pool to the next.
	wet.makePlain(12);
	L.wetness.assign(size_t(n), 0.0f);
	for (int i = 0; i < n; ++i)
		L.wetness[i] = std::clamp(wet(unsigned(t.remainderX(i)), unsigned(i / t.w)), 0.0f, 1.0f);
	const int nx = std::max(1, int(std::lround(t.w / g.spacing)));
	const int ny = std::max(1, int(std::lround(t.h / g.spacing)));
	const double cellW = double(t.w) / nx, cellH = double(t.h) / ny;
	for (int j = 0; j < ny; ++j)
		for (int i = 0; i < nx; ++i)
		{
			// Each pool is nudged up to a third of a cell from the cell's centre: enough to hide
			// the lattice, not enough for two pools to pile into one spot and leave a dry gap.
			const double px = (i + 0.5) * cellW + unit("glades-pools") * cellW / 3;
			const double py = (j + 0.5) * cellH + unit("glades-pools") * cellH / 3;
			const float wetness =
				std::clamp(wet(unsigned(t.x(int(px))), unsigned(t.y(int(py)))), 0.0f, 1.0f);
			const bool slough = context.bounded("glades-pools", 1000) < g.sloughs * 1000;
			// Radius 70% to 130% of the pool size, scaled by wetness; outlines wobble by up to 45%
			// of it, and ordinary pools are stretched up to 1.4 times, so no two pools look alike.
			const double radius =
				g.poolRadius * (0.7 + context.bounded("glades-pools", 601) / 1000.0) *
				(kDryScale + (kWetScale - kDryScale) * wetness) * (slough ? kSloughGrowth : 1);
			RadialShape shape(radius, 0.45, context, "glades-pools");
			const double aspect =
				slough ? kSloughStretch : 1 + context.bounded("glades-pools", 401) / 1000.0;
			const double spin = context.bounded("glades-pools", 3600) / 3600.0 * kPi;
			L.pools.push_back({px, py, aspect, spin, shape.maximumRadius() * aspect + 1, shape});
		}

	L.water.assign(n, 0);
	L.sand.assign(n, 0);
	L.clearingOf.assign(n, -1);
	for (const Pool &p : L.pools)
	{
		const int r = int(std::ceil(p.reach));
		const int x0 = int(std::floor(p.x)), y0 = int(std::floor(p.y));
		for (int dy = -r; dy <= r; ++dy)
			for (int dx = -r; dx <= r; ++dx)
				if (p.holds(x0 + dx - p.x, y0 + dy - p.y))
					L.water[t.at(x0 + dx, y0 + dy)] = 1;
	}
	// The clearings, cut out of the swamp: grass with a pond, the levee, and grass just beyond it.
	for (int k = 0; k < teams; ++k)
	{
		const Home &home = L.homes[k];
		const int r = int(std::ceil(home.outline.maximumRadius() + kLeveeWidth + g.clearance)) + 1;
		for (int dy = -r; dy <= r; ++dy)
			for (int dx = -r; dx <= r; ++dx)
			{
				const int i = t.at(home.x + dx, home.y + dy);
				const double d = std::hypot(double(dx), double(dy)),
							 a = std::atan2(double(dy), double(dx));
				const double edge = home.outline.radiusAt(a);
				if (d < edge)
				{
					L.clearingOf[i] = k;
					L.water[i] = g.pond > 0 && d < home.pond.radiusAt(a);
					L.sand[i] = 0;
				}
				else if (d < edge + kLeveeWidth)
				{
					const int sector =
						int(std::fmod(a + 2 * kPi, 2 * kPi) / (2 * kPi) * kLeveeSectors) %
						kLeveeSectors;
					L.water[i] = 0;
					L.sand[i] = home.standing[sector];
				}
				else if (d < edge + kLeveeWidth + g.clearance)
				{
					L.water[i] = 0;
				}
			}
	}
	context.telemetry.measure("everglades.pools.actual", L.pools.size());
	sketchTerrain(L, o, context);
	return L;
}

// The looks over the mechanic sketch (composeLooks keeps each inside its rule group), by season:
// sawgrass lush where it is wet and tawny where it is dry, wet or dried mud on the banks, pale sand
// in the dry season, moss or peat, the sloughs' dark beds. The clearings and the ground just round
// them stay plain grass.
TerrainSketch lookSketch(const Layout &L, int seasonIndex, GenerationContext &context)
{
	const Torus &t = L.t;
	const int n = t.size();
	const Season &season = kSeasons[size_t(seasonIndex)];
	std::vector<unsigned char> wetVertex(size_t(n), 0);
	for (int i = 0; i < n; ++i)
		wetVertex[i] = L.terrain[i] == WATER || L.terrain[i] == DEEP_WATER;
	const std::vector<int> fromWet = stepsFrom(t, wetVertex);
	const std::vector<int> fray = fractalNoise(t.w, t.h, 8, 2, context.stream("glades-looks"));
	const std::vector<int> split = fractalNoise(t.w, t.h, 20, 2, context.stream("glades-looks"));
	// The ground within the levee clearance of a clearing keeps plain grass.
	std::vector<unsigned char> plain(size_t(n), 0);
	for (int k = 0; k < L.g.teams; ++k)
	{
		const Home &h = L.homes[k];
		const double r = h.outline.maximumRadius() + kLeveeWidth + L.g.clearance + 1;
		const int box = int(std::ceil(r)) + 1;
		for (int dy = -box; dy <= box; ++dy)
			for (int dx = -box; dx <= box; ++dx)
				if (dx * dx + dy * dy <= r * r)
					plain[t.at(h.x + dx, h.y + dy)] = 1;
	}
	TerrainSketch looks(size_t(n), 0);
	int lush = 0, dry = 0;
	for (int i = 0; i < n; ++i)
	{
		// The wetness, shifted by fine noise so the looks' edges fray rather than follow contours.
		const float w = L.wetness[i] + (fray[i] - 32768) / 65536.0f * 0.25f;
		switch (L.terrain[i])
		{
		case GRASS:
			if (plain[i])
				break;
			if (w >= season.lush || (fromWet[i] >= 0 && fromWet[i] <= 2 && w >= season.dry))
			{
				looks[i] = LUSH_GRASS;
				++lush;
			}
			else if (w < season.dry)
			{
				looks[i] = DRY_GRASS;
				++dry;
			}
			break;
		case MOSS:
			looks[i] = fromWet[i] >= 3 && split[i] < season.peatShare ? season.mossSecond : season.moss;
			break;
		case MUDFLAT:
			looks[i] = split[i] < 40000 ? season.mud : season.mudSecond;
			break;
		case SAND:
			if (!L.sand[i] && split[i] > 30000)
				looks[i] = season.bank;
			break;
		case DEEP_WATER:
			looks[i] = season.deep;
			break;
		default:
			break;
		}
	}
	context.telemetry.measure("everglades.looks.lush-grass", lush);
	context.telemetry.measure("everglades.looks.dry-grass", dry);
	return looks;
}

// Every home's kit, identical and unscaled: wheat and wood beside the pond on the sides, a quarry
// on the near side towards the map's centre. The rest of the clearing starts clear.
void furnishHomes(GenerationMap &map, const Layout &L, GenerationContext &context)
{
	const Torus &t = L.t;
	const std::vector<unsigned char> reserved =
		swarmSurroundings(t, context, kSwarmClearance + L.g.buildRoom);
	for (int team = 0; team < L.g.teams; ++team)
	{
		const Home &home = L.homes[team];
		const auto eligible = [&](int i)
		{ return L.clearingOf[i] == team && !reserved[i] && clearGround(map, t.remainderX(i), i / t.w); };
		const double a =
			std::atan2(double(t.offsetY(L.cy, home.y)), double(t.offsetX(L.cx, home.x)));
		const KitFrame frame{home.x, home.y, a};
		// Wheat and wood start either side of the pond, just past its fullest outline (1.3) and
		// beach (3 tiles), where they regrow; the quarry at the clearing's edge 4 tiles in, toward
		// the map's centre, off the farmland.
		const double beside = L.g.pond * 1.3 + 3;
		plantKit(map, t, context,
				 {frame.at(0, -beside, 10), frame.at(0, beside, 10),
				  frame.at(-(L.g.clearing - 4), 0, 8), kHomeWheat, kHomeWood, 2},
				 eligible);
	}
}

// The swamp: standing wood and wheat in patches over the grass off the clearings, stone outcrops
// that never grow, and a few fruit groves. Everything will grow from here.
void stockSwamp(GenerationMap &map, const Layout &L, GenerationContext &context, const EvergladesOptions &o)
{
	const Torus &t = L.t;
	const int n = t.w * t.h;
	HeightMap patch(t.w, t.h, context.stream("glades-patch"));
	// patch (noise cells 10 tiles across) decides where the swamp stands thickest, and split (cells
	// 5 across, finer) whether a stretch is wood or wheat. Wood is most of the swamp (55% of its
	// grass against 15% wheat): a thicket to cut through, which is also the building material an
	// expanding colony needs.
	patch.makePlain(10);
	HeightMap split(t.w, t.h, context.stream("glades-split"));
	split.makePlain(5);
	const auto eligible = [&](int i)
	{ return L.clearingOf[i] < 0 && clearGround(map, t.remainderX(i), i / t.w); };
	std::vector<int> ground;
	std::vector<std::pair<float, int>> byPatch;
	for (int i = 0; i < n; ++i)
		if (eligible(i))
		{
			ground.push_back(i);
			byPatch.push_back({-patch(t.remainderX(i), i / t.w), i});
		}
	if (ground.empty())
		return;
	std::stable_sort(byPatch.begin(), byPatch.end());
	const int area = int(ground.size());
	const int wood = int(scaledCount(std::int64_t(area) * kWoodShare / 100, o.wood));
	const int wheat = int(scaledCount(std::int64_t(area) * kWheatShare / 100, o.wheat));
	std::vector<int> byDensity;
	for (const auto &entry : byPatch)
		byDensity.push_back(entry.second);
	plantFields(map, t, byDensity, wheat, wood,
				[&](int i) { return split.uiLevel(t.remainderX(i), i / t.w, 2048); });
	// One stone outcrop per 1000 tiles of swamp and one fruit grove per 1500, each of a random
	// kind: stone lasts forever, so small scattered quarries are plenty; fruit is a prize to find
	// in the thicket, not something to farm.
	scatterClumps(context, t, ground, int(scaledCount(std::max(2, area / 1000), o.stone)),
				  "glades-stone", eligible,
				  [&](MapGeneratorPoint p)
				  {
					  placeResourceClump(map, context, p, STONE,
										 1 + int(context.bounded("glades-stone", 2)));
				  });
	scatterClumps(context, t, ground, int(scaledCount(std::max(3, area / 1500), o.fruit)),
				  "glades-fruit", eligible,
				  [&](MapGeneratorPoint p)
				  {
					  placeResourceClump(map, context, p,
										 CHERRY + int(context.bounded("glades-fruit", 3)), 2);
				  });
}

// Every colony must be able to walk to colony 0 at the start. Where the swamp or the sloughs box
// one in, the cheapest way through is opened (openColonyRoutes): deposits on it are cleared and water
// on it becomes a ford. openColonyRoutes lays its fords as sand; 2.0 lays every vertex it forded as
// boardwalk instead, so the crossing reads as a walkway over the slough. Returns the boardwalk
// vertices laid.
int openRoutes(GenerationWorld &game, const Layout &L, GenerationContext &context)
{
	GenerationMap &map = game.map;
	const Torus &t = L.t;
	const int n = t.size();
	std::vector<unsigned char> swimming(size_t(n), 0);
	for (int i = 0; i < n; ++i)
		swimming[i] = map.terrainProperties(map.vertexTerrainAt(t.remainderX(i), i / t.w)).swimmable;
	if (!openColonyRoutes(map, context, t,
						  StepCosts{kStepOpen, kStepDeposit, kStepEternal, kStepFord, -1}))
		return 0;
	std::vector<std::pair<int, int>> fords;
	for (int i = 0; i < n; ++i)
		if (swimming[i] && map.vertexTerrainAt(t.remainderX(i), i / t.w) == SAND)
			fords.emplace_back(t.remainderX(i), i / t.w);
	if (!fords.empty())
		map.paintVertices(fords, BOARDWALK, false);
	return int(fords.size());
}

// How far each colony walks from its swarm to the nearest stone over the ground (deposits that can
// be cleared do not block; stone, fruit, water and thicket do): telemetry only.
void measureStone(const GenerationMap &map, const Layout &L, GenerationContext &context)
{
	const Torus &t = L.t;
	const int n = t.size();
	std::vector<unsigned char> open(size_t(n), 0), stoneBeside(size_t(n), 0);
	for (int i = 0; i < n; ++i)
	{
		const int x = t.remainderX(i), y = i / t.w;
		const bool stone = map.isResource(x, y) && map.getResource(x, y).type == STONE;
		const bool eternal = map.isResource(x, y) && permanentResourceBarrier(map, size_t(i));
		open[i] = map.terrainPropertiesAt(x, y).walkable && !eternal;
		if (stone)
			for (int dy = -1; dy <= 1; ++dy)
				for (int dx = -1; dx <= 1; ++dx)
					stoneBeside[t.at(x + dx, y + dy)] = 1;
	}
	int least = INT_MAX, most = -1;
	for (int team = 0; team < L.g.teams; ++team)
	{
		std::vector<unsigned char> swarm(size_t(n), 0);
		for (int dy = 0; dy < 4; ++dy)
			for (int dx = 0; dx < 4; ++dx)
				swarm[t.at(context.bootX[team] + dx, context.bootY[team] + dy)] = 1;
		const std::vector<int> steps = stepsFrom(t, swarm, open);
		int best = INT_MAX;
		for (int i = 0; i < n; ++i)
			if (stoneBeside[i] && open[i] && steps[i] >= 0)
				best = std::min(best, steps[i] + 1);
		const int walk = best == INT_MAX ? -1 : best;
		context.telemetry.measure("everglades.stone.walk", walk, team);
		least = std::min(least, walk);
		most = std::max(most, walk);
	}
	context.telemetry.measure("everglades.stone.walk-spread", most - least);
}

bool generate(GenerationWorld &game, GenerationContext &context)
{
	context.stage = "glades layout";
	const EvergladesOptions o(context.request);
	GenerationMap &map = game.map;
	const int teams = context.request.nbTeams;
	map.fillTerrain(GRASS);
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

	context.stage = "glades terrain";
	const int seasonIndex = seasonOf(o);
	writeVertices(map, composeLooks(L.terrain, lookSketch(L, seasonIndex, context)));
	context.telemetry.choice("everglades.season", kSeasons[size_t(seasonIndex)].name);
	context.telemetry.measure("everglades.deep.vertices", L.deepVertices);
	context.telemetry.measure("everglades.mudflat.vertices", L.mudVertices);
	context.telemetry.measure("everglades.moss.vertices", L.mossVertices);
	context.telemetry.measure("everglades.hammocks.wanted", L.hammocksWanted);
	context.telemetry.measure("everglades.hammocks.placed", L.hammocksPlaced);
	context.telemetry.measure("everglades.hammocks.dropped", L.hammocksDropped);
	context.telemetry.measure("everglades.hammocks.thicket-vertices", L.thicketVertices);
	if (L.hammocksPlaced < L.hammocksWanted)
		context.telemetry.fallback("everglades.hammocks.short",
								   "Fewer hammocks fitted the swamp than were wanted");

	context.stage = "glades colonies";
	const auto clearing = [&](int team)
	{
		std::vector<unsigned char> ground(n, 0);
		for (int i = 0; i < n; ++i)
			ground[i] = L.clearingOf[i] == team && map.terrainPropertiesAt(t.remainderX(i), i / t.w).buildable;
		return ground;
	};
	// The swarm stands just past the pond's beach on the far side from the map's centre, leaving
	// the rest of that side to build on; in a clearing too small for a pond it takes the middle.
	const auto anchor = [&](int team)
	{
		const Home &home = L.homes[team];
		const double a =
			std::atan2(double(t.offsetY(L.cy, home.y)), double(t.offsetX(L.cx, home.x)));
		// 4 tiles past the pond's fullest outline: beyond its beach, on grass the swarm can stand
		// on.
		const double back = L.g.pond > 0 ? L.g.pond * 1.3 + 4 : 0;
		return MapGeneratorPoint(home.x + int(std::lround(back * std::cos(a))) - 2,
								 home.y + int(std::lround(back * std::sin(a))) - 2);
	};
	if (!settleColonies(game, context, "glades-starts", clearing, anchor))
		return false;

	context.stage = "glades resources";
	furnishHomes(map, L, context);
	stockSwamp(map, L, context, o);
	seedAlgae(map, context, t, "glades-algae", o.algae, AlgaeBand::anyWater());
	secureStartingCrops(game, context, t);
	context.stage = "glades routes";
	context.telemetry.measure("everglades.routes.boardwalk-vertices", openRoutes(game, L, context));
	if (context.telemetry.enabled())
		measureStone(map, L, context);
	return true;
}

std::string validateRequest(const GenerationRequest &r)
{
	if (r.nbTeams < 1)
		return "Everglades need at least one colony.";
	if (!clearingsFit(geometryFor(r)))
		return "Too many colonies for this map; use a bigger map or fewer colonies.";
	return "";
}

bool waterGroup(TerrainType v)
{
	const TerrainGroup group = terrainTypeDefinition(v).group;
	return group == TerrainGroup::Water || group == TerrainGroup::DeepWater;
}

// Checked on the finished world: every clearing keeps its pond; no levee tile carries a deposit
// (nothing grows on sand, so nothing may start there); every deep vertex lies inside a shallow margin
// (or beside a boardwalk ford); every hammock tile is blocked and no hammock parts the ground round
// it; every vertex is the design's (mechanics and looks, boardwalk allowed on forded water); and
// colony 0 can walk to every other colony, with water, buildings and every resource blocking
// (openRoutes has already opened any way needed).
std::string validateWorld(const GenerationWorld &game, const GenerationContext &context)
{
	GenerationContext replay(context.request);
	const Layout L = design(context.request, replay);
	const GenerationMap &map = game.map;
	if (const std::string mismatch = designMismatch(L, map, "glades"); !mismatch.empty())
		return mismatch;
	const Torus &t = L.t;
	const int n = t.w * t.h, teams = context.request.nbTeams;
	for (int k = 0; k < teams && L.g.pond > 0; ++k)
	{
		bool pond = false;
		for (int dy = -2; dy <= 2 && !pond; ++dy)
			for (int dx = -2; dx <= 2 && !pond; ++dx)
				pond = map.terrainPropertiesAt(t.x(L.homes[k].x + dx), t.y(L.homes[k].y + dy)).swimmable;
		if (!pond)
			return "Colony " + std::to_string(k) + "'s clearing has lost its pond.";
	}
	std::vector<unsigned char> walkable(size_t(n), 0), hammockBlocked(size_t(n), 0);
	for (int i = 0; i < n; ++i)
	{
		const int x = t.remainderX(i), y = i / t.w;
		if (L.sand[i] && map.isResource(x, y))
			return "A deposit stands on the levee at (" + std::to_string(x) + ", " +
				   std::to_string(y) + ").";
		if (terrainTypeDefinition(map.vertexTerrainAt(x, y)).group == TerrainGroup::DeepWater)
			for (int dy = -1; dy <= 1; ++dy)
				for (int dx = -1; dx <= 1; ++dx)
				{
					const TerrainType v = map.vertexTerrainAt(t.x(x + dx), t.y(y + dy));
					if (!waterGroup(v) && terrainTypeDefinition(v).group != TerrainGroup::Paths)
						return "Deep water touches the bank at (" + std::to_string(x) + ", " +
							   std::to_string(y) + ").";
				}
		const auto &rules = map.terrainPropertiesAt(x, y);
		walkable[i] = rules.walkable;
		if (L.hammock[i] && !rules.walkable)
			hammockBlocked[i] = 1;
	}
	const std::vector<unsigned char> blocked = knotBlocked(t, L.hammock);
	for (int i = 0; i < n; ++i)
		if (blocked[i] && !hammockBlocked[i])
			return "A hammock has been opened at (" + std::to_string(t.remainderX(i)) + ", " +
				   std::to_string(i / t.w) + ").";
	std::vector<int> label;
	if (const int parting = partingGroup(t, walkable, hammockBlocked, label); parting >= 0)
		for (int i = 0; i < n; ++i)
			if (label[i] == parting)
				return "A hammock cuts ground off at (" + std::to_string(t.remainderX(i)) + ", " +
					   std::to_string(i / t.w) + ").";
	// Last, after the checks with specific diagnostics: the drawn map is exactly the design, mechanics
	// and looks, except where openRoutes forded water with boardwalk.
	{
		const EvergladesOptions o(context.request);
		const TerrainSketch drawn = composeLooks(L.terrain, lookSketch(L, seasonOf(o), replay));
		for (int i = 0; i < n; ++i)
		{
			const int x = t.remainderX(i), y = i / t.w;
			const TerrainType v = map.vertexTerrainAt(x, y);
			if (v != TerrainType(drawn[i]) && !(v == BOARDWALK && waterGroup(TerrainType(drawn[i]))))
				return "The glades terrain no longer matches its design at (" + std::to_string(x) + ", " +
					   std::to_string(y) + ").";
		}
	}
	return walkFromFirstColony(map, teams, "the glades", "through the glades").error;
}
} // namespace

EvergladesOptions::EvergladesOptions(const GenerationRequest &r)
	: poolSpacing(r.option("pool-spacing")), poolSize(r.option("pool-size")),
	  sloughs(r.option("sloughs")), clearingSize(r.option("clearing-size")),
	  levee(r.option("levee")), mudflats(r.option("mudflats")), hammocks(r.option("hammocks")),
	  season(r.option("season")), wheat(r.option("wheat-amount")),
	  wood(r.option("wood-amount")), stone(r.option("stone-amount")),
	  algae(r.option("algae-amount")), fruit(r.option("fruit-amount"))
{
	if (season < 1 || season > 2)
		season = GenerationContext::choiceFromSeed(r.seed, "everglades/season", {1, 2});
}

GeneratorDefinition evergladesDefinition()
{
	return {
		"everglades",
		19,
		"Everglades",
		4,
		false,
		// Pool spacing and pool size in tiles; sloughs is the share of pools grown and stretched
		// into sloughs; the clearings' radius in tiles; the levee is the share of each clearing's
		// ring that is sand.
		{GeneratorControl{"pool-spacing", "Pool spacing", 8, 20, 1, 15, ControlGroup::Terrain}
			 .withSearchRange(12, 18),
		 GeneratorControl{"pool-size", "Pool size", 2, 7, 1, 4, ControlGroup::Terrain}
			 .withSearchRange(3, 5),
		 GeneratorControl{"sloughs", "Sloughs", 0, 50, 5, 15, ControlGroup::Terrain}
			 .withSearchRange(5, 30),
		 GeneratorControl{"clearing-size", "Clearing size", 12, 18, 1, 13, ControlGroup::Layout}
			 .withSearchRange(13, 18),
		 GeneratorControl{"levee", "Levee", 0, 100, 10, 70, ControlGroup::Layout}.withSearchRange(
			 50, 100),
		 // 2.0: the share of the swamp's pool banks that is mud rather than sand (banks near a
		 // clearing stay sand): mud keeps the lanes open but stops inhibiting the grass beside
		 // them, so the swamp regrows harder.
		 GeneratorControl{"mudflats", "Mudflats", 0, 100, 10, 50, ControlGroup::Terrain}
			 .withSearchRange(30, 70),
		 // Thicket hammocks (tree islands, impassable) per 128x128 tiles of map.
		 GeneratorControl{"hammocks", "Hammocks", 0, 8, 1, 3, ControlGroup::Terrain}
			 .withSearchRange(1, 5),
		 // Looks only: lush sawgrass and wet mud, or tawny sawgrass, dried mud and peat.
		 GeneratorControl::choice("season", "Season", {"Random", "Wet", "Dry"}, 0,
								  ControlGroup::Terrain)
			 .withSearchValues({1, 2}),
		 // The swamp's standing wood and wheat, its outcrops and groves, and the pools' algae;
		 // every home's kit is unscaled.
		 GeneratorControl::percentage("wheat-amount", "Wheat amount"),
		 GeneratorControl::percentage("wood-amount", "Wood amount"),
		 GeneratorControl::percentage("stone-amount", "Stone amount"),
		 GeneratorControl::percentage("algae-amount", "Algae amount"),
		 GeneratorControl::percentage("fruit-amount", "Fruit amount")},
		generate,
		true,
		validateRequest,
		validateWorld,
		// "style:wide-open" (from the code's own "sprawling" framing) was wrong at a glance:
		// FEEDBACK 2026-09-17 asked for tags read off the rendered map, and this one is nearly
		// wall-to-wall pools and sloughs, not open ground - dense and growth-pressured, not
		// wide-open. Left with no style tag rather than a second guess; see the skill's own
		// "A growth-pressure map can intentionally creep into bases" characterization.
		{"terrain:natural", "feature:swamp", "feature:lakes"}};
}
