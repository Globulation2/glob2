// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "AirGradientTypes.h"
#include "GradientPipeline.h"
#include <map>
class Map;
namespace GAGCore { class OutputStream; class InputStream; }
struct AirGradientRuntime {
    static constexpr unsigned DestinationBase=0x80000000u;
    static constexpr std::size_t MaxEntries=1024;
    static constexpr std::uint64_t Budget=64ull*1024*1024;
    struct Entry {
        air_guidance::Key key;
        std::uint16_t* slot=nullptr;
        std::shared_ptr<const std::uint16_t[]> values;
        std::shared_ptr<const air_guidance::Output> output;
        std::uint64_t captureRevision=0;
        std::uint32_t capturedTick=0,publishedTick=0,topology=0;
        bool unread=false;
    };
    struct Payload {
        air_guidance::Key key;
        std::shared_ptr<air_guidance::Output> output=std::make_shared<air_guidance::Output>();
        std::uint64_t captureRevision=0;
        std::uint32_t capturedTick=0,topology=0;
    };
    struct Retired { unsigned entry=0; std::uint64_t bytes=0; std::shared_ptr<const std::uint16_t[]> values; std::shared_ptr<const air_guidance::Output> output; };
    std::vector<Retired> retired;
    std::uint64_t retiredBytes=0;
    void retireBoundary() { retired.clear(); retiredBytes=0; }
    std::map<unsigned,Entry> entries;
    unsigned nextId=1;
    int width=0,height=0;
    FieldReadiness request(Map&,const air_guidance::Key&,bool refresh=false);
    air_guidance::Published lookup(const air_guidance::Key&) const;
    RouteResult query(Map&,const air_guidance::Key&,int,int,air_guidance::Sample*,bool);
    unsigned idForSlot(std::uint16_t* const*) const;
    void invalidate(Map&);
    bool valid(const Map&,const air_guidance::Key&) const;
    void prepare(Map&,GradientPipeline::Job&,const SimulationSnapshot::Handle&);
    void restoreJob(Map&,GradientPipeline::Job&,std::shared_ptr<Payload>);
    void save(Map&,GAGCore::OutputStream*) const;
    void load(Map&,GAGCore::InputStream*,bool);
    void checkPayload(const Payload&,std::size_t,bool complete,const std::uint16_t* values=nullptr) const;
    void savePayload(GAGCore::OutputStream*,const Payload&,std::size_t) const;
    std::shared_ptr<Payload> loadPayload(GAGCore::InputStream*,std::size_t);
    std::uint32_t checksum(std::uint32_t,bool) const;
};
