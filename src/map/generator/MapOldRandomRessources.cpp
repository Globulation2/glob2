// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#include <math.h>
#include <time.h>
#include <stdlib.h>

//also the Perlin Noise stuff uses random that is not based on syncRand
#include "Game.h"
#include "MapGenerationDescriptor.h"
#include "Map.h"
#include "Unit.h"

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

