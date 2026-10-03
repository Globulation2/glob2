// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "Ressource.h"
#include "UnitConsts.h"
#include "sim/EntityRef.h"

#include <SDLGraphicContext.h>

#include <array>
#include <string>
#include <vector>

struct BuildingType;
class Race;

//! Presentation copy of a team: what drawing needs to colour and filter its entities.
struct SceneTeam
{
	GAGCore::Color color;
	int teamNumber = 0;
	Uint32 me = 0, allies = 0, sharedVisionOther = 0;
	int startPosX = 0, startPosY = 0;
	std::string firstPlayerName; //!< empty when uncontrolled
};

//! Presentation copy of a unit. Field names follow Unit so drawing code reads the
//! same; `team` indexes SceneEntities::teams (indices survive moving a Scene).
struct SceneUnit
{
	Uint16 gid = 0xFFFF;
	Uint32 generation = 0;
	int team = 0;
	Race *race = nullptr; //!< static per-team unit definitions
	Sint32 typeNum = 0, posX = 0, posY = 0, dx = 0, dy = 0, direction = 0, delta = 0;
	//! How far delta advances next tick (unitActionStepSpeed).
	Sint32 stepSpeed = 0;
	Sint32 action = 0, hp = 0, hungry = 0, carriedResource = 0, experienceLevel = 0;
	Sint32 levelUpAnimation = 0, magicActionAnimation = 0;
	bool validTarget = false;
	Sint32 targetX = 0, targetY = 0;
	Sint32 performance[NB_ABILITY] = {};
};

//! Presentation copy of a building, including flags (virtual buildings).
struct SceneBuilding
{
	Uint16 gid = 0xFFFF;
	Uint32 generation = 0;
	int team = 0;
	BuildingType *type = nullptr; //!< static building type definition
	Sint32 typeNum = 0, shortTypeNum = 0, posX = 0, posY = 0, hp = 0, effectiveMaxHp = 0;
	Sint32 maxUnitInside = 0, unitsInside = 0, maxUnitWorking = 0, unitsWorking = 0;
	Sint32 resources[MAX_RESOURCES] = {};
	Sint32 bullets = 0, unitStayRange = 0;
	Uint32 seenByMask = 0;
	Uint32 lastShootStep = 0;
	Sint32 lastShootSpeedX = 0, lastShootSpeedY = 0;
};

//! What the selected building contributes to the map view (off-screen worker
//! arrows and failing-unit badges).
struct SceneSelectedBuilding
{
	static constexpr int FailReasons = 8; //!< Building::UnitCantWorkReasonSize (checked)
	BuildingRef ref;
	bool recordFailingUnits = false;
	Sint32 desiredMaxUnitWorking = 0;
	std::array<Uint32, FailReasons> unitsFailingRequirements{};
	std::array<std::vector<Uint16>, FailReasons> unitsFailingByReason;
	std::vector<Uint16> unitsWorking; //!< gids, in the building's order
};

struct SceneBullet { Sint32 px = 0, py = 0, speedX = 0, speedY = 0, ticksLeft = 0, ticksInitial = 0; };
struct SceneExplosion { int x = 0, y = 0, ticksLeft = 0; };
struct SceneDeathAnimation { int x = 0, y = 0, ticksLeft = 0, team = 0; };

//! Bullets and animations of one map sector, in drawing order.
struct SceneSectorEffects
{
	std::vector<SceneBullet> bullets;
	std::vector<SceneExplosion> explosions;
	std::vector<SceneDeathAnimation> deaths;
};

//! All units, buildings and effects of a Scene, with lookup by gid.
struct SceneEntities
{
	static constexpr int Teams = 32;           //!< >= Team::MAX_COUNT (checked)
	static constexpr int SlotsPerTeam = 1024;  //!< >= Unit/Building::MAX_COUNT (checked)

	int teamCount = 0;
	std::array<SceneTeam, Teams> teams{};
	std::vector<SceneUnit> units;
	std::vector<SceneBuilding> buildings;
	//! Flag gids per team, in each team's order.
	std::array<std::vector<Uint16>, Teams> virtualBuildings;
	std::vector<SceneSectorEffects> sectors;
	SceneSelectedBuilding selectedBuilding;
	UnitRef selectedUnit;
	Uint32 highlightUnitType = 0, highlightBuildingType = 0;

	const SceneTeam &owner(const SceneUnit &u) const { return teams[u.team]; }
	const SceneTeam &owner(const SceneBuilding &b) const { return teams[b.team]; }
	const SceneUnit *unit(Uint16 gid) const
	{
		const int i = gid < unitIndex.size() ? unitIndex[gid] : -1;
		return i >= 0 ? &units[i] : nullptr;
	}
	const SceneBuilding *building(Uint16 gid) const
	{
		const int i = gid < buildingIndex.size() ? buildingIndex[gid] : -1;
		return i >= 0 ? &buildings[i] : nullptr;
	}
	bool isSelected(const SceneUnit &u) const { return u.gid == selectedUnit.gid && u.generation == selectedUnit.generation; }
	bool isSelected(const SceneBuilding &b) const
	{
		return b.gid == selectedBuilding.ref.gid && b.generation == selectedBuilding.ref.generation;
	}

	std::vector<int> unitIndex, buildingIndex; //!< gid -> index, -1 when absent
};
