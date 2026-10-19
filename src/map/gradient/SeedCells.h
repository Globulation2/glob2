// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "MapStateView.h"
#include "SeedTerrain.h"
#include "SeedPoint.h"
#include "MapInternal.h"
#include "Building.h"
#include "field/GradientPreparationRules.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <type_traits>

namespace gradient_preparation
{
// Run supplies either the owner's fixed chunk barrier or one worker's range.
// Both callers read the same plain records and preserve goal/obstacle precedence.
template<bool Air, class Run>
void materialCellsForMovement(const MapState::View& view, const Uint32* fog, int team, int material,
    int swim, Uint16* out, const Uint16* suppliers, Run run, std::atomic<std::uint64_t>* goalCount=nullptr)
{
    const Uint32 mask=Uint32(1)<<team;
    const Uint32 forbiddenMask=Air ? 0 : mask;
    const unsigned base=unsigned(team)*Building::MAX_COUNT;
    const MaterialMask requested=MaterialMask(1u<<material);
    withOpenTerrain(view,swim,[&](auto terrainAt) {
    const auto natural=naturalMaterialSeed<Air>(view,fog,team,material,terrainAt);
    const auto prepare=[&](auto market) {
        run([&](size_t begin, size_t end) {
            std::uint64_t count=0;
            for (size_t i=begin; i<end; ++i) {
                Uint16 value=GRADIENT_FORBIDDEN;
                if constexpr(!std::remove_cvref_t<decltype(market)>::value) value=natural(i);
                else {
                    const auto& cell=view.occupancy[i];
                    if (!(view.areas[i].forbidden&forbiddenMask) && (Air || cell.immobileUnit==IMMOBILE_UNIT_NONE)) {
                        const auto& deposit=view.resources[i].resource;
                        const auto* p=deposit.type==NO_RES_TYPE?nullptr:&view.resourceProperties(deposit.type);
                        if (!p || !(Air ? p->blocksAir : p->blocksGround)) {
                            if (cell.building==NOGBID || (Air && unsigned(cell.building)-base>=Building::MAX_COUNT)) value=terrainAt(i);
                            else {
                                const unsigned id=unsigned(cell.building)-base;
                                if(id<Building::MAX_COUNT)value=suppliers[id];
                                if(Air&&value==GRADIENT_FORBIDDEN)value=terrainAt(i);
                            }
                        }
                        if(p&&(p->materialMask&requested)) {
                            const bool stocked=std::has_single_bit(p->materialMask)?deposit.amount!=0:MapState::materialAmountAt(view,i,material)>0;
                            if(stocked&&(!p->visibleToHarvest||(fog[i]&mask)))value=GRADIENT_AT_GOAL;
                        }
                    }
                }
                out[i]=value;
                if(goalCount)count+=value>GRADIENT_UNREACHABLE;
            }
            if(goalCount)goalCount->fetch_add(count,std::memory_order_relaxed);
        });
    };
    if (suppliers) prepare(std::true_type{}); else prepare(std::false_type{});
    });
}
template<class Run>
void materialCells(const MapState::View& view, const Uint32* fog, int team, int material,
    int swim, Uint16* out, const Uint16* suppliers, Run run, std::atomic<std::uint64_t>* goalCount=nullptr)
{
    if (swim==AIR_MOVEMENT_CLASS) return materialCellsForMovement<true>(view,fog,team,material,swim,out,suppliers,run,goalCount);
    return materialCellsForMovement<false>(view,fog,team,material,swim,out,suppliers,run,goalCount);
}
template<bool Air, class Run>
void clearCellsForMovement(const MapState::View& view, bool farm, int team, int swim, Uint16* out, Run run, std::atomic<std::uint64_t>* goalCount=nullptr)
{
    withOpenTerrain(view,swim,[&](auto terrainAt) {
    const auto current=clearSeed<Air>(view,farm,team,terrainAt);
    run([&](size_t begin,size_t end) {
        std::uint64_t count=0;
        for(size_t i=begin;i<end;++i) {
            const auto value=current(i);out[i]=value;
            if(goalCount)count+=value>GRADIENT_UNREACHABLE;
        }
        if(goalCount)goalCount->fetch_add(count,std::memory_order_relaxed);
    });
    });
}
template<class Run>
void clearCells(const MapState::View& view, bool farm, int team, int swim, Uint16* out, Run run, std::atomic<std::uint64_t>* goalCount=nullptr)
{
    if (swim==AIR_MOVEMENT_CLASS) return clearCellsForMovement<true>(view,farm,team,swim,out,run,goalCount);
    return clearCellsForMovement<false>(view,farm,team,swim,out,run,goalCount);
}
template<bool Air, class Run>
bool guardCellsForMovement(const MapState::View& view, Uint32 allies, int team, int swim, Uint16* out, Run run, std::atomic<std::uint64_t>* goalCount=nullptr)
{
    std::array<Uint8, ResourceRegistry::Capacity> blocked;
    const auto& properties=view.resourceRegistry->propertyTable();
    for (size_t id=0; id<properties.size(); ++id) blocked[id]=(Air ? properties[id].blocksAir : properties[id].blocksGround);
    const Uint32 mask=Uint32(1)<<team;
    const Uint32 forbiddenMask=Air ? 0 : mask;
    std::atomic<size_t> painted{0};
    withOpenTerrain(view,swim,[&](auto terrainAt) {
    run([&](size_t begin, size_t end) {
        size_t count=0;
        for (size_t i=begin; i<end; ++i) {
            const auto& cell=view.occupancy[i];
            const auto resource=view.resources[i].resource.type;
            Uint16 value=GRADIENT_FORBIDDEN;
            if (!(view.areas[i].forbidden&forbiddenMask) && (Air || cell.immobileUnit==IMMOBILE_UNIT_NONE)
                && (resource==NO_RES_TYPE || !blocked[resource])
                && ((Air || cell.building==NOGBID) || !(allies&(Uint32(1)<<Building::GIDtoTeam(cell.building))))) {
                if (terrainAt(i)!=GRADIENT_FORBIDDEN) value=(view.areas[i].guard&mask)?GRADIENT_AT_GOAL:GRADIENT_UNREACHABLE;
            }
            out[i]=value;
            count += value==GRADIENT_AT_GOAL;
        }
        painted.fetch_add(count,std::memory_order_relaxed);
    });
    });
    if(goalCount)goalCount->fetch_add(painted.load(std::memory_order_relaxed),std::memory_order_relaxed);
    return painted.load(std::memory_order_relaxed)!=0;
}

template<class Run>
bool guardCells(const MapState::View& view, Uint32 allies, int team, int swim, Uint16* out, Run run, std::atomic<std::uint64_t>* goalCount=nullptr)
{
    if (swim==AIR_MOVEMENT_CLASS) return guardCellsForMovement<true>(view,allies,team,swim,out,run,goalCount);
    return guardCellsForMovement<false>(view,allies,team,swim,out,run,goalCount);
}
// Owner scalars of one building field: everything the seeder reads besides
// the captured map, so a worker never dereferences the Building.
struct BuildingSeed
{
    int posX=0, posY=0, width=1, height=1, unitStayRange=0, swim=0;
    Uint16 gid=NOGBID;
    BuildingRoute route=BuildingRoute::Footprint; // resolved, never Automatic
    bool occupiesGround=true;
    Uint32 teamMask=0, allies=0;
    std::array<bool, MaterialCount> clearingMaterials{};
};
struct BuildingSeedResult
{
    bool locked=false;
    // Clearing routes only: 1 with a resource to clear in range, 2 without.
    Uint8 resourceState=0;
};
// Mirrors Map::updateGlobalGradient's seeding for every route. Run supplies the
// owner's chunk barrier or one worker's range, as for the other seeders.
template<bool WaterOnly, bool Air=false, class Run>
BuildingSeedResult buildingCellsForMovement(const MapState::View& view, const BuildingSeed& b, Uint16* out, Run run)
{
    BuildingSeedResult result;
    const bool canSwim=b.swim>0;
    constexpr bool air=Air;
    const auto closed=[&](size_t i) {
        const auto& p=view.terrainProperties(i);
        if (air) return !p.flyable;
        if constexpr (WaterOnly) return !p.swimmable;
        else return !p.walkable && !(canSwim && p.swimmable);
    };
    if (b.route==BuildingRoute::Footprint)
    {
        run([&](size_t begin, size_t end) {
            for (size_t i=begin; i<end; ++i)
            {
                const auto& cell=view.occupancy[i];
                if ((!air && cell.building!=NOGBID))
                    out[i]=cell.building==b.gid ? GRADIENT_AT_GOAL : GRADIENT_FORBIDDEN;
                else if ((!air && (view.areas[i].forbidden&b.teamMask)) || (air ? MapState::resourceBlocksAir(view,i) : MapState::resourceBlocksGround(view,i))
                    || (!air && cell.immobileUnit!=IMMOBILE_UNIT_NONE) || closed(i))
                    out[i]=GRADIENT_FORBIDDEN;
                else
                    out[i]=GRADIENT_UNREACHABLE;
            }
        });
        if (air || !b.occupiesGround)
            for (int y=0; y<b.height; ++y)
                for (int x=0; x<b.width; ++x)
                    out[view.index(b.posX+x,b.posY+y)]=GRADIENT_AT_GOAL;
        bool reachable=false;
        if (b.width==b.height)
            reachable=spiralFindNonZero(out, view.normalizeX(b.posX-1), view.normalizeY(b.posY-1), b.width+1,
                int(view.wMask), int(view.hMask), int(view.wDec));
        else
        {
            for (int x=-1; x<=b.width && !reachable; ++x)
                reachable=out[view.index(b.posX+x,b.posY-1)]>GRADIENT_FORBIDDEN || out[view.index(b.posX+x,b.posY+b.height)]>GRADIENT_FORBIDDEN;
            for (int y=0; y<b.height && !reachable; ++y)
                reachable=out[view.index(b.posX-1,b.posY+y)]>GRADIENT_FORBIDDEN || out[view.index(b.posX+b.width,b.posY+y)]>GRADIENT_FORBIDDEN;
        }
        result.locked=!reachable;
        return result;
    }
    const bool clearing=b.route==BuildingRoute::Clearing, war=b.route==BuildingRoute::Combat;
    std::fill(out, out+size_t(view.width)*view.height, GRADIENT_UNREACHABLE);
    const int r=b.unitStayRange, r2=r*r;
    bool anyResourceToClear=false;
    for (int yi=-r; yi<=r; yi++)
        for (int xi=-r; xi<=r; xi++)
            if (yi*yi+xi*xi<=r2)
            {
                const size_t addr=view.index(b.posX+xi, b.posY+yi);
                if (clearing)
                {
                    const auto type=view.resources[addr].resource.type;
                    if (type==NO_RES_TYPE) continue;
                    const auto& p=view.resourceProperties(type);
                    if (!p.clearable) continue;
                    bool selected=false;
                    for (unsigned m=0; m<MaterialCount; ++m)
                        if (b.clearingMaterials[m] && (p.materialMask&(1u<<m))) selected=true;
                    if (!selected) continue;
                    anyResourceToClear=true;
                }
                if (out[addr]==GRADIENT_UNREACHABLE) out[addr]=GRADIENT_AT_GOAL;
            }
    if (clearing) result.resourceState=anyResourceToClear ? 1 : 2;
    run([&](size_t begin, size_t end) {
        for (size_t i=begin; i<end; ++i)
        {
            const auto& cell=view.occupancy[i];
            if ((air || cell.building==NOGBID))
            {
                // Clearing goals stand on their resource, possibly in water.
                const bool clearingGoal=clearing && out[i]==GRADIENT_AT_GOAL;
                if ((!air && (view.areas[i].forbidden&b.teamMask))
                    || ((air ? MapState::resourceBlocksAir(view,i) : MapState::resourceBlocksGround(view,i)) && !clearingGoal)
                    || (!air && cell.immobileUnit!=IMMOBILE_UNIT_NONE)
                    || (closed(i) && !clearingGoal))
                    out[i]=GRADIENT_FORBIDDEN;
            }
            else if (!war || ((1u<<Building::GIDtoTeam(cell.building))&b.allies))
                out[i]=GRADIENT_FORBIDDEN;
            else if (out[i]!=GRADIENT_AT_GOAL)
                out[i]=GRADIENT_UNREACHABLE;
        }
    });
    return result;
}

template<class Run>
BuildingSeedResult buildingCells(const MapState::View& view, const BuildingSeed& b, Uint16* out, Run run)
{
    // Select the movement policy once per field, outside every cell loop.
    // The ordinary walking/swimming path retains its original predicate.
    if (b.swim==AIR_MOVEMENT_CLASS)
        return buildingCellsForMovement<false,true>(view,b,out,run);
    if (b.swim==WATER_ONLY_CLASS)
        return buildingCellsForMovement<true>(view,b,out,run);
    return buildingCellsForMovement<false>(view,b,out,run);
}
}
