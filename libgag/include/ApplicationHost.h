// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstdint>

namespace GAGCore::ApplicationHost
{
// Transitional wait for legacy modal loops. The browser implementation yields
// through Asyncify until these loops become resumable application screens.
void wait(std::uint32_t milliseconds);

// Read-only diagnostics; hosts decide whether and how to publish them.
void screenChanged(const char* name);
void simulationAdvanced(std::uint32_t tick);
void matchFrame(bool paused);
void exited(int result);
}
