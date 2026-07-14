// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#include <math.h>
#include <time.h>
#include <stdlib.h>

//also the Perlin Noise stuff uses random that is not based on syncRand
#include "Game.h"
#include "HeightMapGenerator.h"
#include "MapGenerationDescriptor.h"
#include "Map.h"

/// This random map generator generates a heightfield and then choses levels at which to draw the line between water, sand, gras and sand again (desert)
bool Map::makeRandomMap(MapGenerationDescriptor &descriptor)
{
	/// all under waterLevel is water, under sandLevel is beach, under grassLevel is grass and above grasslevel is desert
	float waterLevel, sandLevel, grassLevel, wheatWoodLevel, algaeLevel, stoneLevel;
	/// to influence the roughness
	float smoothingFactor=(float)(descriptor.smooth+4)*3;
	/// the proportions requested through the gui can directly be translated into tile counts of the undermap.
	unsigned int waterTiles, sandTiles, grassTiles, wheatWoodTiles, algaeTiles;
	/// grass + sand + water + desert as from the gui
	unsigned int totalGSWFromUI=descriptor.waterRatio+descriptor.sandRatio+descriptor.grassRatio+descriptor.desertRatio+descriptor.fruitRatio;
	/// respect symmetry-requirements
	unsigned int wPower2Divider=0, hPower2Divider=0;
	int power2Divider=descriptor.logRepeatAreaTimes;
	for (int i = 0; i<power2Divider; i++)
		if ((w >> wPower2Divider) > (h >> hPower2Divider))
			wPower2Divider++;
		else
			hPower2Divider++;
	int wRepeat = 1 << wPower2Divider;
	int hRepeat = 1 << hPower2Divider;
	unsigned int wHeightMap=(unsigned int)(w/wRepeat);
	unsigned int hHeightMap=(unsigned int)(h/hRepeat);
	/// lets generate a patch of perlin noise. That's a smooth mapping R^2 to ]0;1[
	HeightMap hm(wHeightMap,hHeightMap);
	/// 1 to avoid division by zero,
	unsigned int tmpTotal=1+descriptor.waterRatio+descriptor.grassRatio;
	unsigned int sectionIslandCount=std::max(1u, static_cast<unsigned int>((descriptor.nbTeams+descriptor.extraIslands) / (1 << power2Divider)));
	switch (descriptor.methode)
	{
		case MapGenerationDescriptor::eSWAMP:
			hm.makeSwamp(smoothingFactor);
			waterTiles=(unsigned int)((float)descriptor.waterRatio*wHeightMap*hHeightMap/(float)tmpTotal);
			sandTiles=0;
			grassTiles=wHeightMap*hHeightMap-waterTiles;
			break;
		case MapGenerationDescriptor::eRIVER:
			hm.makeRiver(descriptor.riverDiameter*(wHeightMap+hHeightMap)/2/100,smoothingFactor);
			waterTiles=(unsigned int)((float)descriptor.waterRatio/(float)totalGSWFromUI*wHeightMap*hHeightMap);
			sandTiles=(unsigned int)((float)descriptor.sandRatio/(float)totalGSWFromUI*wHeightMap*hHeightMap);
			grassTiles =(unsigned int)((float)descriptor.grassRatio /(float)totalGSWFromUI*wHeightMap*hHeightMap);
			break;
		case MapGenerationDescriptor::eCRATERLAKES:
			hm.makeCraters(wHeightMap*hHeightMap*descriptor.craterDensity/30000, 30, smoothingFactor);
			waterTiles=(unsigned int)((float)descriptor.waterRatio/(float)totalGSWFromUI*wHeightMap*hHeightMap);
			sandTiles=(unsigned int)((float)descriptor.sandRatio/(float)totalGSWFromUI*wHeightMap*hHeightMap);
			grassTiles =(unsigned int)((float)descriptor.grassRatio /(float)totalGSWFromUI*wHeightMap*hHeightMap);
			break;
		case MapGenerationDescriptor::eISLANDS:
			hm.makeIslands(sectionIslandCount, smoothingFactor);
			waterTiles=(unsigned int)((float)descriptor.waterRatio/(float)totalGSWFromUI*wHeightMap*hHeightMap);
			sandTiles=(unsigned int)((float)descriptor.sandRatio/(float)totalGSWFromUI*wHeightMap*hHeightMap);
			grassTiles =(unsigned int)((float)descriptor.grassRatio /(float)totalGSWFromUI*wHeightMap*hHeightMap);
			break;
		default: assert(false);
			break;
	}
	/// wheat/wood needs ground to stand on and water. So:
	wheatWoodTiles=waterTiles<grassTiles?waterTiles/2:grassTiles/2;
	algaeTiles=waterTiles/6;

	/// histogram[i] collects the count of all terrain levels == i
	int histogram[2048];
	memset(histogram, 0, 2048*sizeof(int));

	for (unsigned i=0; i<wHeightMap*hHeightMap; i++)
	{
		histogram[hm.uiLevel(i,2048)]++;
	}
	unsigned int accumulatedHistogram=0;
	int i=0;
	waterLevel=0;
	sandLevel=0;
	grassLevel=0;
	wheatWoodLevel=0;
	stoneLevel=0;
	algaeLevel=0;	
	while ((waterLevel==0) && (i<2048))
	{
		accumulatedHistogram+=histogram[i++];
		if (algaeLevel==0 && accumulatedHistogram >= algaeTiles)
			algaeLevel = (float)(i-1)/2048.0;
		if (accumulatedHistogram >= waterTiles)
			waterLevel = (float)(i-1)/2048.0;
	}
	while ((sandLevel==0) && (i<2048))
	{
		accumulatedHistogram+=histogram[i++];
		if (accumulatedHistogram >= waterTiles+sandTiles)
			sandLevel = (float)(i-1)/2048.0;
	}
	while ((grassLevel==0) && (i<2048))
	{
		accumulatedHistogram+=histogram[i++];
		if (wheatWoodLevel==0 && accumulatedHistogram >= waterTiles+sandTiles+wheatWoodTiles)
			wheatWoodLevel = (float)(i-1)/2048.0;		
		if (stoneLevel==0 && accumulatedHistogram >= waterTiles+sandTiles+(wheatWoodTiles / 3))
			stoneLevel = (float)(i-1)/2048.0;	
		if (accumulatedHistogram >= waterTiles+sandTiles+grassTiles)
			grassLevel = (float)(i-1)/2048.0;
	}
	for (unsigned y=0; y<hHeightMap; y++)
		for (unsigned x=0; x<wHeightMap; x++)
			{
			int tmpUndermap;
			if (hm(y*wHeightMap+x)<waterLevel)
				tmpUndermap=WATER;
			else if (hm(y*wHeightMap+x)<sandLevel)
				tmpUndermap=SAND;
			else if (hm(y*wHeightMap+x)<grassLevel)
				tmpUndermap=GRASS;
			else
				tmpUndermap=SAND;
			for (int yRepeat=0; yRepeat<hRepeat; yRepeat++)
				for (int xRepeat=0; xRepeat<wRepeat; xRepeat++)
					undermap[xRepeat*wHeightMap+x+(yRepeat*hHeightMap+y)*w]=tmpUndermap;
			}
	controlSand();
	
	//Now, we have to find suitable places for teams:
	int nbTeams=descriptor.nbTeams;
	int minDistSquare=(int)((double)w*h/(double)nbTeams/5);
	//std::cout << "minDistSquare=" << minDistSquare << " (" << sqrt((double)minDistSquare) << ").\n";
	if (minDistSquare<=0)
	{
		//std::cout << "debugoutput 1\n";
		return false;
	}
	assert(minDistSquare>0);
	int* bootX=descriptor.bootX;
	int* bootY=descriptor.bootY;
	
	//TODO: First pass to find the number of available places.
	for (int team=0; team<nbTeams; team++)
	{
		int maxSurface=0;
		int maxX=0;
		int maxY=0;
		for (int y=0; y<h; y++)
		{
			int width=0;
			int startX=0;
			for (int x=0; x<w; x++)
			{
				int a=undermap[y*w+x];
				if (a==GRASS)
					width++;
				else
				{
					if (width>7)
					{
						int centerX=((x+startX)>>1);
						int top, bot;
						for (top=0; top<h; top++)
							if (getUMTerrain(centerX, y-top)!=GRASS)
								break;
						for (bot=0; bot<h; bot++)
							if (getUMTerrain(centerX, y+bot)!=GRASS)
								break;
						int height=top+bot-1;
						int surface=height*width;
						assert(surface>0);
						
						int centerY=y+((bot-top)>>1);
						bool farEnough=true;
						for (int ti=0; ti<team; ti++)
							if (warpDistSquare(centerX, centerY, bootX[ti], bootY[ti])<minDistSquare)
							{
								farEnough=false;
								break;
							}
						
						if (surface>maxSurface && farEnough)
						{
							maxSurface=surface;
							maxX=centerX;
							maxY=centerY;
						}
					}
					width=0;
					startX=x;
				}
			}
		}
		
		if (maxSurface<=0)
		{
			//std::cout << "debugoutput 2\n";
			return false;
		}
		assert(maxSurface);
		bootX[team]=maxX;
		bootY[team]=maxY;
	}
	
	controlSand();
	regenerateMap(0, 0, w, h);
	//now to add primary resources for current map generator
	for (unsigned y=0; y<hHeightMap; y++)
	{
		for (unsigned x=0; x<wHeightMap; x++)
		{
			int tmpRessource=NO_RES;
			if(hm(x+wHeightMap*y)<algaeLevel)
			{
				tmpRessource=ALGA;
			//following places stone next to sand & water and keeps wheat & wood more inland without clogging up the interior too badly
			}
			else if(hm(x+wHeightMap*y) < stoneLevel)
			{
				tmpRessource=STONE;
			}
			else if(hm(x+wHeightMap*y)<wheatWoodLevel)
			{
				//patch to get smooth areas of wheat and wood:
				//if the map is ascending at x+w/2,y set wheat. else set wood
				if(hm((x+wHeightMap/2)%wHeightMap+wHeightMap*y)<hm((x+wHeightMap/2+1)%wHeightMap+wHeightMap*y))
				{
					tmpRessource=CORN;
				}
				else
				{
					tmpRessource=WOOD;
				}
			}
			if (tmpRessource!=NO_RES)
			{
				for (int yRepeat=0; yRepeat<hRepeat; yRepeat++)
				{
					for (int xRepeat=0; xRepeat<wRepeat; xRepeat++)
					{
						setRessource(xRepeat*wHeightMap+x,yRepeat*hHeightMap+y,tmpRessource,1);
					}
				}
			}
		}
	}

	//TODO: count of groves(=descriptor.fruitRatio) does not scale with mapsize.
	//so it has to be adjusted higher on bigger maps now.

	// in mapgeneration syncRand is not needed. In earlier versions we assumed
	// to profit from sharing only the generation seeds for common random maps.
	// this assumption was dropped in favour of easier code.

	srand((unsigned)time(NULL));
	//fruit-placement:
	if (descriptor.fruitRatio > 0)
	{
		for (int q1=0; q1<descriptor.fruitRatio; q1++) //counting groves
		{
			//choose fruit
			int fruit;
			switch (rand()%3)
			{
				case 0: fruit = CHERRY; break;
				case 1: fruit = ORANGE; break;
				case 2:
				default: fruit = PRUNE; break;
			}
			//choose coordinate where there is grass but no ressource yet
			int x, y;
			do
			{
				x=(rand()%wHeightMap);
				y=(rand()%hHeightMap);
			} while (getUMTerrain(x, y)!=GRASS || isRessource(x,y));
			//choose size of grove (tree count)
			int grovesize=(rand()%10)+1;
			for (int i=0; i<grovesize; i++)
			{
				for (int yRepeat=0; yRepeat<hRepeat; yRepeat++)
					for (int xRepeat=0; xRepeat<wRepeat; xRepeat++)
						setRessource(xRepeat*wHeightMap+x,yRepeat*hHeightMap+y,fruit,1);
				//find a valid neighbor of actual coordinate
				for (int iTry=0; iTry<100; iTry++)
				{
					int xNew=x+rand()%3-1;
					int yNew=y+rand()%3-1;
					if(getUMTerrain(xNew, yNew)==GRASS && !isRessource(xNew,yNew))
					{
						x=xNew;
						y=yNew;
						break;
					}
				}
			}
		}
	}
	return true;
}

