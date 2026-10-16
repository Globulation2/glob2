// SPDX-License-Identifier: GPL-3.0-or-later
#include "SharedBuildingGradient.h"
#include "WorldSnapshot.h"
#include "SnapshotStorage.h"
#include "Building.h"
#include "Game.h"
#include "GameRuleOverrides.h"
#include "BuildingCapabilities.h"
#include "GlobalContainer.h"
#include "Unit.h"
#include "Team.h"
#include "OutcomeReason.h"
#include "Player.h"
#include "Bullet.h"
#include "Sector.h"
#include "render/GameAnimations.h"
#include "sim/ClientEvents.h"
#include <string>
#include "Race.h"
#include <algorithm>
#include <iterator>
#include <stdexcept>
#include <chrono>
#include <cstring>
#include <bit>
#include <utility>

namespace SimulationSnapshot
{
static_assert(SWIM_CLASS_COUNT<=8);
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
	std::shared_ptr<const std::vector<BuildingKindView>> catalog, Requirements requirements, const Handle* previous, Storage* storage, bool unchangedBoundary)
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
		// The store certifies the complete owner boundary before widening its
		// optional payload. Entity records and their sidecars remain immutable.
		reuse(Component::Entities, result->entities, previous->entities, unchangedBoundary);
        reuse(Component::Teams, result->teams, previous->teams, unchangedBoundary);
        reuse(Component::StatisticsInputs, result->statisticsInputs, previous->statisticsInputs, unchangedBoundary);
        reuse(Component::ResourceFields, result->resourceFields, previous->resourceFields, unchangedBoundary);
        reuse(Component::Session, result->session, previous->session, unchangedBoundary);
        reuse(Component::Effects, result->effects, previous->effects, unchangedBoundary);
        reuse(Component::Statistics, result->statistics, previous->statistics, unchangedBoundary);
        reuse(Component::History, result->history, previous->history, unchangedBoundary);
        reuse(Component::Telemetry, result->telemetry, previous->telemetry, unchangedBoundary);
        reuse(Component::EntityDiagnostics, result->entityDiagnostics, previous->entityDiagnostics, unchangedBoundary);
        if (unchangedBoundary && needs(requirements, Component::Growth)) result->areaFertility = previous->areaFertility;
		const bool sameCatalog = previous->catalogs && previous->catalogs->buildings == catalog && previous->configurationRevision == result->configurationRevision
			&& (!needs(requirements, Component::Session) || !previous->catalogs->buildingFingerprint.empty())
            && previous->catalogs->assets == game.map.frozenAssetBundle()
            && previous->catalogs->resources == game.map.frozenResourceRegistry();
		reuse(Component::Catalogs, result->catalogs, previous->catalogs, sameCatalog);
		reuse(Component::Rules, result->rules, previous->rules, previous->configurationRevision == result->configurationRevision);
		const auto generations = result->mapGenerations;
		const auto old = previous->mapGenerations;
		const bool sameTerrain = previous->terrain && previous->terrain->revision == game.map.terrainGeneration() && generations[0] == old[0];
		reuse(Component::Terrain, result->terrain, previous->terrain, sameTerrain);
		reuse(Component::Resources, result->resources, previous->resources, sameTerrain && generations[1] == old[1]);
		reuse(Component::Occupancy, result->occupancy, previous->occupancy, generations[2] == old[2]);
		reuse(Component::Areas, result->areas, previous->areas, generations[3] == old[3] && previous->configurationRevision == result->configurationRevision);
		reuse(Component::Annotations, result->annotations, previous->annotations, generations[3] == old[3]);
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
	// Chunk-stamped map arrays reuse the free buffer filled most recently.
	auto acquireStamped = [&]<class T>(BufferPool<T> Storage::* pool, Component component) {
		if (!needs(requirements, component)) return std::shared_ptr<T>{};
		return storage ? (storage->*pool).acquire(storage->allocations, [](const T& buffer) { return buffer.stamps.filledTick; }) : std::make_shared<T>();
	};
	auto catalogsOwner = acquire(&Storage::catalogs, Component::Catalogs);
	auto* catalogs = catalogsOwner.get();
	auto terrainOwner = acquireStamped(&Storage::terrain, Component::Terrain);
	auto* terrain = terrainOwner.get();
	auto resourcesOwner = acquireStamped(&Storage::resources, Component::Resources);
	auto* resources = resourcesOwner.get();
	auto occupancyOwner = acquireStamped(&Storage::occupancy, Component::Occupancy);
	auto* occupancy = occupancyOwner.get();
	auto areasOwner = acquireStamped(&Storage::areas, Component::Areas);
	auto* areas = areasOwner.get();
	auto visibilityOwner = acquireStamped(&Storage::visibility, Component::Visibility);
	auto* visibility = visibilityOwner.get();
	auto entitiesOwner = acquire(&Storage::entities, Component::Entities);
	auto* entities = entitiesOwner.get();
	auto teamsOwner = acquire(&Storage::teams, Component::Teams);
	auto* teams = teamsOwner.get();
	auto rulesOwner = acquire(&Storage::rules, Component::Rules);
	auto* rules = rulesOwner.get();
	// Map buffers keep their constructed range when reused. Overwrite selected
	// arrays directly rather than clearing and growing them one cell at a time.
    if (needs(requirements, Component::Entities)) {
        if (storage) ++storage->coreMaterializations;
        entities->funding.clear(); entities->relationships.clear(); entities->projects.clear();
        entities->extraProduction.clear();
        entities->unitCargo = game.unitCargo; entities->availableGroundClasses.fill(0);
        if (storage) for (const auto& [gid, inventory] : game.unitCargo.entries())
            storage->entitySidecarBytesCopied += sizeof(gid) + inventory.size() * sizeof(UnitCargoEntry);
    }
	if (needs(requirements, Component::Entities)) {
		const auto teams = game.mapHeader.getNumberOfTeams();
		std::size_t buildings = 0, units = 0;
		for (int t = 0; t < teams; ++t) if (const auto* team = game.teams[t]) {
			buildings += team->liveBuildings.size(); units += team->liveUnits.size();
		}
		reserve(entities->buildings, buildings); reserve(entities->units, units);
		// Reuse constructed records and fill them in place. Every field is
		// overwritten below; no temporary record or per-record push_back copy.
		entities->buildings.resize(buildings); entities->units.resize(units);
		reserve(entities->buildingSlotIndices, teams * Building::MAX_COUNT);
		reserve(entities->unitSlotIndices, teams * Unit::MAX_COUNT);
		entities->buildingSlotIndices.resize(teams * Building::MAX_COUNT);
		entities->unitSlotIndices.resize(teams * Unit::MAX_COUNT);
		reserve(entities->buildingRanges, teams);
		reserve(entities->unitRanges, teams);
		entities->buildingRanges.assign(teams, {}); entities->unitRanges.assign(teams, {});
	}
	if (needs(requirements, Component::Teams)) { reserve(teams->values, game.mapHeader.getNumberOfTeams()); teams->values.resize(game.mapHeader.getNumberOfTeams()); }
	result->tick = game.stepCounter;
	result->width = game.map.getW(); result->height = game.map.getH();
	if (needs(requirements, Component::Terrain))
	{ terrain->registry = game.map.frozenTerrainRegistry();
	terrain->rules = game.map.frozenCellRules();
	terrain->vertices = game.map.frozenVertexSnapshot();
	terrain->revision = game.map.terrainGeneration();
	terrain->movementModifiers = game.map.hasTerrainMovementModifiers();
	terrain->airConstraints = game.map.hasAirTerrainConstraints(); }
	const auto& header = game.gameHeader;
	if (needs(requirements, Component::Catalogs)) {
		// Preference identity is needed by session consumers, not standalone AI
        // observations. Computing it serializes the authored catalog; do that
        // once when a shared session catalog is admitted, never per AI poll.
        catalogs->buildingFingerprint = needs(requirements, Component::Session)
            ? game.buildingsTypes.fingerprint() : std::string();
		catalogs->buildings = std::move(catalog);
		catalogs->capabilities = game.buildingCapabilities().frozenTables();
		catalogs->units = header.getUnitCatalog();
        catalogs->unitTypes.catalog = catalogs->units;
		catalogs->typeDefinitions = std::make_shared<const std::vector<BuildingType>>(*game.buildingsTypes.retainTypes());
		catalogs->resources = game.map.frozenResourceRegistry();
        catalogs->assets = game.map.frozenAssetBundle();
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
        if (storage) storage->bytesCopied += source.size_bytes();
    };
	// Map arrays: a buffer that already mirrors this world copies only the
	// chunks whose live stamp moved since its last fill, unless most are dirty.
	// Dense changes use contiguous copies to avoid strided row-copy overhead.
	// New buffers, world changes and resizes also copy everything. Arrays
	// sharing one tracker are refreshed from the same dirty set.
	const auto identity = game.map.identity();
	const auto& geometry = game.map.chunks();
	const auto cellCount = game.map.cellCount();
	const auto prepare = [&](auto& destination, std::size_t count) {
		const bool resized = destination.size() != count;
		reserve(destination, count); destination.resize(count);
		return resized;
	};
	const auto copyCells = [&](auto& destination, const auto source, std::size_t start, std::size_t length) {
		using Element = typename std::remove_reference_t<decltype(destination)>::value_type;
		static_assert(std::is_trivially_copyable_v<Element>);
		static_assert(std::is_same_v<Element, std::remove_const_t<typename decltype(source)::element_type>>);
		if (length) std::memcpy(destination.data() + start, source.data() + start, length * sizeof(Element));
		if (storage) storage->bytesCopied += length * sizeof(Element);
	};
	const auto refresh = [&](ChunkStamps& stamps, const MapState::ChangeTracker& live, bool resized, auto copyRange) {
		const bool everything = resized || stamps.worldIdentity != identity || stamps.width != geometry.width
			|| stamps.height != geometry.height || stamps.chunks.size() != live.chunks.size();
		std::size_t dirty = 0;
		if (!everything)
			for (std::size_t chunk = 0; chunk < live.chunks.size(); ++chunk)
				dirty += stamps.chunks[chunk] != live.chunks[chunk];
		if (everything || dirty > live.chunks.size() / 2)
		{
			copyRange(std::size_t(0), cellCount);
			if (storage && stamps.chunks.capacity() < live.chunks.size()) ++storage->allocations;
			stamps.chunks.assign(live.chunks.begin(), live.chunks.end());
		}
		else for (std::size_t chunk = 0; chunk < live.chunks.size(); ++chunk)
			if (stamps.chunks[chunk] != live.chunks[chunk])
			{
				geometry.forEachRow(chunk, copyRange);
				stamps.chunks[chunk] = live.chunks[chunk];
			}
		stamps.worldIdentity = identity; stamps.width = geometry.width; stamps.height = geometry.height;
		stamps.filledTick = game.stepCounter;
	};
    if (needs(requirements, Component::Annotations)) {
        auto annotations=storage ? storage->annotations.acquire(storage->allocations, [](const Annotations& a) { return a.stamps.filledTick; }) : std::make_shared<Annotations>();
        annotations->areaNames = game.map.areaNames;
        const auto source=game.map.scriptAreaState();
        const bool resized=prepare(annotations->scriptAreas,source.size());
        refresh(annotations->stamps,game.map.changes(MapState::TrackedArray::Areas),resized,
            [&](size_t start,size_t length) { copyCells(annotations->scriptAreas,source,start,length); });
        result->annotations=std::move(annotations);
    }
	if (needs(requirements, Component::Terrain))
	{
		// Rule indices stay valid while the table only grows; replacing the
		// table marks every terrain chunk changed, so no stale index survives.
		const auto source = game.map.cellRuleState();
		const bool resized = prepare(terrain->cellRules, source.size());
		refresh(terrain->stamps, game.map.changes(MapState::TrackedArray::Terrain), resized,
			[&](std::size_t start, std::size_t length) { copyCells(terrain->cellRules, source, start, length); });
	}
	if (needs(requirements, Component::Resources))
	{
		const auto cellsSource = game.map.resourceState();
		const auto indexSource = game.map.resourceStockIndexState();
		bool resized = prepare(resources->cells, cellsSource.size());
		resized |= prepare(resources->stockIndices, indexSource.size());
		refresh(resources->stamps, game.map.changes(MapState::TrackedArray::Resources), resized, [&](std::size_t start, std::size_t length) {
			copyCells(resources->cells, cellsSource, start, length);
			if (!indexSource.empty()) copyCells(resources->stockIndices, indexSource, start, length);
		});
		copyArray(resources->stocks, game.map.resourceStockState());
		resources->staticMaterialSourceGeneration = game.map.staticMaterialSourceGeneration();
		{ const auto live = game.map.cellView(); std::copy(live.materialSourceCounts.begin(), live.materialSourceCounts.end(), resources->materialSourceCounts.begin()); }
	}
	if (needs(requirements, Component::Occupancy))
	{
		const auto source = game.map.occupancyState();
		const bool resized = prepare(occupancy->cells, source.size());
		refresh(occupancy->stamps, game.map.changes(MapState::TrackedArray::Occupancy), resized,
			[&](std::size_t start, std::size_t length) { copyCells(occupancy->cells, source, start, length); });
	}
	if (needs(requirements, Component::Areas))
	{
		const auto source = game.map.areaState();
		const bool resized = prepare(areas->cells, source.size());
		refresh(areas->stamps, game.map.changes(MapState::TrackedArray::Areas), resized,
			[&](std::size_t start, std::size_t length) { copyCells(areas->cells, source, start, length); });
	}
	if (needs(requirements, Component::Visibility))
	{
		const std::span<const Uint32> discovered(game.map.mapDiscovered);
		const std::span<const Uint32> visible = game.map.fogOfWar ? std::span<const Uint32>(game.map.fogOfWar, cellCount) : std::span<const Uint32>{};
		bool resized = prepare(visibility->discovered, discovered.size());
		resized |= prepare(visibility->visible, cellCount);
		refresh(visibility->stamps, game.map.changes(MapState::TrackedArray::Visibility), resized, [&](std::size_t start, std::size_t length) {
			copyCells(visibility->discovered, discovered, start, length);
			if (!visible.empty()) copyCells(visibility->visible, visible, start, length);
			else std::fill_n(visibility->visible.begin() + start, length, 0u);
		});
	}
	std::size_t buildingRecordCount = 0, unitRecordCount = 0;
	for (int t = 0; (needs(requirements, Component::Teams) || needs(requirements, Component::Entities)) && t < game.mapHeader.getNumberOfTeams(); ++t)
	{
		const auto* team = game.teams[t];
		if (!team) {
			if (needs(requirements, Component::Teams)) teams->values[t] = {};
			if (needs(requirements, Component::Entities)) {
				entities->buildingRanges[t]={Uint32(buildingRecordCount),0};
				entities->unitRanges[t]={Uint32(unitRecordCount),0};
				std::fill_n(entities->buildingSlotIndices.begin() + t * Building::MAX_COUNT, Building::MAX_COUNT, Entities::NoRecord);
				std::fill_n(entities->unitSlotIndices.begin() + t * Unit::MAX_COUNT, Unit::MAX_COUNT, Entities::NoRecord);
			}
			continue;
		}
		if (needs(requirements, Component::Teams)) {
		auto& target = teams->values[t]; target.number = t;
        target.supplierPresence=(!team->stockSuppliers.empty()?1:0)|(!team->directStockSuppliers.empty()?2:0);
		target.virtualBuildings.clear(); target.swarms.clear();
		target.prestige = team->prestige; target.alive = team->isAlive;
		target.startX = team->startPosX; target.startY = team->startPosY;
		target.mask = team->me; target.allies = team->allies;
		target.enemies = team->attackableTeams();
		target.playersMask = team->playersMask;
		target.foodVision = team->sharedVisionFood;
		target.exchangeVision = team->sharedVisionExchange;
		target.otherVision = team->sharedVisionOther;
		if (storage && target.statistics.buildingCountByVariant.capacity() < std::as_const(team->stats).getLatestStat()->buildingCountByVariant.size()) ++storage->allocations;
        target.color = {team->color.r,team->color.g,team->color.b,team->color.a}; target.won = team->hasWon; target.lost = team->hasLost;
        target.outcomeReasonKey = outcomeReasonKey(game, t);
        target.unitConversionGained = team->unitConversionGained;
        target.unitConversionLost = team->unitConversionLost;
        target.noMoreBuildingSitesCountdown = team->noMoreBuildingSitesCountdown;
        target.firstPlayerName.clear();
        for (int p=0;p<game.gameHeader.getNumberOfPlayers();++p)
            if (game.players[p] && game.players[p]->team==team) { target.firstPlayerName=game.players[p]->name; break; }
        std::copy_n(team->reservedTeamMaterials,MaterialSlotCount,target.reservedMaterials.begin());
		target.statistics = *std::as_const(team->stats).getLatestStat();
		// Match the existing smoothed-stat queries, rather than deriving a new
		// balance from the current sample and changing policy inputs.
		target.workerBalance = target.statistics.idleCarriers - target.statistics.totalNeeded;
		target.starving = target.statistics.needFoodCritical;
		std::copy_n(target.statistics.workersByConstructionLevel, NB_UNIT_LEVELS, target.workersLevel.begin());
		for (auto* building : team->swarms) append(target.swarms, Game::refOf(building));
		for (auto* building : team->virtualBuildings) append(target.virtualBuildings, Game::refOf(building));
		std::copy_n(team->teamMaterials, MaterialSlotCount, target.materials.begin());
		}
		if (!needs(requirements, Component::Entities)) continue;
		// Preserve authoritative append order in compact immutable team ranges.
		entities->buildingRanges[t] = {Uint32(buildingRecordCount), Uint32(team->liveBuildings.size())};
		entities->unitRanges[t] = {Uint32(unitRecordCount), Uint32(team->liveUnits.size())};
		std::fill_n(entities->buildingSlotIndices.begin() + t * Building::MAX_COUNT, Building::MAX_COUNT, Entities::NoRecord);
		std::fill_n(entities->unitSlotIndices.begin() + t * Unit::MAX_COUNT, Unit::MAX_COUNT, Entities::NoRecord);
		for (auto* b : team->liveBuildings.entries())
		{
			auto& slot = entities->buildingSlotIndices[b->gid];
			slot = Uint32(buildingRecordCount);
			auto& v = entities->buildings[buildingRecordCount++];
			std::memcpy(static_cast<BuildingStateRecord*>(&v), static_cast<const BuildingStateRecord*>(b), sizeof(BuildingStateRecord));
			if (storage) storage->entityBytesCopied += sizeof(BuildingStateRecord);
			v.identity = Game::refOf(b); v.team = t; v.fundingOffset = Entities::NoRecord;
                const auto nonzero = [](const auto &values) {
                    return std::any_of(values.begin(), values.end(), [](auto value) { return value != 0; });
                };
                if (nonzero(b->constructionBudget) || nonzero(b->constructionReserved) || nonzero(b->reservedMaterials)) {
                    v.fundingOffset = Uint32(entities->funding.size());
                    append(entities->funding, FundingState{b->constructionBudget, b->constructionReserved, b->reservedMaterials});
                }
                for (unsigned id=0; id<BuiltinUnitCount; ++id) v.constructionOriginRatios[id]=b->originProductionRatio(id);
                v.extraProductionOffset = Uint32(entities->extraProduction.size());
                v.extraProductionCount = Uint32(b->extraProductionRatios.size());
                for (unsigned extra=0; extra<v.extraProductionCount; ++extra) {
                    const unsigned id=BuiltinUnitCount+extra;
                    append(entities->extraProduction, ExtraProductionState{b->productionRatio(id),b->productionUsed(id),b->originProductionRatio(id)});
                }
			v.maxHp = b->getEffectiveMaxHp();
			v.usesTeamResources = b->usesTeamMaterials();
			v.availableSupplyMask = 0;
			for (unsigned supplied=b->runtime->suppliesStockMask|b->runtime->suppliesDirectStockMask; supplied; supplied &= supplied-1) {
				const unsigned material=std::countr_zero(supplied);
				if (b->availableMaterial(material)>0) v.availableSupplyMask |= MaterialMask(1u<<material);
			}
			v.working = {Uint32(entities->relationships.size()), Uint32(b->unitsWorking.size())};
			for (const auto* u : b->unitsWorking) append(entities->relationships, Game::refOf(u));
			v.inside = {Uint32(entities->relationships.size()), Uint32(b->unitsInside.size())};
			for (const auto* u : b->unitsInside) append(entities->relationships, Game::refOf(u));
			if (storage) storage->entitySidecarBytesCopied += sizeof(v.constructionOriginRatios)
                    + v.extraProductionCount * sizeof(ExtraProductionState)
                    + (v.working.count + v.inside.count) * sizeof(UnitRef);
		}
		for (const auto* u : team->liveUnits.entries())
		{
			auto& slot = entities->unitSlotIndices[u->gid];
			slot = Uint32(unitRecordCount);
			auto& v = entities->units[unitRecordCount++];
			std::memcpy(static_cast<UnitState*>(&v), static_cast<const UnitState*>(u), sizeof(UnitState));
			if (storage) storage->entityBytesCopied += sizeof(UnitState);
			v.identity = Game::refOf(u); v.team = t;
			v.realAttackStrength = u->getRealAttackStrength();
			v.hasClearingClaim = u->previousClearingArea && game.map.isClearingAreaClaimed(
				u->previousClearingArea->x, u->previousClearingArea->y, t) == u->gid;
			v.clearingClaimX = v.hasClearingClaim ? u->previousClearingArea->x : 0;
			v.clearingClaimY = v.hasClearingClaim ? u->previousClearingArea->y : 0;
			v.clearingClaimDistance = v.hasClearingClaim ? u->previousClearingAreaDistance : 0;
                if(!v.performance[FLY] && !v.isDead && v.activity==Unit::ACT_RANDOM && v.medical==Unit::MED_FREE)entities->availableGroundClasses[t]|=Uint8(1u<<Map::swimClass(v.performance[WALK],v.performance[SWIM]));
			v.attached = Game::refOf(u->attachedBuilding);
			v.target = Game::refOf(u->targetBuilding);
		}
	}
	if (needs(requirements, Component::Entities)) {
		entities->buildings.resize(buildingRecordCount);
		entities->units.resize(unitRecordCount);
	}

    if (auto session=acquire(&Storage::session,Component::Session)) {
        session->editor=game.edit!=nullptr;
        session->fixedAlliances=game.gameHeader.areAllyTeamsFixed();
        session->objectives=game.objectives.frozen();
        session->hints=game.gameHints.frozen();
        const auto briefing=previous && previous->session ? previous->session->missionBriefing : nullptr;
        session->missionBriefing=briefing && *briefing==game.missionBriefing ? briefing : std::make_shared<const std::string>(game.missionBriefing);
        session->terrainSeed=game.map.terrainSeed(); session->fertilityMaximum=game.map.fertilityMaximum;
        session->executedOrderRevision=game.clientEvents ? game.clientEvents->executedOrderRevision() : 0;
        session->totalPrestigeReached=game.totalPrestigeReached;
        session->prestigeWinCondition=const_cast<Game&>(game).isPrestigeWinCondition();
        session->prestigeToReach=game.prestigeToReach;
        session->legacyScriptTextShown=game.legacyScriptActive() && game.sgslScript.isTextShown;
        session->legacyScriptText=session->legacyScriptTextShown ? game.sgslScript.textShown : std::string();
        session->legacyScriptTimer=game.legacyScriptTimer();
        session->players.resize(game.gameHeader.getNumberOfPlayers());
        for (size_t p=0;p<session->players.size();++p) {
            const auto* player=game.players[p];
            session->players[p]=player ? Session::Player{player->name,player->teamNumber,
                player->type!=BasePlayer::P_NONE && player->type!=BasePlayer::playerTypeFromImplementationID(AI::NONE), int(player->type)} : Session::Player{};
        }
        result->session=std::move(session);
    }
    if (auto effects=acquire(&Storage::effects,Component::Effects)) {
        const auto count=game.map.getSectorW()*game.map.getSectorH();
        effects->sectors.resize(count);
        for (int i=0;i<count;++i) {
            auto& sector=effects->sectors[i]; sector.bullets.clear(); sector.explosions.clear(); sector.deaths.clear();
            for (const auto* b:const_cast<Map&>(game.map).getSector(i)->bullets)
                sector.bullets.push_back({b->px,b->py,b->speedX,b->speedY,b->ticksLeft,b->ticksInitial});
            if (game.animations) {
                for (const auto* e:game.animations->getExplosions(i)) sector.explosions.push_back({e->x,e->y,e->ticksLeft});
                for (const auto* d:game.animations->getDeathAnimations(i)) sector.deaths.push_back({d->x,d->y,d->ticksLeft,d->team->teamNumber});
            }
        }
        result->effects=std::move(effects);
    }
    if (auto diagnostics=acquire(&Storage::entityDiagnostics,Component::EntityDiagnostics)) {
        diagnostics->buildings.clear();
        for (int t=0;t<game.teamsCount();++t) for (const auto* b:game.teams[t]->liveBuildings.entries()) {
            if (!b || (!b->verbose && !b->recordFailingUnits)) continue;
            EntityDiagnostics::Building value;
            value.identity=Game::refOf(b); value.verbose=b->verbose; value.recordFailingUnits=b->recordFailingUnits;
            std::copy_n(b->unitsFailingRequirements,EntityDiagnostics::FailReasons,value.failingCounts.begin());
            for (unsigned r=0;r<EntityDiagnostics::FailReasons;++r) value.failingUnits[r]=b->unitsFailingByReason[r];
            if (b->verbose==1 || b->verbose==2) {
                int swim=b->verbose==1 ? 0 : 1;
                while (b->verbose==2 && swim<SWIM_CLASS_COUNT-1 && !b->globalGradient[swim]) ++swim;
                auto guard=SharedBuildingGradient::lock(b->sharedGradient[swim]);
                if (b->globalGradient[swim]) value.gradient.assign(b->globalGradient[swim],b->globalGradient[swim]+cellCount);
            }
            diagnostics->buildings.push_back(std::move(value));
        }
        result->entityDiagnostics=std::move(diagnostics);
    }
    if (auto history = acquire(&Storage::history, Component::History)) {
        history->teams.resize(game.teamsCount());
        for (int t = 0; t < game.teamsCount(); ++t)
            history->teams[t] = game.teams[t]->stats.frozenHistory();
        result->history = std::move(history);
    }
    if (auto stats = acquire(&Storage::statistics, Component::Statistics)) {
        stats->teams.resize(game.teamsCount());
        for (int t = 0; t < game.teamsCount(); ++t)
            stats->teams[t] = game.teams[t]->stats.frozenDisplay();
        result->statistics = std::move(stats);
    }
    if (auto telemetry = acquire(&Storage::telemetry, Component::Telemetry)) {
        telemetry->rows.clear();
        for (int t = 0; t < game.teamsCount(); ++t)
            for (const auto& series : game.teams[t]->stats.aiTelemetry)
                if (series->active)
                    telemetry->rows.push_back({t, series->player, series->playerName, series->current.available,
                        series->fields, series->current.values, series->named});
        result->telemetry = std::move(telemetry);
    }
	if (needs(requirements, Component::Entities))
		for (const auto& p : game.buildProjects) append(entities->projects, {p.posX,p.posY,p.teamNumber,p.typeNum,p.unitWorking,p.unitWorkingFuture});
	if (needs(requirements, Component::Teams)) teams->totalPrestige = game.totalPrestige;
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
		const auto& source=game.map.resourceGrowthField();
		auto frozen=storage ? storage->growth.acquire(storage->allocations) : std::make_shared<Fertility::GrowthCache>();
		const auto oldCap=frozen->storageCapacities(), required=source.storageSizes();
		if (storage) for(unsigned i=0;i<oldCap.size();++i) if(oldCap[i]<required[i]) ++storage->allocations;
		*frozen=source; result->growth=std::move(frozen);

	}
	// Natural ecology can be reused while building coverage changes independently.
	if (needs(result->requirements, Component::Growth) && !result->areaFertility &&
		!game.areaEffects.fertilityValues().empty())
	{
		if (previous && previous->areaFertility &&
			previous->areaFertility->world == game.map.identity() &&
			previous->areaFertility->generation == game.areaEffects.fertilityGeneration())
			result->areaFertility = previous->areaFertility;
		else
		{
			auto area = storage ? storage->areaFertility.acquire(
									  storage->allocations,
									  [&](const BuildingAreaEffects::FertilitySnapshot &buffer) {
										  return buffer.world == game.map.identity()
													 ? buffer.generation
													 : Uint64(0);
									  })
								: std::make_shared<BuildingAreaEffects::FertilitySnapshot>();
			if (storage && area->values.capacity() < game.areaEffects.fertilityValues().size())
				++storage->allocations;
			Uint64 copied = 0;
			game.areaEffects.captureFertility(*area, copied);
			if (storage)
				storage->bytesCopied += copied;
			result->areaFertility = std::move(area);
		}
	}
	if (needs(requirements, Component::ResourceFields))
	{
		auto fields = acquire(&Storage::resourceFields, Component::ResourceFields);
		fields->clear();
		const auto planeCells = std::size_t(game.map.getW()) * game.map.getH();
		const ResourceFields *kept = previous && previous->worldIdentity == result->worldIdentity
										 ? previous->resourceFields.get()
										 : nullptr;
		for (const auto &plane : game.map.publishedResourceFields())
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
	if (auto input = acquire(&Storage::statisticsInputs, Component::StatisticsInputs)) {
        input->events.resize(game.teamsCount());
        input->enemies.resize(game.teamsCount());
        input->coverage.resize(game.teamsCount());
        for (int t=0; t<game.teamsCount(); ++t) {
            const auto* team=game.teams[t];
            if (team) {
                input->events[t].capture(team->stats.events());
                input->coverage[t] = team->stats.coverageBuildings;
            } else {
                input->events[t] = {};
                input->coverage[t].clear();
            }
            input->enemies[t] = team ? team->enemies : 0;
        }
        result->statisticsInputs = std::move(input);
    }
    if (storage && storage->verify) verifyCapture(game, value);
	return value;
}

