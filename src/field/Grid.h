// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <array>
#include <cassert>
#include <bit>
#include <cstddef>

namespace field
{
struct Offset
{
	int x, y;
};
// Ordering is part of the traversal contract, including aliased neighbours on
// thin grids. Callers may supply a different stencil without changing the solver.
inline constexpr std::array<Offset, 4> Cardinal = {{{-1, 0}, {1, 0}, {0, -1}, {0, 1}}};
inline constexpr std::array<Offset, 8> Surrounding = {
	{{-1, -1}, {0, -1}, {1, -1}, {-1, 0}, {1, 0}, {-1, 1}, {0, 1}, {1, 1}}};

// Positive rectangular geometry with row-major flat indices. Traversal entries
// must be in [0, cells()); index() accepts coordinates across a wrap seam.
class Grid
{
	int w, h, wMask, hMask;
	static int mask(int size) { return (size & (size - 1)) == 0 ? size - 1 : -1; }
	static int wrap(int coordinate, int size, int bitMask)
	{
		if (bitMask >= 0)
			return coordinate & bitMask;
		coordinate %= size;
		return coordinate < 0 ? coordinate + size : coordinate;
	}

  public:
	Grid(int width, int height) : w(width), h(height), wMask(mask(width)), hMask(mask(height))
	{
		assert(width > 0 && height > 0);
	}
	int width() const { return w; }
	int height() const { return h; }
	bool powerOfTwo() const { return wMask >= 0 && hMask >= 0; }
	int maskX() const { return wMask; }
	int maskY() const { return hMask; }
	int widthShift() const
	{
		assert(wMask >= 0);
		return std::countr_zero(unsigned(w));
	}
	std::size_t cells() const { return std::size_t(w) * h; }
	int wrapX(int x) const { return wrap(x, w, wMask); }
	int wrapY(int y) const { return wrap(y, h, hMask); }
	int index(int x, int y) const { return wrapY(y) * w + wrapX(x); }
	// Wrapped flat indices, in stencil order; aliases are intentionally retained.
	template <class Stencil, class Visitor>
	void neighborIndices(int index, const Stencil &stencil, Visitor visit) const
	{
		if (powerOfTwo())
		{
			const int shift = widthShift(), x = index & wMask, y = index >> shift;
			for (const auto d : stencil)
				visit(((y + d.y) & hMask) * w + ((x + d.x) & wMask));
		}
		else
		{
			const int x = index % w, y = index / w;
			for (const auto d : stencil)
				visit(this->index(x + d.x, y + d.y));
		}
	}
	// Raw coordinates, so a bounded caller can reject before wrapping.
	template <class Stencil, class Visit>
	void neighbors(int index, const Stencil &stencil, Visit visit) const
	{
		const int x = index % w, y = index / w;
		for (const auto offset : stencil)
			visit(x + offset.x, y + offset.y);
	}
};
} // namespace field
