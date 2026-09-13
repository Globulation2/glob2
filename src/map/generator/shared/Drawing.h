// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "Geometry.h"
#include "Grid.h"
#include <algorithm>
#include <cmath>
#include <vector>
namespace MapGeneration
{
// Drawing on the torus: thick lines (threads, roads, channels, walls) as a path of points with a
// width at each, and filled radial shapes, rasterized onto tile masks. Coordinates are map tiles
// and may run past the edges; every tile is written through the wrap.

/// A point on a stroked path and the path's half width there. The width is interpolated linearly
/// between points, and every point is a round joint, so a path turns without gaps.
struct StrokePoint
{
	double x, y, halfWidth;
};

/// The point `radius` tiles from (cx, cy) at `angle` radians.
inline ShapePoint polarPoint(double cx, double cy, double radius, double angle)
{
	return {cx + radius * std::cos(angle), cy + radius * std::sin(angle)};
}

/// Sets `value` on every tile whose centre lies within the path's half width of it: a tile at
/// (x, y) is covered when its distance to the nearest segment is under the half width
/// interpolated at the nearest point. A closed path joins its last point back to its first.
void strokePath(std::vector<unsigned char> &mask, const Torus &, const std::vector<StrokePoint> &,
				unsigned char value = 1, bool closed = false);

/// Sets `value` on a line one tile thick through the path's points: each segment is traced from
/// its rounded ends with Bresenham's steps, so consecutive tiles always touch, at least at a corner,
/// and the line has no gaps. Half widths are ignored. The thinnest stroke there is: a sand road
/// down the middle of land that must stay open.
void tracePath(std::vector<unsigned char> &mask, const Torus &, const std::vector<StrokePoint> &,
			   unsigned char value = 1);

/// `segments` + 1 points along the quadratic Bezier from `from` through the pull of `control`
/// to `to`, with the half width running linearly from one end's to the other's: a thread that
/// sags or bows.
std::vector<StrokePoint> bezierPath(ShapePoint from, ShapePoint control, ShapePoint to,
									double fromHalfWidth, double toHalfWidth, int segments);
/// `segments` + 1 points along a path `length` tiles long leaving `from` along `heading`
/// (radians), bowing sideways by `bend` times its length at its middle (positive bows to the
/// left of the heading), its half width running from `fromHalfWidth` to `toHalfWidth`: a branch,
/// a twig or a root. A quadratic Bezier whose control point sits off the chord's middle.
std::vector<StrokePoint> bentPath(ShapePoint from, double heading, double length, double bend,
								  double fromHalfWidth, double toHalfWidth, int segments);

/// The narrowest gap between two stroked paths: the least distance from any point of either to
/// the other's centre line, less both half widths there. Negative where they overlap. Points of
/// `a` nearer than `skip` tiles to its first point are ignored, so a branch can be measured
/// against the land it grows out of without its own root counting as a collision. The paths are
/// compared as given, without the wrap.
double pathClearance(const std::vector<StrokePoint> &a, const std::vector<StrokePoint> &b,
					 double skip = 0);

/// The bounding circle of a path's stroke, for cheap early outs before pathClearance.
struct PathBounds
{
	double x, y, radius;
};
PathBounds pathBounds(const std::vector<StrokePoint> &);

/// One branch of a tree grown by forking (growBranches): its stroked path, the index of the
/// branch it forked from (-1 for the root) and how many forks separate it from the root.
struct Branch
{
	std::vector<StrokePoint> path;
	int parent, depth;
	PathBounds bounds;
	double heading;   // at its tip
	bool leaf = true; // no child was accepted
};

/// How a tree forks. Every fork splits a tip in two, one child either side of the parent's
/// heading, each turned by `spread` radians give or take `spreadJitter` of that; children are
/// `lengthRatio` and `widthRatio` of their parent, lengths varied by `lengthJitter`, and each
/// bows by up to `bend` of its length. A child shorter than `minimumLength` is never proposed,
/// so a tree stops by itself as its twigs shrink.
struct ForkStyle
{
	double spread = 0.5, spreadJitter = 0.3;
	double lengthRatio = 0.7, lengthJitter = 0.25, widthRatio = 0.75;
	double bend = 0.1, minimumLength = 4, minimumHalfWidth = 1;
};

/// Grows a forking tree from one branch, a whole level at a time, and returns how many branches it
/// added.
///
/// The root runs `length` tiles from `from` along `heading`; at its tip it forks in two, and each
/// child forks again until `forks` levels are spent or the twigs fall below the style's minimum.
/// Every branch is offered to `accept(path, parentIndex)` before it is kept, so the caller decides
/// what may grow: a tree that must keep water between its twigs and other land refuses the ones
/// that come too close. A refused branch is offered once more at half its length before it and
/// everything that would have grown from it are dropped, which lets a tree fill a tight space
/// with stubs rather than leave it bare.
///
/// Growth is breadth first: every branch of one level is offered before any of the next. Grown
/// depth first, the first child's whole subtree would claim the room before its sibling had grown
/// at all, and a tree whose twigs compete for space would come out lopsided; level by level, the
/// two sides of every fork compete on equal terms and the tree fans out evenly.
///
/// `roll()` returns a number in [0, 1). Each branch draws its bend and both children's turns and
/// lengths when it is offered, so its retry at half length reuses them; the rolls of a refused
/// branch's subtree are never drawn. Growth is deterministic - the same rolls and the same answers
/// from `accept` grow the same tree - so a generator that grows one tree and turns it round the map
/// gives every colony an identical copy.
template <typename Roll, typename Accept>
int growBranches(std::vector<Branch> &tree, int parent, ShapePoint from, double heading,
				 double length, double halfWidth, int forks, const ForkStyle &style, Roll &roll,
				 Accept &accept)
{
	// A branch waiting to be offered: where it starts and how it runs.
	struct Sprout
	{
		int parent;
		ShapePoint from;
		double heading, length, halfWidth;
		int forks;
	};
	std::vector<Sprout> queue{{parent, from, heading, length, halfWidth, forks}};
	int added = 0;
	for (size_t head = 0; head < queue.size(); ++head)
	{
		const Sprout sprout = queue[head];
		const double tipHalfWidth =
			std::max(style.minimumHalfWidth, sprout.halfWidth * style.widthRatio);
		const double bend = (2 * roll() - 1) * style.bend;
		double turns[2], lengths[2];
		for (int side = 0; side < 2; ++side)
		{
			const double turn = style.spread * (1 + (2 * roll() - 1) * style.spreadJitter);
			const double stretch = 1 + (2 * roll() - 1) * style.lengthJitter;
			turns[side] = (side ? 1 : -1) * turn;
			lengths[side] = sprout.length * style.lengthRatio * stretch;
		}
		for (const double share : {1.0, 0.5})
		{
			const double reach = sprout.length * share;
			if (reach < style.minimumLength)
				break;
			const int segments = std::max(2, int(std::ceil(reach / 3)));
			std::vector<StrokePoint> path = bentPath(sprout.from, sprout.heading, reach, bend,
													 sprout.halfWidth, tipHalfWidth, segments);
			if (!accept(path, sprout.parent))
				continue;
			const StrokePoint tip = path.back(), before = path[path.size() - 2];
			const int self = int(tree.size());
			const int depth = sprout.parent < 0 ? 0 : tree[sprout.parent].depth + 1;
			const PathBounds bounds = pathBounds(path);
			const double tipHeading = std::atan2(tip.y - before.y, tip.x - before.x);
			tree.push_back({std::move(path), sprout.parent, depth, bounds, tipHeading});
			if (sprout.parent >= 0)
				tree[sprout.parent].leaf = false;
			++added;
			if (sprout.forks > 0)
				for (int side = 0; side < 2; ++side)
					if (lengths[side] * share >= style.minimumLength)
						queue.push_back({self,
										 {tip.x, tip.y},
										 tipHeading + turns[side],
										 lengths[side] * share,
										 tipHalfWidth,
										 sprout.forks - 1});
			break;
		}
	}
	return added;
}

/// Visits every tile a RadialShape centred at (cx, cy) and turned by `turn` radians covers,
/// searching only its bounding box: `visit(tile, dx, dy)` with the tile's offset from the centre.
/// Wrapped tiles are visited through the torus, each once while the shape is narrower than the
/// map. Turning one shape to each of several headings stamps the same outline round a centre. With
/// a `stretch` the outline is stretched with it, so a round pad on a design becomes an oval on a
/// rectangular map.
template <typename Visit>
void forEachTileInShape(const Torus &t, double cx, double cy, const RadialShape &shape, double turn,
						Visit visit, const Stretch &stretch = {})
{
	const int reachX = int(std::ceil(shape.maximumRadius() * stretch.sx)) + 1;
	const int reachY = int(std::ceil(shape.maximumRadius() * stretch.sy)) + 1;
	const int x0 = int(std::lround(cx)), y0 = int(std::lround(cy));
	for (int y = y0 - reachY; y <= y0 + reachY; ++y)
		for (int x = x0 - reachX; x <= x0 + reachX; ++x)
		{
			const double dx = x - cx, dy = y - cy;
			const ShapePoint round = stretch.undo(dx, dy);
			if (std::hypot(round.x, round.y) < shape.radiusAt(std::atan2(round.y, round.x) - turn))
				visit(t.at(x, y), dx, dy);
		}
}

/// Sets `value` on every tile of a RadialShape centred at (cx, cy), turned by `turn`, stretched by
/// `stretch`.
inline void fillShape(std::vector<unsigned char> &mask, const Torus &t, double cx, double cy,
					  const RadialShape &shape, double turn = 0, unsigned char value = 1,
					  const Stretch &stretch = {})
{
	forEachTileInShape(
		t, cx, cy, shape, turn, [&](int i, double, double) { mask[i] = value; }, stretch);
}

/// A path laid out round (cx, cy) placed on the map by `stretch`: its points move, its half widths
/// stay in tiles.
inline std::vector<StrokePoint> stretchPath(const std::vector<StrokePoint> &path, double cx,
											double cy, const Stretch &stretch)
{
	std::vector<StrokePoint> placed;
	placed.reserve(path.size());
	for (const StrokePoint &p : path)
	{
		const ShapePoint q = stretch.apply(cx, cy, {p.x, p.y});
		placed.push_back({q.x, q.y, p.halfWidth});
	}
	return placed;
}
} // namespace MapGeneration
