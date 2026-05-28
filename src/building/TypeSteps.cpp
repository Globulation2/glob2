// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#include <list>
#include <math.h>
#include <Stream.h>
#include <stdlib.h>
#include <algorithm>
#include <climits>

#include "Building.h"
#include "BuildingType.h"
#include "EngineTiming.h"
#include "FixedPoint.h"
#include "Game.h"
#include "GlobalContainer.h"
#include "Map.h"
#include "Team.h"
#include "Unit.h"
#include "Utilities.h"
#include "Order.h"
#include "Bullet.h"
#include "Integrity.h"

void Building::swarmStep(void)
{
	// increase HP
	if (hp<type->hpMax)
		hp++;
	assert(NB_UNIT_TYPE==3);
	if ((ressources[CORN]>=type->ressourceForOneUnit)&&(ratio[0]|ratio[1]|ratio[2]))
		productionTimeout--;

	if (productionTimeout<0)
	{
		// We find the kind of unit we have to create:
		Sint32 fProportion;
		Sint32 fMinProportion = MIN_PROPORTION_INIT;
		int minType=-1;
		for (int i=0; i<NB_UNIT_TYPE; i++)
			if (ratio[i]!=0)
			{
				fProportion=(percentUsed[i]<<FIXED_POINT_SHIFT_16)/ratio[i];
				if (fProportion<=fMinProportion)
				{
					fMinProportion=fProportion;
					minType=i;
				}
			}

		if (minType==-1)
			minType=0;
		assert(minType>=0);
		assert(minType<NB_UNIT_TYPE);
		if (minType<0 || minType>=NB_UNIT_TYPE)
			minType=0;

		// We get the unit UnitType:
		int posX, posY, dx, dy;
		UnitType *ut=owner->race.getUnitType(minType, 0);

		// Is there a place to exit ?
		bool exitFound;
		if (ut->performance[FLY])
			exitFound=findAirExit(&posX, &posY, &dx, &dy);
		else
			exitFound=findGroundExit(&posX, &posY, &dx, &dy, ut->performance[SWIM]);
		if (exitFound)
		{
			Unit * u=owner->game->addUnit(posX, posY, owner->teamNumber, minType, 0, 0, dx, dy);
			if (u)
			{
				ressources[CORN]-=type->ressourceForOneUnit;
				updateCallLists();

				u->activity=Unit::ACT_RANDOM;
				u->displacement=Unit::DIS_RANDOM;
				u->movement=Unit::MOV_EXITING_BUILDING;
				u->speed=u->performance[u->action];

				productionTimeout=type->unitProductionTime;

				// We update percentUsed[]
				percentUsed[minType]++;

				bool allDone=true;
				for (int i=0; i<NB_UNIT_TYPE; i++)
					if (percentUsed[i]<ratio[i])
						allDone=false;

				if (allDone)
					for (int i=0; i<NB_UNIT_TYPE; i++)
						percentUsed[i]=0;
			}
			else if (verbose)
				printf("WARNING, no more UNIT ID free for team %d\n", owner->teamNumber);
		}
	}
}


