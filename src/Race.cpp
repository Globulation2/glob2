// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#include <assert.h>

#include <SDL_endian.h>

#include <vector>

#include <FileManager.h>
#include <Stream.h>
#include <TextStream.h>

#include "GlobalContainer.h"
#include "Version.h"
#include "Race.h"

UnitType Race::unitTypes[NB_UNIT_TYPE][NB_UNIT_LEVELS];
Sint32 Race::hungryness;

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
	
	hungryness = stream->readSint32("hungryness");
	
	stream->readEnterSection("worker");
	for (int i = 0; i < NB_UNIT_LEVELS; i++)
	{
		stream->readEnterSection(i);
		unitTypes[0][i].load(stream, VERSION_MINOR);
		stream->readLeaveSection();
	}
	stream->readLeaveSection();
	
	stream->readEnterSection("explorer");
	for (int i = 0; i < NB_UNIT_LEVELS; i++)
	{
		stream->readEnterSection(i);
		unitTypes[1][i].load(stream, VERSION_MINOR);
		stream->readLeaveSection();
	}
	stream->readLeaveSection();
	
	stream->readEnterSection("warrior");
	for (int i = 0; i < NB_UNIT_LEVELS; i++)
	{
		stream->readEnterSection(i);
		unitTypes[2][i].load(stream, VERSION_MINOR);
		stream->readLeaveSection();
	}
	stream->readLeaveSection();
	
	delete stream;
}

Uint32 Race::checkSumDefault()
{
	return checkSum();
}

void Race::load()
{
}

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
