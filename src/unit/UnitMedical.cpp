// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#include "Unit.h"
#include "Race.h"
#include "Team.h"
#include "Map.h"
#include "Game.h"

#include "Building.h"

#include "Utilities.h"
#include "render/GameAnimations.h"
#include <set>
#include <climits>

void Unit::selectPreferredMovement(void)
{
	if (performance[FLY])
		action=FLY;
	else if ((performance[SWIM]) && (owner->map->terrainPropertiesAt(posX, posY).swimmable) )
		action=SWIM;
	else if ((performance[WALK]) && (owner->map->terrainPropertiesAt(posX, posY).walkable) )
		action=WALK;
	else
		action=STOP_WALK;
}

void Unit::selectPreferredGroundMovement(void)
{
	assert(!performance[FLY]);
	if ((performance[SWIM]) && (owner->map->terrainPropertiesAt(posX, posY).swimmable) )
		action=SWIM;
	else if ((performance[WALK]) && (owner->map->terrainPropertiesAt(posX, posY).walkable) )
		action=WALK;
	else
		action=STOP_WALK;
}

bool Unit::isUnitHungry(void)
{
	// A saved hungry unit must recover under no hunger rather than keep seeking food.
	if (owner->game->gameHeader.isHungerDisabled() || hungriness<=0) return false;
	int realTrigHungry;
	if (carriedMaterial==-1)
		realTrigHungry=trigHungry;
	else
		realTrigHungry=trigHungryCarrying;

	return (hungry<=realTrigHungry);
}

void Unit::standardRandomActivity()
{
	if (attachedBuilding) attachedBuilding->releaseService(this);
	attachedBuilding=NULL;
	setTargetBuilding(NULL);
	ownExchangeBuilding=NULL;
	jobPurpose=UnitJobPurpose::None;
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

	jobPurpose=UnitJobPurpose::None;
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

	if (magicActionTimeout>INT_MIN) --magicActionTimeout;
	if (magicActionTimeout > 0)
		return;

	Map *map = &owner->game->map;
	Team **teams = owner->game->teams;

	bool hasUsedMagicAction = false;
	if (performance[MAGIC_ATTACK_AIR] || performance[MAGIC_ATTACK_GROUND])
	{
		std::set<Uint16> damagedBuildings;
		damagedBuildings.insert(NOGBID);
		const int ATTACK_RANGE = runtimeTraits().magicRange;
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
						Uint32 targetTeamMask = Team::teamNumberToMask(targetTeam);
						if (owner->attackableTeams() & targetTeamMask)
						{
							Unit *enemyUnit = teams[targetTeam]->myUnits[targetID];
							const int strength=applyAreaAttack(int(std::clamp<Sint64>(
								(Sint64(attackForce)+experienceLevel)*owner->game->gameHeader.getGlassCannonScale(),0,INT_MAX)));
							const int damage=int(std::clamp<Sint64>(Sint64(strength)-enemyUnit->getRealArmor(true),0,INT_MAX));
							if (damage > 0)
							{
								TeamStats::recordDamage(
									owner, enemyUnit->owner, GameplayMeasurements::MAGIC,
									GameplayMeasurements::UNIT, enemyUnit->hp, damage);
								enemyUnit->recordLethalDamage(damage, GameplayMeasurements::COMBAT);
								enemyUnit->hp = int(std::max<Sint64>(INT_MIN,Sint64(enemyUnit->hp)-damage));

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
		{
			++owner->stats.measurements.shots[GameplayMeasurements::MAGIC];
			// Historical replays indexed an idle/movement level using the magic
			// level as an ability index. Keep that quirk only for imported tables.
			const int cooldownLevel=hasCapability(UnitRuntimeTraits::LegacyPerformancePolicies)
				? level[magicLevel] : magicLevel;
			magicActionTimeout = race->getUnitType(typeNum, cooldownLevel)->magicActionCooldown;
		}
	}
}

void Unit::handleMedical(void)
{
	/* Make sure explorers try to immediately feed after healing to increase their range. */
	if (hasCapability(UnitRuntimeTraits::ServiceRebound) && displacement == DIS_EXITING_BUILDING)
	{
		medical=MED_FREE;
		if (!owner->game->gameHeader.isHungerDisabled() && hungriness > 0 && (destinationPurpose == HEAL) && (hungry < ((Sint64(foodCapacity()) * runtimeTraits().reboundNumerator) / runtimeTraits().reboundDenominator)))
		{
			needToRecheckMedical = 1;
			medical = MED_HUNGRY;
			return;
		}
		else if ((destinationPurpose == FEED) && (hp < ((Sint64(performance[HP]) * runtimeTraits().reboundNumerator) / runtimeTraits().reboundDenominator)))
		{
			needToRecheckMedical = 1;
			medical = MED_DAMAGED;
			return;
		}
	}

	if ((displacement==DIS_ENTERING_BUILDING) || (displacement==DIS_INSIDE) || (displacement==DIS_EXITING_BUILDING))
		return;

	if (verbose)
		printf("guid=(%d) handleMedical...\n", gid);
	// Custom-game "no hunger" rule: units never grow hungry or starve.
	if (!owner->game->gameHeader.isHungerDisabled() && hungriness>0)
	{
		hungry = Sint32(std::max<Sint64>(INT_MIN,Sint64(hungry)-hungriness));
		if (hungry<=0)
		{
			const int damage=runtimeTraits().starvationDamage;
			recordLethalDamage(damage, GameplayMeasurements::STARVATION);
			hp=int(std::max<Sint64>(INT_MIN,Sint64(hp)-damage));
		}
	}

	medical=MED_FREE;
	if (isUnitHungry())
		medical=MED_HUNGRY;
	else if (hp<=trigHP)
		medical=MED_DAMAGED;

	resolveDeath();
}

void Unit::resolveDeath()
{
	// Custom-game "no permadeath" rule: clamp back up instead of letting the
	// unit cross the death threshold; like the rule's description, HP stops at 1.
	if (owner->game->gameHeader.isPermadeathDisabled() && hp<UNIT_HP_DEATH_THRESHOLD+1)
		hp = UNIT_HP_DEATH_THRESHOLD+1;

	if (hp<UNIT_HP_DEATH_THRESHOLD)
	{
		if (!isDead)
		{
			++owner->stats.measurements.deaths[typeNum][diagnosticDeathCause];
			if (diagnosticDeathCause == GameplayMeasurements::COMBAT)
				owner->stats.recordCombatDeath(this);
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

			jobPurpose=UnitJobPurpose::None;
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

			clearCargo();

			// generate death animation (no-op in headless mode)
			owner->game->animations->onUnitDeath(*owner->map, posX, posY, owner);
		}
		isDead = true;
	}
}
