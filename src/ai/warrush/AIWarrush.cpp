#include "AIResourcePolicy.h"
#include "Material.h"
#include "AIRuleOrders.h"
// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière
// Copyright (C) 2005 Eli Dupree

#include "AITelemetryFields.h"
#include "AIWarrush.h"
#include <sstream>
#include "AIWarrushTuning.h"
#include "ai/observation/WorldQueries.h"
#include "ai/observation/ObservationAreaOrders.h"
#include "AIStateSerialization.h"
#include "FileFormatVersions.h"
#include "Building.h"
#include "Unit.h"
#include "Game.h"
#include <algorithm>
#include <array>

#include "Order.h"
#include "Player.h"
#include "Brush.h"
#include "Utilities.h"

using std::shared_ptr;

namespace {
	template<typename Pred>
	int countUnitsIf(const AIEngine::AIWorldView& world, const auto *team, Pred p)
	{
		int n = 0;
		for (int i = 0; i < Unit::MAX_COUNT; i++)
		{
			const AIEngine::UnitView *u = world.unitSlots(team->number)[i];
			if (u && p(u)) n++;
		}
		return n;
	}

	template<typename Pred>
	const AIEngine::UnitView *findUnitIf(const AIEngine::AIWorldView& world, const auto *team, Pred p)
	{
		for (int i = 0; i < Unit::MAX_COUNT; i++)
		{
			const AIEngine::UnitView *u = world.unitSlots(team->number)[i];
			if (u && p(u)) return u;
		}
		return nullptr;
	}

	template<typename Pred>
	int countBuildingsIf(const AIEngine::AIWorldView& world, const auto *team, Pred p)
	{
		int n = 0;
		for (int i = 0; i < Building::MAX_COUNT; i++)
		{
			const AIEngine::BuildingView *b = world.buildingSlots(team->number)[i];
			if (b && p(b)) n++;
		}
		return n;
	}

	template<typename Pred>
	const AIEngine::BuildingView *findBuildingIf(const AIEngine::AIWorldView& world, const auto *team, Pred p)
	{
		for (int i = 0; i < Building::MAX_COUNT; i++)
		{
			const AIEngine::BuildingView *b = world.buildingSlots(team->number)[i];
			if (b && p(b)) return b;
		}
		return nullptr;
	}
}

void AIWarrush::init(Player *player)
{
	assert(player);
	
	this->player=player;
	this->team=player->team;
	this->game=player->game;
	this->map=player->map;
 teamNumber=player->team->teamNumber;
	buildingDelay = 0;
	areaUpdatingDelay = 0;
 pendingRequests.clear();resourceInitializations.clear();
	
	assert(this->team);
	assert(this->game);
	assert(this->map);
}

AIWarrush::AIWarrush(Player *player)
{
	init(player);
}

AIWarrush::AIWarrush(GAGCore::InputStream *stream, Player *player, Sint32 versionMinor)
{
	if (!load(stream, player, versionMinor))
		throw std::runtime_error("Invalid Warrush continuation state");
}

AIWarrush::~AIWarrush()
{
}

bool AIWarrush::load(GAGCore::InputStream *stream, Player *player, Sint32 versionMinor)
{
	init(player);
 GAGCore::BinaryInputStream::CheckedReads checked(stream);
	if (versionMinor >= AI_WARRUSH_SAVE_FORMAT_CONTINUATION)
	{
		stream->readEnterSection("AIWarrush");
		buildingDelay = AIStateSerialization::readSint32(stream, "buildingDelay");
		areaUpdatingDelay = AIStateSerialization::readSint32(stream, "areaUpdatingDelay");
  if (buildingDelay < 0 || buildingDelay > AI_WARRUSH_BUILDING_DELAY_TICKS ||
      areaUpdatingDelay < 0 || areaUpdatingDelay > AI_WARRUSH_AREAS_DELAY_TICKS) {stream->readLeaveSection();return false;}
		if(versionMinor>=FILE_FORMAT_VERSION_AI_PIPELINE) {
   stream->readEnterSection("pendingRequests");
   const Uint32 count=stream->readUint32("count");
   if(count>1024) {stream->readLeaveSection(2);return false;}
   for(Uint32 i=0;i<count;++i) {
    stream->readEnterSection(i);PendingRequest request;
    request.tick=stream->readUint32("tick");request.pollSequence=stream->readUint32("pollSequenceLow");request.pollSequence|=Uint64(stream->readUint32("pollSequenceHigh"))<<32;
    request.target.gid=stream->readUint16("gid");request.target.generation=stream->readUint32("generation");
    const Uint32 size=stream->readUint32("size");
    if(!size || size>65536) {stream->readLeaveSection(3);return false;}
    std::vector<Uint8> bytes(size);stream->read(bytes.data(),size,"order");
    request.order=Order::getOrder(bytes.data(),bytes.size(),versionMinor);stream->readLeaveSection();
    if(!request.order || (request.order->getOrderType()!=ORDER_CREATE && request.order->getOrderType()!=ORDER_MODIFY_BUILDING && request.order->getOrderType()!=ORDER_MODIFY_SWARM)
     || (!request.target.empty() && Building::GIDtoTeam(request.target.gid)!=teamNumber)) {stream->readLeaveSection(2);return false;}
    if(const auto* create=dynamic_cast<const OrderCreate*>(request.order.get()))
    if(create->teamNumber!=teamNumber || create->typeNum<0 || size_t(create->typeNum)>=game->buildingsTypes.size()) {stream->readLeaveSection(2);return false;}
   pendingRequests.push_back(std::move(request));
   }
   stream->readLeaveSection();
  }
  stream->readLeaveSection();

	}
	return stream->isValid();
}

void AIWarrush::save(GAGCore::OutputStream *stream)
{
	stream->writeEnterSection("AIWarrush");
	stream->writeSint32(buildingDelay, "buildingDelay");
	stream->writeSint32(areaUpdatingDelay, "areaUpdatingDelay");
	stream->writeEnterSection("pendingRequests");stream->writeUint32(pendingRequests.size(),"count");
 unsigned i=0;
 for(const auto& request:pendingRequests) {
  stream->writeEnterSection(i++);stream->writeUint32(request.tick,"tick");stream->writeUint32(request.pollSequence,"pollSequenceLow");stream->writeUint32(request.pollSequence>>32,"pollSequenceHigh");
  stream->writeUint16(request.target.gid,"gid");stream->writeUint32(request.target.generation,"generation");
  auto& order=*request.order;std::vector<Uint8> bytes{order.getOrderType()};
  if(order.getDataLength()) bytes.insert(bytes.end(),order.getData(),order.getData()+order.getDataLength());
  stream->writeUint32(bytes.size(),"size");stream->write(bytes.data(),bytes.size(),"order");stream->writeLeaveSection();
 }
 stream->writeLeaveSection();
 stream->writeLeaveSection();
}

