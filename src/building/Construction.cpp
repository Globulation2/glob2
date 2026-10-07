// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#include "Material.h"
#include <list>
#include <math.h>
#include <stdlib.h>
#include <algorithm>
#include <limits>
#include <stdexcept>

#include "Building.h"
#include "BuildingType.h"
#include "EngineTiming.h"
#include "FixedPoint.h"
#include "Game.h"
#include "GlobalContainer.h"
#include "Team.h"
#include "Unit.h"
#include "Utilities.h"
#include "Order.h"

int Building::constructionMaterialNeed(int r) const
{
	return type->isBuildingSite ? std::max(0, constructionBudget[r]-constructionReserved[r]) : 0;
}

int Building::materialDeliveryTarget(int r) const
{
	return type->isBuildingSite && (constructionBudget[r] || type->semantics.constructionCost[r]) ? constructionBudget[r] : (runtime->replenishMaterialMask & (1u<<r)) ? type->maxMaterial[r] : 0;
}

int Building::materialDeliveryNeed(int r) const
{
	if (type->isBuildingSite && (constructionBudget[r] || type->semantics.constructionCost[r]))
	{
		const int uncommitted = constructionResultState == REPAIR ? 0 : availableMaterial(r);
		return std::max(0, constructionMaterialNeed(r)-uncommitted);
	}
	return (runtime->replenishMaterialMask & (1u<<r)) ? std::max(0, type->maxMaterial[r]-materials[r]) : 0;
}

void Building::fundConstructionFromInventory()
{
	if (!type->isBuildingSite || constructionResultState == REPAIR) return;
	BuildingMaterialCost newlyReserved{};
	for (int r=0; r<MaterialSlotCount; ++r)
		newlyReserved[r] = std::min(constructionMaterialNeed(r), availableMaterial(r));
	if (reserveMaterials(newlyReserved))
	{
		int funded=0;
		for (int r=0; r<MaterialSlotCount; ++r) { constructionReserved[r] += newlyReserved[r]; funded+=newlyReserved[r]; }
		applyConstructionHealth(funded);
	}
}

void Building::restoreConstructionReservations()
{
	if (!restoreMaterialsReservation(constructionReserved))
		throw std::runtime_error("Saved construction materials exceed available inventory");
}

void Building::cancelConstructionMaterials()
{
	if (constructionResultState == REPAIR)
	{
		consumeReservedMaterials(constructionReserved,-1);
		constructionReserved.fill(0);
	}
	else releaseConstructionReservations();
}

void Building::applyConstructionHealth(int funded, bool finishRepair)
{
	if (constructionResultState == REPAIR)
	{
		Sint64 total=0, paid=0;
		for (int r=0; r<MaterialSlotCount; ++r) { total+=constructionBudget[r]; paid+=constructionReserved[r]; }
		const int earned = !total || finishRepair ? repairInitialDeficit : Sint64(repairInitialDeficit)*paid/total;
		hp=std::min<Sint64>(getEffectiveMaxHp(),Sint64(hp)+std::max(0,earned-repairHealthGranted));
		repairHealthGranted=earned;
	}
	else if (constructionResultState == NEW_BUILDING || constructionResultState == UPGRADE)
		hp=std::min<Sint64>(getEffectiveMaxHp(),Sint64(hp)+Sint64(funded)*getEffectiveHpInc());
}

void Building::releaseConstructionReservations()
{
	releaseMaterials(constructionReserved);
	constructionReserved.fill(0);
}

bool Building::canTransferMaterialsTo(const BuildingType* destination) const
{
	if (!type->useTeamMaterials && destination->useTeamMaterials)
		for (int r=0; r<MaterialSlotCount; ++r)
			if (Sint64(owner->teamMaterials[r])+localMaterials[r]>std::numeric_limits<Sint32>::max()) return false;
	return true;
}

