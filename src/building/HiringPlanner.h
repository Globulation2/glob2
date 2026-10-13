// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "sim/snapshot/WorldSnapshot.h"
#include "HiringJobs.h"
#include <array>
#include <cstdint>
#include <memory>
#include <vector>

class Game;
struct SharedBuildingGradient;
namespace HiringPlanner
{
using Hiring::Job;
using Hiring::Key;
struct Candidate { UnitRef unit; std::int64_t level = 0, value = 0; bool available = false; };
enum class Pools { All, Combined, Split };
struct List
{
    Key key;
    std::vector<Candidate> combined, available, busy;
};
struct BuildingLists
{
    BuildingRef building;
    int type = -1;
    std::uint64_t configuration = 0;
    std::vector<List> jobs;
};
// Batch timers are elapsed nanoseconds, not CPU time. Candidate evaluation
// includes route work; seed/propagation timers are nested within it.
#define HIRING_PLANNER_METRICS(X) \
    X(sharedQueries) X(sharedExpansions) X(sharedWaitNs) X(sharedQueryNs) X(missingBuildingFields) \
    X(buildingsExamined) X(buildingsNeedingHiring) X(buildingsSkippedNoDemand) \
    X(setupNs) X(geographyNs) X(precheckNs) X(evaluationNs) X(sortOutputNs) \
    X(seedNs) X(propagationNs) X(fieldHits) X(fieldMisses) X(fieldCells) \
    X(terrainChanges) X(buildingOccupancyChanges) X(immobileChanges) X(forbiddenChanges) X(resourceBlockingChanges) \
    X(evictions) X(geographyChanges) X(refreshes) X(jobs) X(skippedJobs) \
    X(rankedCandidates) X(publishedResourceReads) X(privateResourceReads) X(airQueries)
struct Metrics
{
#define HIRING_DECLARE_METRIC(name) std::uint64_t name = 0;
    HIRING_PLANNER_METRICS(HIRING_DECLARE_METRIC)
#undef HIRING_DECLARE_METRIC
    void add(const Metrics& other) {
#define HIRING_ADD_METRIC(name) name += other.name;
        HIRING_PLANNER_METRICS(HIRING_ADD_METRIC)
#undef HIRING_ADD_METRIC
    }
};
struct Output
{
    unsigned tick = 0, team = 0;
    Metrics metrics;
    std::uint64_t ns = 0, candidateVisits = 0, retainedBytes = 0;
    std::vector<BuildingLists> buildings;
};
struct Inputs
{
    SimulationSnapshot::Handle world;
    struct Route { BuildingRef building; int slot; std::shared_ptr<SharedBuildingGradient> field; std::uint64_t bytes; };
    struct Supply { BuildingRef building; std::array<unsigned, MaterialCount> modes{};
        std::array<bool, MaterialCount> consumerSpecific{}; };
    std::vector<Supply> supplies;
    std::vector<Route> routes;
    std::uint64_t routeBytes = 0;
    // Derived attack includes experience, rules and auras. Captured once on
    // the owner, not recomputed by a worker through a live unit pointer.
    std::vector<std::pair<UnitRef, int>> attack;
};
Inputs capture(const Game&, SimulationSnapshot::Handle);
class Cache
{
    struct Impl;
    std::unique_ptr<Impl> impl;
public:
    static constexpr std::size_t MaxBytes = 32u * 1024 * 1024;
    explicit Cache(std::size_t budget = MaxBytes); ~Cache();
    Cache(const Cache&) = delete;
    Cache& operator=(const Cache&) = delete;
    Output rank(const Inputs&, unsigned team, unsigned capacity = 128, Pools pools = Pools::All);
    std::size_t bytes() const;
};
bool needsCandidates(const SimulationSnapshot::BuildingView&);
std::uint64_t configuration(const BuildingStateRecord&);
bool better(const Candidate&, const Candidate&, Job);
List shortlist(Key, std::vector<Candidate>, unsigned capacity, Pools = Pools::All);
}
