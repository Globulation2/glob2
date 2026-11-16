// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "DeviceAdaptation.h"
#include <vector>

union SDL_Event;

// The process's step-down controller, wired to globalContainer's settings,
// the asynchronous HD artwork path and the performance telemetry.
namespace DeviceAdaptation
{
Controller &session();
// Once an application frame, before the screens see the events: folds a
// finished VRAM probe into the profile and feeds memory signals to session().
// O(events); never blocks.
void serviceFrame(const std::vector<SDL_Event> &events);
// A match or editor session starts (resets the hitch grace period).
void matchStarted();
// A presented in-game frame.
void framePresented();
// The single trim entry point: forwards to GraphicContext::trimPresentationMemory
// and, for Smallest, keeps the retired HD frames' budget at zero.
void trimPresentationMemory(Trim trim);
} // namespace DeviceAdaptation
