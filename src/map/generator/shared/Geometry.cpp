// SPDX-License-Identifier: GPL-3.0-or-later
#include "Geometry.h"
#include "GenerationContext.h"
#include <cmath>
#include <stdexcept>
namespace MapGeneration
{
ShapeTransform::ShapeTransform(ShapePoint c, double angle, double scale)
	: center(c), cosine(std::cos(angle)), sine(std::sin(angle)), stretch(scale)
{
	if (!std::isfinite(c.x) || !std::isfinite(c.y) || !std::isfinite(angle) ||
		!std::isfinite(scale) || scale <= 0)
		throw std::invalid_argument("Invalid shape transform");
}
ShapePoint ShapeTransform::toMap(ShapePoint p) const
{
	return {center.x + p.x * stretch * cosine - p.y / stretch * sine,
			center.y + p.x * stretch * sine + p.y / stretch * cosine};
}
ShapePoint ShapeTransform::toShape(ShapePoint p) const
{
	p.x -= center.x;
	p.y -= center.y;
	return {(p.x * cosine + p.y * sine) / stretch, (-p.x * sine + p.y * cosine) * stretch};
}
RadialShape::RadialShape(double r, double roughness, GenerationContext &context,
						 const std::string &stream, double amplitudeMaximum)
	: radius(r)
{
	if (!std::isfinite(r) || r <= 0 || !std::isfinite(roughness) || roughness < 0 ||
		roughness >= 1 || !std::isfinite(amplitudeMaximum) || amplitudeMaximum < 0.6 ||
		roughness * amplitudeMaximum >= 1)
		throw std::invalid_argument("Invalid radial shape");
	constexpr double falloff[] = {0.42, 0.28, 0.18, 0.12};
	for (size_t i = 0; i < amplitude.size(); ++i)
	{
		const auto choices = std::uint32_t((amplitudeMaximum - 0.6) * 1000) + 1;
		amplitude[i] = roughness * falloff[i] * (0.6 + context.bounded(stream, choices) / 1000.0);
		phase[i] = 2 * kPi * context.bounded(stream, 65536) / 65536.0;
	}
}
double RadialShape::radiusAt(double theta) const
{
	constexpr int harmonics[] = {2, 3, 5, 7};
	double wobble = 0;
	for (size_t i = 0; i < amplitude.size(); ++i)
		wobble += amplitude[i] * std::cos(harmonics[i] * theta + phase[i]);
	return radius * (1 + wobble);
}
double RadialShape::maximumRadius() const
{
	double sum = 0;
	for (double a : amplitude)
		sum += std::abs(a);
	return radius * (1 + sum);
}
void stampShape(std::vector<int> &labels, int width, int height, int label,
				const ShapeTransform &transform, const RadialShape &shape, bool wrap)
{
	if (width <= 0 || height <= 0 || labels.size() != size_t(width) * height)
		throw std::invalid_argument("Invalid shape grid");
	const auto center = transform.toMap({0, 0});
	for (int y = 0; y < height; ++y)
		for (int x = 0; x < width; ++x)
		{
			ShapePoint p{double(x), double(y)};
			if (wrap)
			{
				p.x = center.x + std::remainder(p.x - center.x, width);
				p.y = center.y + std::remainder(p.y - center.y, height);
			}
			p = transform.toShape(p);
			if (std::hypot(p.x, p.y) < shape.radiusAt(std::atan2(p.y, p.x)))
				labels[size_t(y) * width + x] = label;
		}
}
void stampRoughDisc(std::vector<int> &labels, int width, int height, int label, int x, int y,
					double radius, double roughness, GenerationContext &context,
					const std::string &stream, double amplitudeMaximum)
{
	RadialShape shape(radius, roughness, context, stream, amplitudeMaximum);
	stampShape(labels, width, height, label, ShapeTransform({double(x), double(y)}, 0), shape, true);
}
} // namespace MapGeneration
