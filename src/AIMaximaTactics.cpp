#include "AIMaximaTactics.h"

#include <algorithm>
#include <climits>
#include <cstdlib>

namespace AIMaxima
{
namespace Tactics
{

namespace
{
	int wrappedDelta(int left, int right, int size)
	{
		int delta=std::abs(left-right);
		return size>0 ? std::min(delta, size-delta) : delta;
	}

	int distanceSquare(int x1, int y1, int x2, int y2, int width, int height)
	{
		const int dx=wrappedDelta(x1, x2, width);
		const int dy=wrappedDelta(y1, y2, height);
		return dx*dx+dy*dy;
	}

	bool workerLess(const WorkerSighting& left, const WorkerSighting& right)
	{
		if(left.team!=right.team) return left.team<right.team;
		return left.gid<right.gid;
	}

	bool raidBetter(const RaidCandidate& left, const RaidCandidate& right)
	{
		if(left.score!=right.score) return left.score>right.score;
		if(left.workers!=right.workers) return left.workers>right.workers;
		if(left.team!=right.team) return left.team<right.team;
		if(left.x!=right.x) return left.x<right.x;
		return left.y<right.y;
	}
}

WorkerSighting::WorkerSighting()
	: gid(-1), team(NoTeam), x(0), y(0), tick(0), harvesting(false),
	  carrying(false), economicValue(0)
{
}

WorkerSighting::WorkerSighting(int gid, int team, int x, int y, int tick,
	bool harvesting, bool carrying, int economicValue)
	: gid(gid), team(team), x(x), y(y), tick(tick), harvesting(harvesting),
	  carrying(carrying), economicValue(economicValue)
{
}

ThreatSighting::ThreatSighting()
	: gid(-1), team(NoTeam), x(0), y(0), power(0)
{
}

ThreatSighting::ThreatSighting(int gid, int team, int x, int y, int power)
	: gid(gid), team(team), x(x), y(y), power(power)
{
}

RaidRules::RaidRules()
	: width(1), height(1), tick(0), clusterRadius(5), threatRadius(7),
	  workerMinimum(3), workerWeight(100), harvestingBonus(30),
	  carryingBonus(40), resourceWeight(10), defenderPenalty(120)
{
}

RaidCandidate::RaidCandidate()
	: team(NoTeam), x(0), y(0), tick(0), workers(0), harvesters(0),
	  carriers(0), economicValue(0), defenders(0), defenderPower(0), score(0)
{
}

Mission::Mission()
{
	reset();
}

void Mission::reset()
{
	kind=MissionNone;
	phase=PhaseIdle;
	flagId=NoFlag;
	targetTeam=NoTeam;
	targetGid=NoTarget;
	targetX=0;
	targetY=0;
	rallyX=0;
	rallyY=0;
	requestedForce=0;
	minimumForce=0;
	launchedForce=0;
	startedTick=0;
	phaseSinceTick=0;
	lastContactTick=0;
	lastProgressTick=0;
	cooldownUntil=0;
	initialTargetWorkers=0;
	candidateScore=0;
	lastTargetHp=-1;
}

void Mission::retargetSiege(int gid, int x, int y, int tick)
{
	if(gid!=targetGid)
	{
		lastTargetHp=-1;
		lastProgressTick=tick;
	}
	targetGid=gid;
	targetX=x;
	targetY=y;
}

Program::Program()
	: observationTick(0)
{
}

void Program::reset()
{
	observationTick=0;
	currentWorkers.clear();
	currentThreats.clear();
	raids.clear();
}

void Program::beginObservation(int tick)
{
	observationTick=tick;
	currentWorkers.clear();
	currentThreats.clear();
	raids.clear();
}

void Program::observeWorker(const WorkerSighting& worker)
{
	currentWorkers.push_back(worker);
}

void Program::observeThreat(const ThreatSighting& threat)
{
	currentThreats.push_back(threat);
}

void Program::replaceThreats(int tick,
	const std::vector<ThreatSighting>& threats)
{
	observationTick=tick;
	currentThreats=threats;
	refreshRaidThreats();
}

void Program::finishObservation(const RaidRules& rules)
{
	observationRules=rules;
	raids.clear();
	std::sort(currentWorkers.begin(), currentWorkers.end(), workerLess);
	const int count=static_cast<int>(currentWorkers.size());
	std::vector<int> parent(count);
	for(int i=0; i<count; ++i) parent[i]=i;
	for(int i=0; i<count; ++i)
	{
		for(int j=i+1; j<count; ++j)
		{
			if(currentWorkers[i].team!=currentWorkers[j].team)
				continue;
			if(distanceSquare(currentWorkers[i].x, currentWorkers[i].y,
				currentWorkers[j].x, currentWorkers[j].y, rules.width, rules.height)
				>rules.clusterRadius*rules.clusterRadius)
				continue;
			int rootI=i;
			while(parent[rootI]!=rootI) rootI=parent[rootI];
			int rootJ=j;
			while(parent[rootJ]!=rootJ) rootJ=parent[rootJ];
			if(rootI!=rootJ) parent[rootJ]=rootI;
		}
	}

	for(int root=0; root<count; ++root)
	{
		int actualRoot=root;
		while(parent[actualRoot]!=actualRoot) actualRoot=parent[actualRoot];
		if(actualRoot!=root)
			continue;
		std::vector<int> members;
		for(int i=0; i<count; ++i)
		{
			int candidateRoot=i;
			while(parent[candidateRoot]!=candidateRoot)
				candidateRoot=parent[candidateRoot];
			if(candidateRoot==root)
				members.push_back(i);
		}
		if(static_cast<int>(members.size())<rules.workerMinimum)
			continue;

		RaidCandidate candidate;
		candidate.team=currentWorkers[members.front()].team;
		candidate.tick=rules.tick;
		int medoid=members.front();
		int medoidDistance=INT_MAX;
		for(std::vector<int>::const_iterator possible=members.begin();
			possible!=members.end(); ++possible)
		{
			int total=0;
			for(std::vector<int>::const_iterator other=members.begin();
				other!=members.end(); ++other)
				total+=distanceSquare(currentWorkers[*possible].x,
					currentWorkers[*possible].y, currentWorkers[*other].x,
					currentWorkers[*other].y, rules.width, rules.height);
			if(total<medoidDistance || (total==medoidDistance
			   && currentWorkers[*possible].gid<currentWorkers[medoid].gid))
			{
				medoid=*possible;
				medoidDistance=total;
			}
		}
		candidate.x=currentWorkers[medoid].x;
		candidate.y=currentWorkers[medoid].y;
		candidate.workers=static_cast<int>(members.size());
		for(std::vector<int>::const_iterator member=members.begin();
			member!=members.end(); ++member)
		{
			const WorkerSighting& worker=currentWorkers[*member];
			candidate.workerGids.push_back(worker.gid);
			if(worker.harvesting) ++candidate.harvesters;
			if(worker.carrying) ++candidate.carriers;
			candidate.economicValue+=worker.economicValue;
		}
		raids.push_back(candidate);
	}
	refreshRaidThreats();
}

void Program::refreshRaidThreats()
{
	const RaidRules& rules=observationRules;
	for(size_t i=0; i<raids.size(); ++i)
	{
		RaidCandidate& candidate=raids[i];
		candidate.defenders=0;
		candidate.defenderPower=0;
		for(std::vector<ThreatSighting>::const_iterator threat=currentThreats.begin();
			threat!=currentThreats.end(); ++threat)
		{
			// Observations contain only hostile teams. An enemy's allies can
			// defend its workers just as effectively as its own warriors.
			if(distanceSquare(candidate.x, candidate.y, threat->x, threat->y,
				rules.width, rules.height)<=rules.threatRadius*rules.threatRadius)
			{
				++candidate.defenders;
				candidate.defenderPower+=threat->power;
			}
		}
		candidate.score=candidate.workers*rules.workerWeight
			+candidate.harvesters*rules.harvestingBonus
			+candidate.carriers*rules.carryingBonus
			+candidate.economicValue*rules.resourceWeight
			-candidate.defenders*rules.defenderPenalty;
	}
	std::sort(raids.begin(), raids.end(), raidBetter);
}

const RaidCandidate* Program::bestRaidForTeam(int team) const
{
	for(std::vector<RaidCandidate>::const_iterator raid=raids.begin();
		raid!=raids.end(); ++raid)
		if(team==NoTeam || raid->team==team)
			return &*raid;
	return NULL;
}

int Program::desiredRaidForce(int workerCount, int forceBonus,
	int minimumForce, int maximumForce)
{
	return std::max(minimumForce,
		std::min(maximumForce, workerCount+forceBonus));
}

bool Program::musterReady(int enrolled, int onSite, int requested,
	int requiredPercent)
{
	if(requested<=0 || enrolled<=0 || onSite<=0)
		return false;
	return std::min(enrolled,onSite)*100LL>=requested*static_cast<long long>(requiredPercent);
}

bool Program::raidUnsafe(int visibleDefenders, int launchedForce,
	int defenderMinimum, int defenderPercent)
{
	const int proportional=(launchedForce*defenderPercent+99)/100;
	return visibleDefenders>=std::max(defenderMinimum, proportional);
}

bool Program::musterLaunchAllowed(MissionKind kind, int enrolled, int onSite,
	int requested, int reliefMinimumForce, int requiredPercent)
{
	// Urgent ally relief keeps its existing enrollment-only dispatch rule.
	if(kind==MissionRelief)
		return enrolled>=reliefMinimumForce;
	// Offensive flags use only the configured fraction of their request. A
	// deadline neither raises this quorum nor permits a launch below it.
	return musterReady(enrolled, onSite, requested, requiredPercent);
}

bool Program::raidRetargetAllowed(bool sameTeam, int distanceSquare,
	int followRadius, int candidateScore, int currentScore, int scoreMargin)
{
	// Moving within the current raid area is tracking, not a new objective.
	return !sameTeam || distanceSquare<=followRadius*followRadius
		|| candidateScore>=currentScore+scoreMargin;
}

bool Program::targetQuarantined(int gid, int tick, bool enabled,
	const std::map<int, int>& quarantineUntil)
{
	const std::map<int, int>::const_iterator found=quarantineUntil.find(gid);
	return enabled && found!=quarantineUntil.end() && tick<found->second;
}

bool Program::casualtiesRequireWithdrawal(int enrolled, int launchedForce,
	int survivorMinimum, int casualtyPercent)
{
	if(enrolled<survivorMinimum)
		return true;
	return launchedForce>0
		&& (launchedForce-enrolled)*100>=launchedForce*casualtyPercent;
}

bool Program::reliefRetargetAllowed(bool changed, bool sameTeam,
	int distanceSquare, int followRadius, int candidateScore,
	int currentScore, int scoreMargin)
{
	return !changed
		|| (sameTeam && distanceSquare<=followRadius*followRadius)
		|| candidateScore>=currentScore+scoreMargin;
}

SiegeTargetContinuity Program::siegeTargetContinuity(bool targetRemembered,
	int currentTarget, int replacementTarget)
{
	if(targetRemembered)
		return SiegeTargetTracked;
	if(replacementTarget>=0 && replacementTarget!=currentTarget)
		return SiegeTargetReplacementAvailable;
	return SiegeTargetLost;
}

int Program::forceForPower(const std::vector<int>& descendingPowers,
	int requiredPower, int minimumForce, int maximumForce)
{
	int power=0;
	const int limit=std::min(maximumForce, int(descendingPowers.size()));
	for(int count=1; count<=limit; ++count)
	{
		power+=descendingPowers[count-1];
		if(count>=minimumForce && power>=requiredPower)
			return count;
	}
	return 0;
}

const char* missionKindName(MissionKind kind)
{
	static const char* names[]={"none", "raid", "siege", "relief"};
	return kind>=MissionNone && kind<=MissionRelief ? names[int(kind)] : "unknown";
}

const char* missionPhaseName(MissionPhase phase)
{
	static const char* names[]={"idle", "muster", "transit", "engage",
		"withdraw", "cooldown"};
	return phase>=PhaseIdle && phase<=PhaseCooldown ? names[int(phase)] : "unknown";
}

}
}
