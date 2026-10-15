// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <stdexcept>
#include <thread>

// Opt-in correctness instrumentation, deliberately independent of simulation
// state and executor telemetry. In particular it also catches preparation done
// before submitting a job. Keep the scope alive until its jobs have drained.
class GradientExecutionAudit
{
public:
    enum class Kind { Seed, Propagation, BuildingResolve, EscapeComparison, TerrainPlane, AirSearch, TerrainSafety,
        AirFieldInitialization, AirFieldExpansion, AirGoalEnumeration, AirTraversalPlane, ImmediateAirSearch, Count };
    struct Counts
    {
        std::uint64_t owner = 0, other = 0;
    };

private:
    inline static std::atomic<GradientExecutionAudit*> active{nullptr};
    const std::thread::id ownerThread = std::this_thread::get_id();
    struct Counter { std::atomic<std::uint64_t> owner{0}, other{0}; };
    std::array<Counter, unsigned(Kind::Count)> counters;

public:
    GradientExecutionAudit()
    {
        GradientExecutionAudit* expected = nullptr;
        if (!active.compare_exchange_strong(expected, this))
            throw std::logic_error("Gradient execution audits cannot overlap");
    }
    ~GradientExecutionAudit() { active.store(nullptr); }
    GradientExecutionAudit(const GradientExecutionAudit&) = delete;
    GradientExecutionAudit& operator=(const GradientExecutionAudit&) = delete;

    static void record(Kind kind) noexcept
    {
        if (auto* audit = active.load(std::memory_order_acquire))
        {
            auto& counter = audit->counters[unsigned(kind)];
            auto& value = std::this_thread::get_id() == audit->ownerThread ? counter.owner : counter.other;
            value.fetch_add(1, std::memory_order_relaxed);
        }
    }
    Counts counts(Kind kind) const noexcept
    {
        const auto& counter = counters[unsigned(kind)];
        return {counter.owner.load(std::memory_order_relaxed), counter.other.load(std::memory_order_relaxed)};
    }
    Counts totals() const noexcept
    {
        Counts total;
        for (unsigned i = 0; i < unsigned(Kind::Count); ++i)
        {
            const auto value = counts(Kind(i));
            total.owner += value.owner;
            total.other += value.other;
        }
        return total;
    }
};
