// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "BuildingType.h"
#include <span>
#include <string_view>

//! Queries over the immutable definitions already leased by a presentation frame.
class BuildingCatalogView
{
    std::span<const BuildingType> definitions;
public:
    explicit BuildingCatalogView(std::span<const BuildingType> definitions) : definitions(definitions) {}
    const BuildingType* get(int index) const
    { return index>=0 && size_t(index)<definitions.size() ? &definitions[index] : nullptr; }
    int findByKey(std::string_view name) const
    {
        for (size_t i=0;i<definitions.size();++i) if (definitions[i].key==name) return int(i);
        return -1;
    }
    int getPlaceableTypeNum(std::string_view name) const
    {
        if (name.empty()) return -1;
        if (const int keyed=findByKey(name);keyed>=0) return definitions[keyed].semantics.placeable ? keyed : -1;
        for (size_t i=0;i<definitions.size();++i)
            if (definitions[i].type==name && definitions[i].semantics.placeable) return int(i);
        return -1;
    }
    int getFinishedTypeNum(std::string_view name) const
    {
        if (name.empty()) return -1;
        int index=findByKey(name);
        if (index<0) index=getPlaceableTypeNum(name);
        while (const auto* type=get(index)) {
            if (!type->isBuildingSite) return index;
            index=type->nextLevel;
        }
        return -1;
    }
    const BuildingType* getLastLevel(int index) const
    { const auto* type=get(index); return type ? get(type->terminalTypeNum) : nullptr; }
    const BuildingType* getByType(std::string_view name,int level,bool site) const
    {
        if (name.empty()) return nullptr;
        if (const int keyed=findByKey(name);keyed>=0) {
            const auto* type=get(site ? keyed : getFinishedTypeNum(name));
            return type && bool(type->isBuildingSite)==site ? type : nullptr;
        }
        for (const auto& type:definitions)
            if (type.type==name && type.level==level && bool(type.isBuildingSite)==site) return &type;
        return nullptr;
    }
};
