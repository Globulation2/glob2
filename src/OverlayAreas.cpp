/*
  Copyright (C) 2007 Bradley Arsenault

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

#include "OverlayAreas.h"
#include <algorithm>
#include "Unit.h"
#include "BuildingType.h"

OverlayArea::OverlayArea()
{
	max = 0;
}
	
void OverlayArea::compute(Game& game, OverlayType ntype, int localteam)
{
	type = ntype;
	height = game.map.getH();
	width = game.map.getW();
	overlay.resize(game.map.getW() * game.map.getH());
	std::fill(overlay.begin(), overlay.end(), 0);
	max = 0;
	if(type == Starving || type == Damage)
	{
		for (int i=0; i<1024; i++)
		{
			Unit *u=game.teams[localteam]->myUnits[i];
			if (u)
			{
				if (type == Starving && u->isUnitHungry() && u->hp < u->performance[HP])
				{
					increasePoint(u->posX, u->posY);
				}
				else if(type == Starving && u->medical==Unit::MED_DAMAGED)
				{
					increasePoint(u->posX, u->posY);
				}
			}
		}
	}
	else if(type == Defence)
	{
		for (int i=0; i<1024; i++)
		{
			Building *b = game.teams[localteam]->myBuildings[i];
			if (b)
			{
				if(b->type->shootDamage > 0)
				{
					int power = (b->type->shootDamage*b->type->shootRythme) >> SHOOTING_COOLDOWN_MAGNITUDE;
					spreadPoint(b->posX, b->posY, power, b->type->shootingRange*2);
				}
			}
		}
	}
}



Uint16 OverlayArea::getValue(int x, int y)
{
	return overlay[x * height + y];
}


	
Uint16 OverlayArea::getMaximum()
{
	return max;
}



OverlayArea::OverlayType OverlayArea::getOverlayType()
{
	return type;
}



void OverlayArea::increasePoint(int x, int y)
{
	//Update the map
	for(int n=0; n<8; ++n)
	{
		for(int px=0; px<(n*2+1); ++px)
		{
			for(int py=0; py<(n*2+1); ++py)
			{
				int posx=x-n+px;
				int posy=y-n+py;

				if(posx<0)
					posx+=width;
				if(posx >= width)
					posx-=width;

				if(posy<0)
					posy+=height;
				if(posy >= height)
					posy-=height;

				overlay[posx * height + posy]+=1;
				max=std::max(max, overlay[posx * height + posy]);
			}
		}
	}
}



void OverlayArea::spreadPoint(int x, int y, int value, int distance)
{
	for (int px=x-distance; px<(x+distance); px++)
	{
		for (int py=y-distance; py<(y+distance); py++)
		{
				int dist=std::max(std::abs(px-x), std::abs(py-y));
				int targetX=px;
				int targetY=py;
				if(targetX<0)
					targetX+=width;
				if(targetX >= width)
					targetX-=width;

				if(targetY<0)
					targetY+=height;
				if(targetY >= height)
					targetY-=height;

				overlay[targetX * height + targetY]+=(value/distance)*(distance-dist);
				max=std::max(max, overlay[targetX * height + targetY] );
		}
	}
}
