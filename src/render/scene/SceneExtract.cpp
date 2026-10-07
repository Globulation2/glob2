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
#include "sim/presentation/SceneInputs.h"
#include "sim/snapshot/SnapshotStore.h"
#include "Race.h"
#include "ai/model/BuildingProjection.h"

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

	template<class U> SceneUnit unitOf(const U &u, int team, Race* race)
	{
		SceneUnit s;
		s.gid = u.gid;
		s.generation = u.scriptIdentity;
		s.team = team;
		s.race = race;
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
		s.carriedMaterial = u.carriedMaterial;
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

	template<class B> SceneBuilding buildingOf(const B &b, int team, BuildingType* type,
        const BuildingType* lastType, int maxHp, int inside, int working, const Sint32* materials)
	{
		SceneBuilding s;
		s.gid = b.gid;
		s.generation = b.scriptIdentity;
		s.team = team;
		s.type = type;
		s.lastUpgradeType = const_cast<BuildingType*>(lastType);
		s.typeNum = b.typeNum;
		s.shortTypeNum = b.shortTypeNum;
		s.posX = b.posX;
		s.posY = b.posY;
		s.hp = b.hp;
		s.effectiveMaxHp = maxHp;
		s.maxUnitInside = b.maxUnitInside;
		s.unitsInside = inside;
		s.maxUnitWorking = b.maxUnitWorking;
		s.unitsWorking = working;
		for (int r = 0; r < MaterialCount; ++r)
			s.materials[r] = materials[r];
		s.bullets = b.bullets;
		s.unitStayRange = b.unitStayRange;
		s.seenByMask = b.seenByMask;
		s.lastShootStep = b.lastShootStep;
		s.lastShootSpeedX = b.lastShootSpeedX;
		s.lastShootSpeedY = b.lastShootSpeedY;
		return s;
	}

	void extractEntities(const Game &game, const SceneRequest &request, SceneEntities &e, bool records = true)
	{
		const int teamCount = game.mapHeader.getNumberOfTeams();
		e.teamCount = teamCount;
		e.materialPresence = 0;
		e.units.clear();
		e.buildings.clear();
		if (records)
        {
            e.unitIndex.assign(SceneEntities::Teams * SceneEntities::SlotsPerTeam, -1);
            e.buildingIndex.assign(SceneEntities::Teams * SceneEntities::SlotsPerTeam, -1);
        }
		for (auto &flags : e.virtualBuildings)
			flags.clear();
		for (int t = 0; t < teamCount; ++t)
		{
			const Team *team = game.teams[t];
			for (unsigned m=0; m<MaterialCount; ++m)
				if (team->teamMaterials[m] || team->reservedTeamMaterials[m]) e.materialPresence |= materialBit(static_cast<MaterialId>(m));
			e.teams[t] = SceneTeam{team->color, team->teamNumber, team->me, team->allies, team->sharedVisionOther,
				team->startPosX, team->startPosY, firstPlayerName(game, *team)};
			for (int i = 0; records && i < Unit::MAX_COUNT; ++i)
				if (const Unit *u = team->myUnits[i])
				{
					e.unitIndex[u->gid] = int(e.units.size());
					e.units.push_back(unitOf(*u, t, u->race));
					if (u->carriedMaterial >= 0 && validMaterial(u->carriedMaterial)) e.materialPresence |= 1u << u->carriedMaterial;
				}
			for (int i = 0; records && i < Building::MAX_COUNT; ++i)
				if (const Building *b = team->myBuildings[i])
				{
					e.buildingIndex[b->gid] = int(e.buildings.size());
					e.buildings.push_back(buildingOf(*b, t, b->type, game.buildingsTypes.get(game.buildingsTypes.get(
                        b->constructionResultState==Building::REPAIR ? b->getConstructionCompletionTypeNum() : b->typeNum)->terminalTypeNum),
                        b->getEffectiveMaxHp(), int(b->unitsInside.size()), int(b->unitsWorking.size()), b->materials));
					for (unsigned m=0; m<MaterialCount; ++m) if (b->materials[m]) e.materialPresence |= 1u << m;
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

namespace {
void prepareConnections(const SceneMap& map, SceneEntities& e)
{
        // Resolve segment connections once per extracted frame. Overlay buildings
        // need a sparse footprint index because they do not occupy the map grid.
        std::unordered_multimap<int,const SceneBuilding*> overlays;
        const auto tile=[&](int x,int y) { return int(map.coordToIndex(x,y)); };
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
                    if (connects(e.building(map.getBuilding(x,y)))) return true;
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
	void extractPanels(const Game &game, const SceneRequest &request, ScenePanels &panels, SceneInputs* raw = nullptr)
	{
		Team &local = *game.teams[request.localTeam];
		panels.local = ScenePanelLocal{local.teamNumber, local.allies, raw ? 0 : local.maxBuildLevel(), local.color,
			local.prestige, local.unitConversionGained, local.unitConversionLost, local.noMoreBuildingSitesCountdown};

		SceneHud &hud = panels.hud;
		if (!raw)
		{
		std::vector<int> allianceOf;
		const auto slots = WinProbability::slotsOf(game, allianceOf);
		const auto chances = WinProbability::permille(slots);
		hud.winChances.clear();
		for (int t = 0; t < game.teamsCount(); ++t)
			if (const Team *team = game.teams[t])
				hud.winChances.push_back({firstPlayerName(game, *team), team->color,
					chances[allianceOf[t]], slots[allianceOf[t]].alive});
		}

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
                if (raw)
                {
                    raw->telemetry.push_back({std::move(row), series->fields, series->current.values, series->named});
                    continue;
                }
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
				bp.clearingMaterials[r] = b->clearingMaterials[r];
			for (int r = 0; r < MaterialCount; ++r)
				bp.materials[r] = b->materials[r];
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
			if (!raw && constructible && b->type->semantics.repairable && b->type->prevLevel>=0 &&
				(b->hp < bp.effectiveMaxHp || b->hp < b->type->hpMax))
			{
				bp.hardSpaceForRepair = b->isHardSpaceForBuildingSite(Building::REPAIR) && b->owner->maxBuildLevel()>=game.buildingsTypes.get(b->type->prevLevel)->semantics.requiredWorkerLevel;
				b->getMaterialCountToRepair(bp.repairCost);
			}
			bp.showLevel = b->type->presentation.showLevel;
			if (!raw && constructible && b->isUpgradeAvailable())
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
			up.carriedMaterial = u->carriedMaterial;
			up.fruitCount = u->fruitCount;
			up.experience = u->experience;
			up.experienceLevel = u->experienceLevel;
			for (int a = 0; a < NB_ABILITY; ++a)
			{
				up.performance[a] = u->performance[a];
				up.level[a] = u->level[a];
			}
			if (!raw)
            {
                up.unitHungry = u->isUnitHungry();
                up.realArmor = u->getRealArmor(false);
                up.nextLevelThreshold = u->getNextLevelThreshold();
            }
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
	prepareConnections(scene.map, scene.entities);
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

std::shared_ptr<SceneInputs> SceneExtractor::capture(const Game& game, const SceneRequest& request)
{
    using namespace SimulationSnapshot;
    auto input = std::make_shared<SceneInputs>();
    input->request = request;
    // Presentation can observe paused edits between logical ticks. Declare that
    // boundary explicitly rather than mixing freshly captured metadata with an
    // older same-tick entity snapshot.
    auto& store = game.snapshots();
    store.invalidateBoundary();
    constexpr auto requirements = bit(Component::Catalogs) | bit(Component::Terrain) | bit(Component::Resources)
        | bit(Component::Occupancy) | bit(Component::Visibility) | bit(Component::Entities)
        | bit(Component::Teams) | bit(Component::Rules);
    input->world = store.captureBoundary(game, requirements);
    auto& scene = input->source;
    scene.buildingTypes = game.buildingsTypes.retainTypes();
    scene.race = std::make_shared<Race>();
    scene.editor = game.edit != nullptr;
    scene.tick = input->world.tick; scene.tickTime = request.tickTime; scene.tickInterval = request.tickInterval;
    scene.map.captureDisplay(game.map, request.view.displayW, request.view.displayH, request.includeScriptAreas);
    extractEntities(game, request, scene.entities, false);
    if (request.includePanels) extractPanels(game, request, scene.panels, input.get());
    for (int t=0; t<game.teamsCount(); ++t) input->lost[t] = game.teams[t] && game.teams[t]->hasLost;
    scene.panels.unit.race = scene.race.get();
    if (game.edit) scene.overlay = std::make_shared<OverlayArea>(game.edit->overlay);
    input->fertilityMaximum = game.map.fertilityMaximum;
    return input;
}

namespace {
void preparePanels(const SceneInputs& input, Scene& scene)
{
    if (!input.request.includePanels) return;
    const auto& world = input.world;
    auto& panels = scene.panels;
    std::array<int, SceneEntities::Teams> buildLevels{};
    for (const auto& u : world.entities->units)
        if (u.performance[BUILD]) buildLevels[u.team] = std::max(buildLevels[u.team], int(u.constructionLevel));
    panels.local.maxBuildLevel = buildLevels[input.request.localTeam];
    const auto& configuration = *world.rules->configuration;
    std::vector<int> alliances, allianceOf;
    for (const auto& team : world.teams->values)
    {
        const int alliance = configuration.getAllyTeamNumber(team.number);
        const auto it = std::find(alliances.begin(), alliances.end(), alliance);
        allianceOf.push_back(int(it - alliances.begin()));
        if (it == alliances.end()) alliances.push_back(alliance);
    }
    std::vector<int> providers;
    for (size_t i=0; i<scene.buildingTypes->size(); ++i)
    {
        const auto& t = scene.buildingTypes->at(i);
        const auto& completed = t.isBuildingSite && t.nextLevel>=0 ? scene.buildingTypes->at(t.nextLevel) : t;
        if (ModelBuildingProjection::trainsWarriorCombat(completed)) providers.push_back(int(i));
    }
    std::vector<WinProbability::Slot> slots(alliances.size());
    for (const auto& t : world.teams->values)
    {
        if (!t.alive || input.lost[t.number]) continue;
        auto& slot = slots[allianceOf[t.number]];
        const auto& stat = t.statistics;
        slot.alive = true; slot.units += stat.totalUnit; slot.prestige += t.prestige;
        for (int id : providers) if (size_t(id) < stat.buildingCountByVariant.size()) slot.barracks += stat.buildingCountByVariant[id];
        slot.explorers += stat.numberUnitPerType[EXPLORER]; slot.foodCritical += stat.needFoodCritical; slot.attack += stat.totalAttackPower;
    }
    const auto chances = WinProbability::permille(slots);
    panels.hud.winChances.clear();
    for (int t=0; t<scene.entities.teamCount; ++t)
    {
        const auto& team = scene.entities.teams[t];
        panels.hud.winChances.push_back({team.firstPlayerName,team.color,chances[allianceOf[t]],slots[allianceOf[t]].alive});
    }
    panels.aiTelemetry.clear();
    for (const auto& source : input.telemetry)
    {
        auto row = source.row;
        if (row.available)
        {
            for (size_t i=0; i<source.fields.size() && i<source.values.size(); ++i)
            {
                const auto& field = source.fields[i]; const auto& value = source.values[i];
                if (i >= AITelemetry::OrderTypes && i < AITelemetry::Specific && (!value.valid || !value.bits)) continue;
                row.values.push_back({field.name, AITelemetry::displayValue(field,value),field.unit,field.meaning,value.updated});
            }
            row.values.insert(row.values.end(), source.named.begin(), source.named.end());
        }
        panels.aiTelemetry.push_back(std::move(row));
    }
    auto& bp = panels.building;
    if (bp.valid)
    {
        const auto& type = *bp.type;
        const bool constructible = bp.constructionResultState == BuildingStateRecord::NO_CONSTRUCTION
            && bp.buildingState == BuildingStateRecord::ALIVE && !type.isBuildingSite;
        const auto hardSpace = [&](int next, bool upgrade) {
            if (upgrade && world.rules->values.upgradesDisabled) return false;
            if (next < 0) return true;
            const auto& target = scene.buildingTypes->at(next);
            if (target.isVirtual) return true;
            const int x = bp.posX + target.decLeft - type.decLeft, y = bp.posY + target.decTop - type.decTop;
            for (int dy=0; dy<target.height; ++dy) for (int dx=0; dx<target.width; ++dx)
            {
                const auto& map = scene.map;
                const auto& r = map.getResource(x+dx,y+dy);
                if (r.type != NO_RES_TYPE && map.resourceRegistry().properties(static_cast<ResourceId>(r.type)).blocksBuilding) return false;
                const auto occupant = map.getBuilding(x+dx,y+dy);
                if (occupant != 0xffff && occupant != bp.gid) return false;
                if (!map.terrainRegistry().properties(map.terrainTypeAt(x+dx,y+dy)).buildable) return false;
            }
            return true;
        };
        if (constructible && type.semantics.repairable && type.prevLevel>=0 && (bp.hp<bp.effectiveMaxHp || bp.hp<type.hpMax))
        {
            bp.hardSpaceForRepair = hardSpace(type.prevLevel,false) && buildLevels[bp.owner.teamNumber]>=scene.buildingTypes->at(type.prevLevel).semantics.requiredWorkerLevel;
            const Sint64 ratio = (Sint64(bp.hp)<<16)/bp.effectiveMaxHp;
            Sint32 error=0;
            for (unsigned m=0; m<MaterialCount; ++m)
            {
                const Sint64 value = ratio*type.semantics.repairCost[m];
                int whole = value>>16; error += value & 0xffff;
                if (error>=65536) {error-=65536;++whole;}
                bp.repairCost[m] = type.semantics.repairCost[m]-whole;
            }
        }
        if (constructible && type.nextLevel>=0 && world.catalogs->buildings->at(type.nextLevel).available)
            bp.hardSpaceForUpgrade = hardSpace(type.nextLevel,true) && buildLevels[bp.owner.teamNumber]>=scene.buildingTypes->at(type.nextLevel).semantics.requiredWorkerLevel;
    }
    auto& up = panels.unit;
    if (up.valid)
    {
        const auto index = world.entities->unitSlotIndices.at(up.gid);
        const auto& u = world.entities->units.at(index);
        up.unitHungry = !world.rules->values.hungerDisabled && u.hungry <= (u.carriedMaterial==-1 ? u.trigHungry : u.trigHungryCarrying);
        const auto& types = world.catalogs->unitTypes[u.typeNum];
        up.realArmor = u.performance[ARMOR]/up.glassCannonScale - u.fruitCount*types[u.level[ARMOR]].armorReductionPerHappyness;
        up.nextLevelThreshold = (u.experienceLevel+1)*(u.experienceLevel+1)*types[u.level[ATTACK_STRENGTH]].experiencePerLevel;
    }
}
}

size_t SceneExtractor::preparationChunks(const SceneInputs& input)
{
    return 3 + (input.world.entities->units.size()+255)/256 + (input.world.entities->buildings.size()+255)/256;
}

void SceneExtractor::prepare(const SceneInputs& input, Scene& scene)
{
    for (size_t chunk=0; chunk<preparationChunks(input); ++chunk) prepareChunk(input,scene,chunk);
}

void SceneExtractor::prepareChunk(const SceneInputs& input, Scene& scene, size_t chunk)
{
    if (chunk == 0)
    {
        scene = input.source;
        scene.map.bindSnapshot(input.world);
        scene.entities.unitIndex.assign(SceneEntities::Teams * SceneEntities::SlotsPerTeam, -1);
        scene.entities.buildingIndex.assign(SceneEntities::Teams * SceneEntities::SlotsPerTeam, -1);
        return;
    }
    --chunk;
    const auto& units = input.world.entities->units;
    const auto& buildings = input.world.entities->buildings;
    const size_t unitChunks = (units.size()+255)/256, buildingChunks = (buildings.size()+255)/256;
    auto& e = scene.entities;
    const auto type = [&](int index) { return const_cast<BuildingType*>(&scene.buildingTypes->at(index)); };
    if (chunk < unitChunks)
    {
      for (size_t i=chunk*256; i<std::min(units.size(),(chunk+1)*256); ++i)
      {
        const auto& u = units[i];
        e.unitIndex[u.gid] = int(e.units.size());
        e.units.push_back(unitOf(u, u.team, scene.race.get()));
        if (u.carriedMaterial >= 0 && validMaterial(u.carriedMaterial)) e.materialPresence |= 1u << u.carriedMaterial;
    }
      return;
    }
    chunk -= unitChunks;
    if (chunk < buildingChunks)
    {
      for (size_t i=chunk*256; i<std::min(buildings.size(),(chunk+1)*256); ++i)
      {
        const auto& b = buildings[i];
        auto* definition = type(b.typeNum);
        int completed = b.typeNum;
        if (b.constructionResultState == BuildingStateRecord::REPAIR)
            completed = b.constructionOriginTypeNum >= 0 ? b.constructionOriginTypeNum
                : definition->isBuildingSite ? definition->nextLevel : b.typeNum;
        const auto* materials = b.usesTeamResources ? input.world.teams->values.at(b.team).materials.data() : b.localMaterials;
        e.buildingIndex[b.gid] = int(e.buildings.size());
        e.buildings.push_back(buildingOf(b, b.team, definition, type(type(completed)->terminalTypeNum),
            b.maxHp, int(b.inside.count), int(b.working.count), materials));
        for (unsigned m = 0; m < MaterialCount; ++m) if (materials[m]) e.materialPresence |= 1u << m;
    }
      return;
    }
    chunk -= buildingChunks;
    if (chunk == 0)
    {
        prepareConnections(scene.map, e);
        preparePanels(input, scene);
        return;
    }
    if (chunk != 1) throw std::out_of_range("Scene preparation chunk");
    const auto& request = input.request;
    const Uint8 requested = request.view.overlay;
    const Uint32 window = scene.tick ? (scene.tick - 1) / 25 : 0;
    if (requested == OverlayArea::None) overlay.reset();
    else if (!overlay || requested != overlayType || window != overlayWindow || request.localTeam != overlayTeam)
    {
        auto next = std::make_shared<OverlayArea>();
        next->compute(input.world, OverlayArea::OverlayType(requested), request.localTeam, input.fertilityMaximum);
        overlay = std::move(next);
    }
    overlayType = requested; overlayWindow = window; overlayTeam = request.localTeam;
    if (overlay || !scene.editor) scene.overlay = overlay;
}
