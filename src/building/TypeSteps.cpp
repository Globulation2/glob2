// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#include <list>
#include <math.h>
#include <stdlib.h>
#include <algorithm>
#include <bit>
#include <limits>
#include <stdexcept>

#include "Building.h"
#include "BuildingType.h"
#include "EngineTiming.h"
#include "FixedPoint.h"
#include "Game.h"
#include "Map.h"
#include "Team.h"
#include "Unit.h"
#include "UnitTiming.h"
#include "Utilities.h"
#include "Order.h"
#include "Bullet.h"

int Building::selectProductionRecipe() const
{
	Sint64 best = std::numeric_limits<Sint64>::max();
	int chosen = -1;
	for (int u = 0; u < NB_UNIT_TYPE; ++u)
		if (type->semantics.production.recipes[u].enabled && ratio[u] > 0)
		{
			const Sint64 proportion = (Sint64(percentUsed[u]) << FIXED_POINT_SHIFT_16) / ratio[u];
			if (proportion <= best) { best = proportion; chosen = u; }
		}
	return chosen;
}

bool Building::canAffordProduction(int unitType) const
{
	if (buildingState != ALIVE) return false;
	if (unitType < 0 || unitType >= NB_UNIT_TYPE) return false;
	const auto& recipe = type->semantics.production.recipes[unitType];
	if (!recipe.enabled) return false;
	if (productionUnit == unitType) return true; // committed resources are reserved
	for (unsigned mask = recipe.costMask; mask; mask &= mask - 1)
	{
		const int r = std::countr_zero(mask);
		if (availableResource(r) < recipe.cost[r]) return false;
	}
	return true;
}

void Building::resetProduction()
{
	cancelProduction();
	productionTimeout = 0;
	for (const auto& recipe : type->semantics.production.recipes)
		if (recipe.enabled) { productionTimeout = recipe.duration; break; }
}

void Building::cancelProduction()
{
	if (productionUnit >= 0)
	{
		releaseResources(type->semantics.production.recipes[productionUnit].cost);
		productionUnit = -1;
	}
}

void Building::restoreProductionReservations()
{
	if (productionUnit < 0) return;
	const auto& p = type->semantics.production;
	if (productionUnit >= NB_UNIT_TYPE || !p.recipes[productionUnit].enabled ||
		p.scheduling != BuildingProductionScheduling::WeightedCommittedJob ||
		buildingState != ALIVE || productionTimeout < -1 || productionTimeout > p.recipes[productionUnit].duration)
		throw std::runtime_error("Invalid saved committed production job");
	if (!restoreResourcesReservation(p.recipes[productionUnit].cost))
		throw std::runtime_error("Saved production overbooks resources");
}

void Building::regenerationStep()
{
	if (buildingState == DEAD || hp >= getEffectiveMaxHp()) return;
	hp += std::min(type->semantics.regenerationPerTick, getEffectiveMaxHp() - hp);
}

