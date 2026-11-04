// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "ResourceClaimStratifiedDeep.h"
#include <algorithm>
#include <tuple>
#include <vector>

namespace ResourceClaims::Experiment
{
inline constexpr unsigned DeepTopK=4, DeepTopKMaximum=64;
inline constexpr const char* ScalableTopKDeepDesign="independent-bottom-k-per-disjoint-stratum-v4";
inline constexpr const char* Retention64DeepDesign="independent-bottom-k-per-disjoint-stratum-v5";
inline Uint64 topKManifestLimit(unsigned k,bool scalable) {return k>16?1024*1024:(scalable?256*1024:64*1024);}
inline Uint64 topKFinalLimit(unsigned k) {if(!k || k>DeepTopKMaximum)throw std::invalid_argument("invalid top-k capacity");return DeepStratumCount*Uint64(k)*DeepRecordLimit;}
inline constexpr Uint64 TopKFinalLimit=DeepStratumCount*DeepTopK*DeepRecordLimit;
inline constexpr const char* TopKDeepDesign="independent-bottom-four-per-disjoint-stratum-v3";
inline std::string topKFilename(unsigned stratum,unsigned slot,unsigned capacity=DeepTopK)
{
    if(stratum>=DeepStratumCount || slot>=capacity || !capacity || capacity>DeepTopKMaximum)throw std::invalid_argument("invalid top-k file slot");
    return "event-"+std::string(stratum<10?"0":"")+std::to_string(stratum)+"-"+std::to_string(slot)+".bin";
}
inline bool topKOutputName(std::string_view name,unsigned capacity)
{
    if(!capacity || capacity>DeepTopKMaximum)return false;
    if(name=="manifest.json")return true;
    if(name.size()<14 || name.size()>15 || !name.starts_with("event-") || !name.ends_with(".bin") || name[8]!='-')return false;
    if(name[6]<'0' || name[6]>'9' || name[7]<'0' || name[7]>'9')return false;
    const unsigned stratum=unsigned(name[6]-'0')*10+unsigned(name[7]-'0');
    const auto slot=name.substr(9,name.size()-13);
    if(slot.empty() || (slot.size()>1 && slot[0]=='0'))return false;
    unsigned value=0;
    for(char c:slot) {if(c<'0' || c>'9')return false;value=value*10+unsigned(c-'0');}
    return stratum<DeepStratumCount && value<capacity;
}
struct TopKDeepSlot
{
    struct Entry { Uint64 priority=UINT64_MAX,ordinal=0,writtenOrdinal=0,replacements=0;std::string digest; };
    std::vector<Entry> entries;
    explicit TopKDeepSlot(unsigned capacity=DeepTopK):entries(capacity) {if(!capacity || capacity>DeepTopKMaximum)throw std::invalid_argument("invalid top-k capacity");}
    Uint64 eligible=0,lastOrdinal=0,excludedSampleCap=0,excludedQueueCap=0,excludedInputBytes=0;
    Uint64 unknownPool=0,currentPriorityTies=0;
    std::array<Uint64,3> reasons{};
    unsigned retained() const {return unsigned(std::min<Uint64>(eligible,entries.size()));}
    // The owner calls once per eligible event, before any admission gates.
    // Stable physical slots avoid retaining encoded records or renaming whole cohorts.
    int consider(Uint64 priority,Uint64 ordinal,unsigned reason,bool poolKnown)
    {
        if(!ordinal || ordinal<=lastOrdinal || reason>=reasons.size())throw std::invalid_argument("invalid top-k event");
        lastOrdinal=ordinal;++eligible;++reasons[reason];if(!poolKnown)++unknownPool;
        unsigned worst=0;
        for(unsigned i=0;i<entries.size();++i) {
            if(!entries[i].ordinal) {entries[i].priority=priority;entries[i].ordinal=ordinal;return int(i);}
            if(entries[i].priority==priority)++currentPriorityTies;
            if(std::tie(entries[worst].priority,entries[worst].ordinal)<std::tie(entries[i].priority,entries[i].ordinal))worst=i;
        }
        auto& e=entries[worst];
        if(std::tie(priority,ordinal)>=std::tie(e.priority,e.ordinal))return -1;
        e.priority=priority;e.ordinal=ordinal;return int(worst);
    }
    void excluded(bool queue) {if(queue)++excludedQueueCap;else ++excludedSampleCap;}
    bool current(unsigned slot,Uint64 ordinal) const {return entries.at(slot).ordinal==ordinal;}
    bool written(unsigned slot,Uint64 ordinal,std::string digest)
    {
        if(!current(slot,ordinal))return false; // An evicted pending result must not overwrite its successor.
        auto& e=entries[slot];e.writtenOrdinal=ordinal;e.digest=std::move(digest);++e.replacements;return true;
    }
    bool censored() const
    {
        if(excludedSampleCap || excludedQueueCap || excludedInputBytes)return true;
        unsigned count=0;
        for(const auto& e:entries)if(e.ordinal) {++count;if(e.writtenOrdinal!=e.ordinal || e.digest.empty())return true;}
        return count!=retained();
    }
};
inline void replaceTopKDeepFile(const std::filesystem::path& path,std::span<const Uint8> bytes,unsigned capacity=DeepTopK,bool scalable=false)
{
    const auto finalLimit=topKFinalLimit(capacity);
    const Uint64 manifestLimit=topKManifestLimit(capacity,scalable);
    const bool manifest=path.filename()=="manifest.json";
    const bool knownTarget=topKOutputName(path.filename().string(),capacity);
    if(!knownTarget)throw std::invalid_argument("invalid top-k output target");
    if(bytes.size()>(manifest?manifestLimit:DeepRecordLimit))throw std::runtime_error("top-k record allowance exhausted");
    Uint64 existing=0;
    for(const auto& entry:std::filesystem::directory_iterator(path.parent_path())) {
        const auto name=entry.path().filename().string();const bool known=topKOutputName(name,capacity);
        if(!known || entry.is_symlink() || !entry.is_regular_file())throw std::runtime_error("unexpected top-k output entry");
        const auto size=entry.file_size();
        if(size>(name=="manifest.json"?manifestLimit:DeepRecordLimit))throw std::runtime_error("oversized top-k output entry");
        existing+=size;
    }
    const auto previous=std::filesystem::exists(path)?std::filesystem::file_size(path):0;
    if(existing-previous+bytes.size()>finalLimit+manifestLimit || existing+bytes.size()>finalLimit+StratifiedTransientLimit ||
       std::filesystem::space(path.parent_path()).available<DeepFreeReserve+bytes.size())throw std::runtime_error("top-k storage allowance exhausted");
    const auto temporary=path.string()+".partial";
    if(std::filesystem::exists(temporary))throw std::runtime_error("stale top-k partial");
    std::ofstream out(temporary,std::ios::binary);out.write(reinterpret_cast<const char*>(bytes.data()),std::streamsize(bytes.size()));out.close();
    if(!out)throw std::runtime_error("top-k write failed");std::filesystem::rename(temporary,path);
}
}
