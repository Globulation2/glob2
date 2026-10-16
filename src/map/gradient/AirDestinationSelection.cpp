// SPDX-License-Identifier: GPL-3.0-or-later
#include "AirGradientTypes.h"
#include "field/AirGradient.h"
#include "field/TerrainMovementCosts.h"
#include "sim/snapshot/WorldSnapshot.h"
#include "Building.h"
#include "Unit.h"
#include <limits>

namespace air_guidance {
namespace {
const SimulationSnapshot::BuildingView* findBuilding(const SimulationSnapshot::Handle& snapshot,BuildingRef ref)
{
    if (ref.empty() || ref.gid>=snapshot.entities->buildingSlotIndices.size()) return nullptr;
    const auto slot=snapshot.entities->buildingSlotIndices[ref.gid];
    if (slot==SimulationSnapshot::Entities::NoRecord) return nullptr;
    const auto& building=snapshot.entities->buildings.at(slot);
    return building.identity==ref ? &building : nullptr;
}
bool supplierEligible(const SimulationSnapshot::Handle& snapshot,const Key& key,
    const SimulationSnapshot::BuildingView& supplier)
{
    if (supplier.team!=key.team || supplier.buildingState!=Building::ALIVE || supplier.identity==key.consumer) return false;
    const auto* consumer=findBuilding(snapshot,key.consumer);
    if (consumer && consumer->usesTeamResources && supplier.usesTeamResources) return false;
    const auto& type=snapshot.catalogs->buildings->at(supplier.typeNum).resolvedType;
    const auto supplies=((key.modes&1) ? type.runtimeSuppliesStockMask : 0)
        | ((key.modes&2) ? type.runtimeSuppliesDirectStockMask : 0);
    return (supplies & supplier.availableSupplyMask & (1u<<key.material))!=0;
}
}

void buildSelection(const Key& key,const SimulationSnapshot::Handle& snapshot,Uint16* values,
    Output& output,GradientWorkspace& scratch)
{
    const field::Grid grid(snapshot.width,snapshot.height);
    const auto view=snapshot.view();
    const auto count=grid.cells();
    const Uint32 teamMask=Uint32(1)<<key.team;
    const bool constrained=snapshot.terrain->airConstraints;
    const bool privateChoice=key.family==Family::ClearChoice;
    GradientExecutionAudit::record(GradientExecutionAudit::Kind::AirTraversalPlane);
    std::vector<std::uint8_t> passable(count);
    std::vector<gradient_kernel::EntrySteps> costs(count);
    for (size_t i=0;i<count;++i) {
        const auto& terrain=snapshot.terrainPropertiesAt(i);
        passable[i]=terrain.flyable;
        costs[i]=constrained ? field::air::compileSteps(gradient_kernel::scaledTerrainStep(GRADIENT_STEP,terrain.airSpeedQ8))
            : gradient_kernel::EntrySteps{GRADIENT_STEP,GRADIENT_STEP};
    }
    const field::air::Traversal traversal{grid,passable,costs};
    output={};
    if (!privateChoice) {
        output.entryCosts.resize(count);
        for (size_t i=0;i<count;++i) output.entryCosts[i]=std::uint8_t(costs[i].cardinal);
    }
    GradientExecutionAudit::record(GradientExecutionAudit::Kind::AirGoalEnumeration);
    std::vector<field::air::Goal> seeds;
    // A private choice publishes only its result, never its candidate storage.
    // Shared fields retain at most one goal per cell. Reserve that bound once
    // so vector growth cannot exceed the output-plane reservation budget.
    std::vector<Goal> candidates;
    auto& goals=privateChoice ? candidates : output.goals;
    goals.reserve(count);
    // The original selectors visit y then x, replacing only for a strictly
    // cheaper cost. Goal ranks preserve that order, including supplier cells.
    for (int y=0;y<snapshot.height;++y) for (int x=0;x<snapshot.width;++x) {
        const auto cell=grid.index(x,y);
        const auto area=snapshot.areasAt(cell);
        if (area.forbidden&teamMask) continue;
        bool eligible=false;
        BuildingRef supplier;
        const auto resource=snapshot.resourceAt(cell).resource;
        switch (key.family) {
        case Family::Material:
            eligible=MapState::hasMaterialSlot(view,cell,key.material)
                && (!view.resourceProperties(resource.type).visibleToHarvest || (snapshot.visibilityAt(cell).discovered&teamMask));
            if (!eligible && key.withMarkets) {
                const auto gid=snapshot.occupancyAt(cell).building;
                if (gid!=NOGBID && gid<snapshot.entities->buildingSlotIndices.size()) {
                    const auto slot=snapshot.entities->buildingSlotIndices[gid];
                    if (slot!=SimulationSnapshot::Entities::NoRecord) {
                        const auto& building=snapshot.entities->buildings.at(slot);
                        eligible=supplierEligible(snapshot,key,building);
                        if (eligible) supplier=building.identity;
                    }
                }
            }
            break;
        case Family::Guard:
            eligible=(area.guard&teamMask) && passable[cell];
            break;
        case Family::ClearZone:
            eligible=resource.type!=NO_RES_TYPE && view.resourceProperties(resource.type).clearable
                && (view.resourceProperties(resource.type).materialMask&key.materialMask&AllMaterials)
                && field::air::wrappedSquaredDistance(grid,x,y,key.x,key.y)<=std::uint64_t(key.range)*key.range;
            break;
        case Family::ClearChoice:
            if (resource.type!=NO_RES_TYPE) {
                const auto& properties=view.resourceProperties(resource.type);
                const bool explicitClear=(area.clear&teamMask)!=0;
                eligible=properties.clearable && (explicitClear || (snapshot.areas->farmEnabled && (area.farm&teamMask) && !properties.farmable));
            }
            break;
        case Family::Building: throw std::logic_error("Building air job sent to destination selector");
        }
        if (eligible) {
            if (!privateChoice) seeds.push_back({unsigned(cell),unsigned(goals.size()),0});
            goals.push_back({x,y,supplier});
        }
    }
    if (!privateChoice) {
        output.labels.resize(count);
        field::air::build(traversal,seeds,std::span(values,count),scratch,output.labels,field::air::GoalPolicy::ApproachBlocked);
        return;
    }
    // Claims depend on the alternative's actual cost from this unit. Settle a
    // complete private source field before considering every candidate.
    const field::air::Goal source{unsigned(grid.index(key.sourceX,key.sourceY)),0,0};
    field::air::build(traversal,std::span(&source,1),std::span(values,count),scratch,{},field::air::GoalPolicy::Exact,field::air::Direction::FromSource);
    std::vector<const SimulationSnapshot::UnitView*> claims(count,nullptr);
    for (const auto& unit:snapshot.entities->units) if (unit.team==key.team && unit.hasClearingClaim)
        claims[grid.index(unit.clearingClaimX,unit.clearingClaimY)]=&unit;
    unsigned best=std::numeric_limits<unsigned>::max();
    for (const auto& goal:goals) {
        const auto cell=grid.index(goal.x,goal.y);
        std::optional<unsigned> cost;
        if (passable[cell]) cost=field::air::decode(values[cell]);
        else grid.neighborIndices(cell,field::Surrounding,[&](int neighbor) {
            const auto candidate=field::air::decode(values[neighbor]);
            if (candidate && (!cost || *candidate<*cost)) cost=candidate;
        });
        if (!cost || *cost>=best) continue;
        const auto* claimant=claims[cell];
        if (claimant && claimant->identity!=key.unit
            && std::uint64_t(claimant->clearingClaimDistance)*GRADIENT_STEP<=*cost) continue;
        best=*cost;
        output.choice={true,{goal.x,goal.y,*cost,{}}};
    }
}
}
