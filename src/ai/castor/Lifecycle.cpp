// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#include <Stream.h>
#include <algorithm>
#include <bit>

#include "AICastor.h"
#include "ai/observation/WorldQueries.h"
#include "FileFormatVersions.h"
#include "Game.h"
#include "Order.h"
#include "Player.h"
#include "Unit.h"
#include "Utilities.h"

#define AI_FILE_MIN_VERSION 1
#define AI_FILE_VERSION 6

using std::shared_ptr;

AIPlanning::BuildingIntent AICastor::intentForDemand(int demand)
{
 assert(demand >= 0 && demand < DemandCount);
 return demandIntents[demand];
}
bool AICastor::provides(const AIEngine::BuildingView& b, int demand) const
{
 return queries->matches(queries->kind(b).site ? queries->kind(b).next : b.typeNum, intentForDemand(demand));
}
bool AICastor::demandAvailable(int demand) const
{
 const auto& index=*queries;
 const auto intent = intentForDemand(demand);
 for (const auto& c : index.placements(intent))
  if (index.available(c,intent)) return true;
 return false;
}
int AICastor::desiredWorkers(const AIEngine::BuildingView& building, int request) const
{
 const int completed=queries->kind(building).site ? queries->kind(building).next : building.typeNum;
 const auto mask=queries->rawIntentMask(completed);
 const int demands=std::popcount(mask & demandIntentMask);
 if (demands > 1) request = std::max(request,requestedWorkers(building));
 return std::clamp(request,0,queries->kind(building).semantics.assignmentLimit);
}
int AICastor::selectBuilding(int demand) const
{
 const auto& index=*queries;
 const auto intent = intentForDemand(demand);
 int result = -1, count = 0;
 for (const auto& c : index.placements(intent))
  if (index.available(c,intent) && random() % ++count == 0) result = c;
 return result;
}

// Orders express private intent; they never change the observed simulation.
int AICastor::requestedWorkers(const AIEngine::BuildingView& building) const
{
 const auto it=pendingWorkers.find(building.identity.gid);
 return it!=pendingWorkers.end() && it->second.generation==building.identity.generation
  ? it->second.workers : building.maxUnitWorking;
}
Sint32 AICastor::requestedRatio(const AIEngine::BuildingView& building, int unit) const
{
 const auto it=pendingRatios.find(building.identity.gid);
 return it!=pendingRatios.end() && it->second.generation==building.identity.generation
  ? it->second.ratios[unit] : building.ratio[unit];
}
std::shared_ptr<Order> AICastor::requestWorkers(const AIEngine::BuildingView& building, Sint32 workers)
{
 const auto it=pendingWorkers.find(building.identity.gid);
 if (it!=pendingWorkers.end() && it->second.generation==building.identity.generation)
  return std::make_shared<NullOrder>();
 pendingWorkers[building.identity.gid]={building.identity.generation,workers,observation->tick,decisionSequence};
 return std::make_shared<OrderModifyBuilding>(building.identity.gid,workers);
}
std::shared_ptr<Order> AICastor::requestRatios(const AIEngine::BuildingView& building, const Sint32* ratios)
{
 const auto it=pendingRatios.find(building.identity.gid);
 if (it!=pendingRatios.end() && it->second.generation==building.identity.generation)
  return std::make_shared<NullOrder>();
 PendingRatios intent{building.identity.generation,{},observation->tick,decisionSequence};
 std::copy_n(ratios,NB_UNIT_TYPE,intent.ratios.begin());
 pendingRatios[building.identity.gid]=intent;
 Sint32 payload[NB_UNIT_TYPE];
 std::copy_n(ratios,NB_UNIT_TYPE,payload);
 return std::make_shared<OrderModifySwarm>(building.identity.gid,payload);
}
void AICastor::orderExecutionCompleted(const Order& order, bool)
{
 // Value matching prevents an old receipt from retiring a newer request.
 if (const auto* workers=dynamic_cast<const OrderModifyBuilding*>(&order))
 {
  const auto it=pendingWorkers.find(workers->gid);
  if (it!=pendingWorkers.end() && it->second.workers==workers->numberRequested)
   pendingWorkers.erase(it);
 }
 if (const auto* ratios=dynamic_cast<const OrderModifySwarm*>(&order))
 {
  const auto it=pendingRatios.find(ratios->gid);
  if (it!=pendingRatios.end() && std::equal(it->second.ratios.begin(),it->second.ratios.end(),ratios->ratio))
   pendingRatios.erase(it);
 }
}
void AICastor::reconcilePendingAssignments()
{
 for (auto it=pendingWorkers.begin();it!=pendingWorkers.end();)
 {
  const AIEngine::BuildingView* b=observation->buildingAtSlot(it->first);
  if (!b || b->identity.generation!=it->second.generation || b->maxUnitWorking==it->second.workers)
   it=pendingWorkers.erase(it);
  else ++it;
 }
 for (auto it=pendingRatios.begin();it!=pendingRatios.end();)
 {
  const AIEngine::BuildingView* b=observation->buildingAtSlot(it->first);
  if (!b || b->identity.generation!=it->second.generation || std::equal(it->second.ratios.begin(),it->second.ratios.end(),std::begin(b->ratio)))
   it=pendingRatios.erase(it);
  else ++it;
 }
}

