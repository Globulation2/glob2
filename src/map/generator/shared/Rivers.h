// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "Channels.h"
#include "Drawing.h"
#include "Geometry.h"
#include "Grid.h"
#include "Sketch.h"
#include "Solve.h"
#include <string>
#include <vector>
struct GenerationContext;
namespace MapGeneration
{
// Construct a closed, meandering river, score candidate placements, and choose crossings.
// The caller supplies placement objectives and the desired number of fords. Constructing the
// channel directly guarantees its shape; scoring placements can then account for existing homes
// and lakes. Marchland uses this instead of asking its terrain search to discover a river from
// positional water-share targets, which did not constrain the channel's shape.

/// The shape of a river bed. Widths are in undermap corners, and a tile is pure water only when its
/// four corners are, so a bed of half width `w` leaves about 2w - 1 tiles of open water and spoils
/// kChannelSpoiledTiles more to beach either side (Channels.h).
struct RiverStyle
{
	double halfWidth = 2.6;
	/// How far the bed may stray from its line, as a share of the side it crosses.
	double wander = 0.17;
	/// How much the bed swells and narrows along its length, as a share of `halfWidth`.
	double swell = 0.3;
	/// Tiles between consecutive centre-line points.
	double step = 2.0;
	/// How many harmonics the meander is built from. Each is periodic over the crossing, so the
	/// loop closes exactly however many are used.
	int harmonics = 3;
};

/// A drawn river: a centre line that leaves the map where it entered it, so on the torus it is one
/// closed loop with no source and no mouth, and a radius at every point of it.
struct River
{
	std::vector<ShapePoint> line;
	std::vector<double> radius;
	/// Whether the bed crosses the map from top to bottom rather than side to side. Which way it
	/// runs is the one thing about a finished bed that its own points cannot answer, because on a
	/// torus the loop has no ends to compare: riverWater needs it to know which way to step over
	/// the seam.
	bool vertical = false;
};

/// A river bed crossing the whole torus, entering at `offset` tiles along the axis it does not
/// cross. The meander is a sum of harmonics that are periodic over the crossing, so the bed arrives
/// exactly where it set out and the loop closes without a seam - the trick the engine's own
/// makeRiver plays with a cross-fade, done in a way that takes its draws from a named stream and so
/// replays from the seed. Two draws per harmonic plus one for the swell.
River drawRiver(const Torus &, GenerationContext &, const std::string &stream, bool vertical,
				double offset, const RiverStyle & = {});

/// The corners a river floods: strokePath over its centre line, which joins every point with a
/// round joint so the bed never gaps on a bend. Empty rivers produce an empty mask; mismatched
/// point/radius arrays are rejected.
std::vector<unsigned char> riverWater(const Torus &, const River &);

/// A bed and what it was rated at.
struct RiverChoice
{
	River bed;
	std::vector<unsigned char> water;
	Objective score;
	bool found = false;
};

/// The best of `beds` laid across the map, rated by the caller.
///
/// Where a river should go is the part of it construction cannot settle, because a good bed depends
/// on everything already on the ground. It is also a handful of options rather than a space, so it
/// is scored outright: the beds are spread evenly across the map so the candidates genuinely differ
/// instead of clustering by luck, each is rated, and the lowest wins. Reaching for an annealing run
/// over a dozen candidates is unnecessary when each candidate can be evaluated directly.
///
/// `rate(bed, water)` returns an Objective, so the winner arrives carrying every measurement that
/// chose it - a caller wanting one of them back reads it off the returned score
/// (Objective::residual) rather than keeping its own tally beside the loop.
template <typename Rate>
RiverChoice bestRiverAcross(const Torus &t, GenerationContext &context, const std::string &stream,
							bool vertical, int beds, const RiverStyle &style, Rate rate)
{
	RiverChoice best;
	const double across = vertical ? t.w : t.h;
	for (int bed = 0; bed < beds; ++bed)
	{
		River candidate =
			drawRiver(t, context, stream, vertical, across * (double(bed) + 0.5) / double(beds),
					  style);
		std::vector<unsigned char> water = riverWater(t, candidate);
		const Objective score = rate(candidate, water);
		if (best.found && score.total() >= best.score.total())
			continue;
		best.bed = std::move(candidate);
		best.water = std::move(water);
		best.score = score;
		best.found = true;
	}
	return best;
}

/// A place a river can be forded, and what it would join.
struct RiverFord
{
	/// The centre-line point the ford crosses at.
	int index = 0;
	/// The walkable components either side of the bed there, or -1 where the bank is not walkable.
	int firstBank = -1, secondBank = -1;
};

/// Every point along a river where both banks are walkable ground, with the component each bank
/// belongs to: the ford sites a map may choose between. Banks are read `reach` tiles beyond the
/// bed's radius, and sites nearer than `apart` points to one already taken are dropped, so the
/// candidates are spread along the river rather than clustered round one shallow stretch.
std::vector<RiverFord> fordSites(const Torus &, const River &,
								 const std::vector<unsigned char> &walkable,
								 const std::vector<int> &components, double reach, int apart);

/// Connectivity achieved by a greedy spanning forest over the bank components. The selected
/// indices refer to the input sites. Missing candidate edges can leave disconnected components;
/// callers must inspect connected() and still validate the rasterized, finished terrain.
struct FordConnections
{
	std::vector<int> sites;
	int remainingComponents = 0;
	bool connected() const { return remainingComponents <= 1; }
};
FordConnections fordsToRejoin(const std::vector<RiverFord> &sites, int components);

/// More crossings than bare connectivity, spread along the bed: starting from `taken` (indices into
/// `sites`, as FordConnections::sites gives them), adds sites until there are `wanted` of them, each time
/// taking whichever is farthest along the river from every ford already chosen. `points` is the
/// centre line's length, the distance being measured the short way round the loop.
///
/// A river forded only where it must be is not a landform but a wall, and a wall across the middle
/// of a map does not shape a fight so much as end it: on Marchland, the minimum crossing set left
/// one colony contesting five prizes and another one, which is the map's own fairness check
/// failing.
/// How many crossings there are is the dial between a river that routes a war and a river that
/// stops it, and it belongs to the map, not to this.
std::vector<int> fordsSpreadAlong(const std::vector<RiverFord> &sites, std::vector<int> taken,
								  int wanted, int points);

/// Lays a ford across the river at a centre-line point: sand over every water corner of it, so the
/// bed can be crossed on foot there. Run after layBeaches, whose sand this does not disturb.
void layFord(TerrainSketch &, const Torus &, const River &, int index, double halfWidth,
			 double reach);
} // namespace MapGeneration
