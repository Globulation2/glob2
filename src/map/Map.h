// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière
// Copyright (C) 2006 Bradley Arsenault

#pragma once
#include "ChecksumScheme.h"
#include "ResourceClaims.h"
#include "claims/ActiveMaterialClaims.h"
#include "claims/ClaimRecovery.h"
#include "claims/ClaimTerrainComponents.h"
#include "claims/ClaimContention.h"
#include "claims/ClaimHireTargets.h"
#include "OwnerRandom.h"
namespace GAGCore { class CooperativeTask; }
struct JoinContext;
class ComputeExecutor;
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <list>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string_view>
#include <span>
#include <vector>
#include <assert.h>

#include "CoverageBuilding.h"
#include "TeamCapacity.h"
#include "Ressource.h"
#include "MapState.h"
#include "ResourceRegistry.h"
#include "CellRules.h"
#include "MapStateView.h"
#include "ResourcePlaneKey.h"
#include "MapChangeTracking.h"
#include "Sector.h"
#include "TerrainType.h"
#include "FertilityField.h"
#include "ExperimentalFeatures.h"
#include "TerrainPropertiesLayout.h"
#include "TerrainRegistry.h"
class MapAssetBundle;
#include "BitArray.h"
#include "MapTypes.h"

namespace SimulationSnapshot { struct Handle; }

class Unit;
class Team;
class Building;


class Map;
class Game;
class SessionGame;
class MapHeader;
class MapRouting;
namespace ResourceGrowth { struct Metrics; }