// Byte-compare every captured map array with the live arrays and check the
// team live lists, naming the first mismatch. Catches writes that bypass the
// change trackers; costs a full read of the map, so it is opt-in.
void verifyCapture(const Game& game, const Handle& handle)
{
	const auto& geometry = game.map.chunks();
	const auto compare = [&](const char* component, const auto& captured, const auto live) {
		using Element = typename std::remove_reference_t<decltype(captured)>::value_type;
		if (captured.size() != live.size()) throw std::logic_error(std::string("snapshot verification: ") + component + " size differs from the live map");
		if (live.empty()) return; // an absent sidecar has no chunks to compare
		for (std::size_t chunk = 0; chunk < geometry.count(); ++chunk)
			geometry.forEachRow(chunk, [&](std::size_t start, std::size_t length) {
				if (std::memcmp(captured.data() + start, live.data() + start, length * sizeof(Element)))
					throw std::logic_error(std::string("snapshot verification: ") + component + " chunk " + std::to_string(chunk) + " differs from the live map");
			});
	};
	const auto live = game.map.cellView();
	if (handle.terrain)
	{
		compare("cell rules", handle.terrain->cellRules, game.map.cellRuleState());
		const auto vertices = game.map.vertexTerrainState();
		if (!handle.terrain->vertices || handle.terrain->vertices->size() != vertices.size()
			|| (!vertices.empty() && std::memcmp(handle.terrain->vertices->data(), vertices.data(), vertices.size_bytes())))
			throw std::logic_error("snapshot verification: terrain vertices differ from the live map");
	}
	if (handle.resources)
	{
		compare("resources", handle.resources->cells, game.map.resourceState());
		compare("resource stock indices", handle.resources->stockIndices, game.map.resourceStockIndexState());
		// The stock sidecar and source counts ride on the Resources generation too.
		if (handle.resources->stocks != *live.stocks)
			throw std::logic_error("snapshot verification: resource stocks differ from the live map");
		if (!std::equal(handle.resources->materialSourceCounts.begin(), handle.resources->materialSourceCounts.end(), live.materialSourceCounts.begin(), live.materialSourceCounts.end()))
			throw std::logic_error("snapshot verification: material source counts differ from the live map");
		if (handle.resources->staticMaterialSourceGeneration != game.map.staticMaterialSourceGeneration())
			throw std::logic_error("snapshot verification: static material source generation differs from the live map");
	}
	if (handle.occupancy) compare("occupancy", handle.occupancy->cells, game.map.occupancyState());
	if (handle.areas) compare("areas", handle.areas->cells, game.map.areaState());
	if (handle.visibility)
	{
		compare("discovered", handle.visibility->discovered, std::span<const Uint32>(game.map.mapDiscovered));
		if (game.map.fogOfWar) compare("visible", handle.visibility->visible, std::span<const Uint32>(game.map.fogOfWar, game.map.cellCount()));
	}
    if (handle.catalogs && handle.catalogs->units!=game.gameHeader.getUnitCatalog())
        throw std::logic_error("snapshot verification: unit catalog differs from the game");
    if (handle.entities) {
        const auto& entities = *handle.entities;
        const int teams = game.mapHeader.getNumberOfTeams();
        if (entities.unitSlotIndices.size() != std::size_t(teams) * Unit::MAX_COUNT
            || entities.buildingSlotIndices.size() != std::size_t(teams) * Building::MAX_COUNT
            || entities.unitRanges.size()!=std::size_t(teams) || entities.buildingRanges.size()!=std::size_t(teams))
            throw std::logic_error("snapshot verification: entity lookup sizes differ from the game");
        if (entities.unitCargo.entries() != game.unitCargo.entries())
            throw std::logic_error("snapshot verification: unit cargo differs from the game");

        // Walk the same team/population order as capture, checking completeness as well
        // as values. Resolving only captured identities would miss omitted rows.
        std::size_t unitIndex = 0, buildingIndex = 0, relationshipIndex = 0, productionIndex = 0;
        const auto compareRelationships = [&](UnitRange range, const auto& live) {
            if (range.offset != relationshipIndex || range.count != live.size()
                || relationshipIndex > entities.relationships.size()
                || live.size() > entities.relationships.size() - relationshipIndex)
                throw std::logic_error("snapshot verification: building relationship range differs from the game");
            for (const auto* unit : live)
                if (entities.relationships[relationshipIndex++] != Game::refOf(unit))
                    throw std::logic_error("snapshot verification: building relationship differs from the game");
        };
        for (int t = 0; t < teams; ++t) {
            const auto* team = game.teams[t];
            if (!team) continue;
            if (!team->liveUnits.matches(team->myUnits, Unit::MAX_COUNT)
                || !team->liveBuildings.matches(team->myBuildings, Building::MAX_COUNT))
                throw std::logic_error("snapshot verification: team " + std::to_string(t) + " live entity lists differ from its slots");
            // Deep diagnostic only: audit empty ID lookup entries too.
            for (int slot = 0; slot < Unit::MAX_COUNT; ++slot)
                if (!team->myUnits[slot] && entities.unitSlotIndices[t * Unit::MAX_COUNT + slot] != Entities::NoRecord)
                    throw std::logic_error("snapshot verification: empty unit lookup differs from the game");
            if (entities.unitRanges[t].offset != unitIndex || entities.unitRanges[t].count != team->liveUnits.size())
                throw std::logic_error("snapshot verification: unit team range differs from the game");
            for (const auto* live : team->liveUnits.entries()) {
                const auto lookup = entities.unitSlotIndices[live->gid];
                if (unitIndex >= entities.units.size() || lookup != unitIndex)
                    throw std::logic_error("snapshot verification: unit count or lookup differs from the game");
                const auto& unit = entities.units[unitIndex++];
                if (unit.identity != Game::refOf(live) || unit.team != t)
                    throw std::logic_error("snapshot verification: unit identity or order differs from the game");
                if (static_cast<const UnitState&>(unit) != static_cast<const UnitState&>(*live))
                    throw std::logic_error("snapshot verification: unit state differs from the game");
                if (unit.realAttackStrength != live->getRealAttackStrength())
                    throw std::logic_error("snapshot verification: unit attack strength differs from the game");
                const bool hasClaim=live->previousClearingArea && game.map.isClearingAreaClaimed(
                    live->previousClearingArea->x,live->previousClearingArea->y,t)==live->gid;
                if (unit.hasClearingClaim!=hasClaim || (hasClaim &&
                    (unit.clearingClaimX!=live->previousClearingArea->x || unit.clearingClaimY!=live->previousClearingArea->y
                     || unit.clearingClaimDistance!=live->previousClearingAreaDistance)))
                    throw std::logic_error("snapshot verification: unit clearing claim differs from the game");
                if (unit.attached != Game::refOf(live->attachedBuilding) || unit.target != Game::refOf(live->targetBuilding))
                    throw std::logic_error("snapshot verification: unit building references differ from the game");
            }
            // Deep diagnostic only: audit empty ID lookup entries too.
            for (int slot = 0; slot < Building::MAX_COUNT; ++slot)
                if (!team->myBuildings[slot] && entities.buildingSlotIndices[t * Building::MAX_COUNT + slot] != Entities::NoRecord)
                    throw std::logic_error("snapshot verification: empty building lookup differs from the game");
            if (entities.buildingRanges[t].offset != buildingIndex || entities.buildingRanges[t].count != team->liveBuildings.size())
                throw std::logic_error("snapshot verification: building team range differs from the game");
            for (const auto* live : team->liveBuildings.entries()) {
                const auto lookup = entities.buildingSlotIndices[live->gid];
                if (buildingIndex >= entities.buildings.size() || lookup != buildingIndex)
                    throw std::logic_error("snapshot verification: building count or lookup differs from the game");
                const auto& building = entities.buildings[buildingIndex++];
                if (building.identity != Game::refOf(live) || building.team != t)
                    throw std::logic_error("snapshot verification: building identity or order differs from the game");
                if (static_cast<const BuildingStateRecord&>(building) != static_cast<const BuildingStateRecord&>(*live))
                    throw std::logic_error("snapshot verification: building state differs from the game");
                MaterialMask supplyMask = 0;
                for (unsigned supplied = live->runtime->suppliesStockMask | live->runtime->suppliesDirectStockMask; supplied; supplied &= supplied - 1) {
                    const unsigned material = std::countr_zero(supplied);
                    if (live->availableMaterial(material) > 0) supplyMask |= MaterialMask(1u << material);
                }
                if (building.maxHp != live->getEffectiveMaxHp()
                    || building.usesTeamResources != (live->usesTeamMaterials())
                    || building.availableSupplyMask != supplyMask)
                    throw std::logic_error("snapshot verification: building query values differ from the game");
                static const FundingState noFunding{};
                const auto& funding = building.fundingOffset == Entities::NoRecord ? noFunding : entities.funding.at(building.fundingOffset);
                if (funding.constructionBudget != live->constructionBudget || funding.constructionReserved != live->constructionReserved
                    || funding.reservedMaterials != live->reservedMaterials)
                    throw std::logic_error("snapshot verification: funding differs from the game");
                compareRelationships(building.working, live->unitsWorking);
                compareRelationships(building.inside, live->unitsInside);
                if (building.extraProductionOffset != productionIndex
                    || building.extraProductionCount != live->extraProductionRatios.size()
                    || productionIndex > entities.extraProduction.size()
                    || building.extraProductionCount > entities.extraProduction.size() - productionIndex)
                    throw std::logic_error("snapshot verification: production range differs from the game");
                for (unsigned id = 0; id < BuiltinUnitCount; ++id)
                    if (building.constructionOriginRatios[id] != live->originProductionRatio(id))
                        throw std::logic_error("snapshot verification: production origin differs from the game");
                for (unsigned extra = 0; extra < building.extraProductionCount; ++extra) {
                    const auto& value = entities.extraProduction[productionIndex++];
                    const unsigned id = BuiltinUnitCount + extra;
                    if (value.ratio != live->productionRatio(id) || value.used != live->productionUsed(id)
                        || value.constructionOriginRatio != live->originProductionRatio(id))
                        throw std::logic_error("snapshot verification: additional production differs from the game");
                }
            }
        }
        if (unitIndex != entities.units.size() || buildingIndex != entities.buildings.size()
            || relationshipIndex != entities.relationships.size() || productionIndex != entities.extraProduction.size())
            throw std::logic_error("snapshot verification: entity or sidecar counts differ from the game");
        if (entities.projects.size() != game.buildProjects.size())
            throw std::logic_error("snapshot verification: building project count differs from the game");
        std::size_t projectIndex = 0;
        for (const auto& live : game.buildProjects) {
            const auto& project = entities.projects[projectIndex++];
            if (project.posX != live.posX || project.posY != live.posY || project.teamNumber != live.teamNumber
                || project.typeNum != live.typeNum || project.unitWorking != live.unitWorking || project.unitWorkingFuture != live.unitWorkingFuture)
                throw std::logic_error("snapshot verification: building project differs from the game");
        }
    }
}
} // namespace SimulationSnapshot
