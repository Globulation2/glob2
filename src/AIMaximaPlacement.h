/*
  Deterministic development and placement planner used only by Maxima.

  The planner deliberately consumes a plain grid snapshot.  Keeping the engine
  adapter in AIMaxima.cpp makes geometry, routing, utility and persistence
  independently testable and prevents another shared-AI dependency.
 */

#ifndef AI_MAXIMA_PLACEMENT_H
#define AI_MAXIMA_PLACEMENT_H

#include <stdint.h>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace GAGCore
{
	class InputStream;
	class OutputStream;
}

namespace AIMaximaPlacement
{

enum DevelopmentActionType
{
	BuildCampusMember,
	BuildStandalone,
	UpgradeBuilding,
	RepairBuilding
};

enum DevelopmentPurpose
{
	CoreCapacity,
	ColonySeed
};

enum TemplateId
{
	NoTemplate=0,
	InnCompact,
	InnExpandable,
	HospitalCompact,
	HospitalDispersed,
	SchoolProtectedCampus,
	StandaloneReserved,
	BarracksDefended,
	TowerDefended
};

enum ActionLifecycleState
{
	QueuedIntent,
	ParcelReserved,
	CreateIssued,
	SiteObserved,
	Completed,
	InvalidatedBeforeIssue,
	CreateTimedOut,
	DestroyedDuringConstruction,
	UpgradeBlocked,
	RequiredSourceMissing,
	EngineRejected
};

enum RejectionReason
{
	RejectedUndiscovered,
	RejectedTerrain,
	RejectedBuilding,
	RejectedPermanentResource,
	RejectedClearableResource,
	RejectedReservation,
	RejectedCirculation,
	RejectedWaterTier,
	RejectedAccess,
	RejectedIslandBuilders,
	RejectedQuarantine,
	RejectedUpgradeContract,
	RejectedRequiredSource,
	RejectedColonyDistance,
	RejectedColonyCorn,
	RejectedColonyThreat,
	RejectedNegativeUtility,
	RejectedAuthorization,
	RejectionReasonCount
};

struct Footprint
{
	Footprint();
	Footprint(int left, int top, int width, int height);
	int left;
	int top;
	int width;
	int height;
	int area() const;
	bool empty() const;
};

struct TileMask
{
	TileMask();
	TileMask(int width, int height);
	void reset(int width, int height);
	bool get(int x, int y) const;
	void set(int x, int y, bool value=true);
	int count() const;
	int width;
	int height;
	std::vector<uint8_t> cells;
};

struct PlannedSlot
{
	PlannedSlot();
	int index;
	int centerX;
	int centerY;
	int initialLevel;
	int maximumLevel;
	Footprint initialFootprint;
	Footprint terminalFootprint;
};

struct DevelopmentTemplate
{
	DevelopmentTemplate();
	TemplateId id;
	int buildingType;
	std::vector<PlannedSlot> slots;
	Footprint parcel;
	TileMask accessRing;
	bool compact;
};

struct CampusSlotState
{
	CampusSlotState();
	int buildingId;
	int actionId;
	int currentLevel;
	bool unusable;
};

struct Campus
{
	Campus();
	int id;
	TemplateId templateId;
	int originX;
	int originY;
	std::vector<CampusSlotState> slots;
	bool fallbackWaterTier;
};

struct BuildingLevelProfile
{
	BuildingLevelProfile();
	int level;
	int engineType;
	Footprint footprint;
	int constructionResources[5];
	int serviceThroughput;
	int durability;
	int capability;
};

struct BuildingProfile
{
	BuildingProfile();
	int buildingType;
	std::vector<BuildingLevelProfile> levels;
	const BuildingLevelProfile* atLevel(int level) const;
	int maximumLevel() const;
};

struct WorldTile
{
	WorldTile();
	bool discovered;
	bool foodTraversable;
	bool grass;
	bool water;
	bool sand;
	bool permanentResource;
	bool clearableResource;
	bool occupied;
	bool ownOccupied;
	/// Passage ownership is separate from occupancy: workers may use it,
	/// while construction must never consume its footprint.
	bool gateCorridor;
	/// 0/50/100: no uncovered gate / one gate / two gates covered from here.
	int gateDefense;
	int resourceType;
	int resourceAmount;
	uint32_t fertility;
	uint32_t farmCapacity;
	uint32_t foodOpportunity;
	int threat;
	int protectedness;
	int conqueredOpportunity;
};

struct WorldBuilding
{
	WorldBuilding();
	int id;
	int gid;
	int buildingType;
	int level;
	int centerX;
	int centerY;
	int hp;
	int hpMax;
	int age;
	bool site;
	bool upgrading;
};

struct WorldState
{
	WorldState();
	void reset(int width, int height);
	int normalizeX(int x) const;
	int normalizeY(int y) const;
	int index(int x, int y) const;
	int wrappedManhattan(int ax, int ay, int bx, int by) const;
	const WorldTile& tile(int x, int y) const;
	WorldTile& tile(int x, int y);
	const BuildingProfile* profile(int buildingType) const;
	const WorldBuilding* building(int id) const;
	uint32_t computeSignature() const;

