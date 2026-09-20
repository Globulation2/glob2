#include "../../src/ai/maxima/AIMaximaRecon.h"

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
	// Seen warriors are a sample of an army, not the army: most of it is at
	// home, inside a barracks or at an inn. Measured against the true count in
	// headless games the decayed peak recalls about two fifths, so the estimate
	// scales the sample up by visibleRecallInversePercent (250).
	assert(program.opponent(1)->estimatedWarriors==25);
	assert(program.opponent(1)->lastEconomicSeenTick==100);
	assert(program.opponent(1)->lastEconomicX==7);
	assert(program.opponent(1)->lastEconomicY==9);

	program.beginObservation(2600, living);
	program.finishObservation();
	assert(program.opponent(1)->confidence==75);
	assert(program.opponent(1)->estimatedWarriors==25);
	assert(program.opponent(1)->knownBuildings==1);
	program.beginObservation(2650, living);
	program.observeUnit(1, false, true, false, false, false);
	program.finishObservation();
	assert(program.opponent(1)->estimatedWarriors==25);
	program.beginForceObservation(2660, living);
	for(int i=0; i<14; ++i)
		program.observeUnit(1, true, false, false, false, false);
	program.finishForceObservation();
	assert(program.opponent(1)->estimatedWarriors==35);
	assert(program.opponent(1)->knownBuildings==1);
	program.beginObservation(2700, living);
	program.confirmBuildingAbsent(1, 11);
	program.finishObservation();
	assert(program.opponent(1)->knownBuildings==0);

	// A lower visible sample preserves the held peak until it decays.
	program.beginForceObservation(2800, living);
	program.observeUnit(1, true, false, false, false, false);
	program.finishForceObservation();
	assert(program.opponent(1)->visibleWarriors==1);
	assert(program.opponent(1)->lastObservedWarriors==14);
	assert(program.opponent(1)->estimatedWarriors==35);
	assert(Program::confidenceForAge(2500,2500,10000)==100);
	assert(Program::confidenceForAge(6250,2500,10000)==50);
	assert(Program::confidenceForAge(10000,2500,10000)==0);

	Program noForceMemory;
	noForceMemory.configure(10000, 2500, 1000, false);
	noForceMemory.beginForceObservation(100, living);
	for(int i=0; i<10; ++i)
		noForceMemory.observeUnit(1, true, false, false, false, false);
	noForceMemory.finishForceObservation();
	// Without force memory the estimate is the plain sighting, uncalibrated.
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
	for(int team=1;team<=3;++team)
	{
		staleReport.opponents[team].buildings[team]=BuildingSighting(
			team,team,0,team,team,1,1,false,2000);
		staleReport.opponents[team].lastBuildingSeenTick=2000;
	}
	assert(Program::staleOrUnseenEnemies(staleReport,2000,1000)==0);
	staleReport.opponents[2].lastBuildingSeenTick=900;
	assert(Program::staleOrUnseenEnemies(staleReport,2000,1000)==1);

	Program priority;
	priority.configure(10000,2500,1000);
	priority.beginObservation(0,std::vector<int>{1,2});
	priority.observeBuilding(BuildingSighting(11,1,0,2,2,2,2,false,0));
	priority.finishObservation();
	priority.beginObservation(500,std::vector<int>{1,2});
	priority.observeBuilding(BuildingSighting(22,2,0,18,18,2,2,false,500));
	priority.finishObservation();
	std::vector<unsigned char> explored(24*24,1);
	const auto oldest=Program::planObjectives(priority.report(),1,24,24,explored,12);
	assert(oldest.size()==1 && oldest[0].targetTeam==1);
	const auto both=Program::planObjectives(priority.report(),2,24,24,explored,12);
	assert(both.size()==2 && both[0].targetTeam!=both[1].targetTeam);

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
	const auto repeated=Program::planObjectives(report,2,20,20,discovered,3);
	assert(repeated.size()==objectives.size());
	for(size_t i=0;i<objectives.size();++i)
		assert(repeated[i].x==objectives[i].x && repeated[i].y==objectives[i].y);
	assert(objectives[0].frontier && objectives[1].frontier);
	assert(objectives[0].x!=objectives[1].x
		|| objectives[0].y!=objectives[1].y);
	MissionObjective watch(1, false, 7, 9, 100, true);
	ReconMission watchMission(99, 1, false, 7, 9, 100, true);
	assert(watch.economicWatch && watchMission.economicWatch);
	return 0;
}
