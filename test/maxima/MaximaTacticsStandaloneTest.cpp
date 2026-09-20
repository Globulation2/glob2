#include "../../src/ai/maxima/AIMaximaTactics.h"
#include <climits>

#include <cassert>
#include <string>

using namespace AIMaxima::Tactics;

static void raidFootprintRegression()
{
	RaidRules rules;
	rules.width=rules.height=32;
	rules.flagRadius=2;
	Program program;
	// One connected chain, but only the middle three workers fit the flag.
	// The outliers carry much more value and must contribute no bonus.
	for(int i=0;i<7;++i)
		program.observeWorker(WorkerSighting(i, 1, 4+2*i, 10, 0,
			i<2 || i>4, i<2 || i>4, i<2 || i>4 ? 100 : 1));
	program.finishObservation(rules);
	assert(program.raidCandidates().size()==1);
	const RaidCandidate candidate=program.raidCandidates()[0];
	assert(candidate.x==10 && candidate.y==10);
	assert(candidate.workers==3 && candidate.harvesters==0 && candidate.carriers==0);
	assert(candidate.economicValue==3);
	assert(candidate.workerGids==std::vector<int>({2,3,4}));
	assert(candidate.score==3*rules.workerWeight+3*rules.resourceWeight);
	// The existing strategy flag radius, not clustering radius, controls reward.
	rules.flagRadius=4;
	program.finishObservation(rules);
	assert(program.raidCandidates()[0].workers==5);
	rules.flagRadius=1;
	program.finishObservation(rules);
	assert(program.raidCandidates().empty());

	// Wrap both axes and reject diagonal points outside the circular radius.
	program.beginObservation(1);
	program.observeWorker(WorkerSighting(0, 1, 0, 0, 1, false, false, 0));
	program.observeWorker(WorkerSighting(1, 1, 30, 0, 1, false, false, 0));
	program.observeWorker(WorkerSighting(2, 1, 2, 0, 1, false, false, 0));
	program.observeWorker(WorkerSighting(3, 1, 0, 30, 1, false, false, 0));
	program.observeWorker(WorkerSighting(4, 1, 0, 2, 1, false, false, 0));
	program.observeWorker(WorkerSighting(5, 1, 2, 2, 1, true, true, 100));
	program.observeWorker(WorkerSighting(6, 1, 30, 30, 1, true, true, 100));
	rules.flagRadius=2;
	program.finishObservation(rules);
	assert(program.raidCandidates().size()==1);
	assert(program.raidCandidates()[0].x==0 && program.raidCandidates()[0].y==0);
	assert(program.raidCandidates()[0].workers==5);
	assert(program.raidCandidates()[0].score==5*rules.workerWeight);
	// Arrival order must not change the candidate or its credited workers.
	Program reversed;
	for(auto it=program.workers().rbegin();it!=program.workers().rend();++it)
		reversed.observeWorker(*it);
	reversed.finishObservation(rules);
	assert(reversed.raidCandidates()[0].workerGids==program.raidCandidates()[0].workerGids);
	assert(reversed.raidCandidates()[0].score==program.raidCandidates()[0].score);
}

int main()
{
	raidFootprintRegression();
	assert(desiredArmy(100,0,1000,12,1024)==100);
	assert(desiredArmy(100,10000,1000,12,1024)==110);
	assert(desiredArmy(100,50000,1000,12,1024)==150);
	assert(desiredArmy(0,50000,1000,12,1024)==50);
	assert(desiredArmy(10,100000,1000,12,1024)==100);
	assert(desiredArmy(100,~0u,1000,12,1024)==1024);
	assert(desiredArmy(INT_MAX,~0u,1000000,12,1024)==1024);
	Wave wave;
	wave.requestedForce=20;
	assert(!waveReady(wave,0,500,75,500,3000,4,20,20));
	assert(!waveReady(wave,10,1000,75,500,3000,4,12,20));
	assert(wave.progressTick==1000);
	assert(!waveReady(wave,9,1400,75,500,3000,4,12,20));
	assert(waveReady(wave,10,1500,75,500,3000,4,12,20));
	assert(waveReady(wave,15,1501,75,500,3000,4,20,20));
	assert(!waveReady(wave,3,4000,75,500,3000,4,4,20));
	// Neither timeout can turn four gathered warriors plus sixteen stragglers
	// into a wave; a small but concentrated force can leave after waiting.
	assert(!waveReady(wave,4,4000,75,500,3000,4,20,20));
	Wave small; small.requestedForce=4;
	assert(!waveReady(small,4,100,75,500,3000,4,4,20));
	assert(waveReady(small,4,600,75,500,3000,4,4,20));
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
