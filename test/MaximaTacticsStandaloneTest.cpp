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
	assert(Program::raidUnsafe(program.raidCandidates()[0].defenders,6,2,50));
	// A cluster defended exclusively by another hostile team is unsafe too.
	freshThreats.clear();
	for(int i=0;i<20;++i)
		freshThreats.push_back(ThreatSighting(300+i,3,0,5,10));
	program.replaceThreats(115,freshThreats);
	assert(program.raidCandidates()[0].defenders==20);
	assert(Program::raidUnsafe(program.raidCandidates()[0].defenders,6,2,50));
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

	assert(Program::desiredRaidForce(3, 3, 6, 10)==6);
	assert(Program::desiredRaidForce(12, 3, 6, 10)==10);
	assert(!Program::musterReady(8, 5, 8, 75));
	assert(Program::musterReady(8, 6, 8, 75));
	assert(!Program::raidUnsafe(1, 8, 2, 50));
	assert(Program::raidUnsafe(4, 8, 2, 50));
	assert(!Program::casualtiesRequireWithdrawal(7, 8, 4, 25));
	assert(Program::casualtiesRequireWithdrawal(6, 8, 4, 25));
	assert(Program::casualtiesRequireWithdrawal(3, 8, 4, 25));
	assert(Program::reliefRetargetAllowed(false, true, 10000, 6, 10, 10, 20));
	assert(Program::reliefRetargetAllowed(true, true, 25, 6, 10, 10, 20));
	assert(!Program::reliefRetargetAllowed(true, true, 10000, 6, 29, 10, 20));
	assert(Program::reliefRetargetAllowed(true, true, 10000, 6, 30, 10, 20));
	assert(Program::siegeTargetContinuity(true, 42, -1)
		==SiegeTargetTracked);
	assert(Program::siegeTargetContinuity(false, 42, -1)
		==SiegeTargetLost);
	assert(Program::siegeTargetContinuity(false, 42, 42)
		==SiegeTargetLost);
	assert(Program::siegeTargetContinuity(false, 42, 43)
		==SiegeTargetReplacementAvailable);
	std::vector<int> powers;
	powers.push_back(9);
	powers.push_back(7);
	powers.push_back(5);
	powers.push_back(3);
	assert(Program::forceForPower(powers, 16, 3, 4)==3);
	assert(Program::forceForPower(powers, 30, 2, 4)==0);
	assert(std::string(missionKindName(MissionRelief))=="relief");

	// Offensive quorum is purely proportional, rounds upward, and remains tunable.
	assert(!Program::musterLaunchAllowed(MissionSiege, 11, 5, 11, 11, 50));
	assert(Program::musterLaunchAllowed(MissionSiege, 6, 6, 11, 11, 50));
	assert(Program::musterLaunchAllowed(MissionSiege, 5, 5, 10, 11, 50));
	assert(!Program::musterLaunchAllowed(MissionSiege, 8, 8, 11, 11, 75));
	assert(Program::musterLaunchAllowed(MissionSiege, 9, 9, 11, 11, 75));
	assert(!Program::musterLaunchAllowed(MissionSiege, 30, 23, 30, 11, 80));
	assert(Program::musterLaunchAllowed(MissionSiege, 30, 24, 30, 11, 80));
	assert(!Program::musterLaunchAllowed(MissionRaid, 6, 2, 6, 6, 50));
	assert(Program::musterLaunchAllowed(MissionRaid, 3, 3, 6, 6, 50));
	assert(Program::musterLaunchAllowed(MissionRelief, 6, 0, 10, 6, 80));
	assert(!Program::musterReady(0, 0, 0, 50));
	assert(!Program::musterReady(0, 0, 11, 50));
	assert(!Program::musterReady(5, 6, 11, 50));

	// A nearby moving cluster remains trackable even if its score falls; a
	// distant replacement still needs to clear the existing switch margin.
	assert(Program::raidRetargetAllowed(true, 1, 6, 300, 300, 60));
	assert(Program::raidRetargetAllowed(true, 36, 6, 250, 300, 60));
	assert(!Program::raidRetargetAllowed(true, 37, 6, 359, 300, 60));
	assert(Program::raidRetargetAllowed(true, 37, 6, 360, 300, 60));
	assert(Program::raidRetargetAllowed(false, 100, 6, 250, 300, 60));

	std::map<int, int> quarantine;
	quarantine[42]=100;
	assert(Program::targetQuarantined(42, 99, true, quarantine));
	assert(!Program::targetQuarantined(42, 100, true, quarantine));
	assert(!Program::targetQuarantined(42, 101, true, quarantine));
	assert(!Program::targetQuarantined(43, 99, true, quarantine));
	assert(!Program::targetQuarantined(42, 99, false, quarantine));

	Mission siege;
	siege.targetGid=42;
	siege.lastTargetHp=10;
	siege.lastProgressTick=100;
	siege.retargetSiege(43, 7, 8, 200);
	assert(siege.targetGid==43 && siege.targetX==7 && siege.targetY==8);
	assert(siege.lastTargetHp==-1 && siege.lastProgressTick==200);
	// Repeated updates to the same target must not hide a real stall.
	siege.lastTargetHp=500;
	siege.retargetSiege(43, 7, 8, 300);
	assert(siege.lastTargetHp==500 && siege.lastProgressTick==200);
	return 0;
}
