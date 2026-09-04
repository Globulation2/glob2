// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2008 Bradley Arsenault

#ifndef MapScript_h
#define MapScript_h

#include "SDL.h"
#include <string>
#include "MapScriptUSL.h"

#include "MapScriptError.h"

namespace GAGCore
{
	class OutputStream;
	class InputStream;
}

class GameGUI;

///This class represents the script of the map
class MapScript
{
public:
	///Enumerates the different modes the map script may be
	enum MapScriptMode
	{
		USL=1
	};

	///Constructs the MapScript
	MapScript(GameGUI* gui);

	///Encodes this MapScript into a bit stream
	void encodeData(GAGCore::OutputStream* stream) const;

	///Decodes this MapScript from a bit stream
	void decodeData(GAGCore::InputStream* stream, Uint32 versionMinor);

	///This returns the string representing the mapscript
	const std::string& getMapScript() const;
	
	///This sets the string representing the mapscript
	void setMapScript(const std::string& newScript);
	
	///This returns the current map script mode
	MapScriptMode getMapScriptMode() const;
	
	///This sets the current map script mode
	void setMapScriptMode(MapScriptMode newMode);
	
	///This compiles the code and returns false on error
	bool compileCode();
	
	///This test compiles the code and returns false on error
	bool testCompileCode(const std::string& testScript);
	
	///This returns the error
	const MapScriptError& getError() const;
	
	///Execute a step of script corresponding to a step of the game engine
	void syncStep(GameGUI *gui);

private:
	std::string script;
	MapScriptMode mode;
	MapScriptUSL usl;
};

#endif