void Building::transferMaterialsPointer(bool wasShared)
{
	// Team stock belongs to the team after a building stops using it. Local
	// stock entering a shared pool is transferred once, without a hidden copy.
	if (!wasShared && type->useTeamMaterials)
		for (int r=0; r<MaterialSlotCount; ++r)
		{
			assert(Sint64(owner->teamMaterials[r])+localMaterials[r]<=std::numeric_limits<Sint32>::max());
			owner->teamMaterials[r] += localMaterials[r];
			localMaterials[r] = 0;
			owner->map->dirtyMarketGradientsSlot(owner->teamNumber,r);
		}
	updateMaterialsPointer();
}

bool Building::isMaterialFull(void)
{
	for (int i=0; i<MaterialSlotCount; i++)
	{
		if (materialDeliveryNeed(i)>0)
			return false;
	}
	return true;
}

int Building::neededMaterial(void)
{
	Sint32 minProportion = MIN_PROPORTION_INIT;
	int minType = MATERIAL_TYPE_NONE;
	int demanded[MaterialCount], count=0;
	for (unsigned material=0; material<MaterialCount; ++material)
		if (materialDeliveryTarget(material)>0) demanded[count++]=material;
	if (!count) return minType;
	const int first=syncRand()%count;
	for (int offset=0; offset<count; ++offset)
	{
		const int i=demanded[(first+offset)%count];
		const int target=materialDeliveryTarget(i);
		const Sint32 proportion=(Sint64(target-materialDeliveryNeed(i))<<FIXED_POINT_SHIFT_16)/target;
		if (proportion<minProportion) { minProportion=proportion; minType=i; }
	}
	return minType;
}

void Building::neededMaterials(int needs[MaterialSlotCount])
{
	for (int ri=0; ri<MaterialSlotCount; ri++)
		needs[ri]=Building::neededMaterial(ri);
}

void Building::computeWishedMaterials(int needs[MaterialSlotCount])
{
	 // we balance the system with Units working on it:
	for (int ri = 0; ri < MaterialSlotCount; ri++)
	{
		const int missing=materialDeliveryNeed(ri);
		const int deliveries=(missing+type->materialMultiplier[ri]-1)/type->materialMultiplier[ri];
		needs[ri]=(WISHED_MATERIAL_NUM*deliveries)/WISHED_MATERIAL_DEN;
	}
	for (std::list<Unit *>::iterator ui = unitsWorking.begin(); ui != unitsWorking.end(); ++ui)
		if ((*ui)->destinationPurpose >= 0)
		{
			assert((*ui)->destinationPurpose < MaterialSlotCount);
			needs[(*ui)->destinationPurpose]--;
		}
}

int Building::neededMaterial(int r)
{
	assert(r >= 0);
	return materialDeliveryNeed(r);
}


int Building::totalWishedMaterial()
{
	int sum=0;
	for (int ri = 0; ri < MaterialSlotCount; ri++)
		sum += wishedMaterials[ri];
	return sum;
}



int Building::getConstructionCompletionTypeNum() const
{
	if (constructionResultState == REPAIR && constructionOriginTypeNum >= 0) return constructionOriginTypeNum;
	return type->isBuildingSite ? type->nextLevel : typeNum;
}

