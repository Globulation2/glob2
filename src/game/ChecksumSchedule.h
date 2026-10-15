// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstdint>

namespace SimulationChecksum
{
// Derived solely from saved simulation time: no mutable cadence to serialize.
inline constexpr unsigned SecondaryInterval = 32;
constexpr bool secondaryDue(std::uint32_t tick) { return tick % SecondaryInterval == 0; }
}
