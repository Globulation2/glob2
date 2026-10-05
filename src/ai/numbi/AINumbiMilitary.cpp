#include "AIRuleOrders.h"
// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#include "AITelemetryFields.h"
#include <array>
#include <algorithm>
#include <vector>
#include <span>

#include "AINumbi.h"
#include "Game.h"
#include "Order.h"
#include "Player.h"
#include "Utilities.h"
#include "Unit.h"

using std::shared_ptr;

namespace
{
bool disposableRally(const Building& building)
{
	const auto& p = building.type->semantics;
	return !p.feeding.enabled && !p.healing.enabled && building.type->shootingRange == 0
		&& !p.market.interTeamFruitExchange && !p.market.suppliesStock
		&& std::none_of(p.production.recipes.begin(), p.production.recipes.end(), [](const auto& r) { return r.enabled; })
		&& std::none_of(p.training.begin(), p.training.end(), [](const auto& r) { return r.enabled; });
}
}

std::shared_ptr<Order>AINumbi::mayAttack(int criticalMass, int criticalTimeout, Sint32 numberRequested)
{
	// Combat cannot damage opponents here; military work must not reserve economic labour.
	if (game->gameHeader.isPeacefulModeEnabled()) return std::make_shared<NullOrder>();
	telemetry.set(AITrace::AI1::AINumbi_mayAttack_input_numberRequested, numberRequested);
	telemetry.set(AITrace::AI1::AINumbi_mayAttack_input_criticalTimeout, criticalTimeout);
	telemetry.set(AITrace::AI1::AINumbi_mayAttack_input_criticalMass, criticalMass);
	telemetry.count(AITrace::AI1::AINumbi_mayAttack_calls);
	Unit **myUnits=team->myUnits;
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

		int teamNumber=player->team->teamNumber;

		for (Building* rally : std::span<Building*>(team->myBuildings, Building::MAX_COUNT))
			if (rally && provides(*rally, Intent::AttractWarriors))
			{
				Building *b=rally;
				int gbid=map->getBuilding(b->posX, b->posY);
				if (disposableRally(*b) && (gbid==NOGBID || Building::GIDtoTeam(gbid)==teamNumber))
					return telemetry.returnedOrder(
						AITrace::AI1::AINumbi_mayAttack_result,
						shared_ptr<Order>(
							new OrderDelete(b->gid))); // The target has beed successfully killed.

				if (b->maxUnitWorking!=numberRequested && (disposableRally(*b) || b->maxUnitWorking<numberRequested))
				{
					//printf("AI: OrderModifyBuilding(%d, %d)\n", b->gid, numberRequested);
					return telemetry.returnedOrder(
						AITrace::AI1::AINumbi_mayAttack_result,
						shared_ptr<Order>(new OrderModifyBuilding(b->gid, numberRequested)));
				}
			}

		// We look for a specific enemy:
		Uint32 enemies=player->team->attackableTeams();
		int e=-1;
		for (int i=0; i<game->mapHeader.getNumberOfTeams(); i++)
			if (game->teams[i]->me & enemies)
				e=i;
		telemetry.set(AITrace::AI1::AINumbi_mayAttack_enemy_team, e);
		if (e==-1)
			return telemetry.returnedOrder(AITrace::AI1::AINumbi_mayAttack_result,
										   shared_ptr<Order>(new NullOrder));

		int ex=-1, ey=-1;
		int count=0;
		bool found=false;
		for (int i=0; i<Building::MAX_COUNT; i++)
		{
			Building *b=game->teams[e]->myBuildings[i];
			if (b)
			{
				ex=b->posX;
				ey=b->posY;

				if ((random()&AI_NUMBI_ENEMY_FLAG_CHANCE_MASK)==0)
				{
					bool already=false;
					count=0;
					for (Building* rally : std::span<Building*>(team->myBuildings, Building::MAX_COUNT))
						if (rally && provides(*rally, Intent::AttractWarriors))
						{
							count++;
							if (rally->posX==ex &&rally->posY==ey)
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
			const auto* placement = game->buildingsTypes.get(typeNum);
			bool room = false;
			for (int radius = 0; radius <= 8 && !room; ++radius)
				for (int dy = -radius; dy <= radius && !room; ++dy)
					for (int dx = -radius; dx <= radius && !room; ++dx)
						if (game->checkRoomForBuilding(ex+dx, ey+dy, placement, teamNumber))
						{ ex += dx; ey += dy; room = true; }
			if (!room) return std::make_shared<NullOrder>();
			//printf("AI: OrderCreateWarFlag(%d, %d)\n", ex, ey);
			return telemetry.returnedOrder(
				AITrace::AI1::AINumbi_mayAttack_result,
				AIRules::createOrder(*game, teamNumber, ex, ey, typeNum,
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
		for (Building* rally : std::span<Building*>(team->myBuildings, Building::MAX_COUNT))
			if (rally && provides(*rally, Intent::AttractWarriors) && disposableRally(*rally))
				return telemetry.returnedOrder(AITrace::AI1::AINumbi_mayAttack_result,
											   shared_ptr<Order>(new OrderDelete(rally->gid)));
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
	if (game->gameHeader.isUnitUpgradesDisabled()) return std::make_shared<NullOrder>();
	telemetry.set(AITrace::AI1::AINumbi_mayUpgrade_input_ntrigger, ntrigger);
	telemetry.set(AITrace::AI1::AINumbi_mayUpgrade_input_ptrigger, ptrigger);
	telemetry.count(AITrace::AI1::AINumbi_mayUpgrade_calls);
    // This controller retains its bounded strategic tiers. Authored display
    // levels do not determine which explicit transition belongs to each tier.
    const auto stage=[&](int type) {return std::clamp(game->buildingCapabilities().lineagePosition(type)-1,0,NB_UNIT_LEVELS-1);};
	const Intent priorities[] = {Intent::Feed, Intent::Heal, Intent::TrainAttackStrength,
		Intent::TrainConstruction, Intent::ProjectileDefense};
	std::array<int, NB_UNIT_LEVELS> workers{}, idle{}, training{};
	std::array<std::array<int, NB_UNIT_LEVELS>, std::size(priorities)> ready{}, underway{};
	for (Unit* u : std::span<Unit*>(team->myUnits, Unit::MAX_COUNT))
		if (u && u->typeNum == WORKER)
			for (int level = 0; level <= u->workerLevel() && level < NB_UNIT_LEVELS; ++level)
			{ ++workers[level]; if (u->activity == Unit::ACT_RANDOM) ++idle[level]; }
	for (Building* b : std::span<Building*>(team->myBuildings, Building::MAX_COUNT))
	{
		if (!b) continue;
		if (!b->type->isBuildingSite && provides(*b, Intent::TrainConstruction))
		{
			int qualification = 0;
			for (const auto& grant : b->type->semantics.training)
				if (grant.enabled && (grant.unitMask & (1u << WORKER)))
					qualification = std::max(qualification, grant.constructionLevel);
			for (int level = 0; level <= qualification && level < NB_UNIT_LEVELS; ++level)
				++training[level];
		}
		for (unsigned demand = 0; demand < std::size(priorities); ++demand)
			if (provides(*b, priorities[demand]))
			{
				const int completed = b->type->isBuildingSite ? b->type->nextLevel : b->typeNum;
				const int level = stage(completed);
				if (level >= 0 && level < NB_UNIT_LEVELS)
					++(b->type->isBuildingSite ? underway[demand][level] : ready[demand][level]);
			}
	}
	for (unsigned demand = 0; demand < std::size(priorities); ++demand)

	{
		const Intent intent = priorities[demand];
		if (!AIPlanning::BuildingCapabilityIndex::allowed(intent, game->gameHeader)) continue;
		std::vector<Building*> choices;
		for (Building* b : std::span<Building*>(team->myBuildings, Building::MAX_COUNT))
		{
			if (!b || b->type->isBuildingSite || !provides(*b, intent) || !b->isUpgradeAvailable() || b->type->nextLevel < 0) continue;
			const auto* next = game->buildingsTypes.get(b->type->nextLevel);
			const int targetId = next->isBuildingSite ? next->nextLevel : b->type->nextLevel;
			if (targetId < 0) continue;
			if (!game->buildingCapabilities().available(targetId, intent, game->gameHeader)) continue;
			const int required = next->semantics.requiredWorkerLevel;
			const int tolerance = intent == Intent::TrainConstruction ? AI_NUMBI_SCIENCE_UPGRADE_TOLERANCE : 0;
			if (required >= 0 && required < NB_UNIT_LEVELS
				&& workers[required] + AI_NUMBI_SCHOOL_POTENTIAL_WEIGHT*training[required] > ptrigger && idle[required] > ntrigger
				&& ready[demand][stage(b->typeNum)] > underway[demand][stage(targetId)]+tolerance && b->isHardSpaceForBuildingSite(Building::UPGRADE))
				choices.push_back(b);
		}
		if (!choices.empty())
		{
			Building* selected = choices[choices.size() == 1 ? 0 : random()%choices.size()];
			return telemetry.returnedOrder(AITrace::AI1::AINumbi_mayUpgrade_result,
				AIRules::constructionOrder(*game, *selected, AI_NUMBI_UPGRADE_ORDER_LEVEL, AI_NUMBI_UPGRADE_ORDER_REPAIR));
		}
	}
	return telemetry.returnedOrder(AITrace::AI1::AINumbi_mayUpgrade_result, std::make_shared<NullOrder>());
}
