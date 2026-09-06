// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#include <list>
#include <math.h>
#include <stdlib.h>
#include <algorithm>

#include "Building.h"
#include "BuildingType.h"
#include "EngineTiming.h"
#include "FixedPoint.h"
#include "Game.h"
#include "Map.h"
#include "Team.h"
#include "Unit.h"
#include "Utilities.h"
#include "Order.h"
#include "Bullet.h"

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


namespace
{
	/// A unit's intra-tile movement progress (`delta`) spans 0..255; reaching
	/// 256 means it has fully crossed into the next tile.
	constexpr int TILE_DELTA_RANGE = 256;
	/// Half the side length of a bullet, in pixels — subtracted so the bullet is
	/// aimed at the centre of the target tile rather than its top-left corner.
	constexpr int BULLET_HALF_SIZE_PX = 4;

	/// Ticks a unit will remain on its current tile before it may move away.
	int ticksUntilUnitMoves(const Unit* u)
	{
		return (TILE_DELTA_RANGE - u->delta) / u->speed;
	}
}

void Building::convertStoneToBullet()
{
	// create bullet from stones in stock
	if (ressources[STONE]>0 && (bullets<=(type->maxBullets-type->multiplierStoneToBullets)))
	{
		ressources[STONE]--;
		bullets += type->multiplierStoneToBullets;

		// we need to be stone-feeded
		updateCallLists();
	}
}

bool Building::tickShootingCooldown()
{
	if (shootingCooldown > 0)
	{
		shootingCooldown -= type->shootRythme;
		return false;
	}
	return true;
}

void Building::turretStep(Uint32 stepCounter)
{
	convertStoneToBullet();

	// compute cooldown
	if (!tickShootingCooldown())
		return;

	// if we have no bullet, don't try to shoot
	if (bullets <= 0)
		return;

	//for some reason, any turret that is not 2x2 makes no sense at all to the game
	assert(type->width == TURRET_SIZE);
	assert(type->height == TURRET_SIZE);

	shootingStep = (shootingStep+1) & (SHOOTING_ANIMATION_FRAMES - 1);

	TurretTarget target = findBestTarget();

	if (target.found())
	{
		shootingStep = 0;
		fireBullet(target, stepCounter);
	}
}

int Building::scoreWarriorTarget(const Unit* target, int ring) const
{
	int targetOffense = (target->getRealAttackStrength() * target->performance[ATTACK_SPEED]); // 88 to 1024
	int targetWeakeness = 0; // 0 to 512
	if (target->hp > 0)
	{
		if (target->hp < type->shootDamage) // hahaha, how mean!
			targetWeakeness = 512;
		else
			targetWeakeness = 256 / target->hp;
	}
	int targetProximity = 0; // 0 to 512
	if (ring <= 0)
		targetProximity = 512;
	else
		targetProximity = (256 / ring);
	return targetOffense + targetWeakeness + targetProximity;
}

void Building::applyCandidate(TurretTarget& best, int score, int ticks,
                              int x, int y, TurretTargetType type)
{
	// lower scores are overriden
	if (score > best.score)
	{
		best.score = score;
		best.ticks = ticks;
		best.x = x;
		best.y = y;
		best.type = type;
	}
}

void Building::considerScanTile(int targetX, int targetY, int ring, int ticksToHit,
                                Uint32 enemies, Map* map, TurretTarget& best) const
{
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
				int targetTicks = ticksUntilUnitMoves(testUnit);
				// skip this unit if it will move away too soon.
				if (targetTicks <= ticksToHit)
					return;
				// shoot warrior first, then workers if no warrior
				if (testUnit->typeNum == WARRIOR)
				{
					applyCandidate(best, scoreWarriorTarget(testUnit, ring),
						targetTicks, targetX, targetY, TARGETTYPE_WARRIOR);
				}
				else if ((best.type != TARGETTYPE_WARRIOR) && (testUnit->typeNum == WORKER))
				{
					// adjust score for range
					applyCandidate(best, -testUnit->hp,
						targetTicks, targetX, targetY, TARGETTYPE_WORKER);
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
				int targetTicks = ticksUntilUnitMoves(testUnit);
				// skip this unit if it will move away too soon.
				if (targetTicks <= ticksToHit)
					return;
				//Using simple calculation for now (should always shoot ground-attackers first, probably)
				// adjust score for range
				applyCandidate(best, -testUnit->hp,
					targetTicks, targetX, targetY, TARGETTYPE_EXPLORER);
			}
		}
	}

	// shoot building only if no unit is found
	if (best.type == TARGETTYPE_NONE)
	{
		Uint16 targetGBID = map->getBuilding(targetX, targetY);
		if (targetGBID != NOGBID)
		{
			Sint32 otherTeam = Building::GIDtoTeam(targetGBID);
			Uint32 otherTeamMask = 1<<otherTeam;
			if (enemies & otherTeamMask)
			{
				// adjust score for range
				applyCandidate(best, -ring,
					TILE_DELTA_RANGE, targetX, targetY, TARGETTYPE_BUILDING);
			}
		}
	}
}