bool AIWarrush::hasPending(const AIEngine::BuildingView& building) const
{
 return std::any_of(pendingRequests.begin(),pendingRequests.end(),[&](const auto& request){return request.target==building.identity;});
}
void AIWarrush::settleObservedRequests(const AIEngine::DecisionContext& context)
{
 for(const auto& receipt:context.receipts) std::erase_if(pendingRequests,[&](const auto& request){return request.tick==receipt.request.observedTick && request.pollSequence==receipt.request.pollSequence;});
 std::erase_if(pendingRequests,[&](const auto& request){
  const auto* b=request.target.empty()?nullptr:context.world.building(request.target);
  if(!request.target.empty() && !b) return true;
  if(const auto* staffing=dynamic_cast<const OrderModifyBuilding*>(request.order.get())) return b && b->maxUnitWorking==staffing->numberRequested;
  if(const auto* ratios=dynamic_cast<const OrderModifySwarm*>(request.order.get())) return b && std::equal(std::begin(b->ratio),std::end(b->ratio),ratios->ratio);
  if(const auto* create=dynamic_cast<const OrderCreate*>(request.order.get())) {
   for(const auto& project:context.world.buildProjects)
    if(project.teamNumber==teamNumber && project.typeNum==create->typeNum
     && context.world.normalizeX(project.posX)==context.world.normalizeX(create->posX)
     && context.world.normalizeY(project.posY)==context.world.normalizeY(create->posY)) return true;
   const auto& kind=context.world.catalog->at(create->typeNum);
   for(const auto& building:context.world.buildings)
    if(building.team==teamNumber && (building.typeNum==create->typeNum || (kind.site && building.typeNum==kind.next))
     && building.posX==context.world.normalizeX(create->posX) && building.posY==context.world.normalizeY(create->posY)) return true;
  }
  return false;
 });
}
void AIWarrush::orderExecutionCompleted(const Order& order,bool)
{
 auto& mutableOrder=const_cast<Order&>(order);
 const auto size=mutableOrder.getDataLength();
 std::erase_if(pendingRequests,[&](const auto& request){
  auto& command=*request.order;
  return command.getOrderType()==mutableOrder.getOrderType() && command.getDataLength()==size
   && (size==0 || std::equal(command.getData(),command.getData()+size,mutableOrder.getData()));
 });
}
const AIEngine::TeamView* AIWarrush::teamAt(int index) const
{
 if(index<0 || index>=int(observation->teams.size())) return nullptr;
 return &observation->teams[index];
}
std::shared_ptr<Order> AIWarrush::getOrder()
{
 const auto world=AIEngine::AIWorldView::capture(*game,AIEngine::AIWorldView::captureCatalog(*game));
 const std::vector<AIEngine::ExecutionReceipt> receipts;
 return getOrder(AIEngine::DecisionContext{*world,0,unsigned(teamNumber),receipts});
}
std::shared_ptr<Order> AIWarrush::getOrder(const AIEngine::DecisionContext& context)
{
 if(context.team!=unsigned(teamNumber)) throw std::invalid_argument("Warrush observation has wrong team");
 settleObservedRequests(context);
 AIEngine::WorldQueries captured(context.world,teamNumber,resourceInitializations,context.resourceEnrollments);
 for(const auto& request:pendingRequests)
  if(const auto* create=dynamic_cast<const OrderCreate*>(request.order.get())) captured.reserve(create->typeNum,create->posX,create->posY);
 observation=&context.world;queries=&captured;
 observedTeam=teamAt(teamNumber);
 const auto clear=[&]{observation=nullptr;queries=nullptr;observedTeam=nullptr;};
 try {
  auto order=decide();
  const AIEngine::BuildingView* target=nullptr;
  if(const auto* staffing=dynamic_cast<const OrderModifyBuilding*>(order.get())) target=context.world.buildingAtSlot(staffing->gid);
  if(const auto* ratios=dynamic_cast<const OrderModifySwarm*>(order.get())) target=context.world.buildingAtSlot(ratios->gid);
  if(target || dynamic_cast<const OrderCreate*>(order.get())) pendingRequests.push_back({context.world.tick,context.pollSequence,target?target->identity:BuildingRef{},order});
  clear();return order;
 } catch(...) {clear();throw;}
}

int AIWarrush::numberOfUnitsWithSkillGreaterThanValue(const int skill, const int value)const
{
	telemetry.set(AITrace::AI3::AIWarrush_numberOfUnitsWithSkillGreaterThanValue_input_value,
				  value);
	telemetry.set(AITrace::AI3::AIWarrush_numberOfUnitsWithSkillGreaterThanValue_input_skill,
				  skill);
	telemetry.count(AITrace::AI3::AIWarrush_numberOfUnitsWithSkillGreaterThanValue_calls);
	return telemetry.returnedInt(
		AITrace::AI3::AIWarrush_numberOfUnitsWithSkillGreaterThanValue_result,
		countUnitsIf(*observation, observedTeam, [skill, value](const AIEngine::UnitView *u) { return u->performance[skill] > value; }));
}

int AIWarrush::numberOfUnitsWithSkillEqualToValue(const int skill, const int value)const
{
	telemetry.set(AITrace::AI3::AIWarrush_numberOfUnitsWithSkillEqualToValue_input_value, value);
	telemetry.set(AITrace::AI3::AIWarrush_numberOfUnitsWithSkillEqualToValue_input_skill, skill);
	telemetry.count(AITrace::AI3::AIWarrush_numberOfUnitsWithSkillEqualToValue_calls);
	return telemetry.returnedInt(
		AITrace::AI3::AIWarrush_numberOfUnitsWithSkillEqualToValue_result,
		countUnitsIf(*observation, observedTeam, [skill, value](const AIEngine::UnitView *u) { return u->performance[skill] == value; }));
}

bool AIWarrush::isAnyUnitWithLessThanOneThirdFood()const
{
	telemetry.count(AITrace::AI3::AIWarrush_isAnyUnitWithLessThanOneThirdFood_calls);
	if (observation->rules.hungerDisabled) return false;
	//Yeah, it's a half, not a third. Weird huh? :P
	return telemetry.returnedBool(
		AITrace::AI3::AIWarrush_isAnyUnitWithLessThanOneThirdFood_result,
		AITrace::AI3::AIWarrush_isAnyUnitWithLessThanOneThirdFood_true,
		findUnitIf(*observation, observedTeam,
				   [](const AIEngine::UnitView *u)
				   {
					   return u->hungry < (Unit::HUNGRY_MAX / AI_WARRUSH_HUNGRY_THRESHOLD_DIVISOR);
				   }) != nullptr);
}

bool AIWarrush::provides(const AIEngine::BuildingView& building, Intent intent) const
{
 const int completed = queries->kind(building).site ? queries->kind(building).next : building.typeNum;
 return queries->matches(completed,intent);
}

int AIWarrush::selectBuilding(Intent intent) const
{
 const auto& index = *queries;
 int chosen = -1, count = 0;
 for (const auto& candidate : index.placements(intent))
  if (index.available(candidate,intent) && random() % ++count == 0)
   chosen = candidate.placementType;
 return chosen;
}

const AIEngine::BuildingView *AIWarrush::getSwarmWithoutSettings(const int workerRatio, const int explorerRatio, const int warriorRatio)const
{
 const int ratios[] = {workerRatio, explorerRatio, warriorRatio};
 return findBuildingIf(*observation, observedTeam, [&](const AIEngine::BuildingView *b) {
  if (hasPending(*b) || b->constructionResultState != Building::NO_CONSTRUCTION) return false;
  bool produces = false, differs = false;
  for (int unit = 0; unit < NB_UNIT_TYPE; ++unit) {
   produces |= queries->kind(*b).resolvedType.semantics.production.recipes[unit].enabled;
   const int desired = queries->kind(*b).resolvedType.semantics.production.recipes[unit].enabled ? ratios[unit] : 0;
   differs |= b->ratio[unit] != desired;
  }
  return produces && differs;
 });
}

