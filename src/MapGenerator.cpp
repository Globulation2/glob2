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

void Map::makeRandomMap(MapGenerationDescriptor &descriptor)
{
	int waterRatio=descriptor.waterRatio;
	int sandRatio =descriptor.sandRatio ;
	int grassRatio=descriptor.grassRatio;
	int totalRatio=waterRatio+sandRatio+grassRatio;
	int smooth=descriptor.smooth;
	
	//The smooth will emphasis the differences between the ratios.
	//We have to correct them.
	double baseWater=(float)waterRatio/(float)totalRatio;
	double baseSand =(float)sandRatio /(float)totalRatio ;
	double baseGrass=(float)grassRatio/(float)totalRatio;
	printf("one base=(%f, %f, %f).\n", baseWater, baseSand, baseGrass);
	
	double biaisToWater=1.0-pow(1.0-pow(baseWater, 2.0), 4.0);
	double biaisToSand =1.0-pow(1.0-pow(baseSand , 2.0), 4.0);
	double biaisToGrass=1.0-pow(1.0-pow(baseGrass, 2.0), 4.0);
	printf("one biais=(%f, %f, %f).\n", biaisToWater, biaisToSand, biaisToGrass);
	
	double stayWater=(1.0-biaisToSand )*(1.0-biaisToGrass);
	double staySand =(1.0-biaisToWater)*(1.0-biaisToGrass);
	double stayGrass=(1.0-biaisToWater)*(1.0-biaisToSand );
	printf("one stay=(%f, %f, %f).\n", stayWater, staySand, stayGrass);
	
	double finalWater=baseWater*stayWater;
	double finalSand =baseSand *staySand ;
	double finalGrass=baseGrass*stayGrass;
	double finalSum=finalWater+finalSand+finalGrass;
	finalWater/=finalSum;
	finalSand /=finalSum;
	finalGrass/=finalSum;
	printf("one final=(%f, %f, %f).\n", finalWater, finalSand, finalGrass);
	
	//Sorry, the equation is too complex for me. We uses a numeric aproach:
	double correctionWater=1.0;
	double correctionSand =1.0;
	double correctionGrass=1.0;
	for (int r=1; r<smooth; r++)
	{
		for (int prec=0; prec<3; prec++)
		{
			double finalWater=baseWater*correctionWater;
			double finalSand =baseSand *correctionSand ;
			double finalGrass=baseGrass*correctionGrass;
			double finalSum=finalWater+finalSand+finalGrass;
			finalWater/=finalSum;
			finalSand /=finalSum;
			finalGrass/=finalSum;

			for (int t=0; t<r; t++)
			{
				biaisToWater=1.0-pow(1.0-pow(finalWater, 2.0), 4.0);
				biaisToSand =1.0-pow(1.0-pow(finalSand , 2.0), 4.0);
				biaisToGrass=1.0-pow(1.0-pow(finalGrass, 2.0), 4.0);

				stayWater=(1.0-biaisToSand )*(1.0-biaisToGrass);
				staySand =(1.0-biaisToWater)*(1.0-biaisToGrass);
				stayGrass=(1.0-biaisToWater)*(1.0-biaisToSand );

				finalWater*=stayWater;
				finalSand *=staySand ;
				finalGrass*=stayGrass;
				finalSum=finalWater+finalSand+finalGrass;
				finalWater/=finalSum;
				finalSand /=finalSum;
				finalGrass/=finalSum;
			}

			double newCorrectionWater=correctionWater/stayWater;
			double newCorrectionSand =correctionSand /staySand ;
			double newCorrectionGrass=correctionGrass/stayGrass;
			/*correctionWater/=stayWater;
			correctionSand /=staySand ;
			correctionGrass/=stayGrass;*/
			// This is a first good aproximation for correction.

			double errBaseWater=finalWater-baseWater;
			double errBaseSand =finalSand -baseSand ;
			double errBaseGrass=finalGrass-baseGrass;

			printf("A%d-correction[%d]=(%f, %f, %f).\n", prec, r, correctionWater, correctionSand, correctionGrass);
			printf("A%d-foundFinal[%d]=(%f, %f, %f).\n", prec, r, finalWater, finalSand, finalGrass);
			printf("A%d-error base[%d]=(%f, %f, %f).\n", prec, r, errBaseWater, errBaseSand, errBaseGrass);

			
			finalWater=baseWater*newCorrectionWater;
			finalSand =baseSand *newCorrectionSand ;
			finalGrass=baseGrass*newCorrectionGrass;

			finalSum=finalWater+finalSand+finalGrass;
			finalWater/=finalSum;
			finalSand /=finalSum;
			finalGrass/=finalSum;

			for (int t=0; t<r; t++)
			{
				biaisToWater=1.0-pow(1.0-pow(finalWater, 2.0), 4.0);
				biaisToSand =1.0-pow(1.0-pow(finalSand , 2.0), 4.0);
				biaisToGrass=1.0-pow(1.0-pow(finalGrass, 2.0), 4.0);

				stayWater=(1.0-biaisToSand )*(1.0-biaisToGrass);
				staySand =(1.0-biaisToWater)*(1.0-biaisToGrass);
				stayGrass=(1.0-biaisToWater)*(1.0-biaisToSand );

				finalWater*=stayWater;
				finalSand *=staySand ;
				finalGrass*=stayGrass;
				finalSum=finalWater+finalSand+finalGrass;
				finalWater/=finalSum;
				finalSand /=finalSum;
				finalGrass/=finalSum;
			}
			
			printf("B%d-correction[%d]=(%f, %f, %f).\n", prec, r, newCorrectionWater, newCorrectionSand, newCorrectionGrass);
			printf("B%d-foundFinal[%d]=(%f, %f, %f).\n", prec, r, finalWater, finalSand, finalGrass);
			double errCorrWater=finalWater-baseWater;
			double errCorrSand =finalSand -baseSand ;
			double errCorrGrass=finalGrass-baseGrass;
			printf("B%d-error base[%d]=(%f, %f, %f).\n", prec, r, errCorrWater, errCorrSand, errCorrGrass);

			double proj=(errCorrWater*errBaseWater+errCorrSand*errBaseSand+errCorrGrass*errBaseGrass)
				/(errBaseWater*errBaseWater+errBaseSand*errBaseSand+errBaseGrass*errBaseGrass);
			printf("B%d-proj=%f.\n", prec, proj);
			double corrFactor=1/proj;
			correctionWater=(correctionWater+newCorrectionWater*corrFactor)/(1.0+corrFactor);
			correctionSand =(correctionSand +newCorrectionSand *corrFactor)/(1.0+corrFactor);
			correctionGrass=(correctionGrass+newCorrectionGrass*corrFactor)/(1.0+corrFactor);
			/*correctionWater=(-proj*correctionWater+newCorrectionWater)/(1.0-proj);
			correctionSand =(-proj*correctionSand +newCorrectionSand )/(1.0-proj);
			correctionGrass=(-proj*correctionGrass+newCorrectionGrass)/(1.0-proj);*/
			
			printf("B%d-extra corr[%d]=(%f, %f, %f).\n", prec, r, correctionWater, correctionSand, correctionGrass);
		}
		finalWater=baseWater*correctionWater;
		finalSand =baseSand *correctionSand ;
		finalGrass=baseGrass*correctionGrass;
		
		finalSum=finalWater+finalSand+finalGrass;
		finalWater/=finalSum;
		finalSand /=finalSum;
		finalGrass/=finalSum;
		
		for (int t=0; t<r; t++)
		{
			biaisToWater=1.0-pow(1.0-pow(finalWater, 2.0), 4.0);
			biaisToSand =1.0-pow(1.0-pow(finalSand , 2.0), 4.0);
			biaisToGrass=1.0-pow(1.0-pow(finalGrass, 2.0), 4.0);

			stayWater=(1.0-biaisToSand )*(1.0-biaisToGrass);
			staySand =(1.0-biaisToWater)*(1.0-biaisToGrass);
			stayGrass=(1.0-biaisToWater)*(1.0-biaisToSand );

			finalWater*=stayWater;
			finalSand *=staySand ;
			finalGrass*=stayGrass;
			finalSum=finalWater+finalSand+finalGrass;
			finalWater/=finalSum;
			finalSand /=finalSum;
			finalGrass/=finalSum;
		}
		
		printf("C-foundFinal[%d]=(%f, %f, %f).\n", r, finalWater, finalSand, finalGrass);
	}
	printf("all final=(%f, %f, %f).\n", finalWater, finalSand, finalGrass);
	
	/*for (int r=0; r<smooth; r++)
	{
		biaisToWater=1.0-pow(1.0-pow(baseWater, 2.0), 4.0);
		biaisToSand =1.0-pow(1.0-pow(baseSand , 2.0), 4.0);
		biaisToGrass=1.0-pow(1.0-pow(baseGrass, 2.0), 4.0);

		stayWater=(1.0-biaisToSand )*(1.0-biaisToGrass);
		staySand =(1.0-biaisToWater)*(1.0-biaisToGrass);
		stayGrass=(1.0-biaisToWater)*(1.0-biaisToSand );
		
		baseWater/=stayWater;
		baseSand /=staySand ;
		baseGrass/=stayGrass;
	}*/
	
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
			assert(false);
		}

	for (int i=0; i<smooth; i++)
		for (int y=0; y<h; y++)
			for (int x=0; x<w; x++)
			{
				Uint32 sr=syncRand();
				if (sr&1)
				{
					int left=getUMTerrain(x+1, y);
					int righ=getUMTerrain(x-1, y);
					if (left==righ)
					{
						setUMTerrain(x, y, (TerrainType)left);
						continue;
					}
				}
				else
				{
					int top=getUMTerrain(x, y-1);
					int bot=getUMTerrain(x, y+1);
					if (top==bot)
					{
						setUMTerrain(x, y, (TerrainType)top);
						continue;
					}
				}
				
				if (sr&2)
				{
					int a=getUMTerrain(x-1, y-1);
					int b=getUMTerrain(x+1, y+1);
					if (a==b)
					{
						setUMTerrain(x, y, (TerrainType)a);
						continue;
					}
				}
				else
				{
					int c=getUMTerrain(x+1, y-1);
					int d=getUMTerrain(x-1, y+1);
					if (c==d)
					{
						setUMTerrain(x, y, (TerrainType)c);
						continue;
					}
				}

				if (sr&4)
				{
					int a=getUMTerrain(x-2, y);
					int b=getUMTerrain(x+2, y);
					if (a==b)
					{
						setUMTerrain(x, y, (TerrainType)a);
						continue;
					}
				}
				else
				{
					int c=getUMTerrain(x, y-2);
					int d=getUMTerrain(x, y+2);
					if (c==d)
					{
						setUMTerrain(x, y, (TerrainType)c);
						continue;
					}
				}

				if (sr&8)
				{
					int a=getUMTerrain(x-2, y-2);
					int b=getUMTerrain(x+2, y+2);
					if (a==b)
					{
						setUMTerrain(x, y, (TerrainType)a);
						continue;
					}
				}
				else
				{
					int c=getUMTerrain(x+2, y-2);
					int d=getUMTerrain(x-2, y+2);
					if (c==d)
					{
						setUMTerrain(x, y, (TerrainType)c);
						continue;
					}
				}
			}
	
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
