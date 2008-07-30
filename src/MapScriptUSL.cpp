/*
  Copyright (C) 2008 Bradley Arsenault

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

#include "MapScriptUSL.h"

#include "Stream.h"
#include <iostream>

MapScriptUSL::MapScriptUSL()
	: heap(NULL), debug(NULL), block(NULL)
{
	initialize();
}



MapScriptUSL::~MapScriptUSL()
{
	reset();
}



void MapScriptUSL::encodeData(GAGCore::OutputStream* stream) const
{
	stream->writeEnterSection("MapScriptUSL");
	stream->writeLeaveSection();
}



void MapScriptUSL::decodeData(GAGCore::InputStream* stream, Uint32 versionMinor)
{
	stream->readEnterSection("MapScriptUSL");
	stream->readLeaveSection();
}



int MapScriptUSL::compileCode(const std::string& code)
{
	reset();
	initialize();
	Parser parser("<internal>", code.c_str(), heap);
	try
	{
		parser.parse(block);
	}
	catch(Exception& e)
	{
		std::cout << "Error parsing: " << e.position << ":" << e.what() << std::endl;
		return -1;
	}
	
	try
	{
		block->dump(std::cout);
		block->generateMembers(scope, debug, heap);
	}
	catch(Exception& e)
	{
		std::cout << "Error generating: " << e.position << ":" << e.what() << std::endl;
		return -1;
	}
}


void MapScriptUSL::reset()
{
	if(heap!=NULL)
		delete heap;
	if(debug!=NULL)
		delete debug;
	if(block!=NULL)
		delete block;
	if(scope!=NULL)
		delete scope;
	heap=NULL;
	debug=NULL;
	block=NULL;
	scope=NULL;
}


void MapScriptUSL::initialize()
{
	heap = new Heap;
	debug = new DebugInfo;
	block = new ExecutionBlock(Position());
	scope = new ScopePrototype(heap, 0);
}
