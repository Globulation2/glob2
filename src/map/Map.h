// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière
// Copyright (C) 2006 Bradley Arsenault

#pragma once
#include <CooperativeTask.h>
#include "ComputeExecutor.h"
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <list>
#include <memory>
#include <mutex>
#include <optional>
#include <vector>
#include <assert.h>

#include "Building.h"
#include "Ressource.h"
#include "ResourceRegistry.h"
#include "Sector.h"
#include "Team.h"
#include "TerrainType.h"
#include "FertilityField.h"
#include "TerrainProperties.h"
#include "TerrainRegistry.h"
#include "TerrainExperiments.h"
#include "BitArray.h"

class Unit;

//! No global unit identifier. This value means there is no unit. Used at Tile::groundUnit or Tile::airUnit.
#define NOGUID 0xFFFF

//! No global building identifier. This value means there is no building. Used at Tile::building.
#define NOGBID 0xFFFF

class Map;
class Game;
class SessionGame;
class MapHeader;
struct GradientRuntime;

//! 2D grid offset returned by Map's 3x3-neighborhood "doesTouch" queries.
//! dx and dy are each in {-1, 0, +1}.
struct Offset
{
	int dx;
	int dy;
};

// a 1x1 piece of map
struct Tile
{
	Uint16 terrain = 0; // default, not really meaningful.
	Uint16 building = NOGBID;

	Resource resource;

	Uint16 groundUnit = NOGUID;
	Uint16 airUnit = NOGUID;

	Uint32 forbidden = 0; // This is a mask, one bit by team, 1=forbidden, 0=allowed
	///The difference between forbidden zone and hidden forbidden zone is that hidden forbidden zone
	///is put there by the game engine and is not draw to the screen.
	Uint32 guardArea = 0; // This is a mask, one bit by team, 1=guard area, 0=normal
	Uint32 clearArea = 0; // This is a mask, one bit by team, 1=clear area, 0=normal
	Uint32 farmArea = 0; // This is a mask, one bit by team, 1=farm area, 0=normal (farm-areas experiment)

	Uint16 scriptAreas = 0; // This is also a mask. A single bit represents an area #n, on or off for the square
	Uint8 canResourcesGrow = 1; // This is a boolean, it represents whether resources are allowed to grow into this location.
	
	Uint16 fertility = 0; // This is a value that represents the fertility of this square, the chance that wheat will grow on it
};

/// Types of areas
enum AreaType
{
	ClearingArea = 0,
	ForbiddenArea,
	GuardArea,
	FarmArea
};


/*! Map, handle all physical localisations
	All size are given in 32x32 pixel cell, which is the basic game measurement unit.
	All functions are wrap-safe, excepted the one specified otherwise.
*/
class Map
{
	friend class ResourceSeedCache;
	void resourceSeedChanged(size_t index, unsigned flags);
	void invalidateResourceSeeds();
	void seedMaterialGradientDirect(int team, Uint8 resource, int swim, Uint16 *output, const Uint16 *supplierSeeds);
	void seedMaterialGradientWithSuppliers(int team, Uint8 resource, int swim, Uint16 *output, const Building* consumer, unsigned modes);
	mutable ComputeExecutor compute;
	mutable std::unique_ptr<GradientRuntime> gradientRuntime;
	unsigned computeExperiments = 0;
	mutable std::mutex waterSnapshotMutex;
	mutable std::shared_ptr<const std::vector<Uint8>> waterSnapshot;
	mutable std::shared_ptr<const std::vector<TerrainType>> terrainSnapshot;
	mutable std::array<std::shared_ptr<const TerrainMovementSnapshot>, 7> terrainMovementSnapshots;
	std::shared_ptr<const ResourceRegistry> resourceRegistryValue = ResourceRegistry::availableDefaults();
	// Single-yield tiles keep stock inline. The index plane is allocated only
	// when a multi-yield deposit is first placed; zero means no sidecar slot.
    std::vector<Uint16> resourceHabitatProfiles;
    std::vector<Uint8> resourceHabitatPermissions;
    std::vector<MaterialMask> terrainMaterialPermissions;
    std::vector<std::shared_ptr<const std::vector<Uint64>>> terrainResourceAllowLists;
    std::vector<MaterialMask> explicitTerrainMaterialPermissions;
    std::vector<int> terrainFarmResources;
    unsigned resourceHabitatProfileCount=0;
    void rebuildResourceHabitats();
	std::vector<Uint32> resourceStockIndices;
	std::vector<std::array<Uint16, MaterialCount>> resourceStocks;
	std::vector<Uint32> freeResourceStocks;
	std::array<size_t, MaterialCount> materialSourceCounts{};
	void initializeResourceStock(size_t index);
	void releaseResourceStock(size_t index);
	void refreshResourceTotal(size_t index);
	void rebuildResourceState();
	void materialStockChanged(size_t index, MaterialMask before);
	bool harvestMaterial(size_t index, int material);
	std::vector<TerrainType> terrainIds;
	std::shared_ptr<const TerrainRegistry> terrainRegistryValue = TerrainRegistry::builtins();
	std::vector<Uint16> terrainPropertyIndices;
	const TerrainProperties *terrainPropertyTable = terrainRegistryValue->propertyProfiles().data();
	std::vector<std::size_t> terrainCounts = std::vector<std::size_t>(TERRAIN_COUNT);
	std::array<unsigned, 6> terrainFeatures{};
	unsigned terrainBucketCount = 64;
	std::array<std::array<unsigned, 121>, 7> terrainGroundCostCounts{};
	std::array<unsigned, 41> terrainAirCostCounts{};
	std::array<unsigned, 7> terrainMinimumGround = gradient_kernel::MINIMUM_TERRAIN_ENTRY_COSTS;
	unsigned terrainMinimumAir = GRADIENT_STEP;
	void adjustTerrainFeatures(TerrainType type, bool add);
	std::uint64_t terrainGenerationValue = 1;
    // Explicit availability changes of intrinsically static material sources.
    // Shared AI gradients persist only whether this generation is current.
    std::uint64_t staticMaterialSourceGenerationValue = 1;
    void bumpStaticMaterialSourceGeneration()
    {
        // Zero is reserved for a stale shared-runtime gradient after loading.
        if (++staticMaterialSourceGenerationValue == 0) ++staticMaterialSourceGenerationValue;
    }
	void changeTerrainIdentity(size_t index, TerrainType type);
	void rebuildTerrainCounts();
	unsigned terrainEditDepth = 0;
	bool terrainEditChanged = false, terrainRoutesChanged = false;
	bool terrainHealthEffects = false;
	bool terrainMovementModifiers = false, airTerrainConstraints = false, projectileBlockingTerrain = false;
	void updateTerrainSummary();
	void finishTerrainEdit();
	// Storage only: an idle building still drops its field and saved null state.
	// A fixed slot count avoids allocations in the pool itself.
	static constexpr std::size_t GRADIENT_BUFFER_POOL_SLOTS = 64;
	static constexpr std::size_t GRADIENT_BUFFER_POOL_BYTES = 2 * 1024 * 1024;
	std::array<Uint16 *, GRADIENT_BUFFER_POOL_SLOTS> idleGradientBuffers{};
	std::size_t idleGradientBufferCount = 0;
	std::mutex gradientBufferPoolMutex;
	void clearGradientBufferPool();
public:
	// Immutable terrain costs shared by independent resumed searches.
	std::shared_ptr<const std::vector<Uint8>> frozenWaterSnapshot() const;
	Uint16 *acquireBuildingGradientBuffer();
	void recycleBuildingGradientBuffer(Uint16 *buffer);
	std::uint64_t hiringPrepasses = 0, hiringPoppedEntries = 0;
	enum ComputeExperiment { ComputeAreas = 1, ComputeInitialize = 2, ComputeHiring = 4, ComputeAI = 8 };
	void configureCompute(unsigned threads, unsigned experiments);
	ComputeExecutor &computeExecutor() { return compute; }
	bool computeEnabled(ComputeExperiment experiment) const { return computeExperiments & experiment; }
	// Fixed chunks and synchronous barriers: thresholds affect execution only.
	template<class Function> void initializeGradientCells(Function function) const
	{
		constexpr size_t chunk = 4096;
		if (!computeEnabled(ComputeInitialize) || size < 16384)
		{ function(0, size); return; }
		compute.run((size + chunk - 1) / chunk, [&](size_t part) {
			const size_t begin = part * chunk;
			function(begin, std::min(begin + chunk, size));
		});
	}
	struct GradientPipelineStatus
	{
		bool enabled = false;
		unsigned workers = 0, delay = 0;
		std::size_t pending = 0;
		std::uint64_t jobs = 0, published = 0, discarded = 0;
		std::uint64_t maxPending = 0, waitNs = 0, activeElapsedNs = 0;
	};
	bool gradientPipelineEnabled() const;
	GradientPipelineStatus gradientPipelineStatus() const;
	// Owner selects/reserves before any AI work; preparation writes private job data
	// and synchronized caches only. Drain before world mutation, save or reconfigure.
	void stagePeriodicGradientPreparation();
	bool hasPendingGradientPreparation() const;
	void preparePendingGradient();
	void advanceGradientPipeline();
	void finishGradientPipeline();
	void setGradientWorkerCount(unsigned workers);
	void configureGradientPipeline(unsigned workers, unsigned delay);
	void updateTeamAreaGradients(int teamNumber);
	void seedMaterialGradient(int team, Uint8 resource, int swim, Uint16 *gradient, bool withMarkets = false, const Building* consumer = nullptr, unsigned modes = 0);
	void seedGuardAreasGradient(int team, int swim, Uint16 *gradient);
	void seedClearAreasGradient(int team, int swim, Uint16 *gradient);
	void advanceHiringGradients(Building *building);

