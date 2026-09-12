// SPDX-License-Identifier: GPL-3.0-or-later
// clang++ -std=c++20 test/UnitMotionBlurTest.cpp -o /tmp/unit-blur-test && /tmp/unit-blur-test
#include "../src/render/UnitAnimation.h"
#include <algorithm>
#include <cassert>
#include <iostream>
#include <vector>
int main()
{
	for (int base = 0; base <= 384; base += 64)
		for (int dir = 0; dir < 8; ++dir)
			for (int delta = 0; delta < 256; ++delta)
				for (int span : {1, 11, 16, 21, 26, 30, 32, 480})
				{
					int count = 0;
					drawUnitMotionBlur(base, dir, delta, span, [&](int frame, int alpha)
					{
						assert(frame >= base * 4 + dir * 32 && frame < base * 4 + (dir + 1) * 32);
						assert(alpha >= 0 && alpha <= 255);
						if (count++ == 0)
							assert(alpha == 255);
						if (span == 1)
							assert(frame == unitAnimationFrame(base, dir, delta));
					});
					assert(count >= 1 && count <= (span + 7) / 8 + 1);
				}

	// A shutter of one tick never revisits a pose. The fastest WALK/SWIM in
	// kDefaultUnitTypes is 30 delta per tick, so the span the renderer passes
	// is bounded by that regardless of the game speed preset; a shutter that
	// wrapped would average the same pose into itself. See GameRenderUnits.cpp.
	for (int step = 1; step <= 30; ++step)
		for (int dir = 0; dir < 8; ++dir)
			for (int delta = 0; delta < 256; ++delta)
			{
				std::vector<int> seen;
				drawUnitMotionBlur(0, dir, delta, step, [&](int frame, int)
				{
					assert(std::find(seen.begin(), seen.end(), frame) == seen.end());
					seen.push_back(frame);
				});
				assert(!seen.empty() && seen.size() <= 5);
			}
	std::cout << "PASS: shutter frame bounds, loop wrapping, alpha, one-pose identity\n";
	std::cout << "PASS: one-tick shutter never repeats a pose, at most 5 draws\n";
}
