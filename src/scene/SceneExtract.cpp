// SPDX-License-Identifier: GPL-3.0-or-later
#include "scene/SceneExtract.h"

#include "Building.h"
#include "Bullet.h"
#include "Game.h"
#include "Sector.h"
#include "Team.h"
#include "OverlayAreas.h"
#include "Unit.h"
#include "render/GameAnimations.h"

static_assert(Team::MAX_COUNT <= SceneEntities::Teams, "SceneEntities::Teams too small");
static_assert(Unit::MAX_COUNT <= SceneEntities::SlotsPerTeam, "SceneEntities::SlotsPerTeam too small");
static_assert(Building::MAX_COUNT <= SceneEntities::SlotsPerTeam, "SceneEntities::SlotsPerTeam too small");
static_assert(Building::UnitCantWorkReasonSize == SceneSelectedBuilding::FailReasons,
			  "SceneSelectedBuilding::FailReasons must match Building::UnitCantWorkReasonSize");

namespace
{
	SceneUnit unitOf(const Unit &u)
	{
		SceneUnit s;
		s.gid = u.gid;
		s.generation = u.scriptIdentity;
		s.team = u.owner->teamNumber;
		s.race = u.race;
		s.typeNum = u.typeNum;
		s.posX = u.posX;
		s.posY = u.posY;
		s.dx = u.dx;
		s.dy = u.dy;
		s.direction = u.direction;
		s.delta = u.delta;
		s.action = u.action;
		s.hp = u.hp;
		s.hungry = u.hungry;
		s.carriedResource = u.carriedResource;
		s.experienceLevel = u.experienceLevel;
		s.levelUpAnimation = u.levelUpAnimation;
		s.magicActionAnimation = u.magicActionAnimation;
		s.validTarget = u.validTarget;
		s.targetX = u.targetX;
		s.targetY = u.targetY;
		for (int a = 0; a < NB_ABILITY; ++a)
			s.performance[a] = u.performance[a];
		return s;
	}

	SceneBuilding buildingOf(const Building &b)
	{
		SceneBuilding s;
		s.gid = b.gid;
		s.generation = b.scriptIdentity;
		s.team = b.owner->teamNumber;
		s.type = b.type;
		s.typeNum = b.typeNum;
		s.shortTypeNum = b.shortTypeNum;
		s.posX = b.posX;
		s.posY = b.posY;
		s.hp = b.hp;
		s.effectiveMaxHp = b.getEffectiveMaxHp();
		s.maxUnitInside = b.maxUnitInside;
		s.unitsInside = Sint32(b.unitsInside.size());
		s.maxUnitWorking = b.maxUnitWorking;
		s.unitsWorking = Sint32(b.unitsWorking.size());
		for (int r = 0; r < MAX_RESOURCES; ++r)
			s.resources[r] = b.resources[r];
		s.bullets = b.bullets;
		s.unitStayRange = b.unitStayRange;
		s.seenByMask = b.seenByMask;
		s.lastShootStep = b.lastShootStep;
		s.lastShootSpeedX = b.lastShootSpeedX;
		s.lastShootSpeedY = b.lastShootSpeedY;
		return s;
	}