	int width;
	int height;
	int tick;
	int swimmingBuilders;
	int accessibleSupplies[5];
	std::vector<WorldTile> tiles;
	std::vector<WorldBuilding> buildings;
	std::vector<BuildingProfile> profiles;
};

struct DevelopmentIntent
{
	DevelopmentIntent();
	int buildingType;
	DevelopmentPurpose purpose;
	int unmetCount;
	int priority;
	int workers;
	int requiredResourceType;
	bool emergency;
};

struct DevelopmentLimits
{
	DevelopmentLimits();
	int newConstruction;
	int level1Upgrades;
	int level2Upgrades;
	int activeNewConstruction;
	int activeLevel1Upgrades;
	int activeLevel2Upgrades;
	bool allowUpgrades;
	bool allowLevel2Upgrades;
	bool allowRepairs;
	// Missing entries retain the planner's generic upgrade demand. Zero vetoes
	// the upgrade; positive values supply its director demand score.
	std::map<std::pair<int, int>, int> upgradePriorities;
	int upgradePriority(int buildingType, int fromLevel) const;
};

struct UtilityComponents
{
	UtilityComponents();
	int unmetDemand;
	int serviceGain;
	int capabilityGain;
	int parallelismGain;
	int redundancyGain;
	int roleLocationQuality;
	int defendedness;
	int compactness;
	int projectedFarmLoss;
	int foodZonePressure;
	int newlyReservedLand;
	int resourceScarcity;
	int constructionLabor;
	int serviceDowntime;
	int threatExposure;
	int newArteryLength;
	int frontierGain;
	int conqueredGain;
	int friendlyDistance;
	int cornDistance;
	int total;
};

struct PlacementPolicy
{
	PlacementPolicy();
	bool arteryRoutingEnabled;
	int unmetDemandWeight;
	int serviceGainWeight;
	int capabilityGainWeight;
	int parallelismGainWeight;
	int redundancyGainWeight;
	int roleLocationQualityWeight;
	int defendednessWeight;
	int compactnessWeight;
	int spacingTargetTiles;
	int spacingWeight;
	int projectedFarmLossWeight;
	int foodZonePenaltyWeight;
	int newlyReservedLandWeight;
	int resourceScarcityWeight;
	int constructionLaborWeight;
	int serviceDowntimeWeight;
	int threatExposureWeight;
	int newArteryLengthWeight;
	int upgradeLevel1Workers;
	int upgradeLevel2Workers;
	int unmetCountWeight;
	int unmetCountCap;
	int upgradeUnmetDemand;
	int serviceGainScale;
	int capabilityGainScale;
	int parallelServiceBase;
	int parallelBuildBonus;
	int parallelNoService;
	int duplicateFirstScore;
	int duplicateScoreScale;
	int resourceDistanceWeight;
	int foodZoneRadius;
	int innerFoodZoneMultiplier;
	int hospitalFoodZoneMultiplier;
	int towerFoodZoneMultiplier;
	int towerCriticalDistanceWeight;
	int towerSpacingTarget;
	int towerSpacingWeight;
	int towerThreatTarget;
	int towerThreatWeight;
	int towerGateWeight;
	int laborScale;
	int downtimeWorkerScale;
	int arteryLengthScale;
	int repairBaseDemand;
	int actionTimeoutTicks;
	int routeClearableResourceCost;
	int routeFarmCost;
	int routeFertilityCost;
	int colonyMinimumAnchorDistance;
	int colonySupplyRadius;
	int colonyMinimumFood;
	int colonyMinimumValue;
	int colonyMaximumThreat;
	int score(const UtilityComponents& components,
		DevelopmentPurpose purpose=CoreCapacity,
		int spacingQuality=100) const;
};

struct DevelopmentAction
{
	DevelopmentAction();
	int id;
	DevelopmentActionType type;
	DevelopmentPurpose purpose;
	ActionLifecycleState state;
	TemplateId templateId;
	int campusId;
	int slotId;
	int buildingId;
	int buildingType;
	int fromLevel;
	int targetLevel;
	int centerX;
	int centerY;
	int workers;
	bool fallbackWaterTier;
	bool requiresSwimmingBuilders;
	int reservationId;
	int issuedTick;
	uint32_t worldSignature;
	Footprint initialFootprint;
	Footprint terminalFootprint;
	std::vector<int> parcelTiles;
	std::vector<int> accessTiles;
	std::vector<int> arteryTiles;
	UtilityComponents utility;
};

struct PlacementDiagnostics
{
	PlacementDiagnostics();
	void clear();
	int candidateCount;
	int strictCandidateCount;
	int fallbackCandidateCount;
	int waterTier;
	int reservationId;
	int selectedActionId;
	int rejected[RejectionReasonCount];
	UtilityComponents selectedUtility;
};

enum SelectionProgress
{
	SelectionPending,
	SelectionFound,
	SelectionEmpty
};

struct Reservation
{
	Reservation();
	int id;
	int campusId;
	int buildingId;
	int actionId;
	std::vector<int> footprintTiles;
	std::vector<int> circulationTiles;
	bool permanent;
};

struct StandaloneContract
{
	StandaloneContract();
	int buildingId;
	int buildingType;
	int centerX;
	int centerY;
	int maximumLevel;
	int reservationId;
	bool preexisting;
};

class Planner
{
public:
	Planner();
	void reset();
	void configure(const std::vector<BuildingProfile>& profiles,
		int innType, int hospitalType, int schoolType,
		int barracksType, int towerType, int swarmType=0);
	const std::vector<DevelopmentTemplate>& templates() const { return templateList; }
	bool validateTemplates(std::string* error=NULL) const;

