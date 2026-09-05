// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#include <math.h>
#include <time.h>
#include <stdlib.h>

//also the Perlin Noise stuff uses random that is not based on syncRand
#include "Game.h"
#include "MapGenerationDescriptor.h"
#include "Map.h"

void Map::oldAddRessourcesIslandsMap(MapGenerationDescriptor &descriptor)
{
	int *bootX=descriptor.bootX;
	int *bootY=descriptor.bootY;
	
	int islandsSize=(int)(((w+h)*descriptor.oldIslandSize)/(400.0*sqrt((double)descriptor.nbTeams)));
	if (islandsSize<8)
		islandsSize=8;
	// let's add ressources...
	int smoothRessources=islandsSize/4;
	for (int s=0; s<descriptor.nbTeams; s++)
	{
		int d, p, amount;
		int smallestAmount;
		int smallestRessource;

		//WOOD
		for (d=0; d<islandsSize; d++)
			if (!isGrass(bootX[s], bootY[s]-d))
				break;
		amount=descriptor.ressource[WOOD];
		amount=d-smoothRessources-2;
		if (amount<1)
			amount=1;
		p=d-1-amount/2;
		if (amount>0)
			setRessource(bootX[s], bootY[s]-p, WOOD, amount);
		smallestAmount=amount;
		smallestRessource=WOOD;
		
		//WHEAT
		for (d=0; d<islandsSize; d++)
			if (!isGrass(bootX[s]-d, bootY[s]))
				break;
		amount=descriptor.ressource[CORN];
		amount=d-smoothRessources-0;
		if (amount<1)
			amount=1;
		p=d-1-amount/2;
		if (amount>0)
			setRessource(bootX[s]-p, bootY[s], CORN, amount);
		if (amount<smallestAmount)
		{
			smallestAmount=amount;
			smallestRessource=CORN;
		}
		
		//STONE
		for (d=0; d<islandsSize; d++)
			if (!isGrass(bootX[s], bootY[s]+d))
				break;
		setRessource(bootX[s], bootY[s]+p, STONE, 1);

		//We add the ressource with the smallest amount:
		for (d=0; d<islandsSize; d++)
			if (!isGrass(bootX[s]+d, bootY[s]+d))
				break;
		amount=descriptor.ressource[smallestRessource];
		amount=d-smoothRessources-3;
		if (amount<1)
			amount=1;
		p=d-1-amount/2;
		if (amount>0)
			setRessource(bootX[s]+p, bootY[s]+p, smallestRessource, amount);
		
		//ALGAE
		for (d=0; d<2*islandsSize; d++)
			if (isWater(bootX[s]+d, bootY[s]))
				break;
		amount=descriptor.ressource[ALGA];
		amount=smoothRessources;
		p=d+smoothRessources-1+amount/2;
		if (amount>0)
			setRessource(bootX[s]+p, bootY[s], ALGA, amount);
	}

	// Let's smooth ressources...
	this->smoothRessources(smoothRessources*2);
}

