// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière


#include "AICastor.h"
#include "ai/observation/WorldQueries.h"
#include "Game.h"
#include "Version.h"
#include "Order.h"
#include "Player.h"
#include "Unit.h"

#define AI_FILE_MIN_VERSION 1
#define AI_FILE_VERSION 2

using std::shared_ptr;


const AIEngine::TeamView* AICastor::teamAt(int index) const
{
 if(index<0 || index>=int(observation->teams.size())) return nullptr;
 return &observation->teams[index];
}
std::shared_ptr<Order> AICastor::getOrder()
{
 const auto world=AIEngine::AIWorldView::capture(*game,AIEngine::AIWorldView::captureCatalog(*game));
 const std::vector<AIEngine::ExecutionReceipt> receipts;
 std::vector<AIEngine::ResourceEnrollmentRequest> enrollments;
 AIEngine::DecisionContext context{*world,0,unsigned(teamNumber),receipts};context.resourceEnrollments=&enrollments;
 auto result=getOrder(context);
 for(const auto& request:enrollments) map->installObservedResourceField(request.team,request.resource,request.swim,*request.initialField);
 return result;
}
std::shared_ptr<Order> AICastor::getOrder(const AIEngine::DecisionContext& context)
{
 if(context.team!=unsigned(teamNumber)) throw std::invalid_argument("Castor observation has wrong team");
 for(const auto& receipt:context.receipts) if(!receipt.command.empty()) {
  auto order=Order::getOrder(receipt.command.data(),receipt.command.size(),VERSION_MINOR);
  if(order) {
   bool matches=false;
   if(const auto* staffing=dynamic_cast<const OrderModifyBuilding*>(order.get())) {
    const auto pending=pendingWorkers.find(staffing->gid);
    matches=pending!=pendingWorkers.end() && pending->second.tick==receipt.request.observedTick && pending->second.sequence==receipt.request.pollSequence;
   }
   if(const auto* ratios=dynamic_cast<const OrderModifySwarm*>(order.get())) {
    const auto pending=pendingRatios.find(ratios->gid);
    matches=pending!=pendingRatios.end() && pending->second.tick==receipt.request.observedTick && pending->second.sequence==receipt.request.pollSequence;
   }
   if(matches) orderExecutionCompleted(*order,receipt.status==AIEngine::ExecutionStatus::Accepted);
  }
  std::erase_if(pendingCreates,[&](const auto& intent){return intent.tick==receipt.request.observedTick && intent.sequence==receipt.request.pollSequence;});
 }
 std::erase_if(pendingCreates,[&](const auto& intent){
  for(const auto& project:context.world.buildProjects) if(project.teamNumber==teamNumber && project.typeNum==intent.type && context.world.normalizeX(project.posX)==context.world.normalizeX(intent.x) && context.world.normalizeY(project.posY)==context.world.normalizeY(intent.y)) return true;
  const auto& kind=context.world.catalog->at(intent.type);
  for(const auto& b:context.world.buildings) if(b.team==teamNumber && (b.typeNum==intent.type || (kind.site && b.typeNum==kind.next)) && b.posX==context.world.normalizeX(intent.x) && b.posY==context.world.normalizeY(intent.y)) return true;
  return false;
 });
 AIEngine::WorldQueries captured(context.world,teamNumber,resourceInitializations,context.resourceEnrollments);
 for(const auto& intent:pendingCreates) captured.reserve(intent.type,intent.x,intent.y);
 decisionSequence=context.pollSequence;
 observation=&context.world;queries=&captured;
 observedTeam=teamAt(teamNumber);
 const auto clear=[&]{observation=nullptr;queries=nullptr;observedTeam=nullptr;};
 try {
  auto result=decide();
  if(const auto* create=dynamic_cast<const OrderCreate*>(result.get())) pendingCreates.push_back({context.world.tick,context.pollSequence,create->typeNum,create->posX,create->posY});
  clear();return result;
 } catch(...) {clear();throw;}
}

