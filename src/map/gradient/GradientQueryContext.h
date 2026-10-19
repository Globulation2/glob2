// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "GradientFieldIdentity.h"
#include "EntityRandom.h"
#include <cstdint>

// Ephemeral owner-query context, never a persisted field identity. Every
// alternative receives its own copy of random before any selector branch.
struct GradientQueryContext {
    enum class Mode : std::uint8_t { Standard, GuardBalance, ForbiddenEscape, DirectFlight };
    GradientFieldKey key;
    int x=0,y=0;
    EntityRandom random;
    // Forbidden's secondary ranking can be a lazy building/consumer field.
    // Borrowed only for the duration of one synchronous diagnostic evaluation.
    const std::uint16_t* option=nullptr;
    Mode mode=Mode::Standard;
};
struct GradientQueryResult {
    int dx=0,dy=0;
    bool moved=false;
    // Intentional arrival/balancing/wandering is not failed travel. These flags
    // come from the family's real selector/goal semantics, not raw value alone.
    bool travelRequired=true,atGoal=false,goalValid=false;
};
