// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2007 Leo Wandersleb

#include "DynamicClouds.h"
#include "CloudField.h"
#include "GlobalContainer.h"
#include "GraphicContext.h"

/// how much field depth separates the gullies of the deck from its raised tops
static const float cloudKnee = .24f;
/// the shaded underside of the deck, seen against the black of the fog
static const Color cloudBody(86, 94, 112);
/// how hard a cloud standing between this one and the sun darkens it
static const float cloudSelfShadow = 1.6f;
/// How far the fog mask is grown and then blurred, in world pixels. Growing by as
/// much as the blur softens is what keeps the deck at full strength right up to the
/// edge of the fog; any less and the black layer shows through as a rim.
static const int cloudFogFeather = 16;

static float smoothStep(float from, float to, float value)
{
    float t = (value - from) / (to - from);
    t = std::min(1.0f, std::max(0.0f, t));
    return t * t * (3 - 2 * t);
}

/// How much this cell stands up, and how bright it ends up: a cell whose
/// neighbour towards the sun stands higher is in that neighbour's shadow.
/// The transition widens with the cell size, because a hard one interpolated
/// across a coarse lattice turns into visible rectangles.
static void relief(float threshold, int cellSize, float here, float sunwards, float &raised, float &bright)
{
    float knee = cloudKnee * std::max(1.0f, float(cellSize) / 12);
    raised = smoothStep(threshold, threshold + knee, (here + 1) * .5f);
    float above = smoothStep(threshold, threshold + knee, (sunwards + 1) * .5f);
    bright = raised - cloudSelfShadow * std::max(0.0f, above - raised);
    bright = std::min(1.0f, std::max(0.0f, bright));
}

void DynamicClouds::feather(const std::valarray<unsigned char> &visibility, int gridW, int gridH,
                            int cellSize, bool wrap, std::valarray<unsigned char> &out)
{
    const int radius = std::max(1, cloudFogFeather / std::max(1, cellSize));
    // Grow twice as far as the blur softens. The fog's own black reaches half a case
    // past the last undiscovered case, so the deck has to still be solid there and
    // do all of its fading beyond it, or the ramp shows the black through itself.
    const int reach = 2 * radius;
    if (out.size() != static_cast<size_t>(gridW * gridH))
        out.resize(gridW * gridH);
    out = visibility;
    std::valarray<unsigned char> pass(gridW * gridH);
    // Grow first, so the deck is still at full strength where the fog begins. If it
    // faded out inside the fog instead, the black layer would show through as a rim.
    for (int step = 0; step < reach; ++step)
    {
        for (int y = 0; y < gridH; ++y)
            for (int x = 0; x < gridW; ++x)
            {
                unsigned char highest = 0;
                for (int dy = -1; dy <= 1; ++dy)
                    for (int dx = -1; dx <= 1; ++dx)
                    {
                        int ax = x + dx, ay = y + dy;
                        if (wrap)
                        {
                            ax = ((ax % gridW) + gridW) % gridW;
                            ay = ((ay % gridH) + gridH) % gridH;
                        }
                        else if (ax < 0 || ay < 0 || ax >= gridW || ay >= gridH)
                            continue;
                        highest = std::max(highest, out[ay * gridW + ax]);
                    }
                pass[y * gridW + x] = highest;
            }
        out = pass;
    }
    // Then blur, which turns the case-aligned edge into a coast the deck can end on.
    for (int step = 0; step < radius; ++step)
    {
        for (int y = 0; y < gridH; ++y)
            for (int x = 0; x < gridW; ++x)
            {
                int sum = 0, count = 0;
                for (int dy = -1; dy <= 1; ++dy)
                    for (int dx = -1; dx <= 1; ++dx)
                    {
                        int ax = x + dx, ay = y + dy;
                        if (wrap)
                        {
                            ax = ((ax % gridW) + gridW) % gridW;
                            ay = ((ay % gridH) + gridH) % gridH;
                        }
                        else if (ax < 0 || ay < 0 || ax >= gridW || ay >= gridH)
                            continue;
                        sum += out[ay * gridW + ax];
                        ++count;
                    }
                pass[y * gridW + x] = static_cast<unsigned char>(sum / count);
            }
        out = pass;
    }
}

