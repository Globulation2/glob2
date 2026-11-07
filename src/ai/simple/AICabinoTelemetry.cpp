// SPDX-License-Identifier: GPL-3.0-or-later
// AICabino's AI telemetry capture (schema in src/ai/telemetry/AITelemetryFields.inc).
// It lives with the AI so a compile profile without the AI links without it.
#include "AITelemetryFields.h"
#include "AICabino.h"

void Cabino::AICabino::captureTelemetry()
{
	if (iteration)
		for (const auto *module : modules)
			module->captureTelemetry(telemetry);
	telemetry.set(AITrace::AI8::state_timer, timer);
	telemetry.set(AITrace::AI8::state_iteration, iteration);
	telemetry.set(AITrace::AI8::state_module_timer, module_timer);
	telemetry.set(AITrace::AI8::state_center_x, center_x);
	telemetry.set(AITrace::AI8::state_center_y, center_y);
	telemetry.set(AITrace::AI8::state_orders_count, orders.size());
	telemetry.set(AITrace::AI8::state_modules_count, modules.size());
}

void Cabino::SimpleBuildingDefense::captureTelemetry(const AITelemetry::Sink &sink) const
{
	if (!sink.series || !sink.series->current.values[AITrace::AI8::SimpleBuildingDefense_perform_calls].bits)
		return;
	sink.set(AITrace::AI8::module_SimpleBuildingDefense_defending_zones_count,
			 defending_zones.size());
	sink.set(AITrace::AI8::module_SimpleBuildingDefense_building_health_count,
			 building_health.size());
}

void Cabino::GeneralsDefense::captureTelemetry(const AITelemetry::Sink &sink) const
{
	if (!sink.series || !sink.series->current.values[AITrace::AI8::GeneralsDefense_perform_calls].bits)
		return;
	sink.set(AITrace::AI8::module_GeneralsDefense_defending_flags_count, defending_flags.size());
}

void Cabino::PrioritizedBuildingAttack::captureTelemetry(const AITelemetry::Sink &sink) const
{
	if (!sink.series || !sink.series->current.values[AITrace::AI8::PrioritizedBuildingAttack_perform_calls].bits)
		return;
	sink.set(AITrace::AI8::module_PrioritizedBuildingAttack_attacks_count, attacks.size());
	sink.set(AITrace::AI8::module_PrioritizedBuildingAttack_target_team, enemyTeamNumber == 255 ? -1 : int(enemyTeamNumber));
}

void Cabino::DistributedNewConstructionManager::captureTelemetry(
	const AITelemetry::Sink &sink) const
{
	sink.set(AITrace::AI8::module_DistributedNewConstructionManager_new_buildings_count,
			 new_buildings.size());
	sink.set(AITrace::AI8::module_DistributedNewConstructionManager_num_buildings_wanted_count,
			 num_buildings_wanted.size());
	sink.set(AITrace::AI8::module_DistributedNewConstructionManager_no_build_cache_count,
			 no_build_cache.size());
	sink.set(AITrace::AI8::module_DistributedNewConstructionManager_imap_count, imap.size());
}

void Cabino::RandomUpgradeRepairModule::captureTelemetry(const AITelemetry::Sink &sink) const
{
	if (!sink.series || !sink.series->current.values[AITrace::AI8::RandomUpgradeRepairModule_perform_calls].bits)
		return;
	sink.set(AITrace::AI8::module_RandomUpgradeRepairModule_active_construction_count,
			 active_construction.size());
	sink.set(AITrace::AI8::module_RandomUpgradeRepairModule_pending_construction_count,
			 pending_construction.size());
}

void Cabino::DistributedUnitManager::captureTelemetry(const AITelemetry::Sink &sink) const
{
	if (!sink.series || !sink.series->current.values[AITrace::AI8::BasicDistributedSwarmManager_perform_calls].bits)
		return;
	sink.set(AITrace::AI8::module_DistributedUnitManager_buildings_count, buildings.size());
	sink.set(AITrace::AI8::module_DistributedUnitManager_module_records_count,
			 module_records.size());
	sink.set(AITrace::AI8::module_DistributedUnitManager_unit_names_count, unit_names.size());
	sink.set(AITrace::AI8::module_DistributedUnitManager_ability_names_count, ability_names.size());
}

void Cabino::ExplorationManager::captureTelemetry(const AITelemetry::Sink &sink) const
{
	if (!sink.series || !sink.series->current.values[AITrace::AI8::ExplorationManager_perform_calls].bits)
		return;
	sink.set(AITrace::AI8::module_ExplorationManager_explorers_wanted, explorers_wanted);
	sink.set(AITrace::AI8::module_ExplorationManager_original_explorers_wanted,
			 original_explorers_wanted);
}

void Cabino::InnManager::captureTelemetry(const AITelemetry::Sink &sink) const
{
	if (!sink.series || !sink.series->current.values[AITrace::AI8::InnManager_perform_calls].bits)
		return;
	sink.set(AITrace::AI8::module_InnManager_inns_count, inns.size());
}

void Cabino::BuildingClearer::captureTelemetry(const AITelemetry::Sink &sink) const
{
	if (!sink.series || !sink.series->current.values[AITrace::AI8::BuildingClearer_perform_calls].bits)
		return;
	sink.set(AITrace::AI8::module_BuildingClearer_cleared_buildings_count,
			 cleared_buildings.size());
}

void Cabino::HappinessHandler::captureTelemetry(const AITelemetry::Sink &sink) const
{
	if (!sink.series || !sink.series->current.values[AITrace::AI8::HappinessHandler_perform_calls].bits)
		return;
	sink.set(AITrace::AI8::module_HappinessHandler_fruit_trees_count, fruit_trees.size());
	sink.set(AITrace::AI8::module_HappinessHandler_is_fruit_trees_computed,
			 is_fruit_trees_computed);
	sink.set(AITrace::AI8::module_HappinessHandler_exploring_fruit_trees_count,
			 exploring_fruit_trees.size());
}

void Cabino::Farmer::captureTelemetry(const AITelemetry::Sink &sink) const
{
	if (!sink.series || !sink.series->current.values[AITrace::AI8::Farmer_perform_calls].bits)
		return;
	sink.set(AITrace::AI8::module_Farmer_resources_count, resources.size());
	sink.set(AITrace::AI8::module_Farmer_is_water_gradient_computed, is_water_gradient_computed);
}
