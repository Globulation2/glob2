// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "MapStateView.h"
#include "field/GradientConstants.h"
#include <stdexcept>

namespace gradient_preparation
{
// A compact per-rule lookup for live and frozen cells.
template<class Function>
void withOpenTerrain(const MapState::View& view, int swim, Function fn)
{
    if(swim<0 || unsigned(swim)>=std::size(gradient_kernel::WATER_STEP))
        throw std::invalid_argument("Unknown gradient movement class");
    const auto& table=view.rules->gradientPreparation();
    const auto mask=std::uint32_t(1)<<swim;
    fn([&](size_t i) { return Uint16(table[view.cellRules[i]].accessibility&mask
        ? GRADIENT_UNREACHABLE : GRADIENT_FORBIDDEN); });
}
}
