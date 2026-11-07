// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "render/scene/SceneExtract.h"

//! The editor's scene preparation state. One extractor lives as long as the
//! editor, so its overlay and connection caches survive between frames, and
//! the cache records what the current scene was prepared from, so a frame that
//! changed neither the world nor the request draws the scene it already has.
struct EditorSceneCache
{
	SceneExtractor extractor;
	//! Boundary serial of the world the current scene shows; 0 while none does.
	Uint64 boundarySerial = 0;
	SceneRequest request;
	//! Scenes prepared so far; unchanged frames do not count.
	Uint64 preparations = 0;

	bool holds(const SimulationSnapshot::Handle &world, const SceneRequest &next) const
	{
		return boundarySerial && world.boundarySerial == boundarySerial && request == next;
	}
};
