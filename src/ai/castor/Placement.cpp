// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#include <StringTable.h>
#include <SupportFunctions.h>
#include <Toolkit.h>
#include <Stream.h>

#include "AICastor.h"
#include "Game.h"
#include "GlobalContainer.h"
#include "Order.h"
#include "Player.h"
#include "Unit.h"
#include "Utilities.h"

#define AI_FILE_MIN_VERSION 1
#define AI_FILE_VERSION 2

using std::shared_ptr;

std::shared_ptr<Order>AICastor::findGoodBuilding(Sint32 typeNum, bool food, bool defense, bool critical)
{
	int w=map->w;
	int h=map->h;
	int bw=globalContainer->buildingsTypes.get(typeNum)->width;
	int bh=globalContainer->buildingsTypes.get(typeNum)->height;
	assert(bw==bh);
	//int hDec=map->hDec;
	int wDec=map->wDec;
	int wMask=map->wMask;
	int hMask=map->hMask;
	size_t size=w*h;
	Uint32 *mapDiscovered=&(map->mapDiscovered[0]);
	Uint32 me=team->me;

	// minWork computation:
	Sint32 bestWorkScore=2;
	for (size_t i=0; i<size; i++)
	{
		if ((mapDiscovered[i]&me)==0)
			continue;
		Uint8 work=workAbilityMap[i];
		if (bestWorkScore<work)
			bestWorkScore=work;
	}
	Sint32 minWork=bestWorkScore*2;
	if (critical)
	{
		if (minWork>15*4)
			minWork=15*4;
	}
	else
	{
		if (minWork>30*4)
			minWork=30*4;
	}

	// wheatGradientLimit computation:
	Uint32 wheatGradientLimit;
	if (food)
	{
		if (critical)
			wheatGradientLimit=(255-16)*4;
		else
			wheatGradientLimit=(255-4)*4;
	}
	else
	{
		if (critical)
			wheatGradientLimit=(255-5)*4;
		else
			wheatGradientLimit=(255-7)*4;
	}

	// we find the best place possible:
	size_t bestIndex=0;
	Sint32 bestScore=0;
	
	//wheatLimit=(wheatLimit<<2);
	//printf(" (scaled) minWork=%d, wheatLimit=%d\n", minWork, wheatLimit);
	
	Uint8 *wheatGradientMap=map->ressourcesGradient[team->teamNumber][CORN][canSwim];
	memset(goodBuildingMap, 0, size);
	
	for (int y=0; y<h; y++)
		for (int x=0; x<w; x++)
		{
			size_t corner0=(x|(y<<wDec));
			size_t corner1=(((x+bw-1)&wMask)|(y<<wDec));
			size_t corner2=(x|(((y+bw-1)&hMask)<<wDec));
			size_t corner3=(((x+bw-1)&wMask)|(((y+bw-1)&hMask)<<wDec));
			
			if (critical
				&& (mapDiscovered[corner0]&me)==0
				&& (mapDiscovered[corner1]&me)==0
				&& (mapDiscovered[corner2]&me)==0
				&& (mapDiscovered[corner3]&me)==0)
				continue;
			//goodBuildingMap[corner0]=1;
			
			Uint8 space=spaceForBuildingMap[corner0];
			if (space<bw)
				continue;
			//goodBuildingMap[corner0]=2;
			
			Sint32 work=workAbilityMap[corner0]+workAbilityMap[corner1]+workAbilityMap[corner2]+workAbilityMap[corner3];
			if (work<minWork)
				continue;
			//goodBuildingMap[corner0]=3;
			
			Uint32 wheatGradient=wheatGradientMap[corner0]+wheatGradientMap[corner1]+wheatGradientMap[corner2]+wheatGradientMap[corner3];
			if (!defense)
			{
				if (food)
				{
					if (wheatGradient<wheatGradientLimit)
						continue;
					//if (wheatGrowth<wheatGrowthLimit)
					//	continue;
				}
				else
				{
					if (wheatGradient>wheatGradientLimit)
						continue;
					//if (wheatGrowth>wheatGrowthLimit)
					//	continue;
				}
			}
			//goodBuildingMap[corner0]=4;
			
			Uint32 enemyRange=enemyRangeMap[corner0]+enemyRangeMap[corner1]+enemyRangeMap[corner2]+enemyRangeMap[corner3];
			if (enemyRange>4*(255-8))
				continue;
			//goodBuildingMap[corner0]=5;
			
			Sint32 wheatGrowth=wheatGrowthMap[corner0]+wheatGrowthMap[corner1]+wheatGrowthMap[corner2]+wheatGrowthMap[corner3];
			
			Uint8 neighbour=buildingNeighbourMap[corner0];
			Uint8 directNeighboursCount=(neighbour>>1)&7; // [0, 7]
			Uint8 farNeighboursCount=(neighbour>>5)&7; // [0, 7]
			if ((neighbour&1)||(directNeighboursCount>1))
				continue;
			
			//goodBuildingMap[corner0]=6;
			
			Sint32 score;
			if (defense)
				score=((work<<1)+wheatGradient+(enemyRange<<4))*(16+(directNeighboursCount<<2)+farNeighboursCount);
			else if (food)
				score=((wheatGrowth<<8)+work+(wheatGradient>>1)-enemyRange)*(8+(directNeighboursCount<<2)+farNeighboursCount);
			else
				score=(4096+work-(wheatGrowth<<8)-enemyRange)*(8+(directNeighboursCount<<2)+farNeighboursCount);
			
			if (defense)
			{
				if (score<0)
					goodBuildingMap[corner0]=0;
				else if ((score>>12)>=255)
					goodBuildingMap[corner0]=255;
				else
					goodBuildingMap[corner0]=(score>>12);
			}
			
			if (bestScore<score)
			{
				bestScore=score;
				bestIndex=corner0;
			}
		}
	
	if (bestScore>0)
	{
		Sint32 x=(bestIndex&map->wMask);
		Sint32 y=((bestIndex>>map->wDec)&map->hMask);
		return shared_ptr<Order>(new OrderCreate(team->teamNumber, x, y, typeNum, 1, 1));
	}
	
	return shared_ptr<Order>();
}

