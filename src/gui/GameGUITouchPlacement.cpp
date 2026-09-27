// SPDX-License-Identifier: GPL-3.0-or-later
#include "GameGUITouch.h"
#include "InGameTouchTheme.h"
#include "GameGUI.h"
#include "GameGUIInternal.h"
#include "GlobalContainer.h"
#include "BuildingType.h"
#include <Toolkit.h>
#include <StringTable.h>
#include <algorithm>
#include <cmath>
using namespace GAGCore;

void GameGUITouch::updatePlacementPreview(ViewPoint point)
{
	preview = ViewPoint{double(gui.mapMouseX(point.x) + gui.viewportX * 32),
						double(gui.mapMouseY(point.y) + gui.viewportY * 32)};
	previewType = gui.toolManager.getBuildingName();
}
bool GameGUITouch::commitPlacement()
{
	if (!preview || previewType != gui.toolManager.getBuildingName() ||
		globalContainer->isViewingGame())
		return false;
	const auto cursor = previewCursor();
	return gui.toolManager.confirmBuilding(int(cursor.x), int(cursor.y), gui.localTeamNo,
										   gui.viewportX, gui.viewportY);
}
bool GameGUITouch::processPalettePointer(const SDL_Event &event, ViewPoint point)
{
	const TouchPlacementSession::Pointer pointer{event.tfinger.touchId, event.tfinger.fingerId};
	if (!placement)
	{
		if (event.type != SDL_FINGERDOWN || !fingers.empty() || activeDialog())
			return false;
		const auto item = paletteItemAt(point);
		if (!item || !item->enabled || item->name.starts_with("zone:"))
			return false;
		placement = TouchPlacementSession{pointer,        point, item->name,       false,
										  layout().panel, point, SDL_GetTicks64(), gui.localTeamNo};
		return true;
	}
	if (pointer != placement->pointer)
	{
		// Only a new contact can interrupt the owner. Stray motion/release
		// events must not create a contact that can never be released.
		if (event.type != SDL_FINGERDOWN)
			return true;
		// Quarantine both releases: neither may become a map tap after cancel.
		fingers = {placement->pointer, pointer};
		placement.reset();
		preview.reset();
		gui.clearSelection();
		panelOpen = true;
		ignoreTouchSequence = true;
		return true;
	}
	const double unit = globalContainer->gfx->logicalUnitsPerPoint();
	if (!placement->dragging &&
		std::hypot(point.x - placement->origin.x, point.y - placement->origin.y) >=
			InGameTouchTheme::dragThreshold * unit)
	{
		placement->dragging = true;
		gui.setSelection(GameGUI::TOOL_SELECTION, const_cast<char *>(placement->building.c_str()));
		panelOpen = false;
	}
	placement->pointerPosition = point;
	advancePlacement();
	if (!placement)
		return true;
	if (event.type == SDL_FINGERUP)
	{
		if (placement->dragging)
		{
			if (!placement->sourceBounds.contains(point) && interfaceRegion(point) == 0 &&
				interfaceRegion({point.x, point.y - InGameTouchTheme::fingerLift * unit}) == 0)
				commitPlacement();
			gui.clearSelection();
			preview.reset();
			panelOpen = true;
		}
		else if (const auto item = paletteItemAt(point); item && item->name == placement->building)
		{
			gui.setSelection(GameGUI::TOOL_SELECTION,
							 const_cast<char *>(placement->building.c_str()));
			panelOpen = false;
		}
		placement.reset();
	}
	return true;
}

// Called by input and once per rendered frame so holding at an edge continues
// panning. It only updates camera/preview state; release owns the commit.
void GameGUITouch::advancePlacement()
{
	if (!placement)
		return;
	const auto point = placement->pointerPosition;
	const double unit = globalContainer->gfx->logicalUnitsPerPoint();
	if (placement->dragging)
	{
		if (gui.selectionMode != GameGUI::TOOL_SELECTION ||
			gui.toolManager.getBuildingName() != placement->building ||
			gui.localTeamNo != placement->team || activeDialog() ||
			globalContainer->isViewingGame())
		{
			placement.reset();
			preview.reset();
			return;
		}
		const auto bounds = world();
		const auto now = SDL_GetTicks64();
		const double delta = std::min<std::uint64_t>(100, now - placement->lastUpdate) / 1000.0;
		placement->lastUpdate = now;
		const double margin = InGameTouchTheme::edgePanMargin * unit;
		const double dx = point.x < bounds.x + margin              ? -1
						  : point.x > bounds.x + bounds.w - margin ? 1
																   : 0;
		const double dy = point.y < bounds.y + margin              ? -1
						  : point.y > bounds.y + bounds.h - margin ? 1
																   : 0;
		if ((dx || dy) && interfaceRegion(point) == 0)
		{
			gui.updateCamera();
			const int oldX = gui.viewportX, oldY = gui.viewportY;
			gui.camera.originX +=
				dx * InGameTouchTheme::edgePanPixelsPerSecond * delta / gui.camera.zoom;
			gui.camera.originY +=
				dy * InGameTouchTheme::edgePanPixelsPerSecond * delta / gui.camera.zoom;
			gui.camera.normalize();
			gui.viewportX = gui.camera.tileX();
			gui.viewportY = gui.camera.tileY();
			gui.viewportChanged(oldX, gui.viewportX, oldY, gui.viewportY);
		}
		updatePlacementPreview({point.x, point.y - InGameTouchTheme::fingerLift * unit});
	}
}
