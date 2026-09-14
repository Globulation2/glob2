#include "AIMaximaRecon.h"

#include <algorithm>
#include <climits>

namespace AIMaxima
{
namespace Recon
{

namespace
{
	int wrapCoordinate(int value, int size)
	{
		if(size<=0)
			return 0;
		value%=size;
		return value<0 ? value+size : value;
	}

	int wrappedDelta(int left, int right, int size)
	{
		int delta=left-right;
		if(size<=0)
			return delta;
		if(delta>size/2)
			delta-=size;
		else if(delta<-size/2)
			delta+=size;
		return delta;
	}

	int wrappedDistanceSquare(int x1, int y1, int x2, int y2,
		int width, int height)
	{
		const int dx=wrappedDelta(x1, x2, width);
		const int dy=wrappedDelta(y1, y2, height);
		return dx*dx+dy*dy;
	}

	struct ContactOrder
	{
		int team;
		int lastSeen;
		bool hasLocation;
	};

	bool olderContact(const ContactOrder& left, const ContactOrder& right)
	{
		if(left.lastSeen!=right.lastSeen)
			return left.lastSeen<right.lastSeen;
		return left.team<right.team;
	}

	const BuildingSighting* bestBuilding(const OpponentIntel& intel)
	{
		const BuildingSighting* best=NULL;
		for(std::map<int, BuildingSighting>::const_iterator building=
			intel.buildings.begin(); building!=intel.buildings.end(); ++building)
		{
			if(!best || building->second.lastSeenTick>best->lastSeenTick
			   || (building->second.lastSeenTick==best->lastSeenTick
				&& building->second.gid<best->gid))
				best=&building->second;
		}
		return best;
	}
}

BuildingSighting::BuildingSighting()
	: gid(-1), team(NoTeam), type(-1), x(0), y(0), width(1), height(1),
	  construction(false), lastSeenTick(-1000000), currentlyVisible(false)
{
}

BuildingSighting::BuildingSighting(int gid, int team, int type, int x, int y,
	int width, int height, bool construction, int tick)
	: gid(gid), team(team), type(type), x(x), y(y), width(width), height(height),
	  construction(construction), lastSeenTick(tick), currentlyVisible(true)
{
}

OpponentIntel::OpponentIntel()
	: alive(false), visibleWarriors(0), visibleExplorers(0),
	  visibleAttackExplorers(0), visibleBuildings(0),
	  lastObservedWarriors(0), lastObservedExplorers(0),
	  estimatedWarriors(0), estimatedExplorers(0), knownBuildings(0),
	  strategicValue(0), reachableBuildings(0), nearestBuilding(INT_MAX),
	  lastSeenTick(-1000000), lastForceSeenTick(-1000000),
	  lastWarriorSeenTick(-1000000), lastExplorerSeenTick(-1000000),
	  lastBuildingSeenTick(-1000000), lastEconomicSeenTick(-1000000),
	  lastEconomicX(0), lastEconomicY(0), confidence(0)
{
}

ReconMission::ReconMission()
	: flagId(NoFlag), targetTeam(NoTeam), frontier(true), economicWatch(false),
	  x(0), y(0),
	  createdTick(0), lastRetaskTick(0)
{
}

ReconMission::ReconMission(int flagId, int targetTeam, bool frontier,
	int x, int y, int tick, bool economicWatch)
	: flagId(flagId), targetTeam(targetTeam), frontier(frontier),
	  economicWatch(economicWatch), x(x), y(y),
	  createdTick(tick), lastRetaskTick(tick)
{
}

MissionObjective::MissionObjective()
	: targetTeam(NoTeam), frontier(true), economicWatch(false), x(0), y(0), score(0)
{
}

MissionObjective::MissionObjective(int targetTeam, bool frontier,
	int x, int y, int score, bool economicWatch)
	: targetTeam(targetTeam), frontier(frontier), economicWatch(economicWatch),
	  x(x), y(y), score(score)
{
}

ReconReport::ReconReport()
	: tick(0), visibleWarriors(0), visibleExplorers(0),
	  visibleAttackExplorers(0), visibleColonyThreat(0),
	  visibleColonyExplorerThreat(0), aliveEnemies(0), exploredPercent(0),
	  desiredMissions(0)
{
}

Program::Program()
	: memoryHorizonTicks(1), forceMemoryHoldTicks(0), staleContactAgeTicks(1),
	  forceMemoryEnabled(true)
{
	reset();
}

void Program::configure(int memoryHorizon, int forceMemoryHold,
	int staleContactAge, bool rememberForces)
{
	memoryHorizonTicks=std::max(1, memoryHorizon);
	forceMemoryHoldTicks=std::max(0,
		std::min(forceMemoryHold, memoryHorizonTicks-1));
	staleContactAgeTicks=std::max(1, staleContactAge);
	forceMemoryEnabled=rememberForces;
}

void Program::reset()
{
	current=ReconReport();
}

void Program::beginForceObservation(int tick,
	const std::vector<int>& livingTeams)
{
	current.tick=tick;
	current.visibleWarriors=0;
	current.visibleExplorers=0;
	current.visibleAttackExplorers=0;
	current.visibleColonyThreat=0;
	current.visibleColonyExplorerThreat=0;
	current.aliveEnemies=int(livingTeams.size());
	for(std::map<int, OpponentIntel>::iterator opponent=current.opponents.begin();
		opponent!=current.opponents.end(); ++opponent)
	{
		opponent->second.alive=false;
		opponent->second.visibleWarriors=0;
		opponent->second.visibleExplorers=0;
		opponent->second.visibleAttackExplorers=0;
	}
	for(std::vector<int>::const_iterator team=livingTeams.begin();
		team!=livingTeams.end(); ++team)
		current.opponents[*team].alive=true;
}

void Program::beginObservation(int tick, const std::vector<int>& livingTeams)
{
	beginForceObservation(tick, livingTeams);
	for(std::map<int, OpponentIntel>::iterator opponent=current.opponents.begin();
		opponent!=current.opponents.end(); ++opponent)
	{
		opponent->second.visibleBuildings=0;
		for(std::map<int, BuildingSighting>::iterator building=
			opponent->second.buildings.begin();
			building!=opponent->second.buildings.end(); ++building)
			building->second.currentlyVisible=false;
	}
}

void Program::observeUnit(int team, bool warrior, bool explorer,
	bool attackExplorer, bool colonyThreat, bool colonyExplorerThreat)
{
	OpponentIntel& intel=current.opponents[team];
	intel.alive=true;
	if(warrior)
	{
		intel.visibleWarriors+=1;
		current.visibleWarriors+=1;
	}
	if(explorer)
	{
		intel.visibleExplorers+=1;
		current.visibleExplorers+=1;
	}
	if(attackExplorer)
	{
		intel.visibleAttackExplorers+=1;
		current.visibleAttackExplorers+=1;
	}
	if(colonyThreat)
		current.visibleColonyThreat+=1;
	if(colonyExplorerThreat)
		current.visibleColonyExplorerThreat+=1;
}

void Program::observeEconomicActivity(int team, int x, int y)
{
	OpponentIntel& intel=current.opponents[team];
	intel.alive=true;
	intel.lastEconomicSeenTick=current.tick;
	intel.lastEconomicX=x;
	intel.lastEconomicY=y;
}

void Program::observeBuilding(const BuildingSighting& sighting)
{
	OpponentIntel& intel=current.opponents[sighting.team];
	intel.alive=true;
	std::map<int, BuildingSighting>::iterator existing=
		intel.buildings.find(sighting.gid);
	if(existing==intel.buildings.end())
		intel.buildings[sighting.gid]=sighting;
	else
	{
		existing->second=sighting;
		existing->second.currentlyVisible=true;
	}
}

void Program::confirmBuildingAbsent(int team, int gid)
{
	std::map<int, OpponentIntel>::iterator opponent=current.opponents.find(team);
	if(opponent!=current.opponents.end())
		opponent->second.buildings.erase(gid);
}

void Program::finishForceObservation()
{
	for(std::map<int, OpponentIntel>::iterator opponent=current.opponents.begin();
		opponent!=current.opponents.end(); ++opponent)
	{
		OpponentIntel& intel=opponent->second;
		if(!intel.alive)
		{
			intel.visibleWarriors=0;
			intel.visibleExplorers=0;
			intel.visibleAttackExplorers=0;
			intel.estimatedWarriors=0;
			intel.estimatedExplorers=0;
			intel.confidence=0;
			continue;
		}
		if(!forceMemoryEnabled)
		{
			intel.lastObservedWarriors=intel.visibleWarriors;
			intel.lastObservedExplorers=intel.visibleExplorers;
			intel.lastWarriorSeenTick=current.tick;
			intel.lastExplorerSeenTick=current.tick;
			intel.estimatedWarriors=intel.visibleWarriors;
			intel.estimatedExplorers=intel.visibleExplorers;
			intel.confidence=(intel.visibleWarriors>0 || intel.visibleExplorers>0)
				? 100 : 0;
			continue;
		}
		const int rememberedWarriors=weightedEstimate(intel.lastObservedWarriors,
			current.tick-intel.lastWarriorSeenTick, forceMemoryHoldTicks,
			memoryHorizonTicks);
		const int rememberedExplorers=weightedEstimate(intel.lastObservedExplorers,
			current.tick-intel.lastExplorerSeenTick, forceMemoryHoldTicks,
			memoryHorizonTicks);
		// A lone straggler must not erase a recently observed army. Refresh the
		// peak only when the new observation meets the already-decayed estimate.
		if(intel.visibleWarriors>=rememberedWarriors && intel.visibleWarriors>0)
		{
			intel.lastWarriorSeenTick=current.tick;
			intel.lastObservedWarriors=intel.visibleWarriors;
		}
		if(intel.visibleExplorers>=rememberedExplorers && intel.visibleExplorers>0)
		{
			intel.lastExplorerSeenTick=current.tick;
			intel.lastObservedExplorers=intel.visibleExplorers;
		}
		if(intel.visibleWarriors>0 || intel.visibleExplorers>0)
			intel.lastForceSeenTick=current.tick;
		if(intel.visibleWarriors>0 || intel.visibleExplorers>0)
			intel.lastSeenTick=current.tick;
		intel.confidence=confidenceForAge(current.tick-intel.lastSeenTick,
			memoryHorizonTicks);
		intel.estimatedWarriors=std::max(intel.visibleWarriors,
			weightedEstimate(intel.lastObservedWarriors,
				current.tick-intel.lastWarriorSeenTick, forceMemoryHoldTicks,
				memoryHorizonTicks));
		intel.estimatedExplorers=std::max(intel.visibleExplorers,
			weightedEstimate(intel.lastObservedExplorers,
				current.tick-intel.lastExplorerSeenTick, forceMemoryHoldTicks,
				memoryHorizonTicks));
	}
}

void Program::finishObservation()
{
	for(std::map<int, OpponentIntel>::iterator opponent=current.opponents.begin();
		opponent!=current.opponents.end(); ++opponent)
	{
		OpponentIntel& intel=opponent->second;
		intel.visibleBuildings=0;
		intel.knownBuildings=int(intel.buildings.size());
		for(std::map<int, BuildingSighting>::const_iterator building=
			intel.buildings.begin(); building!=intel.buildings.end(); ++building)
			if(building->second.currentlyVisible)
				intel.visibleBuildings+=1;
		if(intel.visibleBuildings>0)
		{
			intel.lastBuildingSeenTick=current.tick;
			intel.lastSeenTick=current.tick;
		}
	}
	finishForceObservation();
}

void Program::setBuildingAssessment(int team, int strategicValue,
	int reachableBuildings, int nearestBuilding)
{
	OpponentIntel& intel=current.opponents[team];
	intel.strategicValue=strategicValue;
	intel.reachableBuildings=reachableBuildings;
	intel.nearestBuilding=nearestBuilding;
}

void Program::setExploredPercent(int value)
{
	current.exploredPercent=std::max(0, std::min(100, value));
}

void Program::setDesiredMissions(int value)
{
	current.desiredMissions=std::max(0, value);
}

const OpponentIntel* Program::opponent(int team) const
{
	std::map<int, OpponentIntel>::const_iterator found=current.opponents.find(team);
	return found==current.opponents.end() ? NULL : &found->second;
}

OpponentIntel* Program::mutableOpponent(int team)
{
	std::map<int, OpponentIntel>::iterator found=current.opponents.find(team);
	return found==current.opponents.end() ? NULL : &found->second;
}

void Program::addMission(const ReconMission& mission)
{
	current.missions.push_back(mission);
}

void Program::removeMission(int flagId)
{
	for(std::vector<ReconMission>::iterator mission=current.missions.begin();
		mission!=current.missions.end(); ++mission)
	{
		if(mission->flagId==flagId)
		{
			current.missions.erase(mission);
			return;
		}
	}
}

void Program::clearMissions()
{
	current.missions.clear();
}

int Program::confidenceForAge(int age, int memoryHorizonTicks)
{
	if(age<=0)
		return 100;
	if(age>=memoryHorizonTicks)
		return 0;
	return 100-(age*100/memoryHorizonTicks);
}

int Program::confidenceForAge(int age, int memoryHoldTicks,
	int memoryHorizonTicks)
{
	if(age<=0)
		return 100;
	if(age>=memoryHorizonTicks)
		return 0;
	if(age<=memoryHoldTicks)
		return 100;
	return 100-((age-memoryHoldTicks)*100
		/(memoryHorizonTicks-memoryHoldTicks));
}

int Program::weightedEstimate(int observation, int age,
	int memoryHoldTicks, int memoryHorizonTicks)
{
	return std::max(0, observation)
		*confidenceForAge(age, memoryHoldTicks, memoryHorizonTicks)/100;
}

int Program::staleOrUnseenEnemies(const ReconReport& report, int tick,
	int staleContactAgeTicks)
{
	int count=0;
	for(std::map<int, OpponentIntel>::const_iterator opponent=
		report.opponents.begin(); opponent!=report.opponents.end(); ++opponent)
	{
		if(opponent->second.alive
		   && (opponent->second.buildings.empty()
			|| opponent->second.lastBuildingSeenTick<0
			|| tick-opponent->second.lastBuildingSeenTick>=staleContactAgeTicks))
			count+=1;
	}
	return count;
}

int Program::desiredMissionCount(int livingEnemies, int staleOrUnseen,
	int population, bool emergency, int populationDivisor)
{
	if(emergency || livingEnemies<=0)
		return 0;
	const int extraCapacity=std::max(0, population)
		/std::max(1, populationDivisor);
	return std::min(livingEnemies,
		1+std::min(std::max(0, staleOrUnseen), extraCapacity));
}

int Program::frontierScore(int x, int y, int width, int height,
	const std::vector<unsigned char>& discovered, int radius)
{
	if(width<=0 || height<=0 || int(discovered.size())!=width*height)
		return -1;
	int unknown=0;
	for(int dy=-radius; dy<=radius; ++dy)
		for(int dx=-radius; dx<=radius; ++dx)
		{
			if(dx*dx+dy*dy>radius*radius)
				continue;
			const int nx=wrapCoordinate(x+dx, width);
			const int ny=wrapCoordinate(y+dy, height);
			if(!discovered[ny*width+nx])
				unknown+=1;
		}
	return unknown;
}

int Program::exploredPercentInRadius(int x, int y, int width, int height,
	const std::vector<unsigned char>& discovered, int radius)
{
	if(width<=0 || height<=0 || int(discovered.size())!=width*height)
		return 100;
	int total=0;
	int explored=0;
	for(int dy=-radius; dy<=radius; ++dy)
		for(int dx=-radius; dx<=radius; ++dx)
		{
			if(dx*dx+dy*dy>radius*radius)
				continue;
			const int nx=wrapCoordinate(x+dx, width);
			const int ny=wrapCoordinate(y+dy, height);
			total+=1;
			if(discovered[ny*width+nx])
				explored+=1;
		}
	return total>0 ? explored*100/total : 100;
}

std::vector<MissionObjective> Program::planObjectives(
	const ReconReport& report, int desiredMissions, int width, int height,
	const std::vector<unsigned char>& discovered, int radius)
{
	std::vector<MissionObjective> objectives;
	if(desiredMissions<=0 || width<=0 || height<=0
	   || int(discovered.size())!=width*height)
		return objectives;

	std::vector<ContactOrder> contacts;
	for(std::map<int, OpponentIntel>::const_iterator opponent=
		report.opponents.begin(); opponent!=report.opponents.end(); ++opponent)
	{
		if(!opponent->second.alive)
			continue;
		ContactOrder contact;
		contact.team=opponent->first;
		contact.hasLocation=!opponent->second.buildings.empty();
		contact.lastSeen=contact.hasLocation
			? opponent->second.lastBuildingSeenTick : -1000000;
		contacts.push_back(contact);
	}
	std::sort(contacts.begin(), contacts.end(), olderContact);

	int frontierNeeded=0;
	for(std::vector<ContactOrder>::const_iterator contact=contacts.begin();
		contact!=contacts.end() && int(objectives.size())+frontierNeeded<desiredMissions;
		++contact)
	{
		const OpponentIntel& intel=report.opponents.find(contact->team)->second;
		const BuildingSighting* building=bestBuilding(intel);
		if(!building)
		{
			frontierNeeded+=1;
			continue;
		}
		const int age=std::max(0, report.tick-contact->lastSeen);
		objectives.push_back(MissionObjective(contact->team, false,
			wrapCoordinate(building->x+building->width/2, width),
			wrapCoordinate(building->y+building->height/2, height), age+1));
	}

	while(int(objectives.size())<desiredMissions)
	{
		int bestX=-1;
		int bestY=-1;
		int bestScore=-1;
		for(int y=0; y<height; ++y)
			for(int x=0; x<width; ++x)
			{
				if(discovered[y*width+x])
					continue;
				bool onFrontier=false;
				for(int dy=-1; dy<=1 && !onFrontier; ++dy)
					for(int dx=-1; dx<=1; ++dx)
					{
						if(dx==0 && dy==0)
							continue;
						const int nx=wrapCoordinate(x+dx, width);
						const int ny=wrapCoordinate(y+dy, height);
						if(discovered[ny*width+nx])
						{
							onFrontier=true;
							break;
						}
					}
				if(!onFrontier)
					continue;
				bool overlaps=false;
				for(std::vector<MissionObjective>::const_iterator selected=
					objectives.begin(); selected!=objectives.end(); ++selected)
					if(wrappedDistanceSquare(x, y, selected->x, selected->y,
						width, height)<radius*radius*4)
					{
						overlaps=true;
						break;
					}
				if(overlaps)
					continue;
				const int score=frontierScore(x, y, width, height,
					discovered, radius);
				if(score>bestScore)
				{
					bestScore=score;
					bestX=x;
					bestY=y;
				}
			}
		if(bestX<0)
			break;
		objectives.push_back(MissionObjective(NoTeam, true,
			bestX, bestY, bestScore));
	}
	return objectives;
}

}
}
