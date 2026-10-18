// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "ResourceGradientPolicy.h"
#include <charconv>
#include <fstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <string_view>
#include <vector>

// Headless collection only. Kept outside serialized/checksummed policy state:
// resuming interval exploration requires the same explicit command-line options.
// One-shot checkpoint branches reject save output: their consumed/wait state
// cannot be reconstructed by simply passing the original options again.
// No game RNG is consumed, so assignment is stable across platforms and workers.
struct ResourceGradientExploration
{
    std::vector<unsigned> intervals;
    std::uint32_t seed=0, forkStart=0;
    int forkField=-1;
    unsigned forkWait=0;
    bool forkComplete=false;
    std::ofstream decisions;

    static std::vector<unsigned> parseIntervals(const std::string& text) {
        std::vector<unsigned> result;
        std::size_t start=0;
        do {
            const auto end=text.find(',',start), stop=end==std::string::npos?text.size():end;
            unsigned value=0;
            const auto parsed=std::from_chars(text.data()+start,text.data()+stop,value);
            if (parsed.ec!=std::errc{} || parsed.ptr!=text.data()+stop || start==stop
                || (value!=0 && (value<ResourceGradientPolicy::MinimumInterval || value>65536)))
                throw std::invalid_argument("Gradient exploration intervals must be 0 (adaptive) or 8..65536 ticks");
            if (std::find(result.begin(),result.end(),value)!=result.end())
                throw std::invalid_argument("Gradient exploration intervals must be unique");
            result.push_back(value);
            if (end==std::string::npos) return result;
            start=end+1;
        } while (true);
    }
    unsigned intervalFor(int key) const {
        if (intervals.empty()) return 0;
        std::uint32_t hash=std::uint32_t(key)^seed;
        hash^=hash>>16;hash*=0x7feb352du;hash^=hash>>15;hash*=0x846ca68bu;hash^=hash>>16;
        return intervals[hash%intervals.size()];
    }
    // Returns a static provenance reason only when a demanded admission is due.
    // The official path calls the existing method without changing its semantics.
    const char* refresh(int key,ResourceGradientPolicy::Field& field,std::uint32_t tick,
        const ResourceGradientPolicy::Sources& sources) {
        if (key==forkField && !forkComplete) {
            if (!std::exchange(field.requested,false)
                || std::uint32_t(tick-field.submitted)<ResourceGradientPolicy::MinimumInterval
                || std::uint32_t(tick-forkStart)<forkWait) return nullptr;
            return "checkpoint_fork";
        }
        const auto interval=intervalFor(key);
        if (!interval) {
            const bool capped=std::uint32_t(tick-field.submitted)>=ResourceGradientPolicy::MaximumInterval;
            return field.refresh(tick,sources)?(capped?"age_cap":"model"):nullptr;
        }
        return std::exchange(field.requested,false) && std::uint32_t(tick-field.submitted)>=interval
            ? "fixed_interval" : nullptr;
    }
    void record(unsigned tick,int key,const char* reason,bool admitted) {
        decisions<<tick<<','<<key<<','<<seed<<','<<intervalFor(key)<<','<<reason<<','<<admitted<<'\n';
        if (admitted && key==forkField && std::string_view(reason)=="checkpoint_fork") forkComplete=true;
    }
};