Building::TurretTarget Building::findBestTarget() const
{
	int range = type->shootingRange;

	Uint32 enemies = owner->enemies;
	Map *map = owner->map;
	assert(map);

	// half the building's pixel width — the centre offset of the turret footprint
	const int halfWidthPx = (type->width << Map::TILE_PIXEL_SHIFT) / 2;

	TurretTarget best;

	for (int i=0; i<=range ; i++)
	{
		// The number of ticks before the bullet hits the target at range "i".
		int ticksToHit = ((i << Map::TILE_PIXEL_SHIFT) + halfWidthPx) / (type->shootSpeed>>Q8_FIXED_POINT_SHIFT);
		for (int j=0; j<=i ; j++)
		{
			for (int k=0; k<8; k++)
			{
				int targetX, targetY;
				turretScanTile(posX, posY, i, j, k, targetX, targetY);
				considerScanTile(targetX, targetY, i, ticksToHit, enemies, map, best);
			}
		}
		if (best.type == TARGETTYPE_EXPLORER)
			break;//specifying explorers as high priority
	}

	return best;
}

Building::TurretFiringSolution Building::computeFiringSolution(int targetX, int targetY) const
{
	Map *map = owner->map;

	// half the building's pixel width/height — the centre offset of the footprint
	const int halfWidthPx = (type->width << Map::TILE_PIXEL_SHIFT) / 2;
	const int halfHeightPx = (type->height << Map::TILE_PIXEL_SHIFT) / 2;

	TurretFiringSolution sol;
	sol.originX = ((posX)<<Map::TILE_PIXEL_SHIFT)+halfWidthPx;
	sol.originY = ((posY)<<Map::TILE_PIXEL_SHIFT)+halfHeightPx;

	// TODO : shall we really uses shootSpeed ?
	int dpx=(targetX*Map::TILE_PX)+Map::HALF_TILE_PX-BULLET_HALF_SIZE_PX-sol.originX;
	int dpy=(targetY*Map::TILE_PX)+Map::HALF_TILE_PX-BULLET_HALF_SIZE_PX-sol.originY;
	// toroidal wrap: if the target is more than half the map away, aim the short way around
	const int mapWidthPx = map->getW()<<Map::TILE_PIXEL_SHIFT;
	const int mapHeightPx = map->getH()<<Map::TILE_PIXEL_SHIFT;
	if (dpx>(mapWidthPx/2))
		dpx=dpx-mapWidthPx;
	if (dpx<-(mapWidthPx/2))
		dpx=dpx+mapWidthPx;
	if (dpy>(mapHeightPx/2))
		dpy=dpy-mapHeightPx;
	if (dpy<-(mapHeightPx/2))
		dpy=dpy+mapHeightPx;

	int mdp;

	assert(dpx);
	assert(dpy);
	if (abs(dpx)>abs(dpy)) //we avoid a square root, since all ditances are squares lengthed.
	{
		mdp=abs(dpx);
		sol.speedX=((dpx*type->shootSpeed)/(mdp<<Q8_FIXED_POINT_SHIFT));
		sol.speedY=((dpy*type->shootSpeed)/(mdp<<Q8_FIXED_POINT_SHIFT));
		assert(sol.speedX!=0);
		sol.ticksLeft=abs(mdp/sol.speedX);
	}
	else
	{
		mdp=abs(dpy);
		sol.speedX=((dpx*type->shootSpeed)/(mdp<<Q8_FIXED_POINT_SHIFT));
		sol.speedY=((dpy*type->shootSpeed)/(mdp<<Q8_FIXED_POINT_SHIFT));
		assert(sol.speedY!=0);
		sol.ticksLeft=abs(mdp/sol.speedY);
	}

	return sol;
}

void Building::fireBullet(const TurretTarget& target, Uint32 stepCounter)
{
	Sector *s=owner->map->getSector(getMidX(), getMidY());

	TurretFiringSolution sol = computeFiringSolution(target.x, target.y);

	if (sol.ticksLeft < target.ticks)
	{
		Bullet *b = new Bullet(sol.originX, sol.originY, sol.speedX, sol.speedY, sol.ticksLeft, type->shootDamage, target.x, target.y, posX-1, posY-1, type->width+2, type->height+2);
		s->bullets.push_front(b);
		bullets--;
		shootingCooldown = SHOOTING_COOLDOWN_MAX;
		lastShootStep = stepCounter;
		lastShootSpeedX = sol.speedX;
		lastShootSpeedY = sol.speedY;
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



