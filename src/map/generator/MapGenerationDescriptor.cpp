// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

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
	fruitRatio=4;
	logRepeatAreaTimes=0;
	
	oldIslandSize=50;
	oldBeach=1;	
	for (int i=0; i<MAX_NB_RESSOURCES; i++)
		ressource[i]=7;
	
	nbWorkers=4;
	nbTeams=4;
}


MapGenerationDescriptor::~MapGenerationDescriptor()
{
	// Tuut-tuut bom-bom
}

Uint8 *MapGenerationDescriptor::getData()
{
	assert(DATA_SIZE==100+MAX_NB_RESSOURCES*4);
	
	addSint32(data, wDec, 0);
	addSint32(data, hDec, 4);

	addSint32(data, (Sint32)terrainType, 8);

	addSint32(data, (Sint32)methode, 12);
	addSint32(data, waterRatio, 16);
	addSint32(data, sandRatio, 20);
	addSint32(data, grassRatio, 24);
	addSint32(data, desertRatio, 28);
	
	addSint32(data, wheatRatio, 32);
	addSint32(data, woodRatio, 36);
	addSint32(data, algaeRatio, 40);
	addSint32(data, stoneRatio, 44);	
	addSint32(data, riverDiameter, 48);
	
	addSint32(data, craterDensity, 52);
	addSint32(data, extraIslands, 56);
	addSint32(data, smooth, 60);

	addUint32(data, nbWorkers, 64);
	addUint32(data, nbTeams, 68);
	
	addUint32(data, oldIslandSize, 72);
	addUint32(data, oldBeach, 76);
	addUint32(data, fruitRatio, 80);

	addUint32(data, logRepeatAreaTimes, 84);

	for (unsigned i=0; i<MAX_NB_RESSOURCES; i++)
		addSint32(data, ressource[i], 88+i*4);

	return data;
}

bool MapGenerationDescriptor::setData(const Uint8 *data, int dataLength)
{
	assert(DATA_SIZE==100+MAX_NB_RESSOURCES*4);
	assert(getDataLength()==DATA_SIZE);
	assert(getDataLength()==dataLength);
	
	wDec=getSint32(data, 0);
	hDec=getSint32(data, 4);
	
	terrainType=(TerrainType)getSint32(data, 8);

	methode=(Methode)getSint32(data, 12);
	waterRatio=getSint32(data, 16);
	sandRatio=getSint32(data, 20);
	grassRatio=getSint32(data, 24);
	desertRatio = getSint32(data, 28);
	
	wheatRatio = getSint32(data, 32);
	woodRatio = getSint32(data, 36);
	algaeRatio = getSint32(data, 40);
	stoneRatio = getSint32(data, 44);
	riverDiameter = getSint32(data, 48);
	
	craterDensity = getSint32(data, 52);
	extraIslands = getSint32(data, 56);
	smooth=getSint32(data, 60);

	nbWorkers=getSint32(data, 64);
	nbTeams=getSint32(data, 68);
	
	oldIslandSize=getSint32(data, 72);
	oldBeach=getSint32(data, 76);
	
	fruitRatio = getSint32(data, 80);
	logRepeatAreaTimes = getSint32(data, 84);

	for (unsigned i=0; i<MAX_NB_RESSOURCES; i++)
		ressource[i]=getSint32(data, 88+i*4);

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

bool MapGenerationDescriptor::load(GAGCore::InputStream *stream, Sint32 versionMinor)
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
	cs=rotr1(cs);
	cs^=(Sint32)methode;
	cs=rotr1(cs);
	cs ^= waterRatio;
	cs=rotr1(cs);
	cs ^= sandRatio;
	cs=rotr1(cs);
	cs ^= grassRatio;
	cs=rotr1(cs);
	cs ^= desertRatio;
	cs=rotr1(cs);
	cs ^= wheatRatio;
	cs=rotr1(cs);
	cs ^= fruitRatio;
	cs=rotr1(cs);
	cs ^= woodRatio;
	cs=rotr1(cs);
	cs ^= algaeRatio;
	cs=rotr1(cs);
	cs ^= stoneRatio;
	cs=rotr1(cs);
	cs ^= riverDiameter;
	cs=rotr1(cs);
	cs ^= craterDensity;
	cs=rotr1(cs);
	cs ^= extraIslands;
	cs=rotr1(cs);
	cs ^= smooth;
	cs=rotr1(cs);
	cs ^= oldIslandSize;
	cs=rotr1(cs);
	cs ^= oldBeach;
	cs=rotr1(cs);
	cs ^= logRepeatAreaTimes;

	for (unsigned i=0; i<MAX_NB_RESSOURCES; i++)
		cs+=ressource[i]<<(3*i);

	cs=rotr1(cs);
	cs^=nbWorkers;
	cs^=nbTeams<<5;
	
	return cs;
}

