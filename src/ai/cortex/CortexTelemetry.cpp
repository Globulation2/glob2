// SPDX-License-Identifier: GPL-3.0-or-later
// AICortex's AI telemetry capture (schema in src/ai/telemetry/AITelemetryFields.inc).
// It lives with the AI so a compile profile without the AI links without it.
#include "AITelemetryFields.h"
#include "AICortex.h"

void AICortex::captureTelemetry()
{
	telemetry.set(AITrace::AI6::state_timer, timer);
	telemetry.set(AITrace::AI6::state_pendingUpgradeType, pendingUpgradeType);
	telemetry.set(AITrace::AI6::state_pendingUpgradeUntil, pendingUpgradeUntil);
	telemetry.set(AITrace::AI6::state_flagPosture, flagPosture);
	telemetry.set(AITrace::AI6::state_offenseHoldUntil, offenseHoldUntil);
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