bool Building::launchConstruction(Sint32 unitWorking, Sint32 unitWorkingFuture)
{
	if ((buildingState==ALIVE) && (!type->isBuildingSite))
	{
		const int target = hp < getEffectiveMaxHp() ? type->prevLevel : type->nextLevel;
		if (target < 0) return false;
		const BuildingType* site = owner->game->buildingsTypes.get(target);
		const BuildingType* completed = hp < getEffectiveMaxHp() ? type
			: site->isBuildingSite ? owner->game->buildingsTypes.get(site->nextLevel) : site;
		if (unitWorking < 0 || unitWorking > site->semantics.assignmentLimit || unitWorkingFuture < 0
			|| unitWorkingFuture > completed->semantics.assignmentLimit) return false;
		if (hp<getEffectiveMaxHp())
		{
			if (!type->semantics.repairable || (type->prevLevel==BUILDING_LEVEL_NONE) || !isHardSpaceForBuildingSite(REPAIR))
				return false;
			constructionResultState=REPAIR;
		}
		else
		{
			// Enforce the rule here as well as in order validation: local callers
			// may reach this boundary directly. The damaged-building branch above
			// remains a repair, which does not grant an unavailable building level.
			if (owner->game->gameHeader.isUnitUpgradesDisabled()) return false;
			if (!isUpgradeAvailable() || !isHardSpaceForBuildingSite(UPGRADE))
				return false;
			constructionResultState=UPGRADE;
		}

		cancelProduction();
		constructionOriginTypeNum = typeNum;
		std::copy_n(ratio,NB_UNIT_TYPE,constructionOriginRatios.begin());
		owner->removeFromAbilitiesLists(this);

		// We remove all units who are going to the building:
		// Notice that the algorithm is not fast but clean.
		std::list<Unit *> unitsToRemove;
		for (std::list<Unit *>::iterator it=unitsInside.begin(); it!=unitsInside.end(); ++it)
		{
			Unit *u=*it;
			assert(u);
			int d=u->displacement;
			if ((d!=Unit::DIS_INSIDE)&&(d!=Unit::DIS_ENTERING_BUILDING)&&(d!=Unit::DIS_EXITING_BUILDING))
			{
				u->standardRandomActivity();
				unitsToRemove.push_front(u);
			}
		}

		for (std::list<Unit *>::iterator it=unitsToRemove.begin(); it!=unitsToRemove.end(); ++it)
		{
			Unit *u=*it;
			assert(u);
			releaseService(u);
			unitsInside.remove(u);
		}

		maxUnitWorkingPrevious = maxUnitWorking;
		buildingState=WAITING_FOR_CONSTRUCTION;
		if (type->runtimeSuppliesStock || type->runtimeSuppliesDirectStock)
			for (int r=0; r<MaterialSlotCount; ++r) owner->map->dirtyMarketGradientsSlot(owner->teamNumber, r);
		maxUnitWorking=0;
		maxUnitInside=0;
		updateCallLists();
		updateUnitsWorking(); // To remove all units working.
		updateUnitsHarvesting(); // To remove all units working.
		//following reassigns units to work on upgrade, certain buildings will
		//glitch if units are not unassigned and then reassigned like this
		maxUnitWorking = unitWorking;
		maxUnitWorkingPreferred = maxUnitWorking;
		maxUnitWorkingFuture = unitWorkingFuture;
		updateConstructionState(); // To switch to a real building site, if all units have been freed from building.
		return true;
	}
	return false;
}

void Building::cancelConstruction(Sint32 unitWorking)
{
	Sint32 recoverTypeNum=typeNum;
	BuildingType *recoverType=type;

	if (type->isBuildingSite)
	{
		// A cancel order can name any site, e.g. a new building's: those have nothing
		// to return to. These were asserts; an order from the network must not stop
		// the game, and the sites the user interface cancels never reach them.
		if (buildingState!=ALIVE || !unitsInside.empty())
			return;
		int targetLevelTypeNum=getConstructionOriginTypeNum();

		if (targetLevelTypeNum!=BUILDING_LEVEL_NONE)
		{
			recoverTypeNum=targetLevelTypeNum;
			recoverType=owner->game->buildingsTypes.get(targetLevelTypeNum);
			if (!canTransferMaterialsTo(recoverType)) return;
		}
		else
			return;
	}
	else if (buildingState==WAITING_FOR_CONSTRUCTION_ROOM)
	{
		if(constructionResultState == UPGRADE)
			removeForbiddenZoneFromUpgradeArea();

		owner->buildingsTryToBuildingSiteRoom.remove(this);
		buildingState=ALIVE;
	}
	else if (buildingState==WAITING_FOR_CONSTRUCTION)
	{
		buildingState=ALIVE;
	}
	else
	{
		// Congratulation, you have managed to click "cancel upgrade"
		// when the building upgrade" was already canceled.
		return;
	}

	const int recoverX=(posX-type->decLeft+recoverType->decLeft)&owner->map->getMaskW();
	const int recoverY=(posY-type->decTop+recoverType->decTop)&owner->map->getMaskH();
	if (recoverType->semantics.occupiesGround && !owner->map->isFreeForBuilding(recoverX,recoverY,recoverType->width,recoverType->height,gid)) return;
	const bool wasRepair=constructionResultState==REPAIR;
	cancelProduction();
	cancelConstructionMaterials();
	constructionBudget.fill(0);
	constructionOriginTypeNum=-1;
	repairInitialDeficit=repairHealthGranted=0;
	constructionResultState=NO_CONSTRUCTION;
	siteCompletionPending=false;
	const bool wasShared=type->useTeamMaterials;

	if (!type->isVirtual)
		owner->map->setBuilding(posX, posY, type->width, type->height, NOGBID);
	int midPosX=posX-type->decLeft;
	int midPosY=posY-type->decTop;
	owner->removeFromAbilitiesLists(this);
	owner->prestige-=type->prestige;
	const BuildingType* previousType=type;
	bindType(recoverTypeNum);
	transitionProductionPreferences(previousType,nullptr,true);
	constructionOriginRatios.fill(0);
	owner->prestige+=type->prestige;
	owner->addToStaticAbilitiesLists(this);

	//Update the materials pointer to the newly changed type
	transferMaterialsPointer(wasShared);

	posX=midPosX+type->decLeft;
	posY=midPosY+type->decTop;
	resetPathfindGradients();

	if (!type->isVirtual)
		owner->map->setBuilding(posX, posY, type->width, type->height, gid);

	maxUnitWorking=maxUnitWorkingPrevious;
	maxUnitInside=type->maxUnitInside;
	updateCallLists();
	updateUnitsWorking();
	// no unit harvesting at that point

	if (!wasRepair && hp>=getEffectiveInitHp())
		hp=getEffectiveInitHp();

	resetProduction();


	setMapDiscovered();
}

