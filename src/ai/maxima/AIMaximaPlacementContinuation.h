#ifndef AI_MAXIMA_PLACEMENT_CONTINUATION_H
#define AI_MAXIMA_PLACEMENT_CONTINUATION_H
#include "AIMaximaPlacement.h"
#include "FileFormatVersions.h"
namespace AIMaximaPlacement
{
template<class A> void fields(A& a, Footprint& value)
{
	a("left",value.left);
	a("top",value.top);
	a("width",value.width);
	a("height",value.height);
}

template<class A> void fields(A& a, BuildingLevelProfile& value)
{
	a("level",value.level);
	a("engineType",value.engineType);
	a("footprint",value.footprint);
	if(a.version()>=FILE_FORMAT_VERSION_BUILDING_CATALOG) a("constructionResources",value.constructionResources);
	else { int old[5]{};for(int i=0;i<5;++i)old[i]=value.constructionResources[i];a("constructionResources",old);for(int i=0;i<5;++i)value.constructionResources[i]=old[i]; }
	a("serviceThroughput",value.serviceThroughput);
	a("durability",value.durability);
	a("capability",value.capability);
	if(a.version()>=FILE_FORMAT_VERSION_BUILDING_CATALOG) {
	 a("completedType",value.completedType);a("roles",value.roles);a("serviceRates",value.serviceRates);
	 a("productionUnitMask",value.productionUnitMask);a("productionRates",value.productionRates);
     a("operatingAssignmentLimit",value.operatingAssignmentLimit);a("initialCarriers",value.initialCarriers);a("productionDemandPercent",value.productionDemandPercent);
     a("productionTicks",value.productionRecipes.ticks);a("productionCosts",value.productionRecipes.costs);a("productionPacketSize",value.productionRecipes.packetSize);
     if(value.operatingAssignmentLimit<0 || value.operatingAssignmentLimit>1024 || value.initialCarriers < -1 || value.initialCarriers>1024 || value.productionDemandPercent<0 || value.productionDemandPercent>10000)throw std::runtime_error("Invalid saved production operating plan");
     for(int unit=0;unit<3;++unit) {
         if(value.productionRecipes.ticks[unit]<0 || value.productionRecipes.ticks[unit]>1000001)throw std::runtime_error("Invalid saved production clock");
         for(int r=0;r<8;++r)if(value.productionRecipes.costs[unit][r]<0 || value.productionRecipes.costs[unit][r]>1000000)throw std::runtime_error("Invalid saved production cost");
     }
     for(int r=0;r<8;++r)if(value.productionRecipes.packetSize[r]<=0 || value.productionRecipes.packetSize[r]>1000000)throw std::runtime_error("Invalid saved packet size");
	 a("operatingResources",value.operatingResources);
     a("feedingRate",value.feedingRate);a("feedingMask",value.feedingMask);a("feedingResources",value.feedingResources);a("productionResources",value.productionResources);a("independentResources",value.independentResources);a("foodRetirable",value.foodRetirable);a("seats",value.seats);a("assignmentLimit",value.assignmentLimit);
	 a("requiredWorkerLevel",value.requiredWorkerLevel);a("repairable",value.repairable);a("available",value.available);
     if(value.feedingRate<0 || value.feedingMask>7)throw std::runtime_error("Invalid saved feeding profile");
     for(int r=0;r<8;++r)if(value.independentResources[r]<0 || value.independentResources[r]>value.operatingResources[r] || value.feedingResources[r]<0 || value.feedingResources[r]>value.operatingResources[r] || value.productionResources[r]<0 || value.productionResources[r]>value.operatingResources[r])
         throw std::runtime_error("Invalid saved feeding resource component");
	}
}

template<class A> void fields(A& a, BuildingProfile& value)
{
	a("buildingType",value.buildingType);
	a("levels",value.levels);
}

template<class A> void fields(A& a, WorldTile& value)
{
	a("discovered",value.discovered);
	a("foodTraversable",value.foodTraversable);
	a("grass",value.buildable);
	a("water",value.swimmable);
	a("sand",value.growthInhibiting);
	a("permanentResource",value.permanentResource);
	a("clearableResource",value.clearableResource);
	a("occupied",value.occupied);
	a("ownOccupied",value.ownOccupied);
	// Preserve the signed 32-bit fields independently of compact storage.
	int32_t resourceType=value.resourceType, resourceAmount=value.resourceAmount;
	a("resourceType",resourceType); a("resourceAmount",resourceAmount);
	if(resourceType < INT16_MIN || resourceType > INT16_MAX || resourceAmount < 0 || resourceAmount > UINT8_MAX)
		throw std::runtime_error("Invalid compact world resource");
	value.resourceType=int16_t(resourceType); value.resourceAmount=uint8_t(resourceAmount);
	a("fertility",value.fertility);
	a("farmCapacity",value.farmCapacity);
	a("foodOpportunity",value.foodOpportunity);
	a("threat",value.threat);
	a("protectedness",value.protectedness);
	a("conqueredOpportunity",value.conqueredOpportunity);
	a("protectedYield",value.protectedYield);
	if(a.version()>=FILE_FORMAT_VERSION_TERRAIN_PROPERTIES)
	{
		a("walkable",value.walkable);
		a("fertilitySource",value.fertilitySource);
	}
	else
	{
		// Legacy water was both the swimming habitat and irrigation source.
		value.walkable=!value.swimmable;
		value.fertilitySource=value.swimmable;
	}
}

template<class A> void fields(A& a, WorldBuilding& value)
{
	a("id",value.id);
	a("gid",value.gid);
	a("buildingType",value.buildingType);
	a("level",value.level);
	a("centerX",value.centerX);
	a("centerY",value.centerY);
	a("hp",value.hp);
	a("hpMax",value.hpMax);
	a("age",value.age);
	a("site",value.site);
	a("upgrading",value.upgrading);
    if(a.version()>=FILE_FORMAT_VERSION_BUILDING_CATALOG) {
        a("plannedCarriers",value.plannedCarriers);a("productionRatios",value.productionRatios);
        if(value.plannedCarriers < -1 || value.plannedCarriers>1024)throw std::runtime_error("Invalid saved provider staffing");
        for(int ratio:value.productionRatios)if(ratio<0 || ratio>32767)throw std::runtime_error("Invalid saved production ratio");
    }
}

template<class A> void fields(A& a, FeedingColony& value)
{
    a("x",value.x);a("y",value.y);a("demand",value.demand);
    for(int unit=0;unit<3;++unit)if(value.demand[unit]<0)throw std::runtime_error("Invalid saved meal demand");
}

template<class A> void fields(A& a, WorldState& value)
{
	a("width",value.width);
	a("height",value.height);
	a("tick",value.tick);
	a("swimmingBuilders",value.swimmingBuilders);
	if(a.version()>=FILE_FORMAT_VERSION_BUILDING_CATALOG) a("accessibleSupplies",value.accessibleSupplies);
	else { int old[5]{};for(int i=0;i<5;++i)old[i]=value.accessibleSupplies[i];a("accessibleSupplies",old);for(int i=0;i<5;++i)value.accessibleSupplies[i]=old[i]; }
	a("tiles",value.tiles);
	a("buildings",value.buildings);
	a("profiles",value.profiles);
    if(a.version()>=FILE_FORMAT_VERSION_BUILDING_CATALOG) {
        a("feedingColonies",value.feedingColonies);
        for(const auto& colony:value.feedingColonies)if(colony.x<0 || colony.x>=value.width || colony.y<0 || colony.y>=value.height)
            throw std::runtime_error("Invalid saved feeding colony");
    }
	value.invalidateProfileIndex();
}

template<class A> void fields(A& a, DevelopmentIntent& value)
{
	a("buildingType",value.buildingType);
	a("purpose",value.purpose);
	a("unmetCount",value.unmetCount);
	a("priority",value.priority);
	a("workers",value.workers);
	a("requiredResourceType",value.requiredResourceType);
	a("emergency",value.emergency);
	a("replacesBuildingId",value.replacesBuildingId);
}

template<class A> void fields(A& a, DevelopmentLimits& value)
{
	a("newConstruction",value.newConstruction);
	a("level1Upgrades",value.level1Upgrades);
	a("level2Upgrades",value.level2Upgrades);
	a("activeNewConstruction",value.activeNewConstruction);
	a("activeLevel1Upgrades",value.activeLevel1Upgrades);
	a("activeLevel2Upgrades",value.activeLevel2Upgrades);
	a("allowUpgrades",value.allowUpgrades);
	a("allowLevel2Upgrades",value.allowLevel2Upgrades);
	a("allowRepairs",value.allowRepairs);
	a("upgradePriorities",value.upgradePriorities);
}

template<class A> void fields(A& a, UtilityComponents& value)
{
	a("unmetDemand",value.unmetDemand);
	a("serviceGain",value.serviceGain);
	a("capabilityGain",value.capabilityGain);
	a("parallelismGain",value.parallelismGain);
	a("redundancyGain",value.redundancyGain);
	a("roleLocationQuality",value.roleLocationQuality);
	a("defendedness",value.defendedness);
	a("compactness",value.compactness);
	a("projectedFarmLoss",value.projectedFarmLoss);
	a("foodZonePressure",value.foodZonePressure);
	a("newlyReservedLand",value.newlyReservedLand);
	a("resourceScarcity",value.resourceScarcity);
	a("constructionLabor",value.constructionLabor);
	a("serviceDowntime",value.serviceDowntime);
	a("threatExposure",value.threatExposure);
	a("newArteryLength",value.newArteryLength);
	a("frontierGain",value.frontierGain);
	a("conqueredGain",value.conqueredGain);
	a("friendlyDistance",value.friendlyDistance);
	a("cornDistance",value.cornDistance);
	a("total",value.total);
}

template<class A> void fields(A& a, DevelopmentAction& value)
{
	a("id",value.id);
	a("type",value.type);
	a("purpose",value.purpose);
	a("state",value.state);
	a("templateId",value.templateId);
	a("campusId",value.campusId);
	a("slotId",value.slotId);
	a("buildingId",value.buildingId);
	a("buildingType",value.buildingType);
	a("fromLevel",value.fromLevel);
	a("targetLevel",value.targetLevel);
	a("centerX",value.centerX);
	a("centerY",value.centerY);
	a("workers",value.workers);
	a("fallbackWaterTier",value.fallbackWaterTier);
	a("requiresSwimmingBuilders",value.requiresSwimmingBuilders);
	a("reservationId",value.reservationId);
	a("issuedTick",value.issuedTick);
	a("worldSignature",value.worldSignature);
	a("initialFootprint",value.initialFootprint);
	a("terminalFootprint",value.terminalFootprint);
	a("parcelTiles",value.parcelTiles);
	a("accessTiles",value.accessTiles);
	a("arteryTiles",value.arteryTiles);
	a("utility",value.utility);
	a("replacesBuildingId",value.replacesBuildingId);
}

template<class A> void fields(A& a, PlacementDiagnostics& value)
{
	a("candidateCount",value.candidateCount);
	a("strictCandidateCount",value.strictCandidateCount);
	a("fallbackCandidateCount",value.fallbackCandidateCount);
	a("waterTier",value.waterTier);
	a("reservationId",value.reservationId);
	a("selectedActionId",value.selectedActionId);
	a("rejected",value.rejected);
	a("selectedUtility",value.selectedUtility);
}
}
#endif
