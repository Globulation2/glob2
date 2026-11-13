// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "BuildingRoute.h"
#include "MapStateView.h"
#include "SeedTerrain.h"
#include "SeedPoint.h"
#include "MapInternal.h"
#include "BuildingUtils.h"
#include "field/GradientPreparationRules.h"
#include "BuildingSeedPlanes.h"
#include "BuildingSeedAudit.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <stdexcept>
#include <string>
#include <vector>
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
    const unsigned base=unsigned(team)*BuildingUtils::MAX_COUNT;
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
                            if (cell.building==NOGBID || (Air && unsigned(cell.building)-base>=BuildingUtils::MAX_COUNT)) value=terrainAt(i);
                            else {
                                const unsigned id=unsigned(cell.building)-base;
                                if(id<BuildingUtils::MAX_COUNT)value=suppliers[id];
                                if(Air&&value==GRADIENT_FORBIDDEN)value=terrainAt(i);
                            }
                        }
                        if(p&&(p->materialMask&requested)&&(!Air || !(view.areas[i].forbidden&mask))) {
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
                && ((Air || cell.building==NOGBID) || !(allies&(Uint32(1)<<BuildingUtils::GIDtoTeam(cell.building))))) {
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
// Per-cell rules shared by the full scan and the template path, so the two
// cannot drift apart. The scan mirrors Map::updateGlobalGradient's seeding.
inline MapState::SeedPassability seedPassability(int swim)
{
    using MapState::SeedPassability;
    if (swim==AIR_MOVEMENT_CLASS) return SeedPassability::Air;
    if (swim==WATER_ONLY_CLASS) return SeedPassability::WaterOnly;
    return swim>0 ? SeedPassability::WalkSwim : SeedPassability::Walk;
}
template<bool WaterOnly, bool Air>
bool buildingSeedClosed(const MapState::View& view, size_t i, bool canSwim)
{
    const auto& p=view.terrainProperties(i);
    if (Air) return !p.flyable;
    if constexpr (WaterOnly) return !p.swimmable;
    else return !p.walkable && !(canSwim && p.swimmable);
}
template<bool Air>
bool buildingSeedResourceBlocks(const MapState::View& view, size_t i)
{
    return Air ? MapState::resourceBlocksAir(view,i) : MapState::resourceBlocksGround(view,i);
}
//! The team- and building-independent template value of one cell.
template<bool WaterOnly, bool Air>
Uint16 buildingTemplateCell(const MapState::View& view, size_t i, bool canSwim)
{
    const auto& cell=view.occupancy[i];
    if (!Air && cell.building!=NOGBID) return GRADIENT_FORBIDDEN;
    if (buildingSeedResourceBlocks<Air>(view,i) || (!Air && cell.immobileUnit!=IMMOBILE_UNIT_NONE)
        || buildingSeedClosed<WaterOnly,Air>(view,i,canSwim))
        return GRADIENT_FORBIDDEN;
    return GRADIENT_UNREACHABLE;
}
inline Uint16 buildingTemplateCell(const MapState::View& view, size_t i, MapState::SeedPassability kind)
{
    using MapState::SeedPassability;
    switch (kind) {
    case SeedPassability::Walk: return buildingTemplateCell<false,false>(view,i,false);
    case SeedPassability::WalkSwim: return buildingTemplateCell<false,false>(view,i,true);
    case SeedPassability::WaterOnly: return buildingTemplateCell<true,false>(view,i,true);
    case SeedPassability::Air: break;
    }
    return buildingTemplateCell<false,true>(view,i,true);
}
//! Footprint route: the full seed of one cell.
template<bool WaterOnly, bool Air>
Uint16 footprintSeedCell(const MapState::View& view, const BuildingSeed& b, size_t i, bool canSwim)
{
    const auto& cell=view.occupancy[i];
    if (!Air && cell.building!=NOGBID)
        return cell.building==b.gid ? GRADIENT_AT_GOAL : GRADIENT_FORBIDDEN;
    if ((!Air && (view.areas[i].forbidden&b.teamMask)) || buildingSeedResourceBlocks<Air>(view,i)
        || (!Air && cell.immobileUnit!=IMMOBILE_UNIT_NONE) || buildingSeedClosed<WaterOnly,Air>(view,i,canSwim))
        return GRADIENT_FORBIDDEN;
    return GRADIENT_UNREACHABLE;
}
//! Clearing and Combat routes: the final seed of one cell whose initial value
//! is GRADIENT_AT_GOAL inside the goal disk and GRADIENT_UNREACHABLE elsewhere.
template<bool WaterOnly, bool Air>
Uint16 routeSeedCell(const MapState::View& view, const BuildingSeed& b, size_t i, bool canSwim, Uint16 value)
{
    const bool clearing=b.route==BuildingRoute::Clearing, war=b.route==BuildingRoute::Combat;
    const auto& cell=view.occupancy[i];
    if (Air || cell.building==NOGBID)
    {
        // Clearing goals stand on their resource, possibly in water.
        const bool clearingGoal=clearing && value==GRADIENT_AT_GOAL;
        if ((!Air && (view.areas[i].forbidden&b.teamMask))
            || (buildingSeedResourceBlocks<Air>(view,i) && !clearingGoal)
            || (!Air && cell.immobileUnit!=IMMOBILE_UNIT_NONE)
            || (buildingSeedClosed<WaterOnly,Air>(view,i,canSwim) && !clearingGoal))
            return GRADIENT_FORBIDDEN;
        return value;
    }
    if (!war || ((1u<<BuildingUtils::GIDtoTeam(cell.building))&b.allies))
        return GRADIENT_FORBIDDEN;
    return value==GRADIENT_AT_GOAL ? value : GRADIENT_UNREACHABLE;
}
//! Visits every goal cell of a Clearing or Combat route's disk (cells may
//! repeat on maps smaller than the disk). Returns the clearing resource state.
template<class Visit>
Uint8 forEachRouteGoal(const MapState::View& view, const BuildingSeed& b, Visit visit)
{
    const bool clearing=b.route==BuildingRoute::Clearing;
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
                visit(addr);
            }
    return clearing ? (anyResourceToClear ? 1 : 2) : 0;
}
inline bool footprintReachable(const MapState::View& view, const BuildingSeed& b, const Uint16* out)
{
    if (b.width==b.height)
        return spiralFindNonZero(out, view.normalizeX(b.posX-1), view.normalizeY(b.posY-1), b.width+1,
            int(view.wMask), int(view.hMask), int(view.wDec));
    for (int x=-1; x<=b.width; ++x)
        if (out[view.index(b.posX+x,b.posY-1)]>GRADIENT_FORBIDDEN || out[view.index(b.posX+x,b.posY+b.height)]>GRADIENT_FORBIDDEN)
            return true;
    for (int y=0; y<b.height; ++y)
        if (out[view.index(b.posX-1,b.posY+y)]>GRADIENT_FORBIDDEN || out[view.index(b.posX+b.width,b.posY+y)]>GRADIENT_FORBIDDEN)
            return true;
    return false;
}
template<class Visit>
void forEachSeedBit(const MapState::BuildingSeedPlanes::Bits& bits, Visit visit)
{
    for (size_t word=0; word<bits.size(); ++word)
        for (auto remaining=bits[word]; remaining; remaining&=remaining-1)
            visit(word*64+size_t(std::countr_zero(remaining)));
}

