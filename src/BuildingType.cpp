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

#include "BuildingType.h"
#include "GlobalContainer.h"
#include <Toolkit.h>
#include <assert.h>

BuildingType::BuildingType()
{
	gameSpritePtr = NULL;
	miniSpritePtr = NULL;
}

void BuildingType::loadFromConfigFile(const ConfigBlock *configBlock)
{
	configBlock->load(type, "type");

	configBlock->load(gameSprite, "gameSprite");
	configBlock->load(gameSpriteImage, "gameSpriteImage");
	configBlock->load(gameSpriteCount, "gameSpriteCount");
	configBlock->load(miniSprite, "miniSprite");
	configBlock->load(miniSpriteImage, "miniSpriteImage");
	
	configBlock->load(hueImage,"hueImage");
	configBlock->load(flagImage,"flagImage");
	configBlock->load(crossConnectMultiImage,"crossConnectMultiImage");
	
	assert(NB_ABILITY == 17);
	configBlock->load(upgrade[0], "upgradeStopWalk");
	configBlock->load(upgrade[1], "upgradeStopSwim");
	configBlock->load(upgrade[2], "upgradeStopFly");
	configBlock->load(upgrade[3], "upgradeWalk");
	configBlock->load(upgrade[4], "upgradeSwim");
	configBlock->load(upgrade[5], "upgradeFly");
	configBlock->load(upgrade[6], "upgradeBuild");
	configBlock->load(upgrade[7], "upgradeHarvest");
	configBlock->load(upgrade[8], "upgradeAttackSpeed");
	configBlock->load(upgrade[9], "upgradeAttackStrength");
	configBlock->load(upgrade[10], "upgradeMagicAttackAir");
	configBlock->load(upgrade[11], "upgradeMagicAttackGround");
	configBlock->load(upgrade[12], "upgradeMagicCreateWood");
	configBlock->load(upgrade[13], "upgradeMagicCreateCorn");
	configBlock->load(upgrade[14], "upgradeMagicCreateAlga");
	configBlock->load(upgrade[15], "upgradeArmor");
	configBlock->load(upgrade[16], "upgradeHP");
	configBlock->load(upgradeTime[0], "upgradeTimeStopWalk");
	configBlock->load(upgradeTime[1], "upgradeTimeStopSwim");
	configBlock->load(upgradeTime[2], "upgradeTimeStopFly");
	configBlock->load(upgradeTime[3], "upgradeTimeWalk");
	configBlock->load(upgradeTime[4], "upgradeTimeSwim");
	configBlock->load(upgradeTime[5], "upgradeTimeFly");
	configBlock->load(upgradeTime[6], "upgradeTimeBuild");
	configBlock->load(upgradeTime[7], "upgradeTimeHarvest");
	configBlock->load(upgradeTime[8], "upgradeTimeAttackSpeed");
	configBlock->load(upgradeTime[9], "upgradeTimeAttackStrength");
	configBlock->load(upgradeTime[10], "upgradeTimeMagicAttackAir");
	configBlock->load(upgradeTime[11], "upgradeTimeMagicAttackGround");
	configBlock->load(upgradeTime[12], "upgradeTimeMagicCreateWood");
	configBlock->load(upgradeTime[13], "upgradeTimeMagicCreateCorn");
	configBlock->load(upgradeTime[14], "upgradeTimeMagicCreateAlga");
	configBlock->load(upgradeTime[15], "upgradeTimeArmor");
	configBlock->load(upgradeTime[16], "upgradeTimeHP");
	configBlock->load(upgradeInParallel, "upgradeInParallel");
	
	configBlock->load(foodable, "foodable");
	configBlock->load(fillable, "fillable");
	
	assert(NB_UNIT_TYPE == 3);
	configBlock->load(zonable[0], "zonableWorker");
	configBlock->load(zonable[1], "zonableExplorer");
	configBlock->load(zonable[2], "zonableWarrior");
	configBlock->load(zonableForbidden, "zonableForbidden");

	configBlock->load(canFeedUnit, "canFeedUnit");
	configBlock->load(timeToFeedUnit, "timeToFeedUnit");
	configBlock->load(canHealUnit, "canHealUnit");
	configBlock->load(timeToHealUnit, "timeToHealUnit");
	configBlock->load(insideSpeed, "insideSpeed");
	configBlock->load(canExchange, "canExchange");
	configBlock->load(useTeamResources, "useTeamRessources");

	configBlock->load(width, "width");
	configBlock->load(height, "height");
	configBlock->load(decLeft, "decLeft");
	configBlock->load(decTop, "decTop");
	configBlock->load(isVirtual, "isVirtual");
	configBlock->load(isCloaked, "isCloacked");
	configBlock->load(shootingRange, "shootingRange");
	configBlock->load(shootDamage, "shootDamage");
	configBlock->load(shootSpeed, "shootSpeed");
	configBlock->load(shootRhythm, "shootRhythm");
	configBlock->load(maxBullets, "maxBullets");
	configBlock->load(multiplierStoneToBullets, "multiplierStoneToBullets");

	configBlock->load(unitProductionTime, "unitProductionTime");
	configBlock->load(resourceForOneUnit, "ressourceForOneUnit");
	
	assert(MAX_NB_RESOURCES == 15);
	configBlock->load(maxResource[0], "maxWood");
	configBlock->load(maxResource[1], "maxCorn");
	configBlock->load(maxResource[2], "maxPapyrus");
	configBlock->load(maxResource[3], "maxStone");
	configBlock->load(maxResource[4], "maxAlgue");
	configBlock->load(maxResource[5], "maxFruit0");
	configBlock->load(maxResource[6], "maxFruit1");
	configBlock->load(maxResource[7], "maxFruit2");
	configBlock->load(maxResource[8], "maxFruit3");
	configBlock->load(maxResource[9], "maxFruit4");
	configBlock->load(maxResource[10], "maxFruit5");
	configBlock->load(maxResource[11], "maxFruit6");
	configBlock->load(maxResource[12], "maxFruit7");
	configBlock->load(maxResource[13], "maxFruit8");
	configBlock->load(maxResource[14], "maxFruit9");
	configBlock->load(multiplierResource[0], "multiplierWood");
	configBlock->load(multiplierResource[1], "multiplierCorn");
	configBlock->load(multiplierResource[2], "multiplierPapyrus");
	configBlock->load(multiplierResource[3], "multiplierStone");
	configBlock->load(multiplierResource[4], "multiplierAlgue");
	configBlock->load(multiplierResource[5], "multiplierFruit0");
	configBlock->load(multiplierResource[6], "multiplierFruit1");
	configBlock->load(multiplierResource[7], "multiplierFruit2");
	configBlock->load(multiplierResource[8], "multiplierFruit3");
	configBlock->load(multiplierResource[9], "multiplierFruit4");
	configBlock->load(multiplierResource[10], "multiplierFruit5");
	configBlock->load(multiplierResource[11], "multiplierFruit6");
	configBlock->load(multiplierResource[12], "multiplierFruit7");
	configBlock->load(multiplierResource[13], "multiplierFruit8");
	configBlock->load(multiplierResource[14], "multiplierFruit9");

	configBlock->load(maxUnitInside, "maxUnitInside");
	configBlock->load(maxUnitWorking, "maxUnitWorking");

	configBlock->load(hpInit, "hpInit");
	configBlock->load(hpMax, "hpMax");
	configBlock->load(hpInc, "hpInc");
	configBlock->load(armor, "armor");
	configBlock->load(level, "level");
	configBlock->load(shortTypeNum, "shortTypeNum");
	configBlock->load(isBuildingSite, "isBuildingSite");

	configBlock->load(defaultUnitStayRange, "defaultUnitStayRange");
	configBlock->load(maxUnitStayRange, "maxUnitStayRange");

	configBlock->load(viewingRange, "viewingRange");
	configBlock->load(regenerationSpeed, "regenerationSpeed");
	
	configBlock->load(prestige, "prestige");
	
	// regenerate local parameters
	if ((!globalContainer->runNoX) && (type != "null"))
	{
		gameSpritePtr = Toolkit::getSprite(gameSprite.c_str());
		if (miniSpriteImage >= 0)
			miniSpritePtr = Toolkit::getSprite(miniSprite.c_str());
	}
}

