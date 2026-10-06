// SPDX-License-Identifier: GPL-3.0-or-later
#include "SceneExtract.h"

#include "Building.h"
#include "MapEdit.h"
#include "BuildingType.h"
#include "Bullet.h"
#include "Game.h"
#include "Sector.h"
#include "Team.h"
#include "OverlayAreas.h"
#include "Player.h"
#include "UnitTiming.h"
#include "TeamStat.h"
#include "WinProbability.h"
#include "Unit.h"
#include "AITelemetry.h"
#include "render/GameAnimations.h"
#include <unordered_map>

static_assert(Team::MAX_COUNT <= SceneEntities::Teams, "SceneEntities::Teams too small");
static_assert(Unit::MAX_COUNT <= SceneEntities::SlotsPerTeam, "SceneEntities::SlotsPerTeam too small");
static_assert(Building::MAX_COUNT <= SceneEntities::SlotsPerTeam, "SceneEntities::SlotsPerTeam too small");
static_assert(Building::UnitCantWorkReasonSize == SceneSelectedBuilding::FailReasons,
			  "SceneSelectedBuilding::FailReasons must match Building::UnitCantWorkReasonSize");

namespace
{
	//! Team::getFirstPlayerName, tolerating empty player slots: extraction runs for
	//! every team on every frame, including in games that leave slots unset.
	std::string firstPlayerName(const Game &game, const Team &team)
	{
		for (int i = 0; i < game.gameHeader.getNumberOfPlayers(); i++)
			if (game.players[i] && game.players[i]->team == &team)
				return game.players[i]->name;
		return {};
	}

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
		s.stepSpeed = unitActionStepSpeed(u.speed, u.action, u.dx, u.dy);
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
		s.lastUpgradeType = b.owner->game->buildingsTypes.getLastLevel(
			b.constructionResultState==Building::REPAIR ? b.getConstructionCompletionTypeNum() : b.typeNum);
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
				team->startPosX, team->startPosY, firstPlayerName(game, *team)};
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

        // Resolve segment connections once per extracted frame. Overlay buildings
        // need a sparse footprint index because they do not occupy the map grid.
        std::unordered_multimap<int,const SceneBuilding*> overlays;
        const auto tile=[&](int x,int y) { return game.map.normalizeY(y)*game.map.getW()+game.map.normalizeX(x); };
        for (const auto& b : e.buildings)
            if (!b.type->semantics.occupiesGround && b.type->presentation.connectionGroupId>=0)
                for (int dy=0; dy<b.type->height; ++dy)
                    for (int dx=0; dx<b.type->width; ++dx) overlays.emplace(tile(b.posX+dx,b.posY+dy),&b);
        for (auto& b : e.buildings)
            if (b.type->crossConnectMultiImage)
            {
                const auto connects=[&](const SceneBuilding* other) {
                    return other && other->gid!=b.gid &&
                        other->type->presentation.connectionGroupId==b.type->presentation.connectionGroupId &&
                        (b.type->presentation.connectsAcrossTeams || other->team==b.team);
                };
                const auto neighbor=[&](int x,int y) {
                    if (connects(e.building(game.map.getBuilding(x,y)))) return true;
                    const auto range=overlays.equal_range(tile(x,y));
                    for (auto it=range.first; it!=range.second; ++it) if (connects(it->second)) return true;
                    return false;
                };
                for (int dx=0; dx<b.type->width; ++dx)
                {
                    if (!(b.connectionMask&8) && neighbor(b.posX+dx,b.posY-1)) b.connectionMask|=8;
                    if (!(b.connectionMask&4) && neighbor(b.posX+dx,b.posY+b.type->height)) b.connectionMask|=4;
                }
                for (int dy=0; dy<b.type->height; ++dy)
                {
                    if (!(b.connectionMask&2) && neighbor(b.posX-1,b.posY+dy)) b.connectionMask|=2;
                    if (!(b.connectionMask&1) && neighbor(b.posX+b.type->width,b.posY+dy)) b.connectionMask|=1;
                }
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
			selected.verbose = b->verbose;
			if (b->verbose == 1 || b->verbose == 2)
			{
				int swim = b->verbose == 1 ? 0 : 1;
				while (b->verbose == 2 && swim < SWIM_CLASS_COUNT-1 && !b->globalGradient[swim]) ++swim;
				if (b->globalGradient[swim])
					selected.debugGradient.assign(b->globalGradient[swim], b->globalGradient[swim] + size_t(game.map.getW())*game.map.getH());
			}
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

namespace
{
	ScenePanelOwner ownerOf(const Game &game, const Team &team)
	{
		return ScenePanelOwner{team.teamNumber, team.me, team.allies, team.sharedVisionExchange, team.color,
			firstPlayerName(game, team)};
	}

	// The panels' legacy queries (repair cost, hard space, hunger, max build
	// level) only read the game but are not const-qualified; they are called here
	// through const_cast, on the simulation side, as drawing did before.
	void extractPanels(const Game &game, const SceneRequest &request, ScenePanels &panels)
	{
		Team &local = *game.teams[request.localTeam];
		panels.local = ScenePanelLocal{local.teamNumber, local.allies, local.maxBuildLevel(), local.color,
			local.prestige, local.unitConversionGained, local.unitConversionLost, local.noMoreBuildingSitesCountdown};

		SceneHud &hud = panels.hud;
		std::vector<int> allianceOf;
		const auto slots = WinProbability::slotsOf(game, allianceOf);
		const auto chances = WinProbability::permille(slots);
		hud.winChances.clear();
		for (int t = 0; t < game.teamsCount(); ++t)
			if (const Team *team = game.teams[t])
				hud.winChances.push_back({firstPlayerName(game, *team), team->color,
					chances[allianceOf[t]], slots[allianceOf[t]].alive});
		hud.totalPrestige = game.totalPrestige;
		hud.prestigeToReach = game.prestigeToReach;
		hud.anyPlayerWaited = game.anyPlayerWaited;
		hud.maskAwayPlayer = game.maskAwayPlayer;
		hud.players.clear();
		for (int p = 0; p < game.gameHeader.getNumberOfPlayers(); ++p)
			if (const Player *player = game.players[p])
				hud.players.push_back({player->name, player->teamNumber});
			else
				hud.players.push_back({});
		hud.legacyScriptTextShown = game.legacyScriptActive() && game.sgslScript.isTextShown;
		hud.legacyScriptText = hud.legacyScriptTextShown ? game.sgslScript.textShown : std::string();
		hud.legacyScriptTimer = game.legacyScriptTimer();

		// Copy into the scene's own TeamStats, reusing its storage when unshared.
		if (!panels.localStats || panels.localStats.use_count() > 1)
			panels.localStats = std::make_shared<TeamStats>(local.stats);
		else
			*panels.localStats = local.stats;
		panels.localStats->aiTelemetry.clear();

		panels.aiTelemetry.clear();
		for (int t = 0; t < game.mapHeader.getNumberOfTeams(); ++t)
		{
			// A unilateral diplomacy toggle must not reveal an opponent's plans.
			const bool allied = (local.allies & (1u << t)) &&
				(game.teams[t]->allies & local.me);
			if (!request.spectating && t != request.localTeam && !allied)
				continue;
			for (const auto &series : game.teams[t]->stats.aiTelemetry)
			{
				if (!series->active)
					continue;
				SceneAITelemetry row;
				row.team = t;
				row.player = series->player;
				row.name = series->playerName;
				row.available = series->current.available;
				if (row.available)
				{
					for (std::size_t i = 0;
						 i < series->fields.size() && i < series->current.values.size(); ++i)
					{
						const auto &field = series->fields[i];
						const auto &value = series->current.values[i];
						if (i >= AITelemetry::OrderTypes && i < AITelemetry::Specific &&
							(!value.valid || !value.bits))
							continue;
						row.values.push_back({field.name, AITelemetry::displayValue(field, value),
											  field.unit, field.meaning, value.updated});
					}
					row.values.insert(row.values.end(), series->named.begin(), series->named.end());
				}
				panels.aiTelemetry.push_back(std::move(row));
			}
		}

		SceneBuildingPanel &bp = panels.building;
		bp = SceneBuildingPanel();
		if (Building *b = game.resolveBuilding(request.selectedBuilding))
		{
			bp.valid = true;
			bp.gid = b->gid;
			bp.generation = b->scriptIdentity;
			bp.owner = ownerOf(game, *b->owner);
			bp.type = b->type;
			bp.typeNum = b->typeNum;
			bp.posX = b->posX;
			bp.posY = b->posY;
			bp.hp = b->hp;
			bp.effectiveMaxHp = b->getEffectiveMaxHp();
			bp.buildingState = b->buildingState;
			bp.constructionResultState = b->constructionResultState;
			bp.maxUnitWorking = b->maxUnitWorking;
			bp.desiredMaxUnitWorking = b->desiredMaxUnitWorking;
			bp.priority = b->priority;
			bp.unitStayRange = b->unitStayRange;
			bp.minLevelToFlag = b->minLevelToFlag;
			bp.minWorkerLevelToFlag=b->minWorkerLevelToFlag;
			bp.explorersRequireBombing=b->explorersRequireBombing;
			for (int r = 0; r < BASIC_COUNT; ++r)
				bp.clearingResources[r] = b->clearingResources[r];
			for (int r = 0; r < MAX_RESOURCES; ++r)
				bp.resources[r] = b->resources[r];
			bp.bullets = b->bullets;
			bp.productionTimeout = b->productionTimeout;
			const int recipe=b->productionUnit>=0 ? b->productionUnit : b->selectProductionRecipe();
			if (recipe>=0) bp.productionDuration=b->type->semantics.production.recipes[recipe].duration;
			for (int t = 0; t < NB_UNIT_TYPE; ++t)
				bp.ratio[t] = b->ratio[t];
			for (int r = 0; r < SceneSelectedBuilding::FailReasons; ++r)
				bp.unitsFailingRequirements[r] = b->unitsFailingRequirements[r];
			bp.unitsInside = Sint32(b->unitsInside.size());
			bp.unitsWorking = Sint32(b->unitsWorking.size());
			for (const Unit *u : b->unitsInside)
				bp.insideUnits.push_back({u->displacement == Unit::DIS_INSIDE, u->insideTimeout, u->delta});
			for (const Unit *u : b->unitsWorking)
				bp.workerPositions.push_back({u->posX, u->posY});
			// Ask only where the panels offer repair or upgrade, as drawing did: these
			// queries assume a real building and an existing next level.
			const bool constructible = b->constructionResultState == Building::NO_CONSTRUCTION &&
				b->buildingState == Building::ALIVE && !b->type->isBuildingSite;
			if (constructible && b->type->semantics.repairable && b->type->prevLevel>=0 &&
				(b->hp < bp.effectiveMaxHp || b->hp < b->type->hpMax))
			{
				bp.hardSpaceForRepair = b->isHardSpaceForBuildingSite(Building::REPAIR) && b->owner->maxBuildLevel()>=game.buildingsTypes.get(b->type->prevLevel)->semantics.requiredWorkerLevel;
				b->getResourceCountToRepair(bp.repairCost);
			}
			bp.showLevel = b->type->presentation.showLevel;
			if (constructible && b->isUpgradeAvailable())
				bp.hardSpaceForUpgrade = b->isHardSpaceForBuildingSite(Building::UPGRADE) && b->owner->maxBuildLevel()>=game.buildingsTypes.get(b->type->nextLevel)->semantics.requiredWorkerLevel;
			bp.buildingHpMultiplier = game.gameHeader.getBuildingHpMultiplier();
		}

		SceneUnitPanel &up = panels.unit;
		up = SceneUnitPanel();
		if (Unit *u = game.resolveUnit(request.selectedUnit))
		{
			up.valid = true;
			up.gid = u->gid;
			up.generation = u->scriptIdentity;
			up.owner = ownerOf(game, *u->owner);
			up.race = u->race;
			up.typeNum = u->typeNum;
			up.action = u->action;
			up.direction = u->direction;
			up.delta = u->delta;
			up.hp = u->hp;
			up.trigHP = u->trigHP;
			up.hungry = u->hungry;
			up.speed = u->speed;
			up.carriedResource = u->carriedResource;
			up.fruitCount = u->fruitCount;
			up.experience = u->experience;
			up.experienceLevel = u->experienceLevel;
			for (int a = 0; a < NB_ABILITY; ++a)
			{
				up.performance[a] = u->performance[a];
				up.level[a] = u->level[a];
			}
			up.unitHungry = u->isUnitHungry();
			up.realArmor = u->getRealArmor(false);
			up.nextLevelThreshold = u->getNextLevelThreshold();
			up.glassCannonScale = game.gameHeader.getGlassCannonScale();
		}
	}
}

void SceneExtractor::extract(const Game &game, const SceneRequest &request, Scene &scene)
{
	scene.buildingTypes = game.buildingsTypes.retainTypes();
	scene.editor = game.edit != nullptr;
	scene.tick = game.stepCounter;
	scene.tickTime = request.tickTime;
	scene.tickInterval = request.tickInterval;
	scene.map.extract(game.map, request.view.displayW, request.view.displayH, request.includeScriptAreas);
	extractEntities(game, request, scene.entities);
	if (request.includePanels) extractPanels(game, request, scene.panels);
	else scene.panels=ScenePanels{};

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
	scene.overlay = game.edit && !overlay ? std::make_shared<OverlayArea>(game.edit->overlay) : overlay;
}

void extractScene(const Game &game, const SceneRequest &request, Scene &scene)
{
	SceneExtractor().extract(game, request, scene);
}
