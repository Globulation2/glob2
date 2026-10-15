// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "AirGradientTypes.h"
#include "GradientPipeline.h"
#include <map>
#include <set>
#include <array>
#include "UnitUtils.h"
class Map;
namespace GAGCore { class OutputStream; class InputStream; }
// Map-owned adapter for the existing GradientPipeline, not another executor.
// Only the owner mutates entries: request creates stable FIFO metadata, admission
// reserves output capacity, and the pipeline publishes a complete generation at
// its fixed deadline. Jobs capture Payload plus primitive snapshot leases; they
// never borrow an Entry or mutate a live field.
struct AirGradientRuntime {
    static constexpr unsigned DestinationBase=0x80000000u;
    static constexpr std::size_t MaxEntries=Team::MAX_COUNT*(Building::MAX_COUNT*(3+MaterialCount*8)
        + UnitUtils::MAX_COUNT + MaterialCount*8+1) + 2*GradientPipeline::MaxDelay;
    // Published, admitted and boundary-retired output allocations. Pipeline
    // spare buffers, worker scratch and snapshots have separate bounded owners.
    static constexpr std::uint64_t Budget=64ull*1024*1024;
    // Tracker stamps are serialized and hashed as current/stale because load
    // reconstructs their counters. Stock and claim counters are persisted raw.
    using Dependencies=std::array<std::uint64_t,5>;
    Dependencies dependencies(const Map&,const air_guidance::Key&) const;
    struct Entry {
        air_guidance::Key key;
        // Stable destination address used by pipeline tickets. Eviction drops
        // allocations, while prune waits for logical retirement to erase this.
        std::uint16_t* slot=nullptr;
        std::shared_ptr<const std::uint16_t[]> values;
        std::shared_ptr<const air_guidance::Output> output;
        std::uint64_t captureRevision=0;
        Dependencies dependencies{};
        std::uint32_t capturedTick=0,publishedTick=0,topology=0;
        bool unread=false,charged=false;
    };
    struct Payload {
        air_guidance::Key key;
        // The worker owns mutation until publication converts this to const.
        std::shared_ptr<air_guidance::Output> output=std::make_shared<air_guidance::Output>();
        std::uint64_t captureRevision=0;
        Dependencies dependencies{};
        std::uint32_t capturedTick=0,topology=0;
    };
    struct Retired { unsigned entry=0; std::uint64_t bytes=0; std::shared_ptr<const std::uint16_t[]> values; std::shared_ptr<const air_guidance::Output> output; };
    std::vector<Retired> retired;
    std::uint64_t retiredBytes=0;
    void retireBoundary() { retired.clear(); retiredBytes=0; }
    std::array<std::uint64_t,Team::MAX_COUNT> claimRevision{};
    std::map<unsigned,Entry> entries;
    std::map<air_guidance::Key,unsigned> keyIndex;
    std::set<unsigned> freeIds;
    std::map<const void*,unsigned> slotIndex;
    void clear() { entries.clear(); keyIndex.clear(); freeIds.clear(); slotIndex.clear(); retireBoundary(); claimRevision.fill(0); nextId=1; }
    unsigned nextId=1;
    int width=0,height=0;
    FieldReadiness request(Map&,const air_guidance::Key&,bool refresh=false);
    air_guidance::Published lookup(const air_guidance::Key&) const;
    RouteResult query(Map&,const air_guidance::Key&,int,int,air_guidance::Sample*,bool);
    unsigned idForSlot(std::uint16_t* const*) const;
    void invalidate(Map&);
    void prune(Map&);
    bool tryAdmit(Map&,std::uint16_t**);
    void dropPublished(Map&,unsigned,Entry&);
    bool valid(const Map&,const air_guidance::Key&) const;
    void prepare(Map&,GradientPipeline::Job&,const SimulationSnapshot::Handle&);
    void restoreJob(Map&,GradientPipeline::Job&,std::shared_ptr<Payload>);
    void save(Map&,GAGCore::OutputStream*) const;
    void load(Map&,GAGCore::InputStream*,bool);
    void checkPayload(const Payload&,std::size_t,bool complete,const std::uint16_t* values=nullptr) const;
    void savePayload(const Map&,GAGCore::OutputStream*,const Payload&,std::size_t) const;
    std::shared_ptr<Payload> loadPayload(const Map&,GAGCore::InputStream*,std::size_t);
    std::uint32_t checksum(const Map&,std::uint32_t,bool) const;
};