	void adoptStartingBuildings(const WorldState& world);
	void observe(const WorldState& world);
	void observe(const WorldState& world, uint32_t occupancySignature);
	bool selectAction(const WorldState& world,
		const std::vector<DevelopmentIntent>& intents,
		const DevelopmentLimits& limits, DevelopmentAction& selected);
	bool selectAction(const WorldState& world,
		const std::vector<DevelopmentIntent>& intents,
		const DevelopmentLimits& limits, DevelopmentAction& selected,
		uint32_t occupancySignature);
	///Continue one deterministic selection using at most originBudget candidate
	///origins. The first call snapshots every input; later calls retain the
	///same candidate ordering and scoring caches until a winner is finalized.
	SelectionProgress selectActionIncremental(const WorldState& world,
		const std::vector<DevelopmentIntent>& intents,
		const DevelopmentLimits& limits, DevelopmentAction& selected,
		uint32_t occupancySignature, int originBudget=2048);
	bool selectionPending() const { return incrementalSelectionActive; }
	const WorldState& selectionWorld() const { return incrementalWorld; }
	const std::vector<DevelopmentIntent>& selectionIntents() const
		{ return incrementalIntents; }
	const DevelopmentLimits& selectionLimits() const
		{ return incrementalLimits; }
	uint32_t selectionOccupancySignature() const
		{ return incrementalOccupancySignature; }
	///Validate a finished snapshot selection against current director authority
	///and world state, without rescoring or reserving the selected action.
	bool revalidateSelection(const WorldState& world,
		const std::vector<DevelopmentIntent>& intents,
		const DevelopmentLimits& limits, const DevelopmentAction& action,
		RejectionReason* reason=NULL);
	bool reserve(const WorldState& world, DevelopmentAction& action);
	bool revalidate(const WorldState& world, const DevelopmentAction& action,
		RejectionReason* reason=NULL, bool beforeIssue=false) const;
	void markIssued(int actionId, int buildingId, int tick);
	void markInvalidated(int actionId, ActionLifecycleState state,
		uint32_t signature, int coordinateKey=-1);
	void releaseDestroyedCampuses(const WorldState& world);

