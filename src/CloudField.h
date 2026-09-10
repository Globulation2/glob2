// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef GLOB2_CLOUD_FIELD_H
#define GLOB2_CLOUD_FIELD_H
#include "SimplexNoise.h"
#include <algorithm>
#include <cmath>

// One world-anchored field, independent of viewport dimensions and draw count.
struct CloudField
{
    int width, height, timeCoordinate, maxAlpha;
    float offsetX, offsetY, frequency, amplitude;
    CloudField(int w, int h, int time, float size, float stability, float speed, float wind, int alpha)
        : width(w), height(h), maxAlpha(alpha)
    {
        float phase = time / std::max(1.0f, wind);
        offsetX = speed * (.4f * time + .2f * wind * std::sin(phase));
        offsetY = speed * (.2f * time + .2f * wind * std::sin(phase + 1.6f));
        frequency = 256 / std::max(1.0f, size);
        amplitude = std::sqrt(float(alpha)) * 1.8f;
        timeCoordinate = int(float(time) * 256 / std::max(1.0f, stability));
    }
    float sample(int x, int y, float magnification) const
    {
        int nx = int((x / magnification + offsetX) * frequency);
        int ny = int((y / magnification + offsetY) * frequency);
        return amplitude * (SimplexNoise::getNoise3D(nx, ny, timeCoordinate) - 149);
    }
    // Periodic in both axes: the four samples one period apart are blended
    // by position, so the field is continuous across the map seams.
    unsigned char opacity(int x, int y, float magnification = 1) const
    {
        x = ((x % width) + width) % width;
        y = ((y % height) + height) % height;
        float fx = float(x) / width, fy = float(y) / height;
        float top = sample(x, y, magnification) * (1 - fx) + sample(x - width, y, magnification) * fx;
        float bottom = sample(x, y - height, magnification) * (1 - fx) +
                       sample(x - width, y - height, magnification) * fx;
        float value = top * (1 - fy) + bottom * fy;
        return static_cast<unsigned char>(std::min(float(maxAlpha), value * value / 65536));
    }
};
#endif
