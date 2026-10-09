// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "GradientCosts.h"
#include "Grid.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <mutex>
#include <span>
#include <utility>

namespace gradient_kernel
{
// Execution placement only: the field encoding and publication boundary do not
// depend on this setting. Header-only consumers retain the CPU implementation.
enum class Backend
{
    Automatic,
    CPU,
    OpenCL
};
inline std::atomic<Backend> &backendSetting()
{
    static std::atomic<Backend> value{[]
                                      {
                                          const auto *name = std::getenv("GLOB2_GRADIENT_BACKEND");
                                          if (name && std::strcmp(name, "cpu") == 0)
                                              return Backend::CPU;
                                          if (name && std::strcmp(name, "opencl") == 0)
                                              return Backend::OpenCL;
                                          return Backend::Automatic;
                                      }()};
    return value;
}
inline Backend backend() { return backendSetting().load(std::memory_order_relaxed); }
inline void setBackend(Backend value) { backendSetting().store(value, std::memory_order_relaxed); }

// Workload choices are game-local; device failure overrides every class.
enum class Family { Generic, Materials, Markets, Guard, Clear, Forbidden, Count };
// Keep each native batch size separate: measured throughput is not monotonic,
// so merging ranges would let an unmeasured size inherit another size's winner.
inline constexpr std::size_t BATCH_CATEGORIES = 8;
inline std::size_t batchCategory(std::size_t count) { return std::clamp<std::size_t>(count, 1, 8) - 1; }
struct BackendSession
{
    std::array<std::atomic<Backend>, std::size_t(Family::Count) * BATCH_CATEGORIES> choices{};
    // Zero is untuned; otherwise the native variant index plus one. Game-local,
    // like backend choices, and accessed only by the accelerator.
    std::array<std::atomic<unsigned>, std::size_t(Family::Count) * BATCH_CATEGORIES> tileChoices{};
    std::atomic<bool> failed{false};
    std::array<std::mutex, std::size_t(Family::Count)*BATCH_CATEGORIES> calibrationMutexes;
    std::mutex& classMutex(Family family,std::size_t count) {
        return calibrationMutexes[std::size_t(family)*BATCH_CATEGORIES+batchCategory(count)];
    }
    std::atomic<unsigned>& tileSelection(Family family, std::size_t count)
    {
        return tileChoices[std::size_t(family) * BATCH_CATEGORIES + batchCategory(count)];
    }
    std::atomic<Backend>& selection(Family family = Family::Generic, std::size_t count = 1)
    {
        return choices[std::size_t(family) * BATCH_CATEGORIES + batchCategory(count)];
    }
};

// The owner must keep the complete cost input immutable. Retaining it prevents
// pooled storage/address reuse; variant identifies movement class/cost semantics.
// Without an identity accelerators must compare actual cost contents.
struct CostIdentity
{
    std::shared_ptr<const void> owner;
    std::uint64_t variant = 0, revision = 0;
    // True only when costAt is valid for every cell, including forbidden ones.
    // Such planes depend on terrain/movement alone, not the field's obstacles.
    bool allCells = false;
};
struct BackendRequest
{
    std::uint16_t *gradient;
    int limit;
    field::Grid grid;
    BackendSession &session;
    void *context;
    EntrySteps (*costAt)(void *, std::size_t);
    void (*cpu)(void *, std::uint16_t *);
    CostIdentity identity;
    Family family = Family::Generic;
    // Optional caller-owned CPU batch executor, also used during calibration.
    void (*cpuBatch)(std::span<const BackendRequest* const>, std::span<std::uint16_t* const>) = nullptr;
};
// An accelerator must leave the seed buffer untouched when returning false.
// Registered by the optional native implementation; absent in standalone users.
inline bool (*accelerator)(const BackendRequest &, Backend) = nullptr;
// Explicit scheduler batches own all contexts until this synchronous call ends.
inline bool (*batchAccelerator)(std::span<const BackendRequest>, Backend) = nullptr;
inline bool canBatch(const BackendSession &session)
{
    const auto choice = backend();
    return batchAccelerator && choice != Backend::CPU && !session.failed.load(std::memory_order_relaxed);
}

template <class Costs, class CPU>
bool tryAcceleratedGradient(std::uint16_t *gradient, int maxCost, field::Grid grid, BackendSession &session,
                            Costs costs, CPU cpu, CostIdentity identity = {}, Family family = Family::Generic)
{
    const auto choice = backend();
    if (!accelerator || choice == Backend::CPU || maxCost < 0)
        return false;
    if (session.failed.load(std::memory_order_relaxed))
        return false;
    struct Context
    {
        Costs costs;
        CPU cpu;
    } context{costs, cpu};
    const BackendRequest request{gradient,
                                 std::min(maxCost, COST_LIMIT),
                                 grid,
                                 session,
                                 &context,
                                 [](void *p, std::size_t i) { return static_cast<Context *>(p)->costs(i); },
                                 [](void *p, std::uint16_t *out) { static_cast<Context *>(p)->cpu(out); },
                                 std::move(identity), family};
    return accelerator(request, choice);
}
} // namespace gradient_kernel