	void saveRuntimeState(GAGCore::OutputStream *stream) const;
	void loadRuntimeState(GAGCore::InputStream *stream, Sint32 versionMinor);
	//! Type of terrain (used for undermap)

	// === Tile geometry (cross-slice) ===
	//! Bit-shift converting a tile index to its top-left pixel coordinate
	//! (i.e. log2 of TILE_PX). Used by mapCaseToPixelCase and friends in
	//! MapView.cpp / TypeSteps.cpp.
	static constexpr int TILE_PIXEL_SHIFT = 5;
	//! Side length of one map tile in screen pixels (1 << TILE_PIXEL_SHIFT).
	static constexpr int TILE_PX = 32;
	//! Half-tile in pixels — used when centring sprites / bullets on a tile.
	static constexpr int HALF_TILE_PX = 16;

	//! Sentinel returned by Map::getTerrainType when the underlying terrain
	//! sprite ID does not fall in any of the registered terrain ranges
	//! (GRASS / SAND / WATER). Callers test for `< 0` / `== TERRAIN_TYPE_UNKNOWN`.
	static constexpr int TERRAIN_TYPE_UNKNOWN = -1;

	//! "Infinity" / "unvisited" sentinel for the A* algorithm's Uint16 cost fields
	//! (moveCost, totalCost). All real costs fit within 0..0xFFFE so 0xFFFF is safe.
	static constexpr Uint32 ASTAR_COST_INFINITY = static_cast<Uint32>(-1);

public:
	static constexpr int MIN_SUPPORTED_SIZE_EXPONENT = 4;
	static constexpr int MAX_SUPPORTED_SIZE_EXPONENT = 9;
	static constexpr bool supportedDimensions(int widthExponent, int heightExponent)
	{
		return widthExponent >= MIN_SUPPORTED_SIZE_EXPONENT &&
			widthExponent <= MAX_SUPPORTED_SIZE_EXPONENT &&
			heightExponent >= MIN_SUPPORTED_SIZE_EXPONENT &&
			heightExponent <= MAX_SUPPORTED_SIZE_EXPONENT;
	}

	//! Map constructor
	Map();
	//! Map destructor
	virtual ~Map(void);
	//! Reset map and free arrays
	void clear();

	//! Reset map size to width = 2^wDec and height=2^hDec, and fill background with terrainType
	void setSize(int wDec, int hDec, TerrainType terrainType=WATER);
	//! Repeat the terrain, resources and discovery rx by ry times; units, buildings and team zones are dropped
	void tile(int rx, int ry);
	// !This call is needed to use the Map!
	void setGame(Game *game);
	//! Load a map from a stream and relink with associated game
	bool load(GAGCore::InputStream *stream, MapHeader& header, Game *game=NULL);
    GAGCore::CooperativeTask loadTask(GAGCore::InputStream *stream, MapHeader& header, Game *game);
	//! Save a map
	void save(GAGCore::OutputStream *stream);
	//! Write the per-team explored area. Saved games only; save() decides.
	void saveExploredArea(GAGCore::OutputStream *stream, int numberOfTeams);
	//! Read the section written by saveExploredArea into freshly allocated
	//! exploredArea arrays. With keep=false the data is consumed and dropped,
	//! for loads that have no game to attach it to.
	void loadExploredArea(GAGCore::InputStream *stream, int numberOfTeams, bool keep, int versionMinor);
	
	// add & remove teams, used by the map editor and the random map generator
	// Have to be called *after* session.numberOfTeam has been changed.
	void addTeam(void);
    GAGCore::CooperativeTask addTeamTask(void);
	void removeTeam(void);

	//! Grow resources on map
	void growResources(void);
	void recordNaturalGrowth(int x, int y, int resourceType, int oldType, int oldAmount);
    void recordNaturalGrowth(int x,int y,int resourceType,int oldType,const std::array<Uint16,MaterialCount>& oldStocks);
	void rebuildGrowthCoverage();
	//! Mask of teams with a non-flag building within GROWTH_COVERAGE_RADII[band] of
	//! (x, y), as of each team's last 512-tick building snapshot. Call
	//! rebuildGrowthCoverage() first; it repaints only what changed since.
	Uint32 teamsWithBuildingsNear(int x, int y, int band) const
	{
		return Uint32(growthCoverage[coordToIndex(x, y)] >> (band * Team::MAX_COUNT)) &
			((Uint32(1) << Team::MAX_COUNT) - 1);
	}
	//! Do a step associated with map (grow resources and process bullets)
	// Standalone map callers retain synchronous periodic preparation. Game defers
	// selection until its entire tick (including scripts/fog/projects) is complete.
	void syncStep(Uint32 stepCounter, bool preparePeriodic = true);
	//! Switch the Fog of War bufferResourceType
	void switchFogOfWar(void);

	//! Return map width
	int getW(void) const { return w; }
	//! Return map height
	int getH(void) const { return h; }
	//! Return map width mask
	int getMaskW(void) const { return wMask; }
	//! Return map height maskint
	int getMaskH(void) const { return hMask; }
	//! Return map width shift 
	int getShiftW(void) const { return wDec; }
	//! Return map height shift
	int getShiftH(void) const { return hDec; }
	//! Return the number of sectors on x, which corresponds to the sector map width
	int getSectorW(void) const { return wSector; }
	//! Return the number of sectors on y, which corresponds to the sector map height
	int getSectorH(void) const { return hSector; }

	/// Return an index into map arrays for a given position, wrap-safe
	inline size_t coordToIndex(int x, int y) const {
		return ((y & hMask) << wDec) + (x & wMask);
	}

	///Returns a normalized version of the x coordinate, taking into account that x coordinates wrap around
	int normalizeX(int x) const
	{
		return x & wMask;
	}
	
	///Returns a normalized version of the y coordinate, taking into account that y coordinates wrap around
	int normalizeY(int y) const
	{
		return y & hMask;
	}

	//! Set map to discovered state at position (x, y) for all teams in sharedVision (mask).
	void setMapDiscovered(int x, int y, Uint32 sharedVision);

	//! Set map to discovered state at rect (x, y, w, h) for all teams in sharedVision (mask).
	void setMapDiscovered(int x, int y, int w, int h,  Uint32 sharedVision);

	//! Make the building at (x, y) visible for all teams in sharedVision (mask).
	void setMapBuildingsDiscovered(int x, int y, Uint32 sharedVision, Team *teams[Team::MAX_COUNT]);

	//! Make the building at rect (x, y, w, h) visible for all teams in sharedVision (mask).
	void setMapBuildingsDiscovered(int x, int y, int w, int h, Uint32 sharedVision, Team *teams[Team::MAX_COUNT]);
	
	//! Make the map at rect (x, y, w, h) explored by unit, i.e. to 255
	void setMapExploredByUnit(int x, int y, int w, int h, int team);
	
	//! Make the map at rect (x, y, w, h) explored by building, i.e. to minimum 2
	void setMapExploredByBuilding(int x, int y, int w, int h, int team);

	//! Set all map for all teams to undiscovered state
	void unsetMapDiscovered(void);

	//! Returns true if map is discovered at position (x,y) for a given vision mask.
	//! This mask represents which team's part of map we are allowed to see.
	bool isMapDiscovered(int x, int y, Uint32 visionMask) const
	{
		return ((mapDiscovered[coordToIndex(x, y)]) & visionMask) != 0;
	}
	
	//! Returns true if map is discovered at position (x1..x2,y1..y2) for a given vision mask.
	//! This mask represents which team's part of map we are allowed to see.
	bool isMapPartiallyDiscovered(int x1, int y1, int x2, int y2, Uint32 visionMask) const;

