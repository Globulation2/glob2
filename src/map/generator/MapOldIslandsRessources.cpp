// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#include <math.h>
#include <time.h>
#include <stdlib.h>

//also the Perlin Noise stuff uses random that is not based on syncRand
#include "Game.h"
#include "MapGenerationDescriptor.h"
#include "Map.h"

void Map::oldAddResourcesIslandsMap(MapGenerationDescriptor &descriptor)
{
	int *bootX=descriptor.bootX;
	int *bootY=descriptor.bootY;
	
	int islandsSize=(int)(((w+h)*descriptor.oldIslandSize)/(400.0*sqrt((double)descriptor.nbTeams)));
	if (islandsSize<8)
		islandsSize=8;
	// let's add resources...
	int smoothResources=islandsSize/4;
	for (int s=0; s<descriptor.nbTeams; s++)
	{
		int d, p, amount;
		int smallestAmount;
		int smallestResource;

		//WOOD
		for (d=0; d<islandsSize; d++)
			if (!isGrass(bootX[s], bootY[s]-d))
				break;
		amount=descriptor.resource[WOOD];
		amount=d-smoothResources-2;
		if (amount<1)
			amount=1;
		p=d-1-amount/2;
		if (amount>0)
			setResource(bootX[s], bootY[s]-p, WOOD, amount);
		smallestAmount=amount;
		smallestResource=WOOD;
		
		//WHEAT
		for (d=0; d<islandsSize; d++)
			if (!isGrass(bootX[s]-d, bootY[s]))
				break;
		amount=descriptor.resource[CORN];
		amount=d-smoothResources-0;
		if (amount<1)
			amount=1;
		p=d-1-amount/2;
		if (amount>0)
			setResource(bootX[s]-p, bootY[s], CORN, amount);
		if (amount<smallestAmount)
		{
			smallestAmount=amount;
			smallestResource=CORN;
		}
		
		//STONE
		for (d=0; d<islandsSize; d++)
			if (!isGrass(bootX[s], bootY[s]+d))
				break;
		setResource(bootX[s], bootY[s]+p, STONE, 1);

		//We add the resource with the smallest amount:
		for (d=0; d<islandsSize; d++)
			if (!isGrass(bootX[s]+d, bootY[s]+d))
				break;
		amount=descriptor.resource[smallestResource];
		amount=d-smoothResources-3;
		if (amount<1)
			amount=1;
		p=d-1-amount/2;
		if (amount>0)
			setResource(bootX[s]+p, bootY[s]+p, smallestResource, amount);
		
		//ALGAE
		for (d=0; d<2*islandsSize; d++)
			if (isWater(bootX[s]+d, bootY[s]))
				break;
		amount=descriptor.resource[ALGA];
		amount=smoothResources;
		p=d+smoothResources-1+amount/2;
		if (amount>0)
			setResource(bootX[s]+p, bootY[s], ALGA, amount);
	}

	// Let's smooth resources...
	this->smoothResources(smoothResources*2);
}

