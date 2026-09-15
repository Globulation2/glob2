// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "Geometry.h"
#include "Grid.h"
#include <algorithm>
#include <cmath>
#include <utility>
#include <random>
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

/// A path that wanders from `from` to `to` the short way round the torus: points about a tile apart,
/// pushed sideways by up to `wander` tiles (three harmonics with random amplitudes, zero at both ends,
/// so it leaves and arrives exactly where asked), and a half width of `halfWidth` swelling and
/// narrowing by up to `widthJitter` of itself. `to` is placed beside `from` (its nearest copy), so
/// points may run past the seam, as strokePath expects. Six draws from `random`. A tunnel between two
/// chambers, a lane between two plazas, a trail that doesn't look ruled.
std::vector<StrokePoint> wanderingPath(const Torus &, ShapePoint from, ShapePoint to,
									   double halfWidth, double wander, double widthJitter,
									   std::mt19937 &random);

/// Strokes a wanderingPath into `mask` with `value`: a corridor carved from one point to another.
void carveCorridor(std::vector<unsigned char> &mask, const Torus &, ShapePoint from, ShapePoint to,
				   double halfWidth, double wander, double widthJitter, std::mt19937 &random,
				   unsigned char value = 1);

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

/// Visits every tile a Teardrop centred at (cx, cy) with its axis along `heading` (radians, head
/// to tail) covers, through the wrap: `visit(tile, along, across)` with the tile's place in the
/// shape's own frame. Searches only the bounding box of the length. Every drumlin of a field is
/// stamped this way at the field's one heading.
template <typename Visit>
void forEachTileInTeardrop(const Torus &t, double cx, double cy, double heading,
						   const Teardrop &shape, Visit visit)
{
	const int reach = int(std::ceil(shape.length / 2)) + 1;
	const int x0 = int(std::lround(cx)), y0 = int(std::lround(cy));
	const double c = std::cos(heading), s = std::sin(heading);
	for (int y = y0 - reach; y <= y0 + reach; ++y)
		for (int x = x0 - reach; x <= x0 + reach; ++x)
		{
			const double dx = x - cx, dy = y - cy;
			const double along = dx * c + dy * s, across = -dx * s + dy * c;
			if (shape.contains(along, across))
				visit(t.at(x, y), along, across);
		}
}

/// Sets `value` on every tile of a Teardrop centred at (cx, cy) along `heading`.
inline void fillTeardrop(std::vector<unsigned char> &mask, const Torus &t, double cx, double cy,
						 double heading, const Teardrop &shape, unsigned char value = 1)
{
	forEachTileInTeardrop(t, cx, cy, heading, shape,
						  [&](int i, double, double) { mask[i] = value; });
}

/// Points along the arc `radius` tiles round (cx, cy) from angle `from` to angle `to` (radians,
/// either way round), about `step` tiles apart along the arc, all with the same half width: a
/// corridor that follows a circle, or a ring drawn a piece at a time.
inline std::vector<StrokePoint> arcPath(double cx, double cy, double radius, double from, double to,
										double halfWidth, double step = 3)
{
	const int segments =
		std::max(1, int(std::ceil(std::abs(to - from) * radius / std::max(0.5, step))));
	std::vector<StrokePoint> path;
	path.reserve(segments + 1);
	for (int i = 0; i <= segments; ++i)
	{
		const double a = from + (to - from) * i / segments;
		path.push_back({cx + radius * std::cos(a), cy + radius * std::sin(a), halfWidth});
	}
	return path;
}

/// A zigzag in a frame, the trail a switchback climbs: it starts at `start` along the frame on the
/// left side (`span` across), runs down that side to the first leg at `firstLeg`, crosses to the
/// other side, turns down that side by `pitch`, crosses back, and so on for `legs` legs, then runs
/// along whichever side it finished on to `finish`. `legs` holds each leg's straight run on its own,
/// short of the turns at its ends by the path's half width, so a caller can tell leg from turn.
/// `pitch` is signed: positive steps the legs back towards the frame's origin.
struct Zigzag
{
	std::vector<StrokePoint> path;
	std::vector<std::vector<StrokePoint>> legs;
	double finishAcross = 0; // the side the path finishes on
};
Zigzag zigzagPath(const AxisFrame &, double start, double firstLeg, double pitch, int legs,
				  double span, double finish, double halfWidth);

