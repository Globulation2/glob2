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
	
	static int shortNumberFromType(const std::string &type);
	static const std::string & typeFromShortNumber(int number);
	
	static void init(void);
};

#endif
