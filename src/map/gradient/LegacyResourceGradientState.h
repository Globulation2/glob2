// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "Material.h"
#include <array>
#include <cstdint>
#include <map>

namespace GAGCore { class InputStream; }

// Read-only decoder for the resource-only adaptive state in saves through 177.
// The unified policy does not inherit these histories: their seed populations
// differ. MapIO still validates the old source stock and pending captures before
// migrating field cells/deadlines with explicitly unknown unified histories.
// Keep wire validation here independent of any current inference model.
struct LegacyResourceGradientState
{
    struct Sources { std::uint64_t count=0,added=0,removed=0; };
    // Historical selected counter IDs were 0 (invalid goal), 4 (no move), 5
    // (queries). Other IDs in the mask must still be consumed from old saves.
    using MovementCounts=std::array<std::uint64_t,3>;
    struct Capture {
        std::uint32_t tick=0;
        Sources sources{};
        MovementCounts movementBaseline{};
        bool contradictionKnown=false;
        static Capture load(GAGCore::InputStream*,unsigned version);
    };
    struct Field {
        Capture serving;
        MovementCounts movementTotal{};
        bool validMovementCapture(const Capture&) const;
    };
    void load(GAGCore::InputStream*,unsigned teams,unsigned version);
    bool initialized=false;
    std::array<Sources,MaterialCount> sources{};
    std::map<int,Field> fields;
};
