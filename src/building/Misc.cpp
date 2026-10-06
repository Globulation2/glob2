// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#include "Material.h"
#include <list>
#include <math.h>
#include <stdlib.h>
#include <algorithm>
#include <numeric>
#include <limits>

#include "Building.h"
#include <stdexcept>
#include "BuildingType.h"
#include "Game.h"
#include "Team.h"
#include "Unit.h"
#include "Order.h"
#include "Integrity.h"

// Custom-game "fortress buildings" rule: mirrors the getRealAttackStrength()/
// getRealArmor() unit pattern -- scale once, on read, at a single accessor,
// rather than touching every one of the many direct type->hpMax/hpInit
// reads across building logic, rendering and the GUI.
// A map's starting buildings predate the match's header, so their stored hp
// is scaled once by Game::applyStartingRules.
int Building::getEffectiveMaxHp(void) const
{
	const BuildingRuntimeTraits* healthType = constructionResultState == REPAIR && constructionOriginTypeNum >= 0
		? owner->game->buildingsTypes.getRuntime(constructionOriginTypeNum) : runtime;
	return healthType->hpMax * owner->game->gameHeader.getBuildingHpMultiplier();
}

int Building::getEffectiveInitHp(void) const
{
	return type->hpInit * owner->game->gameHeader.getBuildingHpMultiplier();
}

int Building::getEffectiveHpInc(void) const
{
	return type->hpInc * owner->game->gameHeader.getBuildingHpMultiplier();
}

void Building::releaseAllWorkers()
{
	for (std::list<Unit *>::iterator it=unitsWorking.begin(); it!=unitsWorking.end(); ++it)
	{
		assert(*it);
		(*it)->standardRandomActivity();
	}
	unitsWorking.clear();
}

void Building::kill(int diagnosticRemoval)
{
	if (buildingState==DEAD)
		return;
	const bool unfundedNewSite = type->isBuildingSite && constructionResultState == NEW_BUILDING
		&& std::none_of(constructionReserved.begin(), constructionReserved.end(), [](int amount) { return amount > 0; });
	cancelProduction();
	cancelConstructionMaterials();

	if (!type->isVirtual)
	{
		auto& measurements=owner->stats.measurements;
		measurements.variants.resize(owner->game->buildingsTypes.size());
		++measurements.variants[typeNum].removed[diagnosticRemoval];
		if (type->shortTypeNum>=0 && type->shortTypeNum<IntBuildingType::NB_BUILDING && getLongLevel()<NB_BUILDING_LONG_LEVELS)
			++measurements.removed[diagnosticRemoval][type->shortTypeNum][getLongLevel()];
	}
	// Units inside need the footprint freed before they can be placed.
	std::vector<Unit *> unitsToExpel;
	for (std::list<Unit *>::iterator it=unitsInside.begin(); it!=unitsInside.end(); ++it)
	{
		Unit *u=*it;
		if (u->displacement==Unit::DIS_INSIDE || u->displacement==Unit::DIS_EXITING_BUILDING)
			unitsToExpel.push_back(u);
		else if (u->displacement==Unit::DIS_ENTERING_BUILDING)
		{
			// An entering unit still owns the tile it came from: step back onto it.
			int x=(u->posX-u->dx)&owner->map->getMaskW();
			int y=(u->posY-u->dy)&owner->map->getMaskH();
			if (u->performance[FLY])
				owner->map->setAirUnit(x, y, NOGUID);
			else
				owner->map->setGroundUnit(x, y, NOGUID);
			u->expelFromBuilding(x, y, -u->dx, -u->dy);
		}
		else
			u->standardRandomActivity();
	}
	unitsInside.clear();

	releaseAllWorkers();

	maxUnitWorking=0;
	maxUnitInside=0;
	desiredMaxUnitWorking = 0;
	updateCallLists();

	// A building waiting for upgrade room has a forbidden zone stamped over
	// its would-be footprint (addForbiddenZoneToUpgradeArea). The other exits
	// from that state (tryToBuildingSiteRoom, cancelConstruction) remove it;
	// dying must too, or the zone leaks and corrupts the team's pathfinding.
	if (buildingState==WAITING_FOR_CONSTRUCTION_ROOM && constructionResultState==UPGRADE)
		removeForbiddenZoneFromUpgradeArea();

	if (!type->isVirtual)
	{
		owner->map->setBuilding(posX, posY, type->width, type->height, NOGBID);
		owner->dirtyGlobalGradient();
		owner->map->updateTeamAreaGradients(owner->teamNumber);
		if (unfundedNewSite)
			owner->noMoreBuildingSitesCountdown=Team::noMoreBuildingSitesCountdownMax;

	}

	for (std::vector<Unit *>::iterator it=unitsToExpel.begin(); it!=unitsToExpel.end(); ++it)
	{
		Unit *u=*it;
		int x, y, dx, dy;
		if (findExpelTile(u->performance[FLY], u->performance[SWIM], &x, &y, &dx, &dy))
			u->expelFromBuilding(x, y, dx, dy);
		else
		{
			if (!u->isDead)
				++u->owner->stats.measurements.deaths[u->typeNum][u->hp < UNIT_HP_DEATH_THRESHOLD ? u->diagnosticDeathCause : GameplayMeasurements::TRAPPED];
			u->isDead=true;
			u->standardRandomActivity();
		}
	}

	buildingState=DEAD;
	owner->stockSuppliers.remove(this);
	owner->directStockSuppliers.remove(this);
	if (type->runtimeSuppliesStock || type->runtimeSuppliesDirectStock) owner->map->invalidateSupplierLocations();
	if (type->runtimeSuppliesStock || type->runtimeSuppliesDirectStock)
		for (int r=0; r<MaterialSlotCount; r++)
			if (materials[r]>0)
				owner->map->dirtyMarketGradientsSlot(owner->teamNumber, r);
	
	updateUnitsHarvesting();
	
	owner->prestige-=type->prestige;

	owner->buildingsToBeDestroyed.push_front(this);
}


