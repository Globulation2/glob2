// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <MapCamera.h>
#include "GlobalContainer.h"

// Fixed screen-space controls shared by gameplay, replays, and the editor.
inline void drawMapZoomControls(const MapCamera &camera)
{
    auto gfx = globalContainer->gfx;
    const int y = gfx->getH() - 26;
    gfx->drawFilledRect(8, y, 170, 22, 20, 25, 35, 230);
    gfx->drawRect(8, y, 28, 22, 120, 130, 150);
    gfx->drawRect(36, y, 56, 22, 120, 130, 150);
    gfx->drawRect(92, y, 28, 22, 120, 130, 150);
    gfx->drawString(18, y + 3, globalContainer->littleFont, "-");
    gfx->drawString(48, y + 3, globalContainer->littleFont, "100%");
    gfx->drawString(102, y + 3, globalContainer->littleFont, "+");
    gfx->drawString(128, y + 3, globalContainer->littleFont,
        gfx->canDrawStretchedSprite()
            ? std::to_string(int(std::round(camera.zoom * 100))) + "%"
            : "GL only");
}

inline bool clickMapZoomControls(MapCamera &camera, int x, int y)
{
    if (x < 8 || x >= 178 || y < globalContainer->gfx->getH() - 26 ||
        y >= globalContainer->gfx->getH() - 4)
        return false;
    if (x < 120 && globalContainer->gfx->canDrawStretchedSprite())
    {
        const double zoom = x < 36 ? camera.zoom / 1.1
                          : x >= 92 ? camera.zoom * 1.1 : 1.0;
        camera.setZoom(zoom, camera.width / 2, camera.height / 2);
    }
    return true;
}
