// SPDX-License-Identifier: GPL-3.0-or-later
#include "WorldSnapshot.h"
#include "SnapshotStorage.h"
#include "Building.h"
#include "Game.h"
#include "GameRuleOverrides.h"
#include "BuildingCapabilities.h"
#include "GlobalContainer.h"
#include "Unit.h"
#include "Race.h"
#include <algorithm>
#include <iterator>
#include <stdexcept>
#include <chrono>

namespace SimulationSnapshot
{
std::shared_ptr<const std::vector<BuildingKindView>> captureCatalog(const Game& game)
{
	auto result = std::make_shared<std::vector<BuildingKindView>>();
	result->reserve(game.buildingsTypes.size());
	for (std::size_t i = 0; i < game.buildingsTypes.size(); ++i)
	{
		const auto& type = *game.buildingsTypes.get(i);
		BuildingKindView kind;
		kind.resolvedType = type;
		kind.resolvedType.gameSpritePtr = nullptr; kind.resolvedType.miniSpritePtr = nullptr;
		std::copy_n(type.multiplierResource, MAX_NB_RESOURCES, kind.multiplierResource.begin());
		kind.key = type.key;
		kind.legacyType = type.type;
		kind.semantics = type.semantics;
		kind.width = type.width; kind.height = type.height; kind.level = type.level;
		kind.previous = type.prevLevel; kind.next = type.nextLevel;
		kind.maximumWorkers = type.maxUnitWorking; kind.maximumInside = type.maxUnitInside;
		kind.maximumRange = type.maxUnitStayRange; kind.shootingRange = type.shootingRange;
		kind.site = type.isBuildingSite;
		kind.available = game.isBuildingTypeAvailable(i);
		kind.isVirtual = type.isVirtual; kind.isCloaked = type.isCloaked;
		kind.shortTypeNum = type.shortTypeNum; kind.hpMax = type.hpMax;
		kind.shootSpeed = type.shootSpeed; kind.shootRhythm = type.shootRhythm;
		kind.decLeft = type.decLeft; kind.decTop = type.decTop;
		kind.suppliesStock = type.runtimeSuppliesStock; kind.fetchesStock = type.runtimeFetchesStock;
		std::copy_n(type.maxResource, MAX_NB_RESOURCES, kind.maxResource.begin());
		std::copy_n(type.zonable, NB_UNIT_TYPE, kind.zonable.begin());
		const int completed = type.isBuildingSite ? type.nextLevel : int(i);
		if (completed >= 0)
			for (unsigned intent = 0; intent < unsigned(AIPlanning::BuildingIntent::Count); ++intent)
				if (game.buildingCapabilities().available(completed, AIPlanning::BuildingIntent(intent), game.gameHeader))
					kind.capabilityMask |= Uint64(1) << intent;
		kind.lineagePosition = game.buildingCapabilities().lineagePosition(i);
		if (completed >= 0)
			for (unsigned intent = 0; intent < unsigned(AIPlanning::BuildingIntent::Count); ++intent)
				if (game.buildingCapabilities().matches(completed, AIPlanning::BuildingIntent(intent)))
					kind.rawCapabilityMask |= Uint64(1) << intent;
		result->push_back(std::move(kind));
	}
	return result;
}

Handle capture(const Game& game,
	std::shared_ptr<const std::vector<BuildingKindView>> catalog, Requirements requirements, const Handle* previous, Storage* storage)
{
	if (needs(requirements, Component::Catalogs) && (!catalog || catalog->size() != game.buildingsTypes.size()))
		throw std::invalid_argument("AI observation needs the current building catalog");
	Handle value;
	auto* result = &value;
	result->requirements = requirements;
	result->configurationRevision = game.gameHeader.observationRevision();
	result->worldIdentity = game.map.identity();
	result->mapGenerations = game.map.snapshotGenerations();
	if (previous && previous->worldIdentity == result->worldIdentity)
	{
		auto reuse = [&](Component component, auto& destination, const auto& source, bool unchanged) {
			if (needs(requirements, component) && source && unchanged)
			{ destination = source; requirements &= ~bit(component); }
		};
		const bool sameCatalog = previous->catalogs && previous->catalogs->buildings == catalog && previous->configurationRevision == result->configurationRevision;
		reuse(Component::Catalogs, result->catalogs, previous->catalogs, sameCatalog);
		reuse(Component::Rules, result->rules, previous->rules, previous->configurationRevision == result->configurationRevision);
		const auto generations = result->mapGenerations;
		const auto old = previous->mapGenerations;
		const bool sameTerrain = previous->terrain && previous->terrain->revision == game.map.terrainGeneration() && generations[0] == old[0];
		reuse(Component::Terrain, result->terrain, previous->terrain, sameTerrain);
		reuse(Component::Resources, result->resources, previous->resources, sameTerrain && generations[1] == old[1]);
		reuse(Component::Occupancy, result->occupancy, previous->occupancy, generations[2] == old[2]);
		reuse(Component::Areas, result->areas, previous->areas, generations[3] == old[3] && previous->configurationRevision == result->configurationRevision);
		reuse(Component::Visibility, result->visibility, previous->visibility, generations[4] == old[4]);
		reuse(Component::Growth, result->growth, previous->growth, sameTerrain);
	}

	auto append = [&]<class T>(std::vector<T>& values, T value) {
		if (storage && values.size() == values.capacity()) ++storage->allocations;
		values.push_back(std::move(value));
	};
	auto reserve = [&]<class T>(std::vector<T>& values, std::size_t count) {
		if (storage && values.capacity() < count) ++storage->allocations;
		values.reserve(count);
	};
	auto acquire = [&]<class T>(BufferPool<T> Storage::* pool, Component component) {
		if (!needs(requirements, component)) return std::shared_ptr<T>{};
		return storage ? (storage->*pool).acquire(storage->allocations) : std::make_shared<T>();
	};
	Catalogs unusedCatalogs; auto catalogsOwner = acquire(&Storage::catalogs, Component::Catalogs);
	auto* catalogs = catalogsOwner ? catalogsOwner.get() : &unusedCatalogs;
	Terrain unusedTerrain; auto terrainOwner = acquire(&Storage::terrain, Component::Terrain);
	auto* terrain = terrainOwner ? terrainOwner.get() : &unusedTerrain;
	Resources unusedResources; auto resourcesOwner = acquire(&Storage::resources, Component::Resources);
	auto* resources = resourcesOwner ? resourcesOwner.get() : &unusedResources;
	Occupancy unusedOccupancy; auto occupancyOwner = acquire(&Storage::occupancy, Component::Occupancy);
	auto* occupancy = occupancyOwner ? occupancyOwner.get() : &unusedOccupancy;
	Areas unusedAreas; auto areasOwner = acquire(&Storage::areas, Component::Areas);
	auto* areas = areasOwner ? areasOwner.get() : &unusedAreas;
	Visibility unusedVisibility; auto visibilityOwner = acquire(&Storage::visibility, Component::Visibility);
	auto* visibility = visibilityOwner ? visibilityOwner.get() : &unusedVisibility;
	Entities unusedEntities; auto entitiesOwner = acquire(&Storage::entities, Component::Entities);
	auto* entities = entitiesOwner ? entitiesOwner.get() : &unusedEntities;
	Teams unusedTeams; auto teamsOwner = acquire(&Storage::teams, Component::Teams);
	auto* teams = teamsOwner ? teamsOwner.get() : &unusedTeams;
	Rules unusedRules; auto rulesOwner = acquire(&Storage::rules, Component::Rules);
	auto* rules = rulesOwner ? rulesOwner.get() : &unusedRules;
	// Map buffers keep their constructed range when reused. Overwrite selected
	// arrays directly rather than clearing and growing them one cell at a time.
	entities->buildings.clear(); entities->units.clear(); entities->relationships.clear(); entities->projects.clear();
	if (needs(requirements, Component::Teams)) { reserve(teams->values, game.mapHeader.getNumberOfTeams()); teams->values.resize(game.mapHeader.getNumberOfTeams()); }
	result->tick = game.stepCounter;
	result->width = game.map.getW(); result->height = game.map.getH();
	if (needs(requirements, Component::Terrain))
	{ terrain->registry = game.map.frozenTerrainRegistry();
	terrain->identity = game.map.frozenTerrainSnapshot(); }
	terrain->revision = game.map.terrainGeneration();
	terrain->movementModifiers = game.map.hasTerrainMovementModifiers();
	terrain->airConstraints = game.map.hasAirTerrainConstraints();
	const auto& header = game.gameHeader;
	if (needs(requirements, Component::Catalogs)) {
		catalogs->buildings = std::move(catalog);
		for (int type = 0; type < NB_UNIT_TYPE; ++type)
			std::copy_n(Race::unitTypes[type], NB_UNIT_LEVELS, catalogs->unitTypes[type].begin());
		catalogs->sizesCount.fill(0); catalogs->eternal.fill(false);
		catalogs->shrinkable.fill(false); catalogs->visibleToBeCollected.fill(false);
		for (unsigned r = 0; r < std::min<unsigned>(MAX_NB_RESOURCES, globalContainer->resourcesTypes.size()); ++r) {
			catalogs->sizesCount[r] = globalContainer->resourcesTypes.get(r)->sizesCount;
			catalogs->eternal[r] = globalContainer->resourcesTypes.get(r)->eternal;
			catalogs->shrinkable[r] = globalContainer->resourcesTypes.get(r)->shrinkable;
			catalogs->visibleToBeCollected[r] = globalContainer->resourcesTypes.get(r)->visibleToBeCollected;
		}
	}
	if (needs(requirements, Component::Rules)) {
		auto config = std::make_shared<GameHeader>(header);
		config->getWinningConditions().clear();
		rules->configuration = std::move(config);
		rules->named = gameRuleValues(header);
		rules->experiments.clear();
		for (const auto& key : header.getExperiments().keys()) rules->experiments.push_back(key);
		rules->values.hungerDisabled = header.isHungerDisabled();
		rules->values.upgradesDisabled = header.isUnitUpgradesDisabled();
		rules->values.peaceful = header.isPeacefulModeEnabled();
		rules->values.instantConstruction = header.isInstantConstructionEnabled();
		rules->values.resourceGrowthDisabled = header.isResourceGrowthDisabled();
	}
	if (needs(requirements, Component::Areas)) areas->farmEnabled = game.map.farmAreasEnabled();
	const auto& sourceTiles = game.map.getTiles();
	const auto immobile = game.map.immobileState();
	const bool copyTerrain=needs(requirements,Component::Terrain), copyResources=needs(requirements,Component::Resources);
	const bool copyOccupancy=needs(requirements,Component::Occupancy), copyAreas=needs(requirements,Component::Areas);
	const bool copyVisibility=needs(requirements,Component::Visibility);
	const auto sizeMapArray=[&](auto& array) { reserve(array,sourceTiles.size()); array.resize(sourceTiles.size()); };
	if (copyTerrain) sizeMapArray(terrain->legacy);
	if (copyResources) sizeMapArray(resources->cells);
	if (copyOccupancy) sizeMapArray(occupancy->cells);
	if (copyAreas) sizeMapArray(areas->cells);
	if (copyVisibility) sizeMapArray(visibility->cells);
	const auto terrainData=terrain->legacy.data(); const auto resourceData=resources->cells.data();
	const auto occupancyData=occupancy->cells.data(); const auto areaData=areas->cells.data();
	const auto visibilityData=visibility->cells.data();
	for (std::size_t i = 0; (copyTerrain || copyResources || copyOccupancy || copyAreas || copyVisibility) && i < sourceTiles.size(); ++i)
	{
		const auto& tile = sourceTiles[i];
		if (copyTerrain) terrainData[i]=tile.terrain;
		if (copyResources) resourceData[i]={tile.resource,tile.fertility,tile.canResourcesGrow != 0};
		if (copyOccupancy) occupancyData[i]={tile.building,tile.groundUnit,tile.airUnit,immobile[i]};
		if (copyAreas) areaData[i]={tile.forbidden,tile.guardArea,tile.clearArea,tile.farmArea};
		if (copyVisibility) visibilityData[i]={game.map.mapDiscovered[i],game.map.fogOfWar ? game.map.fogOfWar[i] : 0};
	}
	for (int t = 0; (needs(requirements, Component::Teams) || needs(requirements, Component::Entities)) && t < game.mapHeader.getNumberOfTeams(); ++t)
	{
		const auto* team = game.teams[t];
		if (!team) { if (needs(requirements, Component::Teams)) teams->values[t] = {}; continue; }
		if (needs(requirements, Component::Teams)) {
		auto& target = teams->values[t]; target.number = t;
		target.virtualBuildings.clear(); target.swarms.clear();
		target.prestige = team->prestige; target.alive = team->isAlive;
		target.startX = team->startPosX; target.startY = team->startPosY;
		target.mask = team->me; target.allies = team->allies;
		target.enemies = team->attackableTeams();
		target.foodVision = team->sharedVisionFood;
		target.exchangeVision = team->sharedVisionExchange;
		target.otherVision = team->sharedVisionOther;
		if (storage && target.statistics.buildingCountByVariant.capacity() < team->stats.getLatestStat()->buildingCountByVariant.size()) ++storage->allocations;
		target.statistics = *team->stats.getLatestStat();
		// Match the existing smoothed-stat queries, rather than deriving a new
		// balance from the current sample and changing policy inputs.
		target.workerBalance = target.statistics.isFree[WORKER] - target.statistics.totalNeeded;
		target.starving = target.statistics.needFoodCritical;
		std::copy_n(target.statistics.workersByConstructionLevel, NB_UNIT_LEVELS, target.workersLevel.begin());
		for (auto* building : team->swarms) append(target.swarms, Game::refOf(building));
		for (auto* building : team->virtualBuildings) append(target.virtualBuildings, Game::refOf(building));
		std::copy_n(team->teamResources, MAX_NB_RESOURCES, target.resources.begin());
		}
		if (!needs(requirements, Component::Entities)) continue;
		for (int i = 0; i < Building::MAX_COUNT; ++i)
			if (auto* b = team->myBuildings[i])
			{
				BuildingView v;
				v.shortType = b->shortTypeNum; v.requireBombing = b->explorersRequireBombing;
				v.productionTimeout = b->productionTimeout; v.receiveMask = b->receiveResourceMask;
				v.sendMask = b->sendResourceMask; v.bullets = b->bullets;
				std::copy_n(b->clearingResources, BASIC_COUNT, v.clearingResources.begin());
				v.identity = Game::refOf(b); v.team = t; v.type = b->typeNum;
				v.x = b->posX; v.y = b->posY;
				v.state = b->buildingState; v.construction = b->constructionResultState;
				v.originType = b->getConstructionOriginTypeNum();
				// Feasibility is prepared below from captured values, not live queries.
				std::copy_n(b->locked, BUILDING_ACCESS_COUNT, v.locked.begin());
				v.hp = b->hp; v.maxHp = b->getEffectiveMaxHp();
				v.workers = b->maxUnitWorking; v.futureWorkers = b->getMaxUnitWorkingFuture();
				v.desiredWorkers = b->desiredMaxUnitWorking; v.maxInside = b->maxUnitInside;
				v.priority = b->priority; v.range = b->unitStayRange;
				v.minimumLevel = b->minLevelToFlag; v.minimumWorkerLevel = b->minWorkerLevelToFlag;
				v.seenBy = b->seenByMask; v.underAttack = b->underAttackTimer;
				std::copy_n(b->resources, MAX_NB_RESOURCES, v.resources.begin());
				std::copy_n(b->wishedResources, MAX_NB_RESOURCES, v.wishedResources.begin());
				std::copy_n(b->ratio, NB_UNIT_TYPE, v.ratios.begin());
				v.working = {Uint32(entities->relationships.size()), Uint32(b->unitsWorking.size())};
				for (const auto* u : b->unitsWorking) append(entities->relationships, Game::refOf(u));
				v.inside = {Uint32(entities->relationships.size()), Uint32(b->unitsInside.size())};
				for (const auto* u : b->unitsInside) append(entities->relationships, Game::refOf(u));
				append(entities->buildings, std::move(v));
			}
		for (int i = 0; i < Unit::MAX_COUNT; ++i)
			if (const auto* u = team->myUnits[i])
			{
				UnitView v;
				v.dx = u->dx; v.dy = u->dy; v.insideTimeout = u->insideTimeout;
				v.experience = u->experience; v.experienceLevel = u->experienceLevel; v.fruitCount = u->fruitCount;
				v.movement = u->movement; v.action = u->action; v.carriedResource = u->carriedResource;
				v.speed = u->speed; v.direction = u->direction; v.fruitMask = u->fruitMask;
				v.destinationPurpose = u->destinationPurpose; v.targetX = u->targetX; v.targetY = u->targetY;
				v.target = Game::refOf(u->targetBuilding);
				v.identity = Game::refOf(u); v.team = t; v.type = u->typeNum;
				v.x = u->posX; v.y = u->posY; v.hp = u->hp;
				v.medical = u->medical; v.activity = u->activity; v.displacement = u->displacement;
				v.hungriness = u->hungriness; v.hungry = u->hungry; v.hungryTrigger = u->trigHungry;
				v.constructionLevel = u->workerLevel(); v.dead = u->isDead;
				v.underAttack = u->underAttackTimer; v.attached = Game::refOf(u->attachedBuilding);
				std::copy_n(u->canLearn, NB_ABILITY, v.canLearn.begin());
				std::copy_n(u->performance, NB_ABILITY, v.performance.begin());
				std::copy_n(u->level, NB_ABILITY, v.levels.begin());
				append(entities->units, std::move(v));
			}
	}

	if (needs(requirements, Component::Entities))
		for (const auto& p : game.buildProjects) append(entities->projects, {p.posX,p.posY,p.teamNumber,p.typeNum,p.unitWorking,p.unitWorkingFuture});
	teams->totalPrestige = game.totalPrestige;
	if (needs(requirements, Component::Catalogs)) result->catalogs = std::move(catalogsOwner);
	if (needs(requirements, Component::Terrain)) result->terrain = std::move(terrainOwner);
	if (needs(requirements, Component::Resources)) result->resources = std::move(resourcesOwner);
	if (needs(requirements, Component::Occupancy)) result->occupancy = std::move(occupancyOwner);
	if (needs(requirements, Component::Areas)) result->areas = std::move(areasOwner);
	if (needs(requirements, Component::Visibility)) result->visibility = std::move(visibilityOwner);
	if (needs(requirements, Component::Entities)) result->entities = std::move(entitiesOwner);
	if (needs(requirements, Component::Teams)) result->teams = std::move(teamsOwner);
	if (needs(requirements, Component::Rules)) result->rules = std::move(rulesOwner);
	if (needs(requirements, Component::Entities) && result->catalogs && result->terrain && result->resources && result->occupancy && result->rules) {
		const auto start = std::chrono::steady_clock::now();
		for (auto& building : entities->buildings) {
			const auto& kind = result->catalogs->buildings->at(building.type);
			building.upgradeAvailable = kind.next >= 0 && std::size_t(kind.next) < result->catalogs->buildings->size()
				&& result->catalogs->buildings->at(kind.next).available;
			auto space = [&](bool upgrade) {
				if (upgrade && result->rules->values.upgradesDisabled) return false;
				if (!upgrade && !kind.semantics.repairable) return false;
				const int next = upgrade ? kind.next : kind.previous;
				if (next == BUILDING_LEVEL_NONE) return true;
				const auto& target = result->catalogs->buildings->at(next);
				if (target.isVirtual) return true;
				const int x = building.x + target.decLeft - kind.decLeft, y = building.y + target.decTop - kind.decTop;
				for (int dy=0;dy<target.height;++dy) for (int dx=0;dx<target.width;++dx) {
					const auto index=std::size_t((y+dy)&(result->height-1))*result->width+((x+dx)&(result->width-1));
					if (result->resources->cells[index].resource.type != NO_RES_TYPE) return false;
					const auto occupant = result->occupancy->cells[index].building;
					if (occupant != NOGBID && occupant != building.identity.gid) return false;
					if (!result->terrain->registry->properties((*result->terrain->identity)[index]).buildable) return false;
				}
				return true;
			};
			building.hardSpaceUpgrade=space(true); building.hardSpaceRepair=space(false);
		}
		if (storage) storage->preparationNs += std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now()-start).count();
	}
	if (needs(requirements, Component::Growth)) {
		const auto start=std::chrono::steady_clock::now();
		const auto& source=game.map.resourceGrowthField();
		if (storage) storage->preparationNs += std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now()-start).count();
		auto frozen=storage ? storage->growth.acquire(storage->allocations) : std::make_shared<Fertility::GrowthCache>();
		const auto oldCap=frozen->storageCapacities(), required=source.storageSizes();
		if (storage) for(unsigned i=0;i<oldCap.size();++i) if(oldCap[i]<required[i]) ++storage->allocations;
		*frozen=source; result->growth=std::move(frozen);
	}
	if (needs(requirements, Component::ResourceFields))
	{
		auto fields = acquire(&Storage::resourceFields, Component::ResourceFields);
		for (auto& [key, field] : fields->values) field.values.reset();
		game.map.visitPublishedResourceFields([&](int team, int resource, int swim, bool market, Uint64 generation, const Uint16* data, std::size_t size) {
			const ResourceFieldKey key{team, resource, swim, market};
			if (previous && previous->worldIdentity == result->worldIdentity && previous->resourceFields)
			{
				const auto found = previous->resourceFields->values.find(key);
				if (found != previous->resourceFields->values.end() && found->second.generation == generation)
				{ if (storage && !fields->values.contains(key)) ++storage->allocations;
					fields->values.insert_or_assign(key, found->second); return; }
			}
			if (storage && !storage->resourcePlanes.contains(key)) ++storage->allocations;
			auto plane = storage ? storage->resourcePlanes[key].acquire(storage->allocations) : std::make_shared<std::vector<Uint16>>();
			if (storage && plane->capacity() < size) ++storage->allocations;
			plane->resize(size); std::copy_n(data, size, plane->begin());
			if (storage && !fields->values.contains(key)) ++storage->allocations;
			fields->values.insert_or_assign(key, ResourceField{generation, std::move(plane)});
		});
		std::erase_if(fields->values, [](const auto& entry) { return !entry.second.values; });
		result->resourceFields = std::move(fields);
	}
	return value;
}
} // namespace SimulationSnapshot
