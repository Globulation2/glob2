// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2008 Bradley Arsenault

#include "MapScript.h"
#include <assert.h>
#include <iostream>

#include "Stream.h"

MapScript::MapScript(GameGUI* gui):
	usl(gui)
{
	mode = USL;
}



void MapScript::encodeData(GAGCore::OutputStream* stream) const
{
	stream->writeEnterSection("MapScript");
	stream->writeText(script, "script");
	stream->writeUint8(static_cast<Uint8>(mode), "mode");
	usl.encodeData(stream);
	stream->writeLeaveSection();
}



bool MapScript::decodeData(GAGCore::InputStream* stream, Uint32 versionMinor)
{
	stream->readEnterSection("MapScript");
	script = stream->readText("script");
	const Uint8 rawMode = stream->readUint8("mode");
	if (rawMode != static_cast<Uint8>(USL))
	{
		std::cerr << "MapScript::decodeData(): unknown map script mode " << static_cast<unsigned>(rawMode) << " (corrupt or newer-version file)." << std::endl;
		stream->readLeaveSection();
		return false;
	}
	mode = static_cast<MapScriptMode>(rawMode);
	usl.compileCode(script);
	usl.decodeData(stream, versionMinor);
	stream->readLeaveSection();
	return true;
}



const std::string& MapScript::getMapScript() const
{
	return script;
}



void MapScript::setMapScript(const std::string& newScript)
{
	script = newScript;
}


MapScript::MapScriptMode MapScript::getMapScriptMode() const
{
	return mode;
}

	
void MapScript::setMapScriptMode(MapScript::MapScriptMode newMode)
{
	mode = newMode;
}



bool MapScript::compileCode()
{
	if(mode == USL)
	{
		return usl.compileCode(script);
	}
	std::cerr << "MapScript::compileCode(): mode unknown." << std::endl;
	assert(false);
	return false;
}


const MapScriptError& MapScript::getError() const
{
	return usl.getError();
}

void MapScript::syncStep(GameGUI *gui)
{
	usl.syncStep(gui);
}


