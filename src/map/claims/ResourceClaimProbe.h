// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <SDL3/SDL_stdinc.h>
#include <memory>
#include <array>
#include <string>
#include <optional>
#include "ClaimReleaseReason.h"
class Unit;
class ComputeExecutor;
namespace ResourceClaims
{
struct RouteDiagnostics;
// Observer of an active claim game: live claim events, sampled journeys,
// decision outcome counters and the all-transport movement audit, written as
// a bounded compressed JSONL trace. Never consulted by simulation decisions.
class Probe
{
    struct Impl;
    std::unique_ptr<Impl> impl;
public:
    struct Config {
        // Journey sampling period and admission cap (at most 512 journeys).
        Uint32 sampleEvery=1000, maxSamples=10000;
        Uint64 maxOutputBytes=64ull*1024*1024;
        std::optional<Uint32> movementStartTick;
        bool movementOnly=false; // Movement audit only: no claim events or journeys.
    };
    enum class LiveEvent : unsigned {
        Acquired, Released, Retargeted, HarvestBegin, Harvested, HarvestFailed,
        GoalRefreshSame, GoalRefreshChanged, GoalRefreshFailed,
        FallbackWait, FallbackRoam, FallbackRelease,
        RouteInitial, RouteRefresh, RouteRepair, RouteProbeFallback,
        PendingPath, TrafficWait, GeometryContradiction, ReanchorJoined,
        ReanchorFailed, ProbeTailFailed, ObsoleteProbe, Count
    };
    // Opt-in movement/selection policy extensions (observer counts only).
    enum class PolicyExtension : unsigned { PendingGradientStep, TrafficSidestep, TrafficWaitLimit,
        DistanceFactorRejection, FallbackGradient, ClaimGradientSignal, Count };
    struct LiveMetrics {
        std::array<Uint64,unsigned(LiveEvent::Count)> events{};
        Uint64 moves=0,liveCost=0,journeyEligible=0,journeySampled=0,journeyAdmitted=0,journeyCompleted=0;
        Uint64 excludedCapacity=0,excludedLimit=0,untrackedMoves=0;
        Uint64 routeCompleted=0,routeCanceled=0,routeExpansions=0,routeElapsedNs=0,routeUnobserved=0,routeRestored=0;
        std::array<Uint64,4> routeStatuses{};
        // One count per Released event, by its reason; sums to events[Released].
        std::array<Uint64,unsigned(ReleaseReason::Count)> releaseReasons{};
        // Optional selection filters: accesses skipped, and refreshes kept
        // only because the margin rejected every other candidate.
        Uint64 terrainPrefilterRejections=0,refreshMarginRejections=0,refreshMarginKept=0;
        std::array<Uint64,unsigned(PolicyExtension::Count)> policyExtensions{};
        // claim_trigger "contention": resource decisions by mode, indexed by
        // ContentionEvent. Tests are ordinary-mode decisions with a valid
        // ordinary target; triggers are the tests that engaged claim selection.
        std::array<Uint64,4> contention{};
    };
    struct Metrics {
        Uint64 captureBytes=0, outputBytes=0;
    };
    enum class MovementKind : unsigned { Medical, ClaimedResource, UnclaimedResource, ReturnToJob, Idle, Other, Count };
    struct MovementMetrics {
        std::array<Uint64,unsigned(MovementKind::Count)> moves{},weightedCost{};
        Uint64 totalMoves=0,totalCost=0,excludedNonNeighbor=0;
    };
    Probe(ComputeExecutor&, std::string path, Config);
    ~Probe();
    // Owner-stack observation of one claim selection decision; counted only.
    struct DecisionOutcome {
        Uint64 preTask=0,selectedTask=0;
        Uint32 preAccess=UINT32_MAX,selectedAccess=UINT32_MAX;
        unsigned material=0; bool pickup=false,attempted=false,selected=false;
        const char* noAttempt="held_claim";
        const char* route="not_attempted";
        const char* fallback="none";
        const char* fallbackReason="none";
    };
    bool observesDecisionOutcomes() const;
    DecisionOutcome beginDecisionOutcome(const Unit&, bool pickup) const;
    void selectedDecisionOutcome(DecisionOutcome&, const Unit&) const;
    void finishDecisionOutcome(const DecisionOutcome&) const;
    void publish(Uint32 tick);
    void finish();
    const Metrics& metrics() const;
    // Observers never influence gameplay, consume RNG, or persist simulation state.
    void liveEvent(const Unit&, LiveEvent) const;
    // A Released live event carrying its reason.
    void liveRelease(const Unit&, ReleaseReason) const;
    enum class SelectionFilter : unsigned { TerrainComponent, RefreshMargin, RefreshMarginKept };
    void selectionFilter(SelectionFilter, Uint64 count=1) const;
    void policyExtension(PolicyExtension, Uint64 count=1) const;
    enum class ContentionEvent : unsigned { OrdinaryDecision, Test, Trigger, ClaimModeDecision };
    void contentionEvent(ContentionEvent) const;
    void moved(const Unit&, int oldX, int oldY) const;
    void routeEvent(Uint64 owner,Uint64 task,Uint32 tick,LiveEvent) const;
    const LiveMetrics& liveMetrics() const;
    const MovementMetrics& movementMetrics() const;
    void routeWork(const RouteDiagnostics&) const;
};
}
