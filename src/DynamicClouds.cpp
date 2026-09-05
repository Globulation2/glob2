// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2007 Leo Wandersleb

#include "DynamicClouds.h"
#include "CloudField.h"
#include "GlobalContainer.h"
#include "GraphicContext.h"

void DynamicClouds::compute(const int viewPortX, const int viewPortY, const int viewPortWidth,
                            const int viewPortHeight, const int time, const int worldWidth,
                            const int worldHeight)
{
    if (!(globalContainer->gfx->getOptionFlags() & GraphicContext::USEGPU))
        return;
    // Use the same sampling lattice in both views, including partial grid cells.
    int pixelX = viewPortX * 32, pixelY = viewPortY * 32;
    renderOffsetX = -(pixelX % granularity);
    renderOffsetY = -(pixelY % granularity);
    int startX = pixelX + renderOffsetX, startY = pixelY + renderOffsetY;
    wGrid = (viewPortWidth - renderOffsetX + granularity - 1) / granularity + 1;
    hGrid = (viewPortHeight - renderOffsetY + granularity - 1) / granularity + 1;
    if (alphaMap.size() != static_cast<size_t>(wGrid * hGrid))
    {
        alphaMap.resize(wGrid * hGrid);
        cloudMap.resize(wGrid * hGrid);
    }
    CloudField field(worldWidth * 32, worldHeight * 32, time, cloudSize, cloudStability, maxCloudSpeed,
                     windStability, maxAlpha);
    for (int y = 0; y < hGrid; ++y)
        for (int x = 0; x < wGrid; ++x)
        {
            int wx = startX + x * granularity, wy = startY + y * granularity;
            alphaMap[y * wGrid + x] = field.opacity(wx, wy);
            cloudMap[y * wGrid + x] = field.opacity(wx, wy, std::max(.01f, cloudHeight));
        }
}

void DynamicClouds::computeWorld(const int worldWidth, const int worldHeight, const int time,
                                 std::valarray<unsigned char> &out, int &gridW, int &gridH) const
{
    int cell = granularity;
    while (std::max(worldWidth, worldHeight) * 32 / cell > 128)
        cell *= 2;
    gridW = worldWidth * 32 / cell;
    gridH = worldHeight * 32 / cell;
    if (out.size() != static_cast<size_t>(gridW * gridH))
        out.resize(gridW * gridH);
    CloudField field(worldWidth * 32, worldHeight * 32, time, cloudSize, cloudStability, maxCloudSpeed,
                     windStability, maxAlpha);
    for (int y = 0; y < gridH; ++y)
        for (int x = 0; x < gridW; ++x)
            out[y * gridW + x] = field.opacity(x * cell, y * cell, std::max(.01f, cloudHeight));
}

void DynamicClouds::render(DrawableSurface *dest, const int, const int, DynamicClouds::Layer layer)
{
    if (!(globalContainer->gfx->getOptionFlags() & GraphicContext::USEGPU))
        return;
    // Magnification is sampled in world space, never around the viewport center.
    bool cloud = layer == DynamicClouds::CLOUD;
    dest->drawAlphaMap(cloud ? cloudMap : alphaMap, wGrid, hGrid, renderOffsetX, renderOffsetY, granularity,
                       granularity, cloud ? Color(255, 255, 255) : Color(0, 0, 0));
}