std::shared_ptr<Order> AIWarrush::staffingOrder() const
{
	constexpr std::array intents = {Intent::ProduceWorker, Intent::ProduceExplorer,
		Intent::ProduceWarrior, Intent::Feed, Intent::TrainAttackStrength};
	constexpr std::array requests = {AI_WARRUSH_SWARM_WORKER_COUNT,
		AI_WARRUSH_SWARM_WORKER_COUNT, AI_WARRUSH_SWARM_WORKER_COUNT,
		AI_WARRUSH_INN_WORKER_COUNT, AI_WARRUSH_BARRACKS_WORKER_COUNT};
	constexpr auto completedIntents = (std::uint64_t{1} << unsigned(Intent::ProduceWorker))
		| (std::uint64_t{1} << unsigned(Intent::Feed));
	std::array<const AIEngine::BuildingView*, intents.size()> candidates{};
	const auto& index = *queries;
	for (int slot = 0; slot < Building::MAX_COUNT; ++slot)
	{
		const AIEngine::BuildingView* building = observation->buildingSlots(observedTeam->number)[slot];
		if (!building || hasPending(*building) || building->maxUnitWorking >= queries->kind(*building).resolvedType.semantics.assignmentLimit)
			continue;
		const int completed = queries->kind(*building).site ? queries->kind(*building).next : building->typeNum;
		auto mask = queries->kind(completed).rawCapabilityMask;
		if (building->constructionResultState == Building::NO_CONSTRUCTION)
			mask &= completedIntents;
		for (std::size_t intent = 0; intent < intents.size(); ++intent)
			if (!candidates[intent] && (mask & (std::uint64_t{1} << unsigned(intents[intent])))
				&& building->maxUnitWorking < requests[intent])
				candidates[intent] = building;
		// Intent priority precedes slot order: a later worker producer wins over
		// an earlier explorer producer. Only the first worker candidate ends the scan.
		if (candidates.front()) break;
	}
	for (std::size_t intent = 0; intent < intents.size(); ++intent)
		if (const AIEngine::BuildingView* building = candidates[intent])
			return std::make_shared<OrderModifyBuilding>(building->identity.gid,
				std::min(requests[intent], queries->kind(*building).resolvedType.semantics.assignmentLimit));
	return std::make_shared<NullOrder>();
}

const AIEngine::BuildingView *AIWarrush::getSwarmAtRandom()const
{
	const auto myBuildings=observation->buildingSlots(observedTeam->number);
	int swarmsfound = 0;
	const AIEngine::BuildingView *chosen_swarm = NULL;
	for (int i=0; i<Building::MAX_COUNT; i++)
	{
		const AIEngine::BuildingView *b=myBuildings[i];
		if ((b) && (provides(*b, Intent::ProduceWorker)))
		{
			++swarmsfound;
			if(random()%swarmsfound == 0)
			{
				chosen_swarm = b;
			}
		}
	}
	return chosen_swarm;
}

bool AIWarrush::allOfBuildingTypeAreCompleted(Intent intent)const
{
	telemetry.set(AITrace::AI3::AIWarrush_allOfBuildingTypeAreCompleted_input_shortTypeNum,
				  static_cast<unsigned>(intent));
	telemetry.count(AITrace::AI3::AIWarrush_allOfBuildingTypeAreCompleted_calls);
	return telemetry.returnedBool(AITrace::AI3::AIWarrush_allOfBuildingTypeAreCompleted_result,
								  AITrace::AI3::AIWarrush_allOfBuildingTypeAreCompleted_true,
								  findBuildingIf(*observation, observedTeam,
												 [this, intent](const AIEngine::BuildingView *b)
												 {
													 return provides(*b, intent) &&
															(b->constructionResultState !=
																 Building::NO_CONSTRUCTION ||
															 b->buildingState == Building::DEAD);
												 }) == nullptr);
}

bool AIWarrush::allOfBuildingTypeAreFull(Intent intent)const
{
	telemetry.set(AITrace::AI3::AIWarrush_allOfBuildingTypeAreFull_input_shortTypeNum,
				  static_cast<unsigned>(intent));
	telemetry.count(AITrace::AI3::AIWarrush_allOfBuildingTypeAreFull_calls);
	return telemetry.returnedBool(AITrace::AI3::AIWarrush_allOfBuildingTypeAreFull_result,
								  AITrace::AI3::AIWarrush_allOfBuildingTypeAreFull_true,
								  findBuildingIf(*observation, observedTeam,
												 [this, intent](const AIEngine::BuildingView *b)
												 {
													 return provides(*b, intent) &&
															b->inside.count <
																(size_t)b->maxUnitInside;
												 }) == nullptr);
}

int AIWarrush::numberOfBuildingsOfType(Intent intent)const
{
	telemetry.set(AITrace::AI3::AIWarrush_numberOfBuildingsOfType_input_shortTypeNum, static_cast<unsigned>(intent));
	telemetry.count(AITrace::AI3::AIWarrush_numberOfBuildingsOfType_calls);
	return telemetry.returnedInt(AITrace::AI3::AIWarrush_numberOfBuildingsOfType_result,
								 countBuildingsIf(*observation, observedTeam, [this, intent](const AIEngine::BuildingView *b)
												  { return provides(*b, intent); }));
}


int AIWarrush::numberOfExtraBuildings()const
{
	telemetry.count(AITrace::AI3::AIWarrush_numberOfExtraBuildings_calls);
	return telemetry.returnedInt(
		AITrace::AI3::AIWarrush_numberOfExtraBuildings_result,
		countBuildingsIf(*observation, observedTeam,
						 [this](const AIEngine::BuildingView *b)
						 {
							 return provides(*b, Intent::Heal) ||
									provides(*b, Intent::TrainWalk) ||
									provides(*b, Intent::TrainSwim) ||
									provides(*b, Intent::TrainConstruction) ||
									provides(*b, Intent::ProjectileDefense);
						 }));
}

bool AIWarrush::allOfBuildingTypeAreFullyWorked(Intent intent)const
{
	telemetry.set(AITrace::AI3::AIWarrush_allOfBuildingTypeAreFullyWorked_input_shortTypeNum,
				  static_cast<unsigned>(intent));
	telemetry.count(AITrace::AI3::AIWarrush_allOfBuildingTypeAreFullyWorked_calls);
	return telemetry.returnedBool(AITrace::AI3::AIWarrush_allOfBuildingTypeAreFullyWorked_result,
								  AITrace::AI3::AIWarrush_allOfBuildingTypeAreFullyWorked_true,
								  findBuildingIf(*observation, observedTeam,
												 [this, intent](const AIEngine::BuildingView *b)
												 {
													 return provides(*b, intent) &&
															b->working.count !=
																(size_t)b->maxUnitWorking;
												 }) == nullptr);
}

