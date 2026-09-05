// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#include <math.h>
#include <float.h>
#include <time.h>
#include <stdlib.h>

//also the Perlin Noise stuff uses random that is not based on syncRand
#include "Game.h"
#include "MapGenerationDescriptor.h"
#include "Map.h"

void simulateRandomMap(int smooth, double baseWater, double baseSand, double baseGrass, double *finalWater, double *finalSand, double *finalGrass);
#include "Utilities.h"

bool Map::oldMakeRandomMap(MapGenerationDescriptor &descriptor)
{
	int waterRatio=descriptor.waterRatio;
	int sandRatio =descriptor.sandRatio ;
	int grassRatio=descriptor.grassRatio;
	int totalRatio=waterRatio+sandRatio+grassRatio;
	int smooth=descriptor.smooth;
	double baseWater, baseSand, baseGrass;
	
	if (totalRatio == 0)
	{
		baseWater = baseSand = baseGrass = 1.0/3.0;
	}
	else
	{
		baseWater=(float)waterRatio/(float)totalRatio;
		baseSand =(float)sandRatio /(float)totalRatio;
		baseGrass=(float)grassRatio/(float)totalRatio;
	}
	//Sorry, the equation is too complex for me. We use a numeric approach:
	double alphaWater=baseWater;
	double alphaSand =baseSand ;
	double alphaGrass=baseGrass;
	double alphaSum=alphaWater+alphaSand+alphaGrass;
	alphaWater/=alphaSum;
	alphaSand /=alphaSum;
	alphaGrass/=alphaSum;
	
	for (int r=1; r<=smooth; r++)
	{
		for (int prec=0; prec<3; prec++) 
		{
			double finalAlphaWater, finalAlphaSand, finalAlphaGrass;
			simulateRandomMap(r, alphaWater, alphaSand, alphaGrass, &finalAlphaWater, &finalAlphaSand, &finalAlphaGrass);
			
			
			double errAlphaWater=finalAlphaWater-baseWater;
			double errAlphaSand =finalAlphaSand -baseSand ;
			double errAlphaGrass=finalAlphaGrass-baseGrass;
			

			double betaWater;
			double betaSand ; 
			double betaGrass;
			
			if (finalAlphaWater)
				betaWater=(alphaWater*baseWater)/finalAlphaWater;
			else
				betaWater=0;
			if (finalAlphaSand)
				betaSand =(alphaSand *baseSand )/finalAlphaSand ;
			else
				betaSand=0;
			if (finalAlphaGrass)
				betaGrass=(alphaGrass*baseGrass)/finalAlphaGrass;
			else
				betaGrass=0;
			double betaSum=betaWater+betaSand+betaGrass;
			betaWater/=betaSum;
			betaSand /=betaSum;
			betaGrass/=betaSum;
			
			double finalBetaWater, finalBetaSand, finalBetaGrass;
			simulateRandomMap(r, betaWater, betaSand, betaGrass, &finalBetaWater, &finalBetaSand, &finalBetaGrass);
			
			
			double errBetaWater=finalBetaWater-baseWater;
			double errBetaSand =finalBetaSand -baseSand ;
			double errBetaGrass=finalBetaGrass-baseGrass;
			
			
			double projNom=(errBetaWater*errAlphaWater+errBetaSand*errAlphaSand+errBetaGrass*errAlphaGrass);
			double projDen=(errAlphaWater*errAlphaWater+errAlphaSand*errAlphaSand+errAlphaGrass*errAlphaGrass);
			if (projDen<=0)
				continue;
			double proj=projNom/projDen;
						
			
			double minErr=DBL_MAX;
			for (double cfi=0.0; cfi<=1.0; cfi+=0.1)
			{
				double cf=cfi*proj;
				
				double sumCenter=1.0-cf;
				double gammaWater=(-cf*betaWater+1.0*alphaWater)/sumCenter;
				double gammaSand =(-cf*betaSand +1.0*alphaSand )/sumCenter;
				double gammaGrass=(-cf*betaGrass+1.0*alphaGrass)/sumCenter;
				if (gammaWater<0.0)
					gammaWater=0.0;
				if (gammaSand<0.0)
					gammaSand=0.0;
				if (gammaGrass<0.0)
					gammaGrass=0.0;
				double gammaSum=gammaWater+gammaSand+gammaGrass;
				if (gammaSum<=0)
					continue;
				gammaWater/=gammaSum;
				gammaSand /=gammaSum;
				gammaGrass/=gammaSum;

				double finalGammaWater, finalGammaSand, finalGammaGrass;
				simulateRandomMap(r, gammaWater, gammaSand, gammaGrass, &finalGammaWater, &finalGammaSand, &finalGammaGrass);
				

				double errGammaWater=finalGammaWater-baseWater;
				double errGammaSand =finalGammaSand -baseSand ;
				double errGammaGrass=finalGammaGrass-baseGrass;
				double errGamma=(errGammaWater*errGammaWater+errGammaSand*errGammaSand+errGammaGrass*errGammaGrass);
				
				if (errGamma<minErr)
				{
					minErr=errGamma;
					alphaWater=gammaWater;
					alphaSand =gammaSand;
					alphaGrass=gammaGrass;
				}
			}
		}
	}
	
	double simWater, simSand, simGrass;
	simulateRandomMap(smooth, alphaWater, alphaSand, alphaGrass, &simWater, &simSand, &simGrass);

	totalRatio=0x7FFF;
	waterRatio=(int)(((double)alphaWater)*((double)totalRatio));
	sandRatio =(int)(((double)alphaSand )*((double)totalRatio));
	grassRatio=(int)(((double)alphaGrass)*((double)totalRatio));
	if (waterRatio<0)
		waterRatio=0;
	if (sandRatio<0)
		sandRatio=0;
	if (grassRatio<0)
		grassRatio=0;
	totalRatio=waterRatio+sandRatio+grassRatio;
	
	
	// First, we create a fully random patchwork:
	for (int y=0; y<h; y++)
		for (int x=0; x<w; x++)
		{
			int r=syncRand()%totalRatio;
			r-=waterRatio;
			if (r<0)
			{
				undermap[y*w+x]=WATER;
				continue;
			}
			r-=sandRatio;
			if (r<0)
			{
				undermap[y*w+x]=SAND;
				continue;
			}
			r-=grassRatio;
			if (r<0)
			{
				undermap[y*w+x]=GRASS;
				continue;
			}
			assert(false);//Want's to sing ?
		}
	
	// What's finally in ?
	int waterCount=0;
	int sandCount =0;
	int grassCount=0;
	for (int y=0; y<h; y++)
		for (int x=0; x<w; x++)
		{
			switch (undermap[y*w+x])
			{
				case WATER:
					waterCount++;
				continue;
				case SAND :
					sandCount ++;
				continue;
				case GRASS:
					grassCount++;
				continue;
			}
		}
	double totalCount=(double)(waterCount+sandCount+grassCount);

	for (int i=0; i<smooth; i++)
	{
		// What's in now?
		waterCount=0;
		sandCount =0;
		grassCount=0;
		for (int y=0; y<h; y++)
			for (int x=0; x<w; x++)
			{
				switch (undermap[y*w+x])
				{
					case WATER:
						waterCount++;
					continue;
					case SAND :
						sandCount ++;
					continue;
					case GRASS:
						grassCount++;
					continue;
				}
			}
		double totalRatioCount=(double)(waterCount+sandCount+grassCount);
		double waterRatioCount=waterCount/totalRatioCount;
		double sandRatioCount =sandCount /totalRatioCount;
		double grassRatioCount=grassCount/totalRatioCount;
		
		double errWaterRatioCount=waterRatioCount-baseWater;
		double errSandRatioCount =sandRatioCount -baseSand ;
		double errGrassRatioCount=grassRatioCount-baseGrass;
		
		Uint32 allowed[3];
		if (errWaterRatioCount>0)
			allowed[0]=(Uint32)(pow(errWaterRatioCount, 0.125)*4294967296.0);
		else
			allowed[0]=0;
		if (errSandRatioCount>0)
			allowed[1]=(Uint32)(pow(errSandRatioCount , 0.125)*4294967296.0);
		else
			allowed[1]=0;
		if (errGrassRatioCount>0)
			allowed[2]=(Uint32)(pow(errGrassRatioCount, 0.125)*4294967296.0);
		else
			allowed[2]=0;
		
		assert(allowed[0]<=(Uint32)0xFFFFFFFF);
		assert(allowed[1]<=(Uint32)0xFFFFFFFF);
		assert(allowed[2]<=(Uint32)0xFFFFFFFF);
		
		if (i==0)
		{
			allowed[0]=0;
			allowed[1]=0;
			allowed[2]=0;
		}
		
		for (int y=0; y<h; y++)
			for (int x=0; x<w; x++)
			{
				if (syncRand()&4)
				{
					int a=getUMTerrain(x+1, y);
					int b=getUMTerrain(x-1, y);
					if ((a==b)&&(allowed[a]<=syncRand()))
					{
						setUMTerrain(x, y, (TerrainType)a);
						continue;
					}
				}
				else
				{
					int a=getUMTerrain(x, y-1);
					int b=getUMTerrain(x, y+1);
					if ((a==b)&&(allowed[a]<=syncRand()))
					{
						setUMTerrain(x, y, (TerrainType)a);
						continue;
					}
				}
				if (syncRand()&4)
				{
					int a=getUMTerrain(x+1, y+1);
					int b=getUMTerrain(x-1, y-1);
					if ((a==b)&&(allowed[a]<=syncRand()))
					{
						setUMTerrain(x, y, (TerrainType)a);
						continue;
					}
				}
				else
				{
					int a=getUMTerrain(x+1, y-1);
					int b=getUMTerrain(x-1, y+1);
					if ((a==b)&&(allowed[a]<=syncRand()))
					{
						setUMTerrain(x, y, (TerrainType)a);
						continue;
					}
				}
				if (syncRand()&4)
				{
					int a=getUMTerrain(x+2, y);
					int b=getUMTerrain(x-2, y);
					if ((a==b)&&(allowed[a]<=syncRand()))
					{
						setUMTerrain(x, y, (TerrainType)a);
						continue;
					}
				}
				else
				{
					int a=getUMTerrain(x, y-2);
					int b=getUMTerrain(x, y+2);
					if ((a==b)&&(allowed[a]<=syncRand()))
					{
						setUMTerrain(x, y, (TerrainType)a);
						continue;
					}
				}
				if (syncRand()&4)
				{
					int a=getUMTerrain(x+2, y+2);
					int b=getUMTerrain(x-2, y-2);
					if ((a==b)&&(allowed[a]<=syncRand()))
					{
						setUMTerrain(x, y, (TerrainType)a);
						continue;
					}
				}
				else
				{
					int a=getUMTerrain(x+2, y-2);
					int b=getUMTerrain(x-2, y+2);
					if ((a==b)&&(allowed[a]<=syncRand()))
					{
						setUMTerrain(x, y, (TerrainType)a);
						continue;
					}
				}
			}
	}
	// What's finally in ?
	waterCount=0;
	sandCount =0;
	grassCount=0;
	for (int y=0; y<h; y++)
		for (int x=0; x<w; x++)
		{
			switch (undermap[y*w+x])
			{
				case WATER:
					waterCount++;
				continue;
				case SAND :
					sandCount ++;
				continue;
				case GRASS:
					grassCount++;
				continue;
			}
		}
	totalCount=(double)(waterCount+sandCount+grassCount);

	controlSand();

	//Now, we have to find suitable places for teams:
	int nbTeams=descriptor.nbTeams;
	int minDistSquare=(int)((double)((double)w*(double)h*(double)grassCount)/(double)((double)nbTeams*(double)totalCount));
	if (minDistSquare<=0)
		return false;
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
			return false;
		assert(maxSurface);
		bootX[team]=maxX;
		bootY[team]=maxY;
		
		for (int dx=-1; dx<6; dx++)
			for (int dy=0; dy<6; dy++)
				setUMTerrain(maxX+dx, maxY+dy, GRASS);
		
		
	}
	
	// Let's add some green space for teams:
	int squareSize=5+(int)(sqrt((double)minDistSquare)/4.5);
	for (int team=0; team<nbTeams; team++)
	{
		setUMatPos(descriptor.bootX[team]+2, descriptor.bootY[team]+0, GRASS, squareSize);
		setUMatPos(descriptor.bootX[team]+2, descriptor.bootY[team]+2, GRASS, squareSize);
	}
	
	controlSand();
	regenerateMap(0, 0, w, h);

	return true;
}

