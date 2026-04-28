/*
  Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière
  for any question or comment contact us at <stephane at magnenat dot net> or <NuageBleu at gmail dot com>

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 3 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
*/

#include "Unit.h"
#include "Race.h"
#include "UnitSkin.h"
#include "UnitsSkins.h"
#include "Team.h"
#include "Map.h"
#include "Game.h"

#include "Building.h"
#include "Integrity.h"

#include "Utilities.h"
#include "GlobalContainer.h"
#include "LogFileManager.h"
#include <Stream.h>
#include <set>
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

//! Compute the skin pointer from a skin name
void Unit::skinPointerFromName(void)
{
	if (!globalContainer->runNoX)
	{
		skin = globalContainer->unitsSkins->getSkin(skinName);
		if (skin == NULL)
		{
			// if skin is invalid, retry with default
			std::cerr << "Unit::skinPointerFromName : invalid skin name " << skinName << std::endl;
			defaultSkinNameFromType();
			skin = globalContainer->unitsSkins->getSkin(skinName);
			if (!skin)
				abort();
		}
	}
	else
		skin = NULL;
}


//! Compute the skin name from the unit type
void Unit::defaultSkinNameFromType(void)
{
	switch (typeNum)
	{
		case WORKER: skinName = "worker"; break;
		case EXPLORER: skinName = "explorer"; break;
		case WARRIOR: skinName = "warrior"; break;
		default: assert(false); break;
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
	bool allResourcesAreTooFar = true;
	for (size_t ri = 0; ri < MAX_RESSOURCES; ri++)
		if (!owner->map->ressourceAvailable(owner->teamNumber, ri, performance[SWIM], posX, posY, &minDistToResource[ri]))
			minDistToResource[ri] = -1;
		else if (minDistToResource[ri] < stepsLeftUntilHungry)
			allResourcesAreTooFar = false;
	// the dist to an already carried resource is zero
	if (carriedRessource >= 0)
		minDistToResource[carriedRessource] = 0;
}
