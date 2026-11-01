// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

namespace AISharedRuntime
{
class Runtime;

/// Reconcile seed protection, field expansion and wood overgrowth clearance.
/// Shared by Econo and Nicowar; callers control cadence and telemetry.
void updateFarmingAreas(Runtime& runtime);
}