// AICastor::Project part:

AICastor::Project::Project(int demand, const char *suffix)
{
	this->demand=demand;
	init(suffix);
}
AICastor::Project::Project(int demand, int amount, Sint32 mainWorkers, const char *suffix)
{
	this->demand=demand;
	init(suffix);
	this->amount=amount;
	this->mainWorkers=mainWorkers;
}
void AICastor::Project::init(const char *suffix)
{
	amount=AI_CASTOR_PROJECT_DEFAULT_AMOUNT;
	food=(this->demand==AICastor::ProduceWorkers
		|| this->demand==AICastor::FeedUnits);
	defense=(this->demand==AICastor::DefendWithProjectiles);

	debugStdName += std::to_string(this->demand);
	debugStdName += "-";
	debugStdName += suffix;
	this->debugName=debugStdName.c_str();

	//printf("new project(%s)\n", debugName);

	subPhase=AI_CASTOR_SUBPHASE_BOOT;

	successWait=0;
	blocking=true;
	critical=false;
	priority=AI_CASTOR_PROJECT_DEFAULT_PRIORITY;
	triesLeft=AI_CASTOR_PROJECT_TRIES_LEFT;

	mainWorkers=AI_CASTOR_WORKERS_UNSET;
	foodWorkers=AI_CASTOR_WORKERS_UNSET;
	otherWorkers=AI_CASTOR_WORKERS_UNSET;

	multipleStart=false;
	waitFinished=false;
	finalWorkers=AI_CASTOR_WORKERS_UNSET;

	finished=false;

	timer=AI_CASTOR_TIMER_NEVER;
}


// AICastor::Strategy part:

AICastor::Strategy::Strategy()
{
	isFreePart=warAmountTrigger=strikeWarPowerTriggerUp=strikeWarPowerTriggerDown=0;
	strikeTimeTrigger=0;
	for (auto &entry : build) entry = {};
	defined=false;
	
	successWait=0;
	
	warLevelTrigger=0;
	warTimeTrigger=0;
	maxAmountGoal=0;
};

// AICastor main class part:

void AICastor::firstInit()
{
	obstacleUnitMap=NULL;
	obstacleBuildingMap=NULL;
	spaceForBuildingMap=NULL;
	buildingNeighbourMap=NULL;
	
	workPowerMap=NULL;
	workRangeMap=NULL;
	workAbilityMap=NULL;
	hydratationMap=NULL;
	notGrassMap=NULL;
	wheatGrowthMap=NULL;
	for (int i=0; i<4; i++)
		oldWheatGradient[i]=NULL;
	for (int i=0; i<2; i++)
		wheatCareMap[i]=NULL;
	
	enemyWarriorsMap=NULL;
	enemyPowerMap=NULL;
	enemyRangeMap=NULL;
}

