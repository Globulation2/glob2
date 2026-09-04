// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2008 Bradley Arsenault

#ifndef MapScriptUSL_h
#define MapScriptUSL_h

#include "usl.h"
#include "interpreter.h"
#include "MapScriptError.h"
#include "SDL.h"

namespace GAGCore
{
	class OutputStream;
	class InputStream;
}

class GameGUI;

///This represents a USL based map script
class MapScriptUSL
{
public:
	///Construct a map script
	MapScriptUSL(GameGUI* gui);
	
	///Destruct a map script
	~MapScriptUSL();
	
	///Construct all the global (from USL POV) values that reference glob2 objects
	void addGlob2Values(GameGUI* gui);

	///Encodes this MapScript into a bit stream
	void encodeData(GAGCore::OutputStream* stream) const;

	///Decodes this MapScript from a bit stream
	void decodeData(GAGCore::InputStream* stream, Uint32 versionMinor);
	
	///This compiles the code, returns false on failure
	bool compileCode(const std::string& code);
	
	///This returns the error of the most recent compile
	const MapScriptError& getError() const;
	
	///Execute a step of script corresponding to a step of the game engine
	void syncStep(GameGUI *gui);
	
private:
	
	Usl usl;
	MapScriptError error;
};



#endif
