#include "AIMaximaContinuation.h"
#include "AIMaximaPlacementContinuation.h"
/* Maxima deterministic development and placement planner. */

#include "AIMaximaPlacement.h"
#include "Stream.h"

#include <algorithm>
#include <climits>
#include <deque>
#include <queue>
#include <sstream>

namespace AIMaximaPlacement
{

namespace
{
	int clamp100(int value) { return std::max(0, std::min(100, value)); }

	template<typename T> void sortUnique(std::vector<T>& values)
	{
		std::sort(values.begin(), values.end());
		values.erase(std::unique(values.begin(), values.end()), values.end());
	}

	bool contains(const std::vector<int>& values, int value)
	{
		return std::binary_search(values.begin(), values.end(), value);
	}

	bool activeState(ActionLifecycleState state)
	{
		return state==ParcelReserved || state==CreateIssued || state==SiteObserved;
	}

	void hashValue(uint32_t& hash, uint32_t value)
	{
		hash^=value;
		hash*=16777619u;
	}

	void writeIntVector(GAGCore::OutputStream* stream, const char* name,
		const std::vector<int>& values)
	{
		stream->writeEnterSection(name);
		stream->writeUint32(values.size(), "size");
		for(size_t i=0; i<values.size(); ++i)
		{
			stream->writeEnterSection(i);
			stream->writeSint32(values[i], "value");
			stream->writeLeaveSection();
		}
		stream->writeLeaveSection();
	}

	void readIntVector(GAGCore::InputStream* stream, const char* name,
		std::vector<int>& values)
	{
		values.clear();
		stream->readEnterSection(name);
		const uint32_t size=stream->readUint32("size");
		for(uint32_t i=0; i<size; ++i)
		{
			stream->readEnterSection(i);
			values.push_back(stream->readSint32("value"));
			stream->readLeaveSection();
		}
		stream->readLeaveSection();
	}

	void writeUtility(GAGCore::OutputStream* stream, const UtilityComponents& u)
	{
#define WRITE_UTILITY(field) stream->writeSint32(u.field, #field)
		WRITE_UTILITY(unmetDemand); WRITE_UTILITY(serviceGain);
		WRITE_UTILITY(capabilityGain); WRITE_UTILITY(parallelismGain);
		WRITE_UTILITY(redundancyGain); WRITE_UTILITY(roleLocationQuality);
		WRITE_UTILITY(defendedness); WRITE_UTILITY(compactness);
		WRITE_UTILITY(projectedFarmLoss); WRITE_UTILITY(foodZonePressure);
		WRITE_UTILITY(newlyReservedLand);
		WRITE_UTILITY(resourceScarcity); WRITE_UTILITY(constructionLabor);
		WRITE_UTILITY(serviceDowntime); WRITE_UTILITY(threatExposure);
		WRITE_UTILITY(newArteryLength); WRITE_UTILITY(frontierGain);
		WRITE_UTILITY(conqueredGain); WRITE_UTILITY(friendlyDistance);
		WRITE_UTILITY(cornDistance); WRITE_UTILITY(total);
#undef WRITE_UTILITY
	}