bool Building::fetchesFromMarkets() const
{
	return type->runtimeFetchesStock;
}

bool Building::isUpgradeAvailable() const
{
	return owner->game->isBuildingTypeAvailable(type->nextLevel);
}

bool Building::canUnitWorkHere(Unit* unit, bool attraction)
{
	if(attraction)
	{
		if(type->zonable[unit->typeNum])
		{
			if (unit->typeNum == WARRIOR)
			{
				int level=std::min(unit->level[ATTACK_SPEED], unit->level[ATTACK_STRENGTH]);
				if(minLevelToFlag<=level)
					return true;
			}
			else if (unit->typeNum == EXPLORER)
			{
				if(explorersRequireBombing && !unit->level[MAGIC_ATTACK_GROUND])
					return false;
				else
					return true;
			}
			else if (unit->typeNum == WORKER)
			{
				return unit->workerLevel() >= minWorkerLevelToFlag;
			}

		}
	}
	else if(unit->typeNum ==  WORKER)
	{
		if(runtime->requiredWorkerLevel <= unit->workerLevel())
			return true;
	}
	return false;

}



void Building::removeUnitFromWorking(Unit* unit)
{
	unitsWorking.remove(unit);
	updateCallLists();
}

void Building::insertUnitToHarvesting(Unit* unit)
{
	unitsHarvesting.push_front(unit);
}


void Building::removeUnitFromHarvesting(Unit* unit)
{
	unitsHarvesting.remove(unit);
}


void Building::removeUnitFromInside(Unit* unit)
{
	releaseService(unit);
	unitsInside.remove(unit);
	updateCallLists();
}



void Building::updateMaterialsPointer()
{
	if (type->runtimeSuppliesStock || type->runtimeSuppliesDirectStock)
		for (int r = 0; r < MaterialSlotCount; ++r)
			owner->map->dirtyMarketGradientsSlot(owner->teamNumber, r);
	if(!type->useTeamMaterials)
	{
		materials=localMaterials;
	}
	else
	{
		materials=owner->teamMaterials;
	}
}



void Building::addMaterialIntoBuilding(int resourceType)
{
	deliverMaterialPacket(resourceType,{});
}

