// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "Drawing.h"
#include "Grid.h"
#include "Sketch.h"
#include <vector>
class Map;
namespace MapGeneration
{
// Water between two pieces of land, sized for what it is meant to do: a canal towers shoot across
// from the first minute, a moat no tower can reach over, a strait with a few sand bridges. Every
// generator that parts land with water has done the same beach arithmetic by hand; it lives here once.
//
// The arithmetic. A straight channel `w` undermap corners wide gets a sand corner either side from
// layBeaches, and a tile is pure grass only when its four corners are, so the channel spoils w + 3
// tiles of grass across (kChannelSpoiledTiles), of which w - 1 are pure water. A tower's footprint on
// the last grass tile of one bank is w + 4 tiles (Chebyshev) from the first grass tile of the other,
// and a tower of range r hits what is r tiles away or nearer (towerReach, Walls.h).

/// The tiles of grass a channel spoils beyond its water corners.
constexpr int kChannelSpoiledTiles = 3;

/// The distance from a tower on one bank's last grass tile to the other bank's first grass tile, for a
/// straight channel `waterCorners` wide.
inline int bankToBank(int waterCorners)
{
	return waterCorners + kChannelSpoiledTiles + 1;
}

/// The widest channel (in water corners) a tower of `level` (1 to 3) on one bank still shoots across,
/// reaching `depth` tiles of grass into the far bank (1 = its first tile). 0 when even a single water
/// corner is too wide.
int widestChannelTowersCross(int level, int depth = 1);

/// The narrowest channel (in water corners) a tower of `level` cannot shoot across at all.
int narrowestChannelTowersMiss(int level);

/// A sketch's beach as the game will draw it: every tile that is neither pure grass nor pure water,
/// the walkable, unbuildable rim round water. Towers stand against it to cover a canal
/// (TowerRequest::against).
std::vector<unsigned char> beachTiles(const TerrainSketch &, const Torus &);

/// The same on a finished map.
std::vector<unsigned char> beachTiles(const Map &, const Torus &);

/// A sand bridge laid straight across water from `from` to `to`: every water corner within
/// `halfWidth` of the line becomes sand (strokePath over corners). A tile is water only when all four
/// of its corners are, so a bridge one corner wide already carries units on the two tiles either side
/// of it. Run after layBeaches (a bridge's own sand needs no beach). Returns the corners changed.
int bridgeAcross(TerrainSketch &, const Torus &, ShapePoint from, ShapePoint to, double halfWidth);

/// How many separate bridges (eight-connected pieces of `bridges`) touch each label's ground, for the
/// labels 0 to `labels` - 1: the check that every colony got as many ways across as every other.
std::vector<int> crossingsPerLabel(const Torus &, const std::vector<unsigned char> &bridges,
								   const std::vector<int> &labelled, int labels);

// Channels drawn as centre lines: a river planned as a path of points a half tile apart with a
// radius at each (Watershed's reaches, Braided river's threads), stamped as water, and the fords
// laid across them. What a ford or a channel is meant to do is checked against the rasterized
// water, on the sketch and again on the finished world, never assumed from the plan.

/// A ford: sand laid across a channel so it can be crossed on foot, in map coordinates. The
/// channel's centre line passes (x, y) in the unit direction (alongX, alongY), and (acrossX,
/// acrossY) is the unit direction across it. The sand reaches `halfWidth` along the channel either
/// side of the line across it (2 makes three rows of pure sand, five rows walkable) and `span`
/// across (the channel's radius there plus a tile or two, so the sand meets both beaches).
struct SandFord
{
	double x, y;
	double alongX, alongY, acrossX, acrossY;
	double span, halfWidth;
	/// Where (px, py) lies from the ford's centre: along the channel and across it, the short way
	/// round the torus.
	void offsets(const Torus &, double px, double py, double &along, double &across) const;
	/// Whether (px, py) lies within the ford's outline grown by the margins.
	bool covers(const Torus &, double px, double py, double alongMargin, double acrossMargin) const;
};

/// Sand on every water corner of the ford. Only water changes, so beaches already laid stay
/// right. Run after layBeaches.
void stampFord(TerrainSketch &, const Torus &, const SandFord &);

/// The ford across the channel at `index` of a centre line (map coordinates, a radius per point,
/// a closed loop when `closed`): its directions from the points two either side of the index, its
/// span the radius there plus `reach`.
SandFord fordAlong(const Torus &, const std::vector<ShapePoint> &centreline,
				   const std::vector<double> &radius, int index, bool closed, double halfWidth,
				   double reach);

/// The ford on the rasterized water: "" when it interrupts open water (there is water within a
/// tile of the point `probe` past its edge along the channel, both ways), is dry along three lines
/// across (its own and a tile either side of it, every half tile from span to span) and reaches
/// land at both ends (a tile past its span); else why not, with the ford's tile.
/// `water(x, y)` answers for a map tile.
template <typename Water>
std::string fordFault(const Torus &, const SandFord &, Water water, double probe = 2.5);

/// Whether a walkable tile (no water, no deposit, no building) lies within one tile of the ford's
/// landing on `side` (-1 or 1), a tile past its span.
bool fordLandingWalkable(const Map &, const Torus &, const SandFord &, int side);

/// A channel's core of open water, on the rasterized water: the tile under every point of
/// `centreline` (map coordinates, in order) is pure water, and no two consecutive points' tiles
/// touch only at a corner with land on both sides of it, so no unit can step over the channel
/// even diagonally; except at points `skip(index)` (a ford, the sea), where the check starts
/// afresh. "" or the first fault with its tile.
template <typename Skip, typename Water>
std::string channelCoreFault(const Torus &, const std::vector<ShapePoint> &centreline, Skip skip,
							 Water water);

/// A stretch of channel that two labelled regions face each other across, so a ford there joins
/// them: the point at the stretch's middle, the two labels and the stretch's length in points.
struct ChannelCrossing
{
	int index, a, b, run;
};

/// Every stretch of a channel that two labelled regions face each other across. Along the centre
/// line (map coordinates, a radius per point, a closed loop when `closed`), a probe `reach` tiles
/// beyond the water on either side reads `labels` (-1 for none); every run of consecutive points
/// with the same two labels on its two sides (both labelled, different) where `clear(index)` holds
/// (no other channel near, say), of at least `minimumRun` points, is one crossing at the run's
/// middle. A run cut by the seam of a closed loop is one run.
template <typename Clear>
std::vector<ChannelCrossing>
channelCrossings(const Torus &, const std::vector<ShapePoint> &centreline,
				 const std::vector<double> &radius, const std::vector<int> &labels, double reach,
				 int minimumRun, bool closed, Clear clear);
} // namespace MapGeneration