bool AIWarrush::percentageOfBuildingsAreFullyWorked(int percentage)const
{
	telemetry.set(AITrace::AI3::AIWarrush_percentageOfBuildingsAreFullyWorked_input_percentage,
				  percentage);
	telemetry.count(AITrace::AI3::AIWarrush_percentageOfBuildingsAreFullyWorked_calls);
	const auto myBuildings=observation->buildingSlots(observedTeam->number);
	int num_buildings = 0;
	int num_worked_buildings = 0;
	for (int i=0; i<Building::MAX_COUNT; i++)
	{
		const AIEngine::BuildingView *b=myBuildings[i];
		if (b && queries->kind(*b).resolvedType.semantics.occupiesGround && queries->kind(*b).resolvedType.maxUnitWorking > 0)
		{
			++num_buildings;
			if(b->working.count == (size_t)b->maxUnitWorking)
			{
				++num_worked_buildings;
				if(verbose)bufferedDiagnostics.push_back({"", "", [&] { std::ostringstream text; text << "A"; return text.str(); }()});
			}
			else if (b->constructionResultState == Building::NO_CONSTRUCTION
    && [&]() {
     bool consumes = false;
     for (int resource = 0; resource < MaterialSlotCount; ++resource) {
      bool required = queries->kind(*b).resolvedType.semantics.feeding.enabled && queries->kind(*b).resolvedType.semantics.feeding.cost[resource] > 0;
      for (const auto& recipe : queries->kind(*b).resolvedType.semantics.production.recipes)
       required |= recipe.enabled && recipe.cost[resource] > 0;
      if (!required) continue;
      consumes = true;
      if (observation->buildingResources(*b)[resource] <= b->wishedMaterials[resource] * AI_WARRUSH_HEAVILY_WORKED_RATIO_NUM / AI_WARRUSH_HEAVILY_WORKED_RATIO_DEN) return false;
     }
     return consumes;
    }())
			{//heavily worked swarms and inns sometimes are full and have no workers
				++num_worked_buildings;
				if(verbose)bufferedDiagnostics.push_back({"", "", [&] { std::ostringstream text; text << "C"; return text.str(); }()});
			}
		}
	}
	if(verbose)bufferedDiagnostics.push_back({"", "", [&] { std::ostringstream text; text << ": " << num_worked_buildings << " worked out of " << num_buildings << "\n"; return text.str(); }()});
	return telemetry.returnedBool(
		AITrace::AI3::AIWarrush_percentageOfBuildingsAreFullyWorked_result,
		AITrace::AI3::AIWarrush_percentageOfBuildingsAreFullyWorked_true,
		num_worked_buildings * 100 >= num_buildings * percentage);
}

std::shared_ptr<Order> AIWarrush::decide()
{
	// reduce delays
	if (buildingDelay > 0)
		buildingDelay--;
	if (areaUpdatingDelay > 0)
		areaUpdatingDelay--;
	
	if(!observation->rules.peaceful && observation->tick < AI_WARRUSH_BOOTSTRAP_EXPLORE_WINDOW && observation->tick%AI_WARRUSH_BOOTSTRAP_EXPLORE_INTERVAL == 0)
	{
		int teamIndex = observation->tick / AI_WARRUSH_BOOTSTRAP_EXPLORE_INTERVAL;
		const AIEngine::TeamView *enemy_team = teamAt(teamIndex);
		if((enemy_team)&&(observedTeam->enemies & enemy_team->mask))return setupExploreFlagForTeam(enemy_team);
	}

	//keep those areas up to date
	if(!observation->rules.peaceful && areaUpdatingDelay == AI_WARRUSH_AREAS_DELAY_TICKS*AI_WARRUSH_AREAS_PRUNE_PHASE_NUM/AI_WARRUSH_AREAS_PRUNE_PHASE_DEN)
		return pruneGuardAreas();
	if(!observation->rules.peaceful && areaUpdatingDelay == AI_WARRUSH_AREAS_DELAY_TICKS/AI_WARRUSH_AREAS_PLACE_PHASE_DEN)
		return placeGuardAreas();
	if(areaUpdatingDelay <= 0)
	{
		areaUpdatingDelay = AI_WARRUSH_AREAS_DELAY_TICKS;
		return farm();
	}

	//assuming we didn't have to mess with areas or explore flags, check if we can build stuff
	if (buildingDelay <= 0)
	{
		bool shouldBuildMore = percentageOfBuildingsAreFullyWorked(AI_WARRUSH_BUILD_MORE_PCT_THRESHOLD);
		if(verbose)if(shouldBuildMore)bufferedDiagnostics.push_back({"", "", [&] { std::ostringstream text; text << "AIWarrush is ready to build more stuff!"; return text.str(); }()});
		//Build another swarm if all are swarms are working at capacity, and if we have other random stuff we should-have / need
		if(verbose)bufferedDiagnostics.push_back({"", "", [&] { std::ostringstream text; text << "Chance to build swarm: "; return text.str(); }()});
		if(shouldBuildMore && allOfBuildingTypeAreCompleted(Intent::ProduceWorker) && numberOfExtraBuildings() >= numberOfBuildingsOfType(Intent::ProduceWorker) && (observation->rules.hungerDisabled || numberOfBuildingsOfType(Intent::Feed) >= numberOfBuildingsOfType(Intent::ProduceWorker) * AI_WARRUSH_INNS_PER_SWARM_RATIO))
		{
			if(verbose)bufferedDiagnostics.push_back({"", "", [&] { std::ostringstream text; text << "TAKEN!\n"; return text.str(); }()});
			return buildBuildingOfType(Intent::ProduceWorker);
		}
		if(verbose)bufferedDiagnostics.push_back({"", "", [&] { std::ostringstream text; text << "ignored.\n"; return text.str(); }()});
		
		//Silly inns. Don't build them right away and don't build too many at a time and don't build too many.
		//More limits than it should have, maybe, but the idea of the AI is that it should either win
		//or lose warriors (so it shouldn't need so many inns.)
		if(verbose)bufferedDiagnostics.push_back({"", "", [&] { std::ostringstream text; text << "Chance to build inn: "; return text.str(); }()});
		if(isAnyUnitWithLessThanOneThirdFood()
				&&
				shouldBuildMore
				&&
				 numberOfExtraBuildings() >= numberOfBuildingsOfType(Intent::Feed) - AI_WARRUSH_INN_LOOKAHEAD
				&&
				(
				 (allOfBuildingTypeAreCompleted(Intent::Feed)
				&& allOfBuildingTypeAreFull(Intent::Feed))
				|| allOfBuildingTypeAreFullyWorked(Intent::Feed))
				  )
		{
			if(verbose)bufferedDiagnostics.push_back({"", "", [&] { std::ostringstream text; text << "TAKEN!\n"; return text.str(); }()});
			return buildBuildingOfType(Intent::Feed);
		}
		if(verbose)bufferedDiagnostics.push_back({"", "", [&] { std::ostringstream text; text << "ignored.\n"; return text.str(); }()});
		
		//if the barracks are all working at capacity,
		//build more barracks! (this also builds the first barracks...)
		if(verbose)bufferedDiagnostics.push_back({"", "", [&] { std::ostringstream text; text << "Chance to build barracks: "; return text.str(); }()});
		if(
			!observation->rules.peaceful && !observation->rules.upgradesDisabled
			&& allOfBuildingTypeAreCompleted(Intent::TrainAttackStrength)
			&& allOfBuildingTypeAreFull(Intent::TrainAttackStrength)
				)
		{
			if(verbose)bufferedDiagnostics.push_back({"", "", [&] { std::ostringstream text; text << "TAKEN!\n"; return text.str(); }()});
			return buildBuildingOfType(Intent::TrainAttackStrength);
		}
		if(verbose)bufferedDiagnostics.push_back({"", "", [&] { std::ostringstream text; text << "ignored.\n"; return text.str(); }()});
	
		//and if we have excess workers, build random other buildings!
		if(verbose)bufferedDiagnostics.push_back({"", "", [&] { std::ostringstream text; text << "Chance to build etc: "; return text.str(); }()});
		if(shouldBuildMore)
		{
			if(verbose)bufferedDiagnostics.push_back({"", "", [&] { std::ostringstream text; text << "TAKEN!\n"; return text.str(); }()});
			Intent type;
			int random_number = random()%AI_WARRUSH_RANDOM_BUILDING_DENOM;
			if(random_number < AI_WARRUSH_HEAL_PCT_THRESHOLD || numberOfBuildingsOfType(Intent::Heal) == 0)type = Intent::Heal;
			else if(random_number < AI_WARRUSH_WALKSPEED_PCT_THRESHOLD || numberOfBuildingsOfType(Intent::TrainWalk) == 0)type = Intent::TrainWalk;
			else if(random_number < AI_WARRUSH_SWIMSPEED_PCT_THRESHOLD)type = Intent::TrainSwim;
			else if(random_number < AI_WARRUSH_SCIENCE_PCT_THRESHOLD)type = Intent::TrainConstruction;
			else type = Intent::ProjectileDefense;
			// Skip unavailable choices before committing a construction turn. A rejected
			// barracks or school must not repeatedly preempt production and staffing.
			if (!queries->allowed(type)) type=Intent::Heal;
			return buildBuildingOfType(type);
		}
		if(verbose)bufferedDiagnostics.push_back({"", "", [&] { std::ostringstream text; text << "ignored.\n"; return text.str(); }()});
	}

	//If we have enough workers, we can switch to dedicated warrushing production.
	// With training off, a level threshold would leave every swarm in its opening mix forever.
	if((observation->rules.upgradesDisabled ? observedTeam->statistics.numberUnitPerType[WORKER]
		: numberOfUnitsWithSkillGreaterThanValue(HARVEST,0)) >= AI_WARRUSH_HARVESTER_THRESHOLD)
	{
		//This is basically a way to change all the swarms without bothering to remember
		//anything. (It can only issue one order per tick, so it has to do it over several
		//ticks and calculate the orders separately.)
		const int warriors=observation->rules.peaceful ? 0 : AI_WARRUSH_SWARM_RATIO_WARRIOR;
        if(auto order=queries->missingProductionOrder({AI_WARRUSH_SWARM_RATIO_WORKER,AI_WARRUSH_SWARM_RATIO_EXPLORER,warriors},AI_WARRUSH_SWARM_WORKER_COUNT,AI_WARRUSH_SWARM_WORKER_COUNT)) return order;
		const AIEngine::BuildingView *out_of_date_swarm = getSwarmWithoutSettings(AI_WARRUSH_SWARM_RATIO_WORKER, AI_WARRUSH_SWARM_RATIO_EXPLORER, warriors);
		if(out_of_date_swarm)
		{
			Sint32 settings[3] = {AI_WARRUSH_SWARM_RATIO_WORKER, AI_WARRUSH_SWARM_RATIO_EXPLORER, warriors};
   for (int unit = 0; unit < NB_UNIT_TYPE; ++unit)
    if (!queries->kind(*out_of_date_swarm).resolvedType.semantics.production.recipes[unit].enabled) settings[unit] = 0;
			return shared_ptr<Order>(new OrderModifySwarm(out_of_date_swarm->identity.gid, settings));
		}
	}

	return staffingOrder();
}

