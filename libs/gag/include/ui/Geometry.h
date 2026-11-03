// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <algorithm>
#include <climits>

// Integer geometry in logical pixels, shared by layout, painting and hit testing.
namespace GAGGUI::ui
{
struct Point
{
	int x = 0, y = 0;
	bool operator==(const Point &) const = default;
};

struct Size
{
	int w = 0, h = 0;
	bool operator==(const Size &) const = default;
};

struct Insets
{
	int left = 0, top = 0, right = 0, bottom = 0;
	int horizontal() const { return left + right; }
	int vertical() const { return top + bottom; }
	static Insets all(int value) { return {value, value, value, value}; }
	static Insets symmetric(int horizontal, int vertical)
	{
		return {horizontal, vertical, horizontal, vertical};
	}
	bool operator==(const Insets &) const = default;
};

struct Rect
{
	int x = 0, y = 0, w = 0, h = 0;
	int right() const { return x + w; }
	int bottom() const { return y + h; }
	Point origin() const { return {x, y}; }
	Size size() const { return {w, h}; }
	Point center() const { return {x + w / 2, y + h / 2}; }
	bool empty() const { return w <= 0 || h <= 0; }
	// Half-open on both axes so abutting rectangles tile without overlap.
	bool contains(Point p) const { return p.x >= x && p.y >= y && p.x < x + w && p.y < y + h; }
	bool contains(const Rect &o) const
	{
		return o.x >= x && o.y >= y && o.right() <= right() && o.bottom() <= bottom();
	}
	bool intersects(const Rect &o) const
	{
		return o.x < right() && x < o.right() && o.y < bottom() && y < o.bottom();
	}
	Rect intersect(const Rect &o) const
	{
		const int nx = std::max(x, o.x), ny = std::max(y, o.y);
		const int nr = std::min(right(), o.right()), nb = std::min(bottom(), o.bottom());
		return {nx, ny, std::max(0, nr - nx), std::max(0, nb - ny)};
	}
	Rect unite(const Rect &o) const
	{
		if (empty())
			return o;
		if (o.empty())
			return *this;
		const int nx = std::min(x, o.x), ny = std::min(y, o.y);
		return {nx, ny, std::max(right(), o.right()) - nx, std::max(bottom(), o.bottom()) - ny};
	}
	Rect inset(const Insets &i) const
	{
		return {x + i.left, y + i.top, std::max(0, w - i.horizontal()),
				std::max(0, h - i.vertical())};
	}
	Rect inset(int amount) const { return inset(Insets::all(amount)); }
	Rect translated(int dx, int dy) const { return {x + dx, y + dy, w, h}; }
	bool operator==(const Rect &) const = default;
};

// Layout constraints. Unbounded axes let content report its natural size.
struct Constraints
{
	static constexpr int Unbounded = INT_MAX / 4;
	int minW = 0, minH = 0, maxW = Unbounded, maxH = Unbounded;

	static Constraints loose(Size max) { return {0, 0, max.w, max.h}; }
	static Constraints tight(Size size) { return {size.w, size.h, size.w, size.h}; }
	bool boundedW() const { return maxW < Unbounded; }
	bool boundedH() const { return maxH < Unbounded; }
	Size clamp(Size size) const
	{
		return {std::clamp(size.w, minW, std::max(minW, maxW)),
				std::clamp(size.h, minH, std::max(minH, maxH))};
	}
	Size biggest() const
	{
		return {boundedW() ? maxW : minW, boundedH() ? maxH : minH};
	}
	Constraints deflate(const Insets &i) const
	{
		Constraints out;
		out.minW = std::max(0, minW - i.horizontal());
		out.minH = std::max(0, minH - i.vertical());
		out.maxW = boundedW() ? std::max(0, maxW - i.horizontal()) : Unbounded;
		out.maxH = boundedH() ? std::max(0, maxH - i.vertical()) : Unbounded;
		return out;
	}
	Constraints loosen() const { return {0, 0, maxW, maxH}; }
	Constraints withMaxW(int value) const
	{
		Constraints out = *this;
		out.maxW = std::max(minW, value);
		return out;
	}
	Constraints withMaxH(int value) const
	{
		Constraints out = *this;
		out.maxH = std::max(minH, value);
		return out;
	}
	Constraints withMinW(int value) const
	{
		Constraints out = *this;
		out.minW = std::min(value, maxW);
		return out;
	}
	Constraints withMinH(int value) const
	{
		Constraints out = *this;
		out.minH = std::min(value, maxH);
		return out;
	}
	Constraints unboundedH() const
	{
		Constraints out = *this;
		out.minH = 0;
		out.maxH = Unbounded;
		return out;
	}
	bool operator==(const Constraints &) const = default;
};
} // namespace GAGGUI::ui
