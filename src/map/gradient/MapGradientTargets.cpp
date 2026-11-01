// SPDX-License-Identifier: GPL-3.0-or-later
#include "Map.h"
#include "GradientFieldIdentity.h"
#include "GradientTargetCell.h"
#include "GradientRuntime.h"
#include "Building.h"
#include "Game.h"
#include "Team.h"
#include "MapInternal.h"
#include "field/GradientPreparationRules.h"
#include "field/GradientWorkspace.h"

// Validate a destination against the same unweighted membership predicates as
// seed preparation. A positive weighted market/guard seed is a destination too;
// comparing its value with GRADIENT_AT_GOAL would incorrectly reject arrival.
// This never requests a field, updates policy state, or consumes selector RNG.
bool Map::gradientGoalValid(GradientFieldKey key,int x,int y) const
{
    if (!game || !key.valid(game->teamsCount(),MOVEMENT_CLASS_COUNT,MaterialCount)) return false;
    const auto index=coordToIndex(x,y);
    const auto view=cellView();
    const auto& area=view.areas[index];
    const auto& occupancy=view.occupancy[index];
    const auto& resource=view.resources[index].resource;
    const auto* properties=resource.type==NO_RES_TYPE ? nullptr : &view.resourceProperties(resource.type);
    const bool air=key.swim==AIR_MOVEMENT_CLASS;
    const Uint32 teamMask=Uint32(1)<<key.team;
    const bool forbidden=!air && (area.forbidden&teamMask);
    const bool mobile=air || occupancy.immobileUnit==IMMOBILE_UNIT_NONE;
    const bool resourceBlocked=properties && (air ? properties->blocksAir : properties->blocksGround);
    switch (key.family) {
    case GradientFieldFamily::Materials:
    case GradientFieldFamily::Markets: {
        if (forbidden || !mobile) return false;
        const bool natural=(materialMaskAt(index)&(1u<<key.material))
            && (!properties || !properties->visibleToHarvest || (fogOfWar[index]&teamMask));
        // Natural sources precede resource/building/terrain obstacles in both
        // seed builders. Do not impose movement-blocker precedence on goals.
        if (natural) return true;
        if (key.family==GradientFieldFamily::Materials) return false;
        // Serving shared market fields initialize this exact supplier ledger at
        // capture/load. Validate weighted arrivals with one sparse point lookup;
        // only uncaptured diagnostic calls need the legacy provider-list fallback.
        const auto& providers=gradientRuntime->marketTargets;
        if(providers.initialized(key.team,key.material)) {
            const auto cell=providers.inverse.find(index);
            if(cell==providers.inverse.end())return false;
            const auto team=cell->second.find(key.team);
            if(team==cell->second.end())return false;
            const auto bit=Uint32(1)<<key.material;
            return (team->second.overlay&bit) || (!resourceBlocked && occupancy.building!=NOGBID
                && Building::GIDtoTeam(occupancy.building)==key.team && (team->second.occupying&bit));
        }
        const auto* team=game->teams[key.team];
        if (!resourceBlocked && occupancy.building!=NOGBID
            && Building::GIDtoTeam(occupancy.building)==key.team) {
            const auto* supplier=team->myBuildings[Building::GIDtoID(occupancy.building)];
            if (stockSupplierEligibleSlot(supplier,nullptr,key.material,1)) return true;
        }
        // Overlay suppliers have no occupancy tile. Reuse the existing supplier
        // membership list only for an arrival validation; never scan map cells.
        if (game->buildingsTypes.usesOverlaySuppliers())
            for (const Building* supplier:team->stockSuppliers) {
                if (supplier->runtime->has(BuildingRuntimeTraits::OccupiesGround)
                    || !stockSupplierEligibleSlot(supplier,nullptr,key.material,1)) continue;
                if (((x-supplier->posX)&wMask)<supplier->type->width
                    && ((y-supplier->posY)&hMask)<supplier->type->height) return true;
            }
        return false;
    }
    case GradientFieldFamily::Guard:
		return !forbidden && mobile && !resourceBlocked && (area.guard & teamMask) &&
			   (air || occupancy.building == NOGBID ||
				!(game->teams[key.team]->allies &
				  (Uint32(1) << Building::GIDtoTeam(occupancy.building)))) &&
			   cellRule(index).allowsMovement(key.swim);
	case GradientFieldFamily::Clear: {
        const auto flags=properties ? view.resourceRegistry->gradientPreparation()[resource.type] : 0;
        return gradient_preparation::rules::clearSeed(forbidden,mobile,resourceBlocked,
            !air && occupancy.building!=NOGBID,GRADIENT_UNREACHABLE,
            flags&(1u<<18),flags&(1u<<19),area.clear&teamMask,area.farm&teamMask,
            farmAreasEnabled())==GRADIENT_AT_GOAL;
    }
    case GradientFieldFamily::Forbidden:
        // Outside free tiles are the actual goal set; the existing border pass
        // derives interior propagation seeds from it. No extra neighbor scan.
		return !forbidden && mobile && !resourceBlocked && (air || occupancy.building == NOGBID) &&
			   cellRule(index).allowsMovement(key.swim);
	}
    return false;
}

