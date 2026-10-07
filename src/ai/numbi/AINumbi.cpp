// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

// AINumbi is split across several translation units by responsibility:
//   AINumbi.cpp          — lifecycle (ctors/init/load/save), getOrder dispatch, countUnits
//   AINumbiPlacement.cpp — building-site scanning and emplacement search
//   AINumbiEconomy.cpp   — food estimation, swarms, building adjustment, expansion
//   AINumbiMilitary.cpp  — attack management and level upgrades
// All share the single AINumbi declaration in AINumbi.h.

#include <Stream.h>
#include <sstream>

#include "AINumbi.h"
#include "NumbiQueries.h"
#include "ai/engine/AIDecision.h"
#include "AIStateSerialization.h"
#include "FileFormatVersions.h"
#include "Game.h"
#include "Order.h"
#include "Player.h"
#include "Utilities.h"
#include "Unit.h"

using std::shared_ptr;

AINumbi::AINumbi(Player *player)
{
	init(player);
}

AINumbi::AINumbi(GAGCore::InputStream *stream, Player *player, Sint32 versionMinor)
{
	bool goodLoad=load(stream, player, versionMinor);
	if (!goodLoad) throw std::runtime_error("Invalid saved AI");
}

void AINumbi::init(Player *player)
{
	timer=0;
	phase=0;
	phaseTime=AI_NUMBI_PHASE_TIME_DEFAULT_TICKS;
	attackPhase=0;
	criticalWarriors=AI_NUMBI_CRITICAL_WARRIORS_DEFAULT;
	criticalTime=AI_NUMBI_CRITICAL_TIME_DEFAULT_TICKS;
	attackTimer=0;
	mainBuilding.fill(0);
	pendingRequests.clear();
	resourceInitializations.clear();

	assert(player);

	this->player=player;
	this->team=player->team;
	this->game=player->game;
	this->map=player->map;
	teamNumber=player->team->teamNumber;

	assert(this->team);
	assert(this->game);
	assert(this->map);
}

AINumbi::~AINumbi()
{
}

