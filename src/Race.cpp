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

#include <assert.h>

#include <vector>

#include <FileManager.h>

#include "GlobalContainer.h"
#include "Race.h"


Race::~Race()
{
	
}

void Race::create(CreationType creationType)
{
	// First we load units.txt (into 6 differents usable UnitType-s).
		
	UnitType baseUnit[2];//[min, max]
	UnitType limits[2][3];//[min, max], [worker, explorer, warrior]
	UnitType baseCost;
	UnitType evolvable;
	UnitType costs[3];//[worker, explorer, warrior]
	
	SDL_RWops *stream=globalContainer->fileManager->open("data/units.txt","rb");	
		
	baseUnit[0].loadText(stream);
	baseUnit[1]=baseUnit[0];
	baseUnit[1].loadText(stream);

	for (int i=0; i<2; i++)
		for (int j=0; j<3; j++)
			limits[i][j]=baseUnit[i];
	
	baseCost.loadText(stream);
	
	for (int j=0; j<3; j++)
		costs[j]=baseCost;
	
	evolvable.loadText(stream);

	for (int j=0; j<3; j++)
	{
		int offset=SDL_RWtell(stream);
		limits[0][j].loadText(stream);

		SDL_RWseek(stream, offset, SEEK_SET);
		limits[1][j].loadText(stream);
		//limits[1][j] = limits[0][j]; // TODO : seek !! zzz

		limits[1][j].loadText(stream);
		costs[j].loadText(stream);	
	}
	
	SDL_RWclose(stream);
	
	// Now we custom our race, by a minLevel and a maxLevel.
	UnitType choosed[2][3];
	
	// Here we should popup a big dialog window toallow the player to custom his own race.
	// The evolvable parabetter will have a range, and the other only a point.
	// But now we only create a default race.
	// Our default units will be based on the minUnit and maxUnit.
	
	for (int i=0; i<2; ++i)
	{
		for (int j=0; j<3; ++j)
		{
			choosed[i][j].copyIf(limits[i][j], evolvable); 
			choosed[i][j].copyIfNot( (limits[0][j]+limits[1][j])/2 , evolvable);
		}
	}
	
	// The weight is based on the medUnits.
	// medUnits = (minUnits+maxUnits)/2	
	UnitType middle[3];
	for (int j=0; j<3; ++j)
		middle[j]=(choosed[0][j]+choosed[1][j])/2;
	
	int weight[3];
	for (int j=0; j<3; ++j)
		weight[j]=middle[j]*costs[j];
	
	// BETA : we still don't know what's the best thing to do between:
	// 1) The 3 weight of the 3 units are separated.
	// 2) The 3 weight of the 3 unts are the sames.
	// 3) A linear interpolation of both.
	// Now we do a 50% interpolation of both.
	
	int averageWeight=0;
		for (int j=0; j<3; ++j)
			averageWeight+=weight[j];
	
	averageWeight/=3;
	for (int j=0; j<3; ++j)
	weight[j]=(averageWeight+weight[j])/2;
	
	// Here we can add any linear or non-linear function.	
	int hungry[3];
	for (int j=0; j<3; ++j)
		hungry[j]=weight[j]*1;
	
	// we calculate a linear interpolation between the choosedMin and the choosedMax,
	// to create the 4 diferents levels of units.
	UnitType unitTypes[3][4];
	for (int j=0; j<3; ++j)
	{
		for (int i=0; i<4; ++i)
		{
			unitTypes[j][i]=(choosed[0][j]*(3-i)+choosed[1][j]*(i))/3;
			unitTypes[j][i].hungryness=hungry[j];
		}
	}
	
	//we copy the hungryness to all 12 UnitType-s to the race.
	// TODO : delete his, it serves no purprose
	for (int j=0; j<3; ++j)
		for (int i=0; i<4; ++i)
			this->unitTypes[j][i]=unitTypes[j][i];
}

UnitType *Race::getUnitType(int type, int level)
{
	assert (level>=0);
	assert (level<NB_UNIT_LEVELS);
	assert (type>=0);
	assert (type<NB_UNIT_TYPE);
	return &(unitTypes[type][level]);
}

void Race::save(SDL_RWops *stream)
{
	for (int i=0; i<NB_UNIT_TYPE; i++)
		for(int j=0; j<NB_UNIT_LEVELS; j++)
			unitTypes[i][j].save(stream);
}

bool Race::load(SDL_RWops *stream)
{
	//printf("loading race\n");
	for (int i=0; i<NB_UNIT_TYPE; i++)
		for(int j=0; j<NB_UNIT_LEVELS; j++)
			unitTypes[i][j].load(stream);
	
	return true;
}

/*
NOTE : never use this, it is depreciated, replaced by create
void Race::loadText(const char *filename)
{
	SDL_RWops *stream=globalContainer->fileManager->open(filename, "r");
	
    for (int i=0; i<UnitType::NB_UNIT_TYPE; i++)
		for(int j=0; j<UnitType::NB_UNIT_LEVELS; j++)
		{
			unitTypes[i][j].init();
			unitTypes[i][j].loadText(stream);
        }
	SDL_RWclose(stream);
}
*/
