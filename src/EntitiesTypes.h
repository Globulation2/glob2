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

#ifndef __ENTITIES_TYPES_H
#define __ENTITIES_TYPES_H

#include <vector>
#include <assert.h>

#include <FileManager.h>
#include <Toolkit.h>
#include <Stream.h>
#include <BinaryStream.h>

#include "EntityType.h"

template <class T> class EntitiesTypes
{
public:
	virtual ~EntitiesTypes()
	{
		for (typename std::vector <T *>::iterator it=entitiesTypes.begin(); it!=entitiesTypes.end(); ++it)
		{
			delete (*it);
		}
	}

	virtual void load(const std::string filename)
	{
		GAGCore::InputStream *stream = new GAGCore::BinaryInputStream(GAGCore::Toolkit::getFileManager()->openInputStreamBackend(filename));
		if (stream->isEndOfStream())
		{
			std::cerr << "EntitiesTypes::load(\"" << filename << "\") : error, can't open file." << std::endl;
			delete stream;
			return;
		}
		
		bool result = true;

		T defaultEntityType;
		defaultEntityType.init();
		result = defaultEntityType.loadText(stream);

		while (result)
		{
			T *entityType = new T();
			*entityType = defaultEntityType;
			result = entityType->loadText(stream);
			if (result)
			{
				entitiesTypes.push_back(entityType);
			}
			else
				delete entityType;
		}

		delete stream;
	}

	T* get(unsigned int num)
	{
		if ((num)<entitiesTypes.size())
		{
			return entitiesTypes[num];
		}
		else
		{
			assert(false);
			return NULL;
		}
	}

	size_t size(void) { return entitiesTypes.size(); }
	
	void dump(void)
	{
		for (typename std::vector <T *>::iterator it=entitiesTypes.begin(); it!=entitiesTypes.end(); ++it)
			(*it)->dump();
	}

protected:
	std::vector<T*> entitiesTypes;
};

#endif
