// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef GLOB2_CLOUD_FIELD_H
#define GLOB2_CLOUD_FIELD_H
#include "SimplexNoise.h"
#include <algorithm>
#include <cmath>

// One world-anchored field, independent of viewport dimensions and draw count.
//
// The field is fractal noise read through a domain warp. The warp is what stops
// a sum of octaves reading as stacked round blobs: displacing the domain by a
// slower, larger noise shears the banks the way wind does. Callers turn the depth
// into light and opacity themselves, because a deck confined to the fog of war
// wants the field as relief while one over open ground wants it as coverage.
struct CloudField
{
    /// octaves of the summed noise; the third is already at the sampling limit of
    /// the overview lattice, so more would only alias there
    static const int octaves = 3;
    /// how far the warp displaces the domain, in 256ths of a cloud length
    static const int warp = 90;

    int width, height, timeCoordinate;
    float offsetX, offsetY, frequency;
    CloudField(int w, int h, int time, float size, float stability, float speed, float wind)
        : width(w), height(h)
    {
        float phase = time / std::max(1.0f, wind);
        offsetX = speed * (.4f * time + .2f * wind * std::sin(phase));
        offsetY = speed * (.2f * time + .2f * wind * std::sin(phase + 1.6f));
        frequency = 256 / std::max(1.0f, size);
        timeCoordinate = int(float(time) * 256 / std::max(1.0f, stability));
    }
    // One multi-octave sample in [-1;1]. Not periodic on its own.
    float sample(int x, int y) const
    {
        float px = (x + offsetX) * frequency;
        float py = (y + offsetY) * frequency;
        int warpX = SimplexNoise::getNoise3D(int(px * .5f), int(py * .5f), timeCoordinate / 2) - 128;
        int warpY = SimplexNoise::getNoise3D(int(px * .5f) + 4096, int(py * .5f) - 4096, timeCoordinate / 2) - 128;
        px += warp * warpX / 128.0f;
        py += warp * warpY / 128.0f;
        float sum = 0, amplitude = 1, total = 0, step = 1;
        for (int i = 0; i < octaves; ++i)
        {
            // small wisps churn faster than the banks that carry them
            float age = timeCoordinate * step * (1 + .6f * i);
            int noise = SimplexNoise::getNoise3D(int(px * step), int(py * step), int(age));
            sum += amplitude * (noise - 128) / 128.0f;
            total += amplitude;
            amplitude *= .5f;
            step *= 2;
        }
        return sum / total;
    }
    // Periodic in both axes: the four samples one period apart are blended
    // by position, so the field is continuous across the map seams.
    float depth(int x, int y) const
    {
        x = ((x % width) + width) % width;
        y = ((y % height) + height) % height;
        float fx = float(x) / width, fy = float(y) / height;
        float nw = (1 - fx) * (1 - fy), ne = fx * (1 - fy);
        float sw = (1 - fx) * fy, se = fx * fy;
        float value = sample(x, y) * nw + sample(x - width, y) * ne + sample(x, y - height) * sw +
                      sample(x - width, y - height) * se;
        // Blending four independent fields shrinks the variance towards the middle
        // of the map. Without this the deck is thin there and dense at the origin.
        float spread = std::sqrt(nw * nw + ne * ne + sw * sw + se * se);
        return value / std::max(.5f, spread);
    }
};
#endif
