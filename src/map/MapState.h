// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "Ressource.h"
#include <type_traits>

// Authoritative map records. Simulation arrays and immutable captures use these
// exact types; runtime pointers and query scratch do not belong in this state.
namespace MapState
{
struct ResourceCell { Resource resource; Uint16 fertility = 0; Uint8 mayGrow = 0; };
struct OccupancyCell { Uint16 building = 0xffff, groundUnit = 0xffff, airUnit = 0xffff; Uint8 immobileUnit = 255; };
struct AreaCell { Uint32 forbidden = 0, guard = 0, clear = 0, farm = 0; };
static_assert(std::is_trivially_copyable_v<ResourceCell>);
static_assert(std::is_trivially_copyable_v<OccupancyCell>);
static_assert(std::is_trivially_copyable_v<AreaCell>);
}
