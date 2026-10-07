// SPDX-License-Identifier: GPL-3.0-or-later
#include "Building.h"
#include "BuildingType.h"
#include "Game.h"
#include "Team.h"
#include "Unit.h"
#include <algorithm>
#include <bit>
#include <stdexcept>

Sint32 Building::availableMaterial(int resource) const
{
	const auto reserved = type->useTeamMaterials ? owner->reservedTeamMaterials[resource] : reservedMaterials[resource];
	return std::max(0, materials[resource] - reserved);
}

bool Building::restoreMaterialsReservation(const BuildingMaterialCost& cost)
{
	for (int r=0; r<MaterialSlotCount; ++r)
		if (cost[r] > availableMaterial(r)) return false;
	for (int r=0; r<MaterialSlotCount; ++r)
	{
		reservedMaterials[r] += cost[r];
		if (type->useTeamMaterials)
		{
			owner->reservedTeamMaterials[r] += cost[r];
		}
	}
	return true;
}

bool Building::reserveMaterials(const BuildingMaterialCost& cost)
{
	if (!restoreMaterialsReservation(cost)) return false;
	if (type->useTeamMaterials || type->runtimeSuppliesStock || type->runtimeSuppliesDirectStock)
		for (int r=0; r<MaterialSlotCount; ++r)
			if (cost[r] && availableMaterial(r)==0) owner->map->dirtyMarketGradientsSlot(owner->teamNumber, r);
	return true;
}

void Building::releaseMaterials(const BuildingMaterialCost& cost)
{
	for (int r=0; r<MaterialSlotCount; ++r)
	{
		const int before=availableMaterial(r);
		assert(reservedMaterials[r] >= cost[r]);
		reservedMaterials[r] -= cost[r];
		if (type->useTeamMaterials)
		{
			assert(owner->reservedTeamMaterials[r] >= cost[r]);
			owner->reservedTeamMaterials[r] -= cost[r];
		}
		if ((type->useTeamMaterials || type->runtimeSuppliesStock || type->runtimeSuppliesDirectStock) && cost[r] && before==0)
			owner->map->dirtyMarketGradientsSlot(owner->teamNumber,r);
	}
}

void Building::consumeReservedMaterials(const BuildingMaterialCost& cost, int diagnosticUse)
{
	// Settling simultaneously removes stock and its reservation. Availability
	// does not change, so no resource-gradient invalidation is needed.
	for (int r=0; r<MaterialSlotCount; ++r)
	{
		assert(materials[r] >= cost[r] && reservedMaterials[r] >= cost[r]);
		materials[r] -= cost[r];
		reservedMaterials[r] -= cost[r];
		if (type->useTeamMaterials)
		{
			assert(owner->reservedTeamMaterials[r] >= cost[r]);
			owner->reservedTeamMaterials[r] -= cost[r];
		}
		if (diagnosticUse >= 0) owner->stats.measurements.consumed[diagnosticUse][r] += cost[r];
	}
}

BuildingMaterialCost Building::serviceCost(const Unit* unit, int purpose) const
{
	if (purpose == FEED) return type->semantics.feeding.cost;
	if (purpose == HEAL) return type->semantics.healing.cost;
	if (purpose < 0 || purpose >= NB_ABILITY) return {};
	if (!type->semantics.trainingCostMask) return {};
	if (!type->semantics.trainingInParallel || !unit) return type->semantics.training[purpose].cost;
	BuildingMaterialCost result{};
	for (int a=WALK; a<NB_ABILITY; ++a)
	{
		const auto& training = type->semantics.training[a];
		if (!unit->needsTraining(training, a)) continue;
		for (int r=0; r<MaterialSlotCount; ++r) result[r] += training.cost[r];
	}
	return result;
}