// The full scan: reads every cell of the view.
template<bool WaterOnly, bool Air=false, class Run>
BuildingSeedResult buildingCellsForMovement(const MapState::View& view, const BuildingSeed& b, Uint16* out, Run run)
{
    BuildingSeedResult result;
    const bool canSwim=b.swim>0;
    if (b.route==BuildingRoute::Footprint)
    {
        run([&](size_t begin, size_t end) {
            for (size_t i=begin; i<end; ++i)
                out[i]=footprintSeedCell<WaterOnly,Air>(view,b,i,canSwim);
        });
        if (Air || !b.occupiesGround)
            for (int y=0; y<b.height; ++y)
                for (int x=0; x<b.width; ++x)
                    out[view.index(b.posX+x,b.posY+y)]=GRADIENT_AT_GOAL;
        result.locked=!footprintReachable(view,b,out);
        return result;
    }
    std::fill(out, out+size_t(view.width)*view.height, GRADIENT_UNREACHABLE);
    result.resourceState=forEachRouteGoal(view,b,[&](size_t addr) { out[addr]=GRADIENT_AT_GOAL; });
    run([&](size_t begin, size_t end) {
        for (size_t i=begin; i<end; ++i)
            out[i]=routeSeedCell<WaterOnly,Air>(view,b,i,canSwim,out[i]);
    });
    return result;
}

