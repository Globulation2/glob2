// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#include "Unit.h"
#include "Race.h"
#include "Team.h"
#include "Map.h"

#include "Building.h"

#include <climits>

//! Return the real armor, taking into account the reduction due to fruits
int Unit::getRealArmor(bool isMagic) const
{
	int armorReductionPerHappyness = race->getUnitType(typeNum, level[ARMOR])->armorReductionPerHappyness;
	if (isMagic) //magic bypasses armor yet fruit penalties still apply
		return 0 - fruitCount * armorReductionPerHappyness;
	else
		return performance[ARMOR] - fruitCount * armorReductionPerHappyness;
}

//! Return the real attack strengh, taking into account the experience level
int Unit::getRealAttackStrength(void) const
{
	return performance[ATTACK_STRENGTH] + experienceLevel;
}

//! Return the amount of experience to level-up
int Unit::getNextLevelThreshold(void) const
{
	return (experienceLevel + 1) * (experienceLevel + 1) * race->getUnitType(typeNum, level[ATTACK_STRENGTH])->experiencePerLevel;
}

//! Increment experience. If level-up occures, handle it. Multiple level-up may occur at once.
void Unit::incrementExperience(int increment)
{
	experience += increment;
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
	if (hungryness)
		timeLeft = (hungry-trigHungry) / hungryness;
	else
		timeLeft = INT_MAX;
	stepsLeftUntilHungry = timeLeft;
	return timeLeft;
}

//! Iterate on all resource types to see if it is gettable
void Unit::computeMinDistToResources(void)
{
	for (size_t ri = 0; ri < MAX_RESSOURCES; ri++)
		if (!owner->map->ressourceAvailable(owner->teamNumber, ri, performance[SWIM], posX, posY, &minDistToResource[ri]))
			minDistToResource[ri] = UNIT_MIN_DIST_NOT_REACHABLE;
	// the dist to an already carried resource is zero
	if (carriedRessource >= 0)
		minDistToResource[carriedRessource] = 0;
}
