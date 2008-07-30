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

#ifndef MapScriptUSL_h
#define MapScriptUSL_h

#include "parser.h"
#include "types.h"
#include "debug.h"
#include "code.h"
#include "interpreter.h"
#include "error.h"

#include "SDL.h"

namespace GAGCore
{
	class OutputStream;
	class InputStream;
}


///This represents a USL based map script
class MapScriptUSL
{
public:
	///Construct a map script
	MapScriptUSL();
	
	///Destruct a map script
	~MapScriptUSL();

	///Encodes this MapScript into a bit stream
	void encodeData(GAGCore::OutputStream* stream) const;

	///Decodes this MapScript from a bit stream
	void decodeData(GAGCore::InputStream* stream, Uint32 versionMinor);
	
	///This compiles the code, returning an error code of -1 on failure
	int compileCode(const std::string& code);
	
private:
	///This resets the interpreter
	void reset();
	//Initializes the compiler
	void initialize();

	Heap* heap;
	DebugInfo* debug;
	ExecutionBlock* block;
	ScopePrototype* scope;
};



#endif
