/*
  Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière
  for any question or comment contact us at <stephane at magnenat dot net> or <NuageBleu at gmail dot com>

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

#ifndef DX9_BACKEND	// TODO:Die!
#include <SDL_endian.h>
#else
#include <Types.h>
#endif

#include <vector>

#include <FileManager.h>
#include <Stream.h>
#include <TextStream.h>

#include "GlobalContainer.h"
#include "Version.h"
#include "Race.h"

Race Race::defaultRace;

Race::Race()
{
}

Race::~Race()
{
	
}

void Race::loadDefault()
{
	// read datas from backend
	StreamBackend *backend = Toolkit::getFileManager()->openInputStreamBackend("data/units.txt");
	TextInputStream *stream = new TextInputStream(backend);
	delete backend;
	
	if (stream->isEndOfStream())
	{
		std::cerr << "Race::create : error, can't open file data/units.txt." << std::endl;
		delete stream;
		assert(false);
		return;
	}
	
	defaultRace.hungryness = stream->readSint32("hungryness");
	
	stream->readEnterSection("worker");
	for (int i = 0; i < NB_UNIT_LEVELS; i++)
	{
		stream->readEnterSection(i);
		defaultRace.unitTypes[0][i].load(stream, VERSION_MINOR);
		stream->readLeaveSection();
	}
	stream->readLeaveSection();
	
	stream->readEnterSection("explorer");
	for (int i = 0; i < NB_UNIT_LEVELS; i++)
	{
		stream->readEnterSection(i);
		defaultRace.unitTypes[1][i].load(stream, VERSION_MINOR);
		stream->readLeaveSection();
	}
	stream->readLeaveSection();
	
	stream->readEnterSection("warrior");
	for (int i = 0; i < NB_UNIT_LEVELS; i++)
	{
		stream->readEnterSection(i);
		defaultRace.unitTypes[2][i].load(stream, VERSION_MINOR);
		stream->readLeaveSection();
	}
	stream->readLeaveSection();
	
	delete stream;
}

Uint32 Race::checkSumDefault()
{
	return defaultRace.checkSum();
}

void Race::load()
{
	*this = defaultRace;
}

/*void Race::load()
{
	// First we load units.txt (into 6 differents usable UnitType-s).
		
	UnitType baseUnit[2];//[min, max]
	UnitType limits[2][3];//[min, max], [worker, explorer, warrior]
	UnitType baseCost;
	UnitType evolvable;
	UnitType costs[3];//[worker, explorer, warrior]
	
	TextInputStream *stream = new TextInputStream(Toolkit::getFileManager()->openInputStreamBackend("data/units.txt"));
	if (stream->isEndOfStream())
	{
		std::cerr << "Race::create : error, can't open file data/units.txt." << std::endl;
		delete stream;
		assert(false);
		return;
	}
	
	stream->readEnterSection("minUnit");
	baseUnit[0].load(stream, VERSION_MINOR);
	stream->readLeaveSection();
	
	baseUnit[1] = baseUnit[0];
	
	stream->readEnterSection("maxUnit");
	baseUnit[1].load(stream, VERSION_MINOR);
	stream->readLeaveSection();

	for (int i=0; i<2; i++)
		for (int j=0; j<3; j++)
			limits[i][j] = baseUnit[i];
	
	stream->readEnterSection("baseCost");
	baseCost.load(stream, VERSION_MINOR);
	stream->readLeaveSection();
	
	for (int j=0; j<3; j++)
		costs[j] = baseCost;
	
	stream->readEnterSection("evolvable");
	evolvable.load(stream, VERSION_MINOR);
	stream->readLeaveSection();
	
	// zzzz : change to text stream there
	// TODO : clean this

	for (int j=0; j<3; j++)
	{
		int offset = stream->getPosition();
		limits[0][j].loadText(stream);

		stream->seekFromStart(offset);
		limits[1][j].loadText(stream);
		//limits[1][j] = limits[0][j]; // TODO : seek !! zzz

		limits[1][j].loadText(stream);
		costs[j].loadText(stream);	
	}
	
	delete stream;
	
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
	
	// the hungryness is the same for all units:
	hungryness=unitTypes[0][0].hungryness;
}
*/

UnitType *Race::getUnitType(int type, int level)
{
	assert (level>=0);
	assert (level<NB_UNIT_LEVELS);
	assert (type>=0);
	assert (type<NB_UNIT_TYPE);
	return &(unitTypes[type][level]);
}

void Race::save(GAGCore::OutputStream *stream)
{
	for (int i=0; i<NB_UNIT_TYPE; i++)
		for(int j=0; j<NB_UNIT_LEVELS; j++)
			unitTypes[i][j].save(stream);
	
	stream->writeSint32(hungryness, "hungryness");
}

bool Race::load(GAGCore::InputStream *stream, Sint32 versionMinor)
{
	//printf("loading race\n");
	for (int i=0; i<NB_UNIT_TYPE; i++)
		for(int j=0; j<NB_UNIT_LEVELS; j++)
			unitTypes[i][j].load(stream, versionMinor);

	hungryness = (Sint32)stream->readSint32("hungryness");
	
	return true;
}

Uint32 Race::checkSum(void)
{
	Uint32 cs = 0;
	for (int i=0; i<NB_UNIT_TYPE; i++)
		for(int j=0; j<NB_UNIT_LEVELS; j++)
		{
			cs ^= unitTypes[i][j].checkSum();
			cs = (cs<<1) | (cs>>31);
		}
	return cs;
}