// a 1x1 piece of map
struct Tile
{
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



/*! Map, handle all physical localisations
	All size are given in 32x32 pixel cell, which is the basic game measurement unit.
	All functions are wrap-safe, excepted the one specified otherwise.
*/
class Map
{
	friend class MapRouting;
	friend class ResourceSeedCache;
	friend struct ResourceGradientProbe;
	void resourceSeedChanged(size_t index, unsigned flags);
	void invalidateResourceSeeds();
	// Owned out of line so ComputeExecutor.h (1,500 inline lines) stays out of Map.h;
	// `compute` is used per job and dispatch, never per cell.
	std::unique_ptr<ComputeExecutor> computeOwner;
	ComputeExecutor &compute;
	// Routing queries, gradients and the gradient pipeline (gradient/MapRouting.h),
	// at the position GradientRuntime held so destruction order is unchanged;
	// Map::clear frees its buffers.
	std::unique_ptr<MapRouting> routing_;
	// The live cell view minus the game's growth settings, rebound by
	// refreshLiveView() whenever an input is replaced. Per-cell queries read
	// it directly instead of assembling a view on every call.
	MapState::View liveCells;
	// Per-array change tracking at chunk granularity; see MapChangeTracking.h.
	MapState::ChunkGeometry chunkGeometry;
	MapState::ChangeTracker terrainChanges, resourceChanges, occupancyChanges, areaChanges, visibilityChanges;
	Uint64 discoveryRevisionValue = 1;
	// Derived from painted masks, independent of reachable gradient goals.
	std::array<std::array<Uint32, TEAM_MAX_COUNT>, 4> paintedAreaCounts{};
	void paintedAreaMaskChanged(AreaType kind, Uint32 before, Uint32 after);
	void rebuildPaintedAreaCounts();
	void markTerrain(size_t index) { terrainChanges.mark(chunkGeometry.chunkOf(index)); }
	void markResource(size_t index) { resourceChanges.mark(chunkGeometry.chunkOf(index)); }
	void markOccupancy(size_t index) { occupancyChanges.mark(chunkGeometry.chunkOf(index)); }
	void markArea(size_t index) { areaChanges.mark(chunkGeometry.chunkOf(index)); }
	void markVisibility(size_t index) { visibilityChanges.mark(chunkGeometry.chunkOf(index)); }
	ResourceClaims::Ledger materialClaims;
    // Whether this game uses resource claims (ResourceClaims::Policy). Fixed at
    // new-game setup and saved; saves before format 213 load without claims.
    bool resourceClaimsEnabled=false;
    // The tick at which enableResourceClaims set up this game; claims may be
    // disabled only while no tick has run since. Not saved: a loaded save
    // keeps its own claim setting.
    std::optional<Uint64> claimSetupTick;
    // Rules the held claims were validated under (farm areas, growth, resource
    // catalog); a change invalidates every claim at the next publication.
    std::optional<std::array<Uint64,3>> activeClaimRules;
    std::unique_ptr<ResourceClaims::ActiveRouteRuntime> materialClaimRoutes;
    Uint32 claimRuntimeChecksum(bool hashRouteCells, bool includePendingOutputs) const;
    void validateActiveMaterialClaims(ResourceClaims::Ledger& ledger) const;
    // Derived from terrain only; never saved, checksummed or snapshotted.
    mutable ResourceClaims::TerrainComponentCache claimTerrainComponentCache;
    // Derived per-team target occupancy for the contention trigger; rebuilt
    // from unit state before each team steps its units, never saved or checksummed.
    ResourceClaims::ContentionIndex claimContention;
    // Derived per-team table of hire-time claim target searches, rebuilt
    // immediately before each team's hiring phase; never saved or checksummed.
    ResourceClaims::HireTargetIndex claimHireTargets;
	void markAllChanges() { terrainChanges.markAll(); resourceChanges.markAll(); occupancyChanges.markAll(); areaChanges.markAll(); markDiscoveryChanged(); }
	//! For fixtures that assign the cell arrays directly instead of calling
	//! setSize: size the change trackers and bind the live view to the arrays.
	void bindBootstrappedArrays() { resetChangeTracking(); refreshLiveView(); rebuildPaintedAreaCounts(); }
	//! Size the trackers to the current dimensions; every chunk becomes dirty.
	void resetChangeTracking()
	{
		chunkGeometry.reset(w, h, unsigned(wDec), Uint32(wMask));
		const auto count = chunkGeometry.count();
		terrainChanges.reset(count); resourceChanges.reset(count); occupancyChanges.reset(count);
		areaChanges.reset(count); visibilityChanges.reset(count); ++discoveryRevisionValue;
		materialClaims.reset(std::size_t(w) * h);
	}
	mutable std::mutex waterSnapshotMutex;
	mutable std::shared_ptr<const std::vector<Uint8>> waterSnapshot;
	mutable std::shared_ptr<const std::vector<TerrainType>> vertexSnapshot;
	mutable std::array<std::shared_ptr<const TerrainMovementSnapshot>, MOVEMENT_CLASS_COUNT> terrainMovementSnapshots;
	std::shared_ptr<const ResourceRegistry> resourceRegistryValue = ResourceRegistry::availableDefaults();
	// Single-yield tiles keep stock inline. The index plane is allocated only
	// when a multi-yield deposit is first placed; zero means no sidecar slot.
    void installCatalogs(std::shared_ptr<const TerrainRegistry> terrain,
        std::shared_ptr<const ResourceRegistry> resources, std::shared_ptr<const MapAssetBundle> assets);
	std::vector<Uint32> resourceStockIndices;
	std::vector<std::array<Uint16, MaterialCount>> resourceStocks;
	std::vector<Uint32> freeResourceStocks;
	std::array<Uint32, MaterialCount> materialSourceCounts{};
	void initializeResourceStock(size_t index, const std::array<Uint16, MaterialCount> *stocks = nullptr);
	void releaseResourceStock(size_t index);
	void refreshResourceTotal(size_t index);
	void rebuildResourceState();
	void materialStockChanged(size_t index, MaterialMask before, MaterialMask beforeAvailable, bool notifyTargets=true);
	bool harvestMaterial(size_t index, int material);
	// Terrain is stored once per vertex, and only there: vertex (x,y) is the
	// top-left corner of cell (x,y). Each cell's rules are derived from its four
	// corners (CellRules.h); cells keep only the index of their rule.
	std::vector<TerrainType> vertexTerrain;
	std::vector<Uint16> cellRules;
	std::shared_ptr<const TerrainRegistry> terrainRegistryValue = TerrainRegistry::builtins();
    std::shared_ptr<const MapAssetBundle> assetBundleValue;
	std::shared_ptr<CellRuleTable> cellRuleTable;
	const CellRule *cellRuleData = nullptr;
	// Vertices per terrain type, and cells per cell rule.
	std::vector<std::size_t> terrainCounts; // one per built-in terrain, sized in the constructor
	std::vector<std::size_t> cellRuleCounts;
	std::array<unsigned, 6> terrainFeatures{};
	unsigned terrainBucketCount = 64;
	std::array<std::array<unsigned, 182>, MOVEMENT_CLASS_COUNT> terrainGroundCostCounts{};
	std::array<std::array<unsigned, 256>, MOVEMENT_CLASS_COUNT> terrainGroundDiagonalCounts{};
	// Cheapest cardinal and diagonal entry among the rules present; {0, 0} if none admits the class.
	std::array<gradient_kernel::EntrySteps, MOVEMENT_CLASS_COUNT> terrainRouteFloors{};
	std::array<unsigned, 41> terrainAirCostCounts{};
	unsigned terrainMinimumAir = GRADIENT_STEP;
	void adjustTerrainFeatures(std::uint16_t rule, bool add);
	void refreshRouteFloors();
	std::uint64_t terrainGenerationValue = 1;
    // Explicit availability changes of intrinsically static material sources.
    // Shared AI gradients persist only whether this generation is current.
    std::uint64_t staticMaterialSourceGenerationValue = 1;
    void bumpStaticMaterialSourceGeneration()
    {
        // Zero is reserved for a stale shared-runtime gradient after loading.
        if (++staticMaterialSourceGenerationValue == 0) ++staticMaterialSourceGenerationValue;
    }
	// The rule of cell index for its corners, interned on demand; a table shared with a snapshot
	// (use_count above one) is cloned before it gains a rule.
	std::uint16_t deriveCellRule(size_t index);
	void changeCellRule(size_t index, std::uint16_t rule);
	void writeVertex(size_t index, TerrainType type);
	// Re-derives every cell after a bulk vertex change, keeping the rule table.
	void rederiveAllCells();
	void bindCellRules();
	// Loading formats before 146: vertices under whole-cell terrain take it on.
	void convertLegacyCellTerrain(const std::vector<TerrainType> &cells);
	// Re-derives every cell rule from the vertices, with table or else a fresh
	// table for the current registries, and recounts terrain and features.
	void rebuildTerrainCounts(std::shared_ptr<CellRuleTable> table = {});
	unsigned terrainEditDepth = 0;
	bool terrainEditChanged = false, terrainRoutesChanged = false;
	bool terrainHealthEffects = false;
	bool terrainMovementModifiers = false, airTerrainConstraints = false, projectileBlockingTerrain = false;
	// Load-only discriminator, grouped with flags to preserve hot storage offsets.
	Sint32 loadedHistoricalGrowthVersion = 0;
	bool loadedLegacyGrowth144 = false;
	bool loadedLegacyGrowth145 = false;
	void updateTerrainSummary();
	void finishTerrainEdit();
public:
	// Bulk discovery replacements also invalidate the combined snapshot plane.
	void markDiscoveryChanged() { ++discoveryRevisionValue; visibilityChanges.markAll(); }
	Uint64 discoveryRevision() const { return discoveryRevisionValue; }
	WorldRandomStreams worldRandom;
	EntityRandom& privateRandom(RandomDomain domain);
	std::array<Uint64, 5> snapshotGenerations() const
	{ return {terrainChanges.generation, resourceChanges.generation, occupancyChanges.generation, areaChanges.generation, visibilityChanges.generation}; }
	const MapState::ChunkGeometry& chunks() const { return chunkGeometry; }
	const MapState::ChangeTracker& changes(MapState::TrackedArray array) const
	{
		switch (array)
		{
		case MapState::TrackedArray::Terrain: return terrainChanges;
		case MapState::TrackedArray::Resources: return resourceChanges;
		case MapState::TrackedArray::Occupancy: return occupancyChanges;
		case MapState::TrackedArray::Areas: return areaChanges;
		default: return visibilityChanges;
		}
	}
	// Immutable terrain costs shared by independent resumed searches.
	std::shared_ptr<const std::vector<Uint8>> frozenWaterSnapshot() const;
	void configureCompute(unsigned threads);
	ComputeExecutor &computeExecutor() { return compute; }
    const ComputeExecutor &computeExecutor() const { return compute; }
    void publishWorldResultsDue();
    void submitWorldWork(const SimulationSnapshot::Handle&);
    void stageResourceGrowth();
    void finishResourceGrowth(JoinContext context);
    // Verification join at no tick.
    void finishResourceGrowth();
    void setResourceGrowthDelay(unsigned delay);
    unsigned resourceGrowthDelay() const;
    const ResourceGrowth::Metrics& resourceGrowthMetrics() const;

