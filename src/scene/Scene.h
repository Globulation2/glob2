// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "scene/SceneMap.h"

#include <SDL3/SDL_stdinc.h>

//! Everything the renderer draws for one frame, extracted from the simulation at a
//! tick boundary and read-only afterwards. Grows as render passes are ported to it.
struct Scene
{
	Uint32 tick = 0;
	SceneMap map;
};
