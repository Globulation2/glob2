// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "UnitAnimation.h"
#include "UnitConsts.h"
#include "render/scene/Scene.h"
#include <algorithm>

// A cosmetic 1.92-second loop at normal (30 Hz) simulation speed. Five loops
// take 288 ticks. Fractional presentation time stops with the simulation;
// harvesting orders, action delta and resource collection remain untouched.
inline int unitProneHarvestPhase(const PresentationFrame &scene, const SnapshotUnit &unit,
								 int meshClass, float motion)
{
	if (meshClass != WORKER || unit.action != HARVEST)
		return -1;
	const auto &terrain = scene.map.terrainPropertiesAt(unit.posX, unit.posY);
	if (terrain.walkable || !terrain.swimmable)
		return -1;
	return (int((float(scene.tick % 288) + std::clamp(motion, 0.f, 1.f)) * (10.f / 9.f)) +
			int(unit.gid % 64)) %
		   64;
}