/// Visits every tile within `halfWidth` of the circle of `radius` round (cx, cy), through the wrap:
/// `visit(tile, gate)` with the index into `gates` of the gate the tile lies in - a gap reaching
/// `gateHalfWidth` tiles either side of the ring's point at that angle (radians), measured along the
/// ring - or -1 for the ring itself. A wall of stone round an arena with ramps through it, or a moat
/// with bridges over it.
template <typename Visit>
void ringWithGates(const Torus &t, double cx, double cy, double radius, double halfWidth,
				   const std::vector<double> &gates, double gateHalfWidth, Visit visit)
{
	const int reach = int(std::ceil(radius + halfWidth)) + 1;
	const int x0 = int(std::lround(cx)), y0 = int(std::lround(cy));
	for (int y = y0 - reach; y <= y0 + reach; ++y)
		for (int x = x0 - reach; x <= x0 + reach; ++x)
		{
			const double dx = x - cx, dy = y - cy, r = std::hypot(dx, dy);
			if (std::abs(r - radius) >= halfWidth)
				continue;
			const double angle = std::atan2(dy, dx);
			int gate = -1;
			for (size_t g = 0; g < gates.size() && gate < 0; ++g)
			{
				const double turn = std::remainder(angle - gates[g], 2 * kPi);
				if (std::abs(turn) * r <= gateHalfWidth)
					gate = int(g);
			}
			visit(t.at(x, y), gate);
		}
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

/// Exact geometry for designs whose rasterization must agree with itself to the tile: lines and
/// polygons on a fixed-point grid of kSubtile units per tile, computed in integers, so two polygons
/// that share an edge split its tiles between them the same way on every platform and on both
/// sides of the wrap. A tile (x, y) spans [x, x + 1) * kSubtile on each axis; subtileCentre is the
/// point in its middle.
constexpr int kSubtile = 16;
struct SubtilePoint
{
	long long x = 0, y = 0;
	bool operator==(const SubtilePoint &o) const { return x == o.x && y == o.y; }
};
inline SubtilePoint subtileCentre(int tileX, int tileY)
{
	return {tileX * (long long)kSubtile + kSubtile / 2, tileY * (long long)kSubtile + kSubtile / 2};
}
/// The tile a fixed-point position lies in, before wrapping.
inline long long subtileTile(long long v)
{
	return v >= 0 ? v / kSubtile : -((-v + kSubtile - 1) / kSubtile);
}

/// Every tile the segment from `a` to `b` passes through, end tiles included, as unwrapped tile
/// coordinates in order from `a`. Where the segment crosses a tile corner exactly, both tiles beside
/// the corner are listed as well, so wherever two tiles of the line touch only at a corner, the two
/// tiles across that corner are on the line too: no unit can step across it, even diagonally,
/// however the segment slants.
std::vector<std::pair<long long, long long>> sealedSegmentTiles(SubtilePoint a, SubtilePoint b);

/// Sets `value` on every tile of sealedSegmentTiles along each segment of `points`, through the
/// wrap: a wall line that seals at any angle. A closed path joins its last point back to its first.
void traceSealedPath(std::vector<unsigned char> &mask, const Torus &,
					 const std::vector<SubtilePoint> &points, unsigned char value = 1,
					 bool closed = false);

/// A sealed line right round the torus along one axis: a vertex every `step` tiles at u = 0,
/// step, 2 * step, ... and a last one at u = length, the first vertex's image past the seam, each
/// at the tile `vAt(u)` across (rounded), traced with traceSealedPath so that nothing steps over
/// it at any slant and the seam is crossed rather than the map run back across. A wall of bluffs
/// along the back of a belt that wraps the map the long way.
template <typename VAt>
void traceSealedLap(std::vector<unsigned char> &mask, const Torus &t, bool alongX, int step,
					VAt vAt, unsigned char value = 1)
{
	const int length = alongX ? t.w : t.h;
	std::vector<SubtilePoint> line;
	for (int u = 0; u <= length; u += std::max(1, step))
	{
		const int v = int(std::lround(vAt(u)));
		line.push_back(alongX ? subtileCentre(u, v) : subtileCentre(v, u));
	}
	if (line.size() < 2 || (length % std::max(1, step)) != 0)
	{
		// A step that does not divide the lap still needs the closing vertex at u = length.
		const int v = int(std::lround(vAt(length)));
		line.push_back(alongX ? subtileCentre(length, v) : subtileCentre(v, length));
	}
	traceSealedPath(mask, t, line, value, false);
}

/// Visits each tile whose centre lies inside the polygon `outline` (any simple polygon, either
/// winding, coordinates unwrapped) with its wrapped tile index. A centre exactly on an edge
/// belongs to the side to its right, or below for a horizontal edge, so polygons that tile the
/// plane with shared corners cover every tile exactly once between them.
template <typename Visit>
void forEachTileInPolygon(const Torus &t, const std::vector<SubtilePoint> &outline, Visit visit);

/// Sets `value` on every tile forEachTileInPolygon visits.
void fillPolygon(std::vector<unsigned char> &mask, const Torus &,
				 const std::vector<SubtilePoint> &outline, unsigned char value = 1);

template <typename Visit>
void forEachTileInPolygon(const Torus &t, const std::vector<SubtilePoint> &outline, Visit visit)
{
	const size_t n = outline.size();
	if (n < 3)
		return;
	long long top = outline[0].y, bottom = outline[0].y;
	for (const SubtilePoint &p : outline)
	{
		top = std::min(top, p.y);
		bottom = std::max(bottom, p.y);
	}
	// A row's centre line y = row * kSubtile + kSubtile / 2 meets an edge when it lies in the edge's
	// half-open span [lower y, upper y); the crossing's x is kept as a fraction, num / den.
	struct Crossing
	{
		long long num, den;
	};
	std::vector<Crossing> crossings;
	for (long long row = subtileTile(top); row <= subtileTile(bottom); ++row)
	{
		const long long yc = row * kSubtile + kSubtile / 2;
		crossings.clear();
		for (size_t k = 0; k < n; ++k)
		{
			SubtilePoint a = outline[k], b = outline[(k + 1) % n];
			if (a.y == b.y)
				continue;
			if (a.y > b.y)
				std::swap(a, b);
			if (yc < a.y || yc >= b.y)
				continue;
			crossings.push_back({a.x * (b.y - a.y) + (yc - a.y) * (b.x - a.x), b.y - a.y});
		}
		std::sort(crossings.begin(), crossings.end(), [](const Crossing &p, const Crossing &q)
				  { return p.num * q.den < q.num * p.den; });
		for (size_t k = 0; k + 1 < crossings.size(); k += 2)
		{
			// Tiles whose centre xc satisfies left <= xc < right, compared exactly.
			const Crossing &left = crossings[k], &right = crossings[k + 1];
			auto firstCentreAtOrAfter = [](const Crossing &c)
			{
				// Smallest column whose centre column * kSubtile + kSubtile / 2 >= num / den.
				const long long shifted = c.num - (long long)(kSubtile / 2) * c.den;
				const long long span = (long long)kSubtile * c.den;
				return shifted >= 0 ? (shifted + span - 1) / span : -((-shifted) / span);
			};
			const long long from = firstCentreAtOrAfter(left), to = firstCentreAtOrAfter(right);
			for (long long column = from; column < to; ++column)
				visit(t.at(int(column), int(row)));
		}
	}
}
} // namespace MapGeneration
