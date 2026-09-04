// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

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
