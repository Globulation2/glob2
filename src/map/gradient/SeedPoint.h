// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "MapStateView.h"
#include "MapInternal.h"
#include "SeedTerrain.h"
#include "field/GradientPreparationRules.h"
#include <bit>
#include <span>

namespace gradient_preparation {
// Authoritative natural seed rules, shared by the full production seed pass and
// bounded diagnostic point queries. Registry, masks and terrain column are
// resolved once outside either caller's cell loop.
template<bool Air,class Terrain> struct NaturalMaterialSeed {
    const MapState::View& view;
    const Uint32* fog;
    std::span<const ResourceProperties> properties;
    Uint32 teamMask,forbiddenMask;
    MaterialMask requested;
    int material;
    Terrain terrain;
    Uint16 operator()(size_t i) const {
        const auto& cell=view.occupancy[i];
        if((view.areas[i].forbidden&forbiddenMask) || (!Air&&cell.immobileUnit!=IMMOBILE_UNIT_NONE))return GRADIENT_FORBIDDEN;
        const auto& deposit=view.resources[i].resource;
        const auto* p=deposit.type==NO_RES_TYPE?nullptr:&properties[deposit.type];
        const bool stocked=p&&(p->materialMask&requested)&&
            (std::has_single_bit(p->materialMask)?deposit.amount!=0:MapState::materialAmountAt(view,i,material)>0);
        const bool visible=!p||!p->visibleToHarvest||(fog[i]&teamMask);
        const bool resourceBlocked=p&&(Air?p->blocksAir:p->blocksGround);
        const bool buildingBlocked=!Air&&cell.building!=NOGBID;
        const auto open=(!resourceBlocked&&!buildingBlocked&&!(stocked&&visible))?terrain(i):GRADIENT_FORBIDDEN;
        return Uint16(rules::materialSeed(0,1,resourceBlocked,buildingBlocked,open,stocked,visible));
    }
};
template<bool Air,class Terrain> auto naturalMaterialSeed(const MapState::View& view,const Uint32* fog,int team,int material,Terrain terrain) {
    const auto mask=Uint32(1)<<team;
    return NaturalMaterialSeed<Air,Terrain>{view,fog,view.resourceRegistry->propertyTable(),mask,Air?0:mask,MaterialMask(1u<<material),material,terrain};
}
template<bool Air,class Terrain> struct ClearSeed {
    const MapState::View& view;
    std::span<const Uint32> traits;
    Uint32 teamMask,forbiddenMask;
    bool farm;
    Terrain terrain;
    Uint16 operator()(size_t i) const {
        const auto& area=view.areas[i];
        if(area.forbidden&forbiddenMask)return GRADIENT_FORBIDDEN;
        const auto type=view.resources[i].resource.type;
        const auto flags=type==NO_RES_TYPE?0:traits[type];
        const auto& cell=view.occupancy[i];
        const auto value=Uint16(rules::clearSeed(0,Air||cell.immobileUnit==IMMOBILE_UNIT_NONE,
            flags&(1u<<(Air?20:16)),!Air&&cell.building!=NOGBID,GRADIENT_UNREACHABLE,
            flags&(1u<<18),flags&(1u<<19),area.clear&teamMask,area.farm&teamMask,farm));
        return value==GRADIENT_UNREACHABLE?terrain(i):value;
    }
};
template<bool Air,class Terrain> auto clearSeed(const MapState::View& view,bool farm,int team,Terrain terrain) {
    const auto mask=Uint32(1)<<team;
    return ClearSeed<Air,Terrain>{view,view.resourceRegistry->gradientPreparation(),mask,Air?0:mask,farm,terrain};
}
}
