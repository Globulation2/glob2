// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#include "Map.h"
#include "Game.h"
#include "Utilities.h"
#include "building_type.h"
#include "Unit.h"
#include "MapInternal.h"

#include <algorithm>
#include <valarray>
#include <Stream.h>
#include <queue>


#include "MapGradientImpl.h"

// updateGlobalGradient(Building*) + template, updateLocalRessources, expandLocalGradient

void Map::updateGlobalGradient(Building *building, bool canSwim)
{
	if (size <= 65536)
		updateGlobalGradient<Uint16>(building, canSwim);
	else
		updateGlobalGradient<Uint32>(building, canSwim);
}

template<typename Tint> void Map::updateGlobalGradient(Building *building, bool canSwim)
{
	assert(building);
	assert(building->type);
	//printf("updatingGlobalGradient (gbid=%d)\n", building->gid);
	//fprintf(logFile, "updatingGlobalGradient (gbid=%d)...", building->gid);
	int posX=building->posX;
	int posY=building->posY;
	int posW=building->type->width;
	//int posH=building->type->height;
	Uint32 teamMask=building->owner->me;
	Uint16 bgid=building->gid;
	
	Uint8 *gradient=building->globalGradient[canSwim];
	assert(gradient);
	
	Tint *listedAddr = new Tint[size];
	size_t listCountWrite = 0;

	bool isClearingFlag=false;
	bool isWarFlag=false;
	if (building->type->isVirtual && building->type->zonable[WARRIOR])
		isWarFlag=true;
	
	memset(gradient, 1, size);
	if (building->type->isVirtual && !building->type->zonable[WORKER])
	{
		assert(!building->type->zonableForbidden);
		int r=building->unitStayRange;
		int r2=r*r;
		for (int yi=-r; yi<=r; yi++)
		{
			int yi2=(yi*yi);
			for (int xi=-r; xi<=r; xi++)
				if (yi2+(xi*xi)<=r2)
				{
					size_t addr = coordToIndex(posX+w+xi, posY+h+yi);
					if(gradient[addr] == 1)
					{
						gradient[addr] = 255;
						listedAddr[listCountWrite++] = addr;
					}
				}
		}
	}
	else if (building->type->isVirtual && building->type->zonable[WORKER])
	{
		assert(!building->type->zonableForbidden);
		isClearingFlag=true;
		int r=building->unitStayRange;
		int r2=r*r;
		for (int yi=-r; yi<=r; yi++)
		{
			int yi2=(yi*yi);
			for (int xi=-r; xi<=r; xi++)
				if (yi2+(xi*xi)<=r2)
				{
					size_t addr = coordToIndex(posX+w+xi, posY+h+yi);
					if(cases[addr].ressource.type!=NO_RES_TYPE && building->clearingRessources[cases[addr].ressource.type])
					{
						if(gradient[addr] == 1)
						{
							gradient[addr] = 255;
							listedAddr[listCountWrite++] = addr;
						}
					}
				}
		}
	}

	for (int y=0; y<h; y++)
	{
		int wy=w*y;
		for (int x=0; x<w; x++)
		{
			int wyx=wy+x;
			const Case& c=cases[wyx];
			if (c.building==NOGBID)
			{
				if (c.forbidden&teamMask)
					gradient[wyx] = 0;
				else if (c.ressource.type!=NO_RES_TYPE && !(isClearingFlag && gradient[wyx]==255))
					gradient[wyx] = 0;
				else if(immobileUnits[wyx] != 255)
					gradient[wyx] = 0;
				//Clearing flags don't consider water an obstacle so long as that piece of
				//water is under the flag, like algae
				else if (!canSwim && isWater(x, y) && (!isClearingFlag || gradient[wyx] != 255))
					gradient[wyx] = 0;
			}
			else
			{
				if (c.building==bgid)
				{
					gradient[wyx] = 255;
					listedAddr[listCountWrite++] = wyx;
				}
				//Warflags don't consider enemy buildings an obstacle
				else if(!isWarFlag || (1<<Building::GIDtoTeam(c.building)) & (building->owner->allies))
					gradient[wyx] = 0;
				else if(gradient[wyx]!=255)
					gradient[wyx] = 1;
			}
		}
	}
	
	if (!building->type->isVirtual)
	{
		building->locked[canSwim]=true;
		int x=(posX-1)&wMask;
		int y=(posY-1)&hMask;
		int d=posW+1;
		for (int ai=0; ai<4; ai++) //angle-iterator
			for (int mi=0; mi<d; mi++) //move-iterator
			{
				assert(x>=0);
				assert(y>=0);
				assert(x<w);
				assert(y<h);
				Uint8 g=gradient[w*y+x];
				//printf("ai=%d, mi=%d, (%d, %d), g=%d\n", ai, mi, x, y, g);
				if (g)
				{
					building->locked[canSwim]=false;
					goto doubleBreak;
				}
				switch (ai)
				{
					case 0:
						x++;
					break;
					case 1:
						y++;
					break;
					case 2:
						x--;
					break;
					case 3:
						y--;
					break;
				}
				x=(x+w)&wMask;
				y=(y+h)&hMask;
			}
		
		assert(building->locked[canSwim]);
		//printf("...not updatedGlobalGradient! building bgid=%d is locked!\n", building->gid);
		//fprintf(logFile, "...not updatedGlobalGradient! building bgid=%d is locked!\n", building->gid);
		delete[] listedAddr;
		return;
		doubleBreak:;
	}
	else
		building->locked[canSwim]=false;
	
	updateGlobalGradient(gradient, listedAddr, listCountWrite, GT_BUILDING, canSwim);
	delete[] listedAddr;
}