void AICastor::computeRessourcesCluster()
{
	int w=map->w;
	int h=map->h;
	//int wMask=map->wMask;
	int hMask=map->hMask;
	size_t size=w*h;
	
	memset(ressourcesCluster, 0, size*2);
	
	//int i=0;
	Uint8 old=0xFF;
	Uint16 id=0;
	bool usedid[65536];
	memset(usedid, 0, 65536*sizeof(bool));
	for (int y=0; y<h; y++)
	{
		for (int x=0; x<w; x++)
		{
			const auto& c = map->cases[map->coordToIndex(x, y)]; // case
			const auto& r=c.ressource; // ressource
			Uint8 rt=r.type; // ressources type
			
			int rci=x+y*w; // ressource cluster index
			Uint16 *rcp=&ressourcesCluster[rci]; // ressource cluster pointer
			Uint16 rc=*rcp; // ressource cluster
			
			if (rt==0xFF)
			{
				*rcp=0;
				old=0xFF;
			}
			else
			{
				if (rt!=old)
				{
					id=1;
					while (usedid[id])
						id++;
					if (id)
						usedid[id]=true;
					old=rt;
				}
				if (rc!=id)
				{
					if (rc==0)
					{
						*rcp=id;
					}
					else
					{
						Uint16 oldid=id;
						usedid[oldid]=false;
						id=rc; // newid
						// We have to correct last ressourcesCluster values:
						*rcp=id;
						while (*rcp==oldid)
						{
							*rcp=id;
							rcp--;
						}
					}
				}
			}
		}
		memcpy(ressourcesCluster+((y+1)&hMask)*w, ressourcesCluster+y*w, w*2);
	}
	
}

