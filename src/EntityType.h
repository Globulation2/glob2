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

