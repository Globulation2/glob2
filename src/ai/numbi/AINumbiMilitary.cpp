#include "AIRuleOrders.h"
// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#include "AITelemetryFields.h"
#include <array>
#include <algorithm>
#include <vector>
#include <span>

#include "AINumbi.h"
#include "NumbiQueries.h"
#include "ai/engine/AIDecision.h"
#include "Game.h"
#include "Order.h"
#include "Player.h"
#include "Utilities.h"
#include "Unit.h"

using std::shared_ptr;

namespace
{
bool disposableRally(const AIEngine::BuildingView& building,const NumbiObservation::Queries& queries)
{
	const auto& p = queries.kind(building).semantics;
	return !p.feeding.enabled && !p.healing.enabled && queries.kind(building).shootingRange == 0
		&& !p.market.interTeamFruitExchange && !p.market.suppliesStock && !p.market.suppliesDirectStock
		&& std::none_of(p.production.recipes.begin(), p.production.recipes.end(), [](const auto& r) { return r.enabled; })
		&& std::none_of(p.training.begin(), p.training.end(), [](const auto& r) { return r.enabled; });
}
}

std::shared_ptr<Order>AINumbi::mayAttack(int criticalMass, int criticalTimeout, Sint32 numberRequested)
{
	// Combat cannot damage opponents here; military work must not reserve economic labour.
	if (observation->rules.peaceful) return std::make_shared<NullOrder>();
	telemetry.set(AITrace::AI1::AINumbi_mayAttack_input_numberRequested, numberRequested);
	telemetry.set(AITrace::AI1::AINumbi_mayAttack_input_criticalTimeout, criticalTimeout);
	telemetry.set(AITrace::AI1::AINumbi_mayAttack_input_criticalMass, criticalMass);
	telemetry.count(AITrace::AI1::AINumbi_mayAttack_calls);
	const AIEngine::UnitView* const* myUnits=observedUnits.data();
	int ft=0;
	for (int i=0; i<Unit::MAX_COUNT; i++)
		if ((myUnits[i])&&(myUnits[i]->performance[ATTACK_SPEED])&&(myUnits[i]->medical==0))
			ft++;

	if (attackPhase==0)
	{
		if (ft>=criticalMass)
		{
			//printf("AI:(critical mass)new attack with %d units.\n", ft);
			attackPhase=1;
		}
		attackTimer++;
		if ((attackTimer>=criticalTimeout)&&(ft>numberRequested))
		{
			attackTimer=0;
			//printf("AI:(timeout)new attack with %d units.\n", ft);
			attackPhase=1;
		}
		return telemetry.returnedOrder(AITrace::AI1::AINumbi_mayAttack_result,
									   shared_ptr<Order>(new NullOrder));
	}
	else if (attackPhase==1)
	{
		if (ft<=(criticalMass/AI_NUMBI_STOP_ATTACK_DIVISOR))
		{
			attackPhase=3;
			//printf("AI:stop attack.\n");
			return telemetry.returnedOrder(AITrace::AI1::AINumbi_mayAttack_result,
										   shared_ptr<Order>(new NullOrder));
		}



		for (const AIEngine::BuildingView* rally : observedBuildings)
			if (rally && provides(*rally, Intent::AttractWarriors))
			{
				const AIEngine::BuildingView *b=rally;
				int gbid=observation->tile(b->x,b->y).building;
				if (disposableRally(*b,*queries) && (gbid==NOGBID || Building::GIDtoTeam(gbid)==teamNumber))
					return telemetry.returnedOrder(
						AITrace::AI1::AINumbi_mayAttack_result,
						shared_ptr<Order>(
							new OrderDelete(b->identity.gid))); // The target has beed successfully killed.

				if (requestedWorkers(*b)!=numberRequested && (disposableRally(*b,*queries) || requestedWorkers(*b)<numberRequested))
				{
					//printf("AI: OrderModifyBuilding(%d, %d)\n", b->identity.gid, numberRequested);
					return telemetry.returnedOrder(
						AITrace::AI1::AINumbi_mayAttack_result,
						shared_ptr<Order>(new OrderModifyBuilding(b->identity.gid, numberRequested)));
				}
			}

		// We look for a specific enemy:
		Uint32 enemies=observation->teams[teamNumber].enemies;
		int e=-1;
		for (int i=0; i<int(observation->teams.size()); i++)
			if (observation->teams[i].mask & enemies)
				e=i;
		telemetry.set(AITrace::AI1::AINumbi_mayAttack_enemy_team, e);
		if (e==-1)
			return telemetry.returnedOrder(AITrace::AI1::AINumbi_mayAttack_result,
										   shared_ptr<Order>(new NullOrder));

		int ex=-1, ey=-1;
		int count=pendingBuildings(Intent::AttractWarriors);
		bool found=false;
		for (int i=0; i<Building::MAX_COUNT; i++)
		{
			const AIEngine::BuildingView *b=observation->buildingAtSlot(e*::Building::MAX_COUNT+i);
			if (b)
			{
				ex=b->x;
				ey=b->y;

				if ((random()&AI_NUMBI_ENEMY_FLAG_CHANCE_MASK)==0)
				{
					bool already=false;
     for(const auto& pending:pendingRequests) if(const auto* flag=dynamic_cast<const OrderCreate*>(pending.order.get()))
      if(queries->matches(flag->typeNum,Intent::AttractWarriors) && observation->normalizeX(flag->posX)==ex && observation->normalizeY(flag->posY)==ey) already=true;
					count=pendingBuildings(Intent::AttractWarriors);
					for (const AIEngine::BuildingView* rally : observedBuildings)
						if (rally && provides(*rally, Intent::AttractWarriors))
						{
							count++;
							if (rally->x==ex &&rally->y==ey)
							{
								already=true;
								break;
							}
						}
					if (!already)
					{
						found=true;
						break;
					}
				}
			}
		}

		if (ex!=-1 && ey!=-1 && found && count<AI_NUMBI_MAX_WAR_FLAGS)
		{
			const int typeNum = selectBuilding(Intent::AttractWarriors);
			if (typeNum < 0) return std::make_shared<NullOrder>();
			bool room = false;
			for (int radius = 0; radius <= 8 && !room; ++radius)
				for (int dy = -radius; dy <= radius && !room; ++dy)
					for (int dx = -radius; dx <= radius && !room; ++dx)
						if (queries->checkRoomForBuilding(ex+dx, ey+dy, typeNum, teamNumber))
						{ ex += dx; ey += dy; room = true; }
			if (!room) return std::make_shared<NullOrder>();
			//printf("AI: OrderCreateWarFlag(%d, %d)\n", ex, ey);
			return telemetry.returnedOrder(
				AITrace::AI1::AINumbi_mayAttack_result,
				queries->createOrder( teamNumber, ex, ey, typeNum,
												  AI_NUMBI_WAR_FLAG_INIT_UNITS_WORKING,
												  AI_NUMBI_WAR_FLAG_INIT_FLAG_RADIUS));
		}
		else
			return telemetry.returnedOrder(AITrace::AI1::AINumbi_mayAttack_result,
										   shared_ptr<Order>(new NullOrder));
	}
	else if (attackPhase==2)
	{
		assert(false);
		return telemetry.returnedOrder(AITrace::AI1::AINumbi_mayAttack_result,
									   shared_ptr<Order>(new NullOrder));
	}
	else if (attackPhase==3)
	{
		for (const AIEngine::BuildingView* rally : observedBuildings)
			if (rally && provides(*rally, Intent::AttractWarriors) && disposableRally(*rally,*queries))
				return telemetry.returnedOrder(AITrace::AI1::AINumbi_mayAttack_result,
											   shared_ptr<Order>(new OrderDelete(rally->identity.gid)));
		attackPhase=0;
		criticalWarriors*=AI_NUMBI_ATTACK_BACKOFF_MULTIPLIER;
		criticalTime*=AI_NUMBI_ATTACK_BACKOFF_MULTIPLIER;
		return telemetry.returnedOrder(AITrace::AI1::AINumbi_mayAttack_result,
									   shared_ptr<Order>(new NullOrder));
	}
	else
	{
		assert(false);
		return telemetry.returnedOrder(AITrace::AI1::AINumbi_mayAttack_result,
									   shared_ptr<Order>(new NullOrder));
	}

}

