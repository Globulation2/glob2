// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <algorithm>
#include <cmath>
#include <array>
#include <string>
#include <vector>
struct GenerationContext;
namespace MapGeneration
{
constexpr double kPi = 3.14159265358979323846;
struct ShapePoint
{
	double x, y;
};

/// How a layout designed round a map's centre, in a circle on the map's shorter side, fills a
/// rectangular map: offsets from the centre are scaled by `sx` across and `sy` down, so the circle
/// becomes an ellipse touching all four sides instead of leaving the long ends empty. Widths drawn
/// round a stretched point (a thread, a branch, a beach) stay in tiles, so land is no fatter along
/// the long axis. On a square map both scales are exactly 1 and nothing moves.
struct Stretch
{
	double sx = 1, sy = 1;
	/// The stretch that fills a map of this size.
	static Stretch toFill(int width, int height)
	{
		const double shorter = std::min(width, height);
		return {width / shorter, height / shorter};
	}
	/// Design point `p`, laid out round (cx, cy), placed on the map.
	ShapePoint apply(double cx, double cy, ShapePoint p) const
	{
		// Unstretched points are returned untouched rather than moved out and back, which could
		// change a square map's arithmetic in the last bit.
		if (sx == 1 && sy == 1)
			return p;
		return {cx + (p.x - cx) * sx, cy + (p.y - cy) * sy};
	}
	/// A map offset from the centre, back in the design's round frame.
	ShapePoint undo(double dx, double dy) const { return {dx / sx, dy / sy}; }
	/// The map heading of a design heading, once stretched.
	double heading(double angle) const
	{
		// Unstretched, the heading is returned as it is, not rebuilt through atan2, so a square
		// map's trigonometry is untouched to the last bit.
		return sx == sy ? angle : std::atan2(std::sin(angle) * sy, std::cos(angle) * sx);
	}
	/// The larger of the two scales: how much farther any design distance may reach on the map.
	double longest() const { return std::max(sx, sy); }
};
/// A frame laid along a heading from an origin: `at(along, across)` is the point `along` tiles down
/// the heading (radians) and `across` tiles to its left, and `project` turns a map offset from the
/// origin back into (along, across). A colony's axis out from the map's middle is the usual frame:
/// a trail, a gate or a tower designed once in it lands the same way round every colony.
struct AxisFrame
{
	double x, y, angle;
	ShapePoint at(double along, double across) const
	{
		return {x + along * std::cos(angle) - across * std::sin(angle),
				y + along * std::sin(angle) + across * std::cos(angle)};
	}
	ShapePoint project(double dx, double dy) const
	{
		return {dx * std::cos(angle) + dy * std::sin(angle), -dx * std::sin(angle) + dy * std::cos(angle)};
	}
};
// Area-preserving stretch and rotation. Geometry stays independent of terrain and wrapping.
class ShapeTransform
{
  public:
	ShapeTransform(ShapePoint center, double rotation, double stretch = 1.0);
	ShapePoint toMap(ShapePoint p) const;
	ShapePoint toShape(ShapePoint p) const;

  private:
	ShapePoint center;
	double cosine, sine, stretch;
};
class RadialShape
{
  public:
	RadialShape(double radius, double roughness, GenerationContext &, const std::string &stream,
				double amplitudeMaximum = 1.0);
	double radiusAt(double radians) const;
	double maximumRadius() const;

  private:
	double radius;
	std::array<double, 4> amplitude, phase;
};
/// A teardrop: the outline of a drumlin, a barchan's body, a raindrop. `length` runs along its axis
/// from the blunt head to the tapered tail, and it is `width` across at its widest, which lies
/// `headShare` of the length back from the head (0.4 is a drumlin's: a short round head, a long
/// tail). The outline is two half-ellipses sharing that widest cross-section, a short one for the
/// head and a long one for the tail, so it is smooth with no corner where they meet. Points are
/// measured from the middle of the length: `along` positive towards the tail, `across` either side.
struct Teardrop
{
	double length, width, headShare = 0.4;
	/// Half the width at `along` (0 beyond either end).
	double halfWidthAt(double along) const;
	bool contains(double along, double across) const
	{
		return std::abs(across) < halfWidthAt(along);
	}
	/// The farthest any point of the outline lies from the middle, once `along` is divided by
	/// `stretch` (the length's ratio to the width, say): the radius of the disc that holds the
	/// shape in a frame squashed along its axis. The blunt head bulges past a disc of half the
	/// width, so it is a little over half the width even when length is width times stretch.
	double reach(double stretch) const;
	/// The teardrop, `stretch` times as long as wide, whose reach under `stretch` is exactly
	/// `radius`: the biggest that fits a landform's packed radius (packLandforms, Points.h).
	static Teardrop fitting(double radius, double stretch, double headShare = 0.4);
};
// Rasterizes a shape into a label grid; callers decide what the labels mean.
// Wrapping visits each tile once using its nearest image relative to the shape center.
void stampShape(std::vector<int> &labels, int width, int height, int label, const ShapeTransform &,
				const RadialShape &, bool wrap);
// A rough disc round a tile: a RadialShape of the given radius and roughness drawn from `stream`,
// stamped wrapping. Jagged islands are made of these.
void stampRoughDisc(std::vector<int> &labels, int width, int height, int label, int x, int y,
					double radius, double roughness, GenerationContext &, const std::string &stream,
					double amplitudeMaximum = 1.4);
} // namespace MapGeneration
