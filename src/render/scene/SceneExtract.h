// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "Scene.h"
#include "sim/presentation/PresentationRequest.h"
#include "sim/EntityRef.h"
#include "sim/snapshot/Requirements.h"

#include <memory>
#include <unordered_map>

namespace SimulationSnapshot { struct Handle; }
struct SceneInputs;

class OverlayArea;

//! Prepares derived presentation data from immutable world and view inputs.
//! Retains derived caches between frames; never reads the live simulation.
class SceneExtractor
{
public:
    static SimulationSnapshot::Requirements requirements(const SceneRequest& request);
    static std::shared_ptr<SceneInputs> inputs(const SimulationSnapshot::Handle& world,const SceneRequest& request);
    void prepare(const SimulationSnapshot::Handle& world,const SceneRequest& request,PresentationFrame& scene);
    //! Pure preparation from owned immutable inputs, safe beyond the read phase.
    void prepare(const SceneInputs& inputs, PresentationFrame& scene);
    static size_t preparationChunks(const SceneInputs& inputs);
    bool prepareChunk(const SceneInputs& inputs, PresentationFrame& scene, size_t chunk);

private:
	std::shared_ptr<const OverlayArea> overlay;
    std::shared_ptr<OverlayArea> pendingOverlay;
    std::array<int,SceneEntities::Teams> buildLevels{};
    std::unordered_multimap<int,const SnapshotBuilding*> connectionCandidates;
    size_t candidateBuilding = 0, candidateCell = 0, connectionBuilding = 0;
	Uint8 overlayType = 0;
	Uint32 overlayWindow = 0;
	int overlayTeam = -1;
    Uint64 overlayWorld=0, overlayConfiguration=0, overlayObservation=0;
    Uint32 overlayTick=0;
    std::array<Uint64,5> overlayMapGenerations{};
};
