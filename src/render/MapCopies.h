// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef MAP_COPIES_H
#define MAP_COPIES_H

#include <algorithm>

// Translate a pixel-space primitive to every periodic map copy intersecting
// the viewport. Bounds are inclusive, including any sprite/radius overhang.
// The callback receives translations, so both endpoints of a line move together.
template<class Draw>
void forEachMapCopy(int left, int top, int right, int bottom,
                    int periodW, int periodH, int viewportW, int viewportH, Draw draw)
{
	if (periodW <= 0 || periodH <= 0 || viewportW <= 0 || viewportH <= 0) return;
	const auto floorDiv = [](int value, int divisor) {
		int quotient = value / divisor;
		return quotient - (value % divisor < 0);
	};
	const int firstX = -floorDiv(right, periodW);
	const int lastX = floorDiv(viewportW - 1 - left, periodW);
	const int firstY = -floorDiv(bottom, periodH);
	const int lastY = floorDiv(viewportH - 1 - top, periodH);
	for (int y = firstY; y <= lastY; ++y)
		for (int x = firstX; x <= lastX; ++x)
			draw(x * periodW, y * periodH);
}

#endif