AICastor::AICastor(Player *player)
{
	firstInit();
	init(player);
}

void AICastor::init(Player *player)
{
	assert(player);
	
	// Logical :
	timer=0;
	pendingWorkers.clear();
	pendingRatios.clear();
 resourceInitializations.clear();pendingCreates.clear();
	canSwim=false;
	needSwim=false;
	lastFreeWorkersComputed=AI_CASTOR_TIMER_NEVER;
	lastWheatGrowthMapComputed=AI_CASTOR_TIMER_NEVER;
	lastEnemyRangeMapComputed=AI_CASTOR_TIMER_NEVER;
	lastEnemyPowerMapComputed=AI_CASTOR_TIMER_NEVER;
	lastEnemyWarriorsMapComputed=AI_CASTOR_TIMER_NEVER;
	computeNeedSwimTimer=0;
	controlSwarmsTimer=0;
	expandFoodTimer=0;
	controlFoodTimer=0;
	controlUpgradeTimer=0;
	controlUpgradeDelay=AI_CASTOR_UPGRADE_DELAY_TICKS;
	controlStrikesTimer=0;
	
	warLevel=0;
	warTimeTriggerLevel=0;
	warLevelTriggerLevel=0;
	warAmountTriggerLevel=0;
	
	onStrike=false;
	strikeTimeTrigger=0;
	strikeTeamSelected=false;
	strikeTeam=0;
	
	foodWarning=false;
	foodLock=false;
	foodSurplus=false;
	foodLockStats[0]=0;
	foodLockStats[1]=0;
	overWorkers=false;
	starvingWarning=false;
	starvingWarningStats[0]=0;
	starvingWarningStats[1]=0;
	buildsAmount=0;
	// Telemetry can capture this derived cache before the boot sequence computes
	// it (including immediately after loading an old save). Never serialize heap
	// contents as historical building counts. Gameplay computes it before use.
	for (auto &counts : buildingSum) counts[0] = counts[1] = 0;
	
	
	for (std::list<Project *>::iterator pi=projects.begin(); pi!=projects.end(); pi++)
		delete *pi;
	projects.clear();

	// Structural:
	this->player=player;
	this->team=player->team;
	this->game=player->game;
	this->map=player->map;
 teamNumber=player->team->teamNumber;

	assert(this->team);
	assert(this->game);
	assert(this->map);
	
	size_t size=map->w*map->h;
	assert(size>0);
	
	computeBoot=0;
	strategy=Strategy();
	for (auto &types : buildingLevels)
		for (auto &levels : types)
			for (auto &level : levels) level=0;
	
	if (obstacleUnitMap!=NULL)
		delete[] obstacleUnitMap;
	obstacleUnitMap=new Uint8[size]();
	
	if (obstacleBuildingMap!=NULL)
		delete[] obstacleBuildingMap;
	obstacleBuildingMap=new Uint8[size]();
	
	if (spaceForBuildingMap!=NULL)
		delete[] spaceForBuildingMap;
	spaceForBuildingMap=new Uint8[size]();
	
	if (buildingNeighbourMap!=NULL)
		delete[] buildingNeighbourMap;
	buildingNeighbourMap=new Uint8[size]();
	
	
	if (workPowerMap!=NULL)
		delete[] workPowerMap;
	workPowerMap=new Uint8[size]();
	
	if (workRangeMap!=NULL)
		delete[] workRangeMap;
	workRangeMap=new Uint8[size]();
	
	if (workAbilityMap!=NULL)
		delete[] workAbilityMap;
	workAbilityMap=new Uint8[size]();
	
	if (hydratationMap!=NULL)
		delete[] hydratationMap;
	hydratationMap=new Uint8[size]();

	if (notGrassMap!=NULL)
		delete[] notGrassMap;
	notGrassMap=new Uint8[size]();
	
	if (wheatGrowthMap!=NULL)
		delete[] wheatGrowthMap;
	wheatGrowthMap=new Uint8[size]();
	
	for (int i=0; i<4; i++)
	{
		if (oldWheatGradient[i]!=NULL)
			delete[] oldWheatGradient[i];
		oldWheatGradient[i]=new Uint8[size]();
	}
	
	for (int i=0; i<2; i++)
	{
		if (wheatCareMap[i]!=NULL)
			delete[] wheatCareMap[i];
		wheatCareMap[i]=new Uint8[size]();
	}
	
	if (enemyPowerMap!=NULL)
		delete[] enemyPowerMap;
	enemyPowerMap=new Uint8[size]();
	
	if (enemyRangeMap!=NULL)
		delete[] enemyRangeMap;
	enemyRangeMap=new Uint8[size]();
	
	if (enemyWarriorsMap!=NULL)
		delete[] enemyWarriorsMap;
	enemyWarriorsMap=new Uint8[size]();
}