	const PlacementPolicy& policy() const { return placementPolicy; }
	PlacementPolicy& mutablePolicy() { return placementPolicy; }
	const PlacementDiagnostics& diagnostics() const { return lastDiagnostics; }
	uint32_t spatialRevision() const { return footprintReferenceRevision; }
	const std::vector<Campus>& campuses() const { return campusList; }
	const std::vector<StandaloneContract>& standaloneContracts() const { return standaloneList; }
	const std::map<int, Reservation>& reservations() const { return reservationMap; }
	const std::map<int, DevelopmentAction>& actions() const { return actionMap; }
	int committedBuildingCount(const WorldState& world, int buildingType) const;
	int activeBuildCount(int buildingType, DevelopmentPurpose purpose) const;
	const std::vector<unsigned short>& footprintReferences() const { return footprintRefs; }
	const std::vector<unsigned short>& circulationReferences() const { return circulationRefs; }
	bool isFootprintReserved(int index) const;
	bool isCirculationReserved(int index) const;
	uint32_t blockedSignature(int buildingType,
		DevelopmentPurpose purpose=CoreCapacity) const;

	void save(GAGCore::OutputStream* stream) const;
	void saveExecutionState(GAGCore::OutputStream* stream) const;
	void loadExecutionState(GAGCore::InputStream* stream);
	bool load(GAGCore::InputStream* stream, int versionMinor);

private:
	template<class Archive> void executionState(Archive& archive);
	struct Candidate;
	void clearIncrementalSelection();
	void buildTemplates();
	const BuildingProfile* configuredProfile(int type) const;
	const DevelopmentTemplate* findTemplate(TemplateId id) const;
	std::vector<int> footprintTiles(const WorldState& world, int centerX,
		int centerY, const Footprint& footprint) const;
	std::vector<int> parcelTiles(const WorldState& world, int originX,
		int originY, const DevelopmentTemplate& developmentTemplate) const;
	std::vector<int> parcelRingTiles(const WorldState& world, int originX,
		int originY, const DevelopmentTemplate& developmentTemplate) const;
	std::vector<int> ringTiles(const WorldState& world,
		const std::vector<int>& parcel) const;
	bool legalTiles(const WorldState& world, const std::vector<int>& tiles,
		bool allowClearable, RejectionReason& reason, int ignoredBuildingId=-1,
		int ignoredReservationId=-1) const;
	bool waterTierPasses(const WorldState& world,
		const std::vector<int>& parcel, int minimumDistance) const;
	void prepareWaterDistanceCache(const WorldState& world) const;
	void prepareScoringCaches(const WorldState& world) const;
	int resourceDistanceAt(const WorldState& world,int resourceType,
		int index) const;
	bool requiredSourcePresent(const WorldState& world,
		const DevelopmentIntent& intent) const;
	// Claims include pending food buildings. Revalidation excludes only itself.
	void prepareColonyClaims(const WorldState& world, int excludeAction=-1) const;
	std::vector<int> colonyFoodTiles(const WorldState& world, int x, int y,
		const Footprint& footprint) const;
	int colonyAnchorDistance(const WorldState& world, int x, int y) const;
	std::pair<int,int> colonyBenefit(const WorldState& world,
		const DevelopmentAction& action) const;
	mutable std::vector<uint8_t> colonyFoodClaims;
	mutable std::vector<std::pair<int,int> > colonyAnchors;
	bool colonyCandidatePasses(const WorldState& world,
		const DevelopmentIntent& intent, const DevelopmentAction& action,
		RejectionReason& reason) const;
	int nearestCompletedBuildingDistance(const WorldState& world,
		int x, int y) const;
	bool routeArtery(const WorldState& world, const std::vector<int>& ring,
		int workers, bool hasNetwork, uint32_t cacheSignature,
		std::vector<int>& route,
		RejectionReason& reason) const;
	void prepareRouteCache(const WorldState& world, int orientation) const;
	void addBuildCandidates(const WorldState& world,
		const DevelopmentIntent& intent, uint32_t stateSignature,
		bool hasNetwork, std::vector<Candidate>& candidates);
	bool addBuildCandidatesRange(const WorldState& world,
		const DevelopmentIntent& intent, uint32_t stateSignature,
		bool hasNetwork, std::vector<Candidate>& candidates,
		size_t& originCursor, size_t originBudget,
		DevelopmentAction& bestStrict, bool& hasStrict, size_t& strictCount,
		DevelopmentAction& bestFallback, bool& hasFallback, size_t& fallbackCount);
	void addUpgradeAndRepairCandidates(const WorldState& world,
		const DevelopmentLimits& limits, std::vector<Candidate>& candidates);
	UtilityComponents scoreCandidate(const WorldState& world,
		const DevelopmentIntent* intent, const Candidate& candidate) const;
	bool candidateBetter(const Candidate& lhs, const Candidate& rhs) const;
	int contractMaximumLevel(int buildingId, int buildingType) const;
	int contractReservationId(int buildingId, int buildingType) const;
	int findCampusIndex(int campusId) const;
	int findStandaloneIndex(int buildingId) const;
	void addReservationReferences(const Reservation& reservation);
	void removeReservation(int reservationId);
	void ensureMaskSize(int size);
	void prepareRetrySignature(const WorldState& world);
	uint32_t retrySignature(const DevelopmentIntent& intent, uint32_t spatialSignature) const;
	uint32_t retryInputSignature;
	void recordBlocked(const std::vector<DevelopmentIntent>& intents,
		uint32_t signature);
	bool incrementalSelectionActive;
	size_t incrementalIntentIndex;
	WorldState incrementalWorld;
	std::vector<DevelopmentIntent> incrementalIntents;
	DevelopmentLimits incrementalLimits;
	uint32_t incrementalSignature;
	uint32_t incrementalOccupancySignature;
	bool incrementalHasNetwork;
	bool incrementalCachesPrepared;
	int incrementalRoutePreparation;
	std::vector<DevelopmentAction> incrementalCandidates;
	size_t incrementalBuildOriginCursor;
	DevelopmentAction incrementalBestStrict;
	DevelopmentAction incrementalBestFallback;
	bool incrementalHasStrict;
	bool incrementalHasFallback;
	size_t incrementalStrictCount;
	size_t incrementalFallbackCount;
	uint32_t stateSignature(const WorldState& world) const;
	uint32_t stateSignature(uint32_t worldSignature) const;

