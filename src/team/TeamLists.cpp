// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#include "BuildingType.h"
#include "Map.h"
#include "Team.h"
#include "Unit.h"
#include <algorithm>

void Team::createLists(void)
{
	// Setup helpers can register buildings before requesting a complete rebuild.
	// Rebuild only static capability lists; active service and staffing queues
	// retain their scheduling state.
	stockSuppliers.clear();
	directStockSuppliers.clear();
	combatFlags.clear();
	swarms.clear();
	turrets.clear();
	virtualBuildings.clear();
	clearingFlags.clear();
	canExchange.clear();
	for (Building* building : liveBuildings)
		{
			addToStaticAbilitiesLists(building,false);
			building->update();
		}
    for(int material=0;material<MaterialCount;++material)map->dirtyMarketGradientsSlot(teamNumber,material);
}


void Team::clearLists(void)
{
	for (int i=0; i<NB_ABILITY; i++)
		canUpgrade[i].clear();
	canFeedUnit.clear();
	canHealUnit.clear();
	canExchange.clear();
	buildingsWaitingForDestruction.clear();
	buildingsToBeDestroyed.clear();
	buildingsTryToBuildingSiteRoom.clear();
	buildingsNeedingUnits.clear();
	stockSuppliers.clear();
	directStockSuppliers.clear();
	combatFlags.clear();
	swarms.clear();
	turrets.clear();
	virtualBuildings.clear();
	// clearMem deletes every building next; a clearing flag left listed here would
	// dangle (and be stepped and saved) for the rest of the game.
	clearingFlags.clear();
}




void Team::clearMap(void)
{
	assert(map);

	for (Unit* unit : liveUnits)
		if (unit->performance[FLY]) map->setAirUnit(unit->posX,unit->posY,NOGUID);
		else map->setGroundUnit(unit->posX,unit->posY,NOGUID);
	for (Building* building : liveBuildings)
		if (!building->type->isVirtual)
			map->setBuilding(building->posX,building->posY,building->type->width,building->type->height,NOGBID);

}




void Team::clearMem(void)
{
	while (Unit* unit=liveUnits.front()) delete detachUnit(Unit::GIDtoID(unit->gid));
	while (Building* building=liveBuildings.front()) delete detachBuilding(Building::GIDtoID(building->gid));
	taskSwapNext=0xffff;
}




void Team::removeFromAbilitiesLists(Building *building)
{
	if (building->type->runtimeSuppliesStock || building->type->runtimeSuppliesDirectStock) map->invalidateSupplierLocations();
	building->cancelProduction();
	const auto removedStock=stockSuppliers.remove(building);
	directStockSuppliers.remove(building);
    if(removedStock)for(int material=0;material<MaterialCount;++material)map->dirtyMarketGradientsSlot(teamNumber,material);
	combatFlags.remove(building);
	for (int ui=0; ui<NB_ABILITY; ui++)
		if (building->type->upgrade[ui]) canUpgrade[ui].remove(building);

	if (building->type->canFeedUnit)
		canFeedUnit.remove(building);
	if (building->type->canHealUnit)
		canHealUnit.remove(building);
	building->resetServiceListState();
	if (building->type->canExchange)
		canExchange.remove(building);

	if (!building->type->semantics.production.enabledUnits.empty())
		swarms.remove(building);
	if (building->type->shootingRange)
		turrets.remove(building);

	if (building->runtime->attractsRole(0))
		clearingFlags.remove(building);

	if (building->type->isVirtual)
		virtualBuildings.remove(building);
}




void Team::addToStaticAbilitiesLists(Building *building,bool notifyMarketTargets)
{
	if (building->type->runtimeSuppliesDirectStock && building->buildingState == Building::ALIVE && std::find(directStockSuppliers.begin(),directStockSuppliers.end(),building)==directStockSuppliers.end()) directStockSuppliers.push_back(building);
	if (building->runtime->attractsRole(2) && std::find(combatFlags.begin(),combatFlags.end(),building)==combatFlags.end()) combatFlags.push_back(building);
	if (building->type->runtimeSuppliesStock || building->type->runtimeSuppliesDirectStock) map->invalidateSupplierLocations();
	if (building->type->runtimeSuppliesStock && building->buildingState == Building::ALIVE &&
		std::find(stockSuppliers.begin(), stockSuppliers.end(), building) == stockSuppliers.end())
    {
        stockSuppliers.push_back(building);
        // A newly completed or restored supplier can already contain stock.
        // Diff after list membership changes, using the final current inventory.
        if(notifyMarketTargets)for(int material=0;material<MaterialCount;++material)map->dirtyMarketGradientsSlot(teamNumber,material);
    }
	if (building->type->canExchange)
		if (std::find(canExchange.begin(), canExchange.end(), building) == canExchange.end()) canExchange.push_back(building);

	if (!building->type->semantics.production.enabledUnits.empty())
		if (std::find(swarms.begin(), swarms.end(), building) == swarms.end()) swarms.push_back(building);

	if (building->type->shootingRange)
		if (std::find(turrets.begin(), turrets.end(), building) == turrets.end()) turrets.push_back(building);

	if (building->runtime->attractsRole(0))
		if (std::find(clearingFlags.begin(), clearingFlags.end(), building) == clearingFlags.end()) clearingFlags.push_back(building);
;
	if (building->type->isVirtual)
		if (std::find(virtualBuildings.begin(), virtualBuildings.end(), building) == virtualBuildings.end()) virtualBuildings.push_back(building);
}
