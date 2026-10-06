// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#include "Material.h"
#include <list>
#include <math.h>
#include <stdlib.h>
#include <algorithm>
#include <climits>

#include "Building.h"
#include "BuildingType.h"
#include "FixedPoint.h"
#include "Game.h"
#include "GlobalContainer.h"
#include "Team.h"
#include "Unit.h"
#include "Utilities.h"
#include "Order.h"


void Building::updateBuildingSite(void)
{
	assert(type->isBuildingSite);

	fundConstructionFromInventory();
	const bool resourceFull = constructionReserved == constructionBudget;
	const bool instantComplete = !resourceFull && owner->game->gameHeader.isInstantConstructionEnabled();
	if ((resourceFull || instantComplete || siteCompletionPending) && (buildingState!=WAITING_FOR_DESTRUCTION))
	{
		siteCompletionPending=true;
		cancelProduction();
		maxUnitInside=0;
		updateCallLists();
		// Complete existing visits using their original service definition before
		// replacing it. Reserved service budgets cannot become construction costs.
		if (!unitsInside.empty()) return;
		const int completedTypeNum=getConstructionCompletionTypeNum();
		BuildingType* completed=owner->game->buildingsTypes.get(completedTypeNum);
		if (!canTransferMaterialsTo(completed)) return;
		const int completedX=(posX-type->decLeft+completed->decLeft)&owner->map->getMaskW();
		const int completedY=(posY-type->decTop+completed->decTop)&owner->map->getMaskH();
		if (completed->semantics.occupiesGround && !owner->map->isFreeForBuilding(completedX,completedY,completed->width,completed->height,gid)) return;
		owner->removeFromAbilitiesLists(this);
		if (!type->isVirtual)
		{
			int kind = constructionResultState == REPAIR    ? GameplayMeasurements::REPAIRED
					   : constructionResultState == UPGRADE ? GameplayMeasurements::UPGRADED
															: GameplayMeasurements::NEW_BUILDING;
			auto& measurements=owner->stats.measurements;
			measurements.variants.resize(owner->game->buildingsTypes.size());
			++measurements.variants[completedTypeNum].completed[kind];
			if (completed->shortTypeNum>=0 && completed->shortTypeNum<IntBuildingType::NB_BUILDING && completed->level<NB_UNIT_LEVELS)
				++measurements.completed[kind][completed->shortTypeNum][completed->level];
		}
		const bool wasShared=type->useTeamResources;
		const bool wasRepair=constructionResultState==REPAIR;
		if (wasRepair) applyConstructionHealth(0,instantComplete);
		if (instantComplete) releaseConstructionReservations();
		else
		{
			consumeReservedMaterials(constructionReserved, constructionResultState == REPAIR ? -1 : constructionResultState == UPGRADE ? GameplayMeasurements::UPGRADE : GameplayMeasurements::CONSTRUCTION);
			constructionReserved.fill(0);
		}
		const bool zeroCost = constructionBudget == BuildingMaterialCost{};
		constructionBudget.fill(0);
		const BuildingType* originType=constructionOriginTypeNum>=0 ? owner->game->buildingsTypes.get(constructionOriginTypeNum) : nullptr;
		constructionOriginTypeNum=-1;
		repairInitialDeficit=repairHealthGranted=0;

		if (type->semantics.occupiesGround)
			owner->map->setBuilding(posX,posY,type->width,type->height,NOGBID);
		owner->prestige-=type->prestige;
		const BuildingType* previousType=type;
		bindType(completedTypeNum);
		transitionProductionPreferences(previousType,originType);
		constructionOriginRatios.fill(0);
		posX=completedX; posY=completedY;
		if (type->semantics.occupiesGround)
			owner->map->setBuilding(posX,posY,type->width,type->height,gid);
		siteCompletionPending=false;
		resetPathfindGradients();
		assert(constructionResultState!=NO_CONSTRUCTION);
		constructionResultState=NO_CONSTRUCTION;
		owner->prestige+=type->prestige;

		//Update the pointer resources to the newly changed type
		transferMaterialsPointer(wasShared);


		//now that building is complete clear the workers
		releaseAllWorkers();

		if (type->maxUnitWorking)
		{
			maxUnitWorking = maxUnitWorkingFuture;
			maxUnitWorkingFuture = 0;
		}
		else
			maxUnitWorking=0;

		// The working units still works for us, but
		// we don't have any unit in buildings
		assert(unitsInside.size()==0);
		maxUnitInside=type->maxUnitInside;

		// An instant completion skipped the deliveries that would have raised
		// hp (hpInc per resource for new/upgrade sites, a share of hpMax per
		// resource for repairs), so grant the finished level's full hpInit
		// (scaled by the fortress-buildings rule);
		// otherwise a new building would finish at the site's 1 HP and a
		// repair would finish no less damaged than it started.
		if (!wasRepair && (instantComplete || zeroCost || hp>=getEffectiveInitHp()))
			hp=getEffectiveInitHp();

		resetProduction();
		owner->addToStaticAbilitiesLists(this);

		setMapDiscovered();
		owner->pushGameEvent(GameEvent::buildingCompleted(owner->game->stepCounter, getMidX(), getMidY(), typeNum));

		// we need to do an update again
		updateCallLists();
		updateUnitsWorking();
		// no unit harvesting at that point
	}
}



