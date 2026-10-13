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
#include "TurretTargeting.h"

int Building::selectProductionRecipe() const
{
	Sint64 best = std::numeric_limits<Sint64>::max();
	int chosen = -1;
	for (unsigned u : type->semantics.production.enabledUnits)
		if (runtime->produces(u) && productionRatio(u) > 0)
		{
			const Sint64 proportion = (Sint64(productionUsed(u)) << FIXED_POINT_SHIFT_16) / productionRatio(u);
			if (proportion <= best) { best = proportion; chosen = u; }
		}
	return chosen;
}

bool Building::canAffordProduction(int unitType) const
{
	if (buildingState != ALIVE || siteCompletionPending) return false;
	if (unitType < 0 || unsigned(unitType) >= type->semantics.production.recipes.size()) return false;
	const auto& recipe = type->semantics.production.recipes[unitType];
	if (!runtime->produces(unitType)) return false;
	if (productionUnit == unitType) return true; // committed resources are reserved
	for (unsigned mask = recipe.costMask; mask; mask &= mask - 1)
	{
		const int r = std::countr_zero(mask);
		if (availableMaterial(r) < recipe.cost[r]) return false;
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
		releaseMaterials(type->semantics.production.recipes[productionUnit].cost);
		productionUnit = -1;
	}
}

void Building::restoreProductionReservations()
{
	if (productionUnit < 0) return;
	const auto& p = type->semantics.production;
	if (unsigned(productionUnit) >= p.recipes.size() || !p.recipes[productionUnit].enabled ||
		p.scheduling != BuildingProductionScheduling::WeightedCommittedJob ||
		buildingState != ALIVE || productionTimeout < -1 || productionTimeout > p.recipes[productionUnit].duration)
		throw std::runtime_error("Invalid saved committed production job");
	if (!restoreMaterialsReservation(p.recipes[productionUnit].cost))
		throw std::runtime_error("Saved production overbooks materials");
}

void Building::regenerationStep()
{
	if (buildingState == DEAD || hp >= getEffectiveMaxHp()) return;
	hp += std::min(runtime->regenerationPerTick, getEffectiveMaxHp() - hp);
}

void Building::swarmStep(void)
{
	if (buildingState != ALIVE || siteCompletionPending) return;
	const auto& production = type->semantics.production;
	const bool committed = runtime->has(BuildingRuntimeTraits::CommittedProduction);
	int chosen;
	if (committed)
	{
		if (productionUnit < 0)
		{
			chosen = selectProductionRecipe();
			if (chosen < 0 || !reserveMaterials(production.recipes[chosen].cost)) return;
			productionUnit = chosen;
			productionTimeout = production.recipes[chosen].duration;
		}
		chosen = productionUnit;
		if (productionTimeout >= 0) --productionTimeout;
	}
	else
	{
		// Late-choice recipes have identical validated costs and durations. A
		// pending timer needs only availability and an active ratio, not ranking.
		const bool affordable=canAffordProduction(production.fallbackUnit);
		bool active=false;
		for (unsigned unit : production.enabledUnits)
			active |= runtime->produces(unit) && productionRatio(unit)>0;
		if (active && affordable && productionTimeout>std::numeric_limits<Sint32>::min()) --productionTimeout;
		if (productionTimeout>=0 || !affordable) return;
		chosen=selectProductionRecipe();
		if (chosen<0) chosen=production.fallbackUnit;
	}
	if (committed && (productionTimeout>=0 || !canAffordProduction(chosen))) return;

	int x, y, dx, dy;
	const UnitType* ut = owner->race.getUnitType(chosen, 0);
	const auto& traits=owner->race.getRuntime(chosen);
    const bool canFly=traits.has(UnitRuntimeTraits::Fly) && ut->performance[FLY];
    const bool canWalk=traits.has(UnitRuntimeTraits::Walk) && ut->performance[WALK];
    const bool canSwim=traits.has(UnitRuntimeTraits::Swim) && ut->performance[SWIM];
    // An immobile unit still needs a legal birth cell outside its producer.
	const bool exitFound = canFly ? findAirExit(&x, &y, &dx, &dy)
		: findGroundExit(&x, &y, &dx, &dy, canSwim, canWalk || (!canWalk && !canSwim));
	if (!exitFound) return;
	Unit* u = owner->game->addUnit(x, y, owner->teamNumber, chosen, 0, 0, dx, dy);
	if (!u) return;
	const auto& recipe = production.recipes[chosen];
	if (!committed)
	{
		const bool reserved = reserveMaterials(recipe.cost);
		assert(reserved); // availability was checked before creating the unit
		(void)reserved;
	}
	consumeReservedMaterials(recipe.cost, GameplayMeasurements::SPAWNING);
	productionUnit = -1;
	++owner->stats.events().births[chosen];
	updateCallLists();
	u->activity = Unit::ACT_RANDOM;
	u->displacement = Unit::DIS_RANDOM;
	u->movement = Unit::MOV_EXITING_BUILDING;
	u->speed = unitTerrainMovementSpeed(u->performance[u->action], u->performance[FLY]
		? owner->map->terrainPropertiesAt(u->posX,u->posY).airSpeedQ8
		: owner->map->terrainPropertiesAt(u->posX,u->posY).groundSpeedQ8);
	productionTimeout = recipe.duration;
	++productionUsed(chosen);
	bool allDone = true;
	for (unsigned i : production.enabledUnits)
		if (runtime->produces(i) && productionUsed(i) < productionRatio(i)) allDone = false;
	if (allDone) { std::fill(std::begin(percentUsed), std::end(percentUsed), 0); std::fill(extraProductionUsed.begin(),extraProductionUsed.end(),0); }
}


