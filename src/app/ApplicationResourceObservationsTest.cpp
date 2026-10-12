// SPDX-License-Identifier: GPL-3.0-or-later
#include "Glob2Test.h"
#include "ApplicationResourceObservations.h"
using namespace resource_policy;
TEST_SUITE("ApplicationResourceObservations") {
TEST_CASE("unknown is distinct from foreground no pressure and battery saver") {
    GAGCore::ApplicationHost::ResourceObservations events;
    const auto unknown=observedRuntimeConstraints({},events,SDL_POWERSTATE_UNKNOWN);
    CHECK_FALSE(unknown.onBattery.has_value()); CHECK_FALSE(unknown.backgrounded.has_value());
    CHECK_FALSE(unknown.memoryPressure.has_value()); CHECK_FALSE(unknown.batterySaver.has_value());
    for(const auto state:{SDL_POWERSTATE_NO_BATTERY,SDL_POWERSTATE_CHARGING,SDL_POWERSTATE_CHARGED}) {
        CHECK(onBatteryFromSdl(state)==false);
    }
    CHECK(onBatteryFromSdl(SDL_POWERSTATE_ON_BATTERY)==true);
}
TEST_CASE("lifecycle edges and low memory evidence do not invent pressure recovery") {
    GAGCore::ApplicationHost::ResourceObservations events; SDL_Event event{};
    event.type=SDL_EVENT_DID_ENTER_BACKGROUND; events.observe(event); CHECK(events.backgrounded==true);
    event.type=SDL_EVENT_LOW_MEMORY; events.observe(event); CHECK(events.memoryPressure==true); CHECK(events.lowMemoryEvents==1);
    event.type=SDL_EVENT_DID_ENTER_FOREGROUND; events.observe(event); CHECK(events.backgrounded==false);
    CHECK(events.memoryPressure==true);
    RuntimeConstraints caller; caller.onBattery=false; caller.memoryPressure=false; caller.batterySaver=true;
    const auto retained=observedRuntimeConstraints(caller,events,SDL_POWERSTATE_ON_BATTERY);
    CHECK(retained.onBattery==false); CHECK(retained.memoryPressure==false); CHECK(retained.batterySaver==true);
    CHECK(retained.backgrounded==false);
}
}
