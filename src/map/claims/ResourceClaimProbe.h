// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <SDL3/SDL_stdinc.h>
#include <memory>
#include <string>
class Game;
class Unit;
class ComputeExecutor;
namespace ResourceClaims
{
// Temporary decision-event experiment, never consulted by simulation decisions.
class Probe
{
    struct Impl;
    std::unique_ptr<Impl> impl;
public:
    struct Config {
        Uint32 sampleEvery=1000, maxSamples=10000;
        Uint64 maxOutputBytes=64ull*1024*1024;
        unsigned maxPending=8;
    };
    struct Metrics {
        Uint64 decisions=0, sampled=0, contested=0, submitted=0, completed=0;
        Uint64 excludedSampleLimit=0, excludedQueueLimit=0, excludedMissingField=0, captureBytes=0, outputBytes=0;
    };
    Probe(ComputeExecutor&, std::string path, Config);
    ~Probe();
    void decision(Game&, const Unit&, const Uint16* field, Uint32 pickupAccess = UINT32_MAX);
    void publish(Uint32 tick);
    void finish();
    const Metrics& metrics() const;
};
}
