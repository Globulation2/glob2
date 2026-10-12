// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "ResourcePolicy.h"
#include <nlohmann/json_fwd.hpp>

namespace resource_policy
{
// Observation only: no discovery, configuration, preference writes or execution.
// Memory budgets are planning values, not enforced process-RSS limits. Render
// preparation is discretionary microseconds; zero grants no discretionary work,
// and required preparation is excluded. No current runtime consumer is implied.
nlohmann::json describeResourcePlan(const ResourcePlan &);
// Marker payload for a future capability adapter's "execution_profiles" field.
// Defining this serializer does not advertise support in existing CLI commands.
nlohmann::json describeExecutionProfiles();
} // namespace resource_policy
