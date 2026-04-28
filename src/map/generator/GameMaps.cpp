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

#include <math.h>
#include <float.h>
#include <time.h>
#include <stdlib.h>

#include "boost/integer_traits.hpp"
#include "boost/integer/common_factor.hpp"
//also the Perlin Noise stuff uses random that is not based on syncRand
#include "boost/random.hpp"
#include "Game.h"
#include "GlobalContainer.h"
#include "HeightMapGenerator.h"
#include "MapGenerationDescriptor.h"
#include "MapGenerator.h"
#include "Map.h"
#include <map>
#include <queue>
#include <set>
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

