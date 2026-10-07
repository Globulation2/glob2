// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "MapStateView.h"
#include "field/GradientConstants.h"
#include <array>

namespace gradient_preparation
{
// Preserve the compact built-in terrain lookup for live and frozen cells.
template<class Function>
void withOpenTerrain(const MapState::View& view, int swim, Function fn)
{
    const auto value=[swim](const TerrainProperties& p) {
        return Uint16(p.walkable || (swim>0 && p.swimmable) ? GRADIENT_UNREACHABLE : GRADIENT_FORBIDDEN);
    };
    if (view.terrainRegistry->size()==TERRAIN_COUNT) {
        std::array<Uint16,TERRAIN_COUNT> table;
        for (unsigned i=0;i<TERRAIN_COUNT;++i) table[i]=value(view.terrainRegistry->properties(static_cast<TerrainType>(i)));
        fn([&](size_t i) { return table[view.terrainIds[i]]; });
    } else fn([&](size_t i) { return value(view.terrainProperties(i)); });
}
}
