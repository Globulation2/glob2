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
    unsigned char opacity(int x, int y, float magnification = 1) const
    {
        x = ((x % width) + width) % width;
        y = ((y % height) + height) % height;
        int nx = int((x / magnification + offsetX) * frequency);
        int ny = int((y / magnification + offsetY) * frequency);
        float value = amplitude * (SimplexNoise::getNoise3D(nx, ny, timeCoordinate) - 149);
        return static_cast<unsigned char>(std::min(float(maxAlpha), value * value / 65536));
    }
};
#endif
