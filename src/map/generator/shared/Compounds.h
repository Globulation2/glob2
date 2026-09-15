// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "Bases.h"
#include "Grid.h"
#include <string>
#include <vector>
class Map;
namespace MapGeneration
{
// Square compounds: a walled square round a base, the wall one tile of stone thick on the square at a
// Chebyshev radius from the base's site, with doors cut into it. A square rather than a round home
// (Homes.h) because a base is rectangles: a square wall keeps the same room at every corner, its
// sides are the frame's own axes so towers stand flush against it and gates face exactly along the
// facing, and a ring of whole tiles is sealed against diagonal steps without any tracing. Stone
// stands only on pure grass, so a design keeps water (and its beaches) clear of the wall; the
// margin is the design's business, the wall's standing is proved with wallStanding.

/// The masks a compound adds to a design: every interior tile labelled with its colony in
/// `interiorOf` (-1 elsewhere), the wall tiles and the gate tiles (gaps in the wall, grass).
struct CompoundMasks
{
	std::vector<int> interiorOf;
	std::vector<unsigned char> wall, gate;
	CompoundMasks() = default;
	explicit CompoundMasks(int tiles) : interiorOf(tiles, -1), wall(tiles, 0), gate(tiles, 0) {}
};

/// The side of a compound a gate opens on, in the base's frame: the facing side first (the way
/// out a base is designed to face), then the back, then the two flanks, so `gates` doors of 1 to 4
/// open in that order.
enum class CompoundSide
{
	Front, // +along
	Back,  // -along
	Right, // +across
	Left   // -across
};

/// Stamps one compound for colony `label` at `site`: interior = Chebyshev distance under `radius`
/// from the site, wall = exactly `radius`, and `gates` (0 to 4) doors of `gateWidth` tiles (odd,
/// centred on the side) cut in the order Front, Back, Right, Left. A gate's tiles are taken out of
/// the wall and marked in `gate`. Interior tiles a previous compound labelled keep their label, so
/// overlapping compounds are a design error a caller checks for (compoundsApart) rather than a
/// crash.
void stampCompound(const Torus &, const BaseSite &, int radius, int gates, int gateWidth, int label,
				   CompoundMasks &);

/// The tiles of one gate as a mask: what settleStartingTowers is told no tower may close the way to.
/// Whether two compounds of `radius` at `a` and `b` keep at least `gap` tiles of ground between
/// their walls, the short way round the wrap.
bool compoundsApart(const Torus &, const BaseSite &a, const BaseSite &b, int radius, int gap);

/// A validator's proof that a designed wall stands: the first tile of `wall` that holds no stone on
/// the finished map, or of `doors` that holds stone, as a message naming `what` and the tile; ""
/// when every tile is as designed. A wall tile the beaches spoiled (no longer grass) counts as
/// broken, since the design promised stone there.
std::string wallStanding(const Map &, const Torus &, const std::vector<unsigned char> &wall,
						 const std::vector<unsigned char> &doors, const char *what);
} // namespace MapGeneration