std::shared_ptr<Order>AICastor::decide()
{
	reconcilePendingAssignments();
	timer++;
	
	if (!strategy.defined)
		defineStrategy();
	
	if (computeBoot<AI_CASTOR_BOOT_IDLE_TICKS)
	{
		computeBoot++;
		return shared_ptr<Order>(new NullOrder());
	}
	else if (computeBoot<AI_CASTOR_BOOT_COMPUTE_STEPS+AI_CASTOR_BOOT_IDLE_TICKS)
	{
		switch (computeBoot-AI_CASTOR_BOOT_IDLE_TICKS)
		{
			case 0:
			computeHydratationMap();
			break;
			case 1:
			computeNotGrassMap();
			break;
			case 2:
			computeCanSwim();
			break;
			case 3:
			computeNeedSwim();
			break;
			case 4:
			computeBuildingSum();
			break;
			case 5:
			computeWarLevel();
			break;
			case 6:
			computeObstacleUnitMap();
			break;
			case 7:
			computeObstacleBuildingMap();
			break;
			case 8:
			computeWorkPowerMap();
			break;
			case 9:
			computeWorkRangeMap();
			break;
			case 10:
			computeWorkAbilityMap();
			break;
			case 11:
			computeHydratationMap();
			break;
			case 12:
			{
				size_t size=observation->width*observation->height;
				copyWheatGradient(oldWheatGradient[0]);
				for (int i=1; i<4; i++)
					memcpy(oldWheatGradient[i], oldWheatGradient[0], size);
				for (int i=0; i<2; i++)
					memset(wheatCareMap[i], 1, size);
			}
			break;
			case 13:
			computeWheatGrowthMap();
			break;
			case 14:
			computeEnemyPowerMap();
			break;
			case 15:
			computeEnemyRangeMap();
			break;
			case 16:
			computeEnemyWarriorsMap();
			break;
			default:
			assert(false);
		}
		computeBoot++;
		return shared_ptr<Order>(new NullOrder());
	}
	
	if ((timer&AI_CASTOR_WHEAT_HISTORY_INTERVAL_MASK)==0)
	{
		Uint8 *temp=oldWheatGradient[3];
		for (int i=3; i>0; i--)
			oldWheatGradient[i]=oldWheatGradient[i-1];
		oldWheatGradient[0]=temp;
		copyWheatGradient(oldWheatGradient[0]);
		computeObstacleUnitMap();
		computeWheatCareMap();
	}
	
		
	//printf("getOrder(), %d projects\n", projects.size());
	for (std::list<Project *>::iterator pi=projects.begin(); pi!=projects.end();)
		if ((*pi)->finished)
		{
			//printf("deleting project (%s)\n", (*pi)->debugName);
			delete *pi;
			pi=projects.erase(pi);
		}
		else
			pi++;
	bool blocking=false;
	for (std::list<Project *>::iterator pi=projects.begin(); pi!=projects.end(); pi++)
		if ((*pi)->blocking)
			blocking=true;
	
	computeBuildingSum();
	
	if (!blocking)// No blocking project, we can start a new one:
		addProjects();
	Sint32 priority=AICastor::AI_CASTOR_PRIORITY_NONE;
	for (std::list<Project *>::iterator pi=projects.begin(); pi!=projects.end(); pi++)
		if (priority>(*pi)->priority && (*pi)->critical)
			priority=(*pi)->priority;
	
	if (timer>controlSwarmsTimer)
	{
		computeWarLevel();
		controlSwarmsTimer=timer+AI_CASTOR_CONTROL_SWARMS_INTERVAL; // each 10s
		std::shared_ptr<Order>order=controlSwarms();
		if (order)
			return order;
	}
	
	int minReal=Building::MAX_COUNT;
	for (std::list<Project *>::iterator pi=projects.begin(); pi!=projects.end(); pi++)
		if ((*pi)->priority<=priority)
		{
			int real=buildingSum[(*pi)->demand][0];
			if (minReal>real)
				minReal=real;
		}
	for (std::list<Project *>::iterator pi=projects.begin(); pi!=projects.end(); pi++)
		if ((*pi)->priority<=priority)
		{
			int real=buildingSum[(*pi)->demand][0];
			if (real<=minReal)
			{
				std::shared_ptr<Order>order=continueProject(*pi);
				if (order)
					return order;
			}
		}
	for (std::list<Project *>::iterator pi=projects.begin(); pi!=projects.end(); pi++)
		if ((*pi)->priority<=priority)
		{
			int real=buildingSum[(*pi)->demand][0];
			if (real>minReal)
			{
				std::shared_ptr<Order>order=continueProject(*pi);
				if (order)
					return order;
			}
		}
	
	if (priority>0 && timer>expandFoodTimer)
	{
		expandFoodTimer=timer+AI_CASTOR_EXPAND_FOOD_INTERVAL; // each 10s
		std::shared_ptr<Order>order=expandFood();
		if (order)
			return order;
	}
	
	if (timer>lastEnemyRangeMapComputed+AI_CASTOR_ENEMY_RANGE_REFRESH) // each 41s
	{
		computeEnemyRangeMap();
	}
	if (timer>lastEnemyWarriorsMapComputed+AI_CASTOR_ENEMY_WARRIORS_REFRESH) // each 41s
	{
		computeEnemyWarriorsMap();
	}

	if (priority>0)
	{
		std::shared_ptr<Order>order=controlFood();
		if (order)
			return order;
	}
	
	if (priority>0)
	{
		std::shared_ptr<Order>order=controlUpgrades();
		if (order)
			return order;
	}
	
	if (timer>controlStrikesTimer)
	{
		std::shared_ptr<Order>order=controlStrikes();
		if (order)
			return order;
	}
	
	return shared_ptr<Order>(new NullOrder());
}

