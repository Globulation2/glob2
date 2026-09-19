// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "Channels.h"
#include "Drawing.h"
#include "Geometry.h"
#include "Grid.h"
#include "Sketch.h"
#include <string>
#include <vector>
struct GenerationContext;
namespace MapGeneration
{
// A river a map draws and a search places.
//
// WHY THIS IS NOT A CONSTRAINT. The obvious way to ask a solver for a river is to ask for water in
// the middle of the map, and it does not work. A positional target is met just as well by a blob, a
// ring or a scatter of ponds with the right centre-weighted share; nothing in such a measure
// selects for thin, long or joined-up. Tighten it until only a one-cell band satisfies it and a
// channel does appear - ruler-straight, at a fixed offset, because a band is the only shape the
// measure named. Making it meander means describing the meander, and at that point the route is
// drawn and the search has coloured it in.
//
// The deeper reason is the shape of the search. Every target a solver handles well here is
// statistical - a share, a ratio, a count, a spread - and those have gradients: move one cell and
// the number moves readably, so annealing always has a direction. "One joined-up channel that
// crosses the map" is topological, and topological targets are needles. Nine tenths of a river is a
// lake and a pond, and it scores no better than one tenth of one. There is no partial credit, so
// there is nothing to descend, and a search spends its whole budget wandering.
//
// So the bed is drawn. A closed wandering loop across the torus is a dozen lines of trigonometry,
// is joined up and the right width by construction, and costs one pass. What is left over is the
// part construction genuinely cannot do: which of several beds to lay given where the colonies and
// their farms already are, and where to ford it so the map stays one country. Those are few,
// discrete, and exactly measurable on a candidate - which is the shape of thing Solve.h is for.
// Draw the landform; search its placement.

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
	/// Whether the bed crosses the map from top to bottom rather than side to side.
	bool vertical = false;
	/// Where it crosses, in tiles along the other axis.
	double offset = 0;
};

/// A river bed crossing the whole torus, entering at `offset` tiles along the axis it does not
/// cross. The meander is a sum of harmonics that are periodic over the crossing, so the bed arrives
/// exactly where it set out and the loop closes without a seam - the trick the engine's own
/// makeRiver plays with a cross-fade, done in a way that takes its draws from a named stream and so
/// replays from the seed. Two draws per harmonic plus one for the swell.
River drawRiver(const Torus &, GenerationContext &, const std::string &stream, bool vertical,
				double offset, const RiverStyle & = {});

/// The corners a river floods: strokePath over its centre line, which joins every point with a
/// round joint so the bed never gaps on a bend.
std::vector<unsigned char> riverWater(const Torus &, const River &);

/// A place a river can be forded, and what it would join.
struct RiverFord
{
	/// The centre-line point the ford crosses at.
	int index = 0;
	/// The walkable components either side of the bed there, or -1 where the bank is not walkable.
	int near = -1, far = -1;
};

/// Every point along a river where both banks are walkable ground, with the component each bank
/// belongs to: the ford sites a map may choose between. Banks are read `reach` tiles beyond the
/// bed's radius, and sites nearer than `apart` points to one already taken are dropped, so the
/// candidates are spread along the river rather than clustered round one shallow stretch.
std::vector<RiverFord> fordSites(const Torus &, const River &,
								 const std::vector<unsigned char> &walkable,
								 const std::vector<int> &components, double reach, int apart);

/// The fewest of `sites` that put the banks back into one piece, chosen greedily: a river that cuts
/// a map in two is not a feature but a broken map, so this is construction's job and not a
/// search's. Union-find over the components the sites join; returns the indices into `sites` taken.
/// A map that wants its remaining fords placed well should choose those with a search, over what
/// this leaves - the connectivity is the invariant, where the rest of the crossings fall is the
/// character.
std::vector<int> fordsToRejoin(const std::vector<RiverFord> &sites, int components);

/// More crossings than bare connectivity, spread along the bed: starting from `taken` (indices into
/// `sites`, as fordsToRejoin gives them), adds sites until there are `wanted` of them, each time
/// taking whichever is farthest along the river from every ford already chosen. `points` is the
/// centre line's length, the distance being measured the short way round the loop.
///
/// A river forded only where it must be is not a landform but a wall, and a wall across the middle
/// of a map does not shape a fight so much as end it: on Tug, the minimum crossing set left one
/// colony contesting five prizes and another one, which is the map's own fairness check failing.
/// How many crossings there are is the dial between a river that routes a war and a river that
/// stops it, and it belongs to the map, not to this.
std::vector<int> fordsSpreadAlong(const std::vector<RiverFord> &sites, std::vector<int> taken,
								  int wanted, int points);

/// Lays a ford across the river at a centre-line point: sand over every water corner of it, so the
/// bed can be crossed on foot there. Run after layBeaches, whose sand this does not disturb.
void layFord(TerrainSketch &, const Torus &, const River &, int index, double halfWidth,
			 double reach);
} // namespace MapGeneration
