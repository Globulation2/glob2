// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#include <string.h>
#include <stdlib.h>
#include <assert.h>
#include <sstream>
#include <iostream>
#include <Stream.h>

#include "EntityType.h"
#include "Utilities.h"

EntityType::EntityType()
{
}

EntityType::EntityType(GAGCore::InputStream *stream)
{
	load(stream);
}

void EntityType::init(void)
{
	size_t varSize;
	Uint32 *startData;
	getVars(&varSize, &startData);	
	memset(startData,0,varSize*sizeof(Uint32));
}

void EntityType::load(GAGCore::InputStream *stream)
{
	size_t size;
	Uint32 *startData;
	getVars(&size, &startData);
	for (size_t i=0;i<size;i++)
	{
		std::ostringstream oss;
		oss << "entitytype" << i;
		startData[i] = stream->readUint32(oss.str());
	}
}

bool EntityType::loadText(GAGCore::InputStream *stream)
{
	char temp[256];
	char *token;
	char *varname;
	int val;

	size_t varSize;
	Uint32 *startData;
	const char **tab=getVars(&varSize, &startData);	

	assert(stream);
	while (true)
	{
		if (!Utilities::gets(temp, 256, stream))
			return false;
		if (temp[0]=='*')
			return true;
		token=strtok(temp," \t\n\r=;");
		if ((!token) || (strcmp(token,"//")==0))
			continue;
		varname=token;
		token=strtok(NULL," \t\n\r=;");
		if (token)
			val=atoi(token);
		else
			val=0;

		for (size_t i=0; i<varSize; i++)
			if (strcmp(tab[i],varname)==0)
			{
				*(startData+i)=val;
				break;
			}
	}
}

void EntityType::save(GAGCore::OutputStream *stream)
{
	size_t size;
	Uint32 *startData;
	getVars(&size, &startData);
	for (size_t i=0; i<size; i++)
	{
		std::ostringstream oss;
		oss << "entitytype" << i;
		stream->writeUint32(startData[i], oss.str().c_str());
	}
}

void EntityType::dump(void)
{
	size_t varSize;
	Uint32 *startData;
	const char **tab=getVars(&varSize, &startData);
	
	printf("%d Elements :\n", static_cast<unsigned>(varSize));
	for (size_t i=0; i<varSize;i++)
		printf("\t%s = %d\n",tab[i],*(startData+i));
}
