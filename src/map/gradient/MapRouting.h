// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière
// Copyright (C) 2006 Bradley Arsenault

#pragma once

// The routing half of Map: pathfinding queries, the per-team material, market,
// forbidden, guard and clear gradients with their stale flags, the material
// routing cache, building gradient leases and the gradient pipeline. Map owns
// one MapRouting (Map::routing()); Map.h only forward-declares it, so the many
// translation units that read cells without routing never see this API.
//
// The implementation files keep including Map.h and reach cells through the
// owner's inline per-cell accessors (coordToIndex, terrainPropertiesAt,
// getBuilding, getGroundUnit, resourceBlocks*), so per-cell kernels inline
// exactly as they did when these were Map members. A query pays one extra
// pointer load to reach MapRouting, never one per cell.

#include "Map.h"
#include "ComputeTelemetry.h"
#include "BuildingRoute.h"
#include "gradient/FieldReadiness.h"
#include "gradient/GradientFieldIdentity.h"
#include "gradient/GradientQueryContext.h"
#include "gradient/ResourceGradientContradictions.h"
#include "field/GradientDestinationPlanes.h"
#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

struct GradientTargetCell;
namespace MapState { struct BuildingSeedPlanes; struct BuildingSeedSnapshot; }
struct GradientRuntime;
struct SharedBuildingGradient;
namespace SimulationSnapshot { struct Handle; }
namespace gradient_kernel { enum class Family; }
namespace astar { class Workspace; struct GoalResult; }

class MapRouting
{
	friend class Map;
	friend class ResourceSeedCache;
	friend struct ResourceGradientProbe;

public:
	explicit MapRouting(Map &owner);
	~MapRouting();
	MapRouting(const MapRouting &) = delete;
	MapRouting &operator=(const MapRouting &) = delete;

private:
	//! The owning map; every per-cell read goes through its inline accessors.
	Map &map;

	void seedMaterialGradientDirect(int team, Uint8 resource, int swim, Uint16 *output, const Uint16 *supplierSeeds);
	void seedMaterialGradientWithSuppliers(int team, Uint8 resource, int swim, Uint16 *output, const Building* consumer, unsigned modes);
	mutable std::unique_ptr<GradientRuntime> gradientRuntime;
	// Scheduled building gradients (MapGradientScheduling.cpp).
	void stageBuildingGradientPreparation();
	void prepareStagedBuildingGradients(const SimulationSnapshot::Handle& foundation);
	void resetBuildingGradientPipeline() noexcept;
	Building *buildingGradientDestination(int team, int id, Uint32 identity) const;
	void saveBuildingGradientPipeline(GAGCore::OutputStream *stream) const;
	void loadBuildingGradientPipeline(GAGCore::InputStream *stream, bool packed, int versionMinor);
	// Storage only: an idle building still drops its field and saved null state.
	// A fixed slot count avoids allocations in the pool itself.
	static constexpr std::size_t GRADIENT_BUFFER_POOL_SLOTS = 64;
	static constexpr std::size_t GRADIENT_BUFFER_POOL_BYTES = 2 * 1024 * 1024;
	std::array<Uint16 *, GRADIENT_BUFFER_POOL_SLOTS> idleGradientBuffers{};
	std::size_t idleGradientBufferCount = 0;
	std::mutex gradientBufferPoolMutex;
	void clearGradientBufferPool();

public:
	Uint16 *acquireBuildingGradientBuffer();
	void recycleBuildingGradientBuffer(Uint16 *buffer);
	std::vector<std::pair<std::string,Uint64>> gradientExecutionMetrics() const;
	// Live field seeding retains its serial production order.
	template<class Function> void initializeGradientCells(Function function) const
	{
		function(0, map.size);
	}

