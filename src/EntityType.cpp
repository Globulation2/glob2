/*
  Copyright (C) 2001, 2002, 2003 Stephane Magnenat & Luc-Olivier de Charrière
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

#include "EntityType.h"
#include "Utilities.h"
#include <string.h>
#include <stdlib.h>
#include <assert.h>

EntityType::EntityType()
{
}

EntityType::EntityType(SDL_RWops *stream)
{
	load(stream);
}

void EntityType::init(void)
{
	int varSize;
	Uint32 *startData;
	getVars(&varSize, &startData);	
	memset(startData,0,varSize*sizeof(Uint32));
}

void EntityType::load(SDL_RWops *stream)
{
	int size;
	Uint32 *startData;
	getVars(&size, &startData);
	for (int i=0;i<size;i++)
		startData[i]=SDL_ReadBE32(stream);
}

bool EntityType::loadText(SDL_RWops *stream)
{
	char temp[256];
	char *token;
	char *varname;
	int val;

	int varSize;
	Uint32 *startData;
	const char **tab=getVars(&varSize, &startData);	

	assert(stream);
	while (true)
	{
		if (!Utilities::gets(temp,256,stream))
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

		for (int i=0; i<varSize; i++)
			if (strcmp(tab[i],varname)==0)
			{
				*(startData+i)=val;
				break;
			}
	}
}

void EntityType::save(SDL_RWops *stream)
{
	int size;
	Uint32 *startData;
	getVars(&size, &startData);
	for (int i=0;i<size;i++)
		SDL_WriteBE32(stream, startData[i]);
}

void EntityType::dump(void)
{

	int varSize;
	Uint32 *startData;
	const char **tab=getVars(&varSize, &startData);

	printf("%d Elements :\n",varSize);
	for (int i=0; i<varSize;i++)
		printf("\t%s = %d\n",tab[i],*(startData+i));
}
