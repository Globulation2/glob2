// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "MapStateView.h"
#include "field/GradientConstants.h"
#include <stdexcept>

namespace gradient_preparation
{
// Select one compiled CPU column outside the cell loop. Indexing the 40-byte
// accelerator record and interpreting its mask in every cell costs more than
// loading a prepared 16-bit seed, even when the rule table fits in cache.
template<class Function>
void withOpenTerrain(const MapState::View& view, int swim, Function fn)
{
    if(swim<0 || unsigned(swim)>=MOVEMENT_CLASS_COUNT)
        throw std::invalid_argument("Unknown gradient movement class");
    const auto& table=view.rules->openTerrainSeeds(swimAccessVariant(swim));
    fn([&](size_t i) { return table[view.cellRules[i]]; });
}
}
