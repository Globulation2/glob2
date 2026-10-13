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
	int armorReductionPerHappyness = race->getUnitType(core.typeNum, detail.level[ARMOR])->armorReductionPerHappyness;
	if (isMagic) //magic bypasses armor yet fruit penalties still apply
		return int(std::clamp<Sint64>(-Sint64(detail.fruitCount)*armorReductionPerHappyness,INT_MIN,INT_MAX));
	else
	{
		// Custom-game "glass cannon" rule: armor is reduced by the same
		// factor attack strength is scaled up, below. Only the armor itself
		// shrinks; the fruit penalty stays whole.
		int armor = performance(ARMOR) / owner->game->gameHeader.getGlassCannonScale();
		if (armor && owner->game->areaEffects.enabled() && detail.insideTimeout >= 0 &&
			core.displacement != DIS_INSIDE && core.displacement != DIS_ENTERING_BUILDING &&
			!(core.displacement == DIS_EXITING_BUILDING && attachedBuilding))
		{
			const auto factor =
				owner->game->areaEffects.at(BuildingAreaEffects::UnitArmor, owner->teamNumber,
											owner->map->coordToIndex(core.posX, core.posY));
			if (factor != BuildingAreaEffects::Neutral)
				armor = BuildingAreaEffects::scale(armor, factor);
		}
		return int(std::clamp<Sint64>(Sint64(armor)-Sint64(detail.fruitCount)*armorReductionPerHappyness,INT_MIN,INT_MAX));
	}
}

//! Return the real attack strength, taking into account the experience level
int Unit::getRealAttackStrength(void) const
{
	return applyAreaAttack(int(std::clamp<Sint64>((Sint64(performance(ATTACK_STRENGTH)) + core.experienceLevel) * owner->game->gameHeader.getGlassCannonScale(),0,INT_MAX)));
}

//! Return the amount of experience to level-up
int Unit::getNextLevelThreshold(void) const
{
	const Sint64 next=std::max<Sint64>(1,Sint64(core.experienceLevel)+1);
	const int scale=race->getUnitType(core.typeNum,detail.level[ATTACK_STRENGTH])->experiencePerLevel;
	if (scale<=0) return INT_MAX;
	const Sint64 square=next*next; // even a saved INT_MAX level fits this product
	return square>INT_MAX/scale ? INT_MAX : int(square*scale);
}

//! Increment experience. If level-up occurs, handle it. Multiple level-up may occur at once.
void Unit::incrementExperience(int increment)
{
	if (race->getUnitType(core.typeNum,detail.level[ATTACK_STRENGTH])->experiencePerLevel<=0) return;
	detail.experience=int(std::clamp<Sint64>(Sint64(detail.experience)+increment,0,INT_MAX));
	int nextLevelThreshold = getNextLevelThreshold();
	while (detail.experience > nextLevelThreshold)
	{
		detail.experience -= nextLevelThreshold;
		core.experienceLevel++;
		nextLevelThreshold = getNextLevelThreshold();
		core.levelUpAnimation = LEVEL_UP_ANIMATION_FRAME_COUNT;
	}
}

//! Return how many steps we can do until we are hungry
int Unit::numberOfStepsLeftUntilHungry(void)
{
	int timeLeft;
	if (detail.hungriness)
	{
		const Sint64 remaining = Sint64(core.hungry)-detail.trigHungry;
		if (detail.hungriness > 0 && remaining >= INT_MIN && remaining <= INT_MAX)
			timeLeft = int(remaining)/detail.hungriness;
		else
			timeLeft = int(std::clamp<Sint64>(remaining/detail.hungriness,INT_MIN,INT_MAX));
	}
	else
		timeLeft = INT_MAX;
	stepsLeftUntilHungry = timeLeft;
	return timeLeft;
}

int Unit::applyAreaAttack(int value) const
{
	if (!value || !owner->game->areaEffects.enabled() || detail.insideTimeout < 0 ||
		core.displacement == DIS_INSIDE || core.displacement == DIS_ENTERING_BUILDING ||
		(core.displacement == DIS_EXITING_BUILDING && attachedBuilding))
		return value;
	const auto factor = owner->game->areaEffects.at(
		BuildingAreaEffects::UnitAttack, owner->teamNumber, owner->map->coordToIndex(core.posX, core.posY));
	return factor == BuildingAreaEffects::Neutral ? value
												  : BuildingAreaEffects::scale(value, factor);
}
