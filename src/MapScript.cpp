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



void MapScript::decodeData(GAGCore::InputStream* stream, Uint32 versionMinor)
{
	stream->readEnterSection("MapScript");
	script = stream->readText("script");
	mode = static_cast<MapScriptMode>(stream->readUint8("mode"));
	usl.compileCode(script);
	usl.decodeData(stream, versionMinor);
	stream->readLeaveSection();
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
	else
	{
		std::cerr << "mode unknown." << std::endl;
		assert(false);
	}
}


bool MapScript::testCompileCode(const std::string& testScript)
{
	if(mode == USL)
	{
		return usl.compileCode(testScript);
	}
	else
	{
		std::cerr << "mode unknown." << std::endl;
		assert(false);
	}
}


const MapScriptError& MapScript::getError() const
{
	return usl.getError();
}

void MapScript::syncStep(GameGUI *gui)
{
	usl.syncStep(gui);
}


