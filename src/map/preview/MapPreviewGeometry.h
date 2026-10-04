// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <algorithm>
#include <cmath>

// View-only coordinates: one complete torus period, fitted without distortion.
struct MapPreviewGeometry
{
	struct Rect
	{
		int x, y, w, h;
	};
	double offsetX = 0, offsetY = 0;
	static double wrap(double x) { return x - std::floor(x); }
	static Rect fit(Rect box, int mw, int mh)
	{
		if (mw <= 0 || mh <= 0 || box.w <= 0 || box.h <= 0)
			return {box.x, box.y, 0, 0};
		// A previously fitted rectangle can differ from the exact aspect ratio
		// by less than one pixel on its short axis. Preserve that rounding:
		// fitting 38x309 to a 64x512 map again must not shrink it to 38x304.
		if (std::abs(double(box.w) * mh - double(box.h) * mw) < std::max(mw, mh))
			return box;
		const double scale = std::min(double(box.w) / mw, double(box.h) / mh);
		int w = std::max(1, int(mw * scale)), h = std::max(1, int(mh * scale));
		return {box.x + (box.w - w) / 2, box.y + (box.h - h) / 2, w, h};
	}
	void drag(double dx, double dy, Rect area)
	{
		if (area.w > 0 && area.h > 0)
		{
			offsetX = wrap(offsetX + dx / area.w);
			offsetY = wrap(offsetY + dy / area.h);
		}
	}
	int x(double mapX, int mw, Rect area) const
	{
		return area.x + int(wrap(mapX / mw + offsetX) * area.w);
	}
	int y(double mapY, int mh, Rect area) const
	{
		return area.y + int(wrap(mapY / mh + offsetY) * area.h);
	}
	void reset() { offsetX = offsetY = 0; }
};
