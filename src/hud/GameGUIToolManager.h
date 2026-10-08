// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2007 Bradley Arsenault
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#pragma once

#include <memory>
#include "Brush.h"
#include "Types.h"
#include "ClientAreaPreview.h"
#include <optional>
#include <string>
#include <queue>

class Game;
class GameGUIDefaultAssignManager;
class GameGUIGhostBuildingManager;
class Order;

namespace Utilities
{
	class BitArray;
}

///This class is meant to manage the game gui tool, such as placing a building, flag or zone
struct PresentationFrame;

class GameGUIToolManager
{
public:
	///Constructs a tool manager
	GameGUIToolManager(Game& game, BrushTool& brush, GameGUIDefaultAssignManager& defaultAssign, GameGUIGhostBuildingManager& ghostManager);
	
	///List of tool modes
	enum ToolMode
	{
		NoTool,
		PlaceBuilding,
		PlaceZone,
	};
	
	///List of zone types. Order matters: it is the left-to-right order of the
	///zone strip's buttons, and indexes InGameTouchTheme::zonePreview.
	enum ZoneType
	{
		Forbidden=0,
		Guard,
		Clearing,
		Farm, ///< only in a game carrying the farm-areas experiment
	};

	///Whether this game offers the farm zone (the farm-areas experiment)
	bool farmAreasAvailable() const;
	///Number of zone types this game offers: three, or four with farm areas
	int zoneTypeCount() const { return farmAreasAvailable() ? 4 : 3; }

	///Activates the building tool with the given building or flag type
	void activateBuildingTool(const std::string& building);

	///Activates the building tool with the given zone type
	void activateZoneTool(ZoneType type);
	
	///Activates the zone tool with the last selected zone type
	void activateZoneTool();
	
	///Cancels a tool
	void deactivateTool();

	///Draws the tool on the map
	void drawTool(int mouseX, int mouseY, int localteam, int viewportX, int viewportY, int modifiers);
	//! Draw building previews from scene (the frame's extracted state).
	void setDrawnScene(const PresentationFrame* scene);
    bool canPaintFarmArea(int x,int y) const;
    void trackPaint(const std::shared_ptr<Order>& order) { preview.track(order); }
    void acknowledgePaint(Order& order,Uint64 revision) { preview.acknowledge(order,revision); }
    const std::array<Utilities::BitArray,4>& displayedAreas() const { return preview.shown; }
	
	///Returns the name of the current building
	std::string getBuildingName() const;

	///Returns the current type of zone
	ZoneType getZoneType() const;
	
	///Handles a mouse down
	void handleMouseDown(int mouseX, int mouseY, int localteam, int viewportX, int viewportY);
	
	///Handles a mouse up
	void handleMouseUp(int mouseX, int mouseY, int localteam, int viewportX, int viewportY, int modifiers);
    void cancelDrag(int localteam);
    bool confirmBuilding(int mouseX, int mouseY, int localteam, int viewportX, int viewportY);
	
	///Ends a pointer gesture without placing a building; keeps painted zones.
	void finishPointerGesture(int localteam);

	///Handles the dragging of the mouse
	void handleMouseDrag(int mouseX, int mouseY, int localteam, int viewportX, int viewportY);

	///Returns an order, or shared_ptr() if there are none
	std::shared_ptr<Order> getOrder();

	///Returns the local (display-only) map overlay for the given zone type
	Utilities::BitArray& displayedViewForZone(ZoneType type);

	///The order that paints (MODE_ADD) or erases (MODE_DEL) a zone type over
	///the box at (left, top), width x height, wherever mask is set
	static std::shared_ptr<Order> makeZoneOrder(ZoneType type, Uint8 team, Uint8 mode,
		Sint16 left, Sint16 top, Sint16 width, Sint16 height, const Utilities::BitArray& mask);
private:
	///Handles placing a zone on the map
	void handleZonePlacement(int mouseX, int mouseY, int localteam, int viewportX, int viewportY);

	///Flushes an order for the current brush accumulator
	void flushBrushOrders(int localteam);
	///Places a building at pos x,y
	bool placeBuildingAt(int mapx, int mapy, int localteam);
	///Draws a building at pos x,y
	void drawBuildingAt(int mapx, int mapy, int localteam, int viewportX, int viewportY);
	///Computes a line going from sx,sy to ex,ey of the current building
	///if mode is 1, it will draw the buildings, if mode is 2, it will place them
	void computeBuildingLine(int sx, int sy, int ex, int ey, int localteam, int viewportX, int viewportY, int mode);
	///Computes  a box going from sx,sy to ex,ey of the current building
	///if mode is 1, it will draw the buildings, if mode is 2, it will place them
	void computeBuildingBox(int sx, int sy, int ex, int ey, int localteam, int viewportX, int viewportY, int mode);


	///Map coordinates recorded on mouse-down, the anchor for drag operations
	///(building lines/boxes, zone brush alignment). Empty until the first click.
	struct FirstPlacement
	{
		int x;
		int y;
	};
	std::optional<FirstPlacement> firstPlacement;

	Game& game;
	//! The frame's PresentationFrame; the building preview reads placement room from it.
	const PresentationFrame* drawnScene = nullptr;
    ClientAreaPreview preview;
	BrushTool& brush;
	GameGUIDefaultAssignManager& defaultAssign;
	GameGUIGhostBuildingManager& ghostManager;
	BrushAccumulator brushAccumulator;
	///Tool mode
	ToolMode mode;
	///The name of the building/flag
	std::string building;
	///The type of zone when placing zones
	ZoneType zoneType;
	///Used to indicate the strength of highlight, because it blends during the draw
	float highlightStrength;
	///Queues up orders for this manager
	std::queue<std::shared_ptr<Order> > orders;
};

