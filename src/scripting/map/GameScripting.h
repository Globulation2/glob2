// SPDX-License-Identifier: GPL-3.0-or-later
// Game together with its scripting state, for the files that use Game's scripts,
// objectives or hints. Game.h holds them by pointer so that its other users do not
// compile the script runtimes; the inline accessors it declares are defined here.
#pragma once

#include "Game.h"
#include "GameHints.h"
#include "GameObjectives.h"
#include "MapScript.h"
#include "SGSL.h"

// JavaScript is the selected scenario runtime. Preserve the serialized SGSL
// payload, but leave its execution and presentation dormant in this mode.
inline bool Game::legacyScriptActive() const { return mapscript.getMapScriptMode() != MapScript::JavaScript; }
inline int Game::legacyScriptTimer() const { return legacyScriptActive() ? sgslScript.getMainTimer() : 0; }