//! Return a checksum of all parameter that could lead to a game desynchronization
Uint32 BuildingType::checkSum(void)
{
	Uint32 cs = 0;
	
	for (size_t i = 0; i<(size_t)NB_ABILITY; i++)
	{
		cs ^= upgrade[i];
		cs = (cs<<1) | (cs>>31);
	}
	for (size_t i = 0; i<(size_t)NB_ABILITY; i++)
	{
		cs ^= upgradeTime[i];
		cs = (cs<<1) | (cs>>31);
	}
	cs ^= foodable;
	cs = (cs<<1) | (cs>>31);
	cs ^= fillable;
	cs = (cs<<1) | (cs>>31);
	for (size_t i = 0; i<(size_t)NB_UNIT_TYPE; i++)
	{
		cs ^= zonable[i];
		cs = (cs<<1) | (cs>>31);
	}
	cs ^= zonableForbidden;
	cs = (cs<<1) | (cs>>31);
	cs ^= canFeedUnit;
	cs = (cs<<1) | (cs>>31);
	cs ^= timeToFeedUnit;
	cs = (cs<<1) | (cs>>31);
	cs ^= canHealUnit;
	cs = (cs<<1) | (cs>>31);
	cs ^= timeToHealUnit;
	cs = (cs<<1) | (cs>>31);
	cs ^= insideSpeed;
	cs = (cs<<1) | (cs>>31);
	cs ^= canExchange;
	cs = (cs<<1) | (cs>>31);
	cs ^= useTeamResources;
	cs = (cs<<1) | (cs>>31);
	cs ^= width;
	cs = (cs<<1) | (cs>>31);
	cs ^= height;
	cs = (cs<<1) | (cs>>31);
	cs ^= decLeft;
	cs = (cs<<1) | (cs>>31);
	cs ^= decTop;
	cs = (cs<<1) | (cs>>31);
	cs ^= isVirtual;
	cs = (cs<<1) | (cs>>31);
	cs ^= isCloaked;
	cs = (cs<<1) | (cs>>31);
	cs ^= shootingRange;
	cs = (cs<<1) | (cs>>31);
	cs ^= shootDamage;
	cs = (cs<<1) | (cs>>31);
	cs ^= shootSpeed;
	cs = (cs<<1) | (cs>>31);
	cs ^= shootRhythm;
	cs = (cs<<1) | (cs>>31);
	cs ^= maxBullets;
	cs = (cs<<1) | (cs>>31);
	cs ^= multiplierStoneToBullets;
	cs = (cs<<1) | (cs>>31);
	cs ^= unitProductionTime;
	cs = (cs<<1) | (cs>>31);
	cs ^= resourceForOneUnit;
	cs = (cs<<1) | (cs>>31);
	for (size_t i = 0; i<(size_t)MAX_NB_RESOURCES; i++)
	{
		cs ^= maxResource[i];
		cs = (cs<<1) | (cs>>31);
	}
	for (size_t i = 0; i<(size_t)MAX_NB_RESOURCES; i++)
	{
		cs ^= multiplierResource[i];
		cs = (cs<<1) | (cs>>31);
	}
	cs ^= maxUnitInside;
	cs = (cs<<1) | (cs>>31);
	cs ^= maxUnitWorking;
	cs = (cs<<1) | (cs>>31);
	cs ^= hpInit;
	cs = (cs<<1) | (cs>>31);
	cs ^= hpMax;
	cs = (cs<<1) | (cs>>31);
	cs ^= hpInc;
	cs = (cs<<1) | (cs>>31);
	cs ^= armor;
	cs = (cs<<1) | (cs>>31);
	cs ^= level;
	cs = (cs<<1) | (cs>>31);
	cs ^= shortTypeNum;
	cs = (cs<<1) | (cs>>31);
	cs ^= isBuildingSite;
	cs = (cs<<1) | (cs>>31);
	cs ^= defaultUnitStayRange;
	cs = (cs<<1) | (cs>>31);
	cs ^= maxUnitStayRange;
	cs = (cs<<1) | (cs>>31);
	cs ^= viewingRange;
	cs = (cs<<1) | (cs>>31);
	cs ^= regenerationSpeed;
	cs = (cs<<1) | (cs>>31);
	cs ^= prestige;
	
	return cs;
}
