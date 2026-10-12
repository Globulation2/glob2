// SPDX-License-Identifier: GPL-3.0-or-later
#include "ApplicationResourceObservations.h"
#include "AndroidResourceObservations.h"
#include <ApplicationHost.h>
namespace resource_policy {
std::optional<bool> onBatteryFromSdl(SDL_PowerState state) {
    switch(state) {
    case SDL_POWERSTATE_ON_BATTERY: return true;
    case SDL_POWERSTATE_NO_BATTERY: case SDL_POWERSTATE_CHARGING: case SDL_POWERSTATE_CHARGED: return false;
    default: return std::nullopt;
    }
}
RuntimeConstraints observedRuntimeConstraints(RuntimeConstraints caller,
    const GAGCore::ApplicationHost::ResourceObservations &events, SDL_PowerState power) {
    if(!caller.onBattery) caller.onBattery=onBatteryFromSdl(power);
    if(!caller.backgrounded) caller.backgrounded=events.backgrounded;
    if(!caller.memoryPressure) caller.memoryPressure=events.memoryPressure;
    // batterySaver remains independently supplied/unknown: battery use is not saver mode.
    return caller;
}
RuntimeConstraints discoverApplicationRuntimeConstraints(RuntimeConstraints caller) {
    return mergeAndroidResourceObservations(observedRuntimeConstraints(caller,GAGCore::ApplicationHost::resourceObservations(),SDL_GetPowerInfo(nullptr,nullptr)),discoverAndroidResourceObservations());
}
}