void AICastor::updateGlobalGradientNoObstacle(Uint8 *gradient)
{
	//In this algotithm, "l" stands for one case at Left, "r" for one case at Right, "u" for Up, and "d" for Down.
	// Warning, this is *nearly* a copy-past, 4 times, once for each direction.
	int w=map->w;
	int h=map->h;
	int hMask=map->hMask;
	int wMask=map->wMask;
	//int hDec=map->hDec;
	int wDec=map->wDec;
	
	for (int yi=0; yi<h; yi++)
	{
		int wy=((yi&hMask)<<wDec);
		int wyu=(((yi-1)&hMask)<<wDec);
		for (int xi=yi; xi<(yi+w); xi++)
		{
			int x=xi&wMask;
			Uint8 max=gradient[wy+x];
			if (max!=16)
			{
				int xl=(x-1)&wMask;
				int xr=(x+1)&wMask;

				Uint8 side[4];
				side[0]=gradient[wyu+xl];
				side[1]=gradient[wyu+x ];
				side[2]=gradient[wyu+xr];
				side[3]=gradient[wy +xl];
				max++;

				for (int i=0; i<4; i++)
					if (side[i]>max)
						max=side[i];
				if (max==0)
					gradient[wy+x]=0;
				else
					gradient[wy+x]=max-1;
			}
		}
	}

	for (int y=hMask; y>=0; y--)
	{
		int wy=(y<<wDec);
		int wyd=(((y+1)&hMask)<<wDec);
		for (int xi=y; xi<(y+w); xi++)
		{
			int x=xi&wMask;
			Uint8 max=gradient[wy+x];
			if (max!=16)
			{
				int xl=(x-1)&wMask;
				int xr=(x+1)&wMask;

				Uint8 side[4];
				side[0]=gradient[wyd+xr];
				side[1]=gradient[wyd+x ];
				side[2]=gradient[wyd+xl];
				side[3]=gradient[wy +xl];
				max++;

				for (int i=0; i<4; i++)
					if (side[i]>max)
						max=side[i];
				if (max==0)
					gradient[wy+x]=0;
				else
					gradient[wy+x]=max-1;
			}
		}
	}

	for (int x=0; x<w; x++)
	{
		int xl=(x-1)&wMask;
		for (int yi=x; yi<(x+h); yi++)
		{
			int wy=((yi&hMask)<<wDec);
			int wyu=(((yi-1)&hMask)<<wDec);
			int wyd=(((yi+1)&hMask)<<wDec);
			Uint8 max=gradient[wy+x];
			if (max!=16)
			{
				Uint8 side[4];
				side[0]=gradient[wyu+xl];
				side[1]=gradient[wyd+xl];
				side[2]=gradient[wy +xl];
				side[3]=gradient[wyu+x ];
				max++;

				for (int i=0; i<4; i++)
					if (side[i]>max)
						max=side[i];
				if (max==0)
					gradient[wy+x]=0;
				else
					gradient[wy+x]=max-1;
			}
		}
	}

	for (int x=wMask; x>=0; x--)
	{
		int xr=(x+1)&wMask;
		for (int yi=x; yi<(x+h); yi++)
		{
			int wy=((yi&hMask)<<wDec);
			int wyu=(((yi-1)&hMask)<<wDec);
			int wyd=(((yi+1)&hMask)<<wDec);
			Uint8 max=gradient[wy+x];
			if (max!=16)
			{
				Uint8 side[4];
				side[0]=gradient[wyu+xr];
				side[1]=gradient[wy +xr];
				side[2]=gradient[wyd+xr];
				side[3]=gradient[wyu+x ];
				max++;

				for (int i=0; i<4; i++)
					if (side[i]>max)
						max=side[i];
				if (max==0)
					gradient[wy+x]=0;
				else
					gradient[wy+x]=max-1;
			}
		}
	}
}