void Building::updateUnitsWorking(void)
{
	if (maxUnitWorking==0)
	{
		// This is only a special optimization case:
		releaseAllWorkers();
	}
	else
	{
		while (unitsWorking.size()>(unsigned)desiredMaxUnitWorking)
		{
			int maxDistSquare=0;

			Unit *fu=NULL;
			std::list<Unit *>::iterator ittemp;

			// First choice: free a unit who has a not needed resource..
			for (std::list<Unit *>::iterator it=unitsWorking.begin(); it!=unitsWorking.end();)
			{
				int r=(*it)->carriedMaterial;
				if (r>=0 && !neededMaterial(r))
				{
					fu=(*it);
					fu->standardRandomActivity();
					it=unitsWorking.erase(it);
					continue;
				} else {
					++it;
				}
			}
			if(fu!=NULL) continue;
			// Second choice: free a unit who has no resource..
			if (fu==NULL)
			{
				int minDistSquare=INT_MAX;
				for (std::list<Unit *>::iterator it=unitsWorking.begin(); it!=unitsWorking.end(); ++it)
				{
					int r=(*it)->carriedMaterial;
					if (r<0)
					{
						int tx = posX;
						int ty = posY;
						if((*it)->targetX != -1)
						{
							tx = (*it)->targetX;
							ty = (*it)->targetY;
						}
						int newDistSquare=distSquare((*it)->posX, (*it)->posY, tx, ty);
						if (newDistSquare<minDistSquare)
						{
							minDistSquare=newDistSquare;
							fu=(*it);
							ittemp=it;
						}
					}
				}
			}

			// Third choice: free any unit..
			if (fu==NULL)
				for (std::list<Unit *>::iterator it=unitsWorking.begin(); it!=unitsWorking.end(); ++it)
				{
					int newDistSquare=distSquare((*it)->posX, (*it)->posY, posX, posY);
					if (newDistSquare>maxDistSquare)
					{
						maxDistSquare=newDistSquare;
						fu=(*it);
						ittemp=it;
					}
				}

			if (fu!=NULL)
			{
				if (verbose)
					printf("bgid=%d, we free the unit gid=%d\n", gid, fu->gid);
				// We free the unit.
				fu->standardRandomActivity();
				unitsWorking.erase(ittemp);
			}
			else
				break;
		}
	}
}

void Building::updateUnitsHarvesting(void)
{
	// if we are not alive or has not vision, remove all units harvesting from this building
	for (std::list<Unit *>::iterator it=unitsHarvesting.begin(); it!=unitsHarvesting.end();)
	{
		std::list<Unit *>::iterator tmpIt = it;
		Unit* u = *tmpIt;
		it++;
		
		// if the building is not available to fetch from (invisible or broken)
		if ((buildingState != ALIVE) || !type->runtimeSuppliesDirectStock || ((owner->sharedVisionExchange & u->owner->me) == 0))
		{
			// cancel the task u were just doing
		    u->attachedBuilding->removeUnitFromWorking(u);
		    // cancel fetching resources here
		    removeUnitFromHarvesting(u);
		    // behave randomly
		    u->standardRandomActivity();
			// TODO: replacing the remove by an erase should be a lot faster but
			// it causes the game to crash when a market gets destroyed. No idea
			// why. Actually there's no point bothering about this here as this
			// method is not performance critical but still it's weird to me
			// why it doesn't work the other way round.
			// unitsHarvesting.erase(tmpIt);
		}
	}
}

void Building::update(void)
{
	computeWishedMaterials(wishedMaterials);
	if (buildingState==DEAD)
		return;
	desiredMaxUnitWorking = desiredNumberOfWorkers();
	updateCallLists();
	updateUnitsWorking();
	updateUnitsHarvesting();
	updateConstructionState();
	if (type->isBuildingSite)
		updateBuildingSite();
}

void Building::setMapDiscovered(void)
{
	assert(type);
	int vr=type->viewingRange;
	if (type->semantics.sightSharing == BuildingSightSharing::Exchange)
		owner->map->setMapDiscovered(posX-vr, posY-vr, type->width+vr*2, type->height+vr*2, owner->sharedVisionExchange);
	else if (type->semantics.sightSharing == BuildingSightSharing::Food)
		owner->map->setMapDiscovered(posX-vr, posY-vr, type->width+vr*2, type->height+vr*2, owner->sharedVisionFood);
	else
		owner->map->setMapDiscovered(posX-vr, posY-vr, type->width+vr*2, type->height+vr*2, owner->sharedVisionOther);
	owner->map->setMapExploredByBuilding(posX-vr, posY-vr, type->width+vr*2, type->height+vr*2, owner->teamNumber);
}

