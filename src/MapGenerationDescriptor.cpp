/*
    Copyright (C) 2001, 2002 Stephane Magnenat & Luc-Olivier de Charriere
    for any question or comment contact us at nct@ysagoon.com

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA

*/

#include "MapGenerationDescriptor.h"
#include "Utilities.h"

MapGenerationDescriptor::MapGenerationDescriptor()
{
	wDec=7;
	hDec=7;
	
	terrainType=Map::GRASS;
	
	methode=eUNIFORM;
	waterRatio=50;
	sandRatio=50;
	grassRatio=50;
	smooth=4;
	
	nbIslands=4;
	islandsSize=12;
	beach=1;
	for (int i=0; i<NB_RESSOURCES; i++)
		ressource[i]=7;
	
	nbWorkers=4;
	
	randa=randb=randc=0;
}


MapGenerationDescriptor::~MapGenerationDescriptor()
{
	// Tuut-tuut bom-bom
}

char *MapGenerationDescriptor::getData()
{
	assert(DATA_SIZE==76);
	
	addSint32(data, wDec, 0);
	addSint32(data, hDec, 4);
	
	addSint32(data, (Sint32)terrainType, 8);
	
	addSint32(data, (Sint32)methode, 12);
	addSint32(data, waterRatio, 16);
	addSint32(data, sandRatio, 20);
	addSint32(data, grassRatio, 24);
	addSint32(data, smooth, 28);
	
	addSint32(data, nbIslands, 32);
	addSint32(data, islandsSize, 36);
	addSint32(data, beach, 40);
	
	assert(NB_RESSOURCES==4);
	addSint32(data, ressource[0], 44);
	addSint32(data, ressource[1], 48);
	addSint32(data, ressource[2], 52);
	addSint32(data, ressource[3], 56);
	
	addSint32(data, nbWorkers, 60);
	
	addUint32(data, randa, 64);
	addUint32(data, randb, 68);
	addUint32(data, randc, 72);
	
	return data;
}

bool MapGenerationDescriptor::setData(const char *data, int dataLength)
{
	assert(DATA_SIZE==76);
	assert(getDataLength()==DATA_SIZE);
	assert(getDataLength()==dataLength);
	
	wDec=getSint32(data, 0);
	hDec=getSint32(data, 4);
	
	terrainType=(Map::TerrainType)getSint32(data, 8);
	
	methode=(Methode)getSint32(data, 12);
	waterRatio=getSint32(data, 16);
	sandRatio=getSint32(data, 20);
	grassRatio=getSint32(data, 24);
	smooth=getSint32(data, 28);
	
	nbIslands=getSint32(data, 32);
	islandsSize=getSint32(data, 36);
	beach=getSint32(data, 40);
	
	assert(NB_RESSOURCES==4);
	ressource[0]=getSint32(data, 44);
	ressource[1]=getSint32(data, 48);
	ressource[2]=getSint32(data, 52);
	ressource[3]=getSint32(data, 56);
	
	nbWorkers=getSint32(data, 60);
	
	randa=getSint32(data, 64);
	randb=getSint32(data, 68);
	randc=getSint32(data, 72);
	
	bool good=true;
	if (getDataLength()!=dataLength)
		good=false;
	if (wDec>=32)
		good=false;
	if (hDec>=32)
		good=false;
	if (terrainType>Map::GRASS)
		good=false;
	
	return (good);
}

void MapGenerationDescriptor::save(SDL_RWops *stream)
{
	SDL_RWwrite(stream, "GLO2", 4, 1);
	SDL_RWwrite(stream, getData(), DATA_SIZE, 1);
	SDL_RWwrite(stream, "GLO2", 4, 1);
}

bool MapGenerationDescriptor::load(SDL_RWops *stream)
{
	char signature[4];
	SDL_RWread(stream, signature, 4, 1);
	if (memcmp(signature,"GLO2",4)!=0)
		return false;
	SDL_RWread(stream, data, DATA_SIZE, 1);
	setData(data, DATA_SIZE);
	SDL_RWread(stream, signature, 4, 1);
	if (memcmp(signature,"GLO2",4)!=0)
		return false;
	return true;
}

Sint32 MapGenerationDescriptor::checkSum()
{
	Sint32 cs=0;
	
	cs^=wDec+(hDec<<16);
	cs^=(Sint32)terrainType;
	
	cs=(cs<<31)|(cs>>1);
	cs^=(Sint32)methode;
	cs+=waterRatio<<7;
	cs+=sandRatio<<14;
	cs+=grassRatio<<21;
	cs+=smooth<<27;
	
	cs=(cs<<31)|(cs>>1);
	cs^=nbIslands;
	cs^=islandsSize;
	cs^=beach;
	
	assert(NB_RESSOURCES==4);
	cs=(cs<<31)|(cs>>1);
	cs+=ressource[0];
	cs+=ressource[1]<<4;
	cs+=ressource[2]<<8;
	cs+=ressource[3]<<12;
	
	cs=(cs<<31)|(cs>>1);
	cs^=nbWorkers;
	
	cs=(cs<<31)|(cs>>1);
	cs^=randa%randb;
	cs^=randb%randc;
	cs^=randc%randa;
	
	return 0;
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

