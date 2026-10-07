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
#include <cstring>

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
		std::copy_n(type.materialMultiplier, MaterialSlotCount, kind.materialMultiplier.begin());
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
		std::copy_n(type.maxMaterial, MaterialSlotCount, kind.maxMaterial.begin());
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
		const bool sameCatalog = previous->catalogs && previous->catalogs->buildings == catalog && previous->configurationRevision == result->configurationRevision
			&& previous->catalogs->resources == game.map.frozenResourceRegistry() && previous->catalogs->habitats == game.map.frozenResourceHabitats();
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
	if (needs(requirements, Component::Entities)) {
		const auto teams = game.mapHeader.getNumberOfTeams();
		reserve(entities->buildingSlotIndices, teams * Building::MAX_COUNT);
		reserve(entities->unitSlotIndices, teams * Unit::MAX_COUNT);
		entities->buildingSlotIndices.resize(teams * Building::MAX_COUNT);
		entities->unitSlotIndices.resize(teams * Unit::MAX_COUNT);
	}
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
		catalogs->capabilities = game.buildingCapabilities().frozenTables();
		for (int type = 0; type < NB_UNIT_TYPE; ++type)
			std::copy_n(Race::unitTypes[type], NB_UNIT_LEVELS, catalogs->unitTypes[type].begin());
		catalogs->resources = game.map.frozenResourceRegistry();
		catalogs->habitats = game.map.frozenResourceHabitats();
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
	const auto copyArray = [&](auto& destination, const auto source) {
        using Element = typename std::remove_reference_t<decltype(destination)>::value_type;
        static_assert(std::is_trivially_copyable_v<Element>);
        static_assert(std::is_same_v<Element, std::remove_const_t<typename decltype(source)::element_type>>);
        reserve(destination, source.size()); destination.resize(source.size());
        if (!source.empty()) std::memcpy(destination.data(), source.data(), source.size_bytes());
    };
    if (needs(requirements,Component::Terrain)) copyArray(terrain->legacy, game.map.legacyTerrainState());
    if (needs(requirements,Component::Resources)) {
        copyArray(resources->cells, game.map.resourceState());
        copyArray(resources->stockIndices, game.map.resourceStockIndexState());
        copyArray(resources->stocks, game.map.resourceStockState());
        resources->staticMaterialSourceGeneration = game.map.staticMaterialSourceGeneration();
        { const auto live = game.map.cellView(); std::copy(live.materialSourceCounts.begin(), live.materialSourceCounts.end(), resources->materialSourceCounts.begin()); }
    }
    if (needs(requirements,Component::Occupancy)) copyArray(occupancy->cells, game.map.occupancyState());
    if (needs(requirements,Component::Areas)) copyArray(areas->cells, game.map.areaState());
    if (needs(requirements,Component::Visibility)) {
        copyArray(visibility->discovered, std::span<const Uint32>(game.map.mapDiscovered));
        if (game.map.fogOfWar) copyArray(visibility->visible, std::span<const Uint32>(game.map.fogOfWar,game.map.cellCount()));
        else { reserve(visibility->visible,game.map.cellCount()); visibility->visible.resize(game.map.cellCount());
            std::fill(visibility->visible.begin(),visibility->visible.end(),0); }
    }
	for (int t = 0; (needs(requirements, Component::Teams) || needs(requirements, Component::Entities)) && t < game.mapHeader.getNumberOfTeams(); ++t)
	{
		const auto* team = game.teams[t];
		if (!team) {
			if (needs(requirements, Component::Teams)) teams->values[t] = {};
			if (needs(requirements, Component::Entities)) {
				std::fill_n(entities->buildingSlotIndices.begin() + t * Building::MAX_COUNT, Building::MAX_COUNT, Entities::NoRecord);
				std::fill_n(entities->unitSlotIndices.begin() + t * Unit::MAX_COUNT, Unit::MAX_COUNT, Entities::NoRecord);
			}
			continue;
		}
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
		std::copy_n(team->teamMaterials, MaterialSlotCount, target.materials.begin());
		}
		if (!needs(requirements, Component::Entities)) continue;
		// Live lists walk occupied slots in slot order, so records keep the
		// order a full slot sweep would produce without visiting empty slots.
		std::fill_n(entities->buildingSlotIndices.begin() + t * Building::MAX_COUNT, Building::MAX_COUNT, Entities::NoRecord);
		std::fill_n(entities->unitSlotIndices.begin() + t * Unit::MAX_COUNT, Unit::MAX_COUNT, Entities::NoRecord);
		for (std::size_t n = 0; n < team->liveBuildings.size(); ++n)
		{
			const auto i = team->liveBuildings.slots()[n];
			auto& slot = entities->buildingSlotIndices[t * Building::MAX_COUNT + i];
			if (auto* b = team->liveBuildings.entries()[n])
			{
				slot = Uint32(entities->buildings.size());
				BuildingView v;
				std::memcpy(static_cast<BuildingStateRecord*>(&v), static_cast<const BuildingStateRecord*>(b), sizeof(BuildingStateRecord));
				v.identity = Game::refOf(b); v.team = t;
				v.maxHp = b->getEffectiveMaxHp();
				v.usesTeamResources = b->materials == team->teamMaterials;
				v.working = {Uint32(entities->relationships.size()), Uint32(b->unitsWorking.size())};
				for (const auto* u : b->unitsWorking) append(entities->relationships, Game::refOf(u));
				v.inside = {Uint32(entities->relationships.size()), Uint32(b->unitsInside.size())};
				for (const auto* u : b->unitsInside) append(entities->relationships, Game::refOf(u));
				append(entities->buildings, std::move(v));
			}
		}
		for (std::size_t n = 0; n < team->liveUnits.size(); ++n)
		{
			const auto i = team->liveUnits.slots()[n];
			auto& slot = entities->unitSlotIndices[t * Unit::MAX_COUNT + i];
			if (const auto* u = team->liveUnits.entries()[n])
			{
				slot = Uint32(entities->units.size());
				UnitView v;
				std::memcpy(static_cast<UnitState*>(&v), static_cast<const UnitState*>(u), sizeof(UnitState));
				v.identity = Game::refOf(u); v.team = t;
				v.attached = Game::refOf(u->attachedBuilding);
				v.target = Game::refOf(u->targetBuilding);
				append(entities->units, std::move(v));
			}
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
		fields->clear();
		const auto planeCells = std::size_t(game.map.getW()) * game.map.getH();
		const ResourceFields* kept = previous && previous->worldIdentity == result->worldIdentity ? previous->resourceFields.get() : nullptr;
		for (const auto& plane : game.map.publishedResourceFields())
		{
			if (storage && fields->planes.size() == fields->planes.capacity()) ++storage->allocations;
			if (kept) if (const auto* same = kept->find(plane.key); same && same->generation == plane.generation) { fields->add(*same); continue; }
			auto values = storage ? storage->resourcePlanes.acquire(storage->allocations) : std::make_shared<std::vector<Uint16>>();
			if (storage && values->capacity() < planeCells) ++storage->allocations;
			values->resize(planeCells); std::copy_n(*plane.slot, planeCells, values->begin());
			fields->add(ResourceField{plane.key, plane.generation, std::move(values)});
		}
		result->resourceFields = std::move(fields);
	}
	return value;
}
} // namespace SimulationSnapshot
