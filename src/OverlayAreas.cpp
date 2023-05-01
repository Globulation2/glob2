/*
  Copyright (C) 2007 Bradley Arsenault

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

#include "OverlayAreas.h"
#include <algorithm>
#include "Unit.h"
#include "BuildingType.h"
#include <cmath>
#include <boost/thread/thread.hpp>
#include "Game.h"
#include "Bullet.h"

OverlayArea::OverlayArea()
{
	lastType = None;
	overlayMax = 0;
//	fertilityMax = 0;
//	fertilityComputed = 0;
}


OverlayArea::~OverlayArea()
{

}


void OverlayArea::compute(Game& game, OverlayType type, int localTeam)
{
	this->type = type;
	height = game.map.getH();
	width = game.map.getW();
	overlay.resize(game.map.getW() * game.map.getH());
	if(type == Starving || type == Damage)
	{
		std::fill(overlay.begin(), overlay.end(), 0);
		overlayMax = 0;
		for (int i=0; i<Unit::MAX_COUNT; i++)
		{
			Unit *u=game.teams[localTeam]->myUnits[i];
			if (u && u->activity != Unit::ACT_UPGRADING)
			{
				if (type == Starving && u->isUnitHungry() && u->hp < u->performance[HP])
				{
					increasePoint(u->posX, u->posY, 8, overlay, overlayMax);
				}
				else if(type == Damage && u->medical==Unit::MED_DAMAGED)
				{
					increasePoint(u->posX, u->posY, 8, overlay, overlayMax);
				}
			}
		}
	}
	else if(type == Defence)
	{
		std::fill(overlay.begin(), overlay.end(), 0);
		overlayMax = 0;
		for (int i=0; i<Building::MAX_COUNT; i++)
		{
			Building *b = game.teams[localTeam]->myBuildings[i];
			if (b)
			{
				if(b->type->shootDamage > 0)
				{
					int power = (b->type->shootDamage*b->type->shootRhythm) >> SHOOTING_COOLDOWN_MAGNITUDE;
					spreadPoint(b->posX, b->posY, power, b->type->shootingRange, overlay, overlayMax);
				}
			}


		}
	}
	else if(type == Fertility && lastType != Fertility)
	{
		for(int x=0; x<game.map.getW(); ++x)
		{
			for(int y=0; y<game.map.getH(); ++y)
			{
				overlay[x * height + y] = game.map.getTile(x, y).fertility;
				overlayMax = game.map.fertilityMaximum;
			}
		}
	}
	lastType = type;
}



Uint16 OverlayArea::getValue(int x, int y)
{
	return overlay[x * height + y];
}


	
Uint16 OverlayArea::getMaximum()
{
	return overlayMax;
}



OverlayArea::OverlayType OverlayArea::getOverlayType()
{
	return type;
}



void OverlayArea::forceRecompute()
{
	lastType = None;
}



void OverlayArea::increasePoint(int x, int y, int distance, std::vector<Uint16>& field, Uint16& max)
{
	//Update the map
	for(int px=0; px<(distance*2+1); ++px)
	{
		for(int py=0; py<(distance*2+1); ++py)
		{
			int relX = (px-distance);
			int relY = (py-distance);
			if(relX*relX + relY*relY < distance*distance)
			{
				int posX=(x - distance + px + width) % width;
				int posY=(y - distance + py + height) % height;

				field[posX * height + posY]+=distance - (relX*relX + relY*relY) / distance;
				max=std::max(max, field[posX * height + posY]);
			}
		}
	}
}



void OverlayArea::spreadPoint(int x, int y, int value, int distance, std::vector<Uint16>& field, Uint16& max)
{
	for (int px=x-distance-1; px<(x+distance+1); px++)
	{
		for (int py=y-distance-1; py<(y+distance+1); py++)
		{
			int relX = (px-x);
			int relY = (py-y);
			if((relX*relX + relY*relY) <= (distance*distance))
			{
				int targetX=(px + width) % width;
				int targetY=(py + height) % height;

				field[targetX * height + targetY]+=distance - (relX*relX + relY*relY) / distance;
				max=std::max(max, field[targetX * height + targetY] );
			}
		}
	}
}