	struct GradientPipelineStatus
	{
		bool enabled = false;
		unsigned workers = 0, delay = 0;
		std::size_t pending = 0;
		std::uint64_t jobs = 0, published = 0, discarded = 0;
		std::uint64_t maxPending = 0;
	};
	bool gradientPipelineEnabled() const;
	GradientPipelineStatus gradientPipelineStatus() const;
	// Owner selects/reserves and captures inputs before dispatch. Deferred jobs
	// seed and propagate private data from immutable leases; join before save/reconfigure.
	void stagePeriodicGradientPreparation();
	bool hasPendingGradientPreparation() const;
	void preparePendingGradient(const SimulationSnapshot::Handle& foundation);
	void advanceGradientPipeline();
	void finishGradientPipeline(JoinContext context = {JoinReason::Verification, ComputeTelemetry::NoTick});
	// Live-world resets retain supplier turnover history; world replacement must
	// discard derived supplier ledgers before teams are destroyed or reconstructed.
	void resetGradientPipeline(bool replacingWorld=false) noexcept;
	void setGradientWorkerCount(unsigned workers);
	void configureGradientPipeline(unsigned workers, unsigned delay);
	void configureGradientWorkStats(const std::string& csvPath);
	void configureResourceGradientExploration(const std::string& intervals, Uint32 seed,
		const std::string& decisionPath);
	void applyGradientBackendRequest();
	void configureGradientExecution();
	// Scheduled building gradients (MapGradientScheduling.cpp). Team stepping
	// requests refreshes of existing fields and keeps serving the old ones;
	// after the tick up to four requests per team are staged, captured at the
	// observation boundary, built on workers and published
	// buildingGradientDelay ticks later. Cold fields queue an initial resumable search; area and
	// team-wide resets keep old walking fields serving instead of making them
	// cold (Building::keepsStaleGradients).
	//! Why a building walking field was built on the owner. cold_* name what
	//! last dropped the field (never built, idle eviction, the building's own
	//! reset, a team-wide reset, an area edit).
	enum class BuildingSyncReason : unsigned char
	{
		Inactive, ColdNew, ColdIdle, ColdOwn, ColdTeam, ColdArea, Overflow, Other, Count
	};
	static const char *buildingSyncReasonName(BuildingSyncReason reason);
	struct BuildingGradientPipelineStatus
	{
		bool enabled = false;
		unsigned delay = 0;
		std::size_t pending = 0, queued = 0;
		std::uint64_t jobs = 0, published = 0, discarded = 0, synchronous = 0, maxPending = 0;
		std::array<std::uint64_t, std::size_t(BuildingSyncReason::Count)> synchronousByReason{};
	};
	//! Tags the next synchronous building build for the counters above.
	void noteBuildingSyncReason(BuildingSyncReason reason);
	//! A team-local edit moves current fields to the new topology generation;
	//! pending results captured at the old one move with them.
	void carryPendingBuildingGenerations(Uint32 from, Uint32 to);
	BuildingGradientPipelineStatus buildingGradientPipelineStatus() const;
	//! Follow the header's buildingGradientDelay now; throws with pending work.
	void ensureBuildingGradientPipeline();
	bool buildingGradientPipelineActive() const;
	//! Queue initialization or refresh of a walking field (slot = routeSlot). False
	//! when the caller must rebuild synchronously (no pipeline: a map without
	//! a game).
	bool requestBuildingRefresh(Building *building, int slot);
	//! Worker depth for a scheduled walking field: every reader still resolves
	//! its cell first, so the prediction moves CPU, never values.
	int predictBuildingDepth(const Building *building, int slot) const;
	void updateTeamAreaGradients(int teamNumber);
	void seedMaterialGradient(int team, Uint8 resource, int swim, Uint16 *gradient, bool withMarkets = false, const Building* consumer = nullptr, unsigned modes = 0);
	void seedGuardAreasGradient(int team, int swim, Uint16 *gradient);
	void seedClearAreasGradient(int team, int swim, Uint16 *gradient);

	// Cost selection belongs to preparation, never the public lookup identity.
	int preparationCostClass(int team,int access) const;
	// A serving generation can predate the team mode; score it with its own costs.
	int publishedCostClass(const Uint16* field,int access=Map::SWIM_CLASS_EVEN) const;
	void recordFieldCost(const Uint16* field,int cost);
	void saveGradient(GAGCore::OutputStream*,const Uint16*,size_t,int cost) const;
	void loadGradient(GAGCore::InputStream*,Uint16*&,size_t,bool,int legacyCost=-1);
	void consolidateLegacyRoutingFields();