void Building::swarmStep(void)
{
	if (buildingState != ALIVE || siteCompletionPending) return;
	const auto& production = type->semantics.production;
	const bool committed = production.scheduling == BuildingProductionScheduling::WeightedCommittedJob;
	int chosen;
	if (committed)
	{
		if (productionUnit < 0)
		{
			chosen = selectProductionRecipe();
			if (chosen < 0 || !reserveResources(production.recipes[chosen].cost)) return;
			productionUnit = chosen;
			productionTimeout = production.recipes[chosen].duration;
		}
		chosen = productionUnit;
		if (productionTimeout >= 0) --productionTimeout;
	}
	else
	{
		chosen = selectProductionRecipe();
		if (chosen >= 0 && canAffordProduction(chosen) && productionTimeout > std::numeric_limits<Sint32>::min())
			--productionTimeout;
		if (chosen < 0) chosen = production.fallbackUnit;
	}
	if (productionTimeout >= 0 || !canAffordProduction(chosen)) return;

	int x, y, dx, dy;
	const UnitType* ut = owner->race.getUnitType(chosen, 0);
	const bool exitFound = ut->performance[FLY] ? findAirExit(&x, &y, &dx, &dy)
		: findGroundExit(&x, &y, &dx, &dy, ut->performance[SWIM]);
	if (!exitFound) return;
	Unit* u = owner->game->addUnit(x, y, owner->teamNumber, chosen, 0, 0, dx, dy);
	if (!u) return;
	const auto& recipe = production.recipes[chosen];
	if (!committed)
	{
		const bool reserved = reserveResources(recipe.cost);
		assert(reserved); // availability was checked before creating the unit
		(void)reserved;
	}
	consumeReservedResources(recipe.cost, GameplayMeasurements::SPAWNING);
	productionUnit = -1;
	++owner->stats.measurements.births[chosen];
	updateCallLists();
	u->activity = Unit::ACT_RANDOM;
	u->displacement = Unit::DIS_RANDOM;
	u->movement = Unit::MOV_EXITING_BUILDING;
	u->speed = unitTerrainMovementSpeed(u->performance[u->action], u->performance[FLY]
		? owner->map->terrainPropertiesAt(u->posX,u->posY).airSpeedQ8
		: owner->map->terrainPropertiesAt(u->posX,u->posY).groundSpeedQ8);
	productionTimeout = recipe.duration;
	++percentUsed[chosen];
	bool allDone = true;
	for (int i = 0; i < NB_UNIT_TYPE; ++i)
		if (production.recipes[i].enabled && percentUsed[i] < ratio[i]) allDone = false;
	if (allDone) std::fill(std::begin(percentUsed), std::end(percentUsed), 0);
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
		return u->speed > 0 ? (TILE_DELTA_RANGE - u->delta) / u->speed : std::numeric_limits<int>::max();
	}
}

void Building::convertStoneToBullet()
{
	const int resource = type->semantics.ammunitionResource;
	const int cost = type->semantics.ammunitionCost;
	if (availableResource(resource) >= cost && bullets <= type->maxBullets - type->multiplierStoneToBullets)
	{
		resources[resource] -= cost;
		owner->stats.measurements.consumed[GameplayMeasurements::AMMUNITION][resource] += cost;
		bullets += type->multiplierStoneToBullets;
		if (cost && type->runtimeSuppliesStock) owner->map->dirtyMarketGradients(owner->teamNumber, resource);
		updateCallLists();
	}
}

