// SPDX-License-Identifier: GPL-3.0-or-later
#include "CoralGenerator.h"
#include "Drawing.h"
#include "Game.h"
#include "GenerationContext.h"
#include "Geometry.h"
#include "Grid.h"
#include "LatticeNoise.h"
#include "Pipeline.h"
#include "Planting.h"
#include "Resources.h"
#include "Roads.h"
#include "Settlements.h"
#include "Sketch.h"
#include "Wedge.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>
using namespace MapGeneration;

// Coral: every colony is a sea fan of land. A single trunk leaves the colony's pad on the rim of
// the map and forks, and forks again, as it grows in towards the middle, so each colony's coral is
// a triangle that opens out inwards: narrow and solid at home, wide and fingery at the far end.
//
// THE PLAY IDEA. Home is the apex of the fan, where there is one trunk to walk and one front to
// hold. Every fork doubles the ground to cover and halves the width of it, so pushing outward means
// long single-file supply lines onto ever thinner branches. The water between two sibling branches
// is a triangle opening inwards too, and the far ends of neighbouring fans reach into each other's
// triangles: the front between two colonies is a zigzag of fingertips, each a short swim from enemy
// land and a long walk from either home, and all the fans meet in a tangle in the middle of the
// map. The land is richer the further it is from home - stone at the forks, fruit on the buds at
// the tips - so the prizes sit exactly where the land is hardest to hold. A few thin land bridges
// join neighbouring fans so ground armies can meet; with none, colonies meet only once they can
// swim.
//
// WHY A FORKING TREE AND NOTHING RULED. Running trunks straight at the centre and bending side
// branches round it in arcs would make neat interlocking easy, but it draws a spider web: spokes,
// concentric threads and a hub (which Spider web already is). Here nothing follows the centre: the
// trunk leans off the line to the middle (every colony by the same amount, so the whole map turns
// like a pinwheel rather than forming a star), every branch splits in two at its tip at its own
// random angle and length, and the shape of a colony is whatever that growth makes of its room.
//
// WHY GROWTH IS CHECKED AGAINST THE NEIGHBOURS. The tree is grown once, for colony 0, and turned
// round the map centre for every other colony, so it is fair however random the growth. That only
// works if a branch accepted for colony 0 still fits once every copy is drawn, so every new branch
// must keep a strait of water (the strait width) from all land already grown, from that land's
// copies in every other colony's wedge, and from its own copies. The fans therefore grow until they
// meet a strait short of each other, and it is this refusal, not any layout rule, that makes the
// tips of two neighbours interleave: a branch heading into the neighbour's copy is stopped, a
// branch heading into the gap between two of the neighbour's branches keeps going.
//
// WHY LEVEL BY LEVEL. growBranches grows every branch of one level before the next. Grown depth
// first, the first fork's whole subtree would claim the room before its sibling had grown at all,
// and every fan would come out lopsided; grown a level at a time, the two sides of every fork
// compete on equal terms and the fan opens out evenly.
//
// WHY REFUSED RATHER THAN CLIPPED. A branch that would come within a strait of other land is
// offered once more at half length, then dropped with everything that would have grown from it.
// Clipping strokes would leave blunt stumps and slivers of water narrower than the strait; refusing
// whole branches keeps every gap at least a strait wide, which is what makes swimming the
// deliberate way across and keeps the land readable as coral.
//
// The design is a pure function of the request, so validateWorld rebuilds it and checks the world.
namespace
{

// A colony's pad, and how far it may shrink on a crowded rim before the request is refused; never
// more than kPadShare of the half side, so a small map's pads leave the fan room to grow. Its
// outline wobbles by up to kPadRoughness of its radius. Sea kept across the wrap and between pads.
constexpr int kPadRadius = 12;
constexpr int kMinimumPad = 7;
constexpr double kPadShare = 0.15;
constexpr double kPadRoughness = 0.2;
constexpr double kWrapGap = 6;
constexpr double kPadGap = 6;
// The trunk is this much wider (in half width) than the branch width control, so home reads as the
// solid base of the fan with room for the first buildings.
constexpr double kTrunkExtra = 1.5;
// Branches never taper below a half width of 4: about eight tiles across, which after its beaches
// and the sand road down its middle still keeps grass either side of the road, so the thinnest
// branches are worth holding rather than sandbars.
constexpr double kMinimumHalfWidth = 4.0;
// How each fork's children compare with their parent. Length shrinks slowly so the far levels are
// still long enough to reach the middle; width shrinks so the fan visibly thins as it goes.
constexpr double kLengthRatio = 0.8;
constexpr double kLengthJitter = 0.2;
constexpr double kWidthRatio = 0.85;
constexpr double kSpreadJitter = 0.35;
constexpr double kBend = 0.12;
// The lengths of every level add up to this share of the distance from the pad to the map centre: a
// little over, so the far tips reach the middle and have to compete there rather than stopping
// short in open water. The trunk is kept to kMaximumTrunk where it can be by letting each level
// keep more of its parent's length, up to all of it, so the fan fills out rather than hanging off a
// long bare trunk.
constexpr double kReachShare = 1.15;
constexpr double kMaximumTrunk = 30;
constexpr double kMaximumLengthRatio = 1.0;
// The branching control counts levels on a 256-tile map; each doubling of the map adds one, so a
// fan forks about as densely, tile for tile, whatever the map size.
constexpr int kReferenceHalf = 128;
// The fork angle control is the angle on a 256-tile map. Smaller maps open their forks wider and
// bigger maps narrower, with the square root of the size, within these shares: a small fan has
// few levels, and only wide forks spread them into a fan that fills its wedge, while a big fan's
// many levels at a wide angle curl back on themselves into rings.
constexpr double kMinimumSpreadScale = 0.7, kMaximumSpreadScale = 1.45;
// Every home starts identical: wheat and wood beside the swarm, a quarry behind it, all unscaled.
constexpr int kHomeWheat = 40;
constexpr int kHomeWood = 30;
// The branches' standing wheat and wood, as shares of their open grass at 100: enough that walking
// any branch passes a field or a copse, since the branches are all the land there is. Fields lean
// towards home by kHomeLean against the patch noise, so the trunk and first forks are the
// breadbasket.
constexpr int kWheatShare = 30;
constexpr int kWoodShare = 21;
// Dry patches of sand inside the branches, as a share of their inland grass, at least this many
// steps from water so a strip of grass always lies between patch and beach; and the size of the
// noise the patches follow, in tiles.
constexpr double kSandShare = 0.08;
constexpr int kSandInland = 3;
constexpr double kSandCell = 10;
// One algae clump per this many tiles of shallows at 100, all on the share of each colony's
// shallows where algae regrows most readily.
constexpr int kAlgaeTilesPerClump = 35;
constexpr double kAlgaeBestShare = 0.3;
constexpr double kHomeLean = 0.6;
// Forks carrying stone, in tenths of a percent at 100, and a deposit's size from the home end of
// the fan to the far end: remoteness pays.
constexpr int kForkStone = 400;
constexpr int kForkStoneNear = 3, kForkStoneFar = 9;
// A bud's fruit grove, in tiles, from the home end of the fan to the far end.
constexpr int kBudFruitNear = 3, kBudFruitFar = 12;
// Bridges are looked for first among straits at most this wide and away from the rim, and are kept
// this far apart.
constexpr double kBridgeSearch = 30;
constexpr double kBridgeApart = 30;

struct Geometry
{
	int teams, half;
	int wedges;   // wedges the design is turned through: one per colony, at least two
	double wedge; // radians per wedge
	double trunkHalf, strait;
	double padRadius, padReach, rootRadius; // the pad centres' distance from the map centre
	double reach;                           // no land beyond this radius, clear of the wrap
	double lean, spread;                    // radians
	double trunkLength, lengthRatio;
	int levels;     // forks from the trunk to the tips
	bool padsFit;   // the pads fit round the rim
	bool trunkFits; // and there is room inside them for a fan
};

Geometry geometryFor(const GenerationRequest &r)
{
	const CoralOptions o(r);
	Geometry g;
	g.teams = std::max(1, r.nbTeams);
	// One colony is still designed as if it had a neighbour: its fan grows in half the map, with
	// the other half open sea to grow towards.
	g.wedges = std::max(2, g.teams);
	g.wedge = 2 * kPi / g.wedges;
	g.half = std::min(1 << r.wDec, 1 << r.hDec) / 2;
	g.trunkHalf = o.branchWidth / 2.0 + kTrunkExtra;
	g.strait = o.straitWidth;
	g.reach = g.half - std::max(kWrapGap, g.strait) / 2;
	g.padsFit = false;
	// The home size control scales the pad, 100 being kPadRadius (and kPadShare on a small map): a
	// bigger pad is more room to build the first economy before the colony has to push out along
	// its trunk. It is a size to aim for, not a promise; a crowded rim shrinks it as ever.
	const double homeScale = o.homeSize / 100.0;
	const int largestPad = std::max(
		kMinimumPad,
		int(std::lround(std::min(kPadRadius * homeScale, kPadShare * homeScale * g.half))));
	for (int pad = largestPad; pad >= kMinimumPad && !g.padsFit; --pad)
	{
		g.padRadius = pad;
		g.padReach = pad * (1 + kPadRoughness);
		g.rootRadius = g.reach - g.padReach;
		g.padsFit =
			g.teams < 2 || 2 * g.rootRadius * std::sin(kPi / g.teams) >= 2 * g.padReach + kPadGap;
	}
	g.lean = o.lean * kPi / 180;
	const double sizeScale =
		std::clamp(std::sqrt(double(kReferenceHalf) / (std::max(1 << r.wDec, 1 << r.hDec) / 2)),
				   kMinimumSpreadScale, kMaximumSpreadScale);
	g.spread = o.forkAngle * sizeScale * kPi / 180;
	// The lengths of the trunk and every level below it form a geometric series; its sum, shortened
	// by how far the branches turn off the line in, should reach just past the centre. With
	// kLengthRatio the trunk is whatever length does that; where that is longer than kMaximumTrunk,
	// each level keeps more of its parent's length until the trunk is short enough or every level
	// is as long as the trunk. The number of levels is the control's, adjusted for map size only,
	// so branching always changes the map.
	const double target = kReachShare * g.rootRadius;
	const auto series = [&](int levels, double ratio)
	{
		double sum = 0, scale = 1;
		for (int level = 0; level <= levels; ++level, scale *= ratio)
			sum += scale * std::cos(std::min(kPi / 3, level * g.spread / 2));
		return sum;
	};
	// Levels follow the longer side: on a rectangular map the fan is stretched along it, so it
	// needs the forks of the longer side to stay as dense there as on a square map of that size.
	const int longHalf = std::max(1 << r.wDec, 1 << r.hDec) / 2;
	int sizeLevels = 0;
	for (int half = longHalf; half > kReferenceHalf; half /= 2)
		++sizeLevels;
	for (int half = longHalf; half < kReferenceHalf; half *= 2)
		--sizeLevels;
	g.levels = std::max(1, o.branching + sizeLevels);
	g.lengthRatio = kLengthRatio;
	while (g.lengthRatio < kMaximumLengthRatio &&
		   target / series(g.levels, g.lengthRatio) > kMaximumTrunk)
		// Raise the ratio a hundredth at a time: fine enough that the trunk lands just under its
		// maximum.
		g.lengthRatio = std::min(kMaximumLengthRatio, g.lengthRatio + 0.01);
	g.trunkLength = target / series(g.levels, g.lengthRatio);
	// A trunk must clear its pad by a strait before it forks, or its first fork is refused against
	// the pad and nothing grows. On a small map with many levels the series makes the trunk shorter
	// than that, so levels are given up until it clears; only a map where even a single fork cannot
	// clear is refused.
	const double clearsPad = g.padReach + g.strait + kMinimumHalfWidth;
	while (g.trunkLength < clearsPad && g.levels > 1)
		g.trunkLength = target / series(--g.levels, g.lengthRatio);
	g.trunkFits = g.trunkLength >= clearsPad;
	return g;
}

// What a piece of the design is.
enum class Kind
{
	Branch,  // a branch of the fan: the trunk is branch 0
	Bud,     // a disc of land at a branch's tip, for a fruit grove
	Bridge,  // a strip of land joining this fan to the next colony's
	Obstacle // the pad, as a circle growth keeps clear of; drawn as a shape instead
};

// A stroked piece of land in colony 0's wedge.
struct Piece
{
	std::vector<StrokePoint> path;
	PathBounds bounds;
	Kind kind;
	int branch; // the tree branch it is or ends, else -1
};

// A point where a branch leaves its parent, and whether it carries stone: rolled once for colony 0,
// so every colony's copy of the fork gets the same.
struct Fork
{
	ShapePoint at;
	bool stone;
};

// A bud in colony 0's wedge and its fruit.
struct Bud
{
	ShapePoint at;
	int fruit; // 0, 1, 2: CHERRY + fruit
};

struct Layout
{
	Torus t{1, 1};
	Geometry g{};
	double cx = 0, cy = 0, phase = 0;
	// The fan is designed and grown in a round frame on the map's shorter side; this places it on
	// the map, stretched along the longer side so the corals fill a rectangular map.
	Stretch stretch;
	std::vector<Branch> tree;  // colony 0's fan: the trunk first
	std::vector<Piece> pieces; // every stroked piece of colony 0's coral
	std::vector<Fork> forks;   // colony 0's forks
	std::vector<Bud> buds;     // colony 0's buds, in the order their pieces were added
	std::vector<ShapePoint> pads;
	std::vector<double> padAngle;                   // each pad's heading, outwards from the centre
	std::vector<unsigned char> land, water, bridge; // per tile
	// Per tile: the colony whose pad it is, the bud it is part of (numbered over every colony), and
	// the colony whose trunk tip it is; -1 elsewhere.
	std::vector<int> padOf, budOf, tipOf;
	std::vector<std::vector<ShapePoint>> trunkLines; // each colony's trunk centre line
	// The centre lines of every branch that forks on and every bridge, for the sand roads.
	std::vector<std::vector<StrokePoint>> roads;
	std::string failure;