AICastor::~AICastor()
{
	if (obstacleUnitMap!=NULL)
		delete[] obstacleUnitMap;
	
	if (obstacleBuildingMap!=NULL)
		delete[] obstacleBuildingMap;
	
	if (spaceForBuildingMap!=NULL)
		delete[] spaceForBuildingMap;
	
	if (buildingNeighbourMap!=NULL)
		delete[] buildingNeighbourMap;
	
	
	if (workPowerMap!=NULL)
		delete[] workPowerMap;
	
	if (workRangeMap!=NULL)
		delete[] workRangeMap;
	
	if (workAbilityMap!=NULL)
		delete[] workAbilityMap;
	
	if (hydratationMap!=NULL)
		delete[] hydratationMap;
	
	if (notGrassMap!=NULL)
		delete[] notGrassMap;
	
	if (wheatGrowthMap!=NULL)
		delete[] wheatGrowthMap;
	
	for (int i=0; i<4; i++)
		if (oldWheatGradient[i]!=NULL)
			delete[] oldWheatGradient[i];
	
	for (int i=0; i<2; i++)
		if (wheatCareMap[i]!=NULL)
			delete[] wheatCareMap[i];
	
	if (enemyPowerMap!=NULL)
		delete[] enemyPowerMap;
	
	if (enemyRangeMap!=NULL)
		delete[] enemyRangeMap;
	
	if (enemyWarriorsMap!=NULL)
		delete[] enemyWarriorsMap;

	for(std::list<Project *>::iterator i=projects.begin(); i!=projects.end(); ++i)
	{
		delete *i;
	}

}


// Keep names unique for text saves and scalar encodings endian-safe for binary saves.

