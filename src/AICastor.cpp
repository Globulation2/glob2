/*
  Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière
  for any question or comment contact us at nct@ysagoon.com or nuage@ysagoon.com

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 2 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
*/

#include "AICastor.h"
#include "Player.h"
#include "Unit.h"
#include "Utilities.h"
#include "Game.h"
#include "GlobalContainer.h"
#include "Order.h"

#define AI_FILE_MIN_VERSION 1
#define AI_FILE_VERSION 2

inline static void dxdyfromDirection(int direction, int *dx, int *dy)
{
const int tab[9][2]={	{ -1, -1},
						{ 0, -1},
						{ 1, -1},
						{ 1, 0},
						{ 1, 1},
						{ 0, 1},
						{ -1, 1},
						{ -1, 0},
						{ 0, 0} };
assert(direction>=0);
assert(direction<=8);
*dx=tab[direction][0];
*dy=tab[direction][1];
}

AICastor::AICastor(Player *player)
{
	init(player);
}

AICastor::AICastor(SDL_RWops *stream, Player *player, Sint32 versionMinor)
{
	init(player);
	bool goodLoad=load(stream, versionMinor);
	assert(goodLoad);
}

void AICastor::init(Player *player)
{
	assert(player);
	
	// Logical :
	timer=0;

	// Structural:
	this->player=player;
	this->team=player->team;
	this->game=player->game;
	this->map=player->map;

	assert(this->team);
	assert(this->game);
	assert(this->map);
}

AICastor::~AICastor()
{
}

bool AICastor::load(SDL_RWops *stream, Sint32 versionMinor)
{
	assert(game);
	if (versionMinor<29)
	{
		//TODO:init
		return true;
	}
	
	Sint32 aiFileVersion=SDL_ReadBE32(stream);
	if (aiFileVersion<AI_FILE_MIN_VERSION)
		return false;
	if (aiFileVersion>=1)
		timer=SDL_ReadBE32(stream);
	else
		timer=0;
	
	return true;
}

void AICastor::save(SDL_RWops *stream)
{
	SDL_WriteBE32(stream, AI_FILE_VERSION);
	SDL_WriteBE32(stream, timer);
}


Order *AICastor::getOrder(void)
{
	timer++;
	
	// We compute simples state vars:
	
	bool bootTime=(timer<25*60);
	bool lateTime=(timer>25*60*10);
	
	int swarms=0;
	Building **myBuildings=team->myBuildings;
	for (int i=0; i<1024; i++)
	{
		Building *b=myBuildings[i];
		if (b!=NULL && b->type->typeNum==BuildingType::SWARM_BUILDING)
			swarms++;
	}
	
	int workers=0;
	int explorers=0;
	int warriors=0;
	Unit **myUnits=team->myUnits;
	for (int i=0; i<1024; i++)
	{
		Unit *u=myUnits[i];
		if (u)
		{
			if (u->typeNum==WORKER)
				workers++;
			else if (u->typeNum==EXPLORER)
				explorers++;
			else if (u->typeNum==WARRIOR)
				warriors++;
		}
	}
	
	if (swarms==0)
	{
		if (bootTime)
			return getStartSwarmOrder();
	}
	
	
	
	return new NullOrder();
}

Order *AICastor::getStartSwarmOrder(void)
{
	
}

int AICastor::getFoodPlace(int x, int y, int *fx, int *fy, int *score)
{
	int bestScore=0;
	int bestScoreDirection=-1;
	int bestX=0;
	int bestY=0;
	for (int d=0; d<8; d++)
	{
		int dx, dy;
		dxdyfromDirection(d, &dx, &dy);
		int px=x;
		int py=y;
		int state=0;
		int sand=0;
		int water=0;
		int corn=0;
		int continuousCorn=0;
		bool isFood=false;
		int ffx=x; // first food
		int ffy=y;
		int lfx=x;
		int lfy=y;
		for (int r=0; r<32; r++)
		{
			bool wasFood=false;
			int tt=map->getTerrainType(px, py);
			if (tt==SAND)
				sand++;
			else if (tt==WATER)
				water++;
			else
			{
				Ressource r=map->getRessource(px, py);
				if (r.type==CORN)
				{
					if (!isFood)
					{
						ffx=px;
						ffy=py;
						isFood=true;
					}
					lfx=px;
					lfy=py;
					corn++;
					if (wasFood)
						continuousCorn++;
					wasFood=true;
				}
			}
			px+=dx;
			py+=dy;
		}
		int width=1;
		if (isFood)
		{
			int pdx=dy;
			int pdy=-dx;
			int wpx=(ffx-lfx)/2;
			int wpy=(ffy-lfy)/2;
			for (int r=0; r<32; r++)
			{
				wpx+=pdx;
				wpy+=pdy;
				Ressource r=map->getRessource(wpx, wpy);
				if (r.type==CORN)
					width++;
				else
					break;
			}
			pdx=-pdx;
			pdy=-pdy;
			wpx=(ffx-lfx)/2;
			wpy=(ffy-lfy)/2;
			for (int r=0; r<32; r++)
			{
				wpx+=pdx;
				wpy+=pdy;
				Ressource r=map->getRessource(wpx, wpy);
				if (r.type==CORN)
					width++;
				else
					break;
			}
		}
		
		int score=((continuousCorn+corn)*width*water)/(1+sand);
		printf("score[%d]=%d\n", d, score);
		if (score>bestScore)
		{
			score=bestScore;
			bestScoreDirection=d;
			bestX=ffx;
			bestY=ffy;
		}
	}
	if (bestScoreDirection==-1)
	{
		*fx=x;
		*fy=y;
		*score=0;
	}
	else
	{
		*fx=bestX;
		*fy=bestY;
		*score=bestScore;
	}
}










