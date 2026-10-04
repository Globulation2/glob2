// SPDX-License-Identifier: GPL-3.0-or-later
#include "TouchDial.h"
#include "GlobalContainer.h"
#include <algorithm>
#include <cmath>
using namespace GAGCore;

namespace TouchDial
{
namespace
{
constexpr double pi = 3.14159265358979323846;
double radians(double degrees)
{
	return degrees * pi / 180;
}
} // namespace

std::optional<Polar> polar(const Geometry &geometry, ViewPoint p)
{
	const double across = (geometry.mirrored ? p.x - geometry.center.x : geometry.center.x - p.x) / geometry.unit;
	const double up = (geometry.center.y - p.y) / geometry.unit;
	if (across < 0 || up < 0)
		return std::nullopt;
	return Polar{std::hypot(across, up), std::atan2(up, across) * 180 / pi};
}
ViewPoint point(const Geometry &geometry, double radius, double angle)
{
	const double across = radius * std::cos(radians(angle)) * geometry.unit;
	const double up = radius * std::sin(radians(angle)) * geometry.unit;
	return {geometry.mirrored ? geometry.center.x + across : geometry.center.x - across, geometry.center.y - up};
}
double padAngle(const Ring &ring, double points)
{
	return points / std::max(1.0, ring.middle()) * 180 / pi;
}
int value(double angle, double from, double to, int maximum)
{
	if (maximum <= 0 || to <= from)
		return 0;
	return std::clamp(int(std::lround((angle - from) / (to - from) * maximum)), 0, maximum);
}
double angleOf(int value, double from, double to, int maximum)
{
	return maximum <= 0 ? from : from + (to - from) * std::clamp(value, 0, maximum) / double(maximum);
}
std::array<int, 3> shares(const std::array<int, 3> &weights, int budget)
{
	const int total = weights[0] + weights[1] + weights[2];
	if (total <= 0)
		return {budget, 0, 0};
	std::array<int, 3> result{}, remainder{};
	int assigned = 0;
	for (int i = 0; i < 3; ++i)
	{
		result[i] = weights[i] * budget / total;
		remainder[i] = weights[i] * budget % total;
		assigned += result[i];
	}
	while (assigned++ < budget)
	{
		const auto best = std::max_element(remainder.begin(), remainder.end());
		++result[best - remainder.begin()];
		*best = -1;
	}
	return result;
}
// Scanline spans: every row above the centre intersects an annular sector
// inside one quadrant in a single interval, so no polygon primitive is needed.
void fill(const Geometry &geometry, double inner, double outer, double from, double to, const Color &color)
{
	if (to <= from || outer <= inner)
		return;
	auto *gfx = globalContainer->gfx;
	const double r0 = inner * geometry.unit, r1 = outer * geometry.unit;
	const double t0 = std::tan(radians(std::max(0.0, from)));
	const double t1 = to >= 89.999 ? 0 : 1 / std::tan(radians(to)); // cot(to)
	for (int row = 0; row < int(std::ceil(r1)); ++row)
	{
		const double v = row + 0.5;
		// Horizontal reach of the row from the centre (`near`/`far` are macros on Windows).
		double reachOut = std::sqrt(std::max(0.0, r1 * r1 - v * v));
		double reachIn = v < r0 ? std::sqrt(r0 * r0 - v * v) : 0;
		reachIn = std::max(reachIn, v * t1);
		if (t0 > 1e-9)
			reachOut = std::min(reachOut, v / t0);
		if (reachOut <= reachIn)
			continue;
		const double x0 = geometry.mirrored ? geometry.center.x + reachIn : geometry.center.x - reachOut;
		const double width = reachOut - reachIn;
		gfx->drawFilledRect(int(std::floor(x0)), int(std::floor(geometry.center.y - v)),
							std::max(1, int(std::ceil(x0 + width) - std::floor(x0))), 1, color);
	}
}
} // namespace TouchDial
