// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2008 Bradley Arsenault

#pragma once

#include "usl.h"
#include "interpreter.h"
#include "MapScriptError.h"
#include "SDL.h"

#include <memory>

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
	
	///Install the glob2 bridge constants ("gui", "engine", "hints", "objectives")
	///into the current interpreter's heap. These constants are the script's only
	///access to game state; they must be re-installed whenever the interpreter is
	///replaced (see compileCode).
	void addGlob2Values(GameGUI* gui);

	///Encodes this MapScript into a bit stream
	void encodeData(GAGCore::OutputStream* stream) const;

	///Decodes this MapScript from a bit stream
	void decodeData(GAGCore::InputStream* stream, Uint32 versionMinor);
	
	///This compiles the code, returns false on failure.
	///Discards ALL interpreter state (heap, threads, constants) and rebuilds the
	///interpreter from scratch: a fresh Usl, the glob2 bridge constants, the USL
	///runtime library scripts, then the given code as a new "<mapscript>" thread.
	///A recompile is a full reset, never an incremental update.
	bool compileCode(const std::string& code);
	
	///This returns the error of the most recent compile
	const MapScriptError& getError() const;
	
	///Execute a step of script corresponding to a step of the game engine
	void syncStep(GameGUI *gui);
	
private:
	
	///Held by pointer so compileCode can replace the whole interpreter with a
	///plain assignment (the old instance's destructor frees its GC heap).
	///Never null after construction.
	std::unique_ptr<Usl> usl;
	MapScriptError error;
};



