// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "ExperimentalFeatures.h"
#include "UnitConsts.h"
#include <array>
#include <map>

// Authoritative continuation: speed modes need their own tie history, even
// when two speed pairs quantize to the same routing class.
struct RoutingRatioState {
    unsigned selected=3, walk=0, swim=0, tick=0;
    bool known=false;
    static bool amphibious(int c) { return c>0 && c<LEGACY_SWIM_CLASS_COUNT; }
    int route(int physical) const { return known && amphibious(physical) ? int(selected) : physical; }
    static unsigned mode(const std::map<unsigned,unsigned>& counts,unsigned previous) {
        unsigned best=0,maximum=0;
        for (const auto& [value,count]:counts) if(count>maximum){best=value;maximum=count;}
        if (auto i=counts.find(previous); i!=counts.end() && i->second==maximum) return previous;
        return best;
    }
};
inline bool sharedRoutingRatios(const ExperimentSet& experiments) {
    return experiments.has(ExperimentId::GradientRatioMode) || experiments.has(ExperimentId::GradientSpeedModes)
        || experiments.has(ExperimentId::GradientRatioMedian) || experiments.has(ExperimentId::GradientRatioEqual);
}