	void saveRuntimeState(GAGCore::OutputStream *stream) const;
	void loadRuntimeState(GAGCore::InputStream *stream, Sint32 versionMinor);

	// === Tile geometry (cross-slice) ===
	//! Bit-shift converting a tile index to its top-left pixel coordinate
	//! (i.e. log2 of TILE_PX). Used by mapCaseToPixelCase and friends in
	//! MapView.cpp / TypeSteps.cpp.
	static constexpr int TILE_PIXEL_SHIFT = 5;
	//! Side length of one map tile in screen pixels (1 << TILE_PIXEL_SHIFT).
	static constexpr int TILE_PX = 32;
	//! Half-tile in pixels — used when centring sprites / bullets on a tile.
	static constexpr int HALF_TILE_PX = 16;

	//! Sentinel returned by Map::getTerrainType for a cell whose corners hold
	//! different terrains. Callers test for `< 0` / `== TERRAIN_TYPE_UNKNOWN`.
	static constexpr int TERRAIN_TYPE_UNKNOWN = -1;


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
	//! Set by the game: finishes its hiring (and with true, team statistics) work
	//! before the map reconfigures compute, without the map linking Game.
	std::function<void(bool teamStatistics)> drainGameWork;
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
		return Uint32(growthCoverage[coordToIndex(x, y)] >> (band * TEAM_MAX_COUNT)) &
			((Uint32(1) << TEAM_MAX_COUNT) - 1);
	}
	//! Do a step associated with map (grow resources and process bullets)
	// Applies live map mutations only. The Game observation boundary publishes
	// due fields, captures the completed world, and submits subsequent jobs.
	void syncStep(Uint32 stepCounter);
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
	void setMapBuildingsDiscovered(int x, int y, Uint32 sharedVision, Team *teams[TEAM_MAX_COUNT]);

