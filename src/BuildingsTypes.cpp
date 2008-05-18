/*
  Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charri�e
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

#include <assert.h>
#include <vector>
#include <iostream>

#include "BuildingsTypes.h"
#include "GlobalContainer.h"

void BuildingsTypes::load()
{
	ConfigVector<BuildingType>::load("data/buildings.default.txt", true);
	ConfigVector<BuildingType>::load("data/buildings.txt");
	
	resolveUpgradeReferences();
	
	checkIntegrity();
}

void BuildingsTypes::checkIntegrity(void)
{
	for (size_t i=0; i<entries.size(); i++)
	{
		BuildingType *bt = entries[i];
		assert(bt);
		
		//Need ressource integrity:
		bool needRessource=false;
		for (unsigned j=0; j<MAX_RESSOURCES; j++)
			if (bt->maxRessource[j])
			{
				needRessource=true;
				break;
			}
		if (needRessource)
			assert(bt->fillable || bt->foodable);
		
		//hpInc integrity:
		if (bt->isBuildingSite)
			assert(bt->hpInc > 0);
		else
			assert(bt->hpInc == 0);
		
		//mpMax/hpInit integrity:
		if (bt->isBuildingSite)
		{
			if (bt->level)
			{
				assert(bt->prevLevel != -1);
				BuildingType *bt2 = get(bt->prevLevel);
				assert(bt2);
				if (bt->hpInit != bt2->hpMax)
				{
					std::cerr << "BuildingsTypes::load() : warning : " << bt->type << " : Building site (" << entriesToName[i] << ") has hpInit=" << bt->hpInit << ", but final building (" << entriesToName[bt->prevLevel] << ") has hpMax=" << bt2->hpMax << std::endl;
				}
			}
		}
		
		
		//hpInit/hpInc integrity:
		if (bt->isBuildingSite)
		{
			int resSum=0;
			for (int i=0; i<MAX_RESSOURCES; i++)
				resSum += bt->maxRessource[i];
			int hpSum = bt->hpInit+resSum*bt->hpInc;
			if (hpSum < bt->hpMax)
			{
				std::cerr << "BuildingsTypes::load() : warning : " << bt->type << " : hpSum(" << hpSum <<") < hpMax(" << bt->hpMax << ") with hpInit=" << bt->hpInit << ", hpInc=" << bt->hpInc << ", resSum=" << resSum << ". Make hpInc>=" << (bt->hpMax-bt->hpInit+resSum-1)/resSum << std::endl;;
			}
		}
		
		
		//flag integrity:
		if (bt->isVirtual)
		{
			assert(bt->isCloacked);
			assert(bt->defaultUnitStayRange);
		}
		if (bt->isCloacked)
		{
			assert(bt->isVirtual);
			assert(bt->defaultUnitStayRange);
		}
		if (bt->defaultUnitStayRange)
		{
			assert(bt->isCloacked);
			assert(bt->isVirtual);
		}
		if (bt->zonableForbidden)
		{
			assert(bt->isCloacked);
			assert(bt->isVirtual);
			assert(bt->defaultUnitStayRange);
		}
		
	}
}

void BuildingsTypes::resolveUpgradeReferences(void)
{
	for (size_t i=0; i<entries.size(); i++)
	{
		entries[i]->nextLevel = entries[i]->prevLevel = -1;
	}
	
	for (size_t i=0; i<entries.size(); i++)
	{
		BuildingType *bt1 = entries[i];
		for (size_t j=0; j<entries.size(); j++)
		{
			BuildingType *bt2 = entries[j];
			if (bt1 != bt2)
				if (bt1->isBuildingSite)
				{
					if ((bt2->level == bt1->level) && (bt2->type  == bt1->type) && !(bt2->isBuildingSite))
					{
						bt1->nextLevel = j;
						bt2->prevLevel = i;
						break;
					}
				}
				else
				{
					if ((bt2->level == bt1->level+1) && (bt2->type == bt1->type) && (bt2->isBuildingSite))
					{
						bt1->nextLevel = j;
						bt2->prevLevel = i;
						break;
					}
				}
		}
	}
}

Sint32 BuildingsTypes::getTypeNum(const char *type, int level, bool isBuildingSite)
{
	assert(type);
	for (size_t i=0; i<entries.size(); i++)
	{
		if ((entries[i]->type == type) && (entries[i]->level == level) && ((entries[i]->isBuildingSite!=0) == isBuildingSite))
			return i;
	}
	
	//std::cerr << "BuildingsTypes::getTypeNum(" << type << "," << level << "," << isBuildingSite << ") : error : type does not exists" << std::endl;
	// we can reach this point if we request a flag
	return -1;
}

BuildingType *BuildingsTypes::getByType(const char *type, int level, bool isBuildingSite)
{
	assert(type);
	for (size_t i=0; i<entries.size(); i++)
	{
		if ((entries[i]->type == type) && (entries[i]->level == level) && ((entries[i]->isBuildingSite!=0) == isBuildingSite))
			return entries[i];
	}
	
	//std::cerr << "BuildingsTypes::getByType(" << type << "," << level << "," << isBuildingSite << ") : error : type does not exists" << std::endl;
	// we can reach this point if we request a flag
	return NULL;
}

Sint32 BuildingsTypes::getTypeNum(const std::string &s, int level, bool isBuildingSite)
{
	return getTypeNum(s.c_str(), level, isBuildingSite);
}

BuildingType *BuildingsTypes::getByType(const std::string &s,int level, bool isBuildingSite)
{
	return getByType(s.c_str(), level, isBuildingSite);
}

Uint32 BuildingsTypes::checkSum(void)
{
	Uint32 cs = 0;
	
	for (size_t i=0; i<entries.size(); ++i)
	{
		cs ^= entries[i]->checkSum();
		cs = (cs<<1) | (cs>>31);
	}
	
	return cs;
}