void Building::launchDelete(void)
{
	if (buildingState==ALIVE)
	{
		cancelProduction();
		buildingState=WAITING_FOR_DESTRUCTION;
		owner->stockSuppliers.remove(this);
		owner->directStockSuppliers.remove(this);
		if (type->runtimeSuppliesStock || type->runtimeSuppliesDirectStock) owner->map->invalidateSupplierLocations();
		if (type->runtimeSuppliesStock || type->runtimeSuppliesDirectStock)
			for (int r=0; r<MaterialSlotCount; ++r) owner->map->dirtyMarketGradientsSlot(owner->teamNumber, r);
		maxUnitWorkingPrevious = maxUnitWorking;
		maxUnitWorking=0;
		maxUnitInside=0;
		desiredMaxUnitWorking = 0;
		updateCallLists();
		updateUnitsWorking();
		updateUnitsHarvesting();
		owner->buildingsWaitingForDestruction.push_front(this);
	}
}

void Building::cancelDelete(void)
{
	if (buildingState!=WAITING_FOR_DESTRUCTION) return;
	buildingState=ALIVE;
	owner->addToStaticAbilitiesLists(this);
	if (type->runtimeSuppliesStock || type->runtimeSuppliesDirectStock)
		for (int resource=0; resource<MaterialCount; ++resource) owner->map->dirtyMarketGradientsSlot(owner->teamNumber,resource);
	maxUnitWorking=maxUnitWorkingPrevious;
	maxUnitInside=type->maxUnitInside;
	updateCallLists();
	updateUnitsWorking();
	// we do not update units harvesting because there is none at this point
	// we do not update owner->buildingsWaitingForDestruction because Team::syncStep will remove this building from the list
}


void Building::resetServiceListState()
{
	inCanFeedUnit=inCanHealUnit=LS_OUT;
	std::fill_n(inUpgrade,NB_ABILITY,LS_OUT);
}

