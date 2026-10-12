// SPDX-License-Identifier: GPL-3.0-or-later
#include <HostResourceObservations.h>
#include <limits>
namespace GAGCore::ApplicationHost {
void ResourceObservations::observe(const SDL_Event &event) {
    if(event.type==SDL_EVENT_WILL_ENTER_BACKGROUND || event.type==SDL_EVENT_DID_ENTER_BACKGROUND) backgrounded=true;
    if(event.type==SDL_EVENT_DID_ENTER_FOREGROUND) backgrounded=false;
    if(event.type==SDL_EVENT_LOW_MEMORY) {
        memoryPressure=true; // Sticky event observation: SDL exposes no pressure-cleared edge.
        if(lowMemoryEvents<std::numeric_limits<std::uint64_t>::max()) ++lowMemoryEvents;
    }
}
}