std::shared_ptr<Order> AIWarrush::pruneGuardAreas()
{
	telemetry.count(AITrace::AI3::AIWarrush_pruneGuardAreas_calls);
	//If we have any guard areas that aren't adjacent to an enemy building, we remove them.
	BrushAccumulator acc;
	for(int x=0;x<observation->width;x++)
	{
		for(int y=0;y<observation->height;y++)
		{
			if(queries->isGuardArea(x,y,observedTeam->mask))
			{
				bool keep = false;
				for(int xmod=-1;xmod<=1;xmod++)
				{
					for(int ymod=-1;ymod<=1;ymod++)
					{
						//if there's a building...
						if(queries->getBuilding(x+xmod,y+ymod)!=NOGBID)
						{
							//...AND it's an enemy building...
							if(observedTeam->enemies & teamAt(Building::GIDtoTeam(queries->getBuilding(x+xmod,y+ymod)))->mask)
							{
								//...then we still want it guarded.
								keep=true;
							}
						}
					}
				}
				if(!keep)
				{
					acc.applyBrush(BrushApplication(x,y,0),observation->width,observation->height);
				}
			}
		}
	}
	if(acc.getApplicationCount())
	{
		return telemetry.returnedOrder(AITrace::AI3::AIWarrush_pruneGuardAreas_result,
									   AIEngine::observationAreaOrder<OrderAlterGuardArea>(teamNumber,BrushTool::MODE_DEL,acc));
	}
	else
		return telemetry.returnedOrder(AITrace::AI3::AIWarrush_pruneGuardAreas_result,
									   shared_ptr<Order>(new NullOrder));
}
	
std::shared_ptr<Order> AIWarrush::placeGuardAreas()
{
	telemetry.count(AITrace::AI3::AIWarrush_placeGuardAreas_calls);
	BrushAccumulator guard_add_acc;
	//Place guard area on an enemy building if there is one...
	for(int i=0;i<Team::MAX_COUNT;i++)
	{
		const AIEngine::TeamView *t = teamAt(i);
		if((t)&&(observedTeam->enemies & t->mask))
		{
			for(int j=0;j<Building::MAX_COUNT;j++)
			{
				const AIEngine::BuildingView *b = observation->buildingSlots(t->number)[j];
				if ((b)&&(b->buildingState != Building::DEAD)&&(b->hp != 1 || b->constructionResultState == Building::NO_CONSTRUCTION)&&queries->kind(*b).resolvedType.semantics.occupiesGround)
				{
					const BuildingType *bt = &queries->kind(*b).resolvedType;
					if(
							//the area must be discovered to prevent AI cheating.
							(
									queries->isFOWDiscovered(b->posX,               b->posY,              observedTeam->mask)
								||	queries->isFOWDiscovered(b->posX+bt->width - 1, b->posY,              observedTeam->mask)
								||	queries->isFOWDiscovered(b->posX+bt->width - 1, b->posY+bt->height-1, observedTeam->mask)
								||	queries->isFOWDiscovered(b->posX,               b->posY+bt->height-1, observedTeam->mask)
									)
							&&
							//do not order a building attacked if the order is already in place.
							(
								!queries->isGuardArea(b->posX,b->posY,observedTeam->mask)
									)			
										)
					{
						if((queries->getBuilding(b->posX, b->posY)!=NOGBID)&&(observedTeam->enemies & teamAt(Building::GIDtoTeam(queries->getBuilding(b->posX, b->posY)))->mask)) //paranoia
						{
							telemetry.set(AITrace::AI3::AIWarrush_placeGuardAreas_last_team, i);
							for(int x = 0; x < bt->width; x++)
							{
								for(int y = 0; y < bt->height; y++)
								{
									guard_add_acc.applyBrush(BrushApplication((b->posX+x) % queries->getW(), (b->posY+y) % queries->getH(),AI_WARRUSH_GUARD_BRUSH_SIZE),observation->width,observation->height);
								}
							}
						}
					}
				}
			}
		}
	}
	
	if(guard_add_acc.getApplicationCount())
	{
		return telemetry.returnedOrder(
			AITrace::AI3::AIWarrush_placeGuardAreas_result,
			AIEngine::observationAreaOrder<OrderAlterGuardArea>(teamNumber,BrushTool::MODE_ADD,guard_add_acc));
	}
	else
		return telemetry.returnedOrder(AITrace::AI3::AIWarrush_placeGuardAreas_result,
									   shared_ptr<Order>(new NullOrder));
}
	