void Building::getMaterialCountToRepair(int materials[MaterialCount])
{
	assert(!type->isBuildingSite);
	if (!type->semantics.repairable || type->prevLevel < 0)
	{
		std::fill(materials, materials + MaterialCount, 0);
		return;
	}
	int repairLevelTypeNum=type->prevLevel;
	BuildingType *repairBt=owner->game->buildingsTypes.get(repairLevelTypeNum);
	assert(repairBt);
	Sint64 fDestructionRatio=(Sint64(hp)<<FIXED_POINT_SHIFT_16)/getEffectiveMaxHp();
	Sint32 fTotErr=0;
	for (int i=0; i<MaterialCount; i++)
	{
		Sint64 fVal=fDestructionRatio*type->semantics.repairCost[i];
		int iVal=(fVal>>FIXED_POINT_SHIFT_16);
		fTotErr+=fVal&(int)FIXED_POINT_FRAC_MASK;
		if (fTotErr>=(int)FIXED_POINT_ONE)
		{
			fTotErr-=(int)FIXED_POINT_ONE;
			iVal++;
		}
		materials[i]=type->semantics.repairCost[i]-iVal;
	}
}

bool Building::tryToBuildingSiteRoom(void)
{
	int midPosX=posX-type->decLeft;
	int midPosY=posY-type->decTop;

	int targetLevelTypeNum=BUILDING_LEVEL_NONE;
	if (constructionResultState==UPGRADE)
		targetLevelTypeNum=type->nextLevel;
	else if (constructionResultState==REPAIR)
		targetLevelTypeNum=type->prevLevel;
	else
		assert(false);

	if (targetLevelTypeNum==BUILDING_LEVEL_NONE)
		return false;

	BuildingType *targetBt=owner->game->buildingsTypes.get(targetLevelTypeNum);
	if (!canTransferMaterialsTo(targetBt)) return false;
	int newPosX=midPosX+targetBt->decLeft;
	int newPosY=midPosY+targetBt->decTop;

	int newWidth=targetBt->width;
	int newHeight=targetBt->height;

	bool isRoom=!targetBt->semantics.occupiesGround || owner->map->isFreeForBuilding(newPosX, newPosY, newWidth, newHeight, gid);
	if (isRoom)
	{
		if(constructionResultState == UPGRADE)
			removeForbiddenZoneFromUpgradeArea();

		// Repair requires only the damaged fraction. Healthy-material credits
		// are neither inventory nor refundable construction materials.
		constructionBudget = targetBt->semantics.constructionCost;
		constructionReserved.fill(0);
		repairInitialDeficit=repairHealthGranted=0;
		if (constructionResultState==REPAIR)
		{
			repairInitialDeficit=std::max(0,getEffectiveMaxHp()-hp);
			const Sint64 ratio=(Sint64(hp)<<FIXED_POINT_SHIFT_16)/getEffectiveMaxHp();
			Sint32 remainder=0;
			for (int r=0; r<MAX_NB_RESOURCES; ++r)
			{
				const Sint64 value=ratio*type->semantics.repairCost[r];
				int healthy=value>>FIXED_POINT_SHIFT_16;
				remainder += value&FIXED_POINT_FRAC_MASK;
				if (remainder>=FIXED_POINT_ONE) { remainder-=FIXED_POINT_ONE; ++healthy; }
				constructionBudget[r]=type->semantics.repairCost[r]-healthy;
			}
		}
		const bool wasShared=type->useTeamResources;

		if (type->semantics.occupiesGround)
			owner->map->setBuilding(posX, posY, type->width, type->height, NOGBID);
		if (targetBt->semantics.occupiesGround)
			owner->map->setBuilding(newPosX, newPosY, newWidth, newHeight, gid);
		siteCompletionPending=false;


		owner->prestige-=type->prestige;
		const BuildingType* previousType=type;
		bindType(targetLevelTypeNum);
		transitionProductionPreferences(previousType);
		owner->prestige+=type->prestige;

		//Update the pointer resources to the newly changed type
		transferMaterialsPointer(wasShared);
		fundConstructionFromInventory();

		buildingState=ALIVE;
		owner->addToStaticAbilitiesLists(this);

		siteCompletionPending = constructionReserved == constructionBudget;

		// units
		if (verbose)
			printf("bgid=%d, uses maxUnitWorkingPreferred=%d\n", gid, maxUnitWorkingPreferred);
		maxUnitWorking=maxUnitWorkingPreferred;
		maxUnitInside=type->maxUnitInside;
		updateCallLists();
		updateUnitsWorking();
		// no unit harvesting at that point

		// position
		posX=newPosX;
		posY=newPosY;
		resetPathfindGradients();

		// flag useful :
		unitStayRange=type->defaultUnitStayRange;

		// quality parameters
		// hp=type->hpInit; // (Uint16)

		// preferred parameters
		resetProduction();

	}
	return isRoom;
}

