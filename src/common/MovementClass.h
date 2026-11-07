// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Movement classes shared by units, terrain costs, routing and gradients. Kept
// apart from unit/UnitConsts.h so that field and map code need not include the
// unit tuning constants.
//! Durable preparation-cost IDs. Amphibious costs 1..6 bucket walk/swim ratios
//! (see Map::swimClass). Live field identities use MovementAccess instead;
//! unused legacy array slots remain only for loading historical saves.
static constexpr int LEGACY_SWIM_CLASS_COUNT = 7;
static constexpr int WATER_ONLY_CLASS = LEGACY_SWIM_CLASS_COUNT;
static constexpr int SWIM_CLASS_COUNT = LEGACY_SWIM_CLASS_COUNT + 1;
// Ground ratio IDs are durable; air has one independent movement class.
static constexpr int AIR_MOVEMENT_CLASS = SWIM_CLASS_COUNT;
static constexpr int MOVEMENT_CLASS_COUNT = SWIM_CLASS_COUNT + 1;
// Routing identity describes passability only. Ratio choices are preparation
// costs, never cache identities. Numeric values retain legacy save namespaces.
enum class MovementAccess : unsigned char { Land = 0, Amphibious = 3, Water = 7, Air = 8 };

// Adapter for durable array slots and historical save IDs. New route queries
// should pass MovementAccess directly rather than choosing a ratio slot.
inline constexpr MovementAccess movementAccessFromLegacy(int value)
{
    if (value == AIR_MOVEMENT_CLASS) return MovementAccess::Air;
    if (value == WATER_ONLY_CLASS) return MovementAccess::Water;
    return value > 0 ? MovementAccess::Amphibious : MovementAccess::Land;
}
inline constexpr MovementAccess movementAccessFromLegacy(MovementAccess value) { return value; }
inline constexpr int accessClass(MovementAccess value) { return int(value); }
inline constexpr MovementAccess movementAccessForSpeeds(int walk, int swim, int fly = 0)
{
    if (fly > 0) return MovementAccess::Air;
    if (walk <= 0 && swim > 0) return MovementAccess::Water;
    return swim > 0 ? MovementAccess::Amphibious : MovementAccess::Land;
}
inline constexpr unsigned swimAccessVariant(int movement)
{ return movement==AIR_MOVEMENT_CLASS?3u:movement==WATER_ONLY_CLASS?2u:unsigned(movement>0); }
