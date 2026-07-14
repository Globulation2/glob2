// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#include "Unit.h"
#include "Race.h"
#include "Team.h"
#include "Map.h"
#include "Game.h"

#include "Building.h"

#include "Utilities.h"
#ifndef YOG_SERVER_ONLY
#include "render/GameAnimations.h"
#endif  // !YOG_SERVER_ONLY
#include <set>

void Unit::selectPreferredMovement(void)
{
	if (performance[FLY])
		action=FLY;
	else if ((performance[SWIM]) && (owner->map->isWater(posX, posY)) )
		action=SWIM;
	else if ((performance[WALK]) && (!owner->map->isWater(posX, posY)) )
		action=WALK;
	else
		assert(false);
}

void Unit::selectPreferredGroundMovement(void)
{
	assert(!performance[FLY]);
	if ((performance[SWIM]) && (owner->map->isWater(posX, posY)) )
		action=SWIM;
	else if ((performance[WALK]) && (!owner->map->isWater(posX, posY)) )
		action=WALK;
	else
		assert(false);
}

bool Unit::isUnitHungry(void)
{
	int realTrigHungry;
	if (carriedRessource==-1)
		realTrigHungry=trigHungry;
	else
		realTrigHungry=trigHungryCarying;

	return (hungry<=realTrigHungry);
}

void Unit::standardRandomActivity()
{
	attachedBuilding=NULL;
	setTargetBuilding(NULL);
	ownExchangeBuilding=NULL;
	activity=Unit::ACT_RANDOM;
	displacement=Unit::DIS_RANDOM;
	validTarget=false;
	needToRecheckMedical=true;
}

void Unit::stopAttachedForBuilding(bool goingInside)
{
	if (verbose)
		printf("guid=(%d) stopAttachedForBuilding()\n", gid);
	assert(attachedBuilding);

	if (goingInside)
	{
		attachedBuilding->removeUnitFromInside(this);
		if (activity==ACT_UPGRADING)
		{
			assert(displacement==DIS_GOING_TO_BUILDING);
			if (destinationPurpose==HEAL || destinationPurpose==FEED)
				needToRecheckMedical=true;
		}
	}
	else
	{
		for (std::list<Unit *>::iterator  it=attachedBuilding->unitsInside.begin(); it!=attachedBuilding->unitsInside.end(); ++it)
			assert(*it!=this);
	}

	activity=ACT_RANDOM;
	displacement=DIS_RANDOM;
	validTarget=false;

	attachedBuilding->removeUnitFromWorking(this);
	attachedBuilding=NULL;
	setTargetBuilding(NULL);
	ownExchangeBuilding=NULL;
	assert(needToRecheckMedical);
}

void Unit::handleMagic(void)
{
	assert(medical==MED_FREE);
	assert((displacement!=DIS_ENTERING_BUILDING) && (displacement!=DIS_INSIDE) && (displacement!=DIS_EXITING_BUILDING));

	magicActionTimeout--;
	if (magicActionTimeout > 0)
		return;

	Map *map = &owner->game->map;
	Team **teams = owner->game->teams;

	bool hasUsedMagicAction = false;
	if (performance[MAGIC_ATTACK_AIR] || performance[MAGIC_ATTACK_GROUND])
	{
		std::set<Uint16> damagedBuildings;
		damagedBuildings.insert(NOGBID);
		constexpr int ATTACK_RANGE = UNIT_MAGIC_ATTACK_RANGE;
		for (int yi=posY-ATTACK_RANGE; yi<=posY+ATTACK_RANGE; yi++)
			for (int xi=posX-ATTACK_RANGE; xi<=posX+ATTACK_RANGE; xi++)
			{
				// damaging enemy units:
				for (int altitude=0; altitude<2; altitude++)
				{
					Uint16 targetGUID;
					Sint32 attackForce;
					if ((altitude == 1) && performance[MAGIC_ATTACK_AIR])
					{
						targetGUID = map->getAirUnit(xi, yi);
						attackForce = performance[MAGIC_ATTACK_AIR];
					}
					else if ((altitude == 0) && performance[MAGIC_ATTACK_GROUND])
					{
						targetGUID = map->getGroundUnit(xi, yi);
						attackForce = performance[MAGIC_ATTACK_GROUND];
					}
					else
						continue;
					if (targetGUID != NOGUID)
					{
						Sint32 targetTeam = Unit::GIDtoTeam(targetGUID);
						Uint16 targetID = Unit::GIDtoID(targetGUID);
						Uint32 targetTeamMask = 1<<targetTeam;
						if (owner->enemies & targetTeamMask)
						{
							Unit *enemyUnit = teams[targetTeam]->myUnits[targetID];
							Sint32 damage = attackForce + experienceLevel - enemyUnit->getRealArmor(true);
							if (damage > 0)
							{
								enemyUnit->hp -= damage;

								enemyUnit->owner->pushGameEvent(GameEvent::unitUnderAttack(owner->game->stepCounter, xi, yi, enemyUnit->typeNum));

								incrementExperience(damage);
								magicActionAnimation = MAGIC_ACTION_ANIMATION_FRAME_COUNT;
								hasUsedMagicAction = true;
							}
						}
					}
				}

				// damaging enemy buildings: this has been removed for balance purposes
			}

		Sint32 magicLevel = std::max(level[MAGIC_ATTACK_AIR], level[MAGIC_ATTACK_GROUND]);
		if (hasUsedMagicAction)
			magicActionTimeout = race->getUnitType(typeNum, level[magicLevel])->magicActionCooldown;
	}
}