/// Toggle (add or remove) the forbidden zone covering the footprint this building would
/// occupy if it completed its current upgrade. Disperses units so the building site
/// isn't waiting for space when there are lots of units around.
///
/// The post-mutation refresh of displayedForbiddenView is per-client display state
/// (not in Map::checkSum) — it runs only when this building's team is the locally-
/// displayed team, identified via Map::getDisplayedTeam() (mirrored from GameGUI's
/// localTeamNo). updateForbiddenGradient, by contrast, is sim state and runs
/// unconditionally for the owning team.
void Building::modifyForbiddenZoneForUpgradeArea(bool add)
{
	int midPosX=posX-type->decLeft;
	int midPosY=posY-type->decTop;

	BuildingType *targetBt=owner->game->buildingsTypes.get(type->nextLevel);
	int newPosX=midPosX+targetBt->decLeft;
	int newPosY=midPosY+targetBt->decTop;
	int newWidth=targetBt->width;
	int newHeight=targetBt->height;

	for(int x=newPosX; x<(newPosX+newWidth); ++x)
	{
		for(int y=newPosY; y<(newPosY+newHeight); ++y)
		{
			if (add)
				owner->map->addForbidden(x, y, owner->teamNumber);
			else
				owner->map->removeForbidden(x, y, owner->teamNumber);
		}
	}
	if(owner->teamNumber == owner->map->getDisplayedTeam())
		owner->map->computeDisplayedForbidden(owner->teamNumber);
	owner->map->updateForbiddenGradient(owner->teamNumber);
}

void Building::addForbiddenZoneToUpgradeArea(void)      { modifyForbiddenZoneForUpgradeArea(true); }
void Building::removeForbiddenZoneFromUpgradeArea(void) { modifyForbiddenZoneForUpgradeArea(false); }



bool Building::isHardSpaceForBuildingSite(void)
{
	return isHardSpaceForBuildingSite(constructionResultState);
}

bool Building::isHardSpaceForBuildingSite(ConstructionResultState requestedState)
{
	// Also informs extracted scene controls and AI feasibility checks.
	if (requestedState==UPGRADE && owner->game->gameHeader.isUnitUpgradesDisabled()) return false;
	if (requestedState==REPAIR && !type->semantics.repairable) return false;
	int futureBuildingTypeId=BUILDING_LEVEL_NONE;
	if (requestedState==UPGRADE)
		futureBuildingTypeId=type->nextLevel;
	else if (requestedState==REPAIR)
		futureBuildingTypeId=type->prevLevel;
	else
		assert(false);

	if (futureBuildingTypeId==BUILDING_LEVEL_NONE)
		return true;
	BuildingType *bt=owner->game->buildingsTypes.get(futureBuildingTypeId);
	int x=posX+bt->decLeft-type->decLeft;
	int y=posY+bt->decTop -type->decTop ;
	int w=bt->width;
	int h=bt->height;

	if (bt->isVirtual)
		return true;
	return owner->map->isHardSpaceForBuilding(x, y, w, h, gid);
}

bool Building::fullInside(void)
{
	if (type->canFeedUnit && !canOfferService(nullptr, FEED))
		return true;
	else
		return ((signed)unitsInside.size()>=maxUnitInside);
}


int Building::desiredNumberOfWorkers(void)
{
	//If It's virtual, then this building is a flag and always gets
	//full resources
	if(type->zonable[WORKER] || type->zonable[EXPLORER] || type->zonable[WARRIOR])
	{
		return std::min(maxUnitWorking, type->semantics.assignmentLimit);
	}
	//Otherwise, this building gets what the user desires, up to a limit of 2 units per 1 needed resource,
	//thus if no resources are needed, then no units will be working here.
	int neededResourcesSum = 0;
	for (size_t ri = 0; ri < MaterialCount; ri++)
	{
		int neededMaterials = (materialDeliveryNeed(ri) + type->materialMultiplier[ri] - 1) / type->materialMultiplier[ri];
		if (neededMaterials > 0)
			neededResourcesSum += neededMaterials;
	}
	int user_num = std::min(maxUnitWorking, type->semantics.assignmentLimit);
	int max_considering_resources = (WISHED_RESOURCE_NUM * neededResourcesSum) / WISHED_RESOURCE_DEN;
	return std::min(user_num, max_considering_resources);
}