	//! Cost of stepping (dx, dy) into the cell at targetIndex, in gradient units.
	int stepCost(int dx, int dy, size_t targetIndex, int swimClass) const;

	// Gradients are built per team and swim class the first time a unit of that
	// class asks for one, so classes nobody uses cost nothing.
	//! withMarkets: the variant where the team's stocked markets are goals too,
	//! priced by each supplier's configured pickup penalty. Used when a
	//! consumer enables stock fetching (Building::fetchesFromMarkets).
	const Uint16* lookupMaterialGradientSlot(int team, int material, MovementAccess movementAccessValue, bool markets = false, const Building* consumer = nullptr) const;
	const Uint16* lookupForbiddenGradient(int team, MovementAccess movementAccessValue) const {
	int swim=accessClass(movementAccessValue); return forbiddenGradient[team][swim]; }
	const Uint16* lookupGuardAreasGradient(int team, MovementAccess movementAccessValue) const {
	int swim=accessClass(movementAccessValue); return guardAreasGradient[team][swim]; }
	const Uint16* lookupClearAreasGradient(int team, MovementAccess movementAccessValue) const {
	int swim=accessClass(movementAccessValue); return clearAreasGradient[team][swim]; }
	const Uint16 *getMaterialGradientSlot(int teamNumber, int resourceType, MovementAccess movementAccessValue, bool withMarkets = false, const Building* consumer = nullptr, std::optional<GradientFieldKey>* selectedKey = nullptr);
	const Uint16* getMaterialGradient(int team,MaterialId material,MovementAccess movementAccessValue,bool withMarkets=false,const Building* consumer=nullptr) {
		return getMaterialGradientSlot(team,materialIndex(material),movementAccessValue,withMarkets,consumer); }

	// Consumer-aware requests are simulation-thread only. Returned cache pointers
	// remain valid until the next consumer-aware request (which may evict them).
	unsigned materialSupplyModesSlot(const Building* consumer, int resource) const;
	const Uint16 *cachedMaterialGradientSlot(const Building* consumer, int resource, MovementAccess movementAccessValue, unsigned modes);
	bool stockSupplierEligibleSlot(const Building* supplier, const Building* consumer, int resource, unsigned modes) const;
	bool materialDestinationValidSlot(int team, int material, MovementAccess movementAccessValue, int x, int y, bool markets = false, const Building* consumer = nullptr) const;
	// Current weighted pursuit projection; future occupancy/field changes can retarget it.
	bool projectMaterialDestination(const Uint16* field, int team, int material, MovementAccess movementAccessValue, int x, int y,
		Sint32* targetX, Sint32* targetY, bool markets = false, const Building* consumer = nullptr) const;
	void setMaterialRoutingCacheBudget(Uint64 bytes);
	Uint64 materialRoutingCacheBytes() const;
	// Destination planes record, per cell, the material destination its
	// gradient leads to. Claims need them; they are set up with claims.
	void setMaterialDestinationsEnabled(bool enabled);
	bool materialDestinationsEnabled() const;
	std::shared_ptr<const gradient_kernel::OwnedDestinations> materialGradientDestinations(const Uint16* field) const;
	Uint64 materialGradientDestinationGeneration(Uint16* const* slot) const;
	void saveMaterialRoutingCache(GAGCore::OutputStream* stream) const;
	void loadMaterialRoutingCache(GAGCore::InputStream* stream, bool packed, int versionMinor);
	unsigned gradientDestination(Uint16* const*) const;
	Uint16** gradientDestination(unsigned, unsigned savedClasses=MOVEMENT_CLASS_COUNT);
	std::optional<GradientFieldKey> gradientFieldKey(Uint16* const* slot) const;
	Uint16** gradientFieldDestination(GradientFieldKey);
	GradientTargetCell captureGradientTargetCell(size_t) const;
	void gradientTargetCellChanged(size_t,const GradientTargetCell&);
	void gradientMarketSupplierMasks(size_t,GradientTargetCell&) const;
	void ensureGradientMarketTargets(int team,int material);
	bool gradientMarketSupplierGoal(GradientFieldKey,size_t) const;
	void gradientGuardCostsChanged(int team);
	void gradientAlliancesChanged(unsigned team, Uint32 previousAllies);
	void gradientSupplierTargetsChanged(int team,int material);
	bool gradientGoalValid(GradientFieldKey,int x,int y) const;
	GradientQueryResult selectGradientQueryImpl(GradientQueryContext&,const Uint16*) const;
	GradientQueryResult selectGradientQuery(const GradientQueryContext&,const Uint16*) const;
	std::optional<GradientFieldKey> materialGradientFieldKey(int team,int material,MovementAccess movementAccessValue,const Uint16*) const;
	void directGradientMovement(GradientFieldKey,int x,int y,const EntityRandom&,const Uint16*,bool moved,bool destinationValid,bool recordDemand=true);
	void gradientMovementDemand(GradientFieldKey);
	void gradientMovementOutcome(const GradientQueryContext&,const Uint16*,const GradientQueryResult&);
	bool requestGradientField(Uint16** slot, int kind, int team, int material, MovementAccess movementAccessValue, int consumer = -1, unsigned modes = 1, int refreshReason = -1);
	StepResult queryMaterialStep(EntityRandom&, int, Uint8, MovementAccess movementAccessValue, int, int, int*, int*, bool*, Building*, bool = false,
		Sint32* projectedX = nullptr, Sint32* projectedY = nullptr, bool* projectedValid = nullptr);

