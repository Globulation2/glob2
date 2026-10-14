// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <SDL3/SDL_stdinc.h>
#include "Material.h"

inline constexpr Sint32 PrivateBuildingStockLimit = 255;
using PrivateBuildingStock = Uint8;

// Borrowed inventory from an immutable snapshot: private stock is compact,
// while shared team stock retains its full signed width. Reads return values.
class BuildingStockView
{
    const Uint8* compact = nullptr;
    const Sint32* wide = nullptr;
public:
    BuildingStockView() = default;
    BuildingStockView(const Uint8* values) : compact(values) {}
    BuildingStockView(const Sint32* values) : wide(values) {}
    Sint32 operator[](int material) const { return wide ? wide[material] : compact[material]; }
    const void* privateData() const { return compact ? static_cast<const void*>(compact) : wide; }
    const Sint32* teamData() const { return wide; }
    explicit operator bool() const { return compact || wide; }
};
