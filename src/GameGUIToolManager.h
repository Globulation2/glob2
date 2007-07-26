/*
  Copyright (C) 2007 Bradley Arsenault

  Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière
  for any question or comment contact us at <stephane at magnenat dot net> or <NuageBleu at gmail dot com>

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 2 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
*/

#ifndef GameGUIToolManager_h
#define GameGUIToolManager_h

#include "Game.h"
#include "boost/shared_ptr.hpp"
#include "Brush.h"
#include "GameGUIDefaultAssignManager.h"
#include <string>
#include <queue>

///This class is meant to manage the game gui tool, such as placing a building, flag or zone
class GameGUIToolManager
{
public:
	///Constructs a tool manager
	GameGUIToolManager(Game& game, BrushTool& brush, GameGUIDefaultAssignManager& defaultAssign);
	
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
		Clearing,
		Guard,
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
	
	///Handles the dragging of the mouse
	void handleMouseDrag(int mouseX, int mouseY, int localteam, int viewportX, int viewportY);

	///Returns an order, or shared_ptr() if there are none
	boost::shared_ptr<Order> getOrder();
private:
	///Handles placing a zone on the map
	void handleZonePlacement(int mouseX, int mouseY, int localteam, int viewportX, int viewportY);

	///Flushes an order for the current brush accumulator
	void flushBrushOrders(int localteam);

	Game& game;
	BrushTool& brush;
	GameGUIDefaultAssignManager& defaultAssign;
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
	std::queue<boost::shared_ptr<Order> > orders;
};

#endif
