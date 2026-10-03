// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "Grid.h"
#include <algorithm>
#include <cstdint>

namespace field
{
// One row at a time so the owner can preserve cooperative yield boundaries.
// Repeat forward and reverse passes to convergence. 0 is blocked, 1 is free,
// 255 is pinned, and stronger contributions decay by one per eight-neighbour
// step. Values 2..254 may be raised by a stronger source. Call beginPass(), scan
// forward rows in ascending y then reverse rows in descending y, and inspect
// passChanged(). The owner controls checkpoints and the convergence-pass cap.
class ConvergentInfluence
{
	std::uint8_t *values;
	Grid grid;
	bool changed = false;

  public:
	ConvergentInfluence(std::uint8_t *cells, Grid geometry) : values(cells), grid(geometry) {}
	bool hasSources() const
	{
		return std::any_of(values, values + grid.cells(), [](auto v) { return v >= 3; });
	}
	void beginPass() { changed = false; }
	bool passChanged() const { return changed; }
	void forwardRow(int y)
	{
		if (grid.powerOfTwo())
			row<true, true>(y);
		else
			row<true, false>(y);
	}
	void reverseRow(int y)
	{
		if (grid.powerOfTwo())
			row<false, true>(y);
		else
			row<false, false>(y);
	}

  private:
	template <bool forward, bool Masked> void row(int y)
	{
		auto *current = values + y * grid.width();
		const auto *adjacent = values + grid.wrapY(y + (forward ? -1 : 1)) * grid.width();
		for (int step = 0; step < grid.width(); ++step)
		{
			const int x = forward ? step : grid.width() - 1 - step;
			const auto value = current[x];
			if (value == 0 || value == 255)
				continue;
			const int left = Masked ? (x - 1) & grid.maskX() : grid.wrapX(x - 1);
			const int right = Masked ? (x + 1) & grid.maskX() : grid.wrapX(x + 1);
			const auto strongest =
				std::max(std::max(adjacent[left], adjacent[x]),
						 std::max(adjacent[right], current[forward ? left : right]));
			if (strongest >= 3 && strongest - 1 > value)
			{
				current[x] = strongest - 1;
				changed = true;
			}
		}
	}
};

// Cell contracts for fixed directional sweeps. These are not convergent
// transforms: the staggered scan order and Uint8 increment wrap are observable.
// Every value except Pinned is writable; zero remains the propagation floor.
template <std::uint8_t Pinned> struct ZeroFloorPinned
{
	bool writable(std::uint8_t value) const { return value != Pinned; }
	std::uint8_t finish(std::uint8_t value) const { return value == 0 ? 0 : value - 1; }
};
// Zero is blocked, Pinned is immutable, and writable cells have a floor of one.
template <std::uint8_t Pinned> struct BlockedUnitFloor
{
	bool writable(std::uint8_t value) const { return value != 0 && value != Pinned; }
	std::uint8_t finish(std::uint8_t value) const { return value == 1 ? 1 : value - 1; }
};

namespace detail
{
template <bool Masked> struct InfluenceRows
{
	Grid grid;
	int shift;
	explicit InfluenceRows(Grid geometry) : grid(geometry), shift(Masked ? grid.widthShift() : 0) {}
	int row(int y) const
	{
		if constexpr (Masked)
			return (y & grid.maskY()) << shift;
		else
			return grid.wrapY(y) * grid.width();
	}
	int x(int coordinate) const
	{
		if constexpr (Masked)
			return coordinate & grid.maskX();
		else
			return grid.wrapX(coordinate);
	}
};
template <class Rows, class CellRule>
void directionalSweeps(std::uint8_t *gradient, Rows rows, CellRule rule)
{
	int w = rows.grid.width();
	int h = rows.grid.height();

	for (int yi = 0; yi < h; yi++)
	{
		int wy = rows.row(yi);
		int wyu = rows.row(yi - 1);
		for (int xi = yi; xi < (yi + w); xi++)
		{
			int x = rows.x(xi);
			std::uint8_t max = gradient[wy + x];
			if (rule.writable(max))
			{
				int xl = rows.x(x - 1);
				int xr = rows.x(x + 1);

				std::uint8_t side[4];
				side[0] = gradient[wyu + xl];
				side[1] = gradient[wyu + x];
				side[2] = gradient[wyu + xr];
				side[3] = gradient[wy + xl];
				max++;

				for (int i = 0; i < 4; i++)
					if (side[i] > max)
						max = side[i];
				gradient[wy + x] = rule.finish(max);
			}
		}
	}

	for (int y = h - 1; y >= 0; y--)
	{
		int wy = rows.row(y);
		int wyd = rows.row(y + 1);
		for (int xi = y; xi < (y + w); xi++)
		{
			int x = rows.x(xi);
			std::uint8_t max = gradient[wy + x];
			if (rule.writable(max))
			{
				int xl = rows.x(x - 1);
				int xr = rows.x(x + 1);

				std::uint8_t side[4];
				side[0] = gradient[wyd + xr];
				side[1] = gradient[wyd + x];
				side[2] = gradient[wyd + xl];
				side[3] = gradient[wy + xl];
				max++;

				for (int i = 0; i < 4; i++)
					if (side[i] > max)
						max = side[i];
				gradient[wy + x] = rule.finish(max);
			}
		}
	}

	for (int x = 0; x < w; x++)
	{
		int xl = rows.x(x - 1);
		for (int yi = x; yi < (x + h); yi++)
		{
			int wy = rows.row(yi);
			int wyu = rows.row(yi - 1);
			int wyd = rows.row(yi + 1);
			std::uint8_t max = gradient[wy + x];
			if (rule.writable(max))
			{
				std::uint8_t side[4];
				side[0] = gradient[wyu + xl];
				side[1] = gradient[wyd + xl];
				side[2] = gradient[wy + xl];
				side[3] = gradient[wyu + x];
				max++;

				for (int i = 0; i < 4; i++)
					if (side[i] > max)
						max = side[i];
				gradient[wy + x] = rule.finish(max);
			}
		}
	}

	for (int x = w - 1; x >= 0; x--)
	{
		int xr = rows.x(x + 1);
		for (int yi = x; yi < (x + h); yi++)
		{
			int wy = rows.row(yi);
			int wyu = rows.row(yi - 1);
			int wyd = rows.row(yi + 1);
			std::uint8_t max = gradient[wy + x];
			if (rule.writable(max))
			{
				std::uint8_t side[4];
				side[0] = gradient[wyu + xr];
				side[1] = gradient[wy + xr];
				side[2] = gradient[wyd + xr];
				side[3] = gradient[wyu + x];
				max++;

				for (int i = 0; i < 4; i++)
					if (side[i] > max)
						max = side[i];
				gradient[wy + x] = rule.finish(max);
			}
		}
	}
}
} // namespace detail
// Exactly four staggered sweeps, with no convergence loop. The caller selects
// the cell encoding through a rule above; pinned values are part of that encoding.
template <class CellRule>
void directionalInfluence(std::uint8_t *gradient, const Grid &grid, CellRule rule)
{
	if (grid.powerOfTwo())
		detail::directionalSweeps(gradient, detail::InfluenceRows<true>(grid), rule);
	else
		detail::directionalSweeps(gradient, detail::InfluenceRows<false>(grid), rule);
}
} // namespace field
