// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "Material.h"

// Operation-local results, never part of BuildingStateRecord or a snapshot.
// Discard before inventory, reservations, workers, or building type can change.
struct BuildingMaterialDemand
{
    std::array<int, MaterialSlotCount> deliveries{};
    int totalDeliveries = 0; // Staffing historically counts only the 12 real materials.
    MaterialMask missingMaterials = 0; // Fullness observes all 15 storage slots.
};
