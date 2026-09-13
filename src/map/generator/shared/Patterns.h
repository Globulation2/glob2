// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "Drawing.h"
#include "Grid.h"
#include <cmath>
#include <random>
#include <vector>
namespace MapGeneration
{
// Whole-map patterns with a grain: labyrinths that grow themselves, parallel stripes that wrap the
// torus exactly at any slant, the shadow a ridge casts downwind, and curves that follow a field.
// LatticeNoise.h gives smooth isotropic noise; these give structure. The iterated and stepped ones
// are integer arithmetic, since an error repeated over many passes would compound.

/// A Turing pattern: stripes and spots that grow out of noise by local activation and wider
/// inhibition, the way a fingerprint, a zebra's coat or a brain coral does. Each pass blurs the field
/// over a near and a far box, adds the difference (near minus far) to every tile, then recentres and
/// rescales the field to its own peak; after enough passes the field settles into bands about
/// `wavelength` tiles from crest to crest (within about 15%). A stretch above 100 on one axis widens both
/// blurs along it, so the bands line up across that axis (a grain). Values are integers in
/// [-32768, 32768]; threshold at the median for a labyrinth of equal stripes, or near a tail for spots
/// (percentile, LatticeNoise.h). Starts from periodicNoise, so it tiles the torus.
struct TuringStyle
{
	int wavelength = 16;
	int stretchX = 100, stretchY = 100;
	int iterations = 20;
};
std::vector<int> turingPattern(const Torus &, const TuringStyle &, std::mt19937 &);

/// Parallel stripes that wrap the torus exactly: a phase in [0, 65536) per tile that climbs by one
/// whole turn `acrossX` times along the width and `acrossY` times down the height, so every stripe
/// meets itself across both seams at any slant those whole numbers allow. Stripes run across the
/// direction (acrossX / width, acrossY / height), about stripeSpacing tiles apart. `warpPercent` bends
/// them with fractal noise `warpPeriod` tiles across, by up to that share of one stripe spacing.
struct StripeStyle
{
	int acrossX = 3, acrossY = 1;
	int warpPeriod = 32;
	int warpPercent = 20;
};
std::vector<int> stripePhase(const Torus &, const StripeStyle &, std::mt19937 &);

/// The phase along the stripes: a second stripe field whose gradient runs perpendicular to the
/// first's, also exactly periodic on the torus, so anything laid out along the stripes by it (gaps,
/// pools, dykes) repeats seamlessly. The first field's normal is (a / w, b / h); a vector along its
/// stripes is (b / h, -a / w), scaled by whichever of w and h is larger to keep the turn counts whole.
/// Unwarped: the warp belongs to the field it was drawn for.
StripeStyle alongStripes(const Torus &, const StripeStyle &across);

/// The distance between neighbouring stripes, in tiles.
double stripeSpacing(const Torus &, const StripeStyle &);

/// The heading (radians) along the stripes, and the heading across them towards rising phase.
double stripeHeading(const Torus &, const StripeStyle &);
double stripeNormal(const Torus &, const StripeStyle &);

/// How far a phase lies from the nearest crest (phase 0), in [0, 32768]: a ridge `share` of the spacing
/// wide is every tile under share * 32768.
inline int stripeDistance(int phase)
{
	return phase < 32768 ? phase : 65536 - phase;
}

/// The shadow a mask casts downwind: for every tile, the number of steps upwind (against the direction
/// (dx, dy), in Chebyshev steps along the line) to the nearest mask tile, 0 on the mask, -1 when none
/// lies within `reach`. The wind is given as whole numbers, so every step lands on a tile the same way
/// on every platform: (1, 0) blows east, (2, 1) east-south-east. What lies close behind a ridge is dry.
std::vector<int> upwindSteps(const Torus &, const std::vector<unsigned char> &mask, int dx, int dy,
							 int reach);

/// A curve that follows a field of headings: from `start`, `steps` steps of `stepLength` tiles, each
/// along `headingAt(x, y)` (radians, sampled at the wrapped position) at the midpoint of the step, with
/// every point's half width `halfWidth`. Positions keep climbing past the seam rather than wrapping, as
/// strokePath expects. Rivers down a slope, ridges along a grain, dunes across the wind.
template <typename HeadingAt>
std::vector<StrokePoint> traceStreamline(const Torus &t, ShapePoint start, HeadingAt headingAt,
										 int steps, double stepLength, double halfWidth)
{
	const auto wrapped = [&](double v, int n)
	{
		double r = std::fmod(v, double(n));
		return r < 0 ? r + n : r;
	};
	std::vector<StrokePoint> path{{start.x, start.y, halfWidth}};
	double x = start.x, y = start.y;
	for (int i = 0; i < steps; ++i)
	{
		const double h0 = headingAt(wrapped(x, t.w), wrapped(y, t.h));
		const double mx = x + 0.5 * stepLength * std::cos(h0),
					 my = y + 0.5 * stepLength * std::sin(h0);
		const double h = headingAt(wrapped(mx, t.w), wrapped(my, t.h));
		x += stepLength * std::cos(h);
		y += stepLength * std::sin(h);
		path.push_back({x, y, halfWidth});
	}
	return path;
}
} // namespace MapGeneration