	/// The axis of colony k's wedge: where its pad sits.
	double axis(int k) const { return phase + (k + 0.5) * g.wedge; }
	/// A point of colony 0's design turned onto colony k's wedge.
	ShapePoint turn(ShapePoint p, int k) const
	{
		const double a = k * g.wedge, c = std::cos(a), s = std::sin(a);
		const double dx = p.x - cx, dy = p.y - cy;
		return {cx + dx * c - dy * s, cy + dx * s + dy * c};
	}
	/// A point or path of colony 0's design turned onto colony k's wedge and placed on the map.
	ShapePoint place(ShapePoint p, int k) const { return stretch.apply(cx, cy, turn(p, k)); }
	std::vector<StrokePoint> place(const std::vector<StrokePoint> &path, int k) const
	{
		return stretchPath(turn(path, k), cx, cy, stretch);
	}
	std::vector<StrokePoint> turn(const std::vector<StrokePoint> &path, int k) const
	{
		std::vector<StrokePoint> turned;
		for (const StrokePoint &p : path)
		{
			const ShapePoint q = turn(ShapePoint{p.x, p.y}, k);
			turned.push_back({q.x, q.y, p.halfWidth});
		}
		return turned;
	}

	/// Whether `candidate`, growing from branch `parent`, keeps a strait from all land already in
	/// the design, from that land's copies in every other wedge, and from its own copies.
	///
	/// Every wedge is checked, not just the neighbours': the fans all converge on the middle of the
	/// map, where a branch can come close to the copy of a colony two or more wedges round.
	///
	/// A branch is meant to join the land it grows from, so its parent is exempt, and so is any
	/// sibling forking from the same point (a fork's two children start together). A bud (a
	/// one-point path) passes its branch as `parent`; that branch's siblings are exempt for it too,
	/// so the bud on one side of a fork is not refused for the branch on the other.
	bool keepsClear(const std::vector<StrokePoint> &candidate, int parent) const
	{
		const bool bud = candidate.size() == 1;
		const ShapePoint root =
			bud && parent >= 0
				? ShapePoint{tree[parent].path.front().x, tree[parent].path.front().y}
				: ShapePoint{candidate.front().x, candidate.front().y};
		const int forkParent = bud ? (parent >= 0 ? tree[parent].parent : -2) : parent;
		for (const StrokePoint &p : candidate)
			if (std::hypot(p.x - cx, p.y - cy) + p.halfWidth > g.reach)
				return false;
		// Only the stub of a branch buried in its parent's width is skipped: a longer allowance
		// would skip short branches entirely and let them cross other land unchecked. The trunk
		// grows out of the middle of its pad, so its whole stretch across the pad is skipped.
		const double skip = bud          ? 0.0
							: parent < 0 ? g.padReach + g.strait + candidate.front().halfWidth
										 : candidate.front().halfWidth + 1;
		const PathBounds own = pathBounds(candidate);
		const auto nearby = [&](const PathBounds &b, int k)
		{
			const ShapePoint c = turn(ShapePoint{b.x, b.y}, k);
			return std::hypot(c.x - own.x, c.y - own.y) < own.radius + b.radius + g.strait;
		};
		for (int k = 0; k < g.wedges; ++k)
		{
			for (const Piece &piece : pieces)
			{
				if (k == 0 && piece.kind == Kind::Branch &&
					(piece.branch == parent ||
					 (forkParent >= 0 && tree[piece.branch].parent == forkParent &&
					  std::hypot(piece.path.front().x - root.x, piece.path.front().y - root.y) <
						  1)))
					continue;
				if (!nearby(piece.bounds, k))
					continue;
				if (pathClearance(candidate, k ? turn(piece.path, k) : piece.path, skip) < g.strait)
					return false;
			}
			if (k != 0 && nearby(own, k) &&
				pathClearance(candidate, turn(candidate, k), skip) < g.strait)
				return false;
		}
		return true;
	}
	void add(std::vector<StrokePoint> path, Kind kind, int branch)
	{
		const PathBounds bounds = pathBounds(path);
		pieces.push_back({std::move(path), bounds, kind, branch});
	}
};

// A stream of numbers in [0, 1) from the design's stream. mt19937's output is the same on every
// platform, unlike the standard distributions, so this keeps maps identical everywhere.
struct Rolls
{
	std::mt19937 &random;
	double operator()() { return random() / 4294967296.0; }
};

// Joins this fan to the next colony's with up to `count` strips of land across the narrowest
// straits between them. Every point of this coral is paired with the nearest land of its copy one
// wedge on, the pairs are ranked by the water between them, and the narrowest are bridged, each
// kBridgeApart from the last so the crossings are spread along the front. Straits near the rim are
// passed over at first - there a bridge would join the two trunks just below their pads into a
// road between homes - and only straits up to kBridgeSearch wide are looked at; where that finds
// nothing (a small map, or two colonies far apart) the search widens until every boundary has a
// bridge, or ground armies could never meet. Chosen once and turned round, every boundary gets
// the same bridges.
void buildBridges(Layout &L, int count)
{
	const Geometry &g = L.g;
	struct Crossing
	{
		double gap;
		StrokePoint from, to;
	};
	std::vector<std::vector<StrokePoint>> images;
	std::vector<PathBounds> imageBounds;
	for (const Piece &piece : L.pieces)
		if (piece.kind == Kind::Branch || piece.kind == Kind::Bud)
		{
			images.push_back(L.turn(piece.path, 1));
			imageBounds.push_back(pathBounds(images.back()));
		}
	const double rim = g.rootRadius - g.padReach - 2 * g.strait;
	std::vector<Crossing> crossings;
	const auto collect = [&](double within, bool awayFromRim)
	{
		crossings.clear();
		for (const Piece &piece : L.pieces)
		{
			if (piece.kind != Kind::Branch && piece.kind != Kind::Bud)
				continue;
			for (size_t m = 0; m < images.size(); ++m)
			{
				const PathBounds &b = imageBounds[m];
				if (std::hypot(b.x - piece.bounds.x, b.y - piece.bounds.y) >
					b.radius + piece.bounds.radius + within)
					continue;
				for (const StrokePoint &p : piece.path)
					for (const StrokePoint &q : images[m])
					{
						if (awayFromRim && std::max(std::hypot(p.x - L.cx, p.y - L.cy),
													std::hypot(q.x - L.cx, q.y - L.cy)) > rim)
							continue;
						if (const double gap =
								std::hypot(p.x - q.x, p.y - q.y) - p.halfWidth - q.halfWidth;
							gap < within)
							crossings.push_back({gap, p, q});
					}
			}
		}
	};
	collect(kBridgeSearch, true);
	if (crossings.empty())
		collect(kBridgeSearch, false);
	if (crossings.empty())
		collect(INFINITY, false);
	std::stable_sort(crossings.begin(), crossings.end(),
					 [](const Crossing &a, const Crossing &b) { return a.gap < b.gap; });
	std::vector<ShapePoint> chosen;
	const double half = kMinimumHalfWidth - 0.5;
	for (const Crossing &c : crossings)
	{
		if (int(chosen.size()) >= count)
			break;
		bool apart = true;
		for (const ShapePoint &at : chosen)
			apart = apart && std::hypot(at.x - c.from.x, at.y - c.from.y) >= kBridgeApart;
		if (!apart)
			continue;
		chosen.push_back({c.from.x, c.from.y});
		L.add({{c.from.x, c.from.y, half}, {c.to.x, c.to.y, half}}, Kind::Bridge, -1);
	}
}

Layout design(const GenerationRequest &request, GenerationContext &context)
{
	Layout L;
	L.t = {1 << request.wDec, 1 << request.hDec};
	L.g = geometryFor(request);
	const Torus &t = L.t;
	const Geometry &g = L.g;
	const CoralOptions o(request);
	const int n = t.size(), teams = g.teams;
	if (!g.padsFit || !g.trunkFits)
	{
		L.failure = "the colonies' corals do not fit on the map";
		return L;
	}
	L.cx = t.w / 2;
	L.stretch = Stretch::toFill(t.w, t.h);
	L.cy = t.h / 2;
	L.phase = context.bounded("coral-layout", 3600) / 3600.0 * 2 * kPi;
	Rolls roll{context.stream("coral-growth")};

	// Colony 0's pad comes first, so every branch keeps a strait from it and, through the copies,
	// from every other colony's pad.
	const ShapePoint pad0 = polarPoint(L.cx, L.cy, g.rootRadius, L.axis(0));
	L.add({{pad0.x, pad0.y, g.padReach}}, Kind::Obstacle, -1);

	// The fan. The trunk leaves the pad's centre heading for the map centre, turned off it by the
	// lean; everything else is growBranches. A branch is added to the design the moment it is
	// accepted, since growBranches keeps it straight after and every later branch must see it.
	ForkStyle style;
	style.spread = g.spread;
	style.spreadJitter = kSpreadJitter;
	style.lengthRatio = g.lengthRatio;
	style.lengthJitter = kLengthJitter;
	style.widthRatio = kWidthRatio;
	style.bend = kBend;
	// A branch shorter than a strait plus the narrowest half width would be a stub that does not
	// clear its parent's water, so forks stop there (at least 5 tiles).
	style.minimumLength = std::max(5.0, kMinimumHalfWidth + g.strait);
	style.minimumHalfWidth = kMinimumHalfWidth;
	const auto accept = [&](const std::vector<StrokePoint> &path, int parent)
	{
		if (!L.keepsClear(path, parent))
			return false;
		L.add(path, Kind::Branch, int(L.tree.size()));
		return true;
	};
	growBranches(L.tree, -1, pad0, L.axis(0) + kPi + g.lean, g.trunkLength, g.trunkHalf, g.levels,
				 style, roll, accept);
	if (L.tree.empty())
	{
		L.failure = "the trunk does not fit between the pads";
		return L;
	}

	// Every fork, rolled for stone once; then a bud on every tip that keeps clear, rolled for its
	// fruit. Both roll whether or not they are used, so the count never shifts later rolls.
	const std::int64_t stonePerMille =
		std::min<std::int64_t>(1000, scaledCount(kForkStone, o.stone));
	for (const Branch &branch : L.tree)
		if (branch.parent >= 0)
			L.forks.push_back(
				{{branch.path.front().x, branch.path.front().y}, roll() * 1000 < stonePerMille});
	for (int b = 0; b < int(L.tree.size()); ++b)
	{
		const StrokePoint &tip = L.tree[b].path.back();
		const int fruit = int(roll() * 3);
		if (!L.tree[b].leaf)
			continue;
		// A little wider than the tip it ends, so a few grass tiles survive inside its beach.
		const std::vector<StrokePoint> bud{{tip.x, tip.y, std::max(4.0, tip.halfWidth + 1.5)}};
		if (L.keepsClear(bud, b))
		{
			L.add(bud, Kind::Bud, b);
			L.buds.push_back({{tip.x, tip.y}, fruit});
		}
	}

	if (teams >= 2 && o.landBridges > 0)
		buildBridges(L, o.landBridges);

	// Stamp every colony's copy: its pad, its fan and buds, its bridges to the next colony.
	L.land.assign(n, 0);
	L.bridge.assign(n, 0);
	L.padOf.assign(n, -1);
	L.budOf.assign(n, -1);
	L.tipOf.assign(n, -1);
	const RadialShape padShape(g.padRadius, kPadRoughness, context, "coral-pads");
	for (int k = 0; k < teams; ++k)
	{
		const ShapePoint centre = L.place(pad0, k);
		L.pads.push_back(centre);
		L.padAngle.push_back(L.stretch.heading(L.axis(k)));
		forEachTileInShape(
			t, centre.x, centre.y, padShape, L.axis(k),
			[&](int i, double, double)
			{
				L.land[i] = 1;
				L.padOf[i] = k;
			},
			L.stretch);
		const std::vector<StrokePoint> trunk = L.place(L.tree[0].path, k);
		std::vector<ShapePoint> line;
		for (const StrokePoint &p : trunk)
			line.push_back({p.x, p.y});
		L.trunkLines.push_back(line);
		// The trunk's tip, where it first forks: the ground every colony must be able to walk to.
		const StrokePoint &tip = trunk.back();
		const int tipReach = int(std::ceil(tip.halfWidth));
		for (int dy = -tipReach; dy <= tipReach; ++dy)
			for (int dx = -tipReach; dx <= tipReach; ++dx)
				if (dx * dx + dy * dy <= tip.halfWidth * tip.halfWidth)
					L.tipOf[t.at(int(std::lround(tip.x)) + dx, int(std::lround(tip.y)) + dy)] = k;
		int budIndex = 0;
		for (const Piece &piece : L.pieces)
		{
			if (piece.kind == Kind::Obstacle)
				continue;
			const std::vector<StrokePoint> turned = L.place(piece.path, k);
			if (piece.kind == Kind::Bud)
			{
				// The same disc strokePath would draw for a one-point path, labelled as it goes.
				const StrokePoint &c = turned.front();
				const int id = k * int(L.buds.size()) + budIndex++;
				const int reach = int(std::ceil(c.halfWidth)) + 1;
				for (int y = int(std::floor(c.y)) - reach; y <= int(std::ceil(c.y)) + reach; ++y)
					for (int x = int(std::floor(c.x)) - reach; x <= int(std::ceil(c.x)) + reach;
						 ++x)
						if ((x - c.x) * (x - c.x) + (y - c.y) * (y - c.y) <
							c.halfWidth * c.halfWidth)
						{
							const int i = t.at(x, y);
							L.land[i] = 1;
							L.budOf[i] = id;
						}
				continue;
			}
			if (piece.kind == Kind::Bridge)
				strokePath(L.bridge, t, turned);
			strokePath(L.land, t, turned);
			// Roads keep routes open, so they run down every branch that forks on and every bridge,
			// but not down the tips: a dead end carries no traffic, and on a tip's narrow land the
			// road would leave next to no grass for its fields.
			if (piece.kind == Kind::Bridge || !L.tree[piece.branch].leaf)
				L.roads.push_back(turned);
		}
	}
	L.water.assign(n, 0);
	for (int i = 0; i < n; ++i)
		L.water[i] = !L.land[i];
	return L;
}

void furnishPads(Map &map, const Layout &L, GenerationContext &context)
{
	const Torus &t = L.t;
	const std::vector<unsigned char> reserved = swarmSurroundings(t, context);
	for (int team = 0; team < L.g.teams; ++team)
	{
		const auto eligible = [&](int i)
		{ return L.padOf[i] == team && !reserved[i] && clearGround(map, i % t.w, i / t.w); };
		// Facing in, towards the trunk.
		const KitFrame frame{int(std::lround(L.pads[team].x)), int(std::lround(L.pads[team].y)),
							 L.padAngle[team] + kPi};
		// Wheat and wood 6 tiles to either side, 3 toward the trunk; stone 8 tiles back toward the
		// rim: the crops sit on the way out to the coral, the quarry behind the swarm, and no patch
		// grows over another.
		plantKit(
			map, t, context,
			{frame.at(3, -6, 8), frame.at(3, 6, 8), frame.at(-8, 0, 6), kHomeWheat, kHomeWood, 2},
			eligible);
	}
}

// The coral's resources, richer the further the ground is from home. Distance is the walk along the
// coral from the nearest pad, normalised to the farthest land: a fork halfway out carries a
// middling stone deposit, a bud at the far end a big grove. Fields are ranked by patch noise
// sampled in the wedge frame, leaning towards home, so the trunk and first forks are farmland.
void stockCoral(Map &map, const Layout &L, GenerationContext &context, const CoralOptions &o)
{
	const Torus &t = L.t;
	const int n = t.size();
	std::vector<unsigned char> pads(n, 0);
	for (int i = 0; i < n; ++i)
		pads[i] = L.padOf[i] >= 0;
	const std::vector<int> steps = stepsFrom(t, pads, L.land);
	const std::vector<unsigned char> &coral = L.land;
	int farthest = 1;
	for (int i = 0; i < n; ++i)
		if (coral[i] && steps[i] >= 0)
			farthest = std::max(farthest, steps[i]);
	const auto remote = [&](double x, double y)
	{
		const int i = t.at(int(std::lround(x)), int(std::lround(y)));
		return steps[i] >= 0 ? std::min(1.0, double(steps[i]) / farthest) : 1.0;
	};
	const auto onBranch = [&](int i)
	{
		return coral[i] && L.padOf[i] < 0 && L.budOf[i] < 0 && !L.bridge[i] &&
			   clearGround(map, i % t.w, i / t.w);
	};

	const int colonies = L.g.teams;
	for (int k = 0; k < colonies; ++k)
		for (const Fork &fork : L.forks)
			if (fork.stone)
			{
				const ShapePoint at = L.place(fork.at, k);
				const int tiles = int(std::lround(
					kForkStoneNear + (kForkStoneFar - kForkStoneNear) * remote(at.x, at.y)));
				if (const int seed =
						seedNear(t, int(std::lround(at.x)), int(std::lround(at.y)), 3, onBranch);
					seed >= 0)
					growPatch(map, t, seed, STONE, tiles, onBranch);
			}

	const int budsPerColony = int(L.buds.size());
	for (int k = 0; k < colonies; ++k)
		for (int b = 0; b < budsPerColony; ++b)
		{
			const Bud &bud = L.buds[b];
			const ShapePoint at = L.place(bud.at, k);
			const int id = k * budsPerColony + b;
			const auto onBud = [&](int i)
			{ return L.budOf[i] == id && clearGround(map, i % t.w, i / t.w); };
			const int tiles = int(scaledCount(
				std::lround(kBudFruitNear + (kBudFruitFar - kBudFruitNear) * remote(at.x, at.y)),
				o.fruit));
			if (tiles <= 0)
				continue;
			if (const int seed =
					seedNear(t, int(std::lround(at.x)), int(std::lround(at.y)), 3, onBud);
				seed >= 0)
				growPatch(map, t, seed, CHERRY + bud.fruit, tiles, onBud);
		}

	const WedgeFrame wedges(t, L.phase, L.g.wedges, L.stretch);
	// patch (10-tile cells) ranks where on the branches the fields go, with kHomeLean tilting them
	// toward home; split (7-tile cells) picks wheat or wood. Both are read in the wedge's frame, so
	// every fan is farmed alike.
	const PeriodicNoise patch(t.w, t.h, 10, context.stream("coral-patch"));
	const PeriodicNoise split(t.w, t.h, 7, context.stream("coral-split"));
	std::vector<std::pair<double, int>> ranked;
	std::vector<double> splitKey(n, 0.0);
	for (int i = 0; i < n; ++i)
		if (onBranch(i))
		{
			const WedgeFrame::Cell cell = wedges.cell(i % t.w, i / t.w);
			const double home = steps[i] >= 0 ? 1 - std::min(1.0, double(steps[i]) / farthest) : 0;
			ranked.push_back({-(patch.at(cell.s, cell.d) + kHomeLean * home), i});
			splitKey[i] = split.at(cell.s, cell.d);
		}
	std::stable_sort(ranked.begin(), ranked.end());
	std::vector<int> tiles;
	for (const auto &entry : ranked)
		tiles.push_back(entry.second);
	const std::int64_t area = std::int64_t(tiles.size());
	plantFields(map, t, tiles, int(scaledCount(area * kWheatShare / 100, o.wheat)),
				int(scaledCount(area * kWoodShare / 100, o.wood)),
				[&](int i) { return splitKey[i]; });
}

bool generate(Game &game, GenerationContext &context)
{
	context.stage = "coral layout";
	const CoralOptions o(context.request);
	Map &map = game.map;
	const int teams = context.request.nbTeams;
	map.makeHomogenMap(WATER);
	for (int i = 0; i < teams; ++i)
		game.addTeam();
	const Layout L = design(context.request, context);
	if (!L.failure.empty())
	{
		context.detail = L.failure;
		return false;
	}
	const Torus &t = L.t;
	const int n = t.size();

	context.stage = "coral terrain";
	TerrainSketch terrain(n, GRASS);
	for (int i = 0; i < n; ++i)
		if (L.water[i])
			terrain[i] = WATER;
	// A few dry patches along the branches, sampled in the wedge frame so every colony's fan gets
	// the same ones; never on a pad, a bud or a bridge.
	{
		std::vector<unsigned char> branches(n, 0);
		for (int i = 0; i < n; ++i)
			branches[i] = L.land[i] && L.padOf[i] < 0 && L.budOf[i] < 0 && !L.bridge[i];
		const WedgeFrame wedges(t, L.phase, L.g.wedges, L.stretch);
		const PeriodicNoise dry(t.w, t.h, kSandCell, context.stream("coral-sand"));
		sprinkleSand(terrain, t, branches, kSandShare, kSandInland,
					 [&](int i)
					 {
						 const WedgeFrame::Cell cell = wedges.cell(i % t.w, i / t.w);
						 return dry.at(cell.s, cell.d);
					 });
	}
	// The sand roads: a line of sand one tile thick (tracePath, the thinnest there is) down the
	// middle of every branch that forks on and every bridge, off the pads and the buds, so homes
	// keep their building room and every tip its grove. Every tile touching a sand corner loses the
	// pure grass a deposit or a building needs, so nothing can grow or be built across the road.
	if (o.sandRoads)
	{
		std::vector<unsigned char> road(n, 0);
		for (const std::vector<StrokePoint> &line : L.roads)
			tracePath(road, t, line);
		for (int i = 0; i < n; ++i)
			if (road[i] && terrain[i] == GRASS && L.padOf[i] < 0 && L.budOf[i] < 0)
				terrain[i] = SAND;
	}
	layBeaches(terrain, t);
	writeUndermap(map, terrain);

	context.stage = "coral colonies";
	const auto pad = [&](int team)
	{
		std::vector<unsigned char> home(n, 0);
		for (int i = 0; i < n; ++i)
			home[i] = L.padOf[i] == team && map.isGrass(i % t.w, i / t.w);
		return home;
	};
	// The swarm stands a little seaward of the pad's middle, so the kit and the trunk lie in front
	// of it; placeSettlement measures from the footprint's top-left.
	const auto anchor = [&](int team)
	{
		const ShapePoint p = polarPoint(L.pads[team].x, L.pads[team].y, 3, L.padAngle[team]);
		return MapGeneratorPoint(int(std::lround(p.x)) - 2, int(std::lround(p.y)) - 2);
	};
	if (!settleColonies(game, context, "coral-starts", pad, anchor))
		return false;

	context.stage = "coral resources";
	furnishPads(map, L, context);
	stockCoral(map, L, context, o);
	// Algae only grows in water with solid sand within reach (see algaeGrowthChance), so it is
	// counted over the shallows but seeded on each colony's best-growing water, the same number of
	// clumps in every wedge.
	const WedgeFrame algaeWedges(t, L.phase, L.g.wedges, L.stretch);
	seedAlgae(map, context, t, "coral-algae", o.algae,
			  AlgaeBand::shallows(1, 10, kAlgaeTilesPerClump).thriving(kAlgaeBestShare),
			  &algaeWedges);
	secureStartingCrops(game, context, t);

	// Deposits only land on grass and beaches keep every branch walkable along its edge, but a thin
	// trunk can still be stocked shut. Keep every colony's walk to its own trunk tip open, and with
	// bridges, colony 0's walk to every colony.
	context.stage = "coral roads";
	const std::vector<std::vector<int>> workers = unitTilesByTeam(map, teams);
	for (int team = 0; team < teams; ++team)
	{
		std::vector<unsigned char> tip(n, 0);
		for (int i = 0; i < n; ++i)
			tip[i] = L.tipOf[i] == team;
		if (workers[team].empty() || !openRoad(map, t, workers[team], tip))
		{
			context.detail = "colony " + std::to_string(team) + " cannot walk to its trunk's tip";
			return false;
		}
	}
	if (o.landBridges > 0)
		for (int team = 1; team < teams; ++team)
			if (!openRoad(map, t, workers[0], tileMask(t, workers[team])))
			{
				context.detail = "no walk from colony 0 reaches colony " + std::to_string(team);
				return false;
			}
	return true;
}

std::string validateRequest(const GenerationRequest &r)
{
	if (r.nbTeams < 1)
		return "Coral needs at least one colony.";
	// Below 128 tiles across, a pad and a strait leave the fan nowhere to grow.
	if (std::min(r.wDec, r.hDec) < 7)
		return "Coral needs a map at least 128 tiles across.";
	const Geometry g = geometryFor(r);
	if (!g.padsFit)
		return "Too many colonies for this map; use a bigger map or fewer colonies.";
	// A trunk can clear its pad and still not fit: with many colonies and wide trunks or straits,
	// neighbouring trunks crowd each other before they fork. That only happens on maps smaller than
	// 256 tiles, where the design is quick to rebuild, so there it is rebuilt and the request
	// refused with advice rather than failing every roll during generation.
	const auto crowded = [&]
	{
		GenerationContext trial(r);
		return g.half < kReferenceHalf && !design(r, trial).failure.empty();
	};
	if (!g.trunkFits || crowded())
		return "The coral does not fit this map; use a bigger map, narrower straits or fewer "
			   "colonies.";
	return "";
}

// Checked on the finished world: every trunk is still land from its pad to its tip, every colony
// can walk to its own trunk's tip, and with land bridges colony 0 can walk to every colony; water,
// buildings and every resource block.
std::string validateWorld(const Game &game, const GenerationContext &context)
{
	GenerationContext replay(context.request);
	const Layout L = design(context.request, replay);
	const Map &map = game.map;
	if (const std::string mismatch = designMismatch(L, map, "coral"); !mismatch.empty())
		return mismatch;
	const Torus &t = L.t;
	const int teams = context.request.nbTeams;
	for (size_t k = 0; k < L.trunkLines.size(); ++k)
		for (const ShapePoint &p : L.trunkLines[k])
			if (map.isWater(t.x(int(std::lround(p.x))), t.y(int(std::lround(p.y)))))
				return "Colony " + std::to_string(k) + "'s trunk is broken by water.";
	const std::vector<std::vector<int>> workers = unitTilesByTeam(map, teams);
	const std::vector<unsigned char> walkable = walkableTiles(map);
	for (int k = 0; k < teams; ++k)
	{
		if (workers[k].empty())
			return "Colony " + std::to_string(k) + " has no workers.";
		const std::vector<int> steps = stepsFrom(t, tileMask(t, workers[k]), walkable);
		bool reached = false;
		for (int i = 0; i < t.size() && !reached; ++i)
			reached = L.tipOf[i] == k && steps[i] >= 0;
		if (!reached)
			return "Colony " + std::to_string(k) + " cannot walk to its trunk's tip.";
	}
	if (CoralOptions(context.request).landBridges > 0 && L.g.teams >= 2)
	{
		const ColonyWalk walk = walkFromFirstColony(map, teams, "the coral", "over the bridges");
		if (!walk.error.empty())
			return walk.error;
	}
	return "";
}
} // namespace