// Default build-policy table for AICastor::defineStrategy().
//
// One row per hard building, indexed by int 0..7
// (SWARM, FOOD, HEAL, WALKSPEED, SWIMSPEED, ATTACK, SCIENCE, DEFENSE).
// Field names mirror Strategy::Build exactly so the table copies into
// strategy.build[i] field-for-field.
//
// finalWorkers is -1 for HEAL / WALKSPEED / SWIMSPEED / ATTACK / SCIENCE
// because the original defineStrategy() left those slots at the -1 set
// by the pre-fill loop (only SWARM, FOOD, DEFENSE were re-assigned).
// Encoding -1 explicitly here preserves identical post-init state.
//
// Semantic demand policies retain Castor's economic priorities; concrete
// providers are selected separately from the installed catalog.
namespace
{
	struct CastorStrategyDefaults
	{
		Sint32 successWait;
		Sint32 isFreePart;
		Sint32 warLevelTrigger;
		Uint32 warTimeTrigger;
		Sint32 warAmountTrigger;
		Sint32 strikeWarPowerTriggerUp;
		Sint32 strikeWarPowerTriggerDown;
		Uint32 strikeTimeTrigger;
		Sint32 maxAmountGoal;
	};

	// Base/new policies for the first eight economic and defense demands.
	// Column order matches Strategy::Build field order.
	// Row order matches int 0..7.
	static constexpr AICastor::Strategy::Build DEFAULT_BUILD_POLICIES[AICastor::NB_HARD_BUILDING] =
	{
		// baseOrder, base, baseWorkers, baseUpgrade, finalWorkers, newOrder, news, newWorkers, newUpgrade
		/* 0 ProduceWorkers     */ { 1, 2, 2, 0,  2, 1,  1, 3,  0 },
		/* 1 FeedUnits      */ { 4, 4, 3, 2,  1, 2,  7, 2,  3 },
		/* 2 HealUnits      */ { 5, 2, 1, 2, -1, 5,  5, 2,  5 },
		/* 3 TrainWalking */ { 7, 1, 5, 0, -1, 6,  1, 4,  0 },
		/* 4 TrainSwimming */ { 6, 1, 3, 0, -1, 7,  1, 4,  0 },
		/* 5 TrainAttack    */ { 2, 2, 2, 2, -1, 4,  2, 5,  2 },
		/* 6 TrainConstruction   */ { 0, 2, 5, 2, -1, 3,  2, 7,  2 },
		/* 7 ProjectileDefense   */ { 3, 2, 2, 1,  2, 0, 10, 4, 10 },
	};

	// Scalar strategy defaults set once per game by defineStrategy().
	// strikeTimeTrigger = 32768 ticks ≈ 21 min 51 s.
	// isFreePart = 10 (denominator for "1/N of pop = excess"; "good in [3..20]" per source comment).
	static constexpr CastorStrategyDefaults DEFAULTS =
	{
		/* successWait               */ 0,     // TODO: use a "lowDiscovered" flag instead
		/* isFreePart                */ 10,    // good in [3..20]
		/* warLevelTrigger           */ 1,
		/* warTimeTrigger            */ 8192,
		/* warAmountTrigger          */ 3,
		/* strikeWarPowerTriggerUp   */ 4096,
		/* strikeWarPowerTriggerDown */ 2048,
		/* strikeTimeTrigger         */ 32768, // 21 min 51 s
		/* maxAmountGoal             */ 10,
	};
}

void AICastor::defineStrategy()
{
	strategy.defined=true;

    // Every policy is initialized, including demands handled by other modules.
    for(auto& policy:strategy.build) policy=Strategy::Build{};

	// Apply the configured economic and defense demand priorities.
	for (int bi=0; bi<NB_HARD_BUILDING; bi++)
		strategy.build[bi] = DEFAULT_BUILD_POLICIES[bi];

	strategy.successWait              = DEFAULTS.successWait;
	strategy.isFreePart               = DEFAULTS.isFreePart;

	strategy.warLevelTrigger          = DEFAULTS.warLevelTrigger;
	strategy.warTimeTrigger           = DEFAULTS.warTimeTrigger;
	strategy.warAmountTrigger         = DEFAULTS.warAmountTrigger;

	strategy.strikeWarPowerTriggerUp  = DEFAULTS.strikeWarPowerTriggerUp;
	strategy.strikeWarPowerTriggerDown= DEFAULTS.strikeWarPowerTriggerDown;
	strategy.strikeTimeTrigger        = DEFAULTS.strikeTimeTrigger;
	strikeTimeTrigger                 = strategy.strikeTimeTrigger;

	strategy.maxAmountGoal            = DEFAULTS.maxAmountGoal;
}

