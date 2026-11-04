// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "UnitConsts.h"
#include <map>

// Authoritative modal-cost selection and deterministic selection boundary.
struct RoutingRatioState {
    unsigned selected=3, tick=0;
    bool known=false;
    static bool amphibious(int c) { return c>0 && c<LEGACY_SWIM_CLASS_COUNT; }
    static unsigned mode(const std::map<unsigned,unsigned>& counts,unsigned previous) {
        unsigned best=0,maximum=0;
        for (const auto& [value,count]:counts) if(count>maximum){best=value;maximum=count;}
        if (auto i=counts.find(previous); i!=counts.end() && i->second==maximum) return previous;
        return best;
    }
};
