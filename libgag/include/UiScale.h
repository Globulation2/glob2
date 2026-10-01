// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <algorithm>
#include <cmath>

namespace GAGCore
{
// SDL display scale already includes pixel density. Mouse/window coordinates do
// not: divide by density once to get the scale used by layout and input mapping.
inline float windowUiScale(float displayScale, float pixelDensity, float preference,
                           float absoluteOverride = 0)
{
    if (std::isfinite(absoluteOverride) && absoluteOverride > 0)
        return std::clamp(absoluteOverride, 1.0f, 4.0f);
    const float system = std::isfinite(displayScale) && displayScale > 0 &&
                         std::isfinite(pixelDensity) && pixelDensity > 0
        ? displayScale / pixelDensity : 1.0f;
    const float multiplier = std::clamp(std::isfinite(preference) && preference > 0
                                      ? preference : 1.0f, 1.0f, 4.0f);
    return system * multiplier;
}
}
