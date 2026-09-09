#include "../src/AIMaximaRecon.h"

#include <cassert>
#include <vector>

using namespace AIMaxima::Recon;

int main()
{
	Program program;
	program.configure(10000, 2500, 1000);
	std::vector<int> living;
	living.push_back(1);
	living.push_back(2);
	living.push_back(3);
	program.beginObservation(100, living);
	for(int i=0; i<10; ++i)
		program.observeUnit(1, true, false, false, false, false);
	program.observeBuilding(BuildingSighting(11, 1, 0, 4, 4, 2, 2, false, 100));
	program.observeEconomicActivity(1, 7, 9);
	program.finishObservation();
	assert(program.opponent(1)->confidence==100);
	assert(program.opponent(1)->estimatedWarriors==10);
	assert(program.opponent(1)->lastEconomicSeenTick==100);
	assert(program.opponent(1)->lastEconomicX==7);
	assert(program.opponent(1)->lastEconomicY==9);

	program.beginObservation(2600, living);
	program.finishObservation();
	assert(program.opponent(1)->confidence==75);
	assert(program.opponent(1)->estimatedWarriors==10);
	assert(program.opponent(1)->knownBuildings==1);
	program.beginObservation(2650, living);
	program.observeUnit(1, false, true, false, false, false);
	program.finishObservation();
	assert(program.opponent(1)->estimatedWarriors==10);
	program.beginForceObservation(2660, living);
	for(int i=0; i<14; ++i)
		program.observeUnit(1, true, false, false, false, false);
	program.finishForceObservation();
	assert(program.opponent(1)->estimatedWarriors==14);
	assert(program.opponent(1)->knownBuildings==1);
	program.beginObservation(2700, living);
	program.confirmBuildingAbsent(1, 11);
	program.finishObservation();
	assert(program.opponent(1)->knownBuildings==0);

	Program noForceMemory;
	noForceMemory.configure(10000, 2500, 1000, false);
	noForceMemory.beginForceObservation(100, living);
	for(int i=0; i<10; ++i)
		noForceMemory.observeUnit(1, true, false, false, false, false);
	noForceMemory.finishForceObservation();
	assert(noForceMemory.opponent(1)->estimatedWarriors==10);
	noForceMemory.beginForceObservation(110, living);
	noForceMemory.finishForceObservation();
	assert(noForceMemory.opponent(1)->estimatedWarriors==0);
	assert(noForceMemory.opponent(1)->confidence==0);

	assert(Program::desiredMissionCount(1, 1, 90, false)==1);
	assert(Program::desiredMissionCount(3, 3, 10, false)==1);
	assert(Program::desiredMissionCount(3, 3, 30, false)==2);
	assert(Program::desiredMissionCount(3, 3, 60, false)==3);
	assert(Program::desiredMissionCount(3, 3, 60, true)==0);
	ReconReport staleReport;
	staleReport.opponents[1].alive=true;
	staleReport.opponents[2].alive=true;
	staleReport.opponents[3].alive=true;
	assert(Program::staleOrUnseenEnemies(staleReport, 2000, 1000)==3);

	ReconReport report;
	report.tick=1000;
	report.opponents[1].alive=true;
	report.opponents[2].alive=true;
	std::vector<unsigned char> discovered(20*20, 0);
	for(int y=8; y<=11; ++y)
		for(int x=8; x<=11; ++x)
			discovered[y*20+x]=1;
	const std::vector<MissionObjective> objectives=Program::planObjectives(
		report, 2, 20, 20, discovered, 3);
	assert(objectives.size()==2);
	assert(objectives[0].frontier && objectives[1].frontier);
	assert(objectives[0].x!=objectives[1].x
		|| objectives[0].y!=objectives[1].y);
	MissionObjective watch(1, false, 7, 9, 100, true);
	ReconMission watchMission(99, 1, false, 7, 9, 100, true);
	assert(watch.economicWatch && watchMission.economicWatch);
	return 0;
}