CoralOptions::CoralOptions(const GenerationRequest &r)
	: branching(r.option("branching")), forkAngle(r.option("fork-angle")),
	  branchWidth(r.option("branch-width")), straitWidth(r.option("strait-width")),
	  lean(r.option("lean")), landBridges(r.option("land-bridges")),
	  homeSize(r.option("home-size")), sandRoads(r.option("sand-roads") != 0),
	  wheat(r.option("wheat-amount")), wood(r.option("wood-amount")),
	  stone(r.option("stone-amount")), algae(r.option("algae-amount")),
	  fruit(r.option("fruit-amount"))
{
}

GeneratorDefinition coralDefinition()
{
	return {
		"coral",
		21,
		"Coral",
		1,
		false,
		// Forks from the trunk to the tips; how far each fork's children turn from their parent's
		// heading, in degrees either side; the trunk's width in tiles (branches taper from it); the
		// water kept between any two pieces of land; how far every trunk is turned off the line to
		// the map centre, in degrees; land bridges per pair of neighbours.
		{{"branching", "Branching", 3, 9, 1, 6, ControlGroup::Terrain},
		 {"fork-angle", "Fork angle", 10, 40, 2, 28, ControlGroup::Terrain},
		 {"branch-width", "Branch width", 7, 17, 2, 11, ControlGroup::Terrain},
		 {"strait-width", "Strait width", 3, 12, 1, 5, ControlGroup::Terrain},
		 {"lean", "Lean", 0, 60, 5, 25, ControlGroup::Layout},
		 {"land-bridges", "Land bridges", 0, 3, 1, 1, ControlGroup::Layout},
		 // Each home pad's radius as a percentage of the standard pad.
		 {"home-size", "Home size", 60, 200, 10, 130, ControlGroup::Layout},
		 // Off, the branches are grass from shore to shore, with no sand road down the middle.
		 GeneratorControl::toggle("sand-roads", "Sand roads", true, ControlGroup::Layout),
		 // The branches' standing wheat and wood, the forks' stone, the buds' fruit and the
		 // shallows' algae; every pad's kit is unscaled.
		 GeneratorControl::percentage("wheat-amount", "Wheat amount"),
		 GeneratorControl::percentage("wood-amount", "Wood amount"),
		 GeneratorControl::percentage("stone-amount", "Stone amount"),
		 GeneratorControl::percentage("algae-amount", "Algae amount"),
		 GeneratorControl::percentage("fruit-amount", "Fruit amount")},
		generate,
		true,
		validateRequest,
		validateWorld};
}
