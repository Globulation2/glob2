/*
    Copyright (C) 2001, 2002 Stephane Magnenat & Luc-Olivier de Charriere
    for any question or comment contact us at nct@ysagoon.com

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA

*/

#include "Map.h"
#include "Game.h"
#include "Utilities.h"
#include "MapGenerationDescriptor.h"
#include <math.h>
#include <float.h>


void Map::makeHomogenMap(TerrainType terrainType)
{
	for (int y=0; y<h; y++)
		for (int x=0; x<w; x++)
			undermap[y*w+x]=terrainType;
	regenerateMap(0, 0, w, h);
}

void Map::controlSand(void)
{
	for (int y=0; y<h; y++)
		for (int x=0; x<w; x++)
		{
			int tt=(int)undermap[y*w+x];
			switch (tt)
			{
				case Map::WATER:
					for (int dy=-1; dy<=1; dy++)
						for (int dx=-1; dx<=1; dx++)
						{
							int stt=getUMTerrain(x+dx, y+dy);
							if (stt==Map::GRASS)
								goto bad;
						}
				break;
				case Map::SAND:
				continue;
				case Map::GRASS:
					for (int dy=-1; dy<=1; dy++)
						for (int dx=-1; dx<=1; dx++)
						{
							int stt=getUMTerrain(x+dx, y+dy);
							if (stt==Map::WATER)
								goto bad;
						}
				break;
			}
			continue;
			bad:
			undermap[y*w+x]=SAND;
		}
}

void simulateRandomMap(int smooth, double baseWater, double baseSand, double baseGrass, double *finalWater, double *finalSand, double *finalGrass)
{
	int w=32<<(smooth>>2);
	int h=w;
	int s=w*h;
	int m=s-1;
	int undermap[w*h];
	
	int totalRatio=0x7FFF;
	int waterRatio=(int)(baseWater*((double)totalRatio));
	int sandRatio =(int)(baseSand *((double)totalRatio));
	int grassRatio=(int)(baseGrass*((double)totalRatio));
	totalRatio=waterRatio+sandRatio+grassRatio;
	
	// First, we create a fully random patchwork:
	for (int y=0; y<h; y++)
		for (int x=0; x<w; x++)
		{
			int r=syncRand()%totalRatio;
			r-=waterRatio;
			if (r<0)
			{
				undermap[y*w+x]=0;
				continue;
			}
			r-=sandRatio;
			if (r<0)
			{
				undermap[y*w+x]=1;
				continue;
			}
			r-=grassRatio;
			if (r<0)
			{
				undermap[y*w+x]=2;
				continue;
			}
			assert(false);//Want's to sing ?
		}
	
	for (int i=0; i<smooth; i++)
		for (int y=0; y<h; y++)
			for (int x=0; x<w; x++)
			{
				if (syncRand()&4)
				{
					int a=undermap[(y*w+x+1+s)&m];
					int b=undermap[(y*w+x-1+s)&m];
					if (a==b)
					{
						undermap[y*w+x]=a;
						continue;
					}
				}
				else
				{
					int a=undermap[(y*w+x+w+s)&m];
					int b=undermap[(y*w+x-w+s)&m];
					if (a==b)
					{
						undermap[y*w+x]=a;
						continue;
					}
				}
				if (syncRand()&4)
				{
					int a=undermap[(y*w+x+w+1+s)&m];
					int b=undermap[(y*w+x-w-1+s)&m];
					if (a==b)
					{
						undermap[y*w+x]=a;
						continue;
					}
				}
				else
				{
					int a=undermap[(y*w+x+w-1+s)&m];
					int b=undermap[(y*w+x-w+1+s)&m];
					if (a==b)
					{
						undermap[y*w+x]=a;
						continue;
					}
				}
				if (syncRand()&4)
				{
					int a=undermap[(y*w+x+w-2+s)&m];
					int b=undermap[(y*w+x-w+2+s)&m];
					if (a==b)
					{
						undermap[y*w+x]=a;
						continue;
					}
				}
				else
				{
					int a=undermap[(y*w+x+w-(h<<1)+s)&m];
					int b=undermap[(y*w+x-w+(h<<1)+s)&m];
					if (a==b)
					{
						undermap[y*w+x]=a;
						continue;
					}
				}
				if (syncRand()&4)
				{
					int a=undermap[(y*w+x+w+2+(h<<1)+s)&m];
					int b=undermap[(y*w+x-w-2-(h<<1)+s)&m];
					if (a==b)
					{
						undermap[y*w+x]=a;
						continue;
					}
				}
				else
				{
					int a=undermap[(y*w+x+w+2-(h<<1)+s)&m];
					int b=undermap[(y*w+x-w-2+(h<<1)+s)&m];
					if (a==b)
					{
						undermap[y*w+x]=a;
						continue;
					}
				}
			}
	// What's finally in ?
	int waterCount=0;
	int sandCount =0;
	int grassCount=0;
	for (int y=0; y<h; y++)
		for (int x=0; x<w; x++)
			switch (undermap[y*w+x])
			{
				case 0:
					waterCount++;
				continue;
				case 1:
					sandCount ++;
				continue;
				case 2:
					grassCount++;
				continue;
			}
	
	int totalCount=waterCount+sandCount+grassCount;
	
	*finalWater=((double)waterCount)/((double)totalCount);
	*finalSand =((double)sandCount )/((double)totalCount);
	*finalGrass=((double)grassCount)/((double)totalCount);
}

