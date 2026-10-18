// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <SDL3/SDL_stdinc.h>

// Reporting turns, which continue advancing while the simulation is paused.
inline constexpr Uint32 ChecksumDeliveryDelay = 1;

struct ChecksumSample
{
    Uint32 sourceTick = 0;
    Uint32 value = 0;
};
