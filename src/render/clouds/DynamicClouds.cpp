// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2007 Leo Wandersleb

#include "DynamicClouds.h"
#include "CloudField.h"
#include "GlobalContainer.h"
#include "GraphicContext.h"

void DynamicClouds::compute(const int viewPortX, const int viewPortY, const int viewPortWidth,
                            const int viewPortHeight, const int time, const int worldWidth,
                            const int worldHeight, bool includeCloudLayer, int maxGridSize)
{
    if (!(globalContainer->gfx->getOptionFlags() & GraphicContext::USEGPU))
        return;
    // Keep the lattice anchored in world space. An overview can request fewer
    // samples without changing the field or the normal viewport quality.
    renderCellSize = granularity;
    if (maxGridSize > 0)
        while ((std::max(worldWidth, worldHeight) * 32 + renderCellSize - 1) / renderCellSize > maxGridSize)
            renderCellSize *= 2;
    int pixelX = viewPortX * 32, pixelY = viewPortY * 32;
    renderOffsetX = -(pixelX % renderCellSize);
    renderOffsetY = -(pixelY % renderCellSize);
    int startX = pixelX + renderOffsetX, startY = pixelY + renderOffsetY;
    wGrid = (viewPortWidth - renderOffsetX + renderCellSize - 1) / renderCellSize + 1;
    hGrid = (viewPortHeight - renderOffsetY + renderCellSize - 1) / renderCellSize + 1;
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
            int wx = startX + x * renderCellSize, wy = startY + y * renderCellSize;
            alphaMap[y * wGrid + x] = field.opacity(wx, wy);
            if (includeCloudLayer)
                cloudMap[y * wGrid + x] = field.opacity(wx, wy, std::max(.01f, cloudHeight));
        }
}

void DynamicClouds::computeWorld(const int worldWidth, const int worldHeight, const int time,
                                 std::valarray<unsigned char> &out, int &gridW, int &gridH, int maxGridSize) const
{
    // Sample the same world field at the detail needed by the overview.
    int cell = granularity;
    while (std::max(worldWidth, worldHeight) * 32 / cell > std::max(1, maxGridSize))
        cell *= 2;
    gridW = std::max(1, worldWidth * 32 / cell);
    gridH = std::max(1, worldHeight * 32 / cell);
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
    dest->drawAlphaMap(cloud ? cloudMap : alphaMap, wGrid, hGrid, renderOffsetX, renderOffsetY, renderCellSize,
                       renderCellSize, cloud ? Color(255, 255, 255) : Color(0, 0, 0));
}
