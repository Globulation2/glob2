/*
  Copyright (C) 2008 Bradley Arsenault

  Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière
  for any question or comment contact us at <stephane at magnenat dot net> or <NuageBleu at gmail dot com>

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

#ifndef Gradient_h
#define Gradient_h

#include "Building.h"
#include "Map.h"
#include "BuildingType.h"

///This class is a generic gradient. Gradients are used for pathfinding, they are otherwise known as
///potential fields (pathfinding), or wavefronts (pathfinding). GradientMethod is a class that
///calculates whether a particular point on the map is an obstacle, a destination, or simply a passable
///square.
///The class GradientMethod above must have a function as such:
///Uint8 getValue(Map* map, int square) 
///This function returns 0 if the square in question is an obstacle, 1 if the square is passable,
///and 255 if the square is a destination.
template<typename Tint, typename GradientMethod> class Gradient
{
public:
	Gradient(int width, int height, const GradientMethod& method);
	~Gradient();
	
	///This algorithm computes the full gradient of a map
	void computeFullGradient(Map* map);

	///Returns the value of the gradient at a particular point
	inline Uint8 getValue(int x, int y)
	{
		return gradient[y * width + x];
	}

private:

	Uint8* gradient;
	int width;
	int height;
	GradientMethod method;
};


///This gradient method is used to prepare gradients for buildings
template<typename Tint, bool canSwim> class BuildingGradientMethod
{
public:
	BuildingGradientMethod(Building* building);

	Uint8 getValue(Map* map, Tint square);
private:
	Building* building;
};





///The functions are defined here

template<typename Tint, typename GradientMethod> Gradient<Tint, GradientMethod>::Gradient(int width, int height, const GradientMethod& method)
	: width(width), height(height), method(method)
{
	gradient = new Uint8[width*height];
}



template<typename Tint, typename GradientMethod> Gradient<Tint, GradientMethod>::~Gradient()
{
	delete gradient;
}



template<typename Tint, typename GradientMethod> void Gradient<Tint, GradientMethod>::computeFullGradient(Map* map)
{
	Tint *listedAddr = new Tint[width*height];
	size_t listCountWrite = 0;
	
	// We set the obstacle and free places
	for (size_t i=0; i<(width*height); i++)
	{
		int n = method.getValue(map, i);
		gradient[i] = n;
		if(n == 255)
			listedAddr[listCountWrite++] = i;
	}
	
	map->updateGlobalGradientVersionSimple(gradient, listedAddr, listCountWrite, Map::GT_UNDEFINED);
}

template<typename Tint, bool canSwim> BuildingGradientMethod<Tint, canSwim>::BuildingGradientMethod(Building* building)
	: building(building)
{

}


template<typename Tint, bool canSwim> Uint8 BuildingGradientMethod<Tint, canSwim>::getValue(Map* map, Tint square)
{
	int posX=building->posX;
	int posY=building->posY;
	int posW=building->type->width;
	Uint32 teamMask=building->owner->me;
	Uint16 bgid=building->gid;
	
	Tile& c=map->tiles[square];
	
	bool isWarFlag=false;
	bool isWarFlagSquare=false;
	bool isClearingFlag=false;
	bool isClearingFlagSquare=false;
	if (building->type->isVirtual)
	{
		int r=building->unitStayRange;
		int dx = (square%map->getW()) - posX;
		int dy = (square/map->getW()) - posY;
		if(building->type->zonable[WARRIOR])
		{
			isWarFlag=true;
			if(dx * dx + dy * dy < r * r)
			{
				isWarFlagSquare=true;
			}
		}
		else
		{
			isClearingFlag=true;
			if(dx * dx + dy * dy < r * r)
			{
				if(c.resource.type != NO_RES_TYPE && building->clearingResources[c.resource.type])
				{
					isClearingFlagSquare=true;
				}
			}
		}
	}
	
	if(c.building==NOGBID)
	{
		if (c.forbidden&teamMask)
			return 0;
		else if(map->immobileUnits[square] != 255)
			return 0;
		else if (c.resource.type!=NO_RES_TYPE && !isClearingFlagSquare)
			return 0;
		else if (!canSwim && map->isWater(square))
			return 0;
	}
	else
	{
		if (c.building==bgid)
		{
			return 255;
		}
		//Warflags don't consider enemy buildings an obstacle
		else if(!isWarFlag || (1<<Building::GIDtoTeam(c.building)) & (building->owner->allies))
			return 0;
	}
	
	if(isClearingFlagSquare || isWarFlagSquare)
		return 255;
	
	return 1;
}

#endif