std::shared_ptr<Order> AIWarrush::farm()
{
	// This checkerboard reserves seed tiles for future growth. Without regrowth,
	// protecting those finite resources would make them permanently unusable.
	if (observation->rules.resourceGrowthDisabled) return std::make_shared<NullOrder>();
	telemetry.count(AITrace::AI3::AIWarrush_farm_calls);
	// Algorithm initially stolen from Nicowar.
	DynamicGradientMapArray water_gradient(observation->width,observation->height);
	for(int x=0;x<observation->width;x++)
	{
		for(int y=0;y<observation->height;y++)
		{	
			if (terrainProvidesFertility(queries->terrainPropertiesAt(x,y)))
			{
				water_gradient(x, y) = AI_WARRUSH_GRADIENT_MAX;
			}
			else
			{
				water_gradient(x, y) = 1;
			}
		}
	}
	queries->updateGlobalGradient(water_gradient.c_array());

	BrushAccumulator del_acc;
	BrushAccumulator add_acc;
	BrushAccumulator clr_del_acc;
	BrushAccumulator clr_add_acc;
	// With the farm-areas experiment, wheat near water is farmed with a farm
	// area on the same wheat checkerboard.
	const bool farms = queries->farmAreasEnabled();
	BrushAccumulator farm_del_acc;
	BrushAccumulator farm_add_acc;
	for(int x=0;x<observation->width;x++)
	{
		for(int y=0;y<observation->height;y++)
		{
			const auto type=observation->resourceAt(observation->tileIndex(x,y)).resource.type;
			const auto properties=type==NO_RES_TYPE ? ResourceProperties{} : observation->state().resourceProperties(type);
			const bool reserveFood=AIResourcePolicy::needsSeedReserve(observation->state(),observation->tileIndex(x,y),MaterialId::Food);
			const bool reserveWood=AIResourcePolicy::needsSeedReserve(observation->state(),observation->tileIndex(x,y),MaterialId::Wood);
			const bool nearGrowth=properties.ecology!=ResourceEcology::Land
				|| water_gradient(x, y) > (AI_WARRUSH_GRADIENT_MAX - AI_WARRUSH_WATER_NEAR_OFFSET);
			const bool wheat_spot = x%2==y%2 && reserveFood
				&& queries->isMapDiscovered(x, y, observedTeam->mask)
				&& nearGrowth;
			const auto foodYield=reserveFood ? (*observation->state().resourceRegistry).yields(static_cast<ResourceId>(observation->resourceAt(observation->tileIndex(x,y)).resource.type))[materialIndex(MaterialId::Food)] : YieldProperties{};
			const bool wheat_farm = farms && wheat_spot && properties.farmable
					&& foodYield.consumption==ResourceConsumption::One && !foodYield.destroysDeposit && queries->canPaintFarmArea(x, y);
			if(farms && queries->isMapDiscovered(x, y, observedTeam->mask))
			{
				const bool farmed = queries->isFarmArea(x, y, observedTeam->mask);
				if(wheat_farm && !farmed)
					farm_add_acc.applyBrush(BrushApplication(x, y, 0),observation->width,observation->height);
				else if(!wheat_farm && farmed)
					farm_del_acc.applyBrush(BrushApplication(x, y, 0),observation->width,observation->height);
				// The farm replaces forbidden paint on wheat.
				if(queries->isForbidden(x, y, observedTeam->mask)
				   && wheat_farm && !reserveWood)
					del_acc.applyBrush(BrushApplication(x, y, 0),observation->width,observation->height);
			}

			if(!reserveWood && !reserveFood)
			{
				if(queries->isForbidden(x, y, observedTeam->mask))
				{
					if(
						//Make sure we're not deleting buildings' forbidden area!
						!queries->isForbidden (x + 1,y,observedTeam->mask)
						&& !queries->isForbidden (x - 1,y,observedTeam->mask)
						&& !queries->isForbidden (x,y + 1,observedTeam->mask)
						&& !queries->isForbidden (x,y - 1,observedTeam->mask)
						//Or fruits'!
						&& !MapState::hasMaterial(observation->state(),observation->tileIndex(x,y),MaterialId::Cherries)
						&& !MapState::hasMaterial(observation->state(),observation->tileIndex(x,y),MaterialId::Oranges)
						&& !MapState::hasMaterial(observation->state(),observation->tileIndex(x,y),MaterialId::Prunes)
						)
					{
						del_acc.applyBrush(BrushApplication(x, y, 0),observation->width,observation->height);
					}
				}
			}
			
			if(queries->isForbidden(x, y, observedTeam->mask) && queries->isClearArea(x, y, observedTeam->mask))
			{
				del_acc.applyBrush(BrushApplication(x, y, 0),observation->width,observation->height);
			}
			
			const bool woodThreat=properties.clearable && !MapState::hasMaterial(observation->state(),observation->tileIndex(x,y),MaterialId::Food)
				&& AIResourcePolicy::canPropagate(observation->state(),observation->tileIndex(x,y),MaterialId::Wood);
			if(!woodThreat)
			{
				if(queries->isClearArea(x, y, observedTeam->mask))
				{
					bool besideBuilding=false;
					for(int dx=-1;dx<=1;++dx) for(int dy=-1;dy<=1;++dy) {
						const auto gid=queries->getBuilding(x+dx,y+dy);
						besideBuilding|=gid!=NOGBID && Building::GIDtoTeam(gid)==team->teamNumber;
					}
					if(!besideBuilding) clr_del_acc.applyBrush(BrushApplication(x, y, 0),observation->width,observation->height);
				}
			}

			// Clear spreading wood threats without destroying a mixed food source.
			if(woodThreat)
			{
				if(!queries->isClearArea(x, y, observedTeam->mask) && queries->isMapDiscovered(x, y, observedTeam->mask))
				{
					for(int xmod=-1;xmod<=1;xmod++)
					{
						for(int ymod=-1;ymod<=1;ymod++)
						{
							if(MapState::hasMaterial(observation->state(),observation->tileIndex(x+xmod,y+ymod),MaterialId::Food)
									|| (queries->getBuilding(x+xmod,y+ymod)!=NOGBID
									&& (observedTeam->mask & teamAt(Building::GIDtoTeam(queries->getBuilding(x+xmod,y+ymod)))->mask)))
							{
								clr_add_acc.applyBrush(BrushApplication(x, y, 0),observation->width,observation->height);
								goto doublebreak;
							}
						}
					}
					doublebreak:
					/*statement for label to point to*/;
				}
			}


			if(x%2==1 && ((y%2==1 && x%4==1) || (y%2==0 && x%4==3)))
			{
				if(reserveWood)
				{
					if(!queries->isForbidden(x, y, observedTeam->mask) && !queries->isClearArea(x, y, observedTeam->mask) && queries->isMapDiscovered(x, y, observedTeam->mask) && nearGrowth)
					{
						add_acc.applyBrush(BrushApplication(x, y, 0),observation->width,observation->height);
					}
				}
			}

			if(!wheat_farm && wheat_spot && !queries->isForbidden(x, y, observedTeam->mask))
				add_acc.applyBrush(BrushApplication(x, y, 0),observation->width,observation->height);

			//FORBID FRUITS!!! They're horrible for our warriors and we hate converting.
			if(
				(	MapState::hasMaterial(observation->state(),observation->tileIndex(x,y),MaterialId::Cherries)
					|| MapState::hasMaterial(observation->state(),observation->tileIndex(x,y),MaterialId::Oranges)
					|| MapState::hasMaterial(observation->state(),observation->tileIndex(x,y),MaterialId::Prunes)	)
				&& !queries->isForbidden(x, y, observedTeam->mask)
				&& queries->isMapDiscovered(x, y, observedTeam->mask)
					)
			{
				add_acc.applyBrush(BrushApplication(x, y, 0),observation->width,observation->height);
			}

		}
	}

	if(del_acc.getApplicationCount()>0)
		return telemetry.returnedOrder(AITrace::AI3::AIWarrush_farm_result,
									   AIEngine::observationAreaOrder<OrderAlterForbidden>(teamNumber,BrushTool::MODE_DEL,del_acc));
	if(add_acc.getApplicationCount()>0)
		return telemetry.returnedOrder(AITrace::AI3::AIWarrush_farm_result,
									   AIEngine::observationAreaOrder<OrderAlterForbidden>(teamNumber,BrushTool::MODE_ADD,add_acc));
	if(clr_del_acc.getApplicationCount()>0)
		return telemetry.returnedOrder(
			AITrace::AI3::AIWarrush_farm_result,
			shared_ptr<Order>(
				AIEngine::observationAreaOrder<OrderAlterClearArea>(teamNumber,BrushTool::MODE_DEL,clr_del_acc)));
	if(clr_add_acc.getApplicationCount()>0)
		return telemetry.returnedOrder(
			AITrace::AI3::AIWarrush_farm_result,
			shared_ptr<Order>(
				AIEngine::observationAreaOrder<OrderAlterClearArea>(teamNumber,BrushTool::MODE_ADD,clr_add_acc)));
	if(farm_del_acc.getApplicationCount()>0)
		return telemetry.returnedOrder(AITrace::AI3::AIWarrush_farm_result,
									   AIEngine::observationAreaOrder<OrderAlterFarmArea>(teamNumber,BrushTool::MODE_DEL,farm_del_acc));
	if(farm_add_acc.getApplicationCount()>0)
		return telemetry.returnedOrder(AITrace::AI3::AIWarrush_farm_result,
									   AIEngine::observationAreaOrder<OrderAlterFarmArea>(teamNumber,BrushTool::MODE_ADD,farm_add_acc));

	//nothing to do...
	return telemetry.returnedOrder(AITrace::AI3::AIWarrush_farm_result,
								   shared_ptr<Order>(new NullOrder()));
}

