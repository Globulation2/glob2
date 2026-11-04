// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// Temporary observer-only capture. None of these counters enter simulation state.
#include "ResourceClaimDeepEvent.h"
#include "resource/Material.h"
#include <array>

namespace ResourceClaims::Experiment
{
inline constexpr unsigned DeepStratumCount=16;
inline constexpr Uint64 StratifiedFinalLimit=DeepStratumCount*DeepRecordLimit;
inline constexpr Uint64 StratifiedTransientLimit=24ull*1024*1024;
inline constexpr Uint64 StratifiedEncodedPendingLimit=8*DeepRecordLimit;
inline constexpr const char* StratifiedDeepDesign="independent-minhash-per-disjoint-stratum-v2";
inline constexpr std::array<const char*,DeepStratumCount> DeepStratumNames={
    "early-food-movement","early-food-pickup","early-fruit-movement","early-fruit-pickup",
    "early-other-movement","early-other-pickup","late-fruit-movement","late-fruit-pickup",
    "late-other-movement","late-other-pickup","late-food-pooled-movement","late-food-pooled-pickup",
    "late-food-protected-growing-proxy-movement","late-food-protected-growing-proxy-pickup",
    "late-food-other-or-unverified-movement","late-food-other-or-unverified-pickup"};
inline unsigned deepStratum(Uint32 tick,MaterialId material,bool pickup,bool pooled,bool growingProxy)
{
    const bool food=material==MaterialId::Food;
    const bool fruit=material==MaterialId::Cherries || material==MaterialId::Oranges || material==MaterialId::Prunes;
    if(!validMaterial(materialIndex(material)))throw std::invalid_argument("invalid deep material");
    const unsigned phase=pickup?1:0;
    if(tick<60000)return 2*(food?0:fruit?1:2)+phase;
    if(!food)return (fruit?6:8)+phase;
    return (pooled?10:growingProxy?12:14)+phase;
}
struct StratifiedDeepSlot
{
    DeepReservoir reservoir;
    std::array<Uint64,3> reasons{}; // fully_claimed, stale_goal, farm_reserve
    Uint64 unknownPool=0,minimumPriorityTies=0,excludedInputBytes=0;
    std::string digest;
    bool consider(Uint64 priority,Uint64 ordinal,unsigned reason,bool poolKnown)
    {
        if(reason>=reasons.size())throw std::invalid_argument("invalid deep reason");
        ++reasons[reason];if(!poolKnown)++unknownPool;
        if(reservoir.ordinal && priority==reservoir.priority)++minimumPriorityTies;
        return reservoir.consider(priority,ordinal);
    }
    bool censored() const {return excludedInputBytes || reservoir.censored();}
};
// Called serially by the owner after worker completion. No retained record cache.
// One old record plus one replacement may coexist. Only known bounded files are
// accepted; a stale partial is evidence of failure, never silently overwritten.
inline void replaceStratifiedDeepFile(const std::filesystem::path& path,std::span<const Uint8> bytes)
{
    if(bytes.size()>DeepRecordLimit)throw std::runtime_error("stratified deep record allowance exhausted");
    Uint64 existing=0;
    for(const auto& entry:std::filesystem::directory_iterator(path.parent_path())) {
        const auto name=entry.path().filename().string();
        bool known=name=="manifest.json";
        for(unsigned slot=0;slot<DeepStratumCount;++slot)
            known|=name=="event-"+std::string(slot<10?"0":"")+std::to_string(slot)+".bin";
        if(!known || entry.is_symlink() || !entry.is_regular_file())
            throw std::runtime_error("unexpected stratified deep output entry");
        const auto size=entry.file_size();
        if(size>(name=="manifest.json"?64*1024:DeepRecordLimit))
            throw std::runtime_error("oversized stratified deep output entry");
        existing+=size;
    }
    const auto previous=std::filesystem::exists(path)?std::filesystem::file_size(path):0;
    // A manifest is separately bounded to 64 KiB; sixteen records occupy 160 MiB.
    if(existing-previous+bytes.size()>StratifiedFinalLimit+64*1024 ||
        existing+bytes.size()>StratifiedFinalLimit+StratifiedTransientLimit ||
        std::filesystem::space(path.parent_path()).available<DeepFreeReserve+bytes.size())
        throw std::runtime_error("stratified deep storage allowance or free reserve exhausted");
    const auto temporary=path.string()+".partial";
    std::ofstream output(temporary,std::ios::binary|std::ios::out);
    output.write(reinterpret_cast<const char*>(bytes.data()),std::streamsize(bytes.size()));output.close();
    if(!output)throw std::runtime_error("stratified deep output write failed");
    std::filesystem::rename(temporary,path);
}
}
