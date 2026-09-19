// SPDX-License-Identifier: GPL-3.0-or-later
#include "EvenGroundGenerator.h"
#include "Contact.h"
#include "Game.h"
#include "GenerationContext.h"
#include "Grid.h"
#include "LatticeNoise.h"
#include "Morphology.h"
#include "Pipeline.h"
#include "Planting.h"
#include "Points.h"
#include "Regions.h"
#include "Resources.h"
#include "Roads.h"
#include "Settlements.h"
#include "Solve.h"
#include "Sketch.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>
using namespace MapGeneration;

// Even Ground: a country with no drawn shape at all. Every other landscape here decides what it
// looks like - a moat, a braid, a ring of forts - and then works to make that shape fair. This one
// starts from what the finished map must be true of, and searches for ground that satisfies it: the
// colonies' catchments of each crop must come out equal, everyone must be able to walk to everyone,
// every colony must have clear ground to build on, and the ways between neighbours must be about as
// tight as the player asked for. The terrain is whatever meets those conditions, so two seeds at
// the same settings look nothing alike and play to the same balance.
//
// WHY THE RANDOM STREAMS STILL SAY "equilibrium". This map was called Equilibrium until shortly
// before it merged. A stream's name is mixed into every draw taken from it, so renaming
// "even-ground-shape" and its siblings would move every map this generator has ever made, and the
// golden rows, the figures in EVEN_GROUND.md and the games already played would all describe maps
// that no longer exist. Stream names are internal and never shown, so they stay and the public name
// changed around them. Brief is given the two names separately for the same reason.
//
// HOW IT IS SOLVED. A map is far too big to search tile by tile, so the search runs on a coarse
// lattice of cells (8 to 32 tiles a side), each of which is water, open ground, or a field of one
// crop. Simulated annealing over that lattice, in two passes:
//
//   * the shape pass swaps a water cell with a land cell, so the number of water cells never
//     changes and only their arrangement is searched. It is scored on equal reachable land per
//     colony, the width of the tightest way between neighbours, clear ground near every home, the
//     water reading as a few lakes rather than a rash of puddles, and a severe penalty for cutting
//     anyone off.
//   * the stock pass freezes the shape and swaps the crops between land cells, so the number of
//     cells under each crop never changes and only their distribution is searched. It is scored on
//     two things: every colony's decayed catchment of wheat, of wood and of stone equal, and the
//     worst-served colony's catchment as large as it can be made.
//
// Both passes preserve their own budget and search only over arrangement, so a slider sets a
// quantity the search may place but may not argue with. That exactness is on the lattice: the
// finished map carries a little less water than asked for, because painting it lays a beach along
// every bank and a beach is sand, not water. The shape pass is the expensive one - a move changes
// what everyone can walk to, so every colony's walk is measured again - and the stock pass is
// nearly free, because moving a crop between two cells changes each colony's catchment by two
// terms that are already known. So the pass that actually buys the fairness is the one that can
// afford tens of thousands of moves.
//
// The solved lattice is then painted onto the tiles through a noise warp, so cell borders wander
// and the result reads as country rather than as a grid of squares.
//
// GAME RULES IT LEANS ON (docs/map-generators/GAME_RULES_FOR_MAP_DESIGN.md):
// - Resources block ground units and buildings alike, so a crop cell is planted to a share of its
//   ground rather than solid: the fields stay walkable and gatherable from inside.
// - Stone never runs out, so a stone cell is a permanent strategic site, and is planted thinnest of
//   the three: a solid one would be a wall.
// - Water blocks walking until a colony can swim, which is what the tightness slider actually
//   controls: the solver is choosing where the water pinches the routes between neighbours.
// - Grass may not touch water, so layBeaches runs before any deposit is placed.
//
// FAIRNESS. Not by symmetry and not by construction: by measurement of the thing that matters, and
// a search that minimises its spread. The catchment is a walking-distance-decayed sum, so ground a
// colony cannot reach counts for nothing and distant ground counts for little - which also means
// the solver is happy to leave surplus in the middle where it is nobody's, since ground far from
// everyone costs little fairness to leave rich. The contested middles on this map are an emergent
// consequence of the objective, not a drawn feature. What is NOT promised is equal room to expand
// into or equal defensibility; the tournament is the check on those.
namespace
{

// ---------------------------------------------------------------------------
// The lattice the solver searches
// ---------------------------------------------------------------------------

enum CellKind : unsigned char
{
	kWater,
	kOpen,
	kWheat,
	kWood,
	kStone,
	kCellKinds
};
// The crops, as the run of kinds from kWheat: what the stock pass shuffles.
constexpr int kStockKinds = 3;

// A colony's interest in a cell falls away with the walk to it: ground this many cells off counts
// for 1/e of ground at the doorstep. Deliberately short - about a colony's own working
// neighbourhood - because that is what leaves the middles nearly free: ground far from everybody
// costs almost nothing in fairness to leave rich, so the search is content to pile surplus where it
// is nobody's. Lengthen this and every colony's catchment covers the whole map, the only
// arrangement that satisfies it is an even one, and the map comes out the same everywhere.
constexpr double kCatchmentDecay = 4.0;
// How far from home the solver looks for building room, and how many clear cells it wants there.
constexpr int kRoomWalk = 3;
constexpr int kRoomCells = 8;
// What one colony cut off from the rest costs: more than every other term together can pay for.
constexpr double kSeveredCost = 20.0;
// How far out a colony's approach is measured: the ring a rival arrives across. Equalising how much
// open ground lies on it is the map's only statement about defensibility, which is otherwise the
// largest thing neither of these maps looks at - a colony walled in by its own lakes is far cheaper
// to hold than one in open country, however equal their crops.
constexpr int kApproachWalk = 6;
constexpr double kApproachWeight = 1.0;
// How the shape pass weighs its targets against each other. Room outweighs tightness because a
// colony with nowhere to build is unplayable whereas a route a little wider than asked is not.
constexpr double kRoomWeight = 2.0;
constexpr double kPassWeight = 1.0;
// How long a shoreline the water should have for its area, and how heavily that weighs. Scattered
// single cells have the longest shore there is (4 sides each) and a single round sea the shortest,
// so asking for about one shore cell per water cell asks for a handful of lobed lakes. This target
// is not a matter of taste: without it the search discovers that a rash of puddles satisfies every
// other target just as well as lakes do, and the map comes out as a grid of ponds.
// The target is what the ground can actually reach, not what would be tidiest. Set to 0.8 it was
// never met - at a tenth of the map under water the shoreline settles near 1.3 whatever the search
// does - and an unreachable target is not a target but a constant: it contributed about seventy per
// cent of the shape cost while never shrinking, which left the terms that could be improved with
// almost no say in where the search went.
constexpr double kShoreLeast = 0.85, kShoreMost = 2.10;
/// How far a seed's own weather may swing what the sliders asked for: how wet the country is, and
/// how much of it is under crop. The slider stays the middle of the band and its direction always
/// holds; where in the band a seed falls is the difference between a lake country and dry downland.
constexpr double kWetLeast = 0.35, kWetMost = 2.20;
constexpr double kGreenLeast = 0.55, kGreenMost = 1.70;
constexpr double kShoreWeight = 1.5;
// How many separate bodies that water should form, drawn per seed from this range, and how heavily
// that weighs. A short shoreline alone still allows a hundred tidy little lakes spread evenly over
// the map, which is the arrangement equal catchments actually prefer - every colony's surroundings
// identical - and it makes every seed look like every other. Asking for a few bodies instead forces
// the water into seas and lake districts big enough to give a map a face, and drawing the number
// per seed is what makes one seed an inland sea and the next a chain of lakes.
constexpr int kBodiesLeast = 1, kBodiesMost = 8;
constexpr double kBodiesWeight = 1.2;
// The heaviest the balance slider can make equal catchments weigh.
constexpr double kBalanceWeight = 3.0;
// How heavily the worst-served colony's own catchment weighs against the spread between them all.
constexpr double kLevelWeight = 2.0;
// How much of a crop cell's cardinal neighbourhood should be the same crop, and how heavily that
// weighs. Without a word about it the search scatters the crops as finely as the fairness terms
// allow, because a fine scatter is the easiest way to give everybody the same catchment - and the
// country comes out as confetti with no field or wood anywhere a player could point at. Asking for
// about half of each crop cell's neighbours to match gathers the same budget into belts with gaps
// between them. Asking for all of it would build one wall per crop, which is why this is a target
// and not something to maximise.
constexpr double kFieldsLeast = 0.18, kFieldsMost = 0.78;
constexpr double kFieldsWeight = 2.0;
// How far a tile's cell is looked up from, as a share of a cell, and how far a cell's centre is
// jostled off the lattice. Both kept well under half a cell, so cardinal neighbours on the lattice
// still touch on the ground and the arrangement the search settled on survives being painted.
constexpr double kBorderWander = 0.35;
constexpr double kCentreJostle = 0.25;
// The share of a crop cell's ground actually planted. Fields keep gaps to walk and gather through,
// and stone is thinnest because it is a permanent wall. These are fixed: the amount sliders scale
// how many cells hold a crop, which is the one place they apply.
//
// How many cells carry a crop and how densely each is planted are deliberately separate numbers,
// because they buy different things. The cell count is the resolution the search has to equalise
// with - it is the number of pieces it has to share out - and the cover is how much ground the
// crops take away from building. Trading cells for cover (11 per cent of cells at 55 per cent
// cover)
// bought building room at the cost of fairness, 0.913 down to 0.879 over 16 seeds; many thin fields
// gives both, because the same tonnage of deposit is cut into three times as many pieces.
constexpr int kWheatCover = 32, kWoodCover = 26, kStoneCover = 10;
// The share of land cells each crop takes at an amount of 100: the pieces the search has to share
// out between the colonies.
constexpr int kWheatCells = 18, kWoodCells = 16, kStoneCells = 6;
// No more of the land than this may be under crops however high the amounts go, so a map at maximum
// abundance still has open ground to cross and to build on.
constexpr int kStockedCeiling = 70;

// The annealing schedule, by effort. The stock pass gets far more moves than the shape pass because
// each of its moves costs a couple of dozen operations rather than a walk of the whole lattice.
constexpr int kShapeMoves[3] = {900, 2400, 6000};
constexpr int kStockMoves[3] = {6000, 20000, 50000};
constexpr double kShapeHeat[2] = {0.6, 0.005};
constexpr double kStockHeat[2] = {0.05, 0.0005};

// The fewest cells the search wants along a map's short side, and the fewest per colony: below
// either there is not enough to search over for the result to mean anything.
constexpr int kLeastRows = 6;
constexpr int kLeastCellsPerColony = 12;

struct Lattice
{
	int w = 0, h = 0, tiles = 0; // cells across and down, and the tiles down a cell's side
	int size() const { return w * h; }
	Torus torus() const { return Torus{w, h}; }
};

/// The largest power of two at or below `value`, and at least one.
int floorPowerOfTwo(int value)
{
	int power = 1;
	while (power * 2 <= value)
		power *= 2;
	return power;
}

/// The lattice a map of this size is searched on: cells big enough that a beach does not consume
/// one, and few enough that a walk of all of them is cheap, which lands at roughly 16 to 32 cells
/// along the map's longer side.
///
/// The second term is what makes a long thin map work. Sizing the cells off the long side alone
/// gives a 64 by 512 map four rows of them, too few to search in two dimensions, so the short side
/// gets a say and keeps at least kLeastRows of them - which is the same number the request check
/// insists on, deliberately: the two disagreeing is how a perfectly reasonable map ends up refused
/// for being "too small" when it is merely narrow.
Lattice latticeFor(int width, int height)
{
	const int wide = floorPowerOfTwo(std::max(1, std::max(width, height) / 24));
	const int narrow = floorPowerOfTwo(std::max(1, std::min(width, height) / kLeastRows));
	const int tiles = std::clamp(std::min(wide, narrow), 8, 32);
	return {width / tiles, height / tiles, tiles};
}

/// Everything the solver carries between its two passes.
struct Solved
{
	Lattice lat;
	std::vector<unsigned char> kind;   // one CellKind per cell
	std::vector<unsigned char> pinned; // homes and their doorsteps: never water, never planted
	std::vector<int> home;             // each colony's home cell, in colony order
	// What the final state came out at, for telemetry: the residual of each target, and the pass
	// width the shape pass settled on.
	double reachSpread = 0, passWidth = 0, roomShort = 0, shoreRatio = 0;
	Objective cost, stockCost;
	double stockSpreadBefore = 0, stockSpreadAfter = 0, fields = 0;
	int severed = 0, bodies = 0;
	SolveReport shape, stock;
};

/// A cardinal walk over the lattice's land from one cell, into `steps` (-1 where it never lands).
/// `queue` is the caller's scratch, so the solver's inner loop allocates nothing.
///
/// Not Grid.h's floods: those are eight-connected over map tiles, and two land cells touching only
/// at a corner would promise a route that the tiles those cells paint do not have. Four cardinal
/// steps here is the conservative reading, and it is the one the painted map can keep.
void walkLand(const Torus &t, const std::vector<unsigned char> &land, int from,
			  std::vector<int> &steps, std::vector<int> &queue)
{
	std::fill(steps.begin(), steps.end(), -1);
	queue.clear();
	steps[from] = 0;
	queue.push_back(from);
	for (size_t head = 0; head < queue.size(); ++head)
	{
		const int i = queue[head], x = i % t.w, y = i / t.w;
		// Read once. steps[i] cannot change under this loop - a cell is only ever written when it is
		// still unvisited, and this one was dequeued - but the writes below are to the same array,
		// so the compiler has to assume they may alias and reloads it four times otherwise.
		const int next_step = steps[i] + 1;
		for (const auto &step : kCardinalSteps)
		{
			const int next = t.at(x + step[0], y + step[1]);
			if (land[next] && steps[next] < 0)
			{
				steps[next] = next_step;
				queue.push_back(next);
			}
		}
	}
}

// ---------------------------------------------------------------------------
// The shape pass: where the water goes
// ---------------------------------------------------------------------------

/// Everything the shape pass measures about one arrangement of water, and the scratch it measures
/// it with. Held together so a rejected move can be undone without measuring anything twice.
struct Shape
{
	std::vector<unsigned char> land;
	std::vector<std::vector<int>> walk; // each colony's steps over land
	std::vector<double> decay;          // e^(-steps / kCatchmentDecay), by step count
	std::vector<int> queue;             // walkLand's scratch
	// Scratch for scoreShape, which runs once per proposal: the clearance field every colony's walk
	// out is measured over, the goal mask those walks aim at, and the water mask its bodies are
	// counted in. Kept here rather than declared in the scoring function, which allocated three
	// fresh map-sized buffers on every one of thousands of proposals.
	std::vector<int> room;
	std::vector<unsigned char> others, wetMask;
	double reachSpread = 0, passWidth = 0, roomShort = 0, shoreRatio = 0, approachSpread = 0;
	int severed = 0, bodies = 0;
	Objective cost;
};

/// Walks every colony over the land and scores the arrangement: equal reachable land, a way between
/// neighbours about as wide as asked, clear ground near every home, and nobody cut off.
double scoreShape(Shape &s, const Solved &solved, const Torus &t, double balance, double wantWidth,
				  int roomWanted, int bodiesWanted, double shoreWanted,
				  const Brief &wants)
{
	const int teams = int(solved.home.size());
	for (int k = 0; k < teams; ++k)
		walkLand(t, s.land, solved.home[k], s.walk[k], s.queue);

	// Equal reachable land, equal building room at home, and equal exposure. The last of those is
	// how much open ground lies on the ring a rival arrives across: a colony whose approach is
	// mostly water is far cheaper to hold than one standing in the open, and nothing else here
	// measures that. It costs nothing extra to take, being another sum over the same walk field.
	std::vector<double> reach(teams, 0.0), room(teams, 0.0), approach(teams, 0.0);
	for (int k = 0; k < teams; ++k)
	{
		// Walked once into locals: the three sums are doubles and the walk an int array, but the
		// compiler cannot prove the vectors do not overlap, so written straight to reach[k] this
		// reloads the step count and all three totals on every cell.
		const std::vector<int> &walk = s.walk[k];
		double reached = 0, roomy = 0, exposed = 0;
		for (int i = 0; i < t.size(); ++i)
		{
			const int steps = walk[i];
			if (steps < 0)
				continue;
			reached += s.decay[steps];
			roomy += steps <= kRoomWalk && solved.kind[i] == kOpen;
			exposed += steps == kApproachWalk;
		}
		reach[k] = reached;
		room[k] = roomy;
		approach[k] = exposed;
	}

	s.severed = 0;
	for (int k = 1; k < teams; ++k)
		s.severed += s.walk[0][solved.home[k]] < 0;

	// The tightest way out of each colony: the widest walk it has to whichever other colony is
	// easiest to reach, as a passage width in cells. That is the front the player will fight on.
	double widthTotal = 0;
	if (teams > 1)
	{
		// One clearance field for all of them: it depends only on the land, so computing it inside
		// the loop measured the same thing once per colony (Morphology.h).
		s.room = clearance(t, s.land);
		s.others.assign(t.size(), 0);
		std::vector<int> source(1);
		for (int k = 0; k < teams; ++k)
		{
			for (int j = 0; j < teams; ++j)
				s.others[solved.home[j]] = j != k;
			source[0] = solved.home[k];
			const int clear = widestWalkClearance(t, s.land, s.room, source, s.others);
			widthTotal += clear > 0 ? 2 * clear - 1 : 0;
			for (int j = 0; j < teams; ++j)
				s.others[solved.home[j]] = 0;
		}
	}
	s.passWidth = teams > 1 ? widthTotal / double(teams) : wantWidth;

	// The shoreline the water has for its area: how much it reads as lakes rather than as puddles.
	int shore = 0, wet = 0;
	for (int i = 0; i < t.size(); ++i)
	{
		if (s.land[i])
			continue;
		++wet;
		const int x = i % t.w, y = i / t.w;
		for (const auto &step : kCardinalSteps)
			shore += s.land[t.at(x + step[0], y + step[1])] != 0;
	}
	s.shoreRatio = wet > 0 ? double(shore) / double(wet) : shoreWanted;

	// How many separate bodies that water forms. Cardinal, like everything else the search reasons
	// about, so two lakes touching at a corner count as the two lakes the painted map will show.
	s.wetMask.resize(t.size());
	for (int i = 0; i < t.size(); ++i)
		s.wetMask[i] = !s.land[i];
	s.bodies = 0;
	for (const int part : connectedRegions(s.wetMask, t.w, t.h, true, GridNeighbors::Cardinal))
		s.bodies = std::max(s.bodies, part + 1);

	s.reachSpread = imbalance(reach);
	s.approachSpread = imbalance(approach);
	s.roomShort = 0;
	for (const double had : room)
		s.roomShort += std::max(0.0, (roomWanted - had) / double(roomWanted));
	s.roomShort /= double(teams);

	s.cost = Objective()
				 .add("reach", balance, s.reachSpread)
				 .add("approach", wants.weight("approach", kApproachWeight), s.approachSpread)
				 .add("pass", wants.weight("pass", kPassWeight),
					  std::abs(s.passWidth - wantWidth) / std::max(1.0, wantWidth))
				 .add("shore", kShoreWeight, std::abs(s.shoreRatio - shoreWanted) / shoreWanted)
				 .add("bodies", wants.weight("bodies", kBodiesWeight),
					  std::abs(s.bodies - bodiesWanted) / double(bodiesWanted))
				 .add("room", kRoomWeight, s.roomShort)
				 .add("severed", kSeveredCost, s.severed);
	return s.cost.total();
}

/// Anneals the water's arrangement, swapping a water cell with a land cell so the water the player
/// asked for stays exactly what it is. Returns the moves taken.
SolveReport annealShape(Solved &solved, GenerationContext &context, double balance,
						double wantWidth, int bodiesWanted, double shoreWanted,
						const Brief &wants, int moves)
{
	const Torus t = solved.lat.torus();
	const int cells = solved.lat.size(), teams = int(solved.home.size());
	Shape s;
	s.land.assign(cells, 0);
	for (int i = 0; i < cells; ++i)
		s.land[i] = solved.kind[i] != kWater;
	s.walk.assign(teams, std::vector<int>(cells, -1));
	s.decay.resize(cells + 1);
	for (int step = 0; step <= cells; ++step)
		s.decay[step] = std::exp(-double(step) / kCatchmentDecay);
	// A tiny lattice cannot spare kRoomCells per colony; ask for what there is room to ask for.
	const int roomWanted = std::max(2, std::min(kRoomCells, cells / std::max(1, 3 * teams)));

	// One water cell to dry out and one land cell to drown, drawn by rejection.
	//
	// The land cell is looked for against an existing shore three times out of four. Drowning open
	// country makes a new puddle, and the targets ask for a few lakes, so a search spending its
	// moves uniformly never gets there: at ten per cent water it left eleven separate bodies when it
	// had been asked for three. Moving a shoreline instead consolidates. The remaining quarter is
	// drawn from anywhere, which is what still lets water migrate across the map rather than only
	// growing where the first noise put it.
	int wet = -1, dry = -1;
	unsigned char wasDry = kOpen;
	std::vector<unsigned char> best = solved.kind;
	const SolveReport run = anneal(
		Anneal{moves, kShapeHeat[0], kShapeHeat[1], "even-ground-shape"}, context,
		[&]
		{
			const bool alongShore = context.bounded("even-ground-shape", 4) != 0;
			const auto onShore = [&](int i)
			{
				const int x = i % t.w, y = i / t.w;
				for (const auto &step : kCardinalSteps)
					if (!s.land[t.at(x + step[0], y + step[1])])
						return true;
				return false;
			};
			wet = dry = -1;
			for (int attempt = 0; attempt < 48 && (wet < 0 || dry < 0); ++attempt)
			{
				const int i = int(context.bounded("even-ground-shape", std::uint32_t(cells)));
				if (solved.pinned[i])
					continue;
				if (!s.land[i])
				{
					if (wet < 0)
						wet = i;
				}
				else if (dry < 0 && (!alongShore || onShore(i)))
					dry = i;
			}
			if (wet < 0 || dry < 0)
				return false;
			wasDry = solved.kind[dry];
			s.land[wet] = 1;
			s.land[dry] = 0;
			solved.kind[wet] = kOpen;
			solved.kind[dry] = kWater;
			return true;
		},
		[&] {
			return scoreShape(s, solved, t, balance, wantWidth, roomWanted, bodiesWanted,
							  shoreWanted, wants);
		},
		[&]
		{
			s.land[wet] = 0;
			s.land[dry] = 1;
			solved.kind[wet] = kWater;
			solved.kind[dry] = wasDry;
		},
		[&] { best = solved.kind; }, [&] { solved.kind = best; });
	for (int i = 0; i < cells; ++i)
		s.land[i] = solved.kind[i] != kWater;
	// The walks the stock pass inherits must match the shape it inherits, so measure the state the
	// search actually ended on rather than trusting the last move's scratch.
	scoreShape(s, solved, t, balance, wantWidth, roomWanted, bodiesWanted, shoreWanted, wants);
	solved.reachSpread = s.reachSpread;
	solved.passWidth = s.passWidth;
	solved.roomShort = s.roomShort;
	solved.shoreRatio = s.shoreRatio;
	solved.severed = s.severed;
	solved.bodies = s.bodies;
	solved.cost = s.cost;
	return run;
}

// ---------------------------------------------------------------------------
// The stock pass: where the crops go
// ---------------------------------------------------------------------------

/// Every colony's decayed catchment of each crop.
using Catchments = std::vector<std::array<double, kStockKinds>>;

/// How unequally the crops are shared: the plain spread, for reporting.
double stockSpread(const Catchments &had, int teams)
{
	double total = 0;
	std::vector<double> shares(teams);
	for (int r = 0; r < kStockKinds; ++r)
	{
		for (int k = 0; k < teams; ++k)
			shares[k] = had[k][r];
		total += imbalance(shares);
	}
	return total / kStockKinds;
}

/// What the stock pass actually minimises. Two things, not one: how unequally each crop is shared,
/// and how badly off the worst-served colony is in absolute terms.
///
/// Spread alone is satisfied by everyone being equally poor, and that is exactly what the search
/// finds when it is all you ask for - crops drift to wherever they are equidistant from everybody,
/// which is as far from everybody as it is possible to be, and every colony ends up with a fair
/// share of nothing. Rewarding the least-served colony's actual catchment pulls them back in. The
/// two together ask for what a fair map really is: everyone well served, and served alike. The
/// level is divided by the number of cells of that crop, so it stays comparable between crops and
/// across amount settings.
Objective stockObjective(const Catchments &had, int teams,
						 const std::array<int, kStockKinds> &counts, double clustering,
						 double fieldsWanted, double fieldsWeight)
{
	double spread = 0, level = 0;
	std::vector<double> shares(teams);
	for (int r = 0; r < kStockKinds; ++r)
	{
		double least = -1;
		for (int k = 0; k < teams; ++k)
		{
			shares[k] = had[k][r];
			least = least < 0 ? shares[k] : std::min(least, shares[k]);
		}
		spread += imbalance(shares);
		level += std::max(0.0, least) / std::max(1, counts[r]);
	}
	return Objective()
		.add("spread", 1.0 / kStockKinds, spread)
		.add("level", -kLevelWeight / kStockKinds, level)
		.add("fields", fieldsWeight, std::abs(clustering - fieldsWanted) / fieldsWanted);
}

double stockCost(const Catchments &had, int teams, const std::array<int, kStockKinds> &counts,
				 double clustering, double fieldsWanted, double fieldsWeight)
{
	return stockObjective(had, teams, counts, clustering, fieldsWanted, fieldsWeight).total();
}

/// Anneals which land cells hold which crop, swapping the contents of two cells so the number of
/// cells of each crop stays exactly what the amount sliders asked for. Every colony's walk is
/// already known and does not change, so a move costs two terms per colony rather than a fresh walk
/// of the lattice: this is the pass that can afford the moves that actually equalise the map.
SolveReport annealStock(Solved &solved, GenerationContext &context,
						const std::vector<std::vector<int>> &walk,
						const std::vector<double> &decay, int moves, double &spreadBefore,
						double &spreadAfter, double &stockShape, Objective &stockTerms,
						double fieldsWanted, double fieldsWeight)
{
	const int cells = solved.lat.size(), teams = int(solved.home.size());
	const auto worth = [&](int team, int cell)
	{ return walk[team][cell] < 0 ? 0.0 : decay[walk[team][cell]]; };

	Catchments had(teams, {0.0, 0.0, 0.0});
	std::array<int, kStockKinds> counts{};
	for (int i = 0; i < cells; ++i)
		if (solved.kind[i] >= kWheat)
			++counts[solved.kind[i] - kWheat];
	for (int k = 0; k < teams; ++k)
		for (int i = 0; i < cells; ++i)
			if (solved.kind[i] >= kWheat)
				had[k][solved.kind[i] - kWheat] += worth(k, i);
	spreadBefore = stockSpread(had, teams);

	// Swapping the contents of two cells is its own inverse, catchments and all: cell a gives up
	// what it was worth to each colony and takes on what b was worth, and b the other way round, so
	// running it twice puts the lattice and the sums back exactly as they were. That is what lets
	// the refusal path be the same call as the proposal.
	// How much of the crops' neighbourhood is the same crop, kept as a running count so a swap stays
	// a couple of dozen operations. Each matching edge is counted at both ends, so the change at
	// the swapped cells must be doubled to include their neighbours. The cells hold different
	// kinds, so an edge between them never matches, before or after the swap.
	const Torus lattice = solved.lat.torus();
	const auto matching = [&](int cell)
	{
		if (solved.kind[cell] < kWheat)
			return 0;
		int same = 0;
		const int x = cell % lattice.w, y = cell / lattice.w;
		for (const auto &step : kCardinalSteps)
			same += solved.kind[lattice.at(x + step[0], y + step[1])] == solved.kind[cell];
		return same;
	};
	int crops = 0, matched = 0;
	for (int i = 0; i < cells; ++i)
		if (solved.kind[i] >= kWheat)
		{
			++crops;
			matched += matching(i);
		}
	const auto clustering = [&]
	{ return crops > 0 ? double(matched) / (4.0 * crops) : fieldsWanted; };

	int a = -1, b = -1;
	std::vector<unsigned char> best = solved.kind;
	const auto swapStock = [&](int one, int other)
	{
		matched -= 2 * (matching(one) + matching(other));
		const unsigned char kindOne = solved.kind[one], kindOther = solved.kind[other];
		for (int k = 0; k < teams; ++k)
		{
			if (kindOne >= kWheat)
				had[k][kindOne - kWheat] += worth(k, other) - worth(k, one);
			if (kindOther >= kWheat)
				had[k][kindOther - kWheat] += worth(k, one) - worth(k, other);
		}
		solved.kind[one] = kindOther;
		solved.kind[other] = kindOne;
		matched += 2 * (matching(one) + matching(other));
	};
	const SolveReport run = anneal(
		Anneal{moves, kStockHeat[0], kStockHeat[1], "even-ground-stock"}, context,
		[&]
		{
			// Two land cells the design does not pin, holding different things: a swap between two
			// cells holding the same thing is not a move.
			a = b = -1;
			for (int attempt = 0; attempt < 32 && b < 0; ++attempt)
			{
				const int i = int(context.bounded("even-ground-stock", std::uint32_t(cells)));
				if (solved.pinned[i] || solved.kind[i] == kWater)
					continue;
				if (a < 0)
					a = i;
				else if (solved.kind[i] != solved.kind[a])
					b = i;
			}
			if (b < 0)
				return false;
			swapStock(a, b);
			return true;
		},
		[&] { return stockCost(had, teams, counts, clustering(), fieldsWanted, fieldsWeight); },
		[&] { swapStock(a, b); },
		[&] { best = solved.kind; }, [&] { solved.kind = best; });
	// The catchments must match whatever arrangement the run ended on, best-kept or not.
	for (int k = 0; k < teams; ++k)
	{
		had[k] = {0.0, 0.0, 0.0};
		for (int i = 0; i < cells; ++i)
			if (solved.kind[i] >= kWheat)
				had[k][solved.kind[i] - kWheat] += worth(k, i);
	}
	matched = 0;
	for (int i = 0; i < cells; ++i)
		if (solved.kind[i] >= kWheat)
			matched += matching(i);
	spreadAfter = stockSpread(had, teams);
	stockShape = clustering();
	stockTerms = stockObjective(had, teams, counts, clustering(), fieldsWanted, fieldsWeight);
	return run;
}

// ---------------------------------------------------------------------------
// Setting the search up, and painting what it found
// ---------------------------------------------------------------------------

/// The cells the design fixes: every colony's home and its four doorsteps stay dry, open and
/// unplanted, so a colony always has somewhere to stand and something to build on whatever the
/// search decides about the rest of the map.
///
/// Letting the doorsteps carry crops was tried, on the reasoning that a crop at the door is worth
/// most to a catchment and the search would want it there. It measured worse on both counts (over
/// 16 seeds at 256x256 with four colonies: canonical fairness 0.912 to 0.875, worst colony fitness
/// 0.124 to 0.053), because a cleared doorstep leaves every colony taking the same guaranteed
/// opening patch from secureStartingCrops, whereas a plantable one gives each colony whatever the
/// search happened to leave and the openings come out uneven. The engine's own crop guarantee is
/// the better leveller at this range; the search's business is the ground beyond it.
void pinHomes(Solved &solved)
{
	const Torus t = solved.lat.torus();
	solved.pinned.assign(solved.lat.size(), 0);
	for (const int home : solved.home)
	{
		const int x = home % t.w, y = home / t.w;
		solved.pinned[home] = 1;
		solved.kind[home] = kOpen;
		for (const auto &step : kCardinalSteps)
		{
			const int next = t.at(x + step[0], y + step[1]);
			solved.pinned[next] = 1;
			solved.kind[next] = kOpen;
		}
	}
}

/// The water the player asked for, as a starting arrangement: the wettest share of a smooth noise
/// field, so the search begins from something already shaped like country rather than from speckle.
void floodToShare(Solved &solved, GenerationContext &context, int sharePercent)
{
	const int cells = solved.lat.size();
	std::vector<int> free;
	for (int i = 0; i < cells; ++i)
		if (!solved.pinned[i])
			free.push_back(i);
	const int wanted = int(std::int64_t(free.size()) * std::clamp(sharePercent, 0, 100) / 100);
	if (wanted <= 0)
		return;
	const std::vector<int> noise =
		fractalNoise(solved.lat.w, solved.lat.h, std::max(2, solved.lat.w / 4), 3,
					 context.stream("even-ground-shape"));
	std::stable_sort(free.begin(), free.end(), [&](int a, int b) { return noise[a] > noise[b]; });
	for (int k = 0; k < wanted; ++k)
		solved.kind[free[k]] = kWater;
}

/// The crops the amount sliders asked for, dealt over the land as a starting arrangement.
void stockToAmounts(Solved &solved, GenerationContext &context, const EvenGroundOptions &o,
					double greenness)
{
	const int cells = solved.lat.size();
	std::vector<int> free;
	for (int i = 0; i < cells; ++i)
		if (!solved.pinned[i] && solved.kind[i] != kWater)
			free.push_back(i);
	context.shuffle(free.begin(), free.end(), "even-ground-stock");

	const std::int64_t land = std::int64_t(free.size());
	int wheat = int(scaledCount(land * kWheatCells / 100, o.wheat) * greenness);
	int wood = int(scaledCount(land * kWoodCells / 100, o.wood) * greenness);
	int stone = int(scaledCount(land * kStoneCells / 100, o.stone));
	// Leave open ground to cross and build on however high the amounts go.
	const int ceiling = int(land * kStockedCeiling / 100);
	while (wheat + wood + stone > ceiling)
	{
		int &most = wheat >= wood ? (wheat >= stone ? wheat : stone) : (wood >= stone ? wood : stone);
		if (most <= 0)
			break;
		--most;
	}
	size_t at = 0;
	const auto deal = [&](int count, unsigned char kind)
	{
		for (int k = 0; k < count && at < free.size(); ++k, ++at)
			solved.kind[free[at]] = kind;
	};
	deal(wheat, kWheat);
	deal(wood, kWood);
	deal(stone, kStone);
}

/// Each tile's cell. Not the square the lattice would suggest: a square grid painted straight comes
/// out as a staircase of right angles whatever noise is laid over its edges, because every border
/// is a straight run between two corners. Instead each cell keeps a centre jostled off the
/// lattice and takes the ground nearest it, through a warp of the tile's own position (Points.h),
/// so borders meet at the angles a coastline does. The jostle is well under half a spacing, so two
/// cells that are cardinal neighbours on the lattice still touch on the ground and the connectivity
/// the search settled on survives being painted.
std::vector<int> paintedCells(const Torus &t, const Lattice &lat, GenerationContext &context)
{
	std::mt19937 &rng = context.stream("even-ground-warp");
	std::vector<Site> centres;
	centres.reserve(lat.size());
	for (int cy = 0; cy < lat.h; ++cy)
		for (int cx = 0; cx < lat.w; ++cx)
		{
			const int jostle = int(lat.tiles * kCentreJostle);
			const int dx = int(context.bounded("even-ground-warp", 2 * jostle + 1)) - jostle;
			const int dy = int(context.bounded("even-ground-warp", 2 * jostle + 1)) - jostle;
			centres.push_back({t.x(cx * lat.tiles + lat.tiles / 2 + dx),
							   t.y(cy * lat.tiles + lat.tiles / 2 + dy)});
		}
	// Four octaves from three cells down: the long ones bend a border across several cells, the
	// short ones ravel its edge.
	const int period = std::max(8, lat.tiles * 3);
	const std::vector<int> alongX = fractalNoise(t.w, t.h, period, 4, rng);
	const std::vector<int> alongY = fractalNoise(t.w, t.h, period, 4, rng);
	return nearestSiteLabels(t, centres, lat.tiles, alongX, alongY, int(kBorderWander * 100));
}

struct Layout
{
	Torus t{1, 1};
	Lattice lat;
	std::vector<unsigned char> kind;   // the solved lattice
	std::vector<unsigned char> pinned; // the home cells, which carry no deposit
	std::vector<int> cellOf;           // each tile's cell
	std::vector<int> homeOf;         // the colony whose home ground a tile is, or -1
	std::vector<int> home;           // each colony's home cell
	TerrainSketch terrain;
	std::string failure;
};

/// Why a request cannot be solved, or "": the lattice must have room for a search worth running.
std::string latticeFailure(const GenerationRequest &request)
{
	const Lattice lat = latticeFor(1 << request.wDec, 1 << request.hDec);
	if (lat.w < kLeastRows || lat.h < kLeastRows)
		return "This map is too small to solve on; use a bigger map.";
	if (lat.size() < kLeastCellsPerColony * request.nbTeams)
		return "Too many colonies to balance on a map this size; use a bigger map or fewer "
			   "colonies.";
	return "";
}

Layout design(const GenerationRequest &request, GenerationContext &context)
{
	const EvenGroundOptions o(request);
	Layout L;
	L.t = {1 << request.wDec, 1 << request.hDec};
	L.failure = latticeFailure(request);
	if (!L.failure.empty())
		return L;
	L.lat = latticeFor(L.t.w, L.t.h);
	const Torus lattice = L.lat.torus();
	const int teams = request.nbTeams;

	Solved solved;
	solved.lat = L.lat;
	solved.kind.assign(L.lat.size(), kOpen);

	// The homes, spread as far apart as the lattice allows and then dealt to the colonies, so a team
	// number never lands on the same ground map after map.
	const std::vector<unsigned char> everywhere(L.lat.size(), 1);
	solved.home =
		farthestSites(lattice, everywhere, everywhere, teams, context, "even-ground-homes");
	if (int(solved.home.size()) < teams)
	{
		L.failure = "The colonies could not be spread out on this map.";
		return L;
	}
	dealStarts(context, solved.home, "even-ground-homes-deal");

	pinHomes(solved);
	// How wet and how green this seed is, drawn round what the sliders asked for.
	//
	// This is where the variety actually was. Every target above can be drawn and every arrangement
	// searched, and twelve seeds still came out with water between 8.2 and 8.9 per cent of the map
	// and wheat between 4.7 and 5.1, because the budgets were exact and only their arrangement was
	// ever in question - so every seed was the same country rearranged. Rearrangement is a weak
	// lever on how a map looks; composition is a strong one. The sliders are now the middle of a
	// band rather than a figure, which trades their exactness for one seed being a lake country and
	// the next dry downland. What a slider still promises is its direction: more asked for is more
	// on the map, always.
	//
	// The whole brief is drawn here, before any of it is used, so what a seed was asked for can be
	// read in one place rather than gathered from the design as it goes.
	Brief brief(context, "even-ground");
	const double wetness = brief.target("wetness", kWetLeast, kWetMost);
	const double greenness = brief.target("greenness", kGreenLeast, kGreenMost);
	// Ranges, not constants: the whole reason one seed of a solved map looks like another is that
	// they were all handed the same targets.
	const double shoreWanted = brief.target("shore-wanted", kShoreLeast, kShoreMost);
	const double fieldsWanted = brief.target("fields-wanted", kFieldsLeast, kFieldsMost);
	// Which of the optional targets this seed solves to. Equal catchments, building room and a
	// joined-up country are not in here: they are what makes the result a map rather than what gives
	// it a character, and a seed that dropped one would be broken, not varied.
	//
	// Approach parity is in the draw, and it is a fairness property: a seed that drops it can leave
	// one colony far easier to hold than another. That is a deliberate choice for this map - variety
	// is wanted more than an even contest, and a country where the ground genuinely favours somebody
	// is a kind of map, where a country balanced in every dimension at once is only ever the one.
	brief.choose({"bodies", "pass", "fields", "approach"}, 1, 4);

	floodToShare(solved, context, std::clamp(int(std::lround(o.water * wetness)), 0, 90));

	// Tight passes at 100, open country at 0, as a passage width in cells.
	const double wantWidth = 1.0 + 6.0 * (1.0 - std::clamp(o.passes, 0, 100) / 100.0);
	const double balance = kBalanceWeight * std::clamp(o.balance, 0, 100) / 100.0;
	const int effort = std::clamp(o.effort, 0, 2);
	// How many bodies this seed's water should form: one seed's inland sea is the next one's chain
	// of lakes. Drawn from the shape stream rather than the brief, because it is a target of the
	// search rather than a statement of what the country is like.
	const int bodiesWanted =
		kBodiesLeast +
		int(context.bounded("even-ground-shape", kBodiesMost - kBodiesLeast + 1));
	solved.shape = annealShape(solved, context, balance, wantWidth, bodiesWanted, shoreWanted, brief,
							   kShapeMoves[effort]);

	// The stock pass inherits the shape pass's walks: the land is settled now, so every colony's
	// distance to every cell is fixed and a crop's move is worth two known terms.
	std::vector<unsigned char> land(L.lat.size(), 0);
	for (int i = 0; i < L.lat.size(); ++i)
		land[i] = solved.kind[i] != kWater;
	std::vector<std::vector<int>> walk(teams, std::vector<int>(L.lat.size(), -1));
	std::vector<int> queue;
	for (int k = 0; k < teams; ++k)
		walkLand(lattice, land, solved.home[k], walk[k], queue);
	std::vector<double> decay(L.lat.size() + 1);
	for (int step = 0; step <= L.lat.size(); ++step)
		decay[step] = std::exp(-double(step) / kCatchmentDecay);

	// Balance buys search, not just weight. Weighting the shape pass alone left the slider with
	// nothing measurable to do - fairness read 0.900, 0.896 and 0.912 at 0, 50 and 100, which is
	// noise - because the pass that actually equalises a map is this one, and it ran at full
	// strength whatever the slider said. Here the slider is how many moves it gets: at 0 the crops
	// stay where they were dealt and the map is as unfair as chance made it, at 100 it searches
	// until the spread stops falling.
	stockToAmounts(solved, context, o, greenness);
	const int stockMoves =
		int(std::int64_t(kStockMoves[effort]) * std::clamp(o.balance, 0, 100) / 100);
	solved.stock = annealStock(solved, context, walk, decay, stockMoves,
							   solved.stockSpreadBefore, solved.stockSpreadAfter, solved.fields,
							   solved.stockCost, fieldsWanted,
							   brief.weight("fields", kFieldsWeight));

	L.kind = solved.kind;
	L.pinned = solved.pinned;
	L.home = solved.home;
	L.cellOf = paintedCells(L.t, L.lat, context);

	L.terrain.assign(L.t.size(), GRASS);
	for (int i = 0; i < L.t.size(); ++i)
		if (L.kind[L.cellOf[i]] == kWater)
			L.terrain[i] = WATER;
	layBeaches(L.terrain, L.t);

	// A colony's home ground is its pinned cells: its own cell and the four around it.
	std::vector<int> ownerOf(L.lat.size(), -1);
	for (int k = 0; k < teams; ++k)
	{
		const int x = solved.home[k] % lattice.w, y = solved.home[k] / lattice.w;
		ownerOf[solved.home[k]] = k;
		for (const auto &step : kCardinalSteps)
			if (int &owner = ownerOf[lattice.at(x + step[0], y + step[1])]; owner < 0)
				owner = k;
	}
	L.homeOf.assign(L.t.size(), -1);
	for (int i = 0; i < L.t.size(); ++i)
		L.homeOf[i] = ownerOf[L.cellOf[i]];

	context.telemetry.measure("even-ground.lattice.cells", L.lat.size());
	context.telemetry.measure("even-ground.lattice.cell-tiles", L.lat.tiles);
	reportSolve(context.telemetry, "even-ground.shape", solved.shape);
	reportObjective(context.telemetry, "even-ground.shape", solved.cost);
	context.telemetry.measure("even-ground.shape.reach-spread", solved.reachSpread);
	context.telemetry.measure("even-ground.shape.pass-width-target", wantWidth);
	context.telemetry.measure("even-ground.shape.pass-width-cells", solved.passWidth);
	context.telemetry.measure("even-ground.shape.room-shortfall", solved.roomShort);
	context.telemetry.measure("even-ground.shape.shore-per-water-cell", solved.shoreRatio);
	context.telemetry.measure("even-ground.shape.bodies-target", bodiesWanted);
	context.telemetry.measure("even-ground.shape.bodies-actual", solved.bodies);
	reportSolve(context.telemetry, "even-ground.stock", solved.stock);
	reportObjective(context.telemetry, "even-ground.stock", solved.stockCost);
	context.telemetry.measure("even-ground.stock.spread-before", solved.stockSpreadBefore);
	context.telemetry.measure("even-ground.stock.spread-after", solved.stockSpreadAfter);
	context.telemetry.measure("even-ground.stock.field-clustering", solved.fields);
	if (solved.severed > 0)
		context.telemetry.fallback("even-ground.shape.severed",
								   "The search could not join every colony by land.");
	return L;
}

bool generate(Game &game, GenerationContext &context)
{
	context.stage = "even ground solve";
	const EvenGroundOptions o(context.request);
	const Layout L = design(context.request, context);
	if (!L.failure.empty())
	{
		context.detail = L.failure;
		return false;
	}
	Map &map = game.map;
	const Torus &t = L.t;
	const int teams = context.request.nbTeams;
	for (int k = 0; k < teams; ++k)
		game.addTeam();

	context.stage = "even ground terrain";
	writeUndermap(map, L.terrain);

	context.stage = "even ground colonies";
	const auto homeMask = [&](int team)
	{
		std::vector<unsigned char> ground(size_t(t.size()), 0);
		for (int i = 0; i < t.size(); ++i)
			ground[i] = L.homeOf[i] == team && map.isGrass(i % t.w, i / t.w);
		return ground;
	};
	const auto anchor = [&](int team)
	{
		const int cell = L.home[team];
		return MapGeneratorPoint((cell % L.lat.w) * L.lat.tiles + L.lat.tiles / 2,
								 (cell / L.lat.w) * L.lat.tiles + L.lat.tiles / 2);
	};
	if (!settleColonies(game, context, "even-ground-starts", homeMask, anchor))
		return false;

	context.stage = "even ground resources";
	// Each crop cell is planted to a share of its own ground, chosen by a smooth field so the cover
	// comes out as patches with gaps rather than a solid block: fields stay walkable and gatherable
	// from inside, which a solid one would not be.
	const std::vector<unsigned char> reserved = swarmSurroundings(t, context);
	const std::vector<int> grain =
		fractalNoise(t.w, t.h, std::max(4, L.lat.tiles), 3, context.stream("even-ground-cover"));
	std::vector<std::vector<int>> ground(kCellKinds);
	for (int i = 0; i < t.size(); ++i)
	{
		const unsigned char kind = L.kind[L.cellOf[i]];
		if (kind >= kWheat && !L.pinned[L.cellOf[i]] && !reserved[i] &&
			clearGround(map, i % t.w, i / t.w))
			ground[kind].push_back(i);
	}
	const auto level = [&](int i) { return grain[i]; };
	context.telemetry.measure("even-ground.wheat.tiles",
							  plantCoverShare(map, t, ground[kWheat], WHEAT, kWheatCover, level));
	context.telemetry.measure("even-ground.wood.tiles",
							  plantCoverShare(map, t, ground[kWood], WOOD, kWoodCover, level));
	context.telemetry.measure("even-ground.stone.tiles",
							  plantCoverShare(map, t, ground[kStone], STONE, kStoneCover, level));
	seedAlgae(map, context, t, "even-ground-algae", o.algae, AlgaeBand::shallows(1, 4).thriving(0.5));

	context.stage = "even ground openings";
	secureStartingCrops(game, context, t);
	// The solved lattice joins every colony by land, but the warp and the beaches can pinch a
	// one-cell isthmus shut. This is the backstop: it clears the deposits on one walk and, only if
	// the land really has closed, fords it.
	openColonyRoutes(map, context, t, StepCosts{1, 3, -1, 8, -1});
	reopenCrampedStarts(game, context, {o.wheat, o.wood, o.stone, o.algae, 100});
	return true;
}

/// What the map promised, checked on the finished world. Deliberately not a rebuild of the design:
/// the promises here are about the world the solver produced - everyone joined, everyone fed,
/// everyone with room - and all three are measurable directly, where re-running the solve would
/// cost as much as generating the map again and prove only that it is deterministic.
std::string validateWorld(const Game &game, const GenerationContext &context)
{
	const int teams = context.request.nbTeams;
	if (const std::string cut = walkFromFirstColony(game.map, teams, "the land", "").error;
		!cut.empty())
		return cut;
	return startingAccessFailure(game.map, teams,
								 {{WHEAT, 24, "wheat"}, {WOOD, 32, "wood"}});
}
} // namespace

