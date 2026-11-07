// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>

#include <SDL3/SDL_stdinc.h>

#include "UnitConsts.h"
#include "sim/ClientEvents.h"
#include "sim/EntityRef.h"

class Building;
class Unit;
namespace HiringPlanner { struct BuildingLists; }
namespace TurretTargeting { class Pipeline; }

//! The few Game operations team, building and unit code needs beyond the data
//! in SimContext. Game implements it (each override is final, so calls made
//! through a Game stay direct); entity code calls it as
//! `owner->ctx.services->x(...)` without including Game.h. Each is an
//! out-of-line call on Game already, so this adds only the virtual dispatch.
//! See Game.h for what each does.
class SimServices
{
public:
	virtual Uint32 allocateScriptIdentity(bool building, Uint16 gid) = 0;
	virtual void publishClientEvent(ClientEventVariant event) = 0;
	virtual Building *resolveBuilding(BuildingRef ref) const = 0;
	virtual Unit *resolveUnit(UnitRef ref) const = 0;
	virtual const HiringPlanner::BuildingLists *hiringCandidates(const Building &) const = 0;
	virtual TurretTargeting::Pipeline *turretTargeting() const = 0;
	virtual void forgetTurret(BuildingRef) = 0;
	virtual void dirtyWarFlagGradient() = 0;
	virtual bool isBuildingTypeAvailable(int typeNum) const = 0;
	virtual Unit *addUnit(int x, int y, int team, int type, int level, int delta, int dx, int dy,
		const std::array<int, NB_ABILITY> *initialLevels = nullptr) = 0;

protected:
	~SimServices() = default;
};
