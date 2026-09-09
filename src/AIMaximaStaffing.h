/*
  Deterministic helpers for distributing Maxima's colony-wide swarm budget.
*/

#ifndef AI_MAXIMA_STAFFING_H
#define AI_MAXIMA_STAFFING_H

#include <algorithm>
#include <vector>

namespace AIMaxima
{
namespace SwarmStaffing
{

struct Candidate
{
	Candidate(int id, long long fertility) : id(id), fertility(fertility) {}
	int id;
	long long fertility;
};

struct Assignment
{
	Assignment(int id, int workers) : id(id), workers(workers) {}
	int id;
	int workers;
};

struct IdOrder
{
	bool operator()(const Candidate& left, const Candidate& right) const
	{
		return left.id<right.id;
	}
};

/// Allocate the colony budget in proportion to nearby fertile corn supply.
/// The sole producer receives the allowed budget even without nearby supply.
/// With multiple producers, zero supply receives no workers.
/// Candidate IDs only break exact ties; there is no equal-share floor.
inline std::vector<Assignment> distribute(std::vector<Candidate> candidates,
	int totalWorkers, int perBuildingCapacity)
{
	std::vector<Assignment> result;
	if(candidates.empty())
		return result;

	totalWorkers=std::max(0, totalWorkers);
	perBuildingCapacity=std::max(0, perBuildingCapacity);
	std::sort(candidates.begin(), candidates.end(), IdOrder());

	if(candidates.size()==1)
	{
		result.push_back(Assignment(candidates[0].id, std::min(totalWorkers,perBuildingCapacity)));
		return result;
	}

	const int count=int(candidates.size());
	for(int i=0; i<count; ++i)
		result.push_back(Assignment(candidates[i].id, 0));

	// D'Hondt uses raw supply rather than rank and respects engine capacity.
	// Redistribute overflow instead of losing it when the engine clamps a flag.
	// Increasing the budget never removes a previously allocated worker.
	for(int worker=0; worker<totalWorkers; ++worker)
	{
		int best=-1;
		for(int i=0; i<count; ++i)
		{
			if(candidates[i].fertility<=0 || result[i].workers>=perBuildingCapacity) continue;
			if(best<0 || candidates[i].fertility*(result[best].workers+1)
				>candidates[best].fertility*(result[i].workers+1))
				best=i;
		}
		if(best<0) break;
		++result[best].workers;
	}
	return result;
}

}
}

#endif