GradientTargetCell Map::captureGradientTargetCell(size_t index) const
{
    GradientTargetCell cell;
    const auto view=cellView();
    const auto& area=view.areas[index];
    const auto& occupancy=view.occupancy[index];
    const auto& resource=view.resources[index].resource;
    const auto* properties=resource.type==NO_RES_TYPE ? nullptr : &view.resourceProperties(resource.type);
    cell.materials=materialMaskAt(index);
    cell.visibleTeams=properties && properties->visibleToHarvest ? (fogOfWar ? fogOfWar[index] : 0) : UINT32_MAX;
    cell.forbidden=area.forbidden;cell.guard=area.guard;cell.clear=area.clear;cell.farm=area.farm;
    cell.immobile=occupancy.immobileUnit!=IMMOBILE_UNIT_NONE;
    cell.groundBlocked=properties && properties->blocksGround;
    cell.airBlocked=properties && properties->blocksAir;
    if(occupancy.building!=NOGBID) cell.buildingTeam=Building::GIDtoTeam(occupancy.building);
    if(game && cell.buildingTeam!=255)
        for(int team=0;team<game->teamsCount();++team)
            if(game->teams[team]->allies&(Uint32(1)<<cell.buildingTeam))
                cell.guardBuildingBlockedTeams|=Uint32(1)<<team;
    const auto flags=properties ? view.resourceRegistry->gradientPreparation()[resource.type] : 0;
    cell.clearable=flags&(1u<<18);cell.farmable=flags&(1u<<19);cell.farming=farmAreasEnabled();
    const auto& terrain=terrainPropertiesAt(index);
    for(unsigned movement=0;movement<MOVEMENT_CLASS_COUNT;++movement)
		if (cellRule(index).allowsMovement(movement))
			cell.openClasses |= std::uint16_t(1) << movement;
	cell.terrainCosts={std::uint32_t(terrain.groundSpeedQ8),std::uint32_t(terrain.airSpeedQ8),
        std::uint32_t(terrain.groundHealthQ8),std::uint32_t(terrain.airHealthQ8)};
    gradientMarketSupplierMasks(index,cell);
    return cell;
}

