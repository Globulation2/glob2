// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// Temporary portable experiment wire format; never read by the simulation.
#include "ResourceClaimOracle.h"
#include "online/Sha256.h"
#include <nlohmann/json.hpp>
#include <filesystem>
#include <fstream>
#include <span>
#include <stdexcept>

namespace ResourceClaims::Experiment
{
inline constexpr Uint64 DeepRecordLimit=10ull*1024*1024, DeepDirectoryLimit=24ull*1024*1024;
inline constexpr Uint64 DeepFreeReserve=8ull*1024*1024*1024;
inline constexpr const char* DeepDesign="independent-minhash-one-per-game-over-sampled-contested-published-xy";
// Owner-thread only; worker completion order never participates in ranking.
struct DeepReservoir
{
    Uint64 eligible=0,priority=UINT64_MAX,ordinal=0,writtenOrdinal=0,replacements=0;
    Uint64 excludedSampleCap=0,excludedQueueCap=0;
    bool consider(Uint64 rank,Uint64 event)
    {
        if(!event)throw std::invalid_argument("deep event ordinal must be positive");
        ++eligible;
        if(!ordinal || rank<priority || (rank==priority && event<ordinal)) {
            priority=rank;ordinal=event;return true;
        }
        return false;
    }
    void excluded(bool queue) { if(queue)++excludedQueueCap;else ++excludedSampleCap; }
    void written(Uint64 event) { writtenOrdinal=event;++replacements; }
    bool censored() const { return excludedSampleCap || excludedQueueCap || (eligible && writtenOrdinal!=ordinal); }
};
inline std::vector<Uint8> encodeDeepEvent(const nlohmann::json& metadata,
    std::span<const Uint32> destinations,std::span<const Oracle::Candidate> candidates)
{
    const auto text=metadata.dump();
    if(text.size()>64*1024 || destinations.size()>512*512 || candidates.size()>destinations.size())
        throw std::runtime_error("deep event shape or metadata exceeds allowance");
    const auto bytes=24+text.size()+4*destinations.size()+32*candidates.size()+32;
    if(bytes>DeepRecordLimit)throw std::runtime_error("deep event exceeds record allowance");
    std::vector<Uint8> out;out.reserve(bytes);
    const auto append=[&](Uint64 value,unsigned count){for(unsigned i=0;i<count;++i)out.push_back(Uint8(value>>(8*i)));};
    for(const auto c:std::string_view("G2CLMD01"))out.push_back(Uint8(c));
    append(1,4);append(text.size(),4);append(destinations.size(),4);append(candidates.size(),4);
    out.insert(out.end(),text.begin(),text.end());
    for(const auto tile:destinations)append(tile,4);
    for(const auto& c:candidates) {
        append(c.source,4);append(c.component,4);append((c.available?1:0)|(c.accessEligible?2:0),4);
        append(0,4);append(c.distance,8);append(c.accessDistance,8);
    }
    const auto digest=Online::Sha256::of(out);out.insert(out.end(),digest.begin(),digest.end());
    return out;
}
inline void replaceDeepFile(const std::filesystem::path& path,std::span<const Uint8> bytes)
{
    if(bytes.size()>DeepRecordLimit)throw std::runtime_error("deep record allowance exhausted");
    Uint64 existing=0;
    for(const auto& entry:std::filesystem::directory_iterator(path.parent_path())) {
        if(!entry.is_regular_file())throw std::runtime_error("unexpected deep output entry");
        existing+=entry.file_size();
    }
    if(existing+bytes.size()>DeepDirectoryLimit || std::filesystem::space(path.parent_path()).available<DeepFreeReserve+bytes.size())
        throw std::runtime_error("deep storage allowance or free reserve exhausted");
    const auto temporary=path.string()+".partial";
    if(std::filesystem::exists(temporary))throw std::runtime_error("stale partial deep output");
    std::ofstream output(temporary,std::ios::binary|std::ios::out);
    output.write(reinterpret_cast<const char*>(bytes.data()),std::streamsize(bytes.size()));output.close();
    if(!output)throw std::runtime_error("deep output write failed");
    std::filesystem::rename(temporary,path);
}
}
