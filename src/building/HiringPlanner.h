// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "sim/snapshot/WorldSnapshot.h"
#include "HiringJobs.h"
#include <array>
#include <cstdint>
#include <memory>
#include <vector>

struct SharedBuildingGradient;
namespace HiringPlanner
{
using Hiring::Job;
using Hiring::Key;
struct Candidate
{
	UnitRef unit;
	std::int64_t level = 0, value = 0;
};
struct List
{
	Key key;
	std::vector<Candidate> candidates;
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
	X(coordinationNs) \
	X(coordinatedTeams) \
	X(coordinationSkipped) \
	X(distanceReuses) \
	X(availableEvaluations) \
	X(jobsSkippedNoQuota) \
	X(sharedQueries) \
	X(sharedExpansions) \
	X(sharedWaitNs) \
	X(sharedQueryNs) \
	X(missingBuildingFields) \
	X(buildingsExamined) \
	X(buildingsNeedingHiring) \
	X(buildingsSkippedNoDemand) \
	X(preparationNs) \
	X(setupNs) \
	X(precheckNs) \
	X(evaluationNs) \
	X(sortOutputNs) \
	X(distanceBoundSkips) \
	X(refreshes) \
	X(jobs) \
	X(skippedJobs) \
	X(rankedCandidates) \
	X(resourcePlaneReads) \
	X(resourceEstimateFallbacks) \
	X(airQueries)
struct Metrics
{
#define HIRING_DECLARE_METRIC(name) std::uint64_t name = 0;
	HIRING_PLANNER_METRICS(HIRING_DECLARE_METRIC)
#undef HIRING_DECLARE_METRIC
	void add(const Metrics &other)
	{
#define HIRING_ADD_METRIC(name) name += other.name;
		HIRING_PLANNER_METRICS(HIRING_ADD_METRIC)
#undef HIRING_ADD_METRIC
	}
};
struct Output
{
	unsigned tick = 0, team = 0;
	Uint32 checksum = 0; // Immutable output content; prepared once by its producer.
	Metrics metrics;
	std::uint64_t ns = 0, candidateVisits = 0, retainedBytes = 0;
	std::vector<BuildingLists> buildings;
};
struct Inputs
{
	SimulationSnapshot::Handle world;
	struct Route
	{
		BuildingRef building;
		int slot;
		std::shared_ptr<SharedBuildingGradient> field;
	};
	// One immutable table per boundary, shared by every admitted team job.
	std::shared_ptr<const std::vector<Route>> routes;
	// Scalar memory bounds accumulated during the required generation lease walk.
	std::array<std::size_t, 16> outputBounds{};
};
struct Supply
{
	BuildingRef building;
	std::array<unsigned, MaterialCount> modes{}, vacancies{};
	std::array<bool, MaterialCount> consumerSpecific{};
	unsigned seats = 0, visitRank = 0;
	bool materialDemand = false;
};
// Worker-only derived bookkeeping, no live entities or copied world records.
std::vector<Supply> prepare(const Inputs &, unsigned team);
// Stateless worker planner: world records and derived planes are borrowed from
// the shared completed-tick snapshot; only job/output vectors are allocated.
class Planner
{
	bool pruneCandidates = true;
  public:
	struct Options { bool pruneCandidates = true; };
	Planner() = default;
	explicit Planner(Options options) : pruneCandidates(options.pruneCandidates) {}
	Output rank(const Inputs &, unsigned team, unsigned capacity = 128);
};
bool needsCandidates(const SimulationSnapshot::BuildingView &);
std::array<Sint32, 8> configurationFacts(const BuildingStateRecord &);
std::uint64_t configuration(const BuildingStateRecord &);
bool better(const Candidate &, const Candidate &, Job);
List shortlist(Key, std::vector<Candidate>, unsigned capacity);
} // namespace HiringPlanner
