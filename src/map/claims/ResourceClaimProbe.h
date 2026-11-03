// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <SDL3/SDL_stdinc.h>
#include <memory>
#include <array>
#include <string>
#include <optional>
#include "ResourceClaimDecisionToken.h"
class Game;
class Unit;
class ComputeExecutor;
namespace ResourceClaims
{
struct RouteDiagnostics;
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
        std::string deepContext; // Diagnostic JSON: source_revision,bundle_id,cohort_id,game_id.
        std::optional<Uint32> movementStartTick;
        bool movementOnly=false; // No ledger, decision sampling, or journey baselines required.
    };
    enum class LiveEvent : unsigned {
        Acquired, Released, Retargeted, HarvestBegin, Harvested, HarvestFailed,
        GoalRefreshSame, GoalRefreshChanged, GoalRefreshFailed,
        FallbackWait, FallbackRoam, FallbackRelease,
        RouteInitial, RouteRefresh, RouteRepair, RouteProbeFallback,
        PendingPath, TrafficWait, GeometryContradiction, ReanchorJoined,
        ReanchorFailed, ProbeTailFailed, ObsoleteProbe, Count
    };
    struct LiveMetrics {
        std::array<Uint64,unsigned(LiveEvent::Count)> events{};
        Uint64 moves=0,liveCost=0,journeyEligible=0,journeySampled=0,journeyAdmitted=0,journeyCompleted=0;
        Uint64 excludedCapacity=0,excludedLimit=0,untrackedMoves=0;
        Uint64 routeCompleted=0,routeCanceled=0,routeExpansions=0,routeElapsedNs=0,routeUnobserved=0,routeRestored=0;
        std::array<Uint64,4> routeStatuses{};
    };
    struct Metrics {
        Uint64 decisions=0, sampled=0, contested=0, submitted=0, completed=0;
        Uint64 excludedSampleLimit=0, excludedQueueLimit=0, excludedMissingField=0, captureBytes=0, outputBytes=0;
    };
    enum class MovementKind : unsigned { Medical, ClaimedResource, UnclaimedResource, ReturnToJob, Idle, Other, Count };
    struct MovementMetrics {
        std::array<Uint64,unsigned(MovementKind::Count)> moves{},weightedCost{};
        Uint64 totalMoves=0,totalCost=0,excludedNonNeighbor=0;
    };
    Probe(ComputeExecutor&, std::string path, Config);
    ~Probe();
    DecisionToken decision(Game&, const Unit&, const Uint16* field, Uint32 pickupAccess = UINT32_MAX);
    // Owner-stack observation only. Never shared with a diagnostic worker.
    struct DecisionOutcome {
        DecisionToken token;
        Uint64 owner=0,preTask=0,selectedTask=0;
        Uint32 tick=0,preSource=UINT32_MAX,preAccess=UINT32_MAX,selectedSource=UINT32_MAX,selectedAccess=UINT32_MAX;
        unsigned material=0; bool pickup=false,attempted=false,selected=false;
        const char* noAttempt="held_claim";
        const char* route="not_attempted";
        const char* fallback="none";
        const char* fallbackReason="none";
    };
    bool observesDecisionOutcomes() const;
    DecisionOutcome beginDecisionOutcome(const Unit&, bool pickup) const;
    void selectedDecisionOutcome(DecisionOutcome&, const Unit&) const;
    void finishDecisionOutcome(const DecisionOutcome&, const Unit&) const;
    void publish(Uint32 tick);
    void finish();
    const Metrics& metrics() const;
    // Observers never influence gameplay, consume RNG, or persist simulation state.
    void liveEvent(const Unit&, LiveEvent) const;
    void moved(const Unit&, int oldX, int oldY) const;
    void routeEvent(Uint64 owner,Uint64 task,Uint32 tick,LiveEvent) const;
    const LiveMetrics& liveMetrics() const;
    const MovementMetrics& movementMetrics() const;
    void routeWork(const RouteDiagnostics&) const;
};
}
