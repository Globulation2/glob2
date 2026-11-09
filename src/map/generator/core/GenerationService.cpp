// SPDX-License-Identifier: GPL-3.0-or-later
#include "ParallelFor.h"
#include <PerformanceTelemetry.h>
#include "Map.h"
#include "GenerationService.h"
#include "Building.h"
#include "BuildingType.h"
#include "BuildingUtils.h"
#include "Game.h"
#include "GenerationContext.h"
#include "GenerationValidation.h"
#include "GenerationWork.h"
#include "GenerationWorld.h"
#include "IntBuildingType.h"
#include "StartQuality.h"
#include "Team.h"
#include "Unit.h"
#include "UnitConsts.h"
#include "UnitUtils.h"
#include "Utilities.h"
#include <algorithm>
#include <chrono>
#include <limits>
#include <mutex>
#include <string>
std::string GenerationResult::diagnostic() const
{
	return generatorId + " revision " + std::to_string(revision) + " seed " + std::to_string(seed) +
		   " [" + stage + "]: " + detail;
}
GenerationResult GenerationService::generate(Game &game, const GenerationRequest &request,
											 bool collectTelemetry) const
{
	PERF_SCOPE_TIME(Generation);
	GenerationResult result;
	GenerationContext context(request, collectTelemetry);
	const auto finish = [&]()
	{
		if (result.error != GenerationError::None && context.telemetry.enabled())
			context.telemetry.error("generation.failure", result.stage + ": " + result.detail);
		result.telemetry = std::move(context.telemetry);
		return std::move(result);
	};
	result.seed = request.seed;
	result.stage = "validation";
	const auto *definition =
		(useRequestCatalog && request.catalog ? *request.catalog : registry).find(request.method);
	if (!definition)
	{
		result.error = GenerationError::InvalidRequest;
		result.detail = "Unknown generator";
		return finish();
	}
	result.generatorId = definition->id;
	result.revision = definition->revision;
	result.packageHash = definition->packageHash;
	result.apiVersion = definition->apiVersion;
	try
	{
		auto contract=*definition;
        contract.validateRequest={};
        result.detail=validateGenerationRequest(request,contract);
        if(result.detail.empty() && definition->validateRequest)result.detail=definition->validateRequest(request);
	}
	catch (const ScriptGenerationFailure &error)
	{
		result.error = error.error;
		result.detail = error.what();
		return finish();
	}
	if (!result.detail.empty())
	{
		result.error = GenerationError::InvalidRequest;
		return finish();
	}
	if (game.mapHeader.getNumberOfTeams() != 0)
	{
		result.error = GenerationError::NonEmptyTarget;
		result.detail = "Generation requires a fresh Game";
		return finish();
	}
	game.gameHeader.setRandomSeed(request.seed);
	game.map.worldRandom.initialize(request.seed);
	game.map.setSize(request.wDec, request.hDec);
	// The drawn terrain look follows the request seed too, without touching the
	// simulation streams, so a regenerated map is identical on every client.
	game.map.setTerrainSeed(GenerationContext::deriveSeed(request.seed, "terrain-look"));
	game.map.setGame(&game);
	GenerationWorld world(game);
	bool generated = false;
	try
	{
		generated = definition->generate(world, context);
	}
	catch (const ScriptGenerationFailure &error)
	{
		result.error = error.error;
		result.detail = error.what();
		result.stage = context.stage;
		return finish();
	}
	catch (const GenerationFailure &error)
	{
		context.detail = error.what();
	}
	if (!generated)
	{
		result.error = GenerationError::PlacementFailed;
		result.stage = context.stage;
		result.detail =
			context.detail.empty() ? "Could not place the requested map layout" : context.detail;
		return finish();
	}
	result.stage = "validation";
	result.detail = validateGeneratedWorld(world, request, *definition);
	if (!result.detail.empty())
	{
		result.error = GenerationError::InvalidWorld;
		return finish();
	}
	if (definition->validateWorld)
	{
		result.stage = "generator validation";
		try
		{
			result.detail = definition->validateWorld(world, context);
		}
		catch (const ScriptGenerationFailure &error)
		{
			result.error = error.error;
			result.detail = error.what();
			return finish();
		}
		if (!result.detail.empty())
		{
			result.error = GenerationError::InvalidWorld;
			return finish();
		}
	}
	result.quality = MapGeneration::scoreStarts(world, request.nbTeams);
	result.stage = "script";
	const auto script = game.sgslScript.compileScript(&game);
	if (script.type != ErrorReport::ET_OK)
	{
		result.error = GenerationError::InvalidWorld;
		result.detail = script.getErrorString();
		return finish();
	}
	result.stage = "complete";
	return finish();
}

