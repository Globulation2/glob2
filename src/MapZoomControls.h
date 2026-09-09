// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <MapCamera.h>
#include "GlobalContainer.h"

// Fixed screen-space controls shared by gameplay, replays, and the editor.
inline void drawMapZoomControls(const MapCamera &camera, bool sidebar = false)
{
    auto gfx = globalContainer->gfx;
    const int x = sidebar ? int(camera.width) + 8 : 8;
    const int y = gfx->getH() - 26;
    gfx->setClipRect();
    gfx->drawFilledRect(x, y, 144, 22, 20, 25, 35, 230);
    gfx->drawRect(x, y, 24, 22, 120, 130, 150);
    gfx->drawRect(x + 24, y, 44, 22, 120, 130, 150);
    gfx->drawRect(x + 68, y, 24, 22, 120, 130, 150);
    gfx->drawString(x + 8, y + 3, globalContainer->littleFont, "-");
    gfx->drawString(x + 30, y + 3, globalContainer->littleFont, "100%");
    gfx->drawString(x + 76, y + 3, globalContainer->littleFont, "+");
    gfx->drawString(x + 100, y + 3, globalContainer->littleFont,
        gfx->canDrawStretchedSprite()
            ? std::to_string(int(std::round(camera.zoom * 100))) + "%"
            : "GL only");
}

inline bool clickMapZoomControls(MapCamera &camera, int x, int y, bool sidebar = false)
{
    x -= sidebar ? int(camera.width) + 8 : 8;
    if (x < 0 || x >= 144 || y < globalContainer->gfx->getH() - 26 ||
        y >= globalContainer->gfx->getH() - 4)
        return false;
    if (x < 92 && globalContainer->gfx->canDrawStretchedSprite())
    {
        const double zoom = x < 24 ? camera.zoom / 1.1
                          : x >= 68 ? camera.zoom * 1.1 : 1.0;
        camera.setZoom(zoom, camera.width / 2, camera.height / 2);
    }
    return true;
}