MaterialDeliveryResult Building::deliverMaterialPacket(int resourceType, MaterialPacket packet)
{
	if (packet.denominator==0 || packet.denominator>1000000 || packet.numerator>packet.denominator)
		throw std::runtime_error("Invalid material packet");
	const Uint64 multiplier=type->materialMultiplier[resourceType];
	const Sint32 converted=Uint64(packet.numerator)*multiplier/packet.denominator;
	const int before = materials[resourceType];
	if ((type->runtimeSuppliesStock || type->runtimeSuppliesDirectStock || type->useTeamMaterials) && availableMaterial(resourceType)<=0)
		owner->map->dirtyMarketGradientsSlot(owner->teamNumber, resourceType);
	// A shared pool may already exceed this recipient's own acceptance limit.
	// Reject excess delivery without deleting inventory owned by other consumers.
	materials[resourceType] += std::min({materialDeliveryNeed(resourceType),converted,std::numeric_limits<Sint32>::max()-before});
	const int accepted = std::max(0, materials[resourceType] - before);
	int funded=0;
	if (type->isBuildingSite)
	{
		BuildingMaterialCost funding{};
		funding[resourceType]=std::min(accepted,constructionMaterialNeed(resourceType));
		if (reserveMaterials(funding)) { funded=funding[resourceType]; constructionReserved[resourceType]+=funded; }
	}
	owner->stats.measurements.delivered[resourceType] += accepted;
	if (type->canExchange)
		owner->stats.measurements.transferredIn[resourceType] += accepted;
	if (constructionResultState == REPAIR)
		owner->stats.measurements.repairDelivered[resourceType] += accepted;
	applyConstructionHealth(funded);
	MaterialDeliveryResult result; result.acceptedStock=accepted;
	result.discardedNumerator=Uint64(packet.numerator)*multiplier-Uint64(accepted)*packet.denominator;
	result.discardedDenominator=Uint64(packet.denominator)*multiplier;
	const auto divisor=std::gcd(result.discardedNumerator,result.discardedDenominator);
	result.discardedNumerator/=divisor; result.discardedDenominator/=divisor;
	if (result.discardedNumerator) ++owner->stats.measurements.materialSpillageEvents;
	update();
	return result;
}



void Building::removeMaterialFromBuilding(int resourceType)
{
	withdrawMaterialPacket(resourceType);
}

MaterialPacket Building::withdrawMaterialPacket(int resourceType)
{
	const int before = materials[resourceType];
	materials[resourceType]-=std::min(availableMaterial(resourceType), type->materialMultiplier[resourceType]);
	materials[resourceType]= std::max(materials[resourceType], 0);
	owner->stats.measurements.withdrawn[resourceType] += before - materials[resourceType];
	if (type->canExchange)
		owner->stats.measurements.transferredOut[resourceType] += before - materials[resourceType];
	if ((type->runtimeSuppliesStock || type->runtimeSuppliesDirectStock || type->useTeamMaterials) && availableMaterial(resourceType)<=0)
		owner->map->dirtyMarketGradientsSlot(owner->teamNumber, resourceType);
	updateCallLists();
	const Uint32 amount=before-materials[resourceType], denomination=type->materialMultiplier[resourceType];
	const Uint32 divisor=std::gcd(amount,denomination);
	return {amount/divisor,denomination/divisor};
}



int Building::getMidX(void)
{
	return ((posX-type->decLeft)&owner->map->getMaskW());
}

int Building::getMidY(void)
{
	return ((posY-type->decTop)&owner->map->getMaskH());
}

