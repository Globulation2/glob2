// SPDX-License-Identifier: GPL-3.0-or-later
// Production predictor retained from the fitted l0100 policy.
//
// Predicted settle depth for a scheduled building walking field (see
// docs/ai/architecture/building-gradient-depth-model.md), from the field's own past depths.
// The depth only moves worker CPU: readers still resolve unsettled cells
// synchronously, so a different depth never changes a simulation result
// and needs no SIM_REVISION bump.
//
// Fitted on 1472 games, 3408777 scheduled field lifetimes.
// Default l0100 (lambda 0.1).
// Default, held out by game: hit rate 0.9686, coverage 0.9702, extra CPU 1.3641, owner saving 0.9415.
#pragma once


namespace BuildingGradientDepth
{
inline constexpr int MIN_DEPTH = 32;
inline constexpr int MAX_DEPTH = 65535;

/// The cost to settle up front from reader demand on the serving field
/// `serving` and its last replaced lifetime `previous` (each -1 if
/// unknown). Without either, as after loading a save, minimum depth lets the
/// readers decide. Never affects results, only who pays.
constexpr int target(int serving, int previous)
{
	const int m = serving > previous ? serving : previous;
	if (m < 0)
		return MIN_DEPTH;
	const long long d = 166 + (292LL * m >> 8);
	return int(d < MIN_DEPTH ? MIN_DEPTH : d > MAX_DEPTH ? MAX_DEPTH : d);
}

} // namespace BuildingGradientDepth
