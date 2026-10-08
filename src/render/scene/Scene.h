// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "SceneEntities.h"
#include "SceneMap.h"
#include "ScenePanels.h"
#include "sim/snapshot/WorldSnapshot.h"
#include "sim/presentation/PresentationRequest.h"

#include <SDL3/SDL_stdinc.h>

#include <memory>

class OverlayArea;
struct BuildingType;

//! An immutable published world lease, view request, and derived presentation.
struct PresentationFrame
{
    SimulationSnapshot::Handle world;
    SceneRequest request;
	// Retain the immutable type storage referenced by entity and panel records.
	std::shared_ptr<const std::vector<BuildingType>> buildingTypes;
	bool materialVisible(unsigned material) const { return material < 8 || ((map.materialPresence() | entities.materialPresence) & (1u << material)); }
	bool clearableMaterial(unsigned material) const {
		const auto& definitions = map.resourceRegistry();
		for (const auto& p : definitions.propertyTable())
			if (p.clearable && (p.materialMask & (1u << material)) && materialVisible(material)) return true;
		return false;
	}
	bool editor = false;
	Uint32 tick = 0;
    Uint64 executedOrderRevision = 0;
	//! When that tick finished (SDL_GetTicks) and the interval to the next one in
	//! milliseconds (0 when the simulation runs uncapped), for drawing units between ticks.
	Uint64 tickTime = 0;
	Uint32 tickInterval = 0;
	SceneMap map;
	SceneEntities entities;
	ScenePanels panels;
	//! The overlay map (starving, damage, defence or fertility) the client asked for, or
	//! null. Shared and immutable: unchanged frames reuse the same snapshot.
	std::shared_ptr<const OverlayArea> overlay;
    // Called only after the producer/consumer has exclusive ownership of a
    // retired slot. Preserve reusable derived storage without pinning old worlds.
    void releaseWorld()
    {
        world={}; buildingTypes.reset(); panels={};
        auto masks=std::move(entities.connectionMasks);
        entities={}; entities.connectionMasks=std::move(masks);
        map.releaseWorld();
        overlay.reset();
    }
};