void Building::updateCallLists(void)
{
	if (buildingState==DEAD)
		return;
	desiredMaxUnitWorking = desiredNumberOfWorkers();
	bool resourceFull=isMaterialFull();
	if (resourceFull && !(type->canExchange && owner->openMarket()))
	{
		// Then we don't need anyone more to fill me, if I'm still in the call list for units,
		// remove me
		if(callListState != 0)
		{
			owner->removeBuildingNeedingWork(this, oldPriority);
			callListState=0;
			oldPriority = priority;
		}
	}

	if (unitsWorking.size()<(unsigned)desiredMaxUnitWorking)
	{
		if (buildingState==ALIVE)
		{
			// I need units, if I am not in the call lists, add me
			if(callListState != 1)
			{
				owner->addBuildingNeedingWork(this, priority);
				callListState = 1;
				oldPriority = priority;
			}
			// I am in the call list. Re-register at current priority. This
			// handles both BH-230 (priority changed -> move to new bucket)
			// and the original same-priority re-sort, which is observable
			// because Team::updateAllBuildingTasks calls subscribe* on each
			// building in the bucket, and subscribe* -> updateCallLists
			// mutates the bucket mid-iteration.
			else
			{
				owner->removeBuildingNeedingWork(this, oldPriority);
				owner->addBuildingNeedingWork(this, priority);
				oldPriority = priority;
			}
		}
	}
	else
	{
		if(callListState != 0)
		{
			owner->removeBuildingNeedingWork(this, oldPriority);
			callListState=0;
			oldPriority = priority;
		}
	}

	if ((signed)unitsInside.size()<maxUnitInside)
	{
		// Add itself in the right "call-lists":
		for (int i=0; i<NB_ABILITY; i++)
			if (inUpgrade[i]!=LS_IN && type->upgrade[i])
			{
				owner->canUpgrade[i].push_front(this);
				inUpgrade[i]=LS_IN;
			}

		// this is for food handling
		if (type->canFeedUnit)
		{
			if (type->useTeamMaterials || canOfferService(nullptr, FEED))
			{
				if (inCanFeedUnit!=LS_IN)
				{
					owner->canFeedUnit.push_front(this);
					//A Building newly getting available to feed is locked to conversion for CANNOT_CONVERT_TIMER_INIT frames
					canNotConvertUnitTimer=CANNOT_CONVERT_TIMER_INIT;
					inCanFeedUnit=LS_IN;
				}
			}
			else
			{
				if (inCanFeedUnit!=LS_OUT)
				{
					owner->canFeedUnit.remove(this);
					inCanFeedUnit=LS_OUT;
				}
			}
		}

		// this is for Unit healing
		if (type->canHealUnit && inCanHealUnit!=LS_IN)
		{
			owner->canHealUnit.push_front(this);
			inCanHealUnit=LS_IN;
		}
	}
	else
	{
		// delete itself from all Call lists
		for (int i=0; i<NB_ABILITY; i++)
			if (inUpgrade[i]!=LS_OUT && type->upgrade[i])
			{
				owner->canUpgrade[i].remove(this);
				inUpgrade[i]=LS_OUT;
			}

		if (type->canFeedUnit && inCanFeedUnit!=LS_OUT)
		{
			owner->canFeedUnit.remove(this);
			inCanFeedUnit=LS_OUT;
		}
		if (type->canHealUnit && inCanHealUnit!=LS_OUT)
		{
			owner->canHealUnit.remove(this);
			inCanHealUnit=LS_OUT;
		}
	}
}

void Building::updateConstructionState(void)
{
	if (buildingState==DEAD)
		return;

	if ((buildingState==WAITING_FOR_CONSTRUCTION) || (buildingState==WAITING_FOR_CONSTRUCTION_ROOM))
	{
		if (!isHardSpaceForBuildingSite())
		{
			//this is semi-faulty code and needs to be fixed later
			//anytime a building is upgraded but unable to do so it reverts to
			//one worker working instead of previous value
			cancelConstruction(1);
		}
		else if ((unitsWorking.size()==0) && (unitsInside.size()==0))
		{
			if (buildingState!=WAITING_FOR_CONSTRUCTION_ROOM)
			{
				buildingState=WAITING_FOR_CONSTRUCTION_ROOM;
				owner->buildingsTryToBuildingSiteRoom.push_front(this);
				if(constructionResultState == UPGRADE)
					addForbiddenZoneToUpgradeArea();
				if (verbose)
					printf("bgid=%d, inserted in buildingsTryToBuildingSiteRoom\n", gid);
			}
		}
		else if (verbose)
			printf("bgid=%d, Building wait for upgrade, uws=%lu, uis=%lu.\n", gid, (unsigned long)unitsWorking.size(), (unsigned long)unitsInside.size());
	}
}