bool AINumbi::load(GAGCore::InputStream *stream, Player *player, Sint32 versionMinor)
{
	init(player);
	GAGCore::BinaryInputStream::CheckedReads checked(stream);

	stream->readEnterSection("AINumbi");

	phase            = stream->readSint32("phase");
	attackPhase      = stream->readSint32("attackPhase");
	phaseTime        = stream->readSint32("phaseTime");
	criticalWarriors= stream->readSint32("critticalWarriors");
	criticalTime    = stream->readSint32("critticalTime");
	attackTimer      = stream->readSint32("attackTimer");

	// Pre-catalog saves persisted placement anchors in the old family order.
	// This mapping imports those anchors only; planning never classifies by ID.
	static constexpr Intent legacyAnchors[] = {
		Intent::ProduceWorker, Intent::Feed, Intent::Heal, Intent::TrainWalk,
		Intent::TrainSwim, Intent::TrainAttackStrength, Intent::TrainConstruction,
		Intent::ProjectileDefense, Intent::AttractExplorers, Intent::AttractWarriors,
		Intent::ClearResources, Intent::Count, Intent::ExchangeResources};
	const bool semantic = versionMinor >= FILE_FORMAT_VERSION_BUILDING_CATALOG;
	const unsigned count = semantic ? mainBuilding.size() : std::size(legacyAnchors);
	for (unsigned i = 0; i < count; ++i)
	{
		const int anchor = stream->readSint32(("mainBuilding[" + std::to_string(i) + "]").c_str());
		if (anchor < 0 || anchor >= Building::MAX_COUNT) return false;
		const Intent intent = semantic ? static_cast<Intent>(i) : legacyAnchors[i];
		if (intent != Intent::Count) mainBuilding[static_cast<unsigned>(intent)] = anchor;
	}

	if (versionMinor >= AI_NUMBI_SAVE_FORMAT_CONTINUATION)
		timer = AIStateSerialization::readSint32(stream, "timer");
	if (versionMinor >= FILE_FORMAT_VERSION_AI_PIPELINE)
	{
	 stream->readEnterSection("pendingRequests");
	 const Uint32 count=stream->readUint32("count");
	 if(count>1024) {stream->readLeaveSection(2);return false;}
	 for(Uint32 i=0;i<count;++i) {
	  stream->readEnterSection(i);
	  PendingRequest request;
	  request.tick=stream->readUint32("tick");
  request.pollSequence=stream->readUint32("pollSequenceLow");request.pollSequence|=Uint64(stream->readUint32("pollSequenceHigh"))<<32;
	  request.target.gid=stream->readUint16("gid");
	  request.target.generation=stream->readUint32("generation");
	  const Uint32 size=stream->readUint32("size");
	  if(!size || size>65536) {stream->readLeaveSection(3);return false;}
	  std::vector<Uint8> bytes(size);stream->read(bytes.data(),size,"order");
	  request.order=Order::getOrder(bytes.data(),bytes.size(),versionMinor);
	  stream->readLeaveSection();
	  if(!request.order || request.order->getOrderType()==ORDER_NULL) {stream->readLeaveSection(2);return false;}
	  if(!request.target.empty() && Building::GIDtoTeam(request.target.gid)!=teamNumber) {stream->readLeaveSection(2);return false;}
	  if(const auto* create=dynamic_cast<const OrderCreate*>(request.order.get()))
    if(create->teamNumber!=teamNumber || create->typeNum<0 || size_t(create->typeNum)>=game->buildingsTypes.size()) {stream->readLeaveSection(2);return false;}
   pendingRequests.push_back(std::move(request));
	 }
	 stream->readLeaveSection();
	 stream->readEnterSection("resourceInitializations");
	 const Uint32 fields=stream->readUint32("count");
	 if(fields>MAX_NB_RESOURCES*7) {stream->readLeaveSection(2);return false;}
	 for(Uint32 i=0;i<fields;++i) {
	  stream->readEnterSection(i);
	  const int key=stream->readSint32("key");
	  NumbiObservation::ResourceInitialization field;
	  field.observedTick=stream->readUint32("tick");
	  const Uint32 cells=stream->readUint32("cells");
	  if(key<0 || key/(MAX_NB_RESOURCES*7)!=teamNumber || cells!=Uint32(map->getW()*map->getH())) {stream->readLeaveSection(3);return false;}
	  std::vector<Uint8> bytes(size_t(cells)*2);stream->read(bytes.data(),bytes.size(),"values");
	  auto values=std::make_shared<std::vector<Uint16>>(cells);
	  for(size_t cell=0;cell<cells;++cell) (*values)[cell]=bytes[cell*2] | (Uint16(bytes[cell*2+1])<<8);
	  field.values=std::move(values);
	  stream->readLeaveSection();
	  if(!resourceInitializations.emplace(key,std::move(field)).second) {stream->readLeaveSection(2);return false;}
	 }
	 stream->readLeaveSection();
	}
	stream->readLeaveSection();

	if (versionMinor >= AI_NUMBI_SAVE_FORMAT_CONTINUATION &&
		(phaseTime < 0 || timer < 0 || timer > phaseTime)) return false;
	return stream->isValid();
}

void AINumbi::save(GAGCore::OutputStream *stream)
{
	stream->writeEnterSection("AINumbi");

	stream->writeSint32(phase, "phase");
	stream->writeSint32(attackPhase, "attackPhase");
	stream->writeSint32(phaseTime, "phaseTime");
	stream->writeSint32(criticalWarriors, "critticalWarriors");
	stream->writeSint32(criticalTime, "critticalTime");
	stream->writeSint32(attackTimer, "attackTimer");

	for (unsigned bi=0; bi<mainBuilding.size(); bi++)
	{
		std::ostringstream oss;
		oss << "mainBuilding[" << bi << "]";
		stream->writeSint32(mainBuilding[bi], oss.str().c_str());
	}

	stream->writeSint32(timer, "timer");
	stream->writeEnterSection("pendingRequests");
	stream->writeUint32(pendingRequests.size(),"count");
	unsigned i=0;
	for(const auto& request:pendingRequests) {
	 stream->writeEnterSection(i++);
	 stream->writeUint32(request.tick,"tick");
  stream->writeUint32(request.pollSequence,"pollSequenceLow");stream->writeUint32(request.pollSequence>>32,"pollSequenceHigh");
	 stream->writeUint16(request.target.gid,"gid");
	 stream->writeUint32(request.target.generation,"generation");
	 auto& order=*request.order;
	 std::vector<Uint8> bytes{order.getOrderType()};
	 if(order.getDataLength()) bytes.insert(bytes.end(),order.getData(),order.getData()+order.getDataLength());
	 stream->writeUint32(bytes.size(),"size");stream->write(bytes.data(),bytes.size(),"order");
	 stream->writeLeaveSection();
	}
	stream->writeLeaveSection();
	stream->writeEnterSection("resourceInitializations");
	stream->writeUint32(resourceInitializations.size(),"count");i=0;
	for(const auto& [key,field]:resourceInitializations) {
	 stream->writeEnterSection(i++);stream->writeSint32(key,"key");stream->writeUint32(field.observedTick,"tick");
	 const auto& values=*field.values;
	 stream->writeUint32(values.size(),"cells");std::vector<Uint8> bytes(values.size()*2);
	 for(size_t cell=0;cell<values.size();++cell) {bytes[cell*2]=Uint8(values[cell]);bytes[cell*2+1]=Uint8(values[cell]>>8);}
	 stream->write(bytes.data(),bytes.size(),"values");stream->writeLeaveSection();
	}
	stream->writeLeaveSection();
	stream->writeLeaveSection();
}


