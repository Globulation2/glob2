// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "MovementPlanner.h"
#include "MovementDiagnostics.h"
#include "ComputeExecutor.h"
#include <deque>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <fstream>
class Game;
class Unit;
namespace MovementPlanning
{
// Stage A observes the reference trajectory. Nothing here is authoritative,
// serialized, or included in checksums, and no suggestion moves a live unit.
class Shadow
{
public:
    struct Metrics
    {
        Uint64 admitted=0, published=0, stable=0, rapid=0, valid=0, missing=0;
        Uint64 positionRejected=0, taskRejected=0, generationRejected=0;
        Uint64 preferredSuccess=0, blocked=0, goalReached=0, noRoute=0, destinationRejected=0;
        Uint64 predictionErrorTicks=0, selectedRank[9]{};
        // Conditional steering agreement; unavailable/rejected suggestions are not matches.
        Uint64 agreementOpportunities=0, agreementDestinationRejected=0, agreementCancelled=0;
        Uint64 compared=0, directionAgree=0, moveStayAgree=0, bothMove=0, movingDirectionAgree=0, bothStay=0;
        Uint64 proposalMoveReferenceStay=0, proposalStayReferenceMove=0;
        Uint64 destinationCompared=0, destinationCoordinateAgree=0, observationAgeTicks=0;
        Uint64 confusion[9][9]{}, traceSamples[2]{};
        Counters reference;
    };
private:
    struct Comparison
    {
        Request request;
        std::optional<Destination> destination;
        Uint8 proposedDirection=8;
        Disposition disposition=Disposition::NoRoute;
    };
    struct Actor
    {
        Task task;
        Uint64 epoch=0, actionGeneration=0, sequence=0, pendingSequence=0;
        bool initialized=false, requested=false;
        std::optional<Suggestion> published;
        std::optional<Comparison> comparison;
        Uint64 requestedGeneration=0;
    };
    struct TeamJob
    {
        SimulationSnapshot::Handle world;
        std::vector<Request> requests;
        std::vector<Suggestion> outputs;
        Planner* planner = nullptr;
        Uint32 seed = 0;
        Uint64 activeNs = 0;
    };
    struct Batch
    {
        Uint32 due=0;
        ComputeExecutor::Batch ticket;
        std::vector<TeamJob> teams;
    };
    ComputeExecutor& executor;
    unsigned delay;
    bool agreementSweep=false;
    std::unique_ptr<std::ofstream> agreementTrace;
    Uint64 agreementTraceErrors=0;
    std::array<std::unique_ptr<Planner>,Team::MAX_COUNT> planners;
    std::map<Uint16,Actor> actors;
    std::deque<std::unique_ptr<Batch>> pending;
    std::optional<Uint32> lastBoundary;
    std::array<std::array<Metrics,unsigned(Mode::Count)>,Team::MAX_COUNT> metrics;
    Uint64 activeNs=0, admissionNs=0, waitNs=0, waits=0, maxBatches=0, peakBytes=0;
    static void run(void* context,std::size_t);
    void publish(Uint32 tick);
public:
    Shadow(ComputeExecutor& executor,unsigned delay,unsigned teams,int width,int height,bool agreementSweep=false);
    ~Shadow();
    bool planningEnabled() const { return delay != 0 || agreementSweep; }
    bool needsBoundary(Uint32 tick) const { return planningEnabled() && (!lastBoundary || *lastBoundary != tick); }
    static constexpr auto requirements = (SimulationSnapshot::Simulation & ~SimulationSnapshot::bit(SimulationSnapshot::Component::Entities)) | SimulationSnapshot::bit(SimulationSnapshot::Component::BuildingEntities) | SimulationSnapshot::bit(SimulationSnapshot::Component::UnitCores) | SimulationSnapshot::bit(SimulationSnapshot::Component::Exploration);
    void boundary(const Game& game,const SimulationSnapshot::Handle& world);
    void completeAction(const Unit& unit);
    // Pure aggregation helper, also exercises all nine move/stay outcomes in tests.
    static void recordAgreement(Metrics& metrics,unsigned proposed,unsigned reference);
    void observe(const Unit& unit); // after transitions, before the reference action
    Counters* counters(const Unit& unit);
    void drain();
    Uint64 retainedBytes() const;
    std::vector<std::pair<std::string,Uint64>> summary() const;
};
}