void AICastor::updateGlobalGradient(Uint8 *gradient)
{
	//In this algotithm, "l" stands for one case at Left, "r" for one case at Right, "u" for Up, and "d" for Down.
	// Warning, this is *nearly* a copy-past, 4 times, once for each direction.
	
	int w=map->w;
	int h=map->h;
	int hMask=map->hMask;
	int wMask=map->wMask;
	//int hDec=map->hDec;
	int wDec=map->wDec;
	
	for (int yi=0; yi<h; yi++)
	{
		int wy=((yi&hMask)<<wDec);
		int wyu=(((yi-1)&hMask)<<wDec);
		for (int xi=yi; xi<(yi+w); xi++)
		{
			int x=xi&wMask;
			Uint8 max=gradient[wy+x];
			if (max && max!=255)
			{
				int xl=(x-1)&wMask;
				int xr=(x+1)&wMask;

				Uint8 side[4];
				side[0]=gradient[wyu+xl];
				side[1]=gradient[wyu+x ];
				side[2]=gradient[wyu+xr];
				side[3]=gradient[wy +xl];
				max++;

				for (int i=0; i<4; i++)
					if (side[i]>max)
						max=side[i];
				if (max==1)
					gradient[wy+x]=1;
				else
					gradient[wy+x]=max-1;
			}
		}
	}
	
	for (int y=hMask; y>=0; y--)
	{
		int wy=(y<<wDec);
		int wyd=(((y+1)&hMask)<<wDec);
		for (int xi=y; xi<(y+w); xi++)
		{
			int x=xi&wMask;
			Uint8 max=gradient[wy+x];
			if (max && max!=255)
			{
				int xl=(x-1)&wMask;
				int xr=(x+1)&wMask;

				Uint8 side[4];
				side[0]=gradient[wyd+xr];
				side[1]=gradient[wyd+x ];
				side[2]=gradient[wyd+xl];
				side[3]=gradient[wy +xl];
				max++;

				for (int i=0; i<4; i++)
					if (side[i]>max)
						max=side[i];
				if (max==1)
					gradient[wy+x]=1;
				else
					gradient[wy+x]=max-1;
			}
		}
	}
	
	for (int x=0; x<w; x++)
	{
		int xl=(x-1)&wMask;
		for (int yi=x; yi<(x+h); yi++)
		{
			int wy=((yi&hMask)<<wDec);
			int wyu=(((yi-1)&hMask)<<wDec);
			int wyd=(((yi+1)&hMask)<<wDec);
			Uint8 max=gradient[wy+x];
			if (max && max!=255)
			{
				Uint8 side[4];
				side[0]=gradient[wyu+xl];
				side[1]=gradient[wyd+xl];
				side[2]=gradient[wy +xl];
				side[3]=gradient[wyu+x ];
				max++;

				for (int i=0; i<4; i++)
					if (side[i]>max)
						max=side[i];
				if (max==1)
					gradient[wy+x]=1;
				else
					gradient[wy+x]=max-1;
			}
		}
	}

	for (int x=wMask; x>=0; x--)
	{
		int xr=(x+1)&wMask;
		for (int yi=x; yi<(x+h); yi++)
		{
			int wy=((yi&hMask)<<wDec);
			int wyu=(((yi-1)&hMask)<<wDec);
			int wyd=(((yi+1)&hMask)<<wDec);
			Uint8 max=gradient[wy+x];
			if (max && max!=255)
			{
				Uint8 side[4];
				side[0]=gradient[wyu+xr];
				side[1]=gradient[wy +xr];
				side[2]=gradient[wyd+xr];
				side[3]=gradient[wyu+x ];
				max++;

				for (int i=0; i<4; i++)
					if (side[i]>max)
						max=side[i];
				if (max==1)
					gradient[wy+x]=1;
				else
					gradient[wy+x]=max-1;
			}
		}
	}
}
