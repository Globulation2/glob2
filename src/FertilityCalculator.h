// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2007-2008 Bradley Arsenault

#pragma once
#include <functional>
#include <memory>
#include <cstddef>
class Map;
namespace FertilityCalculator
{
    /// Reports compute progress in [0, 1].
    using ProgressCallback = std::function<void(float)>;
    // The map must remain alive and unchanged until commit or cancellation.
    // Work is staged privately. Destruction/cancellation never changes the map.
    class Job
    {
    public:
        explicit Job(Map& map);
        ~Job();
        bool advance(std::size_t operations);
        float progress() const;
        bool ready() const;
        void commit();
    private:
        struct State;
        std::unique_ptr<State> state;
    };
    /// Computes and commits fertility synchronously for native callers.
    void compute(Map& map, const ProgressCallback& progress);
}
