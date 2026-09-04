// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#include "IntBuildingType.h"
#include <assert.h>
#include <iostream>

std::map<std::string, int> IntBuildingType::conversionMap;
std::vector<std::string> IntBuildingType::reverseConversionMap;

std::string IntBuildingType::null = "null";

void IntBuildingType::init(void)
{
	conversionMap["swarm"] = SWARM_BUILDING;
	conversionMap["inn"] = FOOD_BUILDING;
	conversionMap["hospital"] = HEAL_BUILDING;
	conversionMap["racetrack"] = WALKSPEED_BUILDING;
	conversionMap["swimmingpool"] = SWIMSPEED_BUILDING;
	conversionMap["barracks"] = ATTACK_BUILDING;
	conversionMap["school"] = SCIENCE_BUILDING;
	conversionMap["defencetower"] = DEFENSE_BUILDING;
	conversionMap["explorationflag"] = EXPLORATION_FLAG;
	conversionMap["warflag"] = WAR_FLAG;
	conversionMap["clearingflag"] = CLEARING_FLAG;
	conversionMap["stonewall"] = STONE_WALL;
	conversionMap["market"] = MARKET_BUILDING;
	
	reverseConversionMap.resize(NB_BUILDING);
	reverseConversionMap[SWARM_BUILDING] = "swarm";
	reverseConversionMap[FOOD_BUILDING] = "inn";
	reverseConversionMap[HEAL_BUILDING] = "hospital";
	reverseConversionMap[WALKSPEED_BUILDING] = "racetrack";
	reverseConversionMap[SWIMSPEED_BUILDING] = "swimmingpool";
	reverseConversionMap[ATTACK_BUILDING] = "barracks";
	reverseConversionMap[SCIENCE_BUILDING] = "school";
	reverseConversionMap[DEFENSE_BUILDING] = "defencetower";
	reverseConversionMap[EXPLORATION_FLAG] = "explorationflag";
	reverseConversionMap[WAR_FLAG] = "warflag";
	reverseConversionMap[CLEARING_FLAG] = "clearingflag";
	reverseConversionMap[STONE_WALL] = "stonewall";
	reverseConversionMap[MARKET_BUILDING] = "market";
}

int IntBuildingType::shortNumberFromType(const std::string &type)
{
	if (conversionMap.find(type) != conversionMap.end())
		return conversionMap[type];
	else
	{
		std::cerr << "IntBuildingType::shortNumberFromType(\"" << type << "\") : error : type does not exists in conversionMap" << std::endl;
		assert(false);
		return -1;
	}
}

const std::string &IntBuildingType::typeFromShortNumber(int number)
{
	if ((number >= 0) && (number<(int)reverseConversionMap.size()))
		return reverseConversionMap[number];
	else
	{
		std::cerr << "IntBuildingType::typeFromShortNumber(" << number << ") : error : number out of range" << std::endl;
		assert(false);
		return null;
	}
}
