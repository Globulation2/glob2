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
#include "team.h"
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

void Unit::handleAction(void)
{
	owner->map->clearImmobileUnit(posX, posY);
	switch (movement)
	{
		case MOV_RANDOM_GROUND:
		{
			assert(!performance[FLY]);
			owner->map->setGroundUnit(posX, posY, NOGUID);
			owner->map->pathfindRandom(this, verbose);
			posX=(posX+dx)&(owner->map->getMaskW());
			posY=(posY+dy)&(owner->map->getMaskH());
			selectPreferredGroundMovement();
			speed=performance[action];
			assert(owner->map->getGroundUnit(posX, posY)==NOGUID);
			owner->map->setGroundUnit(posX, posY, gid);
			break;
		}

		case MOV_RANDOM_FLY:
		{
			assert(performance[FLY]);
			owner->map->setAirUnit(posX, posY, NOGUID);
			for(int q = 0; q < 5; ++q) //hack - look for a direction safe from guard towers
			{
				dx=-1+syncRand()%3;
				dy=-1+syncRand()%3;
				if(locationIsInEnemyGuardTowerRange(posX + dx, posY + dy))continue;
				else break;
			}
			directionFromDxDy();
			setNewValidDirectionAir();
			posX=(posX+dx)&(owner->map->getMaskW());
			posY=(posY+dy)&(owner->map->getMaskH());
			action=FLY;
			speed=performance[FLY];
			assert(owner->map->getAirUnit(posX, posY)==NOGUID);
			owner->map->setAirUnit(posX, posY, gid);
			break;
		}

		case MOV_GOING_TARGET:
		{
			assert(!performance[FLY]);
			owner->map->setGroundUnit(posX, posY, NOGUID);
			owner->map->pathfindPointToPoint(posX, posY, targetX, targetY, &dx, &dy, (performance[SWIM] > 0 ? true : false), owner->me, 12);
			directionFromDxDy();
			posX=(posX+dx)&(owner->map->getMaskW());
			posY=(posY+dy)&(owner->map->getMaskH());

			if(dx == 0 && dy == 0)
				owner->map->markImmobileUnit(posX, posY, owner->teamNumber);

			selectPreferredGroundMovement();
			speed=performance[action];
			assert(owner->map->getGroundUnit(posX, posY)==NOGUID);
			owner->map->setGroundUnit(posX, posY, gid);
			break;
		}

		case MOV_FLYING_TARGET:
		{
			owner->map->setAirUnit(posX, posY, NOGUID);

			flyToTarget();

			posX=(posX+dx)&(owner->map->getMaskW());
			posY=(posY+dy)&(owner->map->getMaskH());

			action=FLY;
			speed=performance[FLY];

			owner->map->setAirUnit(posX, posY, gid);
			break;
		}

		case MOV_GOING_DX_DY:
		{
			bool fly=performance[FLY];
			if (fly)
				owner->map->setAirUnit(posX, posY, NOGUID);
			else
				owner->map->setGroundUnit(posX, posY, NOGUID);

			directionFromDxDy();

			posX=(posX+dx)&(owner->map->getMaskW());
			posY=(posY+dy)&(owner->map->getMaskH());

			if(dx == 0 && dy == 0)
				owner->map->markImmobileUnit(posX, posY, owner->teamNumber);

			selectPreferredMovement();
			speed=performance[action];

			if (fly)
			{
				assert(owner->map->getAirUnit(posX, posY)==NOGUID);
				owner->map->setAirUnit(posX, posY, gid);
			}
			else
			{
				assert(owner->map->getGroundUnit(posX, posY)==NOGUID);
				owner->map->setGroundUnit(posX, posY, gid);
			}

			if (verbose)
				printf("guid=(%d) MOV_GOING_DX_DY d=(%d, %d; %d).\n", gid, direction, dx, dy);
			break;
		}

		case MOV_ENTERING_BUILDING:
		{
			// NOTE : this is a hack : We don't delete the unit on the map
			// because we have to draw it while it is entering.
			// owner->map->setUnit(posX, posY, NOUID);
			posX=(posX+dx)&(owner->map->getMaskW());
			posY=(posY+dy)&(owner->map->getMaskH());
			directionFromDxDy();
			selectPreferredMovement();
			speed=performance[action];
			break;
		}

		case MOV_EXITING_BUILDING:
		{
			directionFromDxDy();
			selectPreferredMovement();
			speed=performance[action];

			if (performance[FLY])
			{
				assert(owner->map->getAirUnit(posX, posY)==NOGUID);
				owner->map->setAirUnit(posX, posY, gid);
			}
			else
			{
				assert(owner->map->getGroundUnit(posX, posY)==NOGUID);
				owner->map->setGroundUnit(posX, posY, gid);
			}
			break;
		}

		case MOV_INSIDE:
		{
			break;
		}

		case MOV_FILLING:
		{
			owner->map->markImmobileUnit(posX, posY, owner->teamNumber);
			directionFromDxDy();
			action=BUILD;
			speed=performance[action];
			break;
		}

		case MOV_ATTACKING_TARGET:
		{
			owner->map->markImmobileUnit(posX, posY, owner->teamNumber);
			directionFromDxDy();
			action=ATTACK_SPEED;
			speed=performance[action];
			break;
		}

		case MOV_HARVESTING:
		{
			owner->map->markImmobileUnit(posX, posY, owner->teamNumber);
			directionFromDxDy();
			action=HARVEST;
			speed=performance[action];
			assert(speed!=0);
			break;
		}

		default:
		{
			assert (false);
			break;
		}
	}
}
