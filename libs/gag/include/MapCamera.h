// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <algorithm>
#include <cmath>
#include <utility>

// Presentation-only camera. All distances are logical pixels, never map tiles.
// Origins wrap on the torus; small maps can still span multiple periods.
class MapCamera
{
public:
    static constexpr double MAX_ZOOM = 5.0;

    double zoom = 1, originX = 0, originY = 0;
    double width = 0, height = 0, mapWidth = 0, mapHeight = 0;
    double offsetX = 0, offsetY = 0;

    static double wrap(double value, double size)
    {
        return size > 0 ? value - std::floor(value / size) * size : value;
    }

    void resize(double w, double h, double mw, double mh, double x=0, double y=0)
    {
        const bool hadViewport = width > 0 && height > 0;
        const auto center = screenToWorld(offsetX+width / 2, offsetY+height / 2);
        offsetX=x;offsetY=y;
        width = w;
        height = h;
        mapWidth = mw;
        mapHeight = mh;
        if (zoom < minimumZoom()) zoom = minimumZoom();
        if (hadViewport)
        {
            originX = center.first - w / (2 * zoom);
            originY = center.second - h / (2 * zoom);
        }
        normalize();
    }

    // Stop when either map period just fills its viewport dimension. Maps
    // already smaller than the viewport retain their existing 1:1 repeats.
    double minimumZoom() const
    {
        if (width <= 0 || height <= 0 || mapWidth <= 0 || mapHeight <= 0) return 1;
        return std::min(1.0, std::max(width / mapWidth, height / mapHeight));
    }

    void normalize()
    {
        originX = wrap(originX, mapWidth);
        originY = wrap(originY, mapHeight);
    }

    double visibleW() const { return width / zoom; }
    double visibleH() const { return height / zoom; }

    std::pair<double, double> screenToWorld(double x, double y) const
    {
        return {originX + (x - offsetX) / zoom, originY + (y - offsetY) / zoom};
    }

    std::pair<double, double> worldToScreen(double x, double y) const
    {
        return {(x - originX) * zoom + offsetX, (y - originY) * zoom + offsetY};
    }

    void setZoom(double value, double x, double y)
    {
        auto anchor = screenToWorld(x, y);
        zoom = std::clamp(value, minimumZoom(), MAX_ZOOM);
        originX = anchor.first - (x - offsetX) / zoom;
        originY = anchor.second - (y - offsetY) / zoom;
        normalize();
    }

    void wheel(double steps, double x, double y)
    {
        setZoom(zoom * std::pow(1.1, steps), x, y);
    }

    // Bridge to existing tile-origin renderers without discarding subpixel panning.
    int tileX() const { return static_cast<int>(std::floor(originX / 32)); }
    int tileY() const { return static_cast<int>(std::floor(originY / 32)); }
    double fractionX() const { return originX - tileX() * 32; }
    double fractionY() const { return originY - tileY() * 32; }
    int localX(double x) const
    {
        return static_cast<int>(std::floor((x - offsetX) / zoom + fractionX()));
    }
    int localY(double y) const
    {
        return static_cast<int>(std::floor((y - offsetY) / zoom + fractionY()));
    }
    bool contains(double x, double y) const
    {
        return x >= offsetX && y >= offsetY && x < offsetX+width && y < offsetY+height;
    }
};
