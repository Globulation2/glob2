// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "scene/SceneEntities.h"
#include "scene/SceneMap.h"
#include "scene/ScenePanels.h"

#include <SDL_stdinc.h>

#include <memory>

class OverlayArea;

//! Everything the renderer draws for one frame, extracted from the simulation at a
//! tick boundary and read-only afterwards. Grows as render passes are ported to it.
struct Scene
{
	Uint32 tick = 0;
	SceneMap map;
	SceneEntities entities;
	ScenePanels panels;
	//! The overlay map (starving, damage, defence or fertility) the client asked for, or
	//! null. Shared and immutable: unchanged frames reuse the same snapshot.
	std::shared_ptr<const OverlayArea> overlay;
};
