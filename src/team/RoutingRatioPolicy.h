// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "UnitConsts.h"
#include <array>

// Authoritative preparation-cost policy. Field identities describe access only;
// the selected ratio is captured by each preparation and never changes a serving field.
struct RoutingRatioState
{
    static constexpr unsigned SelectionInterval = 512;
    static constexpr unsigned EqualCost = unsigned(MovementAccess::Amphibious);
    using Population = std::array<unsigned, LEGACY_SWIM_CLASS_COUNT>;

    unsigned selected = EqualCost;
    unsigned tick = 0;
    bool known = false;

    static bool amphibious(int cost) { return cost > 0 && cost < LEGACY_SWIM_CLASS_COUNT; }

    static unsigned mode(const Population& counts, unsigned previous)
    {
        unsigned best = EqualCost, maximum = 0;
        // Ascending traversal resolves an otherwise unresolved tie to the lower ratio.
        for (unsigned cost = 1; cost < counts.size(); ++cost)
            if (counts[cost] > maximum) { best = cost; maximum = counts[cost]; }
        // Keeping a tied choice avoids needless changes as populations fluctuate.
        if (amphibious(previous) && maximum && counts[previous] == maximum) return previous;
        return best; // An empty population uses equal cost.
    }
};
