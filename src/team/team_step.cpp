/*
  Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière
  for any question or comment contact us at <stephane at magnenat dot net> or <NuageBleu at gmail dot com>

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 3 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
*/

#include <algorithm>

#include "building_type.h"
#include "Game.h"
#include "GlobalContainer.h"
#include "LogFileManager.h"
#include "Map.h"
#include "team.h"
#include "Unit.h"

bool Team::prioritize_building(Building* lhs, Building* rhs)
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
			return ratio_lhs_ressource > ratio_rhs_ressource;
		}
		else
		{
			return ratio_lhs_unit > ratio_rhs_unit;
		}
	}
	return false;
}


void Team::add_building_needing_work(Building* b, Sint32 priority)
{
	bool did_find_position=false;
	Sint32 p = priority;
	std::vector<Building*>& blist = buildingsNeedingUnits[p];
	for(std::vector<Building*>::iterator i=blist.begin(); i!=blist.end(); ++i)
	{
		if(prioritize_building(b, *i))
		{
			buildingsNeedingUnits[p].insert(i, b);
			did_find_position=true;
			break;
		}
	}
	if(!did_find_position)
		buildingsNeedingUnits[p].push_back(b);
}


void Team::remove_building_needing_work(Building* b, Sint32 priority)
{
	Sint32 p = priority;
	buildingsNeedingUnits[p].erase(std::find(buildingsNeedingUnits[p].begin(), buildingsNeedingUnits[p].end(), b));
}



void Team::updateAllBuildingTasks()
{
	for(std::map<int, std::vector<Building*>, std::greater<int> >::iterator i = buildingsNeedingUnits.begin(); i!=buildingsNeedingUnits.end(); ++i)
	{
		std::sort(i->second.begin(), i->second.end(), Team::prioritize_building);
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
				fprintf(logFile, "unit guid=%d deleted\n", u->gid);
				if (u->attachedBuilding)
					fprintf(logFile, " attachedBuilding->bgid=%d\n", u->attachedBuilding->gid);
				if(game->selectedUnit == u)
					game->selectedUnit = NULL;
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
					map->dirtyLocalGradient(building->posX-16, building->posY-16, 31+building->type->width, 31+building->type->height, teamNumber);
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
		fprintf(logFile, "building guid=%d deleted\n", building->gid);
		fflush(logFile);

		removeFromAbilitiesLists(building);

		assert(building->unitsWorking.size()==0);
		assert(building->unitsInside.size()==0);

		//TODO: optimisation: we can avoid some of thoses remove(Building *) by keeping a building state to detect which remove() are needed.
		buildingsTryToBuildingSiteRoom.remove(building);

		if (game->selectedBuilding==building)
			game->selectedBuilding=NULL;

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
			if (!(*it)->locked[1] && (*it)->ressources[CORN]>(*it)->type->ressourceForOneUnit)
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
		fprintf(logFile, "Team %d is dead:\n", teamNumber);
		fprintf(logFile, " isEnoughFoodInSwarm=%d\n", isEnoughFoodInSwarm);
		fprintf(logFile, " nbUsefullUnitsAlone=%d\n", nbUsefullUnitsAlone);
		fprintf(logFile, " nbUsefullUnits=%d\n", nbUsefullUnits);
		fprintf(logFile, "  canFeedUnit.size()=%zd\n", canFeedUnit.size());
		fprintf(logFile, "  canHealUnit.size()=%zd\n", canHealUnit.size());
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
			for (int canSwim=0; canSwim<2; canSwim++)
				if (b->globalGradient[canSwim])
				{
					//printf("freeing globalGradient for gbid=%d (%p)\n", b->gid, b->globalGradient[canSwim]);
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
			for (int canSwim=0; canSwim<2; canSwim++)
				if (b->globalGradient[canSwim])
				{
					delete[] b->globalGradient[canSwim];
					b->globalGradient[canSwim]=NULL;
					b->locked[canSwim]=false;
				}
	}
}
