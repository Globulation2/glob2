#ifndef AI_MAXIMA_PLACEMENT_CONTINUATION_H
#define AI_MAXIMA_PLACEMENT_CONTINUATION_H
#include "AIMaximaPlacement.h"
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
	a("constructionResources",value.constructionResources);
	a("serviceThroughput",value.serviceThroughput);
	a("durability",value.durability);
	a("capability",value.capability);
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
	a("grass",value.grass);
	a("water",value.water);
	a("sand",value.sand);
	a("permanentResource",value.permanentResource);
	a("clearableResource",value.clearableResource);
	a("occupied",value.occupied);
	a("ownOccupied",value.ownOccupied);
	a("gateCorridor",value.gateCorridor);
	a("gateDefense",value.gateDefense);
	a("resourceType",value.resourceType);
	a("resourceAmount",value.resourceAmount);
	a("fertility",value.fertility);
	a("farmCapacity",value.farmCapacity);
	a("foodOpportunity",value.foodOpportunity);
	a("threat",value.threat);
	a("protectedness",value.protectedness);
	a("conqueredOpportunity",value.conqueredOpportunity);
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
}

template<class A> void fields(A& a, WorldState& value)
{
	a("width",value.width);
	a("height",value.height);
	a("tick",value.tick);
	a("swimmingBuilders",value.swimmingBuilders);
	a("accessibleSupplies",value.accessibleSupplies);
	a("tiles",value.tiles);
	a("buildings",value.buildings);
	a("profiles",value.profiles);
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
