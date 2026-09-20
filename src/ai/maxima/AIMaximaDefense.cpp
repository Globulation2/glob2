#include "AIMaximaDefense.h"

#include <algorithm>
#include <climits>
#include <deque>

namespace AIMaxima
{
namespace Defense
{

namespace
{
	const int Unreachable=-1;

	bool contains(const std::vector<int>& values, int value)
	{
		return std::find(values.begin(), values.end(), value)!=values.end();
	}

	bool overlapsMutually(const Candidate& left, const Candidate& right)
	{
		return left.mode!=right.mode
			&& contains(left.footprint, right.index)
			&& contains(right.footprint, left.index);
	}
}

Policy::Policy()
	: innerDistance(0), bandWidth(1), pathSlack(0), probeRadius(1),
	  maximumCrossSection(1), zoneRadius(0)
{
}

ModeInput::ModeInput() : mode(LandMode), width(0), height(0) {}
TeamField::TeamField() : team(-1), shortestDistance(Unreachable) {}
Candidate::Candidate()
	: mode(LandMode), index(-1), memberships(0), crossSection(INT_MAX),
	  terrainCrossSection(INT_MAX), bandOffset(INT_MAX), homeDistance(Unreachable),
	  state(CandidateUnselected)
{
}
ModeResult::ModeResult() : mode(LandMode), width(0), height(0) {}
PlanResult::PlanResult()
	: width(0), height(0), effectiveZoneCap(0), selectedCount(0)
{
}

void computeDistanceField(int width, int height,
	const std::vector<unsigned char>& walkable,
	const std::vector<int>& sources, std::vector<int>& distances)
{
	const int size=width*height;
	distances.assign(size, Unreachable);
	if(width<=0 || height<=0 || int(walkable.size())!=size)
		return;
	std::deque<int> queue;
	for(std::vector<int>::const_iterator source=sources.begin();
		source!=sources.end(); ++source)
	{
		if(*source<0 || *source>=size || !walkable[*source]
		   || distances[*source]!=Unreachable)
			continue;
		distances[*source]=0;
		queue.push_back(*source);
	}
	while(!queue.empty())
	{
		const int index=queue.front();
		queue.pop_front();
		const int x=index%width;
		const int y=index/width;
		for(int dy=-1; dy<=1; ++dy)
			for(int dx=-1; dx<=1; ++dx)
			{
				if(dx==0 && dy==0)
					continue;
				const int neighbor=((y+dy+height)%height)*width
					+((x+dx+width)%width);
				if(walkable[neighbor] && distances[neighbor]==Unreachable)
				{
					distances[neighbor]=distances[index]+1;
					queue.push_back(neighbor);
				}
			}
	}
}

bool betterCandidate(const Candidate& left, const Candidate& right)
{
	if(left.memberships!=right.memberships)
		return left.memberships>right.memberships;
	if(left.crossSection!=right.crossSection)
		return left.crossSection<right.crossSection;
	if(left.bandOffset!=right.bandOffset)
		return left.bandOffset<right.bandOffset;
	if(left.mode!=right.mode)
		return left.mode==LandMode;
	return left.index<right.index;
}

int effectiveZoneCap(int trainedWarriors, int configuredMinimum,
	int warriorsPerZone, int configuredMaximum)
{
	if(trainedWarriors<std::max(4, configuredMinimum) || warriorsPerZone<=0)
		return 0;
	int result=std::max(1, trainedWarriors/warriorsPerZone);
	if(configuredMaximum>0)
		result=std::min(result, configuredMaximum);
	return result;
}

bool amphibiousEligible(bool systemActive, bool configuredEnabled,
	int swimmingWarriors, int configuredMinimum)
{
	return systemActive && configuredEnabled
		&& swimmingWarriors>=std::max(4, configuredMinimum);
}

bool topologyRefreshRequired(unsigned int currentBuildingSignature,
	unsigned int cachedBuildingSignature, int currentEffectiveCap,
	int cachedEffectiveCap, bool currentAmphibiousEligibility,
	bool cachedAmphibiousEligibility, int tick, int lastRefreshTick,
	int refreshInterval, bool hasDiagnosticSnapshot)
{
	return !hasDiagnosticSnapshot
		|| currentBuildingSignature!=cachedBuildingSignature
		|| currentEffectiveCap!=cachedEffectiveCap
		|| currentAmphibiousEligibility!=cachedAmphibiousEligibility
		|| tick-lastRefreshTick>=refreshInterval;
}

ModeResult analyzeMode(const ModeInput& input, const Policy& policy)
{
	ModeResult result;
	result.mode=input.mode;
	result.width=input.width;
	result.height=input.height;
	result.walkable=input.walkable;
	const int size=input.width*input.height;
	result.memberships.assign(size, 0);
	result.minimumCrossSection.assign(size, INT_MAX);
	result.minimumTerrainCrossSection.assign(size, INT_MAX);
	result.qualified.assign(size, 0);
	computeDistanceField(input.width, input.height, input.walkable,
		input.homeSources, result.homeDistance);
	if(size<=0 || int(input.walkable.size())!=size)
		return result;

	const int bandMaximum=policy.innerDistance+policy.bandWidth;
	for(std::vector<EnemySources>::const_iterator enemy=input.enemies.begin();
		enemy!=input.enemies.end(); ++enemy)
	{
		TeamField field;
		field.team=enemy->team;
		field.shortestDistance=INT_MAX;
		for(std::vector<int>::const_iterator source=enemy->sources.begin();
			source!=enemy->sources.end(); ++source)
			if(*source>=0 && *source<size && result.homeDistance[*source]>=0)
				field.shortestDistance=std::min(field.shortestDistance,
					result.homeDistance[*source]);
		computeDistanceField(input.width, input.height, input.walkable,
			enemy->sources, field.enemyDistance);
		field.corridor.assign(size, 0);
		field.corridorWidth.assign(size, Unreachable);
		field.terrainWidth.assign(size, Unreachable);
		if(field.shortestDistance==INT_MAX)
		{
			field.shortestDistance=Unreachable;
			result.teams.push_back(field);
			continue;
		}
		for(int index=0; index<size; ++index)
			if(result.homeDistance[index]>=policy.innerDistance
			   && result.homeDistance[index]<=bandMaximum
			   && field.enemyDistance[index]>=0
			   && result.homeDistance[index]+field.enemyDistance[index]
				<=field.shortestDistance+policy.pathSlack)
				field.corridor[index]=1;

		std::vector<int> seen(size, -1);
		int stamp=0;
		for(int index=0; index<size; ++index)
		{
			if(!field.corridor[index])
				continue;
			++stamp;
			int corridorWidth=0;
			int terrainWidth=0;
			const int x=index%input.width;
			const int y=index/input.width;
			for(int dy=-policy.probeRadius; dy<=policy.probeRadius; ++dy)
				for(int dx=-policy.probeRadius; dx<=policy.probeRadius; ++dx)
				{
					// A configured probe may span several laps of a small map.
					// C++ remainder stays negative for negative dividends.
					const int neighbor=(((y+dy)%input.height+input.height)%input.height)
						*input.width+(((x+dx)%input.width+input.width)%input.width);
					if(result.homeDistance[neighbor]==result.homeDistance[index]
					   && seen[neighbor]!=stamp)
					{
						seen[neighbor]=stamp;
						++terrainWidth;
						if(field.corridor[neighbor])
							++corridorWidth;
					}
				}
			field.corridorWidth[index]=corridorWidth;
			field.terrainWidth[index]=terrainWidth;
			if(corridorWidth<=policy.maximumCrossSection
			   && terrainWidth<=policy.maximumCrossSection)
			{
				result.qualified[index]=1;
				++result.memberships[index];
				result.minimumCrossSection[index]=std::min(
					result.minimumCrossSection[index], corridorWidth);
				result.minimumTerrainCrossSection[index]=std::min(
					result.minimumTerrainCrossSection[index], terrainWidth);
			}
		}
		result.teams.push_back(field);
	}

	std::vector<unsigned char> visited(size, 0);
	for(int start=0; start<size; ++start)
	{
		if(!result.qualified[start] || visited[start])
			continue;
		Candidate best;
		best.mode=input.mode;
		best.index=start;
		best.memberships=result.memberships[start];
		best.crossSection=result.minimumCrossSection[start];
		best.terrainCrossSection=result.minimumTerrainCrossSection[start];
		best.homeDistance=result.homeDistance[start];
		int delta=2*best.homeDistance
			-(2*policy.innerDistance+policy.bandWidth);
		best.bandOffset=delta<0 ? -delta : delta;
		std::deque<int> component;
		component.push_back(start);
		visited[start]=1;
		while(!component.empty())
		{
			const int index=component.front();
			component.pop_front();
			Candidate candidate;
			candidate.mode=input.mode;
			candidate.index=index;
			candidate.memberships=result.memberships[index];
			candidate.crossSection=result.minimumCrossSection[index];
			candidate.terrainCrossSection=
				result.minimumTerrainCrossSection[index];
			candidate.homeDistance=result.homeDistance[index];
			delta=2*candidate.homeDistance
				-(2*policy.innerDistance+policy.bandWidth);
			candidate.bandOffset=delta<0 ? -delta : delta;
			if(betterCandidate(candidate, best))
				best=candidate;
			const int x=index%input.width;
			const int y=index/input.width;
			for(int dy=-1; dy<=1; ++dy)
				for(int dx=-1; dx<=1; ++dx)
				{
					if(dx==0 && dy==0)
						continue;
					const int neighbor=((y+dy+input.height)%input.height)
						*input.width+((x+dx+input.width)%input.width);
					if(result.qualified[neighbor] && !visited[neighbor])
					{
						visited[neighbor]=1;
						component.push_back(neighbor);
					}
				}
		}
		result.candidates.push_back(best);
	}
	std::sort(result.candidates.begin(), result.candidates.end(),
		betterCandidate);
	return result;
}

std::vector<int> buildFootprint(const ModeResult& mode, int center, int radius)
{
	std::vector<int> footprint;
	const int size=mode.width*mode.height;
	if(center<0 || center>=size || int(mode.walkable.size())!=size
	   || !mode.walkable[center])
		return footprint;
	std::vector<int> distance(size, Unreachable);
	std::deque<int> queue;
	queue.push_back(center);
	distance[center]=0;
	while(!queue.empty())
	{
		const int index=queue.front();
		queue.pop_front();
		footprint.push_back(index);
		if(distance[index]>=radius)
			continue;
		const int x=index%mode.width;
		const int y=index/mode.width;
		for(int dy=-1; dy<=1; ++dy)
			for(int dx=-1; dx<=1; ++dx)
			{
				if(dx==0 && dy==0)
					continue;
				const int neighbor=((y+dy+mode.height)%mode.height)*mode.width
					+((x+dx+mode.width)%mode.width);
				if(mode.walkable[neighbor] && distance[neighbor]==Unreachable)
				{
					distance[neighbor]=distance[index]+1;
					queue.push_back(neighbor);
				}
			}
	}
	std::sort(footprint.begin(), footprint.end());
	return footprint;
}

PlanResult combineModes(const std::vector<ModeResult>& modes,
	int effectiveZoneCap, int zoneRadius)
{
	PlanResult plan;
	plan.effectiveZoneCap=std::max(0, effectiveZoneCap);
	plan.modes=modes;
	if(modes.empty())
		return plan;
	plan.width=modes.front().width;
	plan.height=modes.front().height;
	plan.desired.assign(plan.width*plan.height, 0);
	for(size_t modeIndex=0; modeIndex<modes.size(); ++modeIndex)
		for(size_t index=0; index<modes[modeIndex].candidates.size(); ++index)
		{
			Candidate candidate=modes[modeIndex].candidates[index];
			candidate.footprint=buildFootprint(modes[modeIndex],
				candidate.index, zoneRadius);
			plan.candidates.push_back(candidate);
		}
	std::sort(plan.candidates.begin(), plan.candidates.end(), betterCandidate);
	std::vector<size_t> accepted;
	for(size_t index=0; index<plan.candidates.size(); ++index)
	{
		Candidate& candidate=plan.candidates[index];
		bool overlap=false;
		for(std::vector<size_t>::const_iterator prior=accepted.begin();
			prior!=accepted.end(); ++prior)
			if(overlapsMutually(candidate, plan.candidates[*prior]))
			{
				overlap=true;
				break;
			}
		if(overlap)
			candidate.state=CandidateRejectedOverlap;
		else if(plan.selectedCount>=plan.effectiveZoneCap)
			candidate.state=CandidateRejectedCap;
		else
		{
			candidate.state=CandidateSelected;
			accepted.push_back(index);
			++plan.selectedCount;
			for(std::vector<int>::const_iterator tile=candidate.footprint.begin();
				tile!=candidate.footprint.end(); ++tile)
				plan.desired[*tile]=1;
		}
	}
	return plan;
}

}
}
