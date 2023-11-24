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

	///This returns the string representing the map script
	const std::string& getMapScript() const;
	
	///This sets the string representing the map script
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