	//! Sets all map for all teams to discovered state
	void setMapDiscovered(void);

	//! Returns true if map is currently discovered at position (x,y) for a given vision mask.
	//! This mask represents which team's units and buildings we are allowed to see.
	bool isFOWDiscovered(int x, int y, int visionMask) const
	{
		return ((fogOfWar[coordToIndex(x, y)]) & visionMask) != 0;
	}
	
	//! Return true if (x,y) is forbidden in the locally-displayed team's overlay cache
	//! (i.e. the team computeDisplayedForbidden was last refreshed for). Render-only.
	bool isForbiddenInDisplayedView(int x, int y) const
	{
		return displayedForbiddenView.get(coordToIndex(x, y));
	}
	
	//! Returns true if the position(x, y) is a forbidden area for the given team
	bool isForbidden(int x, int y, Uint32 teamMask) const
	{
		return tiles[coordToIndex(x, y)].forbidden&teamMask;
	}

	//! Return true if (x,y) is a guard area in the locally-displayed team's overlay cache
	//! (i.e. the team computeDisplayedGuardArea was last refreshed for). Render-only.
	bool isGuardAreaInDisplayedView(int x, int y) const
	{
		return displayedGuardAreaView.get(coordToIndex(x, y));
	}
	
	//! Returns true if the position(x, y) is a guard area for the given team
	bool isGuardArea(int x, int y, Uint32 teamMask) const
	{
		return tiles[coordToIndex(x, y)].guardArea&teamMask;
	}

	//! Return true if (x,y) is a clear area in the locally-displayed team's overlay cache
	//! (i.e. the team computeDisplayedClearArea was last refreshed for). Render-only.
	bool isClearAreaInDisplayedView(int x, int y) const
	{
		return displayedClearAreaView.get(coordToIndex(x, y));
	}

	//! Returns true if the position(x, y) is a clear area for the given team
	bool isClearArea(int x, int y, Uint32 teamMask) const
	{
		return tiles[coordToIndex(x, y)].clearArea&teamMask;
	}

	//! Return true if (x,y) is a farm area in the locally-displayed team's overlay cache
	//! (i.e. the team computeDisplayedFarmArea was last refreshed for). Render-only.
	bool isFarmAreaInDisplayedView(int x, int y) const
	{
		return displayedFarmAreaView.get(coordToIndex(x, y));
	}

	//! Returns true if the position(x, y) is a farm area for the given team
	bool isFarmArea(int x, int y, Uint32 teamMask) const
	{
		return tiles[coordToIndex(x, y)].farmArea&teamMask;
	}
	
	// These rebuild the render-only displayed*View caches from the authoritative
	// tiles[] bits, and are only meaningful for the locally-displayed team (the one
	// whose areas are drawn on screen). They do not touch checkSum() state.
	//! Rebuild displayedForbiddenView from tiles[].forbidden for the given team.
	void computeDisplayedForbidden(int teamNumber);
	//! Rebuild displayedGuardAreaView from tiles[].guardArea for the given team.
	void computeDisplayedGuardArea(int teamNumber);
	//! Rebuild displayedClearAreaView from tiles[].clearArea for the given team.
	void computeDisplayedClearArea(int teamNumber);
	//! Rebuild displayedFarmAreaView from tiles[].farmArea for the given team.
	void computeDisplayedFarmArea(int teamNumber);

	//! Sentinel for "no displayed team yet" — used before GameGUI::adjustLocalTeam has run.
	static constexpr Sint32 NO_DISPLAYED_TEAM = -1;
	//! Register the team whose view is currently displayed. Used only to decide whether
	//! to refresh the displayedForbiddenView / displayedGuardAreaView / displayedClearAreaView
	//! caches. This identity is per-client display state — not in checkSum() — and must be
	//! kept in sync with GameGUI's localTeamNo. Never branch sim/network state on it.
	void setDisplayedTeam(Sint32 teamNo) { displayedTeam = teamNo; }
	Sint32 getDisplayedTeam() const { return displayedTeam; }
	
	//! Return the const tile at a given position
	inline const Tile &getTile(int x, int y) const
	{
		return tiles[coordToIndex(x, y)];
	}

	//! Return the terrain for a given coordinate
	inline Uint16 getTerrain(int x, int y) const
	{
		return tiles[coordToIndex(x, y)].terrain;
	}
	
	//! Return the terrain for a given position in tile array
	inline Uint16 getTerrain(size_t pos) const
	{
		return tiles[pos].terrain;
	}

	//! Canonical gameplay identity; never inferred from art in a simulation query.
	const TerrainRegistry &terrainRegistry() const { return *terrainRegistryValue; }
	std::shared_ptr<const TerrainRegistry> frozenTerrainRegistry() const
	{
		return terrainRegistryValue;
	}
	unsigned terrainQueueBuckets() const { return terrainBucketCount; }
	const TerrainProperties &terrainProperties(TerrainType type) const
	{
		return terrainRegistryValue->properties(type);
	}
	const TerrainPresentation &terrainPresentation(TerrainType type) const
	{
		return terrainRegistryValue->presentation(type);
	}
	bool validTerrainType(unsigned type) const { return terrainRegistryValue->valid(type); }
	bool terrainUsesLegacyCorners(TerrainType type) const
	{
		return terrainRegistry().compatibility(type).legacyCorners;
	}
	void importTerrainDefinitions(std::string_view json);
	TerrainType terrainTypeAt(size_t index) const { return terrainIds[index]; }
	TerrainType terrainTypeAt(int x, int y) const { return terrainTypeAt(coordToIndex(x,y)); }
	const TerrainProperties &terrainPropertiesAt(size_t index) const
	{
		return terrainPropertyTable[terrainPropertyIndices[index]];
	}
	const TerrainProperties& terrainPropertiesAt(int x, int y) const { return terrainPropertiesAt(coordToIndex(x,y)); }
	// Terrain habitat only: ignores deposits, buildings and units already here.
	bool terrainSupportsResourceAt(size_t index, ResourceId resource) const;
    bool terrainSupportsResourceType(TerrainType terrain, ResourceId resource) const;
    bool terrainSupportsResourceAt(int x, int y, ResourceId resource) const { return terrainSupportsResourceAt(coordToIndex(x,y),resource); }
    // Integer adapters are retained at legacy authoring/script boundaries.
    bool terrainSupportsResourceAtByIndex(int x,int y,int resource) const { return resource>=0 && resource<NO_RES_TYPE && terrainSupportsResourceAt(x,y,static_cast<ResourceId>(resource)); }
    bool terrainSupportsMaterialAtSlot(int x,int y,int material) const;
    bool terrainSupportsMaterialAt(int x,int y,MaterialId material) const { return terrainSupportsMaterialAtSlot(x,y,materialIndex(material)); }
    std::uint32_t materialGrowthRateAtSlot(size_t index,int material) const;
    std::uint32_t materialGrowthRateAt(size_t index,MaterialId material) const { return materialGrowthRateAtSlot(index,materialIndex(material)); }
    // Total local and offspring supply potential under harvesting, before saturation.
    // This is a planning rate, not the next natural-growth event's expected delta.
    std::uint64_t materialRenewalPotentialAtSlot(size_t index,int material) const;
    std::uint64_t materialRenewalPotentialAt(size_t index,MaterialId material) const { return materialRenewalPotentialAtSlot(index,materialIndex(material)); }
    std::uint64_t materialExpansionRateAtSlot(size_t index,int material) const;
    std::uint64_t materialExpansionRateAt(size_t index,MaterialId material) const { return materialExpansionRateAtSlot(index,materialIndex(material)); }
	const std::vector<TerrainType>& terrainTypes() const { return terrainIds; }
	std::uint64_t terrainGeneration() const { return terrainGenerationValue; }
    std::uint64_t staticMaterialSourceGeneration() const { return staticMaterialSourceGenerationValue; }
	std::shared_ptr<const std::vector<TerrainType>> frozenTerrainSnapshot() const;
	std::shared_ptr<const TerrainMovementSnapshot>
	frozenTerrainMovementSnapshot(unsigned swim) const;
	bool hasTerrainMovementModifiers() const { return terrainMovementModifiers; }
	bool hasTerrainHealthEffects() const { return terrainHealthEffects; }
	bool hasAirTerrainConstraints() const { return airTerrainConstraints; }
	bool hasProjectileBlockingTerrain() const { return projectileBlockingTerrain; }
	bool projectilePathClear(Sint32 x0, Sint32 y0, Sint32 x1, Sint32 y1) const;
	ExperimentSet requiredTerrainExperiments() const;
    ExperimentSet requiredResourceExperiments() const;
	class TerrainEditBatch
	{
		Map& map;
	public:
		explicit TerrainEditBatch(Map& value) : map(value) { ++map.terrainEditDepth; }
		~TerrainEditBatch() { if (--map.terrainEditDepth == 0) map.finishTerrainEdit(); }
		TerrainEditBatch(const TerrainEditBatch&) = delete;
		TerrainEditBatch& operator=(const TerrainEditBatch&) = delete;
	};
	TerrainEditBatch editTerrain() { return TerrainEditBatch(*this); }
	void setCellTerrain(size_t index, TerrainType type);
	void setCellTerrain(int x, int y, TerrainType type) { setCellTerrain(coordToIndex(x,y), type); }
	// Explicit adapter for old serialized state and legacy test/import fixtures.
	void importLegacyTerrain();
	int getTerrainType(int x, int y) const
	{
		const auto type = terrainTypeAt(x,y);
		return type == GRASS_SAND_SHORE || type == SAND_WATER_SHORE ? TERRAIN_TYPE_UNKNOWN : int(type);
	}