int AINumbi::requestedWorkers(const AIEngine::BuildingView& building) const
{
 for(const auto& request:pendingRequests) if(request.target==building.identity)
  if(const auto* workers=dynamic_cast<const OrderModifyBuilding*>(request.order.get())) return workers->numberRequested;
 return building.maxUnitWorking;
}
Sint32 AINumbi::requestedRatio(const AIEngine::BuildingView& building,int unit) const
{
 for(const auto& request:pendingRequests) if(request.target==building.identity)
  if(const auto* ratios=dynamic_cast<const OrderModifySwarm*>(request.order.get())) return ratios->ratio[unit];
 return building.ratio[unit];
}
bool AINumbi::hasPending(const AIEngine::BuildingView& building) const
{
 return std::any_of(pendingRequests.begin(),pendingRequests.end(),[&](const auto& request){return request.target==building.identity;});
}
int AINumbi::pendingBuildings(Intent intent) const
{
 int count=0;
 for(const auto& project:observation->buildProjects)
  if(project.teamNumber==teamNumber && queries->matches(project.typeNum,intent)) ++count;
 for(const auto& request:pendingRequests) if(const auto* create=dynamic_cast<const OrderCreate*>(request.order.get()))
  if(queries->matches(create->typeNum,intent)) ++count;
 return count;
}
std::shared_ptr<Order> AINumbi::remember(std::shared_ptr<Order> order,Uint32 tick,Uint64 pollSequence)
{
 if(!order || order->getOrderType()==ORDER_NULL) return order;
 Uint16 gid=0xffff;
 if(const auto* request=dynamic_cast<const OrderModifyBuilding*>(order.get())) gid=request->gid;
 else if(const auto* request=dynamic_cast<const OrderModifySwarm*>(order.get())) gid=request->gid;
 else if(const auto* request=dynamic_cast<const OrderConstruction*>(order.get())) gid=request->gid;
 else if(const auto* request=dynamic_cast<const OrderDelete*>(order.get())) gid=request->gid;
 const auto* target=gid==0xffff?nullptr:observation->buildingAtSlot(gid);
 if(target && hasPending(*target)) return std::make_shared<NullOrder>();
 pendingRequests.push_back({tick,pollSequence,target?target->identity:BuildingRef{},order});
 return order;
}
void AINumbi::orderExecutionCompleted(const Order& order,bool)
{
 // Legacy synchronous callers deliver the receipt after execution; production
 // asynchronous callers consume immutable receipts inside getOrder(context).
 for(auto it=pendingRequests.begin();it!=pendingRequests.end();++it) {
  auto& pending=*it->order;
  if(typeid(pending)!=typeid(order)) continue;
  if(const auto* a=dynamic_cast<const OrderCreate*>(&order)) {
   const auto& b=static_cast<const OrderCreate&>(pending);
   if(a->typeNum!=b.typeNum || a->posX!=b.posX || a->posY!=b.posY) continue;
  } else {
   // All Numbi entity commands encode the target gid first.
   const auto* received=const_cast<Order&>(order).getData();const auto* emitted=pending.getData();
   if(const_cast<Order&>(order).getDataLength()<2 || received[0]!=emitted[0] || received[1]!=emitted[1]) continue;
  }
  pendingRequests.erase(it);break;
 }
}