//Simple hack to place explore flags on opponents' starting swarms.
std::shared_ptr<Order> AIWarrush::setupExploreFlagForTeam(const AIEngine::TeamView *enemy_team)
{
	telemetry.count(AITrace::AI3::AIWarrush_setupExploreFlagForTeam_calls);
 const int typeNum = selectBuilding(Intent::AttractExplorers);
 if (typeNum < 0) return std::make_shared<NullOrder>();
 auto placeNear = [&](int x, int y) -> std::shared_ptr<Order> {
  for (int radius = 0; radius <= 8; ++radius)
   for (int dx = -radius; dx <= radius; ++dx)
    for (int dy = -radius; dy <= radius; ++dy)
     if (queries->checkRoomForBuilding(x + dx,y + dy,typeNum,teamNumber))
      return queries->createOrder(teamNumber, x + dx, y + dy, typeNum, 1, 1);
  return std::make_shared<NullOrder>();
 };
	if(verbose)bufferedDiagnostics.push_back({"", "", [&] { std::ostringstream text; text << "looking for swarms:\n"; return text.str(); }()});
	for(int j=0;j<Building::MAX_COUNT;j++)
	{
		const AIEngine::BuildingView *b = observation->buildingSlots(enemy_team->number)[j];
		if((b)&&(provides(*b, Intent::ProduceWorker))&&(b->constructionResultState == Building::NO_CONSTRUCTION))
		{
			return telemetry.returnedOrder(AITrace::AI3::AIWarrush_setupExploreFlagForTeam_result,
										   placeNear(b->posX, b->posY));
		}
	}
	if(verbose)bufferedDiagnostics.push_back({"", "", [&] { std::ostringstream text; text << "No swarms found\n"; return text.str(); }()});
	//what, they have no swarm? o_O Find any building:
	for(int j=0;j<Building::MAX_COUNT;j++)
	{
		const AIEngine::BuildingView *b = observation->buildingSlots(enemy_team->number)[j];
		if(b)
		{
			return telemetry.returnedOrder(AITrace::AI3::AIWarrush_setupExploreFlagForTeam_result,
										   placeNear(b->posX, b->posY));
		}
	}
	if(verbose)bufferedDiagnostics.push_back({"", "", [&] { std::ostringstream text; text << "No buildings found\n"; return text.str(); }()});
	//what, they have no buildings? o_O Find any unit:
	for(int j=0;j<Unit::MAX_COUNT;j++)
	{
		const AIEngine::UnitView *u = observation->unitSlots(enemy_team->number)[j];
		if(u)
		{
			return telemetry.returnedOrder(AITrace::AI3::AIWarrush_setupExploreFlagForTeam_result,
										   placeNear(u->posX, u->posY));
		}
	}
	//what, enemy has no buildings or units at the beginning of the game? o_O O_o o_O
	if(verbose)bufferedDiagnostics.push_back({"", "", [&] { std::ostringstream text; text << "No buildings found, no units found o_O\n"; return text.str(); }()});
	return telemetry.returnedOrder(AITrace::AI3::AIWarrush_setupExploreFlagForTeam_result,
								   shared_ptr<Order>(new NullOrder));
}