	const ResourceRegistry& resourceRegistry() const { return *resourceRegistryValue; }
    std::shared_ptr<const ResourceRegistry> frozenResourceRegistry() const { return resourceRegistryValue; }
	void installResourceDefinitions(const std::string& json);
	const ResourceProperties& resourceProperties(ResourceId type) const
	{
	    return resourceRegistry().properties(type);
	}
    const ResourceProperties& resourcePropertiesByIndex(int type) const { return resourceProperties(static_cast<ResourceId>(type)); }
	Uint16 materialAmountAtSlot(size_t index, int material) const
	{
	    if (material < 0 || material >= int(MaterialCount)) return 0;
	    const auto& r = tiles[index].resource;
	    if (r.type == NO_RES_TYPE) return 0;
	    const auto& p = resourcePropertiesByIndex(r.type);
	    if (!(p.materialMask & (1u << material))) return 0;
	    if (std::has_single_bit(p.materialMask)) return static_cast<Uint16>(r.amount);
	    const auto slot = resourceStockIndices.empty() ? 0 : resourceStockIndices[index];
	    return slot ? resourceStocks[slot - 1][material] : 0;
	}
	Uint16 materialAmountAt(size_t index, MaterialId material) const { return materialAmountAtSlot(index, static_cast<int>(material)); }
	MaterialMask materialMaskAt(size_t index) const
	{
	    const auto& r = tiles[index].resource;
	    if (r.type == NO_RES_TYPE) return 0;
	    const auto& p = resourcePropertiesByIndex(r.type);
	    if (std::has_single_bit(p.materialMask)) return r.amount ? p.materialMask : 0;
	    const auto slot=resourceStockIndices.empty() ? 0 : resourceStockIndices[index];
	    if (!slot) return 0;
	    const auto& stocks=resourceStocks[slot-1];
	    MaterialMask result = 0;
	    for (unsigned mask=p.materialMask; mask; mask&=mask-1)
	    {
	        const auto material=std::countr_zero(mask);
	        if (stocks[material]) result|=MaterialMask(1u<<material);
	    }
	    return result;
	}
    std::array<Uint16,MaterialCount> materialStocksAt(size_t index) const;
	MaterialMask resourceMaterialMaskAt(size_t index) const;
	bool resourceBlocksGround(size_t index) const { const auto id=tiles[index].resource.type; return id!=NO_RES_TYPE && resourcePropertiesByIndex(id).blocksGround; }
	bool resourceBlocksAir(size_t index) const { const auto id=tiles[index].resource.type; return id!=NO_RES_TYPE && resourcePropertiesByIndex(id).blocksAir; }
	bool resourceBlocksBuilding(size_t index) const { const auto id=tiles[index].resource.type; return id!=NO_RES_TYPE && resourcePropertiesByIndex(id).blocksBuilding; }
	bool resourceVisibleToHarvest(size_t index) const { const auto id=tiles[index].resource.type; return id!=NO_RES_TYPE && resourcePropertiesByIndex(id).visibleToHarvest; }
	bool hasMaterialSourceSlot(int material) const { return material >= 0 && material < int(MaterialCount) && materialSourceCounts[material] != 0; }
	bool hasMaterialSource(MaterialId material) const { return hasMaterialSourceSlot(static_cast<int>(material)); }
	void setMaterialAmount(size_t index, MaterialId material, Uint16 amount);
    void setMaterialAmountSlot(size_t index,int material,Uint16 amount) { if (validMaterial(material)) setMaterialAmount(index,static_cast<MaterialId>(material),amount); }
	bool growResourceStock(size_t index);
	bool isMaterialTakeable(int x,int y,MaterialId material) const { return materialAmountAt(coordToIndex(x,y),material)>0; }
    bool isMaterialTakeableSlot(int x,int y,int material) const { return validMaterial(material) && isMaterialTakeable(x,y,static_cast<MaterialId>(material)); }
	const Resource& getResource(int x, int y) const
	{
		return tiles[coordToIndex(x, y)].resource;
	}

	const Resource& getResource(size_t pos) const
	{
		return tiles[pos].resource;
	}

	// Explicit cell writes preserve existing topology/refresh timing while keeping
	// derived seed data coherent. Reads never invalidate preparation caches.
	const std::vector<Tile> &getTiles() const { return tiles; }
	const Tile &getTile(size_t index) const { return tiles[index]; }
	// Restores stored cell data (including its sprite), not canonical terrain
	// identity. Terrain changes still use setCellTerrain/importLegacyTerrain.
	void replaceTile(size_t index, const Tile &tile);
	void replaceTile(int x, int y, const Tile &tile) { replaceTile(coordToIndex(x, y), tile); }
	void replaceResource(size_t index, const Resource &resource);
	void replaceResource(int x, int y, const Resource &resource) { replaceResource(coordToIndex(x, y), resource); }
	void setResourceAmount(size_t index, Uint32 amount);
	void setFertility(int x, int y, Uint16 value) { tiles[coordToIndex(x, y)].fertility = value; }
	void setResourcesGrow(int x, int y, Uint8 value) { tiles[coordToIndex(x, y)].canResourcesGrow = value; }
	// Raw mask replacement for order application/import; callers retain their
	// existing topology-generation and displayed-overlay updates.
	void setAreaMask(size_t index, Uint32 Tile::*field, Uint32 value);

	Uint32 getForbidden(int x, int y) const
	{
		return tiles[coordToIndex(x, y)].forbidden;
	}
	
	Uint8 getExplored(int x, int y, int team) const
	{
		return exploredArea[team][coordToIndex(x, y)];
	}
	
	// Legacy corner/sprite authoring adapter. Gameplay mutations use setCellTerrain.
	void setTerrain(int x, int y, Uint16 terrain);

	//! A bump throws away every cached route field in the game, so only paint
	//! a tile that is not already in the state being asked for.
	void addForbidden(int x, int y, Uint32 teamNum);
	void removeForbidden(int x, int y, Uint32 teamNum);

	void addClearArea(int x, int y, Uint32 teamNum)
	{
		tiles[coordToIndex(x, y)].clearArea |=  Team::teamNumberToMask(teamNum);
	}
	
	void addGuardArea(int x, int y, Uint32 teamNum)
	{
		tiles[coordToIndex(x, y)].guardArea |=  Team::teamNumberToMask(teamNum);
	}

	void addFarmArea(int x, int y, Uint32 teamNum)
	{
		tiles[coordToIndex(x, y)].farmArea |=  Team::teamNumberToMask(teamNum);
	}

	
	// Literal identity queries retained for authoring and script introspection.
	bool isWater(int x, int y) const { return terrainTypeAt(x,y) == WATER; }
	bool isWater(unsigned pos) const { return terrainTypeAt(pos) == WATER; }
	bool isGrass(int x, int y) const { return terrainTypeAt(x,y) == GRASS; }
	bool isGrass(unsigned pos) const { return terrainTypeAt(pos) == GRASS; }
	bool isSand(int x, int y) const { return terrainTypeAt(x,y) == SAND; }
	bool hasSand(int x, int y) const
	{
		const auto type = terrainTypeAt(x,y);
		return type == SAND || type == GRASS_SAND_SHORE || type == SAND_WATER_SHORE;
	}

	bool isResource(int x, int y) const
	{
		return getTile(x, y).resource.type != NO_RES_TYPE;
	}