namespace
{
struct SnapshotWriter
{
	GAGCore::OutputStream* stream;
	template<class T> void value(T& v, const char* name) { stream->writeSint32(static_cast<Sint32>(v),name); }
	void value(Uint32& v, const char* name) { stream->writeUint32(v,name); }
	template<class T, size_t N> void value(T (&v)[N], const char* name)
	{
		stream->writeEnterSection(name);
		unsigned i=0;
		for (auto& item : v)
		{
			stream->writeEnterSection(i++);
			value(item,"value");
			stream->writeLeaveSection();
		}
		stream->writeLeaveSection();
	}
	void bytes(Uint8* data, size_t size, const char* name) { stream->write(data,size,name); }
	void enter(const char* name) { stream->writeEnterSection(name); }
	void enter(unsigned i) { stream->writeEnterSection(i); }
	void leave() { stream->writeLeaveSection(); }
};
struct SnapshotReader
{
	GAGCore::InputStream* stream;
	template<class T> void value(T& v, const char* name) { v=static_cast<T>(stream->readSint32(name)); }
	void value(Uint32& v, const char* name) { v=stream->readUint32(name); }
	template<class T, size_t N> void value(T (&v)[N], const char* name)
	{
		stream->readEnterSection(name);
		unsigned i=0;
		for (auto& item : v)
		{
			stream->readEnterSection(i++);
			value(item,"value");
			stream->readLeaveSection();
		}
		stream->readLeaveSection();
	}
	void bytes(Uint8* data, size_t size, const char* name) { stream->read(data,size,name); }
	void enter(const char* name) { stream->readEnterSection(name); }
	void enter(unsigned i) { stream->readEnterSection(i); }
	void leave() { stream->readLeaveSection(); }
};
template<class Archive> void strategyBuild(Archive& archive, AICastor::Strategy::Build& b)
{
	archive.value(b.baseOrder,"baseOrder");
	archive.value(b.base,"base");
	archive.value(b.baseWorkers,"baseWorkers");
	archive.value(b.baseUpgrade,"baseUpgrade");
	archive.value(b.finalWorkers,"finalWorkers");
	archive.value(b.newOrder,"newOrder");
	archive.value(b.news,"news");
	archive.value(b.newWorkers,"newWorkers");
	archive.value(b.newUpgrade,"newUpgrade");
}
template<class Archive> void snapshot(Archive& archive, AICastor& ai, bool legacy = false)
{
	archive.value(ai.canSwim,"canSwim");
	archive.value(ai.needSwim,"needSwim");
	if (legacy) {
  int sums[13][2]{};
  int levels[13][2][4]{};
  archive.value(sums,"buildingSum");
  archive.value(levels,"buildingLevels");
  for (int old=0; old<13; ++old) if (old != 11) {
   const int demand = old == 12 ? AICastor::ExchangeResources : old;
   for (int site=0; site<2; ++site) {
    ai.buildingSum[demand][site]=sums[old][site];
    for (int level=0; level<4; ++level) ai.buildingLevels[demand][site][level]=levels[old][site][level];
   }
  }
 } else {
  archive.value(ai.buildingSum,"buildingSum");
  archive.value(ai.buildingLevels,"buildingLevels");
 }
	archive.value(ai.warLevel,"warLevel");
	archive.value(ai.warTimeTriggerLevel,"warTimeTriggerLevel");
	archive.value(ai.warLevelTriggerLevel,"warLevelTriggerLevel");
	archive.value(ai.warAmountTriggerLevel,"warAmountTriggerLevel");
	archive.value(ai.onStrike,"onStrike");
	archive.value(ai.strikeTimeTrigger,"strikeTimeTrigger");
	archive.value(ai.strikeTeamSelected,"strikeTeamSelected");
	archive.value(ai.strikeTeam,"strikeTeam");
	archive.value(ai.foodWarning,"foodWarning");
	archive.value(ai.foodLock,"foodLock");
	archive.value(ai.foodSurplus,"foodSurplus");
	archive.value(ai.foodLockStats,"foodLockStats");
	archive.value(ai.overWorkers,"overWorkers");
	archive.value(ai.starvingWarning,"starvingWarning");
	archive.value(ai.starvingWarningStats,"starvingWarningStats");
	archive.value(ai.buildsAmount,"buildsAmount");
	archive.value(ai.lastFreeWorkersComputed,"lastFreeWorkersComputed");
	archive.value(ai.lastWheatGrowthMapComputed,"lastWheatGrowthMapComputed");
	archive.value(ai.lastEnemyRangeMapComputed,"lastEnemyRangeMapComputed");
	archive.value(ai.lastEnemyPowerMapComputed,"lastEnemyPowerMapComputed");
	archive.value(ai.lastEnemyWarriorsMapComputed,"lastEnemyWarriorsMapComputed");
	archive.value(ai.computeNeedSwimTimer,"computeNeedSwimTimer");
	archive.value(ai.controlSwarmsTimer,"controlSwarmsTimer");
	archive.value(ai.expandFoodTimer,"expandFoodTimer");
	archive.value(ai.controlFoodTimer,"controlFoodTimer");
	archive.value(ai.controlUpgradeTimer,"controlUpgradeTimer");
	archive.value(ai.controlUpgradeDelay,"controlUpgradeDelay");
	archive.value(ai.controlStrikesTimer,"controlStrikesTimer");
	archive.value(ai.computeBoot,"computeBoot");
	archive.value(ai.strategy.defined,"strategydefined");
	archive.value(ai.strategy.successWait,"strategysuccessWait");
	archive.value(ai.strategy.isFreePart,"strategyisFreePart");
	archive.enter("strategyBuild");
 if (legacy) {
  for (unsigned old=0; old<13; ++old) {
   AICastor::Strategy::Build policy{};
   archive.enter(old);
   strategyBuild(archive,policy);
   archive.leave();
   if (old != 11) ai.strategy.build[old == 12 ? AICastor::ExchangeResources : old]=policy;
  }
 } else {
  unsigned index=0;
  for (auto& policy : ai.strategy.build) {
   archive.enter(index++);
   strategyBuild(archive,policy);
   archive.leave();
  }
 }
	archive.leave();
	archive.value(ai.strategy.warTimeTrigger,"strategywarTimeTrigger");
	archive.value(ai.strategy.warLevelTrigger,"strategywarLevelTrigger");
	archive.value(ai.strategy.warAmountTrigger,"strategywarAmountTrigger");
	archive.value(ai.strategy.strikeTimeTrigger,"strategystrikeTimeTrigger");
	archive.value(ai.strategy.strikeWarPowerTriggerUp,"strategystrikeWarPowerTriggerUp");
	archive.value(ai.strategy.strikeWarPowerTriggerDown,"strategystrikeWarPowerTriggerDown");
	archive.value(ai.strategy.maxAmountGoal,"strategymaxAmountGoal");
	const size_t size=ai.map->w*ai.map->h;
	archive.bytes(ai.obstacleUnitMap,size,"obstacleUnitMap");
	archive.bytes(ai.obstacleBuildingMap,size,"obstacleBuildingMap");
	archive.bytes(ai.spaceForBuildingMap,size,"spaceForBuildingMap");
	archive.bytes(ai.buildingNeighbourMap,size,"buildingNeighbourMap");
	archive.bytes(ai.workPowerMap,size,"workPowerMap");
	archive.bytes(ai.workRangeMap,size,"workRangeMap");
	archive.bytes(ai.workAbilityMap,size,"workAbilityMap");
	archive.bytes(ai.hydratationMap,size,"hydratationMap");
	archive.bytes(ai.notGrassMap,size,"notGrassMap");
	archive.bytes(ai.wheatGrowthMap,size,"wheatGrowthMap");
	archive.bytes(ai.enemyPowerMap,size,"enemyPowerMap");
	archive.bytes(ai.enemyRangeMap,size,"enemyRangeMap");
	archive.bytes(ai.enemyWarriorsMap,size,"enemyWarriorsMap");
	for (unsigned i=0; i<4; ++i) archive.bytes(ai.oldWheatGradient[i],size,("oldWheatGradient"+std::to_string(i)).c_str());
	for (unsigned i=0; i<2; ++i) archive.bytes(ai.wheatCareMap[i],size,("wheatCareMap"+std::to_string(i)).c_str());
}
template<class Archive> void projectSnapshot(Archive& archive, AICastor::Project& p, bool legacy=false)
{
	archive.value(p.demand,legacy ? "shortTypeNum" : "demand");
	archive.value(p.amount,"amount");
	archive.value(p.food,"food");
	archive.value(p.defense,"defense");
	archive.value(p.subPhase,"subPhase");
	archive.value(p.successWait,"successWait");
	archive.value(p.blocking,"blocking");
	archive.value(p.critical,"critical");
	archive.value(p.priority,"priority");
	archive.value(p.triesLeft,"triesLeft");
	archive.value(p.mainWorkers,"mainWorkers");
	archive.value(p.foodWorkers,"foodWorkers");
	archive.value(p.otherWorkers,"otherWorkers");
	archive.value(p.multipleStart,"multipleStart");
	archive.value(p.waitFinished,"waitFinished");
	archive.value(p.finalWorkers,"finalWorkers");
	archive.value(p.finished,"finished");
	archive.value(p.timer,"timer");
}
}

