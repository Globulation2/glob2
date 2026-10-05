#include "AIRuleOrders.h"
// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière
// Copyright (C) 2005 Eli Dupree

#include "AITelemetryFields.h"
#include "AIWarrush.h"
#include "AIWarrushTuning.h"
#include "AIStateSerialization.h"
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
	int countUnitsIf(const Team *team, Pred p)
	{
		int n = 0;
		for (int i = 0; i < Unit::MAX_COUNT; i++)
		{
			Unit *u = team->myUnits[i];
			if (u && p(u)) n++;
		}
		return n;
	}

	template<typename Pred>
	Unit *findUnitIf(const Team *team, Pred p)
	{
		for (int i = 0; i < Unit::MAX_COUNT; i++)
		{
			Unit *u = team->myUnits[i];
			if (u && p(u)) return u;
		}
		return nullptr;
	}

	template<typename Pred>
	int countBuildingsIf(const Team *team, Pred p)
	{
		int n = 0;
		for (int i = 0; i < Building::MAX_COUNT; i++)
		{
			Building *b = team->myBuildings[i];
			if (b && p(b)) n++;
		}
		return n;
	}

	template<typename Pred>
	Building *findBuildingIf(const Team *team, Pred p)
	{
		for (int i = 0; i < Building::MAX_COUNT; i++)
		{
			Building *b = team->myBuildings[i];
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
	buildingDelay = 0;
	areaUpdatingDelay = 0;
	
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
	if (versionMinor >= AI_WARRUSH_SAVE_FORMAT_CONTINUATION)
	{
		stream->readEnterSection("AIWarrush");
		buildingDelay = AIStateSerialization::readSint32(stream, "buildingDelay");
		areaUpdatingDelay = AIStateSerialization::readSint32(stream, "areaUpdatingDelay");
		stream->readLeaveSection();
		if (buildingDelay < 0 || buildingDelay > AI_WARRUSH_BUILDING_DELAY_TICKS ||
			areaUpdatingDelay < 0 || areaUpdatingDelay > AI_WARRUSH_AREAS_DELAY_TICKS)
			return false;
	}
	return stream->isValid();
}

void AIWarrush::save(GAGCore::OutputStream *stream)
{
	stream->writeEnterSection("AIWarrush");
	stream->writeSint32(buildingDelay, "buildingDelay");
	stream->writeSint32(areaUpdatingDelay, "areaUpdatingDelay");
	stream->writeLeaveSection();
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
		countUnitsIf(team, [skill, value](Unit *u) { return u->performance[skill] > value; }));
}

int AIWarrush::numberOfUnitsWithSkillEqualToValue(const int skill, const int value)const
{
	telemetry.set(AITrace::AI3::AIWarrush_numberOfUnitsWithSkillEqualToValue_input_value, value);
	telemetry.set(AITrace::AI3::AIWarrush_numberOfUnitsWithSkillEqualToValue_input_skill, skill);
	telemetry.count(AITrace::AI3::AIWarrush_numberOfUnitsWithSkillEqualToValue_calls);
	return telemetry.returnedInt(
		AITrace::AI3::AIWarrush_numberOfUnitsWithSkillEqualToValue_result,
		countUnitsIf(team, [skill, value](Unit *u) { return u->performance[skill] == value; }));
}

bool AIWarrush::isAnyUnitWithLessThanOneThirdFood()const
{
	telemetry.count(AITrace::AI3::AIWarrush_isAnyUnitWithLessThanOneThirdFood_calls);
	if (game->gameHeader.isHungerDisabled()) return false;
	//Yeah, it's a half, not a third. Weird huh? :P
	return telemetry.returnedBool(
		AITrace::AI3::AIWarrush_isAnyUnitWithLessThanOneThirdFood_result,
		AITrace::AI3::AIWarrush_isAnyUnitWithLessThanOneThirdFood_true,
		findUnitIf(team,
				   [](Unit *u)
				   {
					   return u->hungry < (Unit::HUNGRY_MAX / AI_WARRUSH_HUNGRY_THRESHOLD_DIVISOR);
				   }) != nullptr);
}

bool AIWarrush::provides(const Building& building, Intent intent) const
{
 const int completed = building.type->isBuildingSite ? building.type->nextLevel : building.typeNum;
 return game->buildingCapabilities().matches(completed, intent);
}

int AIWarrush::selectBuilding(Intent intent) const
{
 const auto& index = game->buildingCapabilities();
 int chosen = -1, count = 0;
 for (const auto& candidate : index.placements(intent))
  if (index.available(candidate, intent, game->gameHeader) && random() % ++count == 0)
   chosen = candidate.placementType;
 return chosen;
}