void Unit::handleMedical(void)
{
	/* Make sure explorers try to immediately feed after healing to increase their range. */
	if ((typeNum == EXPLORER) && (displacement == DIS_EXITING_BUILDING))
	{
		medical=MED_FREE;
		if ((destinationPurpose == HEAL) && (hungry < ((HUNGRY_MAX * EXPLORER_FORCE_FEED_RATIO_NUM) / EXPLORER_FORCE_FEED_RATIO_DEN)))
		{
			// fprintf (stderr, "forcing explorer hunger: gid: %d, hungry: %d\n", gid, hungry);
			needToRecheckMedical = 1;
			medical = MED_HUNGRY;
			return;
		}
		else if ((destinationPurpose == FEED) && (hp < (((performance[HP]) * EXPLORER_FORCE_FEED_RATIO_NUM) / EXPLORER_FORCE_FEED_RATIO_DEN)))
		{
			// fprintf (stderr, "forcing explorer healing: gid: %d, hp: %d\n", gid, hp);
			needToRecheckMedical = 1;
			medical = MED_DAMAGED;
			return;
		}
	}

	if ((displacement==DIS_ENTERING_BUILDING) || (displacement==DIS_INSIDE) || (displacement==DIS_EXITING_BUILDING))
		return;

	if (verbose)
		printf("guid=(%d) handleMedical...\n", gid);
	hungry -= hungryness;
	if (hungry<=0)
		hp--;

	medical=MED_FREE;
	if (isUnitHungry())
		medical=MED_HUNGRY;
	else if (hp<=trigHP)
		medical=MED_DAMAGED;

	if (hp<UNIT_HP_DEATH_THRESHOLD)
	{
		if (!isDead)
		{
			// disconnect from building
			if (attachedBuilding)
			{
				assert((displacement!=DIS_ENTERING_BUILDING) && (displacement!=DIS_INSIDE) && (displacement!=DIS_EXITING_BUILDING));
				attachedBuilding->removeUnitFromWorking(this);
				attachedBuilding->removeUnitFromInside(this);
				attachedBuilding=NULL;
				ownExchangeBuilding=NULL;
			}
			setTargetBuilding(NULL);
            // //TODO: in beta4 this line was ommitted. delete?
			// ownExchangeBuilding=NULL;

			activity=ACT_RANDOM;
			validTarget=false;

			// remove from map
			if (performance[FLY])
				owner->map->setAirUnit(posX, posY, NOGUID);
			else
				owner->map->setGroundUnit(posX, posY, NOGUID);

			if (previousClearingArea)
			{
				owner->map->setClearingAreaUnclaimed(previousClearingArea->x, previousClearingArea->y, owner->teamNumber);
			}
			owner->map->clearImmobileUnit(posX, posY);

			// generate death animation (no-op in headless mode)
			owner->game->animations->onUnitDeath(*owner->map, posX, posY, owner);
		}
		isDead = true;
	}
}
