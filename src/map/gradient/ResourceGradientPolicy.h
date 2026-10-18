// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "ResourceGradientModel.h"
#include "Material.h"
#include <map>
#include <limits>
#include <utility>

namespace GAGCore { class InputStream; class OutputStream; }

// Owner-only scheduling state. No diagnostic histories, resource identities or
// wall-clock measurements enter the model. Query counts are exact tick windows,
// aggregated in fixed storage rather than one allocation per unit decision.
struct ResourceGradientPolicy
{
    using Sources = ResourceGradientModel::Sources;
    static constexpr unsigned MinimumInterval = 8, MaximumInterval = 128;
    static constexpr int Threshold = 32000; // model output is milli direction cost
    // These epochs describe the world captured for a particular field. Pending
    // jobs carry their own copy; serving epochs change only at publication.
    struct Capture {
        std::uint32_t tick=0; Sources sources{};
        void save(GAGCore::OutputStream*) const;
        static Capture load(GAGCore::InputStream*);
    };
    struct Field {
        Capture serving;
        std::uint32_t submitted=0, queryTick=0;
        bool requested=false, queryKnown=false;
        std::array<std::uint32_t,128> queries{};
        std::uint32_t count32=0,count128=0,historyHash=0;
        static std::uint32_t bucketHash(unsigned i,std::uint32_t v) { return std::rotl(v*0x9e3779b9u,int(i&31)) ^ v*(i+1); }
        void setBucket(unsigned i,std::uint32_t v) { historyHash^=bucketHash(i,queries[i])^bucketHash(i,v);queries[i]=v; }
        // Expiration is amortized over elapsed ticks, independent of how many
        // units queried this field. Unsigned ages also cover counter wrap.
        void advance(std::uint32_t tick) {
            if (!queryKnown || std::uint32_t(tick-queryTick)>=128) {
                queries.fill(0); count32=count128=historyHash=0; queryTick=tick; queryKnown=true; return;
            }
            while (queryTick!=tick) {
                ++queryTick;
                count32-=queries[(queryTick-32)&127];
                count128-=queries[queryTick&127]; setBucket(queryTick&127,0);
            }
        }
        void queried(std::uint32_t tick, bool request) {
            advance(tick);
            // Every model input clamps at one million. Saturating each bucket
            // preserves that result even under deliberately extreme workloads.
            if (queries[tick&127]<1000000) { setBucket(tick&127,queries[tick&127]+1); ++count32; ++count128; }
            requested|=request;
        }
        std::array<std::int64_t,7> inputs(std::uint32_t tick,const Sources& now) {
            advance(tick);
            const auto bounded=[](std::uint64_t x){return std::min<std::uint64_t>(x,1000000);};
            const auto added=bounded(now.added-serving.sources.added),removed=bounded(now.removed-serving.sources.removed);
            const auto denominator=std::max<std::uint64_t>(1,serving.sources.count);
            return {std::uint32_t(tick-serving.tick),count32,count128,std::int64_t(added),std::int64_t(removed),
                std::int64_t(bounded(added*4096/denominator)),std::int64_t(bounded(removed*4096/denominator))};
        }
        // The age cap applies to demanded fields. An idle field never rebuilds
        // merely because time passed; another query must request it again.
        bool refresh(std::uint32_t tick,const Sources& now) {
            if (!std::exchange(requested,false) || std::uint32_t(tick-submitted)<MinimumInterval) return false;
            return std::uint32_t(tick-submitted)>=MaximumInterval || ResourceGradientModel::predict(inputs(tick,now))>=Threshold;
        }
    };
    void save(GAGCore::OutputStream*) const;
    void load(GAGCore::InputStream*,unsigned teams);
    std::uint32_t checksum() const;
    bool initialized=false;
    std::array<Sources,MaterialCount> sources{};
    std::map<int,Field> fields;
    void stockChanged(unsigned before,unsigned after) {
        if (!initialized) return;
        for (auto bits=before^after;bits;bits&=bits-1) {
            auto& c=sources[std::countr_zero(bits)];
            if (after&(bits&-bits)) { ++c.count; ++c.added; }
            else { assert(c.count); --c.count; ++c.removed; }
        }
    }
};
