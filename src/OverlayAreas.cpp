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
	lasttype = None;
	overlaymax = 0;
//	fertilitymax = 0;
//	fertilityComputed = 0;
}


OverlayArea::~OverlayArea()
{

}


void OverlayArea::compute(Game& game, OverlayType ntype, int localteam)
{
	type = ntype;
	height = game.map.getH();
	width = game.map.getW();
	overlay.resize(game.map.getW() * game.map.getH());
	if(type == Starving || type == Damage)
	{
		std::fill(overlay.begin(), overlay.end(), 0);
		overlaymax = 0;
		for (int i=0; i<Unit::MAX_COUNT; i++)
		{
			Unit *u=game.teams[localteam]->myUnits[i];
			if (u && u->activity != Unit::ACT_UPGRADING)
			{
				if (type == Starving && u->isUnitHungry() && u->hp < u->performance[HP])
				{
					increasePoint(u->posX, u->posY, 8, overlay, overlaymax);
				}
				else if(type == Damage && u->medical==Unit::MED_DAMAGED)
				{
					increasePoint(u->posX, u->posY, 8, overlay, overlaymax);
				}
			}
		}
	}
	else if(type == Defence)
	{
		std::fill(overlay.begin(), overlay.end(), 0);
		overlaymax = 0;
		for (int i=0; i<Building::MAX_COUNT; i++)
		{
			Building *b = game.teams[localteam]->myBuildings[i];
			if (b)
			{
				if(b->type->shootDamage > 0)
				{
					int power = (b->type->shootDamage*b->type->shootRythme) >> SHOOTING_COOLDOWN_MAGNITUDE;
					spreadPoint(b->posX, b->posY, power, b->type->shootingRange, overlay, overlaymax);
				}
			}


		}
	}
	else if(type == Fertility && lasttype != Fertility)
	{
		for(int x=0; x<game.map.getW(); ++x)
		{
			for(int y=0; y<game.map.getH(); ++y)
			{
				overlay[x * height + y] = game.map.getCase(x, y).fertility;
				overlaymax = game.map.fertilityMaximum;
			}
		}
	}
	lasttype = type;
}



Uint16 OverlayArea::getValue(int x, int y)
{
	return overlay[x * height + y];
}



Uint16 OverlayArea::getMaximum()
{
	return overlaymax;
}



OverlayArea::OverlayType OverlayArea::getOverlayType()
{
	return type;
}



void OverlayArea::forceRecompute()
{
	lasttype = None;
}



void OverlayArea::increasePoint(int x, int y, int distance, std::vector<Uint16>& field, Uint16& max)
{
	//Update the map
	for(int px=0; px<(distance*2+1); ++px)
	{
		for(int py=0; py<(distance*2+1); ++py)
		{
			int relx = (px-distance);
			int rely = (py-distance);
			if(relx*relx + rely*rely < distance*distance)
			{
				int posx=(x - distance + px + width) % width;
				int posy=(y - distance + py + height) % height;

				field[posx * height + posy]+=distance - (relx*relx + rely*rely) / distance;
				max=std::max(max, field[posx * height + posy]);
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
			int relx = (px-x);
			int rely = (py-y);
			if((relx*relx + rely*rely) <= (distance*distance))
			{
				int targetX=(px + width) % width;
				int targetY=(py + height) % height;

				field[targetX * height + targetY]+=distance - (relx*relx + rely*rely) / distance;
				max=std::max(max, field[targetX * height + targetY] );
			}
		}
	}
}


