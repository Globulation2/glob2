#ifndef __INT_BUILDING_TYPE_H
#define __INT_BUILDING_TYPE_H

#include <map>
#include <string>
#include <vector>

struct IntBuildingType
{
	enum Number
	{
		SWARM_BUILDING=0,
		FOOD_BUILDING=1,
		HEAL_BUILDING=2,
	
		WALKSPEED_BUILDING=3,
		SWIMSPEED_BUILDING=4,
		ATTACK_BUILDING=5,
		SCIENCE_BUILDING=6,
	
		DEFENSE_BUILDING=7,
		
		EXPLORATION_FLAG=8,
		WAR_FLAG=9,
		CLEARING_FLAG=10,
	
		STONE_WALL=11,
	
		MARKET_BUILDING=12,
	
		NB_BUILDING
	};
	
	static std::map<std::string, int> conversionMap;
	static std::vector<std::string> reverseConversionMap;
	static std::string null;
	
	static int shortNumberFromType(const char *type);
	static const std::string & typeFromShortNumber(int number);
	
	static void init(void);
};

#endif
