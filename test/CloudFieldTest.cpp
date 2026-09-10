// SPDX-License-Identifier: GPL-3.0-or-later

#include "../src/CloudField.h"
#include <cassert>
#include <cmath>
#include <iostream>
static bool same(const CloudField::Sample &a, const CloudField::Sample &b)
{
    return a.height == b.height && a.slopeX == b.slopeX && a.slopeY == b.slopeY;
}
int main()
{
    for (int width : {2048, 4096, 8192})
        for (int height : {2048, 4096})
            for (int time : {0, 250, 10000, 9000000})
            {
                CloudField field(width, height, time, 110, 13000, .3f, 3550);
                bool anyCloud = false, anySlope = false;
                for (int x : {0, 16, 640, width - 16, width + 32})
                    for (int y : {0, 192, height - 16})
                    {
                        auto expected = field.sample(x, y);
                        assert(expected.height >= 0 && expected.height <= 1);
                        anyCloud |= expected.height > 0;
                        anySlope |= expected.slopeX != 0 || expected.slopeY != 0;
                        // The deck tiles the world: one period away it is the same deck.
                        assert(same(field.sample(x + width, y - height), expected));
                        // A full-world atlas and a translated 2D viewport must sample
                        // the identical geographical position, irrespective of their size.
                        for (int viewportX : {0, 32, 992})
                            for (int viewportY : {0, 64, 1024})
                            {
                                int localX = x - viewportX, localY = y - viewportY;
                                CloudField anotherView(width, height, time, 110, 13000, .3f, 3550);
                                assert(same(anotherView.sample(viewportX + localX, viewportY + localY), expected));
                            }
                    }
                assert(anyCloud && anySlope);
                // Repeated sampling cannot advance wind or shape: only elapsed time can.
                auto first = field.sample(512, 640);
                for (int draw = 0; draw < 100; ++draw)
                    assert(same(field.sample(512, 640), first));
                // The wind carries the deck without changing it: what stood at a point
                // stands where the drift has taken it.
                float x0, y0, x1, y1;
                CloudField::drift(time, .3f, 3550, x0, y0);
                CloudField::drift(time + 1, .3f, 3550, x1, y1);
                CloudField later(width, height, time + 1, 110, 13000, .3f, 3550);
                auto moved = later.sample(512 - (x1 - x0), 640 - (y1 - y0));
                assert(std::fabs(moved.height - first.height) < .01f);
            }
    std::cout << "World cloud coordinates, wrapped seams, and draw-independent animation passed\n";
}
