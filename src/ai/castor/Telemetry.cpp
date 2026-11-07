// SPDX-License-Identifier: GPL-3.0-or-later
// AICastor's AI telemetry capture (schema in src/ai/telemetry/AITelemetryFields.inc).
// It lives with the AI so a compile profile without the AI links without it.
#include "AITelemetryFields.h"
#include "AICastor.h"

void AICastor::captureTelemetry()
{
	telemetry.set(AITrace::AI2::state_buildingSum_8_0, buildingSum[8][0]);
	telemetry.set(AITrace::AI2::state_buildingSum_8_1, buildingSum[8][1]);
	telemetry.set(AITrace::AI2::state_buildingSum_9_0, buildingSum[9][0]);
	telemetry.set(AITrace::AI2::state_buildingSum_9_1, buildingSum[9][1]);
	telemetry.set(AITrace::AI2::state_buildingSum_10_0, buildingSum[10][0]);
	telemetry.set(AITrace::AI2::state_buildingSum_10_1, buildingSum[10][1]);
	// Legacy telemetry slots: 11 was walls (no Castor demand), 12 was exchange.
	telemetry.set(AITrace::AI2::state_buildingSum_11_0, 0);
	telemetry.set(AITrace::AI2::state_buildingSum_11_1, 0);
	telemetry.set(AITrace::AI2::state_buildingSum_12_0, buildingSum[ExchangeResources][0]);
	telemetry.set(AITrace::AI2::state_buildingSum_12_1, buildingSum[ExchangeResources][1]);
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
