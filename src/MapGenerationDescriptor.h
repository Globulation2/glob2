/*
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

#ifndef __MAP_GENERATION_DESCRIPTOR_H
#define __MAP_GENERATION_DESCRIPTOR_H

#include "Ressource.h"
#include "TerrainType.h"
#include "Team.h"

namespace GAGCore
{
	class InputStream;
	class OutputStream;
}

class MapGenerationDescriptor
{
public:
	MapGenerationDescriptor();
	virtual ~MapGenerationDescriptor(void);
	
	Uint8 *getData();
	bool setData(const Uint8 *data, int dataLength);
	int getDataLength() {return DATA_SIZE; }
	
	void save(GAGCore::OutputStream *stream);
	bool load(GAGCore::InputStream *stream, Sint32 versionMinor);
	Uint32 checkSum();

public:
	TerrainType terrainType;
	enum Methode
	{
		eNONE=-1,
		eUNIFORM=0,
		eSWAMP=1,
		eRIVER=2,
		eISLANDS=3,
		eCRATERLAKES=4,
		eCONCRETEISLANDS=5,
		eISLES=6,
		eOLDRANDOM=7,
		eOLDISLANDS=8
	};

	Methode methode;
	
	Sint32 wDec, hDec;
	
	Sint32 waterRatio, sandRatio, grassRatio, desertRatio, wheatRatio,
		woodRatio, fruitRatio, algaeRatio, stoneRatio, riverDiameter, craterDensity, extraIslands;
	Sint32 oldIslandSize, oldBeach;
	Sint32 smooth;
	Sint32 ressource[MAX_NB_RESSOURCES];
	///n=2^n-times the same landscape. So 0=all random.
	Uint32 logRepeatAreaTimes;

	Sint32 nbTeams, nbWorkers;
public:
	// Thoses may not be in data
	Sint32 bootX[Team::MAX_COUNT];
	Sint32 bootY[Team::MAX_COUNT];
public:
	enum {DATA_SIZE=100+MAX_NB_RESSOURCES*4};
protected:
	//! Serialized form of MapGenerationDescriptor
	Uint8 data[DATA_SIZE];
};


#endif