std::uint32_t GenerationService::bestSeed(const GenerationRequest &request, std::uint32_t rootSeed,
										  int candidates,
										  std::vector<CandidateRoll> *rolls) const
{
	// Candidates are independent games: generate them in parallel, then choose
	// exactly as a serial search would, the first highest score in attempt order.
	const int count = std::max(1, candidates);
	std::vector<CandidateRoll> rolled(count);
	ParallelFor::run(std::size_t(count), [&](std::size_t attempt)
	{
		// A Game is too large for a worker's default stack on macOS.
		auto game = std::make_unique<Game>(nullptr);
		GenerationRequest roll = request;
		roll.seed = GenerationContext::deriveSeed(rootSeed, "attempt/" + std::to_string(attempt));
		const auto start = std::chrono::steady_clock::now();
		const auto result = generate(*game, roll);
		const double seconds =
			std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
		rolled[attempt] = {roll.seed, bool(result), result ? result.quality.score : 0.0, seconds};
	});
	std::uint32_t chosen = GenerationContext::deriveSeed(rootSeed, "attempt/0");
	double bestScore = -1.0;
	for (const auto &roll : rolled)
	{
		if (rolls)
			rolls->push_back(roll);
		if (!roll.generated || roll.score <= bestScore)
			continue;
		bestScore = roll.score;
		chosen = roll.seed;
	}
	return chosen;
}

// === GenerationWorld.h ===
// The one place generator calls reach the real Map and Game. Each member forwards to the Map or
// Game member of the same meaning, unchanged, so a generator on GenerationWorld.h runs the same
// code it did when it called Map and Game directly.
GenerationMap::GenerationMap(Map &target)
	: map(&target), view(&target.liveView()), vertices(&target.vertexTerrainArray())
{
}
GenerationMap::GenerationMap(const Map &target) : GenerationMap(const_cast<Map &>(target)) {}

Sint32 GenerationMap::warpDistSquare(int px, int py, int qx, int qy) const
{
	return map->warpDistSquare(px, py, qx, qy);
}
void GenerationMap::setSize(int wDec, int hDec)
{
	map->setSize(wDec, hDec);
}
Uint32 GenerationMap::terrainSeed() const
{
	return map->terrainSeed();
}
void GenerationMap::setVertexTerrain(size_t index, TerrainType type)
{
	map->setVertexTerrain(index, type);
}
void GenerationMap::assignVertexTerrain(std::span<const TerrainType> terrain)
{
	map->assignVertexTerrain(terrain);
}
void GenerationMap::fillTerrain(TerrainType type)
{
	map->fillTerrain(type);
}
std::vector<size_t> GenerationMap::paintVertices(const std::vector<std::pair<int, int>> &painted,
												 TerrainType type, bool beaches)
{
	return map->paintVertices(painted, type, beaches);
}
void GenerationMap::paintVertexSquare(int x, int y, TerrainType type, int l)
{
	map->paintVertexSquare(x, y, type, l);
}
void GenerationMap::layBeaches()
{
	map->layBeaches();
}

struct GenerationMap::TerrainEditBatch::Hold
{
	Map::TerrainEditBatch batch;
	explicit Hold(Map &target) : batch(target) {}
};
GenerationMap::TerrainEditBatch::TerrainEditBatch(Map &target)
	: hold(std::make_unique<Hold>(target))
{
}
GenerationMap::TerrainEditBatch::~TerrainEditBatch() = default;

