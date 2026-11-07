// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>

#include "MapRenderState.h"
#include "sim/EntityRef.h"

class Unit;
class Building;
struct PresentationFrame;
namespace Utilities { class BitArray; }

/// Non-simulation view scratch. These fields are NOT part of game state:
/// they are never serialized, never checksummed, and no simulation code
/// reads them. They are owned by the per-client front-end — GameGUI for the
/// in-game view, MapEdit for the editor — and passed into the MapDrawer
/// functions (MapDrawer::drawMap and below) as a `ViewState&` parameter, the
/// same way buildingGuiState is. Input handlers write the selection/mouse
/// fields; MapDrawer hit-testing recomputes mouseUnit each render pass.
///
/// Rust port: keep this in a GUI-side Selection/InputState resource, NOT on
/// the simulation ECS. See cpp-bugs CS-661.
struct ViewState
{
	int mouseX = 0, mouseY = 0;       //!< Mouse position, mirror of GameGUI's own.
	//! Unit under the cursor, recorded by the last draw. A reference, not a pointer:
	//! resolve it through Game::resolveUnit when it is used.
	UnitRef mouseUnit;
	//! Currently selected unit/building, or null. GameGUI keeps its selection
	//! as a UnitRef/BuildingRef and re-resolves these before drawing
	//! (GameGUI::syncSelectionView); they are only valid for that frame.
	Unit *selectedUnit = nullptr;
	Building *selectedBuilding = nullptr;
	MapRenderState render;            //!< This view's animation phases and render caches.
	//! PresentationFrame to draw, published by the simulation; null to extract one from the game.
	const PresentationFrame *scene = nullptr;
	const std::array<Utilities::BitArray,4>* displayedAreas = nullptr;
	//! The scene the last drawMap drew: the published one, else the view's own.
	const PresentationFrame &drawnScene() const { return scene ? *scene : render.ownScene; }
};
