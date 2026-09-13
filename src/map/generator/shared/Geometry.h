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