bool Building::findGroundExit(int *posX, int *posY, int *dx, int *dy, bool canSwim)
{
	int testX, testY;
	int exitQuality=0;
	int oldQuality;
	int exitX=0, exitY=0;

	// TODO: Introduce a border iterator for rectangles

	// if (exitQuality<EXIT_QUALITY_GOOD_ENOUGH)
	{
		testY=this->posY-1;
		oldQuality=0;
		for (testX=this->posX-1; testX<=this->posX+type->width ; testX++)
			checkGroundExitQuality(testX,testY,testX,testY-1,exitX,exitY,exitQuality,oldQuality,canSwim);
	}
	if (exitQuality<EXIT_QUALITY_GOOD_ENOUGH)
	{
		testY=this->posY+type->height;
		oldQuality=0;
		for (testX=this->posX-1; (testX<=this->posX+type->width) ; testX++)
			checkGroundExitQuality(testX,testY,testX,testY+1,exitX,exitY,exitQuality,oldQuality,canSwim);
	}
	if (exitQuality<EXIT_QUALITY_GOOD_ENOUGH)
	{
		oldQuality=0;
		testX=this->posX-1;
		for (testY=this->posY-1; (testY<=this->posY+type->height) ; testY++)
			checkGroundExitQuality(testX,testY,testX-1,testY,exitX,exitY,exitQuality,oldQuality,canSwim);
	}
	if (exitQuality<EXIT_QUALITY_GOOD_ENOUGH)
	{
		oldQuality=0;
		testX=this->posX+type->width;
		for (testY=this->posY-1; (testY<=this->posY+type->height) ; testY++)
			checkGroundExitQuality(testX,testY,testX+1,testY,exitX,exitY,exitQuality,oldQuality,canSwim);
	}
	if (exitQuality>0)
	{
		auto off = owner->map->doesPosTouchBuilding(exitX, exitY, gid);
		assert(off);
		*dx=-off->dx;
		*dy=-off->dy;
		*posX=exitX & owner->map->getMaskW();
		*posY=exitY & owner->map->getMaskH();
		return true;
	}
	return false;
}

void Building::checkGroundExitQuality(
		const int testX,
		const int testY,
		const int extraTestX,
		const int extraTestY,
		int & exitX,
		int & exitY,
		int & exitQuality,
		int & oldQuality,
		bool canSwim)
{
	Uint32 me=owner->me;
	if (owner->map->isFreeForGroundUnit(testX, testY, canSwim, me))
	{
		if (owner->map->isFreeForGroundUnit(extraTestX, extraTestY, canSwim, me))
			oldQuality++;
		if (owner->map->isResource(testX, testY-1))
		{
			if (exitQuality<EXIT_QUALITY_NEAR_RESOURCE+oldQuality)
			{
				exitQuality=EXIT_QUALITY_NEAR_RESOURCE+oldQuality;
				exitX=testX;
				exitY=testY;
			}
			oldQuality=0;
		}
		else
		{
			if (exitQuality<EXIT_QUALITY_OPEN_GROUND+oldQuality)
			{
				exitQuality=EXIT_QUALITY_OPEN_GROUND+oldQuality;
				exitX=testX;
				exitY=testY;
			}
			oldQuality=EXIT_QUALITY_NEAR_RESOURCE;
		}
	}
}

bool Building::findAirExit(int *posX, int *posY, int *dx, int *dy)
{
	for (int xi=this->posX; xi<this->posX+type->width; xi++)
		for (int yi=this->posY; yi<this->posY+type->height; yi++)
			if (owner->map->isFreeForAirUnit(xi, yi))
			{
				*posX=xi;
				*posY=yi;
				int tdx=xi-getMidX();
				int tdy=yi-getMidY();
				if (tdx<0)
					*dx=-1;
				else if (tdx==0)
					*dx=0;
				else
					*dx=1;

				if (tdy<0)
					*dy=-1;
				else if (tdy==0)
					*dy=0;
				else
					*dy=1;
				return true;
			}
	return false;
}

bool Building::findExpelTile(bool fly, bool canSwim, int *posX, int *posY, int *dx, int *dy)
{
	// Pass 0 takes the footprint, pass 1 the ring around it.
	for (int pass=0; pass<2; pass++)
		for (int y=this->posY-1; y<=this->posY+type->height; y++)
			for (int x=this->posX-1; x<=this->posX+type->width; x++)
			{
				int ox=(x>=this->posX+type->width)-(x<this->posX);
				int oy=(y>=this->posY+type->height)-(y<this->posY);
				if ((ox!=0 || oy!=0)!=(pass==1))
					continue;
				int wx=x&owner->map->getMaskW();
				int wy=y&owner->map->getMaskH();
				if (fly ? !owner->map->isFreeForAirUnit(wx, wy) : !owner->map->isFreeForGroundUnit(wx, wy, canSwim, owner->me))
					continue;
				*posX=wx;
				*posY=wy;
				*dx=ox;
				*dy=oy;
				return true;
			}
	return false;
}