bool AICastor::load(GAGCore::InputStream *stream, Player *player, Sint32 versionMinor)
{
	init(player);
	assert(game);
	
	stream->readEnterSection("AICastor");
	Sint32 aiFileVersion = stream->readSint32("aiFileVersion");
	if (aiFileVersion<AI_FILE_MIN_VERSION || aiFileVersion>AI_FILE_VERSION)
	{
		fprintf(stderr, " error: unsupported Castor aiFileVersion=%d (supported %d..%d)\n", aiFileVersion, AI_FILE_MIN_VERSION, AI_FILE_VERSION);
		stream->readLeaveSection();
		return false;
	}
	if (aiFileVersion>=1)
		timer = stream->readUint32("timer");
	else
		timer=0;
		
	if (aiFileVersion>=3)
	{
		SnapshotReader archive{stream};
		snapshot(archive,*this,aiFileVersion<4);
		const Uint32 count=stream->readUint32("projects");
		if (count>4096) { stream->readLeaveSection(); return false; }
		for (Uint32 i=0; i<count; ++i)
		{
			stream->readEnterSection(i);
			auto project=std::make_unique<Project>(AICastor::ProduceWorkers,"restored");
			projectSnapshot(archive,*project,aiFileVersion<4);
   const bool obsolete = aiFileVersion<4 && project->demand==11;
   if (aiFileVersion<4 && project->demand==12) project->demand=ExchangeResources;
			if (project->demand<0 || project->demand>=AICastor::DemandCount)
			{ stream->readLeaveSection(2); return false; }
			project->debugStdName=stream->readText("debugName");
			project->debugName=project->debugStdName.c_str();
			stream->readLeaveSection();
			if (!obsolete) projects.push_back(project.release());
		}
		if (computeBoot<0 || computeBoot>AI_CASTOR_BOOT_IDLE_TICKS+AI_CASTOR_BOOT_COMPUTE_STEPS)
		{ stream->readLeaveSection(); return false; }
	}
	if (aiFileVersion>=5)
	{
		stream->readEnterSection("pendingAssignments");
		const Uint32 workerCount=stream->readUint32("workers");
		if (workerCount>Building::MAX_COUNT) { stream->readLeaveSection(2); return false; }
		for (Uint32 i=0;i<workerCount;++i)
		{
			stream->readEnterSection(i);
			const Uint16 gid=stream->readUint16("gid");
			PendingWorkers intent{stream->readUint32("generation"),stream->readSint32("workers")};
   if(aiFileVersion>=6) {intent.tick=stream->readUint32("tick");intent.sequence=stream->readUint32("sequenceLow");intent.sequence|=Uint64(stream->readUint32("sequenceHigh"))<<32;}
			stream->readLeaveSection();
			if (Building::GIDtoTeam(gid)!=team->teamNumber || intent.workers<0 || !pendingWorkers.emplace(gid,intent).second)
			{ stream->readLeaveSection(2); return false; }
		}
		const Uint32 ratioCount=stream->readUint32("ratios");
		if (ratioCount>Building::MAX_COUNT) { stream->readLeaveSection(2); return false; }
		for (Uint32 i=0;i<ratioCount;++i)
		{
			stream->readEnterSection(i);
			const Uint16 gid=stream->readUint16("gid");
			PendingRatios intent{stream->readUint32("generation"),{}};
   if(aiFileVersion>=6) {intent.tick=stream->readUint32("tick");intent.sequence=stream->readUint32("sequenceLow");intent.sequence|=Uint64(stream->readUint32("sequenceHigh"))<<32;}
			for (unsigned unit=0;unit<NB_UNIT_TYPE;++unit)
				intent.ratios[unit]=stream->readSint32(("ratio"+std::to_string(unit)).c_str());
			stream->readLeaveSection();
			if (Building::GIDtoTeam(gid)!=team->teamNumber || !pendingRatios.emplace(gid,intent).second)
			{ stream->readLeaveSection(2); return false; }
		}
		stream->readLeaveSection();
	}
 if(aiFileVersion>=6) {
  stream->readEnterSection("pendingCreates");const Uint32 count=stream->readUint32("count");
  if(count>1024) {stream->readLeaveSection(2);return false;}
  for(Uint32 i=0;i<count;++i) {
   stream->readEnterSection(i);PendingCreate intent{};
   intent.tick=stream->readUint32("tick");intent.sequence=stream->readUint32("sequenceLow");intent.sequence|=Uint64(stream->readUint32("sequenceHigh"))<<32;
   intent.type=stream->readSint32("type");intent.x=stream->readSint32("x");intent.y=stream->readSint32("y");stream->readLeaveSection();
   if(intent.type<0 || intent.type>=int(game->buildingsTypes.size())) {stream->readLeaveSection(2);return false;}
   pendingCreates.push_back(intent);
  }
  stream->readLeaveSection();
 }
	if(aiFileVersion>=6 && !AIEngine::loadResourceInitializations(stream,resourceInitializations,teamNumber,size_t(map->getW())*map->getH())) {stream->readLeaveSection();return false;}
	stream->readLeaveSection();
	if (versionMinor<FILE_FORMAT_VERSION_TERRAIN_PROPERTIES
		&& computeBoot>AI_CASTOR_BOOT_IDLE_TICKS+1)
 {
  const auto world=AIEngine::AIWorldView::capture(*game,AIEngine::AIWorldView::captureCatalog(*game));
  AIEngine::WorldQueries captured(*world,teamNumber,resourceInitializations);
  observation=world.get();queries=&captured;computeNotGrassMap();observation=nullptr;queries=nullptr;
 }
	return stream->isValid();
}

