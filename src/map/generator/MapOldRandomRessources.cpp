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

void Map::oldAddRessourcesRandomMap(MapGenerationDescriptor &descriptor)
{
	int *bootX=descriptor.bootX;
	int *bootY=descriptor.bootY;
	int nbTeams=descriptor.nbTeams;
	int limiteDist=(w+h)/(2*nbTeams);
	
	// let's add ressources to old map generator
	for (int team=0; team<nbTeams; team++)
	{
		int smallestWidth=limiteDist;
		int smallestRessource=0;
		
		bool dirUsed[8];
		for (int i=0; i<8; i++)
			dirUsed[i]=false;
		int resOrder[4];
		resOrder[0]=CORN;
		resOrder[1]=WOOD;
		resOrder[2]=STONE;
		resOrder[3]=CORN;
		
		int distWeight[4];
		distWeight[0]=1;
		distWeight[1]=1;
		distWeight[2]=1;
		distWeight[3]=2;
		
		int widthWeight[4];
		widthWeight[0]=1;
		widthWeight[1]=1;
		widthWeight[2]=1;
		widthWeight[3]=2;
		
		for (int resI=0; resI<4; resI++)
		{
			int res=resOrder[resI];
			int maxDir=0;
			int maxWidth=0;
			int maxDist=0;
			for (int dir=0; dir<8; dir++)
				if (!dirUsed[dir])
				{
					int width=0;
					int dx, dy, dist;
					Unit::dxDyFromDirection(dir, &dx, &dy);
					for (dist=5; dist<limiteDist; dist++)
						if (isGrass(bootX[team]+dx*dist, bootY[team]+dy*dist))
							width++;
						else if (width>3)
							break;
						else
							width=1;

					if (dist*distWeight[resI]+width*widthWeight[resI]>maxDist*distWeight[resI]+maxWidth*widthWeight[resI])
					{
						maxWidth=width;
						maxDist=dist;
						maxDir=dir;
					}
				}
			dirUsed[maxDir]=true;
			if (maxWidth<smallestWidth)
			{
				smallestWidth=maxWidth;
				smallestRessource=res;
			}
			
			int dx, dy;
			Unit::dxDyFromDirection(maxDir, &dx, &dy);
			int d=maxDist-(maxWidth>>1);
			dx*=d;
			dy*=d;
			
			int amount=descriptor.ressource[res];
			if (amount>0)
				setRessource(bootX[team]+dx, bootY[team]+dy, res, amount);
		}

		if (smallestWidth<limiteDist)
		{
			int maxDir=0;
			int maxWidth=0;
			int maxDist=0;
			for (int dir=0; dir<8; dir++)
				if (!dirUsed[dir])
				{
					int width=0;
					int dx, dy, dist;
					Unit::dxDyFromDirection(dir, &dx, &dy);
					for (dist=0; dist<2*limiteDist; dist++)
						if (isGrass(bootX[team]+dx*dist, bootY[team]+dy*dist))
							width++;
						else if (width>3)
							break;
						else
							width=1;

					if (dist+width>maxDist+maxWidth)
					{
						maxWidth=width;
						maxDist=dist;
						maxDir=dir;
					}
				}
			dirUsed[maxDir]=true;
			
			int dx, dy;
			Unit::dxDyFromDirection(maxDir, &dx, &dy);
			int d=maxDist-(maxWidth>>1);
			dx*=d;
			dy*=d;
			
			int amount=descriptor.ressource[smallestRessource];
			if (amount>0)
				setRessource(bootX[team]+dx, bootY[team]+dy, smallestRessource, amount);
		}

		int maxDir=0;
		int maxWidth=0;
		int maxDist=0;
		for (int dir=0; dir<8; dir++)
		{
			int width=0;
			int dx, dy, dist;
			Unit::dxDyFromDirection(dir, &dx, &dy);
			for (dist=0; dist<2*limiteDist; dist++)
				if (isWater(bootX[team]+dx*dist, bootY[team]+dy*dist))
					width++;
				else if (width>3)
					break;
				else
					width=1;
				
			if (dist+width>width+maxWidth)
			{
				maxWidth=width;
				maxDist=dist;
				maxDir=dir;
			}
		}
		
		int dx, dy;
		Unit::dxDyFromDirection(maxDir, &dx, &dy);
		int d=maxDist-(maxWidth>>1);
		dx*=d;
		dy*=d;
		
		int amount=descriptor.ressource[ALGA];
		if (amount>0)
			setRessource(bootX[team]+dx, bootY[team]+dy, ALGA, amount);
	}
	
	// Let's smooth ressources...
	int maxAmount=0;
	for (int r=0; r<4; r++)
		if (maxAmount<descriptor.ressource[r])
			maxAmount=descriptor.ressource[r];
	smoothRessources(maxAmount*3);
}