std::uint64_t GenerationMap::materialRenewalPotentialAtSlot(size_t index, int material) const
{
	return map->materialRenewalPotentialAtSlot(index, material);
}
std::uint64_t GenerationMap::materialExpansionRateAtSlot(size_t index, int material) const
{
	return map->materialExpansionRateAtSlot(index, material);
}
MapState::View GenerationMap::stateView() const
{
	return map->stateView();
}
bool GenerationMap::growthCacheValid() const
{
	std::lock_guard lock(map->growthCacheMutex);
	return map->growthCache.validFor(*map);
}
void GenerationMap::setResource(int x, int y, ResourceId type, int l)
{
	map->setResource(x, y, type, l);
}
void GenerationMap::setNoResource(int x, int y, int l)
{
	map->setNoResource(x, y, l);
}
void GenerationMap::replaceResource(size_t index, const Resource &resource,
									const std::array<Uint16, MaterialCount> *stocks)
{
	map->replaceResource(index, resource, stocks);
}
void GenerationMap::setResourceAmount(size_t index, Uint32 amount)
{
	map->setResourceAmount(index, amount);
}
void GenerationMap::setResourcesGrow(int x, int y, Uint8 value)
{
	map->setResourcesGrow(x, y, value);
}
void GenerationMap::setFertility(int x, int y, Uint16 value)
{
	map->setFertility(x, y, value);
}
void GenerationMap::growResources()
{
	map->growResources();
}
void GenerationMap::smoothResources(int times)
{
	map->smoothResources(times);
}
void GenerationMap::storeFertility(const Fertility::Field &fertility)
{
	// Same clamp FertilityCalculator uses: kScale's own ceiling (an all-water neighbourhood) is
	// one past what a Uint16 holds.
	constexpr std::uint32_t kFertilityCeiling = std::numeric_limits<Uint16>::max();
	const int w = getW(), h = getH();
	Uint16 fertilityMax = 0;
	for (int y = 0; y < h; ++y)
	{
		::MapGeneration::generationCheckpoint();
		for (int x = 0; x < w; ++x)
		{
			::MapGeneration::generationCheckpoint();
			const Uint16 value =
				static_cast<Uint16>(std::min(fertility.at(x, y), kFertilityCeiling));
			map->setFertility(x, y, value);
			fertilityMax = std::max(fertilityMax, value);
		}
	}
	map->fertilityMaximum = fertilityMax;
}
Fertility::Field Fertility::forMap(const GenerationMap &map, bool gateOnReachableDeposits)
{
	return Fertility::forMap(static_cast<const Map &>(map.target()), gateOnReachableDeposits);
}
bool GenerationMap::isFreeForBuilding(int x, int y) const
{
	return map->isFreeForBuilding(x, y);
}
bool GenerationMap::isFreeForBuilding(int x, int y, int w, int h) const
{
	return map->isFreeForBuilding(x, y, w, h);
}
bool GenerationMap::isHardSpaceForBuilding(int x, int y) const
{
	return map->isHardSpaceForBuilding(x, y);
}
bool GenerationMap::isHardSpaceForBuilding(int x, int y, int w, int h) const
{
	return map->isHardSpaceForBuilding(x, y, w, h);
}
bool GenerationMap::isFreeForGroundUnit(int x, int y, bool canSwim, Uint32 teamMask) const
{
	return map->isFreeForGroundUnit(x, y, canSwim, teamMask);
}
bool GenerationMap::isHardSpaceForGroundUnit(int x, int y, bool canSwim, Uint32 me) const
{
	return map->isHardSpaceForGroundUnit(x, y, canSwim, me);
}
void GenerationMap::setBuilding(int x, int y, int w, int h, Uint16 gbid)
{
	map->setBuilding(x, y, w, h, gbid);
}
void GenerationMap::setGroundUnit(int x, int y, Uint16 guid)
{
	map->setGroundUnit(x, y, guid);
}

namespace
{
GenerationBuildingType describeType(BuildingType *type, int typeNum)
{
	GenerationBuildingType result;
	if (!type)
		return result;
	result.handle = type;
	result.typeNum = typeNum;
	result.width = type->width;
	result.height = type->height;
	result.maxUnitWorking = type->maxUnitWorking;
	result.assignmentLimit = type->semantics.assignmentLimit;
	result.shootingRange = type->shootingRange;
	result.maxBullets = type->maxBullets;
	for (int material = 0; material < MaterialSlotCount; ++material)
		result.maxMaterial[size_t(material)] = type->maxMaterial[material];
	result.runtimeAvailable = type->runtimeAvailable;
	result.occupiesGround = type->semantics.occupiesGround;
	result.isBuildingSite = type->isBuildingSite != 0;
	result.type = type->type;
	return result;
}
GenerationBuilding describeBuilding(Building *building)
{
	GenerationBuilding result;
	if (!building)
		return result;
	result.handle = building;
	result.typeNum = building->typeNum;
	result.posX = building->posX;
	result.posY = building->posY;
	result.type = describeType(building->type, building->typeNum);
	return result;
}
} // namespace

