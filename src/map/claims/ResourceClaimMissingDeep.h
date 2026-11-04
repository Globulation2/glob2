// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// Temporary additive observer wire. Never consumed by simulation.
#include "ResourceClaimDeepEvent.h"
#include "resource/Material.h"
#include <array>
namespace ResourceClaims::Experiment {
inline constexpr unsigned MissingStrata=8;
inline constexpr Uint64 MissingInputLimit=64ull*1024*1024,MissingFinalLimit=8*DeepRecordLimit;
inline constexpr Uint64 MissingTransientLimit=24ull*1024*1024;
inline constexpr unsigned MissingPendingLimit=2,MissingAdmissionLimit=32768;
inline constexpr const char* MissingDesign="independent-minhash-missing-destination-v1";
enum class MissingReason:unsigned { InvalidXY,MissingScalar,MissingXYPlane };
inline unsigned missingStratum(MissingReason reason,MaterialId material,bool pickup) {
    if(!validMaterial(materialIndex(material)) || unsigned(reason)>2)throw std::invalid_argument("invalid missing-destination classification");
    return (reason==MissingReason::InvalidXY?0:4)+(material==MaterialId::Food?0:2)+(pickup?1:0);
}
struct MissingSlot {
    DeepReservoir reservoir;
    std::array<Uint64,3> reasons{};
    Uint64 inputExcluded=0,ties=0;
    std::string digest;
    bool consider(Uint64 rank,Uint64 ordinal,MissingReason reason) {
        ++reasons.at(unsigned(reason));
        if(reservoir.ordinal && rank==reservoir.priority)++ties;
        return reservoir.consider(rank,ordinal);
    }
    bool censored()const{return inputExcluded || reservoir.censored();}
};
inline std::string missingFilename(unsigned slot) {
    if(slot>=MissingStrata)throw std::invalid_argument("missing slot");
    return "event-0"+std::to_string(slot)+".bin";
}
inline std::vector<Uint8> encodeMissingEvent(const nlohmann::json& metadata,Uint32 cells,
    std::span<const Uint32> labels,std::span<const Oracle::Candidate> candidates) {
    const auto text=metadata.dump();
    if(!cells || cells>512*512 || (!labels.empty() && labels.size()!=cells) || candidates.size()>cells || text.size()>64*1024)
        throw std::runtime_error("missing event shape allowance");
    const auto size=28+text.size()+labels.size()*4+candidates.size()*32+32;
    if(size>DeepRecordLimit)throw std::runtime_error("missing event record allowance");
    std::vector<Uint8> out;out.reserve(size);
    const auto append=[&](Uint64 value,unsigned n){for(unsigned i=0;i<n;++i)out.push_back(Uint8(value>>(8*i)));};
    for(char c:std::string_view("G2CLMG01"))out.push_back(Uint8(c));
    append(1,4);append(text.size(),4);append(cells,4);append(labels.size(),4);append(candidates.size(),4);
    out.insert(out.end(),text.begin(),text.end());for(auto tile:labels)append(tile,4);
    for(const auto& c:candidates){append(c.source,4);append(c.component,4);append((c.available?1:0)|(c.accessEligible?2:0),4);append(0,4);append(c.distance,8);append(c.accessDistance,8);}
    const auto hash=Online::Sha256::of(out);out.insert(out.end(),hash.begin(),hash.end());return out;
}
inline void replaceMissingFile(const std::filesystem::path& path,std::span<const Uint8> bytes) {
    if(bytes.size()>(path.filename()=="manifest.json"?64*1024:DeepRecordLimit))throw std::runtime_error("missing file size");
    Uint64 existing=0;
    for(const auto& e:std::filesystem::directory_iterator(path.parent_path())) {
        auto n=e.path().filename().string();bool known=n=="manifest.json";
        for(unsigned i=0;i<MissingStrata;++i)known|=n==missingFilename(i);
        if(!known || e.is_symlink() || !e.is_regular_file() || e.file_size()>(n=="manifest.json"?64*1024:DeepRecordLimit))throw std::runtime_error("unexpected missing capture file");
        existing+=e.file_size();
    }
    auto previous=std::filesystem::exists(path)?std::filesystem::file_size(path):0;
    if(existing-previous+bytes.size()>MissingFinalLimit+64*1024 || existing+bytes.size()>MissingFinalLimit+MissingTransientLimit || std::filesystem::space(path.parent_path()).available<DeepFreeReserve+bytes.size())throw std::runtime_error("missing capture disk allowance");
    const auto partial=path.string()+".partial";std::ofstream f(partial,std::ios::binary);f.write(reinterpret_cast<const char*>(bytes.data()),bytes.size());f.close();
    if(!f)throw std::runtime_error("missing capture write failed");
    std::filesystem::rename(partial,path);
}
}
