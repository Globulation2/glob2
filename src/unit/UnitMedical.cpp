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
#include <climits>

void Unit::selectPreferredMovement(void)
{
	if (performance(FLY))
		core.action=FLY;
	else if ((performance(SWIM)) && (owner->map->terrainPropertiesAt(core.posX, core.posY).swimmable) )
		core.action=SWIM;
	else if ((performance(WALK)) && (owner->map->terrainPropertiesAt(core.posX, core.posY).walkable) )
		core.action=WALK;
	else
		core.action=STOP_WALK;
}

void Unit::selectPreferredGroundMovement(void)
{
	assert(!performance(FLY));
	if ((performance(SWIM)) && (owner->map->terrainPropertiesAt(core.posX, core.posY).swimmable) )
		core.action=SWIM;
	else if ((performance(WALK)) && (owner->map->terrainPropertiesAt(core.posX, core.posY).walkable) )
		core.action=WALK;
	else
		core.action=STOP_WALK;
}

bool Unit::isUnitHungry(void)
{
	// A saved hungry unit must recover under no hunger rather than keep seeking food.
	if (owner->game->gameHeader.isHungerDisabled() || detail.hungriness<=0) return false;
	int realTrigHungry;
	if (core.carriedMaterial==-1)
		realTrigHungry=detail.trigHungry;
	else
		realTrigHungry=detail.trigHungryCarrying;

	return (core.hungry<=realTrigHungry);
}

void Unit::standardRandomActivity()
{
	if (attachedBuilding) attachedBuilding->releaseService(this);
	attachedBuilding=NULL;
	setTargetBuilding(NULL);
	ownExchangeBuilding=NULL;
	core.jobPurpose=UnitJobPurpose::None;
	core.activity=Unit::ACT_RANDOM;
	core.displacement=Unit::DIS_RANDOM;
	core.validTarget=false;
	detail.needToRecheckMedical=true;
}

void Unit::stopAttachedForBuilding(bool goingInside)
{
	if (verbose)
		printf("guid=(%d) stopAttachedForBuilding()\n", core.gid);
	assert(attachedBuilding);

	if (goingInside)
	{
		attachedBuilding->removeUnitFromInside(this);
		if (core.activity==ACT_UPGRADING)
		{
			assert(core.displacement==DIS_GOING_TO_BUILDING);
			if (core.destinationPurpose==HEAL || core.destinationPurpose==FEED)
				detail.needToRecheckMedical=true;
		}
	}
	else
	{
		for (std::list<Unit *>::iterator  it=attachedBuilding->unitsInside.begin(); it!=attachedBuilding->unitsInside.end(); ++it)
			assert(*it!=this);
	}

	core.jobPurpose=UnitJobPurpose::None;
	core.activity=ACT_RANDOM;
	core.displacement=DIS_RANDOM;
	core.validTarget=false;

	attachedBuilding->removeUnitFromWorking(this);
	attachedBuilding=NULL;
	setTargetBuilding(NULL);
	ownExchangeBuilding=NULL;
	assert(detail.needToRecheckMedical);
}

void Unit::handleMagic(void)
{
	assert(core.medical==MED_FREE);
	assert((core.displacement!=DIS_ENTERING_BUILDING) && (core.displacement!=DIS_INSIDE) && (core.displacement!=DIS_EXITING_BUILDING));

	if (detail.magicActionTimeout>INT_MIN) --detail.magicActionTimeout;
	if (detail.magicActionTimeout > 0)
		return;

	Map *map = &owner->game->map;
	Team **teams = owner->game->teams;

	bool hasUsedMagicAction = false;
	if (performance(MAGIC_ATTACK_AIR) || performance(MAGIC_ATTACK_GROUND))
	{
		const int ATTACK_RANGE = runtimeTraits().magicRange;
		for (int yi=core.posY-ATTACK_RANGE; yi<=core.posY+ATTACK_RANGE; yi++)
			for (int xi=core.posX-ATTACK_RANGE; xi<=core.posX+ATTACK_RANGE; xi++)
			{
				// damaging enemy units:
				for (int altitude=0; altitude<2; altitude++)
				{
					Uint16 targetGUID;
					Sint32 attackForce;
					if ((altitude == 1) && performance(MAGIC_ATTACK_AIR))
					{
						targetGUID = map->getAirUnit(xi, yi);
						attackForce = performance(MAGIC_ATTACK_AIR);
					}
					else if ((altitude == 0) && performance(MAGIC_ATTACK_GROUND))
					{
						targetGUID = map->getGroundUnit(xi, yi);
						attackForce = performance(MAGIC_ATTACK_GROUND);
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
								(Sint64(attackForce)+core.experienceLevel)*owner->game->gameHeader.getGlassCannonScale(),0,INT_MAX)));
							const int damage=int(std::clamp<Sint64>(Sint64(strength)-enemyUnit->getRealArmor(true),0,INT_MAX));
							if (damage > 0)
							{
								TeamStats::recordDamage(
									owner, enemyUnit->owner, GameplayMeasurements::MAGIC,
									GameplayMeasurements::UNIT, enemyUnit->core.hp, damage);
								enemyUnit->recordLethalDamage(damage, GameplayMeasurements::COMBAT);
								enemyUnit->core.hp = int(std::max<Sint64>(INT_MIN,Sint64(enemyUnit->core.hp)-damage));

								enemyUnit->owner->pushGameEvent(GameEvent::unitUnderAttack(owner->game->stepCounter, xi, yi, enemyUnit->core.typeNum));

								incrementExperience(damage);
								core.magicActionAnimation = MAGIC_ACTION_ANIMATION_FRAME_COUNT;
								hasUsedMagicAction = true;
							}
						}
					}
				}

				// damaging enemy buildings: this has been removed for balance purposes
			}

		Sint32 magicLevel = std::max(detail.level[MAGIC_ATTACK_AIR], detail.level[MAGIC_ATTACK_GROUND]);
		if (hasUsedMagicAction)
		{
			++owner->stats.measurements.shots[GameplayMeasurements::MAGIC];
			// Historical replays indexed an idle/movement level using the magic
			// level as an ability index. Keep that quirk only for imported tables.
			const int cooldownLevel=hasCapability(UnitRuntimeTraits::LegacyPerformancePolicies)
				? detail.level[magicLevel] : magicLevel;
			detail.magicActionTimeout = race->getUnitType(core.typeNum, cooldownLevel)->magicActionCooldown;
		}
	}
}

