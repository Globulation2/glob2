// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "MapStateView.h"
#include "SeedTerrain.h"
#include "MapInternal.h"
#include "Building.h"
#include <array>
#include <atomic>
#include <type_traits>

namespace gradient_preparation
{
// Run supplies either the owner's fixed chunk barrier or one worker's range.
// Both callers read the same plain records and preserve goal/obstacle precedence.
template<class Run>
void materialCells(const MapState::View& view, const Uint32* fog, int team, int material,
    int swim, Uint16* out, const Uint16* suppliers, Run run)
{
    const Uint32 mask=Uint32(1)<<team;
    const unsigned base=unsigned(team)*Building::MAX_COUNT;
    const MaterialMask requested=MaterialMask(1u<<material);
    withOpenTerrain(view,swim,[&](auto terrainAt) {
    const auto prepare=[&](auto market) {
        run([&](size_t begin, size_t end) {
            for (size_t i=begin; i<end; ++i) {
                const auto& cell=view.occupancy[i];
                Uint16 value=GRADIENT_FORBIDDEN;
                if (!(view.areas[i].forbidden&mask) && cell.immobileUnit==IMMOBILE_UNIT_NONE) {
                    const auto& deposit=view.resources[i].resource;
                    const auto* p=deposit.type==NO_RES_TYPE ? nullptr : &view.resourceProperties(deposit.type);
                    if (!p || !p->blocksGround) {
                        if (cell.building==NOGBID) {
                            value=terrainAt(i);
                        } else if constexpr (decltype(market)::value) {
                            const unsigned id=unsigned(cell.building)-base;
                            if (id<Building::MAX_COUNT) value=suppliers[id];
                        }
                    }
                    if (p && (p->materialMask&requested)) {
                        const bool stocked=std::has_single_bit(p->materialMask) ? deposit.amount!=0 : MapState::materialAmountAt(view,i,material)>0;
                        if (stocked && (!p->visibleToHarvest || (fog[i]&mask))) value=GRADIENT_AT_GOAL;
                    }
                }
                out[i]=value;
            }
        });
    };
    if (suppliers) prepare(std::true_type{}); else prepare(std::false_type{});
    });
}
template<class Run>
void clearCells(const MapState::View& view, bool farm, int team, int swim, Uint16* out, Run run)
{
    constexpr Uint8 blocked=1, clearable=2, farmClearable=4;
    std::array<Uint8, ResourceRegistry::Capacity> traits;
    const auto& properties=view.resourceRegistry->propertyTable();
    for (size_t id=0; id<properties.size(); ++id) {
        const auto& p=properties[id];
        traits[id]=(p.blocksGround?blocked:0)|(p.clearable?clearable:0)|(farm&&p.clearable&&!p.farmable?farmClearable:0);
    }
    const Uint32 mask=Uint32(1)<<team;
    withOpenTerrain(view,swim,[&](auto terrainAt) {
    run([&](size_t begin, size_t end) {
        for (size_t i=begin; i<end; ++i) {
            Uint16 value=GRADIENT_FORBIDDEN;
            const auto& area=view.areas[i];
            if (!(area.forbidden&mask)) {
                const auto type=view.resources[i].resource.type;
                const Uint8 flags=type==NO_RES_TYPE?0:traits[type];
                if (((flags&clearable)&&(area.clear&mask)) || ((flags&farmClearable)&&(area.farm&mask))) value=GRADIENT_AT_GOAL;
                else if (!(flags&blocked) && view.occupancy[i].immobileUnit==IMMOBILE_UNIT_NONE && view.occupancy[i].building==NOGBID) {
                    value=terrainAt(i);
                }
            }
            out[i]=value;
        }
    });
    });
}
template<class Run>
bool guardCells(const MapState::View& view, Uint32 allies, int team, int swim, Uint16* out, Run run)
{
    std::array<Uint8, ResourceRegistry::Capacity> blocked;
    const auto& properties=view.resourceRegistry->propertyTable();
    for (size_t id=0; id<properties.size(); ++id) blocked[id]=properties[id].blocksGround;
    const Uint32 mask=Uint32(1)<<team;
    std::atomic<size_t> painted{0};
    withOpenTerrain(view,swim,[&](auto terrainAt) {
    run([&](size_t begin, size_t end) {
        size_t count=0;
        for (size_t i=begin; i<end; ++i) {
            const auto& cell=view.occupancy[i];
            const auto resource=view.resources[i].resource.type;
            Uint16 value=GRADIENT_FORBIDDEN;
            if (!(view.areas[i].forbidden&mask) && cell.immobileUnit==IMMOBILE_UNIT_NONE
                && (resource==NO_RES_TYPE || !blocked[resource])
                && (cell.building==NOGBID || !(allies&(Uint32(1)<<Building::GIDtoTeam(cell.building))))) {
                if (terrainAt(i)!=GRADIENT_FORBIDDEN) value=(view.areas[i].guard&mask)?GRADIENT_AT_GOAL:GRADIENT_UNREACHABLE;
            }
            out[i]=value;
            count += value==GRADIENT_AT_GOAL;
        }
        painted.fetch_add(count,std::memory_order_relaxed);
    });
    });
    return painted.load(std::memory_order_relaxed)!=0;
}
}
