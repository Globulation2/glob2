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
	std::vector<List> jobs;
};
// Worker-side counters; tests use them to check pruning and plane reuse.
struct Metrics
{
	std::uint64_t coordinatedTeams = 0, coordinationSkipped = 0, buildingsNeedingHiring = 0,
				  buildingsSkippedNoDemand = 0, sharedQueries = 0, missingBuildingFields = 0,
				  distanceBoundSkips = 0, rankedCandidates = 0, resourcePlaneReads = 0,
				  resourceEstimateFallbacks = 0;
};
struct Output
{
	unsigned tick = 0, team = 0;
	Uint32 checksum = 0; // Immutable output content; prepared once by its producer.
	Metrics metrics;
	std::uint64_t ns = 0, candidateVisits = 0;
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
// the shared completed-tick snapshot. Allocations hold job/output bookkeeping
// and query scratch, never a separate world copy or retained resource cache.
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
bool better(const Candidate &, const Candidate &, Job);
List shortlist(Key, std::vector<Candidate>, unsigned capacity);
} // namespace HiringPlanner