    bool isClearableResourceForMaterials(int x,int y,bool materials[MaterialCount]) const
    {
        const auto index=coordToIndex(x,y);
        const auto& r=tiles[index].resource;
        if (r.type==NO_RES_TYPE || !resourcePropertiesByIndex(r.type).clearable) return false;
        // Clearing targets the configured deposit, including empty persistent
        // obstacles; harvested-stock availability is irrelevant to removal.
        const auto mask=resourcePropertiesByIndex(r.type).materialMask;
        for (unsigned m=0;m<MaterialCount;++m) if (materials[m] && (mask&(1u<<m))) return true;
        return false;
    }

	bool isResource(int x, int y, int *resourceType) const
	{
		const Resource &resource = getTile(x, y).resource;
		if (resource.type == NO_RES_TYPE)
			return false;
		*resourceType = resource.type;
		return true;
	}

	bool canResourcesGrow(int x, int y) const
	{
		return getTile(x, y).canResourcesGrow && terrainPropertiesAt(x,y).resourcesGrow;
	}

	//! Apply one clearing action using the deposit's configured consumption policy.
	void decResource(int x, int y);
	bool incResource(int x,int y,ResourceId resource,int variety);
    bool incResourceByIndex(int x,int y,int resource,int variety) { return resource>=0 && resource<NO_RES_TYPE && incResource(x,y,static_cast<ResourceId>(resource),variety); }

	// Farm areas (the "farm-areas" experiment, docs/features/farm-areas.md).
	// Every rule below is inert unless the game carries the experiment, so a
	// farm mask loaded into a game without it changes nothing.

	//! Whether this map's game carries the farm-areas experiment. False for a
	//! map with no game (the editor, Map-only tools).
	bool farmAreasEnabled() const;

	//! Whether the registered habitat and cached fertility field give this
	//! resource a nonzero growth probability at the tile.
	bool canResourceEverGrowHereByIndex(int x, int y, int resourceType) const;

	//! A farmable source of the terrain's configured farm material: prefer a
	//! compatible nearby resource, then the compiled catalog choice. Returns
	//! NO_RES_TYPE when the habitat supports no suitable resource.
	int farmCropAt(int x, int y) const;

	//! Clearable deposits in a clearing area, or nonfarmable clearable deposits
	//! in an enabled farm area. An overlapping clearing area also clears crops.
	//! farmAreas is farmAreasEnabled(), passed in so per-tile loops read it once.
	bool isClearingTarget(size_t index, Uint32 teamMask, bool farmAreas) const;

	//! Whether growth is enabled here and a configured farmable source can grow
	//! in this habitat. Existing deposits must be clearable or farmable.
	bool canPaintFarmArea(int x, int y) const;

	//! The resource definition's farmable property. Pool eligibility additionally
	//! depends on the requested yield's stock, consumption policy and reserve.
	bool isFarmableResourceByIndex(int resourceType) const;

	//! Choose from compatible nondestructive yields of the requested material
	//! in the team's farm area. Flood 8-connected sources from the unit's 3x3;
	//! empty gaps stop pooling. Choose the highest stock above that source's
	//! configured reserve, then shortest distance, then tile index.
	std::optional<size_t> pickFarmHarvestTileSlot(int x, int y, int resourceType, Uint32 teamMask);

	//! Revalidate the touched target's requested stock before delivery. Eligible
	//! farm yields pool from the connected field; destructive or nonfarmable
	//! yields harvest the touched deposit directly. Depleted targets and fields
	//! with no surplus produce nothing.
	bool takeHarvest(int x,int y,int dx,int dy,MaterialId material,Uint32 teamMask);
    bool takeHarvestMaterialSlot(int x,int y,int dx,int dy,int material,Uint32 teamMask) { return validMaterial(material) && takeHarvest(x,y,dx,dy,static_cast<MaterialId>(material),teamMask); }

private:
	//! Allocate/refresh and mark use, without exposing the possibly partial field.
	bool prepareBuildingGradient(Building *building, int swimClass, BuildingRoute route = BuildingRoute::Automatic);
	//! Read or move on a prepared field. Both settle their input cell first;
	//! neither refreshes the field or changes its use timestamp.
	Uint16 buildingGradientValue(Building *building, int swimClass, size_t cell, BuildingRoute route = BuildingRoute::Automatic) const;
	bool buildingGradientDirection(Building *building, int swimClass, int x, int y,
		int *dx, int *dy, bool strict, BuildingRoute route = BuildingRoute::Automatic) const;
	//! Per-tile predicate driver shared by isFree*/isHardSpace*.
	//! Each flag toggles whether one occupancy/terrain test contributes to rejection.
	struct TileChecks {
		bool noResource    : 1; //!< reject if a resource sits on the tile
		bool noUnit         : 1; //!< reject if a ground unit sits on the tile
		bool requireGroundPassable : 1; //!< require walking or an enabled swimming mode
		bool requireBuildable : 1; //!< require terrain that supports buildings
		bool checkForbidden : 1; //!< reject if the tile's forbidden mask intersects teamMask
	};
	//! Returns true iff (x,y) passes every enabled check. A building whose gid
	//! equals ignoreGid is treated as not present (used by the gid-tolerant
	//! isFreeForBuilding/isHardSpaceForBuilding overloads); pass NOGBID to make
	//! every occupant building reject.
	bool checkTile(int x, int y, TileChecks c, bool canSwim,
	               Uint32 teamMask, Uint16 ignoreGid) const;

public:
	//! Return true if unit can go to position (x,y)
	bool isFreeForGroundUnit(int x, int y, bool canSwim, Uint32 teamMask) const;
	bool isFreeForGroundUnitNoForbidden(int x, int y, bool canSwim) const;
	bool isFreeForAirUnit(int x, int y) const { return terrainPropertiesAt(x,y).flyable && !resourceBlocksAir(coordToIndex(x,y)) && (getAirUnit(x+w, y+h)==NOGUID); }
	bool isFreeForBuilding(int x, int y) const;
	bool isFreeForBuilding(int x, int y, int w, int h) const;
	bool isFreeForBuilding(int x, int y, int w, int h, Uint16 gid) const;
	// The "hardSpace" keyword means "Free" but you don't count Ground-Units as obstacles.
	bool isHardSpaceForGroundUnit(int x, int y, bool canSwim, Uint32 me) const;
	bool isHardSpaceForBuilding(int x, int y) const;
	bool isHardSpaceForBuilding(int x, int y, int w, int h) const;
	bool isHardSpaceForBuilding(int x, int y, int w, int h, Uint16 gid) const;
	
	//! Return contact direction (dx, dy) if unit touches building gbid; nullopt otherwise.
	std::optional<Offset> doesUnitTouchBuilding(Unit *unit, Uint16 gbid) const;
	//! Return contact direction (dx, dy) if (x, y) touches building gbid; nullopt otherwise.
	std::optional<Offset> doesPosTouchBuilding(int x, int y, Uint16 gbid) const;

	//! Return contact direction (dx, dy) if unit touches a resource of any type; nullopt otherwise.
	std::optional<Offset> doesUnitTouchResource(Unit *unit) const;
	//! Return contact direction (dx, dy) if unit touches a source of the requested material; nullopt otherwise.
	std::optional<Offset> doesUnitTouchMaterialSource(Unit *unit, MaterialId material) const;
	//! Return contact direction (dx, dy) if (x, y) touches a source of the requested material; nullopt otherwise.
	std::optional<Offset> doesPosTouchMaterialSource(int x, int y, MaterialId material) const;
	//! Return contact direction (dx, dy) if unit touches an enemy; nullopt otherwise.
	std::optional<Offset> doesUnitTouchEnemy(Unit *unit) const;

	//! Sets this particular clearing area location as claimed
	void setClearingAreaClaimed(int x, int y, int teamNumber, int gid);
	//! Sets this particular clearing area location as unclaimed
	void setClearingAreaUnclaimed(int x, int y, int teamNumber);
	//! Returns the gid if this clearing area is claimed, NOGUID otherwise
	int isClearingAreaClaimed(int x, int y, int teamNumber) const;

	//! Marks a particular square as containing an immobile unit
	void markImmobileUnit(int x, int y, int teamNumber);
	//! Clears a particular square of having an immobile unit
	void clearImmobileUnit(int x, int y);
	//! Returns true if theres an immobile unit on the square
	bool isImmobileUnit(int x, int y) const;
	//! Returns the team number of the immobile unit on the given square, 255 for none
	Uint8 getImmobileUnit(int x, int y) const;

	//! Return GID
	Uint16 getGroundUnit(int x, int y) const { return tiles[coordToIndex(x, y)].groundUnit; }
	Uint16 getAirUnit(int x, int y) const { return tiles[coordToIndex(x, y)].airUnit; }
	Uint16 getBuilding(int x, int y) const { return tiles[coordToIndex(x, y)].building; }
	
