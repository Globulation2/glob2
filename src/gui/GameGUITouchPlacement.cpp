// SPDX-License-Identifier: GPL-3.0-or-later
#include "GameGUITouch.h"
#include "InGameTouchTheme.h"
#include "GameGUI.h"
#include "GameGUIInternal.h"
#include "GlobalContainer.h"
#include "BuildingType.h"
#include "Building.h"
#include "Team.h"
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

// Pans while a held contact sits in the band along an exposed map edge. Speed is
// in points per second, so it is density- and zoom-aware. Returns whether the
// camera moved.
bool GameGUITouch::edgePan(ViewPoint point, std::uint64_t &lastUpdate)
{
	const double unit = globalContainer->gfx->logicalUnitsPerPoint();
	const auto bounds = world();
	const auto now = SDL_GetTicks64();
	const double delta = std::min<std::uint64_t>(100, now - lastUpdate) / 1000.0;
	lastUpdate = now;
	const double margin = InGameTouchTheme::edgePanMargin * unit;
	const double dx = point.x < bounds.x + margin              ? -1
					  : point.x > bounds.x + bounds.w - margin ? 1
															   : 0;
	const double dy = point.y < bounds.y + margin              ? -1
					  : point.y > bounds.y + bounds.h - margin ? 1
															   : 0;
	if ((!dx && !dy) || interfaceRegion(point) != 0 || delta <= 0)
		return false;
	gui.updateCamera();
	const int oldX = gui.viewportX, oldY = gui.viewportY;
	gui.camera.originX += dx * InGameTouchTheme::edgePanPixelsPerSecond * unit * delta / gui.camera.zoom;
	gui.camera.originY += dy * InGameTouchTheme::edgePanPixelsPerSecond * unit * delta / gui.camera.zoom;
	gui.camera.normalize();
	gui.viewportX = gui.camera.tileX();
	gui.viewportY = gui.camera.tileY();
	gui.viewportChanged(oldX, gui.viewportX, oldY, gui.viewportY);
	return true;
}

// Called by input and once per rendered frame so holding at an edge continues
// panning. It only updates camera/preview state; release owns the commit.
void GameGUITouch::advancePlacement()
{
	if (!placement && !placementHold)
		return;
	auto &session = placement ? placement : placementHold;
	const bool lifted = bool(placement);
	const auto point = session->pointerPosition;
	const double unit = globalContainer->gfx->logicalUnitsPerPoint();
	if (session->dragging)
	{
		if (gui.selectionMode != GameGUI::TOOL_SELECTION ||
			gui.toolManager.getBuildingName() != session->building ||
			gui.localTeamNo != session->team || activeDialog() ||
			globalContainer->isViewingGame())
		{
			session.reset();
			preview.reset();
			return;
		}
		edgePan(point, session->lastUpdate);
		if (interfaceRegion(point) == 0)
			updatePlacementPreview({point.x, point.y - (lifted ? InGameTouchTheme::fingerLift * unit : 0)});
	}
}

// The local team's flag a new contact should carry instead of panning: one on
// the touched tile, or on the phone HUD the nearest within the same 24-point
// reach that selects flags. As with selection, that reach never claims a unit
// or a building directly under the finger.
Building *GameGUITouch::grabbableFlag(ViewPoint point)
{
	if (globalContainer->isViewingGame() || gui.torusView.active() || gui.putMark ||
		!world().contains(point) || controls().contains(point) || interfaceRegion(point) != 0)
		return nullptr;
	gui.updateCamera();
	if (!gui.camera.contains(int(point.x), int(point.y)))
		return nullptr;
	const int mx = gui.mapMouseX(int(point.x)), my = gui.mapMouseY(int(point.y));
	if (auto *flag = gui.flagAt(mx, my, false))
		return flag;
	if (!usesHUD() || unitAt(point))
		return nullptr;
	int tileX, tileY;
	gui.game.map.displayToMapCaseAligned(mx, my, &tileX, &tileY, gui.viewportX, gui.viewportY);
	if (gui.game.map.getBuilding(tileX, tileY) != NOGBID)
		return nullptr;
	return gui.flagAt(mx, my, true);
}

Building *GameGUITouch::draggedFlag() const
{
	if (!flagDrag || flagDrag->team != gui.localTeamNo || globalContainer->isViewingGame())
		return nullptr;
	auto *team = gui.game.teams[Building::GIDtoTeam(Uint16(flagDrag->gid))];
	auto *flag = team ? team->myBuildings[Building::GIDtoID(Uint16(flagDrag->gid))] : nullptr;
	if (!flag || flag->gid != flagDrag->gid || flag->owner != gui.localTeam || !flag->type->isVirtual ||
		flag->buildingState != Building::ALIVE)
		return nullptr;
	return flag;
}

void GameGUITouch::beginFlagDrag(Building &flag, TouchPlacementSession::Pointer pointer, ViewPoint point)
{
	const auto &map = gui.game.map;
	const auto wrapped = [](double delta, double period)
	{ return MapCamera::wrap(delta + period / 2, period) - period / 2; };
	TouchFlagSession session;
	session.pointer = pointer;
	session.gid = flag.gid;
	session.team = gui.localTeamNo;
	session.start = session.position = point;
	session.originX = gui.displayedPosX(flag);
	session.originY = gui.displayedPosY(flag);
	const double fingerX = gui.mapMouseX(int(point.x)) + gui.viewportX * 32.;
	const double fingerY = gui.mapMouseY(int(point.y)) + gui.viewportY * 32.;
	session.offsetX = wrapped(session.originX * 32. + 16 - fingerX, map.getW() * 32.);
	session.offsetY = wrapped(session.originY * 32. + 16 - fingerY, map.getH() * 32.);
	session.lastUpdate = SDL_GetTicks64();
	flagDrag = session;
}

// Called on finger motion and once per rendered frame, so a flag held at a map
// edge keeps the map panning under it. While the finger is over the HUD the flag
// waits where it last was.
void GameGUITouch::advanceFlagDrag()
{
	if (!flagDrag || !flagDrag->dragging)
		return;
	auto *flag = draggedFlag();
	if (!flag)
	{
		flagDrag.reset();
		return;
	}
	const auto point = flagDrag->position;
	edgePan(point, flagDrag->lastUpdate);
	if (interfaceRegion(point) != 0 || !world().contains(point))
		return;
	gui.updateCamera();
	const int mx = int(std::floor(gui.mapMouseX(int(point.x)) + flagDrag->offsetX));
	const int my = int(std::floor(gui.mapMouseY(int(point.y)) + flagDrag->offsetY));
	int x, y;
	gui.game.map.cursorToBuildingPos(mx, my, flag->type->width, flag->type->height, &x, &y,
									 gui.viewportX, gui.viewportY);
	if (x != gui.displayedPosX(*flag) || y != gui.displayedPosY(*flag))
	{
		gui.queueFlagMove(*flag, x, y, false);
		flagDrag->moved = true;
	}
}

// Lands the flag where it is, or with `restore` puts it back where it was
// grabbed. A drag that moved the flag ends with a drop order, as a released mouse
// sends; one that never left the flag's tile sends nothing.
void GameGUITouch::releaseFlagDrag(bool restore)
{
	auto *flag = flagDrag && flagDrag->moved ? draggedFlag() : nullptr;
	if (flag)
		gui.queueFlagMove(*flag, restore ? flagDrag->originX : gui.displayedPosX(*flag),
						  restore ? flagDrag->originY : gui.displayedPosY(*flag), true);
	flagDrag.reset();
}
