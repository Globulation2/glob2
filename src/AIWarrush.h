 /*
  Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière
  Copyright (C) 2005 Eli Dupree
  for any question or comment contact us at nct@ysagoon.com or nuage@ysagoon.com

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 2 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
*/
 
#ifndef __AI_WARRUSH_H
#define __AI_WARRUSH_H

#include "AIImplementation.h"


class Game;
class Map;
class Order;
class Player;
class Team;
class Building;


class AIWarrush : public AIImplementation
{
public:
	AIWarrush(Player *player);
	AIWarrush(GAGCore::InputStream *stream, Player *player, Sint32 versionMinor);
	~AIWarrush();

	Player *player;
	Team *team;
	Game *game;
	Map *map;

	bool load(GAGCore::InputStream *stream, Player *player, Sint32 versionMinor);
	void save(GAGCore::OutputStream *stream);
	
	Order *getOrder(void);
private:
	void init(Player *player);
	//implementation functions to make the code more like the pseudocode;
	//these should be improved, and some should be moved to Team.h.
	int numberOfBuildingsOfType(int buildingType)const;
	int numberOfUnitsWithSkillGreaterThanValue(int skill, int value)const;
	bool isAnyUnitWithLessThanOneThirdFood()const;
	Building *getSwarmWithoutSettings(int workerRatio, int explorerRatio, int warriorRatio)const;
	Building *getSwarmWithLeastProduction()const;
	Building *getSwarmWithMostProduction()const;
	int numberOfJobsForWorkers()const;
	int numberOfIdleLevel1Warriors()const;
	bool allBarracksAreCompletedAndFull()const;
	//functions called by getOrder, filled with pseudocode and its product,
	//real code.
	Order *initialRush(void);
	Order *maintain(void);
	Order *setupAttack(void);
	bool locationIsAvailableForBuilding(int x, int y, int width, int height);
	Order *buildBuildingOfType(Sint32 typeNum);
};

#endif


