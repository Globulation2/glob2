// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "Scene.h"
#include "sim/ClientRequests.h"
#include "sim/EntityRef.h"

#include <memory>

class Game;
namespace SimulationSnapshot { struct Handle; }
struct SceneInputs;

//! What the client wants drawn this frame: whose view, and what it has selected.
struct SceneRequest
{
	std::optional<std::pair<Uint32, Uint32>> highlights;
	bool includeScriptAreas = false;
	bool includePanels = true;
	int localTeam = 0;
	bool spectating = false;
	ClientRequests::ClientView view;
	BuildingRef selectedBuilding;
	UnitRef selectedUnit;
	//! When the latest tick finished and the interval to the next (see Scene).
	Uint64 tickTime = 0;
	Uint32 tickInterval = 0;
};

class OverlayArea;

//! Fills Scenes from a game. The only place presentation code reads the
//! simulation: it runs where the game may be read (between ticks, on the
//! simulation side once threaded) and never modifies the game. It keeps the
//! state that spans frames, such as the overlay map, which refreshes when the
//! requested type changes or the game enters a new 25-tick window.
class SceneExtractor
{
public:
	static void extractInputPanels(const Game& game, const SceneRequest& request, ScenePanels& panels);
	void extract(const Game &game, const SceneRequest &request, Scene &scene);
    //! Capture simulation-owned source values; only called at an owner boundary.
    std::shared_ptr<SceneInputs> capture(const Game& game, const SceneRequest& request);
    //! Pure preparation from owned immutable inputs, safe beyond the read phase.
    void prepare(const SceneInputs& inputs, Scene& scene);
    static size_t preparationChunks(const SceneInputs& inputs);
    void prepareChunk(const SceneInputs& inputs, Scene& scene, size_t chunk);

private:
	std::shared_ptr<const OverlayArea> overlay;
	Uint8 overlayType = 0;
	Uint32 overlayWindow = 0;
	int overlayTeam = -1;
};

//! Extract with a fresh extractor: for one-off drawing without frame-spanning state.
void extractScene(const Game &game, const SceneRequest &request, Scene &scene);
