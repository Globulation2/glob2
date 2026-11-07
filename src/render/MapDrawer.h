// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <set>

#include <SDL3/SDL_stdinc.h>

#include "BuildingGuiState.h"
#include "Map.h"
#include "ViewState.h"

class BuildingType;
class SceneMap;
struct PresentationFrame;
struct MapRenderState;
namespace SimulationSnapshot { struct UnitView; }
using SnapshotUnit = SimulationSnapshot::UnitView;
namespace Utilities { class BitArray; }

//! Draws one view of the map from an extracted PresentationFrame and the
//! viewer's ViewState (see docs/architecture/rendering.md, "Scene renderer").
//! Definitions are in src/render/GameRender*.cpp; `drawOptions` takes the
//! Game::DrawOption flags.
namespace MapDrawer
{
	enum BarOrientation
	{
		LEFT_TO_RIGHT,
		RIGHT_TO_LEFT,
		TOP_TO_BOTTOM,
		BOTTOM_TO_TOP
	};

	void drawUnit(int x, int y, Uint16 gid, int viewportX, int viewportY, int screenW, int screenH, int localTeam, Uint32 drawOptions, ViewState& view, const PresentationFrame& scene);
	// Prepare one immutable presentation frame for several offscreen regions.
	void finishMapCapture(ViewState &view, Uint32 options, bool paused);
	void finishMapCapture(MapRenderState &render, Uint32 options, bool paused);
	void prepareSceneMapFrame(const PresentationFrame &scene, int team, ViewState &view, Uint32 options, bool paused);
	/// `view` carries the calling front-end's selection/mouse state (see
	/// ViewState); render reads selectedUnit/selectedBuilding for highlights and
	/// writes back mouseUnit from hit-testing. `buildingGuiState` (optional)
	/// provides per-flag pending positions so drag-targets render before the
	/// move-flag order has executed. The map editor passes nullptr for it — it
	/// mutates buildings directly without an orderQueue, so there is no pending
	/// shadow to consult.
	void drawSceneMap(const PresentationFrame& scene, int sx, int sy, int sw, int sh, int rightMargin, int topMargin, int viewportX, int viewportY, int teamSelected, ViewState& view, Uint32 drawOptions = 0, std::set<Uint16> *visibleBuildings = 0, const BuildingGuiStateMap* buildingGuiState = nullptr, bool animationsPaused = false, int cloudGridLimit = 0, bool preparedCapture = false);
	// Bounded capture culling keeps layer wrapping in the complete target coordinate system.
	void drawSceneMapRegion(const PresentationFrame& scene, int sx, int sy, int sw, int sh, int rightMargin, int topMargin, int viewportX, int viewportY, int teamSelected, ViewState& view, Uint32 drawOptions, std::set<Uint16> *visibleBuildings, const BuildingGuiStateMap* buildingGuiState, bool animationsPaused, int cloudGridLimit, bool preparedCapture, int targetWidth, int targetHeight);
	void drawMap(int sx, int sy, int sw, int sh, int rightMargin, int topMargin, int viewportX, int viewportY, int teamSelected, ViewState& view, Uint32 drawOptions = 0, std::set<Uint16> *visibleBuildings = 0, const BuildingGuiStateMap* buildingGuiState = nullptr, bool animationsPaused = false, int cloudGridLimit = 0, bool preparedCapture = false);

