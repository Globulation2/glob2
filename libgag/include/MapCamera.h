// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <algorithm>
#include <cmath>
#include <utility>

// Presentation-only camera. All distances are logical pixels, never map tiles.
// Origins wrap on the torus; a viewport larger than one period is centered.
class MapCamera
{
public:
    double zoom = 1, originX = 0, originY = 0;
    double width = 0, height = 0, mapWidth = 0, mapHeight = 0;
    double offsetX = 0, offsetY = 0;

    static double wrap(double value, double size)
    {
        return size > 0 ? value - std::floor(value / size) * size : value;
    }

    void resize(double w, double h, double mw, double mh)
    {
        if (width > 0 && height > 0)
        {
            auto center = screenToWorld(width / 2, height / 2);
            originX = center.first - w / (2 * zoom);
            originY = center.second - h / (2 * zoom);
        }
        width = w;
        height = h;
        mapWidth = mw;
        mapHeight = mh;
        normalize();
    }

    void normalize()
    {
        originX = wrap(originX, mapWidth);
        originY = wrap(originY, mapHeight);
        offsetX = std::max(0.0, (width - mapWidth * zoom) / 2);
        offsetY = std::max(0.0, (height - mapHeight * zoom) / 2);
        if (offsetX > 0) originX = 0;
        if (offsetY > 0) originY = 0;
    }

    double visibleW() const { return std::min(width / zoom, mapWidth); }
    double visibleH() const { return std::min(height / zoom, mapHeight); }

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
        zoom = std::clamp(value, .5, 3.0);
        offsetX = std::max(0.0, (width - mapWidth * zoom) / 2);
        offsetY = std::max(0.0, (height - mapHeight * zoom) / 2);
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
        return x >= offsetX && y >= offsetY && x < width - offsetX && y < height - offsetY;
    }
};
