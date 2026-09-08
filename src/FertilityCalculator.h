// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <functional>
#include <memory>
#include <cstddef>
class Map;
namespace FertilityCalculator
{
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
    // Synchronous adapter for callers not yet migrated to scheduled jobs.
    void compute(Map& map, const ProgressCallback& progress);
}