bool Building::tickShootingCooldown()
{
	if (shootingCooldown > 0)
	{
		shootingCooldown -= type->shootRhythm;
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
	int targetWeakness = 0; // 0 to 512
	if (target->hp > 0)
	{
		if (target->hp < type->semantics.projectileDamage[target->typeNum]) // hahaha, how mean!
			targetWeakness = 512;
		else
			targetWeakness = 256 / target->hp;
	}
	int targetProximity = 0; // 0 to 512
	if (ring <= 0)
		targetProximity = 512;
	else
		targetProximity = (256 / ring);
	return targetOffense + targetWeakness + targetProximity;
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

bool Building::hasClearShotTo(int targetX, int targetY) const
{
    const Map *map = owner->map;
    if (!map->hasProjectileBlockingTerrain()) return true;
    if (map->terrainPropertiesAt(targetX,targetY).projectileBlocks) return false;
    const auto shot = computeFiringSolution(targetX,targetY);
    return map->projectilePathClear(shot.originX,shot.originY,
        shot.originX+shot.speedX*shot.ticksLeft,shot.originY+shot.speedY*shot.ticksLeft);
}

void Building::considerScanTile(int targetX, int targetY, int ring, int ticksToHit,
                                Uint32 enemies, Map* map, TurretTarget& best) const
{
	if (!hasClearShotTo(targetX,targetY)) return;
	int targetGUID = map->getGroundUnit(targetX, targetY);
	int airTargetGUID = map->getAirUnit(targetX, targetY);
	if (targetGUID != NOGUID)
	{
		Sint32 otherTeam = Unit::GIDtoTeam(targetGUID);
		Sint32 targetID = Unit::GIDtoID(targetGUID);
		Uint32 otherTeamMask = Team::teamNumberToMask(otherTeam);
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
				if (testUnit->typeNum == WARRIOR && type->semantics.projectileDamage[WARRIOR] > 0)
				{
					applyCandidate(best, scoreWarriorTarget(testUnit, ring),
						targetTicks, targetX, targetY, TARGETTYPE_WARRIOR);
				}
				else if ((best.type != TARGETTYPE_WARRIOR) && (testUnit->typeNum == WORKER) && type->semantics.projectileDamage[WORKER] > 0)
				{
					// adjust score for range
					applyCandidate(best, -testUnit->hp,
						targetTicks, targetX, targetY, TARGETTYPE_WORKER);
				}
			}
		}
	}
	//explorers are now priority targets as defined later

	if (airTargetGUID != NOGUID && type->semantics.projectileDamage[EXPLORER] > 0)
	{
		Sint32 otherTeam = Unit::GIDtoTeam(airTargetGUID);
		Sint32 targetID = Unit::GIDtoID(airTargetGUID);
		Uint32 otherTeamMask = Team::teamNumberToMask(otherTeam);
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
	if (best.type == TARGETTYPE_NONE && type->semantics.projectileBuildingDamage > 0)
	{
		Uint16 targetGBID = map->getBuilding(targetX, targetY);
		if (targetGBID != NOGBID)
		{
			Sint32 otherTeam = Building::GIDtoTeam(targetGBID);
			Uint32 otherTeamMask = Team::teamNumberToMask(otherTeam);
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

	Uint32 enemies = owner->attackableTeams();
	Map *map = owner->map;
	assert(map);

	// half the building's pixel width — the centre offset of the turret footprint
	const int halfWidthPx = (type->width << Map::TILE_PIXEL_SHIFT) / 2;

	TurretTarget best;
	if (type->width != TURRET_SIZE || type->height != TURRET_SIZE)
	{
		const auto consider = [&](int x, int y, int ring) {
			const auto shot = computeFiringSolution(x, y);
			considerScanTile(x, y, ring, shot.ticksLeft, enemies, map, best);
		};
		// The footprint may contain targets for a non-occupying structure.
		for (int y = posY; y < posY + type->height; ++y)
			for (int x = posX; x < posX + type->width; ++x) consider(x, y, 0);
		for (int ring = 1; ring <= range && best.type != TARGETTYPE_EXPLORER; ++ring)
		{
			const int left = posX - ring, right = posX + type->width - 1 + ring;
			const int top = posY - ring, bottom = posY + type->height - 1 + ring;
			for (int x = left; x <= right; ++x) { consider(x, top, ring); consider(x, bottom, ring); }
			for (int y = top + 1; y < bottom; ++y) { consider(left, y, ring); consider(right, y, ring); }
		}
		return best;
	}

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

	if (dpx == 0 && dpy == 0)
	{
		sol.speedX = sol.speedY = sol.ticksLeft = 0;
		return sol;
	}
	if (abs(dpx)>abs(dpy)) //we avoid a square root, since all distances are squares lengthed.
	{
		mdp=abs(dpx);
		sol.speedX=static_cast<int>((Sint64(dpx)*type->shootSpeed)/(Sint64(mdp)<<Q8_FIXED_POINT_SHIFT));
		sol.speedY=static_cast<int>((Sint64(dpy)*type->shootSpeed)/(Sint64(mdp)<<Q8_FIXED_POINT_SHIFT));
		assert(sol.speedX!=0);
		sol.ticksLeft=abs(mdp/sol.speedX);
	}
	else
	{
		mdp=abs(dpy);
		sol.speedX=static_cast<int>((Sint64(dpx)*type->shootSpeed)/(Sint64(mdp)<<Q8_FIXED_POINT_SHIFT));
		sol.speedY=static_cast<int>((Sint64(dpy)*type->shootSpeed)/(Sint64(mdp)<<Q8_FIXED_POINT_SHIFT));
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
		Bullet *b = new Bullet(sol.originX, sol.originY, sol.speedX, sol.speedY, sol.ticksLeft, type->semantics.projectileBuildingDamage, target.x, target.y, posX-1, posY-1, type->width+2, type->height+2);
		b->unitDamage = type->semantics.projectileDamage;
		b->sourceTeam = owner->teamNumber;
		++owner->stats.measurements.shots[GameplayMeasurements::TOWER];
		s->bullets.push_front(b);
		bullets--;
		shootingCooldown = SHOOTING_COOLDOWN_MAX;
		lastShootStep = stepCounter;
		lastShootSpeedX = sol.speedX;
		lastShootSpeedY = sol.speedY;
	}
}




