// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#include <Stream.h>
#include <array>
#include <sstream>

#include "AINumbi.h"
#include "Game.h"
#include "GlobalContainer.h"
#include "Order.h"
#include "Player.h"
#include "Utilities.h"
#include "Unit.h"

using std::shared_ptr;

std::shared_ptr<Order>AINumbi::mayAttack(int critticalMass, int critticalTimeout, Sint32 numberRequested)
{
	Unit **myUnits=team->myUnits;
	int ft=0;
	for (int i=0; i<Unit::MAX_COUNT; i++)
		if ((myUnits[i])&&(myUnits[i]->performance[ATTACK_SPEED])&&(myUnits[i]->medical==0))
			ft++;

	if (attackPhase==0)
	{
		if (ft>=critticalMass)
		{
			//printf("AI:(crittical mass)new attack with %d units.\n", ft);
			attackPhase=1;
		}
		attackTimer++;
		if ((attackTimer>=critticalTimeout)&&(ft>numberRequested))
		{
			attackTimer=0;
			//printf("AI:(timeout)new attack with %d units.\n", ft);
			attackPhase=1;
		}
		return shared_ptr<Order>(new NullOrder);
	}
	else if (attackPhase==1)
	{
		if (ft<=(critticalMass/AI_NUMBI_STOP_ATTACK_DIVISOR))
		{
			attackPhase=3;
			//printf("AI:stop attack.\n");
			return shared_ptr<Order>(new NullOrder);
		}

		int teamNumber=player->team->teamNumber;

		for (std::list<Building *>::iterator bit=team->virtualBuildings.begin(); bit!=team->virtualBuildings.end(); ++bit)
			if ((*bit)->type->shortTypeNum==IntBuildingType::WAR_FLAG)
			{
				Building *b=*bit;
				int gbid=map->getBuilding(b->posX, b->posY);
				if (gbid==NOGBID || Building::GIDtoTeam(gbid)==teamNumber)
					return shared_ptr<Order>(new OrderDelete(b->gid)); // The target has beed successfully killed.

				if (b->maxUnitWorking!=numberRequested)
				{
					//printf("AI: OrderModifyBuilding(%d, %d)\n", b->gid, numberRequested);
					return shared_ptr<Order>(new OrderModifyBuilding(b->gid, numberRequested));
				}
			}

		// We look for a specific enemy:
		Uint32 enemies=player->team->enemies;
		int e=-1;
		for (int i=0; i<game->mapHeader.getNumberOfTeams(); i++)
			if (game->teams[i]->me & enemies)
				e=i;
		if (e==-1)
			return shared_ptr<Order>(new NullOrder);

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

				if ((syncRand()&AI_NUMBI_ENEMY_FLAG_CHANCE_MASK)==0)
				{
					bool already=false;
					count=0;
					for (std::list<Building *>::iterator bit=team->virtualBuildings.begin(); bit!=team->virtualBuildings.end(); ++bit)
						if ((*bit)->type->shortTypeNum==IntBuildingType::WAR_FLAG)
						{
							count++;
							if ((*bit)->posX==ex &&(*bit)->posY==ey)
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
			Sint32 typeNum=globalContainer->buildingsTypes.getTypeNum("warflag", 0, false);
			//printf("AI: OrderCreateWarFlag(%d, %d)\n", ex, ey);
			return shared_ptr<Order>(new OrderCreate(teamNumber, ex, ey, typeNum, AI_NUMBI_WAR_FLAG_INIT_UNITS_WORKING, AI_NUMBI_WAR_FLAG_INIT_FLAG_RADIUS));
		}
		else
			return shared_ptr<Order>(new NullOrder);
	}
	else if (attackPhase==2)
	{
		assert(false);
		return shared_ptr<Order>(new NullOrder);
	}
	else if (attackPhase==3)
	{
		for (std::list<Building *>::iterator bit=team->virtualBuildings.begin(); bit!=team->virtualBuildings.end(); ++bit)
			if ((*bit)->type->shortTypeNum==IntBuildingType::WAR_FLAG)
				return shared_ptr<Order>(new OrderDelete((*bit)->gid));
		attackPhase=0;
		critticalWarriors*=AI_NUMBI_ATTACK_BACKOFF_MULTIPLIER;
		critticalTime*=AI_NUMBI_ATTACK_BACKOFF_MULTIPLIER;
		return shared_ptr<Order>(new NullOrder);
	}
	else
	{
		assert(false);
		return shared_ptr<Order>(new NullOrder);
	}

}

namespace {

// The five building kinds AINumbi considers for level upgrades. Iteration
// order is the upgrade-priority order — food first, defense last — and is
// part of the deterministic order stream; do not reorder without rebaselining.
enum UpgradeKind
{
	UK_FOOD = 0,
	UK_HEAL,
	UK_ATTACK,
	UK_SCIENCE,
	UK_DEFENSE,
	NB_UPGRADE_KINDS
};

// Extra in-flight upgrades tolerated at each kind's rung threshold. Only
// SCIENCE carries a non-zero value; the original C++ added
// AI_NUMBI_SCIENCE_UPGRADE_TOLERANCE inline in two of ten copy-pasted
// conditionals.
constexpr int kUpgradeKindTolerance[NB_UPGRADE_KINDS] = {
	0,                                  // UK_FOOD
	0,                                  // UK_HEAL
	0,                                  // UK_ATTACK
	AI_NUMBI_SCIENCE_UPGRADE_TOLERANCE, // UK_SCIENCE
	0,                                  // UK_DEFENSE
};

struct UpgradeInventory
{
	int number[NB_UNIT_LEVELS] = {};      // completed (non-site) buildings per level
	int upgrading[NB_UNIT_LEVELS] = {};   // sites currently upgrading to this level
	Building *exemplar[NB_UNIT_LEVELS] = {}; // a chosen instance per level, or null
};

int upgradeKindFor(int shortTypeNum)
{
	switch (shortTypeNum)
	{
		case IntBuildingType::FOOD_BUILDING:    return UK_FOOD;
		case IntBuildingType::HEAL_BUILDING:    return UK_HEAL;
		case IntBuildingType::ATTACK_BUILDING:  return UK_ATTACK;
		case IntBuildingType::SCIENCE_BUILDING: return UK_SCIENCE;
		case IntBuildingType::DEFENSE_BUILDING: return UK_DEFENSE;
		default:                                return -1;
	}
}

// Walks every building owned by `team` and tallies, per (kind, level): the
// count of completed buildings, the count of upgrading sites, and one
// "exemplar" — a deterministically chosen building used as the target for
// the next upgrade order. The exemplar is selected by an unbiased syncRand
// coin flip on each completed building, so for k buildings at one (kind,
// level) the last one wins with probability 1/2, the previous with 1/4,
// etc. syncRand() is the lockstep RNG, so the result is identical across
// networked clients.
std::array<UpgradeInventory, NB_UPGRADE_KINDS> collectUpgradeInventory(Team *team)
{
	std::array<UpgradeInventory, NB_UPGRADE_KINDS> inv{};
	Building **myBuildings = team->myBuildings;
	for (int i = 0; i < Building::MAX_COUNT; i++)
	{
		Building *b = myBuildings[i];
		if (!b)
			continue;
		const int kind = upgradeKindFor(b->type->shortTypeNum);
		if (kind < 0)
			continue;
		const int l = b->type->level;
		if (b->type->isBuildingSite)
			inv[kind].upgrading[l]++;
		else
		{
			inv[kind].number[l]++;
			if (syncRand() & 1)
				inv[kind].exemplar[l] = b;
		}
	}
	return inv;
}

// Tries one ladder rung: for each upgradeable kind in priority order,
// checks whether the colony has more completed level-srcLevel buildings
// than are currently being upgraded to level srcLevel+1 (plus the per-kind
// tolerance). Returns an OrderConstruction targeting the first eligible
// kind's exemplar at srcLevel, or nullptr if none.
//
// Pre BH-220, the C++ original passed exemplar[0] for both rungs (level
// 0→1 and 1→2), so the level-1→2 path always re-issued level-0→1 upgrades
// and AINumbi's tech tree stalled at level 1. This helper reads
// exemplar[srcLevel] uniformly, fixing that behavior.
std::shared_ptr<Order> tryUpgradeRung(
	const std::array<UpgradeInventory, NB_UPGRADE_KINDS> &inv,
	int srcLevel)
{
	for (int kind = 0; kind < NB_UPGRADE_KINDS; ++kind)
	{
		const UpgradeInventory &slot = inv[kind];
		if (slot.number[srcLevel] > slot.upgrading[srcLevel + 1] + kUpgradeKindTolerance[kind])
		{
			Building *b = slot.exemplar[srcLevel];
			if (b)
				return std::make_shared<OrderConstruction>(b->gid, AI_NUMBI_UPGRADE_ORDER_LEVEL, AI_NUMBI_UPGRADE_ORDER_REPAIR);
		}
	}
	return nullptr;
}

} // namespace

// Issues one building-upgrade order if (a) the colony has enough free or
// schooled units to staff higher-level buildings — gated against ptrigger
// (potential = working units at higher levels, weighted by SCIENCE stock)
// and ntrigger (now = free units at higher levels) — and (b) there is a
// completed building of an upgradeable kind that is not already saturated
// with in-flight upgrades. Tries level 0→1 first, then 1→2; returns
// NullOrder if neither rung is eligible.
std::shared_ptr<Order> AINumbi::mayUpgrade(const int ptrigger, const int ntrigger)
{
	const auto inv = collectUpgradeInventory(team);

	Unit **myUnits = team->myUnits;
	int wun[NB_UNIT_LEVELS] = {}; // working units per BUILD level
	int fun[NB_UNIT_LEVELS] = {}; // free (ACT_RANDOM) units per BUILD level
	for (int i = 0; i < Unit::MAX_COUNT; i++)
	{
		Unit *u = myUnits[i];
		if (!u)
			continue;
		const int l = u->level[BUILD];
		if (u->activity == Unit::ACT_RANDOM)
			fun[l]++;
		wun[l]++;
	}

	const UpgradeInventory &science = inv[UK_SCIENCE];

	// Level 0 → 1 rung.
	{
		const int sciencePool = science.number[0] + science.number[1] + science.number[2] + science.number[3];
		const int potential = wun[1] + wun[2] + wun[3] + AI_NUMBI_SCHOOL_POTENTIAL_WEIGHT * sciencePool;
		const int now = fun[1] + fun[2] + fun[3];
		if (potential > ptrigger && now > ntrigger)
		{
			if (auto order = tryUpgradeRung(inv, 0))
				return order;
		}
	}

	// Level 1 → 2 rung.
	{
		const int sciencePool = science.number[1] + science.number[2] + science.number[3];
		const int potential = wun[2] + wun[3] + AI_NUMBI_SCHOOL_POTENTIAL_WEIGHT * sciencePool;
		const int now = fun[2] + fun[3];
		if (potential > ptrigger && now > ntrigger)
		{
			if (auto order = tryUpgradeRung(inv, 1))
				return order;
		}
	}

	return std::make_shared<NullOrder>();
}
