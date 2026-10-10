// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "BuildingCatalog.h"
#include <algorithm>
#include <stdexcept>
#include <unordered_map>
#include <utility>

// Setup-only interning. Runtime rows keep direct pointers; hashing and allocations
// never occur while stepping the simulation. Hash fields, not structure padding.
class BuildingUnitInteractionPool
{
public:
    static constexpr std::size_t MaximumBytes=16u*1024u*1024u;
    explicit BuildingUnitInteractionPool(std::size_t maximumBytes=MaximumBytes): maximumBytes_(maximumBytes) {}
    std::size_t intern(const std::vector<BuildingUnitInteraction>& row)
    {
        if(row.empty() || (width_ && width_!=row.size()))throw std::runtime_error("Invalid compiled interaction row width");
        width_=row.size();
        Uint64 hash=14695981039346656037ull;
        for(const auto& interaction:row) for(Uint32 field:{Uint32(interaction.projectileDamage),interaction.trainingMask,Uint32(interaction.flags),Uint32(interaction.recruitmentMask)})
        {hash^=field;hash*=1099511628211ull;}
        auto& candidates=buckets_[hash];
        for(std::size_t offset:candidates)
        {
            bool equal=true;
            for(std::size_t i=0;i<row.size() && equal;++i)
            {const auto& existing=rows_[offset+i];equal=existing.projectileDamage==row[i].projectileDamage && existing.trainingMask==row[i].trainingMask && existing.flags==row[i].flags && existing.recruitmentMask==row[i].recruitmentMask;}
            if(equal)return offset;
        }
        if(row.size()>maximumBytes_/sizeof(BuildingUnitInteraction) || rows_.size()>maximumBytes_/sizeof(BuildingUnitInteraction)-row.size())
            throw std::runtime_error("Compiled building/unit interactions exceed storage limit");
        const auto offset=rows_.size();
        const auto required=offset+row.size();
        if(required>rows_.capacity())rows_.reserve(std::min(maximumBytes_/sizeof(BuildingUnitInteraction),std::max(required,2*rows_.capacity())));
        rows_.insert(rows_.end(),row.begin(),row.end());candidates.push_back(offset);return offset;
    }
    std::vector<BuildingUnitInteraction> release() {return std::move(rows_);}
private:
    std::size_t maximumBytes_;
    std::size_t width_=0;
    std::vector<BuildingUnitInteraction> rows_;
    std::unordered_map<Uint64,std::vector<std::size_t>> buckets_;
};
