/*
* Globulation 2 building type
* (c) 2001 Stephane Magnenat, Luc-Olivier de Charriere, Ysagoon
*/

#include <vector>
#include "BuildingType.h"
#include "GlobalContainer.h"



BuildingType *BuildingsTypes::getBuildingType(unsigned int typeNum)
{
	if ((typeNum)<buildingsTypes.size())
		return buildingsTypes[typeNum];
	else
		return NULL;
}

BuildingsTypes::BuildingsTypes(const char *filename)
{
	// we load the building description now
	SDL_RWops *stream=globalContainer->fileManager.open(filename, "r");

	bool result=true;
	
	BuildingType defaultBuildingType;
	result=defaultBuildingType.loadText(stream);
	
	while (result)
	{
		BuildingType *buildingType=new BuildingType();
		*buildingType=defaultBuildingType;
		result=buildingType->loadText(stream);
		if (result)
			buildingsTypes.push_back(buildingType);
		else
			delete buildingType;
	}

	SDL_FreeRW(stream);

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
	// we should never be here
	assert(false);
	return -1;
}

BuildingsTypes::~BuildingsTypes()
{
	for (std::vector <BuildingType *>::iterator it=buildingsTypes.begin(); it!=buildingsTypes.end(); ++it)
	{
		delete (*it);
	}
}
