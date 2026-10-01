// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2008 Bradley Arsenault

#include "MapScript.h"
#include "GameGUI.h"
#include <assert.h>
#include <iostream>
#include <type_traits>
#include <utility>

#include "Stream.h"

MapScript::MapScript(GameGUI* gui):
	usl(gui), gui(gui)
{
	mode = USL;
}

void MapScript::reset()
{
	script.clear();
	mode = USL;
	javascript.reset();
	jsError = MapScriptError();
	usl.compileCode(script);
}



void MapScript::encodeData(GAGCore::OutputStream* stream) const
{
	stream->writeEnterSection("MapScript");
	stream->writeText(script, "script");
	stream->writeUint8(static_cast<Uint8>(mode), "mode");
	if(mode==JavaScript) javascript.save(stream);
	else usl.encodeData(stream);
	stream->writeLeaveSection();
}



bool MapScript::decodeData(GAGCore::InputStream* stream, Uint32 versionMinor)
{
	stream->readEnterSection("MapScript");
	script = stream->readText("script");
	const Uint8 rawMode = stream->readUint8("mode");
	if (rawMode != static_cast<Uint8>(USL) && (versionMinor < 124 || rawMode != static_cast<Uint8>(JavaScript)))
	{
		std::cerr << "MapScript::decodeData(): unknown map script mode " << static_cast<unsigned>(rawMode) << " (corrupt or newer-version file)." << std::endl;
		stream->readLeaveSection();
		return false;
	}
	mode = static_cast<MapScriptMode>(rawMode);
	if(mode==JavaScript)
	{
		javascript.validate(script);javascript.load(stream);
	}
	else
	{
		usl.compileCode(script);usl.decodeData(stream, versionMinor);
	}
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


void MapScript::setMapScriptMode(MapScript::MapScriptMode newMode)
{
	mode = newMode;
	if (mode == JavaScript && gui)
	{
		// An SGSL space wait must not intercept input while that payload is dormant.
		gui->setIsSpaceSet(false);
		gui->setSwallowSpaceKey(false);
	}
}



bool MapScript::compileCode()
{
	if(mode == JavaScript)
	{
		try{javascript.validate(script);javascript.reset();jsError=MapScriptError();return true;}
		catch(const std::exception& ex){jsError=MapScriptError(0,0,ex.what());return false;}
	}
	if(mode == USL)
	{
		return usl.compileCode(script);
	}
	std::cerr << "MapScript::compileCode(): mode unknown." << std::endl;
	assert(false);
	return false;
}


bool MapScript::replaceSource(MapScriptMode newMode, const std::string& newScript,
							 MapScriptError& error)
{
	MapScript candidate(gui);
	// Preparing a draft must not clear the GUI's legacy space-wait flags.
	// The public mode setter applies that presentation change only on commit.
	candidate.mode = newMode;
	candidate.script = newScript;
	if (!candidate.compileCode())
	{
		error = candidate.getError();
		return false;
	}
	static_assert(std::is_nothrow_swappable_v<Script::JavaScriptMap>);
	static_assert(std::is_nothrow_swappable_v<MapScriptError>);
	script.swap(candidate.script);
	usl.swap(candidate.usl);
	std::swap(javascript, candidate.javascript);
	std::swap(jsError, candidate.jsError);
	setMapScriptMode(newMode);
	return true;
}

const MapScriptError& MapScript::getError() const
{
	return mode==JavaScript?jsError:usl.getError();
}

void MapScript::syncStep(GameGUI *gui)
{
	if(mode==JavaScript) javascript.step(script,*gui);
	else usl.syncStep(gui);
}



Uint32 MapScript::checkSum() const
{
 if(mode!=JavaScript)return 0;
 Uint32 h=javascript.checksum(gui);for(unsigned char c:script)h=(h^c)*16777619u;return h;
}
bool MapScript::buildingAllowed(const std::string& name,bool flag) const
{
 return mode!=JavaScript || javascript.buildingAllowed(name,flag);
}

void MapScript::restorePresentation(GameGUI& target) const
{
 if (mode == JavaScript) javascript.present(target, false);
}
