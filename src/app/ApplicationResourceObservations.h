// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "ResourcePolicy.h"
#include <HostResourceObservations.h>
namespace resource_policy {
std::optional<bool> onBatteryFromSdl(SDL_PowerState);
RuntimeConstraints observedRuntimeConstraints(RuntimeConstraints caller,
    const GAGCore::ApplicationHost::ResourceObservations &, SDL_PowerState);
// Owner/setup boundary only; no periodic power polling or live pool reconfiguration.
RuntimeConstraints discoverApplicationRuntimeConstraints(RuntimeConstraints caller);
}
