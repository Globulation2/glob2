// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#ifndef __ENTITY_TYPE_H
#define __ENTITY_TYPE_H

#include <GAGSys.h>

namespace GAGCore
{
	class InputStream;
	class OutputStream;
}

class EntityType
{
public:
	EntityType();
	EntityType(GAGCore::InputStream *stream);
	virtual ~EntityType() { }
	virtual const char **getVars(size_t *size, Uint32 **data) = 0;
	virtual void init(void);
	virtual void load(GAGCore::InputStream *stream);
	virtual bool loadText(GAGCore::InputStream *stream);
	virtual void save(GAGCore::OutputStream *stream);
	virtual void dump(void);
};

#endif

