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

