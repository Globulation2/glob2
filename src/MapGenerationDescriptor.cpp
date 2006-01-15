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
#include <Stream.h>

#include "MapGenerationDescriptor.h"
#include "Marshaling.h"
#include "Utilities.h"

MapGenerationDescriptor::MapGenerationDescriptor()
{
	wDec=7;
	hDec=7;
	
	terrainType=GRASS;
	
	methode=eUNIFORM;
	waterRatio=50;
	sandRatio=50;
	grassRatio=50;
	desertRatio=50;
	wheatRatio=50;
	woodRatio=50;
	stoneRatio=50;
	algaeRatio=50;
	riverDiameter=50;
	craterDensity=50;
	extraIslands=0;
	smooth=4;
	
//	islandsSize=50;
//	beach=1;
	for (int i=0; i<MAX_NB_RESSOURCES; i++)
		ressource[i]=7;
	
	nbWorkers=4;
	nbTeams=4;
	
	randa=randb=randc=0;
}


MapGenerationDescriptor::~MapGenerationDescriptor()
{
	// Tuut-tuut bom-bom
}

Uint8 *MapGenerationDescriptor::getData()
{
	assert(DATA_SIZE==60+MAX_NB_RESSOURCES*4);

	addSint32(data, wDec, 0);
	addSint32(data, hDec, 4);

	addSint32(data, (Sint32)terrainType, 8);

	addSint32(data, (Sint32)methode, 12);
	addSint32(data, waterRatio, 16);
	addSint32(data, sandRatio, 20);
	addSint32(data, grassRatio, 24);
	addSint32(data, smooth, 28);

//	addSint32(data, islandsSize, 32);
//	addSint32(data, beach, 36);

	addSint32(data, nbWorkers, 40);
	addSint32(data, nbTeams, 44);

	addUint32(data, randa, 48);
	addUint32(data, randb, 52);
	addUint32(data, randc, 56);

	for (unsigned i=0; i<MAX_NB_RESSOURCES; i++)
		addSint32(data, ressource[i], 60+i*4);

	return data;
}

bool MapGenerationDescriptor::setData(const Uint8 *data, int dataLength)
{
	assert(DATA_SIZE==60+MAX_NB_RESSOURCES*4);
	assert(getDataLength()==DATA_SIZE);
	assert(getDataLength()==dataLength);
	
	wDec=getSint32(data, 0);
	hDec=getSint32(data, 4);
	
	terrainType=(TerrainType)getSint32(data, 8);

	methode=(Methode)getSint32(data, 12);
	waterRatio=getSint32(data, 16);
	sandRatio=getSint32(data, 20);
	grassRatio=getSint32(data, 24);
	smooth=getSint32(data, 28);
	
//	islandsSize=getSint32(data, 32);
//	beach=getSint32(data, 36);

	nbWorkers=getSint32(data, 40);
	nbTeams=getSint32(data, 44);

	randa=getSint32(data, 48);
	randb=getSint32(data, 52);
	randc=getSint32(data, 56);

	for (unsigned i=0; i<MAX_NB_RESSOURCES; i++)
		ressource[i]=getSint32(data, 60+i*4);

	bool good=true;
	if (getDataLength()!=dataLength)
		good=false;
	if (wDec>=32)
		good=false;
	if (hDec>=32)
		good=false;
	if (terrainType>GRASS)
		good=false;
	
	return (good);
}

void MapGenerationDescriptor::save(GAGCore::OutputStream *stream)
{
	stream->writeEnterSection("MapGenerationDescriptor");
	stream->write("MgdB", 4, "signatureStart");
	stream->write(getData(), DATA_SIZE, "data");
	stream->write("MgdE", 4, "signatureEnd");
	stream->writeLeaveSection();
}

bool MapGenerationDescriptor::load(GAGCore::InputStream *stream)
{
	stream->readEnterSection("MapGenerationDescriptor");
	char signature[4];
	stream->read(signature, 4, "signatureStart");
	if (memcmp(signature, "MgdB", 4) != 0)
	{
		stream->readLeaveSection();
		return false;
	}
	stream->read(getData(), DATA_SIZE, "data");
	setData(data, DATA_SIZE);
	stream->read(signature, 4, "signatureEnd");
	stream->readLeaveSection();
	if (memcmp(signature, "MgdE", 4) != 0)
		return false;
	return true;
}

Uint32 MapGenerationDescriptor::checkSum()
{
	Uint32 cs=0;
	
	cs^=wDec+(hDec<<16);
	cs^=(Sint32)terrainType;
	
	cs=(cs<<31)|(cs>>1);
	cs^=(Sint32)methode;
	cs+=waterRatio<<7;
	cs+=sandRatio<<14;
	cs+=grassRatio<<21;
	cs+=smooth<<27;
	
	cs=(cs<<31)|(cs>>1);
//	cs^=islandsSize;
//	cs^=beach;

	for (unsigned i=0; i<MAX_NB_RESSOURCES; i++)
		cs+=ressource[i]<<(3*i);

	cs=(cs<<31)|(cs>>1);
	cs^=nbWorkers;
	cs^=nbTeams<<5;

	cs=(cs<<31)|(cs>>1);
	cs^=randa%randb;
	cs^=randb%randc;
	cs^=randc%randa;
	
	return cs;
}

void MapGenerationDescriptor::saveSyncronization(void)
{
	randa=getSyncRandSeedA();
	randb=getSyncRandSeedB();
	randc=getSyncRandSeedC();
}

void MapGenerationDescriptor::loadSyncronization(void)
{
	setSyncRandSeedA(randa);
	setSyncRandSeedB(randb);
	setSyncRandSeedC(randc);
}

void MapGenerationDescriptor::synchronizeNow(void)
{
	if (randa|randb|randc)
		loadSyncronization();
	else
		saveSyncronization();
}