	StepResult queryForbiddenStep(const Uint16*, int, MovementAccess movementAccessValue, int, int, int*, int*);
	const Uint16 *getForbiddenGradient(int teamNumber, MovementAccess movementAccessValue);
	const Uint16 *getGuardAreasGradient(int teamNumber, MovementAccess movementAccessValue);
	const Uint16 *getClearAreasGradient(int teamNumber, MovementAccess movementAccessValue);
	//! Guard-area balancing: out[i] = number of the team's warriors within
	//! GUARD_CROWD_RADIUS tiles of tile i (units inside buildings excluded).
	//! Separable box sum, linear in map size. Returns false, leaving out
	//! untouched, when the team has no such warrior.
	bool computeWarriorCrowding(int teamNumber, Uint16 *out) const;

	bool materialAvailableSlot(int teamNumber, int resourceType, MovementAccess movementAccessValue, int x, int y, bool withMarkets = false, const Building* consumer = nullptr);
	bool materialAvailable(int team,MaterialId material,MovementAccess movementAccessValue,int x,int y,bool markets=false,const Building* consumer=nullptr) {
		return materialAvailableSlot(team,materialIndex(material),movementAccessValue,x,y,markets,consumer); }
	bool materialAvailableSlot(int teamNumber, int resourceType, MovementAccess movementAccessValue, int x, int y, int *dist, bool withMarkets = false, const Building* consumer = nullptr);
	bool materialAvailable(int team,MaterialId material,MovementAccess movementAccessValue,int x,int y,int* distance,bool markets=false,const Building* consumer=nullptr) {
		return materialAvailableSlot(team,materialIndex(material),movementAccessValue,x,y,distance,markets,consumer); }
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

