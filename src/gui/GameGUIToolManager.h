// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2007 Bradley Arsenault
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#pragma once

#include <memory>
#include "Brush.h"
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
	
	///List of zone types
	enum ZoneType
	{
		Forbidden=0,
		Guard,
		Clearing,
	};

	///Activates the building tool with the given building or flag type
	void activateBuildingTool(const std::string& building);

	///Activates the building tool with the given zone type
	void activateZoneTool(ZoneType type);
	
	///Activates the zone tool with the last selected zone type
	void activateZoneTool();
	
	///Cancels a tool
	void deactivateTool();

	///Draws the tool on the map
	void drawTool(int mouseX, int mouseY, int localteam, int viewportX, int viewportY);
	
	///Returns the name of the current building
	std::string getBuildingName() const;

	///Returns the current type of zone
	ZoneType getZoneType() const;
	
	///Handles a mouse down
	void handleMouseDown(int mouseX, int mouseY, int localteam, int viewportX, int viewportY);
	
	///Handles a mouse up
	void handleMouseUp(int mouseX, int mouseY, int localteam, int viewportX, int viewportY);
	
	///Ends a pointer gesture without placing a building; keeps painted zones.
	void finishPointerGesture(int localteam);

	///Handles the dragging of the mouse
	void handleMouseDrag(int mouseX, int mouseY, int localteam, int viewportX, int viewportY);

	///Returns an order, or shared_ptr() if there are none
	std::shared_ptr<Order> getOrder();
private:
	///Handles placing a zone on the map
	void handleZonePlacement(int mouseX, int mouseY, int localteam, int viewportX, int viewportY);

	///Returns the local (display-only) map overlay for the given zone type
	Utilities::BitArray& displayedViewForZone(ZoneType type);

	///Flushes an order for the current brush accumulator
	void flushBrushOrders(int localteam);
	///Places a building at pos x,y
	void placeBuildingAt(int mapx, int mapy, int localteam);
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
	///Used to indicate the stength of hilight, because it blends during the draw
	float hilightStrength;
	///Queues up orderws for this manager
	std::queue<std::shared_ptr<Order> > orders;
};

