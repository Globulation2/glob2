// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#include <math.h>
#include <time.h>
#include <stdlib.h>

// Generation randomness is drawn from the explicitly seeded synchronized stream.
#include "Game.h"
#include "HeightMapGenerator.h"
#include "MapGenerationDescriptor.h"
#include "Map.h"
#include "Utilities.h"

/// This random map generator generates a height field and then chooses levels separating water, sand, grass, and desert.
bool Map::makeRandomMap(MapGenerationDescriptor &descriptor)
{
    return makeRandomMapTask(descriptor).run();
}

GAGCore::CooperativeTask Map::makeRandomMapTask(MapGenerationDescriptor &descriptor)
{
    unsigned work = 0;
    co_await GAGCore::CooperativeTask::checkpoint("[Generating map]");
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
	switch (descriptor.method)
	{
		case MapGenerationDescriptor::eSWAMP:
			co_await hm.makeSwampTask(smoothingFactor);
			waterTiles=(unsigned int)((float)descriptor.waterRatio*wHeightMap*hHeightMap/(float)tmpTotal);
			sandTiles=0;
			grassTiles=wHeightMap*hHeightMap-waterTiles;
			break;
		case MapGenerationDescriptor::eRIVER:
			co_await hm.makeRiverTask(descriptor.riverDiameter*(wHeightMap+hHeightMap)/2/100,smoothingFactor);
			waterTiles=(unsigned int)((float)descriptor.waterRatio/(float)totalGSWFromUI*wHeightMap*hHeightMap);
			sandTiles=(unsigned int)((float)descriptor.sandRatio/(float)totalGSWFromUI*wHeightMap*hHeightMap);
			grassTiles =(unsigned int)((float)descriptor.grassRatio /(float)totalGSWFromUI*wHeightMap*hHeightMap);
			break;
		case MapGenerationDescriptor::eCRATERLAKES:
			co_await hm.makeCratersTask(wHeightMap*hHeightMap*descriptor.craterDensity/30000, 30, smoothingFactor);
			waterTiles=(unsigned int)((float)descriptor.waterRatio/(float)totalGSWFromUI*wHeightMap*hHeightMap);
			sandTiles=(unsigned int)((float)descriptor.sandRatio/(float)totalGSWFromUI*wHeightMap*hHeightMap);
			grassTiles =(unsigned int)((float)descriptor.grassRatio /(float)totalGSWFromUI*wHeightMap*hHeightMap);
			break;
		case MapGenerationDescriptor::eISLANDS:
			co_await hm.makeIslandsTask(sectionIslandCount, smoothingFactor);
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
        if (++work % 64 == 0) co_await GAGCore::CooperativeTask::checkpoint();
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
        if (++work % 64 == 0) co_await GAGCore::CooperativeTask::checkpoint();
		accumulatedHistogram+=histogram[i++];
		if (algaeLevel==0 && accumulatedHistogram >= algaeTiles)
			algaeLevel = (float)(i-1)/2048.0;
		if (accumulatedHistogram >= waterTiles)
			waterLevel = (float)(i-1)/2048.0;
	}
	while ((sandLevel==0) && (i<2048))
	{
        if (++work % 64 == 0) co_await GAGCore::CooperativeTask::checkpoint();
		accumulatedHistogram+=histogram[i++];
		if (accumulatedHistogram >= waterTiles+sandTiles)
			sandLevel = (float)(i-1)/2048.0;
	}
	while ((grassLevel==0) && (i<2048))
	{
        if (++work % 64 == 0) co_await GAGCore::CooperativeTask::checkpoint();
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
	if (minDistSquare<=0)
	{
		co_return false;
	}
	assert(minDistSquare>0);
	int* bootX=descriptor.bootX;
	int* bootY=descriptor.bootY;
	
	//TODO: First pass to find the number of available places.
	for (int team=0; team<nbTeams; team++)
	{
        if (++work % 64 == 0) co_await GAGCore::CooperativeTask::checkpoint();
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
			co_return false;
		}
		assert(maxSurface);
		bootX[team]=maxX;
		bootY[team]=maxY;
	}
	
	controlSand();
    for (int column = 0; column < w; ++column) {
        regenerateMap(column, 0, 1, h);
        if (column % 8 == 0) co_await GAGCore::CooperativeTask::checkpoint();
    }
	//now to add primary resources for current map generator
	for (unsigned y=0; y<hHeightMap; y++)
	{
        if (++work % 64 == 0) co_await GAGCore::CooperativeTask::checkpoint();
		for (unsigned x=0; x<wHeightMap; x++)
		{
			int tmpResource=NO_RES;
			if(hm(x+wHeightMap*y)<algaeLevel)
			{
				tmpResource=ALGA;
			//following places stone next to sand & water and keeps wheat & wood more inland without clogging up the interior too badly
			}
			else if(hm(x+wHeightMap*y) < stoneLevel)
			{
				tmpResource=STONE;
			}
			else if(hm(x+wHeightMap*y)<wheatWoodLevel)
			{
				//patch to get smooth areas of wheat and wood:
				//if the map is ascending at x+w/2,y set wheat. else set wood
				if(hm((x+wHeightMap/2)%wHeightMap+wHeightMap*y)<hm((x+wHeightMap/2+1)%wHeightMap+wHeightMap*y))
				{
					tmpResource=CORN;
				}
				else
				{
					tmpResource=WOOD;
				}
			}
			if (tmpResource!=NO_RES)
			{
				for (int yRepeat=0; yRepeat<hRepeat; yRepeat++)
				{
					for (int xRepeat=0; xRepeat<wRepeat; xRepeat++)
					{
						setResource(xRepeat*wHeightMap+x,yRepeat*hHeightMap+y,tmpResource,1);
					}
				}
			}
		}
	}

	//TODO: count of groves(=descriptor.fruitRatio) does not scale with mapsize.
	//so it has to be adjusted higher on bigger maps now.

	// Use the generation RNG stream; do not reseed libc global state here.

	//fruit-placement:
	if (descriptor.fruitRatio > 0)
	{
		for (int q1=0; q1<descriptor.fruitRatio; q1++) //counting groves
		{
			//choose fruit
			int fruit;
			switch (syncRand()%3)
			{
				case 0: fruit = CHERRY; break;
				case 1: fruit = ORANGE; break;
				case 2:
				default: fruit = PRUNE; break;
			}
			//choose coordinate where there is grass but no resource yet
            int x, y;
            std::size_t attempts = 0;
            do
            {
                if (++work % 64 == 0) co_await GAGCore::CooperativeTask::checkpoint();
                if (++attempts > std::size_t(wHeightMap) * hHeightMap * 8) {
                    bool found = false;
                    for (unsigned candidateY = 0; candidateY < hHeightMap && !found; ++candidateY)
                        for (unsigned candidateX = 0; candidateX < wHeightMap && !found; ++candidateX)
                            if (getUMTerrain(candidateX, candidateY) == GRASS && !isResource(candidateX, candidateY)) {
                                x = candidateX; y = candidateY; found = true;
                            }
                    if (!found) co_return false;
                    break;
                }
				x=(syncRand()%wHeightMap);
				y=(syncRand()%hHeightMap);
			} while (getUMTerrain(x, y)!=GRASS || isResource(x,y));
			//choose size of grove (tree count)
			int grovesize=(syncRand()%10)+1;
			for (int i=0; i<grovesize; i++)
			{
				for (int yRepeat=0; yRepeat<hRepeat; yRepeat++)
					for (int xRepeat=0; xRepeat<wRepeat; xRepeat++)
						setResource(xRepeat*wHeightMap+x,yRepeat*hHeightMap+y,fruit,1);
				//find a valid neighbor of actual coordinate
				for (int iTry=0; iTry<100; iTry++)
				{
					int xNew=x+syncRand()%3-1;
					int yNew=y+syncRand()%3-1;
					if(getUMTerrain(xNew, yNew)==GRASS && !isResource(xNew,yNew))
					{
						x=xNew;
						y=yNew;
						break;
					}
				}
			}
		}
	}
	co_return true;
}

