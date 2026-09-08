// SPDX-License-Identifier: GPL-3.0-or-later
#include <ApplicationHost.h>
#include <SDL.h>

namespace GAGCore::ApplicationHost
{
void wait(std::uint32_t milliseconds)
{
    if (milliseconds) SDL_Delay(milliseconds);
}
void screenChanged(const char*) {}
void simulationAdvanced(std::uint32_t) {}
void matchFrame(bool) {}
void exited(int) {}
}