bool Building::canOfferService(const Unit* unit, int purpose) const
{
	if (buildingState != ALIVE || static_cast<int>(unitsInside.size()) >= maxUnitInside) return false;
	const auto& spec = type->semantics;
	unsigned allowed = spec.admittedUnitMask;
	bool holdAdmission = false;
	if (purpose == FEED)
	{
		if (!spec.feeding.enabled) return false;
		allowed &= spec.feeding.unitMask;
		holdAdmission = spec.feeding.holdAdmissionUntilExit;
	}
	else if (purpose == HEAL)
	{
		if (!spec.healing.enabled) return false;
		allowed &= spec.healing.unitMask;
		holdAdmission = spec.healing.holdAdmissionUntilExit;
	}
	else
	{
		if (purpose < 0 || purpose >= NB_ABILITY || !spec.training[purpose].enabled
			|| owner->game->gameHeader.isUnitUpgradesDisabled()) return false;
		allowed &= spec.training[purpose].unitMask;
		if (unit && !unit->needsTraining(spec.training[purpose], purpose)) return false;
	}
	if (!allowed || (unit && !(allowed & (1u << unit->typeNum)))) return false;
	BuildingMaterialCost bundleCost{};
	const BuildingMaterialCost* cost;
	unsigned mask;
	if (purpose == FEED) { cost = &spec.feeding.cost; mask = spec.feeding.costMask; }
	else if (purpose == HEAL) { cost = &spec.healing.cost; mask = spec.healing.costMask; }
	else if (spec.trainingInParallel && spec.trainingCostMask && unit)
	{
		bundleCost = serviceCost(unit, purpose);
		cost = &bundleCost;
		mask = 0;
		for (int r=0; r<MaterialSlotCount; ++r) if (bundleCost[r]) mask |= 1u << r;
	}
	else { cost = &spec.training[purpose].cost; mask = spec.training[purpose].costMask; }
	for (; mask; mask &= mask - 1)
	{
		const int r = std::countr_zero(mask);
		if (availableMaterial(r) < (*cost)[r]) return false;
		if (holdAdmission)
		{
			Sint64 admissionBuffer = 0;
			for (const Unit* occupant : unitsInside)
			{
				if (occupant->serviceResourcesReserved) continue;
				if (occupant->destinationPurpose == FEED && spec.feeding.holdAdmissionUntilExit)
					admissionBuffer += spec.feeding.cost[r];
				else if (occupant->destinationPurpose == HEAL && spec.healing.holdAdmissionUntilExit)
					admissionBuffer += spec.healing.cost[r];
			}
			if (availableMaterial(r) < Sint64((*cost)[r]) + admissionBuffer) return false;
		}
	}
	return true;
}

void Building::reserveService(Unit* unit)
{
	assert(!unit->serviceResourcesReserved);
	if (!reserveMaterials(serviceCost(unit, unit->destinationPurpose)))
		throw std::runtime_error("Service admission overbooked building materials");
	unit->serviceResourcesReserved = true;
}

void Building::releaseService(Unit* unit)
{
	if (!unit->serviceResourcesReserved) return;
	releaseMaterials(serviceCost(unit, unit->destinationPurpose));
	unit->serviceResourcesReserved = false;
}

void Building::settleService(Unit* unit)
{
	// Completion, expulsion and destruction may all visit settlement. Only the
	// outstanding commitment can debit inventory; an already settled visit cannot.
	if (!unit->serviceResourcesReserved) return;
	consumeReservedMaterials(serviceCost(unit, unit->destinationPurpose),
		unit->destinationPurpose == FEED ? GameplayMeasurements::MEAL :
		unit->destinationPurpose == HEAL ? GameplayMeasurements::HEALING_COST : GameplayMeasurements::TRAINING_COST);
	unit->serviceResourcesReserved = false;
}

void Building::restoreServiceReservations()
{
	for (Unit* unit : unitsInside)
	{
		if (!unit->serviceResourcesReserved) continue;
		const auto cost = serviceCost(unit, unit->destinationPurpose);
		if (!restoreMaterialsReservation(cost)) throw std::runtime_error("Saved building services exceed available materials");
	}
}
