// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "RenderedCpuWindow.h"
#include <PerformanceTelemetry.h>
#include <array>
#include <memory>
#include <vector>

namespace RenderedCpuDiagnostics {
// Diagnostic-only, two single-producer buffers. Construction and summarize()
// allocate outside the measured interval. The borrowed counter belongs to the
// Engine and is read exclusively by the actual simulation owner. Engine joins
// that owner before detaching the sink or releasing the counter/capture.
class WarmDurations final : public PerformanceTelemetry::DurationEventSink {
public:
    using TickGetter=std::uint64_t(*)(const void*) noexcept;
    static constexpr std::size_t MaxTicks=65536, FrameCapacity=131072;
    struct Sample {std::uint64_t start=0,end=0,duration=0;};
    struct TickSamples {std::uint64_t tick=0;std::array<Sample,3> scopes{};};
    struct Frame {std::uint64_t start=0,end=0;};
    struct Summary {
        // Separate overlapping scopes; never sum these inclusive observations.
        std::array<PerformanceTelemetry::DurationDistribution,3> owner;
        PerformanceTelemetry::DurationDistribution frame;
        std::uint64_t ownerRecords=0,missingTicks=0,unexpectedTicks=0,scopeOutside=0,
                      frameOutside=0,frameBoundary=0;
        bool valid=false,ownerValid=false,frameValid=false;
    };
    const Range ticks;
    struct alignas(64) OwnerState {
        std::vector<TickSamples> samples;
        std::size_t count=0;
        std::uint64_t overflow=0,invalid=0,unassociated=0,abortedSelected=0;
        std::uint64_t expected=0,previousEnd=0,lastEventEnd=0;
        std::array<Sample,3> pending{};
        std::array<unsigned,3> seen{};
        bool active=false,bad=false;
    } owner;
    struct alignas(64) PresentationState {
        std::vector<Frame> samples;
        std::size_t count=0;
        std::uint64_t previous=0,overflow=0,invalid=0,observations=0;
    } presentation;

    WarmDurations(Range range,TickGetter actualTick,const void* context,
                  std::size_t frameCapacity=FrameCapacity)
        :ticks(range),actualTick(actualTick),context(context) {
        const auto count=ticks.end-ticks.start;
        if(ticks.initial>ticks.start||ticks.end<=ticks.start||count>MaxTicks
            ||ticks.end>std::numeric_limits<std::uint32_t>::max()||!actualTick||!context
            ||!frameCapacity||frameCapacity>FrameCapacity)
            throw std::invalid_argument("Warm distributions require bounded ticks, frames and an owner counter");
        owner.samples.resize(std::size_t(count));
        presentation.samples.resize(frameCapacity);
    }
    // Created before scopes, so unwind stops scopes before clearing association.
    class Advance {
        WarmDurations* capture=nullptr;
        bool success=false;
    public:
        Advance(WarmDurations* c,std::uint64_t before) noexcept: capture(c) {
            if(capture)capture->begin(before);
        }
        ~Advance(){if(capture)capture->finish(success);}
        void completed() noexcept {success=true;}
        Advance(const Advance&)=delete;
        Advance& operator=(const Advance&)=delete;
    };
    void duration(PerformanceTelemetry::Id id,std::uint64_t start,
                  std::uint64_t end,std::uint64_t duration) noexcept override {
        const auto slot=index(id);
        if(slot==3)return;
        if(!owner.active){++owner.unassociated;return;}
        // Idle calls and unexpected multi-step/unrelated calls acquire no tags.
        if(actualTick(context)!=owner.expected)return;
        if(owner.expected<=ticks.start||owner.expected>ticks.end)return;
        if(++owner.seen[slot]!=1||!start||end<start||start<owner.previousEnd||end<owner.lastEventEnd
            ||duration>end-start){owner.bad=true;++owner.invalid;return;}
        owner.lastEventEnd=end;
        owner.pending[slot]={start,end,duration};
    }
    void presented(std::uint64_t t) noexcept override {
        ++presentation.observations;
        if(!t||(presentation.previous&&t<presentation.previous))++presentation.invalid;
        if(presentation.previous&&t>=presentation.previous) {
            if(presentation.count<presentation.samples.size())
                presentation.samples[presentation.count++]={presentation.previous,t};
            else ++presentation.overflow;
        }
        // Independent of Collector::configure/reset of mode or pacing budgets.
        presentation.previous=t;
    }
    // Only after both producers have stopped. Whole frame intervals wholly
    // inside the wall window are selected; boundary intervals are counted and
    // excluded, never clipped into fabricated short intervals.
    std::unique_ptr<Summary> summarize(std::uint64_t start,std::uint64_t end,
                                       bool wallValid) const {
        auto result=std::make_unique<Summary>();
        std::uint64_t expected=ticks.start+1;
        for(std::size_t i=0;i<owner.count;++i) {
            const auto& record=owner.samples[i];
            if(record.tick<expected||record.tick>ticks.end){++result->unexpectedTicks;continue;}
            // Retain later valid ticks after a gap, but report the missing tick
            // count below and never declare the partial distribution valid.
            expected=record.tick;
            ++expected;++result->ownerRecords;
            for(unsigned s=0;s<3;++s) {
                const auto& sample=record.scopes[s];
                if(sample.start<start||sample.end>end)++result->scopeOutside;
                result->owner[s].add(sample.duration);
            }
        }
        result->missingTicks=ticks.end-ticks.start-result->ownerRecords;
        for(std::size_t i=0;i<presentation.count;++i) {
            const auto& f=presentation.samples[i];
            if(f.end<=start||f.start>=end)++result->frameOutside;
            else if(f.start<start||f.end>end)++result->frameBoundary;
            else result->frame.add(f.end-f.start);
        }
        result->ownerValid=wallValid&&!owner.overflow&&!owner.invalid&&!owner.abortedSelected
            &&!result->missingTicks&&!result->unexpectedTicks&&!result->scopeOutside
            &&result->ownerRecords==ticks.end-ticks.start;
        result->frameValid=wallValid&&!presentation.overflow&&!presentation.invalid&&result->frame.count;
        result->valid=result->ownerValid&&result->frameValid;
        return result;
    }
private:
    TickGetter actualTick;
    const void* context;
    static unsigned index(PerformanceTelemetry::Id id) noexcept {
        using Id=PerformanceTelemetry::Id;
        return id==Id::Loop?0:id==Id::Work?1:id==Id::Tick?2:3;
    }
    void begin(std::uint64_t before) noexcept {
        if(owner.active){++owner.invalid;owner.bad=true;return;}
        owner.active=true;owner.expected=before+1;owner.seen={};owner.pending={};owner.bad=false;
        owner.lastEventEnd=owner.previousEnd;
    }
    void finish(bool success) noexcept {
        const auto actual=actualTick(context);
        const bool selected=owner.expected>ticks.start&&owner.expected<=ticks.end;
        if(selected&&actual==owner.expected) {
            if(!success||owner.bad||owner.seen!=std::array<unsigned,3>{1,1,1})++owner.abortedSelected;
            else if(owner.count<owner.samples.size())
                owner.samples[owner.count++]={actual,owner.pending};
            else ++owner.overflow;
        }
        if(success&&actual==owner.expected&&owner.seen[0]==1)
            owner.previousEnd=owner.pending[0].end;
        owner.active=false;owner.expected=0;owner.seen={};owner.pending={};owner.bad=false;
    }
};
} // namespace RenderedCpuDiagnostics