#include <cmath>
#include <string>
namespace MapGeneration
{
namespace ChannelDetail
{
inline int wrapIndex(int value, int period)
{
	return ((value % period) + period) % period;
}
inline double centred(double delta, double period)
{
	return delta - period * std::round(delta / period);
}
/// The unit tangent at a point of a centre line, from the points two either side (clamped at an
/// open line's ends), the short way round the torus.
inline ShapePoint tangentAt(const Torus &t, const std::vector<ShapePoint> &line, int index,
							bool closed)
{
	const int n = int(line.size());
	const int before = closed ? wrapIndex(index - 2, n) : std::max(0, index - 2);
	const int after = closed ? wrapIndex(index + 2, n) : std::min(n - 1, index + 2);
	const double dx = centred(line[after].x - line[before].x, t.w);
	const double dy = centred(line[after].y - line[before].y, t.h);
	const double len = std::hypot(dx, dy);
	return len > 1e-9 ? ShapePoint{dx / len, dy / len} : ShapePoint{1, 0};
}
inline int tileOf(const Torus &t, double x, double y)
{
	return t.at(int(std::floor(x)), int(std::floor(y)));
}
inline std::string where(const Torus &t, double x, double y)
{
	const int i = tileOf(t, x, y);
	return " at (" + std::to_string(i % t.w) + ", " + std::to_string(i / t.w) + ")";
}
} // namespace ChannelDetail

template <typename Water>
std::string fordFault(const Torus &t, const SandFord &f, Water water, double probe)
{
	using namespace ChannelDetail;
	const auto wet = [&](double x, double y)
	{
		const int i = tileOf(t, x, y);
		return water(i % t.w, i / t.w);
	};
	// Open water within a tile of a point: a thin or diagonal channel's water core need not cover
	// any one exact tile once its shores are sanded.
	const auto wetNear = [&](double x, double y)
	{
		for (int dy = -1; dy <= 1; ++dy)
			for (int dx = -1; dx <= 1; ++dx)
				if (wet(x + dx, y + dy))
					return true;
		return false;
	};
	for (int side : {-1, 1})
	{
		const double d = side * (f.halfWidth + probe);
		if (!wetNear(f.x + f.alongX * d, f.y + f.alongY * d))
			return "A ford" + where(t, f.x, f.y) + " does not cross a channel.";
	}
	for (int a = -1; a <= 1; ++a)
		for (double s = -f.span; s <= f.span + 1e-9; s += 0.5)
			if (wet(f.x + f.alongX * a + f.acrossX * s, f.y + f.alongY * a + f.acrossY * s))
				return "A ford" + where(t, f.x, f.y) + " is cut by open water.";
	for (int side : {-1, 1})
	{
		const double s = side * (f.span + 1.0);
		if (wet(f.x + f.acrossX * s, f.y + f.acrossY * s))
			return "A ford" + where(t, f.x, f.y) + " does not reach its bank.";
	}
	return "";
}

template <typename Skip, typename Water>
std::string channelCoreFault(const Torus &t, const std::vector<ShapePoint> &centreline, Skip skip,
							 Water water)
{
	using namespace ChannelDetail;
	bool previous = false;
	int px = 0, py = 0;
	for (size_t i = 0; i < centreline.size(); ++i)
	{
		if (skip(int(i)))
		{
			previous = false;
			continue;
		}
		const int tile = tileOf(t, centreline[i].x, centreline[i].y);
		const int x = tile % t.w, y = tile / t.w;
		if (!water(x, y))
			return "A channel silts up" + where(t, centreline[i].x, centreline[i].y) + ".";
		if (previous && x != px && y != py && !water(x, py) && !water(px, y))
			return "A channel can be stepped across" + where(t, centreline[i].x, centreline[i].y) +
				   ".";
		previous = true;
		px = x;
		py = y;
	}
	return "";
}

template <typename Clear>
std::vector<ChannelCrossing>
channelCrossings(const Torus &t, const std::vector<ShapePoint> &centreline,
				 const std::vector<double> &radius, const std::vector<int> &labels, double reach,
				 int minimumRun, bool closed, Clear clear)
{
	using namespace ChannelDetail;
	struct Run
	{
		int a, b, first, last;
	};
	std::vector<Run> runs;
	const int n = int(centreline.size());
	for (int i = 0; i < n; ++i)
	{
		const ShapePoint tangent = tangentAt(t, centreline, i, closed);
		const double nx = -tangent.y, ny = tangent.x, probe = radius[i] + reach;
		const ShapePoint p = centreline[i];
		const int a = labels[tileOf(t, p.x + nx * probe, p.y + ny * probe)];
		const int b = labels[tileOf(t, p.x - nx * probe, p.y - ny * probe)];
		if (a < 0 || b < 0 || a == b || !clear(i))
			continue;
		const int lo = std::min(a, b), hi = std::max(a, b);
		if (!runs.empty() && runs.back().a == lo && runs.back().b == hi &&
			runs.back().last == i - 1)
			runs.back().last = i;
		else
			runs.push_back({lo, hi, i, i});
	}
	if (closed && runs.size() >= 2 && runs.front().first == 0 && runs.back().last == n - 1 &&
		runs.front().a == runs.back().a && runs.front().b == runs.back().b)
	{
		runs.back().last = runs.front().last + n;
		runs.erase(runs.begin());
	}
	std::vector<ChannelCrossing> crossings;
	for (const Run &run : runs)
	{
		const int length = run.last - run.first + 1;
		if (length < minimumRun)
			continue;
		crossings.push_back({wrapIndex((run.first + run.last) / 2, n), run.a, run.b, length});
	}
	return crossings;
}
} // namespace MapGeneration
