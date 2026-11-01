// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <array>
#include <cassert>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

namespace field
{
struct Offset
{
	int x, y;
};
// Ordering is part of the traversal contract, including aliased neighbours on
// thin grids. Callers may supply a different stencil without changing the solver.
constexpr unsigned directionIndex(int dx, int dy)
{
	const unsigned slot = (dy + 1) * 3 + dx + 1;
	return slot - (slot > 4);
}
inline constexpr std::array<Offset, 4> Cardinal = {{{-1, 0}, {1, 0}, {0, -1}, {0, 1}}};
inline constexpr std::array<Offset, 8> Surrounding = {
	{{-1, -1}, {0, -1}, {1, -1}, {-1, 0}, {1, 0}, {-1, 1}, {0, 1}, {1, 1}}};

// Positive rectangular geometry with row-major flat indices. Traversal entries
// must be in [0, cells()); index() accepts coordinates across a wrap seam.
class Grid
{
	int w, h, wMask, hMask;
	std::shared_ptr<const std::vector<std::uint8_t>> edges;
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
	Grid withIncomingConnections(std::shared_ptr<const std::vector<std::uint8_t>> value) const
	{
		assert(value && value->size() == cells());
		auto result = *this;
		result.edges = std::move(value);
		return result;
	}
	// Public connections are forward edges. Reverse solvers retain their transpose.
	Grid withConnections(std::shared_ptr<const std::vector<std::uint8_t>> value) const
	{
		assert(value && value->size() == cells());
		auto incoming = std::make_shared<std::vector<std::uint8_t>>(cells(), 0);
		for (int y = 0; y < h; ++y)
			for (int x = 0; x < w; ++x)
				for (unsigned d = 0; d < Surrounding.size(); ++d)
					if ((*value)[y * w + x] & (1u << d))
					{
						const auto o = Surrounding[d];
						(*incoming)[index(x + o.x, y + o.y)] |= 1u << (7 - d);
					}
		return withIncomingConnections(std::move(incoming));
	}
	std::size_t connectionBytes() const { return edges ? edges->capacity() : 0; }
	bool hasConnections() const { return bool(edges); }
	// Bits name predecessors relative to destination i, in Surrounding order.
	std::uint8_t incomingMask(std::size_t i) const { return edges ? (*edges)[i] : 255; }
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
		const int x = wrapX(index), y = index / w;
		for (const auto offset : stencil)
			visit(x + offset.x, y + offset.y);
	}
};
} // namespace field