	PlacementPolicy placementPolicy;
	PlacementDiagnostics lastDiagnostics;
	std::vector<BuildingProfile> configuredProfiles;
	std::vector<DevelopmentTemplate> templateList;
	std::vector<Campus> campusList;
	std::vector<StandaloneContract> standaloneList;
	std::map<int, Reservation> reservationMap;
	std::map<int, DevelopmentAction> actionMap;
	std::map<std::pair<int, int>, uint32_t> blockedIntentSignatures;
	std::map<int, uint32_t> coordinateQuarantines;
	std::vector<unsigned short> footprintRefs;
	std::vector<unsigned short> circulationRefs;
	uint32_t circulationReservedTileCount;
	mutable uint32_t routeCacheSignature;
	mutable std::vector<int> routeDistanceCache[2];
	mutable std::vector<int> routeParentCache[2];
	mutable std::vector<uint8_t> waterMaskCache;
	mutable std::vector<int> waterDistanceCache;
	mutable std::vector<int> footprintDistanceCache;
	mutable uint32_t footprintDistanceCacheSignature;
	mutable std::vector<int8_t> resourceSourceCache;
	mutable std::vector<int> resourceDistanceCache[8];
	mutable bool resourceDistanceCacheValid[8];
	mutable uint64_t maximumFarmCapacityCache;
	mutable uint64_t maximumFoodOpportunityCache;
	mutable std::vector<uint64_t> foodOpportunitySourceCache;
	mutable std::vector<uint64_t> foodHaloMaximumCache;
	mutable int foodHaloRadiusCache;
	mutable std::vector<int> threatProtectionSourceCache;
	mutable std::vector<int> threatPrefixCache;
	mutable std::vector<int> protectionPrefixCache;
	mutable std::vector<int> buildingDistanceSourceCache;
	mutable std::vector<int> completedBuildingDistanceCache;
	mutable std::vector<int> criticalBuildingDistanceCache;
	mutable std::vector<int> towerBuildingDistanceCache;
	mutable std::map<int,int> completedBuildingCountCache;
	mutable std::vector<uint32_t> scoringReservedGeneration;
	mutable std::vector<uint32_t> scoringAffectedGeneration;
	mutable std::vector<uint8_t> scoringBlockedNeighbors;
	mutable std::vector<int> scoringNeighborhoodCache;
	mutable int scoringNeighborhoodWidth;
	mutable int scoringNeighborhoodHeight;
	mutable std::vector<int> scoringReservedScratch;
	mutable std::vector<int> scoringAffectedScratch;
	mutable uint32_t scoringGeneration;
	uint32_t footprintReferenceRevision;
	int configuredInnType;
	int configuredHospitalType;
	int configuredSchoolType;
	int configuredBarracksType;
	int configuredTowerType;
	int configuredSwarmType;
	int nextCampusId;
	int nextReservationId;
	int nextActionId;
};

const char* lifecycleName(ActionLifecycleState state);
const char* rejectionName(RejectionReason reason);

}

#endif