int Building::getLongLevel(void)
{
	return ((type->level)<<1)+1-type->isBuildingSite;
}

Uint32 Building::eatOnce(Uint32 *mask, Unit* visitor)
{
	if (visitor) settleService(visitor);
	else
	{
		const auto& cost = type->semantics.feeding.cost;
		if (!reserveMaterials(cost)) throw std::runtime_error("Insufficient meal materials");
		consumeReservedMaterials(cost, GameplayMeasurements::MEAL);
	}
	++owner->stats.measurements.meals;
	Uint32 fruitMask=0;
	Uint32 fruitCount=0;
	for (int i=0; i<HAPPINESS_COUNT; i++)
	{
		int resId=i+HAPPINESS_BASE;
		if ((type->semantics.feeding.optionalFruitMask & (1u << i)) && availableMaterial(resId) > 0)
		{
			materials[resId]--;
			if ((type->useTeamMaterials || type->runtimeSuppliesStock || type->runtimeSuppliesDirectStock) && availableMaterial(resId)==0)
				owner->map->dirtyMarketGradientsSlot(owner->teamNumber,resId);
			++owner->stats.measurements.consumed[GameplayMeasurements::MEAL][resId];
			fruitMask|=(1<<i);
			fruitCount++;
		}
	}
	if (mask)
		*mask=fruitMask;
	return fruitCount;
}

int Building::availableHappynessLevel()
{
	int inside = (int)unitsInside.size();
	if (!canOfferService(nullptr, FEED))
		return 0;
	int happyness = 1;
	for (int i = 0; i < HAPPINESS_COUNT; i++)
		if ((type->semantics.feeding.optionalFruitMask & (1u << i)) && availableMaterial(i + HAPPINESS_BASE) > inside)
			happyness++;
	return happyness;
}

bool Building::canConvertUnit(void)
{
	assert(type->canFeedUnit);
	return
			canNotConvertUnitTimer<=0 && type->semantics.feeding.convertsUnits && canOfferService(nullptr, FEED);
}

bool Building::integrity()
{
	checkInvariant((int)unitsWorking.size()<=Unit::MAX_COUNT);
	for (std::list<Unit *>::iterator  it=unitsWorking.begin(); it!=unitsWorking.end(); ++it)
	{
		checkInvariant(*it);
		checkInvariant(owner->myUnits[Unit::GIDtoID((*it)->gid)]);
		checkInvariant((*it)->attachedBuilding==this);
	}

	checkInvariant((int)unitsInside.size()<=Unit::MAX_COUNT);
	for (std::list<Unit *>::iterator  it=unitsInside.begin(); it!=unitsInside.end(); ++it)
	{
		checkInvariant(*it);
		checkInvariant(owner->myUnits[Unit::GIDtoID((*it)->gid)]);
		checkInvariant((*it)->attachedBuilding==this);
	}
	for (std::list<Unit *>::iterator  it=unitsHarvesting.begin(); it!=unitsHarvesting.end(); ++it)
	{
		checkInvariant(*it);
		checkInvariant((*it)->targetBuilding==this);
	}
	return true;
}