	RouteResult queryMaterialDistance(int team, int material, MovementAccess movementAccessValue, int x, int y, int* distance = nullptr, bool withMarkets = false, const Building* consumer = nullptr);
	Uint16 getGradient(int teamNumber, Uint8 resourceType, MovementAccess movementAccessValue, int x, int y, bool withMarkets = false, const Building* consumer = nullptr)
	{
		const auto* field = getMaterialGradientSlot(teamNumber, resourceType, movementAccessValue, withMarkets, consumer);
		return field ? field[map.coordToIndex(x, y)] : GRADIENT_UNREACHABLE;
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
	//! swimClass must be in [0, MOVEMENT_CLASS_COUNT).
	GAGCore::CooperativeTask updateGlobalGradientTask(Uint8 *gradient);
	void propagateGradient(Uint16 *gradient, int swimClass, int maxCost = Map::GRADIENT_COST_LIMIT);
	void propagateGradient(Uint16 *gradient, int swimClass, int maxCost, gradient_kernel::Family family);
	//! Step toward the neighbour with the highest value minus step cost. strict requires
	//! real progress; otherwise a random sidestep to an equal cell is accepted when blocked.
	//! With guardAreaMask, only neighbours painted as a guard area for those teams count
	//! (guard-area balancing: stepping within an area).
	bool directionByGradient(EntityRandom& random, Uint32 teamMask, MovementAccess movementAccessValue, int x, int y, const Uint16 *gradient, int *dx, int *dy, bool strict, Uint32 guardAreaMask = 0) const;
	void updateMaterialGradient(int teamNumber, Uint8 resourceType, MovementAccess movementAccessValue, bool withMarkets = false);
	//! Direction toward the resource of resourceType nearest to (x, y). A target building
	//! selects which suppliers (markets, stock) its resource gradient includes.
	bool pathfindMaterial(EntityRandom& random, int teamNumber, Uint8 resourceType, MovementAccess movementAccessValue, int x, int y, int *dx, int *dy, bool *stopWork, Building *target, bool withMarkets = false);
	void pathfindRandom(Unit *unit);
	//! Idle escape toward non-damaging terrain using a lazily shared field.
	//! Checks live occupancy at descent; returns false with zero direction if blocked.
	bool pathfindTerrainSafety(Unit *unit);

	//! Request a lazy building field. A fresh field is Pending until publication;
	//! point reads then extend its retained frontier and buildingGradient completes it.
	FieldReadiness requestBuildingField(Building*,MovementAccess movementAccessValue,BuildingRoute = BuildingRoute::Automatic);
	const Uint16* lookupBuildingField(const Building*,MovementAccess movementAccessValue,BuildingRoute = BuildingRoute::Automatic) const;
	RouteResult queryBuildingDistance(Building*,MovementAccess movementAccessValue,int,int,int*,BuildingRoute = BuildingRoute::Automatic);
	StepResult queryBuildingStep(EntityRandom&,Building*,MovementAccess movementAccessValue,int,int,int*,int*,BuildingRoute = BuildingRoute::Automatic);
	//! Allocate/refresh and mark use without completing the lazy generation.
	//! Warm fields retain their published guidance while a refresh is queued.
	bool prepareBuildingGradient(Building *building, MovementAccess movementAccessValue, BuildingRoute route = BuildingRoute::Automatic);
	//! Initialize a lazy field from the current map; point reads settle demand.
	void updateGlobalGradient(Building *building, MovementAccess movementAccessValue, BuildingRoute route = BuildingRoute::Automatic);
	//! Complete a lazy generation; return NULL when its route is locked.
	const Uint16 *buildingGradient(Building *building, MovementAccess movementAccessValue, BuildingRoute route = BuildingRoute::Automatic);
	// Owner-only: establish a safe generation without querying or finishing it.
	std::shared_ptr<SharedBuildingGradient> leaseBuildingRoute(Building *building, MovementAccess movementAccessValue, BuildingRoute route);
	//! Finish a cached field without refreshing its age or last-use timestamp.
	void finishBuildingGradient(Building *building, MovementAccess movementAccessValue, BuildingRoute route = BuildingRoute::Automatic) const;
	bool buildingAvailable(Building *building, MovementAccess movementAccessValue, int x, int y, int *dist, BuildingRoute route = BuildingRoute::Automatic);
	//!requests the next step (dx, dy) to take to get to the building from (x,y)
	bool pathfindBuilding(EntityRandom& random, Building *building, MovementAccess movementAccessValue, int x, int y, int *dx, int *dy, BuildingRoute route = BuildingRoute::Automatic);

	bool pathfindForbidden(const Uint16 *optionGradient, int teamNumber, MovementAccess movementAccessValue, int x, int y, int *dx, int *dy);
	enum class AreaKind { Guard, Clear };
	StepResult queryAreaStep(EntityRandom&, AreaKind, int, MovementAccess movementAccessValue, int, int, int*, int*);
	//! Find the best direction toward a guard or clear area; return true if one has been found.
	bool pathfindArea(EntityRandom& random, AreaKind kind, int teamNumber, MovementAccess movementAccessValue, int x, int y, int *dx, int *dy);
	//! Update the forbidden gradient,
	void updateForbiddenGradient(int teamNumber, MovementAccess movementAccessValue);
	void updateForbiddenGradient(int teamNumber);
	void updateForbiddenGradient();
	//! Update the guard area gradient
	void updateGuardAreasGradient(int teamNumber, MovementAccess movementAccessValue);
	void updateGuardAreasGradient(int teamNumber);
	void updateGuardAreasGradient();
	//! Update the clear area gradient
	void updateClearAreasGradient(int teamNumber, MovementAccess movementAccessValue);
	void updateClearAreasGradient(int teamNumber);
	void updateClearAreasGradient();

	//! A* from (x,y) toward a target within maximumLength land steps of cost.
	//! Writes the first step to (dx,dy), or (0,0) when no path is found; a
	//! ground target cell may be entered even when occupied.
	bool pathfindPointToPoint(int x, int y, int targetX, int targetY, int *dx, int *dy, MovementAccess movementAccessValue, Uint32 teamMask, int maximumLength);
	bool pathfindAirPointToPoint(int x, int y, int targetX, int targetY, int *dx, int *dy);
	struct PointTarget { int x, y; };
	struct PointTargetResult { bool reached = false; Sint8 dx = 0, dy = 0; };
	//! Ground movement only: one bounded search resolving every target. Each is
	//! reached, at the same cost, exactly when pathfindPointToPoint would reach
	//! it with every other target cell occupied; targets are terminal and may be
	//! entered when occupied. Among equally cheap routes the step may differ.
	void pathfindPointTargets(int x, int y, std::span<const PointTarget> targets, MovementAccess movementAccessValue,
							  Uint32 teamMask, int maximumLength, std::vector<PointTargetResult> &results);

private:
	//! Read or move on a prepared field. Both settle their input cell first;
	//! neither refreshes the field or changes its use timestamp.
	Uint16 buildingGradientValue(Building *building, MovementAccess movementAccessValue, size_t cell, BuildingRoute route = BuildingRoute::Automatic) const;
	bool buildingGradientDirection(EntityRandom& random, Building *building, MovementAccess movementAccessValue, int x, int y,
		int *dx, int *dy, bool strict, BuildingRoute route = BuildingRoute::Automatic) const;
	//! Whether every better neighbour of (x, y) on the building's field is held
	//! only by ground units. That is traffic: moving units are never in a
	//! field, so refreshing it would not help.
	bool buildingStepBlockedByTraffic(Building *building, MovementAccess movementAccessValue, int x, int y, BuildingRoute route) const;
	//! A field is stale at the unit's own cell when it holds no distance there
	//! (blocked or unreached) yet the live map leaves the cell open to the
	//! unit: typically the unit itself stood still there when the field was
	//! captured, which seeds the cell as an obstacle. Steps to the best free
	//! neighbour that has a distance, scored as directionByGradient scores
	//! steps. Ground movement only; false when the cell is really blocked (the
	//! caller's own handling, such as the forbidden-area escape, then applies)
	//! or no neighbour qualifies.
	bool staleCellStep(const Uint16* gradient, MovementAccess movementAccessValue, Uint32 teamMask, int x, int y,
		int* dx, int* dy, Uint32 guardAreaMask = 0) const;

protected:
	// Pathfinding gradients, see GradientConstants.h for the cell values. Indexed
	// [team][swim class]; NULL until a unit of that class asks for one.
	// Map owns the buffers and frees them on clear. Shared fields refresh only
	// for cold initialization, explicit reactive events or demanded policy decisions;
	// publication occurs at deterministic completed-world boundaries.
	// Used to go to resources
	//[int team][int resourceNumber][int swimClass]
	Uint16 *materialGradients[TEAM_MAX_COUNT][MaterialSlotCount][MOVEMENT_CLASS_COUNT];
	// Suspended ground fields keep their storage; renewed demand queues a refresh.
	bool materialGradientStale[TEAM_MAX_COUNT][MaterialSlotCount][MOVEMENT_CLASS_COUNT]{};
	bool marketMaterialGradientStale[TEAM_MAX_COUNT][MaterialSlotCount][MOVEMENT_CLASS_COUNT]{};
	bool forbiddenGradientStale[TEAM_MAX_COUNT][MOVEMENT_CLASS_COUNT]{};
	bool guardGradientStale[TEAM_MAX_COUNT][MOVEMENT_CLASS_COUNT]{};
	bool clearGradientStale[TEAM_MAX_COUNT][MOVEMENT_CLASS_COUNT]{};

	mutable std::mutex materialGradientMutex;
	//! Same, with the team's stocked markets as goals (see getResourceGradient).
	Uint16 *marketMaterialGradients[TEAM_MAX_COUNT][MaterialSlotCount][MOVEMENT_CLASS_COUNT];
	// Dense registry of the published planes in both arrays above, maintained
	// by every publication site so a capture visits only live planes. Plane
	// generations are kept per key and survive republication, so a consumer
	// never mistakes a re-added plane for the one it already holds.
	std::vector<MapState::PublishedPlane> publishedPlanes;
	std::array<Uint16, MapState::PlaneCount> publishedPlaneIndex{};
	std::array<Uint64, MapState::PlaneCount> planeGenerations{};
	//! The plane key of a material gradient slot; nullopt for any other field slot.
	std::optional<Uint16> planeKeyForSlot(Uint16* const* slot) const;
	//! Register a publication into slot; other field slots (areas, buildings) are ignored.
	void publishPlane(Uint16* const* slot);
	void clearPlaneRegistry() { publishedPlanes.clear(); publishedPlaneIndex.fill(0); planeGenerations.fill(0); }
	//! Re-register every non-null slot with a fresh generation after bulk slot
	//! changes (load, team removal) that bypass publishPlane.
	void rebuildPlaneRegistry();

	// Used to go out of forbidden areas
	Uint16 *forbiddenGradient[TEAM_MAX_COUNT][MOVEMENT_CLASS_COUNT];

	// Used to attract idle warriors into guard areas
	Uint16 *guardAreasGradient[TEAM_MAX_COUNT][MOVEMENT_CLASS_COUNT];
	//! Guard-area balancing: replace the zero cost of every painted seed in
	//! gradient with its crowding cost (MapInternal.h).
	void seedGuardAreaCrowding(int teamNumber, Uint16 *gradient) const;
	//! Replace every cell of grid with the sum of the cells within GUARD_CROWD_RADIUS
	//! of it (Chebyshev, torus): two row-major sliding-window passes.
	void boxSumInPlace(Uint16 *grid) const;

	// Used to attract idle workers into clearing
	// areas that aren't clear
	Uint16 *clearAreasGradient[TEAM_MAX_COUNT][MOVEMENT_CLASS_COUNT];

public:
	//! Live published material planes in publication order; every plane has size cells.
	std::span<const MapState::PublishedPlane> publishedResourceFields() const { return publishedPlanes; }
	//! Owner thread: the live building seed templates, rebuilt first if stale.
	const MapState::BuildingSeedPlanes& buildingSeedTemplates() const;
	//! Owner thread: whether a captured copy still equals the live templates.
	bool buildingSeedsCurrent(const MapState::BuildingSeedSnapshot& copy) const;
	//! Owner thread: bring a captured copy up to date, copying changed chunks.
	void captureBuildingSeeds(MapState::BuildingSeedSnapshot& copy, Uint64& bytesCopied) const;

protected:
	bool isStockedMarketTile(Uint16 gid, int teamNumber, int resourceType) const;

	//! Point-to-point search scratch for the simulation thread, made on first use.
	std::unique_ptr<astar::Workspace> pointSearch;
	std::vector<std::uint32_t> pointTargetCells;
	std::vector<astar::GoalResult> pointTargetResults;
	astar::Workspace &pointSearchWorkspace();
	int pointSearchClass(MovementAccess movementAccessValue, Uint32 teamMask) const;
};