	void readUtility(GAGCore::InputStream* stream, UtilityComponents& u,
		int versionMinor)
	{
#define READ_UTILITY(field) u.field=stream->readSint32(#field)
		READ_UTILITY(unmetDemand); READ_UTILITY(serviceGain);
		READ_UTILITY(capabilityGain); READ_UTILITY(parallelismGain);
		READ_UTILITY(redundancyGain); READ_UTILITY(roleLocationQuality);
		READ_UTILITY(defendedness); READ_UTILITY(compactness);
		READ_UTILITY(projectedFarmLoss); READ_UTILITY(foodZonePressure);
		READ_UTILITY(newlyReservedLand);
		READ_UTILITY(resourceScarcity); READ_UTILITY(constructionLabor);
		READ_UTILITY(serviceDowntime); READ_UTILITY(threatExposure);
		READ_UTILITY(newArteryLength);
		if(versionMinor>=92)
		{
			READ_UTILITY(frontierGain); READ_UTILITY(conqueredGain);
			READ_UTILITY(friendlyDistance); READ_UTILITY(cornDistance);
		}
		READ_UTILITY(total);
#undef READ_UTILITY
	}
}

Footprint::Footprint() : left(0), top(0), width(0), height(0) {}
Footprint::Footprint(int l, int t, int w, int h)
	: left(l), top(t), width(w), height(h) {}
int Footprint::area() const { return width*height; }
bool Footprint::empty() const { return width<=0 || height<=0; }

TileMask::TileMask() : width(0), height(0) {}
TileMask::TileMask(int w, int h) { reset(w, h); }
void TileMask::reset(int w, int h)
{ width=w; height=h; cells.assign(std::max(0, w*h), 0); }
bool TileMask::get(int x, int y) const
{ return x>=0 && y>=0 && x<width && y<height && cells[y*width+x]!=0; }
void TileMask::set(int x, int y, bool value)
{ if(x>=0 && y>=0 && x<width && y<height) cells[y*width+x]=value ? 1 : 0; }
int TileMask::count() const
{ return int(std::count(cells.begin(), cells.end(), uint8_t(1))); }

PlannedSlot::PlannedSlot()
	: index(-1), centerX(0), centerY(0), initialLevel(1), maximumLevel(1) {}
DevelopmentTemplate::DevelopmentTemplate()
	: id(NoTemplate), buildingType(-1), compact(false) {}
CampusSlotState::CampusSlotState()
	: buildingId(-1), actionId(-1), currentLevel(0), unusable(false) {}
Campus::Campus()
	: id(-1), templateId(NoTemplate), originX(0), originY(0),
	  fallbackWaterTier(false) {}
BuildingLevelProfile::BuildingLevelProfile()
	: level(0), engineType(-1), serviceThroughput(0), durability(0), capability(0)
{ std::fill(constructionResources, constructionResources+5, 0); }
BuildingProfile::BuildingProfile() : buildingType(-1) {}
const BuildingLevelProfile* BuildingProfile::atLevel(int requested) const
{
	for(size_t i=0; i<levels.size(); ++i)
		if(levels[i].level==requested) return &levels[i];
	return NULL;
}
int BuildingProfile::maximumLevel() const
{
	int result=0;
	for(size_t i=0; i<levels.size(); ++i) result=std::max(result, levels[i].level);
	return result;
}

WorldTile::WorldTile()
	: discovered(false), foodTraversable(true), grass(false), water(false), sand(false),
	  permanentResource(false), clearableResource(false), occupied(false),
	  ownOccupied(false), gateCorridor(false), gateDefense(0), resourceType(-1), resourceAmount(0), fertility(0),
	  farmCapacity(0), foodOpportunity(0), threat(0), protectedness(50),
	  conqueredOpportunity(0) {}
WorldBuilding::WorldBuilding()
	: id(-1), gid(-1), buildingType(-1), level(0), centerX(0), centerY(0),
	  hp(0), hpMax(0), age(0), site(false), upgrading(false) {}
WorldState::WorldState() : width(0), height(0), tick(0), swimmingBuilders(0)
{ std::fill(accessibleSupplies, accessibleSupplies+5, 0); }
void WorldState::reset(int w, int h)
{
	width=w; height=h; tick=0; swimmingBuilders=0;
	std::fill(accessibleSupplies, accessibleSupplies+5, 0);
	tiles.assign(std::max(0, w*h), WorldTile()); buildings.clear(); profiles.clear();
}
int WorldState::normalizeX(int x) const
{ return width ? (x%width+width)%width : 0; }
int WorldState::normalizeY(int y) const
{ return height ? (y%height+height)%height : 0; }
int WorldState::index(int x, int y) const
{ return normalizeY(y)*width+normalizeX(x); }
int WorldState::wrappedManhattan(int ax, int ay, int bx, int by) const
{
	int dx=std::abs(normalizeX(ax)-normalizeX(bx));
	int dy=std::abs(normalizeY(ay)-normalizeY(by));
	dx=std::min(dx, width-dx); dy=std::min(dy, height-dy);
	return dx+dy;
}
const WorldTile& WorldState::tile(int x, int y) const { return tiles[index(x,y)]; }
WorldTile& WorldState::tile(int x, int y) { return tiles[index(x,y)]; }
const BuildingProfile* WorldState::profile(int type) const
{
	for(size_t i=0; i<profiles.size(); ++i)
		if(profiles[i].buildingType==type) return &profiles[i];
	return NULL;
}
const WorldBuilding* WorldState::building(int id) const
{
	for(size_t i=0; i<buildings.size(); ++i) if(buildings[i].id==id) return &buildings[i];
	return NULL;
}
uint32_t WorldState::computeSignature() const
{
	uint32_t result=2166136261u;
	hashValue(result, uint32_t(width)); hashValue(result, uint32_t(height));
	for(size_t i=0; i<tiles.size(); ++i)
	{
		const WorldTile& t=tiles[i];
		uint32_t flags=(t.discovered?1u:0u)|(t.grass?2u:0u)|(t.water?4u:0u)
			|(t.sand?8u:0u)|(t.permanentResource?16u:0u)
			|(t.clearableResource?32u:0u)|(t.occupied?64u:0u)|(t.foodTraversable?128u:0u)
			|(t.gateCorridor?256u:0u);
		hashValue(result, flags); hashValue(result, uint32_t(t.resourceType+1));
		hashValue(result,uint32_t(t.gateDefense));
	}
	for(size_t i=0; i<buildings.size(); ++i)
	{
		const WorldBuilding& b=buildings[i];
		hashValue(result, uint32_t(b.id)); hashValue(result, uint32_t(b.buildingType));
		hashValue(result, uint32_t(index(b.centerX,b.centerY)));
	}
	return result;
}

DevelopmentIntent::DevelopmentIntent()
	: buildingType(-1), purpose(CoreCapacity), unmetCount(0), priority(0), workers(1),
	  requiredResourceType(-1), emergency(false) {}
DevelopmentLimits::DevelopmentLimits()
	: newConstruction(0), level1Upgrades(0), level2Upgrades(0),
	  activeNewConstruction(0), activeLevel1Upgrades(0), activeLevel2Upgrades(0),
	  allowUpgrades(false), allowLevel2Upgrades(false), allowRepairs(true) {}
int DevelopmentLimits::upgradePriority(int buildingType, int fromLevel) const
{
	const auto priority=upgradePriorities.find(std::make_pair(buildingType,fromLevel));
	return priority==upgradePriorities.end() ? -1 : priority->second;
}
UtilityComponents::UtilityComponents()
	: unmetDemand(0), serviceGain(0), capabilityGain(0), parallelismGain(0),
	  redundancyGain(0), roleLocationQuality(0), defendedness(0), compactness(0),
	  projectedFarmLoss(0), foodZonePressure(0), newlyReservedLand(0),
	  resourceScarcity(0),
	  constructionLabor(0), serviceDowntime(0), threatExposure(0),
	  newArteryLength(0), frontierGain(0), conqueredGain(0),
	  friendlyDistance(-1), cornDistance(-1), total(0) {}
PlacementPolicy::PlacementPolicy()
	: arteryRoutingEnabled(true), unmetDemandWeight(12), serviceGainWeight(10), capabilityGainWeight(8),
	  parallelismGainWeight(4), redundancyGainWeight(4), roleLocationQualityWeight(6),
	  defendednessWeight(6), compactnessWeight(5), spacingTargetTiles(2),
	  spacingWeight(6), projectedFarmLossWeight(12),
	  foodZonePenaltyWeight(12), newlyReservedLandWeight(4),
	  resourceScarcityWeight(8),
	  constructionLaborWeight(4), serviceDowntimeWeight(6),
	  threatExposureWeight(8), newArteryLengthWeight(2), upgradeLevel1Workers(4),
	  upgradeLevel2Workers(8), unmetCountWeight(10), unmetCountCap(40),
	  upgradeUnmetDemand(25), serviceGainScale(5), capabilityGainScale(10),
	  parallelServiceBase(30), parallelBuildBonus(20), parallelNoService(10),
	  duplicateFirstScore(100), duplicateScoreScale(50), resourceDistanceWeight(8),
	  foodZoneRadius(2), innerFoodZoneMultiplier(100),
	  hospitalFoodZoneMultiplier(50), towerFoodZoneMultiplier(25),
	  towerCriticalDistanceWeight(10), towerSpacingTarget(8), towerSpacingWeight(12),
	  towerThreatTarget(35), towerThreatWeight(2), towerGateWeight(0), laborScale(10),
	  downtimeWorkerScale(2), arteryLengthScale(5), repairBaseDemand(20),
	  actionTimeoutTicks(300), routeClearableResourceCost(25), routeFarmCost(12),
	  routeFertilityCost(2), colonyMinimumAnchorDistance(24),
	  colonySupplyRadius(12), colonyMinimumFood(8), colonyMinimumValue(5),
	  colonyMaximumThreat(35) {}

int PlacementPolicy::score(const UtilityComponents& c,
	DevelopmentPurpose purpose, int spacingQuality) const
{
	// Colony value already accounts for new food and establishment cost.
	// No distance bonus: farther is useful only when it unlocks additional food.
	if(purpose==ColonySeed) return int(std::min<long long>(INT_MAX,
		static_cast<long long>(c.conqueredGain)*unmetDemandWeight));
	const int defendedWeight=defendednessWeight;
	const int compactWeight=compactnessWeight;
	const int arteryWeight=newArteryLengthWeight;
	return unmetDemandWeight*c.unmetDemand + serviceGainWeight*c.serviceGain
		+capabilityGainWeight*c.capabilityGain + parallelismGainWeight*c.parallelismGain
		+redundancyGainWeight*c.redundancyGain
		+roleLocationQualityWeight*c.roleLocationQuality
		+defendedWeight*c.defendedness + compactWeight*c.compactness
		-projectedFarmLossWeight*c.projectedFarmLoss
		-foodZonePenaltyWeight*c.foodZonePressure
		-newlyReservedLandWeight*c.newlyReservedLand
		-resourceScarcityWeight*c.resourceScarcity
		-constructionLaborWeight*c.constructionLabor
		-serviceDowntimeWeight*c.serviceDowntime
		-threatExposureWeight*c.threatExposure
		-arteryWeight*c.newArteryLength
		+(purpose==ColonySeed ? 0 : spacingWeight*clamp100(spacingQuality));
}
DevelopmentAction::DevelopmentAction()
	: id(-1), type(BuildStandalone), purpose(CoreCapacity), state(QueuedIntent), templateId(NoTemplate),
	  campusId(-1), slotId(-1), buildingId(-1), buildingType(-1), fromLevel(0),
	  targetLevel(1), centerX(0), centerY(0), workers(1), fallbackWaterTier(false),
	  requiresSwimmingBuilders(false), reservationId(-1), issuedTick(-1), worldSignature(0) {}
PlacementDiagnostics::PlacementDiagnostics() { clear(); }
void PlacementDiagnostics::clear()
{
	candidateCount=0; strictCandidateCount=0; fallbackCandidateCount=0;
	waterTier=0; reservationId=-1; selectedActionId=-1;
	std::fill(rejected, rejected+RejectionReasonCount, 0);
	selectedUtility=UtilityComponents();
}
Reservation::Reservation()
	: id(-1), campusId(-1), buildingId(-1), actionId(-1), permanent(false) {}
StandaloneContract::StandaloneContract()
	: buildingId(-1), buildingType(-1), centerX(0), centerY(0), maximumLevel(1),
	  reservationId(-1), preexisting(false) {}

struct Planner::Candidate
{
	Candidate() : newCampus(false) {}
	DevelopmentAction action;
	bool newCampus;
};

Planner::Planner() { reset(); }
void Planner::clearIncrementalSelection()
{
	incrementalSelectionActive=false;
	incrementalIntentIndex=0;
	incrementalWorld=WorldState();
	incrementalIntents.clear();
	incrementalLimits=DevelopmentLimits();
	incrementalSignature=0;
	incrementalOccupancySignature=0;
	incrementalHasNetwork=false;
	incrementalCachesPrepared=false;
	incrementalRoutePreparation=0;
	incrementalCandidates.clear();
	incrementalBuildOriginCursor=0;
	incrementalBestStrict=DevelopmentAction();
	incrementalBestFallback=DevelopmentAction();
	incrementalHasStrict=false;
	incrementalHasFallback=false;
	incrementalStrictCount=0;
	incrementalFallbackCount=0;
}
void Planner::reset()
{
	clearIncrementalSelection();
	lastDiagnostics.clear(); configuredProfiles.clear(); templateList.clear();
	campusList.clear(); standaloneList.clear(); reservationMap.clear(); actionMap.clear();
	blockedIntentSignatures.clear(); coordinateQuarantines.clear();
	retryInputSignature=0;
	footprintRefs.clear(); circulationRefs.clear();
	circulationReservedTileCount=0;
	routeCacheSignature=0;routeDistanceCache[0].clear();routeDistanceCache[1].clear();
	routeParentCache[0].clear();routeParentCache[1].clear();
	waterMaskCache.clear();waterDistanceCache.clear();
	footprintDistanceCache.clear();footprintDistanceCacheSignature=0;
	resourceSourceCache.clear();
	for(int resource=0;resource<8;++resource)
	{resourceDistanceCache[resource].clear();resourceDistanceCacheValid[resource]=false;}
	maximumFarmCapacityCache=1;maximumFoodOpportunityCache=1;
	foodOpportunitySourceCache.clear();foodHaloMaximumCache.clear();
	foodHaloRadiusCache=-1;
	threatProtectionSourceCache.clear();threatPrefixCache.clear();
	protectionPrefixCache.clear();
	buildingDistanceSourceCache.clear();
	completedBuildingDistanceCache.clear();criticalBuildingDistanceCache.clear();
	towerBuildingDistanceCache.clear();completedBuildingCountCache.clear();
	scoringReservedGeneration.clear();scoringAffectedGeneration.clear();
	scoringBlockedNeighbors.clear();scoringNeighborhoodCache.clear();
	scoringNeighborhoodWidth=scoringNeighborhoodHeight=0;
	scoringReservedScratch.clear();scoringAffectedScratch.clear();
	scoringGeneration=0;
	footprintReferenceRevision=1;
	configuredInnType=configuredHospitalType=configuredSchoolType=-1;
	configuredBarracksType=configuredTowerType=configuredSwarmType=-1;
	nextCampusId=1; nextReservationId=1; nextActionId=1;
}

void Planner::configure(const std::vector<BuildingProfile>& profiles,
	int innType, int hospitalType, int schoolType, int barracksType, int towerType,
	int swarmType)
{
	configuredProfiles=profiles; configuredInnType=innType;
	configuredHospitalType=hospitalType; configuredSchoolType=schoolType;
	configuredBarracksType=barracksType; configuredTowerType=towerType;
	configuredSwarmType=swarmType;
	buildTemplates();
}

const BuildingProfile* Planner::configuredProfile(int type) const
{
	for(size_t i=0; i<configuredProfiles.size(); ++i)
		if(configuredProfiles[i].buildingType==type) return &configuredProfiles[i];
	return NULL;
}

void Planner::buildTemplates()
{
	templateList.clear();
	for(size_t p=0; p<configuredProfiles.size(); ++p)
	{
		const BuildingProfile& profile=configuredProfiles[p];
		const BuildingLevelProfile* initial=profile.atLevel(1);
		const BuildingLevelProfile* terminal=profile.atLevel(profile.maximumLevel());
		if(!initial || !terminal) continue;

		std::vector<std::pair<TemplateId,int> > descriptions;
		if(profile.buildingType==configuredInnType)
		{
			descriptions.push_back(std::make_pair(InnCompact, 4));
			descriptions.push_back(std::make_pair(InnExpandable, 1));
		}
		else if(profile.buildingType==configuredHospitalType)
		{
			descriptions.push_back(std::make_pair(HospitalCompact, 4));
			descriptions.push_back(std::make_pair(HospitalDispersed, 1));
		}
		else if(profile.buildingType==configuredSchoolType)
			descriptions.push_back(std::make_pair(SchoolProtectedCampus, 4));
		else if(profile.buildingType==configuredBarracksType)
			descriptions.push_back(std::make_pair(BarracksDefended, 1));
		else if(profile.buildingType==configuredTowerType)
			descriptions.push_back(std::make_pair(TowerDefended, 1));
		else
			descriptions.push_back(std::make_pair(StandaloneReserved, 1));

		for(size_t d=0; d<descriptions.size(); ++d)
		{
			DevelopmentTemplate t; t.id=descriptions[d].first;
			t.buildingType=profile.buildingType; t.compact=descriptions[d].second==4;
			const int maximum=(t.id==InnCompact) ? std::min(2,profile.maximumLevel())
				: profile.maximumLevel();
			const BuildingLevelProfile* slotTerminal=profile.atLevel(maximum);
			if(!slotTerminal) slotTerminal=initial;
			if(t.compact)
			{
				const int cellW=slotTerminal->footprint.width;
				const int cellH=slotTerminal->footprint.height;
				t.parcel=Footprint(0,0,cellW*2,cellH*2);
				for(int sy=0; sy<2; ++sy) for(int sx=0; sx<2; ++sx)
				{
					PlannedSlot slot; slot.index=sy*2+sx;
					slot.centerX=sx*cellW-slotTerminal->footprint.left;
					slot.centerY=sy*cellH-slotTerminal->footprint.top;
					slot.initialLevel=1; slot.maximumLevel=maximum;
					slot.initialFootprint=initial->footprint;
					slot.terminalFootprint=slotTerminal->footprint;
					t.slots.push_back(slot);
				}
			}
			else
			{
				t.parcel=Footprint(0,0,slotTerminal->footprint.width,
					slotTerminal->footprint.height);
				PlannedSlot slot; slot.index=0;
				slot.centerX=-slotTerminal->footprint.left;
				slot.centerY=-slotTerminal->footprint.top;
				slot.maximumLevel=maximum; slot.initialFootprint=initial->footprint;
				slot.terminalFootprint=slotTerminal->footprint;
				t.slots.push_back(slot);
			}
			t.accessRing.reset(t.parcel.width+2,t.parcel.height+2);
			for(int y=0; y<t.accessRing.height; ++y)
				for(int x=0; x<t.accessRing.width; ++x)
					if(x==0 || y==0 || x==t.accessRing.width-1 || y==t.accessRing.height-1)
						t.accessRing.set(x,y);
			templateList.push_back(t);
		}
	}
}

bool Planner::validateTemplates(std::string* error) const
{
	for(size_t t=0; t<templateList.size(); ++t)
	{
		const DevelopmentTemplate& item=templateList[t];
		if(item.slots.empty() || item.parcel.empty())
		{ if(error) *error="empty template"; return false; }
		std::set<int> occupied;
		const BuildingProfile* profile=configuredProfile(item.buildingType);
		if(!profile){if(error)*error="missing building profile";return false;}
		for(size_t s=0; s<item.slots.size(); ++s)
		{
			const PlannedSlot& slot=item.slots[s];
			if(slot.maximumLevel<slot.initialLevel || slot.terminalFootprint.empty())
			{ if(error) *error="invalid slot levels"; return false; }
			bool exposed=false;
			for(int dy=0; dy<slot.terminalFootprint.height; ++dy)
				for(int dx=0; dx<slot.terminalFootprint.width; ++dx)
				{
					const int x=slot.centerX+slot.terminalFootprint.left+dx;
					const int y=slot.centerY+slot.terminalFootprint.top+dy;
					if(x<0 || y<0 || x>=item.parcel.width || y>=item.parcel.height)
					{ if(error) *error="slot outside parcel"; return false; }
					const int key=y*item.parcel.width+x;
					if(!occupied.insert(key).second)
					{ if(error) *error="overlapping slots"; return false; }
					if(x==0 || y==0 || x==item.parcel.width-1 || y==item.parcel.height-1)
						exposed=true;
				}
			if(item.compact && !exposed)
			{ if(error) *error="campus member lacks outer-ring edge"; return false; }
			for(int level=slot.initialLevel;level<=slot.maximumLevel;++level)
			{
				const BuildingLevelProfile* geometry=profile->atLevel(level);
				if(!geometry){if(error)*error="missing reachable level geometry";return false;}
				for(int dy=0;dy<geometry->footprint.height;++dy)
					for(int dx=0;dx<geometry->footprint.width;++dx)
					{
						const int x=slot.centerX+geometry->footprint.left+dx;
						const int y=slot.centerY+geometry->footprint.top+dy;
						if(x<0||y<0||x>=item.parcel.width||y>=item.parcel.height)
						{if(error)*error="reachable level escapes terminal parcel";return false;}
					}
			}
		}
		if(item.id==InnCompact)
			for(size_t s=0; s<item.slots.size(); ++s)
				if(item.slots[s].maximumLevel>2)
				{ if(error) *error="compact inn reaches level 3"; return false; }
	}
	return true;
}

const DevelopmentTemplate* Planner::findTemplate(TemplateId id) const
{
	for(size_t i=0; i<templateList.size(); ++i) if(templateList[i].id==id) return &templateList[i];
	return NULL;
}

std::vector<int> Planner::footprintTiles(const WorldState& world, int centerX,
	int centerY, const Footprint& footprint) const
{
	std::vector<int> result;result.reserve(std::max(0,footprint.area()));
	for(int dy=0; dy<footprint.height; ++dy)
		for(int dx=0; dx<footprint.width; ++dx)
			result.push_back(world.index(centerX+footprint.left+dx,
				centerY+footprint.top+dy));
	sortUnique(result); return result;
}

std::vector<int> Planner::parcelTiles(const WorldState& world, int originX,
	int originY, const DevelopmentTemplate& t) const
{
	std::vector<int> result;result.reserve(std::max(0,t.parcel.area()));
	for(int dy=0; dy<t.parcel.height; ++dy)
		for(int dx=0; dx<t.parcel.width; ++dx)
			result.push_back(world.index(originX+dx,originY+dy));
	// Row-major generation is already sorted unless the rectangle wraps around
	// an edge. Avoid sorting the common non-wrapping candidate while preserving
	// the ordering required by binary-search consumers.
	if(originX<0 || originY<0 || originX+t.parcel.width>world.width
	   ||originY+t.parcel.height>world.height)
		sortUnique(result);
	return result;
}

std::vector<int> Planner::parcelRingTiles(const WorldState& world, int originX,
	int originY, const DevelopmentTemplate& t) const
{
	// Templates are rectangles.  Build their four outside edges directly instead
	// of rediscovering the perimeter from every parcel tile.
	if(t.parcel.width>=world.width||t.parcel.height>=world.height)
		return ringTiles(world,parcelTiles(world,originX,originY,t));
	std::vector<int> result;
	result.reserve(2*t.parcel.width+2*t.parcel.height);
	for(int dx=0;dx<t.parcel.width;++dx)
	{
		result.push_back(world.index(originX+dx,originY-1));
		result.push_back(world.index(originX+dx,originY+t.parcel.height));
	}
	for(int dy=0;dy<t.parcel.height;++dy)
	{
		result.push_back(world.index(originX-1,originY+dy));
		result.push_back(world.index(originX+t.parcel.width,originY+dy));
	}
	sortUnique(result);return result;
}

std::vector<int> Planner::ringTiles(const WorldState& world,
	const std::vector<int>& parcel) const
{
	std::vector<int> sorted=parcel; sortUnique(sorted);
	std::vector<int> result;
	for(size_t i=0; i<parcel.size(); ++i)
	{
		const int x=parcel[i]%world.width, y=parcel[i]/world.width;
		const int n[4]={world.index(x-1,y),world.index(x+1,y),
			world.index(x,y-1),world.index(x,y+1)};
		for(int d=0; d<4; ++d) if(!contains(sorted,n[d])) result.push_back(n[d]);
	}
	sortUnique(result); return result;
}

void Planner::ensureMaskSize(int size)
{
	if(int(footprintRefs.size())!=size)
	{
		footprintRefs.assign(size,0);
		if(++footprintReferenceRevision==0)footprintReferenceRevision=1;
		footprintDistanceCache.clear();footprintDistanceCacheSignature=0;
	}
	if(int(circulationRefs.size())!=size)
	{circulationRefs.assign(size,0);circulationReservedTileCount=0;}
	if(int(scoringReservedGeneration.size())!=size)
	{
		scoringReservedGeneration.assign(size,0);
		scoringAffectedGeneration.assign(size,0);
		scoringBlockedNeighbors.assign(size,0);
		scoringGeneration=0;
	}
}
bool Planner::isFootprintReserved(int index) const
{ return index>=0 && index<int(footprintRefs.size()) && footprintRefs[index]!=0; }
bool Planner::isCirculationReserved(int index) const
{ return index>=0 && index<int(circulationRefs.size()) && circulationRefs[index]!=0; }

bool Planner::legalTiles(const WorldState& world, const std::vector<int>& tiles,
	bool allowClearable, RejectionReason& reason, int ignoredBuildingId,
	int ignoredReservationId) const
{
	const auto existing=reservationMap.find(ignoredReservationId);
	std::set<int> ignored;
	if(ignoredBuildingId>=0)
	{
		const WorldBuilding* b=world.building(ignoredBuildingId);
		const BuildingProfile* p=b ? world.profile(b->buildingType) : NULL;
		const BuildingLevelProfile* l=(p&&b) ? p->atLevel(b->level) : NULL;
		if(l)
		{
			std::vector<int> own=footprintTiles(world,b->centerX,b->centerY,l->footprint);
			ignored.insert(own.begin(),own.end());
		}
	}
	for(size_t i=0; i<tiles.size(); ++i)
	{
		const int index=tiles[i]; const WorldTile& t=world.tiles[index];
		if(!t.discovered){reason=RejectedUndiscovered;return false;}
		if(!t.grass){reason=RejectedTerrain;return false;}
		if(t.occupied && ignored.count(index)==0){reason=RejectedBuilding;return false;}
		if(t.gateCorridor && ignored.count(index)==0){reason=RejectedCirculation;return false;}
		if(t.permanentResource){reason=RejectedPermanentResource;return false;}
		if(t.clearableResource && !allowClearable){reason=RejectedClearableResource;return false;}
		const bool ownFootprint=existing!=reservationMap.end()
			&&contains(existing->second.footprintTiles,index);
		const bool ownCirculation=existing!=reservationMap.end()
			&&contains(existing->second.circulationTiles,index);
		if(isFootprintReserved(index)&&footprintRefs[index]>unsigned(ownFootprint))
		{reason=RejectedReservation;return false;}
		if(isCirculationReserved(index)&&circulationRefs[index]>unsigned(ownCirculation))
		{reason=RejectedCirculation;return false;}
	}
	return true;
}

bool Planner::waterTierPasses(const WorldState& world,
	const std::vector<int>& parcel, int minimumDistance) const
{
	for(size_t p=0; p<parcel.size(); ++p)
		if(waterDistanceCache[parcel[p]]<minimumDistance)return false;
	return true;
}

void Planner::prepareWaterDistanceCache(const WorldState& world) const
{
	const int size=world.width*world.height;
	bool changed=int(waterMaskCache.size())!=size;
	if(!changed)
		for(int i=0;i<size;++i)
			if((waterMaskCache[i]!=0)!=world.tiles[i].water){changed=true;break;}
	if(!changed&&int(waterDistanceCache.size())==size)return;

	waterMaskCache.assign(size,0);waterDistanceCache.assign(size,INT_MAX);
	std::vector<int> queue;queue.reserve(size);
	for(int i=0;i<size;++i)if(world.tiles[i].water)
	{
		waterMaskCache[i]=1;waterDistanceCache[i]=0;queue.push_back(i);
	}
	for(size_t head=0;head<queue.size();++head)
	{
		const int current=queue[head],x=current%world.width,y=current/world.width;
		const int neighbors[4]={world.index(x-1,y),world.index(x+1,y),
			world.index(x,y-1),world.index(x,y+1)};
		for(int d=0;d<4;++d)if(waterDistanceCache[neighbors[d]]==INT_MAX)
		{
			waterDistanceCache[neighbors[d]]=waterDistanceCache[current]+1;
			queue.push_back(neighbors[d]);
		}
	}
}

void Planner::prepareScoringCaches(const WorldState& world) const
{
	prepareColonyClaims(world);
	const int size=world.width*world.height;
	if(scoringNeighborhoodWidth!=world.width
	   ||scoringNeighborhoodHeight!=world.height)
	{
		scoringNeighborhoodWidth=world.width;
		scoringNeighborhoodHeight=world.height;
		scoringNeighborhoodCache.resize(size*9);
		// Farm-loss scoring uses this same wrapped 3x3 stencil for every
		// candidate.  Normalizing it once removes modulo operations from the hot
		// loop without changing either traversal order or neighbour multiplicity.
		for(int index=0;index<size;++index)
		{
			const int x=index%world.width,y=index/world.width;
			int offset=0;
			for(int dy=-1;dy<=1;++dy)for(int dx=-1;dx<=1;++dx)
				scoringNeighborhoodCache[index*9+offset++]=world.index(x+dx,y+dy);
		}
	}
	// Compare complete building source data rather than a hash: an unchanged
	// value is therefore an exact cache hit, not a probabilistic one.  Upgrading
	// is retained as an invalidation boundary even when the current coordinates
	// match; planner observation can change the represented footprint at that
	// lifecycle edge, and preserving the old rebuild point keeps match checksums
	// identical.
	std::vector<int> buildingSources;buildingSources.reserve(world.buildings.size()*5);
	completedBuildingCountCache.clear();
	for(size_t i=0;i<world.buildings.size();++i)
	{
		const WorldBuilding& building=world.buildings[i];
		buildingSources.push_back(building.id);
		buildingSources.push_back(building.buildingType);
		buildingSources.push_back(world.index(building.centerX,building.centerY));
		buildingSources.push_back(building.site?1:0);
		buildingSources.push_back(building.upgrading?1:0);
		if(!building.site)++completedBuildingCountCache[building.buildingType];
	}
	if(buildingSources!=buildingDistanceSourceCache
	   ||int(completedBuildingDistanceCache.size())!=size)
	{
		buildingDistanceSourceCache.swap(buildingSources);
		completedBuildingDistanceCache.assign(size,INT_MAX);
		criticalBuildingDistanceCache.assign(size,INT_MAX);
		towerBuildingDistanceCache.assign(size,INT_MAX);
		std::vector<int> queues[3];
		for(int field=0;field<3;++field)queues[field].reserve(size);
		for(size_t i=0;i<world.buildings.size();++i)
		{
			const WorldBuilding& building=world.buildings[i];
			const int index=world.index(building.centerX,building.centerY);
			if(!building.site&&completedBuildingDistanceCache[index]==INT_MAX)
			{completedBuildingDistanceCache[index]=0;queues[0].push_back(index);}
			if((building.buildingType==configuredSwarmType
			   ||building.buildingType==configuredInnType
			   ||building.buildingType==configuredSchoolType)
			   &&criticalBuildingDistanceCache[index]==INT_MAX)
			{criticalBuildingDistanceCache[index]=0;queues[1].push_back(index);}
			if(building.buildingType==configuredTowerType
			   &&towerBuildingDistanceCache[index]==INT_MAX)
			{towerBuildingDistanceCache[index]=0;queues[2].push_back(index);}
		}
		std::vector<int>* fields[3]={&completedBuildingDistanceCache,
			&criticalBuildingDistanceCache,&towerBuildingDistanceCache};
		// A four-neighbour multi-source BFS is exactly wrapped Manhattan distance,
		// which replaces a full building scan at every candidate coordinate.
		for(int field=0;field<3;++field)
			for(size_t head=0;head<queues[field].size();++head)
			{
				const int current=queues[field][head];
				const int x=current%world.width,y=current/world.width;
				const int neighbors[4]={world.index(x-1,y),world.index(x+1,y),
					world.index(x,y-1),world.index(x,y+1)};
				for(int direction=0;direction<4;++direction)
					if((*fields[field])[neighbors[direction]]==INT_MAX)
					{
						(*fields[field])[neighbors[direction]]=
							(*fields[field])[current]+1;
						queues[field].push_back(neighbors[direction]);
					}
			}
	}
	// Footprint spacing changes only when a reservation or a building footprint
	// changes. Most placement reviews see the same sources, so retain the
	// multi-source field instead of rebuilding a complete-map BFS every time.
	uint32_t footprintSignature=2166136261u;
	hashValue(footprintSignature,uint32_t(world.width));
	hashValue(footprintSignature,uint32_t(world.height));
	hashValue(footprintSignature,footprintReferenceRevision);
	for(size_t i=0;i<world.buildings.size();++i)
	{
		const WorldBuilding& building=world.buildings[i];
		hashValue(footprintSignature,uint32_t(building.id));
		hashValue(footprintSignature,uint32_t(building.buildingType));
		hashValue(footprintSignature,uint32_t(building.level));
		hashValue(footprintSignature,uint32_t(world.index(
			building.centerX,building.centerY)));
	}
	if(int(footprintDistanceCache.size())!=size
	   ||footprintDistanceCacheSignature!=footprintSignature)
	{
		footprintDistanceCache.assign(size,INT_MAX);
		std::vector<int> footprintQueue;footprintQueue.reserve(size);
		auto addFootprintSource=[&](int index)
		{
			if(index>=0&&index<size&&footprintDistanceCache[index]==INT_MAX)
			{footprintDistanceCache[index]=0;footprintQueue.push_back(index);}
		};
		for(int i=0;i<size;++i)
			if(i<int(footprintRefs.size())&&footprintRefs[i])addFootprintSource(i);
		for(size_t i=0;i<world.buildings.size();++i)
		{
			const WorldBuilding& building=world.buildings[i];
			const BuildingProfile* profile=world.profile(building.buildingType);
			const BuildingLevelProfile* level=profile
				?profile->atLevel(std::max(1,building.level)):NULL;
			if(!level)
			{
				addFootprintSource(world.index(building.centerX,building.centerY));
				continue;
			}
			const std::vector<int> tiles=footprintTiles(world,building.centerX,
				building.centerY,level->footprint);
			for(size_t tile=0;tile<tiles.size();++tile)addFootprintSource(tiles[tile]);
		}
		for(size_t head=0;head<footprintQueue.size();++head)
		{
			const int current=footprintQueue[head];
			const int x=current%world.width,y=current/world.width;
			const int neighbors[4]={world.index(x-1,y),world.index(x+1,y),
				world.index(x,y-1),world.index(x,y+1)};
			for(int direction=0;direction<4;++direction)
				if(footprintDistanceCache[neighbors[direction]]==INT_MAX)
				{
					footprintDistanceCache[neighbors[direction]]=
						footprintDistanceCache[current]+1;
					footprintQueue.push_back(neighbors[direction]);
				}
		}
		footprintDistanceCacheSignature=footprintSignature;
	}
	bool changed=int(resourceSourceCache.size())!=size;
	bool foodChanged=int(foodOpportunitySourceCache.size())!=size
		||foodHaloRadiusCache!=placementPolicy.foodZoneRadius;
	bool threatProtectionChanged=
		int(threatProtectionSourceCache.size())!=size*2;
	maximumFarmCapacityCache=1;maximumFoodOpportunityCache=1;
	for(int i=0;i<size;++i)
	{
		const WorldTile& tile=world.tiles[i];
		const int type=!tile.discovered ? -1 : (tile.foodOpportunity>0 ? 1
			: (tile.resourceType>=0 && tile.resourceType<8 && tile.resourceType!=1
				? tile.resourceType : -1));
		if(!changed&&resourceSourceCache[i]!=type)changed=true;
		if(!foodChanged&&foodOpportunitySourceCache[i]!=tile.foodOpportunity)
			foodChanged=true;
		if(!threatProtectionChanged
		   &&(threatProtectionSourceCache[i*2]!=tile.threat
		      ||threatProtectionSourceCache[i*2+1]!=tile.protectedness))
			threatProtectionChanged=true;
		maximumFarmCapacityCache=std::max<uint64_t>(maximumFarmCapacityCache,
			tile.farmCapacity);
		maximumFoodOpportunityCache=std::max<uint64_t>(maximumFoodOpportunityCache,
			tile.foodOpportunity);
	}
	if(changed)
	{
		resourceSourceCache.assign(size,-1);
		for(int i=0;i<size;++i)
		{
			const WorldTile& tile=world.tiles[i];
			if(!tile.discovered)continue;
			if(tile.foodOpportunity>0)resourceSourceCache[i]=1;
			else if(tile.resourceType>=0&&tile.resourceType<8&&tile.resourceType!=1)
				resourceSourceCache[i]=int8_t(tile.resourceType);
		}
		for(int resource=0;resource<8;++resource)
		{resourceDistanceCache[resource].clear();resourceDistanceCacheValid[resource]=false;}
	}
	if(foodChanged)
	{
		foodHaloRadiusCache=placementPolicy.foodZoneRadius;
		foodOpportunitySourceCache.resize(size);
		foodHaloMaximumCache.assign(size,0);
		for(int i=0;i<size;++i)
			foodOpportunitySourceCache[i]=world.tiles[i].foodOpportunity;
		// Scoring asks for the strongest weighted food opportunity in the same
		// diamond around every reserved tile.  Precomputing that dilation once
		// preserves the max and integer operations while removing it per candidate.
		const int radius=foodHaloRadiusCache;
		for(int index=0;index<size;++index)
		{
			const int x=index%world.width,y=index/world.width;
			uint64_t strongest=0;
			for(int dy=-radius;dy<=radius;++dy)
				for(int dx=-(radius-std::abs(dy));
					dx<=radius-std::abs(dy);++dx)
				{
					const int distance=std::abs(dx)+std::abs(dy);
					const uint64_t opportunity=world.tile(x+dx,y+dy).foodOpportunity;
					strongest=std::max(strongest,
						opportunity*uint64_t(radius+1-distance));
				}
			foodHaloMaximumCache[index]=strongest;
		}
	}
	if(threatProtectionChanged)
	{
		threatProtectionSourceCache.resize(size*2);
		const int stride=world.width+1;
		threatPrefixCache.assign((world.height+1)*stride,0);
		protectionPrefixCache.assign((world.height+1)*stride,0);
		// Both location metrics are rectangular sums.  Summed-area tables retain
		// the exact integer totals while replacing a footprint scan with at most
		// four queries when the rectangle crosses a wrapped map edge.
		for(int y=0;y<world.height;++y)
		{
			int threatRow=0,protectionRow=0;
			for(int x=0;x<world.width;++x)
			{
				const int index=y*world.width+x;
				const WorldTile& tile=world.tiles[index];
				threatProtectionSourceCache[index*2]=tile.threat;
				threatProtectionSourceCache[index*2+1]=tile.protectedness;
				threatRow+=tile.threat;protectionRow+=tile.protectedness;
				threatPrefixCache[(y+1)*stride+x+1]=
					threatPrefixCache[y*stride+x+1]+threatRow;
				protectionPrefixCache[(y+1)*stride+x+1]=
					protectionPrefixCache[y*stride+x+1]+protectionRow;
			}
		}
	}
}

int Planner::resourceDistanceAt(const WorldState& world,int resourceType,
	int index) const
{
	if(resourceType<0||resourceType>=8)return INT_MAX;
	if(!resourceDistanceCacheValid[resourceType])
	{
		const int size=world.width*world.height;
		std::vector<int>& distances=resourceDistanceCache[resourceType];
		distances.assign(size,INT_MAX);
		std::vector<int> queue;queue.reserve(size);
		for(int i=0;i<size;++i)if(resourceSourceCache[i]==resourceType)
		{distances[i]=0;queue.push_back(i);}
		for(size_t head=0;head<queue.size();++head)
		{
			const int current=queue[head];
			const int x=current%world.width,y=current/world.width;
			const int neighbors[4]={world.index(x-1,y),world.index(x+1,y),
				world.index(x,y-1),world.index(x,y+1)};
			for(int d=0;d<4;++d)
				if(distances[neighbors[d]]==INT_MAX)
				{
					distances[neighbors[d]]=distances[current]+1;
					queue.push_back(neighbors[d]);
				}
		}
		resourceDistanceCacheValid[resourceType]=true;
	}
	return resourceDistanceCache[resourceType][index];
}

bool Planner::requiredSourcePresent(const WorldState& world,
	const DevelopmentIntent& intent) const
{
	if(intent.requiredResourceType<0) return true;
	for(size_t i=0; i<world.tiles.size(); ++i)
		if(world.tiles[i].discovered
		   && (intent.requiredResourceType==1 ? world.tiles[i].foodOpportunity>0
			: world.tiles[i].resourceType==intent.requiredResourceType)) return true;
	return false;
}

int Planner::nearestCompletedBuildingDistance(const WorldState& world,
	int x, int y) const
{
	if(int(completedBuildingDistanceCache.size())==world.width*world.height)
		return completedBuildingDistanceCache[world.index(x,y)];
	int result=INT_MAX;
	for(size_t i=0;i<world.buildings.size();++i)
	{
		const WorldBuilding& building=world.buildings[i];
		if(building.site)continue;
		result=std::min(result,world.wrappedManhattan(x,y,
			building.centerX,building.centerY));
	}
	return result;
}

// Match production's food reach: eight-neighbor paths from the building edge,
// bounded by the same supply radius. Only actual discovered corn contributes.
std::vector<int> Planner::colonyFoodTiles(const WorldState& world, int x, int y,
	const Footprint& footprint) const
{
	std::vector<int> distance(world.tiles.size(),-1), queue, food;
	const auto visit=[&](int px,int py,int depth) {
		const int index=world.index(px,py);
		const WorldTile& tile=world.tiles[index];
		if(distance[index]>=0 || !tile.discovered || !tile.foodTraversable
		   || tile.occupied || (tile.water && !world.swimmingBuilders)
		   || (tile.resourceType>=0 && tile.resourceType!=1)) return;
		// A hypothetical building must not offer a shortcut through its footprint.
		if(world.normalizeX(px-x-footprint.left)<footprint.width
		   && world.normalizeY(py-y-footprint.top)<footprint.height)return;
		distance[index]=depth;queue.push_back(index);
	};
	for(int dy=-1;dy<=footprint.height;++dy)
		for(int dx=-1;dx<=footprint.width;++dx)
			if(dx==-1||dy==-1||dx==footprint.width||dy==footprint.height)
				visit(x+footprint.left+dx,y+footprint.top+dy,0);
	for(size_t head=0;head<queue.size();++head)
	{
		const int index=queue[head];
		if(world.tiles[index].foodOpportunity>0)food.push_back(index);
		if(distance[index]>=placementPolicy.colonySupplyRadius)continue;
		for(int dy=-1;dy<=1;++dy)for(int dx=-1;dx<=1;++dx)
			if(dx||dy)visit(index%world.width+dx,index/world.width+dy,distance[index]+1);
	}
	return food;
}

void Planner::prepareColonyClaims(const WorldState& world,int excludeAction) const
{
	colonyFoodClaims.assign(world.tiles.size(),0);colonyAnchors.clear();
	const auto claim=[&](int x,int y,const Footprint& footprint) {
		colonyAnchors.push_back(std::make_pair(x,y));
		for(int index:colonyFoodTiles(world,x,y,footprint))colonyFoodClaims[index]=1;
	};
	for(const WorldBuilding& building:world.buildings)
	{
		if(building.buildingType!=configuredSwarmType
		   && building.buildingType!=configuredInnType)continue;
		const BuildingProfile* profile=world.profile(building.buildingType);
		const BuildingLevelProfile* level=profile?profile->atLevel(std::max(1,building.level)):NULL;
		if(level)claim(building.centerX,building.centerY,level->footprint);
	}
	for(const auto& entry:actionMap)
	{
		const DevelopmentAction& action=entry.second;
		if(action.id==excludeAction || (action.buildingType!=configuredSwarmType
		   && action.buildingType!=configuredInnType))continue;
		if(action.state==ParcelReserved||action.state==CreateIssued||action.state==SiteObserved)
			claim(action.centerX,action.centerY,action.initialFootprint);
	}
}

int Planner::colonyAnchorDistance(const WorldState& world,int x,int y) const
{
	int distance=INT_MAX;
	for(const auto& anchor:colonyAnchors)
		distance=std::min(distance,world.wrappedManhattan(x,y,anchor.first,anchor.second));
	return distance;
}

std::pair<int,int> Planner::colonyBenefit(const WorldState& world,
	const DevelopmentAction& action) const
{
	long long food=0;
	for(int index:colonyFoodTiles(world,action.centerX,action.centerY,action.initialFootprint))
		if(!colonyFoodClaims[index])food+=world.tiles[index].foodOpportunity;
	const int capacity=int(std::min(1000000LL,food/65536));
	const int distance=colonyAnchorDistance(world,action.centerX,action.centerY);
	if(distance==INT_MAX)return std::make_pair(0,0);
	// A transparent cost proxy, not an invented precise completion-time estimate:
	// material deliveries + builder-distance. More distant sites must earn their cost.
	long long cost=1+static_cast<long long>(distance)*std::max(1,action.workers);
	const BuildingProfile* profile=world.profile(action.buildingType);
	const BuildingLevelProfile* level=profile?profile->atLevel(1):NULL;
	if(level)for(int amount:level->constructionResources)cost+=amount;
	return std::make_pair(capacity,int(capacity*100LL/cost));
}

bool Planner::colonyCandidatePasses(const WorldState& world,
	const DevelopmentIntent& intent,const DevelopmentAction& action,
	RejectionReason& reason) const
{
	if(intent.purpose!=ColonySeed)return true;
	const int distance=colonyAnchorDistance(world,action.centerX,action.centerY);
	if(distance==INT_MAX||distance<placementPolicy.colonyMinimumAnchorDistance)
	{reason=RejectedColonyDistance;return false;}
	for(int index:action.parcelTiles)
		if(world.tiles[index].threat>placementPolicy.colonyMaximumThreat)
		{reason=RejectedColonyThreat;return false;}
	const auto benefit=colonyBenefit(world,action);
	if(benefit.first<placementPolicy.colonyMinimumFood)
	{reason=RejectedColonyCorn;return false;}
	if(benefit.second<placementPolicy.colonyMinimumValue)
	{reason=RejectedNegativeUtility;return false;}
	return true;
}

uint32_t Planner::stateSignature(const WorldState& world) const
{
	return stateSignature(world.computeSignature());
}

uint32_t Planner::stateSignature(uint32_t worldSignature) const
{
	uint32_t signature=worldSignature;
	for(size_t i=0;i<footprintRefs.size();++i)if(footprintRefs[i])
	{hashValue(signature,uint32_t(i)^0x46505249u);hashValue(signature,footprintRefs[i]);}
	for(size_t i=0;i<circulationRefs.size();++i)if(circulationRefs[i])
	{hashValue(signature,uint32_t(i)^0x43495243u);hashValue(signature,circulationRefs[i]);}
	for(size_t i=0;i<campusList.size();++i)
	{hashValue(signature,uint32_t(campusList[i].id));hashValue(signature,uint32_t(campusList[i].templateId));}
	return signature;
}

void Planner::prepareRouteCache(const WorldState& world,int orientation) const
{
	const int size=world.width*world.height;
	std::vector<int>& distance=routeDistanceCache[orientation];
	std::vector<int>& parent=routeParentCache[orientation];
	distance.assign(size,INT_MAX);parent.assign(size,-1);
	typedef std::pair<int,int> QueueEntry;
	std::priority_queue<QueueEntry,std::vector<QueueEntry>,
		std::greater<QueueEntry> > queue;
	for(int index=0; index<size; ++index)
	{
		const int x=index%world.width,y=index/world.width;
		const int other=orientation==0?world.index(x+1,y):world.index(x,y+1);
		if(isCirculationReserved(index)&&isCirculationReserved(other))
		{distance[index]=0;queue.push(QueueEntry(0,index));}
	}
	while(!queue.empty())
	{
		const int cost=queue.top().first,current=queue.top().second;queue.pop();
		if(cost!=distance[current])continue;
		const int x=current%world.width,y=current/world.width;
		const int neighbors[4]={world.index(x-1,y),world.index(x+1,y),
			world.index(x,y-1),world.index(x,y+1)};
		for(int d=0;d<4;++d)
		{
			const int next=neighbors[d];
			const int nx=next%world.width,ny=next/world.width;
			const int other=orientation==0?world.index(nx+1,ny):world.index(nx,ny+1);
			const int pair[2]={next,other}; bool pass=true,pairAlreadyNetwork=true;
			int stepCost=1;
			for(int k=0;k<2;++k)
			{
				const WorldTile& tile=world.tiles[pair[k]];
				if(!tile.discovered||!tile.grass||tile.occupied||tile.permanentResource
				   ||isFootprintReserved(pair[k])) {pass=false;break;}
				pairAlreadyNetwork=pairAlreadyNetwork&&isCirculationReserved(pair[k]);
				if(!isCirculationReserved(pair[k]))
				{
					stepCost+=tile.clearableResource
						? placementPolicy.routeClearableResourceCost : 0;
					stepCost+=tile.farmCapacity
						? placementPolicy.routeFarmCost : 0;
					stepCost+=tile.fertility
						? placementPolicy.routeFertilityCost : 0;
				}
			}
			if(!pass)continue;
			if(pairAlreadyNetwork)stepCost=0;
			const int nextCost=cost+stepCost;
			if(nextCost<distance[next]
			   ||(distance[next]>0&&nextCost==distance[next]
			      &&(parent[next]<0||current<parent[next])))
			{
				distance[next]=nextCost;parent[next]=current;
				queue.push(QueueEntry(nextCost,next));
			}
		}
	}
}

bool Planner::routeArtery(const WorldState& world, const std::vector<int>& ring,
	int workers, bool hasNetwork, uint32_t cacheSignature,
	std::vector<int>& route,
	RejectionReason& reason) const
{
	route.clear();
	std::vector<int> targets[2];
	for(size_t i=0;i<ring.size();++i)
	{
		const int x=ring[i]%world.width,y=ring[i]/world.width;
		if(std::binary_search(ring.begin(),ring.end(),world.index(x+1,y)))
			targets[0].push_back(ring[i]);
		if(std::binary_search(ring.begin(),ring.end(),world.index(x,y+1)))
			targets[1].push_back(ring[i]);
	}
	if(targets[0].empty() && targets[1].empty()) {reason=RejectedAccess;return false;}

	if(!hasNetwork)
	{
		if(!world.buildings.empty() && world.swimmingBuilders<workers)
		{
			// A land-connected preexisting ring is installed during adoption. Reaching
			// this branch with buildings present therefore means a new island.
			reason=RejectedIslandBuilders; return false;
		}
		return true;
	}

	const int size=world.width*world.height;
	int selectedCost=INT_MAX,selectedOrientation=-1,selectedTarget=-1;
	if(routeCacheSignature!=cacheSignature
	   ||int(routeDistanceCache[0].size())!=size)
	{
		for(int orientation=0;orientation<2;++orientation)
			prepareRouteCache(world,orientation);
		routeCacheSignature=cacheSignature;
	}
	for(int orientation=0; orientation<2; ++orientation)
	{
		const std::vector<int>& distance=routeDistanceCache[orientation];
		int best=-1;
		for(size_t i=0;i<targets[orientation].size();++i)
		{
			const int target=targets[orientation][i];
			if(distance[target]!=INT_MAX && (best<0 || distance[target]<distance[best]
			   || (distance[target]==distance[best]&&target<best))) best=target;
		}
		if(best>=0&&(distance[best]<selectedCost
		   ||(distance[best]==selectedCost&&(selectedOrientation<0
		      ||orientation<selectedOrientation
		      ||(orientation==selectedOrientation&&best<selectedTarget)))))
		{selectedCost=distance[best];selectedOrientation=orientation;
		 selectedTarget=best;}
	}
	if(selectedTarget>=0)
	{
		const std::vector<int>& selectedParent=routeParentCache[selectedOrientation];
		for(int at=selectedTarget;at>=0;at=selectedParent[at])
		{
			const int x=at%world.width,y=at/world.width;
			const int other=selectedOrientation==0?world.index(x+1,y):world.index(x,y+1);
			if(!isCirculationReserved(at))route.push_back(at);
			if(!isCirculationReserved(other))route.push_back(other);
			if(selectedParent[at]<0)break;
		}
		sortUnique(route);return true;
	}
	if(world.swimmingBuilders>=workers) return true; // root a new land component
	reason=RejectedIslandBuilders; return false;
}

bool Planner::addBuildCandidatesRange(const WorldState& world,
	const DevelopmentIntent& intent, uint32_t signature,
	bool hasNetwork, std::vector<Candidate>& candidates,
	size_t& originCursor, size_t originBudget,
	DevelopmentAction& bestStrictAction, bool& hasStrict, size_t& strictCount,
	DevelopmentAction& bestFallbackAction, bool& hasFallback, size_t& fallbackCount)
{
	const size_t mapArea=size_t(world.width)*size_t(world.height);
	const size_t totalOrigins=templateList.size()*mapArea;
	if(intent.unmetCount<=0) { originCursor=totalOrigins; return true; }
	const std::pair<int,int> key(intent.buildingType,int(intent.purpose));
	std::map<std::pair<int,int>,uint32_t>::const_iterator blocked=
		blockedIntentSignatures.find(key);
	if(blocked!=blockedIntentSignatures.end() && blocked->second==retrySignature(intent,signature))
	{ originCursor=totalOrigins; return true; }
	if(originCursor==0&&!requiredSourcePresent(world,intent))
	{
		lastDiagnostics.rejected[RejectedRequiredSource]++;
		blockedIntentSignatures[key]=retrySignature(intent,signature);
		DevelopmentAction missing;missing.id=nextActionId++;
		missing.state=RequiredSourceMissing;missing.buildingType=intent.buildingType;
		missing.purpose=intent.purpose;
		missing.workers=intent.workers;missing.worldSignature=signature;
		actionMap[missing.id]=missing;
		originCursor=totalOrigins;return true;
	}

	// The selector only needs the best candidate from each intent.  Retaining
	// every legal parcel duplicates several tile vectors thousands of times on
	// large maps, even though all but one are discarded immediately afterwards.
	// Stream the same total ordering into one strict and one fallback winner.
	Candidate bestStrict, bestFallback;
	if(hasStrict)bestStrict.action=bestStrictAction;
	if(hasFallback)bestFallback.action=bestFallbackAction;
	auto retainCandidate=[&](const Candidate& candidate, bool fallbackTier)
	{
		Candidate& best=fallbackTier ? bestFallback : bestStrict;
		bool& present=fallbackTier ? hasFallback : hasStrict;
		size_t& count=fallbackTier ? fallbackCount : strictCount;
		++count;
		if(!present || candidateBetter(candidate,best))
		{
			best=candidate;
			present=true;
		}
	};
	// Existing campuses are always considered before consuming more land, but
	// only on the first slice of this intent.
	if(originCursor==0)for(size_t c=0;c<campusList.size();++c)
	{
		Campus& campus=campusList[c]; const DevelopmentTemplate* t=findTemplate(campus.templateId);
		if(!t||t->buildingType!=intent.buildingType)continue;
		for(size_t s=0;s<campus.slots.size();++s)
		{
			if(campus.slots[s].unusable||campus.slots[s].buildingId>=0
			   ||campus.slots[s].actionId>=0)continue;
			const PlannedSlot& slot=t->slots[s]; Candidate candidate;
			candidate.action.type=BuildCampusMember;candidate.action.templateId=t->id;
			candidate.action.purpose=intent.purpose;
			candidate.action.campusId=campus.id;candidate.action.slotId=int(s);
			candidate.action.buildingType=intent.buildingType;
			candidate.action.centerX=world.normalizeX(campus.originX+slot.centerX);
			candidate.action.centerY=world.normalizeY(campus.originY+slot.centerY);
			candidate.action.workers=intent.workers;candidate.action.targetLevel=1;
			candidate.action.initialFootprint=slot.initialFootprint;
			candidate.action.terminalFootprint=slot.terminalFootprint;
			candidate.action.parcelTiles=footprintTiles(world,candidate.action.centerX,
				candidate.action.centerY,slot.terminalFootprint);
			RejectionReason reason=RejectedTerrain;
			if(!colonyCandidatePasses(world,intent,candidate.action,reason))
			{lastDiagnostics.rejected[reason]++;continue;}
			const std::vector<int> initialTiles=footprintTiles(world,
				candidate.action.centerX,candidate.action.centerY,
				slot.initialFootprint);
			const int coordinateKey=world.index(candidate.action.centerX,
				candidate.action.centerY);
			std::map<int,uint32_t>::const_iterator quarantine=
				coordinateQuarantines.find(coordinateKey);
			// A failed engine coordinate is a local fact. Unrelated discovery and
			// resource changes elsewhere continually alter the world signature, so
			// do not let those changes reactivate the same broken campus slot.
			if(quarantine!=coordinateQuarantines.end())
			{lastDiagnostics.rejected[RejectedQuarantine]++;continue;}
			reason=RejectedTerrain; bool legal=true;
			for(size_t i=0;i<candidate.action.parcelTiles.size();++i)
			{
				const WorldTile& tile=world.tiles[candidate.action.parcelTiles[i]];
				if(!tile.discovered){reason=RejectedUndiscovered;legal=false;break;}
				if(!tile.grass){reason=RejectedTerrain;legal=false;break;}
				if(tile.occupied){reason=RejectedBuilding;legal=false;break;}
				if(tile.gateCorridor){reason=RejectedCirculation;legal=false;break;}
				if(tile.permanentResource){reason=RejectedPermanentResource;legal=false;break;}
				bool conflict=false;
				for(std::map<int,Reservation>::const_iterator reservation=
					reservationMap.begin(); reservation!=reservationMap.end(); ++reservation)
					if(reservation->second.campusId!=candidate.action.campusId
					   &&contains(reservation->second.footprintTiles,
						candidate.action.parcelTiles[i]))
					{conflict=true;break;}
				if(conflict){reason=RejectedReservation;legal=false;break;}
				// Resources outside the immediate footprint may remain inside the
				// long-lived upgrade reservation; reservations never request clearing.
			}
			for(size_t i=0;i<initialTiles.size()&&legal;++i)
				if(world.tiles[initialTiles[i]].clearableResource)
				{reason=RejectedClearableResource;legal=false;}
			if(!legal){lastDiagnostics.rejected[reason]++;continue;}
			candidate.action.fallbackWaterTier=campus.fallbackWaterTier;
			candidate.action.utility=scoreCandidate(world,&intent,candidate);
			retainCandidate(candidate,campus.fallbackWaterTier);
		}
	}

	size_t processed=0;
	while(originCursor<totalOrigins&&processed<std::max<size_t>(1,originBudget))
	{
		const size_t ti=originCursor/mapArea;
		const DevelopmentTemplate& t=templateList[ti];
		if(t.buildingType!=intent.buildingType)
		{
			originCursor=(ti+1)*mapArea;
			continue;
		}
		const size_t origin=originCursor++%mapArea;
		++processed;
		const int originY=int(origin/size_t(world.width));
		const int originX=int(origin%size_t(world.width));
		{
				Candidate candidate;candidate.newCampus=t.compact;
				candidate.action.type=t.compact?BuildCampusMember:BuildStandalone;
				candidate.action.purpose=intent.purpose;
				candidate.action.templateId=t.id;candidate.action.slotId=0;
				candidate.action.buildingType=intent.buildingType;
				candidate.action.centerX=world.normalizeX(originX+t.slots[0].centerX);
				candidate.action.centerY=world.normalizeY(originY+t.slots[0].centerY);
				candidate.action.workers=intent.workers;candidate.action.targetLevel=1;
				candidate.action.initialFootprint=t.slots[0].initialFootprint;
				candidate.action.terminalFootprint=t.slots[0].terminalFootprint;
				RejectionReason reason=RejectedTerrain;
				// Reject illegal rectangles before allocating their parcel vector.
				// Most origins fail this inexpensive pass, so materialize tile lists
				// only for candidates that can reach the scoring stages.
				bool parcelLegal=true;
				for(int dy=0;dy<t.parcel.height&&parcelLegal;++dy)
					for(int dx=0;dx<t.parcel.width;++dx)
					{
						const int index=world.index(originX+dx,originY+dy);
						const WorldTile& tile=world.tiles[index];
						if(!tile.discovered)
						{reason=RejectedUndiscovered;parcelLegal=false;break;}
						if(!tile.grass)
						{reason=RejectedTerrain;parcelLegal=false;break;}
						if(tile.occupied)
						{reason=RejectedBuilding;parcelLegal=false;break;}
						if(tile.gateCorridor)
						{reason=RejectedCirculation;parcelLegal=false;break;}
						if(tile.permanentResource)
						{reason=RejectedPermanentResource;parcelLegal=false;break;}
						if(isFootprintReserved(index))
						{reason=RejectedReservation;parcelLegal=false;break;}
						if(isCirculationReserved(index))
						{reason=RejectedCirculation;parcelLegal=false;break;}
					}
				if(!parcelLegal)
				{lastDiagnostics.rejected[reason]++;continue;}
				candidate.action.parcelTiles=parcelTiles(world,originX,originY,t);
				if(!colonyCandidatePasses(world,intent,candidate.action,reason))
				{lastDiagnostics.rejected[reason]++;continue;}
				const std::vector<int> initialTiles=footprintTiles(world,
					candidate.action.centerX,candidate.action.centerY,
					candidate.action.initialFootprint);
				bool initialClear=true;
				for(size_t i=0;i<initialTiles.size();++i)
					if(world.tiles[initialTiles[i]].clearableResource)
					{initialClear=false;break;}
				if(!initialClear)
				{lastDiagnostics.rejected[RejectedClearableResource]++;continue;}
				candidate.action.accessTiles=parcelRingTiles(world,originX,originY,t);
				bool ringLegal=true;
				for(size_t i=0;i<candidate.action.accessTiles.size();++i)
				{
					const int index=candidate.action.accessTiles[i];const WorldTile& tile=world.tiles[index];
					if(!tile.discovered){reason=RejectedUndiscovered;ringLegal=false;break;}
					if(!tile.grass||tile.occupied||tile.permanentResource
					   ||isFootprintReserved(index)){reason=RejectedAccess;ringLegal=false;break;}
				}
				if(!ringLegal){lastDiagnostics.rejected[reason]++;continue;}
				if(placementPolicy.arteryRoutingEnabled
				   && !routeArtery(world,candidate.action.accessTiles,intent.workers,
					hasNetwork,signature,candidate.action.arteryTiles,reason))
				{lastDiagnostics.rejected[reason]++;continue;}
				// Origins rejected by the rectangle checks are cheap; origins reaching
				// routing and scoring are not. Charge a deterministic extra weight so
				// the per-update budget tracks CPU work without using wall-clock timing,
				// which would make networked matches hardware-dependent.
				processed+=63;
				bool ringConnected=false;
				for(size_t n=0;n<candidate.action.accessTiles.size()&&!ringConnected;++n)
				{
					const int index=candidate.action.accessTiles[n];
					const int x=index%world.width,y=index/world.width;
					ringConnected=isCirculationReserved(index)
						&&(isCirculationReserved(world.index(x+1,y))
						   ||isCirculationReserved(world.index(x,y+1)));
				}
				candidate.action.requiresSwimmingBuilders=placementPolicy.arteryRoutingEnabled
					&&hasNetwork
					&&candidate.action.arteryTiles.empty()&&!ringConnected;
				bool routeCrossesParcel=false;
				for(size_t r=0;r<candidate.action.arteryTiles.size();++r)
					if(contains(candidate.action.parcelTiles,candidate.action.arteryTiles[r]))
					{routeCrossesParcel=true;break;}
				if(routeCrossesParcel){lastDiagnostics.rejected[RejectedCirculation]++;continue;}
				const int coordinateKey=world.index(candidate.action.centerX,candidate.action.centerY);
				std::map<int,uint32_t>::const_iterator quarantine=coordinateQuarantines.find(coordinateKey);
				if(quarantine!=coordinateQuarantines.end())
				{lastDiagnostics.rejected[RejectedQuarantine]++;continue;}
				if(waterTierPasses(world,candidate.action.parcelTiles,6))
				{
					candidate.action.fallbackWaterTier=false;
					candidate.action.utility=scoreCandidate(world,&intent,candidate);
					retainCandidate(candidate,false);
				}
				else if(waterTierPasses(world,candidate.action.parcelTiles,5))
				{
					candidate.action.fallbackWaterTier=true;
					candidate.action.utility=scoreCandidate(world,&intent,candidate);
					retainCandidate(candidate,true);
				}
				else lastDiagnostics.rejected[RejectedWaterTier]++;
		}
	}
	bestStrictAction=bestStrict.action;
	bestFallbackAction=bestFallback.action;
	if(originCursor<totalOrigins)return false;
	lastDiagnostics.strictCandidateCount+=strictCount;
	lastDiagnostics.fallbackCandidateCount+=fallbackCount;
	// The fallback tier is searched only when no strict legal candidate exists.
	if(hasStrict)
	{
		candidates.push_back(bestStrict);
		lastDiagnostics.candidateCount+=strictCount;
	}
	else if(hasFallback)
	{
		candidates.push_back(bestFallback);
		lastDiagnostics.candidateCount+=fallbackCount;
	}
	return true;
}

void Planner::addBuildCandidates(const WorldState& world,
	const DevelopmentIntent& intent, uint32_t signature,
	bool hasNetwork, std::vector<Candidate>& candidates)
{
	size_t cursor=0,strictCount=0,fallbackCount=0;
	DevelopmentAction bestStrict,bestFallback;
	bool hasStrict=false,hasFallback=false;
	while(!addBuildCandidatesRange(world,intent,signature,hasNetwork,candidates,
		cursor,size_t(-1),bestStrict,hasStrict,strictCount,
		bestFallback,hasFallback,fallbackCount)) {}
}

int Planner::findCampusIndex(int id) const
{ for(size_t i=0;i<campusList.size();++i)if(campusList[i].id==id)return int(i);return -1; }
int Planner::findStandaloneIndex(int buildingId) const
{ for(size_t i=0;i<standaloneList.size();++i)if(standaloneList[i].buildingId==buildingId)return int(i);return -1; }
int Planner::contractMaximumLevel(int buildingId, int buildingType) const
{
	const int standalone=findStandaloneIndex(buildingId);
	if(standalone>=0)return standaloneList[standalone].maximumLevel;
	for(size_t c=0;c<campusList.size();++c)
	{
		const DevelopmentTemplate* t=findTemplate(campusList[c].templateId);
		if(!t||t->buildingType!=buildingType)continue;
		for(size_t s=0;s<campusList[c].slots.size();++s)
			if(campusList[c].slots[s].buildingId==buildingId)return t->slots[s].maximumLevel;
	}
	return 0;
}
int Planner::contractReservationId(int buildingId, int buildingType) const
{
	const int standalone=findStandaloneIndex(buildingId);
	if(standalone>=0)return standaloneList[standalone].reservationId;
	for(size_t c=0;c<campusList.size();++c)
	{
		const DevelopmentTemplate* t=findTemplate(campusList[c].templateId);
		if(!t||t->buildingType!=buildingType)continue;
		for(size_t s=0;s<campusList[c].slots.size();++s)
			if(campusList[c].slots[s].buildingId==buildingId)
				for(std::map<int,Reservation>::const_iterator r=reservationMap.begin();r!=reservationMap.end();++r)
					if(r->second.campusId==campusList[c].id&&r->second.permanent)return r->first;
	}
	return -1;
}

void Planner::addUpgradeAndRepairCandidates(const WorldState& world,
	const DevelopmentLimits& limits, std::vector<Candidate>& candidates)
{
	for(size_t b=0;b<world.buildings.size();++b)
	{
		const WorldBuilding& building=world.buildings[b];
		if(building.site||building.upgrading)continue;
		bool alreadyActive=false;
		for(std::map<int,DevelopmentAction>::const_iterator a=actionMap.begin();a!=actionMap.end();++a)
			if(activeState(a->second.state)&&a->second.buildingId==building.id)
			{alreadyActive=true;break;}
		if(alreadyActive)continue;
		const BuildingProfile* profile=world.profile(building.buildingType);
		if(!profile)continue;

		if(building.hp<building.hpMax)
		{
			if(!limits.allowRepairs)
				continue;
			Candidate candidate;candidate.action.type=RepairBuilding;
			candidate.action.buildingId=building.id;candidate.action.buildingType=building.buildingType;
			candidate.action.fromLevel=building.level;candidate.action.targetLevel=building.level;
			candidate.action.centerX=building.centerX;candidate.action.centerY=building.centerY;
			candidate.action.workers=1;RejectionReason reason=RejectedUpgradeContract;
			if(!revalidate(world,candidate.action,&reason))
			{lastDiagnostics.rejected[reason]++;continue;}
			candidate.action.utility=scoreCandidate(world,NULL,candidate);
			candidates.push_back(candidate);
			continue;
		}
		if(!limits.allowUpgrades)continue;
		const int priority=limits.upgradePriority(building.buildingType,building.level);
		if(priority==0)continue;
		if(building.level==1 && limits.activeLevel1Upgrades>=limits.level1Upgrades)continue;
		if(building.level==2 && (!limits.allowLevel2Upgrades
		   ||limits.activeLevel2Upgrades>=limits.level2Upgrades))continue;
		if(building.level<1||building.level>=profile->maximumLevel())continue;
		const int maximum=contractMaximumLevel(building.id,building.buildingType);
		if(maximum<=building.level){lastDiagnostics.rejected[RejectedUpgradeContract]++;continue;}
		if(building.buildingType==configuredSchoolType&&building.level==2)
		{
			bool specialistActive=false;
			for(std::map<int,DevelopmentAction>::const_iterator a=actionMap.begin();
				a!=actionMap.end();++a)
				if(activeState(a->second.state)&&a->second.type==UpgradeBuilding
				   &&a->second.buildingType==configuredSchoolType
				   &&a->second.targetLevel==3){specialistActive=true;break;}
			if(specialistActive)continue;
		}
		Candidate candidate;candidate.action.type=UpgradeBuilding;
		candidate.action.buildingId=building.id;candidate.action.buildingType=building.buildingType;
		candidate.action.fromLevel=building.level;candidate.action.targetLevel=building.level+1;
		candidate.action.centerX=building.centerX;candidate.action.centerY=building.centerY;
		candidate.action.workers=building.level==1
			? placementPolicy.upgradeLevel1Workers
			: placementPolicy.upgradeLevel2Workers;
		RejectionReason reason=RejectedUpgradeContract;
		if(!revalidate(world,candidate.action,&reason))
		{lastDiagnostics.rejected[reason]++;continue;}
		candidate.action.utility=scoreCandidate(world,NULL,candidate);
		if(priority>0)
		{
			// Replace only demand; retain the location's original spacing score.
			const int demand=clamp100(priority);
			candidate.action.utility.total+=placementPolicy.unmetDemandWeight
				*(demand-candidate.action.utility.unmetDemand);
			candidate.action.utility.unmetDemand=demand;
		}
		candidates.push_back(candidate);
	}
}

UtilityComponents Planner::scoreCandidate(const WorldState& world,
	const DevelopmentIntent* intent, const Candidate& candidate) const
{
	UtilityComponents u; const DevelopmentAction& action=candidate.action;
	const DevelopmentPurpose purpose=intent?intent->purpose:CoreCapacity;
	u.unmetDemand=intent?clamp100(intent->priority+std::min(
		placementPolicy.unmetCountCap,
		intent->unmetCount*placementPolicy.unmetCountWeight))
		:placementPolicy.upgradeUnmetDemand;
	const BuildingProfile* profile=world.profile(action.buildingType);
	const BuildingLevelProfile* from=profile?profile->atLevel(action.fromLevel):NULL;
	const BuildingLevelProfile* target=profile?profile->atLevel(action.targetLevel):NULL;
	const int fromService=(action.type==UpgradeBuilding&&from)?from->serviceThroughput:0;
	const int targetService=target?target->serviceThroughput:0;
	u.serviceGain=clamp100((targetService-fromService)*placementPolicy.serviceGainScale);
	u.capabilityGain=clamp100(target
		?(target->capability-(from?from->capability:0))
			*placementPolicy.capabilityGainScale:0);
	u.parallelismGain=clamp100(targetService>0
		?placementPolicy.parallelServiceBase
			+(action.type<=BuildStandalone?placementPolicy.parallelBuildBonus:0)
		:placementPolicy.parallelNoService);
	int same=0;
	std::map<int,int>::const_iterator sameType=
		completedBuildingCountCache.find(action.buildingType);
	if(sameType!=completedBuildingCountCache.end())same=sameType->second;
	u.redundancyGain=clamp100(same==0?placementPolicy.duplicateFirstScore
		:placementPolicy.duplicateScoreScale/same);

	// Most build actions already own their location vector.  Borrow it instead of
	// copying it for every score; upgrades use this one fallback scratch vector.
	std::vector<int> footprintLocationTiles;
	const std::vector<int>* locationTiles=&action.parcelTiles;
	if(locationTiles->empty()&&target)
	{
		footprintLocationTiles=footprintTiles(world,action.centerX,
			action.centerY,target->footprint);
		locationTiles=&footprintLocationTiles;
	}
	int threat=0,protection=0;
	const DevelopmentTemplate* t=findTemplate(action.templateId);
	Footprint locationRectangle;
	int locationOriginX=0,locationOriginY=0;
	if(candidate.newCampus&&t&&!t->slots.empty())
	{
		locationRectangle=t->parcel;
		locationOriginX=action.centerX-t->slots[0].centerX;
		locationOriginY=action.centerY-t->slots[0].centerY;
	}
	else if(!action.parcelTiles.empty())
	{
		locationRectangle=action.terminalFootprint;
		locationOriginX=action.centerX+locationRectangle.left;
		locationOriginY=action.centerY+locationRectangle.top;
	}
	else if(target)
	{
		locationRectangle=target->footprint;
		locationOriginX=action.centerX+locationRectangle.left;
		locationOriginY=action.centerY+locationRectangle.top;
	}
	if(!locationRectangle.empty()
	   &&locationRectangle.width<=world.width
	   &&locationRectangle.height<=world.height
	   &&int(threatPrefixCache.size())==(world.width+1)*(world.height+1))
	{
		const int stride=world.width+1;
		auto rectangleSum=[stride](const std::vector<int>& prefix,
			int x,int y,int width,int height)->int
		{
			const int right=x+width,bottom=y+height;
			return prefix[bottom*stride+right]-prefix[y*stride+right]
				-prefix[bottom*stride+x]+prefix[y*stride+x];
		};
		const int x=world.normalizeX(locationOriginX);
		const int y=world.normalizeY(locationOriginY);
		const int firstWidth=std::min(locationRectangle.width,world.width-x);
		const int secondWidth=locationRectangle.width-firstWidth;
		const int firstHeight=std::min(locationRectangle.height,world.height-y);
		const int secondHeight=locationRectangle.height-firstHeight;
		threat=rectangleSum(threatPrefixCache,x,y,firstWidth,firstHeight);
		protection=rectangleSum(protectionPrefixCache,x,y,firstWidth,firstHeight);
		if(secondWidth)
		{
			threat+=rectangleSum(threatPrefixCache,0,y,secondWidth,firstHeight);
			protection+=rectangleSum(protectionPrefixCache,0,y,secondWidth,firstHeight);
		}
		if(secondHeight)
		{
			threat+=rectangleSum(threatPrefixCache,x,0,firstWidth,secondHeight);
			protection+=rectangleSum(protectionPrefixCache,x,0,firstWidth,secondHeight);
			if(secondWidth)
			{
				threat+=rectangleSum(threatPrefixCache,0,0,secondWidth,secondHeight);
				protection+=rectangleSum(protectionPrefixCache,0,0,
					secondWidth,secondHeight);
			}
		}
	}
	else for(size_t i=0;i<locationTiles->size();++i)
	{
		threat+=world.tiles[(*locationTiles)[i]].threat;
		protection+=world.tiles[(*locationTiles)[i]].protectedness;
	}
	if(!locationTiles->empty())
	{threat/=locationTiles->size();protection/=locationTiles->size();}
	u.threatExposure=clamp100(threat);u.defendedness=clamp100(protection-threat/2);
	int spacingQuality=100;
	const bool opensNewParcel=(candidate.newCampus
		||action.type==BuildStandalone)&&purpose!=ColonySeed;
	if(opensNewParcel&&!locationTiles->empty()&&!footprintDistanceCache.empty())
	{
		int distance=INT_MAX;
		for(size_t i=0;i<locationTiles->size();++i)
			distance=std::min(distance,
				footprintDistanceCache[(*locationTiles)[i]]);
		const int gap=distance==INT_MAX ? placementPolicy.spacingTargetTiles
			:std::max(0,distance-1);
		u.friendlyDistance=gap;
		spacingQuality=placementPolicy.spacingTargetTiles<=0 ? 100
			:clamp100(gap*100/placementPolicy.spacingTargetTiles);
	}
	if(action.buildingType==configuredSchoolType)
		u.threatExposure=clamp100(threat+threat/2);

	auto resourceQuality=[this,&world,&action](int resourceType)->int
	{
		const int best=resourceDistanceAt(world,resourceType,
			world.index(action.centerX,action.centerY));
		return best==INT_MAX?0:clamp100(100-best*placementPolicy.resourceDistanceWeight);
	};
	int materialType=-1,materialNeed=0;
	if(target)for(int resource=0;resource<5;++resource)
		if(target->constructionResources[resource]>materialNeed)
		{materialNeed=target->constructionResources[resource];materialType=resource;}
	const int materialQuality=materialType>=0?resourceQuality(materialType):0;
	if(action.buildingType==configuredInnType)
	{
		int fruitQuality=0;
		for(int fruit=5;fruit<=7;++fruit)fruitQuality=std::max(fruitQuality,resourceQuality(fruit));
		u.roleLocationQuality=clamp100((resourceQuality(1)*3+fruitQuality)/4);
	}
	else if(action.buildingType==configuredSwarmType)
		u.roleLocationQuality=clamp100((resourceQuality(1)*3+protection)/4);
	else if(action.buildingType==configuredSchoolType)
		u.roleLocationQuality=clamp100(protection-threat);
	else if(action.buildingType==configuredHospitalType)
	{
		u.roleLocationQuality=clamp100(protection-threat/2);
		if(t&&t->id==HospitalDispersed)
			u.roleLocationQuality=clamp100(u.roleLocationQuality+threat/2);
	}
	else if(action.buildingType==configuredBarracksType)
		u.roleLocationQuality=clamp100(protection-threat); // no deployment reward
	else if(action.buildingType==configuredTowerType)
	{
		int criticalDistance=INT_MAX,towerDistance=INT_MAX;
		if(action.buildingId<0&&!criticalBuildingDistanceCache.empty())
		{
			const int index=world.index(action.centerX,action.centerY);
			criticalDistance=criticalBuildingDistanceCache[index];
			towerDistance=towerBuildingDistanceCache[index];
		}
		else for(size_t i=0;i<world.buildings.size();++i)
		{
			const WorldBuilding& building=world.buildings[i];
			const int distance=world.wrappedManhattan(action.centerX,action.centerY,
				building.centerX,building.centerY);
			if(building.buildingType==configuredSwarmType
			   ||building.buildingType==configuredInnType
			   ||building.buildingType==configuredSchoolType)
				criticalDistance=std::min(criticalDistance,distance);
			if(building.buildingType==configuredTowerType&&building.id!=action.buildingId)
				towerDistance=std::min(towerDistance,distance);
		}
		const int criticalQuality=criticalDistance==INT_MAX?0:
			clamp100(100-criticalDistance*placementPolicy.towerCriticalDistanceWeight);
		const int spacingQuality=towerDistance==INT_MAX?100:
			clamp100(100-std::abs(towerDistance-placementPolicy.towerSpacingTarget)
				*placementPolicy.towerSpacingWeight);
		const int threatFacing=clamp100(100
			-std::abs(threat-placementPolicy.towerThreatTarget)
				*placementPolicy.towerThreatWeight);
		// Reward a real firing arc over an uncovered entrance. A generic
		// protectedness field also attracts inns and schools, so it cannot
		// express this tower-specific defensive purpose.
		const int gateQuality=world.tile(action.centerX,action.centerY).gateDefense;
		u.roleLocationQuality=clamp100((criticalQuality*4+resourceQuality(3)*2
			+threatFacing*2+spacingQuality+protection
			+gateQuality*placementPolicy.towerGateWeight)/(10+placementPolicy.towerGateWeight));
	}
	else
		// Training institutions stay inside the settlement while favoring the
		// material that dominates their actual construction-site requirements.
		u.roleLocationQuality=clamp100((materialQuality*3+protection*2)/5);
	if(purpose==ColonySeed)
	{
		u.friendlyDistance=colonyAnchorDistance(world,action.centerX,action.centerY);
		u.cornDistance=resourceDistanceAt(world,1,world.index(action.centerX,action.centerY));
		const auto benefit=colonyBenefit(world,action);
		// Preserve the legacy serialized slots; their colony meanings are now
		// new food capacity and food-per-cost, not frontier/conquest bonuses.
		u.frontierGain=benefit.first;
		u.conqueredGain=benefit.second;
	}

	if(t&&t->compact)
	{
		const int standalone=(t->slots[0].terminalFootprint.area()+
			2*t->slots[0].terminalFootprint.width+2*t->slots[0].terminalFootprint.height+4)
			*int(t->slots.size());
		const int campus=t->parcel.area()+t->accessRing.count();
		u.compactness=clamp100((standalone-campus)*100/std::max(1,standalone));
	}

	// These buffers are temporary working sets and never escape scoreCandidate.
	// Reusing their capacity avoids thousands of allocator calls during a map scan.
	std::vector<int>& reserved=scoringReservedScratch;reserved.clear();
	reserved.reserve(action.parcelTiles.size()+action.accessTiles.size()
		+action.arteryTiles.size());
	reserved.insert(reserved.end(),action.parcelTiles.begin(),action.parcelTiles.end());
	reserved.insert(reserved.end(),action.accessTiles.begin(),action.accessTiles.end());
	reserved.insert(reserved.end(),action.arteryTiles.begin(),action.arteryTiles.end());
	if(action.type==BuildCampusMember&&action.campusId>=0)
		reserved.clear();
	if(++scoringGeneration==0)
	{
		std::fill(scoringReservedGeneration.begin(),scoringReservedGeneration.end(),0);
		std::fill(scoringAffectedGeneration.begin(),scoringAffectedGeneration.end(),0);
		scoringGeneration=1;
	}
	// Candidate parcels, rings and arteries are individually sorted, but their
	// concatenation can overlap.  The scorer needs uniqueness, not ordering, so
	// use the existing generation mask as an O(n) set instead of sorting every
	// candidate's complete reservation.
	size_t uniqueReserved=0;
	for(size_t i=0;i<reserved.size();++i)
		if(scoringReservedGeneration[reserved[i]]!=scoringGeneration)
		{
			scoringReservedGeneration[reserved[i]]=scoringGeneration;
			reserved[uniqueReserved++]=reserved[i];
		}
	reserved.resize(uniqueReserved);
	uint64_t farmLoss=0;const uint64_t maxFarm=maximumFarmCapacityCache;
	std::vector<int>& affected=scoringAffectedScratch;affected.clear();
	affected.reserve(reserved.size()*9);
	for(size_t i=0;i<reserved.size();++i)
	{
		const int* neighborhood=&scoringNeighborhoodCache[reserved[i]*9];
		for(int offset=0;offset<9;++offset)
		{
			const int index=neighborhood[offset];
			if(scoringAffectedGeneration[index]!=scoringGeneration)
			{
				scoringAffectedGeneration[index]=scoringGeneration;
				scoringBlockedNeighbors[index]=0;
				affected.push_back(index);
			}
			if(offset!=4)++scoringBlockedNeighbors[index];
		}
	}
	// Count blocked neighbours while expanding the reservation above.  This is
	// the reverse of asking every affected tile about its eight neighbours; the
	// counts (and the per-tile integer division below) are exactly identical.
	for(size_t i=0;i<affected.size();++i)
	{
		const int index=affected[i];
		const int blockedNeighbors=scoringBlockedNeighbors[index];
		farmLoss+=scoringReservedGeneration[index]==scoringGeneration
			?world.tiles[index].farmCapacity
			:world.tiles[index].farmCapacity*blockedNeighbors/8;
	}
	u.projectedFarmLoss=clamp100(int(farmLoss*100/(maxFarm*std::max<size_t>(1,affected.size()))));
	// Reserve the high-value food halo for food infrastructure.  This is a
	// proximity opportunity cost, distinct from projectedFarmLoss: the latter
	// prevents every role from paving over productive cells, while this term
	// discourages new inner-settlement parcels from claiming the nearby sites
	// where inns and swarms are most useful.
	if((action.type==BuildCampusMember||action.type==BuildStandalone)
	   && !reserved.empty() && action.buildingType!=configuredInnType
	   && action.buildingType!=configuredSwarmType)
	{
		uint64_t strongest=0;
		for(size_t i=0;i<reserved.size();++i)
			strongest=std::max(strongest,foodHaloMaximumCache[reserved[i]]);
		const int radius=placementPolicy.foodZoneRadius;
		const uint64_t maximum=maximumFoodOpportunityCache*uint64_t(radius+1);
		int multiplier=placementPolicy.innerFoodZoneMultiplier;
		if(action.buildingType==configuredHospitalType)
			multiplier=placementPolicy.hospitalFoodZoneMultiplier;
		else if(action.buildingType==configuredTowerType)
			multiplier=placementPolicy.towerFoodZoneMultiplier;
		u.foodZonePressure=clamp100(int(strongest*100
			/std::max<uint64_t>(1,maximum)));
		u.foodZonePressure=clamp100(u.foodZonePressure*multiplier/100);
	}
	u.newlyReservedLand=clamp100(int(reserved.size()*100/std::max(1,world.width*world.height/8)));
	if(target)
	{
		int scarcity=0,needed=0;
		for(int r=0;r<5;++r)if(target->constructionResources[r]>0)
		{needed+=target->constructionResources[r];scarcity+=std::max(0,target->constructionResources[r]-world.accessibleSupplies[r]);}
		u.resourceScarcity=needed?clamp100(scarcity*100/needed):0;
	}
	u.constructionLabor=clamp100(action.workers*placementPolicy.laborScale);
	if((action.type==UpgradeBuilding||action.type==RepairBuilding)&&from)
	{
		int constructionWork=1;
		if(target)for(int resource=0;resource<5;++resource)
			constructionWork+=target->constructionResources[resource];
		u.serviceDowntime=clamp100(from->serviceThroughput*constructionWork
			/std::max(1,action.workers*placementPolicy.downtimeWorkerScale));
	}
	u.newArteryLength=clamp100(int(action.arteryTiles.size())
		*placementPolicy.arteryLengthScale);
	if(action.type==RepairBuilding)
	{
		const WorldBuilding* b=world.building(action.buildingId);
		const int damage=b&&b->hpMax?100-(b->hp*100/b->hpMax):0;
		u.unmetDemand=clamp100(placementPolicy.repairBaseDemand+damage);
		u.redundancyGain=clamp100(damage);
		u.serviceGain=clamp100(damage+(from?from->serviceThroughput:0));
	}
	u.total=placementPolicy.score(u,purpose,spacingQuality); return u;
}

bool Planner::candidateBetter(const Candidate& lhs, const Candidate& rhs) const
{
	const DevelopmentAction& a=lhs.action;const DevelopmentAction& b=rhs.action;
	if(a.utility.total!=b.utility.total)return a.utility.total>b.utility.total;
	if(a.fallbackWaterTier!=b.fallbackWaterTier)return !a.fallbackWaterTier;
	if(a.type!=b.type)return a.type<b.type;
	if(a.templateId!=b.templateId)return a.templateId<b.templateId;
	if(a.campusId!=b.campusId)return a.campusId<b.campusId;
	if(a.slotId!=b.slotId)return a.slotId<b.slotId;
	if(a.buildingId!=b.buildingId)return a.buildingId<b.buildingId;
	if(a.centerY!=b.centerY)return a.centerY<b.centerY;
	return a.centerX<b.centerX;
}

void Planner::prepareRetrySignature(const WorldState& world)
{
	// Geometry caches use the occupancy signature. Failed selections also depend
	// on live demand, mobility, resources and utility, so use a separate key.
	uint32_t signature=2166136261u;
	hashValue(signature,world.swimmingBuilders);
	for(int r=0;r<5;++r)hashValue(signature,world.accessibleSupplies[r]);
	for(const WorldTile& tile:world.tiles)
	{
		hashValue(signature,tile.fertility);hashValue(signature,tile.farmCapacity);
		hashValue(signature,tile.foodOpportunity);hashValue(signature,tile.threat);
		hashValue(signature,tile.protectedness);hashValue(signature,tile.conqueredOpportunity);
	}
	for(const WorldBuilding& building:world.buildings)
	{
		hashValue(signature,building.level);hashValue(signature,building.site);
		hashValue(signature,building.upgrading);hashValue(signature,building.hp);
		hashValue(signature,building.hpMax);
	}
	for(const BuildingProfile& profile:world.profiles)
	{
		hashValue(signature,profile.buildingType);
		for(const BuildingLevelProfile& level:profile.levels)
		{
			hashValue(signature,level.level);hashValue(signature,level.serviceThroughput);
			hashValue(signature,level.capability);
			for(int r=0;r<5;++r)hashValue(signature,level.constructionResources[r]);
		}
	}
	hashValue(signature,placementPolicy.arteryRoutingEnabled);
	hashValue(signature,placementPolicy.unmetDemandWeight);
	hashValue(signature,placementPolicy.serviceGainWeight);
	hashValue(signature,placementPolicy.capabilityGainWeight);
	hashValue(signature,placementPolicy.parallelismGainWeight);
	hashValue(signature,placementPolicy.redundancyGainWeight);
	hashValue(signature,placementPolicy.roleLocationQualityWeight);
	hashValue(signature,placementPolicy.defendednessWeight);
	hashValue(signature,placementPolicy.compactnessWeight);
	hashValue(signature,placementPolicy.spacingTargetTiles);
	hashValue(signature,placementPolicy.spacingWeight);
	hashValue(signature,placementPolicy.projectedFarmLossWeight);
	hashValue(signature,placementPolicy.foodZonePenaltyWeight);
	hashValue(signature,placementPolicy.newlyReservedLandWeight);
	hashValue(signature,placementPolicy.resourceScarcityWeight);
	hashValue(signature,placementPolicy.constructionLaborWeight);
	hashValue(signature,placementPolicy.serviceDowntimeWeight);
	hashValue(signature,placementPolicy.threatExposureWeight);
	hashValue(signature,placementPolicy.newArteryLengthWeight);
	hashValue(signature,placementPolicy.upgradeLevel1Workers);
	hashValue(signature,placementPolicy.upgradeLevel2Workers);
	hashValue(signature,placementPolicy.unmetCountWeight);
	hashValue(signature,placementPolicy.unmetCountCap);
	hashValue(signature,placementPolicy.upgradeUnmetDemand);
	hashValue(signature,placementPolicy.serviceGainScale);
	hashValue(signature,placementPolicy.capabilityGainScale);
	hashValue(signature,placementPolicy.parallelServiceBase);
	hashValue(signature,placementPolicy.parallelBuildBonus);
	hashValue(signature,placementPolicy.parallelNoService);
	hashValue(signature,placementPolicy.duplicateFirstScore);
	hashValue(signature,placementPolicy.duplicateScoreScale);
	hashValue(signature,placementPolicy.resourceDistanceWeight);
	hashValue(signature,placementPolicy.foodZoneRadius);
	hashValue(signature,placementPolicy.innerFoodZoneMultiplier);
	hashValue(signature,placementPolicy.hospitalFoodZoneMultiplier);
	hashValue(signature,placementPolicy.towerFoodZoneMultiplier);
	hashValue(signature,placementPolicy.towerCriticalDistanceWeight);
	hashValue(signature,placementPolicy.towerSpacingTarget);
	hashValue(signature,placementPolicy.towerSpacingWeight);
	hashValue(signature,placementPolicy.towerThreatTarget);
	hashValue(signature,placementPolicy.towerThreatWeight);
	hashValue(signature,placementPolicy.towerGateWeight);
	hashValue(signature,placementPolicy.laborScale);
	hashValue(signature,placementPolicy.downtimeWorkerScale);
	hashValue(signature,placementPolicy.arteryLengthScale);
	hashValue(signature,placementPolicy.repairBaseDemand);
	hashValue(signature,placementPolicy.actionTimeoutTicks);
	hashValue(signature,placementPolicy.routeClearableResourceCost);
	hashValue(signature,placementPolicy.routeFarmCost);
	hashValue(signature,placementPolicy.routeFertilityCost);
	hashValue(signature,placementPolicy.colonyMinimumAnchorDistance);
	hashValue(signature,placementPolicy.colonyMaximumThreat);
	hashValue(signature,placementPolicy.colonyMinimumFood);
	hashValue(signature,placementPolicy.colonyMinimumValue);
	hashValue(signature,placementPolicy.colonySupplyRadius);
	retryInputSignature=signature;
}

uint32_t Planner::retrySignature(const DevelopmentIntent& intent,uint32_t spatialSignature) const
{
	uint32_t signature=spatialSignature;
	hashValue(signature,retryInputSignature);
	hashValue(signature,intent.buildingType);hashValue(signature,intent.purpose);
	hashValue(signature,intent.unmetCount);hashValue(signature,intent.priority);
	hashValue(signature,intent.workers);hashValue(signature,intent.requiredResourceType);
	hashValue(signature,intent.emergency);
	return signature;
}

void Planner::recordBlocked(const std::vector<DevelopmentIntent>& intents,uint32_t signature)
{
	for(size_t i=0;i<intents.size();++i)if(intents[i].unmetCount>0)
		blockedIntentSignatures[std::make_pair(intents[i].buildingType,
			int(intents[i].purpose))]=retrySignature(intents[i],signature);
}

bool Planner::selectAction(const WorldState& world,
	const std::vector<DevelopmentIntent>& intents,const DevelopmentLimits& limits,
	DevelopmentAction& selected)
{
	return selectAction(world,intents,limits,selected,world.computeSignature());
}

bool Planner::selectAction(const WorldState& world,
	const std::vector<DevelopmentIntent>& intents,const DevelopmentLimits& limits,
	DevelopmentAction& selected,uint32_t occupancySignature)
{
	clearIncrementalSelection();
	lastDiagnostics.clear();ensureMaskSize(world.width*world.height);
	prepareScoringCaches(world);
	prepareRetrySignature(world);
	const uint32_t signature=stateSignature(occupancySignature);
	const bool hasNetwork=circulationReservedTileCount!=0;
	std::vector<Candidate> candidates;
	if(limits.activeNewConstruction<limits.newConstruction)
	{
		prepareWaterDistanceCache(world);
		for(size_t i=0;i<intents.size();++i)
			addBuildCandidates(world,intents[i],signature,hasNetwork,candidates);
	}
	const size_t buildWinners=candidates.size();
	addUpgradeAndRepairCandidates(world,limits,candidates);
	lastDiagnostics.candidateCount+=candidates.size()-buildWinners;
	if(candidates.empty())
	{
		if(limits.activeNewConstruction<limits.newConstruction)
			recordBlocked(intents,signature);
		return false;
	}
	Candidate best=candidates[0];
	for(size_t i=1;i<candidates.size();++i)if(candidateBetter(candidates[i],best))best=candidates[i];
	if(best.action.utility.total<0)
	{
		lastDiagnostics.rejected[RejectedNegativeUtility]++;
		if(limits.activeNewConstruction<limits.newConstruction)
			recordBlocked(intents,signature);
		return false;
	}
	selected=best.action; selected.id=nextActionId++;selected.worldSignature=signature;
	lastDiagnostics.waterTier=selected.fallbackWaterTier?5:6;
	lastDiagnostics.selectedActionId=selected.id;lastDiagnostics.selectedUtility=selected.utility;
	return true;
}

SelectionProgress Planner::selectActionIncremental(const WorldState& world,
	const std::vector<DevelopmentIntent>& intents,const DevelopmentLimits& limits,
	DevelopmentAction& selected,uint32_t occupancySignature,int originBudget)
{
	if(!incrementalSelectionActive)
	{
		lastDiagnostics.clear();ensureMaskSize(world.width*world.height);
		// Snapshotting is intentional: spreading a selector over several engine
		// updates must not mix candidate scores from different world states.
		incrementalWorld=world;
		incrementalIntents=intents;
		incrementalLimits=limits;
		incrementalOccupancySignature=occupancySignature;
		incrementalSignature=stateSignature(occupancySignature);
		incrementalHasNetwork=circulationReservedTileCount!=0;
		incrementalCachesPrepared=false;
		incrementalRoutePreparation=0;
		incrementalIntentIndex=0;
		incrementalCandidates.clear();
		incrementalBuildOriginCursor=0;
		incrementalHasStrict=false;
		incrementalHasFallback=false;
		incrementalStrictCount=0;
		incrementalFallbackCount=0;
		incrementalSelectionActive=true;
		// World collection and snapshot copying are enough work for this update.
		// Cache construction gets the next update, and origin scans start after it.
		return SelectionPending;
	}
	if(!incrementalCachesPrepared)
	{
		prepareScoringCaches(incrementalWorld);
		prepareRetrySignature(incrementalWorld);
		if(incrementalLimits.activeNewConstruction
		   <incrementalLimits.newConstruction)
			prepareWaterDistanceCache(incrementalWorld);
		else
			incrementalIntentIndex=incrementalIntents.size();
		incrementalCachesPrepared=true;
		return SelectionPending;
	}
	if(incrementalHasNetwork
	   &&(routeCacheSignature!=incrementalSignature
	      ||int(routeDistanceCache[0].size())
	         !=incrementalWorld.width*incrementalWorld.height)
	   &&incrementalRoutePreparation<2)
	{
		// Horizontal and vertical two-tile arteries are independent shortest-path
		// fields. Build one per update so a new road network cannot monopolize a
		// frame; routeArtery will consume the identical cached distances later.
		prepareRouteCache(incrementalWorld,incrementalRoutePreparation++);
		if(incrementalRoutePreparation==2)
			routeCacheSignature=incrementalSignature;
		return SelectionPending;
	}

	size_t remaining=size_t(std::max(1,originBudget));
	while(incrementalIntentIndex<incrementalIntents.size()&&remaining)
	{
		const size_t before=incrementalBuildOriginCursor;
		std::vector<Candidate> winners;
		const bool complete=addBuildCandidatesRange(incrementalWorld,
			incrementalIntents[incrementalIntentIndex],incrementalSignature,
			incrementalHasNetwork,winners,incrementalBuildOriginCursor,remaining,
			incrementalBestStrict,incrementalHasStrict,incrementalStrictCount,
			incrementalBestFallback,incrementalHasFallback,incrementalFallbackCount);
		const size_t consumed=incrementalBuildOriginCursor-before;
		remaining=consumed>=remaining?0:remaining-consumed;
		if(!complete)return SelectionPending;
		for(size_t candidate=0;candidate<winners.size();++candidate)
			incrementalCandidates.push_back(winners[candidate].action);
		++incrementalIntentIndex;
		incrementalBuildOriginCursor=0;
		incrementalBestStrict=DevelopmentAction();
		incrementalBestFallback=DevelopmentAction();
		incrementalHasStrict=false;
		incrementalHasFallback=false;
		incrementalStrictCount=0;
		incrementalFallbackCount=0;
	}
	if(incrementalIntentIndex<incrementalIntents.size())
		return SelectionPending;

	std::vector<Candidate> candidates;
	for(size_t candidate=0;candidate<incrementalCandidates.size();++candidate)
	{
		Candidate value;value.action=incrementalCandidates[candidate];
		candidates.push_back(value);
	}
	const size_t buildWinners=candidates.size();
	addUpgradeAndRepairCandidates(incrementalWorld,incrementalLimits,candidates);
	lastDiagnostics.candidateCount+=candidates.size()-buildWinners;
	if(candidates.empty())
	{
		if(incrementalLimits.activeNewConstruction<incrementalLimits.newConstruction)
			recordBlocked(incrementalIntents,incrementalSignature);
		// The caller still needs the captured world for diagnostics and final
		// revalidation after this method returns. Mark the scan complete but retain
		// its snapshot until the next selection replaces it.
		incrementalSelectionActive=false;
		incrementalCandidates.clear();return SelectionEmpty;
	}
	Candidate best=candidates[0];
	for(size_t i=1;i<candidates.size();++i)
		if(candidateBetter(candidates[i],best))best=candidates[i];
	if(best.action.utility.total<0)
	{
		lastDiagnostics.rejected[RejectedNegativeUtility]++;
		if(incrementalLimits.activeNewConstruction<incrementalLimits.newConstruction)
			recordBlocked(incrementalIntents,incrementalSignature);
		incrementalSelectionActive=false;
		incrementalCandidates.clear();return SelectionEmpty;
	}
	selected=best.action;selected.id=nextActionId++;
	selected.worldSignature=incrementalSignature;
	lastDiagnostics.waterTier=selected.fallbackWaterTier?5:6;
	lastDiagnostics.selectedActionId=selected.id;
	lastDiagnostics.selectedUtility=selected.utility;
	incrementalSelectionActive=false;
	incrementalCandidates.clear();return SelectionFound;
}

int Planner::committedBuildingCount(const WorldState& world,int buildingType) const
{
	int count=0;
	for(size_t i=0;i<world.buildings.size();++i)
		if(world.buildings[i].buildingType==buildingType)
			++count;
	for(std::map<int,DevelopmentAction>::const_iterator i=actionMap.begin();
		i!=actionMap.end();++i)
	{
		const DevelopmentAction& action=i->second;
		if((action.type!=BuildCampusMember&&action.type!=BuildStandalone)
		   ||action.buildingType!=buildingType||!activeState(action.state))
			continue;
		// Once the register observes the building, it is already represented in
		// world.buildings. Before then, its active planner action is the commitment.
		if(!world.building(action.buildingId))
			++count;
	}
	return count;
}

int Planner::activeBuildCount(int buildingType,DevelopmentPurpose purpose) const
{
	int count=0;
	for(std::map<int,DevelopmentAction>::const_iterator i=actionMap.begin();
		i!=actionMap.end();++i)
		if(i->second.buildingType==buildingType
		   &&i->second.purpose==purpose
		   &&(i->second.type==BuildCampusMember
		      ||i->second.type==BuildStandalone)
		   &&activeState(i->second.state))
			++count;
	return count;
}

void Planner::addReservationReferences(const Reservation& reservation)
{
	if(!reservation.footprintTiles.empty())
	{
		if(++footprintReferenceRevision==0)footprintReferenceRevision=1;
		footprintDistanceCacheSignature=0;
	}
	for(size_t i=0;i<reservation.footprintTiles.size();++i)
		if(reservation.footprintTiles[i]>=0&&reservation.footprintTiles[i]<int(footprintRefs.size()))
			++footprintRefs[reservation.footprintTiles[i]];
	for(size_t i=0;i<reservation.circulationTiles.size();++i)
		if(reservation.circulationTiles[i]>=0&&reservation.circulationTiles[i]<int(circulationRefs.size()))
		{
			if(circulationRefs[reservation.circulationTiles[i]]==0)
				++circulationReservedTileCount;
			++circulationRefs[reservation.circulationTiles[i]];
		}
}

bool Planner::revalidateSelection(const WorldState& world,
	const std::vector<DevelopmentIntent>& intents,
	const DevelopmentLimits& suppliedLimits, const DevelopmentAction& action,
	RejectionReason* rejected)
{
	DevelopmentLimits limits=suppliedLimits;
	const auto pending=actionMap.find(action.id);
	if(pending!=actionMap.end()&&pending->second.state==ParcelReserved
	   &&action.type==UpgradeBuilding)
	{
		// A waiting upgrade already owns one quota slot. Recheck its authority
		// against the other commitments, without counting it against itself.
		if(action.fromLevel==1)limits.activeLevel1Upgrades=std::max(0,limits.activeLevel1Upgrades-1);
		else if(action.fromLevel==2)limits.activeLevel2Upgrades=std::max(0,limits.activeLevel2Upgrades-1);
	}
	RejectionReason reason=RejectedAuthorization;
	bool authorized=false;
	const DevelopmentIntent* intent=NULL;
	int active=0;
	for(std::map<int,DevelopmentAction>::const_iterator i=actionMap.begin();
		i!=actionMap.end(); ++i)
		if(activeState(i->second.state)&&i->first!=action.id)++active;
	active=std::max(active,limits.activeNewConstruction
		+limits.activeLevel1Upgrades+limits.activeLevel2Upgrades);
	if(active<limits.newConstruction+limits.level1Upgrades+limits.level2Upgrades)
	{
		if(action.type==BuildCampusMember || action.type==BuildStandalone)
		{
			for(size_t i=0; i<intents.size(); ++i)
				if(intents[i].buildingType==action.buildingType
				   && intents[i].purpose==action.purpose && intents[i].unmetCount>0)
				{intent=&intents[i];break;}
			authorized=intent && limits.activeNewConstruction<limits.newConstruction;
		}
		else if(action.type==RepairBuilding)
			authorized=limits.allowRepairs;
		else if(action.type==UpgradeBuilding)
			authorized=limits.allowUpgrades
				&& limits.upgradePriority(action.buildingType,action.fromLevel)!=0
				&& ((action.fromLevel==1 && limits.activeLevel1Upgrades<limits.level1Upgrades)
					||(action.fromLevel==2 && limits.allowLevel2Upgrades
						&& limits.activeLevel2Upgrades<limits.level2Upgrades));
	}
	if(!authorized){if(rejected)*rejected=reason;return false;}
	if(intent)
	{
		if(!requiredSourcePresent(world,*intent))
		{if(rejected)*rejected=RejectedRequiredSource;return false;}
		if(intent->purpose==ColonySeed)
		{
			// These distance caches may still describe the selection snapshot.
			prepareScoringCaches(world);
			prepareColonyClaims(world,action.id);
			if(!colonyCandidatePasses(world,*intent,action,reason))
			{if(rejected)*rejected=reason;return false;}
		}
	}
	else
	{
		const WorldBuilding* building=world.building(action.buildingId);
		if(!building || building->buildingType!=action.buildingType
		   || building->level!=action.fromLevel)
		{if(rejected)*rejected=RejectedUpgradeContract;return false;}
	}
	// An authorized upgrade may first need its reserved expansion cleared.
	// The separate before-issue check still requires an empty engine footprint.
	return revalidate(world,action,rejected,action.type!=UpgradeBuilding);
}

bool Planner::reserve(const WorldState& world, DevelopmentAction& action)
{
	ensureMaskSize(world.width*world.height);RejectionReason reason;
	if(!revalidate(world,action,&reason))
	{action.state=action.type==UpgradeBuilding?UpgradeBlocked:InvalidatedBeforeIssue;
	 actionMap[action.id]=action;
	 if(action.type==BuildCampusMember||action.type==BuildStandalone)
		blockedIntentSignatures[std::make_pair(action.buildingType,
			int(action.purpose))]=stateSignature(world);
	 lastDiagnostics.rejected[reason]++;return false;}
	Reservation reservation;reservation.id=nextReservationId++;reservation.actionId=action.id;
	reservation.footprintTiles=action.parcelTiles;
	if(action.type==UpgradeBuilding)
	{
		const BuildingLevelProfile* target=world.profile(action.buildingType)
			->atLevel(action.targetLevel);
		reservation.footprintTiles=footprintTiles(world,action.centerX,
			action.centerY,target->footprint);
	}
	reservation.circulationTiles=action.accessTiles;
	reservation.circulationTiles.insert(reservation.circulationTiles.end(),
		action.arteryTiles.begin(),action.arteryTiles.end());sortUnique(reservation.circulationTiles);
	if(action.type==BuildCampusMember&&action.campusId<0)
	{
		const DevelopmentTemplate* t=findTemplate(action.templateId);if(!t)return false;
		Campus campus;campus.id=nextCampusId++;campus.templateId=t->id;
		campus.originX=world.normalizeX(action.centerX-t->slots[action.slotId].centerX);
		campus.originY=world.normalizeY(action.centerY-t->slots[action.slotId].centerY);
		campus.slots.assign(t->slots.size(),CampusSlotState());campus.fallbackWaterTier=action.fallbackWaterTier;
		action.campusId=campus.id;campus.slots[action.slotId].actionId=action.id;
		campusList.push_back(campus);reservation.campusId=campus.id;reservation.permanent=true;
	}
	else if(action.type==BuildCampusMember)
	{
		const int c=findCampusIndex(action.campusId);if(c<0)return false;
		campusList[c].slots[action.slotId].actionId=action.id;
		// The original campus reservation already owns parcel and ring.
		reservation.footprintTiles.clear();reservation.circulationTiles.clear();
		reservation.campusId=action.campusId;reservation.permanent=false;
	}
	else if(action.type==BuildStandalone) reservation.permanent=true;
	action.reservationId=reservation.id;action.state=ParcelReserved;
	reservationMap[reservation.id]=reservation;addReservationReferences(reservation);
	actionMap[action.id]=action;lastDiagnostics.reservationId=reservation.id;return true;
}

bool Planner::revalidate(const WorldState& world,const DevelopmentAction& action,
	RejectionReason* rejected,bool beforeIssue) const
{
	RejectionReason reason=RejectedTerrain;
	if(action.type==UpgradeBuilding||action.type==RepairBuilding)
	{
		const WorldBuilding* b=world.building(action.buildingId);
		if(!b||b->site||b->upgrading){reason=RejectedUpgradeContract;if(rejected)*rejected=reason;return false;}
		if(action.type==UpgradeBuilding&&(b->hp<b->hpMax
		   ||contractMaximumLevel(b->id,b->buildingType)<action.targetLevel))
		{reason=RejectedUpgradeContract;if(rejected)*rejected=reason;return false;}
		if(action.type==UpgradeBuilding)
		{
			const BuildingProfile* p=world.profile(b->buildingType);
			const BuildingLevelProfile* current=p?p->atLevel(b->level):NULL;
			const BuildingLevelProfile* target=p?p->atLevel(action.targetLevel):NULL;
			const int reservationId=contractReservationId(b->id,b->buildingType);
			std::map<int,Reservation>::const_iterator reservation=reservationMap.find(reservationId);
			if(!current||!target||reservation==reservationMap.end())
			{reason=RejectedUpgradeContract;if(rejected)*rejected=reason;return false;}
			std::vector<int> own=footprintTiles(world,b->centerX,b->centerY,current->footprint);
			std::vector<int> promised=footprintTiles(world,b->centerX,b->centerY,target->footprint);
			for(size_t i=0;i<promised.size();++i)
			{
				const WorldTile& tile=world.tiles[promised[i]];
				if(!contains(reservation->second.footprintTiles,promised[i])
				   ||(tile.occupied&&!contains(own,promised[i]))
				   ||!tile.discovered||!tile.grass||tile.permanentResource)
				{reason=RejectedUpgradeContract;if(rejected)*rejected=reason;return false;}
				if(tile.gateCorridor&&!contains(own,promised[i]))
				{if(rejected)*rejected=RejectedCirculation;return false;}
				if(tile.clearableResource&&beforeIssue)
				{if(rejected)*rejected=RejectedClearableResource;return false;}
			}
		}
		if(action.type==RepairBuilding&&b->hp>=b->hpMax)
		{reason=RejectedUpgradeContract;if(rejected)*rejected=reason;return false;}
		return true;
	}
	// Reserved sites may wait for clearing. Recheck new-food ownership when
	// they finally issue too, not only when the original candidate was selected.
	if(beforeIssue && action.purpose==ColonySeed)
	{
		prepareColonyClaims(world,action.id);
		DevelopmentIntent colony;colony.purpose=ColonySeed;
		if(!colonyCandidatePasses(world,colony,action,reason))
		{if(rejected)*rejected=reason;return false;}
	}
	const std::vector<int> initial=footprintTiles(world,action.centerX,action.centerY,
		action.initialFootprint);
	const bool fillsExistingCampus=action.type==BuildCampusMember
		&&action.campusId>=0;
	if(beforeIssue&&action.requiresSwimmingBuilders&&world.swimmingBuilders<action.workers)
	{reason=RejectedIslandBuilders;if(rejected)*rejected=reason;return false;}
	bool valid=true;
	for(size_t i=0;i<initial.size();++i)
	{
		const WorldTile& t=world.tiles[initial[i]];
		if(!t.discovered){reason=RejectedUndiscovered;valid=false;break;}
		if(!t.grass){reason=RejectedTerrain;valid=false;break;}
		if(t.occupied){reason=RejectedBuilding;valid=false;break;}
		if(t.gateCorridor){reason=RejectedCirculation;valid=false;break;}
		if(t.permanentResource){reason=RejectedPermanentResource;valid=false;break;}
		if(t.clearableResource&&beforeIssue){reason=RejectedClearableResource;valid=false;break;}
		bool conflict=false;
		for(std::map<int,Reservation>::const_iterator r=reservationMap.begin();r!=reservationMap.end();++r)
			if(r->first!=action.reservationId
			   &&(action.campusId<0||r->second.campusId!=action.campusId)
			   &&contains(r->second.footprintTiles,initial[i])){conflict=true;break;}
		if(conflict){reason=RejectedReservation;valid=false;break;}
	}
	if(!valid){if(rejected)*rejected=reason;return false;}
	// The permanent reservation of an existing compact campus covers its whole
	// parcel and outer access ring.  Other completed campus members therefore
	// legitimately occupy parcel tiles when a later slot is issued.  Revalidate
	// the new member's own initial footprint above and the durable reservation
	// contract below; applying the new-campus whole-parcel test here caused the
	// same valid slot to be selected, rejected and retried every eight ticks.
	if(!fillsExistingCampus)
	{
		for(size_t i=0;i<action.parcelTiles.size();++i)
		{
			const WorldTile& t=world.tiles[action.parcelTiles[i]];
			if(t.gateCorridor){if(rejected)*rejected=RejectedCirculation;return false;}
			if(!t.discovered||!t.grass||t.occupied||t.permanentResource)
			{reason=t.occupied?RejectedBuilding:RejectedTerrain;if(rejected)*rejected=reason;return false;}
		}
		for(size_t i=0;i<action.accessTiles.size();++i)
		{
			const WorldTile& t=world.tiles[action.accessTiles[i]];
			if(!t.discovered||!t.grass||t.occupied||t.permanentResource)
			{reason=RejectedAccess;if(rejected)*rejected=reason;return false;}
		}
		for(size_t i=0;i<action.arteryTiles.size();++i)
		{
			const int index=action.arteryTiles[i];const WorldTile& tile=world.tiles[index];
			if(!tile.discovered||!tile.grass||tile.occupied||tile.permanentResource
			   ||isFootprintReserved(index))
			{
				reason=RejectedCirculation;
				if(rejected)*rejected=reason;return false;
			}
		}
	}
	if(action.reservationId>=0)
	{
		std::map<int,Reservation>::const_iterator reservation=reservationMap.find(action.reservationId);
		if(reservation==reservationMap.end())
		{reason=RejectedReservation;if(rejected)*rejected=reason;return false;}
		// A campus-fill action uses the permanent campus reservation; its own
		// transient reservation is intentionally empty.
		const Reservation* promised=&reservation->second;
		if(action.campusId>=0&&promised->footprintTiles.empty())
			for(std::map<int,Reservation>::const_iterator r=reservationMap.begin();r!=reservationMap.end();++r)
				if(r->second.campusId==action.campusId&&r->second.permanent){promised=&r->second;break;}
		for(size_t i=0;i<action.parcelTiles.size();++i)
			if(!contains(promised->footprintTiles,action.parcelTiles[i]))
			{reason=RejectedReservation;if(rejected)*rejected=reason;return false;}
		for(size_t i=0;i<action.accessTiles.size();++i)
			if(!contains(promised->circulationTiles,action.accessTiles[i]))
			{reason=RejectedAccess;if(rejected)*rejected=reason;return false;}
	}
	return true;
}

void Planner::markIssued(int actionId,int buildingId,int tick)
{
	std::map<int,DevelopmentAction>::iterator action=actionMap.find(actionId);
	if(action==actionMap.end())return;
	action->second.buildingId=buildingId;action->second.issuedTick=tick;
	action->second.state=CreateIssued;
	std::map<int,Reservation>::iterator reservation=reservationMap.find(action->second.reservationId);
	if(reservation!=reservationMap.end())reservation->second.buildingId=buildingId;
}

void Planner::markInvalidated(int actionId,ActionLifecycleState state,uint32_t signature,
	int coordinateKey)
{
	std::map<int,DevelopmentAction>::iterator action=actionMap.find(actionId);
	if(action==actionMap.end())return;action->second.state=state;
	if(action->second.type==BuildCampusMember||action->second.type==BuildStandalone)
		blockedIntentSignatures[std::make_pair(action->second.buildingType,
			int(action->second.purpose))]=signature;
	if((state==EngineRejected||state==InvalidatedBeforeIssue)
	   &&coordinateKey>=0&&(action->second.type==BuildCampusMember
	   ||action->second.type==BuildStandalone))
		coordinateQuarantines[coordinateKey]=signature;
	const auto reservation=reservationMap.find(action->second.reservationId);
	// The first member owns the shared campus contract. Its failure cannot
	// release space still promised to another live or pending member.
	if(reservation!=reservationMap.end()
	   &&(reservation->second.campusId<0||!reservation->second.permanent))
		removeReservation(reservation->first);
	const int campusIndex=findCampusIndex(action->second.campusId);
	if(campusIndex>=0)
	{
		Campus& campus=campusList[campusIndex];
		if(action->second.slotId>=0&&action->second.slotId<int(campus.slots.size()))
			campus.slots[action->second.slotId].actionId=-1;
		bool occupied=false,pending=false;
		for(size_t slot=0;slot<campus.slots.size();++slot)
		{occupied=occupied||campus.slots[slot].buildingId>=0;
		 pending=pending||campus.slots[slot].actionId>=0;}
		if(!occupied&&!pending)
		{
			std::vector<int> reservations;
			for(std::map<int,Reservation>::const_iterator r=reservationMap.begin();
				r!=reservationMap.end();++r)
				if(r->second.campusId==campus.id)reservations.push_back(r->first);
			for(size_t r=0;r<reservations.size();++r)removeReservation(reservations[r]);
			campusList.erase(campusList.begin()+campusIndex);
		}
	}
}

void Planner::removeReservation(int id)
{
	std::map<int,Reservation>::iterator found=reservationMap.find(id);if(found==reservationMap.end())return;
	const Reservation& r=found->second;
	if(!r.footprintTiles.empty())
	{
		if(++footprintReferenceRevision==0)footprintReferenceRevision=1;
		footprintDistanceCacheSignature=0;
	}
	for(size_t i=0;i<r.footprintTiles.size();++i)if(footprintRefs[r.footprintTiles[i]])--footprintRefs[r.footprintTiles[i]];
	for(size_t i=0;i<r.circulationTiles.size();++i)
		if(circulationRefs[r.circulationTiles[i]])
		{
			--circulationRefs[r.circulationTiles[i]];
			if(circulationRefs[r.circulationTiles[i]]==0)
				--circulationReservedTileCount;
		}
	reservationMap.erase(found);
}

void Planner::adoptStartingBuildings(const WorldState& world)
{
	ensureMaskSize(world.width*world.height);
	std::vector<const WorldBuilding*> ordered;
	for(size_t b=0;b<world.buildings.size();++b)ordered.push_back(&world.buildings[b]);
	std::sort(ordered.begin(),ordered.end(),[this](const WorldBuilding* a,
		const WorldBuilding* b)
	{
		const bool aSwarm=a->buildingType==configuredSwarmType;
		const bool bSwarm=b->buildingType==configuredSwarmType;
		if(aSwarm!=bSwarm)return aSwarm;
		if(a->age!=b->age)return a->age>b->age;
		return a->id<b->id;
	});
	for(size_t index=0;index<ordered.size();++index)
	{
		const WorldBuilding& building=*ordered[index];
		const int existingIndex=findStandaloneIndex(building.id);
		if(building.site||building.upgrading
		   ||(existingIndex>=0&&!standaloneList[existingIndex].preexisting))continue;
		bool plannerOwned=false;
		for(size_t c=0;c<campusList.size()&&!plannerOwned;++c)
			for(size_t s=0;s<campusList[c].slots.size();++s)
				if(campusList[c].slots[s].buildingId==building.id)
				{plannerOwned=true;break;}
		for(std::map<int,DevelopmentAction>::const_iterator action=actionMap.begin();
			action!=actionMap.end()&&!plannerOwned;++action)
			if(action->second.buildingId==building.id)
				plannerOwned=true;
		if(plannerOwned&&existingIndex<0)continue;
		const BuildingProfile* profile=world.profile(building.buildingType);if(!profile)continue;
		if(existingIndex>=0&&standaloneList[existingIndex].maximumLevel>=profile->maximumLevel())continue;
		StandaloneContract contract;contract.buildingId=building.id;contract.buildingType=building.buildingType;
		contract.centerX=building.centerX;contract.centerY=building.centerY;contract.preexisting=true;
		contract.maximumLevel=building.level;
		if(existingIndex>=0)contract=standaloneList[existingIndex];
		const BuildingLevelProfile* currentLevel=profile->atLevel(building.level);
		if(!currentLevel)continue;
		std::vector<int> promised=footprintTiles(world,building.centerX,
			building.centerY,currentLevel->footprint);
		RejectionReason currentReason=RejectedTerrain;
		if(!legalTiles(world,promised,false,currentReason,building.id,contract.reservationId))
			continue;
		std::vector<int> access=ringTiles(world,promised);
		// Retain the highest legal intermediate level, and reconsider capped
		// preexisting contracts after discovery or an obstruction changes.
		const int previousMaximum=contract.maximumLevel;
		for(int level=profile->maximumLevel();level>previousMaximum;--level)
		{
			const BuildingLevelProfile* terminal=profile->atLevel(level);
			if(!terminal)continue;
			std::vector<int> footprint=footprintTiles(world,building.centerX,building.centerY,terminal->footprint);
			std::vector<int> terminalRing=ringTiles(world,footprint);
			RejectionReason reason=RejectedTerrain;
			bool ringLegal=true;
			for(size_t i=0;i<terminalRing.size();++i)
			{
				const WorldTile& tile=world.tiles[terminalRing[i]];
				const auto own=reservationMap.find(contract.reservationId);
				const bool ownFootprint=own!=reservationMap.end()
					&&contains(own->second.footprintTiles,terminalRing[i]);
				if(!tile.discovered||!tile.grass||tile.occupied||tile.permanentResource
				   ||(isFootprintReserved(terminalRing[i])
				      &&footprintRefs[terminalRing[i]]>unsigned(ownFootprint)))
				{ringLegal=false;break;}
			}
			if(legalTiles(world,footprint,false,reason,building.id,contract.reservationId)&&ringLegal)
			{contract.maximumLevel=level;promised=footprint;access=terminalRing;break;}
		}
		if(existingIndex>=0&&contract.maximumLevel==previousMaximum)continue;
		// Preexisting buildings are allowed to retain only the access that is
		// currently usable, but every retained tile is protected from new parcels.
		std::vector<int> usableAccess;
		for(size_t i=0;i<access.size();++i)
		{
			const WorldTile& tile=world.tiles[access[i]];
			if(tile.discovered&&tile.grass&&!tile.occupied&&!tile.permanentResource
			   &&!contains(promised,access[i])&&!isFootprintReserved(access[i]))
				usableAccess.push_back(access[i]);
		}
		Reservation reservation;reservation.id=existingIndex>=0
			?contract.reservationId:nextReservationId++;
		if(existingIndex>=0)removeReservation(reservation.id);
		reservation.buildingId=building.id;reservation.footprintTiles=promised;
		reservation.circulationTiles=usableAccess;reservation.permanent=true;
		contract.reservationId=reservation.id;reservationMap[reservation.id]=reservation;
		addReservationReferences(reservation);
		if(existingIndex>=0)standaloneList[existingIndex]=contract;
		else standaloneList.push_back(contract);
	}
}

void Planner::observe(const WorldState& world)

{
	observe(world,world.computeSignature());
}

void Planner::observe(const WorldState& world,uint32_t occupancySignature)
{
	ensureMaskSize(world.width*world.height);
	const uint32_t signature=stateSignature(occupancySignature);
	for(std::map<int,DevelopmentAction>::iterator i=actionMap.begin();i!=actionMap.end();++i)
	{
		DevelopmentAction& action=i->second;if(!activeState(action.state)||action.state==ParcelReserved)continue;
		const WorldBuilding* building=world.building(action.buildingId);
		if(action.type==BuildCampusMember||action.type==BuildStandalone)
		{
			if(building)
			{
				if(building->site){action.state=SiteObserved;continue;}
				action.state=Completed;
				if(action.type==BuildStandalone&&findStandaloneIndex(building->id)<0)
				{
					StandaloneContract c;c.buildingId=building->id;c.buildingType=building->buildingType;
					c.centerX=building->centerX;c.centerY=building->centerY;
					const BuildingProfile* p=world.profile(building->buildingType);
					c.maximumLevel=p?p->maximumLevel():building->level;c.reservationId=action.reservationId;
					standaloneList.push_back(c);
				}
				const int c=findCampusIndex(action.campusId);
				if(c>=0&&action.slotId>=0&&action.slotId<int(campusList[c].slots.size()))
				{CampusSlotState& s=campusList[c].slots[action.slotId];s.buildingId=building->id;s.actionId=-1;s.currentLevel=building->level;}
				if(action.type==BuildCampusMember)
				{
					std::map<int,Reservation>::const_iterator r=reservationMap.find(action.reservationId);
					if(r!=reservationMap.end()&&!r->second.permanent)removeReservation(action.reservationId);
				}
			}
			else if(action.state==SiteObserved)
			{
				action.state=DestroyedDuringConstruction;
				blockedIntentSignatures[std::make_pair(action.buildingType,
					int(action.purpose))]=signature;
				std::map<int,Reservation>::const_iterator reservation=
					reservationMap.find(action.reservationId);
				if(action.type==BuildStandalone
				   ||(reservation!=reservationMap.end()&&!reservation->second.permanent))
					removeReservation(action.reservationId);
			}
			else if(action.issuedTick>=0
				&&world.tick-action.issuedTick>placementPolicy.actionTimeoutTicks)
			{
				coordinateQuarantines[world.index(action.centerX,action.centerY)]=occupancySignature;
				markInvalidated(action.id,CreateTimedOut,signature);
			}
		}
		else
		{
			if(!building)
			{action.state=DestroyedDuringConstruction;removeReservation(action.reservationId);continue;}
			if(building->site||building->upgrading){action.state=SiteObserved;continue;}
			if(action.type==RepairBuilding&&building->hp>=building->hpMax)
			{action.state=Completed;removeReservation(action.reservationId);}
			else if(action.type==UpgradeBuilding&&building->level>=action.targetLevel)
			{action.state=Completed;removeReservation(action.reservationId);}
			else if(action.issuedTick>=0
				&&world.tick-action.issuedTick>placementPolicy.actionTimeoutTicks)
			{action.state=UpgradeBlocked;removeReservation(action.reservationId);}
		}
	}
	releaseDestroyedCampuses(world);
}

void Planner::releaseDestroyedCampuses(const WorldState& world)
{
	for(size_t c=0;c<campusList.size();)
	{
		Campus& campus=campusList[c];const DevelopmentTemplate* t=findTemplate(campus.templateId);
		bool member=false,pending=false;
		for(size_t s=0;s<campus.slots.size();++s)
		{
			CampusSlotState& slot=campus.slots[s];
			if(slot.buildingId>=0)
			{
				const WorldBuilding* b=world.building(slot.buildingId);
				if(b){member=true;slot.currentLevel=b->level;}else slot.buildingId=-1;
			}
			if(slot.actionId>=0)
			{
				std::map<int,DevelopmentAction>::const_iterator a=actionMap.find(slot.actionId);
				if(a!=actionMap.end()&&activeState(a->second.state))pending=true;else slot.actionId=-1;
			}
			if(!slot.unusable&&slot.buildingId<0&&slot.actionId<0&&t)
			{
				const std::vector<int> tiles=footprintTiles(world,
					campus.originX+t->slots[s].centerX,campus.originY+t->slots[s].centerY,
					t->slots[s].initialFootprint);
				for(size_t k=0;k<tiles.size();++k)if(world.tiles[tiles[k]].occupied)
				{slot.unusable=true;break;}
			}
		}
		if(member||pending){++c;continue;}
		std::vector<int> remove;
		for(std::map<int,Reservation>::const_iterator r=reservationMap.begin();r!=reservationMap.end();++r)
			if(r->second.campusId==campus.id)remove.push_back(r->first);
		for(size_t r=0;r<remove.size();++r)removeReservation(remove[r]);
		campusList.erase(campusList.begin()+c);
	}
	for(size_t i=0;i<standaloneList.size();)
	{
		if(world.building(standaloneList[i].buildingId)){++i;continue;}
		removeReservation(standaloneList[i].reservationId);standaloneList.erase(standaloneList.begin()+i);
	}
}

uint32_t Planner::blockedSignature(int type,DevelopmentPurpose purpose) const
{
	std::map<std::pair<int,int>,uint32_t>::const_iterator i=
		blockedIntentSignatures.find(std::make_pair(type,int(purpose)));
	return i==blockedIntentSignatures.end()?0:i->second;
}

template<class Archive> void Planner::executionState(Archive& a)
{
	a("retryInputSignature",retryInputSignature);
	a("incrementalSelectionActive",incrementalSelectionActive);
	a.index("incrementalIntentIndex",incrementalIntentIndex);
	a("incrementalWorld",incrementalWorld);
	a("incrementalIntents",incrementalIntents);
	a("incrementalLimits",incrementalLimits);
	a("incrementalSignature",incrementalSignature);
	a("incrementalOccupancySignature",incrementalOccupancySignature);
	a("incrementalHasNetwork",incrementalHasNetwork);
	a("incrementalCachesPrepared",incrementalCachesPrepared);
	a("incrementalRoutePreparation",incrementalRoutePreparation);
	a("incrementalCandidates",incrementalCandidates);
	a.index("incrementalBuildOriginCursor",incrementalBuildOriginCursor);
	a("incrementalBestStrict",incrementalBestStrict);
	a("incrementalBestFallback",incrementalBestFallback);
	a("incrementalHasStrict",incrementalHasStrict);
	a("incrementalHasFallback",incrementalHasFallback);
	a.index("incrementalStrictCount",incrementalStrictCount);
	a.index("incrementalFallbackCount",incrementalFallbackCount);
	a("lastDiagnostics",lastDiagnostics);
	a("circulationReservedTileCount",circulationReservedTileCount);
	a("routeCacheSignature",routeCacheSignature);
	a("routeDistanceCache",routeDistanceCache);
	a("routeParentCache",routeParentCache);
	a("waterMaskCache",waterMaskCache);
	a("waterDistanceCache",waterDistanceCache);
	a("footprintDistanceCache",footprintDistanceCache);
	a("footprintDistanceCacheSignature",footprintDistanceCacheSignature);
	a("resourceSourceCache",resourceSourceCache);
	a("resourceDistanceCache",resourceDistanceCache);
	a("resourceDistanceCacheValid",resourceDistanceCacheValid);
	a("maximumFarmCapacityCache",maximumFarmCapacityCache);
	a("maximumFoodOpportunityCache",maximumFoodOpportunityCache);
	a("foodOpportunitySourceCache",foodOpportunitySourceCache);
	a("foodHaloMaximumCache",foodHaloMaximumCache);
	a("foodHaloRadiusCache",foodHaloRadiusCache);
	a("threatProtectionSourceCache",threatProtectionSourceCache);
	a("threatPrefixCache",threatPrefixCache);
	a("protectionPrefixCache",protectionPrefixCache);
	a("buildingDistanceSourceCache",buildingDistanceSourceCache);
	a("completedBuildingDistanceCache",completedBuildingDistanceCache);
	a("criticalBuildingDistanceCache",criticalBuildingDistanceCache);
	a("towerBuildingDistanceCache",towerBuildingDistanceCache);
	a("completedBuildingCountCache",completedBuildingCountCache);
	a("scoringReservedGeneration",scoringReservedGeneration);
	a("scoringAffectedGeneration",scoringAffectedGeneration);
	a("scoringBlockedNeighbors",scoringBlockedNeighbors);
	a("scoringNeighborhoodCache",scoringNeighborhoodCache);
	a("scoringNeighborhoodWidth",scoringNeighborhoodWidth);
	a("scoringNeighborhoodHeight",scoringNeighborhoodHeight);
	a("scoringReservedScratch",scoringReservedScratch);
	a("scoringAffectedScratch",scoringAffectedScratch);
	a("scoringGeneration",scoringGeneration);
	a("footprintReferenceRevision",footprintReferenceRevision);
	a("colonyFoodClaims",colonyFoodClaims);
	a("colonyAnchors",colonyAnchors);
}

void Planner::saveExecutionState(GAGCore::OutputStream* stream) const
{
    stream->writeEnterSection("PlacementExecution95");
    AIMaximaContinuation::Writer archive(stream);
    const_cast<Planner*>(this)->executionState(archive);
    stream->writeLeaveSection();
}
void Planner::loadExecutionState(GAGCore::InputStream* stream)
{
    stream->readEnterSection("PlacementExecution95");
    AIMaximaContinuation::Reader archive(stream);
    executionState(archive);
    stream->readLeaveSection();
}

void Planner::save(GAGCore::OutputStream* stream) const
{
	stream->writeEnterSection("V3PlacementPlanner");
	stream->writeSint32(nextCampusId,"next_campus_id");stream->writeSint32(nextReservationId,"next_reservation_id");stream->writeSint32(nextActionId,"next_action_id");
	stream->writeEnterSection("campuses");stream->writeUint32(campusList.size(),"size");
	for(size_t i=0;i<campusList.size();++i){stream->writeEnterSection(i);const Campus& c=campusList[i];stream->writeSint32(c.id,"id");stream->writeSint32(c.templateId,"template_id");stream->writeSint32(c.originX,"origin_x");stream->writeSint32(c.originY,"origin_y");stream->writeUint8(c.fallbackWaterTier,"fallback");stream->writeUint32(c.slots.size(),"slot_size");for(size_t s=0;s<c.slots.size();++s){stream->writeEnterSection(s);stream->writeSint32(c.slots[s].buildingId,"building_id");stream->writeSint32(c.slots[s].actionId,"action_id");stream->writeSint32(c.slots[s].currentLevel,"level");stream->writeUint8(c.slots[s].unusable,"unusable");stream->writeLeaveSection();}stream->writeLeaveSection();}stream->writeLeaveSection();
	stream->writeEnterSection("standalone");stream->writeUint32(standaloneList.size(),"size");for(size_t i=0;i<standaloneList.size();++i){stream->writeEnterSection(i);const StandaloneContract& c=standaloneList[i];stream->writeSint32(c.buildingId,"building_id");stream->writeSint32(c.buildingType,"building_type");stream->writeSint32(c.centerX,"x");stream->writeSint32(c.centerY,"y");stream->writeSint32(c.maximumLevel,"maximum_level");stream->writeSint32(c.reservationId,"reservation_id");stream->writeUint8(c.preexisting,"preexisting");stream->writeLeaveSection();}stream->writeLeaveSection();
	stream->writeEnterSection("reservations");stream->writeUint32(reservationMap.size(),"size");size_t n=0;for(std::map<int,Reservation>::const_iterator i=reservationMap.begin();i!=reservationMap.end();++i,++n){stream->writeEnterSection(n);const Reservation& r=i->second;stream->writeSint32(r.id,"id");stream->writeSint32(r.campusId,"campus_id");stream->writeSint32(r.buildingId,"building_id");stream->writeSint32(r.actionId,"action_id");stream->writeUint8(r.permanent,"permanent");writeIntVector(stream,"footprint",r.footprintTiles);writeIntVector(stream,"circulation",r.circulationTiles);stream->writeLeaveSection();}stream->writeLeaveSection();
	stream->writeEnterSection("reference_masks");stream->writeUint32(footprintRefs.size(),"size");for(size_t i=0;i<footprintRefs.size();++i){stream->writeEnterSection(i);stream->writeUint16(footprintRefs[i],"footprint");stream->writeUint16(circulationRefs[i],"circulation");stream->writeLeaveSection();}stream->writeLeaveSection();
	stream->writeEnterSection("blocked");stream->writeUint32(blockedIntentSignatures.size(),"size");n=0;for(std::map<std::pair<int,int>,uint32_t>::const_iterator i=blockedIntentSignatures.begin();i!=blockedIntentSignatures.end();++i,++n){stream->writeEnterSection(n);stream->writeSint32(i->first.first,"type");stream->writeSint32(i->first.second,"purpose");stream->writeUint32(i->second,"signature");stream->writeLeaveSection();}stream->writeLeaveSection();
	stream->writeEnterSection("quarantines");stream->writeUint32(coordinateQuarantines.size(),"size");n=0;for(std::map<int,uint32_t>::const_iterator i=coordinateQuarantines.begin();i!=coordinateQuarantines.end();++i,++n){stream->writeEnterSection(n);stream->writeSint32(i->first,"coordinate");stream->writeUint32(i->second,"signature");stream->writeLeaveSection();}stream->writeLeaveSection();
	stream->writeEnterSection("actions");stream->writeUint32(actionMap.size(),"size");n=0;for(std::map<int,DevelopmentAction>::const_iterator i=actionMap.begin();i!=actionMap.end();++i,++n){stream->writeEnterSection(n);const DevelopmentAction& a=i->second;stream->writeSint32(a.id,"id");stream->writeSint32(a.type,"type");stream->writeSint32(a.purpose,"purpose");stream->writeSint32(a.state,"state");stream->writeSint32(a.templateId,"template_id");stream->writeSint32(a.campusId,"campus_id");stream->writeSint32(a.slotId,"slot_id");stream->writeSint32(a.buildingId,"building_id");stream->writeSint32(a.buildingType,"building_type");stream->writeSint32(a.fromLevel,"from_level");stream->writeSint32(a.targetLevel,"target_level");stream->writeSint32(a.centerX,"x");stream->writeSint32(a.centerY,"y");stream->writeSint32(a.workers,"workers");stream->writeUint8(a.fallbackWaterTier,"fallback");stream->writeUint8(a.requiresSwimmingBuilders,"requires_swimming_builders");stream->writeSint32(a.reservationId,"reservation_id");stream->writeSint32(a.issuedTick,"issued_tick");stream->writeUint32(a.worldSignature,"signature");stream->writeSint32(a.initialFootprint.left,"initial_left");stream->writeSint32(a.initialFootprint.top,"initial_top");stream->writeSint32(a.initialFootprint.width,"initial_width");stream->writeSint32(a.initialFootprint.height,"initial_height");stream->writeSint32(a.terminalFootprint.left,"terminal_left");stream->writeSint32(a.terminalFootprint.top,"terminal_top");stream->writeSint32(a.terminalFootprint.width,"terminal_width");stream->writeSint32(a.terminalFootprint.height,"terminal_height");writeIntVector(stream,"parcel",a.parcelTiles);writeIntVector(stream,"access",a.accessTiles);writeIntVector(stream,"artery",a.arteryTiles);stream->writeEnterSection("utility");writeUtility(stream,a.utility);stream->writeLeaveSection();stream->writeLeaveSection();}stream->writeLeaveSection();stream->writeLeaveSection();
}

bool Planner::load(GAGCore::InputStream* stream,int versionMinor)
{
	campusList.clear();standaloneList.clear();reservationMap.clear();actionMap.clear();blockedIntentSignatures.clear();coordinateQuarantines.clear();footprintRefs.clear();circulationRefs.clear();
	circulationReservedTileCount=0;
	routeCacheSignature=0;routeDistanceCache[0].clear();routeDistanceCache[1].clear();
	routeParentCache[0].clear();routeParentCache[1].clear();
	waterMaskCache.clear();waterDistanceCache.clear();
	footprintDistanceCache.clear();footprintDistanceCacheSignature=0;
	resourceSourceCache.clear();
	for(int resource=0;resource<8;++resource)
	{resourceDistanceCache[resource].clear();resourceDistanceCacheValid[resource]=false;}
	maximumFarmCapacityCache=1;maximumFoodOpportunityCache=1;
	scoringReservedGeneration.clear();scoringAffectedGeneration.clear();
	scoringGeneration=0;
	footprintReferenceRevision=1;
	stream->readEnterSection("V3PlacementPlanner");nextCampusId=stream->readSint32("next_campus_id");nextReservationId=stream->readSint32("next_reservation_id");nextActionId=stream->readSint32("next_action_id");uint32_t size;
	stream->readEnterSection("campuses");size=stream->readUint32("size");for(uint32_t i=0;i<size;++i){stream->readEnterSection(i);Campus c;c.id=stream->readSint32("id");c.templateId=static_cast<TemplateId>(stream->readSint32("template_id"));c.originX=stream->readSint32("origin_x");c.originY=stream->readSint32("origin_y");c.fallbackWaterTier=stream->readUint8("fallback");uint32_t slots=stream->readUint32("slot_size");c.slots.assign(slots,CampusSlotState());for(uint32_t s=0;s<slots;++s){stream->readEnterSection(s);c.slots[s].buildingId=stream->readSint32("building_id");c.slots[s].actionId=stream->readSint32("action_id");c.slots[s].currentLevel=stream->readSint32("level");c.slots[s].unusable=stream->readUint8("unusable");stream->readLeaveSection();}campusList.push_back(c);stream->readLeaveSection();}stream->readLeaveSection();
	stream->readEnterSection("standalone");size=stream->readUint32("size");for(uint32_t i=0;i<size;++i){stream->readEnterSection(i);StandaloneContract c;c.buildingId=stream->readSint32("building_id");c.buildingType=stream->readSint32("building_type");c.centerX=stream->readSint32("x");c.centerY=stream->readSint32("y");c.maximumLevel=stream->readSint32("maximum_level");c.reservationId=stream->readSint32("reservation_id");c.preexisting=stream->readUint8("preexisting");standaloneList.push_back(c);stream->readLeaveSection();}stream->readLeaveSection();
	stream->readEnterSection("reservations");size=stream->readUint32("size");for(uint32_t i=0;i<size;++i){stream->readEnterSection(i);Reservation r;r.id=stream->readSint32("id");r.campusId=stream->readSint32("campus_id");r.buildingId=stream->readSint32("building_id");r.actionId=stream->readSint32("action_id");r.permanent=stream->readUint8("permanent");readIntVector(stream,"footprint",r.footprintTiles);readIntVector(stream,"circulation",r.circulationTiles);reservationMap[r.id]=r;stream->readLeaveSection();}stream->readLeaveSection();
	stream->readEnterSection("reference_masks");size=stream->readUint32("size");footprintRefs.resize(size);circulationRefs.resize(size);for(uint32_t i=0;i<size;++i){stream->readEnterSection(i);footprintRefs[i]=stream->readUint16("footprint");circulationRefs[i]=stream->readUint16("circulation");if(circulationRefs[i])++circulationReservedTileCount;stream->readLeaveSection();}stream->readLeaveSection();
	stream->readEnterSection("blocked");size=stream->readUint32("size");for(uint32_t i=0;i<size;++i){stream->readEnterSection(i);int type=stream->readSint32("type");int purpose=versionMinor>=92?stream->readSint32("purpose"):int(CoreCapacity);blockedIntentSignatures[std::make_pair(type,purpose)]=stream->readUint32("signature");stream->readLeaveSection();}stream->readLeaveSection();
	stream->readEnterSection("quarantines");size=stream->readUint32("size");for(uint32_t i=0;i<size;++i){stream->readEnterSection(i);int coordinate=stream->readSint32("coordinate");coordinateQuarantines[coordinate]=stream->readUint32("signature");stream->readLeaveSection();}stream->readLeaveSection();
	stream->readEnterSection("actions");size=stream->readUint32("size");for(uint32_t i=0;i<size;++i){stream->readEnterSection(i);DevelopmentAction a;a.id=stream->readSint32("id");a.type=static_cast<DevelopmentActionType>(stream->readSint32("type"));a.purpose=versionMinor>=92?static_cast<DevelopmentPurpose>(stream->readSint32("purpose")):CoreCapacity;a.state=static_cast<ActionLifecycleState>(stream->readSint32("state"));a.templateId=static_cast<TemplateId>(stream->readSint32("template_id"));a.campusId=stream->readSint32("campus_id");a.slotId=stream->readSint32("slot_id");a.buildingId=stream->readSint32("building_id");a.buildingType=stream->readSint32("building_type");a.fromLevel=stream->readSint32("from_level");a.targetLevel=stream->readSint32("target_level");a.centerX=stream->readSint32("x");a.centerY=stream->readSint32("y");a.workers=stream->readSint32("workers");a.fallbackWaterTier=stream->readUint8("fallback");a.requiresSwimmingBuilders=stream->readUint8("requires_swimming_builders");a.reservationId=stream->readSint32("reservation_id");a.issuedTick=stream->readSint32("issued_tick");a.worldSignature=stream->readUint32("signature");a.initialFootprint.left=stream->readSint32("initial_left");a.initialFootprint.top=stream->readSint32("initial_top");a.initialFootprint.width=stream->readSint32("initial_width");a.initialFootprint.height=stream->readSint32("initial_height");a.terminalFootprint.left=stream->readSint32("terminal_left");a.terminalFootprint.top=stream->readSint32("terminal_top");a.terminalFootprint.width=stream->readSint32("terminal_width");a.terminalFootprint.height=stream->readSint32("terminal_height");readIntVector(stream,"parcel",a.parcelTiles);readIntVector(stream,"access",a.accessTiles);readIntVector(stream,"artery",a.arteryTiles);stream->readEnterSection("utility");readUtility(stream,a.utility,versionMinor);stream->readLeaveSection();actionMap[a.id]=a;stream->readLeaveSection();}stream->readLeaveSection();stream->readLeaveSection();return true;
}

const char* lifecycleName(ActionLifecycleState state)
{
	switch(state){case QueuedIntent:return "QueuedIntent";case ParcelReserved:return "ParcelReserved";case CreateIssued:return "CreateIssued";case SiteObserved:return "SiteObserved";case Completed:return "Completed";case InvalidatedBeforeIssue:return "InvalidatedBeforeIssue";case CreateTimedOut:return "CreateTimedOut";case DestroyedDuringConstruction:return "DestroyedDuringConstruction";case UpgradeBlocked:return "UpgradeBlocked";case RequiredSourceMissing:return "RequiredSourceMissing";case EngineRejected:return "EngineRejected";}return "Unknown";
}
const char* rejectionName(RejectionReason reason)
{
	static const char* names[RejectionReasonCount]={"undiscovered","terrain",
		"building","permanent_resource","clearable_resource","reservation",
		"circulation","water_tier","access","island_builders","quarantine",
		"upgrade_contract","required_source","colony_distance","colony_corn",
		"colony_threat","negative_utility","authorization"};
	return reason>=0&&reason<RejectionReasonCount?names[reason]:"unknown";
}

}