	void extractEntities(const Game &game, const SceneRequest &request, SceneEntities &e)
	{
		const int teamCount = game.mapHeader.getNumberOfTeams();
		e.teamCount = teamCount;
		e.units.clear();
		e.buildings.clear();
		e.unitIndex.assign(SceneEntities::Teams * SceneEntities::SlotsPerTeam, -1);
		e.buildingIndex.assign(SceneEntities::Teams * SceneEntities::SlotsPerTeam, -1);
		for (auto &flags : e.virtualBuildings)
			flags.clear();
		for (int t = 0; t < teamCount; ++t)
		{
			const Team *team = game.teams[t];
			e.teams[t] = SceneTeam{team->color, team->teamNumber, team->me, team->allies, team->sharedVisionOther,
				team->startPosX, team->startPosY};
			for (int i = 0; i < Unit::MAX_COUNT; ++i)
				if (const Unit *u = team->myUnits[i])
				{
					e.unitIndex[u->gid] = int(e.units.size());
					e.units.push_back(unitOf(*u));
				}
			for (int i = 0; i < Building::MAX_COUNT; ++i)
				if (const Building *b = team->myBuildings[i])
				{
					e.buildingIndex[b->gid] = int(e.buildings.size());
					e.buildings.push_back(buildingOf(*b));
				}
			for (const Building *b : team->virtualBuildings)
				e.virtualBuildings[t].push_back(b->gid);
		}

		// Bullets and animations, grouped by sector in drawing order.
		Map &map = const_cast<Map &>(game.map); // Map::getSector has no const overload; read only.
		const int sectorCount = map.getSectorW() * map.getSectorH();
		e.sectors.resize(sectorCount);
		for (int i = 0; i < sectorCount; ++i)
		{
			SceneSectorEffects &sector = e.sectors[i];
			sector.bullets.clear();
			sector.explosions.clear();
			sector.deaths.clear();
			for (const Bullet *b : map.getSector(i)->bullets)
				sector.bullets.push_back({b->px, b->py, b->speedX, b->speedY, b->ticksLeft, b->ticksInitial});
			if (game.animations)
			{
				for (const BulletExplosion *x : game.animations->getExplosions(i))
					sector.explosions.push_back({x->x, x->y, x->ticksLeft});
				for (const UnitDeathAnimation *d : game.animations->getDeathAnimations(i))
					sector.deaths.push_back({d->x, d->y, d->ticksLeft, d->team->teamNumber});
			}
		}

		// The selection's contribution to the map view.
		SceneSelectedBuilding &selected = e.selectedBuilding;
		selected = SceneSelectedBuilding();
		if (const Building *b = game.resolveBuilding(request.selectedBuilding))
		{
			selected.ref = request.selectedBuilding;
			selected.recordFailingUnits = b->recordFailingUnits;
			selected.desiredMaxUnitWorking = b->desiredMaxUnitWorking;
			for (int r = 0; r < SceneSelectedBuilding::FailReasons; ++r)
			{
				selected.unitsFailingRequirements[r] = b->unitsFailingRequirements[r];
				selected.unitsFailingByReason[r] = b->unitsFailingByReason[r];
			}
			for (const Unit *u : b->unitsWorking)
				selected.unitsWorking.push_back(u->gid);
		}
		e.selectedUnit = game.resolveUnit(request.selectedUnit) ? request.selectedUnit : UnitRef();
		e.highlightUnitType = game.highlightUnitType;
		e.highlightBuildingType = game.highlightBuildingType;
	}
}

void SceneExtractor::extract(const Game &game, const SceneRequest &request, Scene &scene)
{
	scene.tick = game.stepCounter;
	scene.map.extract(game.map);
	extractEntities(game, request, scene.entities);

	// Overlay maps refresh every 25 ticks (windows start at ticks 25k+1), and at once
	// when the client switches overlay or team.
	const Uint8 type = request.view.overlay;
	const Uint32 window = game.stepCounter ? (game.stepCounter - 1) / 25 : 0;
	if (type == OverlayArea::None)
		overlay.reset();
	else if (!overlay || type != overlayType || window != overlayWindow || request.localTeam != overlayTeam)
	{
		auto next = std::make_shared<OverlayArea>();
		// OverlayArea::compute only reads the game; it takes Game& for legacy accessors.
		next->compute(const_cast<Game &>(game), OverlayArea::OverlayType(type), request.localTeam);
		overlay = std::move(next);
	}
	overlayType = type;
	overlayWindow = window;
	overlayTeam = request.localTeam;
	scene.overlay = overlay;
}

void extractScene(const Game &game, const SceneRequest &request, Scene &scene)
{
	SceneExtractor().extract(game, request, scene);
}
