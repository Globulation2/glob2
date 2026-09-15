// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <algorithm>
#include <climits>
#include <cmath>
#include <cstdlib>
#include <string>
#include <vector>
class Map;
namespace MapGeneration
{
/// A width by height torus addressed row-major by tile index: the wrap arithmetic every
/// generator needs, written once.
struct Torus
{
	int w, h;
	Torus(int width, int height) : w(width), h(height) {}
	explicit Torus(const Map &);
	int size() const { return w * h; }
	int x(int v) const { return ((v % w) + w) % w; }
	int y(int v) const { return ((v % h) + h) % h; }
	int at(int px, int py) const { return y(py) * w + x(px); }
	/// Signed shortest offset from one column to another across the wrap.
	int offsetX(int from, int to) const
	{
		const int d = x(to - from);
		return d > w / 2 ? d - w : d;
	}
	/// Signed shortest offset from one row to another across the wrap.
	int offsetY(int from, int to) const
	{
		const int d = y(to - from);
		return d > h / 2 ? d - h : d;
	}
	/// Squared distance between two tiles, the short way round.
	int dist2(int ax, int ay, int bx, int by) const
	{
		const int dx = offsetX(ax, bx), dy = offsetY(ay, by);
		return dx * dx + dy * dy;
	}
	/// Steps between two tiles when a step may be diagonal, the short way round.
	int chebyshev(int ax, int ay, int bx, int by) const
	{
		return std::max(std::abs(offsetX(ax, bx)), std::abs(offsetY(ay, by)));
	}
};
/// The four cardinal steps, in the order the generators have always listed them.
constexpr int kCardinalSteps[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};

/// The map's axes turned so that u runs along its longer side and v across it: the frame of a
/// belt that wraps the map the long way (Ring world's continent, Braided river's braid), in which
/// a tall map gets the same design as a wide one, turned. A square map runs along x unless the
/// design says otherwise. Tiles are addressed as (u, v) and as map (x, y) alike.
struct Axes
{
	int width, height;
	bool alongX;
	int length() const { return alongX ? width : height; }
	int breadth() const { return alongX ? height : width; }
	int u(int x, int y) const { return alongX ? x : y; }
	int v(int x, int y) const { return alongX ? y : x; }
	int stepU(int dx, int dy) const { return alongX ? dx : dy; }
	/// The tile at (u, v), wrapped, and a tile's u and v.
	int at(int u, int v) const
	{
		const Torus t{width, height};
		return alongX ? t.at(u, v) : t.at(v, u);
	}
	int uOf(int tile) const { return alongX ? tile % width : tile / width; }
	int vOf(int tile) const { return alongX ? tile / width : tile % width; }
	/// A frame point as map coordinates (unwrapped, as Drawing.h takes them).
	double mapX(double u, double v) const { return alongX ? u : v; }
	double mapY(double u, double v) const { return alongX ? v : u; }
	/// A frame heading (du along, dv across) as a map heading, in radians.
	double heading(double du, double dv) const
	{
		return alongX ? std::atan2(dv, du) : std::atan2(du, dv);
	}
};
inline Axes axesFor(int width, int height)
{
	return {width, height, width >= height};
}

/// A breadth-first flood, eight-connected across the wrap, through open tiles only.
struct Flood
{
	/// Steps from the nearest source: 0 at a source, -1 where nothing was reached. Every step
	/// costs one, so the field is the same whatever order the tiles were visited in.
	std::vector<int> steps;
	/// Every tile reached, in the order it was reached.
	std::vector<int> visited;
};
/// Floods from every source tile. A source is reached whether or not it is open. A tile
/// `limit` steps out is reached but not stepped on from.
Flood floodFrom(const Torus &, const std::vector<unsigned char> &source,
				const std::vector<unsigned char> &open, int limit = INT_MAX);
/// The steps of floodFrom.
std::vector<int> stepsFrom(const Torus &, const std::vector<unsigned char> &source,
						   const std::vector<unsigned char> &open);
/// The same flood with every tile open.
std::vector<int> stepsFrom(const Torus &, const std::vector<unsigned char> &source);
/// A mask with one at each listed tile.
std::vector<unsigned char> tileMask(const Torus &, const std::vector<int> &tiles);
/// Where each of the first `teams` teams' ground units stand, as tile indices in row-major
/// order.
std::vector<std::vector<int>> unitTilesByTeam(const Map &, int teams);
/// The tiles a ground unit can stand on and walk through: land carrying no deposit and no
/// building.
std::vector<unsigned char> walkableTiles(const Map &);
/// The same question put to the engine's own rule, Map::isHardSpaceForGroundUnit, for a unit
/// that cannot swim and belongs to no team.
std::vector<unsigned char> groundUnitTiles(const Map &);
/// The lowest colony from `first` on none of whose units the flood reached, or -1 when every
/// one was: the check every validator makes that colonies can walk to colony 0.
int firstColonyCutOff(const std::vector<int> &steps, const std::vector<std::vector<int>> &units,
					  int first = 1);
} // namespace MapGeneration
