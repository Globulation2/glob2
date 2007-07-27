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
		if(fertilityComputed > 1 && (fertilityComputed == 2 || lasttype != Fertility))
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



void OverlayArea::computeFertility(Game& game, int localteam)
{
	if(fertilityComputed == 0)
	{
		//Create the thread
		boost::thread thread(FertilityCalculator(fertility, fertilitymax, game, localteam, width, height, fertilityComputed));
	}
}



void OverlayArea::forceFertilityRecompute()
{
	fertilityComputed = 0;
}



bool OverlayArea::isFertilityBeingCalculated()
{
	return fertilityComputed == 1;
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



FertilityCalculator::FertilityCalculator(std::vector<Uint16>& fertility, Uint16& fertilityMax, Game& game, int localteam, int width, int height, int& fertilityComputed)
	: fertility(fertility), fertilitymax(fertilityMax), game(game), localteam(localteam), width(width), height(height), fertilityComputed(fertilityComputed)
{
}

void FertilityCalculator::operator()()
{
	fertilityComputed = 1;
	computeRessourcesGradient();	
	fertilitymax = 0;
	fertility.resize(width * height);
	std::fill(fertility.begin(), fertility.end(), 0);
	for(int x=0; x<game.map.getW(); ++x)
	{
		for(int y=0; y<game.map.getH(); ++y)
		{
			if(game.map.isGrass(x, y))
			{
				Uint8 dist = gradient[get_pos(x,y)];
				//if dist = 0 or 1, then no path can be found.
				if(dist > 1)
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



void FertilityCalculator::computeRessourcesGradient()
{
	gradient.resize(width*height);
	std::fill(gradient.begin(), gradient.end(),0); 

	std::queue<position> positions;
	for(int x=0; x<width; ++x)
	{
		for(int y=0; y<height; ++y)
		{
			if(game.map.isRessourceTakeable(x, y, CORN) || game.map.isRessourceTakeable(x, y, WOOD))
			{
				gradient[get_pos(x, y)]=2;
				positions.push(position(x, y));
			}
			else if(!game.map.isGrass(x, y))
				gradient[get_pos(x, y)]=1;
		}
	}
	while(!positions.empty())
	{
		position p=positions.front();
		positions.pop();

		int left=game.map.normalizeX(p.x-1);
		int right=game.map.normalizeX(p.x+1);
		int up=game.map.normalizeY(p.y-1);
		int down=game.map.normalizeY(p.y+1);
		int center_h=p.x;
		int center_y=p.y;
		int n=gradient[get_pos(center_h, center_y)];

		if(gradient[get_pos(left, up)]==0)
		{
			gradient[get_pos(left, up)]=n+1;
			positions.push(position(left, up));
		}

		if(gradient[get_pos(center_h, up)]==0)
		{
			gradient[get_pos(center_h, up)]=n+1;
			positions.push(position(center_h, up));
		}

		if(gradient[get_pos(right, up)]==0)
		{
			gradient[get_pos(right, up)]=n+1;
			positions.push(position(right, up));
		}

		if(gradient[get_pos(left, center_y)]==0)
		{
			gradient[get_pos(left, center_y)]=n+1;
			positions.push(position(left, center_y));
		}

		if(gradient[get_pos(right, center_y)]==0)
		{
			gradient[get_pos(right, center_y)]=n+1;
			positions.push(position(right, center_y));
		}

		if(gradient[get_pos(left, down)]==0)
		{
			gradient[get_pos(left, down)]=n+1;
			positions.push(position(left, down));
		}

		if(gradient[get_pos(center_h, down)]==0)
		{
			gradient[get_pos(center_h, down)]=n+1;
			positions.push(position(center_h, down));
		}

		if(gradient[get_pos(right, down)]==0)
		{
			gradient[get_pos(right, down)]=n+1;
			positions.push(position(right, down));
		}
	}
}