GenerationWorld::GenerationWorld(Game &target) : map(target.map), game(&target) {}
GenerationWorld::GenerationWorld(const Game &target) : GenerationWorld(const_cast<Game &>(target))
{
}
int GenerationWorld::teamsCount() const
{
	return game->teamsCount();
}
void GenerationWorld::addTeam()
{
	game->addTeam();
}
bool GenerationWorld::hasTeam(int team) const
{
	return team >= 0 && team < game->teamsCount() && game->teams[team];
}
GenerationTeamStart GenerationWorld::teamStart(int team) const
{
	const Team *t = game->teams[team];
	return {t->startPosX, t->startPosY, t->startPosSet};
}
void GenerationWorld::setTeamStart(int team, int x, int y)
{
	game->teams[team]->startPosX = x;
	game->teams[team]->startPosY = y;
}
void GenerationWorld::setTeamStart(int team, int x, int y, int source)
{
	setTeamStart(team, x, y);
	game->teams[team]->startPosSet = source;
}
void GenerationWorld::finishTeam(int team)
{
	game->teams[team]->createLists();
}
int GenerationWorld::resourceScarcityLevel() const
{
	return int(game->gameHeader.getResourceScarcityLevel());
}
GenerationBuildingType GenerationWorld::startingBuildingType() const
{
	return buildingType(game->buildingsTypes.getStartingBuildingTypeNum());
}
GenerationBuildingType GenerationWorld::buildingType(const std::string &name, int level,
													 bool isBuildingSite) const
{
	return buildingType(game->buildingsTypes.getTypeNum(name, level, isBuildingSite));
}
GenerationBuildingType GenerationWorld::buildingType(int typeNum) const
{
	GenerationBuildingType result =
		describeType(typeNum >= 0 ? game->buildingsTypes.get(typeNum) : nullptr, typeNum);
	result.typeNum = typeNum;
	return result;
}
bool GenerationWorld::isBuildingTypeAvailable(int typeNum) const
{
	return game->isBuildingTypeAvailable(typeNum);
}
GenerationBuildingType GenerationWorld::legacyBuildingType(int shortType, int level,
														   bool isBuildingSite) const
{
	return buildingType(IntBuildingType::typeFromShortNumber(shortType), level, isBuildingSite);
}
bool GenerationWorld::checkRoomForBuilding(int x, int y, const BuildingType *type, int team) const
{
	return game->checkRoomForBuilding(x, y, type, team, false);
}
GenerationBuilding GenerationWorld::describe(Building *building)
{
	return describeBuilding(building);
}
GenerationBuilding GenerationWorld::addBuilding(int x, int y, int typeNum, int team,
												Sint32 unitWorking, Sint32 unitWorkingFuture)
{
	return describeBuilding(game->addBuilding(x, y, typeNum, team, unitWorking, unitWorkingFuture));
}
bool GenerationWorld::addWorker(int x, int y, int team)
{
	return game->addUnit(x, y, team, GenerationUnit::Worker, 0, 0, 0, 0) != nullptr;
}
void GenerationWorld::registerBuilding(int team, const GenerationBuilding &building)
{
	game->teams[team]->addToStaticAbilitiesLists(building.handle);
}
void GenerationWorld::setBuildingBullets(const GenerationBuilding &building, int bullets)
{
	building.handle->bullets = bullets;
}
void GenerationWorld::setBuildingMaterial(const GenerationBuilding &building, int material,
										  int amount)
{
	building.handle->setMaterial(material, amount);
}
void GenerationWorld::updateBuildingCallLists(const GenerationBuilding &building)
{
	building.handle->updateCallLists();
}
void GenerationWorld::updateBuilding(const GenerationBuilding &building)
{
	building.handle->update();
}
std::vector<GenerationBuilding> GenerationWorld::teamBuildings(int team) const
{
	std::vector<GenerationBuilding> result;
	for (Building *building : game->teams[team]->liveBuildings.entries())
		if (building)
			result.push_back(describeBuilding(building));
	return result;
}
std::vector<GenerationUnit> GenerationWorld::teamUnits(int team) const
{
	std::vector<GenerationUnit> result;
	for (const Unit *unit : game->teams[team]->liveUnits.entries())
		if (unit)
			result.push_back({unit->typeNum, unit->posX, unit->posY});
	return result;
}
int GenerationWorld::buildingTypeNumAt(Uint16 gbid) const
{
	return game->teams[BuildingUtils::GIDtoTeam(gbid)]
		->myBuildings[BuildingUtils::GIDtoID(gbid)]
		->typeNum;
}
int GenerationWorld::unitTypeNumAt(Uint16 guid) const
{
	return game->teams[UnitUtils::GIDtoTeam(guid)]->myUnits[UnitUtils::GIDtoID(guid)]->typeNum;
}
static_assert(GenerationWorld::EntitiesPerTeam == BuildingUtils::MAX_COUNT);
static_assert(GenerationWorld::EntitiesPerTeam == UnitUtils::MAX_COUNT);
static_assert(GenerationUnit::Worker == WORKER);
void GenerationWorld::attachMap()
{
	game->map.setGame(game);
}
void GenerationWorld::seedRandom(std::uint32_t seed)
{
	game->gameHeader.setRandomSeed(seed);
	game->map.worldRandom.initialize(seed);
}

GenerationScratchWorld::GenerationScratchWorld()
	: game(std::make_unique<Game>(nullptr)), view(std::make_unique<GenerationWorld>(*game))
{
}
GenerationScratchWorld::~GenerationScratchWorld() = default;