void Map::makeRandomMap(MapGenerationDescriptor &descriptor)
{
	int waterRatio=descriptor.waterRatio;
	int sandRatio =descriptor.sandRatio ;
	int grassRatio=descriptor.grassRatio;
	int totalRatio=waterRatio+sandRatio+grassRatio;
	int smooth=descriptor.smooth;
	
	double baseWater=(float)waterRatio/(float)totalRatio;
	double baseSand =(float)sandRatio /(float)totalRatio;
	double baseGrass=(float)grassRatio/(float)totalRatio;
	printf("makeRandomMap::old-base=(%f, %f, %f).\n", baseWater, baseSand, baseGrass);
	
	//Sorry, the equation is too complex for me. We uses a numeric aproach:
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
			//printf("alpha-alpha=(%f, %f, %f) (%f).\n", alphaWater, alphaSand, alphaGrass, alphaWater+alphaSand+alphaGrass);
			//printf("alpha-final=(%f, %f, %f) (%f).\n", finalAlphaWater, finalAlphaSand, finalAlphaGrass, finalAlphaWater+finalAlphaSand+finalAlphaGrass);
			
			double errAlphaWater=finalAlphaWater-baseWater;
			double errAlphaSand =finalAlphaSand -baseSand ;
			double errAlphaGrass=finalAlphaGrass-baseGrass;
			//double errAlpha=(errAlphaWater*errAlphaWater+errAlphaSand*errAlphaSand+errAlphaGrass*errAlphaGrass);
			//printf("errAlpha=(%f, %f, %f) (%f).\n", errAlphaWater, errAlphaSand, errAlphaGrass, errAlpha);
			
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
			//printf("beta-beta=(%f, %f, %f) (%f).\n", betaWater, betaSand, betaGrass, betaWater+betaSand+betaGrass);
			//printf("beta-final=(%f, %f, %f) (%f).\n", finalBetaWater, finalBetaSand, finalBetaGrass, finalBetaWater+finalBetaSand+finalBetaGrass);
			
			double errBetaWater=finalBetaWater-baseWater;
			double errBetaSand =finalBetaSand -baseSand ;
			double errBetaGrass=finalBetaGrass-baseGrass;
			//double errBeta=(errBetaWater*errBetaWater+errBetaSand*errBetaSand+errBetaGrass*errBetaGrass);
			//printf("errBeta=(%f, %f, %f) (%f).\n", errBetaWater, errBetaSand, errBetaGrass, errBeta);
			
			double projNom=(errBetaWater*errAlphaWater+errBetaSand*errAlphaSand+errBetaGrass*errAlphaGrass);
			double projDen=(errAlphaWater*errAlphaWater+errAlphaSand*errAlphaSand+errAlphaGrass*errAlphaGrass);
			double proj=projNom/projDen;
			//printf("proj=%f.\n", proj);
			
			
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
				double gammaSum=betaWater+betaSand+betaGrass;
				if (gammaSum<=0)
					continue;
				gammaWater/=gammaSum;
				gammaSand /=gammaSum;
				gammaGrass/=gammaSum;

				double finalGammaWater, finalGammaSand, finalGammaGrass;
				simulateRandomMap(r, gammaWater, gammaSand, gammaGrass, &finalGammaWater, &finalGammaSand, &finalGammaGrass);
				//printf("[%f]gamma-gamma=(%f, %f, %f) (%f).\n", cf, gammaWater, gammaSand, gammaGrass, gammaWater+gammaSand+gammaGrass);
				//printf("[%f]gamma-final=(%f, %f, %f) (%f).\n", cf, finalGammaWater, finalGammaSand, finalGammaGrass,  finalGammaWater+finalGammaSand+finalGammaGrass);

				double errGammaWater=finalGammaWater-baseWater;
				double errGammaSand =finalGammaSand -baseSand ;
				double errGammaGrass=finalGammaGrass-baseGrass;
				double errGamma=(errGammaWater*errGammaWater+errGammaSand*errGammaSand+errGammaGrass*errGammaGrass);
				//printf("[%f]err=%f.\n", cf, errGamma);
				if (errGamma<minErr)
				{
					minErr=errGamma;
					alphaWater=gammaWater;
					alphaSand =gammaSand;
					alphaGrass=gammaGrass;
				}
			}
			//printf("best-gamma=(%f, %f, %f) (%f) err=%f.\n", alphaWater, alphaSand, alphaGrass, alphaWater+alphaSand+alphaGrass, minErr);
			
		}
	}
	
	printf("makeRandomMap::new-base =(%f, %f, %f).\n", alphaWater, alphaSand, alphaGrass);
	
	double simWater, simSand, simGrass;
	simulateRandomMap(smooth, alphaWater, alphaSand, alphaGrass, &simWater, &simSand, &simGrass);
	printf("makeRandomMap::simulateRandomMap=(%f, %f, %f).\n", simWater, simSand, simGrass);
	
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
	//printf("ratios=(%d, %d, %d) / (%d).\n", waterRatio, sandRatio, grassRatio, totalRatio);
	
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
	printf("makeRandomMap::beforeCount=(%f, %f, %f).\n", waterCount/totalCount, sandCount/totalCount, grassCount/totalCount);
	
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
		
		//printf("[%d]errCount=(%f, %f, %f).\n", i, errWaterRatioCount, errSandRatioCount, errGrassRatioCount);
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
		//printf("[%d]allowed=(%d, %d, %d).\n", i, allowed[0], allowed[1], allowed[2]);
		
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
	printf("makeRandomMap::final count=(%f, %f, %f).\n", waterCount/totalCount, sandCount/totalCount, grassCount/totalCount);
	
	controlSand();
	regenerateMap(0, 0, w, h);
}

void Game::generateMap(MapGenerationDescriptor &descriptor)
{
	switch (descriptor.methode)
	{
		case MapGenerationDescriptor::eUNIFORM:
			map.makeHomogenMap(descriptor.terrainType);
		return;
		case MapGenerationDescriptor::eRANDOM:
			map.makeRandomMap(descriptor);
		return;
		default:
			assert(false);
	}
}
/*
WATER=0,
SAND=1,
GRASS=2
*/