std::shared_ptr<Order> AINumbi::getOrder()
{
 const auto world=AIEngine::AIWorldView::capture(*game,AIEngine::AIWorldView::captureCatalog(*game));
 const std::vector<AIEngine::ExecutionReceipt> receipts;
 std::vector<AIEngine::ResourceEnrollmentRequest> enrollments;
 AIEngine::DecisionContext context{*world,0,unsigned(teamNumber),receipts};
 context.resourceEnrollments=&enrollments;
 auto order=getOrder(context);
 for(const auto& request:enrollments)
  map->installObservedResourceField(request.team,request.resource,request.swim,*request.initialField);
 return order;
}

std::shared_ptr<Order> AINumbi::getOrder(const AIEngine::DecisionContext& context)
{
 if(context.team!=unsigned(teamNumber)) throw std::invalid_argument("Numbi observation has wrong team");
 for(const auto& receipt:context.receipts)
  std::erase_if(pendingRequests,[&](const auto& request){return request.tick==receipt.request.observedTick && request.pollSequence==receipt.request.pollSequence;});
 std::erase_if(pendingRequests,[&](const auto& request){
  const auto* b=request.target.empty()?nullptr:context.world.building(request.target);
  if(!request.target.empty() && !b) return true;
  if(const auto* workers=dynamic_cast<const OrderModifyBuilding*>(request.order.get())) return b && b->maxUnitWorking==workers->numberRequested;
  if(const auto* ratios=dynamic_cast<const OrderModifySwarm*>(request.order.get()))
   return b && std::equal(std::begin(b->ratio),std::end(b->ratio),ratios->ratio);
  if(dynamic_cast<const OrderConstruction*>(request.order.get()))
   return b && (b->constructionResultState!=Building::NO_CONSTRUCTION || context.world.catalog->at(b->typeNum).site);
  if(const auto* create=dynamic_cast<const OrderCreate*>(request.order.get())) {
   for(const auto& project:context.world.buildProjects)
    if(project.teamNumber==teamNumber && project.typeNum==create->typeNum
     && context.world.normalizeX(project.posX)==context.world.normalizeX(create->posX)
     && context.world.normalizeY(project.posY)==context.world.normalizeY(create->posY)) return true;
   const auto& k=context.world.catalog->at(create->typeNum);
   for(const auto& building:context.world.buildings)
    if(building.team==teamNumber && (building.typeNum==create->typeNum || (k.site && building.typeNum==k.next))
     && building.posX==context.world.normalizeX(create->posX) && building.posY==context.world.normalizeY(create->posY)) return true;
  }
  return false;
 });
 NumbiObservation::Queries captured(context.world,teamNumber,resourceInitializations,context.resourceEnrollments);
 for(const auto& request:pendingRequests)
  if(const auto* create=dynamic_cast<const OrderCreate*>(request.order.get())) captured.reserve(create->typeNum,create->posX,create->posY);
 observation=&context.world; queries=&captured;
 observedBuildings.fill(nullptr); observedUnits.fill(nullptr);
 for(const auto& b:context.world.buildings) if(b.team==teamNumber) observedBuildings[Building::GIDtoID(b.identity.gid)]=&b;
 for(const auto& u:context.world.units) if(u.team==teamNumber) observedUnits[Unit::GIDtoID(u.identity.gid)]=&u;
 try {
  auto result=remember(decide(),context.world.tick,context.pollSequence); observation=nullptr;queries=nullptr;observedBuildings.fill(nullptr);observedUnits.fill(nullptr);return result;
 } catch(...) {observation=nullptr;queries=nullptr;observedBuildings.fill(nullptr);observedUnits.fill(nullptr);throw;}
}

