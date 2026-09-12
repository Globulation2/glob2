#include "../src/AIMaximaTactics.h"

#include <cassert>
#include <string>

using namespace AIMaxima::Tactics;

int main()
{
	Program program;
	program.beginObservation(100);
	program.observeWorker(WorkerSighting(1, 2, 0, 5, 100, true, false, 2));
	program.observeWorker(WorkerSighting(2, 2, 31, 5, 100, false, true, 4));
	program.observeWorker(WorkerSighting(3, 2, 1, 6, 100, true, true, 5));
	RaidRules rules;
	rules.width=32;
	rules.height=32;
	rules.tick=100;
	program.finishObservation(rules);
	assert(program.raidCandidates().size()==1);
	assert(program.raidCandidates()[0].workers==3);
	const int safeScore=program.raidCandidates()[0].score;

	// Force-only samples must update safety without pretending workers were
	// seen again. Also exercise wrapping, other hostile teams, and removal.
	std::vector<ThreatSighting> freshThreats;
	for(int i=0;i<10;++i)
		freshThreats.push_back(ThreatSighting(100+i,2,31,5,7));
	freshThreats.push_back(ThreatSighting(200,3,0,5,100));
	freshThreats.push_back(ThreatSighting(201,2,16,16,100));
	program.replaceThreats(110,freshThreats);
	assert(program.raidCandidates()[0].defenders==11);
	assert(program.raidCandidates()[0].defenderPower==170);
	assert(program.raidCandidates()[0].score==safeScore-11*rules.defenderPenalty);
	assert(program.raidCandidates()[0].tick==100);
	// A cluster defended exclusively by another hostile team still scores lower.
	freshThreats.clear();
	for(int i=0;i<20;++i)
		freshThreats.push_back(ThreatSighting(300+i,3,0,5,10));
	program.replaceThreats(115,freshThreats);
	assert(program.raidCandidates()[0].defenders==20);
	program.replaceThreats(120,std::vector<ThreatSighting>());
	assert(program.raidCandidates()[0].defenders==0);
	assert(program.raidCandidates()[0].defenderPower==0);
	assert(program.raidCandidates()[0].score==safeScore);

	program.beginObservation(200);
	program.observeWorker(WorkerSighting(1, 2, 10, 10, 200, true, true, 4));
	program.observeWorker(WorkerSighting(2, 2, 11, 10, 200, true, true, 4));
	program.observeWorker(WorkerSighting(3, 2, 12, 10, 200, true, true, 4));
	program.observeThreat(ThreatSighting(9, 2, 11, 12, 20));
	program.finishObservation(rules);
	assert(program.raidCandidates().size()==1);
	assert(program.raidCandidates()[0].defenders==1);
	assert(program.raidCandidates()[0].score<safeScore+100);

	assert(std::string(missionKindName(MissionSiege))=="siege");

	std::map<int, int> quarantine;
	quarantine[42]=100;
	assert(Program::targetQuarantined(42, 99, true, quarantine));
	assert(!Program::targetQuarantined(42, 100, true, quarantine));
	assert(!Program::targetQuarantined(42, 101, true, quarantine));
	assert(!Program::targetQuarantined(43, 99, true, quarantine));
	assert(!Program::targetQuarantined(42, 99, false, quarantine));

	// A reset objective holds no flag and arms the stall watch with its
	// "never seen" sentinel, so the first damage reading counts as progress.
	Mission mission;
	mission.kind=MissionSiege;
	mission.phase=PhaseEngage;
	mission.flagId=7;
	mission.lastTargetHp=500;
	mission.reset();
	assert(mission.kind==MissionNone && mission.phase==PhaseIdle);
	assert(mission.flagId==NoFlag && mission.targetGid==NoTarget);
	assert(mission.lastTargetHp==-1);
	return 0;
}
