// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2008 Bradley Arsenault

#pragma once

#include <SDL3/SDL.h>
#include <memory>
#include <string>
#include "MapScriptUSL.h"
#include "script/JavaScriptMap.h"

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
		USL=1,
		JavaScript=2
	};

	///Constructs the MapScript
	MapScript(GameGUI* gui);
	///Clear all script state before loading another map, including pre-USL maps.
	void reset();

	///Encodes this MapScript into a bit stream
	void encodeData(GAGCore::OutputStream* stream) const;

	///Decodes this MapScript from a bit stream. Returns false if the stored
	///mode byte is not a known MapScriptMode (corrupt or future-version file);
	///the caller must treat that as a load failure — `mode` is left at USL and
	///no compile is attempted.
	bool decodeData(GAGCore::InputStream* stream, Uint32 versionMinor);

	///This returns the string representing the mapscript
	const std::string& getMapScript() const;
	
	///This sets the string representing the map script
	void setMapScript(const std::string& newScript);
	
	///This returns the current map script mode
	MapScriptMode getMapScriptMode() const { return mode; }
	
	///This sets the current map script mode
	void setMapScriptMode(MapScriptMode newMode) noexcept;
	
	///This compiles the code and returns false on error.
	///Both USL and JavaScript compile through their respective backends.
	bool compileCode();
	///Compile a replacement separately, then commit its source and runtime together.
	///Failure leaves the active source, mode and saved globals unchanged.
	bool replaceSource(MapScriptMode newMode, const std::string& newScript, MapScriptError& error);
	///Prepare without changing this map; null means a compilation failure.
	std::unique_ptr<MapScript> prepareSource(MapScriptMode newMode, const std::string& newScript,
											MapScriptError& error) const;
	///Commit a successfully prepared candidate for this same GUI, without allocating.
	///The candidate receives the previous backend and owns its eventual destruction.
	void commitPrepared(MapScript& candidate) noexcept;

	///This returns the error
	const MapScriptError& getError() const;
	
	///Execute a step of script corresponding to a step of the game engine
	void syncStep(GameGUI *gui);

	void restorePresentation(GameGUI& gui) const;
	Uint32 checkSum() const;
	bool buildingAllowed(const std::string& name,bool flag) const;

private:
	std::string script;
	MapScriptMode mode;
	MapScriptUSL usl;
	Script::JavaScriptMap javascript;
	GameGUI* gui;
	MapScriptError jsError;
};