// The template path: copy the passability plane, then overlay what depends
// on the team and the building. Overlay order keeps the scan's precedence of
// building cells over forbidden paint. The footprint overlay visits only the
// building's type rectangle at its position, where Map::setBuilding stamps
// its gid; the scan marks every cell holding the gid. The two agree while
// occupancy matches the rectangle, an invariant the save integrity check
// enforces and GLOB2_BUILDING_SEED_AUDIT verifies in tests.
template<bool WaterOnly, bool Air=false, class Run>
BuildingSeedResult buildingCellsFromTemplates(const MapState::View& view, const BuildingSeed& b, Uint16* out, Run run)
{
    BuildingSeedResult result;
    const auto& t=*view.buildingSeeds;
    const bool canSwim=b.swim>0;
    const Uint16* plane=t.plane(seedPassability(b.swim));
    run([&](size_t begin, size_t end) { std::copy(plane+begin, plane+end, out+begin); });
    if (!Air)
        for (unsigned mask=b.teamMask; mask; mask&=mask-1)
        {
            const auto team=size_t(std::countr_zero(mask));
            if (team<t.forbidden.size())
                forEachSeedBit(t.forbidden[team],[&](size_t i) { out[i]=GRADIENT_FORBIDDEN; });
        }
    if (b.route==BuildingRoute::Footprint)
    {
        for (int y=0; y<b.height; ++y)
            for (int x=0; x<b.width; ++x)
            {
                const size_t i=view.index(b.posX+x,b.posY+y);
                if (Air || !b.occupiesGround || view.occupancy[i].building==b.gid) out[i]=GRADIENT_AT_GOAL;
            }
        result.locked=!footprintReachable(view,b,out);
        return result;
    }
    if (!Air && b.route==BuildingRoute::Combat)
        for (size_t team=0; team<t.buildings.size(); ++team)
            if (!((1u<<team)&b.allies))
                forEachSeedBit(t.buildings[team],[&](size_t i) { out[i]=GRADIENT_UNREACHABLE; });
    result.resourceState=forEachRouteGoal(view,b,[&](size_t addr) {
        out[addr]=routeSeedCell<WaterOnly,Air>(view,b,addr,canSwim,GRADIENT_AT_GOAL);
    });
    return result;
}

template<bool WaterOnly, bool Air=false, class Run>
BuildingSeedResult buildingCellsSelected(const MapState::View& view, const BuildingSeed& b, Uint16* out, Run run)
{
    const size_t cells=size_t(view.width)*view.height;
    if (!view.buildingSeeds || view.buildingSeeds->cells!=cells)
        return buildingCellsForMovement<WaterOnly,Air>(view,b,out,run);
    const auto result=buildingCellsFromTemplates<WaterOnly,Air>(view,b,out,run);
    if (building_seed::auditEnabled())
    {
        std::vector<Uint16> expected(cells);
        const auto scanned=buildingCellsForMovement<WaterOnly,Air>(view,b,expected.data(),[cells](auto fn) { fn(0,cells); });
        const auto mismatch=std::mismatch(expected.begin(),expected.end(),out);
        if (mismatch.first!=expected.end() || scanned.locked!=result.locked || scanned.resourceState!=result.resourceState)
            throw std::logic_error("Building seed template differs from the full scan at cell "
                + std::to_string(mismatch.first-expected.begin()) + " (route "
                + std::to_string(int(b.route)) + ", swim " + std::to_string(b.swim) + ")");
    }
    return result;
}

template<class Run>
BuildingSeedResult buildingCells(const MapState::View& view, const BuildingSeed& b, Uint16* out, Run run)
{
    // Select the movement policy once per field, outside every cell loop.
    // The ordinary walking/swimming path retains its original predicate.
    if (b.swim==AIR_MOVEMENT_CLASS)
        return buildingCellsSelected<false,true>(view,b,out,run);
    if (b.swim==WATER_ONLY_CLASS)
        return buildingCellsSelected<true>(view,b,out,run);
    return buildingCellsSelected<false>(view,b,out,run);
}
}