	void setGroundUnit(int x, int y, Uint16 guid) { tiles[coordToIndex(x, y)].groundUnit = guid; }
	void setAirUnit(int x, int y, Uint16 guid) { tiles[coordToIndex(x, y)].airUnit = guid; }
	void setBuilding(int x, int y, int w, int h, Uint16 gbid);

	//! Return the sector index of the sector containing tile (x,y). The
	//! formula is: y is wrapped to the map height, divided by SECTOR_TILES
	//! to get the sector row, then multiplied by sector-grid width and
	//! offset by the wrapped/divided x. Used by Map::getSector and by
	//! GameAnimations to bucket render effects per sector.
	int getSectorIndex(int x, int y) const { return wSector*((y&hMask)>>Sector::SECTOR_SHIFT)+((x&wMask)>>Sector::SECTOR_SHIFT); }
	//! Return sector at (x,y).
	Sector *getSector(int x, int y) { return &(sectors[getSectorIndex(x, y)]); }
	//! Return a sector in the sector array. It is not clean because too high level
	Sector *getSector(int i) { assert(i>=0); assert(i<sizeSector); return sectors+i; }

	//! Set undermap terrain type at (x,y) (undermap positions)
	void setUMTerrain(int x, int y, TerrainType t) { undermap[coordToIndex(x, y)] = (Uint8)t; }
	//! Return undermap terrain type at (x,y)
	TerrainType getUMTerrain(int x, int y) const { return (TerrainType)undermap[coordToIndex(x, y)]; }
	//! Set undermap terrain type at (x,y) (undermap positions) on an area
	void setUMatPos(int x, int y, TerrainType t, int l);

	//! With l==0, it will remove no resource. (Unaligned coordinates)
	void setNoResource(int x, int y, int l);
	//! Removes every resource in the w by h area at (x, y) whose terrain no longer allows it,
	//! used after the terrain under it changed
	void removeUnallowedResources(int x, int y, int w, int h);
	//! With l==0, it will add resource only on one tile. (Aligned coordinates)
	void setResource(int x,int y,ResourceId type,int l);
    void setResourceByIndex(int x,int y,int type,int l) { if (type>=0 && type<NO_RES_TYPE) setResource(x,y,static_cast<ResourceId>(type),l); }
	bool isResourceAllowed(int x, int y, int type);
	

	///The following is for script areas, which are named areas for map scripts set in the editor
	///@{
	///Returns whether area #n is set for a particular point. n can be from 0 to 8
	bool isPointSet(int n, int x, int y) const;
	///Sets a particular point on area #n
	void setPoint(int n, int x, int y);
	///Unsets a particular point on area #n
	void unsetPoint(int n, int x, int y);
	///Returns the name of area #n
	std::string getAreaName(int n) const;
	///Sets the name of area #n
	void setAreaName(int n, std::string name);
	///A vector holding the area names
	std::vector<std::string> areaNames;
	///@}
	
	//! Transform coordinate from map scale (mx,my) to pixel scale (px,py)
	void mapCaseToPixelCase(int mx, int my, int *px, int *py) const { *px=(mx<<5); *py=(my<<5); }
	//! Transform coordinate from map (mx,my) to screen (px,py). Use this one to display a building or an unit to the screen.
	// Presentation bounds only; never serialized or included in simulation checksums.
	int displayViewportW=0, displayViewportH=0;
	//! Process-unique identity, renewed whenever the map is cleared or resized.
	//! Presentation caches key on it to notice a replaced map; never saved.
	Uint64 identity() const { return identityValue; }
	//! Seed of the terrain's drawn appearance. It salts the deterministic hashes
	//! that choose texture variants, boundary contours, pebbles and diagonal
	//! joins, so two maps with the same cells look different while every client
	//! of one map draws it identically. Saved with the map (format 138) and shared
	//! through it; presentation only: never in checkSum(), never read by
	//! simulation code. Generators derive it from their request seed, the editor
	//! rerolls it, and maps saved before format 138 load with seed 0.
	Uint32 terrainSeed() const { return terrainSeedValue; }
	void setTerrainSeed(Uint32 seed) { terrainSeedValue = seed; }
	void mapCaseToDisplayable(int mx, int my, int *px, int *py, int viewportX, int viewportY) const;
	//! Transform coordinate from map (mx,my) to screen (px,py). Use this one to display a path line to the screen.
	void mapCaseToDisplayableVector(int mx, int my, int *px, int *py, int viewportX, int viewportY, int screenW, int screenH) const;
	//! Transform coordinate from screen (mx,my) to map (px,py) for standard grid aligned object (buildings, resources, units)
	void displayToMapCaseAligned(int mx, int my, int *px, int *py, int viewportX, int viewportY) const;
	//! Transform coordinate from screen (mx,my) to map (px,py) for standard grid unaligned object (terrain)
	void displayToMapCaseUnaligned(int mx, int my, int *px, int *py, int viewportX, int viewportY) const;
	//! Transform coordinate from screen (mx,my) to building (px,py)
	void cursorToBuildingPos(int mx, int my, int buildingWidth, int buildingHeight, int *px, int *py, int viewportX, int viewportY) const;
	//! Transform coordinate from building (px,py) to screen (mx,my)
	void buildingPosToCursor(int px, int py, int buildingWidth, int buildingHeight, int *mx, int *my, int viewportX, int viewportY) const;
	
	enum GradientType
	{
		GT_UNDEFINED = 0,
		GT_MATERIAL = 1,
		GT_BUILDING = 2,
		GT_FORBIDDEN = 3,
		GT_GUARD_AREA = 4,
		GT_CLEAR_AREA=5,
		GT_SIZE = 6
	};
	
	//! Swim class of a unit with these walk and swim speeds (0 = cannot swim).
	static int swimClass(int walkSpeed, int swimSpeed);
	//! Swim class used where no unit is at hand: water costs the same as land.
	static constexpr int SWIM_CLASS_EVEN = 3;
	//! Cheapest possible step for a class, the A* heuristic unit.
	int minStepCost(int swimClass) const;
	//! Highest cost a pathfinding gradient can hold (see GradientConstants.h).
	static constexpr int GRADIENT_COST_LIMIT = 0xFFFF - 1 - 1 - 42;
	//! Cost of stepping (dx, dy) into the cell at targetIndex, in gradient units.
	int stepCost(int dx, int dy, size_t targetIndex, int swimClass) const;
	
	// Gradients are built per team and swim class the first time a unit of that
	// class asks for one, so classes nobody uses cost nothing.
	//! withMarkets: the variant where the team's stocked markets are goals too,
	//! priced by each supplier's configured pickup penalty. Used when a
	//! consumer enables stock fetching (Building::fetchesFromMarkets).
	Uint16 *getMaterialGradientSlot(int teamNumber, int resourceType, int swimClass, bool withMarkets = false, const Building* consumer = nullptr);
    Uint16* getMaterialGradient(int team,MaterialId material,int swim,bool withMarkets=false,const Building* consumer=nullptr) { return getMaterialGradientSlot(team,materialIndex(material),swim,withMarkets,consumer); }
	// Consumer-aware requests are simulation-thread only. Returned cache pointers
	// remain valid until the next consumer-aware request (which may evict them).
	unsigned materialSupplyModesSlot(const Building* consumer, int resource) const;
	Uint16 *cachedMaterialGradientSlot(const Building* consumer, int resource, int swim, unsigned modes);
	bool stockSupplierEligibleSlot(const Building* supplier, const Building* consumer, int resource, unsigned modes) const;
	void setMaterialRoutingCacheBudget(Uint64 bytes);
	Uint64 materialRoutingCacheBytes() const;
	void saveMaterialRoutingCache(GAGCore::OutputStream* stream) const;
	void loadMaterialRoutingCache(GAGCore::InputStream* stream, bool packed, int versionMinor);
	Uint16 *getForbiddenGradient(int teamNumber, int swimClass);
	Uint16 *getGuardAreasGradient(int teamNumber, int swimClass);
	Uint16 *getClearAreasGradient(int teamNumber, int swimClass);
	//! Guard-area balancing: out[i] = number of the team's warriors within
	//! GUARD_CROWD_RADIUS tiles of tile i (units inside buildings excluded).
	//! Separable box sum, linear in map size. Returns false, leaving out
	//! untouched, when the team has no such warrior.
	bool computeWarriorCrowding(int teamNumber, Uint16 *out) const;
	
