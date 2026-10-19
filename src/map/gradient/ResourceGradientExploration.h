// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "AdaptiveGradientState.h"
#include "GradientFieldIdentity.h"
#include <charconv>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

// Headless collection only. Kept outside serialized/checksummed policy state:
// resuming interval exploration requires the same explicit command-line options.
// No game RNG is consumed, so assignment is stable across platforms and workers.
struct ResourceGradientExploration
{
    std::vector<unsigned> intervals;
    std::uint32_t seed=0;
    std::ofstream decisions;

    static std::vector<unsigned> parseIntervals(const std::string& text) {
        std::vector<unsigned> result;
        std::size_t start=0;
        do {
            const auto end=text.find(',',start), stop=end==std::string::npos?text.size():end;
            unsigned value=0;
            const auto parsed=std::from_chars(text.data()+start,text.data()+stop,value);
            if (parsed.ec!=std::errc{} || parsed.ptr!=text.data()+stop || start==stop
                || (value!=0 && (value<AdaptiveGradientState::MinimumInterval || value>65536)))
                throw std::invalid_argument("Gradient exploration intervals must be 0 (adaptive) or 8..65536 ticks");
            if (std::find(result.begin(),result.end(),value)!=result.end())
                throw std::invalid_argument("Gradient exploration intervals must be unique");
            result.push_back(value);
            if (end==std::string::npos) return result;
            start=end+1;
        } while (true);
    }
    unsigned intervalFor(GradientFieldKey key) const {return intervalFor(key.packed());}
    unsigned intervalFor(std::uint32_t key) const {
        if (intervals.empty()) return 0;
        std::uint32_t hash=std::uint32_t(key)^seed;
        hash^=hash>>16;hash*=0x7feb352du;hash^=hash>>15;hash*=0x846ca68bu;hash^=hash>>16;
        return intervals[hash%intervals.size()];
    }
    void record(unsigned tick,int key,const char* reason,bool admitted) {
        decisions<<tick<<','<<key<<','<<seed<<','<<intervalFor(key)<<','<<reason<<','<<admitted<<'\n';
    }
};