Building *AIWarrush::getSwarmWithoutSettings(const int workerRatio, const int explorerRatio, const int warriorRatio)const
{
 const int ratios[] = {workerRatio, explorerRatio, warriorRatio};
 return findBuildingIf(team, [&](Building *b) {
  if (b->constructionResultState != Building::NO_CONSTRUCTION) return false;
  bool produces = false, differs = false;
  for (int unit = 0; unit < NB_UNIT_TYPE; ++unit) {
   produces |= b->type->semantics.production.recipes[unit].enabled;
   const int desired = b->type->semantics.production.recipes[unit].enabled ? ratios[unit] : 0;
   differs |= b->ratio[unit] != desired;
  }
  return produces && differs;
 });
}

Building *AIWarrush::getBuildingWithoutWorkersAssigned(Intent intent, int num_workers)const
{
 return findBuildingIf(team, [=, this](Building *b) {
  return provides(*b, intent)
   && b->maxUnitWorking < std::min(num_workers, b->type->semantics.assignmentLimit)
   && (b->constructionResultState != Building::NO_CONSTRUCTION
    || intent == Intent::ProduceWorker || intent == Intent::Feed);
 });
}

Building *AIWarrush::getSwarmAtRandom()const
{
	Building **myBuildings=team->myBuildings;
	int swarmsfound = 0;
	Building *chosen_swarm = NULL;
	for (int i=0; i<Building::MAX_COUNT; i++)
	{
		Building *b=myBuildings[i];
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
								  findBuildingIf(team,
												 [this, intent](Building *b)
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
								  findBuildingIf(team,
												 [this, intent](Building *b)
												 {
													 return provides(*b, intent) &&
															b->unitsInside.size() <
																(size_t)b->maxUnitInside;
												 }) == nullptr);
}

int AIWarrush::numberOfBuildingsOfType(Intent intent)const
{
	telemetry.set(AITrace::AI3::AIWarrush_numberOfBuildingsOfType_input_shortTypeNum, static_cast<unsigned>(intent));
	telemetry.count(AITrace::AI3::AIWarrush_numberOfBuildingsOfType_calls);
	return telemetry.returnedInt(AITrace::AI3::AIWarrush_numberOfBuildingsOfType_result,
								 countBuildingsIf(team, [this, intent](Building *b)
												  { return provides(*b, intent); }));
}


int AIWarrush::numberOfExtraBuildings()const
{
	telemetry.count(AITrace::AI3::AIWarrush_numberOfExtraBuildings_calls);
	return telemetry.returnedInt(
		AITrace::AI3::AIWarrush_numberOfExtraBuildings_result,
		countBuildingsIf(team,
						 [this](Building *b)
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
								  findBuildingIf(team,
												 [this, intent](Building *b)
												 {
													 return provides(*b, intent) &&
															b->unitsWorking.size() !=
																(size_t)b->maxUnitWorking;
												 }) == nullptr);
}

bool AIWarrush::percentageOfBuildingsAreFullyWorked(int percentage)const
{
	telemetry.set(AITrace::AI3::AIWarrush_percentageOfBuildingsAreFullyWorked_input_percentage,
				  percentage);
	telemetry.count(AITrace::AI3::AIWarrush_percentageOfBuildingsAreFullyWorked_calls);
	Building **myBuildings=team->myBuildings;
	int num_buildings = 0;
	int num_worked_buildings = 0;
	for (int i=0; i<Building::MAX_COUNT; i++)
	{
		Building *b=myBuildings[i];
		if (b && b->type->semantics.occupiesGround && b->type->maxUnitWorking > 0)
		{
			++num_buildings;
			if(b->unitsWorking.size() == (size_t)b->maxUnitWorking)
			{
				++num_worked_buildings;
				if(verbose)std::cout << "A";
			}
			else if (b->constructionResultState == Building::NO_CONSTRUCTION
    && [&]() {
     bool consumes = false;
     for (int resource = 0; resource < MAX_NB_RESOURCES; ++resource) {
      bool required = b->type->semantics.feeding.enabled && b->type->semantics.feeding.cost[resource] > 0;
      for (const auto& recipe : b->type->semantics.production.recipes)
       required |= recipe.enabled && recipe.cost[resource] > 0;
      if (!required) continue;
      consumes = true;
      if (b->resources[resource] <= b->wishedResources[resource] * AI_WARRUSH_HEAVILY_WORKED_RATIO_NUM / AI_WARRUSH_HEAVILY_WORKED_RATIO_DEN) return false;
     }
     return consumes;
    }())
			{//heavily worked swarms and inns sometimes are full and have no workers
				++num_worked_buildings;
				if(verbose)std::cout << "C";
			}
		}
	}
	if(verbose)std::cout << ": " << num_worked_buildings << " worked out of " << num_buildings << "\n";
	return telemetry.returnedBool(
		AITrace::AI3::AIWarrush_percentageOfBuildingsAreFullyWorked_result,
		AITrace::AI3::AIWarrush_percentageOfBuildingsAreFullyWorked_true,
		num_worked_buildings * 100 >= num_buildings * percentage);
}

std::shared_ptr<Order> AIWarrush::getOrder(void)
{
	// reduce delays
	if (buildingDelay > 0)
		buildingDelay--;
	if (areaUpdatingDelay > 0)
		areaUpdatingDelay--;
	
	if(!game->gameHeader.isPeacefulModeEnabled() && game->stepCounter < AI_WARRUSH_BOOTSTRAP_EXPLORE_WINDOW && game->stepCounter%AI_WARRUSH_BOOTSTRAP_EXPLORE_INTERVAL == 0)
	{
		int teamIndex = game->stepCounter / AI_WARRUSH_BOOTSTRAP_EXPLORE_INTERVAL;
		Team *enemy_team = game->teams[teamIndex];
		if((enemy_team)&&(team->attackableTeams() & enemy_team->me))return setupExploreFlagForTeam(enemy_team);
	}

	//keep those areas up to date
	if(!game->gameHeader.isPeacefulModeEnabled() && areaUpdatingDelay == AI_WARRUSH_AREAS_DELAY_TICKS*AI_WARRUSH_AREAS_PRUNE_PHASE_NUM/AI_WARRUSH_AREAS_PRUNE_PHASE_DEN)
		return pruneGuardAreas();
	if(!game->gameHeader.isPeacefulModeEnabled() && areaUpdatingDelay == AI_WARRUSH_AREAS_DELAY_TICKS/AI_WARRUSH_AREAS_PLACE_PHASE_DEN)
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
		if(verbose)if(shouldBuildMore)std::cout << "AIWarrush is ready to build more stuff!";
		//Build another swarm if all are swarms are working at capacity, and if we have other random stuff we should-have / need
		if(verbose)std::cout << "Chance to build swarm: ";
		if(shouldBuildMore && allOfBuildingTypeAreCompleted(Intent::ProduceWorker) && numberOfExtraBuildings() >= numberOfBuildingsOfType(Intent::ProduceWorker) && (game->gameHeader.isHungerDisabled() || numberOfBuildingsOfType(Intent::Feed) >= numberOfBuildingsOfType(Intent::ProduceWorker) * AI_WARRUSH_INNS_PER_SWARM_RATIO))
		{
			if(verbose)std::cout << "TAKEN!\n";
			return buildBuildingOfType(Intent::ProduceWorker);
		}
		if(verbose)std::cout << "ignored.\n";
		
		//Silly inns. Don't build them right away and don't build too many at a time and don't build too many.
		//More limits than it should have, maybe, but the idea of the AI is that it should either win
		//or lose warriors (so it shouldn't need so many inns.)
		if(verbose)std::cout << "Chance to build inn: ";
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
			if(verbose)std::cout << "TAKEN!\n";
			return buildBuildingOfType(Intent::Feed);
		}
		if(verbose)std::cout << "ignored.\n";
		
		//if the barracks are all working at capacity,
		//build more barracks! (this also builds the first barracks...)
		if(verbose)std::cout << "Chance to build barracks: ";
		if(
			!game->gameHeader.isPeacefulModeEnabled() && !game->gameHeader.isUnitUpgradesDisabled()
			&& allOfBuildingTypeAreCompleted(Intent::TrainAttackStrength)
			&& allOfBuildingTypeAreFull(Intent::TrainAttackStrength)
				)
		{
			if(verbose)std::cout << "TAKEN!\n";
			return buildBuildingOfType(Intent::TrainAttackStrength);
		}
		if(verbose)std::cout << "ignored.\n";
	
		//and if we have excess workers, build random other buildings!
		if(verbose)std::cout << "Chance to build etc: ";
		if(shouldBuildMore)
		{
			if(verbose)std::cout << "TAKEN!\n";
			Intent type;
			int random_number = random()%AI_WARRUSH_RANDOM_BUILDING_DENOM;
			if(random_number < AI_WARRUSH_HEAL_PCT_THRESHOLD || numberOfBuildingsOfType(Intent::Heal) == 0)type = Intent::Heal;
			else if(random_number < AI_WARRUSH_WALKSPEED_PCT_THRESHOLD || numberOfBuildingsOfType(Intent::TrainWalk) == 0)type = Intent::TrainWalk;
			else if(random_number < AI_WARRUSH_SWIMSPEED_PCT_THRESHOLD)type = Intent::TrainSwim;
			else if(random_number < AI_WARRUSH_SCIENCE_PCT_THRESHOLD)type = Intent::TrainConstruction;
			else type = Intent::ProjectileDefense;
			// Skip unavailable choices before committing a construction turn. A rejected
			// barracks or school must not repeatedly preempt production and staffing.
			if (!AIPlanning::BuildingCapabilityIndex::allowed(type, game->gameHeader)) type=Intent::Heal;
			return buildBuildingOfType(type);
		}
		if(verbose)std::cout << "ignored.\n";
	}

	//If we have enough workers, we can switch to dedicated warrushing production.
	// With training off, a level threshold would leave every swarm in its opening mix forever.
	if((game->gameHeader.isUnitUpgradesDisabled() ? team->stats.getLatestStat()->numberUnitPerType[WORKER]
		: numberOfUnitsWithSkillGreaterThanValue(HARVEST,0)) >= AI_WARRUSH_HARVESTER_THRESHOLD)
	{
		//This is basically a way to change all the swarms without bothering to remember
		//anything. (It can only issue one order per tick, so it has to do it over several
		//ticks and calculate the orders separately.)
		const int warriors=game->gameHeader.isPeacefulModeEnabled() ? 0 : AI_WARRUSH_SWARM_RATIO_WARRIOR;
        if(auto order=AIPlanning::missingProductionOrder(*game,*team,{AI_WARRUSH_SWARM_RATIO_WORKER,AI_WARRUSH_SWARM_RATIO_EXPLORER,warriors},AI_WARRUSH_SWARM_WORKER_COUNT,AI_WARRUSH_SWARM_WORKER_COUNT)) return order;
		Building *out_of_date_swarm = getSwarmWithoutSettings(AI_WARRUSH_SWARM_RATIO_WORKER, AI_WARRUSH_SWARM_RATIO_EXPLORER, warriors);
		if(out_of_date_swarm)
		{
			Sint32 settings[3] = {AI_WARRUSH_SWARM_RATIO_WORKER, AI_WARRUSH_SWARM_RATIO_EXPLORER, warriors};
   for (int unit = 0; unit < NB_UNIT_TYPE; ++unit)
    if (!out_of_date_swarm->type->semantics.production.recipes[unit].enabled) settings[unit] = 0;
			return shared_ptr<Order>(new OrderModifySwarm(out_of_date_swarm->gid, settings));
		}
	}

	//all swarms should always have 5 workers at them!
    Building* weak_swarm=nullptr;
    for(int unit=0;unit<NB_UNIT_TYPE && !weak_swarm;++unit)
        weak_swarm=getBuildingWithoutWorkersAssigned(static_cast<Intent>(unit),AI_WARRUSH_SWARM_WORKER_COUNT);
	if (weak_swarm) return shared_ptr<Order>(new OrderModifyBuilding(weak_swarm->gid, std::min(AI_WARRUSH_SWARM_WORKER_COUNT, weak_swarm->type->semantics.assignmentLimit)));

	//all inns should always have 3 workers at them! (best to build fast, make sure they're fed)
	Building *weak_inn = getBuildingWithoutWorkersAssigned(Intent::Feed, AI_WARRUSH_INN_WORKER_COUNT);
	if (weak_inn) return shared_ptr<Order>(new OrderModifyBuilding(weak_inn->gid, std::min(AI_WARRUSH_INN_WORKER_COUNT, weak_inn->type->semantics.assignmentLimit)));

	//work barracks more too.
	Building *weak_barracks = getBuildingWithoutWorkersAssigned(Intent::TrainAttackStrength, AI_WARRUSH_BARRACKS_WORKER_COUNT);
	if (weak_barracks && weak_barracks->constructionResultState != Building::NO_CONSTRUCTION)
	 return shared_ptr<Order>(new OrderModifyBuilding(weak_barracks->gid, std::min(AI_WARRUSH_BARRACKS_WORKER_COUNT, weak_barracks->type->semantics.assignmentLimit)));
	
	//nothing at all to do?!
	return shared_ptr<Order>(new NullOrder);
}

