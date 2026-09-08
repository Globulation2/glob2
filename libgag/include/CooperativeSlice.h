// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "CooperativeTask.h"
#include <chrono>
#include <functional>
namespace GAGCore {
// Host-side pacing for explicit jobs. A checkpoint may exceed the time budget;
// this never preempts a job or changes its result. The cap also bounds work when
// the clock has insufficient resolution. Simulation uses synchronous adapters.
class CooperativeSlice {
public:
    using Time = std::chrono::steady_clock::time_point;
    using Clock = std::function<Time()>;
    explicit CooperativeSlice(Clock clock = std::chrono::steady_clock::now,
                              std::chrono::steady_clock::duration budget = std::chrono::milliseconds(4),
                              unsigned checkpointLimit = 64)
        : clock(std::move(clock)), budget(budget), checkpointLimit(checkpointLimit)
    {
        if (!this->clock || budget <= decltype(budget)::zero() || !checkpointLimit)
            throw std::invalid_argument("Cooperative slice requires a clock and positive budgets");
    }
    bool advance(CooperativeTask& task) const {
        const auto start = clock();
        for (unsigned step = 0; step < checkpointLimit; ++step) {
            if (task.advance()) return true;
            if (clock() - start >= budget) return false;
        }
        return false;
    }
private:
    Clock clock;
    std::chrono::steady_clock::duration budget;
    unsigned checkpointLimit;
};
}