	bool materialAvailableSlot(int teamNumber, int resourceType, int swimClass, int x, int y, bool withMarkets = false, const Building* consumer = nullptr);
    bool materialAvailable(int team,MaterialId material,int swim,int x,int y,bool markets=false,const Building* consumer=nullptr) { return materialAvailableSlot(team,materialIndex(material),swim,x,y,markets,consumer); }
	bool materialAvailableSlot(int teamNumber, int resourceType, int swimClass, int x, int y, int *dist, bool withMarkets = false, const Building* consumer = nullptr);
    bool materialAvailable(int team,MaterialId material,int swim,int x,int y,int* distance,bool markets=false,const Building* consumer=nullptr) { return materialAvailableSlot(team,materialIndex(material),swim,x,y,distance,markets,consumer); }
	bool materialAvailableUpdateSlot(int teamNumber, int resourceType, int swimClass, int x, int y, Sint32 *targetX, Sint32 *targetY, int *dist, bool withMarkets = false, const Building* consumer = nullptr);
	//! The team's own, alive market next to the unit that holds resourceType, or NULL.
	Building *touchedStockedMarketSlot(Unit *unit, int resourceType) const;
	//! A stock of resourceType in one of the team's markets appeared or ran out:
	//! rebuild the "with markets" gradients for it at the next step.
	bool marketsV2Enabled() const;
	void dirtyMarketGradientsSlot(int teamNumber, int resourceType);
	void invalidateSupplierLocations();
	
	//! Follow the gradient uphill from (x, y). Returns whether a goal cell was reached; the
	//! last position is in (targetX, targetY). Works on the Uint16 pathfinding gradients and
	//! on the AIs' Uint8 maps alike: the goal is the maximum of the element type.
	template<typename T>
	bool getGlobalGradientDestination(const T *gradient, int x, int y, Sint32 *targetX, Sint32 *targetY) const;
	//! Whether (x, y) is a local maximum of gradient: no neighbour holds a strictly higher
	//! value. True at any tile getGlobalGradientDestination's ascent could end on, including
	//! gradients like a round-trip field whose seeded goal is a finite cost, not the type's max.
	template<typename T>
	bool isGradientPeak(const T *gradient, int x, int y) const;

	Uint16 getGradient(int teamNumber, Uint8 resourceType, int swimClass, int x, int y, bool withMarkets = false, const Building* consumer = nullptr)
	{
		return getMaterialGradientSlot(teamNumber, resourceType, swimClass, withMarkets, consumer)[coordToIndex(x, y)];
	}
	
	// Chamfer distance transform on a pre-seeded Uint8 buffer. Caller fills the
	// buffer (0 = obstacle, 1 = free, any cell >= 3 = source); chamfer sweeps it
	// forward and backward until stable. Only the AIs' own helper maps use it;
	// the pathfinding gradients are built by propagateGradient. Defined in
	// MapGradientChamfer.cpp.
	void updateGlobalGradient(Uint8 *gradient);
	//! Dijkstra from every seeded cell of a pathfinding gradient (see GradientConstants.h).
	//! Seeds may carry any cost up to GRADIENT_COST_LIMIT (0 for GRADIENT_AT_GOAL; e.g. a
	//! resource tile seeded with its distance to a building); do not pass a completed
	//! field. With maxCost, propagation does not add cells beyond the cap; existing
	//! seeds remain. Uses worker-owned scratch storage: parallel calls must use this
	//! Map's executor and distinct fields.
	//! swimClass must be in [0, SWIM_CLASS_COUNT).
    GAGCore::CooperativeTask updateGlobalGradientTask(Uint8 *gradient);
	void propagateGradient(Uint16 *gradient, int swimClass, int maxCost = GRADIENT_COST_LIMIT);
	//! Step toward the neighbour with the highest value minus step cost. strict requires
	//! real progress; otherwise a random sidestep to an equal cell is accepted when blocked.
	//! With guardAreaMask, only neighbours painted as a guard area for those teams count
	//! (guard-area balancing: stepping within an area).
	bool directionByGradient(Uint32 teamMask, int swimClass, int x, int y, const Uint16 *gradient, int *dx, int *dy, bool strict, Uint32 guardAreaMask = 0) const;
	void updateMaterialGradient(int teamNumber, Uint8 resourceType, int swimClass, bool withMarkets = false);
	//! Direction toward a resource of resourceType. With a target building the round-trip
	//! gradient is descended, so the unit heads for the resource that is nearest for
	//! fetching and carrying it there; without one, for the resource nearest to itself.
	bool pathfindMaterial(int teamNumber, Uint8 resourceType, int swimClass, int x, int y, int *dx, int *dy, bool *stopWork, Building *target, bool withMarkets = false);
	void pathfindRandom(Unit *unit);

	//! Initialize a fresh building field and retain its search frontier. Point
	//! queries extend it on demand; buildingGradient returns a complete field.
	void updateGlobalGradient(Building *building, int swimClass, BuildingRoute route = BuildingRoute::Automatic);
	//! Rebuild the building's round-trip gradient for a resource type and swim class:
	//! every tile of that resource is seeded with its distance to the building, so a
	//! cell's value is the cheapest fetch-and-carry trip from there.
	void updateRoundTripGradientSlot(Building *building, int resourceType, int swimClass);
	//! The building's round-trip gradient, built or refreshed on demand. NULL when the
	//! building cannot be reached.
	const Uint16 *roundTripGradientSlot(Building *building, int resourceType, int swimClass);
	//! Tiles of the cheapest trip from (x, y) to a resource of resourceType and on to the
	//! building, read from a round-trip gradient a fetcher's walk has already built. False
	//! when there is none or no such trip; the caller then scores by the plain distances.
	bool roundTripDistanceSlot(Building *building, int resourceType, int swimClass, int x, int y, int *dist);
	//! Complete field, refreshed as needed; NULL when locked. Point queries use
	//! buildingAvailable/pathfindBuilding so partial arrays never escape this API.
	const Uint16 *buildingGradient(Building *building, int swimClass, BuildingRoute route = BuildingRoute::Automatic);
	//! Finish a cached field without refreshing its age or last-use timestamp.
	void finishBuildingGradient(Building *building, int swimClass, BuildingRoute route = BuildingRoute::Automatic) const;
	bool buildingAvailable(Building *building, int swimClass, int x, int y, int *dist, BuildingRoute route = BuildingRoute::Automatic);
	//!requests the next step (dx, dy) to take to get to the building from (x,y)
	bool pathfindBuilding(Building *building, int swimClass, int x, int y, int *dx, int *dy, BuildingRoute route = BuildingRoute::Automatic);

	//! Bumped whenever a footprint or a forbidden mask changes. A route field
	//! spans the map, so any such change may cross it: each field records the
	//! value it was built at and is rebuilt on use once it differs. Resources
	//! and immobile units are left out on purpose; they change far too often
	//! and a unit blocked by one forces its own rebuild in pathfindBuilding.
	Uint32 topologyGeneration;
	void bumpTopologyGeneration() { topologyGeneration++; }
	bool pathfindForbidden(const Uint16 *optionGradient, int teamNumber, int swimClass, int x, int y, int *dx, int *dy);
	enum class AreaKind { Guard, Clear };
	//! Find the best direction toward a guard or clear area; return true if one has been found.
	bool pathfindArea(AreaKind kind, int teamNumber, int swimClass, int x, int y, int *dx, int *dy);
	//! Update the forbidden gradient, 
	void updateForbiddenGradient(int teamNumber, int swimClass);
	void updateForbiddenGradient(int teamNumber);
	void updateForbiddenGradient();
	//! Update the guard area gradient
	void updateGuardAreasGradient(int teamNumber, int swimClass);
	void updateGuardAreasGradient(int teamNumber);
	void updateGuardAreasGradient();
	//! Update the clear area gradient
	void updateClearAreasGradient(int teamNumber, int swimClass);
	void updateClearAreasGradient(int teamNumber);
	void updateClearAreasGradient();
	
	///Implements A* algorithm for point to point pathfinding. Does not cache path, designed to be fast
	bool pathfindPointToPoint(int x, int y, int targetX, int targetY, int *dx, int *dy, int swimClass, Uint32 teamMask, int maximumLength);
	bool pathfindAirPointToPoint(int x, int y, int targetX, int targetY, int *dx, int *dy);
	
	void initExploredArea(int teamNumber);
	void makeDiscoveredAreasExplored(int teamNumber);
	void updateExploredArea(int teamNumber);
	
public:
	Game *game;
	// Diagnostic tile masks and per-team overlap counts. Building changes update
	// only their footprints, keeping growth-event lookups contiguous and O(1).
	// The distance-band team masks fit one 64-bit tile entry, so an event fetches one
	// cache line rather than three separately allocated band planes.
	std::vector<Uint64> growthCoverage;
	std::vector<Uint16> growthCoverageCounts[GROWTH_COVERAGE_BANDS];
	std::vector<TeamStats::CoverageBuilding> growthCoverageBuildings[Team::MAX_COUNT];
	Uint32 growthCoverageGeneration[Team::MAX_COUNT]{};
	bool growthCoverageValid = false;
private:
	std::vector<Tile> tiles;
public:
	Uint64 identityValue = 0;
	Uint32 terrainSeedValue = 0;
	Sint32 w, h;
	Sint32 wMask, hMask;
	Sint32 wDec, hDec;
	
protected:
	// private functions, used for edition