bool Map::updateLocalRessources(Building *building, bool canSwim)
{
	assert(building);
	assert(building->type);
	assert(building->type->isVirtual);


	int posX=building->posX;
	int posY=building->posY;
	Uint32 teamMask=building->owner->me;
	
	Uint8 *gradient=building->localRessources[canSwim];
	if (gradient==NULL)
	{
		gradient=new Uint8[1024];
		building->localRessources[canSwim]=gradient;
	}
	assert(gradient);
	
	bool *clearingRessources=building->clearingRessources;
	bool anyRessourceToClear=false;
	
	memset(gradient, 1, 1024);
	int range=building->unitStayRange;
	if (range>15)
		range=15;
	int range2=range*range;
	for (int yl=0; yl<32; yl++)
	{
		int wyl=(yl<<5);
		int yg=(yl+posY-15)&hMask;
		int wyg=w*yg;
		int dyl2=(yl-15)*(yl-15);
		for (int xl=0; xl<32; xl++)
		{
			int xg=(xl+posX-15)&wMask;
			const Case& c=cases[wyg+xg];
			int addrl=wyl+xl;
			int dist2=(xl-15)*(xl-15)+dyl2;
			if (dist2<=range2)
			{
				if (c.forbidden&teamMask)
					gradient[addrl]=0;
				else if (c.ressource.type!=NO_RES_TYPE)
				{
					Sint8 t=c.ressource.type;
					if (t<BASIC_COUNT && clearingRessources[t])
					{
						gradient[addrl]=255;
						anyRessourceToClear=true;
					}
					else
						gradient[addrl]=0;
				}
				else if (c.building!=NOGBID)
					gradient[addrl]=0;
				else if(immobileUnits[wyg+xg] != 255)
					gradient[addrl]=0;
				else if (!canSwim && isWater(xg, yg))
					gradient[addrl]=0;
			}
			else
				gradient[addrl]=0;
		}
	}
	// PORT: this is the SOLE reset for localRessourcesCleanTime[canSwim]; runs unconditionally
	// PORT: before both the false return below and the true return at function end. Building::clearingFlagStep
	// PORT: relies on this side effect rather than resetting the timer itself.
	building->localRessourcesCleanTime[canSwim]=0;
	if (anyRessourceToClear)
		building->anyRessourceToClear[canSwim]=1;
	else
	{
		building->anyRessourceToClear[canSwim]=2;
		return false;
	}
	expandLocalGradient(gradient);
	return true;
}



void Map::expandLocalGradient(Uint8 *gradient)
{
	for (int depth=0; depth<2; depth++) // With a higher depth, we can have more complex obstacles.
	{
		for (int down=0; down<2; down++)
		{
			int x, y, dis, die, ddi;
			if (down)
			{
				x=0;
				y=0;
				dis=31;
				die=1;
				ddi=-2;
			}
			else
			{
				x=15;
				y=15;
				dis=1;
				die=31;
				ddi=+2;
			}
			
			for (int di=dis; di!=die; di+=ddi) //distance-iterator
			{
				for (int bi=0; bi<2; bi++) //back-iterator
				{
					for (int ai=0; ai<4; ai++) //angle-iterator
					{
						for (int mi=0; mi<di; mi++) //move-iterator
						{
							//printf("di=%d, ai=%d, mi=%d, p=(%d, %d)\n", di, ai, mi, x, y);
							assert(x>=0);
							assert(y>=0);
							assert(x<32);
							assert(y<32);

							int wy=(y<<5);
							int wyu, wyd;
							if (y==0)
								wyu=0;
							else
								wyu=((y-1)<<5);
							if (y==31)
								wyd=32*31;
							else
								wyd=((y+1)<<5);
							Uint8 max=gradient[wy+x];
							if (max && max!=255)
							{
								int xl, xr;
								if (x==0)
									xl=0;
								else
									xl=x-1;
								if (x==31)
									xr=31;
								else
									xr=x+1;

								Uint8 side;
								
								side=gradient[wyu+xl];
								if (side > max) max=side;
								side=gradient[wyu+x ];
								if (side > max) max=side;
								side=gradient[wyu+xr];
								if (side > max) max=side;

								side=gradient[wy +xr];
								if (side > max) max=side;

								side=gradient[wyd+xr];
								if (side > max) max=side;
								side=gradient[wyd+x ];
								if (side > max) max=side;
								side=gradient[wyd+xl];
								if (side > max) max=side;

								side=gradient[wy +xl];
								if (side > max) max=side;

								assert(max);
								if (max==1)
									gradient[wy+x]=1;
								else
									gradient[wy+x]=max-1;
							}

							if (bi==0)
							{
								switch (ai)
								{
									case 0:
										x++;
									break;
									case 1:
										y++;
									break;
									case 2:
										x--;
									break;
									case 3:
										y--;
									break;
								}
							}
							else
							{
								switch (ai)
								{
									case 0:
										y++;
									break;
									case 1:
										x++;
									break;
									case 2:
										y--;
									break;
									case 3:
										x--;
									break;
								}
							}
						}
					}
				}
				if (down)
				{
					x++;
					y++;
				}
				else
				{
					x--;
					y--;
				}
			}
		}
	}
}


