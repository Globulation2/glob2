/*
  Copyright (C) 2001, 2002 Stephane Magnenat & Luc-Olivier de Charri�e
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

#include "GraphicContext.h"
#include "SDLGraphicContext.h"
#include "GLGraphicContext.h"
#include <assert.h>

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif


GraphicContext *GraphicContext::createGraphicContext(GraphicContextType type)
{
	if (type==GC_SDL)
	{
		return new SDLGraphicContext;
	}
#ifdef HAVE_LIBGL
	else if (type==GC_GL)
	{
		return new GLGraphicContext;
	}
#endif
	else
	{
		fprintf(stderr, "GAG : Critical, don't know how to create graphic context 0x%x\n", (unsigned)type );
		assert(false);
		return NULL;
	}
}

GraphicContext::~GraphicContext(void)
{
}

int Font::getStringWidth(const int i) const
{
	char temp[32];
	snprintf(temp, 32, "%d", i);
	return getStringWidth(temp);
}
