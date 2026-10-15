// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <mutex>
#include <stdexcept>
#include <thread>

// Opt-in correctness instrumentation, deliberately independent of simulation
// state and executor telemetry. In particular it also catches preparation done
// before submitting a job. Drain jobs before reading final totals. Observer
// removal is also safe while a failed test unwinds before its executor drains.
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
    // Disabled instrumentation pays only one atomic boolean read. While
    // enabled, the lock pins the observer for each short kernel-entry update;
    // a worker that observed an old enabled flag cannot access a dead scope.
    inline static std::atomic<bool> enabled{false};
    inline static std::mutex observerMutex;
    inline static GradientExecutionAudit* active=nullptr;
    const std::thread::id ownerThread = std::this_thread::get_id();
    struct Counter { std::atomic<std::uint64_t> owner{0}, other{0}; };
    std::array<Counter, unsigned(Kind::Count)> counters;

public:
    GradientExecutionAudit()
    {
        std::lock_guard lock(observerMutex);
        if (active)
            throw std::logic_error("Gradient execution audits cannot overlap");
        active=this;
        enabled.store(true,std::memory_order_relaxed);
    }
    ~GradientExecutionAudit()
    {
        std::lock_guard lock(observerMutex);
        active=nullptr;
        enabled.store(false,std::memory_order_relaxed);
    }
    GradientExecutionAudit(const GradientExecutionAudit&) = delete;
    GradientExecutionAudit& operator=(const GradientExecutionAudit&) = delete;

    static void record(Kind kind) noexcept
    {
        if (!enabled.load(std::memory_order_relaxed)) return;
        std::lock_guard lock(observerMutex);
        if (auto* audit=active)
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
