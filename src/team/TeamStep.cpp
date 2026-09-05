// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#include <algorithm>

#include "BuildingType.h"
#include "Game.h"
#include "GameGUI.h"
#include "Map.h"
#include "Team.h"
#include "Unit.h"

bool Team::buildingHasHigherPriority(Building* lhs, Building* rhs)
{
	if(lhs->priority != rhs->priority)
		return lhs->priority > rhs->priority;

	int priority_lhs=0;
	if(lhs->type->shortTypeNum==IntBuildingType::FOOD_BUILDING && !lhs->type->isBuildingSite)
		priority_lhs=2+lhs->type->level*10;
	else
		priority_lhs=1+lhs->type->level*10;

	int priority_rhs=0;
	if(rhs->type->shortTypeNum==IntBuildingType::FOOD_BUILDING && !rhs->type->isBuildingSite)
		priority_rhs=2+rhs->type->level*10;
	else
		priority_rhs=1+rhs->type->level*10;

	if(priority_lhs != priority_rhs)
	{
		return priority_lhs > priority_rhs;
	}
	else
	{
		//This uses some fraction math in order to be able to compare the relative percent of units needed
		//for each building. The fractions are (needed_units / wanted_units) for both lhs and rhs.
		//The trick is to put them into a common denominator, which is done by cross multiplying.
		//The denominators don't actually need to be computed, only the numerators.
		int ratio_lhs_unit = (lhs->maxUnitWorking  - lhs->unitsWorking.size()) * rhs->unitsWorking.size();
		int ratio_rhs_unit = (rhs->maxUnitWorking  - rhs->unitsWorking.size()) * lhs->unitsWorking.size();
		if(ratio_lhs_unit == ratio_rhs_unit)
		{
			int ratio_lhs_ressource = lhs->totalWishedRessource();
			int ratio_rhs_ressource = rhs->totalWishedRessource();
			if(ratio_lhs_ressource != ratio_rhs_ressource)
				return ratio_lhs_ressource > ratio_rhs_ressource;
			// Tiebreak on gid: std::sort is unstable, so without a final
			// total order the position of tied buildings is unspecified
			// and can diverge across binaries (= multiplayer desync).
			return lhs->gid < rhs->gid;
		}
		else
		{
			return ratio_lhs_unit > ratio_rhs_unit;
		}
	}
	return false;
}


void Team::addBuildingNeedingWork(Building* b, Sint32 priority)
{
	bool did_find_position=false;
	Sint32 p = priority;
	std::vector<Building*>& blist = buildingsNeedingUnits[p];
	for(std::vector<Building*>::iterator i=blist.begin(); i!=blist.end(); ++i)
	{
		if(buildingHasHigherPriority(b, *i))
		{
			buildingsNeedingUnits[p].insert(i, b);
			did_find_position=true;
			break;
		}
	}
	if(!did_find_position)
		buildingsNeedingUnits[p].push_back(b);
}


void Team::removeBuildingNeedingWork(Building* b, Sint32 priority)
{
	Sint32 p = priority;
	buildingsNeedingUnits[p].erase(std::find(buildingsNeedingUnits[p].begin(), buildingsNeedingUnits[p].end(), b));
}



void Team::updateAllBuildingTasks()
{
	for(std::map<int, std::vector<Building*>, std::greater<int> >::iterator i = buildingsNeedingUnits.begin(); i!=buildingsNeedingUnits.end(); ++i)
	{
		std::sort(i->second.begin(), i->second.end(), Team::buildingHasHigherPriority);
		bool cont=true;
		std::vector<bool> foundPer(i->second.size(), true);
		while(cont)
		{
			bool found=false;
			for(unsigned j=0; j<(i->second.size()); ++j)
			{
				if(foundPer[j])
				{
					bool thisFound=false;
					if(i->second[j]->type->isVirtual)
						thisFound |= (i->second)[j]->subscribeForFlagingStep();
					else
						thisFound |= (i->second)[j]->subscribeToBringRessourcesStep();
					found |= thisFound;
					foundPer[j] = thisFound;
				}
			}
			if(!found)
				cont = false;
		}
	}
}