	//! Make the building at rect (x, y, w, h) visible for all teams in sharedVision (mask).
	void setMapBuildingsDiscovered(int x, int y, int w, int h, Uint32 sharedVision, Team *teams[TEAM_MAX_COUNT]);
	
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
		return areaCells[coordToIndex(x, y)].forbidden&teamMask;
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
		return areaCells[coordToIndex(x, y)].guard&teamMask;
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
		return areaCells[coordToIndex(x, y)].clear&teamMask;
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
		return areaCells[coordToIndex(x, y)].farm&teamMask;
	}
	
	// These rebuild the render-only displayed*View caches from the authoritative
	// areaCells[] bits, and are only meaningful for the locally-displayed team (the one
	// whose areas are drawn on screen). They do not touch checkSum() state.
	//! Rebuild displayedForbiddenView from areaCells[].forbidden for the given team.
	void computeDisplayedForbidden(int teamNumber);
	//! Rebuild displayedGuardAreaView from areaCells[].guard for the given team.
	void computeDisplayedGuardArea(int teamNumber);
	//! Rebuild displayedClearAreaView from areaCells[].clear for the given team.
	void computeDisplayedClearArea(int teamNumber);
	//! Rebuild displayedFarmAreaView from areaCells[].farm for the given team.
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
	inline const Tile getTile(int x, int y) const
	{
		return getTile(size_t(coordToIndex(x, y)));
	}


	//! Canonical gameplay identity; never inferred from art in a simulation query.
	std::shared_ptr<const MapAssetBundle> frozenAssetBundle() const { return assetBundleValue; }
    void editCustomEntry(std::string_view key, std::string_view definition, std::string_view artwork);
    void updateSet(std::string_view source, std::string_view oldVersion, const std::vector<std::string>& selected = {});
    void importSet(std::string_view json, const std::vector<std::string>& selected = {});
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
	void importTerrainDefinitions(std::string_view json);
	// === Vertex terrain ===
	TerrainType vertexTerrainAt(size_t index) const { return vertexTerrain[index]; }
	TerrainType vertexTerrainAt(int x, int y) const { return vertexTerrain[coordToIndex(x, y)]; }
	std::span<const TerrainType> vertexTerrainState() const { return vertexTerrain; }
	//! The corners of cell (x,y): top-left, top-right, bottom-left, bottom-right.
	std::array<TerrainType, 4> cellCorners(int x, int y) const
	{
		return {vertexTerrainAt(x, y), vertexTerrainAt(x + 1, y), vertexTerrainAt(x, y + 1), vertexTerrainAt(x + 1, y + 1)};
	}
	std::array<TerrainType, 4> cellCorners(size_t index) const { return cellCorners(int(index & wMask), int(index >> wDec)); }
	//! Write one vertex; the four cells around it are re-derived immediately.
	void setVertexTerrain(size_t index, TerrainType type);
	void setVertexTerrain(int x, int y, TerrainType type) { setVertexTerrain(size_t(coordToIndex(x, y)), type); }
	//! Replace every vertex at once.
	void assignVertexTerrain(std::span<const TerrainType> vertices);
	void fillTerrain(TerrainType type);
	//! Paints the listed vertices (unwrapped coordinates allowed) with type.
	//! With beaches, grass and water never meet: an opposite vertex next to a
	//! painted grass or water vertex, outside the painted set, becomes sand.
	//! Returns every vertex index that changed, beaches included.
	std::vector<size_t> paintVertices(const std::vector<std::pair<int, int>> &vertices, TerrainType type,
									  bool beaches = true);
	//! Paints the square of 2*(l/2)+1 vertices a side centred on (x,y), with
	//! beaches; l of 0 or 1 paints the single vertex.
	void paintVertexSquare(int x, int y, TerrainType type, int l);
	//! Turns every grass vertex next to water, and every water vertex next to
	//! grass, into sand. Reads the terrain as it was, so the order of the scan
	//! does not matter.
	void layBeaches();
	// === Cell rules ===
	std::uint16_t cellRuleAt(size_t index) const { return cellRules[index]; }
	const CellRule &cellRule(size_t index) const { return cellRuleData[cellRules[index]]; }
	const CellRuleTable &cellRuleTableRef() const { return *cellRuleTable; }
	std::shared_ptr<const CellRuleTable> frozenCellRules() const { return cellRuleTable; }
	std::span<const Uint16> cellRuleState() const { return cellRules; }
	//! A cell's terrain when all four corners agree, otherwise MIXED_TERRAIN.
	TerrainType terrainTypeAt(size_t index) const
	{
		const auto &corners = cellRuleData[cellRules[index]].corners;
		return corners[0] == corners[3] ? corners[0] : MIXED_TERRAIN;
	}
	TerrainType terrainTypeAt(int x, int y) const { return terrainTypeAt(size_t(coordToIndex(x,y))); }
	const TerrainProperties &terrainPropertiesAt(size_t index) const
	{
		return cellRuleData[cellRules[index]].properties;
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
	std::uint64_t terrainGeneration() const { return terrainGenerationValue; }
    std::uint64_t staticMaterialSourceGeneration() const { return staticMaterialSourceGenerationValue; }
	std::shared_ptr<const std::vector<TerrainType>> frozenVertexSnapshot() const;
	std::shared_ptr<const TerrainMovementSnapshot>
	frozenTerrainMovementSnapshot(unsigned swim) const;
	bool hasTerrainMovementModifiers() const { return terrainMovementModifiers; }
    //! Cheapest cardinal and diagonal entry costs among the cell rules present
    //! for a movement class: the floors of the point-to-point search heuristic.
    gradient_kernel::EntrySteps routeStepFloor(int movement) const { return terrainRouteFloors[movement]; }
    bool hasMovementCostModifiers(int movement) const
    { return movement==AIR_MOVEMENT_CLASS ? hasAirTerrainConstraints() : hasTerrainMovementModifiers(); }
    unsigned movementQueueBuckets(int movement) const
    { return movement==AIR_MOVEMENT_CLASS ? 256u : terrainQueueBuckets(); }
    bool isFreeForMovement(int x, int y, int movement, Uint32 teamMask) const
    {
        if(movement==AIR_MOVEMENT_CLASS)
            return isFreeForAirUnit(x,y);
        return (movement!=WATER_ONLY_CLASS || terrainPropertiesAt(x,y).swimmable)
            && isFreeForGroundUnit(x,y,movement>0,teamMask);
    }
	bool hasTerrainHealthEffects() const { return terrainHealthEffects; }
	bool hasAirTerrainConstraints() const { return airTerrainConstraints; }
    // Forbidden paint applies only to ground movement. Transient air traffic
    // uses local avoidance; neither disables direct flight.
    bool hasAirRoutingConstraints() const;
	bool hasProjectileBlockingTerrain() const { return projectileBlockingTerrain; }
	bool projectilePathClear(Sint32 x0, Sint32 y0, Sint32 x1, Sint32 y1) const;
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
	//! Paints the four corner vertices of a cell, so the cell becomes uniform
	//! and the cells around it transitions. No beaches are laid.
	void paintCell(size_t index, TerrainType type);
	void paintCell(int x, int y, TerrainType type) { paintCell(coordToIndex(x,y), type); }
	int getTerrainType(int x, int y) const
	{
		const auto type = terrainTypeAt(x,y);
		return type == MIXED_TERRAIN ? TERRAIN_TYPE_UNKNOWN : int(type);
	}

	const ResourceRegistry& resourceRegistry() const { return *resourceRegistryValue; }
    std::shared_ptr<const ResourceRegistry> frozenResourceRegistry() const { return resourceRegistryValue; }
    std::span<const Uint32> resourceStockIndexState() const { return resourceStockIndices; }
    std::span<const std::array<Uint16, MaterialCount>> resourceStockState() const { return resourceStocks; }
    int resourceScarcityLevel() const;
    // Borrowed read-only view over the authoritative records; the shared
    // MapState queries read these arrays directly, as do engine snapshots.
    //! Borrowed read-only view of the live cell arrays and registries plus
    //! the game's growth settings; stateView() adds the growth field.
    MapState::View cellView() const;
    MapState::View stateView() const;
    //! The live view itself, kept current by refreshLiveView(); GenerationMap
    //! (map/generator/core/GenerationWorld.h) reads cells through it.
    const MapState::View &liveView() const { return liveCells; }
    //! The vertex terrain array itself, for the same reader.
    const std::vector<TerrainType> &vertexTerrainArray() const { return vertexTerrain; }
    //! Rebind liveCells after an array, registry or habitat table is replaced.
    void refreshLiveView();
    //! Expected growth opportunities per visit for a deposit of this type here.
    std::uint32_t resourceGrowthRateAt(size_t index,int resourceType) const;
	void installResourceDefinitions(const std::string& json);
	const ResourceProperties& resourceProperties(ResourceId type) const
	{
	    return resourceRegistry().properties(type);
	}
    const ResourceProperties& resourcePropertiesByIndex(int type) const { return resourceProperties(static_cast<ResourceId>(type)); }
	Uint16 materialAmountAtSlot(size_t index, int material) const { return MapState::materialAmountAt(liveCells, index, material); }
	Uint16 materialAmountAt(size_t index, MaterialId material) const { return materialAmountAtSlot(index, static_cast<int>(material)); }
	MaterialMask materialMaskAt(size_t index) const { return MapState::materialMaskAt(liveCells, index); }
    std::array<Uint16,MaterialCount> materialStocksAt(size_t index) const;
	MaterialMask resourceMaterialMaskAt(size_t index) const;
	bool resourceBlocksGround(size_t index) const { return MapState::resourceBlocksGround(liveCells, index); }
	bool resourceBlocksAir(size_t index) const { return MapState::resourceBlocksAir(liveCells, index); }
	bool resourceBlocksBuilding(size_t index) const { return MapState::resourceBlocksBuilding(liveCells, index); }
	bool resourceVisibleToHarvest(size_t index) const { return MapState::resourceVisibleToHarvest(liveCells, index); }
	bool hasMaterialSourceSlot(int material) const { return MapState::hasMaterialSource(liveCells, material); }
	bool hasMaterialSource(MaterialId material) const { return hasMaterialSourceSlot(static_cast<int>(material)); }
	void setMaterialAmount(size_t index, MaterialId material, Uint16 amount);
    void setMaterialAmountSlot(size_t index,int material,Uint16 amount) { if (validMaterial(material)) setMaterialAmount(index,static_cast<MaterialId>(material),amount); }
	bool growResourceStock(size_t index);
	bool isMaterialTakeable(int x,int y,MaterialId material) const { return materialAmountAt(coordToIndex(x,y),material)>0; }
    bool isMaterialTakeableSlot(int x,int y,int material) const { return validMaterial(material) && isMaterialTakeable(x,y,static_cast<MaterialId>(material)); }
	const Resource& getResource(int x, int y) const
	{
		return resourceCells[coordToIndex(x, y)].resource;
	}

	const Resource& getResource(size_t pos) const
	{
		return resourceCells[pos].resource;
	}

	// Explicit cell writes preserve existing topology/refresh timing while keeping
	// derived seed data coherent. Reads never invalidate preparation caches.
	size_t cellCount() const { return resourceCells.size(); }
	std::span<const MapState::ResourceCell> resourceState() const { return resourceCells; }
    // Caller-owned farm flood scratch, for readers that must not mutate the map.
    struct FarmFloodScratch { std::vector<Uint32> stamps; std::vector<size_t> queue; Uint32 generation = 0; };
    bool activeClaimsEnabled() const { return resourceClaimsEnabled; }
    //! New-game setup: material destination planes plus resource claims.
    //! Saved games keep whatever they stored (claims off before format 213).
    void enableResourceClaims();
    //! New-game setup without destination planes or claims.
    void disableResourceClaims();
    ResourceClaims::StepResult nextClaimRouteStep(Unit&, int*, int*);
    //! True unless the held claim has a published route (computing, awaiting
    //! admission or not yet requested).
    bool claimRouteComputing(const Unit&) const;
    //! Gradient demand, and optionally movement evidence, for the held claim's
    //! natural-material field, so adaptive refresh keeps serving claim workers
    //! as it serves gradient followers (mirrors pathfindMaterial).
    void claimPursuitGradientSignal(Unit&, bool recordOutcome, bool moved);
    void cancelClaimRoute(ResourceClaims::Owner);
    void flushClaimRoutes();
    void publishClaimRoutes();
    Uint32 activeClaimRouteChecksum(bool includePending) const;
    Uint32 activeClaimSnapshotChecksum() const;
    ResourceClaims::ClaimRecoverySelection beginClaimRecoverySelection(const Unit&);
    void finishClaimRecoverySelection(const Unit&, unsigned probe, unsigned patternSize);
    unsigned recordClaimFailure(const Unit&, ResourceClaims::ClaimFailureKind);
    void claimRecoveryHarvested(const Unit&);
    void forgetClaimRecovery(ResourceClaims::Owner);
    //! Hire-time claim check: search, from the state frozen just before this
    //! team's hiring phase, every free worker on a published fetch shortlist
    //! of a building needing units. Team::syncStep calls it immediately before
    //! Team::updateAllBuildingTasks; Unit::materialClaimHireAllowed reads it.
    void prepareClaimHireTargets(int team);
    const ResourceClaims::HireTargetIndex& claimHireTargetIndex() const { return claimHireTargets; }

    void saveActiveClaimRoutes(GAGCore::OutputStream*) const;
    void loadActiveClaimRoutes(GAGCore::InputStream*, Sint32 versionMinor);
    void rebuildActiveMaterialClaims();
    //! Throws unless the ledger and claim routes are exactly what load would
    //! rebuild from the units (see validateActiveMaterialClaims). Saves run it
    //! when GLOB2_CLAIM_AUDIT is set (the test runner sets it).
    void auditActiveMaterialClaims() const;
    //! Whether saves run auditActiveMaterialClaims (from GLOB2_CLAIM_AUDIT).
    //! Tests that save deliberately corrupt state turn it off.
    static bool claimSaveAudit;
    void invalidateMaterialClaimOwners(std::vector<ResourceClaims::Owner>,Uint32 teams=~Uint32(0));
    void invalidateMaterialClaimsAt(size_t,bool source=true,bool access=true,Uint32 teams=~Uint32(0));
    std::vector<ResourceClaims::Owner> farmMaterialClaimOwners(size_t,Uint32 teams=~Uint32(0),MaterialMask materials=~MaterialMask(0));
    void revalidateMaterialClaimOwners(std::vector<ResourceClaims::Owner>,bool checkFarmConnectivity=true);
    void invalidateAllActiveMaterialClaims();
    void checkActiveClaimRules();
    bool activeClaimFarmConnected(const ResourceClaims::Claim&, Uint32 teamMask);
    bool activeClaimFarmConnectedIn(FarmFloodScratch&, const ResourceClaims::Claim&, Uint32 teamMask) const;
    bool takeClaimedMaterial(const ResourceClaims::Claim&, Uint32 teamMask);
    //! Terrain-only connected components for a movement class (claim prefilter).
    std::shared_ptr<const ResourceClaims::TerrainComponentLabels> claimTerrainComponents(int movement) const
    { return claimTerrainComponentCache.labels(*this, movement); }
    Uint64 claimTerrainComponentRebuilds() const { return claimTerrainComponentCache.rebuilds(); }
    //! Snapshot the team target occupancy; Team::syncStep calls this before its units step.
    void prepareClaimContention(int team);
    //! Same-team workers other than `unit` fetching `material` whose target
    //! lies within ContentionRadius of (x,y); snapshots lazily if stale.
    unsigned claimContentionOthers(const Unit& unit,int material,int x,int y);
	const ResourceClaims::Ledger& materialClaimLedger() const { return materialClaims; }
	//! Record a claim without the capacity check (load path only, see
	//! rebuildActiveMaterialClaims).
	bool recordMaterialClaim(ResourceClaims::Owner owner, const ResourceClaims::Claim& claim)
	{ return materialClaims.record(owner, claim); }
	//! The terms of a claim on material of resource type `resource` picked up
	//! at `access` by team `teamMask` (ResourceClaims::ClaimTerms).
	ResourceClaims::ClaimTerms claimTerms(Uint16 resource, int material, Uint32 access, Uint32 teamMask) const;
	//! The seed floor a pooled claim on this yield keeps back for regrowth.
	Uint16 pooledClaimReserve(const YieldProperties& yield) const;
	//! Ledger availability: one claimant per spendable unit of stock. Selection,
	//! acquisition and revalidation all use this same rule.
	bool materialClaimAvailable(ResourceClaims::Owner owner, const ResourceClaims::Claim& claim, Uint32 capacity, bool infinite = false) const
	{ return materialClaims.available(owner, claim, capacity, infinite); }
	bool acquireMaterialClaim(ResourceClaims::Owner owner, const ResourceClaims::Claim& claim, Uint32 capacity, bool infinite = false)
	{ return materialClaims.acquire(owner, claim, capacity, infinite); }
	bool releaseMaterialClaim(ResourceClaims::Owner owner)
	{ return materialClaims.release(owner); }
	std::span<const MapState::OccupancyCell> occupancyState() const { return occupancyCells; }
	std::span<const MapState::AreaCell> areaState() const { return areaCells; }
	const Tile getTile(size_t index) const
	{
		Tile tile;
		tile.resource = resourceCells[index].resource;
		tile.fertility = resourceCells[index].fertility;
		tile.canResourcesGrow = resourceCells[index].mayGrow;
		tile.building = occupancyCells[index].building;
		tile.groundUnit = occupancyCells[index].groundUnit;
		tile.airUnit = occupancyCells[index].airUnit;
		tile.forbidden = areaCells[index].forbidden;
		tile.guardArea = areaCells[index].guard;
		tile.clearArea = areaCells[index].clear;
		tile.farmArea = areaCells[index].farm;
		tile.scriptAreas = scriptAreaCells[index];
		return tile;
	}
	// Restores stored cell data. Terrain lives on vertices and is not part of a Tile.
	void replaceTile(size_t index, const Tile &tile);
	void replaceTile(int x, int y, const Tile &tile) { replaceTile(coordToIndex(x, y), tile); }
	// Optional initial stocks seed explicit material units instead of catalog initial stocks.
	void replaceResource(size_t index, const Resource &resource, const std::array<Uint16, MaterialCount> *stocks = nullptr);
	void replaceResource(int x, int y, const Resource &resource) { replaceResource(coordToIndex(x, y), resource); }
	void setResourceAmount(size_t index, Uint32 amount);
	void setFertility(int x, int y, Uint16 value)
	{
		const auto index = coordToIndex(x, y);
		auto &stored = resourceCells[index].fertility;
		const bool changed = stored != value;
		stored = value;
		if (changed) markResource(index);
	}
	void setResourcesGrow(int x, int y, Uint8 value)
	{
		const auto index = coordToIndex(x, y);
		auto &stored = resourceCells[index].mayGrow;
		// Preserve the stored legacy byte in shared live/snapshot records.
		const bool changed = stored != value;
		stored = value;
		if (changed) markResource(index);
	}
	// Raw mask replacement for order application/import; callers retain their
	// existing topology-generation and displayed-overlay updates.
	void setAreaMask(size_t index, Uint32 Tile::*field, Uint32 value);
	Uint32 paintedAreaTileCount(AreaType kind, int team) const
	{
		assert(unsigned(kind)<paintedAreaCounts.size() && unsigned(team)<TEAM_MAX_COUNT);
		return paintedAreaCounts[kind][team];
	}
	bool hasClearingArea(int team) const
	{
		return paintedAreaTileCount(ClearingArea,team) ||
			(farmAreasEnabled() && paintedAreaTileCount(FarmArea,team));
	}

	Uint32 getForbidden(int x, int y) const
	{
		return areaCells[coordToIndex(x, y)].forbidden;
	}
	
	Uint8 getExplored(int x, int y, int team) const
	{
		return exploredArea[team][coordToIndex(x, y)];
	}
	

	//! A bump throws away every cached route field in the game, so only paint
	//! a tile that is not already in the state being asked for.
	void addForbidden(int x, int y, Uint32 teamNum);
	void removeForbidden(int x, int y, Uint32 teamNum);

	void addClearArea(int x, int y, Uint32 teamNum)
	{
		setAreaMask(coordToIndex(x,y), &Tile::clearArea, areaCells[coordToIndex(x,y)].clear | teamNumberToMask(teamNum));
	}
	
	void addGuardArea(int x, int y, Uint32 teamNum)
	{
		setAreaMask(coordToIndex(x,y), &Tile::guardArea, areaCells[coordToIndex(x,y)].guard | teamNumberToMask(teamNum));
	}

	void addFarmArea(int x, int y, Uint32 teamNum)
	{
		setAreaMask(coordToIndex(x,y), &Tile::farmArea, areaCells[coordToIndex(x,y)].farm | teamNumberToMask(teamNum));
	}

	
	// Literal identity queries retained for authoring and script introspection.
	bool isWater(int x, int y) const { return terrainTypeAt(x,y) == WATER; }
	bool isWater(unsigned pos) const { return terrainTypeAt(pos) == WATER; }
	bool isGrass(int x, int y) const { return terrainTypeAt(x,y) == GRASS; }
	bool isGrass(unsigned pos) const { return terrainTypeAt(pos) == GRASS; }
	bool isSand(int x, int y) const { return terrainTypeAt(x,y) == SAND; }

	bool isResource(int x, int y) const
	{
		return getResource(x, y).type != NO_RES_TYPE;
	}

    bool isClearableResourceForMaterials(int x,int y,MaterialMask materials) const
    {
        const auto index=coordToIndex(x,y);
        const auto& r=resourceCells[index].resource;
        if (r.type==NO_RES_TYPE || !resourcePropertiesByIndex(r.type).clearable) return false;
        // Clearing targets the configured deposit, including empty persistent
        // obstacles; harvested-stock availability is irrelevant to removal.
        const auto mask=resourcePropertiesByIndex(r.type).materialMask;
        return (materials & mask & AllMaterials) != 0;
    }

	bool isResource(int x, int y, int *resourceType) const
	{
		const Resource &resource = getResource(x, y);
		if (resource.type == NO_RES_TYPE)
			return false;
		*resourceType = resource.type;
		return true;
	}

	bool canResourcesGrow(int x, int y) const
	{
		return resourceCells[coordToIndex(x, y)].mayGrow && terrainPropertiesAt(x,y).resourcesGrow;
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
	//! entry: flood from that tile only (a claim's access tile) instead of the
	//! unit's 3x3. availableOwner: consider only tiles where the claim ledger
	//! still has room for that owner's claim on the material.
	std::optional<size_t> pickFarmHarvestTileSlot(int x, int y, int resourceType, Uint32 teamMask, std::optional<size_t> entry = std::nullopt, std::optional<ResourceClaims::Owner> availableOwner = std::nullopt);
    // The same pick with caller-owned flood scratch and no map mutation, for
    // parallel readers of a frozen map (hire target searches).
    std::optional<size_t> pickFarmHarvestTileSlotIn(FarmFloodScratch& scratch, int x, int y, int resourceType, Uint32 teamMask, std::optional<size_t> entry = std::nullopt, std::optional<ResourceClaims::Owner> availableOwner = std::nullopt) const;

	//! Revalidate the touched target's requested stock before delivery. Eligible
	//! farm yields pool from the connected field; destructive or nonfarmable
	//! yields harvest the touched deposit directly. Depleted targets and fields
	//! with no surplus produce nothing.
	bool takeHarvest(int x,int y,int dx,int dy,MaterialId material,Uint32 teamMask);
    bool takeHarvestMaterialSlot(int x,int y,int dx,int dy,int material,Uint32 teamMask) { return validMaterial(material) && takeHarvest(x,y,dx,dy,static_cast<MaterialId>(material),teamMask); }

private:
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
	Uint16 getGroundUnit(int x, int y) const { return occupancyCells[coordToIndex(x, y)].groundUnit; }
	Uint16 getAirUnit(int x, int y) const { return occupancyCells[coordToIndex(x, y)].airUnit; }
	Uint16 getBuilding(int x, int y) const { return occupancyCells[coordToIndex(x, y)].building; }
	
	void setGroundUnit(int x, int y, Uint16 guid)
	{
		const auto index = coordToIndex(x, y);
		auto &stored = occupancyCells[index].groundUnit;
		const bool changed = stored != guid;
		stored = guid;
		if (changed) markOccupancy(index);
	}
	void setAirUnit(int x, int y, Uint16 guid)
	{
		const auto index = coordToIndex(x, y);
		auto &stored = occupancyCells[index].airUnit;
		const bool changed = stored != guid;
		stored = guid;
		if (changed) markOccupancy(index);
	}
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
	std::span<const Uint16> scriptAreaState() const { return scriptAreaCells; }
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
	void setTerrainSeed(Uint32 seed);
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
	//! Highest cost a pathfinding gradient can hold (see GradientConstants.h).
	static constexpr int GRADIENT_COST_LIMIT = 0xFFFF - 1 - 1 - 42;
	
	//! Pathfinding, gradients and the gradient pipeline; see gradient/MapRouting.h.
	//! Callers include that header; Map.h only forward-declares MapRouting.
	MapRouting &routing() { return *routing_; }
	const MapRouting &routing() const { return *routing_; }

	//! Bumped whenever a footprint or a forbidden mask changes. A route field
	//! spans the map, so any such change may cross it: each field records the
	//! value it was built at and is rebuilt on use once it differs. Resources
	//! and immobile units are left out on purpose; they change far too often
	//! and a unit blocked by one forces its own rebuild in pathfindBuilding.
	Uint32 topologyGeneration;
	void bumpTopologyGeneration() { topologyGeneration++; }
	
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
	std::vector<CoverageBuilding> growthCoverageBuildings[TEAM_MAX_COUNT];
	Uint32 growthCoverageGeneration[TEAM_MAX_COUNT]{};
	bool growthCoverageValid = false;
private:
	std::vector<MapState::ResourceCell> resourceCells;
	std::vector<MapState::OccupancyCell> occupancyCells;
	std::vector<MapState::AreaCell> areaCells;
	std::vector<Uint16> scriptAreaCells;
public:
	Uint64 identityValue = 0;
	Uint32 terrainSeedValue = 0;
	Sint32 w, h;
	Sint32 wMask, hMask;
	Sint32 wDec, hDec;
	
public:
    // here we handle terrain
	// mapDiscovered
	bool arraysBuilt; // if true, the next pointers(arrays) have to be valid and filled.
	std::vector<Uint32> mapDiscovered;
	std::vector<Uint32> fogOfWarA;
	std::vector<Uint32> fogOfWarB;
	Uint32* fogOfWar = nullptr; // if valid, either points to &fogOfWarA[0] or &fogOfWarB[0]
	//! Render-only overlay caches for the locally-displayed team's areas (forbidden /
	//! guard / clear / farm). These mirror the per-team bits in areaCells[].{forbidden,guard,
	//! clear,farm} but only for displayedTeam, so the renderer can query one tile cheaply.
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
	std::optional<size_t> pickFarmTileWith(std::vector<Uint32>& stamps, std::vector<size_t>& queue, Uint32& generation, int x, int y, int resourceType, Uint32 teamMask, std::optional<size_t> entry, std::optional<ResourceClaims::Owner> availableOwner) const;
	//! Team whose view is locally displayed. Mirror of GameGUI::localTeamNo, used only to
	//! decide whether to refresh the displayed*View caches above. Not in checkSum.
	Sint32 displayedTeam = NO_DISPLAYED_TEAM;
	
	///This is the maximum fertility of any point on the map
	Uint16 fertilityMaximum;
	const Fertility::GrowthCache& resourceGrowthField() const;
	mutable Fertility::GrowthCache growthCache;
	mutable std::mutex growthCacheMutex;
	
public:
	// Used to guide explorers
	//[int team]
	// 0=unexplored, 255=just explored
	Uint8 *exploredArea[TEAM_MAX_COUNT];
	
	/// This shows how many "claims" there are on a particular resource square
	/// This is so that not all 150 free units go after one piece of wood
	/// Each square is the gid of the claiming unit
	Uint16 *clearingAreaClaims[TEAM_MAX_COUNT];
	
	/// These are integers that tell whether an immobile unit is standing on the
	/// square, and if so, what team number it is. In terms of the engine, these
	/// are treated like forbidden areas
private:

public:
	
protected:

	Uint8 **listedAddr;
	size_t size;

	Sector *sectors;
	Sint32 wSector, hSector;
	int sizeSector;
	

public:
	Uint32 checkSum(bool heavy, bool includePending = true, ChecksumScheme scheme = ChecksumScheme::Current);
	Sint32 warpDist1d(int p, int q, int l) const;///distance of coordinates p and q on a loop of length l
	Sint32 warpDistSquare(int px, int py, int qx, int qy) const; //!< The distance^2 between (px, py) and (qx, qy), warp-safe.
	Sint32 warpDistMax(int px, int py, int qx, int qy) const; //!< The max distance on x or y axis, between (px, py) and (qx, qy), warp-safe.
	void dumpGradient(Uint8 *gradient, const std::string filename = "gradient.dump.pgm");

public:
	void smoothResources(int times);

private:
};