	void regenerateMap(int x, int y, int w, int h);
	
	Uint16 lookup(Uint8 tl, Uint8 tr, Uint8 bl, Uint8 br) const;

public:
	// Rebuild rendered terrain after bulk undermap edits.
	void rebuildTerrain() { regenerateMap(0, 0, w, h); }
    // here we handle terrain
	// mapDiscovered
	bool arraysBuilt; // if true, the next pointers(arrays) have to be valid and filled.
	std::vector<Uint32> mapDiscovered;
	std::vector<Uint32> fogOfWarA;
	std::vector<Uint32> fogOfWarB;
	Uint32* fogOfWar = nullptr; // if valid, either points to &fogOfWarA[0] or &fogOfWarB[0]
	//! Render-only overlay caches for the locally-displayed team's areas (forbidden /
	//! guard / clear / farm). These mirror the per-team bits in tiles[].{forbidden,guardArea,
	//! clearArea,farmArea} but only for displayedTeam, so the renderer can query one tile cheaply.
	//! They are NOT in checkSum() and must never be read from a sim path — doing so would
	//! desync, because displayedTeam differs per client. true = bit set.
	Utilities::BitArray displayedForbiddenView;
	Utilities::BitArray displayedGuardAreaView;
	Utilities::BitArray displayedClearAreaView;
	Utilities::BitArray displayedFarmAreaView;

	// Scratch for pickFarmHarvestTile's flood fill: one stamp per tile, compared
	// against a counter bumped per call instead of clearing the buffer, and the
	// queue kept across calls so a harvest does not allocate. Never saved, never
	// checksummed, never read across calls.
	std::vector<Uint32> farmFloodStamps;
	std::vector<size_t> farmFloodQueue;
	Uint32 farmFloodGeneration = 0;
	//! Team whose view is locally displayed. Mirror of GameGUI::localTeamNo, used only to
	//! decide whether to refresh the displayed*View caches above. Not in checkSum.
	Sint32 displayedTeam = NO_DISPLAYED_TEAM;
	
	///This is the maximum fertility of any point on the map
	Uint16 fertilityMaximum;
	const Fertility::GrowthCache& resourceGrowthField() const;
	mutable Fertility::GrowthCache growthCache;
	mutable std::mutex growthCacheMutex;
	
protected:
	// Pathfinding gradients, see GradientConstants.h for the cell values. Indexed
	// [team][swim class]; NULL until a unit of that class asks for one.
	// Map owns the buffers and frees them on clear. Resource/guard/clear fields
	// refresh round-robin in syncStep; forbidden fields refresh through map edits.
	// Used to go to resources
	//[int team][int resourceNumber][int swimClass]
	Uint16 *materialGradients[Team::MAX_COUNT][MaterialSlotCount][SWIM_CLASS_COUNT];
	mutable std::mutex materialGradientMutex;
	//! Same, with the team's stocked markets as goals (see getResourceGradient).
	Uint16 *marketMaterialGradients[Team::MAX_COUNT][MaterialSlotCount][SWIM_CLASS_COUNT];
	
	// Used to go out of forbidden areas
	Uint16 *forbiddenGradient[Team::MAX_COUNT][SWIM_CLASS_COUNT];
	
	// Used to attract idle warriors into guard areas
	Uint16 *guardAreasGradient[Team::MAX_COUNT][SWIM_CLASS_COUNT];
	//! Guard-area balancing: replace the zero cost of every painted seed in
	//! gradient with its crowding cost (MapInternal.h).
	void seedGuardAreaCrowding(int teamNumber, Uint16 *gradient) const;
	//! Replace every cell of grid with the sum of the cells within GUARD_CROWD_RADIUS
	//! of it (Chebyshev, torus): two row-major sliding-window passes.
	void boxSumInPlace(Uint16 *grid) const;
	
	// Used to attract idle workers into clearing
	// areas that aren't clear
	Uint16 *clearAreasGradient[Team::MAX_COUNT][SWIM_CLASS_COUNT];
	
public:
	// Used to guide explorers
	//[int team]
	// 0=unexplored, 255=just explored
	Uint8 *exploredArea[Team::MAX_COUNT];
	
	/// This shows how many "claims" there are on a particular resource square
	/// This is so that not all 150 free units go after one piece of wood
	/// Each square is the gid of the claiming unit
	Uint16 *clearingAreaClaims[Team::MAX_COUNT];
	
	/// These are integers that tell whether an immobile unit is standing on the
	/// square, and if so, what team number it is. In terms of the engine, these
	/// are treated like forbidden areas
private:
	Uint8 *immobileUnits;
public:
	
protected:
	//Used for scheduling computation time.
	bool gradientUpdated[Team::MAX_COUNT][MaterialSlotCount][SWIM_CLASS_COUNT];
	//! A market's stock of the resource switched: rebuild the twin before its plain
	//! gradient's next turn in the round robin.
	bool marketGradientDirty[Team::MAX_COUNT][MaterialSlotCount][SWIM_CLASS_COUNT];
	bool marketGradientUpdated[Team::MAX_COUNT][MaterialSlotCount][SWIM_CLASS_COUNT];
	//! Whether tile gid is one of teamNumber's alive markets holding resourceType.
	bool isStockedMarketTile(Uint16 gid, int teamNumber, int resourceType) const;
	//Used for scheduling computation time on the guard area gradients
	bool guardGradientUpdated[Team::MAX_COUNT][SWIM_CLASS_COUNT];
	//Used for scheduling computation time on the clear area gradients
	bool clearGradientUpdated[Team::MAX_COUNT][SWIM_CLASS_COUNT];
	
	Uint8 *undermap;
	Uint8 **listedAddr;
	size_t size;

	Sector *sectors;
	Sint32 wSector, hSector;
	int sizeSector;
	
	
	///This is a single point in the array used for A* algorithm
	struct AStarAlgorithmPoint
	{
		AStarAlgorithmPoint() : x(-1), y(-1), dx(-1), dy(-1), moveCost(ASTAR_COST_INFINITY), totalCost(ASTAR_COST_INFINITY), isClosed(false) { }
		AStarAlgorithmPoint(Sint16 x, Sint16 y, Sint16 dx, Sint16 dy, Uint32 moveCost, Uint32 totalCost, bool isClosed) : x(x), y(y), dx(dx), dy(dy), moveCost(moveCost), totalCost(totalCost), isClosed(isClosed) {}
		//Pos x
		Sint16 x;
		//Pos y
		Sint16 y;
		//The direction from the starting point that leads to this path
		Sint16 dx;
		//The direction from the starting point that leads to this path
		Sint16 dy;
		//Cost to get to square x
		Uint32 moveCost;
		//Cost to get to square x + estimate to get to the end
		Uint32 totalCost;
		//Whether this cell has been examined
		bool isClosed;
	};
	
	///This is a function-object that compares two points based on their total score in the A* algorithm
	struct AStarComparator
	{
		AStarComparator(const AStarAlgorithmPoint* points) : points(points) {}
		bool operator()(int lhs, int rhs)
		{
			if(points[lhs].totalCost != points[rhs].totalCost)
				return points[lhs].totalCost > points[rhs].totalCost;
			// Total order on equal keys so the pop sequence does not depend on
			// how the standard library arranges equal heap elements.
			return lhs > rhs;
		}
		const AStarAlgorithmPoint* points;
	};
	
	//This array is kept and re-used for every point-to-point pathfind call
	AStarAlgorithmPoint* aStarPoints;
	std::vector<int> aStarExaminedPoints;

public:
	Uint32 checkSum(bool heavy);
	Sint32 warpDist1d(int p, int q, int l);///distance of coordinates p and q on a loop of length l
	Sint32 warpDistSquare(int px, int py, int qx, int qy); //!< The distance^2 between (px, py) and (qx, qy), warp-safe.
	Sint32 warpDistMax(int px, int py, int qx, int qy); //!< The max distance on x or y axis, between (px, py) and (qx, qy), warp-safe.
	void dumpGradient(Uint8 *gradient, const std::string filename = "gradient.dump.pgm");

public:
	void makeHomogenMap(TerrainType terrainType);
    GAGCore::CooperativeTask makeHomogenMapTask(TerrainType terrainType);
	void controlSand(void);
	void smoothResources(int times);

};
