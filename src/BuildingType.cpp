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

void BuildingsTypes::load(const char *filename)
{
	// load the file
	EntitiesTypes<BuildingType>::load(filename);

	// We resolve the nextLevelTypeNum references, used for upgrade.
	for (std::vector <BuildingType *>::iterator it=entitiesTypes.begin(); it!=entitiesTypes.end(); ++it)
	{
		(*it)->lastLevelTypeNum=-1;
		(*it)->typeNum=-1;
		(*it)->nextLevelTypeNum=-1;
	}
	BuildingType *bt1;
	BuildingType *bt2;
	int j=0;
	for (std::vector <BuildingType *>::iterator it1=entitiesTypes.begin(); it1!=entitiesTypes.end(); ++it1)
	{
		bt1=*it1;
		bt1->nextLevelTypeNum=-1;
		bt1->typeNum=j;
		int i=0;
		for (std::vector <BuildingType *>::iterator it2=entitiesTypes.begin(); it2!=entitiesTypes.end(); ++it2)
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
	
	for (std::vector <BuildingType *>::iterator it=entitiesTypes.begin(); it!=entitiesTypes.end(); ++it)
	{
		//Need ressource integrity:
		bool needRessource=false;
		for (int i=0; i<MAX_RESSOURCES; i++)
			if ((*it)->maxRessource[i])
			{
				needRessource=true;
				break;
			}
		if (needRessource)
			assert((*it)->fillable || (*it)->foodable);
		
		//hpInc integrity:
		if ((*it)->isBuildingSite)
			assert((*it)->hpInc>0);
		else
			assert((*it)->hpInc==0);
		
		//flag integrity:
		if ((*it)->isVirtual)
		{
			assert((*it)->isCloacked);
			assert((*it)->defaultUnitStayRange);
		}
		if ((*it)->isCloacked)
		{
			assert((*it)->isVirtual);
			assert((*it)->defaultUnitStayRange);
		}
		if ((*it)->defaultUnitStayRange)
		{
			assert((*it)->isCloacked);
			assert((*it)->isVirtual);
		}
		if ((*it)->zonableForbidden)
		{
			assert((*it)->isCloacked);
			assert((*it)->isVirtual);
			assert((*it)->defaultUnitStayRange);
		}
		
	}
}

int BuildingsTypes::getTypeNum(int type, int level, bool isBuildingSite)
{
	int i=0;
	for (std::vector <BuildingType *>::iterator it=entitiesTypes.begin(); it!=entitiesTypes.end(); ++it)
	{
		BuildingType *bt=*it;
		if ((bt->type==type) && (bt->level==level) && (bt->isBuildingSite==(int)isBuildingSite))
			return i;
		i++;
	}
	// we can reach this point if we request a flag
	return -1;
}