void Building::turretStep(Uint32 stepCounter)
{
	// create bullet from stones in stock
	if (ressources[STONE]>0 && (bullets<=(type->maxBullets-type->multiplierStoneToBullets)))
	{
		ressources[STONE]--;
		bullets += type->multiplierStoneToBullets;

		// we need to be stone-feeded
		updateCallLists();
	}

	// compute cooldown
	if (shootingCooldown > 0)
	{
		shootingCooldown -= type->shootRythme;
		return;
	}

	// if we have no bullet, don't try to shoot
	if (bullets <= 0)
		return;

	//for some reason, any turret that is not 2x2 makes no sense at all to the game
	assert(type->width == TURRET_SIZE);
	assert(type->height == TURRET_SIZE);

	int range = type->shootingRange;
	shootingStep = (shootingStep+1) & (SHOOTING_ANIMATION_FRAMES - 1);

	Uint32 enemies = owner->enemies;
	Map *map = owner->map;
	assert(map);

	// the type of target we have found
	enum TargetType
	{
		TARGETTYPE_NONE,
		TARGETTYPE_BUILDING,
		TARGETTYPE_WORKER,
		TARGETTYPE_WARRIOR,
		TARGETTYPE_EXPLORER,
	};
	// The type of the best target we have found up to now
	TargetType targetFound = TARGETTYPE_NONE;
	// The score of the best target we have found up to now
	int bestScore = INT_MIN;
	// The number of ticks before the unit may move away
	int bestTicks = 0;
	// The position of the best target we have found up to now
	int bestTargetX = 0, bestTargetY=0;

	for (int i=0; i<=range ; i++)
	{
		// The number of ticks before the bullet hits the target at range "i".
		int ticksToHit = ((i << Map::TILE_PIXEL_SHIFT) + ((type->width) << 4)) / (type->shootSpeed>>Q8_FIXED_POINT_SHIFT);
		for (int j=0; j<=i ; j++)
		{
			for (int k=0; k<8; k++)
			{
				int targetX, targetY;
				switch (k)
				{
					case 0:
					targetX=posX-j;
					targetY=posY-i;
					break;
					case 1:
					targetX=posX+j+1;
					targetY=posY-i;
					break;
					case 2:
					targetX=posX-j;
					targetY=posY+i+1;
					break;
					case 3:
					targetX=posX+j+1;
					targetY=posY+i+1;
					break;
					case 4:
					targetX=posX-i;
					targetY=posY-j;
					break;
					case 5:
					targetX=posX+i+1;
					targetY=posY-j;
					break;
					case 6:
					targetX=posX-i;
					targetY=posY+j+1;
					break;
					case 7:
					targetX=posX+i+1;
					targetY=posY+j+1;
					break;
					default:
					assert(false);
					targetX=0;
					targetY=0;
					break;
				}
				int targetGUID = map->getGroundUnit(targetX, targetY);
				int airTargetGUID = map->getAirUnit(targetX, targetY);
				if (targetGUID != NOGUID)
				{
					Sint32 otherTeam = Unit::GIDtoTeam(targetGUID);
					Sint32 targetID = Unit::GIDtoID(targetGUID);
					Uint32 otherTeamMask = 1<<otherTeam;
					if (enemies & otherTeamMask)
					{
						Unit *testUnit = owner->game->teams[otherTeam]->myUnits[targetID];
						if ((owner->sharedVisionExchange & otherTeamMask) == 0)
						{
							int targetTicks = (256 - testUnit->delta) / testUnit->speed;
							// skip this unit if it will move away too soon.
							if (targetTicks <= ticksToHit)
								continue;
							// shoot warrior first, then workers if no warrior
							if (testUnit->typeNum == WARRIOR)
							{
								int targetOffense = (testUnit->getRealAttackStrength() * testUnit->performance[ATTACK_SPEED]); // 88 to 1024
								int targetWeakeness = 0; // 0 to 512
								if (testUnit->hp > 0)
								{
									if (testUnit->hp < type->shootDamage) // hahaha, how mean!
										targetWeakeness = 512;
									else
										targetWeakeness = 256 / testUnit->hp;
								}
								int targetProximity = 0; // 0 to 512
								if (i <= 0)
									targetProximity = 512;
								else
									targetProximity = (256 / i);
								int targetScore = targetOffense + targetWeakeness + targetProximity;
								// lower scores are overriden
								if (targetScore > bestScore)
								{
									bestScore = targetScore;
									bestTicks = targetTicks;
									bestTargetX = targetX;
									bestTargetY = targetY;
									targetFound = TARGETTYPE_WARRIOR;
								}
							}
							else if ((targetFound != TARGETTYPE_WARRIOR) && (testUnit->typeNum == WORKER))
							{
								// adjust score for range
								int targetScore = - testUnit->hp;
								// lower scores are overriden
								if (targetScore > bestScore)
								{
									bestScore = targetScore;
									bestTicks = targetTicks;
									bestTargetX = targetX;
									bestTargetY = targetY;
									targetFound = TARGETTYPE_WORKER;
								}
							}
						}
					}
				}
				//explorers are now priority targets as defined later

				if (airTargetGUID != NOGUID)
				{
					Sint32 otherTeam = Unit::GIDtoTeam(airTargetGUID);
					Sint32 targetID = Unit::GIDtoID(airTargetGUID);
					Uint32 otherTeamMask = 1<<otherTeam;
					if (enemies & otherTeamMask)
					{
						Unit *testUnit = owner->game->teams[otherTeam]->myUnits[targetID];
						if ((owner->sharedVisionExchange & otherTeamMask) == 0)
						{
							int targetTicks = (256 - testUnit->delta) / testUnit->speed;
							// skip this unit if it will move away too soon.
							if (targetTicks <= ticksToHit)
								continue;
							//Using simple calculation for now (should always shoot ground-attackers first, probably)
							// adjust score for range
							int targetScore = - testUnit->hp;
							// lower scores are overriden
							if (targetScore > bestScore)
							{
								bestScore = targetScore;
								bestTicks = targetTicks;
								bestTargetX = targetX;
								bestTargetY = targetY;
								targetFound = TARGETTYPE_EXPLORER;
							}
						}
					}
				}

				// shoot building only if no unit is found
				if (targetFound == TARGETTYPE_NONE)
				{
					Uint16 targetGBID = map->getBuilding(targetX, targetY);
					if (targetGBID != NOGBID)
					{
						Sint32 otherTeam = Building::GIDtoTeam(targetGBID);
						//int otherID = Building::GIDtoID(targetGBID);
						Uint32 otherTeamMask = 1<<otherTeam;
						if (enemies & otherTeamMask)
						{
							// adjust score for range
							int targetScore = - i;
							if (targetScore > bestScore)
							{
								bestScore = targetScore;
								bestTicks = 256;
								bestTargetX = targetX;
								bestTargetY = targetY;
								targetFound = TARGETTYPE_BUILDING;
							}
						}
					}
				}
			}
		}
		if (targetFound == TARGETTYPE_EXPLORER)
			break;//specifying explorers as high priority
	}

	if (targetFound != TARGETTYPE_NONE)
	{
		shootingStep = 0;

		//printf("%d found target found: (%d, %d) \n", gid, targetX, targetY);
		Sector *s=owner->map->getSector(getMidX(), getMidY());

		int px, py;
		px=((posX)<<Map::TILE_PIXEL_SHIFT)+((type->width)<<4);
		py=((posY)<<Map::TILE_PIXEL_SHIFT)+((type->height)<<4);

		int speedX, speedY, ticksLeft;

		// TODO : shall we really uses shootSpeed ?
		// FIXME : is it correct this way ? Is there a function for this ?
		int dpx=(bestTargetX*Map::TILE_PX)+Map::HALF_TILE_PX-4-px; // 4 is the half size of the bullet
		int dpy=(bestTargetY*Map::TILE_PX)+Map::HALF_TILE_PX-4-py;
		//printf("%d insert: dp=(%d, %d).\n", gid, dpx, dpy);
		if (dpx>(map->getW()<<4))
			dpx=dpx-(map->getW()<<Map::TILE_PIXEL_SHIFT);
		if (dpx<-(map->getW()<<4))
			dpx=dpx+(map->getW()<<Map::TILE_PIXEL_SHIFT);
		if (dpy>(map->getH()<<4))
			dpy=dpy-(map->getH()<<Map::TILE_PIXEL_SHIFT);
		if (dpy<-(map->getH()<<4))
			dpy=dpy+(map->getH()<<Map::TILE_PIXEL_SHIFT);

		int mdp;

		assert(dpx);
		assert(dpy);
		if (abs(dpx)>abs(dpy)) //we avoid a square root, since all ditances are squares lengthed.
		{
			mdp=abs(dpx);
			speedX=((dpx*type->shootSpeed)/(mdp<<Q8_FIXED_POINT_SHIFT));
			speedY=((dpy*type->shootSpeed)/(mdp<<Q8_FIXED_POINT_SHIFT));
			assert(speedX!=0);
			ticksLeft=abs(mdp/speedX);
		}
		else
		{
			mdp=abs(dpy);
			speedX=((dpx*type->shootSpeed)/(mdp<<Q8_FIXED_POINT_SHIFT));
			speedY=((dpy*type->shootSpeed)/(mdp<<Q8_FIXED_POINT_SHIFT));
			assert(speedY!=0);
			ticksLeft=abs(mdp/speedY);
		}

		if (ticksLeft < bestTicks)
		{
			Bullet *b = new Bullet(px, py, speedX, speedY, ticksLeft, type->shootDamage, bestTargetX, bestTargetY, posX-1, posY-1, type->width+2, type->height+2);
			s->bullets.push_front(b);
			bullets--;
			shootingCooldown = SHOOTING_COOLDOWN_MAX;
			lastShootStep = stepCounter;
			lastShootSpeedX = speedX;
			lastShootSpeedY = speedY;
		}
	}

}