Uint32 Building::checkSum(std::vector<Uint32> *checkSumsVector)
{
	// `cs` is signed `int` so the open-coded `(cs<<31)|(cs>>1)` rotates
	// use arithmetic right-shift (sign-extending). Do NOT replace these
	// with the unsigned `rotr1(Uint32)` helper in Utilities.h: when XOR
	// mixing leaves bit 31 set, signed `>>1` and unsigned `>>1` produce
	// different bit patterns, and the network checksum diverges. The
	// Rust port should preserve the arithmetic-shift behavior — i.e.
	// `((cs as i32) >> 1) as u32` — not `cs.rotate_right(1)`.
	int cs=0;

	cs^=typeNum;
	if (checkSumsVector)
		checkSumsVector->push_back(cs);// [0]

	cs^=buildingState;
	if (checkSumsVector)
		checkSumsVector->push_back(cs);// [1]
	cs=(cs<<31)|(cs>>1);

	cs^=constructionResultState;
	if (checkSumsVector)
		checkSumsVector->push_back(cs);// [2]
	cs=(cs<<31)|(cs>>1);

	cs^=maxUnitWorking;
	if (checkSumsVector)
		checkSumsVector->push_back(cs);// [3]

	cs^=maxUnitWorkingFuture;
	if (checkSumsVector)
		checkSumsVector->push_back(cs);// [4]

	cs^=maxUnitWorkingPreferred;
	if (checkSumsVector)
		checkSumsVector->push_back(cs);// [5]

	cs^=maxUnitWorkingPrevious;
	if (checkSumsVector)
		checkSumsVector->push_back(cs);// [6]

	cs^=desiredMaxUnitWorking;
	if (checkSumsVector)
		checkSumsVector->push_back(cs);// [7]

	cs^=unitsWorking.size();
	if (checkSumsVector)
		checkSumsVector->push_back(cs);// [8]

	cs^=subscriptionWorkingTimer;
	if (checkSumsVector)
		checkSumsVector->push_back(cs);// [9]

	cs^=unitsInside.size();
	if (checkSumsVector)
		checkSumsVector->push_back(cs);// [10]
	cs=(cs<<31)|(cs>>1);

	cs^=posX;
	if (checkSumsVector)
		checkSumsVector->push_back(cs);// [11]

	cs^=posY;
	if (checkSumsVector)
		checkSumsVector->push_back(cs);// [12]
	cs=(cs<<31)|(cs>>1);

	cs^=unitStayRange;
	if (checkSumsVector)
		checkSumsVector->push_back(cs);// [13]

	for (int i=0; i<MaterialCount; i++)
		cs^=localMaterials[i];
	if (checkSumsVector)
		checkSumsVector->push_back(cs);// [14]
	cs=(cs<<31)|(cs>>1);

	cs^=hp;
	if (checkSumsVector)
		checkSumsVector->push_back(cs);// [15]

	cs^=productionTimeout;
	cs^=productionUnit + 1;
	cs^=constructionOriginTypeNum+1;
	for (int value : constructionOriginRatios) cs=rotl1(cs)^value;
	cs^=repairInitialDeficit; cs=(cs<<1)|(cs>>31); cs^=repairHealthGranted;
	for (int r=0; r<MaterialSlotCount; ++r)
	{
		cs=(cs<<1)|(cs>>31); cs^=constructionBudget[r];
		cs=(cs<<1)|(cs>>31); cs^=constructionReserved[r];
	}
	cs^=siteCompletionPending ? 0x73697465 : 0;
	cs^=explorersRequireBombing ? 0x626f6d62 : 0;
	cs^=Uint32(minWorkerLevelToFlag)<<24;
	if (checkSumsVector)
		checkSumsVector->push_back(cs);// [16]


	cs^=totalRatio;
	if (checkSumsVector)
		checkSumsVector->push_back(cs);// [17]


	for (int i=0; i<NB_UNIT_TYPE; i++)
	{
		cs^=ratio[i];
		cs^=percentUsed[i];
		cs=(cs<<31)|(cs>>1);
	}
	if (checkSumsVector)
		checkSumsVector->push_back(cs);// [18]

	cs^=shootingStep;
	if (checkSumsVector)
		checkSumsVector->push_back(cs);// [19]


	cs^=shootingCooldown;
	if (checkSumsVector)
		checkSumsVector->push_back(cs);// [20]


	cs^=bullets;
	if (checkSumsVector)
		checkSumsVector->push_back(cs);// [21]
	cs=(cs<<31)|(cs>>1);

	cs^=seenByMask;
	if (checkSumsVector)
		checkSumsVector->push_back(cs);// [22]

	cs^=gid;
	if (checkSumsVector)
		checkSumsVector->push_back(cs);// [23]

	
	cs^=unitsHarvesting.size();
	if (checkSumsVector)
		checkSumsVector->push_back(cs);// [24]
	
	return cs;
}
