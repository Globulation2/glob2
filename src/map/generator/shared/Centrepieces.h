// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <algorithm>
#include <cstdlib>
namespace MapGeneration
{
// Centrepieces: the pond at the middle of a repeated field or valley, drawn in one of several
// designs so a map of identical modules does not read as one stamp repeated (maintainer review
// 2026-09-16, Hedgerow Country and Breachable highlands: "a few different designs and patterns for
// each square... similar to how canals was set up"). Every design lies inside the square of
// half-width `radius` undermap corners round the centre, so a design's crop-growth envelope and
// its keep-out from hedges and ridges are the square's, whatever design a module draws; only
// how much water it holds, and so how fast its crops regrow, differs. A map that promises its
// colonies equal food gives every home the same design and varies the rest.

enum class PondDesign
{
	Square,  // the whole square
	Round,   // a disc touching the square's sides
	Diamond, // a square on its point, its corners clipped by the square
	Cross,   // two broad arms, a plus sign
	Moat,    // a square of water round a square sand islet: a fountain's plinth
	Twin,    // two long pools either side of a central sand walk
	Clover,  // four square pools round a sand cross
	Oblong,  // a long pool, turned by the quarter turn
	Ring,    // a round moat round a round sand islet
	Count
};

inline const char *pondDesignName(PondDesign design)
{
	static const char *const names[] = {"square", "round", "diamond", "cross", "moat",
										"twin",   "clover", "oblong", "ring"};
	return names[int(design)];
}

/// The least radius at which a design keeps its character (its islet or its walks need room);
/// below it a module draws Round instead.
inline int pondDesignMinimumRadius(PondDesign design)
{
	switch (design)
	{
	case PondDesign::Moat:
	case PondDesign::Twin:
	case PondDesign::Clover:
	case PondDesign::Ring:
		return 4;
	default:
		return 3;
	}
}

/// What a design puts at corner offset (dx, dy) from the centre, `quarter` quarter turns round:
/// 'w' water, 's' sand (an islet or a walk), or 0 for nothing, leaving the map's own terrain.
inline char pondDesignAt(PondDesign design, int radius, int quarter, int dx, int dy)
{
	if (quarter % 2)
		std::swap(dx, dy);
	const int ax = std::abs(dx), ay = std::abs(dy), r = radius;
	if (ax > r || ay > r)
		return 0;
	const double r2 = (r + 0.5) * (r + 0.5);
	switch (design)
	{
	case PondDesign::Square:
		return 'w';
	case PondDesign::Round:
		return dx * dx + dy * dy <= r2 ? 'w' : 0;
	case PondDesign::Diamond:
		return ax + ay <= r + r / 2 ? 'w' : 0;
	case PondDesign::Cross:
		return ax <= r / 2 || ay <= r / 2 ? 'w' : 0;
	case PondDesign::Moat:
		return std::max(ax, ay) >= r - 2 ? 'w' : 's';
	case PondDesign::Twin:
		return ax >= 2 ? 'w' : 's';
	case PondDesign::Clover:
		return ax >= 2 && ay >= 2 ? 'w' : 's';
	case PondDesign::Oblong:
		return ay <= r - 2 ? 'w' : 0;
	case PondDesign::Ring:
	{
		const int d2 = dx * dx + dy * dy;
		return d2 > r2 ? 0 : d2 >= (r - 2) * (r - 2) ? 'w' : 's';
	}
	default:
		return 0;
	}
}
} // namespace MapGeneration
