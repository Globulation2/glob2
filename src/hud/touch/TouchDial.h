// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <ViewportTransform.h>
#include <array>
#include <optional>

namespace GAGCore
{
struct Color;
}

// Geometry for the phone inspector's thumb dial: concentric quarter rings
// centred on the bottom corner under the thumb, so a value changes by sweeping
// the thumb along its natural arc. Angles are degrees of sweep: 0 runs along
// the toolbar away from the thumb, 90 points straight up. Radii are points;
// positions are drawable units. Drawing and hit testing share these functions.
namespace TouchDial
{
struct Ring
{
	double inner = 0, outer = 0;
	double middle() const { return (inner + outer) / 2; }
};
struct Geometry
{
	GAGCore::ViewPoint center;
	bool mirrored = false; // Left thumb: rings open to the right of the centre.
	double unit = 1;	   // Drawable units per point.
	std::array<Ring, 3> rings{};
	double sweepStart = 0, sweepEnd = 90;
};
struct Polar
{
	double radius, angle;
};
//! Radius and sweep angle of a point, or nothing outside the dial's quadrant.
std::optional<Polar> polar(const Geometry &geometry, GAGCore::ViewPoint point);
//! Drawable position at a radius (points) and sweep angle.
GAGCore::ViewPoint point(const Geometry &geometry, double radius, double angle);
//! Angular width (degrees) of a thumb-sized pad centred on a ring.
double padAngle(const Ring &ring, double points);
//! Value for an angle within [from, to], rounded and clamped to [0, maximum].
int value(double angle, double from, double to, int maximum);
//! Angle for a value within [from, to].
double angleOf(int value, double from, double to, int maximum);
//! Round relative weights to a fixed budget; largest remainders preserve the total.
std::array<int, 3> shares(const std::array<int, 3> &weights, int budget);
//! Fills the annular sector between two radii (points) and two angles.
void fill(const Geometry &geometry, double inner, double outer, double from, double to,
		  const GAGCore::Color &color);
} // namespace TouchDial