std::shared_ptr<Order>AINumbi::decide()
{
	timer++;

	if (timer>phaseTime)
	{
		timer-=timer;
		phase++;
		//printf("AI: new phase %d.\n", phase);
	}
	if (phase==0)
	{
		// rush for food building, explore for room.
		switch (timer&AI_NUMBI_DECISION_SLOT_MASK)
		{
			case 0:
				return swarmsForWorkers(AI_NUMBI_PHASE0_SWARM_MIN, AI_NUMBI_PHASE0_SWARM_FACTOR, AI_NUMBI_PHASE0_SWARM_WORKERS, AI_NUMBI_PHASE0_SWARM_EXPLORER, AI_NUMBI_PHASE0_SWARM_WARRIOR);
			case 1:
				return adjustBuildings(AI_NUMBI_PHASE0_INN_NUMBERS, AI_NUMBI_PHASE0_INN_NUMBERS_INC, AI_NUMBI_PHASE0_INN_WORKERS, Intent::Feed);
		}
	}
	else if (phase==1)
	{
		// rush for food building
		switch (timer&AI_NUMBI_DECISION_SLOT_MASK)
		{
			case 0:
				return swarmsForWorkers(AI_NUMBI_PHASE1_SWARM_MIN, AI_NUMBI_PHASE1_SWARM_FACTOR, AI_NUMBI_PHASE1_SWARM_WORKERS, AI_NUMBI_PHASE1_SWARM_EXPLORER, AI_NUMBI_PHASE1_SWARM_WARRIOR);
			case 1:
				return adjustBuildings(AI_NUMBI_PHASE1_INN_NUMBERS, AI_NUMBI_PHASE1_INN_NUMBERS_INC, AI_NUMBI_PHASE1_INN_WORKERS, Intent::Feed);
		}
	}
	else if (phase<AI_NUMBI_MID_GAME_PHASE)
	{
		// mainly produce units, improve health and science if possible
		switch (timer&AI_NUMBI_DECISION_SLOT_MASK)
		{
			case 0:
				return swarmsForWorkers(AI_NUMBI_PHASE2_SWARM_MIN, AI_NUMBI_PHASE2_SWARM_FACTOR, AI_NUMBI_PHASE2_SWARM_WORKERS, AI_NUMBI_PHASE2_SWARM_EXPLORER, AI_NUMBI_PHASE2_SWARM_WARRIOR);
			case 1:
				return adjustBuildings(AI_NUMBI_PHASE2_INN_NUMBERS, AI_NUMBI_PHASE2_INN_NUMBERS_INC, AI_NUMBI_PHASE2_INN_WORKERS, Intent::Feed);
			case 2:
				return adjustBuildings(AI_NUMBI_PHASE2_HEAL_NUMBERS, AI_NUMBI_PHASE2_HEAL_NUMBERS_INC, AI_NUMBI_PHASE2_HEAL_WORKERS, Intent::Heal);
			case 3:
				return adjustBuildings(AI_NUMBI_PHASE2_SCIENCE_NUMBERS, AI_NUMBI_PHASE2_SCIENCE_NUMBERS_INC, AI_NUMBI_PHASE2_SCIENCE_WORKERS, Intent::TrainConstruction);
			case 4:
				return adjustBuildings(AI_NUMBI_PHASE2_RACETRACK_NUMBERS, AI_NUMBI_PHASE2_RACETRACK_NUMBERS_INC, AI_NUMBI_PHASE2_RACETRACK_WORKERS, Intent::TrainWalk);
			case 5:
				return adjustBuildings(AI_NUMBI_PHASE2_BARRACKS_NUMBERS, AI_NUMBI_PHASE2_BARRACKS_NUMBERS_INC, AI_NUMBI_PHASE2_BARRACKS_WORKERS, Intent::TrainAttackStrength);
			case 6:
				return adjustBuildings(AI_NUMBI_PHASE2_DEFENSE_NUMBERS, AI_NUMBI_PHASE2_DEFENSE_NUMBERS_INC, AI_NUMBI_PHASE2_DEFENSE_WORKERS, Intent::ProjectileDefense);
		}
	}
	else if (phase<AI_NUMBI_LATE_MID_PHASE)
	{
		// mainly produce units, improve health and science if possible
		switch (timer&AI_NUMBI_DECISION_SLOT_MASK)
		{
			case 0:
				return swarmsForWorkers(AI_NUMBI_PHASE4_SWARM_MIN, AI_NUMBI_PHASE4_SWARM_FACTOR, AI_NUMBI_PHASE4_SWARM_WORKERS, AI_NUMBI_PHASE4_SWARM_EXPLORER, AI_NUMBI_PHASE4_SWARM_WARRIOR);
			case 1:
				return adjustBuildings(AI_NUMBI_PHASE4_INN_NUMBERS, AI_NUMBI_PHASE4_INN_NUMBERS_INC, AI_NUMBI_PHASE4_INN_WORKERS, Intent::Feed);
			case 2:
				return adjustBuildings(AI_NUMBI_PHASE4_HEAL_NUMBERS, AI_NUMBI_PHASE4_HEAL_NUMBERS_INC, AI_NUMBI_PHASE4_HEAL_WORKERS, Intent::Heal);
			case 3:
				return adjustBuildings(AI_NUMBI_PHASE4_SCIENCE_NUMBERS, AI_NUMBI_PHASE4_SCIENCE_NUMBERS_INC, AI_NUMBI_PHASE4_SCIENCE_WORKERS, Intent::TrainConstruction);
			case 4:
				return adjustBuildings(AI_NUMBI_PHASE4_DEFENSE_NUMBERS, AI_NUMBI_PHASE4_DEFENSE_NUMBERS_INC, AI_NUMBI_PHASE4_DEFENSE_WORKERS, Intent::ProjectileDefense);
			case 5:
				return mayUpgrade(AI_NUMBI_PHASE4_UPGRADE_PTRIGGER, AI_NUMBI_PHASE4_UPGRADE_NTRIGGER);
		}
	}
	else if (phase<AI_NUMBI_SCIENCE_PHASE)
	{
		// improve science now
		switch (timer&AI_NUMBI_DECISION_SLOT_MASK)
		{
			case 0:
				return swarmsForWorkers(AI_NUMBI_PHASE6_SWARM_MIN, AI_NUMBI_PHASE6_SWARM_FACTOR, AI_NUMBI_PHASE6_SWARM_WORKERS, AI_NUMBI_PHASE6_SWARM_EXPLORER, AI_NUMBI_PHASE6_SWARM_WARRIOR);
			case 1:
				return adjustBuildings(AI_NUMBI_PHASE6_INN_NUMBERS, AI_NUMBI_PHASE6_INN_NUMBERS_INC, AI_NUMBI_PHASE6_INN_WORKERS, Intent::Feed);
			case 2:
				return adjustBuildings(AI_NUMBI_PHASE6_HEAL_NUMBERS, AI_NUMBI_PHASE6_HEAL_NUMBERS_INC, AI_NUMBI_PHASE6_HEAL_WORKERS, Intent::Heal);
			case 3:
				return adjustBuildings(AI_NUMBI_PHASE6_SCIENCE_NUMBERS, AI_NUMBI_PHASE6_SCIENCE_NUMBERS_INC, AI_NUMBI_PHASE6_SCIENCE_WORKERS, Intent::TrainConstruction);
			case 4:
				return mayUpgrade(AI_NUMBI_PHASE6_UPGRADE_PTRIGGER, AI_NUMBI_PHASE6_UPGRADE_NTRIGGER);
		}
	}
	else if (phase<AI_NUMBI_DEFEND_PHASE)
	{
		// produce good units, defend too.
		switch (timer&AI_NUMBI_DECISION_SLOT_MASK)
		{
			case 0:
				return swarmsForWorkers(AI_NUMBI_PHASE8_SWARM_MIN, AI_NUMBI_PHASE8_SWARM_FACTOR, AI_NUMBI_PHASE8_SWARM_WORKERS, AI_NUMBI_PHASE8_SWARM_EXPLORER, AI_NUMBI_PHASE8_SWARM_WARRIOR);
			case 1:
				return adjustBuildings(AI_NUMBI_PHASE8_INN_NUMBERS, AI_NUMBI_PHASE8_INN_NUMBERS_INC, AI_NUMBI_PHASE8_INN_WORKERS, Intent::Feed);
			case 2:
				return adjustBuildings(AI_NUMBI_PHASE8_HEAL_NUMBERS, AI_NUMBI_PHASE8_HEAL_NUMBERS_INC, AI_NUMBI_PHASE8_HEAL_WORKERS, Intent::Heal);
			case 3:
				return adjustBuildings(AI_NUMBI_PHASE8_SCIENCE_NUMBERS, AI_NUMBI_PHASE8_SCIENCE_NUMBERS_INC, AI_NUMBI_PHASE8_SCIENCE_WORKERS, Intent::TrainConstruction);
			case 4:
				return adjustBuildings(AI_NUMBI_PHASE8_RACETRACK_NUMBERS, AI_NUMBI_PHASE8_RACETRACK_NUMBERS_INC, AI_NUMBI_PHASE8_RACETRACK_WORKERS, Intent::TrainWalk);
			case 5:
				return adjustBuildings(AI_NUMBI_PHASE8_DEFENSE_NUMBERS, AI_NUMBI_PHASE8_DEFENSE_NUMBERS_INC, AI_NUMBI_PHASE8_DEFENSE_WORKERS, Intent::ProjectileDefense);
			case 6:
				return adjustBuildings(AI_NUMBI_PHASE8_BARRACKS_NUMBERS, AI_NUMBI_PHASE8_BARRACKS_NUMBERS_INC, AI_NUMBI_PHASE8_BARRACKS_WORKERS, Intent::TrainAttackStrength);
			case 7:
				return checkoutExpands(AI_NUMBI_PHASE8_EXPAND_NUMBERS, AI_NUMBI_PHASE8_EXPAND_WORKERS);
			case 8:
				return mayUpgrade(AI_NUMBI_PHASE8_UPGRADE_PTRIGGER, AI_NUMBI_PHASE8_UPGRADE_NTRIGGER);
		}
	}
	else
	{
		// produce warriors
		switch (timer&AI_NUMBI_DECISION_SLOT_MASK)
		{
			case 0:
				return swarmsForWorkers(AI_NUMBI_PHASE10_SWARM_MIN, AI_NUMBI_PHASE10_SWARM_FACTOR, AI_NUMBI_PHASE10_SWARM_WORKERS, AI_NUMBI_PHASE10_SWARM_EXPLORER, AI_NUMBI_PHASE10_SWARM_WARRIOR);
			case 1:
				return adjustBuildings(AI_NUMBI_PHASE10_INN_NUMBERS, AI_NUMBI_PHASE10_INN_NUMBERS_INC, AI_NUMBI_PHASE10_INN_WORKERS, Intent::Feed);
			case 2:
				return adjustBuildings(AI_NUMBI_PHASE10_HEAL_NUMBERS, AI_NUMBI_PHASE10_HEAL_NUMBERS_INC, AI_NUMBI_PHASE10_HEAL_WORKERS, Intent::Heal);
			case 3:
				return adjustBuildings(AI_NUMBI_PHASE10_SCIENCE_NUMBERS, AI_NUMBI_PHASE10_SCIENCE_NUMBERS_INC, AI_NUMBI_PHASE10_SCIENCE_WORKERS, Intent::TrainConstruction);
			case 4:
				return adjustBuildings(AI_NUMBI_PHASE10_RACETRACK_NUMBERS, AI_NUMBI_PHASE10_RACETRACK_NUMBERS_INC, AI_NUMBI_PHASE10_RACETRACK_WORKERS, Intent::TrainWalk);
			case 5:
				return adjustBuildings(AI_NUMBI_PHASE10_DEFENSE_NUMBERS, AI_NUMBI_PHASE10_DEFENSE_NUMBERS_INC, AI_NUMBI_PHASE10_DEFENSE_WORKERS, Intent::ProjectileDefense);
			case 6:
				return adjustBuildings(AI_NUMBI_PHASE10_BARRACKS_NUMBERS, AI_NUMBI_PHASE10_BARRACKS_NUMBERS_INC, AI_NUMBI_PHASE10_BARRACKS_WORKERS, Intent::TrainAttackStrength);
			case 7:
				return mayAttack(criticalWarriors, criticalTime, AI_NUMBI_WAR_FLAG_UNITS);
			case 8:
				return checkoutExpands(AI_NUMBI_PHASE10_EXPAND_NUMBERS, AI_NUMBI_PHASE10_EXPAND_WORKERS);
			case 9:
				return mayUpgrade(AI_NUMBI_PHASE10_UPGRADE_PTRIGGER, AI_NUMBI_PHASE10_UPGRADE_NTRIGGER);
		}
	}

	return shared_ptr<Order>(new NullOrder);
}

int AINumbi::countUnits(void)
{
	return observation->teams[teamNumber].statistics.totalUnit;
}

int AINumbi::countUnits(const int medicalState)
{
	if (medicalState == Unit::MED_FREE)
	{
		return observation->teams[teamNumber].statistics.totalUnit
			- observation->teams[teamNumber].statistics.needFoodCritical
			- observation->teams[teamNumber].statistics.needFood
			- observation->teams[teamNumber].statistics.needHeal;
	}
	else if (medicalState == Unit::MED_HUNGRY)
	{
		return observation->teams[teamNumber].statistics.needFoodCritical
			+ observation->teams[teamNumber].statistics.needFood;
	}
	else if (medicalState == Unit::MED_DAMAGED)
	{
		return observation->teams[teamNumber].statistics.needHeal;
	}
	else
		assert(false);
	return 0;
}
