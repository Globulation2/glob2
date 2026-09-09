// SPDX-License-Identifier: GPL-3.0-or-later
// clang++ -std=c++20 test/UnitMotionBlurTest.cpp -o /tmp/unit-blur-test && /tmp/unit-blur-test
#include "../src/render/UnitAnimation.h"
#include <cassert>
#include <iostream>
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
	std::cout << "PASS: shutter frame bounds, loop wrapping, alpha, one-pose identity\n";
}
