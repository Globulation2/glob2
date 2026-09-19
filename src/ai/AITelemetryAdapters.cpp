// SPDX-License-Identifier: GPL-3.0-or-later
#include "AITelemetryFields.h"
#include "AI.h"
#include "AINumbi.h"
#include "AICastor.h"
#include "AIWarrush.h"
#include "AINicowar.h"
#include "echo/Echo.h"
#include "cortex/AICortex.h"
#include "AIMaxima.h"
#include "AICabino.h"

namespace AITelemetry
{
const std::vector<Field> &schema(int implementation)
{
	static const std::vector<std::vector<Field>> schemas = []
	{
		std::vector<std::vector<Field>> all(AI::SIZE);
		for (auto &f : all)
		{
			f.push_back({"polls", "calls", "Actual AI implementation polls", Unsigned, Counter});
			f.push_back({"null_orders", "orders", "Returned null orders", Unsigned, Counter});
			f.push_back({"emitted_orders", "orders",
						 "Returned non-null orders; not engine acceptance", Unsigned, Counter});
			for (int n = 0; n < 256; ++n)
				f.push_back({"orders_" + std::to_string(n), "orders", "Returned engine order type",
							 Unsigned, Counter});
		}
#define AI_BEGIN(number)                                                                           \
	{                                                                                              \
		auto &f = all[number];
#define AI_FIELD(key, name, unit, meaning, type, kind)                                             \
	f.push_back({name, unit, meaning, type, kind});
#define AI_END() }
#include "AITelemetryFields.inc"
#undef AI_BEGIN
#undef AI_FIELD
#undef AI_END
		return all;
	}();
	return schemas.at(implementation);
}
} // namespace AITelemetry

void AINumbi::captureTelemetry()
{
	telemetry.set(AITrace::AI1::state_timer, timer);
	telemetry.set(AITrace::AI1::state_phase, phase);
	telemetry.set(AITrace::AI1::state_attackPhase, attackPhase);
	telemetry.set(AITrace::AI1::state_phaseTime, phaseTime);
	telemetry.set(AITrace::AI1::state_criticalWarriors, criticalWarriors);
	telemetry.set(AITrace::AI1::state_criticalTime, criticalTime);
	telemetry.set(AITrace::AI1::state_attackTimer, attackTimer);
}

void AICastor::captureTelemetry()
{
	telemetry.set(AITrace::AI2::state_buildingSum_8_0, buildingSum[8][0]);
	telemetry.set(AITrace::AI2::state_buildingSum_8_1, buildingSum[8][1]);
	telemetry.set(AITrace::AI2::state_buildingSum_9_0, buildingSum[9][0]);
	telemetry.set(AITrace::AI2::state_buildingSum_9_1, buildingSum[9][1]);
	telemetry.set(AITrace::AI2::state_buildingSum_10_0, buildingSum[10][0]);
	telemetry.set(AITrace::AI2::state_buildingSum_10_1, buildingSum[10][1]);
	telemetry.set(AITrace::AI2::state_buildingSum_11_0, buildingSum[11][0]);
	telemetry.set(AITrace::AI2::state_buildingSum_11_1, buildingSum[11][1]);
	telemetry.set(AITrace::AI2::state_buildingSum_12_0, buildingSum[12][0]);
	telemetry.set(AITrace::AI2::state_buildingSum_12_1, buildingSum[12][1]);
	telemetry.set(AITrace::AI2::state_timer, timer);
	telemetry.set(AITrace::AI2::state_canSwim, canSwim);
	telemetry.set(AITrace::AI2::state_needSwim, needSwim);
	telemetry.set(AITrace::AI2::state_warLevel, warLevel);
	telemetry.set(AITrace::AI2::state_warTimeTriggerLevel, warTimeTriggerLevel);
	telemetry.set(AITrace::AI2::state_warLevelTriggerLevel, warLevelTriggerLevel);
	telemetry.set(AITrace::AI2::state_warAmountTriggerLevel, warAmountTriggerLevel);
	telemetry.set(AITrace::AI2::state_onStrike, onStrike);
	telemetry.set(AITrace::AI2::state_strikeTeamSelected, strikeTeamSelected);
	telemetry.set(AITrace::AI2::state_strikeTeam, strikeTeam);
	telemetry.set(AITrace::AI2::state_foodWarning, foodWarning);
	telemetry.set(AITrace::AI2::state_foodLock, foodLock);
	telemetry.set(AITrace::AI2::state_foodSurplus, foodSurplus);
	telemetry.set(AITrace::AI2::state_overWorkers, overWorkers);
	telemetry.set(AITrace::AI2::state_starvingWarning, starvingWarning);
	telemetry.set(AITrace::AI2::state_buildsAmount, buildsAmount);
	telemetry.set(AITrace::AI2::state_projects_count, projects.size());
	telemetry.set(AITrace::AI2::state_buildingSum_0_0, buildingSum[0][0]);
	telemetry.set(AITrace::AI2::state_buildingSum_0_1, buildingSum[0][1]);
	telemetry.set(AITrace::AI2::state_buildingSum_1_0, buildingSum[1][0]);
	telemetry.set(AITrace::AI2::state_buildingSum_1_1, buildingSum[1][1]);
	telemetry.set(AITrace::AI2::state_buildingSum_2_0, buildingSum[2][0]);
	telemetry.set(AITrace::AI2::state_buildingSum_2_1, buildingSum[2][1]);
	telemetry.set(AITrace::AI2::state_buildingSum_3_0, buildingSum[3][0]);
	telemetry.set(AITrace::AI2::state_buildingSum_3_1, buildingSum[3][1]);
	telemetry.set(AITrace::AI2::state_buildingSum_4_0, buildingSum[4][0]);
	telemetry.set(AITrace::AI2::state_buildingSum_4_1, buildingSum[4][1]);
	telemetry.set(AITrace::AI2::state_buildingSum_5_0, buildingSum[5][0]);
	telemetry.set(AITrace::AI2::state_buildingSum_5_1, buildingSum[5][1]);
	telemetry.set(AITrace::AI2::state_buildingSum_6_0, buildingSum[6][0]);
	telemetry.set(AITrace::AI2::state_buildingSum_6_1, buildingSum[6][1]);
	telemetry.set(AITrace::AI2::state_buildingSum_7_0, buildingSum[7][0]);
	telemetry.set(AITrace::AI2::state_buildingSum_7_1, buildingSum[7][1]);
}

void AIWarrush::captureTelemetry()
{
	telemetry.set(AITrace::AI3::state_buildingDelay, buildingDelay);
	telemetry.set(AITrace::AI3::state_areaUpdatingDelay, areaUpdatingDelay);
}

void AIEcho::Econo::captureTelemetry()
{
	telemetry.set(AITrace::AI4::state_timer, timer);
	telemetry.set(AITrace::AI4::state_flag_on_cherry, flag_on_cherry);
	telemetry.set(AITrace::AI4::state_flag_on_orange, flag_on_orange);
	telemetry.set(AITrace::AI4::state_flag_on_prune, flag_on_prune);
	telemetry.set(AITrace::AI4::state_flags_on_enemy_count, flags_on_enemy.size());
}

void NewNicowar::captureTelemetry()
{
	telemetry.set(AITrace::AI5::state_timer, timer);
	telemetry.set(AITrace::AI5::state_growth_phase, growth_phase);
	telemetry.set(AITrace::AI5::state_skilled_work_phase, skilled_work_phase);
	telemetry.set(AITrace::AI5::state_upgrading_phase_1, upgrading_phase_1);
	telemetry.set(AITrace::AI5::state_upgrading_phase_2, upgrading_phase_2);
	telemetry.set(AITrace::AI5::state_war_preparation, war_preparation);
	telemetry.set(AITrace::AI5::state_war, war);
	telemetry.set(AITrace::AI5::state_fruit_phase, fruit_phase);
	telemetry.set(AITrace::AI5::state_starving_recovery, starving_recovery);
	telemetry.set(AITrace::AI5::state_no_workers_phase, no_workers_phase);
	telemetry.set(AITrace::AI5::state_can_swim, can_swim);
	telemetry.set(AITrace::AI5::state_defend_explorers, defend_explorers);
	telemetry.set(AITrace::AI5::state_explorer_attack_preparation_phase,
				  explorer_attack_preparation_phase);
	telemetry.set(AITrace::AI5::state_explorer_attack_phase, explorer_attack_phase);
	telemetry.set(AITrace::AI5::state_starving_recovery_inns, starving_recovery_inns);
	telemetry.set(AITrace::AI5::state_buildings_under_construction, buildings_under_construction);
	telemetry.set(AITrace::AI5::state_placement_queue_count, placement_queue.size());
	telemetry.set(AITrace::AI5::state_construction_queue_count, construction_queue.size());
	telemetry.set(AITrace::AI5::state_target, target);
	telemetry.set(AITrace::AI5::state_is_digging_out, is_digging_out);
	telemetry.set(AITrace::AI5::state_attack_flags_count, attack_flags.size());
	telemetry.set(AITrace::AI5::state_defense_flags_count, defense_flags.size());
	telemetry.set(AITrace::AI5::state_explorer_attack_flags_count, explorer_attack_flags.size());
	telemetry.set(AITrace::AI5::state_exploration_on_fruit, exploration_on_fruit);
}

void AICortex::captureTelemetry()
{
	telemetry.set(AITrace::AI6::state_timer, timer);
	telemetry.set(AITrace::AI6::state_pendingUpgradeType, pendingUpgradeType);
	telemetry.set(AITrace::AI6::state_pendingUpgradeUntil, pendingUpgradeUntil);
	telemetry.set(AITrace::AI6::state_flagPosture, flagPosture);
	telemetry.set(AITrace::AI6::state_offenseHoldUntil, offenseHoldUntil);
	telemetry.set(AITrace::AI6::state_lastOffenseTargetTeam, lastOffenseTargetTeam);
	telemetry.set(AITrace::AI6::state_wheatOpenMargin, wheatOpenMargin);
	telemetry.set(AITrace::AI6::state_swarmKickstarted, swarmKickstarted);
	telemetry.set(AITrace::AI6::state_orderQueue_count, orderQueue.size());
	telemetry.set(AITrace::AI6::state_offenseWaves_0_gid, offenseWaves[0].gid);
	telemetry.set(AITrace::AI6::state_offenseWaves_0_phase, offenseWaves[0].phase);
	telemetry.set(AITrace::AI6::state_offenseWaves_0_phaseDeadline, offenseWaves[0].phaseDeadline);
	telemetry.set(AITrace::AI6::state_offenseWaves_0_musterBestArrived,
				  offenseWaves[0].musterBestArrived);
	telemetry.set(AITrace::AI6::state_offenseWaves_0_createCooldown,
				  offenseWaves[0].createCooldown);
	telemetry.set(AITrace::AI6::state_offenseWaves_1_gid, offenseWaves[1].gid);
	telemetry.set(AITrace::AI6::state_offenseWaves_1_phase, offenseWaves[1].phase);
	telemetry.set(AITrace::AI6::state_offenseWaves_1_phaseDeadline, offenseWaves[1].phaseDeadline);
	telemetry.set(AITrace::AI6::state_offenseWaves_1_musterBestArrived,
				  offenseWaves[1].musterBestArrived);
	telemetry.set(AITrace::AI6::state_offenseWaves_1_createCooldown,
				  offenseWaves[1].createCooldown);
	telemetry.set(AITrace::AI6::state_offenseWaves_2_gid, offenseWaves[2].gid);
	telemetry.set(AITrace::AI6::state_offenseWaves_2_phase, offenseWaves[2].phase);
	telemetry.set(AITrace::AI6::state_offenseWaves_2_phaseDeadline, offenseWaves[2].phaseDeadline);
	telemetry.set(AITrace::AI6::state_offenseWaves_2_musterBestArrived,
				  offenseWaves[2].musterBestArrived);
	telemetry.set(AITrace::AI6::state_offenseWaves_2_createCooldown,
				  offenseWaves[2].createCooldown);
}

void AIMaxima::Maxima::captureTelemetry()
{
	if (!director.initialized)
		return;
	telemetry.set(AITrace::AI7::opponent_0_alive, opponents[0].alive);
	telemetry.set(AITrace::AI7::opponent_0_visible_warriors, opponents[0].visible_warriors);
	telemetry.set(AITrace::AI7::opponent_0_estimated_warriors, opponents[0].estimated_warriors);
	telemetry.set(AITrace::AI7::opponent_0_last_observed_warriors,
				  opponents[0].last_observed_warriors);
	telemetry.set(AITrace::AI7::opponent_0_visible_explorers, opponents[0].visible_explorers);
	telemetry.set(AITrace::AI7::opponent_0_visible_buildings, opponents[0].visible_buildings);
	telemetry.set(AITrace::AI7::opponent_0_known_buildings, opponents[0].known_buildings);
	telemetry.set(AITrace::AI7::opponent_0_reachable_buildings, opponents[0].reachable_buildings);
	telemetry.set(AITrace::AI7::opponent_0_strategic_value, opponents[0].strategic_value);
	telemetry.set(AITrace::AI7::opponent_0_nearest_building, opponents[0].nearest_building);
	telemetry.set(AITrace::AI7::opponent_0_score, opponents[0].score);
	telemetry.set(AITrace::AI7::opponent_0_last_seen_tick, opponents[0].last_seen_tick);
	telemetry.set(AITrace::AI7::opponent_0_last_force_seen_tick, opponents[0].last_force_seen_tick);
	telemetry.set(AITrace::AI7::opponent_0_last_building_seen_tick,
				  opponents[0].last_building_seen_tick);
	telemetry.set(AITrace::AI7::opponent_0_intel_confidence, opponents[0].intel_confidence);
	telemetry.set(AITrace::AI7::opponent_1_alive, opponents[1].alive);
	telemetry.set(AITrace::AI7::opponent_1_visible_warriors, opponents[1].visible_warriors);
	telemetry.set(AITrace::AI7::opponent_1_estimated_warriors, opponents[1].estimated_warriors);
	telemetry.set(AITrace::AI7::opponent_1_last_observed_warriors,
				  opponents[1].last_observed_warriors);
	telemetry.set(AITrace::AI7::opponent_1_visible_explorers, opponents[1].visible_explorers);
	telemetry.set(AITrace::AI7::opponent_1_visible_buildings, opponents[1].visible_buildings);
	telemetry.set(AITrace::AI7::opponent_1_known_buildings, opponents[1].known_buildings);
	telemetry.set(AITrace::AI7::opponent_1_reachable_buildings, opponents[1].reachable_buildings);
	telemetry.set(AITrace::AI7::opponent_1_strategic_value, opponents[1].strategic_value);
	telemetry.set(AITrace::AI7::opponent_1_nearest_building, opponents[1].nearest_building);
	telemetry.set(AITrace::AI7::opponent_1_score, opponents[1].score);
	telemetry.set(AITrace::AI7::opponent_1_last_seen_tick, opponents[1].last_seen_tick);
	telemetry.set(AITrace::AI7::opponent_1_last_force_seen_tick, opponents[1].last_force_seen_tick);
	telemetry.set(AITrace::AI7::opponent_1_last_building_seen_tick,
				  opponents[1].last_building_seen_tick);
	telemetry.set(AITrace::AI7::opponent_1_intel_confidence, opponents[1].intel_confidence);
	telemetry.set(AITrace::AI7::opponent_2_alive, opponents[2].alive);
	telemetry.set(AITrace::AI7::opponent_2_visible_warriors, opponents[2].visible_warriors);
	telemetry.set(AITrace::AI7::opponent_2_estimated_warriors, opponents[2].estimated_warriors);
	telemetry.set(AITrace::AI7::opponent_2_last_observed_warriors,
				  opponents[2].last_observed_warriors);
	telemetry.set(AITrace::AI7::opponent_2_visible_explorers, opponents[2].visible_explorers);
	telemetry.set(AITrace::AI7::opponent_2_visible_buildings, opponents[2].visible_buildings);
	telemetry.set(AITrace::AI7::opponent_2_known_buildings, opponents[2].known_buildings);
	telemetry.set(AITrace::AI7::opponent_2_reachable_buildings, opponents[2].reachable_buildings);
	telemetry.set(AITrace::AI7::opponent_2_strategic_value, opponents[2].strategic_value);
	telemetry.set(AITrace::AI7::opponent_2_nearest_building, opponents[2].nearest_building);
	telemetry.set(AITrace::AI7::opponent_2_score, opponents[2].score);
	telemetry.set(AITrace::AI7::opponent_2_last_seen_tick, opponents[2].last_seen_tick);
	telemetry.set(AITrace::AI7::opponent_2_last_force_seen_tick, opponents[2].last_force_seen_tick);
	telemetry.set(AITrace::AI7::opponent_2_last_building_seen_tick,
				  opponents[2].last_building_seen_tick);
	telemetry.set(AITrace::AI7::opponent_2_intel_confidence, opponents[2].intel_confidence);
	telemetry.set(AITrace::AI7::opponent_3_alive, opponents[3].alive);
	telemetry.set(AITrace::AI7::opponent_3_visible_warriors, opponents[3].visible_warriors);
	telemetry.set(AITrace::AI7::opponent_3_estimated_warriors, opponents[3].estimated_warriors);
	telemetry.set(AITrace::AI7::opponent_3_last_observed_warriors,
				  opponents[3].last_observed_warriors);
	telemetry.set(AITrace::AI7::opponent_3_visible_explorers, opponents[3].visible_explorers);
	telemetry.set(AITrace::AI7::opponent_3_visible_buildings, opponents[3].visible_buildings);
	telemetry.set(AITrace::AI7::opponent_3_known_buildings, opponents[3].known_buildings);
	telemetry.set(AITrace::AI7::opponent_3_reachable_buildings, opponents[3].reachable_buildings);
	telemetry.set(AITrace::AI7::opponent_3_strategic_value, opponents[3].strategic_value);
	telemetry.set(AITrace::AI7::opponent_3_nearest_building, opponents[3].nearest_building);
	telemetry.set(AITrace::AI7::opponent_3_score, opponents[3].score);
	telemetry.set(AITrace::AI7::opponent_3_last_seen_tick, opponents[3].last_seen_tick);
	telemetry.set(AITrace::AI7::opponent_3_last_force_seen_tick, opponents[3].last_force_seen_tick);
	telemetry.set(AITrace::AI7::opponent_3_last_building_seen_tick,
				  opponents[3].last_building_seen_tick);
	telemetry.set(AITrace::AI7::opponent_3_intel_confidence, opponents[3].intel_confidence);
	telemetry.set(AITrace::AI7::opponent_4_alive, opponents[4].alive);
	telemetry.set(AITrace::AI7::opponent_4_visible_warriors, opponents[4].visible_warriors);
	telemetry.set(AITrace::AI7::opponent_4_estimated_warriors, opponents[4].estimated_warriors);
	telemetry.set(AITrace::AI7::opponent_4_last_observed_warriors,
				  opponents[4].last_observed_warriors);
	telemetry.set(AITrace::AI7::opponent_4_visible_explorers, opponents[4].visible_explorers);
	telemetry.set(AITrace::AI7::opponent_4_visible_buildings, opponents[4].visible_buildings);
	telemetry.set(AITrace::AI7::opponent_4_known_buildings, opponents[4].known_buildings);
	telemetry.set(AITrace::AI7::opponent_4_reachable_buildings, opponents[4].reachable_buildings);
	telemetry.set(AITrace::AI7::opponent_4_strategic_value, opponents[4].strategic_value);
	telemetry.set(AITrace::AI7::opponent_4_nearest_building, opponents[4].nearest_building);
	telemetry.set(AITrace::AI7::opponent_4_score, opponents[4].score);
	telemetry.set(AITrace::AI7::opponent_4_last_seen_tick, opponents[4].last_seen_tick);
	telemetry.set(AITrace::AI7::opponent_4_last_force_seen_tick, opponents[4].last_force_seen_tick);
	telemetry.set(AITrace::AI7::opponent_4_last_building_seen_tick,
				  opponents[4].last_building_seen_tick);
	telemetry.set(AITrace::AI7::opponent_4_intel_confidence, opponents[4].intel_confidence);
	telemetry.set(AITrace::AI7::opponent_5_alive, opponents[5].alive);
	telemetry.set(AITrace::AI7::opponent_5_visible_warriors, opponents[5].visible_warriors);
	telemetry.set(AITrace::AI7::opponent_5_estimated_warriors, opponents[5].estimated_warriors);
	telemetry.set(AITrace::AI7::opponent_5_last_observed_warriors,
				  opponents[5].last_observed_warriors);
	telemetry.set(AITrace::AI7::opponent_5_visible_explorers, opponents[5].visible_explorers);
	telemetry.set(AITrace::AI7::opponent_5_visible_buildings, opponents[5].visible_buildings);
	telemetry.set(AITrace::AI7::opponent_5_known_buildings, opponents[5].known_buildings);
	telemetry.set(AITrace::AI7::opponent_5_reachable_buildings, opponents[5].reachable_buildings);
	telemetry.set(AITrace::AI7::opponent_5_strategic_value, opponents[5].strategic_value);
	telemetry.set(AITrace::AI7::opponent_5_nearest_building, opponents[5].nearest_building);
	telemetry.set(AITrace::AI7::opponent_5_score, opponents[5].score);
	telemetry.set(AITrace::AI7::opponent_5_last_seen_tick, opponents[5].last_seen_tick);
	telemetry.set(AITrace::AI7::opponent_5_last_force_seen_tick, opponents[5].last_force_seen_tick);
	telemetry.set(AITrace::AI7::opponent_5_last_building_seen_tick,
				  opponents[5].last_building_seen_tick);
	telemetry.set(AITrace::AI7::opponent_5_intel_confidence, opponents[5].intel_confidence);
	telemetry.set(AITrace::AI7::opponent_6_alive, opponents[6].alive);
	telemetry.set(AITrace::AI7::opponent_6_visible_warriors, opponents[6].visible_warriors);
	telemetry.set(AITrace::AI7::opponent_6_estimated_warriors, opponents[6].estimated_warriors);
	telemetry.set(AITrace::AI7::opponent_6_last_observed_warriors,
				  opponents[6].last_observed_warriors);
	telemetry.set(AITrace::AI7::opponent_6_visible_explorers, opponents[6].visible_explorers);
	telemetry.set(AITrace::AI7::opponent_6_visible_buildings, opponents[6].visible_buildings);
	telemetry.set(AITrace::AI7::opponent_6_known_buildings, opponents[6].known_buildings);
	telemetry.set(AITrace::AI7::opponent_6_reachable_buildings, opponents[6].reachable_buildings);
	telemetry.set(AITrace::AI7::opponent_6_strategic_value, opponents[6].strategic_value);
	telemetry.set(AITrace::AI7::opponent_6_nearest_building, opponents[6].nearest_building);
	telemetry.set(AITrace::AI7::opponent_6_score, opponents[6].score);
	telemetry.set(AITrace::AI7::opponent_6_last_seen_tick, opponents[6].last_seen_tick);
	telemetry.set(AITrace::AI7::opponent_6_last_force_seen_tick, opponents[6].last_force_seen_tick);
	telemetry.set(AITrace::AI7::opponent_6_last_building_seen_tick,
				  opponents[6].last_building_seen_tick);
	telemetry.set(AITrace::AI7::opponent_6_intel_confidence, opponents[6].intel_confidence);
	telemetry.set(AITrace::AI7::opponent_7_alive, opponents[7].alive);
	telemetry.set(AITrace::AI7::opponent_7_visible_warriors, opponents[7].visible_warriors);
	telemetry.set(AITrace::AI7::opponent_7_estimated_warriors, opponents[7].estimated_warriors);
	telemetry.set(AITrace::AI7::opponent_7_last_observed_warriors,
				  opponents[7].last_observed_warriors);
	telemetry.set(AITrace::AI7::opponent_7_visible_explorers, opponents[7].visible_explorers);
	telemetry.set(AITrace::AI7::opponent_7_visible_buildings, opponents[7].visible_buildings);
	telemetry.set(AITrace::AI7::opponent_7_known_buildings, opponents[7].known_buildings);
	telemetry.set(AITrace::AI7::opponent_7_reachable_buildings, opponents[7].reachable_buildings);
	telemetry.set(AITrace::AI7::opponent_7_strategic_value, opponents[7].strategic_value);
	telemetry.set(AITrace::AI7::opponent_7_nearest_building, opponents[7].nearest_building);
	telemetry.set(AITrace::AI7::opponent_7_score, opponents[7].score);
	telemetry.set(AITrace::AI7::opponent_7_last_seen_tick, opponents[7].last_seen_tick);
	telemetry.set(AITrace::AI7::opponent_7_last_force_seen_tick, opponents[7].last_force_seen_tick);
	telemetry.set(AITrace::AI7::opponent_7_last_building_seen_tick,
				  opponents[7].last_building_seen_tick);
	telemetry.set(AITrace::AI7::opponent_7_intel_confidence, opponents[7].intel_confidence);
	telemetry.set(AITrace::AI7::opponent_8_alive, opponents[8].alive);
	telemetry.set(AITrace::AI7::opponent_8_visible_warriors, opponents[8].visible_warriors);
	telemetry.set(AITrace::AI7::opponent_8_estimated_warriors, opponents[8].estimated_warriors);
	telemetry.set(AITrace::AI7::opponent_8_last_observed_warriors,
				  opponents[8].last_observed_warriors);
	telemetry.set(AITrace::AI7::opponent_8_visible_explorers, opponents[8].visible_explorers);
	telemetry.set(AITrace::AI7::opponent_8_visible_buildings, opponents[8].visible_buildings);
	telemetry.set(AITrace::AI7::opponent_8_known_buildings, opponents[8].known_buildings);
	telemetry.set(AITrace::AI7::opponent_8_reachable_buildings, opponents[8].reachable_buildings);
	telemetry.set(AITrace::AI7::opponent_8_strategic_value, opponents[8].strategic_value);
	telemetry.set(AITrace::AI7::opponent_8_nearest_building, opponents[8].nearest_building);
	telemetry.set(AITrace::AI7::opponent_8_score, opponents[8].score);
	telemetry.set(AITrace::AI7::opponent_8_last_seen_tick, opponents[8].last_seen_tick);
	telemetry.set(AITrace::AI7::opponent_8_last_force_seen_tick, opponents[8].last_force_seen_tick);
	telemetry.set(AITrace::AI7::opponent_8_last_building_seen_tick,
				  opponents[8].last_building_seen_tick);
	telemetry.set(AITrace::AI7::opponent_8_intel_confidence, opponents[8].intel_confidence);
	telemetry.set(AITrace::AI7::opponent_9_alive, opponents[9].alive);
	telemetry.set(AITrace::AI7::opponent_9_visible_warriors, opponents[9].visible_warriors);
	telemetry.set(AITrace::AI7::opponent_9_estimated_warriors, opponents[9].estimated_warriors);
	telemetry.set(AITrace::AI7::opponent_9_last_observed_warriors,
				  opponents[9].last_observed_warriors);
	telemetry.set(AITrace::AI7::opponent_9_visible_explorers, opponents[9].visible_explorers);
	telemetry.set(AITrace::AI7::opponent_9_visible_buildings, opponents[9].visible_buildings);
	telemetry.set(AITrace::AI7::opponent_9_known_buildings, opponents[9].known_buildings);
	telemetry.set(AITrace::AI7::opponent_9_reachable_buildings, opponents[9].reachable_buildings);
	telemetry.set(AITrace::AI7::opponent_9_strategic_value, opponents[9].strategic_value);
	telemetry.set(AITrace::AI7::opponent_9_nearest_building, opponents[9].nearest_building);
	telemetry.set(AITrace::AI7::opponent_9_score, opponents[9].score);
	telemetry.set(AITrace::AI7::opponent_9_last_seen_tick, opponents[9].last_seen_tick);
	telemetry.set(AITrace::AI7::opponent_9_last_force_seen_tick, opponents[9].last_force_seen_tick);
	telemetry.set(AITrace::AI7::opponent_9_last_building_seen_tick,
				  opponents[9].last_building_seen_tick);
	telemetry.set(AITrace::AI7::opponent_9_intel_confidence, opponents[9].intel_confidence);
	telemetry.set(AITrace::AI7::opponent_10_alive, opponents[10].alive);
	telemetry.set(AITrace::AI7::opponent_10_visible_warriors, opponents[10].visible_warriors);
	telemetry.set(AITrace::AI7::opponent_10_estimated_warriors, opponents[10].estimated_warriors);
	telemetry.set(AITrace::AI7::opponent_10_last_observed_warriors,
				  opponents[10].last_observed_warriors);
	telemetry.set(AITrace::AI7::opponent_10_visible_explorers, opponents[10].visible_explorers);
	telemetry.set(AITrace::AI7::opponent_10_visible_buildings, opponents[10].visible_buildings);
	telemetry.set(AITrace::AI7::opponent_10_known_buildings, opponents[10].known_buildings);
	telemetry.set(AITrace::AI7::opponent_10_reachable_buildings, opponents[10].reachable_buildings);
	telemetry.set(AITrace::AI7::opponent_10_strategic_value, opponents[10].strategic_value);
	telemetry.set(AITrace::AI7::opponent_10_nearest_building, opponents[10].nearest_building);
	telemetry.set(AITrace::AI7::opponent_10_score, opponents[10].score);
	telemetry.set(AITrace::AI7::opponent_10_last_seen_tick, opponents[10].last_seen_tick);
	telemetry.set(AITrace::AI7::opponent_10_last_force_seen_tick,
				  opponents[10].last_force_seen_tick);
	telemetry.set(AITrace::AI7::opponent_10_last_building_seen_tick,
				  opponents[10].last_building_seen_tick);
	telemetry.set(AITrace::AI7::opponent_10_intel_confidence, opponents[10].intel_confidence);
	telemetry.set(AITrace::AI7::opponent_11_alive, opponents[11].alive);
	telemetry.set(AITrace::AI7::opponent_11_visible_warriors, opponents[11].visible_warriors);
	telemetry.set(AITrace::AI7::opponent_11_estimated_warriors, opponents[11].estimated_warriors);
	telemetry.set(AITrace::AI7::opponent_11_last_observed_warriors,
				  opponents[11].last_observed_warriors);
	telemetry.set(AITrace::AI7::opponent_11_visible_explorers, opponents[11].visible_explorers);
	telemetry.set(AITrace::AI7::opponent_11_visible_buildings, opponents[11].visible_buildings);
	telemetry.set(AITrace::AI7::opponent_11_known_buildings, opponents[11].known_buildings);
	telemetry.set(AITrace::AI7::opponent_11_reachable_buildings, opponents[11].reachable_buildings);
	telemetry.set(AITrace::AI7::opponent_11_strategic_value, opponents[11].strategic_value);
	telemetry.set(AITrace::AI7::opponent_11_nearest_building, opponents[11].nearest_building);
	telemetry.set(AITrace::AI7::opponent_11_score, opponents[11].score);
	telemetry.set(AITrace::AI7::opponent_11_last_seen_tick, opponents[11].last_seen_tick);
	telemetry.set(AITrace::AI7::opponent_11_last_force_seen_tick,
				  opponents[11].last_force_seen_tick);
	telemetry.set(AITrace::AI7::opponent_11_last_building_seen_tick,
				  opponents[11].last_building_seen_tick);
	telemetry.set(AITrace::AI7::opponent_11_intel_confidence, opponents[11].intel_confidence);
	telemetry.set(AITrace::AI7::state_posture, posture);
	telemetry.set(AITrace::AI7::state_posture_since, posture_since);
	telemetry.set(AITrace::AI7::state_established_colonies, established_colonies);
	telemetry.set(AITrace::AI7::state_last_colony_completed_tick, last_colony_completed_tick);
	telemetry.set(AITrace::AI7::state_reconnaissance_suspended, reconnaissance_suspended);
	telemetry.set(AITrace::AI7::state_last_recon_mission_tick, last_recon_mission_tick);
	telemetry.set(AITrace::AI7::state_snapshot_tick, snapshot.tick);
	telemetry.set(AITrace::AI7::state_snapshot_population, snapshot.population);
	telemetry.set(AITrace::AI7::state_snapshot_workers, snapshot.workers);
	telemetry.set(AITrace::AI7::state_snapshot_free_warriors, snapshot.free_warriors);
	telemetry.set(AITrace::AI7::state_snapshot_explorers, snapshot.explorers);
	telemetry.set(AITrace::AI7::state_snapshot_trained_explorers, snapshot.trained_explorers);
	telemetry.set(AITrace::AI7::state_snapshot_warriors, snapshot.warriors);
	telemetry.set(AITrace::AI7::state_snapshot_trained_workers, snapshot.trained_workers);
	telemetry.set(AITrace::AI7::state_snapshot_trained_workers_level2,
				  snapshot.trained_workers_level2);
	telemetry.set(AITrace::AI7::state_snapshot_trained_warriors, snapshot.trained_warriors);
	telemetry.set(AITrace::AI7::state_snapshot_swimming_workers, snapshot.swimming_workers);
	telemetry.set(AITrace::AI7::state_snapshot_swimming_explorers, snapshot.swimming_explorers);
	telemetry.set(AITrace::AI7::state_snapshot_swimming_warriors, snapshot.swimming_warriors);
	telemetry.set(AITrace::AI7::state_snapshot_amphibious_attack_explorers,
				  snapshot.amphibious_attack_explorers);
	telemetry.set(AITrace::AI7::state_snapshot_free_workers, snapshot.free_workers);
	telemetry.set(AITrace::AI7::state_snapshot_worker_jobs_open, snapshot.worker_jobs_open);
	telemetry.set(AITrace::AI7::state_snapshot_hungry, snapshot.hungry);
	telemetry.set(AITrace::AI7::state_snapshot_critical_food, snapshot.critical_food);
	telemetry.set(AITrace::AI7::state_snapshot_unserved_food, snapshot.unserved_food);
	telemetry.set(AITrace::AI7::state_snapshot_need_heal, snapshot.need_heal);
	telemetry.set(AITrace::AI7::state_snapshot_buildings, snapshot.buildings);
	telemetry.set(AITrace::AI7::state_snapshot_building_sites, snapshot.building_sites);
	telemetry.set(AITrace::AI7::state_snapshot_swarms, snapshot.swarms);
	telemetry.set(AITrace::AI7::state_snapshot_completed_swarms, snapshot.completed_swarms);
	telemetry.set(AITrace::AI7::state_snapshot_inns, snapshot.inns);
	telemetry.set(AITrace::AI7::state_snapshot_inn_level1, snapshot.inn_level1);
	telemetry.set(AITrace::AI7::state_snapshot_inn_level2, snapshot.inn_level2);
	telemetry.set(AITrace::AI7::state_snapshot_inn_level3, snapshot.inn_level3);
	telemetry.set(AITrace::AI7::state_snapshot_barracks, snapshot.barracks);
	telemetry.set(AITrace::AI7::state_snapshot_schools, snapshot.schools);
	telemetry.set(AITrace::AI7::state_snapshot_school_level1, snapshot.school_level1);
	telemetry.set(AITrace::AI7::state_snapshot_school_level2, snapshot.school_level2);
	telemetry.set(AITrace::AI7::state_snapshot_school_level3, snapshot.school_level3);
	telemetry.set(AITrace::AI7::state_snapshot_pools, snapshot.pools);
	telemetry.set(AITrace::AI7::state_snapshot_hospitals, snapshot.hospitals);
	telemetry.set(AITrace::AI7::state_snapshot_racetracks, snapshot.racetracks);
	telemetry.set(AITrace::AI7::state_snapshot_towers, snapshot.towers);
	telemetry.set(AITrace::AI7::state_snapshot_own_buildings_under_attack,
				  snapshot.own_buildings_under_attack);
	telemetry.set(AITrace::AI7::state_snapshot_own_units_under_attack,
				  snapshot.own_units_under_attack);
	telemetry.set(AITrace::AI7::state_snapshot_visible_enemy_warriors,
				  snapshot.visible_enemy_warriors);
	telemetry.set(AITrace::AI7::state_snapshot_visible_enemy_explorers,
				  snapshot.visible_enemy_explorers);
	telemetry.set(AITrace::AI7::state_snapshot_visible_enemy_attack_explorers,
				  snapshot.visible_enemy_attack_explorers);
	telemetry.set(AITrace::AI7::state_snapshot_visible_colony_explorer_threat,
				  snapshot.visible_colony_explorer_threat);
	telemetry.set(AITrace::AI7::state_snapshot_visible_colony_threat,
				  snapshot.visible_colony_threat);
	telemetry.set(AITrace::AI7::state_snapshot_alive_enemies, snapshot.alive_enemies);
	telemetry.set(AITrace::AI7::state_snapshot_total_hp, snapshot.total_hp);
	telemetry.set(AITrace::AI7::state_snapshot_attack_power, snapshot.attack_power);
	telemetry.set(AITrace::AI7::state_snapshot_prestige, snapshot.prestige);
	telemetry.set(AITrace::AI7::state_snapshot_enemy_prestige, snapshot.enemy_prestige);
	telemetry.set(AITrace::AI7::state_snapshot_tower_stone, snapshot.tower_stone);
	telemetry.set(AITrace::AI7::state_snapshot_tower_bullets, snapshot.tower_bullets);
	telemetry.set(AITrace::AI7::state_trends_population, trends.population);
	telemetry.set(AITrace::AI7::state_trends_workers, trends.workers);
	telemetry.set(AITrace::AI7::state_trends_warriors, trends.warriors);
	telemetry.set(AITrace::AI7::state_trends_food_pressure, trends.food_pressure);
	telemetry.set(AITrace::AI7::state_trends_colony_pressure, trends.colony_pressure);
	telemetry.set(AITrace::AI7::state_environment_known_tiles, environment.known_tiles);
	telemetry.set(AITrace::AI7::state_environment_accessible_corn, environment.accessible_corn);
	telemetry.set(AITrace::AI7::state_environment_accessible_wood, environment.accessible_wood);
	telemetry.set(AITrace::AI7::state_environment_accessible_stone, environment.accessible_stone);
	telemetry.set(AITrace::AI7::state_environment_accessible_algae, environment.accessible_algae);
	telemetry.set(AITrace::AI7::state_environment_buildable_tiles, environment.buildable_tiles);
	telemetry.set(AITrace::AI7::state_environment_water_tiles, environment.water_tiles);
	telemetry.set(AITrace::AI7::state_environment_feeding_capacity, environment.feeding_capacity);
	telemetry.set(AITrace::AI7::state_environment_food_headroom, environment.food_headroom);
	telemetry.set(AITrace::AI7::state_environment_resource_capacity, environment.resource_capacity);
	telemetry.set(AITrace::AI7::state_environment_space_capacity, environment.space_capacity);
	telemetry.set(AITrace::AI7::state_environment_food_security, environment.food_security);
	telemetry.set(AITrace::AI7::state_environment_abundance, environment.abundance);
	telemetry.set(AITrace::AI7::state_environment_terrain_abundance, environment.terrain_abundance);
	telemetry.set(AITrace::AI7::state_environment_connected_abundance,
				  environment.connected_abundance);
	telemetry.set(AITrace::AI7::state_environment_mobility_opportunity,
				  environment.mobility_opportunity);
	telemetry.set(AITrace::AI7::state_environment_economic_momentum, environment.economic_momentum);
	telemetry.set(AITrace::AI7::state_environment_mobility_constraint,
				  environment.mobility_constraint);
	telemetry.set(AITrace::AI7::state_environment_topology_complexity,
				  environment.topology_complexity);
	telemetry.set(AITrace::AI7::state_environment_threat_pressure, environment.threat_pressure);
	telemetry.set(AITrace::AI7::state_environment_confidence, environment.confidence);
	telemetry.set(AITrace::AI7::state_demands_survival, demands.survival);
	telemetry.set(AITrace::AI7::state_demands_food, demands.food);
	telemetry.set(AITrace::AI7::state_demands_growth, demands.growth);
	telemetry.set(AITrace::AI7::state_demands_expansion, demands.expansion);
	telemetry.set(AITrace::AI7::state_demands_access, demands.access);
	telemetry.set(AITrace::AI7::state_demands_technology, demands.technology);
	telemetry.set(AITrace::AI7::state_demands_mobility, demands.mobility);
	telemetry.set(AITrace::AI7::state_demands_military, demands.military);
	telemetry.set(AITrace::AI7::state_demands_aggression, demands.aggression);
	telemetry.set(AITrace::AI7::state_budget_construction_sites, budget.construction_sites);
	telemetry.set(AITrace::AI7::state_budget_desired_inns, budget.desired_inns);
	telemetry.set(AITrace::AI7::state_budget_desired_swarms, budget.desired_swarms);
	telemetry.set(AITrace::AI7::state_budget_desired_barracks, budget.desired_barracks);
	telemetry.set(AITrace::AI7::state_budget_desired_schools, budget.desired_schools);
	telemetry.set(AITrace::AI7::state_budget_desired_pools, budget.desired_pools);
	telemetry.set(AITrace::AI7::state_budget_desired_racetracks, budget.desired_racetracks);
	telemetry.set(AITrace::AI7::state_budget_desired_hospitals, budget.desired_hospitals);
	telemetry.set(AITrace::AI7::state_budget_desired_towers, budget.desired_towers);
	telemetry.set(AITrace::AI7::state_budget_swarm_workers, budget.swarm_workers);
	telemetry.set(AITrace::AI7::state_budget_worker_ratio, budget.worker_ratio);
	telemetry.set(AITrace::AI7::state_budget_explorer_ratio, budget.explorer_ratio);
	telemetry.set(AITrace::AI7::state_budget_warrior_ratio, budget.warrior_ratio);
	telemetry.set(AITrace::AI7::state_budget_desired_explorers, budget.desired_explorers);
	telemetry.set(AITrace::AI7::state_budget_desired_warriors, budget.desired_warriors);
	telemetry.set(AITrace::AI7::state_budget_defense_reserve, budget.defense_reserve);
	telemetry.set(AITrace::AI7::state_budget_attack_flags, budget.attack_flags);
	telemetry.set(AITrace::AI7::state_budget_attack_units, budget.attack_units);
	telemetry.set(AITrace::AI7::state_budget_allow_upgrades, budget.allow_upgrades);
	telemetry.set(AITrace::AI7::state_budget_allow_level2_upgrades, budget.allow_level2_upgrades);
	telemetry.set(AITrace::AI7::state_budget_upgrade_level1_workers, budget.upgrade_level1_workers);
	telemetry.set(AITrace::AI7::state_budget_upgrade_level2_workers, budget.upgrade_level2_workers);
	telemetry.set(AITrace::AI7::state_budget_upgrade_level1_trained_units_per_slot,
				  budget.upgrade_level1_trained_units_per_slot);
	telemetry.set(AITrace::AI7::state_budget_upgrade_level2_trained_units_per_slot,
				  budget.upgrade_level2_trained_units_per_slot);
	telemetry.set(AITrace::AI7::state_budget_upgrade_level1_inn_weight,
				  budget.upgrade_level1_inn_weight);
	telemetry.set(AITrace::AI7::state_budget_upgrade_level1_hospital_weight,
				  budget.upgrade_level1_hospital_weight);
	telemetry.set(AITrace::AI7::state_budget_upgrade_level1_racetrack_weight,
				  budget.upgrade_level1_racetrack_weight);
	telemetry.set(AITrace::AI7::state_budget_upgrade_level1_pool_weight,
				  budget.upgrade_level1_pool_weight);
	telemetry.set(AITrace::AI7::state_budget_upgrade_level1_barracks_weight,
				  budget.upgrade_level1_barracks_weight);
	telemetry.set(AITrace::AI7::state_budget_upgrade_level2_inn_weight,
				  budget.upgrade_level2_inn_weight);
	telemetry.set(AITrace::AI7::state_budget_upgrade_level2_hospital_weight,
				  budget.upgrade_level2_hospital_weight);
	telemetry.set(AITrace::AI7::state_budget_upgrade_level2_racetrack_weight,
				  budget.upgrade_level2_racetrack_weight);
	telemetry.set(AITrace::AI7::state_budget_upgrade_level2_pool_weight,
				  budget.upgrade_level2_pool_weight);
	telemetry.set(AITrace::AI7::state_budget_upgrade_level2_barracks_weight,
				  budget.upgrade_level2_barracks_weight);
	telemetry.set(AITrace::AI7::state_budget_first_prestige_trained_workers,
				  budget.first_prestige_trained_workers);
	telemetry.set(AITrace::AI7::state_budget_second_prestige_trained_workers,
				  budget.second_prestige_trained_workers);
	telemetry.set(AITrace::AI7::state_budget_second_prestige_population_min,
				  budget.second_prestige_population_min);
	telemetry.set(AITrace::AI7::state_budget_food_ledger_enabled, budget.food_ledger_enabled);
	telemetry.set(AITrace::AI7::state_budget_food_retirement_enabled,
				  budget.food_retirement_enabled);
	telemetry.set(AITrace::AI7::state_budget_food_inn_burden_percent,
				  budget.food_inn_burden_percent);
	telemetry.set(AITrace::AI7::state_budget_food_swarm_burden_percent,
				  budget.food_swarm_burden_percent);
	telemetry.set(AITrace::AI7::state_budget_food_recovered_percent, budget.food_recovered_percent);
	telemetry.set(AITrace::AI7::state_budget_food_burden_confirm_ticks,
				  budget.food_burden_confirm_ticks);
	telemetry.set(AITrace::AI7::state_budget_food_retirement_cooldown_ticks,
				  budget.food_retirement_cooldown_ticks);
	telemetry.set(AITrace::AI7::state_budget_food_relocation_enabled,
				  budget.food_relocation_enabled);
	telemetry.set(AITrace::AI7::state_budget_food_relocation_min_quality_tiles,
				  budget.food_relocation_min_quality_tiles);
	telemetry.set(AITrace::AI7::state_budget_food_relocation_confirm_ticks,
				  budget.food_relocation_confirm_ticks);
	telemetry.set(AITrace::AI7::state_budget_food_relocation_cooldown_ticks,
				  budget.food_relocation_cooldown_ticks);
	telemetry.set(AITrace::AI7::state_budget_food_relocation_offer_ticks,
				  budget.food_relocation_offer_ticks);
	telemetry.set(AITrace::AI7::state_budget_food_inn_seats_level1, budget.food_inn_seats_level1);
	telemetry.set(AITrace::AI7::state_budget_food_inn_seats_level2, budget.food_inn_seats_level2);
	telemetry.set(AITrace::AI7::state_budget_food_inn_seats_level3, budget.food_inn_seats_level3);
	telemetry.set(AITrace::AI7::state_budget_staffing_window_samples,
				  budget.staffing_window_samples);
	telemetry.set(AITrace::AI7::state_budget_staffing_low_permille, budget.staffing_low_permille);
	telemetry.set(AITrace::AI7::state_budget_staffing_high_permille, budget.staffing_high_permille);
	telemetry.set(AITrace::AI7::state_budget_staffing_slack, budget.staffing_slack);
	telemetry.set(AITrace::AI7::state_budget_staffing_minimum_workers,
				  budget.staffing_minimum_workers);
	telemetry.set(AITrace::AI7::state_budget_staffing_maximum_workers,
				  budget.staffing_maximum_workers);
	telemetry.set(AITrace::AI7::state_budget_staffing_cooldown_passes,
				  budget.staffing_cooldown_passes);
	telemetry.set(AITrace::AI7::state_budget_staffing_new_inn_workers,
				  budget.staffing_new_inn_workers);
	telemetry.set(AITrace::AI7::state_budget_staffing_new_swarm_workers,
				  budget.staffing_new_swarm_workers);
	telemetry.set(AITrace::AI7::state_budget_swarm_supply_radius, budget.swarm_supply_radius);
	telemetry.set(AITrace::AI7::state_budget_attack_clearing_workers,
				  budget.attack_clearing_workers);
	telemetry.set(AITrace::AI7::state_budget_can_swim, budget.can_swim);
	telemetry.set(AITrace::AI7::state_budget_recovery_active, budget.recovery_active);
	telemetry.set(AITrace::AI7::state_budget_food_emergency, budget.food_emergency);
	telemetry.set(AITrace::AI7::state_budget_colony_emergency, budget.colony_emergency);
	telemetry.set(AITrace::AI7::state_budget_colony_swarm_requested, budget.colony_swarm_requested);
	telemetry.set(AITrace::AI7::state_budget_colony_swarm_priority, budget.colony_swarm_priority);
	telemetry.set(AITrace::AI7::state_budget_explorer_campaign_active,
				  budget.explorer_campaign_active);
	telemetry.set(AITrace::AI7::state_budget_explorer_campaign_flags,
				  budget.explorer_campaign_flags);
	telemetry.set(AITrace::AI7::state_budget_explorer_campaign_units_per_flag,
				  budget.explorer_campaign_units_per_flag);
	telemetry.set(AITrace::AI7::state_budget_fruit_active, budget.fruit_active);
	telemetry.set(AITrace::AI7::state_budget_fruit_units_per_flag, budget.fruit_units_per_flag);
	telemetry.set(AITrace::AI7::state_budget_fruit_flag_radius, budget.fruit_flag_radius);
	telemetry.set(AITrace::AI7::state_budget_reactive_defense_enabled,
				  budget.reactive_defense_enabled);
	telemetry.set(AITrace::AI7::state_budget_reactive_defense_flag_radius,
				  budget.reactive_defense_flag_radius);
	telemetry.set(AITrace::AI7::state_budget_reactive_defense_move_radius,
				  budget.reactive_defense_move_radius);
	telemetry.set(AITrace::AI7::state_budget_reactive_defense_move_deadband,
				  budget.reactive_defense_move_deadband);
	telemetry.set(AITrace::AI7::state_budget_reactive_defense_unit_cap,
				  budget.reactive_defense_unit_cap);
	telemetry.set(AITrace::AI7::state_budget_reactive_defense_advantage_min,
				  budget.reactive_defense_advantage_min);
	telemetry.set(AITrace::AI7::state_budget_reactive_defense_advantage_percent,
				  budget.reactive_defense_advantage_percent);
	telemetry.set(AITrace::AI7::state_budget_preemptive_defense_active,
				  budget.preemptive_defense_active);
	telemetry.set(AITrace::AI7::state_budget_preemptive_amphibious_active,
				  budget.preemptive_amphibious_active);
	telemetry.set(AITrace::AI7::state_budget_preemptive_effective_zone_max,
				  budget.preemptive_effective_zone_max);
	telemetry.set(AITrace::AI7::state_budget_target_switch_margin, budget.target_switch_margin);
	telemetry.set(AITrace::AI7::state_budget_preemptive_recompute_ticks,
				  budget.preemptive_recompute_ticks);
	telemetry.set(AITrace::AI7::state_budget_preemptive_inner_distance,
				  budget.preemptive_inner_distance);
	telemetry.set(AITrace::AI7::state_budget_preemptive_band_width, budget.preemptive_band_width);
	telemetry.set(AITrace::AI7::state_budget_preemptive_path_slack, budget.preemptive_path_slack);
	telemetry.set(AITrace::AI7::state_budget_preemptive_probe_radius,
				  budget.preemptive_probe_radius);
	telemetry.set(AITrace::AI7::state_budget_preemptive_cross_section_max,
				  budget.preemptive_cross_section_max);
	telemetry.set(AITrace::AI7::state_budget_preemptive_zone_radius, budget.preemptive_zone_radius);
	telemetry.set(AITrace::AI7::state_budget_preemptive_zone_max, budget.preemptive_zone_max);
	telemetry.set(AITrace::AI7::state_budget_reconnaissance_suspended,
				  budget.reconnaissance_suspended);
	telemetry.set(AITrace::AI7::state_budget_reconnaissance_flag_radius,
				  budget.reconnaissance_flag_radius);
	telemetry.set(AITrace::AI7::state_budget_reconnaissance_review_interval,
				  budget.reconnaissance_review_interval);
	telemetry.set(AITrace::AI7::state_budget_reconnaissance_economic_watch_revisit,
				  budget.reconnaissance_economic_watch_revisit);
	telemetry.set(AITrace::AI7::state_budget_farming_normal_interval,
				  budget.farming_normal_interval);
	telemetry.set(AITrace::AI7::state_budget_farming_urgent_interval,
				  budget.farming_urgent_interval);
	telemetry.set(AITrace::AI7::state_budget_farming_enabled, budget.farming_enabled);
	telemetry.set(AITrace::AI7::state_budget_farming_protection_enabled,
				  budget.farming_protection_enabled);
	telemetry.set(AITrace::AI7::state_budget_farming_maintenance_clearing_enabled,
				  budget.farming_maintenance_clearing_enabled);
	telemetry.set(AITrace::AI7::state_budget_farming_resource_preserving_circulation_enabled,
				  budget.farming_resource_preserving_circulation_enabled);
	telemetry.set(AITrace::AI7::state_budget_farming_wheat_invasion_clearing_enabled,
				  budget.farming_wheat_invasion_clearing_enabled);
	telemetry.set(AITrace::AI7::state_budget_farming_wood_firebreak_enabled,
				  budget.farming_wood_firebreak_enabled);
	telemetry.set(AITrace::AI7::state_budget_farming_proactive_clearing_enabled,
				  budget.farming_proactive_clearing_enabled);
	telemetry.set(AITrace::AI7::state_budget_farming_urgent, budget.farming_urgent);
	telemetry.set(AITrace::AI7::state_budget_farming_wood_pressure, budget.farming_wood_pressure);
	telemetry.set(AITrace::AI7::state_budget_farming_minimum_wood_fertility,
				  budget.farming_minimum_wood_fertility);
	telemetry.set(AITrace::AI7::state_budget_farming_allow_proactive_clearing,
				  budget.farming_allow_proactive_clearing);
	telemetry.set(AITrace::AI7::state_budget_farming_clearing_for_placement,
				  budget.farming_clearing_for_placement);
	telemetry.set(AITrace::AI7::state_budget_farming_min_workers_for_clearing,
				  budget.farming_min_workers_for_clearing);
	telemetry.set(AITrace::AI7::state_budget_farming_clearing_cooldown,
				  budget.farming_clearing_cooldown);
	telemetry.set(AITrace::AI7::state_budget_farming_clearing_duration,
				  budget.farming_clearing_duration);
	telemetry.set(AITrace::AI7::state_budget_farming_clearing_quota, budget.farming_clearing_quota);
	telemetry.set(AITrace::AI7::state_budget_farming_management_radius,
				  budget.farming_management_radius);
	telemetry.set(AITrace::AI7::state_budget_farming_wheat_fertility_min,
				  budget.farming_wheat_fertility_min);
	telemetry.set(AITrace::AI7::state_budget_farming_wood_fertility_base_percent,
				  budget.farming_wood_fertility_base_percent);
	telemetry.set(AITrace::AI7::state_budget_farming_wood_fertility_pressure_percent,
				  budget.farming_wood_fertility_pressure_percent);
	telemetry.set(AITrace::AI7::state_budget_farming_wood_pressure_base,
				  budget.farming_wood_pressure_base);
	telemetry.set(AITrace::AI7::state_budget_farming_wood_pressure_space_divisor,
				  budget.farming_wood_pressure_space_divisor);
	telemetry.set(AITrace::AI7::state_budget_farming_wood_pressure_supply_divisor,
				  budget.farming_wood_pressure_supply_divisor);
	telemetry.set(AITrace::AI7::state_budget_farming_wood_pressure_construction_divisor,
				  budget.farming_wood_pressure_construction_divisor);
	telemetry.set(AITrace::AI7::state_budget_farming_wood_pressure_growth_divisor,
				  budget.farming_wood_pressure_growth_divisor);
	telemetry.set(AITrace::AI7::state_budget_farming_economic_envelope_radius,
				  budget.farming_economic_envelope_radius);
	telemetry.set(AITrace::AI7::state_budget_priority_inns, budget.priority_inns);
	telemetry.set(AITrace::AI7::state_budget_priority_swarms, budget.priority_swarms);
	telemetry.set(AITrace::AI7::state_budget_priority_barracks, budget.priority_barracks);
	telemetry.set(AITrace::AI7::state_budget_priority_schools, budget.priority_schools);
	telemetry.set(AITrace::AI7::state_budget_priority_pools, budget.priority_pools);
	telemetry.set(AITrace::AI7::state_budget_priority_racetracks, budget.priority_racetracks);
	telemetry.set(AITrace::AI7::state_budget_priority_hospitals, budget.priority_hospitals);
	telemetry.set(AITrace::AI7::state_budget_priority_towers, budget.priority_towers);
	telemetry.set(AITrace::AI7::state_budget_tactical_target_team, budget.tactical_target_team);
	telemetry.set(AITrace::AI7::state_budget_tactical_dig_out_team, budget.tactical_dig_out_team);
	telemetry.set(AITrace::AI7::state_budget_tactical_target_gid, budget.tactical_target_gid);
	telemetry.set(AITrace::AI7::state_budget_tactical_target_x, budget.tactical_target_x);
	telemetry.set(AITrace::AI7::state_budget_tactical_target_y, budget.tactical_target_y);
	telemetry.set(AITrace::AI7::state_budget_tactical_candidate_score,
				  budget.tactical_candidate_score);
	telemetry.set(AITrace::AI7::state_budget_tactical_requested_force,
				  budget.tactical_requested_force);
	telemetry.set(AITrace::AI7::state_budget_tactical_review_interval,
				  budget.tactical_review_interval);
	telemetry.set(AITrace::AI7::state_budget_tactics_enabled, budget.tactics_enabled);
	telemetry.set(AITrace::AI7::state_budget_tactical_flag_level, budget.tactical_flag_level);
	telemetry.set(AITrace::AI7::state_budget_tactical_siege_radius, budget.tactical_siege_radius);
	telemetry.set(AITrace::AI7::state_budget_raid_flag_radius, budget.raid_flag_radius);
	telemetry.set(AITrace::AI7::state_budget_tactical_stall_ticks, budget.tactical_stall_ticks);
	telemetry.set(AITrace::AI7::state_budget_tactical_quarantine_enabled,
				  budget.tactical_quarantine_enabled);
	telemetry.set(AITrace::AI7::state_budget_tactical_quarantine_ticks,
				  budget.tactical_quarantine_ticks);
	telemetry.set(AITrace::AI7::state_campaign_state, campaign.state);
	telemetry.set(AITrace::AI7::state_campaign_target_team, campaign.target_team);
	telemetry.set(AITrace::AI7::state_campaign_started_tick, campaign.started_tick);
	telemetry.set(AITrace::AI7::state_campaign_last_progress_tick, campaign.last_progress_tick);
	telemetry.set(AITrace::AI7::state_campaign_last_target_buildings,
				  campaign.last_target_buildings);
	telemetry.set(AITrace::AI7::state_campaign_buildings_destroyed, campaign.buildings_destroyed);
	telemetry.set(AITrace::AI7::state_campaign_cooldown_until, campaign.cooldown_until);
	telemetry.set(AITrace::AI7::state_offense_diagnostics_tick, offense_diagnostics.tick);
	telemetry.set(AITrace::AI7::state_offense_diagnostics_eligibleWarriors,
				  offense_diagnostics.eligibleWarriors);
	telemetry.set(AITrace::AI7::state_offense_diagnostics_openTrainingSlots,
				  offense_diagnostics.openTrainingSlots);
	telemetry.set(AITrace::AI7::state_offense_diagnostics_buildingCandidates,
				  offense_diagnostics.buildingCandidates);
	telemetry.set(AITrace::AI7::state_offense_diagnostics_viableBuildings,
				  offense_diagnostics.viableBuildings);
	telemetry.set(AITrace::AI7::state_offense_diagnostics_clusterCandidates,
				  offense_diagnostics.clusterCandidates);
	telemetry.set(AITrace::AI7::state_offense_diagnostics_viableClusters,
				  offense_diagnostics.viableClusters);
	telemetry.set(AITrace::AI7::state_offense_diagnostics_bestScore, offense_diagnostics.bestScore);
	telemetry.set(AITrace::AI7::state_posture_utilities_0, posture_utilities[0]);
	telemetry.set(AITrace::AI7::state_posture_utilities_1, posture_utilities[1]);
	telemetry.set(AITrace::AI7::state_posture_utilities_2, posture_utilities[2]);
	telemetry.set(AITrace::AI7::state_posture_utilities_3, posture_utilities[3]);
	telemetry.set(AITrace::AI7::state_posture_utilities_4, posture_utilities[4]);
	telemetry.set(AITrace::AI7::state_posture_utilities_5, posture_utilities[5]);
	telemetry.set(AITrace::AI7::state_posture_utilities_6, posture_utilities[6]);
	telemetry.set(AITrace::AI7::state_policy_bids_0_utility, policy_bids[0].utility);
	telemetry.set(AITrace::AI7::state_policy_bids_0_construction_sites,
				  policy_bids[0].construction_sites);
	telemetry.set(AITrace::AI7::state_policy_bids_0_desired_inns, policy_bids[0].desired_inns);
	telemetry.set(AITrace::AI7::state_policy_bids_0_desired_swarms, policy_bids[0].desired_swarms);
	telemetry.set(AITrace::AI7::state_policy_bids_0_desired_barracks,
				  policy_bids[0].desired_barracks);
	telemetry.set(AITrace::AI7::state_policy_bids_0_desired_schools,
				  policy_bids[0].desired_schools);
	telemetry.set(AITrace::AI7::state_policy_bids_0_desired_pools, policy_bids[0].desired_pools);
	telemetry.set(AITrace::AI7::state_policy_bids_0_desired_racetracks,
				  policy_bids[0].desired_racetracks);
	telemetry.set(AITrace::AI7::state_policy_bids_0_desired_hospitals,
				  policy_bids[0].desired_hospitals);
	telemetry.set(AITrace::AI7::state_policy_bids_0_desired_towers, policy_bids[0].desired_towers);
	telemetry.set(AITrace::AI7::state_policy_bids_0_swarm_workers, policy_bids[0].swarm_workers);
	telemetry.set(AITrace::AI7::state_policy_bids_0_worker_ratio, policy_bids[0].worker_ratio);
	telemetry.set(AITrace::AI7::state_policy_bids_0_explorer_ratio, policy_bids[0].explorer_ratio);
	telemetry.set(AITrace::AI7::state_policy_bids_0_warrior_ratio, policy_bids[0].warrior_ratio);
	telemetry.set(AITrace::AI7::state_policy_bids_0_desired_explorers,
				  policy_bids[0].desired_explorers);
	telemetry.set(AITrace::AI7::state_policy_bids_0_desired_warriors,
				  policy_bids[0].desired_warriors);
	telemetry.set(AITrace::AI7::state_policy_bids_0_defense_reserve,
				  policy_bids[0].defense_reserve);
	telemetry.set(AITrace::AI7::state_policy_bids_0_attack_flags, policy_bids[0].attack_flags);
	telemetry.set(AITrace::AI7::state_policy_bids_0_attack_units, policy_bids[0].attack_units);
	telemetry.set(AITrace::AI7::state_policy_bids_0_request_upgrades,
				  policy_bids[0].request_upgrades);
	telemetry.set(AITrace::AI7::state_policy_bids_1_utility, policy_bids[1].utility);
	telemetry.set(AITrace::AI7::state_policy_bids_1_construction_sites,
				  policy_bids[1].construction_sites);
	telemetry.set(AITrace::AI7::state_policy_bids_1_desired_inns, policy_bids[1].desired_inns);
	telemetry.set(AITrace::AI7::state_policy_bids_1_desired_swarms, policy_bids[1].desired_swarms);
	telemetry.set(AITrace::AI7::state_policy_bids_1_desired_barracks,
				  policy_bids[1].desired_barracks);
	telemetry.set(AITrace::AI7::state_policy_bids_1_desired_schools,
				  policy_bids[1].desired_schools);
	telemetry.set(AITrace::AI7::state_policy_bids_1_desired_pools, policy_bids[1].desired_pools);
	telemetry.set(AITrace::AI7::state_policy_bids_1_desired_racetracks,
				  policy_bids[1].desired_racetracks);
	telemetry.set(AITrace::AI7::state_policy_bids_1_desired_hospitals,
				  policy_bids[1].desired_hospitals);
	telemetry.set(AITrace::AI7::state_policy_bids_1_desired_towers, policy_bids[1].desired_towers);
	telemetry.set(AITrace::AI7::state_policy_bids_1_swarm_workers, policy_bids[1].swarm_workers);
	telemetry.set(AITrace::AI7::state_policy_bids_1_worker_ratio, policy_bids[1].worker_ratio);
	telemetry.set(AITrace::AI7::state_policy_bids_1_explorer_ratio, policy_bids[1].explorer_ratio);
	telemetry.set(AITrace::AI7::state_policy_bids_1_warrior_ratio, policy_bids[1].warrior_ratio);
	telemetry.set(AITrace::AI7::state_policy_bids_1_desired_explorers,
				  policy_bids[1].desired_explorers);
	telemetry.set(AITrace::AI7::state_policy_bids_1_desired_warriors,
				  policy_bids[1].desired_warriors);
	telemetry.set(AITrace::AI7::state_policy_bids_1_defense_reserve,
				  policy_bids[1].defense_reserve);
	telemetry.set(AITrace::AI7::state_policy_bids_1_attack_flags, policy_bids[1].attack_flags);
	telemetry.set(AITrace::AI7::state_policy_bids_1_attack_units, policy_bids[1].attack_units);
	telemetry.set(AITrace::AI7::state_policy_bids_1_request_upgrades,
				  policy_bids[1].request_upgrades);
	telemetry.set(AITrace::AI7::state_policy_bids_2_utility, policy_bids[2].utility);
	telemetry.set(AITrace::AI7::state_policy_bids_2_construction_sites,
				  policy_bids[2].construction_sites);
	telemetry.set(AITrace::AI7::state_policy_bids_2_desired_inns, policy_bids[2].desired_inns);
	telemetry.set(AITrace::AI7::state_policy_bids_2_desired_swarms, policy_bids[2].desired_swarms);
	telemetry.set(AITrace::AI7::state_policy_bids_2_desired_barracks,
				  policy_bids[2].desired_barracks);
	telemetry.set(AITrace::AI7::state_policy_bids_2_desired_schools,
				  policy_bids[2].desired_schools);
	telemetry.set(AITrace::AI7::state_policy_bids_2_desired_pools, policy_bids[2].desired_pools);
	telemetry.set(AITrace::AI7::state_policy_bids_2_desired_racetracks,
				  policy_bids[2].desired_racetracks);
	telemetry.set(AITrace::AI7::state_policy_bids_2_desired_hospitals,
				  policy_bids[2].desired_hospitals);
	telemetry.set(AITrace::AI7::state_policy_bids_2_desired_towers, policy_bids[2].desired_towers);
	telemetry.set(AITrace::AI7::state_policy_bids_2_swarm_workers, policy_bids[2].swarm_workers);
	telemetry.set(AITrace::AI7::state_policy_bids_2_worker_ratio, policy_bids[2].worker_ratio);
	telemetry.set(AITrace::AI7::state_policy_bids_2_explorer_ratio, policy_bids[2].explorer_ratio);
	telemetry.set(AITrace::AI7::state_policy_bids_2_warrior_ratio, policy_bids[2].warrior_ratio);
	telemetry.set(AITrace::AI7::state_policy_bids_2_desired_explorers,
				  policy_bids[2].desired_explorers);
	telemetry.set(AITrace::AI7::state_policy_bids_2_desired_warriors,
				  policy_bids[2].desired_warriors);
	telemetry.set(AITrace::AI7::state_policy_bids_2_defense_reserve,
				  policy_bids[2].defense_reserve);
	telemetry.set(AITrace::AI7::state_policy_bids_2_attack_flags, policy_bids[2].attack_flags);
	telemetry.set(AITrace::AI7::state_policy_bids_2_attack_units, policy_bids[2].attack_units);
	telemetry.set(AITrace::AI7::state_policy_bids_2_request_upgrades,
				  policy_bids[2].request_upgrades);
	telemetry.set(AITrace::AI7::state_policy_bids_3_utility, policy_bids[3].utility);
	telemetry.set(AITrace::AI7::state_policy_bids_3_construction_sites,
				  policy_bids[3].construction_sites);
	telemetry.set(AITrace::AI7::state_policy_bids_3_desired_inns, policy_bids[3].desired_inns);
	telemetry.set(AITrace::AI7::state_policy_bids_3_desired_swarms, policy_bids[3].desired_swarms);
	telemetry.set(AITrace::AI7::state_policy_bids_3_desired_barracks,
				  policy_bids[3].desired_barracks);
	telemetry.set(AITrace::AI7::state_policy_bids_3_desired_schools,
				  policy_bids[3].desired_schools);
	telemetry.set(AITrace::AI7::state_policy_bids_3_desired_pools, policy_bids[3].desired_pools);
	telemetry.set(AITrace::AI7::state_policy_bids_3_desired_racetracks,
				  policy_bids[3].desired_racetracks);
	telemetry.set(AITrace::AI7::state_policy_bids_3_desired_hospitals,
				  policy_bids[3].desired_hospitals);
	telemetry.set(AITrace::AI7::state_policy_bids_3_desired_towers, policy_bids[3].desired_towers);
	telemetry.set(AITrace::AI7::state_policy_bids_3_swarm_workers, policy_bids[3].swarm_workers);
	telemetry.set(AITrace::AI7::state_policy_bids_3_worker_ratio, policy_bids[3].worker_ratio);
	telemetry.set(AITrace::AI7::state_policy_bids_3_explorer_ratio, policy_bids[3].explorer_ratio);
	telemetry.set(AITrace::AI7::state_policy_bids_3_warrior_ratio, policy_bids[3].warrior_ratio);
	telemetry.set(AITrace::AI7::state_policy_bids_3_desired_explorers,
				  policy_bids[3].desired_explorers);
	telemetry.set(AITrace::AI7::state_policy_bids_3_desired_warriors,
				  policy_bids[3].desired_warriors);
	telemetry.set(AITrace::AI7::state_policy_bids_3_defense_reserve,
				  policy_bids[3].defense_reserve);
	telemetry.set(AITrace::AI7::state_policy_bids_3_attack_flags, policy_bids[3].attack_flags);
	telemetry.set(AITrace::AI7::state_policy_bids_3_attack_units, policy_bids[3].attack_units);
	telemetry.set(AITrace::AI7::state_policy_bids_3_request_upgrades,
				  policy_bids[3].request_upgrades);
	telemetry.set(AITrace::AI7::state_policy_bids_4_utility, policy_bids[4].utility);
	telemetry.set(AITrace::AI7::state_policy_bids_4_construction_sites,
				  policy_bids[4].construction_sites);
	telemetry.set(AITrace::AI7::state_policy_bids_4_desired_inns, policy_bids[4].desired_inns);
	telemetry.set(AITrace::AI7::state_policy_bids_4_desired_swarms, policy_bids[4].desired_swarms);
	telemetry.set(AITrace::AI7::state_policy_bids_4_desired_barracks,
				  policy_bids[4].desired_barracks);
	telemetry.set(AITrace::AI7::state_policy_bids_4_desired_schools,
				  policy_bids[4].desired_schools);
	telemetry.set(AITrace::AI7::state_policy_bids_4_desired_pools, policy_bids[4].desired_pools);
	telemetry.set(AITrace::AI7::state_policy_bids_4_desired_racetracks,
				  policy_bids[4].desired_racetracks);
	telemetry.set(AITrace::AI7::state_policy_bids_4_desired_hospitals,
				  policy_bids[4].desired_hospitals);
	telemetry.set(AITrace::AI7::state_policy_bids_4_desired_towers, policy_bids[4].desired_towers);
	telemetry.set(AITrace::AI7::state_policy_bids_4_swarm_workers, policy_bids[4].swarm_workers);
	telemetry.set(AITrace::AI7::state_policy_bids_4_worker_ratio, policy_bids[4].worker_ratio);
	telemetry.set(AITrace::AI7::state_policy_bids_4_explorer_ratio, policy_bids[4].explorer_ratio);
	telemetry.set(AITrace::AI7::state_policy_bids_4_warrior_ratio, policy_bids[4].warrior_ratio);
	telemetry.set(AITrace::AI7::state_policy_bids_4_desired_explorers,
				  policy_bids[4].desired_explorers);
	telemetry.set(AITrace::AI7::state_policy_bids_4_desired_warriors,
				  policy_bids[4].desired_warriors);
	telemetry.set(AITrace::AI7::state_policy_bids_4_defense_reserve,
				  policy_bids[4].defense_reserve);
	telemetry.set(AITrace::AI7::state_policy_bids_4_attack_flags, policy_bids[4].attack_flags);
	telemetry.set(AITrace::AI7::state_policy_bids_4_attack_units, policy_bids[4].attack_units);
	telemetry.set(AITrace::AI7::state_policy_bids_4_request_upgrades,
				  policy_bids[4].request_upgrades);
	telemetry.set(AITrace::AI7::state_policy_bids_5_utility, policy_bids[5].utility);
	telemetry.set(AITrace::AI7::state_policy_bids_5_construction_sites,
				  policy_bids[5].construction_sites);
	telemetry.set(AITrace::AI7::state_policy_bids_5_desired_inns, policy_bids[5].desired_inns);
	telemetry.set(AITrace::AI7::state_policy_bids_5_desired_swarms, policy_bids[5].desired_swarms);
	telemetry.set(AITrace::AI7::state_policy_bids_5_desired_barracks,
				  policy_bids[5].desired_barracks);
	telemetry.set(AITrace::AI7::state_policy_bids_5_desired_schools,
				  policy_bids[5].desired_schools);
	telemetry.set(AITrace::AI7::state_policy_bids_5_desired_pools, policy_bids[5].desired_pools);
	telemetry.set(AITrace::AI7::state_policy_bids_5_desired_racetracks,
				  policy_bids[5].desired_racetracks);
	telemetry.set(AITrace::AI7::state_policy_bids_5_desired_hospitals,
				  policy_bids[5].desired_hospitals);
	telemetry.set(AITrace::AI7::state_policy_bids_5_desired_towers, policy_bids[5].desired_towers);
	telemetry.set(AITrace::AI7::state_policy_bids_5_swarm_workers, policy_bids[5].swarm_workers);
	telemetry.set(AITrace::AI7::state_policy_bids_5_worker_ratio, policy_bids[5].worker_ratio);
	telemetry.set(AITrace::AI7::state_policy_bids_5_explorer_ratio, policy_bids[5].explorer_ratio);
	telemetry.set(AITrace::AI7::state_policy_bids_5_warrior_ratio, policy_bids[5].warrior_ratio);
	telemetry.set(AITrace::AI7::state_policy_bids_5_desired_explorers,
				  policy_bids[5].desired_explorers);
	telemetry.set(AITrace::AI7::state_policy_bids_5_desired_warriors,
				  policy_bids[5].desired_warriors);
	telemetry.set(AITrace::AI7::state_policy_bids_5_defense_reserve,
				  policy_bids[5].defense_reserve);
	telemetry.set(AITrace::AI7::state_policy_bids_5_attack_flags, policy_bids[5].attack_flags);
	telemetry.set(AITrace::AI7::state_policy_bids_5_attack_units, policy_bids[5].attack_units);
	telemetry.set(AITrace::AI7::state_policy_bids_5_request_upgrades,
				  policy_bids[5].request_upgrades);
	telemetry.set(AITrace::AI7::state_policy_bids_6_utility, policy_bids[6].utility);
	telemetry.set(AITrace::AI7::state_policy_bids_6_construction_sites,
				  policy_bids[6].construction_sites);
	telemetry.set(AITrace::AI7::state_policy_bids_6_desired_inns, policy_bids[6].desired_inns);
	telemetry.set(AITrace::AI7::state_policy_bids_6_desired_swarms, policy_bids[6].desired_swarms);
	telemetry.set(AITrace::AI7::state_policy_bids_6_desired_barracks,
				  policy_bids[6].desired_barracks);
	telemetry.set(AITrace::AI7::state_policy_bids_6_desired_schools,
				  policy_bids[6].desired_schools);
	telemetry.set(AITrace::AI7::state_policy_bids_6_desired_pools, policy_bids[6].desired_pools);
	telemetry.set(AITrace::AI7::state_policy_bids_6_desired_racetracks,
				  policy_bids[6].desired_racetracks);
	telemetry.set(AITrace::AI7::state_policy_bids_6_desired_hospitals,
				  policy_bids[6].desired_hospitals);
	telemetry.set(AITrace::AI7::state_policy_bids_6_desired_towers, policy_bids[6].desired_towers);
	telemetry.set(AITrace::AI7::state_policy_bids_6_swarm_workers, policy_bids[6].swarm_workers);
	telemetry.set(AITrace::AI7::state_policy_bids_6_worker_ratio, policy_bids[6].worker_ratio);
	telemetry.set(AITrace::AI7::state_policy_bids_6_explorer_ratio, policy_bids[6].explorer_ratio);
	telemetry.set(AITrace::AI7::state_policy_bids_6_warrior_ratio, policy_bids[6].warrior_ratio);
	telemetry.set(AITrace::AI7::state_policy_bids_6_desired_explorers,
				  policy_bids[6].desired_explorers);
	telemetry.set(AITrace::AI7::state_policy_bids_6_desired_warriors,
				  policy_bids[6].desired_warriors);
	telemetry.set(AITrace::AI7::state_policy_bids_6_defense_reserve,
				  policy_bids[6].defense_reserve);
	telemetry.set(AITrace::AI7::state_policy_bids_6_attack_flags, policy_bids[6].attack_flags);
	telemetry.set(AITrace::AI7::state_policy_bids_6_attack_units, policy_bids[6].attack_units);
	telemetry.set(AITrace::AI7::state_policy_bids_6_request_upgrades,
				  policy_bids[6].request_upgrades);
}

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

void AIEcho::Echo::captureTelemetry()
{
	echoai->telemetry = telemetry;
	echoai->captureTelemetry();
	telemetry.set(telemetry.series->implementation == 4 ? AITrace::AI4::echo_timer
														: AITrace::AI5::echo_timer,
				  timer);
	telemetry.set(telemetry.series->implementation == 4 ? AITrace::AI4::echo_orders_count
														: AITrace::AI5::echo_orders_count,
				  orders.size());
	telemetry.set(telemetry.series->implementation == 4 ? AITrace::AI4::echo_building_orders_count
														: AITrace::AI5::echo_building_orders_count,
				  building_orders.size());
	telemetry.set(telemetry.series->implementation == 4
					  ? AITrace::AI4::echo_management_orders_count
					  : AITrace::AI5::echo_management_orders_count,
				  management_orders.size());
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
	sink.set(AITrace::AI8::module_PrioritizedBuildingAttack_target_team, enemy ? enemy->teamNumber : -1);
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
