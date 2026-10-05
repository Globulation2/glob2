// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstdint>

namespace GAGCore
{
// Presentation only. Callers supply monotonic nanosecond timestamps so the
// scheduler is independent of SDL and can be tested without sleeping.
// A context starts uncapped; interactive hosts opt in to the user's preference.
class RenderFramePacer
{
public:
    void configure(int fps)
    {
        if (fps == targetFps) return;
        targetFps = fps;
        periodNs = fps > 0 ? (NANOSECONDS_PER_SECOND + fps - 1) / fps : 0;
        reset();
    }

    void reset() { nextDrawNs = 0; }
    bool due(std::uint64_t now) const { return !periodNs || now >= nextDrawNs; }

    // Reserve a drawing opportunity before any expensive painting begins.
    bool begin(std::uint64_t now)
    {
        if (!due(now)) return false;
        // Preserve phase across sub-frame scheduling jitter. A missed full
        // interval discards the backlog; it never causes catch-up painting.
        // The ceiling is an average rate, rather than a minimum frame interval.
        if (periodNs)
            nextDrawNs = !nextDrawNs || now - nextDrawNs >= periodNs
                ? now + periodNs : nextDrawNs + periodNs;
        return true;
    }

    // Round the remaining wait up only at the millisecond host-wait boundary.
    std::uint32_t waitMilliseconds(std::uint64_t now) const
    {
        return due(now) ? 0 : static_cast<std::uint32_t>((nextDrawNs - now + 999999) / 1000000);
    }

private:
    static constexpr std::uint64_t NANOSECONDS_PER_SECOND = 1000000000;
    int targetFps = 0;
    std::uint64_t periodNs = 0, nextDrawNs = 0;
};
}