	//! `opacity` fades an entity's bars and pips with it, as a unit fading into the fog of war.
	void anchorBars(int x, int y, MapRenderState* drawnRender = nullptr, float opacity = 1.f);
	void drawStatusPip(int x, int y, Uint8 r, Uint8 g, Uint8 b, MapRenderState* drawnRender = nullptr, float opacity = 1.f);
	///draws a point bar. This can be health, hunger, fill level, etc. Point bars can have 2 sections of actLength and secondActLength, followed by black until maxLength. r/g/b is for the first section, r2/g2/b2 for the second
	//! With drawnRender the bar takes the opacity of the last anchorBars; without it, `opacity`.
	void drawPointBar(int x, int y, BarOrientation orientation, int maxLength, int actLength, int secondActLength, Uint8 r, Uint8 g, Uint8 b, Uint8 r2, Uint8 g2, Uint8 b2, int barWidth=2, MapRenderState* drawnRender=nullptr, float opacity = 1.f);
	inline void drawPointBar(int x, int y, BarOrientation orientation, int maxLength, int actLength, Uint8 r, Uint8 g, Uint8 b, int barWidth=2, MapRenderState* drawnRender=nullptr, float opacity = 1.f)
	{
		drawPointBar(x, y, orientation, maxLength, actLength, 0, r, g, b, r, g, b, barWidth, drawnRender, opacity);
	}
	///draws an HP bar coloured green/yellow/red against the 0.6 / 0.3 hpRatio thresholds
	void drawHealthBar(int x, int y, int maxLength, int actLength, float hpRatio, MapRenderState* drawnRender=nullptr, float opacity = 1.f);
	///draws a building resource bar (food, bullets, ...) auto-shrinking to fit within (height*32)-10 pixels
	void drawBuildingResourceBar(int x, int y, const BuildingType* type, int maxValue, int currentValue, Uint8 r, Uint8 g, Uint8 b, MapRenderState* drawnRender=nullptr);
	///draws sampled terrain palette colours and tile resource tints when zoomed far out
	void drawMapOverview(int left, int top, int right, int bot, int viewportX, int viewportY, int localTeam, Uint32 drawOptions, const SceneMap& sceneMap, MapRenderState& render);
	///draws a faint wash of each team's colour over the land around its buildings, in the strategic view
	void drawMapTerritory(int left, int top, int right, int bot, int viewportX, int viewportY, int localTeam, Uint32 drawOptions, const PresentationFrame& scene, float opacity);
	///draws the overlay representing water
	///draws the terrain tiles of sand and gras
	void drawMapTerrain(int left, int top, int right, int bot, int viewportX, int viewportY, int localTeam, Uint32 drawOptions, const SceneMap& sceneMap, int animationTime = 0);
	///draws the resources like algae, wheat or fruit trees
	void drawMapResources(int left, int top, int right, int bot, int viewportX, int viewportY, int localTeam, Uint32 drawOptions, const SceneMap& sceneMap, MapRenderState *render = nullptr);
	///draws the ground units. up till now those are workers and warriors
	void drawMapGroundUnits(int left, int top, int right, int bot, int sw, int sh, int viewportX, int viewportY, int localTeam, Uint32 drawOptions, ViewState& view, const PresentationFrame& scene);
	///draws debug information. switched in the code.
	void drawMapDebugAreas(int left, int top, int right, int bot, int sw, int sh, int viewportX, int viewportY, int localTeam, Uint32 drawOptions, ViewState& view);
	void drawMapGroundBuildings(int left, int top, int right, int bot, int sw, int sh, int viewportX, int viewportY, int localTeam, Uint32 drawOptions, std::set<Uint16> *visibleBuildings, const BuildingGuiStateMap* buildingGuiState, const PresentationFrame& scene, MapRenderState* drawnRender, ViewState* view = nullptr);
	void drawMapBuilding(int x, int y, int gid, int viewportX, int viewportY, int localTeam, Uint32 drawOptions, const PresentationFrame& scene, MapRenderState* drawnRender, ViewState* view = nullptr);
	void drawMapAreas(int left, int top, int right, int bot, int sw, int sh, int viewportX, int viewportY, int localTeam, Uint32 drawOptions, ViewState& view, const SceneMap& sceneMap, bool advanceAnimation = true);
	void drawMapArea(int left, int top, int right, int bot, int sw, int sh, int viewportX, int viewportY, int localTeam, Uint32 drawOptions, const SceneMap& map, bool (SceneMap::*mapIs)(int, int) const, int areaAnimationTick, AreaType areaType, const MapRenderState& render, const Utilities::BitArray* preview = nullptr);
	void drawMapAirUnits(int left, int top, int right, int bot, int sw, int sh, int viewportX, int viewportY, int localTeam, Uint32 drawOptions, ViewState& view, const PresentationFrame& scene);
	void drawMapScriptAreas(int left, int top, int right, int bot, int viewportX, int viewportY, const SceneMap& map);
	void drawMapBulletsExplosionsDeathAnimations(int left, int top, int right, int bot, int sw, int sh, int viewportX, int viewportY, int localTeam, Uint32 drawOptions, const PresentationFrame& scene);
	//! The dust clouds of units swarms have just produced (UnitEffects.h).
	void drawMapUnitEffects(int left, int top, int right, int bot, int sw, int sh, int viewportX, int viewportY, int localTeam, Uint32 drawOptions, ViewState& view, const PresentationFrame& scene);
	void drawMapFogOfWar(int left, int top, int right, int bot, int sw, int sh, int viewportX, int viewportY, int localTeam, Uint32 drawOptions, const MapRenderState& render, const PresentationFrame& scene);
	void drawMapOverlayMaps(int left, int top, int right, int bot, int sw, int sh, int viewportX, int viewportY, int localTeam, Uint32 drawOptions, ViewState& view);
	void drawUnitPathLines(int left, int top, int right, int bot, int sw, int sh, int viewportX, int viewportY, int localTeam, Uint32 drawOptions, ViewState& view, const PresentationFrame& scene);
	void drawUnitPathLine(int left, int top, int right, int bot, int sw, int sh, int viewportX, int viewportY, int localTeam, Uint32 drawOptions, const SnapshotUnit& unit, const PresentationFrame& scene, float unitMotion = 0);
	void drawUnitOffScreen(int sx, int sy, int sw, int sh, int viewportX, int viewportY, const SnapshotUnit& unit, Uint32 drawOptions, const PresentationFrame& scene, float unitMotion = 0);
	bool isOnScreen(int left, int top, int right, int bot, int viewportX, int viewportY, int x, int y, const SceneMap& map);
}
