// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "MapStateView.h"
#include "field/GradientConstants.h"
#include <array>

namespace gradient_preparation
{
// A compact per-rule lookup for live and frozen cells.
template<class Function>
void withOpenTerrain(const MapState::View& view, int swim, Function fn)
{
    std::vector<Uint16> table(view.rules->size());
    for (size_t r=0;r<table.size();++r)
    {
        const auto& p=view.rules->properties(std::uint16_t(r));
        table[r]=Uint16(p.walkable || (swim>0 && p.swimmable) ? GRADIENT_UNREACHABLE : GRADIENT_FORBIDDEN);
    }
    fn([&](size_t i) { return table[view.cellRules[i]]; });
}
}