void DynamicClouds::shade(int gridW, int gridH, int cellSize, const std::valarray<unsigned char> *fog,
                          std::valarray<unsigned char> &body, std::valarray<unsigned char> &core) const
{
    int step = sunStep(cellSize);
    for (int y = 0; y < gridH; ++y)
        for (int x = 0; x < gridW; ++x)
        {
            // The sun stands off the top right corner, so a cloud is brightest a
            // little that way from where it is deepest.
            int sunX = std::min(x + step, gridW - 1), sunY = std::max(y - step, 0);
            float raised, bright;
            relief(threshold, cellSize, depthMap[y * gridW + x], depthMap[sunY * gridW + sunX], raised,
                   bright);
            // Inside the fog the sky is solid overcast and the field only decides how
            // the deck is lit. Over open ground it decides where there is cloud at all.
            float weight = fog ? float((*fog)[y * gridW + x]) / 255 : 1.0f;
            float cover = fog ? weight : raised * weight;
            body[y * gridW + x] = static_cast<unsigned char>(cover * (fog ? 255 : maxAlpha));
            core[y * gridW + x] = static_cast<unsigned char>(cover * bright * 255);
        }
}

void DynamicClouds::prepare(const int viewPortX, const int viewPortY, const int viewPortWidth,
                            const int viewPortHeight, int &gridW, int &gridH, int &originX, int &originY,
                            int &cellSize)
{
    // Keep the lattice anchored in world space, so the deck does not swim when the
    // viewport scrolls.
    renderCellSize = granularity;
    int pixelX = viewPortX * 32, pixelY = viewPortY * 32;
    renderOffsetX = -(pixelX % renderCellSize);
    renderOffsetY = -(pixelY % renderCellSize);
    wGrid = (viewPortWidth - renderOffsetX + renderCellSize - 1) / renderCellSize + 1;
    hGrid = (viewPortHeight - renderOffsetY + renderCellSize - 1) / renderCellSize + 1;
    gridW = wGrid;
    gridH = hGrid;
    originX = pixelX + renderOffsetX;
    originY = pixelY + renderOffsetY;
    cellSize = renderCellSize;
}

void DynamicClouds::compute(const int viewPortX, const int viewPortY, const int viewPortWidth,
                            const int viewPortHeight, const int time, const int worldWidth,
                            const int worldHeight, const std::valarray<unsigned char> *visibility)
{
    if (!(globalContainer->gfx->getOptionFlags() & GraphicContext::USEGPU))
        return;
    int gridW, gridH, startX, startY, cellSize;
    prepare(viewPortX, viewPortY, viewPortWidth, viewPortHeight, gridW, gridH, startX, startY, cellSize);
    if (cloudMap.size() != static_cast<size_t>(wGrid * hGrid))
    {
        depthMap.resize(wGrid * hGrid);
        cloudMap.resize(wGrid * hGrid);
        coreMap.resize(wGrid * hGrid);
    }
    if (visibility)
        feather(*visibility, wGrid, hGrid, renderCellSize, false, fogMap);
    else
        fogMap.resize(0);

    CloudField field(worldWidth * 32, worldHeight * 32, time, cloudSize, cloudStability, maxCloudSpeed,
                     windStability);
    for (int y = 0; y < hGrid; ++y)
        for (int x = 0; x < wGrid; ++x)
        {
            // Ground the player can see never carries cloud, so skip the noise there.
            if (fogMap.size() && fogMap[y * wGrid + x] == 0)
            {
                depthMap[y * wGrid + x] = -1;
                continue;
            }
            int wx = startX + x * renderCellSize, wy = startY + y * renderCellSize;
            depthMap[y * wGrid + x] = field.depth(wx, wy);
        }
    shade(wGrid, hGrid, renderCellSize, fogMap.size() ? &fogMap : nullptr, cloudMap, coreMap);
}

