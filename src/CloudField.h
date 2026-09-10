// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef GLOB2_CLOUD_FIELD_H
#define GLOB2_CLOUD_FIELD_H
#include <algorithm>
#include <cmath>
#include <vector>

// One world-anchored deck of round lobes, independent of viewport dimensions
// and draw count.
//
// Cartoon clouds are heaps of overlapping domes, so that is what the field is:
// two lattices of spherical caps, one of big lobes and one of small ones filling
// the gaps between them, each lobe jittered off its lattice point and given its
// own radius and height. The deck is the upper envelope of the caps. Callers get
// the height and the slope of the cap on top at a point, and turn those into
// light and opacity themselves. The lattices tile the map exactly, so the deck
// is continuous across the seams, and the wind only translates it.
struct CloudField
{
    struct Lobe
    {
        float x, y, radius, top;
    };
    struct Layer
    {
        int columns, rows;
        float spacingX, spacingY;
        std::vector<Lobe> lobes;
    };
    struct Sample
    {
        /// how high the deck stands here, 0 in a gap and 1 on the tallest lobe
        float height;
        /// how steeply it rises, per world pixel
        float slopeX, slopeY;
    };
    /// how far a lobe may sit off its lattice point, in lattice spacings
    static constexpr float jitter = .45f;
    /// how much the lobes swell and shrink as they age
    static constexpr float breathing = .12f;

    int width, height;
    float offsetX, offsetY, spacing;
    Layer layers[2];

    /// The wind carries the whole deck: this is how far it has come by `time`.
    static void drift(int time, float speed, float wind, float &x, float &y)
    {
        float phase = time / std::max(1.0f, wind);
        x = speed * (.4f * time + .2f * wind * std::sin(phase));
        y = speed * (.2f * time + .2f * wind * std::sin(phase + 1.6f));
    }
    CloudField(int w, int h, int time, float size, float stability, float speed, float wind)
        : width(w), height(h)
    {
        drift(time, speed, wind, offsetX, offsetY);
        // The deck is periodic, so only the fraction of a period matters, and
        // keeping it small keeps the precision however long the wind has blown.
        offsetX = wrap(offsetX, width);
        offsetY = wrap(offsetY, height);
        float age = 2 * 3.14159265f * time / std::max(1.0f, stability);
        // The big lobes set the scale of the deck; the small ones sit between them.
        const float radiusLow[2] = {.7f, .55f}, radiusHigh[2] = {1.15f, .9f};
        const float topLow[2] = {.8f, .55f}, topHigh[2] = {1.0f, .8f};
        for (int l = 0; l < 2; ++l)
        {
            Layer &layer = layers[l];
            float want = std::max(8.0f, size) / (l + 1);
            layer.columns = std::max(1, int(std::lround(width / want)));
            layer.rows = std::max(1, int(std::lround(height / want)));
            layer.spacingX = float(width) / layer.columns;
            layer.spacingY = float(height) / layer.rows;
            float radiusScale = (layer.spacingX + layer.spacingY) * .5f;
            layer.lobes.resize(layer.columns * layer.rows);
            for (int j = 0; j < layer.rows; ++j)
                for (int i = 0; i < layer.columns; ++i)
                {
                    // one independent draw per property, so none of them correlate
                    auto draw = [&](unsigned property) { return unit(hash(l + 8 * property, i, j)); };
                    Lobe &lobe = layer.lobes[j * layer.columns + i];
                    lobe.x = (i + .5f + jitter * (2 * draw(0) - 1)) * layer.spacingX;
                    lobe.y = (j + .5f + jitter * (2 * draw(1) - 1)) * layer.spacingY;
                    float swell = 1 + breathing * std::sin(age + 6.2831853f * draw(2));
                    lobe.radius = radiusScale * mix(radiusLow[l], radiusHigh[l], draw(3)) * swell;
                    lobe.top = mix(topLow[l], topHigh[l], draw(4));
                }
        }
        spacing = (layers[0].spacingX + layers[0].spacingY) * .5f;
    }
    /// The cap standing highest over world pixel (px, py), after the wind.
    Sample sample(int px, int py) const
    {
        Sample best = {0, 0, 0};
        // Wrapped before the wind is added, so the same point of the world goes
        // through the same arithmetic whichever period it was named in.
        float x = ((px % width) + width) % width + offsetX;
        float y = ((py % height) + height) % height + offsetY;
        for (const Layer &layer : layers)
        {
            // A lobe reaches at most jitter + radius past its own lattice cell,
            // which is under two cells, so the five around the point are enough.
            int ix = int(std::floor(x / layer.spacingX)), iy = int(std::floor(y / layer.spacingY));
            for (int dj = -2; dj <= 2; ++dj)
                for (int di = -2; di <= 2; ++di)
                {
                    int cx = ix + di, cy = iy + dj;
                    int wx = ((cx % layer.columns) + layer.columns) % layer.columns;
                    int wy = ((cy % layer.rows) + layer.rows) % layer.rows;
                    const Lobe &lobe = layer.lobes[wy * layer.columns + wx];
                    // The lobe's position is stored in its home tile; bring it
                    // to the tile the neighbour index points at.
                    float lx = lobe.x + (cx - wx) * layer.spacingX;
                    float ly = lobe.y + (cy - wy) * layer.spacingY;
                    float dx = x - lx, dy = y - ly;
                    float d2 = (dx * dx + dy * dy) / (lobe.radius * lobe.radius);
                    if (d2 >= 1)
                        continue;
                    // A cap stands vertical at its rim; hold the slope short of that
                    // so the lit and shaded bands stay broad enough to read.
                    float rise = std::max(.35f, std::sqrt(1 - d2));
                    float z = lobe.top * std::sqrt(1 - d2);
                    if (z <= best.height)
                        continue;
                    best.height = z;
                    float k = -lobe.top / (lobe.radius * lobe.radius * rise);
                    best.slopeX = k * dx;
                    best.slopeY = k * dy;
                }
        }
        return best;
    }

  private:
    static float wrap(float v, int period)
    {
        v = std::fmod(v, float(period));
        return v < 0 ? v + period : v;
    }
    static float mix(float a, float b, float t) { return a + (b - a) * t; }
    static float unit(unsigned h) { return (h & 0xffffu) / 65536.0f; }
    static unsigned hash(unsigned a, unsigned b, unsigned c)
    {
        unsigned h = a * 0x9E3779B1u ^ (b + 0x7F4A7C15u) * 0x85EBCA77u ^ (c + 0x165667B1u) * 0xC2B2AE3Du;
        h ^= h >> 15;
        h *= 0x2C1B3C6Du;
        h ^= h >> 12;
        h *= 0x297A2D39u;
        h ^= h >> 15;
        return h;
    }
};
#endif