namespace
{
	/// Half the side length of a bullet, in pixels — subtracted so the bullet is
	/// aimed at the centre of the target tile rather than its top-left corner.
	constexpr int BULLET_HALF_SIZE_PX = 4;

}

void Building::convertStoneToBullet()
{
	const int resource = type->semantics.ammunitionMaterial;
	const int cost = type->semantics.ammunitionCost;
	if (availableMaterial(resource) >= cost && bullets <= type->maxBullets - type->multiplierStoneToBullets)
	{
		materials[resource] -= cost;
		owner->stats.events().consumed[GameplayMeasurements::AMMUNITION][resource] += cost;
		bullets += type->multiplierStoneToBullets;
		if (cost && (type->useTeamMaterials || type->runtimeSuppliesStock || type->runtimeSuppliesDirectStock)) owner->map->dirtyMarketGradientsSlot(owner->teamNumber, resource);
		updateCallLists();
	}
}

bool Building::tickShootingCooldown()
{
	if (shootingCooldown > 0)
	{
		shootingCooldown -= runtime->shootRhythm;
		return false;
	}
	return true;
}

void Building::turretStep(Uint32 stepCounter)
{
	convertStoneToBullet();
	TurretTargeting::observeAttempt(*this);

	// compute cooldown
	if (!tickShootingCooldown())
		return;

	// if we have no bullet, don't try to shoot
	if (bullets <= 0)
		return;

	shootingStep = (shootingStep+1) & (SHOOTING_ANIMATION_FRAMES - 1);

#ifdef GLOB2_TURRET_ENGINE_BENCHMARK
    // Linked only into the dedicated research binary, never the game or tests.
    extern TurretTarget turretEngineBenchmarkTarget(const Building&);
    TurretTarget target = turretEngineBenchmarkTarget(*this);
#else
    const auto chosen=TurretTargeting::firstValid(*this,owner->game->turretTargeting()->entries(*this));
    TurretTarget target;
    if (chosen.found()) {
        target.type=chosen.entry.kind==TurretTargeting::Kind::Building ? TARGETTYPE_BUILDING : TARGETTYPE_GENERAL;
        target.x=chosen.x; target.y=chosen.y; target.ticks=chosen.ticks;
    }
#endif

	if (target.found())
	{
		shootingStep = 0;
		fireBullet(target, stepCounter);
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

// A read-only immediate reference for diagnostics uses the same pure kernel.
Building::TurretTarget Building::targetingReference() const
{
    const auto chosen=TurretTargeting::plan(*this,0,0).preferred;
    TurretTarget target;
    if (chosen.found()) {
        target.type=static_cast<TurretTargetType>(chosen.priority);
        target.score=chosen.score; target.ticks=chosen.ticks; target.x=chosen.x; target.y=chosen.y;
    }
    return target;
}

Building::TurretFiringSolution Building::computeFiringSolution(int targetX, int targetY) const
{
	Map *map = owner->map;

	// half the building's pixel width/height — the centre offset of the footprint
	const int halfWidthPx = (runtime->width << Map::TILE_PIXEL_SHIFT) / 2;
	const int halfHeightPx = (runtime->height << Map::TILE_PIXEL_SHIFT) / 2;

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
		sol.speedX=static_cast<int>((Sint64(dpx)*runtime->shootSpeed)/(Sint64(mdp)<<Q8_FIXED_POINT_SHIFT));
		sol.speedY=static_cast<int>((Sint64(dpy)*runtime->shootSpeed)/(Sint64(mdp)<<Q8_FIXED_POINT_SHIFT));
		assert(sol.speedX!=0);
		sol.ticksLeft=abs(mdp/sol.speedX);
	}
	else
	{
		mdp=abs(dpy);
		sol.speedX=static_cast<int>((Sint64(dpx)*runtime->shootSpeed)/(Sint64(mdp)<<Q8_FIXED_POINT_SHIFT));
		sol.speedY=static_cast<int>((Sint64(dpy)*runtime->shootSpeed)/(Sint64(mdp)<<Q8_FIXED_POINT_SHIFT));
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
		Bullet *b = new Bullet(sol.originX, sol.originY, sol.speedX, sol.speedY, sol.ticksLeft, applyAreaAttack(runtime->projectileBuildingDamage), target.x, target.y, posX-1, posY-1, runtime->width+2, runtime->height+2);
		b->unitDamage.resize(runtime->unitCount);
		for (unsigned unit=0;unit<runtime->unitCount;++unit) b->unitDamage[unit]=applyAreaAttack(runtime->damage(unit));
		b->sourceTeam = owner->teamNumber;
		++owner->stats.events().shots[GameplayMeasurements::TOWER];
		s->bullets.push_front(b);
		bullets--;
		shootingCooldown = SHOOTING_COOLDOWN_MAX;
		lastShootStep = stepCounter;
		lastShootSpeedX = sol.speedX;
		lastShootSpeedY = sol.speedY;
	}
}






void Building::transitionProductionPreferences(const BuildingType* previous, const BuildingType* origin, bool restoring)
{
    totalRatio=0;
    for (unsigned unit=0; unit<productionTypeCount(); ++unit)
    {
        auto& preference=productionRatio(unit);
        if (restoring) preference=originProductionRatio(unit);
        else if (!type->semantics.production.recipes[unit].enabled) preference=0;
        else if (previous->semantics.production.recipes[unit].enabled) {} // retain the current preference
        else if (origin && origin->semantics.production.recipes[unit].enabled) preference=originProductionRatio(unit);
        else preference=type->semantics.production.initialRatios[unit];
        totalRatio+=preference; productionUsed(unit)=0;
    }
}
