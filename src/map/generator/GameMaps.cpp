// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#include <math.h>
#include <time.h>
#include <stdlib.h>

//also the Perlin Noise stuff uses random that is not based on syncRand
#include "Game.h"
#include "GlobalContainer.h"
#include "MapGenerationDescriptor.h"
#include "MapGenerator.h"
#include "Map.h"
#include "Unit.h"
#include "Utilities.h"

bool Game::oldMakeIslandsMap(MapGenerationDescriptor &descriptor)
{
	for (int s=0; s<descriptor.nbTeams; s++)
	{
		if (mapHeader.getNumberOfTeams()<=s)
			addTeam();
		int squareSize=5+descriptor.oldIslandSize/10;
		map.setUMatPos(descriptor.bootX[s]+2, descriptor.bootY[s]+0, GRASS, squareSize);
		map.setUMatPos(descriptor.bootX[s]+2, descriptor.bootY[s]+2, GRASS, squareSize);
		
		Sint32 typeNum=globalContainer->buildingsTypes.getTypeNum("swarm", 0, false);
		if (!checkRoomForBuilding(descriptor.bootX[s], descriptor.bootY[s], globalContainer->buildingsTypes.get(typeNum), -1, false))
		{
			if (verbose)
				printf("Failed to add swarm of team %d\n", s);
			return false;
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
				return false;
			}
		teams[s]->createLists();
	}
	map.smoothRessources(descriptor.oldIslandSize/10);
	return true;
}

bool Game::makeRandomMap(MapGenerationDescriptor &descriptor)
{
	for (int s=0; s<descriptor.nbTeams; s++)
	{
		assert(mapHeader.getNumberOfTeams()==s);
		if (mapHeader.getNumberOfTeams()<=s)
			addTeam();
		
		map.setUMatPos(descriptor.bootX[s]+2, descriptor.bootY[s]+0, GRASS, 5);
		map.setUMatPos(descriptor.bootX[s]+2, descriptor.bootY[s]+2, GRASS, 5);
		map.setNoRessource(descriptor.bootX[s]+2, descriptor.bootY[s]+0, 5);
		map.setNoRessource(descriptor.bootX[s]+2, descriptor.bootY[s]+2, 5);
		
		Sint32 typeNum=globalContainer->buildingsTypes.getTypeNum("swarm", 0, false);
		if (!checkRoomForBuilding(descriptor.bootX[s], descriptor.bootY[s], globalContainer->buildingsTypes.get(typeNum), s, false))
		{
			if (verbose)
				printf("Failed to add swarm of team %d\n", s);
			return false;
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
				return false;
			}
		teams[s]->createLists();
	}
	return true;
}