std::shared_ptr<Order> AINumbi::mayUpgrade(const int ptrigger, const int ntrigger)
{
	if (observation->rules.upgradesDisabled) return std::make_shared<NullOrder>();
	telemetry.set(AITrace::AI1::AINumbi_mayUpgrade_input_ntrigger, ntrigger);
	telemetry.set(AITrace::AI1::AINumbi_mayUpgrade_input_ptrigger, ptrigger);
	telemetry.count(AITrace::AI1::AINumbi_mayUpgrade_calls);
    // This controller retains its bounded strategic tiers. Authored display
    // levels do not determine which explicit transition belongs to each tier.
    const auto stage=[&](int type) {return std::clamp(queries->kind(type).lineagePosition-1,0,NB_UNIT_LEVELS-1);};
	const Intent priorities[] = {Intent::Feed, Intent::Heal, Intent::TrainAttackStrength,
		Intent::TrainConstruction, Intent::ProjectileDefense};
	std::array<int, NB_UNIT_LEVELS> workers{}, idle{}, training{};
	std::array<std::array<int, NB_UNIT_LEVELS>, std::size(priorities)> ready{}, underway{};
	for (const AIEngine::UnitView* u : observedUnits)
		if (u && u->type == WORKER)
			for (int level = 0; level <= u->constructionLevel && level < NB_UNIT_LEVELS; ++level)
			{ ++workers[level]; if (u->activity == Unit::ACT_RANDOM) ++idle[level]; }
	for (const AIEngine::BuildingView* b : observedBuildings)
	{
		if (!b) continue;
		if (!queries->kind(*b).site && provides(*b, Intent::TrainConstruction))
		{
			int qualification = 0;
			for (const auto& grant : queries->kind(*b).semantics.training)
				if (grant.enabled && (grant.unitMask & (1u << WORKER)))
					qualification = std::max(qualification, grant.constructionLevel);
			for (int level = 0; level <= qualification && level < NB_UNIT_LEVELS; ++level)
				++training[level];
		}
		for (unsigned demand = 0; demand < std::size(priorities); ++demand)
			if (provides(*b, priorities[demand]))
			{
				const int completed = queries->kind(*b).site ? queries->kind(*b).next : b->type;
				const int level = stage(completed);
				if (level >= 0 && level < NB_UNIT_LEVELS)
					++(queries->kind(*b).site ? underway[demand][level] : ready[demand][level]);
			}
	}
	for (unsigned demand = 0; demand < std::size(priorities); ++demand)

	{
		const Intent intent = priorities[demand];
		if (!queries->allowed(intent)) continue;
		std::vector<const AIEngine::BuildingView*> choices;
		for (const AIEngine::BuildingView* b : observedBuildings)
		{
			if (!b || hasPending(*b) || queries->kind(*b).site || !provides(*b, intent) || !b->upgradeAvailable || queries->kind(*b).next < 0) continue;
			const auto* next = &queries->kind(queries->kind(*b).next);
			const int targetId = next->site ? next->next : queries->kind(*b).next;
			if (targetId < 0) continue;
			if (!queries->available(targetId,intent)) continue;
			const int required = next->semantics.requiredWorkerLevel;
			const int tolerance = intent == Intent::TrainConstruction ? AI_NUMBI_SCIENCE_UPGRADE_TOLERANCE : 0;
			if (required >= 0 && required < NB_UNIT_LEVELS
				&& workers[required] + AI_NUMBI_SCHOOL_POTENTIAL_WEIGHT*training[required] > ptrigger && idle[required] > ntrigger
				&& ready[demand][stage(b->type)] > underway[demand][stage(targetId)]+tolerance && b->hardSpaceUpgrade)
				choices.push_back(b);
		}
		if (!choices.empty())
		{
			const AIEngine::BuildingView* selected = choices[choices.size() == 1 ? 0 : random()%choices.size()];
			return telemetry.returnedOrder(AITrace::AI1::AINumbi_mayUpgrade_result,
				queries->constructionOrder( *selected, AI_NUMBI_UPGRADE_ORDER_LEVEL, AI_NUMBI_UPGRADE_ORDER_REPAIR));
		}
	}
	return telemetry.returnedOrder(AITrace::AI1::AINumbi_mayUpgrade_result, std::make_shared<NullOrder>());
}
