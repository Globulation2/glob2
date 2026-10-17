// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "WorldSnapshot.h"
#include "ComputeExecutor.h"
#include "ChecksumSample.h"
#include <array>
#include <optional>

namespace SimulationChecksum
{
// Derive coverage from the recorded setup, never the transport or live seats.
// AI-only turn sessions and their replays must use the same policy.
bool mapChecksumsFor(const GameHeader& players);

// Schema 3: original-style field mixing over existing captured simulation facts.
Uint32 snapshotChecksum(const SimulationSnapshot::Handle& world, bool mapChecksums = false);
class Pipeline
{
    struct Work {
        SimulationSnapshot::Handle world;
        ChecksumSample result;
        Uint32 worldTick = 0;
        ComputeExecutor::Batch batch;
        std::exception_ptr error;
    };
    ComputeExecutor& executor;
    static constexpr std::size_t Capacity = ChecksumDeliveryDelay + 1;
    std::array<Work, Capacity> pending{};
    std::size_t first = 0, count = 0;
    std::optional<Uint32> lastTick;
    Uint64 worldIdentity = 0;
    std::exception_ptr failed;
public:
    static constexpr unsigned Delay = ChecksumDeliveryDelay;
    explicit Pipeline(ComputeExecutor& executor);
    ~Pipeline();
    Pipeline(const Pipeline&) = delete;
    void submit(const SimulationSnapshot::Handle& world, Uint32 sourceTick, bool mapChecksums = false);
    // Delivery uses reporting turns; executor priorities and telemetry use world time.
    // Source ticks may be network turns while worldTick is paused simulation time.
    struct Result { ChecksumSample sample; Uint32 worldTick; };
    struct Results {
        std::array<Result, Capacity> samples{};
        std::size_t count = 0;
        bool empty() const { return count == 0; }
        std::size_t size() const { return count; }
        const Result* begin() const { return samples.data(); }
        const Result* end() const { return samples.data() + count; }
        const Result& operator[](std::size_t index) const { return samples[index]; }
        const Result& front() const { return samples.front(); }
    };
    Results collect(Uint32 now, bool flush = false, std::optional<Uint32> worldNow = {});
    void reset();
    void synchronizeWorld(Uint64 identity) { if (worldIdentity && worldIdentity!=identity) reset(); }
};
}
