// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "AdaptiveGradientPolicy.h"
#include <array>
#include <limits>

namespace gradient_kernel
{
// Immutable offline evidence, parsed on the broker. Each entry names exactly
// one workload and batch size, never a singleton-derived extrapolation.
struct GradientBatchBound
{
    WorkloadKey workload;
    Plan plan=Plan::CPU;
    std::uint64_t costRevision=0,costVariant=0,maxElapsedNs=0,completionMarginNs=0;
};
struct GradientCadenceSample {std::uint64_t tick=0,startedNs=0,nonWaitNs=0;};
class GradientCadenceFloor
{
    std::array<std::uint64_t,128> samples{};
    std::size_t count=0,next=0;
    std::uint64_t minimum=0,revision=0;
    GradientCadenceSample latest;
public:
    void record(GradientCadenceSample sample) noexcept {
        if(latest.tick && sample.tick<=latest.tick) {count=next=0;samples={};minimum=0;++revision;}
        latest=sample;
        if(!sample.nonWaitNs) {count=next=0;samples={};minimum=0;++revision;return;}
        samples[next++%samples.size()]=sample.nonWaitNs;count=std::min(count+1,samples.size());
        // Bounded work on the background coordinator, never the owner.
        auto floor=std::numeric_limits<std::uint64_t>::max();
        for(std::size_t i=0;i<count;++i)floor=std::min(floor,samples[i]);
        if(minimum && floor<minimum)++revision;
        minimum=floor;
    }
    bool valid() const noexcept {return count>=16 && minimum!=0;}
    std::uint64_t floorNs() const noexcept {return valid() ? minimum : 0;}
    std::uint64_t version() const noexcept {return revision;}
    std::size_t observations() const noexcept {return count;}
    GradientCadenceSample last() const noexcept {return latest;}
    bool fits(std::uint64_t publicationTick,std::uint64_t admissionRevision,
              std::uint64_t admissionFloorNs,const GradientBatchBound& bound,
              std::uint64_t nowNs) const noexcept {
        if(!valid() || admissionRevision!=revision || !admissionFloorNs || publicationTick<=latest.tick ||
           !bound.maxElapsedNs || bound.maxElapsedNs>(std::numeric_limits<std::uint64_t>::max()-bound.completionMarginNs)/2) return false;
        const auto floor=std::min(minimum,admissionFloorNs),ticks=publicationTick-latest.tick;
        if(ticks>(std::numeric_limits<std::uint64_t>::max()-latest.startedNs)/floor)return false;
        const auto due=latest.startedNs+ticks*floor;
        return due>nowNs && bound.maxElapsedNs*2+bound.completionMarginNs<due-nowNs;
    }
};
}
