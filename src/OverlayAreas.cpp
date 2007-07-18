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
#include <cmath>

OverlayArea::OverlayArea()
{
	lasttype = None;
	overlaymax = 0;
	fertilitymax = 0;
	fertilityComputed = 0;
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
		for (int i=0; i<1024; i++)
		{
			Unit *u=game.teams[localteam]->myUnits[i];
			if (u)
			{
				if (type == Starving && u->isUnitHungry() && u->hp < u->performance[HP])
				{
					increasePoint(u->posX, u->posY, 8, overlay, overlaymax);
				}
				else if(type == Starving && u->medical==Unit::MED_DAMAGED)
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
		for (int i=0; i<1024; i++)
		{
			Building *b = game.teams[localteam]->myBuildings[i];
			if (b)
			{
				if(b->type->shootDamage > 0)
				{
					int power = (b->type->shootDamage*b->type->shootRythme) >> SHOOTING_COOLDOWN_MAGNITUDE;
					spreadPoint(b->posX, b->posY, power, b->type->shootingRange*2, overlay, overlaymax);
				}
			}
		}
	}
	else if(type == Fertility)
	{
		computeFertility(game, localteam);
		if(fertilityComputed == 2)
		{
			for(size_t i=0; i<fertility.size(); ++i)
			{
				overlay[i] = fertility[i];
			}
			overlaymax = fertilitymax;
			fertilityComputed = 3;
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



void OverlayArea::increasePoint(int x, int y, int distance, std::vector<Uint16>& field, Uint16& max)
{
	//Update the map
	for(int n=0; n<8; ++n)
	{
		for(int px=0; px<(n*2+1); ++px)
		{
			for(int py=0; py<(n*2+1); ++py)
			{
				int posx=(x - n + px + width) % width;
				int posy=(y - n + py + height) % height;

				field[posx * height + posy]+=1;
				max=std::max(max, field[posx * height + posy]);
			}
		}
	}
}



void OverlayArea::spreadPoint(int x, int y, int value, int distance, std::vector<Uint16>& field, Uint16& max)
{
	for (int px=x-distance; px<(x+distance); px++)
	{
		for (int py=y-distance; py<(y+distance); py++)
		{
				int dist=std::max(std::abs(px-x), std::abs(py-y));
				int targetX=(px + width) % width;
				int targetY=(py + height) % height;

				field[targetX * height + targetY]+=(value/distance)*(distance-dist);
				max=std::max(max, field[targetX * height + targetY] );
		}
	}
}



void OverlayArea::computeFertility(Game& game, int localteam)
{
	if(fertilityComputed == 0)
	{
		std::fill(overlay.begin(), overlay.end(), 0);
		overlaymax = 0;

		//Create the thread
		boost::thread thread(FertilityCalculator(fertility, fertilitymax, game, localteam, width, height, fertilityComputed));
	}
}



FertilityCalculator::FertilityCalculator(std::vector<Uint16>& fertility, Uint16& fertilityMax, Game& game, int localteam, int width, int height, int& fertilityComputed)
	: fertility(fertility), fertilitymax(fertilityMax), game(game), localteam(localteam), width(width), height(height), fertilityComputed(fertilityComputed)
{
}

void FertilityCalculator::operator()()
{
	fertilityComputed = 1;
	fertilitymax = 0;
	fertility.resize(width * height);
	std::fill(fertility.begin(), fertility.end(), 0);
	for(int x=0; x<game.map.getW(); ++x)
	{
		for(int y=0; y<game.map.getH(); ++y)
		{
			if(game.map.isGrass(x, y))
			{
				Uint8 gcorn = game.map.getGradient(localteam, CORN, false, x, y);
				Uint8 gwood = game.map.getGradient(localteam, WOOD, false, x, y);
				//if g = 1, then no path can be found. Allow drawing for values of 0
				//(obstacle) only when its wheat or corn
				if(gcorn > 1 || gwood > 1 || game.map.isRessourceTakeable(x, y, CORN) || game.map.isRessourceTakeable(x, y, WOOD))
				{
					Uint16 total=0;
					for(int nx = -15; nx <= 15; ++nx)
					{
						for(int ny = -15; ny <= 15; ++ny)
						{
							int value = (15 - std::abs(nx)) * (15 - std::abs(ny));
							//Square root fall-off, to make things more even
							if(game.map.isWater(x+nx, y+ny))
								total += int(4.2f * std::sqrt((float)value));
						}
					}
					fertilitymax = std::max(fertilitymax, total);
					fertility[x * height + y] = total;
				}
			}
		}
	}
	fertilityComputed = 2;
}

