/*
  Copyright (C) 2001, 2002, 2003 Stephane Magnenat & Luc-Olivier de Charrière
  for any question or comment contact us at nct@ysagoon.com or nuage@ysagoon.com

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 2 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
*/

#include <vector>
#include "BuildingType.h"
#include "GlobalContainer.h"
#include <assert.h>

BuildingsTypes::BuildingsTypes() 
{
}

BuildingType *BuildingsTypes::getBuildingType(unsigned int typeNum)
{
	if ((typeNum)<buildingsTypes.size())
		return buildingsTypes[typeNum];
	else
		return NULL;
}

void BuildingsTypes::load(const char *filename)
{
	// we load the building description now
	SDL_RWops *stream=globalContainer->fileManager->open(filename, "r");
	assert(NB_RESSOURCES==5); // You have to case about parsing.

	bool result=true;
	
	BuildingType defaultBuildingType;
	result=defaultBuildingType.loadText(stream);
	
	while (result)
	{
		BuildingType *buildingType=new BuildingType();
		*buildingType=defaultBuildingType;
		result=buildingType->loadText(stream);
		if (result)
		{
			buildingsTypes.push_back(buildingType);
			//buildingType->dump();
		}
		else
			delete buildingType;
	}

	SDL_RWclose(stream);

	// We resolve the nextLevelTypeNum references, used for upgrade.
	for (std::vector <BuildingType *>::iterator it=buildingsTypes.begin(); it!=buildingsTypes.end(); ++it)
	{
		(*it)->lastLevelTypeNum=-1;
		(*it)->typeNum=-1;
		(*it)->nextLevelTypeNum=-1;
	}
	BuildingType *bt1;
	BuildingType *bt2;
	int j=0;
	for (std::vector <BuildingType *>::iterator it1=buildingsTypes.begin(); it1!=buildingsTypes.end(); ++it1)
	{
		bt1=*it1;
		bt1->nextLevelTypeNum=-1;
		bt1->typeNum=j;
		int i=0;
		for (std::vector <BuildingType *>::iterator it2=buildingsTypes.begin(); it2!=buildingsTypes.end(); ++it2)
		{
			bt2=*it2;
			if (bt1!=bt2)
				if (bt1->isBuildingSite)
				{
					if ((bt2->level==bt1->level) && (bt2->type==bt1->type) && !(bt2->isBuildingSite))
					{
						bt1->nextLevelTypeNum=i;
						bt2->lastLevelTypeNum=j;
						break;
					}
				}
				else
				{
					if ((bt2->level==bt1->level+1) && (bt2->type==bt1->type) && (bt2->isBuildingSite))
					{
						bt1->nextLevelTypeNum=i;
						bt2->lastLevelTypeNum=j;
						break;
					}
				}
			i++;
		}
		j++;
	}
}

int BuildingsTypes::getTypeNum(int type, int level, bool isBuildingSite)
{
	int i=0;
	for (std::vector <BuildingType *>::iterator it=buildingsTypes.begin(); it!=buildingsTypes.end(); ++it)
	{
		BuildingType *bt=*it;
		if ((bt->type==type) && (bt->level==level) && (bt->isBuildingSite==(int)isBuildingSite))
			return i;
		i++;
	}
	// we can reach this point if we request a flag
	return -1;
}

BuildingsTypes::~BuildingsTypes()
{
	for (std::vector <BuildingType *>::iterator it=buildingsTypes.begin(); it!=buildingsTypes.end(); ++it)
	{
		delete (*it);
	}
}
