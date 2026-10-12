// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <optional>
#include <cstdint>
#include <SDL3/SDL.h>
namespace GAGCore::ApplicationHost {
// Owner-thread snapshot. No missing event is evidence of healthy memory/foreground.
struct ResourceObservations {
    std::optional<bool> backgrounded, memoryPressure;
    std::uint64_t lowMemoryEvents=0;
    void observe(const SDL_Event &);
};
}