void Unit::handleMedical(void)
{
	/* Make sure explorers try to immediately feed after healing to increase their range. */
	if (hasCapability(UnitRuntimeTraits::ServiceRebound) && core.displacement == DIS_EXITING_BUILDING)
	{
		core.medical=MED_FREE;
		if (!owner->game->gameHeader.isHungerDisabled() && detail.hungriness > 0 && (core.destinationPurpose == HEAL) && (core.hungry < ((Sint64(foodCapacity()) * runtimeTraits().reboundNumerator) / runtimeTraits().reboundDenominator)))
		{
			detail.needToRecheckMedical = 1;
			core.medical = MED_HUNGRY;
			return;
		}
		else if ((core.destinationPurpose == FEED) && (core.hp < ((Sint64(performance(HP)) * runtimeTraits().reboundNumerator) / runtimeTraits().reboundDenominator)))
		{
			detail.needToRecheckMedical = 1;
			core.medical = MED_DAMAGED;
			return;
		}
	}

	if ((core.displacement==DIS_ENTERING_BUILDING) || (core.displacement==DIS_INSIDE) || (core.displacement==DIS_EXITING_BUILDING))
		return;

	if (verbose)
		printf("guid=(%d) handleMedical...\n", core.gid);
	// Custom-game "no hunger" rule: units never grow hungry or starve.
	if (!owner->game->gameHeader.isHungerDisabled() && detail.hungriness>0)
	{
		core.hungry = Sint32(std::max<Sint64>(INT_MIN,Sint64(core.hungry)-detail.hungriness));
		if (core.hungry<=0)
		{
			const int damage=runtimeTraits().starvationDamage;
			recordLethalDamage(damage, GameplayMeasurements::STARVATION);
			core.hp=int(std::max<Sint64>(INT_MIN,Sint64(core.hp)-damage));
		}
	}

	core.medical=MED_FREE;
	if (isUnitHungry())
		core.medical=MED_HUNGRY;
	else if (core.hp<=detail.trigHP)
		core.medical=MED_DAMAGED;

	resolveDeath();
}

void Unit::resolveDeath()
{
	// Custom-game "no permadeath" rule: clamp back up instead of letting the
	// unit cross the death threshold; like the rule's description, HP stops at 1.
	if (owner->game->gameHeader.isPermadeathDisabled() && core.hp<UNIT_HP_DEATH_THRESHOLD+1)
		core.hp = UNIT_HP_DEATH_THRESHOLD+1;

	if (core.hp<UNIT_HP_DEATH_THRESHOLD)
	{
		if (!core.isDead)
		{
			++owner->stats.measurements.deaths[core.typeNum][diagnosticDeathCause];
			if (diagnosticDeathCause == GameplayMeasurements::COMBAT)
				owner->stats.recordCombatDeath(this);
			// disconnect from building
			if (attachedBuilding)
			{
				assert((core.displacement!=DIS_ENTERING_BUILDING) && (core.displacement!=DIS_INSIDE) && (core.displacement!=DIS_EXITING_BUILDING));
				attachedBuilding->removeUnitFromWorking(this);
				attachedBuilding->removeUnitFromInside(this);
				attachedBuilding=NULL;
				ownExchangeBuilding=NULL;
			}
			setTargetBuilding(NULL);

			core.jobPurpose=UnitJobPurpose::None;
			core.activity=ACT_RANDOM;
			core.validTarget=false;

			// remove from map
			if (performance(FLY))
				owner->map->setAirUnit(core.posX, core.posY, NOGUID);
			else
				owner->map->setGroundUnit(core.posX, core.posY, NOGUID);

			if (previousClearingArea)
			{
				owner->map->setClearingAreaUnclaimed(previousClearingArea->x, previousClearingArea->y, owner->teamNumber);
			}
			owner->map->clearImmobileUnit(core.posX, core.posY);

			clearCargo();

			// generate death animation (no-op in headless mode)
			owner->game->animations->onUnitDeath(*owner->map, core.posX, core.posY, owner);
		}
		core.isDead = true;
	}
}