void DynamicClouds::getWorldGrid(const int worldWidth, const int worldHeight, int &gridW, int &gridH,
                                 int &cellSize, int maxGridSize) const
{
    // Start from a power of two rather than from cloudPatchSize: map sides are
    // powers of two, and a cell that divides them exactly is what lets the texture
    // wrap without a seam.
    cellSize = 8;
    while (std::max(worldWidth, worldHeight) * 32 / cellSize > std::max(1, maxGridSize))
        cellSize *= 2;
    gridW = std::max(1, worldWidth * 32 / cellSize);
    gridH = std::max(1, worldHeight * 32 / cellSize);
}

bool DynamicClouds::sampleWorldRows(const int worldWidth, const int worldHeight, const int time, int gridW,
                                   int gridH, int cellSize, int &nextRow, int rows)
{
    // A whole world at this lattice is far more work than one frame is worth, so it
    // is filled a band at a time and only shown once every row is from the same
    // moment. Nothing reads worldDepth until then.
    if (worldDepth.size() != static_cast<size_t>(gridW * gridH))
    {
        worldDepth.resize(gridW * gridH);
        nextRow = 0;
    }
    CloudField field(worldWidth * 32, worldHeight * 32, time, cloudSize, cloudStability, maxCloudSpeed,
                     windStability);
    int last = std::min(gridH, nextRow + std::max(1, rows));
    for (int y = nextRow; y < last; ++y)
        for (int x = 0; x < gridW; ++x)
            worldDepth[y * gridW + x] = field.depth(x * cellSize, y * cellSize);
    nextRow = last;
    return nextRow >= gridH;
}

void DynamicClouds::shadeWorld(std::valarray<unsigned char> &out, int gridW, int gridH, int cellSize,
                               const std::valarray<unsigned char> *visibility) const
{
    // Two bytes per texel: how bright the cloud is there, and how opaque.
    if (out.size() != static_cast<size_t>(gridW * gridH * 2))
        out.resize(gridW * gridH * 2);
    // The lattice covers the whole toroidal world, so the mask wraps with it.
    std::valarray<unsigned char> fog;
    if (visibility)
        feather(*visibility, gridW, gridH, cellSize, true, fog);

    int step = sunStep(cellSize);
    for (int y = 0; y < gridH; ++y)
        for (int x = 0; x < gridW; ++x)
        {
            int sunX = (x + step) % gridW, sunY = ((y - step) % gridH + gridH) % gridH;
            float raised, bright;
            relief(threshold, cellSize, worldDepth[y * gridW + x], worldDepth[sunY * gridW + sunX], raised,
                   bright);
            float weight = fog.size() ? float(fog[y * gridW + x]) / 255 : 1.0f;
            float cover = fog.size() ? weight : raised * weight;
            // The ring has one texture, so the shaded-to-lit gradient has to travel
            // as luminance next to the alpha rather than as a second pass.
            float luminance = (cloudBody.g + (255 - cloudBody.g) * bright) / 255;
            out[(y * gridW + x) * 2] = static_cast<unsigned char>(luminance * 255);
            out[(y * gridW + x) * 2 + 1] = static_cast<unsigned char>(cover * 255);
        }
}

void DynamicClouds::render(DrawableSurface *dest, const int, const int, DynamicClouds::Layer layer)
{
    if (!(globalContainer->gfx->getOptionFlags() & GraphicContext::USEGPU))
        return;
    if (wGrid == 0 || hGrid == 0)
        return;
    // Magnification is sampled in world space, never around the viewport center.
    const std::valarray<unsigned char> *source = &cloudMap;
    Color colour = cloudBody;
    int offsetX = renderOffsetX, offsetY = renderOffsetY;
    if (layer == DynamicClouds::CLOUD_CORE)
    {
        source = &coreMap;
        colour = Color(255, 255, 255);
    }
    else if (layer == DynamicClouds::SHADOW)
    {
        colour = Color(0, 0, 0);
        // The shadow is this deck cast onto the ground, so it is the same map
        // displaced away from the sun by however high the clouds hang.
        int drop = int((cloudHeight - 1) * 80);
        offsetX -= drop;
        offsetY += drop;
    }
    dest->drawAlphaMap(*source, wGrid, hGrid, offsetX, offsetY, renderCellSize, renderCellSize, colour);
}
