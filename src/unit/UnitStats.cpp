// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#include "Unit.h"
#include "Race.h"
#include "Team.h"
#include "Map.h"
#include "Game.h"

#include "Building.h"

#include <climits>
#include <algorithm>

//! Return the real armor, taking into account the reduction due to fruits
int Unit::getRealArmor(bool isMagic) const
{
	int armorReductionPerHappyness = race->getUnitType(typeNum, level[ARMOR])->armorReductionPerHappyness;
	if (isMagic) //magic bypasses armor yet fruit penalties still apply
		return int(std::clamp<Sint64>(-Sint64(fruitCount)*armorReductionPerHappyness,INT_MIN,INT_MAX));
	else
	{
		// Custom-game "glass cannon" rule: armor is reduced by the same
		// factor attack strength is scaled up, below. Only the armor itself
		// shrinks; the fruit penalty stays whole.
		int armor = performance[ARMOR] / owner->game->gameHeader.getGlassCannonScale();
		if (armor && owner->game->areaEffects.enabled() && insideTimeout >= 0 &&
			displacement != DIS_INSIDE && displacement != DIS_ENTERING_BUILDING &&
			!(displacement == DIS_EXITING_BUILDING && attachedBuilding))
		{
			const auto factor =
				owner->game->areaEffects.at(BuildingAreaEffects::UnitArmor, owner->teamNumber,
											owner->map->coordToIndex(posX, posY));
			if (factor != BuildingAreaEffects::Neutral)
				armor = BuildingAreaEffects::scale(armor, factor);
		}
		return int(std::clamp<Sint64>(Sint64(armor)-Sint64(fruitCount)*armorReductionPerHappyness,INT_MIN,INT_MAX));
	}
}

//! Return the real attack strength, taking into account the experience level
int Unit::getRealAttackStrength(void) const
{
	return applyAreaAttack(int(std::clamp<Sint64>((Sint64(performance[ATTACK_STRENGTH]) + experienceLevel) * owner->game->gameHeader.getGlassCannonScale(),0,INT_MAX)));
}

//! Return the amount of experience to level-up
int Unit::getNextLevelThreshold(void) const
{
	const Sint64 next=std::max<Sint64>(1,Sint64(experienceLevel)+1);
	const int scale=race->getUnitType(typeNum,level[ATTACK_STRENGTH])->experiencePerLevel;
	if (scale<=0) return INT_MAX;
	const Sint64 square=next*next; // even a saved INT_MAX level fits this product
	return square>INT_MAX/scale ? INT_MAX : int(square*scale);
}

//! Increment experience. If level-up occurs, handle it. Multiple level-up may occur at once.
void Unit::incrementExperience(int increment)
{
	if (race->getUnitType(typeNum,level[ATTACK_STRENGTH])->experiencePerLevel<=0) return;
	experience=int(std::clamp<Sint64>(Sint64(experience)+increment,0,INT_MAX));
	int nextLevelThreshold = getNextLevelThreshold();
	while (experience > nextLevelThreshold)
	{
		experience -= nextLevelThreshold;
		experienceLevel++;
		nextLevelThreshold = getNextLevelThreshold();
		levelUpAnimation = LEVEL_UP_ANIMATION_FRAME_COUNT;
	}
}

//! Return how many steps we can do until we are hungry
int Unit::numberOfStepsLeftUntilHungry(void)
{
	int timeLeft;
	if (hungriness)
		timeLeft = int(std::clamp<Sint64>((Sint64(hungry)-trigHungry) / hungriness,INT_MIN,INT_MAX));
	else
		timeLeft = INT_MAX;
	stepsLeftUntilHungry = timeLeft;
	return timeLeft;
}

int Unit::applyAreaAttack(int value) const
{
	if (!value || !owner->game->areaEffects.enabled() || insideTimeout < 0 ||
		displacement == DIS_INSIDE || displacement == DIS_ENTERING_BUILDING ||
		(displacement == DIS_EXITING_BUILDING && attachedBuilding))
		return value;
	const auto factor = owner->game->areaEffects.at(
		BuildingAreaEffects::UnitAttack, owner->teamNumber, owner->map->coordToIndex(posX, posY));
	return factor == BuildingAreaEffects::Neutral ? value
												  : BuildingAreaEffects::scale(value, factor);
}