// Per-tick maintenance for a clearing-flag building. While the flag has worker
// room, each swim variant's `localRessourcesCleanTime` is incremented by one
// tick; once the *prior* value exceeds CLEARING_FLAG_REFRESH_TICKS (~5s), the
// local-ressource gradient is recomputed via Map::updateLocalRessources, which
// resets the timer back to 0. If the recompute reports no reachable ressources,
// every worker is released.
//
// PORT: timer is reset inside Map::updateLocalRessources (MapGradientBuilding.cpp:197),
// PORT: not here. It is also bumped by +=16 from MapPathfindRessource.cpp:132 when units
// PORT: find resources unreachable, which short-circuits the wait.
void Building::clearingFlagStep()
{
	const size_t workerCap = static_cast<size_t>(std::max<Sint32>(0, maxUnitWorking));
	if (unitsWorking.size() >= workerCap)
		return;

	for (int canSwim=0; canSwim<SWIM_VARIANT_COUNT; canSwim++)
	{
		int& timer = localRessourcesCleanTime[canSwim];
		const bool refreshDue = (timer > CLEARING_FLAG_REFRESH_TICKS);
		++timer;
		if (!refreshDue)
			continue;

		if (!owner->map->updateLocalRessources(this, canSwim))
		{
			// PORT: verify standardRandomActivity() detaches unit->attachedBuilding and updates call lists.
			// PORT: if not, the Rust port should call removeUnitFromWorking(unit) per unit instead of clear().
			releaseAllWorkers();
		}
	}
}



