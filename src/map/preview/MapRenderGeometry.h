// SPDX-License-Identifier: GPL-3.0-or-later
#include "field/Grid.h"
#ifndef GLOB2_MAP_RENDER_GEOMETRY_H
#define GLOB2_MAP_RENDER_GEOMETRY_H

namespace MapRenderGeometry
{
// Visit every periodic copy intersecting the view. Bounds include sprite
// overhang, so buildings crossing a map seam are drawn on both sides.
template <typename Draw>
void wrappedCopies(int x, int y, int left, int top, int right, int bottom,
                   int worldW, int worldH, int viewW, int viewH, Draw draw)
{
    const field::Grid grid(worldW,worldH);
    x = grid.wrapX(x);
    y = grid.wrapY(y);
    while (x + right > worldW) x -= worldW;
    while (y + bottom > worldH) y -= worldH;
    for (int py = y; py + top < viewH; py += worldH)
        for (int px = x; px + left < viewW; px += worldW)
            if (px + right > 0 && py + bottom > 0)
                draw(px, py);
}
}
#endif
