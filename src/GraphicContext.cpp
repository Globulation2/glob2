/*
 * GAG graphic context file
 * (c) 2001 Stephane Magnenat, Luc-Olivier de Charriere, Ysagoon
 */

#include "GraphicContext.h"
#include "SDLGraphicContext.h"
#include <assert.h>

GraphicContext *GraphicContext::createGraphicContext(GraphicContextType type)
{
	if (type==GC_SDL)
	{
		return new SDLGraphicContext;
	}
	else
	{
		fprintf(stderr, "VID : Critical, don't know how to create graphic context 0x%x\n", (unsigned)type );
		assert(false);
		return NULL;
	}
}