bool AIWarrush::locationIsAvailableForBuilding(int x, int y, int width, int height)
{
	/*if(queries->isHardSpaceForBuilding(x,y,width,height))
	{*/
		if(		queries->isMapDiscovered(x,			y,			observedTeam->mask)
			||	queries->isMapDiscovered(x+width-1,	y,			observedTeam->mask)
			||	queries->isMapDiscovered(x+width-1,	y+height-1,	observedTeam->mask)
			||	queries->isMapDiscovered(x,			y+height-1,	observedTeam->mask)
				)
		{
			return true;
		}
	/*}*/
	return false;
}

void AIWarrush::initializeGradientWithResource(DynamicGradientMapArray &gradient, Uint8 resource_type)
{
	for(int x=0;x<observation->width;x++)
	{
		for(int y=0;y<observation->height;y++)
		{
			const auto index=observation->tileIndex(x,y);
			if (MapState::hasMaterialSlot(observation->state(),observation->tileIndex(x,y),resource_type))
			{
				gradient(x, y) = AI_WARRUSH_GRADIENT_MAX;
			}
			else if (MapState::resourceBlocksGround(observation->state(),index))
			{
				gradient(x, y) = 0;
			}
			else if (observation->occupancyAt(index).building!=NOGBID)
			{
				gradient(x, y) = 0;
			}
			else if (!observation->terrain->properties(observation->terrainAt(index).type).walkable)
			{
				gradient(x, y) = 0;
			}
			else
			{ //has to be desert or grass with no buildings or resources, at this point.
				gradient(x, y) = 1;
			}
		}
	}
	
	queries->updateGlobalGradient(gradient.c_array());
	
	for(int x=0;x<observation->width;x++)
	{
		for(int y=0;y<observation->height;y++)
		{
			if (gradient(x, y) == AI_WARRUSH_GRADIENT_MAX)
				gradient(x, y) = 0;
			else
				gradient(x, y)++;
		}
	}
}

std::shared_ptr<Order> AIWarrush::buildBuildingOfType(Intent intent)
{
 buildingDelay = AI_WARRUSH_BUILDING_DELAY_TICKS;
 const int typeNum = selectBuilding(intent);
 if (typeNum < 0) return std::make_shared<NullOrder>();
 const BuildingType* bt = &queries->kind(typeNum).resolvedType;
 const BuildingType* complete = bt->isBuildingSite ? &queries->kind(bt->nextLevel).resolvedType : bt;
 telemetry.set(AITrace::AI3::AIWarrush_buildBuildingOfType_input_shortTypeNum, static_cast<unsigned>(intent));
 telemetry.count(AITrace::AI3::AIWarrush_buildBuildingOfType_calls);

	// set delay
	// now doing this first in order to avoid repeated failed builds
	// WARNING THIS IS A HACK FIX
	// in reality, if it fails to build, it should go on and get another order.
	buildingDelay = AI_WARRUSH_BUILDING_DELAY_TICKS;

 // Prefer the dominant recurring input, falling back to construction cost.
 std::array<int, MaterialSlotCount> demand{};
 for (int r = 0; r < MaterialSlotCount; ++r) {
  demand[r] += complete->semantics.feeding.enabled ? complete->semantics.feeding.cost[r] : 0;
  demand[r] += complete->semantics.healing.enabled ? complete->semantics.healing.cost[r] : 0;
  for (const auto& recipe : complete->semantics.production.recipes)
   if (recipe.enabled) demand[r] += recipe.cost[r];
 }
 int resource = std::distance(demand.begin(), std::max_element(demand.begin(), demand.end()));
 if (demand[resource] == 0) {
  demand=bt->semantics.constructionCost;
  resource=std::distance(demand.begin(),std::max_element(demand.begin(),demand.end()));
 }
 const bool needsResource=demand[resource]>0;
 DynamicGradientMapArray resource_gradient(observation->width, observation->height);
 if(needsResource) initializeGradientWithResource(resource_gradient, resource);

	DynamicGradientMapArray availability_gradient(observation->width,observation->height);
	for(int x=0;x<observation->width;x++)
	{
		for(int y=0;y<observation->height;y++)
		{
			const auto index=observation->tileIndex(x,y);
			if (MapState::resourceBlocksBuilding(observation->state(),index))
			{
				availability_gradient(x, y) = 0;
			}
			else if (observation->occupancyAt(index).building!=NOGBID)
			{
				availability_gradient(x, y) = 0;
			}
			else if (!observation->terrain->properties(observation->terrainAt(index).type).walkable)
			{
				availability_gradient(x, y) = 0;
			}
			else if(queries->isHardSpaceForBuilding(x-(bt->width / AI_WARRUSH_BUILDING_CENTER_DIVISOR),y-(bt->width / AI_WARRUSH_BUILDING_CENTER_DIVISOR),bt->width*AI_WARRUSH_BUILDING_CLEARANCE_MULT,bt->height*AI_WARRUSH_BUILDING_CLEARANCE_MULT) && locationIsAvailableForBuilding(x,y,bt->width,bt->height)) //the extra numbers at the ends expand the building
			{
				availability_gradient(x, y) = AI_WARRUSH_GRADIENT_MAX;
			}
			else availability_gradient(x, y) = 1;
		}
	}
	
	queries->updateGlobalGradient(availability_gradient.c_array());
	
	const AIEngine::BuildingView *swarm = getSwarmAtRandom();
	if (!swarm)
	{
		if(verbose)bufferedDiagnostics.push_back({"", "", [&] { std::ostringstream text; text << "No swarm found!\n"; return text.str(); }()});
		return telemetry.returnedOrder(AITrace::AI3::AIWarrush_buildBuildingOfType_result,
									   shared_ptr<Order>(new NullOrder));
	}
	Sint32 destination_x,destination_y;
	{
		Sint32 x,y;
		Sint32 x_temp,y_temp;
		
		x = swarm->posX; y = swarm->posY;
		
  if(needsResource && queries->getGlobalGradientDestination(resource_gradient.c_array(), x, y, &x_temp, &y_temp))
  { x = x_temp; y = y_temp; }
		bool result = queries->getGlobalGradientDestination(availability_gradient.c_array(), x, y, &destination_x, &destination_y);
		
		if(verbose)bufferedDiagnostics.push_back({"", "", [&] { std::ostringstream text; text << "Trying to build " << static_cast<unsigned>(intent) << "(" << bt->width << " x " << bt->height << ")" << " at " << destination_x << "," << destination_y << " from " << x << "," << y << " swarm is " << swarm->posX << "," << swarm->posY << ", found = " << result << std::endl; return text.str(); }()});
		if(verbose)if((int)availability_gradient(destination_x, destination_y) != AI_WARRUSH_GRADIENT_MAX)bufferedDiagnostics.push_back({"", "", [&] { std::ostringstream text; text << "Could not find valid location for building! Best spot: " << destination_x << "," << destination_y << " (" << (int)availability_gradient(destination_x, destination_y) << ")\n"; return text.str(); }()});
	}
		
	// create and return order
	if (!queries->checkRoomForBuilding(destination_x,destination_y,typeNum,teamNumber)) return std::make_shared<NullOrder>();
	return telemetry.returnedOrder(
		AITrace::AI3::AIWarrush_buildBuildingOfType_result,
		queries->createOrder(teamNumber,destination_x,destination_y,typeNum,1,1));
}