EvenGroundOptions::EvenGroundOptions(const GenerationRequest &r)
	: water(r.option("water-share")), balance(r.option("balance")), passes(r.option("passes")),
	  effort(r.option("effort")), wheat(r.option("wheat-amount")), wood(r.option("wood-amount")),
	  stone(r.option("stone-amount")), algae(r.option("algae-amount"))
{
}

GeneratorDefinition evenGroundDefinition()
{
	return {"even-ground",
			58,
			"Even Ground",
			2,
			false,
			// The sliders are the targets the solver is given, not the terrain it draws. Water share
			// and the resource amounts are budgets it may arrange but never change; balance and
			// tightness are what it trades off against each other. Water is a target rather than a
			// figure - the brief multiplies it by this seed's wetness (kWetLeast to kWetMost), so
			// the slider sets the middle of a band and one seed comes out a lake country and the
			// next dry downland. What it still promises is its direction.
			// The default is low for a map with this much shoreline: the search is asked for a few
			// lobed bodies rather than one round sea, and every one takes a beach out of the
			// buildable ground on both banks. Swept over three seed ranges of 16 at 256x256 with
			// four colonies, fairness is flat from 10 to 20 per cent (about 0.906 either end) while
			// the worst colony's building sites fall steadily with every extra lake: about 1470 at
			// 10 per cent against 1200 at 20 and 750 at 35. So the water is kept to what gives the
			// map its shape, not to what the search can still balance around.
			//
			// The ceiling is where the map stops being one, measured rather than guessed: over 80
			// seeds at 256x256 with four colonies, a request refuses on none at 25 per cent, one at
			// 30 and 35, and four at 40, and over 40 seeds it refuses on 10 per cent at 50, 15 at 55
			// and 20 at 60 - colonies with no wood in reach, or too few building origins to settle.
			// The band above 40 also buys less and less of what it asks: the flood saturates near 53
			// per cent actual water however much more is requested, while the map's 4x4 building
			// sites fall from about 13,000 at 40 to 7,700 at 60. So the range ends where the answers
			// stop improving, and 40 is a wet map that is still a map.
			{{"water-share", "Water", 0, 40, 5, 10, ControlGroup::Terrain},
			 {"balance", "Balance", 0, 100, 10, 70, ControlGroup::Layout},
			 {"passes", "Tight passes", 0, 100, 10, 40, ControlGroup::Layout},
			 GeneratorControl::choice("effort", "Solver effort", {"Brief", "Normal", "Patient"}, 1,
									  ControlGroup::Layout),
			 GeneratorControl::percentage("wheat-amount", "Wheat amount"),
			 GeneratorControl::percentage("wood-amount", "Wood amount"),
			 GeneratorControl::percentage("stone-amount", "Stone amount"),
			 GeneratorControl::percentage("algae-amount", "Algae amount")},
			generate,
			true,
			latticeFailure,
			validateWorld,
			{"terrain:natural", "feature:lakes", "style:wide-open", "fairness:solved-catchment"}};
}
