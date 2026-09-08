// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#include <math.h>
#include <time.h>
#include <stdlib.h>

#include "Game.h"
#include "GlobalContainer.h"
#include "MapGenerationDescriptor.h"
#include "MapGenerator.h"
#include "Map.h"
#include "Unit.h"
#include "Utilities.h"

bool Game::oldMakeIslandsMap(MapGenerationDescriptor &descriptor)
{
    return oldMakeIslandsMapTask(descriptor).run();
}

GAGCore::CooperativeTask Game::oldMakeIslandsMapTask(MapGenerationDescriptor &descriptor)
{
    unsigned work = 0;
    co_await GAGCore::CooperativeTask::checkpoint("[Generating map]");
	for (int s=0; s<descriptor.nbTeams; s++)
	{
        if (++work % 64 == 0) co_await GAGCore::CooperativeTask::checkpoint();
		if (mapHeader.getNumberOfTeams()<=s)
			addTeam();
		int squareSize=5+descriptor.oldIslandSize/10;
		map.setUMatPos(descriptor.bootX[s]+2, descriptor.bootY[s]+0, GRASS, squareSize);
		map.setUMatPos(descriptor.bootX[s]+2, descriptor.bootY[s]+2, GRASS, squareSize);
		
		Sint32 typeNum=globalContainer->buildingsTypes.getTypeNum("swarm", 0, false);
		if (!checkRoomForBuilding(descriptor.bootX[s], descriptor.bootY[s], globalContainer->buildingsTypes.get(typeNum), s, false))
		{
			if (verbose)
				printf("Failed to add swarm of team %d\n", s);
			co_return false;
		}
		teams[s]->startPosX=descriptor.bootX[s];
		teams[s]->startPosY=descriptor.bootY[s];
		Building *b=addBuilding(descriptor.bootX[s], descriptor.bootY[s], typeNum, s);
		assert(b);
		for (int i=0; i<descriptor.nbWorkers; i++)
			if (addUnit(descriptor.bootX[s]+(i%4), descriptor.bootY[s]-1-(i/4), s, WORKER, 0, 0, 0, 0)==NULL)
			{
				if (verbose)
					printf("Failed to add unit %d of team %d\n", i, s);
				co_return false;
			}
		teams[s]->createLists();
	}
	map.smoothResources(descriptor.oldIslandSize/10);
	co_return true;
}

bool Game::makeRandomMap(MapGenerationDescriptor &descriptor)
{
    return makeRandomMapTask(descriptor).run();
}

GAGCore::CooperativeTask Game::makeRandomMapTask(MapGenerationDescriptor &descriptor)
{
    unsigned work = 0;
    co_await GAGCore::CooperativeTask::checkpoint("[Generating map]");
	for (int s=0; s<descriptor.nbTeams; s++)
	{
        if (++work % 64 == 0) co_await GAGCore::CooperativeTask::checkpoint();
		assert(mapHeader.getNumberOfTeams()==s);
		if (mapHeader.getNumberOfTeams()<=s)
			addTeam();
		
		map.setUMatPos(descriptor.bootX[s]+2, descriptor.bootY[s]+0, GRASS, 5);
		map.setUMatPos(descriptor.bootX[s]+2, descriptor.bootY[s]+2, GRASS, 5);
		map.setNoResource(descriptor.bootX[s]+2, descriptor.bootY[s]+0, 5);
		map.setNoResource(descriptor.bootX[s]+2, descriptor.bootY[s]+2, 5);
		
		Sint32 typeNum=globalContainer->buildingsTypes.getTypeNum("swarm", 0, false);
		if (!checkRoomForBuilding(descriptor.bootX[s], descriptor.bootY[s], globalContainer->buildingsTypes.get(typeNum), s, false))
		{
			if (verbose)
				printf("Failed to add swarm of team %d\n", s);
			co_return false;
		}
		teams[s]->startPosX=descriptor.bootX[s];
		teams[s]->startPosY=descriptor.bootY[s];
		Building *b=addBuilding(descriptor.bootX[s], descriptor.bootY[s], typeNum, s);
		assert(b);
		for (int i=0; i<descriptor.nbWorkers; i++)
			if (addUnit(descriptor.bootX[s]+(i%4), descriptor.bootY[s]-1-(i/4), s, WORKER, 0, 0, 0, 0)==NULL)
			{
				if (verbose)
					printf("Failed to add unit %d of team %d\n", i, s);
				co_return false;
			}
		teams[s]->createLists();
	}
	co_return true;
}