void AICastor::save(GAGCore::OutputStream *stream)
{
	stream->writeEnterSection("AICastor");
	stream->writeSint32(AI_FILE_VERSION, "aiFileVersion");
	stream->writeUint32(timer, "timer");
	SnapshotWriter archive{stream};
	snapshot(archive,*this);
	stream->writeUint32(static_cast<Uint32>(projects.size()),"projects");
	unsigned i=0;
	for (auto* project : projects)
	{
		stream->writeEnterSection(i++);
		projectSnapshot(archive,*project);
		stream->writeText(project->debugStdName,"debugName");
		stream->writeLeaveSection();
	}
	stream->writeEnterSection("pendingAssignments");
	stream->writeUint32(static_cast<Uint32>(pendingWorkers.size()),"workers");
	i=0;
	for (const auto& [gid,intent]:pendingWorkers)
	{
		stream->writeEnterSection(i++);
		stream->writeUint16(gid,"gid");
		stream->writeUint32(intent.generation,"generation");
  stream->writeUint32(intent.tick,"tick");stream->writeUint32(intent.sequence,"sequenceLow");stream->writeUint32(intent.sequence>>32,"sequenceHigh");
		stream->writeSint32(intent.workers,"workers");
		stream->writeLeaveSection();
	}
	stream->writeUint32(static_cast<Uint32>(pendingRatios.size()),"ratios");
	i=0;
	for (const auto& [gid,intent]:pendingRatios)
	{
		stream->writeEnterSection(i++);
		stream->writeUint16(gid,"gid");
		stream->writeUint32(intent.generation,"generation");
  stream->writeUint32(intent.tick,"tick");stream->writeUint32(intent.sequence,"sequenceLow");stream->writeUint32(intent.sequence>>32,"sequenceHigh");
		for (unsigned unit=0;unit<NB_UNIT_TYPE;++unit)
			stream->writeSint32(intent.ratios[unit],("ratio"+std::to_string(unit)).c_str());
		stream->writeLeaveSection();
	}
	stream->writeLeaveSection();
 stream->writeEnterSection("pendingCreates");stream->writeUint32(pendingCreates.size(),"count");i=0;
 for(const auto& intent:pendingCreates) {
  stream->writeEnterSection(i++);stream->writeUint32(intent.tick,"tick");stream->writeUint32(intent.sequence,"sequenceLow");stream->writeUint32(intent.sequence>>32,"sequenceHigh");
  stream->writeSint32(intent.type,"type");stream->writeSint32(intent.x,"x");stream->writeSint32(intent.y,"y");stream->writeLeaveSection();
 }
 stream->writeLeaveSection();
 AIEngine::saveResourceInitializations(stream,resourceInitializations);
	stream->writeLeaveSection();
}
