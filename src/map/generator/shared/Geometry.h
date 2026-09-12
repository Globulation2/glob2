// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
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