std::shared_ptr<Order> AIWarrush::pruneGuardAreas()
{
	telemetry.count(AITrace::AI3::AIWarrush_pruneGuardAreas_calls);
	//If we have any guard areas that aren't adjacent to an enemy building, we remove them.
	BrushAccumulator acc;
	for(int x=0;x<map->w;x++)
	{
		for(int y=0;y<map->h;y++)
		{
			if(map->isGuardArea(x,y,team->me))
			{
				bool keep = false;
				for(int xmod=-1;xmod<=1;xmod++)
				{
					for(int ymod=-1;ymod<=1;ymod++)
					{
						//if there's a building...
						if(map->getBuilding(x+xmod,y+ymod)!=NOGBID)
						{
							//...AND it's an enemy building...
							if(team->attackableTeams() & game->teams[Building::GIDtoTeam(map->getBuilding(x+xmod,y+ymod))]->me)
							{
								//...then we still want it guarded.
								keep=true;
							}
						}
					}
				}
				if(!keep)
				{
					acc.applyBrush(BrushApplication(x,y,0), map);
				}
			}
		}
	}
	if(acc.getApplicationCount())
	{
		return telemetry.returnedOrder(AITrace::AI3::AIWarrush_pruneGuardAreas_result,
									   shared_ptr<Order>(new OrderAlterGuardArea(
										   team->teamNumber, BrushTool::MODE_DEL, &acc, map)));
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
		Team *t = game->teams[i];
		if((t)&&(team->attackableTeams() & t->me))
		{
			for(int j=0;j<Building::MAX_COUNT;j++)
			{
				Building *b = t->myBuildings[j];
				if ((b)&&(b->buildingState != Building::DEAD)&&(b->hp != 1 || b->constructionResultState == Building::NO_CONSTRUCTION)&&b->type->semantics.occupiesGround)
				{
					BuildingType *bt = b->type;
					if(
							//the area must be discovered to prevent AI cheating.
							(
									map->isFOWDiscovered(b->posX,               b->posY,              team->me)
								||	map->isFOWDiscovered(b->posX+bt->width - 1, b->posY,              team->me)
								||	map->isFOWDiscovered(b->posX+bt->width - 1, b->posY+bt->height-1, team->me)
								||	map->isFOWDiscovered(b->posX,               b->posY+bt->height-1, team->me)
									)
							&&
							//do not order a building attacked if the order is already in place.
							(
								!map->isGuardArea(b->posX,b->posY,team->me)
									)			
										)
					{
						if((map->getBuilding(b->posX, b->posY)!=NOGBID)&&(team->attackableTeams() & game->teams[Building::GIDtoTeam(map->getBuilding(b->posX, b->posY))]->me)) //paranoia
						{
							telemetry.set(AITrace::AI3::AIWarrush_placeGuardAreas_last_team, i);
							for(int x = 0; x < bt->width; x++)
							{
								for(int y = 0; y < bt->height; y++)
								{
									guard_add_acc.applyBrush(BrushApplication((b->posX+x) % map->getW(), (b->posY+y) % map->getH(),AI_WARRUSH_GUARD_BRUSH_SIZE), map);
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
			shared_ptr<Order>(new OrderAlterGuardArea(team->teamNumber, BrushTool::MODE_ADD,
													  &guard_add_acc, map)));
	}
	else
		return telemetry.returnedOrder(AITrace::AI3::AIWarrush_placeGuardAreas_result,
									   shared_ptr<Order>(new NullOrder));
}
	
std::shared_ptr<Order> AIWarrush::farm()
{
	// This checkerboard reserves seed tiles for future growth. Without regrowth,
	// protecting those finite resources would make them permanently unusable.
	if (game->gameHeader.isResourceGrowthDisabled()) return std::make_shared<NullOrder>();
	telemetry.count(AITrace::AI3::AIWarrush_farm_calls);
	// Algorithm initially stolen from Nicowar.
	DynamicGradientMapArray water_gradient(map->w,map->h);
	for(int x=0;x<map->w;x++)
	{
		for(int y=0;y<map->h;y++)
		{	
			if (terrainProvidesFertility(map->terrainPropertiesAt(x,y)))
			{
				water_gradient(x, y) = AI_WARRUSH_GRADIENT_MAX;
			}
			else
			{
				water_gradient(x, y) = 1;
			}
		}
	}
	map->updateGlobalGradient(water_gradient.c_array());

	BrushAccumulator del_acc;
	BrushAccumulator add_acc;
	BrushAccumulator clr_del_acc;
	BrushAccumulator clr_add_acc;
	// With the farm-areas experiment, wheat near water is farmed with a farm
	// area on the same wheat checkerboard.
	const bool farms = map->farmAreasEnabled();
	BrushAccumulator farm_del_acc;
	BrushAccumulator farm_add_acc;
	for(int x=0;x<map->w;x++)
	{
		for(int y=0;y<map->h;y++)
		{
			const bool wheat_spot = x%2==y%2 && map->isResourceTakeable(x, y, WHEAT)
				&& map->isMapDiscovered(x, y, team->me)
				&& water_gradient(x, y) > (AI_WARRUSH_GRADIENT_MAX - AI_WARRUSH_WATER_NEAR_OFFSET);
			if(farms && map->isMapDiscovered(x, y, team->me))
			{
				const bool wheat_farm = wheat_spot && map->canPaintFarmArea(x, y);
				const bool farmed = map->isFarmArea(x, y, team->me);
				if(wheat_farm && !farmed)
					farm_add_acc.applyBrush(BrushApplication(x, y, 0), map);
				else if(!wheat_farm && farmed)
					farm_del_acc.applyBrush(BrushApplication(x, y, 0), map);
				// The farm replaces forbidden paint on wheat.
				if(map->isForbidden(x, y, team->me)
				   && map->isResourceTakeable(x, y, WHEAT))
					del_acc.applyBrush(BrushApplication(x, y, 0), map);
			}

			if((!map->isResourceTakeable(x, y, WOOD) && !map->isResourceTakeable(x, y, WHEAT)))
			{
				if(map->isForbidden(x, y, team->me))
				{
					if(
						//Make sure we're not deleting buildings' forbidden area!
						!map->isForbidden (x + 1,y,team->me)
						&& !map->isForbidden (x - 1,y,team->me)
						&& !map->isForbidden (x,y + 1,team->me)
						&& !map->isForbidden (x,y - 1,team->me)
						//Or fruits'!
						&& !map->isResourceTakeable(x, y, CHERRY)
						&& !map->isResourceTakeable(x, y, ORANGE)
						&& !map->isResourceTakeable(x, y, PRUNE)
						)
					{
						del_acc.applyBrush(BrushApplication(x, y, 0), map);
					}
				}
			}
			
			if(map->isForbidden(x, y, team->me) && map->isClearArea(x, y, team->me))
			{
				del_acc.applyBrush(BrushApplication(x, y, 0), map);
			}
			
			//we never clear anything but wood
			if(!map->isResourceTakeable(x, y, WOOD))
			{
				if(map->isClearArea(x, y, team->me))
				{
					clr_del_acc.applyBrush(BrushApplication(x, y, 0), map);
				}
			}

			//we clear wood if it's next to nice stuff like wheat or buildings
			if(map->isResourceTakeable(x, y, WOOD))
			{
				if(!map->isClearArea(x, y, team->me) && map->isMapDiscovered(x, y, team->me))
				{
					for(int xmod=-1;xmod<=1;xmod++)
					{
						for(int ymod=-1;ymod<=1;ymod++)
						{
							if(map->isResourceTakeable(x+xmod, y+ymod, WHEAT)
									|| (map->getBuilding(x+xmod,y+ymod)!=NOGBID
									&& (team->me & game->teams[Building::GIDtoTeam(map->getBuilding(x+xmod,y+ymod))]->me)))
							{
								clr_add_acc.applyBrush(BrushApplication(x, y, 0), map);
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
				if(map->isResourceTakeable(x, y, WOOD))
				{
					if(!map->isForbidden(x, y, team->me) && !map->isClearArea(x, y, team->me) && map->isMapDiscovered(x, y, team->me) && water_gradient(x, y) > (AI_WARRUSH_GRADIENT_MAX - AI_WARRUSH_WATER_NEAR_OFFSET))
					{
						add_acc.applyBrush(BrushApplication(x, y, 0), map);
					}
				}
			}

			if(!farms && wheat_spot && !map->isForbidden(x, y, team->me))
				add_acc.applyBrush(BrushApplication(x, y, 0), map);

			//FORBID FRUITS!!! They're horrible for our warriors and we hate converting.
			if(
				(	map->isResourceTakeable(x, y, CHERRY)
					|| map->isResourceTakeable(x, y, ORANGE)
					|| map->isResourceTakeable(x, y, PRUNE)	)
				&& !map->isForbidden(x, y, team->me)
				&& map->isMapDiscovered(x, y, team->me)
					)
			{
				add_acc.applyBrush(BrushApplication(x, y, 0), map);
			}

		}
	}

	if(del_acc.getApplicationCount()>0)
		return telemetry.returnedOrder(AITrace::AI3::AIWarrush_farm_result,
									   shared_ptr<Order>(new OrderAlterForbidden(
										   team->teamNumber, BrushTool::MODE_DEL, &del_acc, map)));
	if(add_acc.getApplicationCount()>0)
		return telemetry.returnedOrder(AITrace::AI3::AIWarrush_farm_result,
									   shared_ptr<Order>(new OrderAlterForbidden(
										   team->teamNumber, BrushTool::MODE_ADD, &add_acc, map)));
	if(clr_del_acc.getApplicationCount()>0)
		return telemetry.returnedOrder(
			AITrace::AI3::AIWarrush_farm_result,
			shared_ptr<Order>(
				new OrderAlterClearArea(team->teamNumber, BrushTool::MODE_DEL, &clr_del_acc, map)));
	if(clr_add_acc.getApplicationCount()>0)
		return telemetry.returnedOrder(
			AITrace::AI3::AIWarrush_farm_result,
			shared_ptr<Order>(
				new OrderAlterClearArea(team->teamNumber, BrushTool::MODE_ADD, &clr_add_acc, map)));
	if(farm_del_acc.getApplicationCount()>0)
		return telemetry.returnedOrder(AITrace::AI3::AIWarrush_farm_result,
									   shared_ptr<Order>(new OrderAlterFarmArea(
										   team->teamNumber, BrushTool::MODE_DEL, &farm_del_acc, map)));
	if(farm_add_acc.getApplicationCount()>0)
		return telemetry.returnedOrder(AITrace::AI3::AIWarrush_farm_result,
									   shared_ptr<Order>(new OrderAlterFarmArea(
										   team->teamNumber, BrushTool::MODE_ADD, &farm_add_acc, map)));

	//nothing to do...
	return telemetry.returnedOrder(AITrace::AI3::AIWarrush_farm_result,
								   shared_ptr<Order>(new NullOrder()));
}

//Simple hack to place explore flags on opponents' starting swarms.
std::shared_ptr<Order> AIWarrush::setupExploreFlagForTeam(Team *enemy_team)
{
	telemetry.count(AITrace::AI3::AIWarrush_setupExploreFlagForTeam_calls);
 const int typeNum = selectBuilding(Intent::AttractExplorers);
 if (typeNum < 0) return std::make_shared<NullOrder>();
 auto placeNear = [&](int x, int y) -> std::shared_ptr<Order> {
  for (int radius = 0; radius <= 8; ++radius)
   for (int dx = -radius; dx <= radius; ++dx)
    for (int dy = -radius; dy <= radius; ++dy)
     if (game->checkRoomForBuilding(x + dx, y + dy, game->buildingsTypes.get(typeNum), team->teamNumber))
      return AIRules::createOrder(*game, team->teamNumber, x + dx, y + dy, typeNum, 1, 1);
  return std::make_shared<NullOrder>();
 };
	if(verbose)std::cout << "looking for swarms:\n";
	for(int j=0;j<Building::MAX_COUNT;j++)
	{
		Building *b = enemy_team->myBuildings[j];
		if((b)&&(provides(*b, Intent::ProduceWorker))&&(b->constructionResultState == Building::NO_CONSTRUCTION))
		{
			return telemetry.returnedOrder(AITrace::AI3::AIWarrush_setupExploreFlagForTeam_result,
										   placeNear(b->posX, b->posY));
		}
	}
	if(verbose)std::cout << "No swarms found\n";
	//what, they have no swarm? o_O Find any building:
	for(int j=0;j<Building::MAX_COUNT;j++)
	{
		Building *b = enemy_team->myBuildings[j];
		if(b)
		{
			return telemetry.returnedOrder(AITrace::AI3::AIWarrush_setupExploreFlagForTeam_result,
										   placeNear(b->posX, b->posY));
		}
	}
	if(verbose)std::cout << "No buildings found\n";
	//what, they have no buildings? o_O Find any unit:
	for(int j=0;j<Unit::MAX_COUNT;j++)
	{
		Unit *u = enemy_team->myUnits[j];
		if(u)
		{
			return telemetry.returnedOrder(AITrace::AI3::AIWarrush_setupExploreFlagForTeam_result,
										   placeNear(u->posX, u->posY));
		}
	}
	//what, enemy has no buildings or units at the beginning of the game? o_O O_o o_O
	if(verbose)std::cout << "No buildings found, no units found o_O\n";
	return telemetry.returnedOrder(AITrace::AI3::AIWarrush_setupExploreFlagForTeam_result,
								   shared_ptr<Order>(new NullOrder));
}

bool AIWarrush::locationIsAvailableForBuilding(int x, int y, int width, int height)
{
	/*if(map->isHardSpaceForBuilding(x,y,width,height))
	{*/
		if(		map->isMapDiscovered(x,			y,			team->me)
			||	map->isMapDiscovered(x+width-1,	y,			team->me)
			||	map->isMapDiscovered(x+width-1,	y+height-1,	team->me)
			||	map->isMapDiscovered(x,			y+height-1,	team->me)
				)
		{
			return true;
		}
	/*}*/
	return false;
}

void AIWarrush::initializeGradientWithResource(DynamicGradientMapArray &gradient, Uint8 resource_type)
{
	for(int x=0;x<map->w;x++)
	{
		for(int y=0;y<map->h;y++)
		{
			Tile c=map->getTile(x,y);
			if (c.resource.type==resource_type)
			{
				gradient(x, y) = AI_WARRUSH_GRADIENT_MAX;
			}
			else if (c.resource.type!=NO_RES_TYPE)
			{
				gradient(x, y) = 0;
			}
			else if (c.building!=NOGBID)
			{
				gradient(x, y) = 0;
			}
			else if (!map->terrainPropertiesAt(x,y).walkable)
			{
				gradient(x, y) = 0;
			}
			else
			{ //has to be desert or grass with no buildings or resources, at this point.
				gradient(x, y) = 1;
			}
		}
	}
	
	map->updateGlobalGradient(gradient.c_array());
	
	for(int x=0;x<map->w;x++)
	{
		for(int y=0;y<map->h;y++)
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
 const BuildingType* bt = game->buildingsTypes.get(typeNum);
 const BuildingType* complete = bt->isBuildingSite ? game->buildingsTypes.get(bt->nextLevel) : bt;
 telemetry.set(AITrace::AI3::AIWarrush_buildBuildingOfType_input_shortTypeNum, static_cast<unsigned>(intent));
 telemetry.count(AITrace::AI3::AIWarrush_buildBuildingOfType_calls);

	// set delay
	// now doing this first in order to avoid repeated failed builds
	// WARNING THIS IS A HACK FIX
	// in reality, if it fails to build, it should go on and get another order.
	buildingDelay = AI_WARRUSH_BUILDING_DELAY_TICKS;

 // Prefer the dominant recurring input, falling back to construction cost.
 std::array<int, MAX_NB_RESOURCES> demand{};
 for (int r = 0; r < MAX_NB_RESOURCES; ++r) {
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
 DynamicGradientMapArray resource_gradient(map->w, map->h);
 if(needsResource) initializeGradientWithResource(resource_gradient, resource);

	DynamicGradientMapArray availability_gradient(map->w,map->h);
	for(int x=0;x<map->w;x++)
	{
		for(int y=0;y<map->h;y++)
		{
			Tile c=map->getTile(x,y);
			if (c.resource.type!=NO_RES_TYPE)
			{
				availability_gradient(x, y) = 0;
			}
			else if (c.building!=NOGBID)
			{
				availability_gradient(x, y) = 0;
			}
			else if (!map->terrainPropertiesAt(x,y).walkable)
			{
				availability_gradient(x, y) = 0;
			}
			else if(map->isHardSpaceForBuilding(x-(bt->width / AI_WARRUSH_BUILDING_CENTER_DIVISOR),y-(bt->width / AI_WARRUSH_BUILDING_CENTER_DIVISOR),bt->width*AI_WARRUSH_BUILDING_CLEARANCE_MULT,bt->height*AI_WARRUSH_BUILDING_CLEARANCE_MULT) && locationIsAvailableForBuilding(x,y,bt->width,bt->height)) //the extra numbers at the ends expand the building
			{
				availability_gradient(x, y) = AI_WARRUSH_GRADIENT_MAX;
			}
			else availability_gradient(x, y) = 1;
		}
	}
	
	map->updateGlobalGradient(availability_gradient.c_array());
	
	Building *swarm = getSwarmAtRandom();
	if (!swarm)
	{
		if(verbose)std::cout << "No swarm found!\n";
		return telemetry.returnedOrder(AITrace::AI3::AIWarrush_buildBuildingOfType_result,
									   shared_ptr<Order>(new NullOrder));
	}
	Sint32 destination_x,destination_y;
	{
		Sint32 x,y;
		Sint32 x_temp,y_temp;
		
		x = swarm->posX; y = swarm->posY;
		
  if(needsResource && map->getGlobalGradientDestination(resource_gradient.c_array(), x, y, &x_temp, &y_temp))
  { x = x_temp; y = y_temp; }
		bool result = map->getGlobalGradientDestination(availability_gradient.c_array(), x, y, &destination_x, &destination_y);
		
		if(verbose)std::cout << "Trying to build " << static_cast<unsigned>(intent) << "(" << bt->width << " x " << bt->height << ")" << " at " << destination_x << "," << destination_y << " from " << x << "," << y << " swarm is " << swarm->posX << "," << swarm->posY << ", found = " << result << std::endl;
		if(verbose)if((int)availability_gradient(destination_x, destination_y) != AI_WARRUSH_GRADIENT_MAX)std::cout << "Could not find valid location for building! Best spot: " << destination_x << "," << destination_y << " (" << (int)availability_gradient(destination_x, destination_y) << ")\n";
	}
		
	// create and return order
	if (!game->checkRoomForBuilding(destination_x, destination_y, bt, team->teamNumber)) return std::make_shared<NullOrder>();
	return telemetry.returnedOrder(
		AITrace::AI3::AIWarrush_buildBuildingOfType_result,
		AIRules::createOrder(*game,team->teamNumber,destination_x,destination_y,typeNum,1,1));
}