void Team::syncStep(void)
{
	integrity();

	if (noMoreBuildingSitesCountdown>0)
		noMoreBuildingSitesCountdown--;

	int nbUsefullUnits = 0;
	int nbUsefullUnitsAlone = 0;
	for (int i = 0; i < Unit::MAX_COUNT; i++)
	{
		Unit *u = myUnits[i];
		if (u)
		{
			if (u->typeNum != EXPLORER)
			{
				nbUsefullUnits++;
				if (u->medical == Unit::MED_FREE || (u->insideTimeout < 0 && u->attachedBuilding && u->attachedBuilding->type->canFeedUnit))
					nbUsefullUnitsAlone++;
			}
			u->syncStep();
			if (u->isDead)
			{
				// Sim must not read GameGUI state. Route the selection
				// clear through a GUI hook (see GameGUI::onUnitDestroyed).
				game->gui->onUnitDestroyed(u);
				delete u;
				myUnits[i] = NULL;
			}
		}
	}

	bool isDirtyGlobalGradient=false;
	for (std::list<Building *>::iterator it=buildingsWaitingForDestruction.begin(); it!=buildingsWaitingForDestruction.end();)
	{
		Building *building=*it;
		if (building->unitsInside.size()==0)
		{
			if (building->buildingState==Building::WAITING_FOR_DESTRUCTION)
			{
				if (!building->type->isVirtual)
				{
					map->setBuilding(building->posX, building->posY, building->type->width, building->type->height, NOGBID);
					// One tile narrower than the Game_orders.cpp rects; part of the replay-verified behaviour.
					map->dirtyLocalGradient(building->posX-GRADIENT_DIRTY_BORDER_TILES, building->posY-GRADIENT_DIRTY_BORDER_TILES, 2*GRADIENT_DIRTY_BORDER_TILES-1+building->type->width, 2*GRADIENT_DIRTY_BORDER_TILES-1+building->type->height, teamNumber);
					isDirtyGlobalGradient=true;
				}
				building->buildingState=Building::DEAD;
				prestige-=(*it)->type->prestige;
				buildingsToBeDestroyed.push_front(building);
			}

			std::list<Building *>::iterator ittemp=it;
			it=buildingsWaitingForDestruction.erase(ittemp);
		}
		else
			++it;
	}
	if (isDirtyGlobalGradient)
	{
		dirtyGlobalGradient();
		map->updateForbiddenGradient(teamNumber);
		map->updateGuardAreasGradient(teamNumber);
		map->updateClearAreasGradient(teamNumber);
	}

	for (std::list<Building *>::iterator it=buildingsToBeDestroyed.begin(); it!=buildingsToBeDestroyed.end(); ++it)
	{
		Building *building=*it;

		removeFromAbilitiesLists(building);

		assert(building->unitsWorking.size()==0);
		assert(building->unitsInside.size()==0);

		//TODO: optimisation: we can avoid some of thoses remove(Building *) by keeping a building state to detect which remove() are needed.
		buildingsTryToBuildingSiteRoom.remove(building);

		// Sim must not read GameGUI state. Route the selection
		// clear through a GUI hook (see GameGUI::onBuildingDestroyed).
		game->gui->onBuildingDestroyed(building);

		myBuildings[Building::GIDtoID(building->gid)]=NULL;
		delete building;
	}

	if (buildingsToBeDestroyed.size())
		buildingsToBeDestroyed.clear();

	for (std::list<Building *>::iterator it=buildingsTryToBuildingSiteRoom.begin(); it!=buildingsTryToBuildingSiteRoom.end();)
	{
		if ((*it)->tryToBuildingSiteRoom())
		{
			std::list<Building *>::iterator ittemp=it;
			it=buildingsTryToBuildingSiteRoom.erase(ittemp);
		}
		else
			++it;
	}

	updateAllBuildingTasks();

	bool isEnoughFoodInSwarm=false;

	for (int i=0; i<Building::MAX_COUNT; ++i)
	{
		if(myBuildings[i])
		{
			//Step in myBuildings does virtually nothing
			myBuildings[i]->step();
		}
	}

	for (std::list<Building *>::iterator it=swarms.begin(); it!=swarms.end(); ++it)
		{
			if (!(*it)->locked[SWIM_VARIANT_CAN_SWIM] && (*it)->ressources[CORN]>(*it)->type->ressourceForOneUnit)
				isEnoughFoodInSwarm=true;
			(*it)->swarmStep();
		}

	for (std::list<Building *>::iterator it=turrets.begin(); it!=turrets.end(); ++it)
		(*it)->turretStep(game->stepCounter);

	for (std::list<Building *>::iterator it=clearingFlags.begin(); it!=clearingFlags.end(); ++it)
		(*it)->clearingFlagStep();

	bool isDying= (playersMask==0)
		|| (!isEnoughFoodInSwarm && nbUsefullUnitsAlone==0 && (nbUsefullUnits==0 || (canFeedUnit.size()==0 && canHealUnit.size()==0)));
	if (isAlive && isDying)
	{
		isAlive=false;
	}

	stats.step(this);
	updateEvents();
}




void Team::dirtyGlobalGradient()
{
	game->dirtyWarFlagGradient();
	for (int id=0; id<Building::MAX_COUNT; id++)
	{
		Building *b=myBuildings[id];
		if (b)
			for (int canSwim=0; canSwim<SWIM_VARIANT_COUNT; canSwim++)
				if (b->globalGradient[canSwim])
				{
					delete[] b->globalGradient[canSwim];
					b->globalGradient[canSwim]=NULL;
					b->locked[canSwim]=false;
				}
	}
}

void Team::dirtyWarFlagGradient()
{
	for (std::list<Building *>::const_iterator it = virtualBuildings.begin(); it != virtualBuildings.end(); ++it)
	{
		Building *b = *it;
		if (b->type->zonable[WARRIOR])
			for (int canSwim=0; canSwim<SWIM_VARIANT_COUNT; canSwim++)
				if (b->globalGradient[canSwim])
				{
					delete[] b->globalGradient[canSwim];
					b->globalGradient[canSwim]=NULL;
					b->locked[canSwim]=false;
				}
	}
}
