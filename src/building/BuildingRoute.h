// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "MovementClass.h"

// Which approach a building gradient routes to, apart from Building.h so map and
// routing headers can name it without the building class.
enum class BuildingRoute : unsigned char { Footprint, Clearing, Combat, Automatic };
inline constexpr int BUILDING_ROUTE_COUNT = 3;
// Building gradient slots: one per route and movement class.
inline constexpr int BUILDING_GRADIENT_COUNT = BUILDING_ROUTE_COUNT * MOVEMENT_CLASS_COUNT;