void Map::gradientTargetCellChanged(size_t index,const GradientTargetCell& before)
{
    if(!game) return;
    const auto after=captureGradientTargetCell(index);
    auto& state=gradientRuntime->adaptive;
    if(!before.sameRoutes(after)) state.routesChanged();
    const bool naturalChanged=before.materials!=after.materials || before.visibleTeams!=after.visibleTeams
        || before.forbidden!=after.forbidden || before.immobile!=after.immobile;
    const bool marketsChanged=naturalChanged || before.groundBlocked!=after.groundBlocked
        || before.airBlocked!=after.airBlocked || before.buildingTeam!=after.buildingTeam
        || before.occupyingSupplies!=after.occupyingSupplies || before.overlaySupplies!=after.overlaySupplies;
    const bool clearChanged=before.forbidden!=after.forbidden || before.clear!=after.clear
        || before.farm!=after.farm || before.clearable!=after.clearable
        || before.farmable!=after.farmable || before.farming!=after.farming;
    const bool guardChanged=before.forbidden!=after.forbidden || before.guard!=after.guard
        || before.immobile!=after.immobile || before.openClasses!=after.openClasses
        || before.groundBlocked!=after.groundBlocked || before.airBlocked!=after.airBlocked
        || before.guardBuildingBlockedTeams!=after.guardBuildingBlockedTeams;
    const bool escapeChanged=before.forbidden!=after.forbidden || before.immobile!=after.immobile
        || before.openClasses!=after.openClasses || before.groundBlocked!=after.groundBlocked
        || before.airBlocked!=after.airBlocked || (before.buildingTeam==255)!=(after.buildingTeam==255);
    const auto note=[&](GradientFieldKey key) {
        const bool was=before.goal(key),now=after.goal(key);
        if(was==now) return;
        // A summary starts when its first field enrolls. It is shared across
        // equivalent movement classes; no mutation visits the policy fields.
        const auto scope=GradientTargetKey::of(key);
        if(state.targets.find(scope)!=state.targets.end()) state.targetChanged(scope,now);
    };
    for(unsigned team=0;team<unsigned(game->teamsCount());++team) {
        const auto t=std::uint8_t(team);
        bool guardMembershipChanged=false;
        for(const auto movement:{std::uint8_t(0),std::uint8_t(AIR_MOVEMENT_CLASS)}) {
            // These families' goal precedence is independent of ground terrain
            // ratios. Shared target scopes collapse all ground classes to zero.
            auto materials=before.materials|after.materials;
            for(auto bits=naturalChanged?materials:0;bits;bits&=bits-1) {
                const auto material=std::uint8_t(std::countr_zero(bits));
                note({GradientFieldFamily::Materials,t,movement,material});
            }
            materials|=before.occupyingSupplies[team]|after.occupyingSupplies[team]
                |before.overlaySupplies[team]|after.overlaySupplies[team];
            for(auto bits=marketsChanged?materials:0;bits;bits&=bits-1)
                note({GradientFieldFamily::Markets,t,movement,std::uint8_t(std::countr_zero(bits))});
            if(clearChanged)note({GradientFieldFamily::Clear,t,movement,GradientFieldKey::NoMaterial});
        }
        for(unsigned movement=0;movement<MOVEMENT_CLASS_COUNT;++movement) {
            const GradientFieldKey guard{GradientFieldFamily::Guard,t,std::uint8_t(movement),GradientFieldKey::NoMaterial};
            if(guardChanged) {
                guardMembershipChanged|=before.goal(guard)!=after.goal(guard);
                note(guard);
            }
            if(escapeChanged)note({GradientFieldFamily::Forbidden,t,std::uint8_t(movement),GradientFieldKey::NoMaterial});
        }
        // Changing the eligible paint also changes crowding normalization for
        // nearby destinations. This epoch counts relevant input events, not
        // the number of seed values whose weighted cost actually changed.
        if(guardMembershipChanged) gradientGuardCostsChanged(team);
    }
}

void Map::gradientGuardCostsChanged(int team)
{
    if(team>=0 && team<TEAM_MAX_COUNT) gradientRuntime->adaptive.guardCostsChanged(team);
}

void Map::gradientAlliancesChanged(unsigned team,Uint32 previousAllies)
{
    if(!game || team>=unsigned(game->teamsCount()) || gradientRuntime->adaptive.fields.empty()) return;
    const auto allies=game->teams[team]->allies;
    // Only occupied footprints of teams whose alliance bit changed can alter
    // guard eligibility. This rare order visits existing building slots, never
    // map cells outside those footprints or the enrolled field collection.
    for(auto changed=previousAllies^allies;changed;changed&=changed-1) {
        const auto owner=std::countr_zero(changed);
        if(owner>=unsigned(game->teamsCount())) continue;
        for(unsigned id=0;id<Building::MAX_COUNT;++id) {
            const Building* building=game->teams[owner]->myBuildings[id];
            if(!building) continue;
            for(int y=0;y<building->type->height;++y)
                for(int x=0;x<building->type->width;++x) {
                    const auto index=coordToIndex(building->posX+x,building->posY+y);
                    if(getBuilding(building->posX+x,building->posY+y)!=building->gid) continue;
                    auto before=captureGradientTargetCell(index);
                    if(previousAllies&(Uint32(1)<<owner)) before.guardBuildingBlockedTeams|=Uint32(1)<<team;
                    else before.guardBuildingBlockedTeams&=~(Uint32(1)<<team);
                    gradientTargetCellChanged(index,before);
                }
        }
    }
}
